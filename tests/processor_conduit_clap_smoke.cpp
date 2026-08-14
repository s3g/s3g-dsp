#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/audio-ports-config.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/tail.h>

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

constexpr clap_id kMaterialParamId = 1u;
constexpr clap_id kInputParamId = 2u;
constexpr clap_id kMixParamId = 10u;
constexpr clap_id kPedalParamId = 12u;
constexpr clap_id kPedalDriveParamId = 13u;
constexpr clap_id kOctaveDownParamId = 15u;
constexpr clap_id kPaDriveParamId = 17u;
constexpr clap_id kMicMotionParamId = 18u;
constexpr clap_id kChamberParamId = 19u;
constexpr clap_id kStereoWidthParamId = 20u;
constexpr clap_id kPedalPositionParamId = 21u;
constexpr clap_id kPedalMixParamId = 22u;
constexpr clap_id kInputListenParamId = 23u;
constexpr clap_id kMonoInputConfigId = 100u;
constexpr clap_id kStereoInputConfigId = 101u;
constexpr uint32_t kMaterialCount = 26u;
constexpr uint32_t kPedalCount = 8u;

struct LegacyParamsV1 {
    uint32_t material = 0u;
    float inputGainDb = 6.0f;
    float driver = 0.45f;
    float size = 0.58f;
    float tension = 0.48f;
    float damping = 0.38f;
    float pickup = 0.72f;
    float contact = 0.58f;
    float feedback = 0.16f;
    float mix = 0.82f;
    float outputGainDb = -6.0f;
};

struct LegacyStateV1 {
    uint32_t version = 1u;
    LegacyParamsV1 params {};
};

static_assert(sizeof(LegacyStateV1) == 48u,
    "Unexpected Processor Conduit v1 test state layout.");

struct LegacyParamsV2 {
    uint32_t material = 0u;
    float inputGainDb = 6.0f;
    float driver = 0.45f;
    float size = 0.58f;
    float tension = 0.48f;
    float damping = 0.38f;
    float pickup = 0.72f;
    float contact = 0.58f;
    float feedback = 0.16f;
    float mix = 0.82f;
    float outputGainDb = -6.0f;
    uint32_t pedal = 0u;
    float pedalDrive = 0.36f;
    float pedalTone = 0.55f;
    float octaveDown = 0.0f;
    float octaveDrag = 0.68f;
};

struct LegacyStateV2 {
    uint32_t version = 2u;
    LegacyParamsV2 params {};
};

static_assert(sizeof(LegacyStateV2) == 68u,
    "Unexpected Processor Conduit v2 test state layout.");

struct LegacyParamsV3 {
    uint32_t material = 0u;
    float inputGainDb = 6.0f;
    float driver = 0.45f;
    float size = 0.58f;
    float tension = 0.48f;
    float damping = 0.38f;
    float pickup = 0.72f;
    float contact = 0.58f;
    float feedback = 0.16f;
    float mix = 0.82f;
    float outputGainDb = -6.0f;
    uint32_t pedal = 0u;
    float pedalDrive = 0.36f;
    float pedalTone = 0.55f;
    float octaveDown = 0.0f;
    float octaveDrag = 0.68f;
    float paDrive = 0.46f;
    float micMotion = 0.32f;
    float chamber = 0.62f;
    float stereoWidth = 0.68f;
};

struct LegacyStateV3 {
    uint32_t version = 3u;
    LegacyParamsV3 params {};
};

static_assert(sizeof(LegacyStateV3) == 84u,
    "Unexpected Processor Conduit v3 test state layout.");

struct LegacyParamsV4 {
    uint32_t material = 0u;
    float inputGainDb = 6.0f;
    float driver = 0.45f;
    float size = 0.58f;
    float tension = 0.48f;
    float damping = 0.38f;
    float pickup = 0.72f;
    float contact = 0.58f;
    float feedback = 0.16f;
    float mix = 0.82f;
    float outputGainDb = -6.0f;
    uint32_t pedal = 0u;
    float pedalDrive = 0.36f;
    float pedalTone = 0.55f;
    float octaveDown = 0.0f;
    float octaveDrag = 0.68f;
    float paDrive = 0.46f;
    float micMotion = 0.32f;
    float chamber = 0.62f;
    float stereoWidth = 0.68f;
    uint32_t pedalPosition = 0u;
    float pedalMix = 0.60f;
};

