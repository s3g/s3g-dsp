#include "s3g_no_input_mixer.h"
#include "s3g_realtime.h"

#include <clap/clap.h>
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
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {

constexpr uint32_t kStateVersion = 2u;
constexpr uint32_t kChannelCount = s3g::kNoInputMixerChannels;
constexpr uint32_t kRouteScopeSamples = 96u;
constexpr uint32_t kGuiWidth = 1356u;
constexpr uint32_t kGuiHeight = 820u;
constexpr double kPerformanceMixerReferenceHeight = 760.0;

constexpr clap_id kOutputParamId = 1u;
constexpr clap_id kCeilingParamId = 2u;
constexpr clap_id kLimiterParamId = 3u;
constexpr clap_id kDcBlockParamId = 4u;
constexpr clap_id kFeedbackParamId = 5u;
constexpr clap_id kCouplingParamId = 6u;
constexpr clap_id kPhaseParamId = 7u;
constexpr clap_id kDriftParamId = 8u;
constexpr clap_id kFormantParamId = 9u;
constexpr clap_id kQualityParamId = 10u;
constexpr clap_id kAgencyParamId = 11u;
constexpr clap_id kSpaceParamId = 12u;
constexpr clap_id kVarianceParamId = 13u;
constexpr clap_id kInternalToneParamId = 14u;
constexpr clap_id kHouseToneParamId = 15u;
constexpr clap_id kFlowParamId = 16u;
constexpr clap_id kSpreadParamId = 17u;
constexpr clap_id kVortexParamId = 18u;
constexpr clap_id kMotionParamId = 19u;
constexpr clap_id kMotionShapeParamId = 20u;
constexpr clap_id kMotionRateParamId = 21u;
constexpr clap_id kMotionPhaseParamId = 22u;
constexpr clap_id kAuxATypeParamId = 23u;
constexpr clap_id kAuxAGainParamId = 24u;
constexpr clap_id kAuxAToneParamId = 25u;
constexpr clap_id kAuxAReturnParamId = 26u;
constexpr clap_id kAuxAFeedbackParamId = 27u;
constexpr clap_id kAuxBTypeParamId = 28u;
constexpr clap_id kAuxBGainParamId = 29u;
constexpr clap_id kAuxBToneParamId = 30u;
constexpr clap_id kAuxBReturnParamId = 31u;
constexpr clap_id kAuxBFeedbackParamId = 32u;
constexpr clap_id kMatrixParamBase = 100u;
constexpr clap_id kLaneParamBase = 1000u;
constexpr clap_id kLaneParamStride = 100u;
constexpr clap_id kLaneBodyOffset = 0u;
constexpr clap_id kLaneLossOffset = 1u;
constexpr clap_id kLaneLevelOffset = 2u;
constexpr clap_id kLaneMuteOffset = 3u;
constexpr clap_id kLaneLowOffset = 4u;
constexpr clap_id kLaneMidFrequencyOffset = 5u;
constexpr clap_id kLaneMidGainOffset = 6u;
constexpr clap_id kLaneHighOffset = 7u;
constexpr clap_id kLaneAuxAOffset = 8u;
constexpr clap_id kLaneAuxBOffset = 9u;
constexpr clap_id kLaneInsertBaseOffset = 20u;
constexpr clap_id kLaneInsertStride = 10u;
constexpr clap_id kInsertTypeOffset = 0u;
constexpr clap_id kInsertGainOffset = 1u;
constexpr clap_id kInsertToneOffset = 2u;
constexpr clap_id kInsertBiasOffset = 3u;
constexpr clap_id kInsertLevelOffset = 4u;
constexpr clap_id kInsertBypassOffset = 5u;
constexpr uint32_t kGlobalParamCount = 32u;
constexpr uint32_t kLaneDirectParamCount = 10u;
constexpr uint32_t kInsertParamCount = 6u;
constexpr uint32_t kLaneParamCount = kLaneDirectParamCount
    + s3g::kNoInputMixerInsertSlots * kInsertParamCount;
constexpr uint32_t kTotalParamCount = kGlobalParamCount
    + s3g::kNoInputMixerMatrixCells
    + kChannelCount * kLaneParamCount;

const s3g::NoInputMixerParams kDefaultParams =
    s3g::defaultNoInputMixerParams();

constexpr clap_id matrixParamId(uint32_t destination, uint32_t source)
{
    return kMatrixParamBase + destination * kChannelCount + source;
}

constexpr clap_id laneParamId(uint32_t lane, clap_id offset)
{
    return kLaneParamBase + lane * kLaneParamStride + offset;
}

constexpr clap_id insertParamId(uint32_t lane, uint32_t slot,
    clap_id offset)
{
    return laneParamId(lane, kLaneInsertBaseOffset
        + slot * kLaneInsertStride + offset);
}

bool decodeMatrixParam(clap_id id, uint32_t& destination, uint32_t& source)
{
    if (id < kMatrixParamBase
        || id >= kMatrixParamBase + s3g::kNoInputMixerMatrixCells) {
        return false;
    }
    const uint32_t index = id - kMatrixParamBase;
    destination = index / kChannelCount;
    source = index % kChannelCount;
    return true;
}

bool decodeLaneParam(clap_id id, uint32_t& lane, clap_id& offset)
{
    if (id < kLaneParamBase) return false;
    lane = (id - kLaneParamBase) / kLaneParamStride;
    offset = (id - kLaneParamBase) % kLaneParamStride;
    return lane < kChannelCount;
}

bool decodeInsertOffset(clap_id laneOffset, uint32_t& slot,
    clap_id& insertOffset)
{
    if (laneOffset < kLaneInsertBaseOffset) return false;
    const clap_id relative = laneOffset - kLaneInsertBaseOffset;
    slot = relative / kLaneInsertStride;
    insertOffset = relative % kLaneInsertStride;
    return slot < s3g::kNoInputMixerInsertSlots
        && insertOffset < kInsertParamCount;
}

struct GlobalParamDef {
    clap_id id;
    const char* name;
    const char* module;
    double minimum;
    double maximum;
    double defaultValue;
    bool stepped;
};

const std::array<GlobalParamDef, kGlobalParamCount> kGlobalParamDefs {{
    { kOutputParamId, "Out", "Output", -60.0, 6.0,
        kDefaultParams.outputGainDb, false },
    { kCeilingParamId, "Ceiling", "Output", -18.0, 0.0,
        kDefaultParams.ceilingDb, false },
    { kLimiterParamId, "Limiter", "Output", 0.0, 1.0,
        static_cast<double>(kDefaultParams.limiterEnabled), true },
    { kDcBlockParamId, "DC Block", "Output", 0.0, 1.0,
        static_cast<double>(kDefaultParams.dcBlockEnabled), true },
    { kFeedbackParamId, "Feedback", "Network", 0.0, 1.25,
        kDefaultParams.feedback, false },
    { kCouplingParamId, "Coupling", "Network", 0.0, 1.25,
        kDefaultParams.coupling, false },
    { kPhaseParamId, "Phase", "Network", 0.0, 1.0,
        kDefaultParams.phase, false },
    { kDriftParamId, "Drift", "Network", 0.0, 1.0,
        kDefaultParams.drift, false },
    { kFormantParamId, "Formant", "Network", 0.0, 1.0,
        kDefaultParams.formant, false },
    { kQualityParamId, "Quality", "Containment", 0.0, 2.0,
        static_cast<double>(kDefaultParams.quality), true },
    { kAgencyParamId, "Agency", "Network", 0.0, 1.0,
        kDefaultParams.agency, false },
    { kSpaceParamId, "Space", "Network", 0.0, 1.0,
        kDefaultParams.space, false },
    { kVarianceParamId, "Preset Variance", "Network", 0.0, 1.0,
        kDefaultParams.variance, false },
    { kInternalToneParamId, "Internal Tone", "Tone", -1.0, 1.0,
        kDefaultParams.internalTone, false },
    { kHouseToneParamId, "House Tone", "Tone", -1.0, 1.0,
        kDefaultParams.houseTone, false },
    { kFlowParamId, "Flow", "Movement", 0.0, 1.0,
        kDefaultParams.flow, false },
    { kSpreadParamId, "Spread", "Movement", 0.0, 1.0,
        kDefaultParams.spread, false },
    { kVortexParamId, "Vortex", "Movement", -1.0, 1.0,
        kDefaultParams.vortex, false },
    { kMotionParamId, "Motion", "Movement", 0.0, 1.0,
        kDefaultParams.motion, false },
    { kMotionShapeParamId, "Shape", "Movement", 0.0, 5.0,
        static_cast<double>(kDefaultParams.motionShape), true },
    { kMotionRateParamId, "Rate", "Movement", 0.0, 1.0,
        kDefaultParams.motionRate, false },
    { kMotionPhaseParamId, "Phase Offset", "Movement", 0.0, 1.0,
        kDefaultParams.motionPhase, false },
    { kAuxATypeParamId, "Type", "Aux Return A", 0.0,
        static_cast<double>(s3g::kNoInputDistortionTypeCount - 1u),
        static_cast<double>(kDefaultParams.aux[0].effect.type), true },
    { kAuxAGainParamId, "Gain", "Aux Return A", 0.0, 1.0,
        kDefaultParams.aux[0].effect.gain, false },
    { kAuxAToneParamId, "Tone", "Aux Return A", 0.0, 1.0,
        kDefaultParams.aux[0].effect.tone, false },
    { kAuxAReturnParamId, "Return", "Aux Return A", 0.0, 1.0,
        kDefaultParams.aux[0].returnGain, false },
    { kAuxAFeedbackParamId, "Feedback", "Aux Return A", 0.0, 0.96,
        kDefaultParams.aux[0].feedback, false },
    { kAuxBTypeParamId, "Type", "Aux Return B", 0.0,
        static_cast<double>(s3g::kNoInputDistortionTypeCount - 1u),
        static_cast<double>(kDefaultParams.aux[1].effect.type), true },
    { kAuxBGainParamId, "Gain", "Aux Return B", 0.0, 1.0,
        kDefaultParams.aux[1].effect.gain, false },
    { kAuxBToneParamId, "Tone", "Aux Return B", 0.0, 1.0,
        kDefaultParams.aux[1].effect.tone, false },
    { kAuxBReturnParamId, "Return", "Aux Return B", 0.0, 1.0,
        kDefaultParams.aux[1].returnGain, false },
    { kAuxBFeedbackParamId, "Feedback", "Aux Return B", 0.0, 0.96,
        kDefaultParams.aux[1].feedback, false },
}};

struct ParamRange {
    double minimum = 0.0;
    double maximum = 1.0;
    double defaultValue = 0.0;
    bool stepped = false;
};

bool paramRange(clap_id id, ParamRange& range)
{
    for (const auto& def : kGlobalParamDefs) {
        if (def.id == id) {
            range = { def.minimum, def.maximum, def.defaultValue,
                def.stepped };
            return true;
        }
    }
    uint32_t destination = 0u;
    uint32_t source = 0u;
    if (decodeMatrixParam(id, destination, source)) {
        range = { -1.0, 1.0,
            kDefaultParams.matrix[destination * kChannelCount + source],
            false };
        return true;
    }
    uint32_t lane = 0u;
    clap_id offset = 0u;
    if (!decodeLaneParam(id, lane, offset)) return false;
    const auto& defaults = kDefaultParams.lanes[lane];
    switch (offset) {
    case kLaneBodyOffset:
        range = { 0.0, 1.0, defaults.body, false }; return true;
    case kLaneLossOffset:
        range = { 0.0, 1.0, defaults.loss, false }; return true;
    case kLaneLevelOffset:
        range = { -60.0, 12.0, defaults.levelDb, false }; return true;
    case kLaneMuteOffset:
        range = { 0.0, 1.0, static_cast<double>(defaults.mute), true };
        return true;
    case kLaneLowOffset:
        range = { -18.0, 18.0, defaults.lowDb, false }; return true;
    case kLaneMidFrequencyOffset:
        range = { 80.0, 8000.0, defaults.midFrequencyHz, false };
        return true;
    case kLaneMidGainOffset:
        range = { -18.0, 18.0, defaults.midGainDb, false }; return true;
    case kLaneHighOffset:
        range = { -18.0, 18.0, defaults.highDb, false }; return true;
    case kLaneAuxAOffset:
        range = { 0.0, 1.0, defaults.auxSend[0], false }; return true;
    case kLaneAuxBOffset:
        range = { 0.0, 1.0, defaults.auxSend[1], false }; return true;
    default: break;
    }
    uint32_t slot = 0u;
    clap_id insertOffset = 0u;
    if (!decodeInsertOffset(offset, slot, insertOffset)) return false;
    const auto& insert = defaults.inserts[slot];
    switch (insertOffset) {
    case kInsertTypeOffset:
        range = { 0.0,
            static_cast<double>(s3g::kNoInputDistortionTypeCount - 1u),
            static_cast<double>(insert.type), true }; return true;
    case kInsertGainOffset:
        range = { 0.0, 1.0, insert.gain, false }; return true;
    case kInsertToneOffset:
        range = { 0.0, 1.0, insert.tone, false }; return true;
    case kInsertBiasOffset:
        range = { -1.0, 1.0, insert.bias, false }; return true;
    case kInsertLevelOffset:
        range = { -24.0, 12.0, insert.levelDb, false }; return true;
    case kInsertBypassOffset:
        range = { 0.0, 1.0, static_cast<double>(insert.bypass), true };
        return true;
    default: return false;
    }
}

clap_id paramIdAtIndex(uint32_t index)
{
    if (index < kGlobalParamCount) return kGlobalParamDefs[index].id;
    index -= kGlobalParamCount;
    if (index < s3g::kNoInputMixerMatrixCells) {
        return kMatrixParamBase + index;
    }
    index -= s3g::kNoInputMixerMatrixCells;
    const uint32_t lane = index / kLaneParamCount;
    const uint32_t local = index % kLaneParamCount;
    if (lane >= kChannelCount) return CLAP_INVALID_ID;
    if (local < kLaneDirectParamCount) {
        return laneParamId(lane, local);
    }
    const uint32_t insertLocal = local - kLaneDirectParamCount;
    const uint32_t slot = insertLocal / kInsertParamCount;
    const uint32_t field = insertLocal % kInsertParamCount;
    return insertParamId(lane, slot, field);
}

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::NoInputMixerParams params = s3g::defaultNoInputMixerParams();
    uint32_t selectedLane = 2u;
    uint32_t selectedSlot = 0u;
    uint32_t selectedSource = 2u;
    uint32_t selectedDestination = 2u;
    uint32_t guiPage = 0u;
};

struct LegacyNoInputInsertParams {
    s3g::NoInputDistortionType type = s3g::NoInputDistortionType::Bypass;
    float gain = 0.35f;
    float tone = 0.50f;
    float bias = 0.0f;
    float levelDb = 0.0f;
    uint32_t bypass = 0u;
};

struct LegacyNoInputLaneParams {
    float body = 0.50f;
    float loss = 0.38f;
    float levelDb = -3.0f;
    uint32_t mute = 0u;
    float lowDb = 0.0f;
    float midFrequencyHz = 850.0f;
    float midGainDb = 0.0f;
    float highDb = 0.0f;
    std::array<LegacyNoInputInsertParams,
        s3g::kNoInputMixerInsertSlots> inserts {};
};

struct LegacyNoInputMixerParams {
    float outputGainDb = -18.0f;
    float ceilingDb = -1.0f;
    uint32_t limiterEnabled = 1u;
    uint32_t dcBlockEnabled = 1u;
    float feedback = 0.82f;
    float coupling = 0.42f;
    float phase = 0.34f;
    float drift = 0.18f;
    float formant = 0.30f;
    uint32_t quality = 1u;
    uint32_t seed = 0x5455444fu;
    std::array<float, s3g::kNoInputMixerMatrixCells> matrix {};
    std::array<LegacyNoInputLaneParams, kChannelCount> lanes {};
};

