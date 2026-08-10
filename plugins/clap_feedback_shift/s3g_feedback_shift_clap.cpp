#include "s3g_feedback_shift.h"
#include "s3g_feedback_shift_presets.h"
#include "../common/s3g_clap_gui_param_queue.h"
#include "../common/s3g_clap_state_stream.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include "../common/s3g_clap_macos.h"
#include "../common/s3g_cocoa_gui.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {

constexpr uint32_t kStateMagic = 0x46533353u; // "S3SF"
constexpr uint32_t kStateVersion = 10u;
constexpr uint32_t kBaseStateVersion = 4u;
constexpr uint32_t kHoldCutStateVersion = 5u;
constexpr uint32_t kGranularStateVersion = 6u;
constexpr uint32_t kFeedbackSourceStateVersion = 7u;
constexpr uint32_t kLaneSendStateVersion = 8u;
constexpr uint32_t kPostGranulatorStateVersion = 9u;
constexpr uint32_t kGuiWidth = 1100u;
constexpr uint32_t kGuiHeight = 760u;

enum GlobalParamId : clap_id {
    kExciteParamId = 1u,
    kDriftParamId,
    kPulseDepthParamId,
    kPulseRateParamId,
    kPulseSyncParamId,
    kPulseDivisionParamId,
    kPulseShapeParamId,
    kOutputParamId,
    kRunParamId,
    kOutputModeParamId,
    kOutputRotationParamId,
};

constexpr clap_id kNodeParamBase = 1000u;
constexpr clap_id kNodeParamStride = 18u;
enum NodeParamOffset : clap_id {
    kNodeModeOffset = 0u,
    kNodePedalOffset,
    kNodeFrequencyOffset,
    kNodeRegenerationOffset,
    kNodeColorOffset,
    kNodeLevelOffset,
    kNodePedalAmountOffset,
    kNodePedalToneOffset,
    kNodePedalBiasOffset,
    kNodePedalMixOffset,
    kNodePedalExtra0Offset,
    kNodePedalExtra1Offset,
    kNodePedalExtra2Offset,
    kNodePedalExtra3Offset,
    kNodePedalExtra4Offset,
    kNodePedalExtra5Offset,
    kNodePedalExtra6Offset,
    kNodePedalExtra7Offset,
};

constexpr clap_id kMatrixParamBase = 2000u;
constexpr clap_id kAuxParamBase = 3000u;
enum AuxParamOffset : clap_id {
    kAuxPressOffset = 0u,
    kAuxSaturationOffset,
    kAuxFoldOffset,
    kAuxClipOffset,
    kAuxGrainSizeOffset,
    kAuxGrainDensityOffset,
    kAuxGrainScatterOffset,
    kAuxGrainPitchOffset,
    kAuxGrainEdgeOffset,
    kAuxTiltOffset,
    kAuxMixOffset,
    kAuxGrainMixOffset,
    kAuxSend0Offset,
    kAuxCoherenceOffset = kAuxSend0Offset + s3g::kFeedbackShiftChannels,
    kAuxLaneDriftOffset,
};
constexpr clap_id kExciterParamBase = 4000u;
constexpr clap_id kExciterParamStride = 2u;
enum ExciterParamOffset : clap_id {
    kExciterSourceOffset = 0u,
    kExciterGainOffset,
};
constexpr clap_id kMotionParamBase = 5000u;
constexpr clap_id kMotionParamStride = 4u;
enum MotionParamOffset : clap_id {
    kMotionSourceOffset = 0u,
    kMotionTargetOffset,
    kMotionDepthOffset,
    kMotionSlewOffset,
};
constexpr clap_id kMotionRateParamId = 6000u;
constexpr uint32_t kGlobalParamCount = 11u;
constexpr uint32_t kNodeParamCount =
    s3g::kFeedbackShiftChannels * kNodeParamStride;
constexpr uint32_t kLegacyParamCount = kGlobalParamCount + kNodeParamCount
    + s3g::kFeedbackShiftMatrixCells;
constexpr uint32_t kPreviousAuxParamCount = 11u;
constexpr uint32_t kPreviousParamCount =
    kLegacyParamCount + kPreviousAuxParamCount;
constexpr uint32_t kLegacyAuxProcessorParamCount = 12u;
constexpr uint32_t kFeedbackSourceParamCount =
    kLegacyParamCount + kLegacyAuxProcessorParamCount;
constexpr uint32_t kAuxSendParamCount = s3g::kFeedbackShiftChannels;
constexpr uint32_t kLaneSendParamCount =
    kFeedbackSourceParamCount + kAuxSendParamCount;
constexpr uint32_t kAuxParamCount =
    kLegacyAuxProcessorParamCount + kAuxSendParamCount + 2u;
constexpr uint32_t kPostGranulatorParamCount =
    kLegacyParamCount + kAuxParamCount;
constexpr uint32_t kExciterParamCount =
    s3g::kFeedbackShiftChannels * kExciterParamStride;
constexpr uint32_t kMotionParamCount =
    s3g::kFeedbackShiftChannels * kMotionParamStride;
constexpr uint32_t kParamCount = kPostGranulatorParamCount
    + kExciterParamCount + kMotionParamCount + 1u;

struct ParamRange {
    double minimum = 0.0;
    double maximum = 1.0;
    double defaultValue = 0.0;
    const char* unit = "value";
    bool stepped = false;
};

struct SavedStateHeader {
    uint32_t magic = kStateMagic;
    uint32_t version = kStateVersion;
    uint32_t valueCount = kParamCount;
    uint32_t reserved = 0u;
};

