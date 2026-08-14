#include "s3g_processor_conduit.h"
#include "s3g_processor_conduit_presets.h"
#include "s3g_realtime.h"
#include "../common/s3g_clap_state_stream.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports-config.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/tail.h>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include "../common/s3g_clap_macos.h"
#include "../common/s3g_cocoa_gui.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {

constexpr uint32_t kStateVersion = 5u;
constexpr uint32_t kGuiWidth = 760u;
constexpr uint32_t kGuiHeight = 620u;
constexpr clap_id kMonoInputConfigId = 100u;
constexpr clap_id kStereoInputConfigId = 101u;

enum ParamId : clap_id {
    kMaterialParamId = 1,
    kInputParamId = 2,
    kDriverParamId = 3,
    kSizeParamId = 4,
    kTensionParamId = 5,
    kDampingParamId = 6,
    kPickupParamId = 7,
    kContactParamId = 8,
    kFeedbackParamId = 9,
    kMixParamId = 10,
    kOutputParamId = 11,
    kPedalParamId = 12,
    kPedalDriveParamId = 13,
    kPedalToneParamId = 14,
    kOctaveDownParamId = 15,
    kOctaveDragParamId = 16,
    kPaDriveParamId = 17,
    kMicMotionParamId = 18,
    kChamberParamId = 19,
    kStereoWidthParamId = 20,
    kPedalPositionParamId = 21,
    kPedalMixParamId = 22,
    kInputListenParamId = 23,
};

struct LegacyProcessorConduitParamsV1 {
    s3g::ProcessorConduitMaterial material;
    float inputGainDb;
    float driver;
    float size;
    float tension;
    float damping;
    float pickup;
    float contact;
    float feedback;
    float mix;
    float outputGainDb;
};

static_assert(sizeof(LegacyProcessorConduitParamsV1) == 44u,
    "Unexpected Processor Conduit v1 state layout.");

struct LegacyProcessorConduitParamsV2 {
    s3g::ProcessorConduitMaterial material;
    float inputGainDb;
    float driver;
    float size;
    float tension;
    float damping;
    float pickup;
    float contact;
    float feedback;
    float mix;
    float outputGainDb;
    s3g::ProcessorConduitPedal pedal;
    float pedalDrive;
    float pedalTone;
    float octaveDown;
    float octaveDrag;
};

static_assert(sizeof(LegacyProcessorConduitParamsV2) == 64u,
    "Unexpected Processor Conduit v2 state layout.");

struct LegacyProcessorConduitParamsV3 {
    s3g::ProcessorConduitMaterial material;
    float inputGainDb;
    float driver;
    float size;
    float tension;
    float damping;
    float pickup;
    float contact;
    float feedback;
    float mix;
    float outputGainDb;
    s3g::ProcessorConduitPedal pedal;
    float pedalDrive;
    float pedalTone;
    float octaveDown;
    float octaveDrag;
    float paDrive;
    float micMotion;
    float chamber;
    float stereoWidth;
};

static_assert(sizeof(LegacyProcessorConduitParamsV3) == 80u,
    "Unexpected Processor Conduit v3 state layout.");

struct LegacyProcessorConduitParamsV4 {
    s3g::ProcessorConduitMaterial material;
    float inputGainDb;
    float driver;
    float size;
    float tension;
    float damping;
    float pickup;
    float contact;
    float feedback;
    float mix;
    float outputGainDb;
    s3g::ProcessorConduitPedal pedal;
    float pedalDrive;
    float pedalTone;
    float octaveDown;
    float octaveDrag;
    float paDrive;
    float micMotion;
    float chamber;
    float stereoWidth;
    s3g::ProcessorConduitPedalPosition pedalPosition;
    float pedalMix;
};

static_assert(sizeof(LegacyProcessorConduitParamsV4) == 88u,
    "Unexpected Processor Conduit v4 state layout.");

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::ProcessorConduitParams params {};
};

static_assert(sizeof(s3g::ProcessorConduitParams) == 92u,
    "Unexpected Processor Conduit v5 parameter layout.");
static_assert(sizeof(SavedState) == 96u,
    "Unexpected Processor Conduit v5 state layout.");

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_tail_t* hostTail = nullptr;
    double sampleRate = 48000.0;
    s3g::ProcessorConduitParams params {};
    s3g::ProcessorConduit conduit;
    s3g::ProcessorConduit conduitRight;
    clap_id audioConfigId = kStereoInputConfigId;
    bool active = false;
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<float> materialActivity { 0.0f };
    std::atomic<float> governorReduction { 0.0f };
    std::atomic<float> breakupActivity { 0.0f };
    std::atomic<float> micPosition { 0.0f };
    std::atomic<float> fundamentalHz { 220.0f };
    std::atomic<float> propagationMs { 8.0f };
    std::atomic<float> octaveWindowMs { 120.0f };
    std::atomic<bool> panicRequested { false };
#if defined(__APPLE__)
    void* guiView = nullptr;
    bool guiVisible = false;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
#endif
};

Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}

bool paramAffectsTail(clap_id id)
{
    switch (id) {
    case kMaterialParamId:
    case kSizeParamId:
    case kTensionParamId:
    case kDampingParamId:
    case kPickupParamId:
    case kFeedbackParamId:
    case kPedalParamId:
    case kPedalDriveParamId:
    case kPedalToneParamId:
    case kOctaveDownParamId:
    case kOctaveDragParamId:
    case kPaDriveParamId:
    case kMicMotionParamId:
    case kChamberParamId:
    case kPedalPositionParamId:
    case kPedalMixParamId:
    case kInputListenParamId:
        return true;
    default:
        return false;
    }
}

void applyParam(Plugin& p, clap_id id, double value)
{
    switch (id) {
    case kMaterialParamId:
        p.params.material = static_cast<s3g::ProcessorConduitMaterial>(
            std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)),
                0u, s3g::kProcessorConduitMaterialCount - 1u));
        break;
    case kInputParamId:
        p.params.inputGainDb = static_cast<float>(
            std::clamp(value, -24.0, 36.0));
        break;
    case kDriverParamId:
        p.params.driver = static_cast<float>(std::clamp(value, 0.0, 1.0));
        break;
    case kSizeParamId:
        p.params.size = static_cast<float>(std::clamp(value, 0.0, 1.0));
        break;
    case kTensionParamId:
        p.params.tension = static_cast<float>(std::clamp(value, 0.0, 1.0));
        break;
    case kDampingParamId:
        p.params.damping = static_cast<float>(std::clamp(value, 0.0, 1.0));
        break;
    case kPickupParamId:
        p.params.pickup = static_cast<float>(std::clamp(value, 0.0, 1.0));
        break;
    case kContactParamId:
        p.params.contact = static_cast<float>(std::clamp(value, 0.0, 1.0));
        break;
    case kFeedbackParamId:
        p.params.feedback = static_cast<float>(std::clamp(value, 0.0, 1.0));
        break;
    case kMixParamId:
        p.params.mix = static_cast<float>(std::clamp(value, 0.0, 1.0));
        break;
    case kOutputParamId:
        p.params.outputGainDb = static_cast<float>(
            std::clamp(value, -60.0, 6.0));
        break;
    case kPedalParamId:
        p.params.pedal = static_cast<s3g::ProcessorConduitPedal>(
            std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)),
                0u, s3g::kProcessorConduitPedalCount - 1u));
        break;
    case kPedalDriveParamId:
        p.params.pedalDrive = static_cast<float>(
            std::clamp(value, 0.0, 1.0));
        break;
    case kPedalToneParamId:
        p.params.pedalTone = static_cast<float>(
            std::clamp(value, 0.0, 1.0));
        break;
    case kOctaveDownParamId:
        p.params.octaveDown = static_cast<float>(
            std::clamp(value, 0.0, 1.0));
        break;
    case kOctaveDragParamId:
        p.params.octaveDrag = static_cast<float>(
            std::clamp(value, 0.0, 1.0));
        break;
    case kPaDriveParamId:
        p.params.paDrive = static_cast<float>(
            std::clamp(value, 0.0, 1.0));
        break;
    case kMicMotionParamId:
        p.params.micMotion = static_cast<float>(
            std::clamp(value, 0.0, 1.0));
        break;
    case kChamberParamId:
        p.params.chamber = static_cast<float>(
            std::clamp(value, 0.0, 1.0));
        break;
    case kStereoWidthParamId:
        p.params.stereoWidth = static_cast<float>(
            std::clamp(value, 0.0, 1.0));
        break;
    case kPedalPositionParamId:
        p.params.pedalPosition =
            static_cast<s3g::ProcessorConduitPedalPosition>(
                std::clamp<uint32_t>(
                    static_cast<uint32_t>(std::lround(value)), 0u,
                    s3g::kProcessorConduitPedalPositionCount - 1u));
        break;
    case kPedalMixParamId:
        p.params.pedalMix = static_cast<float>(
            std::clamp(value, 0.0, 1.0));
        break;
    case kInputListenParamId:
        p.params.inputListen = static_cast<s3g::ProcessorConduitInputListen>(
            std::clamp<uint32_t>(
                static_cast<uint32_t>(std::lround(value)), 0u,
                s3g::kProcessorConduitInputListenCount - 1u));
        break;
    default:
        return;
    }
    p.conduit.setParams(p.params);
    p.params = p.conduit.params();
    p.conduitRight.setParams(p.params);
    if (paramAffectsTail(id) && p.hostTail && p.hostTail->changed) {
        p.hostTail->changed(p.host);
    }
}

bool init(const clap_plugin_t*) { return true; }

#if defined(__APPLE__)
void guiDestroy(const clap_plugin_t* plugin);
#endif

void destroy(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    guiDestroy(plugin);
#endif
    delete self(plugin);
}

