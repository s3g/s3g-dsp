#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/audio-ports-config.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-name.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kStateMagic = 0x4d533353u;
constexpr uint32_t kStateVersion = 4u;
constexpr std::size_t kLegacyParamCount = 25u;
constexpr std::size_t kOriginalStereoParamCount = 27u;
constexpr std::size_t kPreviousParamCount = 31u;
constexpr std::size_t kStereoParamCount = 36u;
constexpr std::size_t kParamCount = 42u;
constexpr std::size_t kMaximumPathBytes = 1024u;
constexpr uint32_t kFrameCount = 48000u;

struct SavedState {
    uint32_t magic = kStateMagic;
    uint32_t version = kStateVersion;
    uint32_t parameterCount = static_cast<uint32_t>(kParamCount);
    uint32_t reservedHeader = 0u;
    std::array<double, kParamCount> parameters {};
    std::array<char, kMaximumPathBytes> path {};
    uint8_t embedded = 1u;
    uint8_t channelCount = 2u;
    uint8_t storageMode = 2u; // Embed
    uint8_t reserved1 = 0u;
    uint32_t frameCount = kFrameCount;
    double sampleRate = 48000.0;
};

struct LegacySavedStateV1 {
    uint32_t magic = kStateMagic;
    uint32_t version = 1u;
    uint32_t parameterCount = static_cast<uint32_t>(kLegacyParamCount);
    uint32_t reservedHeader = 0u;
    std::array<double, kLegacyParamCount> parameters {};
    std::array<char, kMaximumPathBytes> path {};
    uint8_t embedded = 0u;
    uint8_t channelCount = 0u;
    uint8_t reserved0 = 0u;
    uint8_t reserved1 = 0u;
    uint32_t frameCount = 0u;
    double sampleRate = 0.0;
};

struct PreviousSavedState {
    uint32_t magic = kStateMagic;
    uint32_t version = 3u;
    uint32_t parameterCount = static_cast<uint32_t>(kPreviousParamCount);
    uint32_t reservedHeader = 0u;
    std::array<double, kPreviousParamCount> parameters {};
    std::array<char, kMaximumPathBytes> path {};
    uint8_t embedded = 1u;
    uint8_t channelCount = 2u;
    uint8_t storageMode = 2u;
    uint8_t reserved1 = 0u;
    uint32_t frameCount = kFrameCount;
    double sampleRate = 48000.0;
};

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequest(const clap_host_t*) {}

std::filesystem::path resolveBinary(const std::filesystem::path& supplied)
{
    if (std::filesystem::is_regular_file(supplied)) return supplied;
#if defined(__APPLE__)
    if (std::filesystem::is_directory(supplied)) {
        const auto directory = supplied / "Contents" / "MacOS";
        for (const auto& entry : std::filesystem::directory_iterator(directory))
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
            const std::size_t count = std::min<std::size_t>(
                self->bytes.size() - self->offset,
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
            if (!self || (!buffer && size != 0u)) return -1;
            const auto* first = static_cast<const uint8_t*>(buffer);
            self->bytes.insert(self->bytes.end(), first,
                first + static_cast<std::size_t>(size));
            return static_cast<int64_t>(size);
        },
    };
};

MemoryInput embeddedFixture()
{
    SavedState saved;
    saved.parameters = {{
        -6.0, 0.0, 0.0, 0.0, 1.0, 0.5, 0.25, 1.0, 0.35, 0.0,
        12.0, 1.0, 0.65, 0.5, 1.0, 0.0, 1.0, 60.0, 0.0, 0.0,
        0.003, 0.020, 1.0, 1.0, 0.0, 0.0, 1.0,
        0.0, 1.0, 0.0, 32.0,
        0.0, 0.0, 4.0, 2.0, 0.05, 0.0, 0.15, 0.0, 0.0,
        0.0, 0.0,
    }};
    std::vector<float> left(kFrameCount);
    std::vector<float> right(kFrameCount);
    constexpr double pi = 3.14159265358979323846;
    for (uint32_t frame = 0u; frame < kFrameCount; ++frame) {
        const double time = static_cast<double>(frame) / 48000.0;
        left[frame] = static_cast<float>(0.7 * std::sin(2.0 * pi * 173.0 * time));
        right[frame] = static_cast<float>(0.6 * std::sin(
            2.0 * pi * 251.0 * time + 0.4));
    }
    MemoryInput input;
    const auto append = [&input](const void* data, std::size_t size) {
        const auto* first = static_cast<const uint8_t*>(data);
        input.bytes.insert(input.bytes.end(), first, first + size);
    };
    append(&saved, sizeof(saved));
    append(left.data(), left.size() * sizeof(float));
    append(right.data(), right.size() * sizeof(float));
    return input;
}

