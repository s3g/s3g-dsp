#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-name.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kStateMagic = 0x44443353u;
constexpr uint32_t kLegacyStateVersion = 1u;
constexpr uint32_t kCurrentStateVersion = 4u;
constexpr std::size_t kParamCount = 17u;
constexpr std::size_t kPriorParamCount = 16u;
constexpr std::size_t kLegacyParamCount = 12u;
constexpr std::size_t kMaximumPathBytes = 1024u;

struct SavedState {
    uint32_t magic = kStateMagic;
    uint32_t version = kLegacyStateVersion;
    uint32_t parameterCount = static_cast<uint32_t>(kLegacyParamCount);
    std::array<double, kLegacyParamCount> parameters {};
    std::array<char, kMaximumPathBytes> path {};
    uint8_t embedded = 1u;
    uint8_t channelCount = 1u;
    uint8_t reserved0 = 0u;
    uint8_t reserved1 = 0u;
    uint32_t frameCount = 4096u;
    double sampleRate = 1000.0;
};

struct SavedStateV2Record {
    uint32_t magic = kStateMagic;
    uint32_t version = 2u;
    uint32_t parameterCount = static_cast<uint32_t>(kPriorParamCount);
    std::array<double, kPriorParamCount> parameters {};
    std::array<char, kMaximumPathBytes> path {};
    uint8_t embedded = 1u;
    uint8_t channelCount = 1u;
    uint8_t reserved0 = 0u;
    uint8_t reserved1 = 0u;
    uint32_t frameCount = 4096u;
    double sampleRate = 1000.0;
};

struct SavedStateV3Record {
    uint32_t magic = kStateMagic;
    uint32_t version = 3u;
    uint32_t parameterCount = static_cast<uint32_t>(kPriorParamCount);
    std::array<double, kPriorParamCount> parameters {};
    std::array<char, kMaximumPathBytes> path {};
    uint8_t embedded = 0u;
    uint8_t channelCount = 0u;
    uint8_t reserved0 = 0u;
    uint8_t reserved1 = 0u;
    uint32_t frameCount = 0u;
    double sampleRate = 0.0;
    double cueA = -1.0;
    double cueB = -1.0;
    uint8_t cueValidMask = 0u;
    std::array<uint8_t, 7u> reservedCue {};
};

struct SavedStateV4Record {
    uint32_t magic = kStateMagic;
    uint32_t version = kCurrentStateVersion;
    uint32_t parameterCount = static_cast<uint32_t>(kParamCount);
    std::array<double, kParamCount> parameters {};
    std::array<char, kMaximumPathBytes> path {};
    uint8_t embedded = 0u;
    uint8_t channelCount = 0u;
    uint8_t reserved0 = 0u;
    uint8_t reserved1 = 0u;
    uint32_t frameCount = 0u;
    double sampleRate = 0.0;
    double cueA = -1.0;
    double cueB = -1.0;
    uint8_t cueValidMask = 0u;
    std::array<uint8_t, 7u> reservedCue {};
};

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequest(const clap_host_t*) {}

std::filesystem::path resolveBinary(const std::filesystem::path& supplied)
{
    if (std::filesystem::is_regular_file(supplied)) return supplied;
#if defined(__APPLE__)
    if (std::filesystem::is_directory(supplied)) {
        const auto macOS = supplied / "Contents" / "MacOS";
        for (const auto& entry : std::filesystem::directory_iterator(macOS))
            if (entry.is_regular_file()) return entry.path();
    }
#endif
    return {};
}

struct MemoryInput {
    std::vector<uint8_t> bytes;
    std::size_t offset = 0u;
    clap_istream_t stream {
        this,
        [](const clap_istream_t* stream, void* buffer,
            uint64_t size) -> int64_t {
            auto* self = static_cast<MemoryInput*>(stream->ctx);
            if (!self || !buffer) return -1;
            const std::size_t available = self->bytes.size() - self->offset;
            const std::size_t count = std::min<std::size_t>(available,
                static_cast<std::size_t>(size));
            if (count == 0u) return 0;
            std::memcpy(buffer, self->bytes.data() + self->offset, count);
            self->offset += count;
            return static_cast<int64_t>(count);
        },
    };
};