bool activate(const clap_plugin_t* plugin, double sampleRate,
    uint32_t, uint32_t)
{
    auto* p = self(plugin);
    p->sampleRate = sampleRate;
    p->conduit.prepare(sampleRate);
    p->conduitRight.prepare(sampleRate);
    p->conduit.setParams(p->params);
    p->conduitRight.setParams(p->params);
    p->conduit.reset();
    p->conduitRight.reset();
    p->active = true;
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
    p->materialActivity.store(0.0f, std::memory_order_relaxed);
    p->governorReduction.store(0.0f, std::memory_order_relaxed);
    p->breakupActivity.store(0.0f, std::memory_order_relaxed);
    p->micPosition.store(0.0f, std::memory_order_relaxed);
    p->panicRequested.store(false, std::memory_order_relaxed);
    return true;
}

void deactivate(const clap_plugin_t* plugin)
{
    self(plugin)->active = false;
}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->conduit.reset();
    p->conduitRight.reset();
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
    p->materialActivity.store(0.0f, std::memory_order_relaxed);
    p->governorReduction.store(0.0f, std::memory_order_relaxed);
    p->breakupActivity.store(0.0f, std::memory_order_relaxed);
    p->micPosition.store(0.0f, std::memory_order_relaxed);
}

void readParamEvents(Plugin& p, const clap_input_events_t* in)
{
    if (!in) return;
    const uint32_t count = in->size(in);
    for (uint32_t i = 0u; i < count; ++i) {
        const auto* event = in->get(in, i);
        if (event && event->space_id == CLAP_CORE_EVENT_SPACE_ID
            && event->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param =
                reinterpret_cast<const clap_event_param_value_t*>(event);
            applyParam(p, param->param_id, param->value);
        }
    }
}

template <typename InputSample, typename OutputSample>
void processSamples(Plugin& p, const InputSample* inputLeft,
    const InputSample* inputRight, bool stereoInput,
    OutputSample* outputLeft, OutputSample* outputRight, uint32_t frames)
{
    float blockPeak = 0.0f;
    const bool stereoThrough = stereoInput
        && p.params.inputListen == s3g::ProcessorConduitInputListen::Stereo;
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        const float inLeft = inputLeft
            ? static_cast<float>(inputLeft[frame]) : 0.0f;
        const float inRight = stereoInput && inputRight
            ? static_cast<float>(inputRight[frame]) : inLeft;
        float left = 0.0f;
        float right = 0.0f;
        if (stereoThrough) {
            p.conduit.processFrame(inLeft, left, right);
            float rightCoreLeft = 0.0f;
            float rightCoreRight = 0.0f;
            p.conduitRight.processFrame(inRight,
                rightCoreLeft, rightCoreRight);
            const float leftCoreRight = right;
            left = left * 0.88f + rightCoreLeft * 0.12f;
            right = rightCoreRight * 0.88f + leftCoreRight * 0.12f;
        } else {
            float selected = inLeft;
            if (p.params.inputListen
                    == s3g::ProcessorConduitInputListen::Channel2) {
                selected = inRight;
            } else if (p.params.inputListen
                    == s3g::ProcessorConduitInputListen::SumMono) {
                selected = (inLeft + inRight) * 0.5f;
            }
            p.conduit.processFrame(selected, left, right);
        }
        if (outputLeft) outputLeft[frame] = static_cast<OutputSample>(left);
        if (outputRight) outputRight[frame] = static_cast<OutputSample>(right);
        blockPeak = std::max(blockPeak,
            std::max(std::abs(left), std::abs(right)));
    }
    p.outputPeak.store(std::max(
        p.outputPeak.load(std::memory_order_relaxed) * 0.90f, blockPeak),
        std::memory_order_relaxed);
}