MemoryInput previousEmbeddedFixture(uint32_t version)
{
    PreviousSavedState saved;
    saved.version = version;
    saved.parameters = {{
        -6.0, 0.0, 0.0, 0.0, 1.0, 0.5, 0.25, 1.0, 0.35, 0.0,
        12.0, 1.0, 0.65, 0.5, 1.0, 0.0, 1.0, 60.0, 0.0, 0.0,
        0.003, 0.020, 1.0, 1.0, 0.0, 0.0, 1.0,
        0.0, 1.0, 0.0, 32.0,
    }};
    std::vector<float> left(kFrameCount);
    std::vector<float> right(kFrameCount);
    constexpr double pi = 3.14159265358979323846;
    for (uint32_t frame = 0u; frame < kFrameCount; ++frame) {
        const double time = static_cast<double>(frame) / 48000.0;
        left[frame] = static_cast<float>(0.7
            * std::sin(2.0 * pi * 173.0 * time));
        right[frame] = static_cast<float>(0.6
            * std::sin(2.0 * pi * 251.0 * time + 0.4));
    }
    MemoryInput input;
    const auto append = [&input](const void* data, std::size_t size) {
        const auto* first = static_cast<const uint8_t*>(data);
        input.bytes.insert(input.bytes.end(), first, first + size);
    };
    append(&saved, sizeof(saved));
    append(left.data(), left.size() * sizeof(float));
    append(right.data(), right.size() * sizeof(float));
    return input;
}

struct Events {
    std::vector<std::vector<uint8_t>> storage;
    clap_input_events_t interface {
        this,
        [](const clap_input_events_t* events) -> uint32_t {
            const auto* self = static_cast<const Events*>(events->ctx);
            return self ? static_cast<uint32_t>(self->storage.size()) : 0u;
        },
        [](const clap_input_events_t* events, uint32_t index)
            -> const clap_event_header_t* {
            const auto* self = static_cast<const Events*>(events->ctx);
            return self && index < self->storage.size()
                ? reinterpret_cast<const clap_event_header_t*>(
                    self->storage[index].data()) : nullptr;
        },
    };

    template <typename Event>
    void add(const Event& event)
    {
        storage.emplace_back(sizeof(Event));
        std::memcpy(storage.back().data(), &event, sizeof(Event));
    }

    void note(uint16_t type, int16_t channel, int16_t key,
        double velocity = 1.0)
    {
        clap_event_note_t event {};
        event.header.size = sizeof(event);
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = type;
        event.port_index = 0;
        event.channel = channel;
        event.key = key;
        event.note_id = key;
        event.velocity = velocity;
        add(event);
    }

    void parameter(clap_id id, double value)
    {
        clap_event_param_value_t event {};
        event.header.size = sizeof(event);
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = id;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
        add(event);
    }

    void midiCc(uint8_t channel, uint8_t controller, uint8_t value)
    {
        clap_event_midi_t event {};
        event.header.size = sizeof(event);
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_MIDI;
        event.port_index = 0u;
        event.data[0u] = static_cast<uint8_t>(0xb0u | (channel & 0x0fu));
        event.data[1u] = controller;
        event.data[2u] = value;
        add(event);
    }
};

bool processBlock(const clap_plugin_t* plugin, uint32_t frameCount,
    const clap_input_events_t* events, std::vector<float>& left,
    std::vector<float>& right)
{
    left.assign(frameCount, 0.0f);
    right.assign(frameCount, 0.0f);
    float* channels[] { left.data(), right.data() };
    clap_audio_buffer_t output {};
    output.data32 = channels;
    output.channel_count = 2u;
    clap_process_t process {};
    process.steady_time = -1;
    process.frames_count = frameCount;
    process.audio_outputs = &output;
    process.audio_outputs_count = 1u;
    process.in_events = events;
    return plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE;
}

