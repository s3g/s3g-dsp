#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/audio-ports-config.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-name.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include "s3g_breakbeat_slicer.h"

#include <dlfcn.h>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

constexpr const char* kPluginId =
    "org.s3g.s3g-dsp.breakbeat-slicer";
constexpr const char* kStereoPluginId =
    "org.s3g.s3g-dsp.breakbeat-slicer-stereo";
constexpr uint32_t kStateMagic = 0x53423353u;
constexpr uint32_t kLegacyStateVersion = 9u;
constexpr uint32_t kStateVersion = 10u;
constexpr std::size_t kPathBytes = 1024u;

struct FixtureSavedSlot {
    std::array<char, kPathBytes> path {};
    std::array<s3g::breakbeat::Slice,
        s3g::breakbeat::kMaximumSlicesPerSlot> slices {};
    s3g::breakbeat::Envelope envelope {};
    uint32_t sliceCount = 0u;
    uint32_t mappedSliceCount = 0u;
    float mixerGain = 1.0f;
    float mixerPan = 0.0f;
    float mixerLowEqDb = 0.0f;
    float mixerMidEqDb = 0.0f;
    float mixerHighEqDb = 0.0f;
    float mixerMidFrequencyHz = 900.0f;
    float mixerAuxSend = 0.0f;
    std::array<s3g::breakbeat::InsertSettings,
        s3g::breakbeat::kInsertSlotsPerStrip> inserts {};
    uint8_t rootNote = 36u;
    uint8_t mappedRootNote = 36u;
    uint8_t midiChannel = 0u;
    uint8_t muted = 0u;
    uint8_t solo = 0u;
    uint8_t embedded = 0u;
    uint8_t channelCount = 0u;
    uint32_t frameCount = 0u;
    double sampleRate = 0.0;
};

struct FixtureSavedState {
    uint32_t magic = kStateMagic;
    uint32_t version = kStateVersion;
    uint32_t selectedSlot = 0u;
    uint32_t interpolation = 1u;
    double outputGainDb = 0.0;
    double velocitySensitivity = 1.0;
    clap_id outputConfigId = 2002u;
    float auxPress = 0.42f;
    float auxSnap = 0.18f;
    float auxRecovery = 0.34f;
    float auxSaturation = 0.20f;
    float auxBite = 0.08f;
    float auxClip = 0.0f;
    float auxTilt = 0.0f;
    float auxReturnDb = -9.0f;
    uint8_t embedSamples = 1u;
    uint8_t auxEnabled = 0u;
    uint8_t auxLinkMode = 0u;
    uint8_t auxFieldSafe = 0u;
    uint32_t transientPreRollMicroseconds = 0u;
    std::array<FixtureSavedSlot,
        s3g::breakbeat::kMaximumSampleSlots> slots {};
};

struct HostContext {
    clap_host_t host {};
    clap_host_note_name_t noteNames {};
    clap_host_audio_ports_t audioPorts {};
    uint32_t processRequests = 0u;
    uint32_t noteNameChanges = 0u;
    uint32_t audioPortRescans = 0u;
};

void hostRequest(const clap_host_t* host)
{
    ++static_cast<HostContext*>(host->host_data)->processRequests;
}

void hostNoteNamesChanged(const clap_host_t* host)
{
    ++static_cast<HostContext*>(host->host_data)->noteNameChanges;
}

bool hostAudioPortRescanSupported(const clap_host_t*, uint32_t)
{
    return true;
}

void hostAudioPortsRescan(const clap_host_t* host, uint32_t)
{
    ++static_cast<HostContext*>(host->host_data)->audioPortRescans;
}

const void* hostGetExtension(const clap_host_t* host, const char* id)
{
    auto* context = static_cast<HostContext*>(host->host_data);
    if (id && std::strcmp(id, CLAP_EXT_NOTE_NAME) == 0)
        return &context->noteNames;
    if (id && std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0)
        return &context->audioPorts;
    return nullptr;
}

struct ParameterEvents {
    clap_event_param_value_t event {};
    clap_input_events_t input {};

    ParameterEvents(clap_id id, double value)
    {
        event.header.size = sizeof(event);
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.param_id = id;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
        input.ctx = this;
        input.size = [](const clap_input_events_t*) { return 1u; };
        input.get = [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            const auto* self = static_cast<const ParameterEvents*>(list->ctx);
            return index == 0u ? &self->event.header : nullptr;
        };
    }
};

struct NoteEvents {
    clap_event_note_t event {};
    clap_input_events_t input {};