struct LegacySavedState {
    uint32_t version = 1u;
    LegacyNoInputMixerParams params {};
    uint32_t selectedLane = 2u;
    uint32_t selectedSlot = 0u;
    uint32_t selectedSource = 2u;
    uint32_t selectedDestination = 2u;
    uint32_t guiPage = 0u;
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0u;
    s3g::NoInputMixerParams params = s3g::defaultNoInputMixerParams();
    s3g::NoInputMixer mixer;
    std::array<float, kChannelCount> frame {};
    std::array<std::atomic<float>,
        s3g::kNoInputMixerMatrixCells * kRouteScopeSamples> routeScope {};
    std::atomic<uint64_t> routeScopeSequence { 0u };
    uint32_t routeScopeDecimation = 2u;
    uint32_t routeScopeCountdown = 0u;
    std::atomic<float> outputPeak { 0.0f };
    std::array<std::atomic<float>, kChannelCount> lanePeaks {};
    std::array<std::atomic<float>, kChannelCount> laneActivity {};
    std::array<std::atomic<float>, 2u> auxActivity {};
    std::atomic<float> networkActivity { 0.0f };
    std::atomic<float> motionPhase { 0.0f };
    std::atomic<float> minimumGovernor { 1.0f };
    std::atomic<uint32_t> containmentState {
        static_cast<uint32_t>(s3g::NoInputContainmentState::Quiet) };
    std::atomic<bool> seedRequested { false };
    std::atomic<bool> panicRequested { false };
    std::atomic<uint32_t> killMask { 0u };
    std::atomic<uint32_t> selectedLane { 2u };
    std::atomic<uint32_t> selectedSlot { 0u };
    std::atomic<uint32_t> selectedSource { 2u };
    std::atomic<uint32_t> selectedDestination { 2u };
    std::atomic<uint32_t> guiPage { 0u };
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

void applyParam(Plugin& plugin, clap_id id, double value)
{
    for (const auto& def : kGlobalParamDefs) {
        if (def.id != id) continue;
        value = std::clamp(value, def.minimum, def.maximum);
        switch (id) {
        case kOutputParamId:
            plugin.params.outputGainDb = static_cast<float>(value); break;
        case kCeilingParamId:
            plugin.params.ceilingDb = static_cast<float>(value); break;
        case kLimiterParamId:
            plugin.params.limiterEnabled = value >= 0.5 ? 1u : 0u; break;
        case kDcBlockParamId:
            plugin.params.dcBlockEnabled = value >= 0.5 ? 1u : 0u; break;
        case kFeedbackParamId:
            plugin.params.feedback = static_cast<float>(value); break;
        case kCouplingParamId:
            plugin.params.coupling = static_cast<float>(value); break;
        case kPhaseParamId:
            plugin.params.phase = static_cast<float>(value); break;
        case kDriftParamId:
            plugin.params.drift = static_cast<float>(value); break;
        case kFormantParamId:
            plugin.params.formant = static_cast<float>(value); break;
        case kQualityParamId:
            plugin.params.quality = static_cast<uint32_t>(std::lround(value));
            break;
        case kAgencyParamId:
            plugin.params.agency = static_cast<float>(value); break;
        case kSpaceParamId:
            plugin.params.space = static_cast<float>(value); break;
        case kVarianceParamId:
            plugin.params.variance = static_cast<float>(value); break;
        case kInternalToneParamId:
            plugin.params.internalTone = static_cast<float>(value); break;
        case kHouseToneParamId:
            plugin.params.houseTone = static_cast<float>(value); break;
        case kFlowParamId:
            plugin.params.flow = static_cast<float>(value); break;
        case kSpreadParamId:
            plugin.params.spread = static_cast<float>(value); break;
        case kVortexParamId:
            plugin.params.vortex = static_cast<float>(value); break;
        case kMotionParamId:
            plugin.params.motion = static_cast<float>(value); break;
        case kMotionShapeParamId:
            plugin.params.motionShape = s3g::matrixFlowShapeFromIndex(
                static_cast<uint32_t>(std::lround(value))); break;
        case kMotionRateParamId:
            plugin.params.motionRate = static_cast<float>(value); break;
        case kMotionPhaseParamId:
            plugin.params.motionPhase = static_cast<float>(value); break;
        case kAuxATypeParamId:
        case kAuxBTypeParamId: {
            const uint32_t bus = id == kAuxATypeParamId ? 0u : 1u;
            plugin.params.aux[bus].effect.type =
                static_cast<s3g::NoInputDistortionType>(
                    static_cast<uint32_t>(std::lround(value)));
            break;
        }
        case kAuxAGainParamId:
        case kAuxBGainParamId:
            plugin.params.aux[id == kAuxAGainParamId ? 0u : 1u]
                .effect.gain = static_cast<float>(value); break;
        case kAuxAToneParamId:
        case kAuxBToneParamId:
            plugin.params.aux[id == kAuxAToneParamId ? 0u : 1u]
                .effect.tone = static_cast<float>(value); break;
        case kAuxAReturnParamId:
        case kAuxBReturnParamId:
            plugin.params.aux[id == kAuxAReturnParamId ? 0u : 1u]
                .returnGain = static_cast<float>(value); break;
        case kAuxAFeedbackParamId:
        case kAuxBFeedbackParamId:
            plugin.params.aux[id == kAuxAFeedbackParamId ? 0u : 1u]
                .feedback = static_cast<float>(value); break;
        default: break;
        }
        plugin.mixer.setParams(plugin.params);
        return;
    }

    uint32_t destination = 0u;
    uint32_t source = 0u;
    if (decodeMatrixParam(id, destination, source)) {
        plugin.params.matrix[destination * kChannelCount + source] =
            static_cast<float>(std::clamp(value, -1.0, 1.0));
        plugin.mixer.setParams(plugin.params);
        return;
    }

    uint32_t lane = 0u;
    clap_id offset = 0u;
    if (!decodeLaneParam(id, lane, offset)) return;
    auto& laneParams = plugin.params.lanes[lane];
    switch (offset) {
    case kLaneBodyOffset:
        laneParams.body = static_cast<float>(std::clamp(value, 0.0, 1.0));
        break;
    case kLaneLossOffset:
        laneParams.loss = static_cast<float>(std::clamp(value, 0.0, 1.0));
        break;
    case kLaneLevelOffset:
        laneParams.levelDb = static_cast<float>(
            std::clamp(value, -60.0, 12.0));
        break;
    case kLaneMuteOffset:
        laneParams.mute = value >= 0.5 ? 1u : 0u;
        break;
    case kLaneLowOffset:
        laneParams.lowDb = static_cast<float>(
            std::clamp(value, -18.0, 18.0));
        break;
    case kLaneMidFrequencyOffset:
        laneParams.midFrequencyHz = static_cast<float>(
            std::clamp(value, 80.0, 8000.0));
        break;
    case kLaneMidGainOffset:
        laneParams.midGainDb = static_cast<float>(
            std::clamp(value, -18.0, 18.0));
        break;
    case kLaneHighOffset:
        laneParams.highDb = static_cast<float>(
            std::clamp(value, -18.0, 18.0));
        break;
    case kLaneAuxAOffset:
        laneParams.auxSend[0] = static_cast<float>(
            std::clamp(value, 0.0, 1.0));
        break;
    case kLaneAuxBOffset:
        laneParams.auxSend[1] = static_cast<float>(
            std::clamp(value, 0.0, 1.0));
        break;
    default: {
        uint32_t slot = 0u;
        clap_id insertOffset = 0u;
        if (!decodeInsertOffset(offset, slot, insertOffset)) return;
        auto& insert = laneParams.inserts[slot];
        switch (insertOffset) {
        case kInsertTypeOffset:
            insert.type = static_cast<s3g::NoInputDistortionType>(
                static_cast<uint32_t>(std::clamp(
                    std::lround(value), 0l,
                    static_cast<long>(s3g::kNoInputDistortionTypeCount - 1u))));
            break;
        case kInsertGainOffset:
            insert.gain = static_cast<float>(std::clamp(value, 0.0, 1.0));
            break;
        case kInsertToneOffset:
            insert.tone = static_cast<float>(std::clamp(value, 0.0, 1.0));
            break;
        case kInsertBiasOffset:
            insert.bias = static_cast<float>(std::clamp(value, -1.0, 1.0));
            break;
        case kInsertLevelOffset:
            insert.levelDb = static_cast<float>(
                std::clamp(value, -24.0, 12.0));
            break;
        case kInsertBypassOffset:
            insert.bypass = value >= 0.5 ? 1u : 0u;
            break;
        default: return;
        }
        break;
    }
    }
    plugin.mixer.setParams(plugin.params);
}

bool paramValue(const Plugin& plugin, clap_id id, double& value)
{
    switch (id) {
    case kOutputParamId: value = plugin.params.outputGainDb; return true;
    case kCeilingParamId: value = plugin.params.ceilingDb; return true;
    case kLimiterParamId: value = plugin.params.limiterEnabled; return true;
    case kDcBlockParamId: value = plugin.params.dcBlockEnabled; return true;
    case kFeedbackParamId: value = plugin.params.feedback; return true;
    case kCouplingParamId: value = plugin.params.coupling; return true;
    case kPhaseParamId: value = plugin.params.phase; return true;
    case kDriftParamId: value = plugin.params.drift; return true;
    case kFormantParamId: value = plugin.params.formant; return true;
    case kQualityParamId: value = plugin.params.quality; return true;
    case kAgencyParamId: value = plugin.params.agency; return true;
    case kSpaceParamId: value = plugin.params.space; return true;
    case kVarianceParamId: value = plugin.params.variance; return true;
    case kInternalToneParamId: value = plugin.params.internalTone; return true;
    case kHouseToneParamId: value = plugin.params.houseTone; return true;
    case kFlowParamId: value = plugin.params.flow; return true;
    case kSpreadParamId: value = plugin.params.spread; return true;
    case kVortexParamId: value = plugin.params.vortex; return true;
    case kMotionParamId: value = plugin.params.motion; return true;
    case kMotionShapeParamId:
        value = static_cast<double>(plugin.params.motionShape); return true;
    case kMotionRateParamId: value = plugin.params.motionRate; return true;
    case kMotionPhaseParamId: value = plugin.params.motionPhase; return true;
    case kAuxATypeParamId:
        value = static_cast<double>(plugin.params.aux[0].effect.type);
        return true;
    case kAuxAGainParamId: value = plugin.params.aux[0].effect.gain; return true;
    case kAuxAToneParamId: value = plugin.params.aux[0].effect.tone; return true;
    case kAuxAReturnParamId: value = plugin.params.aux[0].returnGain; return true;
    case kAuxAFeedbackParamId: value = plugin.params.aux[0].feedback; return true;
    case kAuxBTypeParamId:
        value = static_cast<double>(plugin.params.aux[1].effect.type);
        return true;
    case kAuxBGainParamId: value = plugin.params.aux[1].effect.gain; return true;
    case kAuxBToneParamId: value = plugin.params.aux[1].effect.tone; return true;
    case kAuxBReturnParamId: value = plugin.params.aux[1].returnGain; return true;
    case kAuxBFeedbackParamId: value = plugin.params.aux[1].feedback; return true;
    default: break;
    }
    uint32_t destination = 0u;
    uint32_t source = 0u;
    if (decodeMatrixParam(id, destination, source)) {
        value = plugin.params.matrix[destination * kChannelCount + source];
        return true;
    }
    uint32_t lane = 0u;
    clap_id offset = 0u;
    if (!decodeLaneParam(id, lane, offset)) return false;
    const auto& laneParams = plugin.params.lanes[lane];
    switch (offset) {
    case kLaneBodyOffset: value = laneParams.body; return true;
    case kLaneLossOffset: value = laneParams.loss; return true;
    case kLaneLevelOffset: value = laneParams.levelDb; return true;
    case kLaneMuteOffset: value = laneParams.mute; return true;
    case kLaneLowOffset: value = laneParams.lowDb; return true;
    case kLaneMidFrequencyOffset:
        value = laneParams.midFrequencyHz; return true;
    case kLaneMidGainOffset: value = laneParams.midGainDb; return true;
    case kLaneHighOffset: value = laneParams.highDb; return true;
    case kLaneAuxAOffset: value = laneParams.auxSend[0]; return true;
    case kLaneAuxBOffset: value = laneParams.auxSend[1]; return true;
    default: break;
    }
    uint32_t slot = 0u;
    clap_id insertOffset = 0u;
    if (!decodeInsertOffset(offset, slot, insertOffset)) return false;
    const auto& insert = laneParams.inserts[slot];
    switch (insertOffset) {
    case kInsertTypeOffset:
        value = static_cast<double>(insert.type); return true;
    case kInsertGainOffset: value = insert.gain; return true;
    case kInsertToneOffset: value = insert.tone; return true;
    case kInsertBiasOffset: value = insert.bias; return true;
    case kInsertLevelOffset: value = insert.levelDb; return true;
    case kInsertBypassOffset: value = insert.bypass; return true;
    default: return false;
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

void resetMeters(Plugin& plugin)
{
    for (auto& sample : plugin.routeScope) {
        sample.store(0.0f, std::memory_order_relaxed);
    }
    plugin.routeScopeSequence.store(0u, std::memory_order_release);
    plugin.outputPeak.store(0.0f, std::memory_order_relaxed);
    for (uint32_t lane = 0u; lane < kChannelCount; ++lane) {
        plugin.lanePeaks[lane].store(0.0f, std::memory_order_relaxed);
        plugin.laneActivity[lane].store(0.0f, std::memory_order_relaxed);
    }
    plugin.networkActivity.store(0.0f, std::memory_order_relaxed);
    plugin.motionPhase.store(0.0f, std::memory_order_relaxed);
    for (auto& activity : plugin.auxActivity) {
        activity.store(0.0f, std::memory_order_relaxed);
    }
    plugin.minimumGovernor.store(1.0f, std::memory_order_relaxed);
    plugin.containmentState.store(
        static_cast<uint32_t>(s3g::NoInputContainmentState::Quiet),
        std::memory_order_relaxed);
}

bool activate(const clap_plugin_t* plugin, double sampleRate,
    uint32_t, uint32_t maxFrames)
{
    auto* p = self(plugin);
    p->sampleRate = sampleRate;
    p->maxFrames = maxFrames;
    p->routeScopeDecimation = std::max<uint32_t>(1u,
        static_cast<uint32_t>(std::lround(sampleRate / 24000.0)));
    p->routeScopeCountdown = 0u;
    p->mixer.prepare(sampleRate);
    p->mixer.setParams(p->params);
    p->mixer.reseed(p->params.seed, 0.48f);
    resetMeters(*p);
    p->seedRequested.store(false, std::memory_order_relaxed);
    p->panicRequested.store(false, std::memory_order_relaxed);
    p->killMask.store(0u, std::memory_order_relaxed);
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->mixer.setParams(p->params);
    p->mixer.reseed(p->params.seed, 0.48f);
    resetMeters(*p);
}

void readParamEvents(Plugin& plugin, const clap_input_events_t* events)
{
    if (!events) return;
    const uint32_t count = events->size(events);
    for (uint32_t index = 0u; index < count; ++index) {
        const clap_event_header_t* event = events->get(events, index);
        if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID
            || event->type != CLAP_EVENT_PARAM_VALUE) {
            continue;
        }
        const auto* param =
            reinterpret_cast<const clap_event_param_value_t*>(event);
        applyParam(plugin, param->param_id, param->value);
    }
}

clap_process_status process(const clap_plugin_t* plugin,
    const clap_process_t* process)
{
    auto* p = self(plugin);
    readParamEvents(*p, process->in_events);
    if (p->seedRequested.exchange(false, std::memory_order_acq_rel)) {
        p->params.seed = p->params.seed * 1664525u + 1013904223u;
        if (p->params.seed == 0u) p->params.seed = 1u;
        p->mixer.setParams(p->params);
        p->mixer.reseed(p->params.seed, 0.56f);
    }
    if (p->panicRequested.exchange(false, std::memory_order_acq_rel)) {
        p->mixer.panic();
    }
    uint32_t killMask = p->killMask.exchange(0u,
        std::memory_order_acq_rel);
    for (uint32_t lane = 0u; lane < kChannelCount; ++lane) {
        if ((killMask & (1u << lane)) != 0u) p->mixer.killLane(lane);
    }

    if (process->audio_outputs_count == 0u) return CLAP_PROCESS_CONTINUE;
    const clap_audio_buffer_t& output = process->audio_outputs[0];
    if (!output.data32 && !output.data64) return CLAP_PROCESS_CONTINUE;
    const uint32_t writableChannels = std::min<uint32_t>(
        output.channel_count, kChannelCount);
    std::array<float, kChannelCount> blockPeaks {};
    float blockPeak = 0.0f;

    for (uint32_t frame = 0u; frame < process->frames_count; ++frame) {
        p->mixer.processFrame(p->frame.data());
        if (p->routeScopeCountdown == 0u) {
            const uint64_t sequence = p->routeScopeSequence.load(
                std::memory_order_relaxed);
            const uint32_t slot = static_cast<uint32_t>(
                sequence % kRouteScopeSamples);
            for (uint32_t route = 0u;
                 route < s3g::kNoInputMixerMatrixCells; ++route) {
                p->routeScope[route * kRouteScopeSamples + slot].store(
                    p->mixer.routeSignal(route),
                    std::memory_order_relaxed);
            }
            p->routeScopeSequence.store(sequence + 1u,
                std::memory_order_release);
            p->routeScopeCountdown = p->routeScopeDecimation - 1u;
        } else {
            --p->routeScopeCountdown;
        }
        for (uint32_t lane = 0u; lane < writableChannels; ++lane) {
            const float value = p->frame[lane];
            if (output.data32 && output.data32[lane]) {
                output.data32[lane][frame] = value;
            }
            if (output.data64 && output.data64[lane]) {
                output.data64[lane][frame] = static_cast<double>(value);
            }
            blockPeaks[lane] = std::max(blockPeaks[lane], std::abs(value));
            blockPeak = std::max(blockPeak, std::abs(value));
        }
        for (uint32_t lane = writableChannels;
             lane < output.channel_count; ++lane) {
            if (output.data32 && output.data32[lane]) {
                output.data32[lane][frame] = 0.0f;
            }
            if (output.data64 && output.data64[lane]) {
                output.data64[lane][frame] = 0.0;
            }
        }
    }

    p->outputPeak.store(std::max(
        p->outputPeak.load(std::memory_order_relaxed) * 0.90f, blockPeak),
        std::memory_order_relaxed);
    for (uint32_t lane = 0u; lane < kChannelCount; ++lane) {
        p->lanePeaks[lane].store(std::max(
            p->lanePeaks[lane].load(std::memory_order_relaxed) * 0.90f,
            blockPeaks[lane]), std::memory_order_relaxed);
        p->laneActivity[lane].store(p->mixer.laneActivity(lane),
            std::memory_order_relaxed);
    }
    p->networkActivity.store(p->mixer.networkActivity(),
        std::memory_order_relaxed);
    p->motionPhase.store(p->mixer.motionPhase(),
        std::memory_order_relaxed);
    for (uint32_t bus = 0u; bus < 2u; ++bus) {
        p->auxActivity[bus].store(p->mixer.auxActivity(bus),
            std::memory_order_relaxed);
    }
    p->minimumGovernor.store(p->mixer.minimumGovernor(),
        std::memory_order_relaxed);
    p->containmentState.store(
        static_cast<uint32_t>(p->mixer.containmentState()),
        std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput)
{
    return isInput ? 0u : 1u;
}

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_audio_port_info_t* info)
{
    if (isInput || index != 0u || !info) return false;
    info->id = 20u;
    std::snprintf(info->name, sizeof(info->name), "8ch Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kChannelCount;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts {
    audioPortsCount, audioPortsGet,
};

uint32_t paramsCount(const clap_plugin_t*) { return kTotalParamCount; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index,
    clap_param_info_t* info)
{
    if (!info || index >= kTotalParamCount) return false;
    const clap_id id = paramIdAtIndex(index);
    ParamRange range;
    if (id == CLAP_INVALID_ID || !paramRange(id, range)) return false;
    std::memset(info, 0, sizeof(*info));
    info->id = id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE
        | (range.stepped ? CLAP_PARAM_IS_STEPPED : 0u);
    info->min_value = range.minimum;
    info->max_value = range.maximum;
    info->default_value = range.defaultValue;

    for (const auto& def : kGlobalParamDefs) {
        if (def.id == id) {
            std::snprintf(info->name, sizeof(info->name), "%s", def.name);
            std::snprintf(info->module, sizeof(info->module), "%s",
                def.module);
            return true;
        }
    }
    uint32_t destination = 0u;
    uint32_t source = 0u;
    if (decodeMatrixParam(id, destination, source)) {
        std::snprintf(info->name, sizeof(info->name), "Route L%u to L%u",
            source + 1u, destination + 1u);
        std::snprintf(info->module, sizeof(info->module), "Matrix");
        return true;
    }
    uint32_t lane = 0u;
    clap_id offset = 0u;
    if (!decodeLaneParam(id, lane, offset)) return false;
    static constexpr const char* laneNames[] {
        "Body", "Loss", "Level", "Mute", "Low", "Mid Frequency",
        "Mid Gain", "High", "Aux A Send", "Aux B Send",
    };
    if (offset < kLaneDirectParamCount) {
        std::snprintf(info->name, sizeof(info->name), "%s",
            laneNames[offset]);
        std::snprintf(info->module, sizeof(info->module), "Lane %u",
            lane + 1u);
        return true;
    }
    uint32_t slot = 0u;
    clap_id insertOffset = 0u;
    if (!decodeInsertOffset(offset, slot, insertOffset)) return false;
    static constexpr const char* insertNames[] {
        "Type", "Gain", "Tone", "Bias", "Level", "Bypass",
    };
    std::snprintf(info->name, sizeof(info->name), "Slot %u %s",
        slot + 1u, insertNames[insertOffset]);
    std::snprintf(info->module, sizeof(info->module), "Lane %u / Insert %u",
        lane + 1u, slot + 1u);
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    return value && paramValue(*self(plugin), id, *value);
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value,
    char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
    if (id == kOutputParamId || id == kCeilingParamId) {
        std::snprintf(display, size, "%+.1f dB", value);
        return true;
    }
    if (id == kLimiterParamId || id == kDcBlockParamId) {
        std::snprintf(display, size, "%s", value >= 0.5 ? "ON" : "OFF");
        return true;
    }
    if (id == kQualityParamId) {
        const uint32_t quality = static_cast<uint32_t>(
            std::clamp(std::lround(value), 0l, 2l));
        std::snprintf(display, size, "%uX", 1u << quality);
        return true;
    }
    if (id == kMotionShapeParamId) {
        const auto shape = s3g::matrixFlowShapeFromIndex(
            static_cast<uint32_t>(std::clamp(std::lround(value), 0l, 5l)));
        std::snprintf(display, size, "%s", s3g::matrixFlowShapeName(shape));
        return true;
    }
    if (id == kAuxATypeParamId || id == kAuxBTypeParamId) {
        const uint32_t type = static_cast<uint32_t>(std::clamp(
            std::lround(value), 0l,
            static_cast<long>(s3g::kNoInputDistortionTypeCount - 1u)));
        std::snprintf(display, size, "%s", s3g::noInputDistortionName(
            static_cast<s3g::NoInputDistortionType>(type)));
        return true;
    }
    if ((id >= kFeedbackParamId && id <= kFormantParamId)
        || id == kAgencyParamId || id == kSpaceParamId
        || id == kVarianceParamId || id == kFlowParamId
        || id == kSpreadParamId || id == kMotionParamId
        || id == kMotionRateParamId || id == kMotionPhaseParamId
        || (id >= kAuxAGainParamId && id <= kAuxAFeedbackParamId)
        || (id >= kAuxBGainParamId && id <= kAuxBFeedbackParamId)) {
        std::snprintf(display, size, "%.0f%%", value * 100.0);
        return true;
    }
    if (id == kInternalToneParamId || id == kHouseToneParamId
        || id == kVortexParamId) {
        std::snprintf(display, size, "%+.2f", value);
        return true;
    }
    uint32_t destination = 0u;
    uint32_t source = 0u;
    if (decodeMatrixParam(id, destination, source)) {
        std::snprintf(display, size, "%+.2f", value);
        return true;
    }
    uint32_t lane = 0u;
    clap_id offset = 0u;
    if (!decodeLaneParam(id, lane, offset)) return false;
    if (offset == kLaneBodyOffset || offset == kLaneLossOffset
        || offset == kLaneAuxAOffset || offset == kLaneAuxBOffset) {
        std::snprintf(display, size, "%.0f%%", value * 100.0);
        return true;
    }
    if (offset == kLaneLevelOffset || offset == kLaneLowOffset
        || offset == kLaneMidGainOffset || offset == kLaneHighOffset) {
        std::snprintf(display, size, "%+.1f dB", value);
        return true;
    }
    if (offset == kLaneMuteOffset) {
        std::snprintf(display, size, "%s", value >= 0.5 ? "ON" : "OFF");
        return true;
    }
    if (offset == kLaneMidFrequencyOffset) {
        if (value >= 1000.0) std::snprintf(display, size, "%.2f kHz",
            value * 0.001);
        else std::snprintf(display, size, "%.0f Hz", value);
        return true;
    }
    uint32_t slot = 0u;
    clap_id insertOffset = 0u;
    if (!decodeInsertOffset(offset, slot, insertOffset)) return false;
    if (insertOffset == kInsertTypeOffset) {
        const uint32_t type = static_cast<uint32_t>(std::clamp(
            std::lround(value), 0l,
            static_cast<long>(s3g::kNoInputDistortionTypeCount - 1u)));
        std::snprintf(display, size, "%s", s3g::noInputDistortionName(
            static_cast<s3g::NoInputDistortionType>(type)));
    } else if (insertOffset == kInsertGainOffset
        || insertOffset == kInsertToneOffset) {
        std::snprintf(display, size, "%.0f%%", value * 100.0);
    } else if (insertOffset == kInsertBiasOffset) {
        std::snprintf(display, size, "%+.2f", value);
    } else if (insertOffset == kInsertLevelOffset) {
        std::snprintf(display, size, "%+.1f dB", value);
    } else if (insertOffset == kInsertBypassOffset) {
        std::snprintf(display, size, "%s", value >= 0.5 ? "ON" : "OFF");
    } else {
        return false;
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id,
    const char* display, double* value)
{
    if (!display || !value) return false;
    if (id == kMotionShapeParamId) {
        for (uint32_t shape = 0u; shape <= 5u; ++shape) {
            if (std::strcmp(display, s3g::matrixFlowShapeName(
                    s3g::matrixFlowShapeFromIndex(shape))) == 0) {
                *value = shape;
                return true;
            }
        }
    }
    if (id == kAuxATypeParamId || id == kAuxBTypeParamId) {
        for (uint32_t type = 0u;
             type < s3g::kNoInputDistortionTypeCount; ++type) {
            if (std::strcmp(display, s3g::noInputDistortionName(
                    static_cast<s3g::NoInputDistortionType>(type))) == 0) {
                *value = type;
                return true;
            }
        }
    }
    uint32_t lane = 0u;
    clap_id offset = 0u;
    if (decodeLaneParam(id, lane, offset)) {
        uint32_t slot = 0u;
        clap_id insertOffset = 0u;
        if (decodeInsertOffset(offset, slot, insertOffset)
            && insertOffset == kInsertTypeOffset) {
            for (uint32_t type = 0u;
                 type < s3g::kNoInputDistortionTypeCount; ++type) {
                if (std::strcmp(display, s3g::noInputDistortionName(
                        static_cast<s3g::NoInputDistortionType>(type))) == 0) {
                    *value = type;
                    return true;
                }
            }
        }
    }
    if (std::strcmp(display, "ON") == 0) { *value = 1.0; return true; }
    if (std::strcmp(display, "OFF") == 0) { *value = 0.0; return true; }
    if (id == kQualityParamId) {
        const double parsed = std::atof(display);
        *value = parsed >= 4.0 ? 2.0 : (parsed >= 2.0 ? 1.0 : 0.0);
        return true;
    }
    *value = std::atof(display);
    if (std::strchr(display, '%')) *value *= 0.01;
    if (decodeLaneParam(id, lane, offset)
        && offset == kLaneMidFrequencyOffset
        && (std::strstr(display, "kHz") || std::strstr(display, "khz"))) {
        *value *= 1000.0;
    }
    return true;
}

void paramsFlush(const clap_plugin_t* plugin,
    const clap_input_events_t* in, const clap_output_events_t*)
{
    readParamEvents(*self(plugin), in);
}

const clap_plugin_params_t paramsExt {
    paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText,
    paramsTextToValue, paramsFlush,
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    const auto* p = self(plugin);
    SavedState state;
    state.params = p->params;
    state.selectedLane = p->selectedLane.load(std::memory_order_relaxed);
    state.selectedSlot = p->selectedSlot.load(std::memory_order_relaxed);
    state.selectedSource = p->selectedSource.load(std::memory_order_relaxed);
    state.selectedDestination = p->selectedDestination.load(
        std::memory_order_relaxed);
    state.guiPage = p->guiPage.load(std::memory_order_relaxed);
    const auto* bytes = reinterpret_cast<const uint8_t*>(&state);
    uint64_t offset = 0u;
    while (offset < sizeof(state)) {
        const int64_t written = stream->write(stream, bytes + offset,
            sizeof(state) - offset);
        if (written <= 0) return false;
        offset += static_cast<uint64_t>(written);
    }
    return true;
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    const auto readExact = [stream](void* destination, uint64_t bytes) {
        auto* output = static_cast<uint8_t*>(destination);
        uint64_t offset = 0u;
        while (offset < bytes) {
            const int64_t read = stream->read(stream, output + offset,
                bytes - offset);
            if (read <= 0) return false;
            offset += static_cast<uint64_t>(read);
        }
        return true;
    };
    uint32_t version = 0u;
    if (!readExact(&version, sizeof(version))) return false;
    SavedState state;
    if (version == kStateVersion) {
        state.version = version;
        if (!readExact(reinterpret_cast<uint8_t*>(&state) + sizeof(version),
                sizeof(state) - sizeof(version))) {
            return false;
        }
    } else if (version == 1u) {
        LegacySavedState legacy;
        legacy.version = version;
        if (!readExact(reinterpret_cast<uint8_t*>(&legacy) + sizeof(version),
                sizeof(legacy) - sizeof(version))) {
            return false;
        }
        state.params = s3g::defaultNoInputMixerParams();
        const auto& source = legacy.params;
        auto& destination = state.params;
        destination.outputGainDb = source.outputGainDb;
        destination.ceilingDb = source.ceilingDb;
        destination.limiterEnabled = source.limiterEnabled;
        destination.dcBlockEnabled = source.dcBlockEnabled;
        destination.feedback = source.feedback;
        destination.coupling = source.coupling;
        destination.phase = source.phase;
        destination.drift = source.drift;
        destination.formant = source.formant;
        destination.quality = source.quality;
        destination.seed = source.seed;
        destination.matrix = source.matrix;
        for (uint32_t lane = 0u; lane < kChannelCount; ++lane) {
            const auto& oldLane = source.lanes[lane];
            auto& newLane = destination.lanes[lane];
            newLane.body = oldLane.body;
            newLane.loss = oldLane.loss;
            newLane.levelDb = oldLane.levelDb;
            newLane.mute = oldLane.mute;
            newLane.lowDb = oldLane.lowDb;
            newLane.midFrequencyHz = oldLane.midFrequencyHz;
            newLane.midGainDb = oldLane.midGainDb;
            newLane.highDb = oldLane.highDb;
            for (uint32_t slot = 0u;
                 slot < s3g::kNoInputMixerInsertSlots; ++slot) {
                const auto& oldInsert = oldLane.inserts[slot];
                auto& newInsert = newLane.inserts[slot];
                newInsert.type = oldInsert.type;
                newInsert.gain = oldInsert.gain;
                newInsert.tone = oldInsert.tone;
                newInsert.bias = oldInsert.bias;
                newInsert.levelDb = oldInsert.levelDb;
                newInsert.bypass = oldInsert.bypass;
            }
        }
        state.selectedLane = legacy.selectedLane;
        state.selectedSlot = legacy.selectedSlot;
        state.selectedSource = legacy.selectedSource;
        state.selectedDestination = legacy.selectedDestination;
        state.guiPage = legacy.guiPage == 0u ? 0u : legacy.guiPage + 1u;
    } else {
        return false;
    }
    if (state.version != kStateVersion) {
        state.version = kStateVersion;
    }
    auto* p = self(plugin);
    p->params = s3g::sanitizeNoInputMixerParams(state.params);
    p->selectedLane.store(std::min<uint32_t>(state.selectedLane,
        kChannelCount - 1u), std::memory_order_relaxed);
    p->selectedSlot.store(std::min<uint32_t>(state.selectedSlot,
        s3g::kNoInputMixerInsertSlots - 1u), std::memory_order_relaxed);
    p->selectedSource.store(std::min<uint32_t>(state.selectedSource,
        kChannelCount - 1u), std::memory_order_relaxed);
    p->selectedDestination.store(std::min<uint32_t>(
        state.selectedDestination, kChannelCount - 1u),
        std::memory_order_relaxed);
    p->guiPage.store(std::min<uint32_t>(state.guiPage, 3u),
        std::memory_order_relaxed);
    p->mixer.setParams(p->params);
    p->mixer.reseed(p->params.seed, 0.48f);
    resetMeters(*p);
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)

namespace {

enum OpenMenu : int {
    kMenuNone = -1,
    kMenuPreset = 0,
    kMenuLane,
    kMenuSource,
    kMenuDestination,
    kMenuSlot0,
    kMenuSlot1,
    kMenuSlot2,
    kMenuMotionShape,
    kMenuQuality,
};

void applyCompletePatch(Plugin& plugin, s3g::NoInputMixerParams params,
    float seedAmount)
{
    plugin.params = s3g::sanitizeNoInputMixerParams(params);
    plugin.mixer.setParams(plugin.params);
    plugin.mixer.reseed(plugin.params.seed, seedAmount);
    resetMeters(plugin);
}

NSRect processorMenuRect(const s3g::gui_layout::Panel& panel,
    uint32_t row);

NSRect seedNewButtonRect()
{
    const auto& panel =
        s3g::gui_layout::kNoInputMixerFamilyLayout.network;
    const NSRect row = processorMenuRect(panel, 0u);
    return NSMakeRect(row.origin.x, row.origin.y, 58.0, row.size.height);
}

NSRect randomButtonRect()
{
    const NSRect seed = seedNewButtonRect();
    return NSMakeRect(NSMaxX(seed) + 6.0, seed.origin.y,
        66.0, seed.size.height);
}

NSRect forgetButtonRect()
{
    const NSRect random = randomButtonRect();
    return NSMakeRect(NSMaxX(random) + 6.0, random.origin.y,
        66.0, random.size.height);
}

NSRect processorMenuRect(const s3g::gui_layout::Panel& panel,
    uint32_t row)
{
    return NSMakeRect(
        s3g::gui_layout::processorControlX(panel.frame.x),
        s3g::gui_layout::rowY(panel, row) - 1.0,
        s3g::gui_layout::processorMenuWidth(panel.frame.width), 15.0);
}

NSRect fieldTabRect(uint32_t index)
{
    const auto& family = s3g::gui_layout::kNoInputMixerFamilyLayout;
    constexpr CGFloat width = 58.0;
    constexpr CGFloat gap = 5.0;
    return NSMakeRect(family.fieldPanel.x + family.fieldPanel.width
            - 4.0 * width - 3.0 * gap - 10.0
            + static_cast<CGFloat>(index) * (width + gap),
        family.fieldPanel.y + 4.0, width, 14.0);
}

NSRect mixerPopButtonRect()
{
    const auto& field =
        s3g::gui_layout::kNoInputMixerFamilyLayout.fieldPanel;
    return NSMakeRect(field.x + field.width - 4.0 * 58.0
            - 3.0 * 5.0 - 68.0,
        field.y + 4.0, 48.0, 14.0);
}

NSRect patchVisualRect()
{
    return NSMakeRect(28.0, 78.0, 896.0, 714.0);
}

NSRect channelOverviewRect()
{
    return patchVisualRect();
}

NSRect wiringModeButtonRect(uint32_t index)
{
    const NSRect visual = patchVisualRect();
    return NSMakeRect(NSMaxX(visual) - 116.0 + index * 54.0,
        visual.origin.y + 10.0, 48.0, 18.0);
}

NSRect clearConnectionsButtonRect()
{
    const NSRect visual = patchVisualRect();
    return NSMakeRect(NSMaxX(visual) - 196.0,
        visual.origin.y + 10.0, 74.0, 18.0);
}

NSRect wiringGridRect()
{
    const NSRect visual = patchVisualRect();
    return NSMakeRect(visual.origin.x + 154.0,
        visual.origin.y + 34.0, 588.0, visual.size.height - 46.0);
}

NSPoint wiringPortPoint(bool destination, uint32_t lane)
{
    const NSRect visual = patchVisualRect();
    const CGFloat top = visual.origin.y + 72.0;
    const CGFloat gap = (visual.size.height - 144.0) / 7.0;
    return NSMakePoint(destination ? NSMaxX(visual) - 54.0
                                   : visual.origin.x + 54.0,
        top + lane * gap);
}

NSPoint cubicPoint(NSPoint a, NSPoint c1, NSPoint c2, NSPoint b,
    CGFloat t)
{
    const CGFloat u = 1.0 - t;
    const CGFloat aa = u * u * u;
    const CGFloat bb = 3.0 * u * u * t;
    const CGFloat cc = 3.0 * u * t * t;
    const CGFloat dd = t * t * t;
    return NSMakePoint(aa * a.x + bb * c1.x + cc * c2.x + dd * b.x,
        aa * a.y + bb * c1.y + cc * c2.y + dd * b.y);
}

NSPoint cubicTangent(NSPoint a, NSPoint c1, NSPoint c2, NSPoint b,
    CGFloat t)
{
    const CGFloat u = 1.0 - t;
    return NSMakePoint(
        3.0 * u * u * (c1.x - a.x)
            + 6.0 * u * t * (c2.x - c1.x)
            + 3.0 * t * t * (b.x - c2.x),
        3.0 * u * u * (c1.y - a.y)
            + 6.0 * u * t * (c2.y - c1.y)
            + 3.0 * t * t * (b.y - c2.y));
}

void wiringControlPoints(NSPoint a, NSPoint b, float vortex,
    NSPoint& c1, NSPoint& c2)
{
    const CGFloat centerX = (a.x + b.x) * 0.5;
    const CGFloat arc = (b.y - a.y) * 0.18
        + static_cast<CGFloat>(vortex) * 76.0;
    const CGFloat centerY = (a.y + b.y) * 0.5 + arc;
    c1 = NSMakePoint(centerX - 106.0, centerY);
    c2 = NSMakePoint(centerX + 106.0, centerY);
}

NSPoint mixerSurfaceOffset(NSRect surface)
{
    constexpr CGFloat contentWidth = 1216.0;
    constexpr CGFloat contentHeight = 706.0;
    return NSMakePoint(surface.origin.x - 12.0
            + std::max<CGFloat>(0.0, (surface.size.width - contentWidth) * 0.5),
        surface.origin.y - 42.0
            + std::max<CGFloat>(0.0, (surface.size.height - contentHeight) * 0.5));
}

NSRect translatedRect(NSRect rect, NSPoint offset)
{
    rect.origin.x += offset.x;
    rect.origin.y += offset.y;
    return rect;
}


NSRect popupStripRect(uint32_t lane)
{
    constexpr CGFloat gap = 6.0;
    constexpr CGFloat left = 12.0;
    constexpr CGFloat areaWidth = 858.0;
    const CGFloat width = (areaWidth - gap * 7.0) / 8.0;
    return NSMakeRect(left + lane * (width + gap), 42.0,
        width, kPerformanceMixerReferenceHeight - 54.0);
}

NSRect popupAuxPanelRect()
{
    return NSMakeRect(884.0, 42.0, 344.0, 706.0);
}

NSRect popupBodyRect(NSRect strip)
{
    return NSMakeRect(strip.origin.x + 10.0, strip.origin.y + 54.0,
        strip.size.width - 20.0, 10.0);
}

NSRect popupLossRect(NSRect strip)
{
    return NSMakeRect(strip.origin.x + 10.0, strip.origin.y + 88.0,
        strip.size.width - 20.0, 10.0);
}

NSRect popupLoopRect(NSRect strip)
{
    return NSMakeRect(NSMidX(strip) - 7.0, strip.origin.y + 132.0,
        14.0, 108.0);
}

NSRect popupEqRect(NSRect strip, uint32_t band)
{
    constexpr CGFloat width = 11.0;
    constexpr CGFloat gap = 11.0;
    const CGFloat total = width * 3.0 + gap * 2.0;
    return NSMakeRect(NSMidX(strip) - total * 0.5
            + band * (width + gap), strip.origin.y + 286.0,
        width, 88.0);
}

NSRect popupSendRect(NSRect strip, uint32_t bus)
{
    return NSMakeRect(strip.origin.x + 10.0,
        strip.origin.y + 414.0 + bus * 36.0,
        strip.size.width - 20.0, 10.0);
}

NSRect popupInsertRect(NSRect strip, uint32_t slot)
{
    return NSMakeRect(strip.origin.x + 8.0,
        strip.origin.y + 500.0 + slot * 21.0,
        strip.size.width - 16.0, 18.0);
}

NSRect popupFaderRect(NSRect strip)
{
    return NSMakeRect(NSMidX(strip) - 7.0, strip.origin.y + 582.0,
        14.0, 74.0);
}

NSRect popupMuteRect(NSRect strip)
{
    return NSMakeRect(strip.origin.x + 8.0, strip.origin.y + 672.0,
        strip.size.width - 16.0, 20.0);
}

NSRect popupAuxTypeRect(uint32_t bus)
{
    const NSRect panel = popupAuxPanelRect();
    return NSMakeRect(panel.origin.x + 114.0,
        panel.origin.y + 58.0 + bus * 248.0, 204.0, 18.0);
}

NSRect popupAuxSliderRect(uint32_t bus, uint32_t local)
{
    const NSRect panel = popupAuxPanelRect();
    return NSMakeRect(panel.origin.x + 114.0,
        panel.origin.y + 96.0 + bus * 248.0 + local * 36.0,
        204.0, 10.0);
}

NSRect popupToneRect(uint32_t row)
{
    const NSRect panel = popupAuxPanelRect();
    return NSMakeRect(panel.origin.x + 114.0,
        panel.origin.y + 594.0 + row * 40.0, 204.0, 10.0);
}

NSColor* mixerColor(int rgb, CGFloat alpha = 1.0)
{
    return s3g::clap_gui::color(rgb, alpha);
}

void drawFlatButton(NSRect rect, NSString* text, bool active,
    NSDictionary* attrs)
{
    [mixerColor(active ? 0x303030 : 0x151515) setFill];
    NSRectFill(rect);
    [mixerColor(active ? 0xb8b8b8 : 0x555555) setStroke];
    NSFrameRect(rect);
    const NSSize size = [text sizeWithAttributes:attrs];
    [text drawAtPoint:NSMakePoint(
        rect.origin.x + (rect.size.width - size.width) * 0.5,
        rect.origin.y + (rect.size.height - size.height) * 0.5 - 0.5)
        withAttributes:attrs];
}

} // namespace

@interface S3GNoInputMixerView : NSView <NSWindowDelegate> {
    void* _plugin;
    clap_id _dragParam;
    NSTimer* _timer;
    int _openMenu;
    char _titlePresetName[64];
    clap_id _mixerDragParam;
    NSRect _mixerDragRect;
    double _mixerDragMinimum;
    double _mixerDragMaximum;
    BOOL _mixerDragVertical;
    BOOL _mixerPopupChild;
    S3GNoInputMixerView* _mixerPopupOwner;
    NSPanel* _pagePanels[4];
    S3GNoInputMixerView* _pagePopupViews[4];
    uint32_t _lockedPage;
    BOOL _wiringGridMode;
    int _wireDragSource;
    NSPoint _wireDragPoint;
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)updateSlider:(NSPoint)point;
- (void)applyGuiParam:(clap_id)param value:(double)value;
- (void)markPatchCustom;
- (void)openPagePopup:(uint32_t)page;
- (void)hideMixerPopup;
- (void)destroyMixerPopup;
- (NSPanel*)mixerPanel;
- (NSPanel*)patchPanel;
- (NSPanel*)channelPanel;
- (NSPanel*)safetyPanel;
- (uint32_t)activePage;
- (void)navigatePageBy:(NSInteger)delta;
- (void)clearAllConnections;
- (void)updateMixerDrag:(NSPoint)point;
- (void)beginMixerDrag:(clap_id)param rect:(NSRect)rect
    minimum:(double)minimum maximum:(double)maximum
    vertical:(BOOL)vertical point:(NSPoint)point;
- (void)drawPerformanceMixer:(Plugin*)plugin surface:(NSRect)surface;
@end

@implementation S3GNoInputMixerView

- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragParam = CLAP_INVALID_ID;
        _timer = nil;
        _openMenu = kMenuNone;
        _mixerDragParam = CLAP_INVALID_ID;
        _mixerDragRect = NSZeroRect;
        _mixerDragMinimum = 0.0;
        _mixerDragMaximum = 1.0;
        _mixerDragVertical = NO;
        _mixerPopupChild = NO;
        _mixerPopupOwner = nil;
        for (uint32_t page = 0u; page < 4u; ++page) {
            _pagePanels[page] = nil;
            _pagePopupViews[page] = nil;
        }
        _lockedPage = UINT32_MAX;
        _wiringGridMode = NO;
        _wireDragSource = -1;
        _wireDragPoint = NSZeroPoint;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
            "INIT");
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (uint32_t)activePage
{
    if (_lockedPage < 4u) return _lockedPage;
    auto* plugin = static_cast<Plugin*>(_plugin);
    return plugin ? plugin->guiPage.load(std::memory_order_relaxed) : 0u;
}

- (void)navigatePageBy:(NSInteger)delta
{
    auto* plugin = static_cast<Plugin*>(_plugin);
    if (!plugin || delta == 0) return;
    S3GNoInputMixerView* owner = _mixerPopupChild
        && _mixerPopupOwner ? _mixerPopupOwner : self;
    const NSInteger current = static_cast<NSInteger>([self activePage]);
    const uint32_t next = static_cast<uint32_t>(
        (current + (delta < 0 ? 3 : 1)) % 4);
    if (owner->_pagePanels[next]
        && [owner->_pagePanels[next] isVisible]) {
        [owner->_pagePanels[next] makeKeyAndOrderFront:nil];
        [owner->_pagePanels[next] makeFirstResponder:
            owner->_pagePopupViews[next]];
        return;
    }
    plugin->guiPage.store(next, std::memory_order_relaxed);
    NSWindow* mainWindow = [owner window];
    [mainWindow makeKeyAndOrderFront:nil];
    [mainWindow makeFirstResponder:owner];
    [owner setNeedsDisplay:YES];
}

- (void)keyDown:(NSEvent*)event
{
    const NSEventModifierFlags modifiers = [event modifierFlags]
        & NSEventModifierFlagDeviceIndependentFlagsMask;
    if ((modifiers & (NSEventModifierFlagCommand
            | NSEventModifierFlagOption | NSEventModifierFlagControl)) == 0
        && ([event keyCode] == 123u || [event keyCode] == 124u)) {
        [self navigatePageBy:[event keyCode] == 123u ? -1 : 1];
        return;
    }
    [super keyDown:event];
}

- (void)dealloc
{
    [self stopRefreshTimer];
    if (!_mixerPopupChild) [self destroyMixerPopup];
    [super dealloc];
}

- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [NSTimer timerWithTimeInterval:1.0 / 20.0 target:self
        selector:@selector(refresh:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}

- (void)stopRefreshTimer
{
    if (_timer) {
        [_timer invalidate];
        _timer = nil;
    }
}

- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    if (![self isHidden] && _plugin
        && s3g::clap_support::hostAppIsActive()) {
        [self setNeedsDisplay:YES];
    }
}

- (void)markPatchCustom
{
    if (_mixerPopupChild && _mixerPopupOwner) {
        [_mixerPopupOwner markPatchCustom];
        return;
    }
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
        "CUSTOM");
}

- (void)applyGuiParam:(clap_id)param value:(double)value
{
    auto* plugin = static_cast<Plugin*>(_plugin);
    if (!plugin) return;
    applyParam(*plugin, param, value);
    [self markPatchCustom];
    [self setNeedsDisplay:YES];
    if (_mixerPopupChild && _mixerPopupOwner) {
        [_mixerPopupOwner setNeedsDisplay:YES];
        for (uint32_t page = 0u; page < 4u; ++page) {
            if (_mixerPopupOwner->_pagePopupViews[page])
                [_mixerPopupOwner->_pagePopupViews[page]
                    setNeedsDisplay:YES];
        }
    } else {
        for (uint32_t page = 0u; page < 4u; ++page) {
            if (_pagePopupViews[page])
                [_pagePopupViews[page] setNeedsDisplay:YES];
        }
    }
}

- (void)clearAllConnections
{
    auto* plugin = static_cast<Plugin*>(_plugin);
    if (!plugin) return;
    plugin->params.matrix.fill(0.0f);
    plugin->mixer.setParams(plugin->params);
    [self markPatchCustom];
    [self setNeedsDisplay:YES];
    S3GNoInputMixerView* owner = _mixerPopupChild
        && _mixerPopupOwner ? _mixerPopupOwner : self;
    [owner setNeedsDisplay:YES];
    for (uint32_t page = 0u; page < 4u; ++page) {
        if (owner->_pagePopupViews[page])
            [owner->_pagePopupViews[page] setNeedsDisplay:YES];
    }
}

- (void)beginMixerDrag:(clap_id)param rect:(NSRect)rect
    minimum:(double)minimum maximum:(double)maximum
    vertical:(BOOL)vertical point:(NSPoint)point
{
    _dragParam = CLAP_INVALID_ID;
    _mixerDragParam = param;
    _mixerDragRect = rect;
    _mixerDragMinimum = minimum;
    _mixerDragMaximum = maximum;
    _mixerDragVertical = vertical;
    [self updateMixerDrag:point];
}

- (void)updateMixerDrag:(NSPoint)point
{
    if (_mixerDragParam == CLAP_INVALID_ID) return;
    const double span = _mixerDragVertical
        ? std::max(1.0, static_cast<double>(_mixerDragRect.size.height))
        : std::max(1.0, static_cast<double>(_mixerDragRect.size.width));
    const double normalized = std::clamp(_mixerDragVertical
            ? (NSMaxY(_mixerDragRect) - point.y) / span
            : (point.x - NSMinX(_mixerDragRect)) / span,
        0.0, 1.0);
    [self applyGuiParam:_mixerDragParam
        value:_mixerDragMinimum
            + normalized * (_mixerDragMaximum - _mixerDragMinimum)];
}

- (NSPanel*)mixerPanel
{
    return _pagePanels[1u];
}

- (NSPanel*)patchPanel { return _pagePanels[0u]; }
- (NSPanel*)channelPanel { return _pagePanels[2u]; }
- (NSPanel*)safetyPanel { return _pagePanels[3u]; }

- (void)openPagePopup:(uint32_t)page
{
    if (_mixerPopupChild || page >= 4u) return;
    static NSString* pageNames[4] = {
        @"PATCH", @"MIXER", @"CHANNEL", @"SAFETY",
    };
    if (!_pagePanels[page]) {
        _pagePanels[page] = [[NSPanel alloc] initWithContentRect:NSMakeRect(
                0.0, 0.0, kGuiWidth, kGuiHeight)
            styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                | NSWindowStyleMaskUtilityWindow)
            backing:NSBackingStoreBuffered defer:NO];
        [_pagePanels[page] setTitle:[NSString stringWithFormat:
            @"s3g PROCESSOR NO INPUT MIXER — %@", pageNames[page]]];
        [_pagePanels[page] setReleasedWhenClosed:NO];
        [_pagePanels[page] setHidesOnDeactivate:YES];
        [_pagePanels[page] setDelegate:self];
        _pagePopupViews[page] = [[S3GNoInputMixerView alloc]
            initWithPlugin:_plugin];
        [_pagePopupViews[page] setFrame:NSMakeRect(
            0.0, 0.0, kGuiWidth, kGuiHeight)];
        _pagePopupViews[page]->_mixerPopupChild = YES;
        _pagePopupViews[page]->_mixerPopupOwner = self;
        _pagePopupViews[page]->_lockedPage = page;
        _pagePopupViews[page]->_wiringGridMode = _wiringGridMode;
        [_pagePanels[page] setContentView:_pagePopupViews[page]];
        [_pagePanels[page] setContentSize:NSMakeSize(kGuiWidth, kGuiHeight)];
        [_pagePopupViews[page] setFrame:NSMakeRect(
            0.0, 0.0, kGuiWidth, kGuiHeight)];
        [_pagePopupViews[page] setBounds:NSMakeRect(
            0.0, 0.0, kGuiWidth, kGuiHeight)];
        [_pagePopupViews[page] release];

        NSWindow* parent = [self window];
        const NSRect parentFrame = parent ? [parent frame]
            : [[NSScreen mainScreen] visibleFrame];
        const NSRect panelFrame = [_pagePanels[page] frame];
        NSScreen* screen = parent ? [parent screen] : [NSScreen mainScreen];
        const NSRect visible = screen ? [screen visibleFrame] : parentFrame;
        CGFloat x = NSMaxX(parentFrame) + 8.0 + page * 22.0;
        if (x + panelFrame.size.width > NSMaxX(visible)) {
            x = NSMinX(parentFrame) - panelFrame.size.width
                - 8.0 - page * 22.0;
        }
        const CGFloat y = std::clamp(NSMaxY(parentFrame)
                - panelFrame.size.height - page * 22.0,
            NSMinY(visible), std::max(NSMinY(visible),
                NSMaxY(visible) - panelFrame.size.height));
        [_pagePanels[page] setFrameOrigin:NSMakePoint(
            std::max(NSMinX(visible), x), y)];
    }
    NSWindow* parent = [self window];
    NSWindow* previousParent = [_pagePanels[page] parentWindow];
    if (previousParent && previousParent != parent) {
        [previousParent removeChildWindow:_pagePanels[page]];
    }
    if (parent && [_pagePanels[page] parentWindow] != parent) {
        [parent addChildWindow:_pagePanels[page] ordered:NSWindowAbove];
    }
    auto* plugin = static_cast<Plugin*>(_plugin);
    if (plugin && plugin->guiPage.load(std::memory_order_relaxed) == page) {
        for (uint32_t candidate = 1u; candidate < 5u; ++candidate) {
            const uint32_t next = (page + candidate) % 4u;
            if (!_pagePanels[next] || ![_pagePanels[next] isVisible]) {
                plugin->guiPage.store(next, std::memory_order_relaxed);
                break;
            }
        }
    }
    [_pagePopupViews[page] startRefreshTimer];
    [_pagePanels[page] makeKeyAndOrderFront:nil];
    [self setNeedsDisplay:YES];
}

- (void)hideMixerPopup
{
    for (uint32_t page = 0u; page < 4u; ++page) {
        if (!_pagePanels[page]) continue;
        [_pagePopupViews[page] stopRefreshTimer];
        NSWindow* parent = [_pagePanels[page] parentWindow];
        if (parent) [parent removeChildWindow:_pagePanels[page]];
        [_pagePanels[page] orderOut:nil];
    }
    [self setNeedsDisplay:YES];
}

- (void)destroyMixerPopup
{
    for (uint32_t page = 0u; page < 4u; ++page) {
        if (!_pagePanels[page]) continue;
        [_pagePopupViews[page] stopRefreshTimer];
        [_pagePanels[page] setDelegate:nil];
        NSWindow* parent = [_pagePanels[page] parentWindow];
        if (parent) [parent removeChildWindow:_pagePanels[page]];
        [_pagePanels[page] orderOut:nil];
        [_pagePanels[page] release];
        _pagePanels[page] = nil;
        _pagePopupViews[page] = nil;
    }
}

- (void)windowWillClose:(NSNotification*)notification
{
    for (uint32_t page = 0u; page < 4u; ++page) {
        if ([notification object] != _pagePanels[page]) continue;
        [_pagePopupViews[page] stopRefreshTimer];
        NSWindow* parent = [_pagePanels[page] parentWindow];
        if (parent) [parent removeChildWindow:_pagePanels[page]];
        [self setNeedsDisplay:YES];
        return;
    }
}

- (void)drawSlider:(NSString*)name value:(NSString*)value
    norm:(CGFloat)norm row:(uint32_t)row
    panel:(const s3g::gui_layout::Panel&)panel
    label:(NSDictionary*)label valueAttrs:(NSDictionary*)valueAttrs
    style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawProcessorSlider(name, value, norm,
        s3g::gui_layout::rowY(panel, row), panel.frame.x,
        panel.frame.width, label, valueAttrs, style);
}