struct LegacyStateV4 {
    uint32_t version = 4u;
    LegacyParamsV4 params {};
};

static_assert(sizeof(LegacyStateV4) == 92u,
    "Unexpected Processor Conduit v4 test state layout.");

struct MemoryState {
    std::vector<uint8_t> bytes;
    size_t offset = 0u;
};

int64_t stateWrite(const clap_ostream_t* stream,
    const void* source, uint64_t requested)
{
    auto* state = static_cast<MemoryState*>(stream->ctx);
    if (!state || (!source && requested > 0u)) return -1;
    const size_t count = std::min<size_t>(
        static_cast<size_t>(requested), 9u);
    const auto* bytes = static_cast<const uint8_t*>(source);
    state->bytes.insert(state->bytes.end(), bytes, bytes + count);
    return static_cast<int64_t>(count);
}

int64_t stateRead(const clap_istream_t* stream,
    void* destination, uint64_t requested)
{
    auto* state = static_cast<MemoryState*>(stream->ctx);
    if (!state || (!destination && requested > 0u)) return -1;
    const size_t available = state->offset < state->bytes.size()
        ? state->bytes.size() - state->offset : 0u;
    const size_t count = std::min<size_t>({ available,
        static_cast<size_t>(requested), 7u });
    if (count > 0u) {
        std::memcpy(destination,
            state->bytes.data() + state->offset, count);
        state->offset += count;
    }
    return static_cast<int64_t>(count);
}

struct ParamEvent {
    clap_event_param_value_t event {};
    clap_input_events_t input {
        this,
        [](const clap_input_events_t*) -> uint32_t { return 1u; },
        [](const clap_input_events_t* list,
            uint32_t index) -> const clap_event_header_t* {
            if (index != 0u) return nullptr;
            return &static_cast<const ParamEvent*>(
                list->ctx)->event.header;
        },
    };

    ParamEvent(clap_id id, double value)
    {
        event.header.size = sizeof(event);
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = id;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
    }
};

std::filesystem::path resolveBinary(const std::filesystem::path& supplied)
{
    if (std::filesystem::is_regular_file(supplied)) return supplied;
#if defined(__APPLE__)
    if (std::filesystem::is_directory(supplied)) {
        const auto directory = supplied / "Contents" / "MacOS";
        for (const auto& entry :
             std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file()) return entry.path();
        }
    }
#endif
    return {};
}

const void* hostGetExtension(const clap_host_t*, const char*)
{
    return nullptr;
}
void hostRequest(const clap_host_t*) {}

bool getParam(const clap_plugin_t* plugin,
    const clap_plugin_params_t* params, clap_id id, double expected)
{
    double value = 0.0;
    return params->get_value(plugin, id, &value)
        && std::abs(value - expected) <= 1.0e-6;
}

double renderMaterial(const clap_plugin_t* plugin,
    const clap_plugin_params_t* params, uint32_t material)
{
    ParamEvent materialEvent(kMaterialParamId,
        static_cast<double>(material));
    params->flush(plugin, &materialEvent.input, nullptr);
    ParamEvent mixEvent(kMixParamId, 1.0);
    params->flush(plugin, &mixEvent.input, nullptr);
    plugin->reset(plugin);

    constexpr uint32_t frames = 8192u;
    std::array<float, frames> inputLeft {};
    std::array<float, frames> inputRight {};
    std::array<float, frames> outputLeft {};
    std::array<float, frames> outputRight {};
    for (uint32_t i = 0u; i < 192u; ++i) {
        inputLeft[i] = std::sin(static_cast<float>(i) * 0.37f)
            * (1.0f - static_cast<float>(i) / 192.0f);
        inputRight[i] = std::sin(static_cast<float>(i) * 0.29f)
            * (1.0f - static_cast<float>(i) / 192.0f) * 0.72f;
    }
    float* inputChannels[] { inputLeft.data(), inputRight.data() };
    float* outputChannels[] { outputLeft.data(), outputRight.data() };
    clap_audio_buffer_t inputBuffer {};
    inputBuffer.data32 = inputChannels;
    inputBuffer.channel_count = 2u;
    clap_audio_buffer_t outputBuffer {};
    outputBuffer.data32 = outputChannels;
    outputBuffer.channel_count = 2u;
    clap_process_t process {};
    process.frames_count = frames;
    process.audio_inputs = &inputBuffer;
    process.audio_inputs_count = 1u;
    process.audio_outputs = &outputBuffer;
    process.audio_outputs_count = 1u;
    if (plugin->process(plugin, &process) == CLAP_PROCESS_ERROR) {
        return -1.0;
    }

    double fingerprint = 0.0;
    double energy = 0.0;
    for (uint32_t i = 0u; i < frames; ++i) {
        if (!std::isfinite(outputLeft[i]) || !std::isfinite(outputRight[i]))
            return -1.0;
        energy += static_cast<double>(outputLeft[i]) * outputLeft[i]
            + static_cast<double>(outputRight[i]) * outputRight[i];
        fingerprint += (std::abs(static_cast<double>(outputLeft[i]))
                + std::abs(static_cast<double>(outputRight[i])))
            * (1.0 + static_cast<double>(i % 31u) * 0.003);
    }
    return energy > 1.0e-5 ? fingerprint : -1.0;
}

