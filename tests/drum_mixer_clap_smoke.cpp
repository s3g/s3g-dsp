#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
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
#include <vector>

namespace {

constexpr clap_id kOutputModeParamId = 1u;
constexpr clap_id kMasterLevelParamId = 2u;
constexpr clap_id kBusEnabledParamId = 3u;
constexpr clap_id kBusDriveParamId = 4u;
constexpr clap_id kBusReturnParamId = 9u;
constexpr clap_id kLaneOneLevelParamId = 100u;
constexpr clap_id kLaneOnePanParamId = 101u;
constexpr clap_id kLaneOneAuxParamId = 105u;
constexpr clap_id kLaneOneMuteParamId = 106u;
constexpr clap_id kLaneOneSoloParamId = 107u;
constexpr clap_id kLaneOneMidFrequencyParamId = 108u;
constexpr clap_id kLaneThreeSoloParamId = 139u;
constexpr clap_id kLaneEightHighParamId = 216u;
constexpr uint32_t kChannels = 16u;
constexpr uint32_t kFrames = 256u;
constexpr uint32_t kParamCount = 81u;
constexpr uint32_t kStateMagic = 0x5333444du;

struct LegacyLaneParamsV1 {
    float levelDb = 0.0f;
    float pan = 0.0f;
    float lowEqDb = 0.0f;
    float midEqDb = 0.0f;
    float highEqDb = 0.0f;
    float auxSend = 0.0f;
    bool mute = false;
    bool solo = false;
};

struct LegacyParamsV1 {
    std::array<LegacyLaneParamsV1, 8u> lanes {};
    uint32_t outputMode = 0u;
    float masterLevelDb = -6.0f;
    bool busEnabled = false;
    float busDrive = 0.38f;
    float busGlue = 0.34f;
    float busRoom = 0.0f;
    float busWeight = 0.68f;
    float busTone = 0.0f;
    float busReturnDb = -9.0f;
};

struct EventList {
    std::vector<clap_event_param_value_t> events;
    clap_input_events_t input {
        this,
        [](const clap_input_events_t* list) -> uint32_t {
            return static_cast<uint32_t>(
                static_cast<const EventList*>(list->ctx)->events.size());
        },
        [](const clap_input_events_t* list,
            uint32_t index) -> const clap_event_header_t* {
            const auto* self = static_cast<const EventList*>(list->ctx);
            return index < self->events.size()
                ? &self->events[index].header : nullptr;
        },
    };

    void add(clap_id id, double value)
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
        events.push_back(event);
    }
};

struct MemoryState {
    std::vector<uint8_t> bytes;
    size_t position = 0u;
};

int64_t stateWrite(const clap_ostream_t* stream,
    const void* source, uint64_t requested)
{
    auto* state = static_cast<MemoryState*>(stream->ctx);
    if (!state || (!source && requested > 0u)) return -1;
    const size_t count = std::min<size_t>(
        static_cast<size_t>(requested), 11u);
    const auto* bytes = static_cast<const uint8_t*>(source);
    state->bytes.insert(state->bytes.end(), bytes, bytes + count);
    return static_cast<int64_t>(count);
}

int64_t stateRead(const clap_istream_t* stream,
    void* destination, uint64_t requested)
{
    auto* state = static_cast<MemoryState*>(stream->ctx);
    if (!state || (!destination && requested > 0u)) return -1;
    const size_t available = state->position < state->bytes.size()
        ? state->bytes.size() - state->position : 0u;
    const size_t count = std::min<size_t>({ available,
        static_cast<size_t>(requested), 7u });
    if (count > 0u) {
        std::memcpy(destination,
            state->bytes.data() + state->position, count);
        state->position += count;
    }
    return static_cast<int64_t>(count);
}

std::filesystem::path resolveBinary(const std::filesystem::path& supplied)
{
    if (std::filesystem::is_regular_file(supplied)) return supplied;
#if defined(__APPLE__)
    if (std::filesystem::is_directory(supplied)) {
        const auto directory = supplied / "Contents" / "MacOS";
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file()) return entry.path();
        }
    }
#endif
    return {};
}

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequest(const clap_host_t*) {}

