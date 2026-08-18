#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/note-name.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include <dlfcn.h>

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

constexpr uint32_t kStateMagic = 0x50533353u;
constexpr uint32_t kLegacyStateVersion = 1u;
constexpr uint32_t kStateVersion = 3u;
constexpr std::size_t kLegacyParamCount = 15u;
constexpr std::size_t kParamCount = 20u;
constexpr std::size_t kPathBytes = 1024u;

struct LegacyFixtureState {
    uint32_t magic = kStateMagic;
    uint32_t version = kLegacyStateVersion;
    clap_id outputConfigId = 3002u;
    uint32_t parameterCount = static_cast<uint32_t>(kLegacyParamCount);
    std::array<double, kLegacyParamCount> parameters {{
        0.0, 0.0, 1.0, 0.0, 1.0,
        0.0, 0.0, 60.0,
        0.0, 0.0, 1.0, 50.0,
        0.0, 0.0, 1.0,
    }};
    std::array<char, kPathBytes> path {};
    uint8_t embedded = 1u;
    uint8_t channelCount = 2u;
    uint8_t reserved0 = 0u;
    uint8_t reserved1 = 0u;
    uint32_t frameCount = 64u;
    double sampleRate = 48000.0;
};

struct CurrentFixtureState {
    uint32_t magic = kStateMagic;
    uint32_t version = kStateVersion;
    clap_id outputConfigId = 3002u;
    uint32_t parameterCount = static_cast<uint32_t>(kParamCount);
    std::array<double, kParamCount> parameters {};
    std::array<char, kPathBytes> path {};
    uint8_t embedded = 0u;
    uint8_t channelCount = 0u;
    uint8_t reserved0 = 0u;
    uint8_t reserved1 = 0u;
    uint32_t frameCount = 0u;
    double sampleRate = 0.0;
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
            if (!self || !source) return -1;
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

struct NoteEvents {
    clap_event_note_t event {};
    clap_input_events_t input {};

    NoteEvents()
    {
        event.header.size = sizeof(event);
        event.header.time = 1u;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_NOTE_ON;
        event.note_id = 1;
        event.port_index = 0;
        event.channel = 0;
        event.key = 60;
        event.velocity = 1.0;
        input.ctx = this;
        input.size = [](const clap_input_events_t*) { return 1u; };
        input.get = [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            const auto* self = static_cast<const NoteEvents*>(list->ctx);
            return index == 0u ? &self->event.header : nullptr;
        };
    }
};

struct HostContext {
    clap_host_t host {};
};

const void* hostGetExtension(const clap_host_t*, const char*)
{
    return nullptr;
}

void hostRequest(const clap_host_t*) {}

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
    std::fprintf(stderr, "sample player CLAP: %s\n", message);
    return false;
}

void fillEmbeddedFixture(StateBuffer& state, uint32_t channels,
    clap_id configId)
{
    LegacyFixtureState fixture;
    fixture.outputConfigId = configId;
    fixture.channelCount = static_cast<uint8_t>(channels);
    const auto* header = reinterpret_cast<const uint8_t*>(&fixture);
    state.bytes.insert(state.bytes.end(), header, header + sizeof(fixture));
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        std::array<float, 64u> samples {};
        samples.fill(static_cast<float>(channel + 1u) * 0.1f);
        const auto* bytes = reinterpret_cast<const uint8_t*>(samples.data());
        state.bytes.insert(state.bytes.end(), bytes, bytes + sizeof(samples));
    }
}