clap_process_status process(const clap_plugin_t* plugin,
    const clap_process_t* processData)
{
    auto* p = self(plugin);
    readParamEvents(*p, processData->in_events);
    if (p->panicRequested.exchange(false, std::memory_order_acq_rel)) {
        p->conduit.panic();
        p->conduitRight.panic();
    }
    if (processData->audio_inputs_count == 0u
        || processData->audio_outputs_count == 0u) {
        return CLAP_PROCESS_CONTINUE;
    }
    const auto& input = processData->audio_inputs[0];
    const auto& output = processData->audio_outputs[0];
    if (input.channel_count == 0u || output.channel_count == 0u) {
        return CLAP_PROCESS_CONTINUE;
    }

    p->conduit.setParams(p->params);
    p->conduitRight.setParams(p->params);
    const uint32_t frames = processData->frames_count;
    const bool stereoInput = p->audioConfigId == kStereoInputConfigId
        && input.channel_count > 1u;
    const bool stereoThrough = stereoInput
        && p->params.inputListen
            == s3g::ProcessorConduitInputListen::Stereo;
    if (output.data32 && output.data32[0]) {
        float* outputRight = output.channel_count > 1u
            ? output.data32[1] : nullptr;
        if (input.data32 && input.data32[0]) {
            const float* inputRight = stereoInput ? input.data32[1] : nullptr;
            processSamples(*p, input.data32[0], inputRight, stereoInput,
                output.data32[0], outputRight, frames);
        } else if (input.data64 && input.data64[0]) {
            const double* inputRight = stereoInput ? input.data64[1] : nullptr;
            processSamples(*p, input.data64[0], inputRight, stereoInput,
                output.data32[0], outputRight, frames);
        } else {
            processSamples<float, float>(*p, nullptr, nullptr, stereoInput,
                output.data32[0], outputRight, frames);
        }
    } else if (output.data64 && output.data64[0]) {
        double* outputRight = output.channel_count > 1u
            ? output.data64[1] : nullptr;
        if (input.data64 && input.data64[0]) {
            const double* inputRight = stereoInput ? input.data64[1] : nullptr;
            processSamples(*p, input.data64[0], inputRight, stereoInput,
                output.data64[0], outputRight, frames);
        } else if (input.data32 && input.data32[0]) {
            const float* inputRight = stereoInput ? input.data32[1] : nullptr;
            processSamples(*p, input.data32[0], inputRight, stereoInput,
                output.data64[0], outputRight, frames);
        } else {
            processSamples<float, double>(*p, nullptr, nullptr, stereoInput,
                output.data64[0], outputRight, frames);
        }
    }

    p->materialActivity.store(std::max(p->conduit.materialActivity(),
            stereoThrough ? p->conduitRight.materialActivity() : 0.0f),
        std::memory_order_relaxed);
    p->governorReduction.store(std::max(p->conduit.governorReduction(),
            stereoThrough ? p->conduitRight.governorReduction() : 0.0f),
        std::memory_order_relaxed);
    p->breakupActivity.store(std::max(
            p->conduit.feedbackBreakupActivity(), stereoThrough
                ? p->conduitRight.feedbackBreakupActivity() : 0.0f),
        std::memory_order_relaxed);
    p->micPosition.store(p->conduit.virtualMicPosition(),
        std::memory_order_relaxed);
    p->fundamentalHz.store(p->conduit.fundamentalHz(),
        std::memory_order_relaxed);
    p->propagationMs.store(p->conduit.propagationMilliseconds(),
        std::memory_order_relaxed);
    p->octaveWindowMs.store(p->conduit.octaveWindowMilliseconds(),
        std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1u; }

bool fillAudioPortInfo(bool stereoInput, uint32_t index, bool isInput,
    clap_audio_port_info_t* info)
{
    if (index != 0u || !info) return false;
    *info = {};
    info->id = isInput ? 10u : 20u;
    std::snprintf(info->name, sizeof(info->name), "%s",
        isInput ? (stereoInput ? "Stereo Mic In" : "Live Mic In")
                : "Voice Stereo Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = isInput && !stereoInput ? 1u : 2u;
    info->port_type = isInput && !stereoInput
        ? CLAP_PORT_MONO : CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

bool audioPortsGet(const clap_plugin_t* plugin, uint32_t index, bool isInput,
    clap_audio_port_info_t* info)
{
    return fillAudioPortInfo(
        self(plugin)->audioConfigId == kStereoInputConfigId,
        index, isInput, info);
}

const clap_plugin_audio_ports_t audioPorts {
    audioPortsCount, audioPortsGet
};

uint32_t audioPortsConfigCount(const clap_plugin_t*) { return 2u; }

bool audioPortsConfigGet(const clap_plugin_t*, uint32_t index,
    clap_audio_ports_config_t* config)
{
    if (!config || index >= 2u) return false;
    *config = {};
    const bool stereo = index == 1u;
    config->id = stereo ? kStereoInputConfigId : kMonoInputConfigId;
    std::snprintf(config->name, sizeof(config->name), "%s",
        stereo ? "Stereo Input / Stereo Output"
               : "Mono Input / Stereo Output");
    config->input_port_count = 1u;
    config->output_port_count = 1u;
    config->has_main_input = true;
    config->main_input_channel_count = stereo ? 2u : 1u;
    config->main_input_port_type = stereo ? CLAP_PORT_STEREO : CLAP_PORT_MONO;
    config->has_main_output = true;
    config->main_output_channel_count = 2u;
    config->main_output_port_type = CLAP_PORT_STEREO;
    return true;
}

bool audioPortsConfigSelect(const clap_plugin_t* plugin, clap_id id)
{
    auto* p = self(plugin);
    if (p->active || (id != kMonoInputConfigId
            && id != kStereoInputConfigId)) {
        return false;
    }
    p->audioConfigId = id;
    return true;
}

const clap_plugin_audio_ports_config_t audioPortsConfig {
    audioPortsConfigCount, audioPortsConfigGet, audioPortsConfigSelect
};

clap_id audioPortsConfigCurrent(const clap_plugin_t* plugin)
{
    return self(plugin)->audioConfigId;
}

bool audioPortsConfigInfoGet(const clap_plugin_t*, clap_id configId,
    uint32_t portIndex, bool isInput, clap_audio_port_info_t* info)
{
    if (!info || portIndex != 0u
        || (configId != kMonoInputConfigId
            && configId != kStereoInputConfigId)) {
        return false;
    }
    return fillAudioPortInfo(configId == kStereoInputConfigId,
        portIndex, isInput, info);
}

const clap_plugin_audio_ports_config_info_t audioPortsConfigInfo {
    audioPortsConfigCurrent, audioPortsConfigInfoGet
};

struct ParamDef {
    clap_id id;
    const char* name;
    double minimum;
    double maximum;
    double defaultValue;
    bool stepped = false;
};

constexpr ParamDef kParamDefs[] {
    { kMaterialParamId, "Material", 0.0,
        static_cast<double>(s3g::kProcessorConduitMaterialCount - 1u),
        0.0, true },
    { kInputParamId, "Input", -24.0, 36.0, 6.0 },
    { kDriverParamId, "Driver", 0.0, 1.0, 0.45 },
    { kSizeParamId, "Size", 0.0, 1.0, 0.58 },
    { kTensionParamId, "Tension", 0.0, 1.0, 0.48 },
    { kDampingParamId, "Damping", 0.0, 1.0, 0.38 },
    { kPickupParamId, "Pickup", 0.0, 1.0, 0.72 },
    { kContactParamId, "Contact", 0.0, 1.0, 0.58 },
    { kFeedbackParamId, "Feedback", 0.0, 1.0, 0.16 },
    { kMixParamId, "Mix", 0.0, 1.0, 0.82 },
    { kOutputParamId, "Out", -60.0, 6.0, -6.0 },
    { kPedalParamId, "Pedal", 0.0,
        static_cast<double>(s3g::kProcessorConduitPedalCount - 1u),
        0.0, true },
    { kPedalDriveParamId, "Pedal Drive", 0.0, 1.0, 0.36 },
    { kPedalToneParamId, "Pedal Tone", 0.0, 1.0, 0.55 },
    { kOctaveDownParamId, "Octave Down", 0.0, 1.0, 0.0 },
    { kOctaveDragParamId, "Octave Drag", 0.0, 1.0, 0.68 },
    { kPaDriveParamId, "PA Drive", 0.0, 1.0, 0.46 },
    { kMicMotionParamId, "Mic Motion", 0.0, 1.0, 0.32 },
    { kChamberParamId, "Chamber", 0.0, 1.0, 0.62 },
    { kStereoWidthParamId, "Stereo Width", 0.0, 1.0, 0.68 },
    { kPedalPositionParamId, "Pedal Position", 0.0,
        static_cast<double>(s3g::kProcessorConduitPedalPositionCount - 1u),
        0.0, true },
    { kPedalMixParamId, "Pedal Mix", 0.0, 1.0, 0.60 },
    { kInputListenParamId, "Input Listen", 0.0,
        static_cast<double>(s3g::kProcessorConduitInputListenCount - 1u),
        static_cast<double>(s3g::ProcessorConduitInputListen::Stereo), true },
};

uint32_t paramsCount(const clap_plugin_t*)
{
    return static_cast<uint32_t>(std::size(kParamDefs));
}

bool paramsGetInfo(const clap_plugin_t*, uint32_t index,
    clap_param_info_t* info)
{
    if (!info || index >= std::size(kParamDefs)) return false;
    const auto& definition = kParamDefs[index];
    info->id = definition.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE
        | (definition.stepped ? CLAP_PARAM_IS_STEPPED : 0u);
    std::strncpy(info->name, definition.name, sizeof(info->name));
    std::strncpy(info->module, "Processor Conduit", sizeof(info->module));
    info->min_value = definition.minimum;
    info->max_value = definition.maximum;
    info->default_value = definition.defaultValue;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    const auto& params = self(plugin)->params;
    switch (id) {
    case kMaterialParamId:
        *value = static_cast<uint32_t>(params.material); return true;
    case kInputParamId: *value = params.inputGainDb; return true;
    case kDriverParamId: *value = params.driver; return true;
    case kSizeParamId: *value = params.size; return true;
    case kTensionParamId: *value = params.tension; return true;
    case kDampingParamId: *value = params.damping; return true;
    case kPickupParamId: *value = params.pickup; return true;
    case kContactParamId: *value = params.contact; return true;
    case kFeedbackParamId: *value = params.feedback; return true;
    case kMixParamId: *value = params.mix; return true;
    case kOutputParamId: *value = params.outputGainDb; return true;
    case kPedalParamId:
        *value = static_cast<uint32_t>(params.pedal); return true;
    case kPedalDriveParamId: *value = params.pedalDrive; return true;
    case kPedalToneParamId: *value = params.pedalTone; return true;
    case kOctaveDownParamId: *value = params.octaveDown; return true;
    case kOctaveDragParamId: *value = params.octaveDrag; return true;
    case kPaDriveParamId: *value = params.paDrive; return true;
    case kMicMotionParamId: *value = params.micMotion; return true;
    case kChamberParamId: *value = params.chamber; return true;
    case kStereoWidthParamId: *value = params.stereoWidth; return true;
    case kPedalPositionParamId:
        *value = static_cast<uint32_t>(params.pedalPosition); return true;
    case kPedalMixParamId: *value = params.pedalMix; return true;
    case kInputListenParamId:
        *value = static_cast<uint32_t>(params.inputListen); return true;
    default: return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value,
    char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
    if (id == kMaterialParamId) {
        const auto material = static_cast<s3g::ProcessorConduitMaterial>(
            std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)),
                0u, s3g::kProcessorConduitMaterialCount - 1u));
        std::snprintf(display, size, "%s",
            s3g::processorConduitMaterialName(material));
    } else if (id == kPedalParamId) {
        const auto pedal = static_cast<s3g::ProcessorConduitPedal>(
            std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)),
                0u, s3g::kProcessorConduitPedalCount - 1u));
        std::snprintf(display, size, "%s",
            s3g::processorConduitPedalName(pedal));
    } else if (id == kPedalPositionParamId) {
        const auto position =
            static_cast<s3g::ProcessorConduitPedalPosition>(
                std::clamp<uint32_t>(
                    static_cast<uint32_t>(std::lround(value)), 0u,
                    s3g::kProcessorConduitPedalPositionCount - 1u));
        std::snprintf(display, size, "%s",
            s3g::processorConduitPedalPositionName(position));
    } else if (id == kInputListenParamId) {
        const auto listen = static_cast<s3g::ProcessorConduitInputListen>(
            std::clamp<uint32_t>(
                static_cast<uint32_t>(std::lround(value)), 0u,
                s3g::kProcessorConduitInputListenCount - 1u));
        std::snprintf(display, size, "%s",
            s3g::processorConduitInputListenName(listen));
    } else if (id == kInputParamId || id == kOutputParamId) {
        std::snprintf(display, size, "%+.1f dB", value);
    } else {
        std::snprintf(display, size, "%.0f%%", value * 100.0);
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id,
    const char* display, double* value)
{
    if (!display || !value) return false;
    if (id == kMaterialParamId) {
        for (uint32_t material = 0u;
             material < s3g::kProcessorConduitMaterialCount; ++material) {
            if (std::strcmp(display, s3g::processorConduitMaterialName(
                    static_cast<s3g::ProcessorConduitMaterial>(material)))
                == 0) {
                *value = static_cast<double>(material);
                return true;
            }
        }
        return false;
    }
    if (id == kPedalParamId) {
        for (uint32_t pedal = 0u;
             pedal < s3g::kProcessorConduitPedalCount; ++pedal) {
            if (std::strcmp(display, s3g::processorConduitPedalName(
                    static_cast<s3g::ProcessorConduitPedal>(pedal))) == 0) {
                *value = static_cast<double>(pedal);
                return true;
            }
        }
        return false;
    }
    if (id == kPedalPositionParamId) {
        for (uint32_t position = 0u;
             position < s3g::kProcessorConduitPedalPositionCount;
             ++position) {
            if (std::strcmp(display,
                    s3g::processorConduitPedalPositionName(
                        static_cast<s3g::ProcessorConduitPedalPosition>(
                            position))) == 0) {
                *value = static_cast<double>(position);
                return true;
            }
        }
        return false;
    }
    if (id == kInputListenParamId) {
        for (uint32_t listen = 0u;
             listen < s3g::kProcessorConduitInputListenCount; ++listen) {
            if (std::strcmp(display, s3g::processorConduitInputListenName(
                    static_cast<s3g::ProcessorConduitInputListen>(listen)))
                == 0) {
                *value = static_cast<double>(listen);
                return true;
            }
        }
        return false;
    }
    *value = std::atof(display);
    if (std::strchr(display, '%')) *value *= 0.01;
    return true;
}

void paramsFlush(const clap_plugin_t* plugin,
    const clap_input_events_t* input, const clap_output_events_t*)
{
    readParamEvents(*self(plugin), input);
}

