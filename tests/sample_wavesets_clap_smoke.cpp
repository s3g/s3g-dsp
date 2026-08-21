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
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kStateMagic = 0x57533353u;
constexpr uint32_t kStateVersion = 4u;
constexpr std::size_t kStereoParamCount = 25u;
constexpr std::size_t kParamCount = 29u;
constexpr std::size_t kMaximumPathBytes = 1024u;

struct SavedState {
    uint32_t magic = kStateMagic;
    uint32_t version = kStateVersion;
    uint32_t parameterCount = static_cast<uint32_t>(kParamCount);
    std::array<double, kParamCount> parameters {};
    std::array<char, kMaximumPathBytes> path {};
    uint8_t embedded = 1u;
    uint8_t channelCount = 2u;
    uint8_t reserved0 = 0u;
    uint8_t reserved1 = 0u;
    uint32_t frameCount = 48000u;
    double sampleRate = 48000.0;
};

struct LegacySavedStateV2 {
    uint32_t magic = kStateMagic;
    uint32_t version = 2u;
    uint32_t parameterCount = 20u;
    std::array<double, 20u> parameters {};
    std::array<char, kMaximumPathBytes> path {};
    uint8_t embedded = 0u;
    uint8_t channelCount = 0u;
    uint8_t reserved0 = 0u;
    uint8_t reserved1 = 0u;
    uint32_t frameCount = 0u;
    double sampleRate = 0.0;
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

struct Events {
    std::vector<std::vector<uint8_t>> storage;
    clap_input_events_t interface {
        this,
        [](const clap_input_events_t* list) -> uint32_t {
            const auto* self = static_cast<const Events*>(list->ctx);
            return self ? static_cast<uint32_t>(self->storage.size()) : 0u;
        },
        [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            const auto* self = static_cast<const Events*>(list->ctx);
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

    void note(uint32_t frame, uint16_t type, int16_t channel,
        int16_t key, double velocity, int32_t noteId)
    {
        clap_event_note_t event {};
        event.header.size = sizeof(event);
        event.header.time = frame;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = type;
        event.port_index = 0;
        event.channel = channel;
        event.key = key;
        event.note_id = noteId;
        event.velocity = velocity;
        add(event);
    }

    void midiCc(uint32_t frame, uint8_t channel, uint8_t controller,
        uint8_t value)
    {
        clap_event_midi_t event {};
        event.header.size = sizeof(event);
        event.header.time = frame;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_MIDI;
        event.port_index = 0u;
        event.data[0u] = static_cast<uint8_t>(0xb0u | (channel & 0x0fu));
        event.data[1u] = controller;
        event.data[2u] = value;
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
};

bool processBlock(const clap_plugin_t* plugin, uint32_t frames,
    const clap_input_events_t* events, std::vector<float>& left,
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
    process.in_events = events;
    return plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE;
}

bool processMultichannelBlock(const clap_plugin_t* plugin, uint32_t frames,
    const clap_input_events_t* events,
    std::array<std::vector<float>, 32u>& rendered)
{
    std::array<float*, 32u> channels {};
    for (std::size_t channel = 0u; channel < rendered.size(); ++channel) {
        rendered[channel].assign(frames, 0.0f);
        channels[channel] = rendered[channel].data();
    }
    clap_audio_buffer_t output {};
    output.data32 = channels.data();
    output.channel_count = static_cast<uint32_t>(channels.size());
    clap_process_t process {};
    process.steady_time = -1;
    process.frames_count = frames;
    process.audio_outputs = &output;
    process.audio_outputs_count = 1u;
    process.in_events = events;
    return plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE;
}

float maximumMagnitude(const std::vector<float>& samples)
{
    float maximum = 0.0f;
    for (float sample : samples) maximum = std::max(maximum, std::abs(sample));
    return maximum;
}

float channelDifference(const std::vector<float>& left,
    const std::vector<float>& right)
{
    float difference = 0.0f;
    for (std::size_t index = 0u; index < left.size(); ++index)
        difference += std::abs(left[index] - right[index]);
    return difference;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: s3g_sample_wavesets_clap_smoke "
            "<bundle-or-binary>\n";
        return 2;
    }
    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) return 1;
    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) return 1;
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    bool ok = entry && entry->init(binary.c_str());

    clap_host_t host {};
    host.clap_version = CLAP_VERSION_INIT;
    host.name = "Sample Wavesets 2 smoke";
    host.vendor = "s3g";
    host.url = "https://github.com/s3g/s3g-dsp";
    host.version = "1";
    host.get_extension = hostGetExtension;
    host.request_restart = hostRequest;
    host.request_process = hostRequest;
    host.request_callback = hostRequest;