bool processMultichannelBlock(const clap_plugin_t* plugin,
    uint32_t frameCount, const clap_input_events_t* events,
    std::array<std::vector<float>, 32u>& rendered)
{
    std::array<float*, 32u> channels {};
    for (std::size_t channel = 0u; channel < rendered.size(); ++channel) {
        rendered[channel].assign(frameCount, 0.0f);
        channels[channel] = rendered[channel].data();
    }
    clap_audio_buffer_t output {};
    output.data32 = channels.data();
    output.channel_count = static_cast<uint32_t>(channels.size());
    clap_process_t process {};
    process.steady_time = -1;
    process.frames_count = frameCount;
    process.audio_outputs = &output;
    process.audio_outputs_count = 1u;
    process.in_events = events;
    return plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE;
}

float maximumMagnitude(const std::vector<float>& samples)
{
    float result = 0.0f;
    for (const float sample : samples)
        result = std::max(result, std::abs(sample));
    return result;
}

float channelDifference(const std::vector<float>& left,
    const std::vector<float>& right)
{
    float result = 0.0f;
    for (std::size_t index = 0u; index < left.size(); ++index)
        result = std::max(result, std::abs(left[index] - right[index]));
    return result;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: s3g_sample_motion_clap_smoke <plugin>\n";
        return 2;
    }
    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "could not resolve plug-in binary\n";
        return 1;
    }
    void* library = dlopen(binary.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        std::cerr << dlerror() << '\n';
        return 1;
    }
    const auto* entry = reinterpret_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    bool ok = entry && clap_version_is_compatible(entry->clap_version)
        && entry->init(binary.c_str());
    const auto* factory = ok ? static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    ok = ok && factory && factory->get_plugin_count(factory) == 2u;
    const auto* descriptor = ok
        ? factory->get_plugin_descriptor(factory, 0u) : nullptr;
    const auto* multichannelDescriptor = ok
        ? factory->get_plugin_descriptor(factory, 1u) : nullptr;
    ok = ok && descriptor
        && std::strcmp(descriptor->id,
            "org.s3g.s3g-dsp.sample-motion") == 0
        && std::strcmp(descriptor->name, "s3g Sample Motion 2") == 0
        && multichannelDescriptor
        && std::strcmp(multichannelDescriptor->id,
            "org.s3g.s3g-dsp.sample-motion-32") == 0
        && std::strcmp(multichannelDescriptor->name,
            "s3g Sample Motion 32") == 0;

    clap_host_t host {
        CLAP_VERSION_INIT,
        nullptr,
        "s3g sample motion smoke",
        "s3g",
        "",
        "1",
        hostGetExtension,
        hostRequest,
        hostRequest,
        hostRequest,
    };
    const clap_plugin_t* plugin = ok
        ? factory->create_plugin(factory, &host, descriptor->id) : nullptr;
    ok = ok && plugin && plugin->init(plugin);
    const auto* audioPorts = ok ? static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS)) : nullptr;
    const auto* notePorts = ok ? static_cast<const clap_plugin_note_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS)) : nullptr;
    const auto* params = ok ? static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS)) : nullptr;
    const auto* state = ok ? static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE)) : nullptr;
    ok = ok && audioPorts && notePorts && params && state
        && audioPorts->count(plugin, false) == 1u
        && audioPorts->count(plugin, true) == 0u
        && notePorts->count(plugin, true) == 1u
        && params->count(plugin) == kStereoParamCount;
    MemoryOutput defaultState;
    SavedState defaultSaved;
    ok = ok && state->save(plugin, &defaultState.stream)
        && defaultState.bytes.size() == sizeof(SavedState);
    if (defaultState.bytes.size() == sizeof(SavedState))
        std::memcpy(&defaultSaved, defaultState.bytes.data(),
            sizeof(defaultSaved));
    ok = ok && defaultSaved.version == kStateVersion
        && defaultSaved.storageMode == 0u
        && defaultSaved.embedded == 0u;
    clap_audio_port_info_t audioInfo {};
    clap_note_port_info_t noteInfo {};
    ok = ok && audioPorts->get(plugin, 0u, false, &audioInfo)
        && audioInfo.channel_count == 2u
        && notePorts->get(plugin, 0u, true, &noteInfo)
        && (noteInfo.supported_dialects & CLAP_NOTE_DIALECT_MIDI) != 0u;
    const std::array<const char*, kStereoParamCount> names {{
        "Out", "Motion", "Articulation", "Start", "End", "Locus",
        "Field", "Motion Rate", "Travel", "Jitter", "Inner Rate",
        "Outer Rate", "Packet Duty", "Symmetry", "Join", "Voice Mode",
        "Trigger", "Root Note", "Tune", "Fine Tune", "Attack",
        "Release", "Velocity", "Seed", "MIDI Receive", "Rate Basis",
        "Motor Envelope", "Segment Model", "Segment Trigger", "Event Rate",
        "Event Repeats", "Step", "Pitch Scatter", "Level Variation",
        "Interval Curve", "Event Overlap",
    }};
    for (uint32_t index = 0u; ok && index < kStereoParamCount; ++index) {
        clap_param_info_t info {};
        const clap_id expectedId = index < kOriginalStereoParamCount
            ? index + 1u : 32u + index - kOriginalStereoParamCount;
        ok = params->get_info(plugin, index, &info)
            && info.id == expectedId
            && std::strcmp(info.name, names[index]) == 0;
    }
    clap_param_info_t motionInfo {};
    ok = ok && params->get_info(plugin, 1u, &motionInfo)
        && motionInfo.id == 2u
        && std::abs(motionInfo.min_value) < 1.0e-9
        && std::abs(motionInfo.max_value - 7.0) < 1.0e-9;
    char namedText[64] {};
    double namedValue = -1.0;
    ok = ok && params->value_to_text(plugin, 2u, 3.0, namedText,
            sizeof(namedText))
        && std::strcmp(namedText, "Zigzag") == 0
        && params->text_to_value(plugin, 2u, "Reverse", &namedValue)
        && std::abs(namedValue - 5.0) < 1.0e-9
        && params->value_to_text(plugin, 2u, 6.0, namedText,
            sizeof(namedText))
        && std::strcmp(namedText, "Moving Loop") == 0
        && params->text_to_value(plugin, 2u, "BaktoBak", &namedValue)
        && std::abs(namedValue - 7.0) < 1.0e-9
        && params->value_to_text(plugin, 3u, 2.0, namedText,
            sizeof(namedText))
        && std::strcmp(namedText, "Packets") == 0
        && params->value_to_text(plugin, 26u, 0.0, namedText,
            sizeof(namedText))
        && std::strcmp(namedText, "Normal") == 0
        && params->value_to_text(plugin, 27u, 3.0, namedText,
            sizeof(namedText))
        && std::strcmp(namedText, "Plateau") == 0
        && params->value_to_text(plugin, 32u, 5.0, namedText,
            sizeof(namedText))
        && std::strcmp(namedText, "Bounce") == 0
        && params->text_to_value(plugin, 33u, "Turn", &namedValue)
        && std::abs(namedValue - 2.0) < 1.0e-9
        && !params->get_value(plugin, 28u, &namedValue);