struct SavedState {
    SavedStateHeader header {};
    std::array<double, kParamCount> values {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    double sampleRate = 48000.0;
    double transportTempoBpm = 120.0;
    bool transportHasTempo = false;
    s3g::FeedbackShiftParams params { s3g::defaultFeedbackShiftParams() };
    s3g::FeedbackShift dsp {};
    std::array<std::atomic<double>, kParamCount> publishedParams {};
    s3g::clap_gui::ParamEventQueue<> guiParamEvents {};
    std::atomic<bool> guiRetryPending { false };
    std::atomic<bool> activated { false };
    std::atomic<bool> panicRequested { false };
    std::atomic<uint32_t> strikeMask { 0u };
    std::array<std::atomic<float>, s3g::kFeedbackShiftChannels> outputPeaks {};
    std::array<std::atomic<float>, s3g::kFeedbackShiftChannels> nodeActivity {};
    std::array<std::atomic<float>, s3g::kFeedbackShiftChannels> motionValues {};
    std::array<std::atomic<uint32_t>, s3g::kFeedbackShiftChannels>
        temporalPhases {};
    std::array<std::atomic<float>, s3g::kFeedbackShiftChannels>
        temporalProgress {};
    std::array<std::atomic<float>, s3g::kFeedbackShiftMatrixCells>
        routeActivity {};
    std::atomic<float> minimumGovernor { 1.0f };
    std::atomic<float> pulsePhase { 0.0f };
    std::atomic<float> auxActivity { 0.0f };
    std::atomic<float> auxGainReductionDb { 0.0f };
    std::atomic<float> auxGrainActivity { 0.0f };
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

clap_id nodeParamId(uint32_t node, clap_id offset)
{
    return kNodeParamBase + node * kNodeParamStride + offset;
}

clap_id matrixParamId(uint32_t destination, uint32_t source)
{
    return kMatrixParamBase
        + destination * s3g::kFeedbackShiftChannels + source;
}

clap_id auxParamId(uint32_t offset)
{
    return offset < kAuxParamCount
        ? kAuxParamBase + offset : CLAP_INVALID_ID;
}

clap_id auxSendParamId(uint32_t node)
{
    return node < s3g::kFeedbackShiftChannels
        ? auxParamId(kAuxSend0Offset + node) : CLAP_INVALID_ID;
}

clap_id exciterParamId(uint32_t node, clap_id offset)
{
    return node < s3g::kFeedbackShiftChannels
            && offset < kExciterParamStride
        ? kExciterParamBase + node * kExciterParamStride + offset
        : CLAP_INVALID_ID;
}

clap_id motionParamId(uint32_t node, clap_id offset)
{
    return node < s3g::kFeedbackShiftChannels
            && offset < kMotionParamStride
        ? kMotionParamBase + node * kMotionParamStride + offset
        : CLAP_INVALID_ID;
}

bool decodeAuxParam(clap_id id, clap_id& offset)
{
    if (id < kAuxParamBase || id >= kAuxParamBase + kAuxParamCount) {
        return false;
    }
    offset = id - kAuxParamBase;
    return true;
}

bool decodeExciterParam(clap_id id, uint32_t& node, clap_id& offset)
{
    if (id < kExciterParamBase
        || id >= kExciterParamBase + kExciterParamCount) return false;
    const clap_id relative = id - kExciterParamBase;
    node = relative / kExciterParamStride;
    offset = relative % kExciterParamStride;
    return node < s3g::kFeedbackShiftChannels;
}

bool decodeMotionParam(clap_id id, uint32_t& node, clap_id& offset)
{
    if (id < kMotionParamBase
        || id >= kMotionParamBase + kMotionParamCount) return false;
    const clap_id relative = id - kMotionParamBase;
    node = relative / kMotionParamStride;
    offset = relative % kMotionParamStride;
    return node < s3g::kFeedbackShiftChannels;
}

bool decodeNodeParam(clap_id id, uint32_t& node, clap_id& offset)
{
    if (id < kNodeParamBase || id >= kNodeParamBase + kNodeParamCount) {
        return false;
    }
    const clap_id relative = id - kNodeParamBase;
    node = relative / kNodeParamStride;
    offset = relative % kNodeParamStride;
    return node < s3g::kFeedbackShiftChannels;
}

bool decodeMatrixParam(clap_id id, uint32_t& destination, uint32_t& source)
{
    if (id < kMatrixParamBase
        || id >= kMatrixParamBase + s3g::kFeedbackShiftMatrixCells) {
        return false;
    }
    const uint32_t relative = id - kMatrixParamBase;
    destination = relative / s3g::kFeedbackShiftChannels;
    source = relative % s3g::kFeedbackShiftChannels;
    return true;
}

clap_id paramIdAtIndex(uint32_t index)
{
    if (index < kGlobalParamCount) return kExciteParamId + index;
    index -= kGlobalParamCount;
    if (index < kNodeParamCount) {
        const uint32_t node = index / kNodeParamStride;
        const clap_id offset = index % kNodeParamStride;
        return nodeParamId(node, offset);
    }
    index -= kNodeParamCount;
    if (index < s3g::kFeedbackShiftMatrixCells) {
        return kMatrixParamBase + index;
    }
    index -= s3g::kFeedbackShiftMatrixCells;
    if (index < kAuxParamCount) return auxParamId(index);
    index -= kAuxParamCount;
    if (index < kExciterParamCount) {
        return kExciterParamBase + index;
    }
    index -= kExciterParamCount;
    if (index < kMotionParamCount) {
        return kMotionParamBase + index;
    }
    index -= kMotionParamCount;
    return index == 0u ? kMotionRateParamId : CLAP_INVALID_ID;
}

uint32_t paramIndex(clap_id id)
{
    if (id >= kExciteParamId && id <= kOutputRotationParamId) {
        return id - kExciteParamId;
    }
    uint32_t node = 0u;
    clap_id offset = 0u;
    if (decodeNodeParam(id, node, offset)) {
        return kGlobalParamCount + node * kNodeParamStride + offset;
    }
    uint32_t destination = 0u;
    uint32_t source = 0u;
    if (decodeMatrixParam(id, destination, source)) {
        return kGlobalParamCount + kNodeParamCount
            + destination * s3g::kFeedbackShiftChannels + source;
    }
    clap_id auxOffset = 0u;
    if (decodeAuxParam(id, auxOffset)) {
        return kLegacyParamCount + auxOffset;
    }
    if (decodeExciterParam(id, node, offset)) {
        return kPostGranulatorParamCount
            + node * kExciterParamStride + offset;
    }
    if (decodeMotionParam(id, node, offset)) {
        return kPostGranulatorParamCount + kExciterParamCount
            + node * kMotionParamStride + offset;
    }
    if (id == kMotionRateParamId) return kParamCount - 1u;
    return kParamCount;
}

double rawParamValue(const s3g::FeedbackShiftParams& params, clap_id id)
{
    switch (id) {
    case kExciteParamId: return params.excite;
    case kDriftParamId: return params.drift;
    case kPulseDepthParamId: return params.pulseDepth;
    case kPulseRateParamId: return params.pulseRate;
    case kPulseSyncParamId: return params.pulseSync;
    case kPulseDivisionParamId: return params.pulseDivision;
    case kPulseShapeParamId:
        return static_cast<uint32_t>(params.pulseShape);
    case kOutputParamId: return params.outputGainDb;
    case kRunParamId: return params.run ? 1.0 : 0.0;
    case kOutputModeParamId:
        return static_cast<uint32_t>(params.outputMode);
    case kOutputRotationParamId: return params.outputRotationDeg;
    case kMotionRateParamId: return params.motionRate;
    default: break;
    }
    clap_id auxOffset = 0u;
    if (decodeAuxParam(id, auxOffset)) {
        if (auxOffset >= kAuxSend0Offset
            && auxOffset < kAuxSend0Offset + kAuxSendParamCount) {
            return params.auxSend[auxOffset - kAuxSend0Offset];
        }
        switch (auxOffset) {
        case kAuxPressOffset: return params.auxPress;
        case kAuxSaturationOffset: return params.auxSaturation;
        case kAuxFoldOffset: return params.auxFold;
        case kAuxClipOffset: return params.auxClip;
        case kAuxGrainSizeOffset: return params.auxGrainSize;
        case kAuxGrainDensityOffset: return params.auxGrainDensity;
        case kAuxGrainScatterOffset: return params.auxGrainScatter;
        case kAuxGrainPitchOffset: return params.auxGrainPitch;
        case kAuxGrainEdgeOffset: return params.auxGrainEdge;
        case kAuxTiltOffset: return params.auxTilt;
        case kAuxMixOffset: return params.auxMix;
        case kAuxGrainMixOffset: return params.auxGrainMix;
        case kAuxCoherenceOffset: return params.auxGrainCoherence;
        case kAuxLaneDriftOffset: return params.auxGrainLaneDrift;
        default: return 0.0;
        }
    }
    uint32_t appendedNode = 0u;
    clap_id appendedOffset = 0u;
    if (decodeExciterParam(id, appendedNode, appendedOffset)) {
        const auto& lane = params.nodes[appendedNode];
        return appendedOffset == kExciterSourceOffset
            ? static_cast<uint32_t>(lane.exciterSource)
            : lane.exciterGainDb;
    }
    if (decodeMotionParam(id, appendedNode, appendedOffset)) {
        const auto& lane = params.nodes[appendedNode];
        switch (appendedOffset) {
        case kMotionSourceOffset:
            return static_cast<uint32_t>(lane.motionSource);
        case kMotionTargetOffset:
            return static_cast<uint32_t>(lane.motionTarget);
        case kMotionDepthOffset: return lane.motionDepth;
        case kMotionSlewOffset: return lane.motionSlew;
        default: return 0.0;
        }
    }
    uint32_t node = 0u;
    clap_id offset = 0u;
    if (decodeNodeParam(id, node, offset)) {
        const auto& lane = params.nodes[node];
        switch (offset) {
        case kNodeModeOffset: return static_cast<uint32_t>(lane.mode);
        case kNodePedalOffset: return static_cast<uint32_t>(lane.pedal);
        case kNodeFrequencyOffset: return lane.frequencyHz;
        case kNodeRegenerationOffset: return lane.regeneration;
        case kNodeColorOffset: return lane.color;
        case kNodeLevelOffset: return lane.levelDb;
        case kNodePedalAmountOffset: return lane.pedalAmount;
        case kNodePedalToneOffset: return lane.pedalTone;
        case kNodePedalBiasOffset: return lane.pedalBias;
        case kNodePedalMixOffset: return lane.pedalMix;
        default:
            if (offset >= kNodePedalExtra0Offset
                && offset <= kNodePedalExtra7Offset) {
                return lane.pedalExtra[offset - kNodePedalExtra0Offset];
            }
            return 0.0;
        }
    }
    uint32_t destination = 0u;
    uint32_t source = 0u;
    if (decodeMatrixParam(id, destination, source)) {
        return params.matrix[destination * s3g::kFeedbackShiftChannels
            + source];
    }
    return 0.0;
}

bool paramRange(clap_id id, ParamRange& range)
{
    const auto defaults = s3g::defaultFeedbackShiftParams();
    range.defaultValue = rawParamValue(defaults, id);
    switch (id) {
    case kExciteParamId:
    case kDriftParamId:
    case kPulseDepthParamId:
    case kPulseRateParamId:
    case kMotionRateParamId:
        range = { 0.0, 1.0, range.defaultValue, "pct", false }; return true;
    case kPulseSyncParamId:
    case kRunParamId:
        range = { 0.0, 1.0, range.defaultValue, "bool", true }; return true;
    case kPulseDivisionParamId:
        range = { 0.0,
            static_cast<double>(s3g::kFeedbackPulseDivisionBeats.size() - 1u),
            range.defaultValue, "division", true }; return true;
    case kPulseShapeParamId:
        range = { 0.0,
            static_cast<double>(s3g::kFeedbackPulseShapeCount - 1u),
            range.defaultValue, "shape", true }; return true;
    case kOutputParamId:
        range = { -60.0, 6.0, range.defaultValue, "db", false }; return true;
    case kOutputModeParamId:
        range = { 0.0,
            static_cast<double>(s3g::kFeedbackShiftOutputModeCount - 1u),
            range.defaultValue, "output-mode", true }; return true;
    case kOutputRotationParamId:
        range = { -180.0, 180.0, range.defaultValue, "degrees", false };
        return true;
    default: break;
    }
    clap_id auxOffset = 0u;
    if (decodeAuxParam(id, auxOffset)) {
        switch (auxOffset) {
        case kAuxGrainSizeOffset:
            range = { 0.0, 1.0, range.defaultValue,
                "aux-grain-size", false }; break;
        case kAuxGrainDensityOffset:
            range = { 0.0, 1.0, range.defaultValue,
                "aux-grain-density", false }; break;
        case kAuxGrainPitchOffset:
            range = { -1.0, 1.0, range.defaultValue,
                "aux-grain-pitch", false }; break;
        case kAuxTiltOffset:
            range = { -1.0, 1.0, range.defaultValue,
                "bipolar", false }; break;
        default:
            range = { 0.0, 1.0, range.defaultValue, "pct", false }; break;
        }
        return true;
    }
    uint32_t appendedNode = 0u;
    clap_id appendedOffset = 0u;
    if (decodeExciterParam(id, appendedNode, appendedOffset)) {
        (void)appendedNode;
        if (appendedOffset == kExciterSourceOffset) {
            range = { 0.0,
                static_cast<double>(s3g::kFeedbackExciterSourceCount - 1u),
                range.defaultValue, "exciter-source", true };
        } else {
            range = { -60.0, 12.0, range.defaultValue, "db", false };
        }
        return true;
    }
    if (decodeMotionParam(id, appendedNode, appendedOffset)) {
        (void)appendedNode;
        switch (appendedOffset) {
        case kMotionSourceOffset:
            range = { 0.0,
                static_cast<double>(s3g::kFeedbackMotionSourceCount - 1u),
                range.defaultValue, "motion-source", true }; break;
        case kMotionTargetOffset:
            range = { 0.0,
                static_cast<double>(s3g::kFeedbackMotionTargetCount - 1u),
                range.defaultValue, "motion-target", true }; break;
        case kMotionDepthOffset:
            range = { -1.0, 1.0, range.defaultValue,
                "bipolar", false }; break;
        case kMotionSlewOffset:
            range = { 0.0, 1.0, range.defaultValue, "pct", false }; break;
        default: return false;
        }
        return true;
    }
    uint32_t node = 0u;
    clap_id offset = 0u;
    if (decodeNodeParam(id, node, offset)) {
        (void)node;
        switch (offset) {
        case kNodeModeOffset:
            range = { 0.0, 1.0, range.defaultValue, "mode", true }; return true;
        case kNodePedalOffset:
            range = { 0.0,
                static_cast<double>(s3g::kFeedbackPedalTypeCount - 1u),
                range.defaultValue, "pedal", true }; return true;
        case kNodeFrequencyOffset:
            range = { -6000.0, 6000.0, range.defaultValue, "hz", false };
            return true;
        case kNodeRegenerationOffset:
            range = { 0.0, 1.18, range.defaultValue, "regen", false };
            return true;
        case kNodeColorOffset:
            range = { 0.0, 1.0, range.defaultValue, "pct", false };
            return true;
        case kNodeLevelOffset:
            range = { -60.0, 6.0, range.defaultValue, "db", false };
            return true;
        case kNodePedalAmountOffset:
        case kNodePedalToneOffset:
        case kNodePedalMixOffset:
            range = { 0.0, 1.0, range.defaultValue, "pct", false };
            return true;
        case kNodePedalBiasOffset:
            range = { -1.0, 1.0, range.defaultValue, "bipolar", false };
            return true;
        default:
            if (offset >= kNodePedalExtra0Offset
                && offset <= kNodePedalExtra7Offset) {
                range = { 0.0, 1.0, range.defaultValue, "pct", false };
                return true;
            }
            return false;
        }
    }
    uint32_t destination = 0u;
    uint32_t source = 0u;
    if (decodeMatrixParam(id, destination, source)) {
        (void)destination;
        (void)source;
        range = { -1.0, 1.0, range.defaultValue, "route", false };
        return true;
    }
    return false;
}

double clampValue(clap_id id, double value)
{
    ParamRange range;
    if (!paramRange(id, range)) return 0.0;
    value = std::isfinite(value) ? value : range.defaultValue;
    value = std::clamp(value, range.minimum, range.maximum);
    return range.stepped ? std::round(value) : value;
}

void publishParam(Plugin& plugin, clap_id id, double value)
{
    const uint32_t index = paramIndex(id);
    if (index >= kParamCount) return;
    plugin.publishedParams[index].store(clampValue(id, value),
        std::memory_order_release);
}

double paramValue(const Plugin& plugin, clap_id id)
{
    const uint32_t index = paramIndex(id);
    return index < kParamCount
        ? plugin.publishedParams[index].load(std::memory_order_acquire)
        : 0.0;
}

s3g::FeedbackShiftParams paramsSnapshot(const Plugin& plugin)
{
    auto params = s3g::defaultFeedbackShiftParams();
    params.excite = static_cast<float>(paramValue(plugin, kExciteParamId));
    params.drift = static_cast<float>(paramValue(plugin, kDriftParamId));
    params.motionRate = static_cast<float>(
        paramValue(plugin, kMotionRateParamId));
    params.pulseDepth = static_cast<float>(
        paramValue(plugin, kPulseDepthParamId));
    params.pulseRate = static_cast<float>(
        paramValue(plugin, kPulseRateParamId));
    params.pulseSync = static_cast<uint32_t>(
        paramValue(plugin, kPulseSyncParamId));
    params.pulseDivision = static_cast<uint32_t>(
        paramValue(plugin, kPulseDivisionParamId));
    params.pulseShape = static_cast<s3g::FeedbackPulseShape>(
        static_cast<uint32_t>(paramValue(plugin, kPulseShapeParamId)));
    params.outputGainDb = static_cast<float>(
        paramValue(plugin, kOutputParamId));
    params.run = paramValue(plugin, kRunParamId) >= 0.5;
    params.outputMode = static_cast<s3g::FeedbackShiftOutputMode>(
        static_cast<uint32_t>(paramValue(plugin, kOutputModeParamId)));
    params.outputRotationDeg = static_cast<float>(
        paramValue(plugin, kOutputRotationParamId));
    params.auxPress = static_cast<float>(paramValue(plugin,
        auxParamId(kAuxPressOffset)));
    params.auxSaturation = static_cast<float>(paramValue(plugin,
        auxParamId(kAuxSaturationOffset)));
    params.auxFold = static_cast<float>(paramValue(plugin,
        auxParamId(kAuxFoldOffset)));
    params.auxClip = static_cast<float>(paramValue(plugin,
        auxParamId(kAuxClipOffset)));
    params.auxGrainSize = static_cast<float>(paramValue(plugin,
        auxParamId(kAuxGrainSizeOffset)));
    params.auxGrainDensity = static_cast<float>(paramValue(plugin,
        auxParamId(kAuxGrainDensityOffset)));
    params.auxGrainScatter = static_cast<float>(paramValue(plugin,
        auxParamId(kAuxGrainScatterOffset)));
    params.auxGrainPitch = static_cast<float>(paramValue(plugin,
        auxParamId(kAuxGrainPitchOffset)));
    params.auxGrainEdge = static_cast<float>(paramValue(plugin,
        auxParamId(kAuxGrainEdgeOffset)));
    params.auxTilt = static_cast<float>(paramValue(plugin,
        auxParamId(kAuxTiltOffset)));
    params.auxMix = static_cast<float>(paramValue(plugin,
        auxParamId(kAuxMixOffset)));
    params.auxGrainMix = static_cast<float>(paramValue(plugin,
        auxParamId(kAuxGrainMixOffset)));
    params.auxGrainCoherence = static_cast<float>(paramValue(plugin,
        auxParamId(kAuxCoherenceOffset)));
    params.auxGrainLaneDrift = static_cast<float>(paramValue(plugin,
        auxParamId(kAuxLaneDriftOffset)));
    for (uint32_t node = 0u;
         node < s3g::kFeedbackShiftChannels; ++node) {
        params.auxSend[node] = static_cast<float>(paramValue(plugin,
            auxSendParamId(node)));
    }
    for (uint32_t node = 0u; node < s3g::kFeedbackShiftChannels; ++node) {
        auto& lane = params.nodes[node];
        lane.mode = static_cast<s3g::FeedbackShiftMode>(
            static_cast<uint32_t>(paramValue(plugin,
                nodeParamId(node, kNodeModeOffset))));
        lane.pedal = static_cast<s3g::FeedbackPedalType>(
            static_cast<uint32_t>(paramValue(plugin,
                nodeParamId(node, kNodePedalOffset))));
        lane.exciterSource = static_cast<s3g::FeedbackExciterSource>(
            static_cast<uint32_t>(paramValue(plugin,
                exciterParamId(node, kExciterSourceOffset))));
        lane.exciterGainDb = static_cast<float>(paramValue(plugin,
            exciterParamId(node, kExciterGainOffset)));
        lane.motionSource = static_cast<s3g::FeedbackMotionSource>(
            static_cast<uint32_t>(paramValue(plugin,
                motionParamId(node, kMotionSourceOffset))));
        lane.motionTarget = static_cast<s3g::FeedbackMotionTarget>(
            static_cast<uint32_t>(paramValue(plugin,
                motionParamId(node, kMotionTargetOffset))));
        lane.motionDepth = static_cast<float>(paramValue(plugin,
            motionParamId(node, kMotionDepthOffset)));
        lane.motionSlew = static_cast<float>(paramValue(plugin,
            motionParamId(node, kMotionSlewOffset)));
        lane.frequencyHz = static_cast<float>(paramValue(plugin,
            nodeParamId(node, kNodeFrequencyOffset)));
        lane.regeneration = static_cast<float>(paramValue(plugin,
            nodeParamId(node, kNodeRegenerationOffset)));
        lane.color = static_cast<float>(paramValue(plugin,
            nodeParamId(node, kNodeColorOffset)));
        lane.levelDb = static_cast<float>(paramValue(plugin,
            nodeParamId(node, kNodeLevelOffset)));
        lane.pedalAmount = static_cast<float>(paramValue(plugin,
            nodeParamId(node, kNodePedalAmountOffset)));
        lane.pedalTone = static_cast<float>(paramValue(plugin,
            nodeParamId(node, kNodePedalToneOffset)));
        lane.pedalBias = static_cast<float>(paramValue(plugin,
            nodeParamId(node, kNodePedalBiasOffset)));
        lane.pedalMix = static_cast<float>(paramValue(plugin,
            nodeParamId(node, kNodePedalMixOffset)));
        for (uint32_t extra = 0u;
             extra < s3g::kFeedbackPedalExtraParameterCount; ++extra) {
            lane.pedalExtra[extra] = static_cast<float>(paramValue(plugin,
                nodeParamId(node, kNodePedalExtra0Offset + extra)));
        }
    }
    for (uint32_t destination = 0u;
         destination < s3g::kFeedbackShiftChannels; ++destination) {
        for (uint32_t source = 0u;
             source < s3g::kFeedbackShiftChannels; ++source) {
            params.matrix[destination * s3g::kFeedbackShiftChannels + source]
                = static_cast<float>(paramValue(plugin,
                    matrixParamId(destination, source)));
        }
    }
    return params;
}

void syncDspParams(Plugin& plugin)
{
    plugin.params = paramsSnapshot(plugin);
    plugin.dsp.setParams(plugin.params);
}

void requestGuiParamService(Plugin& plugin)
{
    if (plugin.hostParams && plugin.hostParams->request_flush) {
        plugin.hostParams->request_flush(plugin.host);
    } else if (plugin.host && plugin.host->request_process) {
        plugin.host->request_process(plugin.host);
    }
}

void requestGuiParamRetry(Plugin& plugin)
{
    if (plugin.guiRetryPending.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    if (plugin.activated.load(std::memory_order_acquire)) {
        if (plugin.host && plugin.host->request_callback) {
            plugin.host->request_callback(plugin.host);
        } else if (plugin.host && plugin.host->request_process) {
            plugin.host->request_process(plugin.host);
        }
    } else {
        plugin.guiRetryPending.store(false, std::memory_order_release);
        requestGuiParamService(plugin);
    }
}

bool queueGuiParamEvent(Plugin& plugin,
    s3g::clap_gui::ParamEventKind kind, clap_id id, double value = 0.0)
{
    if (!plugin.guiParamEvents.push({ kind, id, value })) return false;
    requestGuiParamService(plugin);
    return true;
}

bool queueGuiParamGestureBegin(Plugin& plugin, clap_id id)
{
    return queueGuiParamEvent(plugin,
        s3g::clap_gui::ParamEventKind::GestureBegin, id);
}

bool queueGuiParamValue(Plugin& plugin, clap_id id, double value)
{
    value = clampValue(id, value);
    if (paramIndex(id) >= kParamCount
        || !plugin.guiParamEvents.push({
            s3g::clap_gui::ParamEventKind::Value, id, value })) {
        return false;
    }
    publishParam(plugin, id, value);
    requestGuiParamService(plugin);
    return true;
}

bool queueGuiParamGestureEnd(Plugin& plugin, clap_id id)
{
    return queueGuiParamEvent(plugin,
        s3g::clap_gui::ParamEventKind::GestureEnd, id);
}

bool queueGuiParamGesture(Plugin& plugin, clap_id id, double value)
{
    value = clampValue(id, value);
    if (paramIndex(id) >= kParamCount) return false;
    using Kind = s3g::clap_gui::ParamEventKind;
    const std::array<s3g::clap_gui::ParamEvent, 3u> events {{
        { Kind::GestureBegin, id, 0.0 },
        { Kind::Value, id, value },
        { Kind::GestureEnd, id, 0.0 },
    }};
    if (!plugin.guiParamEvents.pushBatch(events.data(), events.size())) {
        return false;
    }
    publishParam(plugin, id, value);
    requestGuiParamService(plugin);
    return true;
}

bool queueGuiPatch(Plugin& plugin,
    const s3g::FeedbackShiftParams& patch)
{
    using Kind = s3g::clap_gui::ParamEventKind;
    std::array<s3g::clap_gui::ParamEvent, kParamCount> events {};
    for (uint32_t index = 0u; index < kParamCount; ++index) {
        const clap_id id = paramIdAtIndex(index);
        events[index] = { Kind::Value, id, rawParamValue(patch, id) };
    }
    if (!plugin.guiParamEvents.pushBatch(events.data(), events.size())) {
        return false;
    }
    for (const auto& event : events) {
        publishParam(plugin, event.paramId, event.value);
    }
    plugin.panicRequested.store(true, std::memory_order_release);
    requestGuiParamService(plugin);
    return true;
}

bool pushGuiParamEvent(const clap_output_events_t* output,
    const s3g::clap_gui::ParamEvent& pending)
{
    if (!output || !output->try_push) return false;
    if (pending.kind == s3g::clap_gui::ParamEventKind::Value) {
        clap_event_param_value_t event {};
        event.header.size = sizeof(event);
        event.header.time = 0u;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.param_id = pending.paramId;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = pending.value;
        return output->try_push(output, &event.header);
    }
    clap_event_param_gesture_t event {};
    event.header.size = sizeof(event);
    event.header.time = 0u;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = pending.kind
            == s3g::clap_gui::ParamEventKind::GestureBegin
        ? CLAP_EVENT_PARAM_GESTURE_BEGIN : CLAP_EVENT_PARAM_GESTURE_END;
    event.header.flags = CLAP_EVENT_IS_LIVE;
    event.param_id = pending.paramId;
    return output->try_push(output, &event.header);
}

bool serviceGuiParamEvents(Plugin& plugin,
    const clap_output_events_t* output)
{
    s3g::clap_gui::ParamEvent pending {};
    while (plugin.guiParamEvents.peek(pending)) {
        if (!pushGuiParamEvent(output, pending)) {
            requestGuiParamRetry(plugin);
            return false;
        }
        plugin.guiParamEvents.pop();
    }
    plugin.guiRetryPending.store(false, std::memory_order_release);
    return true;
}

bool init(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (instance->host && instance->host->get_extension) {
        instance->hostParams = static_cast<const clap_host_params_t*>(
            instance->host->get_extension(instance->host, CLAP_EXT_PARAMS));
    }
    return true;
}

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
    auto* instance = self(plugin);
    instance->sampleRate = std::clamp(sampleRate, 8000.0, 768000.0);
    syncDspParams(*instance);
    instance->dsp.prepare(instance->sampleRate);
    instance->dsp.setTransport(instance->transportTempoBpm,
        instance->transportHasTempo);
    instance->activated.store(true, std::memory_order_release);
    return true;
}

void deactivate(const clap_plugin_t* plugin)
{
    self(plugin)->activated.store(false, std::memory_order_release);
}

bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    self(plugin)->dsp.reset();
}

void triggerMidi(Plugin& plugin, int16_t channel, float velocity)
{
    if (channel >= 0
        && channel < static_cast<int16_t>(s3g::kFeedbackShiftChannels)) {
        plugin.dsp.strike(static_cast<uint32_t>(channel), velocity);
    } else {
        plugin.dsp.strikeAll(velocity);
    }
}

void applyInputEvent(Plugin& plugin, const clap_event_header_t* event)
{
    if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
    if (event->type == CLAP_EVENT_PARAM_VALUE
        && event->size >= sizeof(clap_event_param_value_t)) {
        const auto* parameter = reinterpret_cast<
            const clap_event_param_value_t*>(event);
        if (paramIndex(parameter->param_id) < kParamCount) {
            publishParam(plugin, parameter->param_id, parameter->value);
            syncDspParams(plugin);
        }
    } else if (event->type == CLAP_EVENT_NOTE_ON
        && event->size >= sizeof(clap_event_note_t)) {
        const auto* note = reinterpret_cast<const clap_event_note_t*>(event);
        if (note->velocity > 0.0) {
            triggerMidi(plugin, note->channel,
                static_cast<float>(note->velocity));
        }
    } else if (event->type == CLAP_EVENT_MIDI
        && event->size >= sizeof(clap_event_midi_t)) {
        const auto* midi = reinterpret_cast<const clap_event_midi_t*>(event);
        if ((midi->data[0] & 0xf0u) == 0x90u && midi->data[2] > 0u) {
            triggerMidi(plugin, midi->data[0] & 0x0fu,
                static_cast<float>(midi->data[2]) / 127.0f);
        }
    }
}

void writeOutputSample(const clap_audio_buffer_t& output,
    uint32_t channel, uint32_t frame, float value)
{
    if (channel >= output.channel_count) return;
    if (output.data32 && output.data32[channel]) {
        output.data32[channel][frame] = value;
    }
    if (output.data64 && output.data64[channel]) {
        output.data64[channel][frame] = static_cast<double>(value);
    }
}

float readInputSample(const clap_audio_buffer_t* input,
    uint32_t channel, uint32_t frame)
{
    if (!input || channel >= input->channel_count) return 0.0f;
    if (input->data32 && input->data32[channel]) {
        return input->data32[channel][frame];
    }
    if (input->data64 && input->data64[channel]) {
        return static_cast<float>(input->data64[channel][frame]);
    }
    return 0.0f;
}

clap_process_status process(const clap_plugin_t* plugin,
    const clap_process_t* context)
{
    auto* instance = self(plugin);
    if (!context) return CLAP_PROCESS_ERROR;
    if (serviceGuiParamEvents(*instance, context->out_events)) {
        syncDspParams(*instance);
    }
    if (context->transport
        && (context->transport->flags & CLAP_TRANSPORT_HAS_TEMPO) != 0u
        && std::isfinite(context->transport->tempo)) {
        instance->transportTempoBpm = context->transport->tempo;
        instance->transportHasTempo = true;
    } else {
        instance->transportHasTempo = false;
    }
    instance->dsp.setTransport(instance->transportTempoBpm,
        instance->transportHasTempo);
    if (instance->panicRequested.exchange(false,
            std::memory_order_acq_rel)) {
        instance->dsp.panic();
    }
    uint32_t strikeMask = instance->strikeMask.exchange(
        0u, std::memory_order_acq_rel);
    for (uint32_t node = 0u; node < s3g::kFeedbackShiftChannels; ++node) {
        if ((strikeMask & (1u << node)) != 0u) {
            instance->dsp.strike(node, 1.0f);
        }
    }

    const clap_input_events_t* events = context->in_events;
    const uint32_t eventCount = events ? events->size(events) : 0u;
    uint32_t eventIndex = 0u;
    if (context->audio_outputs_count == 0u || !context->audio_outputs) {
        while (eventIndex < eventCount) {
            applyInputEvent(*instance, events->get(events, eventIndex++));
        }
        return CLAP_PROCESS_CONTINUE;
    }
    const auto& output = context->audio_outputs[0u];
    if (output.channel_count == 0u || (!output.data32 && !output.data64)) {
        return CLAP_PROCESS_ERROR;
    }

    const clap_audio_buffer_t* input = context->audio_inputs_count > 0u
            && context->audio_inputs
        ? &context->audio_inputs[0u] : nullptr;
    std::array<float, s3g::kFeedbackShiftChannels> frameInput {};
    std::array<float, s3g::kFeedbackShiftChannels> frameOutput {};
    for (uint32_t frame = 0u; frame < context->frames_count; ++frame) {
        while (eventIndex < eventCount) {
            const auto* event = events->get(events, eventIndex);
            if (!event || event->time > frame) break;
            applyInputEvent(*instance, event);
            ++eventIndex;
        }
        for (uint32_t channel = 0u;
             channel < s3g::kFeedbackShiftChannels; ++channel) {
            frameInput[channel] = readInputSample(input, channel, frame);
        }
        instance->dsp.processFrame(frameInput.data(), frameOutput.data());
        for (uint32_t channel = 0u; channel < output.channel_count; ++channel) {
            const float value = channel < s3g::kFeedbackShiftChannels
                ? frameOutput[channel] : 0.0f;
            writeOutputSample(output, channel, frame, value);
        }
    }
    while (eventIndex < eventCount) {
        applyInputEvent(*instance, events->get(events, eventIndex++));
    }
    for (uint32_t node = 0u; node < s3g::kFeedbackShiftChannels; ++node) {
        instance->outputPeaks[node].store(instance->dsp.outputPeak(node),
            std::memory_order_relaxed);
        instance->nodeActivity[node].store(instance->dsp.nodeActivity(node),
            std::memory_order_relaxed);
        instance->motionValues[node].store(instance->dsp.motionValue(node),
            std::memory_order_relaxed);
        instance->temporalPhases[node].store(
            instance->dsp.temporalPhase(node), std::memory_order_relaxed);
        instance->temporalProgress[node].store(
            instance->dsp.temporalProgress(node), std::memory_order_relaxed);
        for (uint32_t source = 0u;
             source < s3g::kFeedbackShiftChannels; ++source) {
            const uint32_t index = node * s3g::kFeedbackShiftChannels + source;
            instance->routeActivity[index].store(std::abs(
                instance->dsp.routeSignal(node, source)),
                std::memory_order_relaxed);
        }
    }
    instance->minimumGovernor.store(instance->dsp.minimumGovernor(),
        std::memory_order_relaxed);
    instance->pulsePhase.store(instance->dsp.pulsePhase(),
        std::memory_order_relaxed);
    instance->auxActivity.store(instance->dsp.auxActivity(),
        std::memory_order_relaxed);
    instance->auxGainReductionDb.store(instance->dsp.auxGainReductionDb(),
        std::memory_order_relaxed);
    instance->auxGrainActivity.store(instance->dsp.auxGrainActivity(),
        std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (instance->guiRetryPending.exchange(false,
            std::memory_order_acq_rel)) {
        requestGuiParamService(*instance);
    }
}

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput)
{
    return 1u;
}

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_audio_port_info_t* info)
{
    if (!info || index != 0u) return false;
    *info = {};
    info->id = isInput ? 19u : 20u;
    std::strncpy(info->name,
        isInput ? "8ch Excitation In" : "8ch Matrix Out",
        sizeof(info->name) - 1u);
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = s3g::kFeedbackShiftChannels;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = isInput ? 20u : 19u;
    return true;
}