    NoteEvents()
    {
        event.header.size = sizeof(event);
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_NOTE_ON;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.note_id = 1;
        event.port_index = 0;
        event.channel = 0;
        event.key = 60;
        event.velocity = 0.75;
        input.ctx = this;
        input.size = [](const clap_input_events_t*) { return 1u; };
        input.get = [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            const auto* self = static_cast<const NoteEvents*>(list->ctx);
            return index == 0u ? &self->event.header : nullptr;
        };
    }
};

struct StateBuffer {
    clap_ostream_t output {};
    clap_istream_t input {};
    std::vector<uint8_t> bytes;
    std::size_t cursor = 0u;

    StateBuffer()
    {
        output.ctx = this;
        output.write = [](const clap_ostream_t* stream, const void* source,
                           uint64_t count) -> int64_t {
            auto* self = static_cast<StateBuffer*>(stream->ctx);
            if (!self || !source || count > 16u * 1024u * 1024u) return -1;
            const auto* first = static_cast<const uint8_t*>(source);
            self->bytes.insert(self->bytes.end(), first, first + count);
            return static_cast<int64_t>(count);
        };
        input.ctx = this;
        input.read = [](const clap_istream_t* stream, void* destination,
                         uint64_t count) -> int64_t {
            auto* self = static_cast<StateBuffer*>(stream->ctx);
            if (!self || !destination || self->cursor > self->bytes.size())
                return -1;
            const std::size_t available = self->bytes.size() - self->cursor;
            const std::size_t amount = std::min<std::size_t>(
                static_cast<std::size_t>(count), available);
            if (amount == 0u) return 0;
            std::memcpy(destination, self->bytes.data() + self->cursor,
                amount);
            self->cursor += amount;
            return static_cast<int64_t>(amount);
        };
    }
};

std::filesystem::path resolveBinary(const std::filesystem::path& supplied)
{
    if (std::filesystem::is_regular_file(supplied)) return supplied;
#if defined(__APPLE__)
    const auto macOS = supplied / "Contents" / "MacOS";
    if (std::filesystem::is_directory(macOS)) {
        for (const auto& entry : std::filesystem::directory_iterator(macOS))
            if (entry.is_regular_file()) return entry.path();
    }
#endif
    return {};
}