- (void)drawPrimaryWiring:(Plugin*)plugin rect:(NSRect)rect
    label:(NSDictionary*)label valueAttrs:(NSDictionary*)valueAttrs
{
    [mixerColor(0x101010) setFill]; NSRectFill(rect);
    [mixerColor(0x454545) setStroke]; NSFrameRect(rect);
    drawFlatButton(wiringModeButtonRect(0u), @"WIRES",
        !_wiringGridMode, valueAttrs);
    drawFlatButton(wiringModeButtonRect(1u), @"GRID",
        _wiringGridMode, valueAttrs);
    drawFlatButton(clearConnectionsButtonRect(), @"CLEAR ALL",
        false, valueAttrs);
    if (_wiringGridMode) {
        [self drawPrimaryMatrix:plugin rect:wiringGridRect() label:label
            valueAttrs:valueAttrs];
        return;
    }

    const uint64_t scopeSequence = plugin->routeScopeSequence.load(
        std::memory_order_acquire);
    const uint32_t selectedSource = plugin->selectedSource.load(
        std::memory_order_relaxed);
    const uint32_t selectedDestination = plugin->selectedDestination.load(
        std::memory_order_relaxed);
    CGFloat selectedRms = 0.0;

    for (uint32_t destination = 0u; destination < kChannelCount;
         ++destination) {
        for (uint32_t source = 0u; source < kChannelCount; ++source) {
            const uint32_t index = destination * kChannelCount + source;
            const float stored = plugin->params.matrix[index];
            const CGFloat manual = std::abs(stored);
            if (manual <= 0.001f) continue;
            const NSPoint a = wiringPortPoint(false, source);
            const NSPoint b = wiringPortPoint(true, destination);
            NSPoint c1;
            NSPoint c2;
            wiringControlPoints(a, b, plugin->params.vortex, c1, c2);
            NSBezierPath* path = [NSBezierPath bezierPath];
            [path moveToPoint:a];
            [path curveToPoint:b controlPoint1:c1 controlPoint2:c2];
            const bool selected = source == selectedSource
                && destination == selectedDestination;
            if (selected) {
                [path setLineWidth:5.0 + manual * 5.0];
                [mixerColor(0xb8b8b8, 0.22) setStroke];
                [path stroke];
            }
            [path setLineWidth:0.7 + manual * 5.2];
            [mixerColor(stored >= 0.0f ? 0xc95e3b : 0x57bfc4,
                0.12 + manual * 0.32) setStroke];
            [path stroke];

            std::array<CGFloat, kRouteScopeSamples> samples {};
            CGFloat peak = 0.0;
            CGFloat sumSquares = 0.0;
            const uint32_t available = static_cast<uint32_t>(
                std::min<uint64_t>(scopeSequence, kRouteScopeSamples));
            const uint32_t padding = kRouteScopeSamples - available;
            const uint64_t first = scopeSequence - available;
            for (uint32_t sample = padding;
                 sample < kRouteScopeSamples; ++sample) {
                const uint64_t sequence = first + sample - padding;
                const uint32_t slot = static_cast<uint32_t>(
                    sequence % kRouteScopeSamples);
                const CGFloat value = plugin->routeScope[
                    index * kRouteScopeSamples + slot].load(
                        std::memory_order_relaxed);
                samples[sample] = std::isfinite(value) ? value : 0.0;
                peak = std::max(peak, std::abs(samples[sample]));
                sumSquares += samples[sample] * samples[sample];
            }
            const CGFloat rms = std::sqrt(sumSquares
                / static_cast<CGFloat>(kRouteScopeSamples));
            const CGFloat levelDb = 20.0 * std::log10(
                std::max<CGFloat>(rms, 1.0e-6));
            const CGFloat level = std::clamp<CGFloat>(
                (levelDb + 72.0) / 66.0, 0.0, 1.0);
            if (selected) selectedRms = rms;
            if (peak > 1.0e-7) {
                NSBezierPath* waveform = [NSBezierPath bezierPath];
                const CGFloat deviation = 1.2 + level * 8.8;
                for (uint32_t sample = 0u;
                     sample < kRouteScopeSamples; ++sample) {
                    const CGFloat t = (static_cast<CGFloat>(sample) + 0.5)
                        / static_cast<CGFloat>(kRouteScopeSamples);
                    const NSPoint center = cubicPoint(a, c1, c2, b, t);
                    const NSPoint tangent = cubicTangent(
                        a, c1, c2, b, t);
                    const CGFloat length = std::max<CGFloat>(1.0,
                        std::hypot(tangent.x, tangent.y));
                    const CGFloat normalized = std::clamp<CGFloat>(
                        samples[sample] / peak, -1.0, 1.0);
                    const NSPoint signal = NSMakePoint(
                        center.x - tangent.y / length
                            * normalized * deviation,
                        center.y + tangent.x / length
                            * normalized * deviation);
                    if (sample == 0u) [waveform moveToPoint:signal];
                    else [waveform lineToPoint:signal];
                }
                [waveform setLineJoinStyle:NSLineJoinStyleRound];
                [waveform setLineCapStyle:NSLineCapStyleRound];
                [waveform setLineWidth:2.8 + level * 1.8];
                [mixerColor(0x050505, 0.80) setStroke];
                [waveform stroke];
                [waveform setLineWidth:0.9 + level * 1.5];
                [mixerColor(stored >= 0.0f ? 0xff9a73 : 0x9cf8fb,
                    0.24 + level * 0.76) setStroke];
                [waveform stroke];
            }
        }
    }

    if (_wireDragSource >= 0) {
        const NSPoint a = wiringPortPoint(false,
            static_cast<uint32_t>(_wireDragSource));
        const NSPoint b = _wireDragPoint;
        NSBezierPath* cable = [NSBezierPath bezierPath];
        [cable moveToPoint:a];
        [cable curveToPoint:b
            controlPoint1:NSMakePoint(a.x + 180.0, a.y)
            controlPoint2:NSMakePoint(b.x - 180.0, b.y)];
        [cable setLineWidth:2.0];
        [mixerColor(0xb8b8b8, 0.72) setStroke];
        [cable stroke];
    }

    for (uint32_t lane = 0u; lane < kChannelCount; ++lane) {
        const NSPoint source = wiringPortPoint(false, lane);
        const NSPoint destination = wiringPortPoint(true, lane);
        const CGFloat sourceActivity = std::clamp<CGFloat>(
            plugin->laneActivity[lane].load(std::memory_order_relaxed),
            0.0, 1.0);
        const NSRect sourcePort = NSMakeRect(source.x - 12.0,
            source.y - 12.0, 24.0, 24.0);
        const NSRect destinationPort = NSMakeRect(destination.x - 12.0,
            destination.y - 12.0, 24.0, 24.0);
        [mixerColor(0x202020) setFill];
        NSRectFill(sourcePort); NSRectFill(destinationPort);
        [mixerColor(0x666666) setStroke];
        NSFrameRect(sourcePort); NSFrameRect(destinationPort);
        [mixerColor(0x57bfc4, 0.28 + sourceActivity * 0.72) setFill];
        NSRectFill(NSInsetRect(sourcePort, 6.0, 6.0));
        NSRectFill(NSInsetRect(destinationPort, 6.0, 6.0));
        [[NSString stringWithFormat:@"S%u", lane + 1u]
            drawAtPoint:NSMakePoint(source.x - 39.0, source.y - 7.0)
            withAttributes:valueAttrs];
        [[NSString stringWithFormat:@"D%u", lane + 1u]
            drawAtPoint:NSMakePoint(destination.x + 17.0,
                destination.y - 7.0) withAttributes:valueAttrs];
    }
    const CGFloat selectedDb = 20.0 * std::log10(
        std::max<CGFloat>(selectedRms, 1.0e-6));
    [[NSString stringWithFormat:
        @"ROUTED AUDIO  S%u > D%u  %+.1f dBFS  ·  24 kHz SCOPE",
        selectedSource + 1u, selectedDestination + 1u, selectedDb]
        drawAtPoint:NSMakePoint(rect.origin.x + 18.0,
            NSMaxY(rect) - 48.0) withAttributes:label];
    [@"DRAG SOURCE TO DESTINATION · OPTION: NEGATIVE · CLICK WIRE: SELECT / DISSOLVE"
        drawAtPoint:NSMakePoint(rect.origin.x + 18.0,
            NSMaxY(rect) - 25.0) withAttributes:valueAttrs];
}