const clap_plugin_audio_ports_t audioPorts {
    audioPortsCount, audioPortsGet
};

uint32_t notePortsCount(const clap_plugin_t*, bool isInput)
{
    return isInput ? 1u : 0u;
}

bool notePortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_note_port_info_t* info)
{
    if (!info || !isInput || index != 0u) return false;
    *info = {};
    info->id = 30u;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP
        | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::strncpy(info->name, "Node Excitation In", sizeof(info->name) - 1u);
    return true;
}

const clap_plugin_note_ports_t notePorts {
    notePortsCount, notePortsGet
};

uint32_t paramsCount(const clap_plugin_t*) { return kParamCount; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index,
    clap_param_info_t* info)
{
    if (!info || index >= kParamCount) return false;
    const clap_id id = paramIdAtIndex(index);
    ParamRange range;
    if (id == CLAP_INVALID_ID || !paramRange(id, range)) return false;
    *info = {};
    info->id = id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE
        | (range.stepped ? CLAP_PARAM_IS_STEPPED : 0u);
    info->min_value = range.minimum;
    info->max_value = range.maximum;
    info->default_value = range.defaultValue;
    static constexpr std::array<const char*, kGlobalParamCount> globalNames {{
        "Excitation", "Drift", "Pulse Depth", "Pulse Rate",
        "Pulse Sync", "Pulse Division", "Pulse Shape", "Output Gain", "Run",
        "Output Mode", "Output Rotation",
    }};
    if (id >= kExciteParamId && id <= kOutputRotationParamId) {
        std::strncpy(info->name, globalNames[id - kExciteParamId],
            sizeof(info->name) - 1u);
        std::strncpy(info->module,
            id >= kPulseDepthParamId && id <= kPulseShapeParamId
                ? "Rhythm" : (id >= kOutputParamId ? "Output" : "Excitation"),
            sizeof(info->module) - 1u);
        return true;
    }
    if (id == kMotionRateParamId) {
        std::strncpy(info->name, "Motion Rate", sizeof(info->name) - 1u);
        std::strncpy(info->module, "Motion", sizeof(info->module) - 1u);
        return true;
    }
    uint32_t node = 0u;
    clap_id offset = 0u;
    if (decodeNodeParam(id, node, offset)) {
        static constexpr std::array<const char*, kNodeParamStride> names {{
            "Mode", "Pedal", "Frequency", "Regeneration", "Color", "Level",
            "Pedal Amount", "Pedal Tone", "Pedal Bias", "Pedal Mix",
            "Pedal Parameter 5", "Pedal Parameter 6", "Pedal Parameter 7",
            "Pedal Parameter 8", "Pedal Parameter 9", "Pedal Parameter 10",
            "Pedal Parameter 11", "Pedal Parameter 12",
        }};
        std::strncpy(info->name, names[offset], sizeof(info->name) - 1u);
        std::snprintf(info->module, sizeof(info->module), "Node %u", node + 1u);
        return true;
    }
    uint32_t destination = 0u;
    uint32_t source = 0u;
    if (decodeMatrixParam(id, destination, source)) {
        std::snprintf(info->name, sizeof(info->name), "Node %u to Node %u",
            source + 1u, destination + 1u);
        std::strncpy(info->module, "Patch Matrix", sizeof(info->module) - 1u);
        return true;
    }
    uint32_t appendedNode = 0u;
    clap_id appendedOffset = 0u;
    if (decodeExciterParam(id, appendedNode, appendedOffset)) {
        std::strncpy(info->name,
            appendedOffset == kExciterSourceOffset
                ? "Exciter Source" : "Exciter Gain",
            sizeof(info->name) - 1u);
        std::snprintf(info->module, sizeof(info->module),
            "Node %u Exciter", appendedNode + 1u);
        return true;
    }
    if (decodeMotionParam(id, appendedNode, appendedOffset)) {
        static constexpr std::array<const char*, kMotionParamStride> names {{
            "Motion Source", "Motion Target", "Motion Depth", "Motion Slew",
        }};
        std::strncpy(info->name, names[appendedOffset],
            sizeof(info->name) - 1u);
        std::snprintf(info->module, sizeof(info->module),
            "Node %u Motion", appendedNode + 1u);
        return true;
    }
    clap_id auxOffset = 0u;
    if (decodeAuxParam(id, auxOffset)) {
        static constexpr std::array<const char*,
            kLegacyAuxProcessorParamCount> names {{
            "Press", "Saturation", "Fold", "Clip", "Grain Size",
            "Grain Density", "Grain Scatter", "Grain Pitch", "Grain Edge",
            "Tilt", "Return", "Grain Mix",
        }};
        if (auxOffset < kLegacyAuxProcessorParamCount) {
            std::strncpy(info->name, names[auxOffset],
                sizeof(info->name) - 1u);
            const bool postGranulator = auxOffset >= kAuxGrainSizeOffset
                    && auxOffset <= kAuxGrainEdgeOffset
                || auxOffset == kAuxGrainMixOffset;
            std::strncpy(info->module,
                postGranulator ? "Post Granulator" : "Feedback AUX",
                sizeof(info->module) - 1u);
        } else if (auxOffset < kAuxCoherenceOffset) {
            const uint32_t node = auxOffset - kAuxSend0Offset;
            std::snprintf(info->name, sizeof(info->name), "Node %u Send",
                node + 1u);
            std::strncpy(info->module, "AUX Sends",
                sizeof(info->module) - 1u);
        } else {
            std::strncpy(info->name,
                auxOffset == kAuxCoherenceOffset
                    ? "Grain Coherence" : "Grain Lane Drift",
                sizeof(info->name) - 1u);
            std::strncpy(info->module, "Post Granulator",
                sizeof(info->module) - 1u);
        }
        return true;
    }
    return false;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value || paramIndex(id) >= kParamCount) return false;
    *value = paramValue(*self(plugin), id);
    return true;
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value,
    char* display, uint32_t size)
{
    ParamRange range;
    if (!paramRange(id, range) || !display || size == 0u) return false;
    value = clampValue(id, value);
    if (std::strcmp(range.unit, "bool") == 0) {
        std::snprintf(display, size, "%s", value >= 0.5 ? "ON" : "OFF");
    } else if (std::strcmp(range.unit, "mode") == 0) {
        std::snprintf(display, size, "%s",
            value >= 0.5 ? "RING" : "SHIFT");
    } else if (std::strcmp(range.unit, "pedal") == 0) {
        std::snprintf(display, size, "%s", s3g::feedbackPedalName(
            static_cast<s3g::FeedbackPedalType>(
                static_cast<uint32_t>(value))));
    } else if (std::strcmp(range.unit, "exciter-source") == 0) {
        std::snprintf(display, size, "%s", s3g::feedbackExciterSourceName(
            static_cast<s3g::FeedbackExciterSource>(
                static_cast<uint32_t>(value))));
    } else if (std::strcmp(range.unit, "motion-source") == 0) {
        std::snprintf(display, size, "%s", s3g::feedbackMotionSourceName(
            static_cast<s3g::FeedbackMotionSource>(
                static_cast<uint32_t>(value))));
    } else if (std::strcmp(range.unit, "motion-target") == 0) {
        std::snprintf(display, size, "%s", s3g::feedbackMotionTargetName(
            static_cast<s3g::FeedbackMotionTarget>(
                static_cast<uint32_t>(value))));
    } else if (std::strcmp(range.unit, "shape") == 0) {
        std::snprintf(display, size, "%s", s3g::feedbackPulseShapeName(
            static_cast<s3g::FeedbackPulseShape>(
                static_cast<uint32_t>(value))));
    } else if (std::strcmp(range.unit, "division") == 0) {
        std::snprintf(display, size, "%s", s3g::feedbackPulseDivisionName(
            static_cast<uint32_t>(value)));
    } else if (std::strcmp(range.unit, "output-mode") == 0) {
        std::snprintf(display, size, "%s", s3g::feedbackShiftOutputModeName(
            static_cast<s3g::FeedbackShiftOutputMode>(
                static_cast<uint32_t>(value))));
    } else if (std::strcmp(range.unit, "degrees") == 0) {
        std::snprintf(display, size, "%+.1f deg", value);
    } else if (std::strcmp(range.unit, "hz") == 0) {
        const double absolute = std::abs(value);
        if (absolute < 1.0) std::snprintf(display, size, "%+.3f Hz", value);
        else if (absolute < 10.0) std::snprintf(display, size, "%+.2f Hz", value);
        else if (absolute < 100.0) std::snprintf(display, size, "%+.1f Hz", value);
        else std::snprintf(display, size, "%+.0f Hz", value);
    } else if (std::strcmp(range.unit, "regen") == 0
        || std::strcmp(range.unit, "pct") == 0) {
        std::snprintf(display, size, "%.0f%%", value * 100.0);
    } else if (std::strcmp(range.unit, "db") == 0) {
        std::snprintf(display, size, "%+.1f dB", value);
    } else if (std::strcmp(range.unit, "route") == 0) {
        std::snprintf(display, size, "%+.2f", value);
    } else if (std::strcmp(range.unit, "bipolar") == 0) {
        std::snprintf(display, size, "%+.2f", value);
    } else if (std::strcmp(range.unit, "aux-grain-size") == 0) {
        const double milliseconds = 2.0 * std::pow(125.0, value);
        std::snprintf(display, size, milliseconds < 10.0 ? "%.2f ms" : "%.0f ms",
            milliseconds);
    } else if (std::strcmp(range.unit, "aux-grain-density") == 0) {
        std::snprintf(display, size, "%.2fx", 1.25 + value * 6.75);
    } else if (std::strcmp(range.unit, "aux-grain-pitch") == 0) {
        std::snprintf(display, size, "%+.1f st", value * 12.0);
    } else {
        std::snprintf(display, size, "%.2f", value);
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id,
    const char* display, double* value)
{
    ParamRange range;
    if (!paramRange(id, range) || !display || !value) return false;
    if (std::strcmp(range.unit, "bool") == 0) {
        if (std::strcmp(display, "ON") == 0
            || std::strcmp(display, "on") == 0) {
            *value = 1.0; return true;
        }
        if (std::strcmp(display, "OFF") == 0
            || std::strcmp(display, "off") == 0) {
            *value = 0.0; return true;
        }
    }
    if (std::strcmp(range.unit, "mode") == 0) {
        if (std::strcmp(display, "RING") == 0
            || std::strcmp(display, "ring") == 0) {
            *value = 1.0; return true;
        }
        if (std::strcmp(display, "SHIFT") == 0
            || std::strcmp(display, "shift") == 0) {
            *value = 0.0; return true;
        }
    }
    if (std::strcmp(range.unit, "pedal") == 0) {
        for (uint32_t pedal = 0u; pedal < s3g::kFeedbackPedalTypeCount;
             ++pedal) {
            if (std::strcmp(display, s3g::feedbackPedalName(
                    static_cast<s3g::FeedbackPedalType>(pedal))) == 0) {
                *value = pedal; return true;
            }
        }
    }
    if (std::strcmp(range.unit, "output-mode") == 0) {
        for (uint32_t mode = 0u; mode < s3g::kFeedbackShiftOutputModeCount;
             ++mode) {
            if (std::strcmp(display, s3g::feedbackShiftOutputModeName(
                    static_cast<s3g::FeedbackShiftOutputMode>(mode))) == 0) {
                *value = mode;
                return true;
            }
        }
    }
    errno = 0;
    char* end = nullptr;
    double parsed = std::strtod(display, &end);
    if (display == end || errno == ERANGE || !std::isfinite(parsed)) {
        return false;
    }
    while (*end == ' ' || *end == '\t') ++end;
    if (*end == '%') parsed *= 0.01;
    if (std::strcmp(range.unit, "aux-grain-size") == 0) {
        parsed = std::log(std::clamp(parsed / 2.0, 1.0, 125.0))
            / std::log(125.0);
    } else if (std::strcmp(range.unit, "aux-grain-density") == 0) {
        parsed = (parsed - 1.25) / 6.75;
    } else if (std::strcmp(range.unit, "aux-grain-pitch") == 0) {
        parsed /= 12.0;
    }
    *value = clampValue(id, parsed);
    return true;
}

void paramsFlush(const clap_plugin_t* plugin,
    const clap_input_events_t* input, const clap_output_events_t* output)
{
    auto* instance = self(plugin);
    const uint32_t count = input ? input->size(input) : 0u;
    for (uint32_t index = 0u; index < count; ++index) {
        const auto* event = input->get(input, index);
        if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID
            || event->type != CLAP_EVENT_PARAM_VALUE
            || event->size < sizeof(clap_event_param_value_t)) continue;
        const auto* parameter = reinterpret_cast<
            const clap_event_param_value_t*>(event);
        publishParam(*instance, parameter->param_id, parameter->value);
    }
    (void)serviceGuiParamEvents(*instance, output);
}