bool hasFeature(const clap_plugin_descriptor_t* descriptor,
    const char* expected)
{
    if (!descriptor || !descriptor->features) return false;
    for (const char* const* feature = descriptor->features;
         *feature; ++feature) {
        if (std::strcmp(*feature, expected) == 0) return true;
    }
    return false;
}

bool getParam(const clap_plugin_t* plugin,
    const clap_plugin_params_t* params, clap_id id,
    double expected, double tolerance = 1.0e-6)
{
    double actual = 0.0;
    return params->get_value(plugin, id, &actual)
        && std::abs(actual - expected) <= tolerance;
}

bool audioPortProbe(const clap_plugin_t* plugin)
{
    const auto* ports = static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    if (!ports || ports->count(plugin, true) != 1u
        || ports->count(plugin, false) != 1u) return false;
    clap_audio_port_info_t input {};
    clap_audio_port_info_t output {};
    return ports->get(plugin, 0u, true, &input)
        && ports->get(plugin, 0u, false, &output)
        && input.channel_count == kChannels
        && output.channel_count == kChannels
        && input.port_type == nullptr && output.port_type == nullptr
        && (input.flags & CLAP_AUDIO_PORT_IS_MAIN) != 0u
        && (output.flags & CLAP_AUDIO_PORT_IS_MAIN) != 0u
        && input.in_place_pair == output.id
        && output.in_place_pair == input.id;
}

bool parameterProbe(const clap_plugin_t* plugin,
    const clap_plugin_params_t* params)
{
    if (!params || params->count(plugin) != kParamCount) return false;
    bool sawOutputMode = false;
    bool sawLaneEightSolo = false;
    bool sawLaneEightMidFrequency = false;
    for (uint32_t index = 0u; index < kParamCount; ++index) {
        clap_param_info_t info {};
        if (!params->get_info(plugin, index, &info)
            || (info.flags & CLAP_PARAM_IS_AUTOMATABLE) == 0u) return false;
        if (info.id == kOutputModeParamId) {
            sawOutputMode = (info.flags & CLAP_PARAM_IS_STEPPED) != 0u
                && info.min_value == 0.0 && info.max_value == 1.0;
        }
        if (info.id == 219u) sawLaneEightSolo = true;
        if (info.id == 220u) {
            sawLaneEightMidFrequency = std::strcmp(
                    info.name, "Lane 8 Mid Frequency") == 0
                && info.min_value == 120.0 && info.max_value == 8000.0
                && info.default_value == 900.0;
        }
    }
    char sum[32] {};
    char direct[32] {};
    double parsed = 0.0;
    char frequency[32] {};
    return sawOutputMode && sawLaneEightSolo && sawLaneEightMidFrequency
        && params->value_to_text(plugin, kOutputModeParamId,
            0.0, sum, sizeof(sum))
        && params->value_to_text(plugin, kOutputModeParamId,
            1.0, direct, sizeof(direct))
        && std::strcmp(sum, "SUM") == 0
        && std::strcmp(direct, "DIRECT") == 0
        && params->text_to_value(plugin, kMasterLevelParamId,
            "-INF", &parsed) && parsed == -60.0
        && params->text_to_value(plugin, kLaneOnePanParamId,
            "L50", &parsed) && std::abs(parsed + 0.5) < 1.0e-9
        && params->text_to_value(plugin, kLaneOnePanParamId,
            "R25", &parsed) && std::abs(parsed - 0.25) < 1.0e-9
        && params->text_to_value(plugin, kLaneOnePanParamId,
            "C", &parsed) && parsed == 0.0
        && params->text_to_value(plugin, kBusDriveParamId,
            "75%", &parsed) && std::abs(parsed - 0.75) < 1.0e-9
        && params->value_to_text(plugin, kLaneOneMidFrequencyParamId,
            1200.0, frequency, sizeof(frequency))
        && std::strcmp(frequency, "1.20 kHz") == 0
        && params->text_to_value(plugin, kLaneOneMidFrequencyParamId,
            "2.5 kHz", &parsed) && std::abs(parsed - 2500.0) < 1.0e-9;
}