struct MemoryOutput {
    std::vector<uint8_t> bytes;
    clap_ostream_t stream {
        this,
        [](const clap_ostream_t* stream, const void* buffer,
            uint64_t size) -> int64_t {
            auto* self = static_cast<MemoryOutput*>(stream->ctx);
            if (!self || (!buffer && size > 0u)) return -1;
            const auto* first = static_cast<const uint8_t*>(buffer);
            self->bytes.insert(self->bytes.end(), first,
                first + static_cast<std::size_t>(size));
            return static_cast<int64_t>(size);
        },
    };
};

struct NoteEvents {
    std::vector<clap_event_note_t> events;
    clap_input_events_t interface {
        this,
        [](const clap_input_events_t* list) -> uint32_t {
            const auto* self = static_cast<const NoteEvents*>(list->ctx);
            return self ? static_cast<uint32_t>(self->events.size()) : 0u;
        },
        [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            const auto* self = static_cast<const NoteEvents*>(list->ctx);
            return self && index < self->events.size()
                ? &self->events[index].header : nullptr;
        },
    };

    void add(uint32_t frame, uint16_t type, int32_t noteId, int16_t key,
        double velocity)
    {
        clap_event_note_t event {};
        event.header.size = sizeof(event);
        event.header.time = frame;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = type;
        event.port_index = 0;
        event.channel = 0;
        event.key = key;
        event.note_id = noteId;
        event.velocity = velocity;
        events.push_back(event);
    }
};

struct MidiEvents {
    std::vector<clap_event_midi_t> events;
    clap_input_events_t interface {
        this,
        [](const clap_input_events_t* list) -> uint32_t {
            const auto* self = static_cast<const MidiEvents*>(list->ctx);
            return self ? static_cast<uint32_t>(self->events.size()) : 0u;
        },
        [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            const auto* self = static_cast<const MidiEvents*>(list->ctx);
            return self && index < self->events.size()
                ? &self->events[index].header : nullptr;
        },
    };

    void addCc(uint32_t frame, uint8_t controller, uint8_t value)
    {
        clap_event_midi_t event {};
        event.header.size = sizeof(event);
        event.header.time = frame;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_MIDI;
        event.port_index = 0u;
        event.data[0u] = 0xb0u;
        event.data[1u] = controller;
        event.data[2u] = value;
        events.push_back(event);
    }
};

bool processBlock(const clap_plugin_t* plugin, uint32_t frames,
    NoteEvents* events, std::vector<float>& left,
    std::vector<float>& right)
{
    left.assign(frames, 0.0f);
    right.assign(frames, 0.0f);
    float* channels[] { left.data(), right.data() };
    clap_audio_buffer_t output {};
    output.data32 = channels;
    output.channel_count = 2u;
    clap_process_t process {};
    process.steady_time = -1;
    process.frames_count = frames;
    process.audio_outputs = &output;
    process.audio_outputs_count = 1u;
    process.in_events = events ? &events->interface : nullptr;
    return plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE;
}

bool processMidiBlock(const clap_plugin_t* plugin, uint32_t frames,
    MidiEvents* events, std::vector<float>& left,
    std::vector<float>& right)
{
    left.assign(frames, 0.0f);
    right.assign(frames, 0.0f);
    float* channels[] { left.data(), right.data() };
    clap_audio_buffer_t output {};
    output.data32 = channels;
    output.channel_count = 2u;
    clap_process_t process {};
    process.steady_time = -1;
    process.frames_count = frames;
    process.audio_outputs = &output;
    process.audio_outputs_count = 1u;
    process.in_events = events ? &events->interface : nullptr;
    return plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE;
}

bool near(float actual, float expected, float tolerance = 0.002f)
{
    return std::abs(actual - expected) <= tolerance;
}