const clap_plugin_params_t paramsExt {
    paramsCount, paramsGetInfo, paramsGetValue,
    paramsValueToText, paramsTextToValue, paramsFlush
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    const SavedState state { kStateVersion, self(plugin)->params };
    return s3g::clap_state::writeAll(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    uint32_t version = 0u;
    if (!s3g::clap_state::readAll(stream, &version, sizeof(version))) {
        return false;
    }
    s3g::ProcessorConduitParams loaded {};
    if (version == kStateVersion) {
        if (!s3g::clap_state::readAll(stream, &loaded, sizeof(loaded))) {
            return false;
        }
    } else if (version == 4u) {
        LegacyProcessorConduitParamsV4 legacy {};
        if (!s3g::clap_state::readAll(stream, &legacy, sizeof(legacy))) {
            return false;
        }
        loaded.material = legacy.material;
        loaded.inputGainDb = legacy.inputGainDb;
        loaded.driver = legacy.driver;
        loaded.size = legacy.size;
        loaded.tension = legacy.tension;
        loaded.damping = legacy.damping;
        loaded.pickup = legacy.pickup;
        loaded.contact = legacy.contact;
        loaded.feedback = legacy.feedback;
        loaded.mix = legacy.mix;
        loaded.outputGainDb = legacy.outputGainDb;
        loaded.pedal = legacy.pedal;
        loaded.pedalDrive = legacy.pedalDrive;
        loaded.pedalTone = legacy.pedalTone;
        loaded.octaveDown = legacy.octaveDown;
        loaded.octaveDrag = legacy.octaveDrag;
        loaded.paDrive = legacy.paDrive;
        loaded.micMotion = legacy.micMotion;
        loaded.chamber = legacy.chamber;
        loaded.stereoWidth = legacy.stereoWidth;
        loaded.pedalPosition = legacy.pedalPosition;
        loaded.pedalMix = legacy.pedalMix;
        loaded.inputListen = s3g::ProcessorConduitInputListen::Stereo;
    } else if (version == 3u) {
        LegacyProcessorConduitParamsV3 legacy {};
        if (!s3g::clap_state::readAll(stream, &legacy, sizeof(legacy))) {
            return false;
        }
        loaded.material = legacy.material;
        loaded.inputGainDb = legacy.inputGainDb;
        loaded.driver = legacy.driver;
        loaded.size = legacy.size;
        loaded.tension = legacy.tension;
        loaded.damping = legacy.damping;
        loaded.pickup = legacy.pickup;
        loaded.contact = legacy.contact;
        loaded.feedback = legacy.feedback;
        loaded.mix = legacy.mix;
        loaded.outputGainDb = legacy.outputGainDb;
        loaded.pedal = legacy.pedal;
        loaded.pedalDrive = legacy.pedalDrive;
        loaded.pedalTone = legacy.pedalTone;
        loaded.octaveDown = legacy.octaveDown;
        loaded.octaveDrag = legacy.octaveDrag;
        loaded.paDrive = legacy.paDrive;
        loaded.micMotion = legacy.micMotion;
        loaded.chamber = legacy.chamber;
        loaded.stereoWidth = legacy.stereoWidth;
        loaded.pedalPosition =
            s3g::ProcessorConduitPedalPosition::PreDriver;
        loaded.pedalMix = std::sqrt(std::clamp(
            legacy.pedalDrive, 0.0f, 1.0f));
        loaded.inputListen = s3g::ProcessorConduitInputListen::Stereo;
    } else if (version == 2u) {
        LegacyProcessorConduitParamsV2 legacy {};
        if (!s3g::clap_state::readAll(stream, &legacy, sizeof(legacy))) {
            return false;
        }
        loaded.material = legacy.material;
        loaded.inputGainDb = legacy.inputGainDb;
        loaded.driver = legacy.driver;
        loaded.size = legacy.size;
        loaded.tension = legacy.tension;
        loaded.damping = legacy.damping;
        loaded.pickup = legacy.pickup;
        loaded.contact = legacy.contact;
        loaded.feedback = legacy.feedback;
        loaded.mix = legacy.mix;
        loaded.outputGainDb = legacy.outputGainDb;
        loaded.pedal = legacy.pedal;
        loaded.pedalDrive = legacy.pedalDrive;
        loaded.pedalTone = legacy.pedalTone;
        loaded.octaveDown = legacy.octaveDown;
        loaded.octaveDrag = legacy.octaveDrag;
        loaded.paDrive = 0.0f;
        loaded.micMotion = 0.0f;
        loaded.chamber = 0.0f;
        loaded.stereoWidth = 0.0f;
        loaded.pedalPosition =
            s3g::ProcessorConduitPedalPosition::PreDriver;
        loaded.pedalMix = std::sqrt(std::clamp(
            legacy.pedalDrive, 0.0f, 1.0f));
        loaded.inputListen = s3g::ProcessorConduitInputListen::Stereo;
    } else if (version == 1u) {
        LegacyProcessorConduitParamsV1 legacy {};
        if (!s3g::clap_state::readAll(stream, &legacy, sizeof(legacy))) {
            return false;
        }
        loaded.material = legacy.material;
        loaded.inputGainDb = legacy.inputGainDb;
        loaded.driver = legacy.driver;
        loaded.size = legacy.size;
        loaded.tension = legacy.tension;
        loaded.damping = legacy.damping;
        loaded.pickup = legacy.pickup;
        loaded.contact = legacy.contact;
        loaded.feedback = legacy.feedback;
        loaded.mix = legacy.mix;
        loaded.outputGainDb = legacy.outputGainDb;
        loaded.pedal = s3g::ProcessorConduitPedal::Shred;
        loaded.pedalDrive = 0.0f;
        loaded.pedalTone = 0.55f;
        loaded.octaveDown = 0.0f;
        loaded.octaveDrag = 0.68f;
        loaded.paDrive = 0.0f;
        loaded.micMotion = 0.0f;
        loaded.chamber = 0.0f;
        loaded.stereoWidth = 0.0f;
        loaded.pedalPosition =
            s3g::ProcessorConduitPedalPosition::PreDriver;
        loaded.pedalMix = 0.0f;
        loaded.inputListen = s3g::ProcessorConduitInputListen::Stereo;
    } else {
        return false;
    }
    auto* p = self(plugin);
    p->conduit.setParams(loaded);
    p->params = p->conduit.params();
    p->conduitRight.setParams(p->params);
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

uint32_t tailGet(const clap_plugin_t* plugin)
{
    const auto* p = self(plugin);
    if (!p) return 0u;
    const double modalSeconds = 0.12
        + (1.0 - static_cast<double>(p->params.damping)) * 7.4;
    const double feedback = std::clamp(
        static_cast<double>(p->params.feedback) * 0.955, 0.0, 0.955);
    const double returnSeconds = feedback > 0.001
        ? std::ceil(std::log(0.001) / std::log(feedback)) / 24.0
        : 0.0;
    const double seconds = std::clamp(modalSeconds + returnSeconds,
        0.1, 30.0);
    return static_cast<uint32_t>(std::ceil(seconds * p->sampleRate));
}

const clap_plugin_tail_t tailExt { tailGet };

} // namespace

#if defined(__APPLE__)
constexpr uint32_t kContainmentHistorySize = 160u;
constexpr int kMaterialMenuId = 1;
constexpr int kPedalMenuId = 2;
constexpr int kPedalPositionMenuId = 3;
constexpr int kFactoryPresetMenuId = 4;
constexpr int kInputListenMenuId = 5;

constexpr s3g::gui_layout::MacroShredFamilyLayout conduitFamilyLayout()
{
    auto layout = s3g::gui_layout::kMacroShredFamilyLayout;
    layout.engine.frame.height += 26.0;
    layout.engine.rowCount = 10u;
    layout.relationships.frame.y += 26.0;
    return layout;
}

inline constexpr auto kConduitFamilyLayout = conduitFamilyLayout();

static s3g::gui_layout::EncoderTitleBand conduitTitleBand()
{
    auto band = s3g::gui_layout::macroTitleBand(
        kConduitFamilyLayout.canvas);
    band.randomButton = { 512.0, 13.0, 66.0, 15.0 };
    return band;
}

static void applyConduitParams(Plugin& plugin,
    const s3g::ProcessorConduitParams& next)
{
    plugin.conduit.setParams(next);
    plugin.params = plugin.conduit.params();
    plugin.conduitRight.setParams(plugin.params);
    if (plugin.hostTail && plugin.hostTail->changed) {
        plugin.hostTail->changed(plugin.host);
    }
}

@interface S3GProcessorConduitView : NSView {
    Plugin* _plugin;
    int _dragSlider;
    int _openMenu;
    int _factoryPresetIndex;
    float _activityHistory[kContainmentHistorySize];
    float _reductionHistory[kContainmentHistorySize];
    uint32_t _historyWrite;
    NSTimer* _timer;
    char _titlePresetName[64];
}
- (id)initWithPlugin:(Plugin*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)applyFactoryPreset:(int)index;
- (void)drawSlider:(NSString*)name value:(NSString*)value
    norm:(CGFloat)norm y:(CGFloat)y
    panel:(const s3g::gui_layout::Panel&)panel
    attrs:(NSDictionary*)attrs;
- (void)drawContainmentHistory:(NSRect)field
    attrs:(NSDictionary*)attrs
    style:(const s3g::clap_gui::Style&)style;
- (void)updateSlider:(NSPoint)point;
@end

static NSColor* conduitColor(int rgb)
{
    return s3g::clap_gui::color(rgb);
}

@implementation S3GProcessorConduitView
- (id)initWithPlugin:(Plugin*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragSlider = -1;
        _openMenu = 0;
        _factoryPresetIndex = 0;
        std::fill_n(_activityHistory, kContainmentHistorySize, 0.0f);
        std::fill_n(_reductionHistory, kContainmentHistorySize, 0.0f);
        _historyWrite = 0u;
        _timer = nil;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
            s3g::processorConduitFactoryPresetInfo(0u).name);
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (void)dealloc { [self stopRefreshTimer]; [super dealloc]; }

- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [NSTimer timerWithTimeInterval:1.0 / 20.0
        target:self selector:@selector(refresh:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}

- (void)stopRefreshTimer
{
    if (_timer) {
        [_timer invalidate];
        _timer = nil;
    }
}

- (void)applyFactoryPreset:(int)index
{
    if (!_plugin) return;
    index = std::clamp(index, 0,
        static_cast<int>(s3g::kProcessorConduitFactoryPresetCount - 1u));
    applyConduitParams(*_plugin, s3g::processorConduitFactoryPreset(
        static_cast<uint32_t>(index), _plugin->params));
    _factoryPresetIndex = index;
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
        s3g::processorConduitFactoryPresetInfo(
            static_cast<uint32_t>(index)).name);
}

- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    if (![self isHidden] && _plugin
        && s3g::clap_support::hostAppIsActive()) {
        const float activity = std::clamp(
            _plugin->materialActivity.load(std::memory_order_relaxed),
            0.0f, 1.0f);
        const float activityDb = 20.0f * std::log10(
            std::max(activity, 0.000001f));
        _activityHistory[_historyWrite] = std::clamp(
            (activityDb + 60.0f) / 60.0f, 0.0f, 1.0f);
        _reductionHistory[_historyWrite] = std::clamp(
            _plugin->governorReduction.load(std::memory_order_relaxed),
            0.0f, 1.0f);
        _historyWrite = (_historyWrite + 1u) % kContainmentHistorySize;
        [self setNeedsDisplay:YES];
    }
}