struct AudioBlock {
    std::array<std::array<float, kFrames>, kChannels> input {};
    std::array<std::array<float, kFrames>, kChannels> output {};
    std::array<float*, kChannels> inputPointers {};
    std::array<float*, kChannels> outputPointers {};
    clap_audio_buffer_t inputBuffer {};
    clap_audio_buffer_t outputBuffer {};
    clap_process_t process {};

    AudioBlock()
    {
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            inputPointers[channel] = input[channel].data();
            outputPointers[channel] = output[channel].data();
        }
        inputBuffer.data32 = inputPointers.data();
        inputBuffer.channel_count = kChannels;
        outputBuffer.data32 = outputPointers.data();
        outputBuffer.channel_count = kChannels;
        process.frames_count = kFrames;
        process.audio_inputs = &inputBuffer;
        process.audio_inputs_count = 1u;
        process.audio_outputs = &outputBuffer;
        process.audio_outputs_count = 1u;
    }

    void clearOutput()
    {
        for (auto& channel : output) channel.fill(0.0f);
    }
};

struct AudioBlock64 {
    std::array<std::array<double, kFrames>, kChannels> input {};
    std::array<std::array<double, kFrames>, kChannels> output {};
    std::array<double*, kChannels> inputPointers {};
    std::array<double*, kChannels> outputPointers {};
    clap_audio_buffer_t inputBuffer {};
    clap_audio_buffer_t outputBuffer {};
    clap_process_t process {};

    AudioBlock64()
    {
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            inputPointers[channel] = input[channel].data();
            outputPointers[channel] = output[channel].data();
        }
        inputBuffer.data64 = inputPointers.data();
        inputBuffer.channel_count = kChannels;
        outputBuffer.data64 = outputPointers.data();
        outputBuffer.channel_count = kChannels;
        process.frames_count = kFrames;
        process.audio_inputs = &inputBuffer;
        process.audio_inputs_count = 1u;
        process.audio_outputs = &outputBuffer;
        process.audio_outputs_count = 1u;
    }
};