- (void)drawPrimaryMatrix:(Plugin*)plugin rect:(NSRect)rect
    label:(NSDictionary*)label valueAttrs:(NSDictionary*)valueAttrs
{
    [mixerColor(0x101010) setFill];
    NSRectFill(rect);
    [mixerColor(0x454545) setStroke];
    NSFrameRect(rect);
    const CGFloat gridLeft = rect.origin.x + 54.0;
    const CGFloat gridTop = rect.origin.y + 36.0;
    const CGFloat spacing = 58.0;
    const CGFloat gridExtent = spacing * 7.0;
    const uint32_t selectedSource = plugin->selectedSource.load(
        std::memory_order_relaxed);
    const uint32_t selectedDestination = plugin->selectedDestination.load(
        std::memory_order_relaxed);
    const auto motionWeights = s3g::noInputMixerMotionWeights(
        plugin->params, plugin->motionPhase.load(std::memory_order_relaxed));
    std::array<float, kChannelCount> activeMotionPeak {};
    std::array<uint32_t, kChannelCount> activeMotionRouteCount {};
    for (uint32_t destination = 0u; destination < kChannelCount;
         ++destination) {
        for (uint32_t source = 0u; source < kChannelCount; ++source) {
            const uint32_t index = destination * kChannelCount + source;
            if (std::abs(plugin->params.matrix[index]) <= 0.001f) continue;
            activeMotionPeak[source] = std::max(
                activeMotionPeak[source], motionWeights[index]);
            ++activeMotionRouteCount[source];
        }
    }

    [mixerColor(0x343434) setStroke];
    for (uint32_t index = 0u; index < kChannelCount; ++index) {
        const CGFloat x = gridLeft + spacing * index;
        const CGFloat y = gridTop + spacing * index;
        [NSBezierPath strokeLineFromPoint:NSMakePoint(x, gridTop)
            toPoint:NSMakePoint(x, gridTop + gridExtent)];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(gridLeft, y)
            toPoint:NSMakePoint(gridLeft + gridExtent, y)];
        [[NSString stringWithFormat:@"S%u", index + 1u]
            drawAtPoint:NSMakePoint(x - 7.0, gridTop - 24.0)
            withAttributes:valueAttrs];
        [[NSString stringWithFormat:@"D%u", index + 1u]
            drawAtPoint:NSMakePoint(gridLeft - 34.0, y - 6.0)
            withAttributes:valueAttrs];
    }

    for (uint32_t destination = 0u; destination < kChannelCount;
         ++destination) {
        for (uint32_t source = 0u; source < kChannelCount; ++source) {
            const float gain = plugin->params.matrix[
                destination * kChannelCount + source];
            const CGFloat x = gridLeft + spacing * source;
            const CGFloat y = gridTop + spacing * destination;
            const NSRect node = NSMakeRect(x - 7.0, y - 7.0, 14.0, 14.0);
            [mixerColor(0x141414) setFill];
            NSRectFill(node);
            if (std::abs(gain) > 0.001f) {
                const uint32_t index =
                    destination * kChannelCount + source;
                const CGFloat motion = s3g::noInputMixerMotionGainScale(
                    motionWeights[index], activeMotionPeak[source],
                    activeMotionRouteCount[source], plugin->params.motion);
                [mixerColor(gain >= 0.0f ? 0xc95e3b : 0x5daeb6,
                    0.18 + std::abs(gain) * 0.40) setFill];
                NSRectFill(NSInsetRect(node, 1.0, 1.0));
                const CGFloat effectiveGain = std::abs(gain) * motion;
                const CGFloat liveSize = 2.0 + 10.0 * effectiveGain;
                [mixerColor(gain >= 0.0f ? 0xff7047 : 0x69d2dc,
                    0.40 + motion * 0.55) setFill];
                NSRectFill(NSMakeRect(x - liveSize * 0.5,
                    y - liveSize * 0.5, liveSize, liveSize));
            }
            [mixerColor((source == selectedSource
                && destination == selectedDestination)
                    ? 0xc8c8c8 : 0x5a5a5a) setStroke];
            NSFrameRect(node);
        }
    }

    const CGFloat meterTop = gridTop + gridExtent + 38.0;
    const CGFloat meterHeight = 88.0;
    [@"OUTPUT PEAK · dBFS" drawAtPoint:NSMakePoint(
        rect.origin.x + 8.0, meterTop - 22.0) withAttributes:label];
    constexpr CGFloat meterFloorDb = -60.0;
    for (CGFloat tickDb : { -60.0, -30.0, 0.0 }) {
        const CGFloat normalized = (tickDb - meterFloorDb)
            / -meterFloorDb;
        const CGFloat y = meterTop + meterHeight * (1.0 - normalized);
        [mixerColor(0x353535, tickDb == -30.0 ? 0.58 : 0.34) setStroke];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(gridLeft - 9.0, y)
            toPoint:NSMakePoint(gridLeft + gridExtent + 9.0, y)];
        [[NSString stringWithFormat:@"%.0f", tickDb]
            drawAtPoint:NSMakePoint(rect.origin.x + 16.0, y - 6.0)
            withAttributes:valueAttrs];
    }
    for (uint32_t lane = 0u; lane < kChannelCount; ++lane) {
        const CGFloat x = gridLeft + spacing * lane;
        NSRect track = NSMakeRect(x - 7.0, meterTop, 14.0, meterHeight);
        [mixerColor(0x070707) setFill]; NSRectFill(track);
        [mixerColor(0x3e3e3e) setStroke]; NSFrameRect(track);
        const CGFloat peak = std::max<CGFloat>(1.0e-6,
            plugin->lanePeaks[lane].load(std::memory_order_relaxed));
        const CGFloat peakDb = std::clamp<CGFloat>(
            20.0 * std::log10(peak), meterFloorDb, 0.0);
        const CGFloat normalized = (peakDb - meterFloorDb)
            / -meterFloorDb;
        NSRect fill = NSInsetRect(track, 2.0, 2.0);
        fill.origin.y += fill.size.height * (1.0 - normalized);
        fill.size.height *= normalized;
        [mixerColor(0x57bfc4, 0.85) setFill]; NSRectFill(fill);
        NSString* reading = peakDb <= meterFloorDb + 0.1
            ? @"−∞" : [NSString stringWithFormat:@"%.0f", peakDb];
        const NSSize readingSize = [reading sizeWithAttributes:valueAttrs];
        [reading drawAtPoint:NSMakePoint(x - readingSize.width * 0.5,
            NSMaxY(track) + 4.0) withAttributes:valueAttrs];
    }
    const NSRect motionRail = NSMakeRect(rect.origin.x + 18.0,
        rect.origin.y + 598.0, rect.size.width - 36.0, 38.0);
    [mixerColor(0x0a0a0a) setFill]; NSRectFill(motionRail);
    [mixerColor(0x454545) setStroke]; NSFrameRect(motionRail);
    const float rateHz = s3g::noInputMixerMotionRateHz(
        plugin->params.motionRate);
    [[NSString stringWithFormat:
        @"ROUTE GAIN MOD · %@ · DEPTH %.0f%% · %.2f Hz",
        [NSString stringWithUTF8String:s3g::matrixFlowShapeName(
            plugin->params.motionShape)],
        plugin->params.motion * 100.0f, rateHz]
        drawAtPoint:NSMakePoint(motionRail.origin.x + 8.0,
            motionRail.origin.y + 4.0) withAttributes:valueAttrs];
    const CGFloat baseline = NSMaxY(motionRail) - 5.0;
    for (uint32_t source = 0u; source < kChannelCount; ++source) {
        const CGFloat columnX = gridLeft + spacing * source;
        for (uint32_t destination = 0u;
             destination < kChannelCount; ++destination) {
            const uint32_t index = destination * kChannelCount + source;
            const float gain = plugin->params.matrix[index];
            if (std::abs(gain) <= 0.001f) continue;
            const CGFloat modulation = s3g::noInputMixerMotionGainScale(
                motionWeights[index], activeMotionPeak[source],
                activeMotionRouteCount[source], plugin->params.motion);
            const CGFloat effective = std::abs(gain) * modulation;
            const CGFloat height = 2.0 + effective * 9.0;
            const CGFloat x = columnX - 16.0
                + static_cast<CGFloat>(destination) * 4.5;
            [mixerColor(gain >= 0.0f ? 0xc95e3b : 0x57bfc4,
                0.30 + modulation * 0.65) setFill];
            NSRectFill(NSMakeRect(x, baseline - height, 3.0, height));
        }
    }
    [@"CLICK: PATCH / SELECT · CLICK AGAIN: DISSOLVE · OPTION: NEGATIVE"
        drawAtPoint:NSMakePoint(rect.origin.x + 14.0,
            NSMaxY(rect) - 19.0) withAttributes:valueAttrs];
}