- (void)captureDocumentationHistorySample
{
    if (!_plugin) return;
    const float activity = std::clamp(
        _plugin->materialActivity.load(std::memory_order_relaxed),
        0.0f, 1.0f);
    const float activityDb = 20.0f * std::log10(
        std::max(activity, 0.000001f));
    _activityHistory[_historyWrite] = std::clamp(
        (activityDb + 60.0f) / 60.0f, 0.0f, 1.0f);
    _reductionHistory[_historyWrite] = std::clamp(
        _plugin->governorReduction.load(std::memory_order_relaxed),
        0.0f, 1.0f);
    _historyWrite = (_historyWrite + 1u) % kContainmentHistorySize;
    [self setNeedsDisplay:YES];
}

- (void)drawSlider:(NSString*)name value:(NSString*)value
    norm:(CGFloat)norm y:(CGFloat)y
    panel:(const s3g::gui_layout::Panel&)panel
    attrs:(NSDictionary*)attrs
{
    s3g::clap_gui::Style style;
    s3g::clap_gui::drawProcessorSlider(name, value, norm, y,
        panel.frame.x, panel.frame.width, attrs, attrs, style);
}

- (void)drawContainmentHistory:(NSRect)field
    attrs:(NSDictionary*)attrs
    style:(const s3g::clap_gui::Style&)style
{
    [style.strip setFill];
    NSRectFill(field);
    [style.grid setStroke];
    NSFrameRect(field);

    const float activity = std::clamp(
        _plugin->materialActivity.load(std::memory_order_relaxed),
        0.0f, 1.0f);
    const float activityDb = 20.0f * std::log10(
        std::max(activity, 0.000001f));
    const float reduction = std::clamp(
        _plugin->governorReduction.load(std::memory_order_relaxed),
        0.0f, 1.0f);
    NSString* summary = activity > 0.000001f
        ? [NSString stringWithFormat:@"ENERGY %+.0f DB   CUT %.0f%%",
            activityDb, reduction * 100.0f]
        : [NSString stringWithFormat:@"ENERGY -INF DB   CUT %.0f%%",
            reduction * 100.0f];
    [summary drawAtPoint:NSMakePoint(field.origin.x + 10.0,
        field.origin.y + 7.0) withAttributes:attrs];

    const CGFloat labelWidth = 38.0;
    const NSRect energyPlot = NSMakeRect(
        field.origin.x + labelWidth, field.origin.y + 29.0,
        field.size.width - labelWidth - 10.0, 55.0);
    const NSRect reductionPlot = NSMakeRect(
        energyPlot.origin.x, field.origin.y + 95.0,
        energyPlot.size.width, 55.0);
    const auto drawPlotFrame = [&](NSRect plot) {
        [style.bg setFill];
        NSRectFill(plot);
        [style.grid setStroke];
        NSFrameRect(plot);
        [style.grid setFill];
        NSRectFill(NSMakeRect(plot.origin.x,
            std::floor(NSMidY(plot)), plot.size.width, 1.0));
        for (uint32_t division = 1u; division < 4u; ++division) {
            const CGFloat x = plot.origin.x + plot.size.width
                * static_cast<CGFloat>(division) / 4.0;
            NSRectFill(NSMakeRect(std::floor(x), plot.origin.y,
                1.0, plot.size.height));
        }
    };
    drawPlotFrame(energyPlot);
    drawPlotFrame(reductionPlot);

    [@"ENG" drawAtPoint:NSMakePoint(field.origin.x + 10.0,
        energyPlot.origin.y + 20.0) withAttributes:attrs];
    [@"CUT" drawAtPoint:NSMakePoint(field.origin.x + 10.0,
        reductionPlot.origin.y + 20.0) withAttributes:attrs];

    const auto drawHistory = [&](NSRect plot, const float* history,
        NSColor* color, CGFloat width) {
        NSBezierPath* path = [NSBezierPath bezierPath];
        [path setLineWidth:width];
        for (uint32_t sample = 0u;
             sample < kContainmentHistorySize; ++sample) {
            const uint32_t index = (_historyWrite + sample)
                % kContainmentHistorySize;
            const CGFloat normalized = std::clamp<CGFloat>(
                history[index], 0.0, 1.0);
            const CGFloat x = plot.origin.x + 2.0
                + (plot.size.width - 4.0)
                    * static_cast<CGFloat>(sample)
                    / static_cast<CGFloat>(kContainmentHistorySize - 1u);
            const CGFloat y = NSMaxY(plot) - 2.0
                - normalized * (plot.size.height - 4.0);
            if (sample == 0u) {
                [path moveToPoint:NSMakePoint(x, y)];
            } else {
                [path lineToPoint:NSMakePoint(x, y)];
            }
        }
        [color setStroke];
        [path stroke];
    };
    drawHistory(energyPlot, _activityHistory, style.text, 1.5);
    drawHistory(reductionPlot, _reductionHistory, style.fill, 2.0);

    NSString* state = activityDb < -54.0f && reduction < 0.01f
        ? @"QUIET" : (reduction >= 0.12f
            ? @"MASKING" : (reduction >= 0.01f ? @"TRIMMING" : @"OPEN"));
    NSString* footer = [NSString stringWithFormat:
        @"8S HISTORY   %@   BODY %.0f HZ", state,
        _plugin->fundamentalHz.load(std::memory_order_relaxed)];
    [footer drawAtPoint:NSMakePoint(field.origin.x + 10.0,
        NSMaxY(field) - 24.0) withAttributes:attrs];
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    s3g::clap_gui::Style style;
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSDictionary* label = s3g::clap_gui::softLabelAttrs();
    NSDictionary* small = s3g::clap_gui::softValueAttrs();
    const auto& family = kConduitFamilyLayout;
    const auto titleBand = conduitTitleBand();
    s3g::clap_gui::drawMacroTitleBand(
        @"s3g PROCESSOR CONDUIT",
        [NSString stringWithUTF8String:_titlePresetName],
        s3g::clap_gui::peakDbText(
            _plugin->outputPeak.load(std::memory_order_relaxed)),
        titleBand, style);
    s3g::clap_gui::drawHeaderActionButton(
        s3g::clap_gui::cocoaRect(titleBand.randomButton),
        s3g::clap_gui::cocoaRect(titleBand.randomButton),
        @"RANDOM", label, style);

    const auto drawPanel = [&](NSString* name,
        const s3g::gui_layout::Panel& panel) {
        s3g::clap_gui::drawPanelFrame(panel.frame.x, panel.frame.y,
            panel.frame.width, panel.frame.height, style);
        s3g::clap_gui::drawPanelHeader(name, true, panel.frame.x,
            panel.frame.y, panel.frame.width,
            s3g::gui_layout::kStandardMetrics.headerHeight, label, style);
    };
    drawPanel(@"OUTPUT", family.output);
    drawPanel(@"VIRTUAL MATERIAL CHANNEL", family.engine);
    drawPanel(@"PEDAL", family.relationships);
    drawPanel(@"CONTAINMENT HISTORY", family.containment);
    drawPanel(@"PA / TIME / SPACE", family.preview);

    const auto& prm = _plugin->params;
    [self drawSlider:@"OUT"
        value:[NSString stringWithFormat:@"%+.1f dB", prm.outputGainDb]
        norm:(prm.outputGainDb + 60.0f) / 66.0f
        y:s3g::gui_layout::rowY(family.output, 0u)
        panel:family.output attrs:small];
    [self drawSlider:@"MIX"
        value:[NSString stringWithFormat:@"%.0f%%", prm.mix * 100.0f]
        norm:prm.mix y:s3g::gui_layout::rowY(family.output, 1u)
        panel:family.output attrs:small];
    s3g::clap_gui::drawProcessorMenu(@"MATERIAL",
        [NSString stringWithUTF8String:
            s3g::processorConduitMaterialName(prm.material)],
        s3g::gui_layout::rowY(family.engine, 0u),
        family.engine.frame.x, family.engine.frame.width,
        label, small, style);
    s3g::clap_gui::drawProcessorMenu(@"INPUT LISTEN",
        [NSString stringWithUTF8String:
            s3g::processorConduitInputListenName(prm.inputListen)],
        s3g::gui_layout::rowY(family.engine, 1u),
        family.engine.frame.x, family.engine.frame.width,
        label, small, style);

    const std::array<NSString*, 8u> names {
        @"INPUT", @"DRIVER", @"SIZE", @"TENSION",
        @"DAMPING", @"PICKUP", @"CONTACT", @"FEEDBACK"
    };
    const std::array<float, 8u> values {
        (prm.inputGainDb + 24.0f) / 60.0f, prm.driver, prm.size,
        prm.tension, prm.damping, prm.pickup, prm.contact, prm.feedback
    };
    for (uint32_t i = 0u; i < names.size(); ++i) {
        NSString* value = i == 0u
            ? [NSString stringWithFormat:@"%+.1f dB", prm.inputGainDb]
            : [NSString stringWithFormat:@"%.0f%%", values[i] * 100.0f];
        [self drawSlider:names[i] value:value norm:values[i]
            y:s3g::gui_layout::rowY(family.engine, i + 2u)
            panel:family.engine attrs:small];
    }

    s3g::clap_gui::drawProcessorMenu(@"PEDAL",
        [NSString stringWithUTF8String:
            s3g::processorConduitPedalName(prm.pedal)],
        s3g::gui_layout::rowY(family.relationships, 0u),
        family.relationships.frame.x, family.relationships.frame.width,
        label, small, style);
    s3g::clap_gui::drawProcessorMenu(@"POSITION",
        [NSString stringWithUTF8String:
            s3g::processorConduitPedalPositionName(prm.pedalPosition)],
        s3g::gui_layout::rowY(family.relationships, 1u),
        family.relationships.frame.x, family.relationships.frame.width,
        label, small, style);
    [self drawSlider:@"MIX"
        value:[NSString stringWithFormat:@"%.0f%%", prm.pedalMix * 100.0f]
        norm:prm.pedalMix
        y:s3g::gui_layout::rowY(family.relationships, 2u)
        panel:family.relationships attrs:small];
    [self drawSlider:@"DRIVE"
        value:[NSString stringWithFormat:@"%.0f%%", prm.pedalDrive * 100.0f]
        norm:prm.pedalDrive
        y:s3g::gui_layout::rowY(family.relationships, 3u)
        panel:family.relationships attrs:small];
    [self drawSlider:@"TONE"
        value:[NSString stringWithFormat:@"%.0f%%", prm.pedalTone * 100.0f]
        norm:prm.pedalTone
        y:s3g::gui_layout::rowY(family.relationships, 4u)
        panel:family.relationships attrs:small];

    [self drawSlider:@"OCT DOWN"
        value:[NSString stringWithFormat:@"%.0f%%", prm.octaveDown * 100.0f]
        norm:prm.octaveDown
        y:s3g::gui_layout::rowY(family.preview, 0u)
        panel:family.preview attrs:small];
    [self drawSlider:@"DRAG"
        value:[NSString stringWithFormat:@"%.0f%%", prm.octaveDrag * 100.0f]
        norm:prm.octaveDrag
        y:s3g::gui_layout::rowY(family.preview, 1u)
        panel:family.preview attrs:small];
    [self drawSlider:@"PA DRIVE"
        value:[NSString stringWithFormat:@"%.0f%%", prm.paDrive * 100.0f]
        norm:prm.paDrive
        y:s3g::gui_layout::rowY(family.preview, 2u)
        panel:family.preview attrs:small];
    [self drawSlider:@"THROW"
        value:[NSString stringWithFormat:@"%.0f%%", prm.micMotion * 100.0f]
        norm:prm.micMotion
        y:s3g::gui_layout::rowY(family.preview, 3u)
        panel:family.preview attrs:small];
    [self drawSlider:@"CHAMBER"
        value:[NSString stringWithFormat:@"%.0f%%", prm.chamber * 100.0f]
        norm:prm.chamber
        y:s3g::gui_layout::rowY(family.preview, 4u)
        panel:family.preview attrs:small];
    [self drawSlider:@"WIDTH"
        value:[NSString stringWithFormat:@"%.0f%%", prm.stereoWidth * 100.0f]
        norm:prm.stereoWidth
        y:s3g::gui_layout::rowY(family.preview, 5u)
        panel:family.preview attrs:small];
    NSString* timeText = [NSString stringWithFormat:
        @"%@  PATH %.1fMS  CUT %.0f%%  BREAK %.0f%%  MIC %+.2f",
        _plugin->audioConfigId == kStereoInputConfigId
            ? [NSString stringWithUTF8String:
                s3g::processorConduitInputListenName(prm.inputListen)]
            : @"MONO PORT",
        _plugin->propagationMs.load(std::memory_order_relaxed),
        _plugin->governorReduction.load(std::memory_order_relaxed) * 100.0f,
        _plugin->breakupActivity.load(std::memory_order_relaxed) * 100.0f,
        _plugin->micPosition.load(std::memory_order_relaxed)];
    [timeText drawAtPoint:NSMakePoint(family.preview.frame.x + 14.0,
        family.preview.frame.y + family.preview.frame.height - 21.0)
        withAttributes:small];

    const float materialActivity = std::clamp(
        _plugin->materialActivity.load(std::memory_order_relaxed),
        0.0f, 1.0f);
    const float materialDb = 20.0f * std::log10(
        std::max(materialActivity, 0.000001f));
    [@"BODY" drawAtPoint:NSMakePoint(family.containment.frame.x + 16.0,
        family.containmentMeter.y - 2.0) withAttributes:label];

    NSRect governorField =
        s3g::clap_gui::cocoaRect(family.containmentField);
    [self drawContainmentHistory:governorField attrs:small style:style];

    NSRect activityTrack =
        s3g::clap_gui::cocoaRect(family.containmentMeter);
    [conduitColor(0x111111) setFill];
    NSRectFill(activityTrack);
    [conduitColor(0x444444) setStroke];
    NSFrameRect(activityTrack);
    NSRect activityFill = NSInsetRect(activityTrack, 1.0, 1.0);
    const CGFloat activityDisplay = std::clamp<CGFloat>(
        (materialDb + 60.0f) / 60.0f, 0.0, 1.0);
    activityFill.size.width *= activityDisplay;
    [conduitColor(0xb8b8b8) setFill];
    NSRectFill(activityFill);

    const NSRect panicRect =
        s3g::clap_gui::cocoaRect(family.panicButton);
    [conduitColor(0x161616) setFill];
    NSRectFill(panicRect);
    [conduitColor(0x565656) setStroke];
    NSFrameRect(panicRect);
    const NSSize panicTextSize = [@"PANIC" sizeWithAttributes:label];
    [@"PANIC" drawAtPoint:NSMakePoint(
        panicRect.origin.x + (panicRect.size.width - panicTextSize.width) * 0.5,
        panicRect.origin.y + (panicRect.size.height - panicTextSize.height) * 0.5)
        withAttributes:label];

    if (_openMenu == 1) {
        static NSString* items[] = {
            @"METAL", @"GLASS", @"PLASTIC", @"WOOD", @"WATER", @"SKIN",
            @"DIRECT", @"METAL VESSEL", @"GLASS VESSEL", @"PLASTIC VESSEL",
            @"DEEP BRONZE", @"TIERED BRONZE", @"BROAD BRONZE",
            @"BRIGHT BRONZE", @"CARBON LAM.", @"GLASS PLATE", @"STEEL SHELL",
            @"ALUM. PLATE", @"PORCELAIN", @"EARTHENWARE", @"SPRUCE PLATE",
            @"TENSIONED SKIN", @"LOADED MEM.", @"COUPLED MEM.",
            @"CAVITY MEM.", @"LOOSE MEM."
        };
        static_assert(std::size(items) == s3g::kProcessorConduitMaterialCount);
        constexpr CGFloat itemHeight = 18.0;
        constexpr uint32_t columnItems = 13u;
        const CGFloat menuWidth = s3g::gui_layout::processorMenuWidth(
            family.engine.frame.width);
        const NSRect firstMenuRect = NSMakeRect(
            s3g::gui_layout::processorControlX(family.engine.frame.x),
            s3g::gui_layout::rowY(family.engine, 0u) + 17.0,
            menuWidth, itemHeight * columnItems);
        const NSRect secondMenuRect = NSOffsetRect(firstMenuRect,
            menuWidth, 0.0);
        const int selected = static_cast<int>(prm.material);
        s3g::clap_gui::drawDropdownMenu(firstMenuRect, itemHeight, items,
            columnItems, selected < static_cast<int>(columnItems)
                ? selected : -1, -1, small, style);
        s3g::clap_gui::drawDropdownMenu(secondMenuRect, itemHeight,
            items + columnItems, columnItems,
            selected >= static_cast<int>(columnItems)
                ? selected - static_cast<int>(columnItems) : -1,
            -1, small, style);
    }
    if (_openMenu == kInputListenMenuId) {
        static NSString* items[] = {
            @"CHANNEL 1", @"CHANNEL 2", @"SUM MONO", @"STEREO"
        };
        static_assert(std::size(items)
            == s3g::kProcessorConduitInputListenCount);
        constexpr CGFloat itemHeight = 18.0;
        const NSRect menuRect = NSMakeRect(
            s3g::gui_layout::processorControlX(family.engine.frame.x),
            s3g::gui_layout::rowY(family.engine, 1u) + 17.0,
            s3g::gui_layout::processorMenuWidth(family.engine.frame.width),
            itemHeight * static_cast<CGFloat>(std::size(items)));
        s3g::clap_gui::drawDropdownMenu(menuRect, itemHeight, items,
            static_cast<uint32_t>(std::size(items)),
            static_cast<int>(prm.inputListen), -1, small, style);
    }
    if (_openMenu == 2) {
        static NSString* items[] = {
            @"SHRED", @"WOOL", @"RAT", @"ZONE A",
            @"ZONE B", @"FUZZ I", @"FUZZ II", @"DIODE"
        };
        static_assert(std::size(items) == s3g::kProcessorConduitPedalCount);
        constexpr CGFloat itemHeight = 18.0;
        const NSRect menuRect = NSMakeRect(
            s3g::gui_layout::processorControlX(
                family.relationships.frame.x),
            s3g::gui_layout::rowY(family.relationships, 0u) + 17.0,
            s3g::gui_layout::processorMenuWidth(
                family.relationships.frame.width),
            itemHeight * static_cast<CGFloat>(std::size(items)));
        s3g::clap_gui::drawDropdownMenu(menuRect, itemHeight, items,
            static_cast<uint32_t>(std::size(items)),
            static_cast<int>(prm.pedal), -1, small, style);
    }
    if (_openMenu == kPedalPositionMenuId) {
        static NSString* items[] = {
            @"PRE DRIVER", @"PIEZO > PA", @"MIC > LOOP"
        };
        static_assert(std::size(items)
            == s3g::kProcessorConduitPedalPositionCount);
        constexpr CGFloat itemHeight = 18.0;
        const NSRect menuRect = NSMakeRect(
            s3g::gui_layout::processorControlX(
                family.relationships.frame.x),
            s3g::gui_layout::rowY(family.relationships, 1u) + 17.0,
            s3g::gui_layout::processorMenuWidth(
                family.relationships.frame.width),
            itemHeight * static_cast<CGFloat>(std::size(items)));
        s3g::clap_gui::drawDropdownMenu(menuRect, itemHeight, items,
            static_cast<uint32_t>(std::size(items)),
            static_cast<int>(prm.pedalPosition), -1, small, style);
    }
    if (_openMenu == kFactoryPresetMenuId) {
        NSString* items[s3g::kProcessorConduitFactoryPresetCount] {};
        for (uint32_t index = 0u;
             index < s3g::kProcessorConduitFactoryPresetCount; ++index) {
            items[index] = [NSString stringWithUTF8String:
                s3g::processorConduitFactoryPresetInfo(index).name];
        }
        constexpr CGFloat itemHeight = 18.0;
        const auto band = conduitTitleBand();
        const NSRect anchor = s3g::clap_gui::cocoaRect(band.presetMenu);
        const NSRect menuRect = NSMakeRect(anchor.origin.x,
            NSMaxY(anchor) + 2.0, anchor.size.width,
            itemHeight * static_cast<CGFloat>(
                s3g::kProcessorConduitFactoryPresetCount));
        s3g::clap_gui::drawDropdownMenu(menuRect, itemHeight, items,
            s3g::kProcessorConduitFactoryPresetCount,
            _factoryPresetIndex, -1, small, style);
    }
}