bool expect(bool condition, const char* message)
{
    if (condition) return true;
    std::fprintf(stderr, "breakbeat slicer CLAP: %s\n", message);
    return false;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::fprintf(stderr,
            "usage: s3g_breakbeat_slicer_clap_smoke <bundle-or-binary>\n");
        return 2;
    }
    bool ok = true;
    const auto binary = resolveBinary(argv[1]);
    ok &= expect(!binary.empty(), "could not resolve bundle executable");
    void* library = !binary.empty()
        ? dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW) : nullptr;
    ok &= expect(library != nullptr, library ? "" : dlerror());
    const auto* entry = library ? static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry")) : nullptr;
    ok &= expect(entry && entry->init(binary.c_str()),
        "entry initialization failed");

    HostContext context;
    context.noteNames.changed = hostNoteNamesChanged;
    context.audioPorts.is_rescan_flag_supported
        = hostAudioPortRescanSupported;
    context.audioPorts.rescan = hostAudioPortsRescan;
    context.host.clap_version = CLAP_VERSION_INIT;
    context.host.host_data = &context;
    context.host.name = "s3g breakbeat slicer smoke";
    context.host.vendor = "s3g";
    context.host.url = "https://github.com/s3g/s3g-dsp";
    context.host.version = "1";
    context.host.get_extension = hostGetExtension;
    context.host.request_restart = hostRequest;
    context.host.request_process = hostRequest;
    context.host.request_callback = hostRequest;

    const auto* factory = entry ? static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    ok &= expect(factory && factory->get_plugin_count(factory) == 2u
            && factory->get_plugin_descriptor(factory, 0u)
            && factory->get_plugin_descriptor(factory, 1u),
        "expected fixed stereo and fixed 16-channel descriptors");
    const clap_plugin_t* plugin = factory ? factory->create_plugin(factory,
        &context.host, kPluginId) : nullptr;
    ok &= expect(plugin && plugin->init(plugin), "plugin creation failed");

    const auto* audioPorts = plugin
        ? static_cast<const clap_plugin_audio_ports_t*>(
            plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS)) : nullptr;
    const auto* notePorts = plugin
        ? static_cast<const clap_plugin_note_ports_t*>(
            plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS)) : nullptr;
    const auto* params = plugin ? static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS)) : nullptr;
    const auto* state = plugin ? static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE)) : nullptr;
    const auto* noteNames = plugin
        ? static_cast<const clap_plugin_note_name_t*>(
            plugin->get_extension(plugin, CLAP_EXT_NOTE_NAME)) : nullptr;
    const auto* portConfigs = plugin
        ? static_cast<const clap_plugin_audio_ports_config_t*>(
            plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS_CONFIG))
        : nullptr;
    clap_audio_port_info_t audioInfo {};
    ok &= expect(audioPorts && audioPorts->count(plugin, true) == 0u
        && audioPorts->count(plugin, false) == 1u
        && audioPorts->get(plugin, 0u, false, &audioInfo)
        && audioInfo.channel_count == 16u,
        "expected one fixed 16-channel audio output");
    clap_note_port_info_t noteInfo {};
    ok &= expect(notePorts && notePorts->count(plugin, true) == 1u
        && notePorts->count(plugin, false) == 0u
        && notePorts->get(plugin, 0u, true, &noteInfo)
        && (noteInfo.supported_dialects & CLAP_NOTE_DIALECT_MIDI) != 0u,
        "expected one MIDI/CLAP note input");
    ok &= expect(params && params->count(plugin) == 2u,
        "expected output and velocity host parameters");
    ok &= expect(state && noteNames && noteNames->count(plugin) == 0u,
        "state or initial note-name surface is invalid");
    ok &= expect(portConfigs && portConfigs->count(plugin) == 1u,
        "expected one immutable 16-channel output configuration");
    if (portConfigs && audioPorts) {
        clap_audio_ports_config_t config {};
        clap_audio_port_info_t fixedPort {};
        ok &= expect(portConfigs->get(plugin, 0u, &config)
                && config.main_output_channel_count == 16u
                && portConfigs->select(plugin, config.id)
                && !portConfigs->select(plugin, 2002u)
                && audioPorts->get(plugin, 0u, false, &fixedPort)
                && fixedPort.channel_count == 16u,
            "fixed 16-channel output configuration changed or narrowed");
    }

    if (params) {
        ParameterEvents change(1u, -12.0);
        params->flush(plugin, &change.input, nullptr);
        double value = 0.0;
        ok &= expect(params->get_value(plugin, 1u, &value)
            && std::fabs(value + 12.0) < 1.0e-9,
            "parameter flush did not update output gain");
    }

    FixtureSavedState legacyFixture;
    legacyFixture.version = kLegacyStateVersion;
    legacyFixture.embedSamples = 0u;
    // Version 9 used these bytes only as alignment padding. A loader must not
    // mistake any prior padding contents for an authored pre-roll value.
    legacyFixture.transientPreRollMicroseconds = 20000u;
    StateBuffer legacyState;
    const auto* legacyBytes = reinterpret_cast<const uint8_t*>(
        &legacyFixture);
    legacyState.bytes.insert(legacyState.bytes.end(), legacyBytes,
        legacyBytes + sizeof(legacyFixture));
    StateBuffer migratedLegacyState;
    bool legacyMigration = state
        && state->load(plugin, &legacyState.input)
        && state->save(plugin, &migratedLegacyState.output)
        && migratedLegacyState.bytes.size() >= sizeof(FixtureSavedState);
    if (legacyMigration) {
        FixtureSavedState migrated {};
        std::memcpy(&migrated, migratedLegacyState.bytes.data(),
            sizeof(migrated));
        legacyMigration = migrated.version == kStateVersion
            && migrated.transientPreRollMicroseconds == 0u;
    }
    ok &= expect(legacyMigration,
        "state v9 did not migrate with transient pre-roll disabled");

    FixtureSavedState fixture;
    fixture.auxPress = 0.52f;
    fixture.auxSnap = 0.43f;
    fixture.auxRecovery = 0.16f;
    fixture.auxSaturation = 0.74f;
    fixture.auxBite = 0.28f;
    fixture.auxClip = 0.36f;
    fixture.auxTilt = -0.18f;
    fixture.auxReturnDb = -7.5f;
    fixture.auxLinkMode = static_cast<uint8_t>(s3g::BreakBusLinkMode::Pair);
    fixture.auxFieldSafe = 1u;
    fixture.transientPreRollMicroseconds = 1250u;
    auto& fixtureSlot = fixture.slots[0u];
    std::snprintf(fixtureSlot.path.data(), fixtureSlot.path.size(), "%s",
        "missing-original-file.wav");
    fixtureSlot.sliceCount = 1u;
    fixtureSlot.mappedSliceCount = 1u;
    fixtureSlot.mixerGain = 0.5f;
    fixtureSlot.mixerAuxSend = 0.62f;
    fixtureSlot.inserts[0u] = s3g::breakbeat::defaultInsertSettings(
        s3g::breakbeat::InsertType::Degrade);
    fixtureSlot.inserts[0u].values[2u] = 0.37f;
    fixtureSlot.inserts[0u].bypassed = true;
    fixtureSlot.inserts[1u] = s3g::breakbeat::defaultInsertSettings(
        s3g::breakbeat::InsertType::TimeMangler);
    fixtureSlot.inserts[1u].variant = 2u;
    fixtureSlot.inserts[1u].bypassed = true;
    fixtureSlot.rootNote = 60u;
    fixtureSlot.mappedRootNote = 60u;
    fixtureSlot.midiChannel = 1u;
    fixtureSlot.embedded = 1u;
    fixtureSlot.channelCount = 16u;
    fixtureSlot.frameCount = 256u;
    fixtureSlot.sampleRate = 48000.0;
    fixtureSlot.slices[0u].startFrame = 0u;
    fixtureSlot.slices[0u].endFrame = fixtureSlot.frameCount;
    fixtureSlot.envelope.attackProportion = 0.0f;
    fixtureSlot.envelope.decayProportion = 0.0f;
    fixtureSlot.envelope.sustain = 1.0f;
    fixtureSlot.envelope.releaseProportion = 0.2f;
    StateBuffer embeddedFixture;
    const auto* fixtureBytes = reinterpret_cast<const uint8_t*>(&fixture);
    embeddedFixture.bytes.insert(embeddedFixture.bytes.end(), fixtureBytes,
        fixtureBytes + sizeof(fixture));
    for (uint32_t channel = 0u; channel < 16u; ++channel) {
        const std::array<float, 256u> samples = [&] {
            std::array<float, 256u> values {};
            values.fill(static_cast<float>(channel + 1u) * 0.1f);
            return values;
        }();
        const auto* bytes = reinterpret_cast<const uint8_t*>(samples.data());
        embeddedFixture.bytes.insert(embeddedFixture.bytes.end(), bytes,
            bytes + sizeof(samples));
    }
    ok &= expect(state && state->load(plugin, &embeddedFixture.input)
        && noteNames->count(plugin) == 1u,
        "embedded 16-channel fixture did not load without its source file");

    StateBuffer saved;
    bool stateRoundTrip = state && state->save(plugin, &saved.output)
        && saved.bytes.size() >= sizeof(FixtureSavedState)
            + 16u * 256u * sizeof(float);
    if (stateRoundTrip) {
        FixtureSavedState savedFixture;
        std::memcpy(&savedFixture, saved.bytes.data(), sizeof(savedFixture));
        stateRoundTrip = std::fabs(savedFixture.auxPress - 0.52f) < 1.0e-6f
            && std::fabs(savedFixture.auxTilt + 0.18f) < 1.0e-6f
            && savedFixture.auxLinkMode
                == static_cast<uint8_t>(s3g::BreakBusLinkMode::Pair)
            && savedFixture.auxFieldSafe == 1u
            && savedFixture.transientPreRollMicroseconds == 1250u
            && std::fabs(savedFixture.slots[0u].mixerAuxSend - 0.62f)
                < 1.0e-6f
            && savedFixture.slots[0u].inserts[0u].type
                == s3g::breakbeat::InsertType::Degrade
            && std::fabs(savedFixture.slots[0u].inserts[0u].values[2u]
                    - 0.37f) < 1.0e-6f
            && savedFixture.slots[0u].inserts[1u].type
                == s3g::breakbeat::InsertType::TimeMangler
            && savedFixture.slots[0u].inserts[1u].variant == 2u
            && std::fabs(savedFixture.slots[0u].envelope.releaseProportion
                    - 0.2f) < 1.0e-6f;
    }
    if (stateRoundTrip && portConfigs) {
        clap_audio_ports_config_t fixed {};
        stateRoundTrip = portConfigs->get(plugin, 0u, &fixed)
            && fixed.main_output_channel_count == 16u
            && portConfigs->select(plugin, fixed.id);
    }
    stateRoundTrip = stateRoundTrip && state->load(plugin, &saved.input);
    clap_audio_port_info_t restoredPort {};
    stateRoundTrip = stateRoundTrip && audioPorts
        && audioPorts->get(plugin, 0u, false, &restoredPort)
        && restoredPort.channel_count == 16u
        && context.audioPortRescans == 0u;
    ok &= expect(stateRoundTrip,
        "embedded audio did not force and retain 16-channel pass-through");