bool processProbe(const clap_plugin_t* plugin)
{
    AudioBlock block;
    for (uint32_t frame = 0u; frame < kFrames; ++frame) {
        const float phase = static_cast<float>(frame) / kFrames;
        block.input[4u][frame] = 0.20f * std::sin(phase * 12.0f);
        block.input[5u][frame] = 0.15f * std::cos(phase * 9.0f);
    }

    EventList direct;
    direct.add(kOutputModeParamId, 1.0);
    direct.add(kMasterLevelParamId, -60.0);
    block.process.in_events = &direct.input;
    if (plugin->process(plugin, &block.process) != CLAP_PROCESS_CONTINUE) {
        return false;
    }
    for (uint32_t channel = 0u; channel < kChannels; ++channel) {
        for (uint32_t frame = 0u; frame < kFrames; ++frame) {
            if (!std::isfinite(block.output[channel][frame])) return false;
            const float expected = channel == 4u || channel == 5u
                ? block.input[channel][frame] : 0.0f;
            if (std::abs(block.output[channel][frame] - expected) > 1.0e-5f) {
                std::cerr << "DIRECT pair isolation failed\n";
                return false;
            }
        }
    }

    EventList summed;
    summed.add(kOutputModeParamId, 0.0);
    summed.add(kMasterLevelParamId, 0.0);
    block.process.in_events = &summed.input;
    for (uint32_t pass = 0u; pass < 8u; ++pass) {
        block.clearOutput();
        if (plugin->process(plugin, &block.process) != CLAP_PROCESS_CONTINUE) {
            return false;
        }
        block.process.in_events = nullptr;
    }
    double mainEnergy = 0.0;
    for (uint32_t frame = 0u; frame < kFrames; ++frame) {
        mainEnergy += block.output[0u][frame] * block.output[0u][frame]
            + block.output[1u][frame] * block.output[1u][frame];
    }
    if (!(mainEnergy > 0.01)) {
        std::cerr << "SUM produced no stereo master\n";
        return false;
    }
    for (uint32_t channel = 2u; channel < kChannels; ++channel) {
        for (float value : block.output[channel]) {
            if (value != 0.0f) {
                std::cerr << "SUM leaked into outputs 3-16\n";
                return false;
            }
        }
    }

    EventList solo;
    solo.add(kLaneOneSoloParamId, 1.0);
    block.process.in_events = &solo.input;
    for (uint32_t pass = 0u; pass < 16u; ++pass) {
        block.clearOutput();
        if (plugin->process(plugin, &block.process) != CLAP_PROCESS_CONTINUE) {
            return false;
        }
        block.process.in_events = nullptr;
    }
    double soloEnergy = 0.0;
    for (uint32_t frame = 0u; frame < kFrames; ++frame) {
        soloEnergy += block.output[0u][frame] * block.output[0u][frame]
            + block.output[1u][frame] * block.output[1u][frame];
    }
    if (soloEnergy > 1.0e-10) {
        std::cerr << "Solo did not suppress the unselected lane\n";
        return false;
    }

    EventList bus;
    bus.add(kLaneOneSoloParamId, 0.0);
    bus.add(kLaneOneMuteParamId, 0.0);
    bus.add(kLaneOneAuxParamId, 1.0);
    bus.add(kBusEnabledParamId, 1.0);
    bus.add(kBusDriveParamId, 0.9);
    bus.add(kBusReturnParamId, 0.0);
    // Move the test signal to lane one so its post-fader AUX is populated.
    block.input[0u] = block.input[4u];
    block.input[1u] = block.input[5u];
    block.input[4u].fill(0.0f);
    block.input[5u].fill(0.0f);
    block.process.in_events = &bus.input;
    block.clearOutput();
    if (plugin->process(plugin, &block.process) != CLAP_PROCESS_CONTINUE) {
        return false;
    }
    double busDifference = 0.0;
    for (uint32_t frame = 0u; frame < kFrames; ++frame) {
        const double leftDifference = block.output[0u][frame]
            - block.input[0u][frame];
        const double rightDifference = block.output[1u][frame]
            - block.input[1u][frame];
        busDifference += leftDifference * leftDifference
            + rightDifference * rightDifference;
    }
    if (!(busDifference > 1.0e-4)) {
        std::cerr << "Enabled AUX drum bus produced no audible return\n";
        return false;
    }
    for (const auto& channel : block.output) {
        for (float value : channel) {
            if (!std::isfinite(value)) return false;
        }
    }
    return true;
}

bool process64Probe(const clap_plugin_t* plugin)
{
    AudioBlock64 block;
    for (uint32_t frame = 0u; frame < kFrames; ++frame) {
        const double phase = static_cast<double>(frame) / kFrames;
        block.input[6u][frame] = 0.18 * std::sin(phase * 11.0);
        block.input[7u][frame] = 0.12 * std::cos(phase * 7.0);
    }
    EventList direct;
    direct.add(kOutputModeParamId, 1.0);
    block.process.in_events = &direct.input;
    for (uint32_t pass = 0u; pass < 16u; ++pass) {
        if (plugin->process(plugin, &block.process) != CLAP_PROCESS_CONTINUE) {
            return false;
        }
        block.process.in_events = nullptr;
    }
    for (uint32_t channel = 0u; channel < kChannels; ++channel) {
        for (uint32_t frame = 0u; frame < kFrames; ++frame) {
            const double actual = block.output[channel][frame];
            const double expected = channel == 6u || channel == 7u
                ? block.input[channel][frame] : 0.0;
            if (!std::isfinite(actual) || std::abs(actual - expected) > 1.0e-5) {
                std::cerr << "64-bit DIRECT processing failed\n";
                return false;
            }
        }
    }
    return true;
}