- (void)updateSlider:(NSPoint)point
{
    const auto& family = kConduitFamilyLayout;
    const bool outputSlider = _dragSlider == kMixParamId
        || _dragSlider == kOutputParamId;
    const bool pedalSlider = _dragSlider == kPedalDriveParamId
        || _dragSlider == kPedalToneParamId
        || _dragSlider == kPedalMixParamId;
    const bool spaceSlider = _dragSlider >= kOctaveDownParamId
        && _dragSlider <= kStereoWidthParamId;
    const auto& panel = outputSlider ? family.output
        : (pedalSlider ? family.relationships
            : (spaceSlider ? family.preview : family.engine));
    const double controlX =
        s3g::gui_layout::processorControlX(panel.frame.x);
    const double trackWidth =
        s3g::gui_layout::processorTrackWidth(panel.frame.width);
    const double normalized = std::clamp(
        (point.x - controlX) / trackWidth, 0.0, 1.0);
    switch (_dragSlider) {
    case kInputParamId:
        applyParam(*_plugin, kInputParamId, -24.0 + normalized * 60.0); break;
    case kDriverParamId: applyParam(*_plugin, kDriverParamId, normalized); break;
    case kSizeParamId: applyParam(*_plugin, kSizeParamId, normalized); break;
    case kTensionParamId: applyParam(*_plugin, kTensionParamId, normalized); break;
    case kDampingParamId: applyParam(*_plugin, kDampingParamId, normalized); break;
    case kPickupParamId: applyParam(*_plugin, kPickupParamId, normalized); break;
    case kContactParamId: applyParam(*_plugin, kContactParamId, normalized); break;
    case kFeedbackParamId: applyParam(*_plugin, kFeedbackParamId, normalized); break;
    case kMixParamId: applyParam(*_plugin, kMixParamId, normalized); break;
    case kOutputParamId:
        applyParam(*_plugin, kOutputParamId, -60.0 + normalized * 66.0); break;
    case kPedalDriveParamId:
        applyParam(*_plugin, kPedalDriveParamId, normalized); break;
    case kPedalToneParamId:
        applyParam(*_plugin, kPedalToneParamId, normalized); break;
    case kOctaveDownParamId:
        applyParam(*_plugin, kOctaveDownParamId, normalized); break;
    case kOctaveDragParamId:
        applyParam(*_plugin, kOctaveDragParamId, normalized); break;
    case kPaDriveParamId:
        applyParam(*_plugin, kPaDriveParamId, normalized); break;
    case kMicMotionParamId:
        applyParam(*_plugin, kMicMotionParamId, normalized); break;
    case kChamberParamId:
        applyParam(*_plugin, kChamberParamId, normalized); break;
    case kStereoWidthParamId:
        applyParam(*_plugin, kStereoWidthParamId, normalized); break;
    case kPedalMixParamId:
        applyParam(*_plugin, kPedalMixParamId, normalized); break;
    default: break;
    }
    if (_dragSlider != kInputParamId && _dragSlider != kMixParamId
        && _dragSlider != kOutputParamId) {
        _factoryPresetIndex = -1;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
            "CUSTOM");
    }
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    const auto& family = kConduitFamilyLayout;
    if (_openMenu == kMaterialMenuId) {
        constexpr CGFloat itemHeight = 18.0;
        constexpr uint32_t columnItems = 13u;
        const CGFloat menuWidth = s3g::gui_layout::processorMenuWidth(
            family.engine.frame.width);
        const NSRect firstMenuRect = NSMakeRect(
            s3g::gui_layout::processorControlX(family.engine.frame.x),
            s3g::gui_layout::rowY(family.engine, 0u) + 17.0,
            menuWidth, itemHeight * columnItems);
        const NSRect secondMenuRect = NSOffsetRect(firstMenuRect,
            menuWidth, 0.0);
        int hit = s3g::clap_gui::dropdownHitIndex(point, firstMenuRect,
            itemHeight, columnItems);
        if (hit < 0) {
            const int secondHit = s3g::clap_gui::dropdownHitIndex(point,
                secondMenuRect, itemHeight, columnItems);
            if (secondHit >= 0) hit = secondHit
                + static_cast<int>(columnItems);
        }
        _openMenu = 0;
        if (hit >= 0) {
            applyParam(*_plugin, kMaterialParamId, hit);
            _factoryPresetIndex = -1;
            std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                "CUSTOM");
        }
        [self setNeedsDisplay:YES];
        return;
    }
    if (_openMenu == kInputListenMenuId) {
        constexpr CGFloat itemHeight = 18.0;
        const NSRect menuRect = NSMakeRect(
            s3g::gui_layout::processorControlX(family.engine.frame.x),
            s3g::gui_layout::rowY(family.engine, 1u) + 17.0,
            s3g::gui_layout::processorMenuWidth(family.engine.frame.width),
            itemHeight * static_cast<CGFloat>(
                s3g::kProcessorConduitInputListenCount));
        const int hit = s3g::clap_gui::dropdownHitIndex(point, menuRect,
            itemHeight, s3g::kProcessorConduitInputListenCount);
        _openMenu = 0;
        if (hit >= 0) {
            applyParam(*_plugin, kInputListenParamId, hit);
        }
        [self setNeedsDisplay:YES];
        return;
    }
    if (_openMenu == kPedalMenuId) {
        constexpr CGFloat itemHeight = 18.0;
        const NSRect menuRect = NSMakeRect(
            s3g::gui_layout::processorControlX(
                family.relationships.frame.x),
            s3g::gui_layout::rowY(family.relationships, 0u) + 17.0,
            s3g::gui_layout::processorMenuWidth(
                family.relationships.frame.width),
            itemHeight * static_cast<CGFloat>(
                s3g::kProcessorConduitPedalCount));
        const int hit = s3g::clap_gui::dropdownHitIndex(point, menuRect,
            itemHeight, s3g::kProcessorConduitPedalCount);
        _openMenu = 0;
        if (hit >= 0) {
            applyParam(*_plugin, kPedalParamId, hit);
            _factoryPresetIndex = -1;
            std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                "CUSTOM");
        }
        [self setNeedsDisplay:YES];
        return;
    }

    if (_openMenu == kPedalPositionMenuId) {
        constexpr CGFloat itemHeight = 18.0;
        const NSRect menuRect = NSMakeRect(
            s3g::gui_layout::processorControlX(
                family.relationships.frame.x),
            s3g::gui_layout::rowY(family.relationships, 1u) + 17.0,
            s3g::gui_layout::processorMenuWidth(
                family.relationships.frame.width),
            itemHeight * static_cast<CGFloat>(
                s3g::kProcessorConduitPedalPositionCount));
        const int hit = s3g::clap_gui::dropdownHitIndex(point, menuRect,
            itemHeight, s3g::kProcessorConduitPedalPositionCount);
        _openMenu = 0;
        if (hit >= 0) {
            applyParam(*_plugin, kPedalPositionParamId, hit);
            _factoryPresetIndex = -1;
            std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                "CUSTOM");
        }
        [self setNeedsDisplay:YES];
        return;
    }

    if (_openMenu == kFactoryPresetMenuId) {
        constexpr CGFloat itemHeight = 18.0;
        const auto band = conduitTitleBand();
        const NSRect anchor = s3g::clap_gui::cocoaRect(band.presetMenu);
        const NSRect menuRect = NSMakeRect(anchor.origin.x,
            NSMaxY(anchor) + 2.0, anchor.size.width,
            itemHeight * static_cast<CGFloat>(
                s3g::kProcessorConduitFactoryPresetCount));
        const int hit = s3g::clap_gui::dropdownHitIndex(point, menuRect,
            itemHeight, s3g::kProcessorConduitFactoryPresetCount);
        _openMenu = 0;
        if (hit >= 0) [self applyFactoryPreset:hit];
        [self setNeedsDisplay:YES];
        return;
    }

    const auto titleBand = conduitTitleBand();
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.presetMenu))) {
        _openMenu = kFactoryPresetMenuId;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.randomButton))) {
        applyConduitParams(*_plugin, s3g::processorConduitRandomParams(
            _plugin->params, arc4random()));
        _factoryPresetIndex = -1;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
            "RANDOM");
        [self setNeedsDisplay:YES];
        return;
    }
    if (s3g::clap_gui::handleProcessorTitleClick(point,
            &_plugin->plugin, @"Processor Conduit", titleBand,
            _titlePresetName, sizeof(_titlePresetName), kOutputParamId)) {
        [self setNeedsDisplay:YES];
        return;
    }

    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(family.panicButton))) {
        _plugin->panicRequested.store(true, std::memory_order_release);
        [self setNeedsDisplay:YES];
        return;
    }

    const NSRect materialBox = NSMakeRect(
        s3g::gui_layout::processorControlX(family.engine.frame.x),
        s3g::gui_layout::rowY(family.engine, 0u) - 5.0,
        s3g::gui_layout::processorMenuWidth(family.engine.frame.width),
        24.0);
    if (NSPointInRect(point, materialBox)) {
        _openMenu = kMaterialMenuId;
        [self setNeedsDisplay:YES];
        return;
    }

    const NSRect inputListenBox = NSMakeRect(
        s3g::gui_layout::processorControlX(family.engine.frame.x),
        s3g::gui_layout::rowY(family.engine, 1u) - 5.0,
        s3g::gui_layout::processorMenuWidth(family.engine.frame.width),
        24.0);
    if (NSPointInRect(point, inputListenBox)) {
        _openMenu = kInputListenMenuId;
        [self setNeedsDisplay:YES];
        return;
    }

    const NSRect pedalBox = NSMakeRect(
        s3g::gui_layout::processorControlX(family.relationships.frame.x),
        s3g::gui_layout::rowY(family.relationships, 0u) - 5.0,
        s3g::gui_layout::processorMenuWidth(
            family.relationships.frame.width),
        24.0);
    if (NSPointInRect(point, pedalBox)) {
        _openMenu = kPedalMenuId;
        [self setNeedsDisplay:YES];
        return;
    }

    const NSRect positionBox = NSMakeRect(
        s3g::gui_layout::processorControlX(family.relationships.frame.x),
        s3g::gui_layout::rowY(family.relationships, 1u) - 5.0,
        s3g::gui_layout::processorMenuWidth(
            family.relationships.frame.width),
        24.0);
    if (NSPointInRect(point, positionBox)) {
        _openMenu = kPedalPositionMenuId;
        [self setNeedsDisplay:YES];
        return;
    }

    const clap_id engineParamIds[] {
        kInputParamId, kDriverParamId, kSizeParamId, kTensionParamId,
        kDampingParamId, kPickupParamId, kContactParamId, kFeedbackParamId
    };
    const clap_id outputParamIds[] { kOutputParamId, kMixParamId };
    const clap_id pedalParamIds[] {
        kPedalMixParamId, kPedalDriveParamId, kPedalToneParamId
    };
    const clap_id spaceParamIds[] {
        kOctaveDownParamId, kOctaveDragParamId, kPaDriveParamId,
        kMicMotionParamId, kChamberParamId, kStereoWidthParamId
    };
    const auto beginSlider = [&](clap_id paramId) {
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(event,
                &_plugin->plugin, paramId, &defaultValue)) {
            applyParam(*_plugin, paramId, defaultValue);
            _dragSlider = -1;
        } else {
            _dragSlider = static_cast<int>(paramId);
            [self updateSlider:point];
        }
        [self setNeedsDisplay:YES];
    };
    for (uint32_t i = 0u; i < std::size(outputParamIds); ++i) {
        if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
                s3g::gui_layout::sliderHitRect(family.output, i)))) {
            beginSlider(outputParamIds[i]);
            return;
        }
    }
    for (uint32_t i = 0u; i < std::size(engineParamIds); ++i) {
        if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
                s3g::gui_layout::sliderHitRect(family.engine, i + 2u)))) {
            beginSlider(engineParamIds[i]);
            return;
        }
    }
    for (uint32_t i = 0u; i < std::size(pedalParamIds); ++i) {
        if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
                s3g::gui_layout::sliderHitRect(
                    family.relationships, i + 2u)))) {
            beginSlider(pedalParamIds[i]);
            return;
        }
    }
    for (uint32_t i = 0u; i < std::size(spaceParamIds); ++i) {
        if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
                s3g::gui_layout::sliderHitRect(
                    family.preview, i)))) {
            beginSlider(spaceParamIds[i]);
            return;
        }
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    if (_dragSlider > 0) {
        [self updateSlider:[self convertPoint:[event locationInWindow]
            fromView:nil]];
    }
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragSlider = -1;
}
@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api,
    bool isFloating)
{
    return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*, const char** api,
    bool* isFloating)
{
    if (!api || !isFloating) return false;
    *api = CLAP_WINDOW_API_COCOA;
    *isFloating = false;
    return true;
}