- (void)drawPrimaryLanes:(Plugin*)plugin rect:(NSRect)rect
    label:(NSDictionary*)label valueAttrs:(NSDictionary*)valueAttrs
{
    [mixerColor(0x101010) setFill]; NSRectFill(rect);
    [mixerColor(0x454545) setStroke]; NSFrameRect(rect);
    const uint32_t selected = plugin->selectedLane.load(
        std::memory_order_relaxed);
    const CGFloat gap = 12.0;
    const CGFloat cellWidth = (rect.size.width - gap * 5.0) / 4.0;
    const CGFloat cellHeight = (rect.size.height - gap * 3.0) / 2.0;
    for (uint32_t lane = 0u; lane < kChannelCount; ++lane) {
        const uint32_t column = lane % 4u;
        const uint32_t row = lane / 4u;
        NSRect cell = NSMakeRect(rect.origin.x + gap
                + column * (cellWidth + gap),
            rect.origin.y + gap + row * (cellHeight + gap),
            cellWidth, cellHeight);
        [mixerColor(lane == selected ? 0x242424 : 0x171717) setFill];
        NSRectFill(cell);
        [mixerColor(lane == selected ? 0xb8b8b8 : 0x494949) setStroke];
        NSFrameRect(cell);
        [[NSString stringWithFormat:@"L%u", lane + 1u]
            drawAtPoint:NSMakePoint(cell.origin.x + 10.0,
                cell.origin.y + 9.0) withAttributes:label];
        const auto& laneParams = plugin->params.lanes[lane];
        [[NSString stringWithFormat:@"BODY %.0f  LOSS %.0f",
            laneParams.body * 100.0f, laneParams.loss * 100.0f]
            drawAtPoint:NSMakePoint(cell.origin.x + 10.0,
                cell.origin.y + 35.0) withAttributes:valueAttrs];
        [[NSString stringWithFormat:@"EQ %+.0f / %+.0f / %+.0f",
            laneParams.lowDb, laneParams.midGainDb, laneParams.highDb]
            drawAtPoint:NSMakePoint(cell.origin.x + 10.0,
                cell.origin.y + 57.0) withAttributes:valueAttrs];
        for (uint32_t slot = 0u; slot < s3g::kNoInputMixerInsertSlots;
             ++slot) {
            NSString* text = [NSString stringWithFormat:@"%u %@%@",
                slot + 1u,
                [NSString stringWithUTF8String:s3g::noInputDistortionName(
                    laneParams.inserts[slot].type)],
                laneParams.inserts[slot].bypass != 0u ? @" BYP" : @""];
            [text drawAtPoint:NSMakePoint(cell.origin.x + 10.0,
                cell.origin.y + 91.0 + slot * 22.0)
                withAttributes:valueAttrs];
        }
        NSRect activity = NSMakeRect(cell.origin.x + 10.0,
            NSMaxY(cell) - 25.0, cell.size.width - 20.0, 8.0);
        [mixerColor(0x090909) setFill]; NSRectFill(activity);
        [mixerColor(0x404040) setStroke]; NSFrameRect(activity);
        NSRect activityFill = NSInsetRect(activity, 1.0, 1.0);
        activityFill.size.width *= std::clamp<CGFloat>(
            plugin->laneActivity[lane].load(std::memory_order_relaxed),
            0.0, 1.0);
        [mixerColor(0x57bfc4, 0.8) setFill]; NSRectFill(activityFill);
    }
}


- (NSRect)menuAnchorRect:(int)menu
{
    const auto& family = s3g::gui_layout::kNoInputMixerFamilyLayout;
    switch (menu) {
    case kMenuPreset:
        return s3g::clap_gui::cocoaRect(
            s3g::gui_layout::matrixTitleBand(family.canvas).presetMenu);
    case kMenuLane: return processorMenuRect(family.selectedLane, 0u);
    case kMenuSource: return processorMenuRect(family.crosspoint, 0u);
    case kMenuDestination: return processorMenuRect(family.crosspoint, 1u);
    case kMenuSlot0: return processorMenuRect(family.inserts, 0u);
    case kMenuSlot1: return processorMenuRect(family.inserts, 1u);
    case kMenuSlot2: return processorMenuRect(family.inserts, 2u);
    case kMenuMotionShape:
        return processorMenuRect(family.movement, 0u);
    case kMenuQuality:
        return NSMakeRect(family.containment.frame.x + 108.0,
            family.containment.frame.y + 35.0, 110.0, 15.0);
    default: return NSZeroRect;
    }
}

- (uint32_t)menuItemCount:(int)menu
{
    if (menu == kMenuPreset) return s3g::kNoInputMixerFactoryPresetCount;
    if (menu == kMenuSlot0 || menu == kMenuSlot1 || menu == kMenuSlot2) {
        return s3g::kNoInputDistortionTypeCount;
    }
    if (menu == kMenuMotionShape) return 6u;
    if (menu == kMenuQuality) return 3u;
    if (menu == kMenuLane || menu == kMenuSource
        || menu == kMenuDestination) return kChannelCount;
    return 0u;
}

- (NSRect)menuDropdownRect:(int)menu
{
    const NSRect anchor = [self menuAnchorRect:menu];
    return NSMakeRect(anchor.origin.x, NSMaxY(anchor) + 2.0,
        anchor.size.width,
        18.0 * static_cast<CGFloat>([self menuItemCount:menu]));
}

- (void)drawOpenMenu:(Plugin*)plugin attrs:(NSDictionary*)attrs
    style:(const s3g::clap_gui::Style&)style
{
    if (_openMenu == kMenuNone) return;
    NSString* laneItems[kChannelCount] = {
        @"L1", @"L2", @"L3", @"L4", @"L5", @"L6", @"L7", @"L8",
    };
    NSString* typeItems[s3g::kNoInputDistortionTypeCount] = {
        @"BYPASS", @"WOOL", @"RAT", @"ZONE A", @"ZONE B",
        @"FUZZ I", @"FUZZ II", @"DIODE", @"RING",
    };
    NSString* qualityItems[3] = { @"1X", @"2X", @"4X" };
    NSString* shapeItems[6] = {
        @"FLOW", @"PULSE", @"CHASE", @"SWIRL", @"SCAT", @"HOLD",
    };
    NSString* presetItems[s3g::kNoInputMixerFactoryPresetCount] = {
        @"INIT", @"CIRCUIT LATTICE", @"RAIN FOREST", @"WOOL RING",
        @"RAT CAGE", @"ZONE WEB", @"NEGATIVE SPACE", @"RELAY BLOOM",
        @"OPEN HOUSE", @"MOBILE CIRCUIT",
    };
    NSString** items = laneItems;
    uint32_t count = kChannelCount;
    int selected = 0;
    if (_openMenu == kMenuPreset) {
        items = presetItems;
        count = s3g::kNoInputMixerFactoryPresetCount;
        selected = -1;
        for (uint32_t index = 0u; index < count; ++index) {
            if (std::strcmp(_titlePresetName,
                    s3g::noInputMixerFactoryPresetName(index)) == 0) {
                selected = static_cast<int>(index);
                break;
            }
        }
    } else if (_openMenu == kMenuLane) {
        selected = static_cast<int>(plugin->selectedLane.load(
            std::memory_order_relaxed));
    } else if (_openMenu == kMenuSource) {
        selected = static_cast<int>(plugin->selectedSource.load(
            std::memory_order_relaxed));
    } else if (_openMenu == kMenuDestination) {
        selected = static_cast<int>(plugin->selectedDestination.load(
            std::memory_order_relaxed));
    } else if (_openMenu == kMenuQuality) {
        items = qualityItems;
        count = 3u;
        selected = static_cast<int>(plugin->params.quality);
    } else if (_openMenu == kMenuMotionShape) {
        items = shapeItems;
        count = 6u;
        selected = static_cast<int>(plugin->params.motionShape);
    } else {
        items = typeItems;
        count = s3g::kNoInputDistortionTypeCount;
        const uint32_t lane = plugin->selectedLane.load(
            std::memory_order_relaxed);
        const uint32_t slot = static_cast<uint32_t>(
            _openMenu - kMenuSlot0);
        selected = static_cast<int>(plugin->params.lanes[lane]
            .inserts[slot].type);
    }
    s3g::clap_gui::drawDropdownMenu([self menuDropdownRect:_openMenu],
        18.0, items, count, selected, -1, attrs, style);
}