    const auto* factory = ok ? static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    const auto* descriptor = factory
        ? factory->get_plugin_descriptor(factory, 0u) : nullptr;
    const auto* multichannelDescriptor = factory
        ? factory->get_plugin_descriptor(factory, 1u) : nullptr;
    const clap_plugin_t* plugin = factory ? factory->create_plugin(factory,
        &host, "org.s3g.s3g-dsp.sample-wavesets") : nullptr;
    ok = ok && factory && factory->get_plugin_count(factory) == 2u
        && descriptor && std::strcmp(descriptor->name,
            "s3g Sample Wavesets 2") == 0
        && multichannelDescriptor
        && std::strcmp(multichannelDescriptor->id,
            "org.s3g.s3g-dsp.sample-wavesets-32") == 0
        && std::strcmp(multichannelDescriptor->name,
            "s3g Sample Wavesets 32") == 0
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
    clap_note_port_info_t notePort {};
    ok = ok && ports && notePorts && noteNames && params && state
        && ports->count(plugin, true) == 0u
        && ports->count(plugin, false) == 1u
        && ports->get(plugin, 0u, false, &outputPort)
        && outputPort.channel_count == 2u
        && notePorts->count(plugin, true) == 1u
        && notePorts->get(plugin, 0u, true, &notePort)
        && (notePort.supported_dialects & CLAP_NOTE_DIALECT_MIDI) != 0u
        && noteNames->count(plugin) == 0u
        && params->count(plugin) == kStereoParamCount;
#if defined(__APPLE__)
    ok = ok && gui;
#else
    (void)gui;
#endif

    const std::array<const char*, kStereoParamCount> expected {{
        "Out", "Play Mode", "Start", "End", "Loop Start", "Loop End",
        "Stereo Source", "Voice Mode", "Trigger", "Root Note", "Tune",
        "Fine Tune", "Attack", "Release", "Velocity", "Time", "Group",
        "Repeat", "Stride", "Order", "Process", "Depth", "Join",
        "Crossing Detail", "MIDI Receive",
    }};
    for (uint32_t index = 0u; ok && index < kStereoParamCount; ++index) {
        clap_param_info_t info {};
        ok = params->get_info(plugin, index, &info)
            && info.id == index + 1u
            && std::strcmp(info.name, expected[index]) == 0
            && (info.id != 21u || std::abs(info.max_value - 11.0) < 1.0e-9);
    }
    char processText[64] {};
    double processValue = -1.0;
    ok = ok && params->value_to_text(plugin, 21u, 8.0,
            processText, sizeof(processText))
        && std::strcmp(processText, "Harmonic") == 0
        && params->text_to_value(plugin, 21u, "Group Reverse", &processValue)
        && std::abs(processValue - 9.0) < 1.0e-9
        && !params->get_value(plugin, 26u, &processValue);

    SavedState saved;
    saved.parameters = {{
        -6.0, 1.0, 0.0, 1.0, 0.0, 1.0, 3.0, 0.0, 0.0, 60.0,
        0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 3.0, 2.0, 1.0, 0.0,
        0.0, 0.0, 1.0, 3.0, 0.0,
        0.0, 0.0, 0.0, 32.0,
    }};
    std::array<std::vector<float>, 2u> samples;
    for (auto& channel : samples) channel.resize(saved.frameCount);
    constexpr double pi = 3.14159265358979323846;
    for (uint32_t frame = 0u; frame < saved.frameCount; ++frame) {
        const double time = frame / saved.sampleRate;
        samples[0u][frame] = static_cast<float>(
            0.7 * std::sin(2.0 * pi * 220.0 * time));
        samples[1u][frame] = static_cast<float>(
            0.55 * std::sin(2.0 * pi * 331.0 * time + 0.2));
    }
    MemoryInput input;
    input.bytes.resize(sizeof(saved)
        + samples[0u].size() * sizeof(float) * 2u);
    std::memcpy(input.bytes.data(), &saved, sizeof(saved));
    std::memcpy(input.bytes.data() + sizeof(saved), samples[0u].data(),
        samples[0u].size() * sizeof(float));
    std::memcpy(input.bytes.data() + sizeof(saved)
            + samples[0u].size() * sizeof(float),
        samples[1u].data(), samples[1u].size() * sizeof(float));
    ok = ok && state->load(plugin, &input.stream)
        && plugin->activate(plugin, 48000.0, 1u, 256u)
        && plugin->start_processing(plugin);

    std::vector<float> left;
    std::vector<float> right;
    Events root;
    root.note(0u, CLAP_EVENT_NOTE_ON, 0, 60, 1.0, 1);
    ok = ok && processBlock(plugin, 256u, &root.interface, left, right)
        && maximumMagnitude(left) > 0.02f
        && maximumMagnitude(right) > 0.02f
        && channelDifference(left, right) > 0.1f;