bool guiCreate(const clap_plugin_t* plugin, const char* api,
    bool isFloating)
{
    if (!guiIsApiSupported(plugin, api, isFloating)) return false;
    auto* p = self(plugin);
    if (p->guiView) return true;
    p->guiView = [[S3GProcessorConduitView alloc] initWithPlugin:p];
    if (!p->guiView) return false;
    if (!s3g::clap_gui::createResponsiveViewport(p->guiViewport,
            static_cast<NSView*>(p->guiView), kGuiWidth, kGuiHeight)) {
        [static_cast<NSView*>(p->guiView) release];
        p->guiView = nullptr;
        return false;
    }
    return true;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (p->guiView) {
        p->guiVisible = false;
        [static_cast<S3GProcessorConduitView*>(p->guiView)
            stopRefreshTimer];
        s3g::clap_gui::destroyResponsiveViewport(
            p->guiViewport, p->guiView);
    }
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }

bool guiGetSize(const clap_plugin_t* plugin,
    uint32_t* width, uint32_t* height)
{
    return s3g::clap_gui::getResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height);
}

bool guiCanResize(const clap_plugin_t*) { return true; }

bool guiGetResizeHints(const clap_plugin_t*,
    clap_gui_resize_hints_t* hints)
{
    return s3g::clap_gui::getResponsiveResizeHints(hints);
}