#if defined(__APPLE__)
    const auto* gui = plugin ? static_cast<const clap_plugin_gui_t*>(
        plugin->get_extension(plugin, CLAP_EXT_GUI)) : nullptr;
    ok &= expect(gui
        && gui->is_api_supported(plugin, CLAP_WINDOW_API_COCOA, false)
        && gui->can_resize(plugin), "resizable Cocoa editor is missing");
    if (gui) {
        @autoreleasepool {
            (void)[NSApplication sharedApplication];
            uint32_t width = 0u;
            uint32_t height = 0u;
            const bool created = gui->create(plugin,
                CLAP_WINDOW_API_COCOA, false);
            ok &= expect(created && gui->get_size(plugin, &width, &height)
                && width >= 620u && height >= 420u,
                "editor could not be constructed");
            if (created) {
                NSView* parent = [[NSView alloc] initWithFrame:NSMakeRect(
                    0.0, 0.0, width, height)];
                clap_window_t window {};
                window.api = CLAP_WINDOW_API_COCOA;
                window.cocoa = (__bridge clap_nsview)parent;
                uint32_t resizedWidth = 900u;
                uint32_t resizedHeight = 600u;
                ok &= expect(gui->set_parent(plugin, &window)
                    && gui->adjust_size(plugin, &resizedWidth,
                        &resizedHeight)
                    && gui->set_size(plugin, resizedWidth, resizedHeight)
                    && gui->show(plugin) && gui->hide(plugin),
                    "editor attach/resize/show lifecycle failed");
                gui->destroy(plugin);
            }
        }
    }