const clap_plugin_params_t paramsExtension {
    paramsCount, paramsGetInfo, paramsGetValue,
    paramsValueToText, paramsTextToValue, paramsFlush
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    SavedState state;
    for (uint32_t index = 0u; index < kParamCount; ++index) {
        state.values[index] = paramValue(*self(plugin), paramIdAtIndex(index));
    }
    return s3g::clap_state::writeAll(stream, &state.header,
            sizeof(state.header))
        && s3g::clap_state::writeAll(stream, state.values.data(),
            sizeof(state.values));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedStateHeader header;
    if (!s3g::clap_state::readAll(stream, &header, sizeof(header))
        || header.magic != kStateMagic) return false;
    const bool current = header.version == kStateVersion
        && header.valueCount == kParamCount;
    const bool baseLegacy = header.version == kBaseStateVersion
        && header.valueCount == kLegacyParamCount;
    const bool holdCutLegacy = header.version == kHoldCutStateVersion
        && header.valueCount == kPreviousParamCount;
    const bool granularLegacy = header.version == kGranularStateVersion
        && header.valueCount == kPreviousParamCount;
    const bool feedbackSourceLegacy =
        header.version == kFeedbackSourceStateVersion
        && header.valueCount == kFeedbackSourceParamCount;
    const bool laneSendLegacy = header.version == kLaneSendStateVersion
        && header.valueCount == kLaneSendParamCount;
    const bool postGranulatorLegacy =
        header.version == kPostGranulatorStateVersion
        && header.valueCount == kPostGranulatorParamCount;
    if (!current && !baseLegacy && !holdCutLegacy && !granularLegacy
        && !feedbackSourceLegacy && !laneSendLegacy
        && !postGranulatorLegacy) {
        return false;
    }

    std::array<double, kParamCount> values {};
    const uint32_t count = current ? kParamCount
        : postGranulatorLegacy ? kPostGranulatorParamCount
        : laneSendLegacy ? kLaneSendParamCount
        : feedbackSourceLegacy ? kFeedbackSourceParamCount
        : baseLegacy ? kLegacyParamCount : kPreviousParamCount;
    if (!s3g::clap_state::readAll(stream, values.data(),
            sizeof(double) * count)) return false;
    auto* instance = self(plugin);
    if (!current) {
        const auto defaults = s3g::defaultFeedbackShiftParams();
        const uint32_t firstDefault = postGranulatorLegacy
            ? kPostGranulatorParamCount
            : laneSendLegacy
            ? kLaneSendParamCount
            : feedbackSourceLegacy
            ? kFeedbackSourceParamCount
            : granularLegacy ? kPreviousParamCount : kLegacyParamCount;
        for (uint32_t index = firstDefault;
             index < kParamCount; ++index) {
            const clap_id id = paramIdAtIndex(index);
            publishParam(*instance, id, rawParamValue(defaults, id));
        }
    }
    const uint32_t restoredCount = current ? kParamCount
        : postGranulatorLegacy ? kPostGranulatorParamCount
        : laneSendLegacy ? kLaneSendParamCount
        : feedbackSourceLegacy ? kFeedbackSourceParamCount
        : granularLegacy ? kPreviousParamCount : kLegacyParamCount;
    for (uint32_t index = 0u; index < restoredCount; ++index) {
        publishParam(*instance, paramIdAtIndex(index), values[index]);
    }
    instance->panicRequested.store(true, std::memory_order_release);
    if (instance->hostParams && instance->hostParams->rescan) {
        instance->hostParams->rescan(instance->host, CLAP_PARAM_RESCAN_VALUES);
    }
    if (instance->host && instance->host->request_process) {
        instance->host->request_process(instance->host);
    }
    return true;
}