    Events leftMode;
    leftMode.parameter(7u, 0.0);
    leftMode.midiCc(0u, 0u, 123u, 0u);
    leftMode.note(0u, CLAP_EVENT_NOTE_ON, 0, 60, 1.0, 2);
    ok = ok && processBlock(plugin, 256u, &leftMode.interface, left, right)
        && channelDifference(left, right) < 1.0e-5f;

    Events controls;
    controls.midiCc(0u, 0u, 16u, 32u);
    controls.midiCc(0u, 0u, 17u, 120u);
    controls.midiCc(0u, 0u, 18u, 48u);
    controls.midiCc(0u, 0u, 19u, 96u);
    controls.midiCc(0u, 0u, 22u, 100u);
    ok = ok && processBlock(plugin, 64u, &controls.interface, left, right);
    double start = 0.0, end = 0.0, loopStart = 0.0, loopEnd = 0.0,
        depth = 0.0;
    ok = ok && params->get_value(plugin, 3u, &start)
        && params->get_value(plugin, 4u, &end)
        && params->get_value(plugin, 5u, &loopStart)
        && params->get_value(plugin, 6u, &loopEnd)
        && params->get_value(plugin, 22u, &depth)
        && start > 0.24 && start < 0.26
        && end > 0.94 && loopStart > 0.37 && loopEnd > 0.75
        && depth > 0.78 && depth < 0.80;

    Events channelTwo;
    channelTwo.midiCc(0u, 0u, 123u, 0u);
    channelTwo.parameter(25u, 2.0);
    ok = ok && processBlock(plugin, 64u, &channelTwo.interface, left, right);
    Events ignored;
    ignored.note(0u, CLAP_EVENT_NOTE_ON, 0, 60, 1.0, 3);
    ok = ok && processBlock(plugin, 256u, &ignored.interface, left, right)
        && maximumMagnitude(left) < 1.0e-7f;
    Events accepted;
    accepted.note(0u, CLAP_EVENT_NOTE_ON, 1, 60, 1.0, 4);
    ok = ok && processBlock(plugin, 256u, &accepted.interface, left, right)
        && maximumMagnitude(left) > 0.01f;

    MemoryOutput output;
    ok = ok && state->save(plugin, &output.stream)
        && output.bytes.size() == sizeof(SavedState)
            + samples[0u].size() * sizeof(float) * 2u;
    if (ok) {
        SavedState roundTrip;
        std::memcpy(&roundTrip, output.bytes.data(), sizeof(roundTrip));
        ok = roundTrip.magic == kStateMagic
            && roundTrip.version == kStateVersion
            && roundTrip.parameterCount == kParamCount
            && roundTrip.channelCount == 2u
            && roundTrip.frameCount == saved.frameCount
            && std::abs(roundTrip.parameters[24u] - 2.0) < 1.0e-9;
    }

    if (plugin) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        LegacySavedStateV2 legacy;
        legacy.parameters = {{
            -9.0, 0.25, 2.0, 2.0, 4.0, 5.0, -3.0, 1.0, 4.0,
            0.65, 0.4, 2.0, 7.0, 0.0, 0.22, 0.5, -4.0, 0.7, 2.0,
            -8.0,
        }};
        MemoryInput legacyInput;
        legacyInput.bytes.resize(sizeof(legacy));
        std::memcpy(legacyInput.bytes.data(), &legacy, sizeof(legacy));
        double migratedStart = 0.0, migratedStride = 0.0,
            migratedSource = 0.0, migratedMidi = 0.0;
        ok = ok && state->load(plugin, &legacyInput.stream)
            && params->get_value(plugin, 3u, &migratedStart)
            && params->get_value(plugin, 7u, &migratedSource)
            && params->get_value(plugin, 19u, &migratedStride)
            && params->get_value(plugin, 25u, &migratedMidi)
            && std::abs(migratedStart - 0.22) < 1.0e-9
            && std::abs(migratedSource - 3.0) < 1.0e-9
            && std::abs(migratedStride - 3.0) < 1.0e-9
            && std::abs(migratedMidi - 7.0) < 1.0e-9;
        plugin->destroy(plugin);
    }