#endif

    ok &= expect(plugin && plugin->activate(plugin, 48000.0, 16u, 128u)
        && plugin->start_processing(plugin), "activation failed");
    std::array<std::array<float, 128u>, 16u> rendered {};
    std::array<float*, 16u> channels {};
    for (std::size_t channel = 0u; channel < channels.size(); ++channel)
        channels[channel] = rendered[channel].data();
    clap_audio_buffer_t output {};
    output.data32 = channels.data();
    output.channel_count = static_cast<uint32_t>(channels.size());
    NoteEvents noteOn;
    clap_event_transport_t transport {};
    transport.header.size = sizeof(transport);
    transport.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    transport.header.type = CLAP_EVENT_TRANSPORT;
    transport.flags = CLAP_TRANSPORT_HAS_TEMPO
        | CLAP_TRANSPORT_HAS_BEATS_TIMELINE
        | CLAP_TRANSPORT_IS_PLAYING;
    transport.tempo = 137.0;
    transport.song_pos_beats = static_cast<clap_beattime>(
        8.0 * static_cast<double>(CLAP_BEATTIME_FACTOR));
    clap_process_t process {};
    process.frames_count = static_cast<uint32_t>(rendered[0u].size());
    process.transport = &transport;
    process.in_events = &noteOn.input;
    process.audio_outputs = &output;
    process.audio_outputs_count = 1u;
    ok &= expect(plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE,
        "empty-bank process call failed");
    bool passThroughLocked = true;
    for (uint32_t channel = 0u; channel < rendered.size(); ++channel) {
        passThroughLocked = passThroughLocked
            && std::fabs(rendered[channel][0u]
                - static_cast<float>(channel + 1u) * 0.0375f) < 1.0e-6f;
    }
    ok &= expect(passThroughLocked,
        "embedded 16-channel audio was decoded, folded, or de-synchronized");

    if (plugin) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }
    const clap_plugin_t* stereoPlugin = factory ? factory->create_plugin(
        factory, &context.host, kStereoPluginId) : nullptr;
    ok &= expect(stereoPlugin && stereoPlugin->init(stereoPlugin),
        "stereo plug-in creation failed");
    const auto* stereoPorts = stereoPlugin
        ? static_cast<const clap_plugin_audio_ports_t*>(
            stereoPlugin->get_extension(stereoPlugin, CLAP_EXT_AUDIO_PORTS))
        : nullptr;
    clap_audio_port_info_t stereoInfo {};
    ok &= expect(stereoPorts
            && stereoPorts->get(stereoPlugin, 0u, false, &stereoInfo)
            && stereoInfo.channel_count == 2u,
        "stereo variant did not expose one fixed stereo output");
    const auto* stereoState = stereoPlugin
        ? static_cast<const clap_plugin_state_t*>(
            stereoPlugin->get_extension(stereoPlugin, CLAP_EXT_STATE))
        : nullptr;
    saved.cursor = 0u;
    ok &= expect(stereoState && !stereoState->load(stereoPlugin,
            &saved.input),
        "stereo variant accepted a 16-channel sample bank");
    if (stereoPlugin) stereoPlugin->destroy(stereoPlugin);
    if (entry) entry->deinit();
    if (library) dlclose(library);
    if (!ok) return 1;
    std::puts("s3g breakbeat slicer CLAP smoke: ok");
    return 0;
}