const clap_plugin_state_t stateExtension { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)

#include "s3g_feedback_shift_gui.inc"


namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool floating)
{
    return !floating && api
        && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* floating)
{
    if (!api || !floating) return false;
    *api = CLAP_WINDOW_API_COCOA;
    *floating = false;
    return true;
}

bool guiCreate(const clap_plugin_t* plugin, const char* api, bool floating)
{
    if (!guiIsApiSupported(plugin, api, floating)) return false;
    auto* instance = self(plugin);
    if (instance->guiView) return true;
    instance->guiView = [[S3GFeedbackShiftView alloc]
        initWithPlugin:instance];
    if (!instance->guiView) return false;
    if (!s3g::clap_gui::createResponsiveViewport(instance->guiViewport,
            static_cast<NSView*>(instance->guiView), kGuiWidth, kGuiHeight)) {
        [static_cast<NSView*>(instance->guiView) release];
        instance->guiView = nullptr;
        return false;
    }
    return true;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (instance->guiView) {
        instance->guiVisible = false;
        [static_cast<S3GFeedbackShiftView*>(instance->guiView)
            stopRefreshTimer];
        s3g::clap_gui::destroyResponsiveViewport(
            instance->guiViewport, instance->guiView);
    }
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }

bool guiGetSize(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height)
{
    return s3g::clap_gui::getResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height);
}