bool stateProbe(const clap_plugin_t* plugin,
    const clap_plugin_params_t* params,
    const clap_plugin_state_t* state)
{
    EventList chosen;
    chosen.add(kOutputModeParamId, 1.0);
    chosen.add(kMasterLevelParamId, -13.5);
    chosen.add(kLaneOneLevelParamId, -7.25);
    chosen.add(kLaneOneMidFrequencyParamId, 2450.0);
    chosen.add(kLaneOneMuteParamId, 1.0);
    params->flush(plugin, &chosen.input, nullptr);

    MemoryState memory;
    clap_ostream_t output { &memory, stateWrite };
    if (!state->save(plugin, &output) || memory.bytes.empty()) return false;

    EventList changed;
    changed.add(kOutputModeParamId, 0.0);
    changed.add(kMasterLevelParamId, 3.0);
    changed.add(kLaneOneLevelParamId, 2.0);
    changed.add(kLaneOneMidFrequencyParamId, 480.0);
    changed.add(kLaneOneMuteParamId, 0.0);
    params->flush(plugin, &changed.input, nullptr);

    clap_istream_t input { &memory, stateRead };
    return state->load(plugin, &input)
        && getParam(plugin, params, kOutputModeParamId, 1.0)
        && getParam(plugin, params, kMasterLevelParamId, -13.5)
        && getParam(plugin, params, kLaneOneLevelParamId, -7.25)
        && getParam(plugin, params, kLaneOneMidFrequencyParamId, 2450.0)
        && getParam(plugin, params, kLaneOneMuteParamId, 1.0);
}

bool legacyStateProbe(const clap_plugin_t* plugin,
    const clap_plugin_params_t* params,
    const clap_plugin_state_t* state)
{
    const uint32_t version = 1u;
    LegacyParamsV1 legacy;
    legacy.outputMode = 1u;
    legacy.masterLevelDb = -11.0f;
    legacy.lanes[7u].highEqDb = 4.5f;
    MemoryState memory;
    memory.bytes.resize(sizeof(kStateMagic) + sizeof(version) + sizeof(legacy));
    size_t offset = 0u;
    std::memcpy(memory.bytes.data() + offset,
        &kStateMagic, sizeof(kStateMagic));
    offset += sizeof(kStateMagic);
    std::memcpy(memory.bytes.data() + offset, &version, sizeof(version));
    offset += sizeof(version);
    std::memcpy(memory.bytes.data() + offset, &legacy, sizeof(legacy));
    clap_istream_t input { &memory, stateRead };
    return state->load(plugin, &input)
        && memory.position == memory.bytes.size()
        && getParam(plugin, params, kOutputModeParamId, 1.0)
        && getParam(plugin, params, kMasterLevelParamId, -11.0)
        && getParam(plugin, params, kLaneEightHighParamId, 4.5)
        && getParam(plugin, params, kLaneOneMidFrequencyParamId, 900.0);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: s3g_drum_mixer_clap_smoke "
                  << "<bundle-or-binary> <plugin-id>\n";
        return 2;
    }
    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "Could not resolve Drum Mixer binary\n";
        return 1;
    }
    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "Could not load Drum Mixer: " << dlerror() << "\n";
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    bool ok = entry && entry->init(binary.c_str());

    clap_host_t host {};
    host.clap_version = CLAP_VERSION_INIT;
    host.name = "Drum Mixer smoke";
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
    ok = ok && factory && factory->get_plugin_count(factory) == 1u
        && descriptor && std::strcmp(descriptor->id, argv[2]) == 0
        && std::strcmp(descriptor->name, "s3g Drum Mixer 16") == 0
        && hasFeature(descriptor, CLAP_PLUGIN_FEATURE_MIXING)
        && hasFeature(descriptor, CLAP_PLUGIN_FEATURE_UTILITY);

    const clap_plugin_t* plugin = ok
        ? factory->create_plugin(factory, &host, argv[2]) : nullptr;
    ok = ok && plugin && plugin->init(plugin);
    const auto* params = ok ? static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS)) : nullptr;
    const auto* state = ok ? static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE)) : nullptr;
    ok = ok && params && state && audioPortProbe(plugin)
        && parameterProbe(plugin, params)
        && stateProbe(plugin, params, state)
        && legacyStateProbe(plugin, params, state)
        && plugin->activate(plugin, 48000.0, 1u, kFrames)
        && plugin->start_processing(plugin)
        && processProbe(plugin)
        && process64Probe(plugin);

    if (plugin) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }
    if (entry && entry->deinit) entry->deinit();
    dlclose(library);
    if (!ok) {
        std::cerr << "Drum Mixer CLAP smoke failed\n";
        return 1;
    }
    std::cout << "s3g Drum Mixer 16 CLAP smoke passed\n";
    return 0;
}