#if defined(__APPLE__)
    ok = ok && plugin->get_extension(plugin, CLAP_EXT_GUI) != nullptr;
#endif

    auto fixture = embeddedFixture();
    ok = ok && state->load(plugin, &fixture.stream);
    MemoryOutput firstSave;
    ok = ok && state->save(plugin, &firstSave.stream)
        && firstSave.bytes == fixture.bytes;

    // A restored decoded asset with no locator remains embedded even while
    // Project is the requested policy, so an unsaved project cannot make a
    // subsequent host snapshot silently discard the only copy of its audio.
    auto pendingProjectFixture = embeddedFixture();
    SavedState pendingProjectHeader;
    std::memcpy(&pendingProjectHeader, pendingProjectFixture.bytes.data(),
        sizeof(pendingProjectHeader));
    pendingProjectHeader.storageMode = 0u;
    std::memcpy(pendingProjectFixture.bytes.data(), &pendingProjectHeader,
        sizeof(pendingProjectHeader));
    MemoryOutput pendingProjectSave;
    SavedState savedPendingProject;
    ok = ok && state->load(plugin, &pendingProjectFixture.stream)
        && state->save(plugin, &pendingProjectSave.stream)
        && pendingProjectSave.bytes.size() == pendingProjectFixture.bytes.size();
    if (pendingProjectSave.bytes.size() >= sizeof(savedPendingProject))
        std::memcpy(&savedPendingProject, pendingProjectSave.bytes.data(),
            sizeof(savedPendingProject));
    else ok = false;
    ok = ok && savedPendingProject.storageMode == 0u
        && savedPendingProject.embedded == 1u
        && savedPendingProject.path[0] == '\0';

    // A pending Project request that still has a usable file locator keeps the
    // state compact even when its currently decoded asset came from payload.
    auto fileBackedProjectFixture = embeddedFixture();
    SavedState fileBackedProjectHeader;
    std::memcpy(&fileBackedProjectHeader,
        fileBackedProjectFixture.bytes.data(),
        sizeof(fileBackedProjectHeader));
    fileBackedProjectHeader.storageMode = 0u;
    std::snprintf(fileBackedProjectHeader.path.data(),
        fileBackedProjectHeader.path.size(), "%s",
        "/missing/s3g-sample-motion-project.wav");
    std::memcpy(fileBackedProjectFixture.bytes.data(),
        &fileBackedProjectHeader, sizeof(fileBackedProjectHeader));
    MemoryOutput fileBackedProjectSave;
    SavedState savedFileBackedProject;
    ok = ok && state->load(plugin, &fileBackedProjectFixture.stream)
        && state->save(plugin, &fileBackedProjectSave.stream)
        && fileBackedProjectSave.bytes.size() == sizeof(SavedState);
    if (fileBackedProjectSave.bytes.size() == sizeof(savedFileBackedProject))
        std::memcpy(&savedFileBackedProject,
            fileBackedProjectSave.bytes.data(),
            sizeof(savedFileBackedProject));
    else ok = false;
    ok = ok && savedFileBackedProject.storageMode == 0u
        && savedFileBackedProject.embedded == 0u
        && std::strcmp(savedFileBackedProject.path.data(),
            fileBackedProjectHeader.path.data()) == 0;
    ok = ok && plugin->activate(plugin, 48000.0, 32u, 256u)
        && plugin->start_processing(plugin);

    std::vector<float> left;
    std::vector<float> right;
    Events hover;
    hover.note(CLAP_EVENT_NOTE_ON, 0, 60);
    ok = ok && processBlock(plugin, 256u, &hover.interface, left, right)
        && maximumMagnitude(left) > 1.0e-5f
        && maximumMagnitude(right) > 1.0e-5f
        && channelDifference(left, right) > 1.0e-5f;

    Events mirrorMotor;
    mirrorMotor.parameter(2u, 1.0);
    mirrorMotor.parameter(3u, 1.0);
    mirrorMotor.parameter(8u, 7.0);
    mirrorMotor.parameter(11u, 50.0);
    mirrorMotor.parameter(12u, 8.0);
    ok = ok && processBlock(plugin, 256u, &mirrorMotor.interface, left, right)
        && maximumMagnitude(left) > 1.0e-6f;

    Events drunk;
    drunk.parameter(2u, 2.0);
    drunk.parameter(10u, 0.75);
    drunk.parameter(24u, 4312.0);
    ok = ok && processBlock(plugin, 256u, &drunk.interface, left, right)
        && maximumMagnitude(left) > 1.0e-6f;

    Events zigzagPackets;
    zigzagPackets.parameter(2u, 3.0);
    zigzagPackets.parameter(3u, 2.0);
    zigzagPackets.parameter(8u, 0.75);
    zigzagPackets.parameter(26u, 0.0);
    ok = ok && processBlock(plugin, 256u, &zigzagPackets.interface,
        left, right) && maximumMagnitude(left) > 1.0e-6f;

    Events midiControls;
    midiControls.midiCc(0u, 16u, 24u);
    midiControls.midiCc(0u, 17u, 112u);
    midiControls.midiCc(0u, 21u, 64u);
    midiControls.midiCc(0u, 123u, 0u);
    ok = ok && processBlock(plugin, 64u, &midiControls.interface, left, right);
    double startValue = 0.0;
    double endValue = 0.0;
    ok = ok && params->get_value(plugin, 4u, &startValue)
        && params->get_value(plugin, 5u, &endValue)
        && startValue > 0.15 && endValue > 0.85 && startValue < endValue;

    if (plugin) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        auto versionTwoEmbedded = previousEmbeddedFixture(2u);
        MemoryOutput migratedVersionTwo;
        ok = ok && state->load(plugin, &versionTwoEmbedded.stream)
            && state->save(plugin, &migratedVersionTwo.stream);
        SavedState migratedEmbedded;
        if (migratedVersionTwo.bytes.size() >= sizeof(migratedEmbedded))
            std::memcpy(&migratedEmbedded, migratedVersionTwo.bytes.data(),
                sizeof(migratedEmbedded));
        else ok = false;
        ok = ok && migratedEmbedded.version == kStateVersion
            && migratedEmbedded.storageMode == 2u
            && migratedEmbedded.embedded == 1u
            && migratedEmbedded.parameters[31u] == 0.0;

        PreviousSavedState versionTwoLink;
        versionTwoLink.version = 2u;
        versionTwoLink.embedded = 0u;
        versionTwoLink.storageMode = 0u; // formerly a reserved byte
        std::snprintf(versionTwoLink.path.data(), versionTwoLink.path.size(),
            "%s", "/missing/s3g-sample-motion-link.wav");
        MemoryInput versionTwoLinkInput;
        versionTwoLinkInput.bytes.resize(sizeof(versionTwoLink));
        std::memcpy(versionTwoLinkInput.bytes.data(), &versionTwoLink,
            sizeof(versionTwoLink));
        MemoryOutput migratedLinkOutput;
        ok = ok && state->load(plugin, &versionTwoLinkInput.stream)
            && state->save(plugin, &migratedLinkOutput.stream);
        SavedState migratedLink;
        if (migratedLinkOutput.bytes.size() == sizeof(migratedLink))
            std::memcpy(&migratedLink, migratedLinkOutput.bytes.data(),
                sizeof(migratedLink));
        else ok = false;
        ok = ok && migratedLink.version == kStateVersion
            && migratedLink.storageMode == 1u
            && migratedLink.embedded == 0u
            && std::strcmp(migratedLink.path.data(),
                versionTwoLink.path.data()) == 0;

        auto versionThreeEmbedded = previousEmbeddedFixture(3u);
        MemoryOutput migratedVersionThree;
        ok = ok && state->load(plugin, &versionThreeEmbedded.stream)
            && state->save(plugin, &migratedVersionThree.stream);
        SavedState migratedProjectState;
        if (migratedVersionThree.bytes.size()
            >= sizeof(migratedProjectState))
            std::memcpy(&migratedProjectState,
                migratedVersionThree.bytes.data(),
                sizeof(migratedProjectState));
        else ok = false;
        ok = ok && migratedProjectState.version == kStateVersion
            && migratedProjectState.storageMode == 2u
            && migratedProjectState.parameters[33u] == 4.0;

        LegacySavedStateV1 legacy;
        legacy.parameters = {{
            -9.0, 2.0, 1.0, 0.1, 0.9, 0.45, 0.4, 7.0, 0.5, 0.2,
            18.0, 2.0, 0.6, 0.4, 0.8, 1.0, 2.0, 57.0, -2.0, 9.0,
            0.01, 0.2, 0.75, 413.0, 3.0,
        }};
        MemoryInput legacyInput;
        legacyInput.bytes.resize(sizeof(legacy));
        std::memcpy(legacyInput.bytes.data(), &legacy, sizeof(legacy));
        double migratedRate = 0.0;
        double migratedBasis = 0.0;
        double migratedEnvelope = -1.0;
        ok = ok && state->load(plugin, &legacyInput.stream)
            && params->get_value(plugin, 8u, &migratedRate)
            && params->get_value(plugin, 26u, &migratedBasis)
            && params->get_value(plugin, 27u, &migratedEnvelope)
            && std::abs(migratedRate - 7.0) < 1.0e-9
            && std::abs(migratedBasis - 1.0) < 1.0e-9
            && std::abs(migratedEnvelope) < 1.0e-9;
        MemoryOutput migratedLegacyOutput;
        SavedState migratedLegacy;
        ok = ok && state->save(plugin, &migratedLegacyOutput.stream);
        if (migratedLegacyOutput.bytes.size() == sizeof(migratedLegacy))
            std::memcpy(&migratedLegacy, migratedLegacyOutput.bytes.data(),
                sizeof(migratedLegacy));
        else ok = false;
        ok = ok && migratedLegacy.storageMode == 1u
            && migratedLegacy.embedded == 0u;
        plugin->destroy(plugin);
    }

    const clap_plugin_t* multichannel = factory ? factory->create_plugin(
        factory, &host, "org.s3g.s3g-dsp.sample-motion-32") : nullptr;
    ok = ok && multichannel && multichannel->init(multichannel);
    const auto* multiPorts = multichannel
        ? static_cast<const clap_plugin_audio_ports_t*>(
            multichannel->get_extension(multichannel, CLAP_EXT_AUDIO_PORTS))
        : nullptr;
    const auto* multiConfigs = multichannel
        ? static_cast<const clap_plugin_audio_ports_config_t*>(
            multichannel->get_extension(multichannel,
                CLAP_EXT_AUDIO_PORTS_CONFIG)) : nullptr;
    const auto* multiParams = multichannel
        ? static_cast<const clap_plugin_params_t*>(
            multichannel->get_extension(multichannel, CLAP_EXT_PARAMS))
        : nullptr;
    const auto* multiState = multichannel
        ? static_cast<const clap_plugin_state_t*>(
            multichannel->get_extension(multichannel, CLAP_EXT_STATE))
        : nullptr;
    clap_audio_port_info_t multiOutput {};
    clap_audio_ports_config_t multiConfig {};
    ok = ok && multiPorts && multiConfigs && multiParams && multiState
        && multiPorts->get(multichannel, 0u, false, &multiOutput)
        && multiOutput.channel_count == 32u
        && multiOutput.port_type == nullptr
        && multiConfigs->count(multichannel) == 1u
        && multiConfigs->get(multichannel, 0u, &multiConfig)
        && multiConfig.id == 3632u
        && multiConfig.main_output_channel_count == 32u
        && multiParams->count(multichannel) == kParamCount;
    const std::array<const char*, 6u> routingNames {{
        "Output Order", "Voice Output", "Stereo Pair Map", "Output Count",
        "Route On", "Avoid Adjacent",
    }};
    for (uint32_t offset = 0u; ok && offset < routingNames.size(); ++offset) {
        clap_param_info_t info {};
        const uint32_t parameterIndex = offset < 4u
            ? static_cast<uint32_t>(kOriginalStereoParamCount) + offset
            : 40u + offset - 4u;
        const clap_id expectedId = offset < 4u ? 28u + offset
            : 41u + offset - 4u;
        ok = multiParams->get_info(multichannel,
                parameterIndex, &info)
            && info.id == expectedId
            && std::strcmp(info.name, routingNames[offset]) == 0;
    }
    char routingText[64] {};
    double routingValue = -1.0;
    ok = ok && multiParams->value_to_text(multichannel, 28u, 4.0,
            routingText, sizeof(routingText))
        && std::strcmp(routingText, "Random Cycle") == 0
        && multiParams->value_to_text(multichannel, 31u, 8.0,
            routingText, sizeof(routingText))
        && std::strcmp(routingText, "8 CH") == 0
        && multiParams->text_to_value(multichannel, 30u, "Split Banks",
            &routingValue)
        && std::abs(routingValue - 1.0) < 1.0e-9
        && multiParams->text_to_value(multichannel, 41u, "Turn",
            &routingValue)
        && std::abs(routingValue - 1.0) < 1.0e-9;

    auto multiFixture = embeddedFixture();
    ok = ok && multiState->load(multichannel, &multiFixture.stream)
        && multichannel->activate(multichannel, 48000.0, 32u, 256u)
        && multichannel->start_processing(multichannel);
    std::array<std::vector<float>, 32u> multichannelAudio;
    Events monoFirst;
    monoFirst.parameter(28u, 0.0);
    monoFirst.parameter(29u, 0.0);
    monoFirst.parameter(31u, 8.0);
    monoFirst.note(CLAP_EVENT_NOTE_ON, 0, 60);
    ok = ok && processMultichannelBlock(multichannel, 256u,
            &monoFirst.interface, multichannelAudio)
        && maximumMagnitude(multichannelAudio[0u]) > 1.0e-5f
        && maximumMagnitude(multichannelAudio[1u]) < 1.0e-7f;
    Events monoSecond;
    monoSecond.note(CLAP_EVENT_NOTE_ON, 0, 64);
    ok = ok && processMultichannelBlock(multichannel, 256u,
            &monoSecond.interface, multichannelAudio)
        && maximumMagnitude(multichannelAudio[0u]) > 1.0e-5f
        && maximumMagnitude(multichannelAudio[1u]) > 1.0e-5f
        && maximumMagnitude(multichannelAudio[2u]) < 1.0e-7f;
    Events adjacent;
    adjacent.midiCc(0u, 123u, 0u);
    adjacent.parameter(29u, 1.0);
    adjacent.parameter(30u, 0.0);
    adjacent.note(CLAP_EVENT_NOTE_ON, 0, 67);
    ok = ok && processMultichannelBlock(multichannel, 256u,
            &adjacent.interface, multichannelAudio)
        && maximumMagnitude(multichannelAudio[0u]) > 1.0e-5f
        && maximumMagnitude(multichannelAudio[1u]) > 1.0e-5f
        && channelDifference(multichannelAudio[0u],
            multichannelAudio[1u]) > 1.0e-5f
        && maximumMagnitude(multichannelAudio[2u]) < 1.0e-7f;
    Events split;
    split.midiCc(0u, 123u, 0u);
    split.parameter(30u, 1.0);
    split.note(CLAP_EVENT_NOTE_ON, 0, 69);
    ok = ok && processMultichannelBlock(multichannel, 256u,
            &split.interface, multichannelAudio)
        && maximumMagnitude(multichannelAudio[0u]) > 1.0e-5f
        && maximumMagnitude(multichannelAudio[4u]) > 1.0e-5f
        && channelDifference(multichannelAudio[0u],
            multichannelAudio[4u]) > 1.0e-5f;
    Events mchIter;
    mchIter.midiCc(0u, 123u, 0u);
    mchIter.parameter(28u, 0.0);
    mchIter.parameter(29u, 0.0);
    mchIter.parameter(31u, 8.0);
    mchIter.parameter(32u, 6.0);
    mchIter.parameter(34u, 80.0);
    mchIter.parameter(7u, 0.005);
    mchIter.parameter(41u, 2.0);
    mchIter.note(CLAP_EVENT_NOTE_ON, 0, 72);
    std::array<bool, 32u> eventOutputs {};
    for (uint32_t block = 0u; ok && block < 20u; ++block) {
        ok = processMultichannelBlock(multichannel, 256u,
            block == 0u ? &mchIter.interface : nullptr, multichannelAudio);
        for (std::size_t channel = 0u; channel < eventOutputs.size(); ++channel)
            eventOutputs[channel] = eventOutputs[channel]
                || maximumMagnitude(multichannelAudio[channel]) > 1.0e-5f;
    }
    ok = ok && std::count(eventOutputs.begin(), eventOutputs.end(), true)
        >= 4;
    if (multichannel) {
        multichannel->stop_processing(multichannel);
        multichannel->deactivate(multichannel);
        multichannel->destroy(multichannel);
    }
    if (entry) entry->deinit();
    dlclose(library);
    if (!ok) {
        std::cerr << "sample motion CLAP smoke failed\n";
        return 1;
    }
    std::cout << "sample motion family CLAP smoke passed\n";
    return 0;
}