float maximumMagnitude(const std::vector<float>& samples)
{
    float maximum = 0.0f;
    for (const float sample : samples)
        maximum = std::max(maximum, std::abs(sample));
    return maximum;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: s3g_sample_doubles_clap_smoke "
            "<bundle-or-binary>\n";
        return 2;
    }
    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "Could not resolve Sample Doubles binary\n";
        return 1;
    }
    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "Could not load Sample Doubles: " << dlerror() << '\n';
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    bool ok = entry && entry->init(binary.c_str());

    clap_host_t host {};
    host.clap_version = CLAP_VERSION_INIT;
    host.name = "Sample Doubles smoke";
    host.vendor = "s3g";
    host.url = "https://github.com/s3g/s3g-dsp";
    host.version = "1";
    host.get_extension = hostGetExtension;
    host.request_restart = hostRequest;
    host.request_process = hostRequest;
    host.request_callback = hostRequest;

    const auto* factory = ok ? static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    const clap_plugin_t* plugin = factory ? factory->create_plugin(
        factory, &host, "org.s3g.s3g-dsp.sample-doubles") : nullptr;
    ok = ok && factory && factory->get_plugin_count(factory) == 1u
        && plugin && plugin->init(plugin);

    const auto* ports = ok ? static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS)) : nullptr;
    const auto* notePorts = ok ? static_cast<const clap_plugin_note_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS)) : nullptr;
    const auto* noteNames = ok ? static_cast<const clap_plugin_note_name_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_NAME)) : nullptr;
    const auto* params = ok ? static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS)) : nullptr;
    const auto* state = ok ? static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE)) : nullptr;
    const auto* gui = ok ? static_cast<const clap_plugin_gui_t*>(
        plugin->get_extension(plugin, CLAP_EXT_GUI)) : nullptr;
    clap_audio_port_info_t outputPort {};
    ok = ok && ports && notePorts && noteNames && params && state
        && ports->count(plugin, true) == 0u
        && ports->count(plugin, false) == 1u
        && ports->get(plugin, 0u, false, &outputPort)
        && outputPort.channel_count == 2u
        && notePorts->count(plugin, true) == 1u
        && noteNames->count(plugin) == 29u
        && params->count(plugin) == kParamCount;
#if defined(__APPLE__)
    ok = ok && gui;