- (void)drawPerformanceMixer:(Plugin*)plugin surface:(NSRect)surface
{
    [NSGraphicsContext saveGraphicsState];
    const NSPoint offset = mixerSurfaceOffset(surface);
    NSAffineTransform* transform = [NSAffineTransform transform];
    [transform translateXBy:offset.x yBy:offset.y];
    [transform concat];
    s3g::clap_gui::Style style;
    NSDictionary* title = s3g::clap_gui::softTitleAttrs();
    NSDictionary* label = s3g::clap_gui::softLabelAttrs();
    NSDictionary* value = s3g::clap_gui::softValueAttrs();

    const uint32_t selected = plugin->selectedLane.load(
        std::memory_order_relaxed);
    const auto drawHorizontal = [&](NSString* name, NSRect track,
        CGFloat norm, int rgb) {
        [name drawAtPoint:NSMakePoint(track.origin.x,
            track.origin.y - 17.0) withAttributes:value];
        [mixerColor(0x080808) setFill]; NSRectFill(track);
        [mixerColor(0x454545) setStroke]; NSFrameRect(track);
        NSRect fill = NSInsetRect(track, 1.0, 1.0);
        fill.size.width *= std::clamp<CGFloat>(norm, 0.0, 1.0);
        [mixerColor(rgb, 0.88) setFill]; NSRectFill(fill);
    };
    const auto drawVertical = [&](NSRect track, CGFloat norm, int rgb) {
        [mixerColor(0x080808) setFill]; NSRectFill(track);
        [mixerColor(0x454545) setStroke]; NSFrameRect(track);
        const CGFloat y = NSMaxY(track)
            - std::clamp<CGFloat>(norm, 0.0, 1.0) * track.size.height;
        [mixerColor(rgb) setFill];
        NSRectFill(NSMakeRect(track.origin.x - 4.0, y - 2.5,
            track.size.width + 8.0, 5.0));
    };

    for (uint32_t lane = 0u; lane < kChannelCount; ++lane) {
        const NSRect strip = popupStripRect(lane);
        [mixerColor(lane == selected ? 0x222222 : 0x151515) setFill];
        NSRectFill(strip);
        [mixerColor(lane == selected ? 0xb8b8b8 : 0x494949) setStroke];
        NSFrameRect(strip);
        [[NSString stringWithFormat:@"LANE %u", lane + 1u]
            drawAtPoint:NSMakePoint(strip.origin.x + 10.0,
                strip.origin.y + 10.0) withAttributes:label];
        const auto& laneParams = plugin->params.lanes[lane];
        const float loop = plugin->params.matrix[lane * 8u + lane];
        drawHorizontal(@"BODY", popupBodyRect(strip), laneParams.body,
            0x929292);
        drawHorizontal(@"LOSS", popupLossRect(strip), laneParams.loss,
            0x747474);
        [@"LOOP" drawAtPoint:NSMakePoint(strip.origin.x + 10.0,
            strip.origin.y + 112.0) withAttributes:label];
        const NSRect loopTrack = popupLoopRect(strip);
        [mixerColor(0x080808) setFill]; NSRectFill(loopTrack);
        [mixerColor(0x454545) setStroke]; NSFrameRect(loopTrack);
        NSRect loopFill = NSInsetRect(loopTrack, 2.0, 2.0);
        loopFill.origin.y += loopFill.size.height * (1.0 - std::abs(loop));
        loopFill.size.height *= std::abs(loop);
        [mixerColor(loop >= 0.0f ? 0xc95e3b : 0x57bfc4, 0.88) setFill];
        NSRectFill(loopFill);
        [[NSString stringWithFormat:@"%+.2f", loop]
            drawAtPoint:NSMakePoint(strip.origin.x + 10.0,
                strip.origin.y + 248.0) withAttributes:value];
        [@"EQ   L   M   H" drawAtPoint:NSMakePoint(strip.origin.x + 9.0,
            strip.origin.y + 268.0) withAttributes:label];
        const float eq[3] = {
            laneParams.lowDb, laneParams.midGainDb, laneParams.highDb,
        };
        for (uint32_t band = 0u; band < 3u; ++band) {
            drawVertical(popupEqRect(strip, band),
                (eq[band] + 18.0f) / 36.0f, 0xb8b8b8);
        }
        drawHorizontal(@"AUX A", popupSendRect(strip, 0u),
            laneParams.auxSend[0], 0xc95e3b);
        drawHorizontal(@"AUX B", popupSendRect(strip, 1u),
            laneParams.auxSend[1], 0x57bfc4);
        for (uint32_t slot = 0u;
             slot < s3g::kNoInputMixerInsertSlots; ++slot) {
            const auto& insert = laneParams.inserts[slot];
            NSString* name = [NSString stringWithUTF8String:
                s3g::noInputDistortionName(insert.type)];
            if ([name length] > 8u) name = [name substringToIndex:8u];
            drawFlatButton(popupInsertRect(strip, slot),
                [NSString stringWithFormat:@"%u %@", slot + 1u, name],
                plugin->selectedLane.load(std::memory_order_relaxed) == lane
                    && plugin->selectedSlot.load(std::memory_order_relaxed)
                        == slot,
                value);
        }
        [@"FADER" drawAtPoint:NSMakePoint(strip.origin.x + 9.0,
            strip.origin.y + 564.0) withAttributes:label];
        drawVertical(popupFaderRect(strip),
            (laneParams.levelDb + 60.0f) / 72.0f, 0xb8b8b8);
        [[NSString stringWithFormat:@"%+.1f dB", laneParams.levelDb]
            drawAtPoint:NSMakePoint(strip.origin.x + 9.0,
                strip.origin.y + 658.0) withAttributes:value];
        drawFlatButton(popupMuteRect(strip), @"MUTE",
            laneParams.mute != 0u, value);
    }

    const NSRect auxPanel = popupAuxPanelRect();
    [mixerColor(0x151515) setFill]; NSRectFill(auxPanel);
    [mixerColor(0x595959) setStroke]; NSFrameRect(auxPanel);
    [@"AUX RETURNS / MASTER TONE" drawAtPoint:NSMakePoint(
        auxPanel.origin.x + 14.0, auxPanel.origin.y + 14.0)
        withAttributes:label];
    const NSString* auxLabels[4] = { @"GAIN", @"TONE", @"RETURN", @"LOOP" };
    for (uint32_t bus = 0u; bus < 2u; ++bus) {
        const CGFloat baseY = auxPanel.origin.y + 48.0 + bus * 248.0;
        [[NSString stringWithFormat:@"AUX %c", 'A' + bus]
            drawAtPoint:NSMakePoint(auxPanel.origin.x + 14.0, baseY)
            withAttributes:title];
        const auto& aux = plugin->params.aux[bus];
        drawFlatButton(popupAuxTypeRect(bus),
            [NSString stringWithUTF8String:
                s3g::noInputDistortionName(aux.effect.type)], true, value);
        const CGFloat norms[4] = {
            aux.effect.gain, aux.effect.tone, aux.returnGain,
            aux.feedback / 0.96f,
        };
        for (uint32_t local = 0u; local < 4u; ++local) {
            const NSRect track = popupAuxSliderRect(bus, local);
            [auxLabels[local] drawAtPoint:NSMakePoint(
                auxPanel.origin.x + 14.0, track.origin.y - 4.0)
                withAttributes:value];
            drawHorizontal(@"", track, norms[local],
                bus == 0u ? 0xc95e3b : 0x57bfc4);
        }
        const NSRect activity = NSMakeRect(auxPanel.origin.x + 14.0,
            baseY + 206.0, auxPanel.size.width - 28.0, 8.0);
        [mixerColor(0x080808) setFill]; NSRectFill(activity);
        NSRect active = NSInsetRect(activity, 1.0, 1.0);
        active.size.width *= std::clamp<CGFloat>(
            plugin->auxActivity[bus].load(std::memory_order_relaxed),
            0.0, 1.0);
        [mixerColor(bus == 0u ? 0xc95e3b : 0x57bfc4) setFill];
        NSRectFill(active);
    }
    [@"MASTER TONE" drawAtPoint:NSMakePoint(auxPanel.origin.x + 14.0,
        auxPanel.origin.y + 554.0) withAttributes:title];
    const NSString* toneLabels[2] = { @"INTERNAL", @"HOUSE" };
    const CGFloat toneNorms[2] = {
        (plugin->params.internalTone + 1.0f) * 0.5f,
        (plugin->params.houseTone + 1.0f) * 0.5f,
    };
    for (uint32_t row = 0u; row < 2u; ++row) {
        const NSRect track = popupToneRect(row);
        [toneLabels[row] drawAtPoint:NSMakePoint(auxPanel.origin.x + 14.0,
            track.origin.y - 4.0) withAttributes:value];
        drawHorizontal(@"", track, toneNorms[row], 0x929292);
    }
    [@"CLICK PROCESSOR NAME TO CYCLE · DRAG ALL TRACKS"
        drawAtPoint:NSMakePoint(auxPanel.origin.x + 14.0,
            NSMaxY(auxPanel) - 24.0) withAttributes:value];
    [NSGraphicsContext restoreGraphicsState];
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* plugin = static_cast<Plugin*>(_plugin);
    if (!plugin) return;
    const auto& family = s3g::gui_layout::kNoInputMixerFamilyLayout;
    s3g::clap_gui::Style style;
    [style.bg setFill]; NSRectFill([self bounds]);
    NSDictionary* title = s3g::clap_gui::softTitleAttrs();
    NSDictionary* label = s3g::clap_gui::softLabelAttrs();
    NSDictionary* value = s3g::clap_gui::softValueAttrs();
    const auto titleBand = s3g::gui_layout::matrixTitleBand(family.canvas);
    s3g::clap_gui::drawProcessorTitleBand(
        @"s3g PROCESSOR NO INPUT MIXER 8CH",
        [NSString stringWithUTF8String:(_mixerPopupChild
            && _mixerPopupOwner
                ? _mixerPopupOwner->_titlePresetName : _titlePresetName)],
        s3g::clap_gui::peakDbText(plugin->outputPeak.load(
            std::memory_order_relaxed)), titleBand, title, label, value,
        style);

    s3g::clap_gui::drawPanelFrame(family.fieldPanel.x,
        family.fieldPanel.y, family.fieldPanel.width,
        family.fieldPanel.height, style);
    const uint32_t page = _lockedPage < 4u ? _lockedPage
        : plugin->guiPage.load(std::memory_order_relaxed);
    static NSString* pageNames[4] = {
        @"PATCH", @"MIXER", @"CHANNEL", @"SAFETY",
    };
    s3g::clap_gui::drawPanelHeader(pageNames[page], true,
        family.fieldPanel.x, family.fieldPanel.y, family.fieldPanel.width,
        s3g::gui_layout::kStandardMetrics.headerHeight, label, style);
    for (uint32_t index = 0u; index < 4u; ++index) {
        s3g::clap_gui::drawHeaderButton(fieldTabRect(index),
            s3g::clap_gui::cocoaRect(family.fieldPanel), pageNames[index],
            page == index, value, style);
    }
    s3g::clap_gui::drawHeaderButton(mixerPopButtonRect(),
        s3g::clap_gui::cocoaRect(family.fieldPanel),
        _mixerPopupChild ? @"DOCK" : @"POP",
        _mixerPopupChild || (_pagePanels[page]
            && [_pagePanels[page] isVisible]), value, style);
    const NSRect fieldPlot = s3g::clap_gui::cocoaRect(family.fieldPlot);
    if (page == 0u) [self drawPrimaryWiring:plugin rect:patchVisualRect()
        label:label valueAttrs:value];
    else if (page == 1u) [self drawPerformanceMixer:plugin
        surface:fieldPlot];
    else if (page == 2u) [self drawPrimaryLanes:plugin
        rect:channelOverviewRect() label:label valueAttrs:value];
    else {
        [mixerColor(0x101010) setFill]; NSRectFill(fieldPlot);
        [mixerColor(0x454545) setStroke]; NSFrameRect(fieldPlot);
    }

    const auto drawPanel = [&](NSString* name,
        const s3g::gui_layout::Panel& panel) {
        s3g::clap_gui::drawPanelFrame(panel, style);
        s3g::clap_gui::drawPanelHeader(name, true, panel, label, style);
    };
    const uint32_t lane = plugin->selectedLane.load(
        std::memory_order_relaxed);
    const uint32_t slot = plugin->selectedSlot.load(
        std::memory_order_relaxed);
    const auto& laneParams = plugin->params.lanes[lane];
    if (page == 0u) {
        drawPanel(@"NETWORK", family.network);
        drawPanel(@"MATRIX MOVEMENT — ROUTE GAIN", family.movement);
        drawPanel(@"CROSSPOINT", family.crosspoint);
    } else if (page == 2u) {
        drawPanel(@"SELECTED LANE", family.selectedLane);
        drawPanel([NSString stringWithFormat:@"EQ — L%u", lane + 1u],
            family.eq);
        drawPanel([NSString stringWithFormat:@"INSERTS — L%u / S%u",
            lane + 1u, slot + 1u], family.inserts);
    } else if (page == 3u) {
        drawPanel(@"OUTPUT", family.output);
        drawPanel(@"CONTAINMENT", family.containment);
    }

    if (page == 3u) {
    [self drawSlider:@"OUT"
        value:[NSString stringWithFormat:@"%+.1f dB",
            plugin->params.outputGainDb]
        norm:(plugin->params.outputGainDb + 60.0f) / 66.0f row:0u
        panel:family.output label:label valueAttrs:value style:style];
    [self drawSlider:@"CEIL"
        value:[NSString stringWithFormat:@"%+.1f dB",
            plugin->params.ceilingDb]
        norm:(plugin->params.ceilingDb + 18.0f) / 18.0f row:1u
        panel:family.output label:label valueAttrs:value style:style];
    s3g::clap_gui::drawToggle(@"LIMIT",
        plugin->params.limiterEnabled != 0u,
        s3g::gui_layout::rowY(family.output, 2u), label, value, style,
        s3g::gui_layout::processorLabelX(family.output.frame.x),
        s3g::gui_layout::processorControlX(family.output.frame.x), 82.0);
    s3g::clap_gui::drawToggle(@"DC BLOCK",
        plugin->params.dcBlockEnabled != 0u,
        s3g::gui_layout::rowY(family.output, 3u), label, value, style,
        s3g::gui_layout::processorLabelX(family.output.frame.x),
        s3g::gui_layout::processorControlX(family.output.frame.x), 82.0);
    }

    if (page == 0u) {
    [@"SEED" drawAtPoint:NSMakePoint(
        s3g::gui_layout::processorLabelX(family.network.frame.x),
        s3g::gui_layout::rowY(family.network, 0u) - 2.0)
        withAttributes:label];
    drawFlatButton(seedNewButtonRect(), @"NEW", false, value);
    drawFlatButton(randomButtonRect(), @"RANDOM", false, value);
    drawFlatButton(forgetButtonRect(), @"FORGET", false, value);
    [self drawSlider:@"FDBK"
        value:[NSString stringWithFormat:@"%.0f%%",
            plugin->params.feedback * 100.0f]
        norm:plugin->params.feedback / 1.25f row:1u panel:family.network
        label:label valueAttrs:value style:style];
    [self drawSlider:@"COUPL"
        value:[NSString stringWithFormat:@"%.0f%%",
            plugin->params.coupling * 100.0f]
        norm:plugin->params.coupling / 1.25f row:2u panel:family.network
        label:label valueAttrs:value style:style];
    [self drawSlider:@"PHASE"
        value:[NSString stringWithFormat:@"%.0f%%",
            plugin->params.phase * 100.0f]
        norm:plugin->params.phase row:3u panel:family.network label:label
        valueAttrs:value style:style];
    [self drawSlider:@"DRIFT"
        value:[NSString stringWithFormat:@"%.0f%%",
            plugin->params.drift * 100.0f]
        norm:plugin->params.drift row:4u panel:family.network label:label
        valueAttrs:value style:style];
    [self drawSlider:@"FORMANT"
        value:[NSString stringWithFormat:@"%.0f%%",
            plugin->params.formant * 100.0f]
        norm:plugin->params.formant row:5u panel:family.network label:label
        valueAttrs:value style:style];
    [self drawSlider:@"AGENCY"
        value:[NSString stringWithFormat:@"%.0f%%",
            plugin->params.agency * 100.0f]
        norm:plugin->params.agency row:6u panel:family.network label:label
        valueAttrs:value style:style];
    [self drawSlider:@"SPACE"
        value:[NSString stringWithFormat:@"%.0f%%",
            plugin->params.space * 100.0f]
        norm:plugin->params.space row:7u panel:family.network label:label
        valueAttrs:value style:style];
    [self drawSlider:@"VARIANCE"
        value:[NSString stringWithFormat:@"%.0f%%",
            plugin->params.variance * 100.0f]
        norm:plugin->params.variance row:8u panel:family.network label:label
        valueAttrs:value style:style];
    }

    if (page == 2u) {
    s3g::clap_gui::drawProcessorMenu(@"LANE",
        [NSString stringWithFormat:@"L%u", lane + 1u],
        s3g::gui_layout::rowY(family.selectedLane, 0u),
        family.selectedLane.frame.x, family.selectedLane.frame.width,
        label, value, style);
    [self drawSlider:@"BODY"
        value:[NSString stringWithFormat:@"%.0f%%", laneParams.body * 100.0f]
        norm:laneParams.body row:1u panel:family.selectedLane label:label
        valueAttrs:value style:style];
    [self drawSlider:@"LOSS"
        value:[NSString stringWithFormat:@"%.0f%%", laneParams.loss * 100.0f]
        norm:laneParams.loss row:2u panel:family.selectedLane label:label
        valueAttrs:value style:style];
    [self drawSlider:@"LEVEL"
        value:[NSString stringWithFormat:@"%+.1f dB", laneParams.levelDb]
        norm:(laneParams.levelDb + 60.0f) / 72.0f row:3u
        panel:family.selectedLane label:label valueAttrs:value style:style];
    [self drawSlider:@"AUX A"
        value:[NSString stringWithFormat:@"%.0f%%",
            laneParams.auxSend[0] * 100.0f]
        norm:laneParams.auxSend[0] row:4u panel:family.selectedLane
        label:label valueAttrs:value style:style];
    [self drawSlider:@"AUX B"
        value:[NSString stringWithFormat:@"%.0f%%",
            laneParams.auxSend[1] * 100.0f]
        norm:laneParams.auxSend[1] row:5u panel:family.selectedLane
        label:label valueAttrs:value style:style];
    const CGFloat selectedButtonY = s3g::gui_layout::rowY(
        family.selectedLane, 6u) - 1.0;
    drawFlatButton(NSMakeRect(
        s3g::gui_layout::processorControlX(family.selectedLane.frame.x),
        selectedButtonY, 78.0, 17.0), @"MUTE",
        laneParams.mute != 0u, value);
    drawFlatButton(NSMakeRect(
        s3g::gui_layout::processorControlX(family.selectedLane.frame.x)
            + 88.0,
        selectedButtonY, 78.0, 17.0), @"KILL", false, value);
    }

    if (page == 0u) {
    const uint32_t source = plugin->selectedSource.load(
        std::memory_order_relaxed);
    const uint32_t destination = plugin->selectedDestination.load(
        std::memory_order_relaxed);
    const float route = plugin->params.matrix[
        destination * kChannelCount + source];
    s3g::clap_gui::drawProcessorMenu(@"SRC",
        [NSString stringWithFormat:@"L%u", source + 1u],
        s3g::gui_layout::rowY(family.crosspoint, 0u),
        family.crosspoint.frame.x, family.crosspoint.frame.width,
        label, value, style);
    s3g::clap_gui::drawProcessorMenu(@"DEST",
        [NSString stringWithFormat:@"L%u", destination + 1u],
        s3g::gui_layout::rowY(family.crosspoint, 1u),
        family.crosspoint.frame.x, family.crosspoint.frame.width,
        label, value, style);
    [self drawSlider:@"GAIN"
        value:[NSString stringWithFormat:@"%+.2f", route]
        norm:(route + 1.0f) * 0.5f row:2u panel:family.crosspoint
        label:label valueAttrs:value style:style];
    const CGFloat routeButtonX = s3g::gui_layout::processorControlX(
        family.crosspoint.frame.x);
    const CGFloat routeButtonY =
        s3g::gui_layout::rowY(family.crosspoint, 3u) - 1.0;
    drawFlatButton(NSMakeRect(routeButtonX,
        routeButtonY, 78.0, 17.0),
        route < 0.0f ? @"NEGATIVE" : @"POSITIVE",
        route != 0.0f, value);
    drawFlatButton(NSMakeRect(routeButtonX + 88.0,
        routeButtonY, 78.0, 17.0), @"CLEAR", false, value);
    }

    if (page == 2u) {
    [self drawSlider:@"LOW"
        value:[NSString stringWithFormat:@"%+.1f dB", laneParams.lowDb]
        norm:(laneParams.lowDb + 18.0f) / 36.0f row:0u panel:family.eq
        label:label valueAttrs:value style:style];
    const float midNorm = std::log(laneParams.midFrequencyHz / 80.0f)
        / std::log(8000.0f / 80.0f);
    [self drawSlider:@"MID F"
        value:(laneParams.midFrequencyHz >= 1000.0f
            ? [NSString stringWithFormat:@"%.2f k", laneParams.midFrequencyHz * 0.001f]
            : [NSString stringWithFormat:@"%.0f Hz", laneParams.midFrequencyHz])
        norm:midNorm row:1u panel:family.eq label:label valueAttrs:value
        style:style];
    [self drawSlider:@"MID G"
        value:[NSString stringWithFormat:@"%+.1f dB", laneParams.midGainDb]
        norm:(laneParams.midGainDb + 18.0f) / 36.0f row:2u panel:family.eq
        label:label valueAttrs:value style:style];
    [self drawSlider:@"HIGH"
        value:[NSString stringWithFormat:@"%+.1f dB", laneParams.highDb]
        norm:(laneParams.highDb + 18.0f) / 36.0f row:3u panel:family.eq
        label:label valueAttrs:value style:style];

    for (uint32_t insertSlot = 0u;
         insertSlot < s3g::kNoInputMixerInsertSlots; ++insertSlot) {
        const auto& insert = laneParams.inserts[insertSlot];
        s3g::clap_gui::drawProcessorMenu(
            [NSString stringWithFormat:@"SLOT %u", insertSlot + 1u],
            [NSString stringWithUTF8String:s3g::noInputDistortionName(
                insert.type)], s3g::gui_layout::rowY(
                family.inserts, insertSlot), family.inserts.frame.x,
            family.inserts.frame.width, label, value, style);
    }
    const auto& insert = laneParams.inserts[slot];
    [self drawSlider:@"GAIN"
        value:[NSString stringWithFormat:@"%.0f%%", insert.gain * 100.0f]
        norm:insert.gain row:3u panel:family.inserts label:label
        valueAttrs:value style:style];
    [self drawSlider:@"TONE"
        value:[NSString stringWithFormat:@"%.0f%%", insert.tone * 100.0f]
        norm:insert.tone row:4u panel:family.inserts label:label
        valueAttrs:value style:style];
    [self drawSlider:@"BIAS"
        value:[NSString stringWithFormat:@"%+.2f", insert.bias]
        norm:(insert.bias + 1.0f) * 0.5f row:5u panel:family.inserts
        label:label valueAttrs:value style:style];
    [self drawSlider:@"LEVEL"
        value:[NSString stringWithFormat:@"%+.1f dB", insert.levelDb]
        norm:(insert.levelDb + 24.0f) / 36.0f row:6u panel:family.inserts
        label:label valueAttrs:value style:style];
    s3g::clap_gui::drawToggle(@"BYPASS", insert.bypass != 0u,
        s3g::gui_layout::rowY(family.inserts, 7u), label, value, style,
        s3g::gui_layout::processorLabelX(family.inserts.frame.x),
        s3g::gui_layout::processorControlX(family.inserts.frame.x), 82.0);
    }

    if (page == 0u) {
    s3g::clap_gui::drawProcessorMenu(@"SHAPE",
        [NSString stringWithUTF8String:s3g::matrixFlowShapeName(
            plugin->params.motionShape)],
        s3g::gui_layout::rowY(family.movement, 0u),
        family.movement.frame.x, family.movement.frame.width,
        label, value, style);
    [self drawSlider:@"FLOW"
        value:[NSString stringWithFormat:@"%.0f%%", plugin->params.flow * 100.0f]
        norm:plugin->params.flow row:1u panel:family.movement label:label
        valueAttrs:value style:style];
    [self drawSlider:@"SPREAD"
        value:[NSString stringWithFormat:@"%.0f%%", plugin->params.spread * 100.0f]
        norm:plugin->params.spread row:2u panel:family.movement label:label
        valueAttrs:value style:style];
    [self drawSlider:@"VORTEX"
        value:[NSString stringWithFormat:@"%+.2f", plugin->params.vortex]
        norm:(plugin->params.vortex + 1.0f) * 0.5f row:3u
        panel:family.movement label:label valueAttrs:value style:style];
    [self drawSlider:@"DEPTH"
        value:[NSString stringWithFormat:@"%.0f%%", plugin->params.motion * 100.0f]
        norm:plugin->params.motion row:4u panel:family.movement label:label
        valueAttrs:value style:style];
    [self drawSlider:@"RATE"
        value:[NSString stringWithFormat:
            (s3g::noInputMixerMotionRateHz(plugin->params.motionRate) < 1.0f
                ? @"%.2f Hz" : @"%.1f Hz"),
            s3g::noInputMixerMotionRateHz(plugin->params.motionRate)]
        norm:plugin->params.motionRate row:5u panel:family.movement label:label
        valueAttrs:value style:style];
    [self drawSlider:@"PHASE"
        value:[NSString stringWithFormat:@"%.0f%%", plugin->params.motionPhase * 100.0f]
        norm:plugin->params.motionPhase row:6u panel:family.movement label:label
        valueAttrs:value style:style];
    }

    if (page == 3u) {
    [@"QUALITY" drawAtPoint:NSMakePoint(
        family.containment.frame.x + 16.0,
        family.containment.frame.y + 34.0) withAttributes:label];
    s3g::clap_gui::drawMenu(@"",
        [NSString stringWithFormat:@"%uX", 1u << plugin->params.quality],
        family.containment.frame.y + 36.0, label, value, style,
        family.containment.frame.x + 16.0,
        family.containment.frame.x + 108.0, 110.0);
    [@"ENERGY" drawAtPoint:NSMakePoint(
        family.containment.frame.x + 16.0,
        family.containmentMeter.y - 2.0) withAttributes:label];
    NSRect energyTrack = s3g::clap_gui::cocoaRect(
        family.containmentMeter);
    [mixerColor(0x101010) setFill]; NSRectFill(energyTrack);
    [mixerColor(0x454545) setStroke]; NSFrameRect(energyTrack);
    NSRect energyFill = NSInsetRect(energyTrack, 1.0, 1.0);
    energyFill.size.width *= std::clamp<CGFloat>(
        plugin->networkActivity.load(std::memory_order_relaxed), 0.0, 1.0);
    const float governor = plugin->minimumGovernor.load(
        std::memory_order_relaxed);
    [mixerColor(governor > 0.72f ? 0x57bfc4
        : (governor > 0.28f ? 0xc95e3b : 0xb83b32), 0.85) setFill];
    NSRectFill(energyFill);

    NSRect containmentField = s3g::clap_gui::cocoaRect(
        family.containmentField);
    [mixerColor(0x101010) setFill]; NSRectFill(containmentField);
    [mixerColor(0x454545) setStroke]; NSFrameRect(containmentField);
    for (uint32_t ring = 0u; ring < 4u; ++ring) {
        const CGFloat inset = 12.0 + ring * 10.0;
        [mixerColor(0x4a4a4a + ring * 0x080808,
            0.35 + (1.0f - governor) * 0.45) setStroke];
        NSFrameRect(NSInsetRect(containmentField, inset, inset * 0.55));
    }
    for (uint32_t node = 0u; node < kChannelCount; ++node) {
        const CGFloat angle = static_cast<CGFloat>(node) * 2.0 * M_PI / 8.0;
        const CGFloat x = NSMidX(containmentField)
            + std::cos(angle) * containmentField.size.width * 0.36;
        const CGFloat y = NSMidY(containmentField)
            + std::sin(angle) * containmentField.size.height * 0.32;
        const CGFloat activity = std::clamp<CGFloat>(
            plugin->laneActivity[node].load(std::memory_order_relaxed),
            0.0, 1.0);
        [mixerColor(activity > 0.72 ? 0xc95e3b : 0x777777,
            0.45 + activity * 0.5) setFill];
        NSRectFill(NSMakeRect(x - 3.0, y - 3.0, 6.0, 6.0));
    }
    const auto containment = static_cast<s3g::NoInputContainmentState>(
        plugin->containmentState.load(std::memory_order_relaxed));
    [[NSString stringWithUTF8String:s3g::noInputContainmentName(containment)]
        drawAtPoint:NSMakePoint(family.containment.frame.x + 16.0,
            NSMaxY(s3g::clap_gui::cocoaRect(family.containmentField)) + 22.0)
        withAttributes:value];
    }
    NSRect panicRect = s3g::clap_gui::cocoaRect(family.panicButton);
    [mixerColor(0x7e2924) setFill]; NSRectFill(panicRect);
    [mixerColor(0xc95e3b) setStroke]; NSFrameRect(panicRect);
    const NSSize panicSize = [@"PANIC" sizeWithAttributes:label];
    [@"PANIC" drawAtPoint:NSMakePoint(
        panicRect.origin.x + (panicRect.size.width - panicSize.width) * 0.5,
        panicRect.origin.y + (panicRect.size.height - panicSize.height) * 0.5)
        withAttributes:label];

    [self drawOpenMenu:plugin attrs:value style:style];
}