    const clap_plugin_t* multichannel = factory ? factory->create_plugin(
        factory, &host, "org.s3g.s3g-dsp.sample-wavesets-32") : nullptr;
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
        && multiConfig.id == 3432u
        && multiConfig.main_output_channel_count == 32u
        && multiParams->count(multichannel) == kParamCount;
    const std::array<const char*, 4u> routingNames {{
        "Output Order", "Voice Output", "Stereo Pair Map", "Output Count",
    }};
    for (uint32_t offset = 0u; ok && offset < routingNames.size(); ++offset) {
        clap_param_info_t info {};
        ok = multiParams->get_info(multichannel,
                static_cast<uint32_t>(kStereoParamCount) + offset, &info)
            && info.id == 26u + offset
            && std::strcmp(info.name, routingNames[offset]) == 0
            && (info.id != 29u || (std::abs(info.min_value - 2.0) < 1.0e-9
                && std::abs(info.max_value - 32.0) < 1.0e-9));
    }
    char routingText[64] {};
    double routingValue = -1.0;
    ok = ok && multiParams->value_to_text(multichannel, 26u, 4.0,
            routingText, sizeof(routingText))
        && std::strcmp(routingText, "Random Cycle") == 0
        && multiParams->value_to_text(multichannel, 29u, 8.0,
            routingText, sizeof(routingText))
        && std::strcmp(routingText, "8 CH") == 0
        && multiParams->text_to_value(multichannel, 28u, "Split Banks",
            &routingValue)
        && std::abs(routingValue - 1.0) < 1.0e-9;
    char groupText[64] {};
    double groupValue = -1.0;
    char rootText[64] {};
    double rootValue = -1.0;
    ok = ok && multiParams->value_to_text(multichannel, 17u, 0.0,
            groupText, sizeof(groupText))
        && multiParams->text_to_value(multichannel, 17u, groupText,
            &groupValue)
        && std::abs(groupValue) < 1.0e-9
        && multiParams->value_to_text(multichannel, 10u, 60.0,
            rootText, sizeof(rootText))
        && multiParams->text_to_value(multichannel, 10u, rootText,
            &rootValue)
        && std::abs(rootValue - 60.0) < 1.0e-9;

    MemoryInput multiInput;
    multiInput.bytes = input.bytes;
    ok = ok && multiState->load(multichannel, &multiInput.stream)
        && multichannel->activate(multichannel, 48000.0, 1u, 256u)
        && multichannel->start_processing(multichannel);
    std::array<std::vector<float>, 32u> multichannelAudio;
    Events monoFirst;
    monoFirst.parameter(8u, 1.0);
    monoFirst.parameter(26u, 0.0);
    monoFirst.parameter(27u, 0.0);
    monoFirst.parameter(29u, 8.0);
    monoFirst.note(0u, CLAP_EVENT_NOTE_ON, 0, 60, 1.0, 101);
    ok = ok && processMultichannelBlock(multichannel, 256u,
            &monoFirst.interface, multichannelAudio)
        && maximumMagnitude(multichannelAudio[0u]) > 0.01f
        && maximumMagnitude(multichannelAudio[1u]) < 1.0e-7f;
    Events monoSecond;
    monoSecond.note(0u, CLAP_EVENT_NOTE_ON, 0, 64, 1.0, 102);
    ok = ok && processMultichannelBlock(multichannel, 256u,
            &monoSecond.interface, multichannelAudio)
        && maximumMagnitude(multichannelAudio[0u]) < 1.0e-7f
        && maximumMagnitude(multichannelAudio[1u]) > 0.01f
        && maximumMagnitude(multichannelAudio[2u]) < 1.0e-7f
        && maximumMagnitude(multichannelAudio[8u]) < 1.0e-7f;

    Events adjacent;
    adjacent.parameter(27u, 1.0);
    adjacent.parameter(28u, 0.0);
    adjacent.note(0u, CLAP_EVENT_NOTE_ON, 0, 67, 1.0, 103);
    ok = ok && processMultichannelBlock(multichannel, 256u,
            &adjacent.interface, multichannelAudio)
        && maximumMagnitude(multichannelAudio[0u]) > 0.01f
        && maximumMagnitude(multichannelAudio[1u]) > 0.01f
        && channelDifference(multichannelAudio[0u], multichannelAudio[1u])
            > 0.1f
        && maximumMagnitude(multichannelAudio[2u]) < 1.0e-7f;

    Events split;
    split.parameter(28u, 1.0);
    split.note(0u, CLAP_EVENT_NOTE_ON, 0, 69, 1.0, 104);
    ok = ok && processMultichannelBlock(multichannel, 256u,
            &split.interface, multichannelAudio)
        && maximumMagnitude(multichannelAudio[0u]) > 0.01f
        && maximumMagnitude(multichannelAudio[4u]) > 0.01f
        && channelDifference(multichannelAudio[0u], multichannelAudio[4u])
            > 0.1f
        && maximumMagnitude(multichannelAudio[1u]) < 1.0e-7f;
    if (multichannel) {
        multichannel->stop_processing(multichannel);
        multichannel->deactivate(multichannel);
        multichannel->destroy(multichannel);
    }
    if (entry) entry->deinit();
    dlclose(library);
    if (!ok) {
        std::cerr << "Sample Wavesets family CLAP smoke failed\n";
        return 1;
    }
    std::cout << "s3g Sample Wavesets family CLAP smoke: ok\n";
    return 0;
}