bool guiCanResize(const clap_plugin_t*) { return true; }

bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints)
{
    return s3g::clap_gui::getResponsiveResizeHints(hints);
}

bool guiAdjustSize(const clap_plugin_t* plugin,
    uint32_t* width, uint32_t* height)
{
    return s3g::clap_gui::adjustResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height);
}

bool guiSetSize(const clap_plugin_t* plugin, uint32_t width, uint32_t height)
{
    return s3g::clap_gui::setResponsiveViewportSize(
        self(plugin)->guiViewport, width, height);
}

bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    if (!window || !window->api
        || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0
        || !window->cocoa) return false;
    auto* instance = self(plugin);
    return s3g::clap_gui::setResponsiveViewportParent(
        instance->guiViewport, static_cast<NSView*>(window->cocoa),
        instance->host);
}

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}

bool guiShow(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance->guiView
        || !s3g::clap_gui::setResponsiveViewportHidden(
            instance->guiViewport, false)) return false;
    instance->guiVisible = true;
    [static_cast<S3GFeedbackShiftView*>(instance->guiView)
        startRefreshTimer];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance->guiView) return false;
    instance->guiVisible = false;
    [static_cast<S3GFeedbackShiftView*>(instance->guiView)
        stopRefreshTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(
        instance->guiViewport, true);
}