#endif

    bool foundCrossfader = false;
    bool foundPhase = false;
    bool foundDeckALevel = false;
    bool foundDeckBLevel = false;
    bool foundLink = false;
    bool foundLivePhase = false;
    bool foundCuePreroll = false;
    for (uint32_t index = 0u; ok && index < params->count(plugin); ++index) {
        clap_param_info_t info {};
        ok = params->get_info(plugin, index, &info);
        if (info.id == 9u)
            foundCrossfader = std::strcmp(info.name, "Crossfader") == 0;
        if (info.id == 2u)
            foundPhase = std::strcmp(info.name, "Phase Drift") == 0;
        if (info.id == 13u)
            foundDeckALevel = std::strcmp(info.name, "Deck A Level") == 0;
        if (info.id == 14u)
            foundDeckBLevel = std::strcmp(info.name, "Deck B Level") == 0;
        if (info.id == 15u)
            foundLink = std::strcmp(info.name, "Link Decks") == 0;
        if (info.id == 16u)
            foundLivePhase = std::strcmp(
                info.name, "Deck B Live Phase") == 0;
        if (info.id == 17u)
            foundCuePreroll = std::strcmp(
                info.name, "Cue Preroll") == 0;
    }
    ok = ok && foundCrossfader && foundPhase && foundDeckALevel
        && foundDeckBLevel && foundLink && foundLivePhase
        && foundCuePreroll;

    SavedState saved;
    saved.parameters = {{
        0.0, 0.0, 60.0, 1.0, 2.0, 0.0, 1.0, 0.0,
        -1.0, 0.0, 0.0, 0.0,
    }};
    std::vector<float> samples(saved.frameCount);
    for (uint32_t frame = 0u; frame < saved.frameCount; ++frame)
        samples[frame] = static_cast<float>(frame) * 0.0001f;
    MemoryInput input;
    input.bytes.resize(sizeof(saved) + samples.size() * sizeof(float));
    std::memcpy(input.bytes.data(), &saved, sizeof(saved));
    std::memcpy(input.bytes.data() + sizeof(saved), samples.data(),
        samples.size() * sizeof(float));
    ok = ok && state->load(plugin, &input.stream);
    ok = ok && plugin->activate(plugin, 1000.0, 1u, 128u)
        && plugin->start_processing(plugin);

    NoteEvents commands;
    commands.add(0u, CLAP_EVENT_NOTE_ON, 1, 36, 1.0);
    commands.add(10u, CLAP_EVENT_NOTE_ON, 2, 41, 1.0);
    commands.add(30u, CLAP_EVENT_NOTE_OFF, 2, 41, 0.0);
    std::vector<float> left;
    std::vector<float> right;
    ok = ok && processBlock(plugin, 64u, &commands, left, right)
        && near(left[4u], 0.0004f)
        && left[18u] > 0.10f
        && left[42u] < 0.01f
        && near(left[42u], right[42u]);

    // Legacy v1 state must supply defaults for every appended parameter.
    double deckALevel = -99.0;
    double deckBLevel = -99.0;
    double linked = -1.0;
    double livePhase = -99.0;
    double cuePreroll = -1.0;
    ok = ok && params->get_value(plugin, 13u, &deckALevel)
        && params->get_value(plugin, 14u, &deckBLevel)
        && params->get_value(plugin, 15u, &linked)
        && params->get_value(plugin, 16u, &livePhase)
        && params->get_value(plugin, 17u, &cuePreroll)
        && near(static_cast<float>(deckALevel), 0.0f)
        && near(static_cast<float>(deckBLevel), 0.0f)
        && near(static_cast<float>(linked), 1.0f)
        && near(static_cast<float>(livePhase), 0.0f)
        && near(static_cast<float>(cuePreroll), 150.0f);

    MidiEvents cc;
    cc.addCc(0u, 16u, 127u);
    cc.addCc(0u, 17u, 0u);
    cc.addCc(0u, 18u, 127u);
    cc.addCc(0u, 19u, 95u);
    ok = ok && processMidiBlock(plugin, 32u, &cc, left, right);
    double crossfader = 0.0;
    ok = ok && params->get_value(plugin, 9u, &crossfader)
        && params->get_value(plugin, 13u, &deckALevel)
        && params->get_value(plugin, 14u, &deckBLevel)
        && params->get_value(plugin, 16u, &livePhase)
        && crossfader > 0.999
        && deckALevel < -59.9
        && deckBLevel > 11.9
        && livePhase > 0.49 && livePhase < 0.51;

    // The appended command notes must reach the same linked/unlinked deck and
    // momentary Drag paths used by the GUI.
    NoteEvents pauseDecks;
    pauseDecks.add(0u, CLAP_EVENT_NOTE_ON, 20, 44, 1.0);
    ok = ok && processBlock(plugin, 32u, &pauseDecks, left, right)
        && maximumMagnitude(left) < 1.0e-7f
        && maximumMagnitude(right) < 1.0e-7f;
    NoteEvents resumeDecks;
    resumeDecks.add(0u, CLAP_EVENT_NOTE_ON, 21, 45, 1.0);
    ok = ok && processBlock(plugin, 32u, &resumeDecks, left, right)
        && maximumMagnitude(left) > 0.01f;
    NoteEvents dragGate;
    dragGate.add(0u, CLAP_EVENT_NOTE_ON, 22, 46, 0.25);
    dragGate.add(16u, CLAP_EVENT_NOTE_OFF, 22, 46, 0.0);
    ok = ok && processBlock(plugin, 32u, &dragGate, left, right);

    // Each deck owns one replaceable zero-crossing cue. The command-note path
    // is the same one Tracker uses, and triggers are one-shot retriggers rather
    // than gates.
    NoteEvents cueCommands;
    cueCommands.add(0u, CLAP_EVENT_NOTE_ON, 23, 61, 1.0);
    cueCommands.add(4u, CLAP_EVENT_NOTE_ON, 24, 63, 1.0);
    cueCommands.add(8u, CLAP_EVENT_NOTE_ON, 25, 62, 1.0);
    cueCommands.add(12u, CLAP_EVENT_NOTE_ON, 26, 64, 1.0);
    ok = ok && processBlock(plugin, 32u, &cueCommands, left, right);

    MemoryOutput savedV4;
    ok = ok && state->save(plugin, &savedV4.stream)
        && savedV4.bytes.size() >= sizeof(SavedStateV4Record);
    if (ok) {
        SavedStateV4Record record;
        std::memcpy(&record, savedV4.bytes.data(), sizeof(record));
        ok = record.magic == kStateMagic
            && record.version == kCurrentStateVersion
            && record.parameterCount == kParamCount
            && near(static_cast<float>(record.parameters[16u]), 150.0f)
            && record.cueValidMask == 3u
            && record.cueA >= 0.0 && record.cueA <= 1.0
            && record.cueB >= 0.0 && record.cueB <= 1.0;
    }

    // Version two remains readable and intentionally supplies no cue markers.
    if (ok) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        SavedStateV2Record legacyV2;
        legacyV2.parameters = {{
            0.0, 0.0, 60.0, 1.0, 2.0, 0.0, 1.0, 0.0,
            -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0,
        }};
        MemoryInput v2Input;
        v2Input.bytes.resize(sizeof(legacyV2)
            + samples.size() * sizeof(float));
        std::memcpy(v2Input.bytes.data(), &legacyV2, sizeof(legacyV2));
        std::memcpy(v2Input.bytes.data() + sizeof(legacyV2), samples.data(),
            samples.size() * sizeof(float));
        ok = state->load(plugin, &v2Input.stream)
            && plugin->activate(plugin, 1000.0, 1u, 128u)
            && plugin->start_processing(plugin)
            && processBlock(plugin, 1u, nullptr, left, right);
        MemoryOutput migratedV2;
        ok = ok && state->save(plugin, &migratedV2.stream)
            && migratedV2.bytes.size() >= sizeof(SavedStateV4Record);
        if (ok) {
            SavedStateV4Record record;
            std::memcpy(&record, migratedV2.bytes.data(), sizeof(record));
            ok = record.version == kCurrentStateVersion
                && record.parameterCount == kParamCount
                && near(static_cast<float>(record.parameters[16u]), 150.0f)
                && record.cueValidMask == 0u;
        }
    }

    // Version three cue positions remain readable while the appended pre-roll
    // parameter is supplied from the current default.
    if (ok) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        SavedStateV3Record legacyV3;
        legacyV3.parameters = {{
            0.0, 0.0, 60.0, 1.0, 2.0, 0.0, 1.0, 0.0,
            -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0,
        }};
        legacyV3.embedded = 1u;
        legacyV3.channelCount = 1u;
        legacyV3.frameCount = static_cast<uint32_t>(samples.size());
        legacyV3.sampleRate = 1000.0;
        legacyV3.cueA = 0.25;
        legacyV3.cueB = 0.75;
        legacyV3.cueValidMask = 3u;
        MemoryInput v3Input;
        v3Input.bytes.resize(sizeof(legacyV3)
            + samples.size() * sizeof(float));
        std::memcpy(v3Input.bytes.data(), &legacyV3, sizeof(legacyV3));
        std::memcpy(v3Input.bytes.data() + sizeof(legacyV3), samples.data(),
            samples.size() * sizeof(float));
        ok = state->load(plugin, &v3Input.stream)
            && plugin->activate(plugin, 1000.0, 1u, 128u)
            && plugin->start_processing(plugin)
            && processBlock(plugin, 1u, nullptr, left, right);
        MemoryOutput migratedV3;
        ok = ok && state->save(plugin, &migratedV3.stream)
            && migratedV3.bytes.size() >= sizeof(SavedStateV4Record);
        if (ok) {
            SavedStateV4Record record;
            std::memcpy(&record, migratedV3.bytes.data(), sizeof(record));
            ok = record.version == kCurrentStateVersion
                && record.parameterCount == kParamCount
                && near(static_cast<float>(record.parameters[16u]), 150.0f)
                && record.cueValidMask == 3u
                && near(static_cast<float>(record.cueA), 0.25f)
                && near(static_cast<float>(record.cueB), 0.75f);
        }
    }

    // Reload the current state and prove both cue positions survive the state
    // boundary and are adopted by the audio engine on its next block.
    if (ok) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        MemoryInput v4Input;
        v4Input.bytes = savedV4.bytes;
        ok = state->load(plugin, &v4Input.stream)
            && plugin->activate(plugin, 1000.0, 1u, 128u)
            && plugin->start_processing(plugin)
            && processBlock(plugin, 1u, nullptr, left, right);
        MemoryOutput roundTrip;
        ok = ok && state->save(plugin, &roundTrip.stream)
            && roundTrip.bytes.size() >= sizeof(SavedStateV4Record);
        if (ok) {
            SavedStateV4Record record;
            std::memcpy(&record, roundTrip.bytes.data(), sizeof(record));
            ok = record.version == kCurrentStateVersion
                && record.cueValidMask == 3u;
        }
    }

    if (plugin) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }
    if (entry) entry->deinit();
    dlclose(library);
    if (!ok) {
        std::cerr << "Sample Doubles CLAP smoke failed\n";
        return 1;
    }
    std::cout << "s3g Sample Doubles CLAP smoke: ok\n";
    return 0;
}