- (const s3g::gui_layout::Panel*)panelForParam:(clap_id)param
{
    const auto& family = s3g::gui_layout::kNoInputMixerFamilyLayout;
    if (param == kOutputParamId || param == kCeilingParamId)
        return &family.output;
    if ((param >= kFeedbackParamId && param <= kFormantParamId)
        || (param >= kAgencyParamId && param <= kVarianceParamId))
        return &family.network;
    if (param >= kFlowParamId && param <= kMotionPhaseParamId)
        return &family.movement;
    uint32_t destination = 0u;
    uint32_t source = 0u;
    if (decodeMatrixParam(param, destination, source))
        return &family.crosspoint;
    uint32_t lane = 0u;
    clap_id offset = 0u;
    if (!decodeLaneParam(param, lane, offset)) return nullptr;
    if (offset <= kLaneLevelOffset || offset == kLaneAuxAOffset
        || offset == kLaneAuxBOffset) return &family.selectedLane;
    if (offset >= kLaneLowOffset && offset <= kLaneHighOffset)
        return &family.eq;
    uint32_t slot = 0u;
    clap_id insertOffset = 0u;
    if (decodeInsertOffset(offset, slot, insertOffset))
        return &family.inserts;
    return nullptr;
}

- (void)updateSlider:(NSPoint)point
{
    if (_dragParam == CLAP_INVALID_ID) return;
    auto* plugin = static_cast<Plugin*>(_plugin);
    const auto* panel = [self panelForParam:_dragParam];
    ParamRange range;
    if (!panel || !paramRange(_dragParam, range)) return;
    const double controlX = s3g::gui_layout::processorControlX(
        panel->frame.x);
    const double width = s3g::gui_layout::processorTrackWidth(
        panel->frame.width);
    const double normalized = std::clamp(
        (point.x - controlX) / width, 0.0, 1.0);
    double value = range.minimum
        + normalized * (range.maximum - range.minimum);
    if (_dragParam == laneParamId(
            plugin->selectedLane.load(std::memory_order_relaxed),
            kLaneMidFrequencyOffset)) {
        value = 80.0 * std::pow(8000.0 / 80.0, normalized);
    }
    [self applyGuiParam:_dragParam value:value];
    [self setNeedsDisplay:YES];
}

- (void)applyMenuSelection:(uint32_t)index
{
    auto* plugin = static_cast<Plugin*>(_plugin);
    const uint32_t lane = plugin->selectedLane.load(
        std::memory_order_relaxed);
    switch (_openMenu) {
    case kMenuPreset:
        if (index < s3g::kNoInputMixerFactoryPresetCount) {
            uint32_t seed = plugin->params.seed * 1664525u + 1013904223u;
            if (seed == 0u) seed = 1u;
            const float variance = plugin->params.variance;
            auto patch = s3g::noInputMixerFactoryPreset(index);
            patch = s3g::variedNoInputMixerParams(
                patch, seed, variance);
            applyCompletePatch(*plugin,
                patch, 0.58f);
            std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                s3g::noInputMixerFactoryPresetName(index));
        }
        break;
    case kMenuLane:
        plugin->selectedLane.store(std::min<uint32_t>(index,
            kChannelCount - 1u), std::memory_order_relaxed);
        plugin->selectedDestination.store(std::min<uint32_t>(index,
            kChannelCount - 1u), std::memory_order_relaxed);
        break;
    case kMenuSource:
        plugin->selectedSource.store(std::min<uint32_t>(index,
            kChannelCount - 1u), std::memory_order_relaxed);
        break;
    case kMenuDestination:
        plugin->selectedDestination.store(std::min<uint32_t>(index,
            kChannelCount - 1u), std::memory_order_relaxed);
        plugin->selectedLane.store(std::min<uint32_t>(index,
            kChannelCount - 1u), std::memory_order_relaxed);
        break;
    case kMenuSlot0:
    case kMenuSlot1:
    case kMenuSlot2: {
        const uint32_t slot = static_cast<uint32_t>(_openMenu - kMenuSlot0);
        plugin->selectedSlot.store(slot, std::memory_order_relaxed);
        [self applyGuiParam:insertParamId(lane, slot, kInsertTypeOffset)
            value:index];
        break;
    }
    case kMenuQuality:
        [self applyGuiParam:kQualityParamId value:index];
        break;
    case kMenuMotionShape:
        [self applyGuiParam:kMotionShapeParamId value:index];
        break;
    default: break;
    }
}