bool exerciseDescriptor(const clap_plugin_factory_t* factory,
    const clap_host_t* host, const char* id, const char* expectedName,
    uint32_t channels, clap_id configId)
{
    bool ok = true;
    const clap_plugin_t* plugin = factory->create_plugin(factory, host, id);
    ok &= expect(plugin && plugin->init(plugin), "creation failed");
    if (!plugin) return false;
    ok &= expect(std::strcmp(plugin->desc->name, expectedName) == 0,
        "descriptor name mismatch");
    const auto* ports = static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    const auto* notePorts = static_cast<const clap_plugin_note_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
    const auto* noteNames = static_cast<const clap_plugin_note_name_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_NAME));
    const auto* params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    const auto* state = static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    clap_audio_port_info_t portInfo {};
    ok &= expect(ports && ports->get(plugin, 0u, false, &portInfo)
            && portInfo.channel_count == channels,
        "fixed output width mismatch");
    ok &= expect(notePorts && notePorts->count(plugin, true) == 1u,
        "note input missing");
    ok &= expect(params && params->count(plugin) == kParamCount,
        "parameter surface mismatch");
    clap_param_info_t attackInfo {};
    clap_param_info_t decayInfo {};
    clap_param_info_t releaseInfo {};
    clap_param_info_t playModeInfo {};
    clap_param_info_t crossfadeInfo {};
    clap_param_info_t filterTypeInfo {};
    clap_param_info_t cutoffInfo {};
    char envelopeText[32] {};
    char modeText[32] {};
    char filterText[32] {};
    char cutoffText[32] {};
    double envelopeValue = -1.0;
    double modeValue = -1.0;
    double filterValue = -1.0;
    double cutoffValue = -1.0;
    ok &= expect(params && params->get_info(plugin, 0u, &playModeInfo)
            && params->get_info(plugin, 8u, &attackInfo)
            && params->get_info(plugin, 9u, &decayInfo)
            && params->get_info(plugin, 11u, &releaseInfo)
            && params->get_info(plugin, 15u, &crossfadeInfo)
            && params->get_info(plugin, 16u, &filterTypeInfo)
            && params->get_info(plugin, 17u, &cutoffInfo)
            && playModeInfo.id == 1u && playModeInfo.max_value == 5.0
            && attackInfo.id == 9u && decayInfo.id == 10u
            && releaseInfo.id == 12u
            && attackInfo.max_value == 1.0
            && decayInfo.max_value == 1.0
            && releaseInfo.max_value == 1.0
            && params->value_to_text(plugin, attackInfo.id, 0.25,
                envelopeText, sizeof(envelopeText))
            && std::strcmp(envelopeText, "25.0 %") == 0
            && params->text_to_value(plugin, releaseInfo.id, "25 %",
                &envelopeValue)
            && std::fabs(envelopeValue - 0.25) < 1.0e-9
            && crossfadeInfo.id == 16u
            && crossfadeInfo.max_value == 0.5
            && std::fabs(crossfadeInfo.default_value - 0.02) < 1.0e-9
            && filterTypeInfo.id == 17u
            && filterTypeInfo.max_value == 4.0
            && cutoffInfo.id == 18u && cutoffInfo.min_value == 20.0
            && cutoffInfo.max_value == 20000.0
            && params->value_to_text(plugin, playModeInfo.id, 4.0,
                modeText, sizeof(modeText))
            && std::strcmp(modeText, "Forward Ping-Pong") == 0
            && params->text_to_value(plugin, playModeInfo.id,
                "Reverse Ping-Pong", &modeValue)
            && std::fabs(modeValue - 5.0) < 1.0e-9
            && params->value_to_text(plugin, filterTypeInfo.id, 2.0,
                filterText, sizeof(filterText))
            && std::strcmp(filterText, "Band Pass") == 0
            && params->text_to_value(plugin, filterTypeInfo.id,
                "High Pass", &filterValue)
            && std::fabs(filterValue - 3.0) < 1.0e-9
            && params->value_to_text(plugin, cutoffInfo.id, 3200.0,
                cutoffText, sizeof(cutoffText))
            && std::strcmp(cutoffText, "3.20 kHz") == 0
            && params->text_to_value(plugin, cutoffInfo.id, cutoffText,
                &cutoffValue)
            && std::fabs(cutoffValue - 3200.0) < 1.0e-9,
        "expanded loop, ADSR, or filter parameter contract is invalid");

    StateBuffer fixture;
    fillEmbeddedFixture(fixture, channels, configId);
    ok &= expect(state && state->load(plugin, &fixture.input)
            && noteNames && noteNames->count(plugin) == 1u,
        "embedded sample state failed to load");
    StateBuffer roundTrip;
    ok &= expect(state && state->save(plugin, &roundTrip.output)
            && roundTrip.bytes.size() == sizeof(CurrentFixtureState)
                + static_cast<std::size_t>(channels) * 64u * sizeof(float),
        "embedded sample state failed to round-trip");
    CurrentFixtureState migrated {};
    if (roundTrip.bytes.size() >= sizeof(migrated))
        std::memcpy(&migrated, roundTrip.bytes.data(), sizeof(migrated));
    ok &= expect(migrated.magic == kStateMagic
            && migrated.version == kStateVersion
            && migrated.parameterCount == kParamCount
            && migrated.parameters[8u] >= 0.0
            && migrated.parameters[9u] >= 0.0
            && migrated.parameters[11u] >= 0.0
            && migrated.parameters[8u] + migrated.parameters[9u]
                + migrated.parameters[11u] <= 1.000001
            && migrated.parameters[15u] == 0.0
            && migrated.parameters[16u] == 0.0
            && migrated.parameters[17u] == 20000.0
            && migrated.parameters[18u] == 0.0
            && migrated.parameters[19u] == 0.0,
        "legacy state did not preserve wraps while migrating ADSR/filter defaults");
    ok &= expect(plugin->activate(plugin, 48000.0, 8u, 16u)
            && plugin->start_processing(plugin),
        "activation failed");

    std::vector<std::array<float, 16u>> rendered(channels);
    std::vector<float*> pointers(channels);
    for (uint32_t channel = 0u; channel < channels; ++channel)
        pointers[channel] = rendered[channel].data();
    clap_audio_buffer_t output {};
    output.data32 = pointers.data();
    output.channel_count = channels;
    NoteEvents note;
    clap_process_t process {};
    process.frames_count = 16u;
    process.in_events = &note.input;
    process.audio_outputs = &output;
    process.audio_outputs_count = 1u;
    ok &= expect(plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE,
        "process call failed");
    bool locked = rendered[0u][0u] == 0.0f;
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        locked = locked && std::abs(rendered[channel][1u]
            - static_cast<float>(channel + 1u) * 0.1f) < 1.0e-6f;
    }
    ok &= expect(locked,
        "embedded audio did not render sample-accurately by lane");
    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    return ok;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::fprintf(stderr,
            "usage: s3g_sample_player_clap_smoke <bundle-or-binary>\n");
        return 2;
    }
    bool ok = true;
    const auto binary = resolveBinary(argv[1]);
    void* library = !binary.empty()
        ? dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW) : nullptr;
    ok &= expect(library != nullptr, library ? "" : dlerror());
    const auto* entry = library ? static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry")) : nullptr;
    ok &= expect(entry && entry->init(binary.c_str()),
        "entry initialization failed");

    HostContext context;
    context.host.clap_version = CLAP_VERSION_INIT;
    context.host.name = "s3g sample player smoke";
    context.host.vendor = "s3g";
    context.host.url = "https://github.com/s3g/s3g-dsp";
    context.host.version = "1";
    context.host.get_extension = hostGetExtension;
    context.host.request_restart = hostRequest;
    context.host.request_process = hostRequest;
    context.host.request_callback = hostRequest;
    const auto* factory = entry ? static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    ok &= expect(factory && factory->get_plugin_count(factory) == 2u,
        "expected stereo and 16-channel descriptors");
    if (factory) {
        ok &= exerciseDescriptor(factory, &context.host,
            "org.s3g.s3g-dsp.sample-player", "s3g Sample Player 2",
            2u, 3002u);
        ok &= exerciseDescriptor(factory, &context.host,
            "org.s3g.s3g-dsp.sample-player-16", "s3g Sample Player 16",
            16u, 3016u);
    }
    if (entry) entry->deinit();
    if (library) dlclose(library);
    if (!ok) return 1;
    std::puts("s3g Sample Player CLAP smoke: ok");
    return 0;
}