double renderRightOnlyForListenMode(const clap_plugin_t* plugin,
    const clap_plugin_params_t* params, uint32_t listenMode)
{
    ParamEvent listen(kInputListenParamId,
        static_cast<double>(listenMode));
    params->flush(plugin, &listen.input, nullptr);
    plugin->reset(plugin);

    constexpr uint32_t frames = 8192u;
    std::array<float, frames> inputLeft {};
    std::array<float, frames> inputRight {};
    std::array<float, frames> outputLeft {};
    std::array<float, frames> outputRight {};
    for (uint32_t i = 0u; i < 256u; ++i) {
        inputRight[i] = std::sin(static_cast<float>(i) * 0.31f)
            * (1.0f - static_cast<float>(i) / 256.0f) * 0.62f;
    }
    float* inputChannels[] { inputLeft.data(), inputRight.data() };
    float* outputChannels[] { outputLeft.data(), outputRight.data() };
    clap_audio_buffer_t inputBuffer {};
    inputBuffer.data32 = inputChannels;
    inputBuffer.channel_count = 2u;
    clap_audio_buffer_t outputBuffer {};
    outputBuffer.data32 = outputChannels;
    outputBuffer.channel_count = 2u;
    clap_process_t process {};
    process.frames_count = frames;
    process.audio_inputs = &inputBuffer;
    process.audio_inputs_count = 1u;
    process.audio_outputs = &outputBuffer;
    process.audio_outputs_count = 1u;
    if (plugin->process(plugin, &process) == CLAP_PROCESS_ERROR) return -1.0;

    double energy = 0.0;
    for (uint32_t i = 0u; i < frames; ++i) {
        if (!std::isfinite(outputLeft[i]) || !std::isfinite(outputRight[i]))
            return -1.0;
        energy += static_cast<double>(outputLeft[i]) * outputLeft[i]
            + static_cast<double>(outputRight[i]) * outputRight[i];
    }
    return energy;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: s3g_processor_conduit_clap_smoke "
            << "<bundle-or-binary>\n";
        return 2;
    }
    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "Could not resolve Processor Conduit binary\n";
        return 1;
    }

    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "Could not load Processor Conduit: "
                  << dlerror() << "\n";
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    bool ok = entry && entry->init(binary.c_str());

    clap_host_t host {};
    host.clap_version = CLAP_VERSION_INIT;
    host.name = "Processor Conduit smoke host";
    host.vendor = "s3g";
    host.url = "https://github.com/s3g/s3g-dsp";
    host.version = "1";
    host.get_extension = hostGetExtension;
    host.request_restart = hostRequest;
    host.request_process = hostRequest;
    host.request_callback = hostRequest;

    const auto* factory = ok
        ? static_cast<const clap_plugin_factory_t*>(
            entry->get_factory(CLAP_PLUGIN_FACTORY_ID))
        : nullptr;
    const auto* descriptor = factory
        ? factory->get_plugin_descriptor(factory, 0u) : nullptr;
    ok = ok && factory && factory->get_plugin_count(factory) == 1u
        && descriptor
        && std::strcmp(descriptor->id,
            "org.s3g.s3g-dsp.processor-conduit") == 0
        && std::strcmp(descriptor->name,
            "s3g Processor Conduit") == 0;
    bool advertisesStereo = false;
    if (descriptor && descriptor->features) {
        for (const char* const* feature = descriptor->features;
             *feature; ++feature) {
            if (std::strcmp(*feature, CLAP_PLUGIN_FEATURE_STEREO) == 0) {
                advertisesStereo = true;
                break;
            }
        }
    }
    ok = ok && advertisesStereo;
    if (!ok) std::cerr << "descriptor contract failed\n";

    const clap_plugin_t* plugin = ok
        ? factory->create_plugin(factory, &host, descriptor->id)
        : nullptr;
    ok = ok && plugin && plugin->init(plugin);
    const auto* params = ok
        ? static_cast<const clap_plugin_params_t*>(
            plugin->get_extension(plugin, CLAP_EXT_PARAMS))
        : nullptr;
    const auto* ports = ok
        ? static_cast<const clap_plugin_audio_ports_t*>(
            plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS))
        : nullptr;
    const auto* portConfigs = ok
        ? static_cast<const clap_plugin_audio_ports_config_t*>(
            plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS_CONFIG))
        : nullptr;
    const auto* state = ok
        ? static_cast<const clap_plugin_state_t*>(
            plugin->get_extension(plugin, CLAP_EXT_STATE))
        : nullptr;
    const auto* tail = ok
        ? static_cast<const clap_plugin_tail_t*>(
            plugin->get_extension(plugin, CLAP_EXT_TAIL))
        : nullptr;
    ok = ok && params && ports && portConfigs && state && tail
        && params->count(plugin) == 23u
        && ports->count(plugin, true) == 1u
        && ports->count(plugin, false) == 1u
        && portConfigs->count(plugin) == 2u;
    if (!ok) std::cerr << "extension contract failed\n";

    clap_audio_port_info_t inputPort {};
    clap_audio_port_info_t outputPort {};
    if (ok) {
        ok = ports->get(plugin, 0u, true, &inputPort)
            && ports->get(plugin, 0u, false, &outputPort)
            && inputPort.channel_count == 2u
            && outputPort.channel_count == 2u
            && std::strcmp(inputPort.name, "Stereo Mic In") == 0
            && std::strcmp(outputPort.name, "Voice Stereo Out") == 0;
    }
    if (!ok) std::cerr << "audio-port contract failed\n";

    clap_audio_ports_config_t monoConfig {};
    clap_audio_ports_config_t stereoConfig {};
    if (ok) {
        ok = portConfigs->get(plugin, 0u, &monoConfig)
            && portConfigs->get(plugin, 1u, &stereoConfig)
            && monoConfig.id == kMonoInputConfigId
            && monoConfig.main_input_channel_count == 1u
            && monoConfig.main_output_channel_count == 2u
            && stereoConfig.id == kStereoInputConfigId
            && stereoConfig.main_input_channel_count == 2u
            && stereoConfig.main_output_channel_count == 2u
            && portConfigs->select(plugin, kMonoInputConfigId)
            && ports->get(plugin, 0u, true, &inputPort)
            && inputPort.channel_count == 1u
            && std::strcmp(inputPort.name, "Live Mic In") == 0
            && portConfigs->select(plugin, kStereoInputConfigId)
            && ports->get(plugin, 0u, true, &inputPort)
            && inputPort.channel_count == 2u
            && std::strcmp(inputPort.name, "Stereo Mic In") == 0;
    }
    if (!ok) std::cerr << "audio-port configuration contract failed\n";

    clap_param_info_t materialInfo {};
    bool foundMaterial = false;
    if (ok) {
        for (uint32_t index = 0u; index < params->count(plugin); ++index) {
            clap_param_info_t info {};
            if (params->get_info(plugin, index, &info)
                && info.id == kMaterialParamId) {
                materialInfo = info;
                foundMaterial = true;
                break;
            }
        }
        ok = foundMaterial
            && (materialInfo.flags & CLAP_PARAM_IS_STEPPED) != 0u
            && materialInfo.min_value == 0.0
            && materialInfo.max_value
                == static_cast<double>(kMaterialCount - 1u);
    }
    if (!ok) std::cerr << "material parameter contract failed\n";

    constexpr std::array<const char*, kMaterialCount> names {
        "METAL", "GLASS", "PLASTIC", "WOOD", "WATER", "SKIN",
        "DIRECT", "METAL VESSEL", "GLASS VESSEL", "PLASTIC VESSEL",
        "DEEP BRONZE", "TIERED BRONZE", "BROAD BRONZE", "BRIGHT BRONZE",
        "CARBON LAM.", "GLASS PLATE", "STEEL SHELL", "ALUM. PLATE",
        "PORCELAIN", "EARTHENWARE", "SPRUCE PLATE", "TENSIONED SKIN",
        "LOADED MEM.", "COUPLED MEM.", "CAVITY MEM.", "LOOSE MEM."
    };
    if (ok) {
        for (uint32_t material = 0u; material < kMaterialCount; ++material) {
            char text[24] {};
            ok = params->value_to_text(plugin, kMaterialParamId,
                    static_cast<double>(material), text, sizeof(text))
                && std::strcmp(text, names[material]) == 0;
            if (!ok) break;
        }
    }
    if (!ok) std::cerr << "material text contract failed\n";

    constexpr std::array<const char*, kPedalCount> pedalNames {
        "SHRED", "WOOL", "RAT", "ZONE A",
        "ZONE B", "FUZZ I", "FUZZ II", "DIODE"
    };
    if (ok) {
        for (uint32_t pedal = 0u; pedal < kPedalCount; ++pedal) {
            char text[24] {};
            ok = params->value_to_text(plugin, kPedalParamId,
                    static_cast<double>(pedal), text, sizeof(text))
                && std::strcmp(text, pedalNames[pedal]) == 0;
            if (!ok) break;
        }
    }
    if (!ok) std::cerr << "pedal text contract failed\n";

    constexpr std::array<const char*, 3u> pedalPositionNames {
        "PRE DRIVER", "PIEZO > PA", "MIC > LOOP"
    };
    if (ok) {
        for (uint32_t position = 0u;
             position < pedalPositionNames.size(); ++position) {
            char text[24] {};
            ok = params->value_to_text(plugin, kPedalPositionParamId,
                    static_cast<double>(position), text, sizeof(text))
                && std::strcmp(text, pedalPositionNames[position]) == 0;
            if (!ok) break;
        }
    }
    if (!ok) std::cerr << "pedal position text contract failed\n";

    constexpr std::array<const char*, 4u> inputListenNames {
        "CHANNEL 1", "CHANNEL 2", "SUM MONO", "STEREO"
    };
    if (ok) {
        for (uint32_t listen = 0u;
             listen < inputListenNames.size(); ++listen) {
            char text[24] {};
            ok = params->value_to_text(plugin, kInputListenParamId,
                    static_cast<double>(listen), text, sizeof(text))
                && std::strcmp(text, inputListenNames[listen]) == 0;
            if (!ok) break;
        }
    }
    if (!ok) std::cerr << "input-listen text contract failed\n";

    ok = ok && plugin->activate(plugin, 48000.0, 16u, 8192u)
        && plugin->start_processing(plugin);
    ok = ok && !portConfigs->select(plugin, kMonoInputConfigId);
    if (ok) {
        const double channel1 = renderRightOnlyForListenMode(
            plugin, params, 0u);
        const double channel2 = renderRightOnlyForListenMode(
            plugin, params, 1u);
        const double sumMono = renderRightOnlyForListenMode(
            plugin, params, 2u);
        const double stereo = renderRightOnlyForListenMode(
            plugin, params, 3u);
        ok = channel1 >= 0.0 && channel1 <= 1.0e-12
            && channel2 > 1.0e-5
            && sumMono > 1.0e-5
            && stereo > 1.0e-5;
        if (!ok) {
            std::cerr << "input-listen routing failed: ch1=" << channel1
                      << " ch2=" << channel2
                      << " sum=" << sumMono
                      << " stereo=" << stereo << "\n";
        }
    }
    std::array<double, kMaterialCount> fingerprints {};
    if (ok) {
        for (uint32_t material = 0u; material < kMaterialCount; ++material) {
            fingerprints[material] = renderMaterial(plugin, params, material);
            if (fingerprints[material] < 0.0) {
                ok = false;
                break;
            }
        }
    }
    uint32_t distinct = 0u;
    for (uint32_t material = 1u; ok && material < kMaterialCount; ++material) {
        const double ratio = fingerprints[material]
            / std::max(1.0e-12, fingerprints[0]);
        if (ratio < 0.99 || ratio > 1.01) ++distinct;
    }
    if (ok && distinct < 3u) {
        std::cerr << "material fingerprints insufficient:";
        for (const double value : fingerprints) std::cerr << ' ' << value;
        std::cerr << "\n";
    }
    ok = ok && distinct >= 3u && tail->get(plugin) > 0u;

    LegacyStateV1 legacy;
    legacy.params.material = 2u;
    legacy.params.inputGainDb = 3.5f;
    MemoryState legacyInput;
    const auto* legacyBytes = reinterpret_cast<const uint8_t*>(&legacy);
    legacyInput.bytes.assign(legacyBytes, legacyBytes + sizeof(legacy));
    clap_istream_t legacyStream { &legacyInput, stateRead };
    if (ok) {
        ok = state->load(plugin, &legacyStream)
            && getParam(plugin, params, kMaterialParamId, 2.0)
            && getParam(plugin, params, kInputParamId, 3.5)
            && getParam(plugin, params, kPedalParamId, 0.0)
            && getParam(plugin, params, kPedalDriveParamId, 0.0)
            && getParam(plugin, params, kOctaveDownParamId, 0.0)
            && getParam(plugin, params, kPaDriveParamId, 0.0)
            && getParam(plugin, params, kMicMotionParamId, 0.0)
            && getParam(plugin, params, kChamberParamId, 0.0)
            && getParam(plugin, params, kStereoWidthParamId, 0.0)
            && getParam(plugin, params, kPedalPositionParamId, 0.0)
            && getParam(plugin, params, kPedalMixParamId, 0.0)
            && getParam(plugin, params, kInputListenParamId, 3.0);
    }

    LegacyStateV2 legacyV2;
    legacyV2.params.material = 16u;
    legacyV2.params.pedal = 5u;
    legacyV2.params.pedalDrive = 0.73f;
    legacyV2.params.octaveDown = 0.42f;
    MemoryState legacyV2Input;
    const auto* legacyV2Bytes = reinterpret_cast<const uint8_t*>(&legacyV2);
    legacyV2Input.bytes.assign(legacyV2Bytes,
        legacyV2Bytes + sizeof(legacyV2));
    clap_istream_t legacyV2Stream { &legacyV2Input, stateRead };
    if (ok) {
        ok = state->load(plugin, &legacyV2Stream)
            && getParam(plugin, params, kMaterialParamId, 16.0)
            && getParam(plugin, params, kPedalParamId, 5.0)
            && getParam(plugin, params, kPedalDriveParamId, 0.73)
            && getParam(plugin, params, kOctaveDownParamId, 0.42)
            && getParam(plugin, params, kPaDriveParamId, 0.0)
            && getParam(plugin, params, kMicMotionParamId, 0.0)
            && getParam(plugin, params, kChamberParamId, 0.0)
            && getParam(plugin, params, kStereoWidthParamId, 0.0)
            && getParam(plugin, params, kPedalPositionParamId, 0.0)
            && getParam(plugin, params, kPedalMixParamId,
                std::sqrt(0.73))
            && getParam(plugin, params, kInputListenParamId, 3.0);
    }

    LegacyStateV3 legacyV3;
    legacyV3.params.material = 18u;
    legacyV3.params.pedal = 2u;
    legacyV3.params.pedalDrive = 0.49f;
    legacyV3.params.paDrive = 0.81f;
    legacyV3.params.micMotion = 0.67f;
    MemoryState legacyV3Input;
    const auto* legacyV3Bytes = reinterpret_cast<const uint8_t*>(&legacyV3);
    legacyV3Input.bytes.assign(legacyV3Bytes,
        legacyV3Bytes + sizeof(legacyV3));
    clap_istream_t legacyV3Stream { &legacyV3Input, stateRead };
    if (ok) {
        ok = state->load(plugin, &legacyV3Stream)
            && getParam(plugin, params, kMaterialParamId, 18.0)
            && getParam(plugin, params, kPedalParamId, 2.0)
            && getParam(plugin, params, kPaDriveParamId, 0.81)
            && getParam(plugin, params, kMicMotionParamId, 0.67)
            && getParam(plugin, params, kPedalPositionParamId, 0.0)
            && getParam(plugin, params, kPedalMixParamId, 0.7)
            && getParam(plugin, params, kInputListenParamId, 3.0);
    }

    LegacyStateV4 legacyV4;
    legacyV4.params.material = 12u;
    legacyV4.params.pedal = 6u;
    legacyV4.params.pedalPosition = 1u;
    legacyV4.params.pedalMix = 0.57f;
    MemoryState legacyV4Input;
    const auto* legacyV4Bytes = reinterpret_cast<const uint8_t*>(&legacyV4);
    legacyV4Input.bytes.assign(legacyV4Bytes,
        legacyV4Bytes + sizeof(legacyV4));
    clap_istream_t legacyV4Stream { &legacyV4Input, stateRead };
    if (ok) {
        ok = state->load(plugin, &legacyV4Stream)
            && getParam(plugin, params, kMaterialParamId, 12.0)
            && getParam(plugin, params, kPedalParamId, 6.0)
            && getParam(plugin, params, kPedalPositionParamId, 1.0)
            && getParam(plugin, params, kPedalMixParamId, 0.57)
            && getParam(plugin, params, kInputListenParamId, 3.0);
    }

    ParamEvent water(kMaterialParamId, 4.0);
    ParamEvent inputGain(kInputParamId, 7.25);
    ParamEvent diode(kPedalParamId, 7.0);
    ParamEvent octave(kOctaveDownParamId, 0.8);
    ParamEvent paDrive(kPaDriveParamId, 0.91);
    ParamEvent micMotion(kMicMotionParamId, 0.84);
    ParamEvent chamber(kChamberParamId, 0.76);
    ParamEvent width(kStereoWidthParamId, 0.69);
    ParamEvent pedalPosition(kPedalPositionParamId, 2.0);
    ParamEvent pedalMix(kPedalMixParamId, 0.64);
    ParamEvent inputListen(kInputListenParamId, 2.0);
    if (ok) {
        params->flush(plugin, &water.input, nullptr);
        params->flush(plugin, &inputGain.input, nullptr);
        params->flush(plugin, &diode.input, nullptr);
        params->flush(plugin, &octave.input, nullptr);
        params->flush(plugin, &paDrive.input, nullptr);
        params->flush(plugin, &micMotion.input, nullptr);
        params->flush(plugin, &chamber.input, nullptr);
        params->flush(plugin, &width.input, nullptr);
        params->flush(plugin, &pedalPosition.input, nullptr);
        params->flush(plugin, &pedalMix.input, nullptr);
        params->flush(plugin, &inputListen.input, nullptr);
    }
    MemoryState saved;
    clap_ostream_t output { &saved, stateWrite };
    if (ok) {
        ok = state->save(plugin, &output) && saved.bytes.size() == 96u;
    }
    if (ok) {
        uint32_t version = 0u;
        std::memcpy(&version, saved.bytes.data(), sizeof(version));
        ok = version == 5u;
    }
    ParamEvent metal(kMaterialParamId, 0.0);
    ParamEvent resetGain(kInputParamId, 0.0);
    if (ok) {
        params->flush(plugin, &metal.input, nullptr);
        params->flush(plugin, &resetGain.input, nullptr);
        clap_istream_t input { &saved, stateRead };
        ok = state->load(plugin, &input)
            && getParam(plugin, params, kMaterialParamId, 4.0)
            && getParam(plugin, params, kInputParamId, 7.25)
            && getParam(plugin, params, kPedalParamId, 7.0)
            && getParam(plugin, params, kOctaveDownParamId, 0.8)
            && getParam(plugin, params, kPaDriveParamId, 0.91)
            && getParam(plugin, params, kMicMotionParamId, 0.84)
            && getParam(plugin, params, kChamberParamId, 0.76)
            && getParam(plugin, params, kStereoWidthParamId, 0.69)
            && getParam(plugin, params, kPedalPositionParamId, 2.0)
            && getParam(plugin, params, kPedalMixParamId, 0.64)
            && getParam(plugin, params, kInputListenParamId, 2.0);
    }
    if (!ok) std::cerr << "render/tail/state contract failed\n";

    if (plugin) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }
    if (entry && entry->deinit) entry->deinit();
    dlclose(library);
    if (!ok) {
        std::cerr << "Processor Conduit CLAP smoke failed\n";
        return 1;
    }
    std::cout << "Processor Conduit CLAP smoke passed\n";
    return 0;
}