const clap_plugin_gui_t guiExtension {
    guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy,
    guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints,
    guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient,
    guiSuggestTitle, guiShow, guiHide
};

} // namespace

#endif

namespace {

const void* getExtension(const clap_plugin_t*, const char* id)
{
    if (!id) return nullptr;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &notePorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExtension;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExtension;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExtension;
#endif
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SYNTHESIZER,
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_SURROUND,
    nullptr,
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.feedback-shift",
    "s3g Processor Feedback Shift",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.12.0",
    "An eight-node feedback instrument with external excitation and motion.",
    features,
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* instance = new (std::nothrow) Plugin();
    if (!instance) return nullptr;
    const auto defaults = s3g::defaultFeedbackShiftParams();
    for (uint32_t index = 0u; index < kParamCount; ++index) {
        const clap_id id = paramIdAtIndex(index);
        publishParam(*instance, id, rawParamValue(defaults, id));
    }
    instance->params = paramsSnapshot(*instance);
    instance->dsp.setParams(instance->params);
    instance->host = host;
    instance->plugin.desc = &descriptor;
    instance->plugin.plugin_data = instance;
    instance->plugin.init = init;
    instance->plugin.destroy = destroy;
    instance->plugin.activate = activate;
    instance->plugin.deactivate = deactivate;
    instance->plugin.start_processing = startProcessing;
    instance->plugin.stop_processing = stopProcessing;
    instance->plugin.reset = reset;
    instance->plugin.process = process;
    instance->plugin.get_extension = getExtension;
    instance->plugin.on_main_thread = onMainThread;
    return &instance->plugin;
}

uint32_t factoryGetPluginCount(const clap_plugin_factory_t*) { return 1u; }

const clap_plugin_descriptor_t* factoryGetPluginDescriptor(
    const clap_plugin_factory_t*, uint32_t index)
{
    return index == 0u ? &descriptor : nullptr;
}

const clap_plugin_t* factoryCreatePlugin(const clap_plugin_factory_t*,
    const clap_host_t* host, const char* pluginId)
{
    if (!pluginId || std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    return create(host);
}

const clap_plugin_factory_t factory {
    factoryGetPluginCount, factoryGetPluginDescriptor, factoryCreatePlugin,
};

bool entryInit(const char*) { return true; }
void entryDeinit() {}

const void* entryGetFactory(const char* factoryId)
{
    return factoryId && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0
        ? &factory : nullptr;
}

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory,
};