- (void)beginSlider:(clap_id)param event:(NSEvent*)event point:(NSPoint)point
{
    auto* plugin = static_cast<Plugin*>(_plugin);
    double defaultValue = 0.0;
    if (s3g::clap_gui::sliderDoubleClickDefault(event,
            &plugin->plugin, param, &defaultValue)) {
        [self applyGuiParam:param value:defaultValue];
        _dragParam = CLAP_INVALID_ID;
    } else {
        _dragParam = param;
        [self updateSlider:point];
    }
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    [[self window] makeFirstResponder:self];
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    auto* plugin = static_cast<Plugin*>(_plugin);
    const auto& family = s3g::gui_layout::kNoInputMixerFamilyLayout;


    if (_openMenu != kMenuNone) {
        const int hit = s3g::clap_gui::dropdownHitIndex(point,
            [self menuDropdownRect:_openMenu], 18.0,
            [self menuItemCount:_openMenu]);
        if (hit >= 0) [self applyMenuSelection:static_cast<uint32_t>(hit)];
        _openMenu = kMenuNone;
        [self setNeedsDisplay:YES];
        if (hit >= 0) return;
    }

    const auto titleBand = s3g::gui_layout::matrixTitleBand(family.canvas);
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.presetMenu))) {
        _openMenu = kMenuPreset;
        [self setNeedsDisplay:YES];
        return;
    }
    if (s3g::clap_gui::handleProcessorTitleClick(point,
            &plugin->plugin, @"No Input Mixer", titleBand,
            _titlePresetName, sizeof(_titlePresetName))) {
        [self setNeedsDisplay:YES];
        return;
    }

    if (NSPointInRect(point, mixerPopButtonRect())) {
        if (_mixerPopupChild) {
            [[self window] performClose:nil];
        } else {
            [self openPagePopup:plugin->guiPage.load(
                std::memory_order_relaxed)];
        }
        return;
    }

    for (uint32_t page = 0u; page < 4u; ++page) {
        if (NSPointInRect(point, fieldTabRect(page))) {
            if (_mixerPopupChild) return;
            if (_pagePanels[page] && [_pagePanels[page] isVisible]) {
                [_pagePanels[page] makeKeyAndOrderFront:nil];
                return;
            }
            plugin->guiPage.store(page, std::memory_order_relaxed);
            [self setNeedsDisplay:YES];
            return;
        }
    }

    const uint32_t page = _lockedPage < 4u ? _lockedPage
        : plugin->guiPage.load(std::memory_order_relaxed);
    const NSRect plot = s3g::clap_gui::cocoaRect(family.fieldPlot);
    if (NSPointInRect(point, plot)) {
        if (page == 0u) {
            if (NSPointInRect(point, clearConnectionsButtonRect())) {
                [self clearAllConnections];
                return;
            }
            for (uint32_t mode = 0u; mode < 2u; ++mode) {
                if (!NSPointInRect(point, wiringModeButtonRect(mode)))
                    continue;
                _wiringGridMode = mode == 1u;
                if (_mixerPopupChild && _mixerPopupOwner) {
                    _mixerPopupOwner->_wiringGridMode = _wiringGridMode;
                    [_mixerPopupOwner setNeedsDisplay:YES];
                } else if (_pagePopupViews[0u]) {
                    _pagePopupViews[0u]->_wiringGridMode = _wiringGridMode;
                    [_pagePopupViews[0u] setNeedsDisplay:YES];
                }
                [self setNeedsDisplay:YES];
                return;
            }
            if (_wiringGridMode) {
                const NSRect grid = wiringGridRect();
                const CGFloat gridLeft = grid.origin.x + 54.0;
                const CGFloat gridTop = grid.origin.y + 36.0;
                const CGFloat spacing = 58.0;
                const int source = static_cast<int>(std::lround(
                    (point.x - gridLeft) / spacing));
                const int destination = static_cast<int>(std::lround(
                    (point.y - gridTop) / spacing));
                if (source >= 0 && source < static_cast<int>(kChannelCount)
                    && destination >= 0
                    && destination < static_cast<int>(kChannelCount)
                    && std::abs(point.x
                        - (gridLeft + spacing * source)) < 14.0
                    && std::abs(point.y
                        - (gridTop + spacing * destination)) < 14.0) {
                    const bool alreadySelected = plugin->selectedSource.load(
                            std::memory_order_relaxed)
                            == static_cast<uint32_t>(source)
                        && plugin->selectedDestination.load(
                            std::memory_order_relaxed)
                            == static_cast<uint32_t>(destination);
                    plugin->selectedSource.store(source,
                        std::memory_order_relaxed);
                    plugin->selectedDestination.store(destination,
                        std::memory_order_relaxed);
                    plugin->selectedLane.store(destination,
                        std::memory_order_relaxed);
                    const clap_id id = matrixParamId(destination, source);
                    const float current = plugin->params.matrix[
                        destination * kChannelCount + source];
                    const bool negative = ([event modifierFlags]
                        & NSEventModifierFlagOption) != 0;
                    const float created = source == destination
                        ? (negative ? -0.94f : 0.94f)
                        : (negative ? -0.25f : 0.25f);
                    if (std::abs(current) <= 0.001f) {
                        [self applyGuiParam:id value:created];
                    } else if (alreadySelected) {
                        [self applyGuiParam:id value:0.0f];
                    } else {
                        [self setNeedsDisplay:YES];
                    }
                    return;
                }
            } else {
                for (uint32_t source = 0u; source < kChannelCount;
                     ++source) {
                    const NSPoint port = wiringPortPoint(false, source);
                    if (std::hypot(point.x - port.x, point.y - port.y)
                        > 16.0) continue;
                    _wireDragSource = static_cast<int>(source);
                    _wireDragPoint = point;
                    [self setNeedsDisplay:YES];
                    return;
                }
                float closest = 11.0f;
                int hitSource = -1;
                int hitDestination = -1;
                for (uint32_t destination = 0u;
                     destination < kChannelCount; ++destination) {
                    for (uint32_t source = 0u; source < kChannelCount;
                         ++source) {
                        const float stored = plugin->params.matrix[
                            destination * kChannelCount + source];
                        if (std::abs(stored) <= 0.001f) continue;
                        const NSPoint a = wiringPortPoint(false, source);
                        const NSPoint b = wiringPortPoint(true, destination);
                        NSPoint c1;
                        NSPoint c2;
                        wiringControlPoints(a, b, plugin->params.vortex,
                            c1, c2);
                        for (uint32_t sample = 1u; sample < 32u; ++sample) {
                            const NSPoint curve = cubicPoint(a, c1, c2, b,
                                static_cast<CGFloat>(sample) / 32.0);
                            const float distance = static_cast<float>(
                                std::hypot(point.x - curve.x,
                                    point.y - curve.y));
                            if (distance >= closest) continue;
                            closest = distance;
                            hitSource = static_cast<int>(source);
                            hitDestination = static_cast<int>(destination);
                        }
                    }
                }
                if (hitSource >= 0 && hitDestination >= 0) {
                    const bool alreadySelected = plugin->selectedSource.load(
                            std::memory_order_relaxed)
                            == static_cast<uint32_t>(hitSource)
                        && plugin->selectedDestination.load(
                            std::memory_order_relaxed)
                            == static_cast<uint32_t>(hitDestination);
                    plugin->selectedSource.store(hitSource,
                        std::memory_order_relaxed);
                    plugin->selectedDestination.store(hitDestination,
                        std::memory_order_relaxed);
                    plugin->selectedLane.store(hitDestination,
                        std::memory_order_relaxed);
                    if (alreadySelected) {
                        [self applyGuiParam:matrixParamId(
                            hitDestination, hitSource) value:0.0];
                    } else {
                        [self setNeedsDisplay:YES];
                    }
                    return;
                }
            }
        } else if (page == 1u) {
            const NSPoint mixerOffset = mixerSurfaceOffset(plot);
            const auto actual = [&](NSRect rect) {
                return translatedRect(rect, mixerOffset);
            };
            for (uint32_t lane = 0u; lane < kChannelCount; ++lane) {
                const NSRect strip = actual(popupStripRect(lane));
                if (!NSPointInRect(point, strip)) continue;
                plugin->selectedLane.store(lane, std::memory_order_relaxed);
                plugin->selectedDestination.store(lane,
                    std::memory_order_relaxed);
                const auto hit = [&](NSRect rect) {
                    return NSPointInRect(point,
                        NSInsetRect(rect, -6.0, -6.0));
                };
                if (hit(actual(popupBodyRect(popupStripRect(lane))))) {
                    [self beginMixerDrag:laneParamId(lane, kLaneBodyOffset)
                        rect:actual(popupBodyRect(popupStripRect(lane)))
                        minimum:0.0 maximum:1.0
                        vertical:NO point:point];
                    return;
                }
                if (hit(actual(popupLossRect(popupStripRect(lane))))) {
                    [self beginMixerDrag:laneParamId(lane, kLaneLossOffset)
                        rect:actual(popupLossRect(popupStripRect(lane)))
                        minimum:0.0 maximum:1.0
                        vertical:NO point:point];
                    return;
                }
                if (hit(actual(popupLoopRect(popupStripRect(lane))))) {
                    const double polarity = ([event modifierFlags]
                        & NSEventModifierFlagOption) != 0 ? -1.0 : 1.0;
                    [self beginMixerDrag:matrixParamId(lane, lane)
                        rect:actual(popupLoopRect(popupStripRect(lane)))
                        minimum:0.0
                        maximum:polarity vertical:YES point:point];
                    return;
                }
                const clap_id eqIds[3] = {
                    laneParamId(lane, kLaneLowOffset),
                    laneParamId(lane, kLaneMidGainOffset),
                    laneParamId(lane, kLaneHighOffset),
                };
                for (uint32_t band = 0u; band < 3u; ++band) {
                    const NSRect eq = actual(popupEqRect(
                        popupStripRect(lane), band));
                    if (!hit(eq)) continue;
                    [self beginMixerDrag:eqIds[band]
                        rect:eq minimum:-18.0
                        maximum:18.0 vertical:YES point:point];
                    return;
                }
                for (uint32_t bus = 0u; bus < 2u; ++bus) {
                    const NSRect send = actual(popupSendRect(
                        popupStripRect(lane), bus));
                    if (!hit(send)) continue;
                    [self beginMixerDrag:laneParamId(lane,
                            bus == 0u ? kLaneAuxAOffset : kLaneAuxBOffset)
                        rect:send minimum:0.0
                        maximum:1.0 vertical:NO point:point];
                    return;
                }
                for (uint32_t slot = 0u;
                     slot < s3g::kNoInputMixerInsertSlots; ++slot) {
                    const NSRect insert = actual(popupInsertRect(
                        popupStripRect(lane), slot));
                    if (!NSPointInRect(point, insert)) continue;
                    plugin->selectedSlot.store(slot,
                        std::memory_order_relaxed);
                    [self setNeedsDisplay:YES];
                    return;
                }
                if (hit(actual(popupFaderRect(popupStripRect(lane))))) {
                    [self beginMixerDrag:laneParamId(lane, kLaneLevelOffset)
                        rect:actual(popupFaderRect(popupStripRect(lane)))
                        minimum:-60.0 maximum:12.0
                        vertical:YES point:point];
                    return;
                }
                if (NSPointInRect(point,
                        actual(popupMuteRect(popupStripRect(lane))))) {
                    [self applyGuiParam:laneParamId(lane, kLaneMuteOffset)
                        value:(plugin->params.lanes[lane].mute == 0u
                            ? 1.0 : 0.0)];
                    return;
                }
                [self setNeedsDisplay:YES];
                return;
            }
            for (uint32_t bus = 0u; bus < 2u; ++bus) {
                const NSRect typeRect = actual(popupAuxTypeRect(bus));
                if (NSPointInRect(point, typeRect)) {
                    const uint32_t current = static_cast<uint32_t>(
                        plugin->params.aux[bus].effect.type);
                    [self applyGuiParam:bus == 0u ? kAuxATypeParamId
                            : kAuxBTypeParamId
                        value:(current + 1u)
                            % s3g::kNoInputDistortionTypeCount];
                    return;
                }
                const clap_id ids[4] = {
                    bus == 0u ? kAuxAGainParamId : kAuxBGainParamId,
                    bus == 0u ? kAuxAToneParamId : kAuxBToneParamId,
                    bus == 0u ? kAuxAReturnParamId : kAuxBReturnParamId,
                    bus == 0u ? kAuxAFeedbackParamId
                              : kAuxBFeedbackParamId,
                };
                for (uint32_t local = 0u; local < 4u; ++local) {
                    const NSRect track = actual(
                        popupAuxSliderRect(bus, local));
                    if (!NSPointInRect(point,
                            NSInsetRect(track, -8.0, -8.0))) continue;
                    [self beginMixerDrag:ids[local] rect:track minimum:0.0
                        maximum:(local == 3u ? 0.96 : 1.0)
                        vertical:NO point:point];
                    return;
                }
            }
            const clap_id toneIds[2] = {
                kInternalToneParamId, kHouseToneParamId,
            };
            for (uint32_t row = 0u; row < 2u; ++row) {
                const NSRect track = actual(popupToneRect(row));
                if (!NSPointInRect(point,
                        NSInsetRect(track, -8.0, -8.0))) continue;
                [self beginMixerDrag:toneIds[row] rect:track minimum:-1.0
                    maximum:1.0 vertical:NO point:point];
                return;
            }
            return;
        } else if (page == 2u) {
            const NSRect overview = channelOverviewRect();
            const CGFloat gap = 12.0;
            const CGFloat cellWidth = (overview.size.width - gap * 5.0) / 4.0;
            const CGFloat cellHeight = (overview.size.height
                - gap * 3.0) / 2.0;
            for (uint32_t lane = 0u; lane < kChannelCount; ++lane) {
                const uint32_t column = lane % 4u;
                const uint32_t row = lane / 4u;
                NSRect cell = NSMakeRect(overview.origin.x + gap
                        + column * (cellWidth + gap),
                    overview.origin.y + gap + row * (cellHeight + gap),
                    cellWidth, cellHeight);
                if (NSPointInRect(point, cell)) {
                    plugin->selectedLane.store(lane,
                        std::memory_order_relaxed);
                    [self setNeedsDisplay:YES];
                    return;
                }
            }
        }
    }

    const auto openMenuIfHit = [&](int menu) {
        if (NSPointInRect(point, [self menuAnchorRect:menu])) {
            _openMenu = menu;
            [self setNeedsDisplay:YES];
            return true;
        }
        return false;
    };
    if ((page == 0u && (openMenuIfHit(kMenuSource)
            || openMenuIfHit(kMenuDestination)
            || openMenuIfHit(kMenuMotionShape)))
        || (page == 2u && (openMenuIfHit(kMenuLane)
            || openMenuIfHit(kMenuSlot0) || openMenuIfHit(kMenuSlot1)
            || openMenuIfHit(kMenuSlot2)))
        || (page == 3u && openMenuIfHit(kMenuQuality))) return;

    if (page == 0u && NSPointInRect(point, seedNewButtonRect())) {
        plugin->seedRequested.store(true, std::memory_order_release);
        [self markPatchCustom];
        [self setNeedsDisplay:YES];
        return;
    }
    if (page == 0u && NSPointInRect(point, randomButtonRect())) {
        uint32_t seed = plugin->params.seed * 1664525u + 1013904223u;
        if (seed == 0u) seed = 1u;
        applyCompletePatch(*plugin,
            s3g::randomizedNoInputMixerParams(seed), 0.64f);
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
            "RANDOM");
        [self setNeedsDisplay:YES];
        return;
    }
    if (page == 0u && NSPointInRect(point, forgetButtonRect())) {
        uint32_t seed = plugin->params.seed * 1664525u + 1013904223u;
        if (seed == 0u) seed = 1u;
        applyCompletePatch(*plugin,
            s3g::forgottenNoInputMixerParams(plugin->params, seed), 0.42f);
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
            "FORGOTTEN");
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
            family.panicButton))) {
        plugin->panicRequested.store(true, std::memory_order_release);
        return;
    }

    const uint32_t lane = plugin->selectedLane.load(
        std::memory_order_relaxed);
    const uint32_t slot = plugin->selectedSlot.load(
        std::memory_order_relaxed);
    if (page == 3u && NSPointInRect(point, s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(family.output, 2u)))) {
        [self applyGuiParam:kLimiterParamId
            value:(plugin->params.limiterEnabled == 0u ? 1.0 : 0.0)];
        return;
    }
    if (page == 3u && NSPointInRect(point, s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(family.output, 3u)))) {
        [self applyGuiParam:kDcBlockParamId
            value:(plugin->params.dcBlockEnabled == 0u ? 1.0 : 0.0)];
        return;
    }
    if (page == 2u && NSPointInRect(point, s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(family.selectedLane, 6u)))) {
        const CGFloat split = s3g::gui_layout::processorControlX(
            family.selectedLane.frame.x) + 83.0;
        if (point.x < split) {
            [self applyGuiParam:laneParamId(lane, kLaneMuteOffset)
                value:(plugin->params.lanes[lane].mute == 0u ? 1.0 : 0.0)];
        } else {
            plugin->killMask.fetch_or(1u << lane,
                std::memory_order_release);
        }
        return;
    }
    const NSRect crosspointActions = s3g::clap_gui::cocoaRect(
        s3g::gui_layout::sliderHitRect(family.crosspoint, 3u));
    const CGFloat crosspointSplit = s3g::gui_layout::processorControlX(
        family.crosspoint.frame.x) + 83.0;
    if (page == 0u && NSPointInRect(point, crosspointActions)
        && point.x < crosspointSplit) {
        const uint32_t source = plugin->selectedSource.load(
            std::memory_order_relaxed);
        const uint32_t destination = plugin->selectedDestination.load(
            std::memory_order_relaxed);
        const clap_id id = matrixParamId(destination, source);
        double route = 0.0;
        paramValue(*plugin, id, route);
        [self applyGuiParam:id value:(std::abs(route) < 0.001
            ? -0.50 : -route)];
        return;
    }
    if (page == 0u && NSPointInRect(point, crosspointActions)
        && point.x >= crosspointSplit) {
        [self applyGuiParam:matrixParamId(
            plugin->selectedDestination.load(std::memory_order_relaxed),
            plugin->selectedSource.load(std::memory_order_relaxed)) value:0.0];
        return;
    }
    if (page == 2u && NSPointInRect(point, s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(family.inserts, 7u)))) {
        [self applyGuiParam:insertParamId(
            lane, slot, kInsertBypassOffset)
            value:(plugin->params.lanes[lane].inserts[slot].bypass == 0u
                ? 1.0 : 0.0)];
        return;
    }

    const clap_id outputIds[2] = { kOutputParamId, kCeilingParamId };
    for (uint32_t row = 0u; row < 2u; ++row) {
        if (page == 3u && NSPointInRect(point, s3g::clap_gui::cocoaRect(
                s3g::gui_layout::sliderHitRect(family.output, row)))) {
            [self beginSlider:outputIds[row] event:event point:point];
            return;
        }
    }
    const clap_id networkIds[8] = {
        kFeedbackParamId, kCouplingParamId, kPhaseParamId,
        kDriftParamId, kFormantParamId, kAgencyParamId,
        kSpaceParamId, kVarianceParamId,
    };
    for (uint32_t row = 1u; row < 9u; ++row) {
        if (page == 0u && NSPointInRect(point, s3g::clap_gui::cocoaRect(
                s3g::gui_layout::sliderHitRect(family.network, row)))) {
            [self beginSlider:networkIds[row - 1u] event:event point:point];
            return;
        }
    }
    const clap_id selectedIds[5] = {
        laneParamId(lane, kLaneBodyOffset),
        laneParamId(lane, kLaneLossOffset),
        laneParamId(lane, kLaneLevelOffset),
        laneParamId(lane, kLaneAuxAOffset),
        laneParamId(lane, kLaneAuxBOffset),
    };
    for (uint32_t row = 1u; row < 6u; ++row) {
        if (page == 2u && NSPointInRect(point, s3g::clap_gui::cocoaRect(
                s3g::gui_layout::sliderHitRect(family.selectedLane, row)))) {
            [self beginSlider:selectedIds[row - 1u] event:event point:point];
            return;
        }
    }
    if (page == 0u && NSPointInRect(point, s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(family.crosspoint, 2u)))) {
        [self beginSlider:matrixParamId(
            plugin->selectedDestination.load(std::memory_order_relaxed),
            plugin->selectedSource.load(std::memory_order_relaxed))
            event:event point:point];
        return;
    }
    const clap_id eqIds[4] = {
        laneParamId(lane, kLaneLowOffset),
        laneParamId(lane, kLaneMidFrequencyOffset),
        laneParamId(lane, kLaneMidGainOffset),
        laneParamId(lane, kLaneHighOffset),
    };
    for (uint32_t row = 0u; row < 4u; ++row) {
        if (page == 2u && NSPointInRect(point, s3g::clap_gui::cocoaRect(
                s3g::gui_layout::sliderHitRect(family.eq, row)))) {
            [self beginSlider:eqIds[row] event:event point:point];
            return;
        }
    }
    const clap_id insertIds[4] = {
        insertParamId(lane, slot, kInsertGainOffset),
        insertParamId(lane, slot, kInsertToneOffset),
        insertParamId(lane, slot, kInsertBiasOffset),
        insertParamId(lane, slot, kInsertLevelOffset),
    };
    for (uint32_t row = 3u; row < 7u; ++row) {
        if (page == 2u && NSPointInRect(point, s3g::clap_gui::cocoaRect(
                s3g::gui_layout::sliderHitRect(family.inserts, row)))) {
            [self beginSlider:insertIds[row - 3u] event:event point:point];
            return;
        }
    }
    const clap_id movementIds[6] = {
        kFlowParamId, kSpreadParamId, kVortexParamId,
        kMotionParamId, kMotionRateParamId, kMotionPhaseParamId,
    };
    for (uint32_t row = 1u; row < 7u; ++row) {
        if (page == 0u && NSPointInRect(point, s3g::clap_gui::cocoaRect(
                s3g::gui_layout::sliderHitRect(family.movement, row)))) {
            [self beginSlider:movementIds[row - 1u] event:event point:point];
            return;
        }
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    if (_wireDragSource >= 0) {
        _wireDragPoint = point;
        [self setNeedsDisplay:YES];
    } else if (_mixerDragParam != CLAP_INVALID_ID) [self updateMixerDrag:point];
    else if (_dragParam != CLAP_INVALID_ID) [self updateSlider:point];
}

- (void)mouseUp:(NSEvent*)event
{
    if (_wireDragSource >= 0) {
        const NSPoint point = [self convertPoint:[event locationInWindow]
            fromView:nil];
        auto* plugin = static_cast<Plugin*>(_plugin);
        for (uint32_t destination = 0u; destination < kChannelCount;
             ++destination) {
            const NSPoint port = wiringPortPoint(true, destination);
            if (std::hypot(point.x - port.x, point.y - port.y) > 18.0)
                continue;
            const uint32_t source = static_cast<uint32_t>(_wireDragSource);
            plugin->selectedSource.store(source, std::memory_order_relaxed);
            plugin->selectedDestination.store(destination,
                std::memory_order_relaxed);
            plugin->selectedLane.store(destination,
                std::memory_order_relaxed);
            const clap_id id = matrixParamId(destination, source);
            const float current = plugin->params.matrix[
                destination * kChannelCount + source];
            const bool negative = ([event modifierFlags]
                & NSEventModifierFlagOption) != 0;
            const float created = source == destination
                ? (negative ? -0.94f : 0.94f)
                : (negative ? -0.25f : 0.25f);
            [self applyGuiParam:id value:(std::abs(current) > 0.001f
                ? 0.0f : created)];
            break;
        }
        _wireDragSource = -1;
        [self setNeedsDisplay:YES];
    }
    _dragParam = CLAP_INVALID_ID;
    _mixerDragParam = CLAP_INVALID_ID;
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
    p->guiView = [[S3GNoInputMixerView alloc] initWithPlugin:p];
    if (!p->guiView) return false;
    if (!s3g::clap_gui::createResponsiveViewport(p->guiViewport,
            static_cast<NSView*>(p->guiView), kGuiWidth, kGuiHeight,
            kGuiWidth, 360u)) {
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
        [static_cast<S3GNoInputMixerView*>(p->guiView) stopRefreshTimer];
        s3g::clap_gui::destroyResponsiveViewport(p->guiViewport,
            p->guiView);
    }
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }

bool guiGetSize(const clap_plugin_t* plugin, uint32_t* width,
    uint32_t* height)
{
    return s3g::clap_gui::getResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height,
        kGuiWidth, 360u);
}

bool guiCanResize(const clap_plugin_t*) { return true; }

bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints)
{
    return s3g::clap_gui::getResponsiveResizeHints(hints);
}

bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* width,
    uint32_t* height)
{
    return s3g::clap_gui::adjustResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height,
        kGuiWidth, 360u);
}

bool guiSetSize(const clap_plugin_t* plugin, uint32_t width,
    uint32_t height)
{
    return s3g::clap_gui::setResponsiveViewportSize(
        self(plugin)->guiViewport, width, height);
}

bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    if (!window || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0
        || !window->cocoa) return false;
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
            p->guiViewport, false)) return false;
    p->guiVisible = true;
    [static_cast<S3GNoInputMixerView*>(p->guiView) startRefreshTimer];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    p->guiVisible = false;
    [static_cast<S3GNoInputMixerView*>(p->guiView) stopRefreshTimer];
    [static_cast<S3GNoInputMixerView*>(p->guiView) hideMixerPopup];
    return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true);
}

const clap_plugin_gui_t guiExt {
    guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale,
    guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize,
    guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide,
};

} // namespace

#endif

namespace {

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SYNTHESIZER,
    CLAP_PLUGIN_FEATURE_DISTORTION,
    CLAP_PLUGIN_FEATURE_SURROUND,
    nullptr,
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.no-input-mixer-8ch",
    "s3g Processor No Input Mixer 8ch",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "Eight-channel zero-input feedback ecology with signed routing, per-lane EQ, and nonlinear inserts.",
    features,
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*,
    const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
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
    factoryGetPluginCount, factoryGetPluginDescriptor, createPlugin,
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
    CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory,
};