bool guiAdjustSize(const clap_plugin_t* plugin,
    uint32_t* width, uint32_t* height)
{
    return s3g::clap_gui::adjustResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height);
}

bool guiSetSize(const clap_plugin_t* plugin,
    uint32_t width, uint32_t height)
{
    return s3g::clap_gui::setResponsiveViewportSize(
        self(plugin)->guiViewport, width, height);
}

bool guiSetParent(const clap_plugin_t* plugin,
    const clap_window_t* window)
{
    if (!window || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0
        || !window->cocoa) {
        return false;
    }
    auto* p = self(plugin);
    return s3g::clap_gui::setResponsiveViewportParent(p->guiViewport,
        static_cast<NSView*>(window->cocoa), p->host);
}

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*)
{
    return false;
}
void guiSuggestTitle(const clap_plugin_t*, const char*) {}

bool guiShow(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(
            p->guiViewport, false)) {
        return false;
    }
    p->guiVisible = true;
    [static_cast<S3GProcessorConduitView*>(p->guiView) startRefreshTimer];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    p->guiVisible = false;
    [static_cast<S3GProcessorConduitView*>(p->guiView) stopRefreshTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(
        p->guiViewport, true);
}

const clap_plugin_gui_t guiExt {
    guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy,
    guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints,
    guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient,
    guiSuggestTitle, guiShow, guiHide
};

} // namespace
#endif

namespace {

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS_CONFIG) == 0)
        return &audioPortsConfig;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS_CONFIG_INFO) == 0
        || std::strcmp(id, CLAP_EXT_AUDIO_PORTS_CONFIG_INFO_COMPAT) == 0)
        return &audioPortsConfigInfo;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
    if (std::strcmp(id, CLAP_EXT_TAIL) == 0) return &tailExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_DISTORTION,
    CLAP_PLUGIN_FEATURE_STEREO,
    CLAP_PLUGIN_FEATURE_MONO,
    nullptr
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.processor-conduit",
    "s3g Processor Conduit",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "Governed live-mic vocal distortion through pedals, a virtual material channel, contact pickup, and octave-down drag.",
    features
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*,
    const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->hostTail = host && host->get_extension
        ? static_cast<const clap_host_tail_t*>(
            host->get_extension(host, CLAP_EXT_TAIL))
        : nullptr;
    p->plugin.desc = &descriptor;
    p->plugin.plugin_data = p;
    p->plugin.init = init;
    p->plugin.destroy = destroy;
    p->plugin.activate = activate;
    p->plugin.deactivate = deactivate;
    p->plugin.start_processing = startProcessing;
    p->plugin.stop_processing = stopProcessing;
    p->plugin.reset = reset;
    p->plugin.process = process;
    p->plugin.get_extension = pluginGetExtension;
    p->plugin.on_main_thread = onMainThread;
    return &p->plugin;
}

uint32_t factoryGetPluginCount(const clap_plugin_factory*) { return 1u; }

const clap_plugin_descriptor_t* factoryGetPluginDescriptor(
    const clap_plugin_factory*, uint32_t index)
{
    return index == 0u ? &descriptor : nullptr;
}

const clap_plugin_factory_t factory {
    factoryGetPluginCount, factoryGetPluginDescriptor, createPlugin
};

bool entryInit(const char*) { return true; }
void entryDeinit() {}

const void* entryGetFactory(const char* factoryId)
{
    return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0
        ? &factory : nullptr;
}

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory
};
