#include "s3g_no_input_mixer.h"
#include "s3g_parameter_surface.h"
#include "s3g_realtime.h"
#include "s3g_ring_output_mixdown.h"
#include "../common/s3g_nim_midi_feedback.h"

#include <clap/clap.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include "../common/s3g_clap_macos.h"
#include "../common/s3g_cocoa_gui.h"
#include "../common/s3g_parameter_surface_cocoa.h"
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

constexpr uint32_t kStateVersion = 15u;
constexpr uint32_t kChannelCount = s3g::kNoInputMixerChannels;
constexpr uint32_t kRouteScopeSamples = 96u;
constexpr uint32_t kGuiWidth = 1356u;
constexpr uint32_t kGuiHeight = 820u;
constexpr uint32_t kPageCount = 5u;
constexpr double kPerformanceMixerReferenceHeight = 760.0;
constexpr double kMatrixLatchAttackCaptureSeconds = 0.050;
constexpr uint32_t kNrpnFeedbackParamsPerBlock = 16u;
constexpr uint8_t kMidiControlChannel = 15u;

constexpr uint8_t kMidiLaneMuteNoteBase = 32u;
constexpr uint8_t kMidiLaneKillNoteBase = 40u;
constexpr uint8_t kMidiInsertOneBypassNoteBase = 48u;
constexpr uint8_t kMidiInsertTwoBypassNoteBase = 56u;
constexpr uint8_t kMidiInsertThreeBypassNoteBase = 64u;
constexpr uint8_t kMidiAuxMuteNoteBase = 72u;
constexpr uint8_t kMidiPitchLockNoteBase = 80u;
constexpr uint8_t kMidiMatrixFlipNote = 117u;
constexpr uint8_t kMidiMatrixLatchNote = 118u;
constexpr uint8_t kMidiMatrixSignToggleNote = 119u;
constexpr uint8_t kMidiNewNote = 120u;
constexpr uint8_t kMidiForgetNote = 121u;
constexpr uint8_t kMidiRandomMidNote = 122u;
constexpr uint8_t kMidiPanicNote = 123u;
constexpr uint8_t kMidiClearMatrixNote = 124u;
constexpr uint8_t kMidiRandomLowNote = 125u;
constexpr uint8_t kMidiRandomHighNote = 126u;
constexpr uint8_t kMidiReactDirectionToggleNote = 127u;

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
constexpr clap_id kAuxAMuteParamId = 33u;
constexpr clap_id kAuxBMuteParamId = 34u;
constexpr clap_id kBehaviorParamId = 35u;
constexpr clap_id kEventRateParamId = 36u;
constexpr clap_id kEventLengthParamId = 37u;
constexpr clap_id kEventDensityParamId = 38u;
constexpr clap_id kEventChaosParamId = 39u;
constexpr clap_id kEventSlewParamId = 40u;
constexpr clap_id kEventChokeParamId = 41u;
constexpr clap_id kAuxABiasParamId = 42u;
constexpr clap_id kAuxBBiasParamId = 43u;
constexpr clap_id kReactModeParamId = 44u;
constexpr clap_id kReactDepthParamId = 45u;
constexpr clap_id kReactThresholdParamId = 46u;
constexpr clap_id kReactAttackParamId = 47u;
constexpr clap_id kReactReleaseParamId = 48u;
constexpr clap_id kReactPolarityParamId = 49u;
constexpr clap_id kControllerHoldParamId = 50u;
constexpr clap_id kSlowTimeParamId = 51u;
constexpr clap_id kClockSyncParamId = 52u;
constexpr clap_id kFieldDivisionParamId = 53u;
constexpr clap_id kEventDivisionParamId = 54u;
// Retired NIM Parameter Surface IDs. They remain only in legacy state decoder
// helpers and are no longer published as live parameters.
constexpr clap_id kSurfaceXParamId = 55u;
constexpr clap_id kSurfaceYParamId = 56u;
constexpr clap_id kMatrixMidiModeParamId = 57u;
constexpr clap_id kMatrixMidiSignParamId = 58u;
constexpr clap_id kMatrixMidiRampParamId = 59u;
constexpr clap_id kBehaviorDepthParamId = 60u;
constexpr clap_id kOutputFormatParamId = 61u;
constexpr clap_id kOutputRotationParamId = 62u;
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
constexpr clap_id kLaneTuneNoteOffset = 10u;
constexpr clap_id kLaneTuneCentsOffset = 11u;
constexpr clap_id kLanePitchLockOffset = 12u;
constexpr clap_id kLaneAuxTapAOffset = 13u;
constexpr clap_id kLaneAuxTapBOffset = 14u;
constexpr clap_id kLaneAuxReturnAOffset = 15u;
constexpr clap_id kLaneAuxReturnBOffset = 16u;
constexpr clap_id kLaneInsertBaseOffset = 20u;
constexpr clap_id kLaneInsertStride = 10u;
constexpr clap_id kInsertTypeOffset = 0u;
constexpr clap_id kInsertGainOffset = 1u;
constexpr clap_id kInsertToneOffset = 2u;
constexpr clap_id kInsertBiasOffset = 3u;
constexpr clap_id kInsertLevelOffset = 4u;
constexpr clap_id kInsertBypassOffset = 5u;
constexpr uint32_t kGlobalParamCount = 60u;
constexpr uint32_t kLaneDirectParamCount = 17u;
constexpr uint32_t kInsertParamCount = 6u;
constexpr uint32_t kLaneParamCount = kLaneDirectParamCount
    + s3g::kNoInputMixerInsertSlots * kInsertParamCount;
constexpr uint32_t kTotalParamCount = kGlobalParamCount
    + s3g::kNoInputMixerMatrixCells
    + kChannelCount * kLaneParamCount;
constexpr uint32_t kNrpnFeedbackWordCount =
    (kTotalParamCount + 63u) / 64u;

const s3g::NoInputMixerParams kDefaultParams =
    s3g::defaultNoInputMixerParams();
const s3g::NoInputMovementBehaviorParams kDefaultBehaviorParams {};

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
    { kMotionShapeParamId, "Shape", "Movement", 0.0,
        static_cast<double>(s3g::kMatrixFlowShapeCount - 1u),
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
    { kAuxAMuteParamId, "Mute All", "Aux Return A", 0.0, 1.0,
        0.0, true },
    { kAuxBMuteParamId, "Mute All", "Aux Return B", 0.0, 1.0,
        0.0, true },
    { kBehaviorParamId, "Behavior", "Movement / Articulation", 0.0,
        static_cast<double>(s3g::kNoInputMovementBehaviorCount - 1u),
        static_cast<double>(kDefaultBehaviorParams.behavior), true },
    { kEventRateParamId, "Event Rate", "Movement / Articulation", 0.0, 1.0,
        kDefaultBehaviorParams.eventRate, false },
    { kEventLengthParamId, "Event Length", "Movement / Articulation", 0.0, 1.0,
        kDefaultBehaviorParams.length, false },
    { kEventDensityParamId, "Density", "Movement / Articulation", 0.0, 1.0,
        kDefaultBehaviorParams.density, false },
    { kEventChaosParamId, "Chaos", "Movement / Articulation", 0.0, 1.0,
        kDefaultBehaviorParams.chaos, false },
    { kEventSlewParamId, "Slew", "Movement / Articulation", 0.0, 1.0,
        kDefaultBehaviorParams.slew, false },
    { kEventChokeParamId, "Choke", "Movement / Articulation", 0.0, 1.0,
        kDefaultBehaviorParams.choke, false },
    { kAuxABiasParamId, "Bias", "Aux Return A", -1.0, 1.0,
        kDefaultParams.aux[0].effect.bias, false },
    { kAuxBBiasParamId, "Bias", "Aux Return B", -1.0, 1.0,
        kDefaultParams.aux[1].effect.bias, false },
    { kReactModeParamId, "Mode", "Movement / Response", 0.0,
        static_cast<double>(s3g::NoInputReactMode::Count) - 1.0,
        static_cast<double>(kDefaultParams.reactMode), true },
    { kReactDepthParamId, "Depth", "Movement / Response", 0.0, 1.0,
        kDefaultParams.reactDepth, false },
    { kReactThresholdParamId, "Threshold", "Movement / Response", 0.0, 1.0,
        kDefaultParams.reactThreshold, false },
    { kReactAttackParamId, "Attack", "Movement / Response", 0.0, 1.0,
        kDefaultParams.reactAttack, false },
    { kReactReleaseParamId, "Release", "Movement / Response", 0.0, 1.0,
        kDefaultParams.reactRelease, false },
    { kReactPolarityParamId, "Direction", "Movement / Response", -1.0, 1.0,
        kDefaultParams.reactPolarity, true },
    { kControllerHoldParamId, "Hold Ecology", "Movement", 0.0, 1.0,
        static_cast<double>(kDefaultParams.controllerHold), true },
    { kSlowTimeParamId, "Slow Time", "Movement", 0.0, 1.0,
        static_cast<double>(kDefaultParams.slowTime), true },
    { kClockSyncParamId, "Tempo Sync", "Movement", 0.0, 1.0,
        static_cast<double>(kDefaultParams.clockSync), true },
    { kFieldDivisionParamId, "Field Division", "Movement", 0.0,
        static_cast<double>(s3g::kNoInputClockDivisionCount - 1u),
        static_cast<double>(kDefaultParams.fieldDivision), true },
    { kEventDivisionParamId, "Event Division", "Movement", 0.0,
        static_cast<double>(s3g::kNoInputClockDivisionCount - 1u),
        static_cast<double>(kDefaultParams.eventDivision), true },
    { kMatrixMidiModeParamId, "BU16 Mode", "MIDI / Matrix", 0.0,
        static_cast<double>(s3g::NoInputMatrixMidiMode::Count) - 1.0,
        static_cast<double>(s3g::NoInputMatrixMidiMode::Flip), true },
    { kMatrixMidiSignParamId, "BU16 New Sign", "MIDI / Matrix", 0.0,
        static_cast<double>(s3g::NoInputMatrixMidiSign::Count) - 1.0,
        static_cast<double>(s3g::NoInputMatrixMidiSign::Positive), true },
    { kMatrixMidiRampParamId, "BU16 Ramp", "MIDI / Matrix",
        s3g::kNoInputMatrixMidiRampMinimumMs,
        s3g::kNoInputMatrixMidiRampMaximumMs,
        s3g::kNoInputMatrixMidiRampDefaultMs, false },
    { kBehaviorDepthParamId, "Depth", "Movement / Behavior", 0.0, 1.0,
        kDefaultParams.motion, false },
    { kOutputFormatParamId, "Output Format", "Output", 0.0, 2.0,
        0.0, true },
    { kOutputRotationParamId, "Output Rotation", "Output", -180.0, 180.0,
        0.0, false },
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
    case kLaneTuneNoteOffset:
        range = { 24.0, 108.0, defaults.tuneNote, false }; return true;
    case kLaneTuneCentsOffset:
        range = { -100.0, 100.0, defaults.tuneCents, false }; return true;
    case kLanePitchLockOffset:
        range = { 0.0, 1.0, static_cast<double>(defaults.pitchLock), true };
        return true;
    case kLaneAuxTapAOffset:
    case kLaneAuxTapBOffset: {
        const uint32_t bus = offset == kLaneAuxTapAOffset ? 0u : 1u;
        range = { 0.0, static_cast<double>(s3g::NoInputAuxTap::Count) - 1.0,
            static_cast<double>(defaults.auxTap[bus]), true };
        return true;
    }
    case kLaneAuxReturnAOffset:
    case kLaneAuxReturnBOffset: {
        const uint32_t bus = offset == kLaneAuxReturnAOffset ? 0u : 1u;
        range = { -1.0, 1.0, defaults.auxReturn[bus], false };
        return true;
    }
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

struct NoInputSurfaceSnapshot {
    s3g::NoInputMixerParams params = s3g::defaultNoInputMixerParams();
    s3g::NoInputMovementBehaviorParams behavior {};
    std::array<uint32_t, 2u> auxMute {};
    float behaviorDepth = 0.0f;
};

using NoInputSurface = s3g::ParameterSurfaceState<NoInputSurfaceSnapshot>;

enum class NoInputSurfaceTopologyMode : uint32_t {
    Base = 0u,
    Cell,
    Count,
};

constexpr float kNoInputSurfaceDefaultFocus = 0.75f;
constexpr float kNoInputSurfaceDefaultGlideMs = 640.0f;
constexpr uint32_t kNoInputSurfaceNoTopologyCell = ~0u;
constexpr uint32_t kNoInputSurfaceControlFrames = 256u;

struct LegacyNoInputSurfaceSnapshotV8 {
    s3g::NoInputMixerParams params = s3g::defaultNoInputMixerParams();
    s3g::NoInputMovementBehaviorParams behavior {};
    std::array<uint32_t, 2u> auxMute {};
};

using LegacyNoInputSurfaceV8 =
    s3g::ParameterSurfaceState<LegacyNoInputSurfaceSnapshotV8>;

struct LegacyNoInputLaneParamsV4 {
    float body = 0.50f;
    float loss = 0.38f;
    float levelDb = -3.0f;
    uint32_t mute = 0u;
    float lowDb = 0.0f;
    float midFrequencyHz = 850.0f;
    float midGainDb = 0.0f;
    float highDb = 0.0f;
    std::array<float, 2u> auxSend {{ 0.0f, 0.0f }};
    std::array<s3g::NoInputInsertParams,
        s3g::kNoInputMixerInsertSlots> inserts {};
};

struct LegacyNoInputMixerParamsV4 {
    float outputGainDb = -18.0f;
    float ceilingDb = -1.0f;
    uint32_t limiterEnabled = 1u;
    uint32_t dcBlockEnabled = 1u;
    float feedback = 0.82f;
    float coupling = 0.42f;
    float phase = 0.34f;
    float drift = 0.18f;
    float formant = 0.30f;
    float agency = 0.28f;
    float space = 0.10f;
    float variance = 0.12f;
    float internalTone = 0.0f;
    float houseTone = -0.08f;
    float flow = 0.42f;
    float spread = 0.36f;
    float vortex = 0.0f;
    float motion = 0.0f;
    s3g::MatrixFlowShape motionShape = s3g::MatrixFlowShape::Flow;
    float motionRate = 0.15f;
    float motionPhase = 0.0f;
    uint32_t quality = 1u;
    uint32_t seed = 0x5455444fu;
    std::array<s3g::NoInputAuxParams, 2u> aux {};
    std::array<float, s3g::kNoInputMixerMatrixCells> matrix {};
    std::array<LegacyNoInputLaneParamsV4, kChannelCount> lanes {};
};

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::NoInputMixerParams params = s3g::defaultNoInputMixerParams();
    uint32_t selectedLane = 2u;
    uint32_t selectedSlot = 0u;
    uint32_t selectedSource = 2u;
    uint32_t selectedDestination = 2u;
    uint32_t guiPage = 0u;
    uint32_t matrixMidiMode = static_cast<uint32_t>(
        s3g::NoInputMatrixMidiMode::Flip);
    uint32_t matrixMidiSign = static_cast<uint32_t>(
        s3g::NoInputMatrixMidiSign::Positive);
    float matrixMidiRampMs = s3g::kNoInputMatrixMidiRampDefaultMs;
    std::array<uint32_t, 2u> auxMute {};
    s3g::NoInputMovementBehaviorParams behavior {};
    float behaviorDepth = 0.0f;
    NoInputSurface surface {};
    uint32_t surfaceTopologyMode = static_cast<uint32_t>(
        NoInputSurfaceTopologyMode::Base);
    uint32_t surfaceTopologyCell = kNoInputSurfaceNoTopologyCell;
    uint32_t outputFormat = 0u;
    float outputRotationDegrees = 0.0f;
};

struct Version14SavedState {
    uint32_t version = 14u;
    s3g::NoInputMixerParams params = s3g::defaultNoInputMixerParams();
    uint32_t selectedLane = 2u;
    uint32_t selectedSlot = 0u;
    uint32_t selectedSource = 2u;
    uint32_t selectedDestination = 2u;
    uint32_t guiPage = 0u;
    uint32_t matrixMidiMode = static_cast<uint32_t>(
        s3g::NoInputMatrixMidiMode::Flip);
    uint32_t matrixMidiSign = static_cast<uint32_t>(
        s3g::NoInputMatrixMidiSign::Positive);
    float matrixMidiRampMs = s3g::kNoInputMatrixMidiRampDefaultMs;
    std::array<uint32_t, 2u> auxMute {};
    s3g::NoInputMovementBehaviorParams behavior {};
    float behaviorDepth = 0.0f;
    NoInputSurface surface {};
    uint32_t surfaceTopologyMode = static_cast<uint32_t>(
        NoInputSurfaceTopologyMode::Base);
    uint32_t surfaceTopologyCell = kNoInputSurfaceNoTopologyCell;
};

// Version thirteen briefly stored two complete mixer engines and a blend
// control. Version fourteen migrates whichever side was dominant into the
// single direct-recall engine and discards those experimental fields.
struct Version13SavedState {
    uint32_t version = 13u;
    s3g::NoInputMixerParams params = s3g::defaultNoInputMixerParams();
    uint32_t selectedLane = 2u;
    uint32_t selectedSlot = 0u;
    uint32_t selectedSource = 2u;
    uint32_t selectedDestination = 2u;
    uint32_t guiPage = 0u;
    uint32_t matrixMidiMode = static_cast<uint32_t>(
        s3g::NoInputMatrixMidiMode::Flip);
    uint32_t matrixMidiSign = static_cast<uint32_t>(
        s3g::NoInputMatrixMidiSign::Positive);
    float matrixMidiRampMs = s3g::kNoInputMatrixMidiRampDefaultMs;
    std::array<uint32_t, 2u> auxMute {};
    s3g::NoInputMovementBehaviorParams behavior {};
    float behaviorDepth = 0.0f;
    NoInputSurface surface {};
    uint32_t surfaceTopologyMode = static_cast<uint32_t>(
        NoInputSurfaceTopologyMode::Base);
    uint32_t surfaceTopologyCell = kNoInputSurfaceNoTopologyCell;
    float retiredBlend = 0.0f;
    float retiredWarmupMs = 250.0f;
    std::array<NoInputSurfaceSnapshot, 2u> retiredStates {{
        NoInputSurfaceSnapshot {}, NoInputSurfaceSnapshot {},
    }};
    uint32_t retiredTargetIndex = 0u;
};

struct Version12SavedState {
    uint32_t version = 12u;
    s3g::NoInputMixerParams params = s3g::defaultNoInputMixerParams();
    uint32_t selectedLane = 2u;
    uint32_t selectedSlot = 0u;
    uint32_t selectedSource = 2u;
    uint32_t selectedDestination = 2u;
    uint32_t guiPage = 0u;
    uint32_t matrixMidiMode = static_cast<uint32_t>(
        s3g::NoInputMatrixMidiMode::Flip);
    uint32_t matrixMidiSign = static_cast<uint32_t>(
        s3g::NoInputMatrixMidiSign::Positive);
    float matrixMidiRampMs = s3g::kNoInputMatrixMidiRampDefaultMs;
    std::array<uint32_t, 2u> auxMute {};
    s3g::NoInputMovementBehaviorParams behavior {};
    float behaviorDepth = 0.0f;
    NoInputSurface surface {};
    uint32_t surfaceTopologyMode = static_cast<uint32_t>(
        NoInputSurfaceTopologyMode::Base);
    uint32_t surfaceTopologyCell = kNoInputSurfaceNoTopologyCell;
    float retiredDurationMs = 4000.0f;
    float retiredWarmupMs = 250.0f;
};

// Versions 10 and 11 carried the retired Parameter Surface state but no
// experimental transition controls. Preserve the core patch on migration and
// discard the Surface runtime behavior.
struct Version11SavedState {
    uint32_t version = 11u;
    s3g::NoInputMixerParams params = s3g::defaultNoInputMixerParams();
    uint32_t selectedLane = 2u;
    uint32_t selectedSlot = 0u;
    uint32_t selectedSource = 2u;
    uint32_t selectedDestination = 2u;
    uint32_t guiPage = 0u;
    uint32_t matrixMidiMode = static_cast<uint32_t>(
        s3g::NoInputMatrixMidiMode::Flip);
    uint32_t matrixMidiSign = static_cast<uint32_t>(
        s3g::NoInputMatrixMidiSign::Positive);
    float matrixMidiRampMs = s3g::kNoInputMatrixMidiRampDefaultMs;
    std::array<uint32_t, 2u> auxMute {};
    s3g::NoInputMovementBehaviorParams behavior {};
    float behaviorDepth = 0.0f;
    NoInputSurface surface {};
    uint32_t surfaceTopologyMode = static_cast<uint32_t>(
        NoInputSurfaceTopologyMode::Base);
    uint32_t surfaceTopologyCell = kNoInputSurfaceNoTopologyCell;
};

struct Version9SavedState {
    uint32_t version = 9u;
    s3g::NoInputMixerParams params = s3g::defaultNoInputMixerParams();
    uint32_t selectedLane = 2u;
    uint32_t selectedSlot = 0u;
    uint32_t selectedSource = 2u;
    uint32_t selectedDestination = 2u;
    uint32_t guiPage = 0u;
    uint32_t matrixMidiMode = static_cast<uint32_t>(
        s3g::NoInputMatrixMidiMode::Flip);
    uint32_t matrixMidiSign = static_cast<uint32_t>(
        s3g::NoInputMatrixMidiSign::Positive);
    float matrixMidiRampMs = s3g::kNoInputMatrixMidiRampDefaultMs;
    std::array<uint32_t, 2u> auxMute {};
    s3g::NoInputMovementBehaviorParams behavior {};
    float behaviorDepth = 0.0f;
    NoInputSurface surface {};
};

struct Version8SavedState {
    uint32_t version = 8u;
    s3g::NoInputMixerParams params = s3g::defaultNoInputMixerParams();
    uint32_t selectedLane = 2u;
    uint32_t selectedSlot = 0u;
    uint32_t selectedSource = 2u;
    uint32_t selectedDestination = 2u;
    uint32_t guiPage = 0u;
    uint32_t matrixMidiMode = static_cast<uint32_t>(
        s3g::NoInputMatrixMidiMode::Flip);
    uint32_t matrixMidiSign = static_cast<uint32_t>(
        s3g::NoInputMatrixMidiSign::Positive);
    float matrixMidiRampMs = s3g::kNoInputMatrixMidiRampDefaultMs;
    std::array<uint32_t, 2u> auxMute {};
    s3g::NoInputMovementBehaviorParams behavior {};
    LegacyNoInputSurfaceV8 surface {};
};

struct Version7SavedState {
    uint32_t version = 7u;
    s3g::NoInputMixerParams params = s3g::defaultNoInputMixerParams();
    uint32_t selectedLane = 2u;
    uint32_t selectedSlot = 0u;
    uint32_t selectedSource = 2u;
    uint32_t selectedDestination = 2u;
    uint32_t guiPage = 0u;
    uint32_t matrixMidiMode = static_cast<uint32_t>(
        s3g::NoInputMatrixMidiMode::Flip);
    uint32_t matrixMidiSign = static_cast<uint32_t>(
        s3g::NoInputMatrixMidiSign::Positive);
    std::array<uint32_t, 2u> auxMute {};
    s3g::NoInputMovementBehaviorParams behavior {};
    LegacyNoInputSurfaceV8 surface {};
};

struct Version6SavedState {
    uint32_t version = 6u;
    s3g::NoInputMixerParams params = s3g::defaultNoInputMixerParams();
    uint32_t selectedLane = 2u;
    uint32_t selectedSlot = 0u;
    uint32_t selectedSource = 2u;
    uint32_t selectedDestination = 2u;
    uint32_t guiPage = 0u;
    uint32_t matrixMidiMode = static_cast<uint32_t>(
        s3g::NoInputMatrixMidiMode::Flip);
    std::array<uint32_t, 2u> auxMute {};
    s3g::NoInputMovementBehaviorParams behavior {};
    LegacyNoInputSurfaceV8 surface {};
};

struct Version5SavedState {
    uint32_t version = 5u;
    s3g::NoInputMixerParams params = s3g::defaultNoInputMixerParams();
    uint32_t selectedLane = 2u;
    uint32_t selectedSlot = 0u;
    uint32_t selectedSource = 2u;
    uint32_t selectedDestination = 2u;
    uint32_t guiPage = 0u;
    std::array<uint32_t, 2u> auxMute {};
    s3g::NoInputMovementBehaviorParams behavior {};
    LegacyNoInputSurfaceV8 surface {};
};

struct Version4SavedState {
    uint32_t version = 4u;
    LegacyNoInputMixerParamsV4 params {};
    uint32_t selectedLane = 2u;
    uint32_t selectedSlot = 0u;
    uint32_t selectedSource = 2u;
    uint32_t selectedDestination = 2u;
    uint32_t guiPage = 0u;
    std::array<uint32_t, 2u> auxMute {};
    s3g::NoInputMovementBehaviorParams behavior {};
};

struct Version3SavedState {
    uint32_t version = 3u;
    LegacyNoInputMixerParamsV4 params {};
    uint32_t selectedLane = 2u;
    uint32_t selectedSlot = 0u;
    uint32_t selectedSource = 2u;
    uint32_t selectedDestination = 2u;
    uint32_t guiPage = 0u;
    std::array<uint32_t, 2u> auxMute {};
};

struct Version2SavedState {
    uint32_t version = 2u;
    LegacyNoInputMixerParamsV4 params {};
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

s3g::NoInputMixerParams migrateV4Params(
    const LegacyNoInputMixerParamsV4& source)
{
    auto destination = s3g::defaultNoInputMixerParams();
    destination.outputGainDb = source.outputGainDb;
    destination.ceilingDb = source.ceilingDb;
    destination.limiterEnabled = source.limiterEnabled;
    destination.dcBlockEnabled = source.dcBlockEnabled;
    destination.feedback = source.feedback;
    destination.coupling = source.coupling;
    destination.phase = source.phase;
    destination.drift = source.drift;
    destination.formant = source.formant;
    destination.agency = source.agency;
    destination.space = source.space;
    destination.variance = source.variance;
    destination.internalTone = source.internalTone;
    destination.houseTone = source.houseTone;
    destination.flow = source.flow;
    destination.spread = source.spread;
    destination.vortex = source.vortex;
    destination.motion = source.motion;
    destination.motionShape = source.motionShape;
    destination.motionRate = source.motionRate;
    destination.motionPhase = source.motionPhase;
    destination.quality = source.quality;
    destination.seed = source.seed;
    destination.aux = source.aux;
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
        newLane.auxSend = oldLane.auxSend;
        newLane.inserts = oldLane.inserts;
    }
    return destination;
}

NoInputSurface migrateLegacySurfaceV8(
    const LegacyNoInputSurfaceV8& source)
{
    NoInputSurface destination {};
    destination.enabled = source.enabled;
    destination.cellCount = std::min<uint32_t>(source.cellCount,
        s3g::kParameterSurfaceMaxCells);
    destination.focus = source.focus;
    destination.curve = source.curve;
    destination.glideMs = source.glideMs;
    for (uint32_t index = 0u; index < destination.cellCount; ++index) {
        const auto& oldCell = source.cells[index];
        auto& newCell = destination.cells[index];
        newCell.active = oldCell.active;
        newCell.presetIndex = oldCell.presetIndex;
        newCell.x = oldCell.x;
        newCell.y = oldCell.y;
        std::memcpy(newCell.name, oldCell.name, sizeof(newCell.name));
        newCell.params.params = oldCell.params.params;
        newCell.params.behavior = oldCell.params.behavior;
        newCell.params.auxMute = oldCell.params.auxMute;
        // Version 8 and earlier used Field Depth for both field motion and
        // articulation. Seed the independent depth from that value.
        newCell.params.behaviorDepth = oldCell.params.params.motion;
    }
    s3g::sanitizeParameterSurface(destination);
    return destination;
}

enum class GuiCommandType : uint8_t {
    ParamValue = 0u,
    FactoryPreset,
    RandomPatch,
    ForgetPatch,
    NewSeed,
    SetSeed,
    ClearMatrix,
    Panic,
    KillLane,
    ApplyUiSnapshot,
};

// GUI commands are intentionally compact and trivially copyable. Complete
// patches are described by their deterministic recipe; state loads publish a
// complete atomic UI snapshot and enqueue ApplyUiSnapshot as the commit point.
struct GuiCommand {
    GuiCommandType type = GuiCommandType::ParamValue;
    clap_id paramId = CLAP_INVALID_ID;
    double value = 0.0;
    uint32_t argument = 0u;
    uint32_t seed = 0u;
};

constexpr uint32_t kGuiCommandCapacity = 2048u;

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0u;
    s3g::NoInputMixerParams params = s3g::defaultNoInputMixerParams();
    s3g::NoInputMixerParams effectiveParams =
        s3g::defaultNoInputMixerParams();
    std::array<uint32_t, 2u> auxMute {};
    std::array<uint32_t, 2u> effectiveAuxMute {};
    s3g::NoInputMovementBehaviorParams behavior {};
    s3g::NoInputMovementBehaviorParams effectiveBehavior {};
    float behaviorDepth = 0.0f;
    float effectiveBehaviorDepth = 0.0f;
    NoInputSurface surface {};
    uint32_t surfaceTopologyMode = static_cast<uint32_t>(
        NoInputSurfaceTopologyMode::Base);
    uint32_t surfaceTopologyCell = kNoInputSurfaceNoTopologyCell;
    std::atomic<float> effectiveSurfaceX { 0.5f };
    std::atomic<float> effectiveSurfaceY { 0.5f };
    std::array<double, kTotalParamCount> modulation {};
    // Parameter values used by Cocoa and by host get_value/state-save calls.
    // They keep immediate GUI feedback without allowing the main thread to
    // read or write the audio thread's mutable patch structures.
    std::array<std::atomic<double>, kTotalParamCount> uiParamValue {};
    std::atomic<uint32_t> uiSeed { 0x5455444fu };
    std::array<GuiCommand, kGuiCommandCapacity> guiCommands {};
    std::atomic<uint32_t> guiCommandWrite { 0u };
    std::atomic<uint32_t> guiCommandRead { 0u };
    std::atomic<uint64_t> guiCommandDrops { 0u };
    std::atomic<bool> active { false };
    double transportTempoBpm = 120.0;
    bool transportHasTempo = false;
    s3g::NoInputMixer mixer;
    s3g::RingOutputMixdown outputMixdown;
    std::atomic<uint32_t> outputFormat { 0u };
    std::atomic<float> outputRotationDegrees { 0.0f };
    std::array<float, kChannelCount> frame {};
    std::array<std::atomic<float>,
        s3g::kNoInputMixerMatrixCells * kRouteScopeSamples> routeScope {};
    std::atomic<uint64_t> routeScopeSequence { 0u };
    uint32_t routeScopeDecimation = 2u;
    uint32_t routeScopeCountdown = 0u;
    uint32_t surfaceControlCountdown = 0u;
    std::atomic<float> outputPeak { 0.0f };
    std::array<std::atomic<float>, kChannelCount> lanePeaks {};
    std::array<std::atomic<float>, kChannelCount> laneActivity {};
    std::array<std::atomic<float>, 2u> auxActivity {};
    std::atomic<float> networkActivity { 0.0f };
    std::atomic<float> motionPhase { 0.0f };
    std::array<std::atomic<float>,
        s3g::kNoInputMixerMatrixCells> behaviorRouteGate {};
    std::array<std::atomic<float>,
        s3g::kNoInputMixerMatrixCells> reactRouteGate {};
    std::array<std::atomic<float>,
        s3g::kNoInputMixerMatrixCells> midiMatrixGridGain {};
    std::array<std::atomic<float>,
        s3g::kNoInputMixerMatrixCells> midiMatrixBaseGain {};
    std::array<std::atomic<bool>,
        s3g::kNoInputMixerMatrixCells> midiMatrixGridHeld {};
    std::array<std::atomic<bool>,
        s3g::kNoInputMixerMatrixCells> midiMatrixGridActive {};
    std::array<uint8_t,
        s3g::kNoInputMixerMatrixCells> midiMatrixLatchPeakVelocity {};
    std::array<uint8_t,
        s3g::kNoInputMixerMatrixCells> midiMatrixLatchSign {};
    std::array<uint32_t,
        s3g::kNoInputMixerMatrixCells> midiMatrixLatchCaptureFrames {};
    std::atomic<uint32_t> matrixMidiMode { static_cast<uint32_t>(
        s3g::NoInputMatrixMidiMode::Flip) };
    std::atomic<uint32_t> matrixMidiSign { static_cast<uint32_t>(
        s3g::NoInputMatrixMidiSign::Positive) };
    std::atomic<float> matrixMidiRampMs {
        s3g::kNoInputMatrixMidiRampDefaultMs };
    std::array<uint8_t,
        s3g::kNoInputMixerMatrixCells> matrixFeedbackValue {};
    std::array<bool,
        s3g::kNoInputMixerMatrixCells> matrixFeedbackSent {};
    uint64_t matrixFeedbackDirtyMask = ~uint64_t { 0u };
    uint64_t matrixFeedbackTrackedMask = 0u;
    std::array<uint16_t, kTotalParamCount> nrpnFeedbackValue {};
    std::array<bool, kTotalParamCount> nrpnFeedbackSent {};
    std::array<uint64_t, kNrpnFeedbackWordCount> nrpnFeedbackDirty {};
    uint32_t nrpnFeedbackWordCursor = 0u;
    std::atomic<bool> nrpnFeedbackEnabled { true };
    std::atomic<bool> matrixFeedbackEnabled { true };
    std::atomic<bool> feedbackConfigurationChanged { false };
    std::atomic<float> minimumGovernor { 1.0f };
    std::atomic<uint32_t> containmentState {
        static_cast<uint32_t>(s3g::NoInputContainmentState::Quiet) };
    std::atomic<uint32_t> selectedLane { 2u };
    std::atomic<uint32_t> selectedSlot { 0u };
    std::atomic<uint32_t> selectedSource { 2u };
    std::atomic<uint32_t> selectedDestination { 2u };
    std::atomic<uint32_t> guiPage { 0u };
    struct NrpnState {
        uint8_t parameterMsb = 127u;
        uint8_t parameterLsb = 127u;
        uint8_t dataMsb = 0u;
        uint8_t dataLsb = 0u;
        bool hasParameterMsb = false;
        bool hasParameterLsb = false;
        bool hasDataMsb = false;
    } nrpn {};
#if defined(__APPLE__)
    void* guiView = nullptr;
    bool guiVisible = false;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
#endif
};

s3g::NoInputMixer& controlMixer(Plugin& plugin)
{
    return plugin.mixer;
}

const s3g::NoInputMixer& controlMixer(const Plugin& plugin)
{
    return plugin.mixer;
}

Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}

void clearMidiMatrixGrid(Plugin& plugin)
{
    plugin.mixer.clearMidiMatrixConnections();
    plugin.matrixFeedbackTrackedMask = 0u;
    plugin.matrixFeedbackDirtyMask = ~uint64_t { 0u };
    for (uint32_t index = 0u;
         index < s3g::kNoInputMixerMatrixCells; ++index) {
        plugin.midiMatrixGridGain[index].store(0.0f,
            std::memory_order_relaxed);
        plugin.midiMatrixGridHeld[index].store(false,
            std::memory_order_relaxed);
        plugin.midiMatrixGridActive[index].store(false,
            std::memory_order_relaxed);
        plugin.midiMatrixLatchPeakVelocity[index] = 0u;
        plugin.midiMatrixLatchSign[index] = static_cast<uint8_t>(
            s3g::NoInputMatrixMidiSign::Positive);
        plugin.midiMatrixLatchCaptureFrames[index] = 0u;
    }
}

void advanceMidiMatrixLatchCapture(Plugin& plugin, uint32_t frames)
{
    for (auto& remaining : plugin.midiMatrixLatchCaptureFrames) {
        remaining = remaining > frames ? remaining - frames : 0u;
    }
}

float displayedMatrixGain(const Plugin& plugin, uint32_t index);

void refreshMidiMatrixOverlayState(Plugin& plugin)
{
    uint64_t tracked = plugin.matrixFeedbackTrackedMask;
    while (tracked != 0u) {
        const uint32_t index = static_cast<uint32_t>(
            __builtin_ctzll(tracked));
        const uint64_t bit = uint64_t { 1u } << index;
        tracked &= ~bit;
        const uint8_t previousValue =
            s3g::encodeNoInputMatrixFeedbackValue(
                displayedMatrixGain(plugin, index));
        const uint32_t destination = index / kChannelCount;
        const uint32_t source = index % kChannelCount;
        const auto& destinationMixer = controlMixer(plugin);
        const bool active = destinationMixer.midiMatrixConnectionActive(
            destination, source);
        plugin.midiMatrixGridActive[index].store(active,
            std::memory_order_relaxed);
        plugin.midiMatrixGridGain[index].store(active
                ? destinationMixer.effectiveMatrixGain(destination, source)
                : 0.0f,
            std::memory_order_relaxed);
        const uint8_t nextValue = s3g::encodeNoInputMatrixFeedbackValue(
            displayedMatrixGain(plugin, index));
        if (nextValue != previousValue)
            plugin.matrixFeedbackDirtyMask |= bit;
        if (!active && !plugin.midiMatrixGridHeld[index].load(
                std::memory_order_relaxed)) {
            plugin.matrixFeedbackTrackedMask &= ~bit;
        }
    }
}

float displayedMatrixGain(const Plugin& plugin, uint32_t index)
{
    if (index >= s3g::kNoInputMixerMatrixCells) return 0.0f;
    if (plugin.midiMatrixGridActive[index].load(
            std::memory_order_relaxed)) {
        return plugin.midiMatrixGridGain[index].load(
            std::memory_order_relaxed);
    }
    return static_cast<float>(plugin.uiParamValue[
        kGlobalParamCount + index].load(std::memory_order_relaxed));
}

struct RouteStageReadout {
    float base = 0.0f;
    float field = 1.0f;
    float behavior = 1.0f;
    float response = 1.0f;
    float effective = 0.0f;
    float choke = 1.0f;
};

RouteStageReadout routeStageReadout(const Plugin& plugin,
    const NoInputSurfaceSnapshot& visible,
    uint32_t source, uint32_t destination)
{
    RouteStageReadout result;
    if (source >= kChannelCount || destination >= kChannelCount)
        return result;
    const uint32_t selected = destination * kChannelCount + source;
    result.base = displayedMatrixGain(plugin, selected);
    auto weights = s3g::noInputMixerMotionWeights(visible.params,
        plugin.motionPhase.load(std::memory_order_relaxed));
    if (visible.behavior.behavior == s3g::NoInputMovementBehavior::Step) {
        for (uint32_t index = 0u; index < weights.size(); ++index) {
            const float stepped = plugin.behaviorRouteGate[index].load(
                std::memory_order_relaxed);
            weights[index] = s3g::lerp(weights[index], stepped,
                visible.behaviorDepth);
        }
    }
    float activePeak = 0.0f;
    uint32_t activeCount = 0u;
    for (uint32_t routeDestination = 0u;
         routeDestination < kChannelCount; ++routeDestination) {
        const uint32_t index = routeDestination * kChannelCount + source;
        if (std::abs(displayedMatrixGain(plugin, index)) <= 0.001f)
            continue;
        activePeak = std::max(activePeak, weights[index]);
        ++activeCount;
    }
    result.field = s3g::noInputMixerMotionGainScale(weights[selected],
        activePeak, activeCount, visible.params.motion);
    const auto behavior = visible.behavior.behavior;
    if (s3g::noInputMovementBehaviorUsesAmplitude(behavior)) {
        result.behavior = s3g::lerp(1.0f,
            plugin.behaviorRouteGate[selected].load(
                std::memory_order_relaxed), visible.behaviorDepth);
    }
    if (visible.params.reactMode != s3g::NoInputReactMode::Off
        && visible.params.reactDepth > 1.0e-6f) {
        const float response = 0.0316227766f
            + plugin.reactRouteGate[selected].load(
                std::memory_order_relaxed) * 0.9683772234f;
        result.response = s3g::lerp(1.0f, response,
            visible.params.reactDepth);
    }
    if (behavior != s3g::NoInputMovementBehavior::Glide
        && visible.behavior.choke > 1.0e-6f) {
        float laneGate = 0.0f;
        bool hasRoute = false;
        for (uint32_t routeSource = 0u;
             routeSource < kChannelCount; ++routeSource) {
            const uint32_t index = destination * kChannelCount + routeSource;
            if (std::abs(displayedMatrixGain(plugin, index)) <= 0.001f)
                continue;
            hasRoute = true;
            laneGate = std::max(laneGate,
                plugin.behaviorRouteGate[index].load(
                    std::memory_order_relaxed));
        }
        if (!hasRoute) laneGate = 1.0f;
        result.choke = s3g::lerp(1.0f, laneGate,
            visible.behavior.choke);
    }
    result.effective = std::abs(result.base) * result.field
        * result.behavior * result.response;
    return result;
}

bool emitMatrixFeedbackEvent(const clap_output_events_t* events,
    uint32_t time, uint8_t channel, uint8_t note, uint8_t value)
{
    if (!events || !events->try_push) return false;
    clap_event_midi_t event {};
    event.header.size = sizeof(event);
    event.header.time = time;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_MIDI;
    event.header.flags = CLAP_EVENT_DONT_RECORD;
    event.port_index = 0u;
    event.data[0] = static_cast<uint8_t>(
        s3g::kNoInputMatrixFeedbackCommand | channel);
    event.data[1] = note;
    event.data[2] = value;
    return events->try_push(events, &event.header);
}

void emitMatrixFeedback(Plugin& plugin,
    const clap_output_events_t* events, uint32_t time, uint32_t frames)
{
    (void)frames;
    if (!plugin.matrixFeedbackEnabled.load(std::memory_order_relaxed)
        || !events || !events->try_push) return;
    uint64_t dirty = plugin.matrixFeedbackDirtyMask;
    while (dirty != 0u) {
        const uint32_t index = static_cast<uint32_t>(
            __builtin_ctzll(dirty));
        const uint64_t bit = uint64_t { 1u } << index;
        dirty &= ~bit;
        const uint8_t value = s3g::encodeNoInputMatrixFeedbackValue(
            displayedMatrixGain(plugin, index));
        if (plugin.matrixFeedbackSent[index]
            && plugin.matrixFeedbackValue[index] == value) {
            plugin.matrixFeedbackDirtyMask &= ~bit;
            continue;
        }
        uint8_t channel = 0u;
        uint8_t note = 0u;
        if (!s3g::encodeNoInputMatrixGridPoint(
                index / kChannelCount, index % kChannelCount,
                channel, note)) {
            plugin.matrixFeedbackDirtyMask &= ~bit;
            continue;
        }
        if (!emitMatrixFeedbackEvent(events, time, channel, note, value)) {
            plugin.matrixFeedbackSent[index] = false;
            return;
        }
        plugin.matrixFeedbackValue[index] = value;
        plugin.matrixFeedbackSent[index] = true;
        plugin.matrixFeedbackDirtyMask &= ~bit;
    }
}

void syncMixerStateLegacy(Plugin& plugin)
{
    plugin.mixer.setMidiMatrixRampMs(plugin.matrixMidiRampMs.load(
        std::memory_order_relaxed));
    plugin.mixer.setParams(plugin.params);
    plugin.mixer.setMovementBehaviorParams(plugin.behavior);
    for (uint32_t bus = 0u; bus < 2u; ++bus) {
        plugin.mixer.setAuxMuted(bus, plugin.auxMute[bus] != 0u);
    }
}

void applyParamLegacy(Plugin& plugin, clap_id id, double value)
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
        case kAuxAMuteParamId:
        case kAuxBMuteParamId:
            plugin.auxMute[id == kAuxAMuteParamId ? 0u : 1u] =
                value >= 0.5 ? 1u : 0u;
            break;
        case kBehaviorParamId:
            plugin.behavior.behavior = static_cast<s3g::NoInputMovementBehavior>(
                static_cast<uint32_t>(std::lround(value)));
            break;
        case kEventRateParamId:
            plugin.behavior.eventRate = static_cast<float>(value); break;
        case kEventLengthParamId:
            plugin.behavior.length = static_cast<float>(value); break;
        case kEventDensityParamId:
            plugin.behavior.density = static_cast<float>(value); break;
        case kEventChaosParamId:
            plugin.behavior.chaos = static_cast<float>(value); break;
        case kEventSlewParamId:
            plugin.behavior.slew = static_cast<float>(value); break;
        case kEventChokeParamId:
            plugin.behavior.choke = static_cast<float>(value); break;
        case kAuxABiasParamId:
        case kAuxBBiasParamId:
            plugin.params.aux[id == kAuxABiasParamId ? 0u : 1u]
                .effect.bias = static_cast<float>(value);
            break;
        case kOutputFormatParamId:
            plugin.outputFormat.store(static_cast<uint32_t>(std::lround(value)),
                std::memory_order_relaxed);
            break;
        case kOutputRotationParamId:
            plugin.outputRotationDegrees.store(
                s3g::sanitizeRingOutputRotation(static_cast<float>(value)),
                std::memory_order_relaxed);
            break;
        default: break;
        }
        syncMixerStateLegacy(plugin);
        return;
    }

    uint32_t destination = 0u;
    uint32_t source = 0u;
    if (decodeMatrixParam(id, destination, source)) {
        plugin.params.matrix[destination * kChannelCount + source] =
            static_cast<float>(std::clamp(value, -1.0, 1.0));
        syncMixerStateLegacy(plugin);
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
    syncMixerStateLegacy(plugin);
}

bool paramValueLegacy(const Plugin& plugin, clap_id id, double& value)
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
    case kAuxAMuteParamId: value = plugin.auxMute[0]; return true;
    case kAuxBMuteParamId: value = plugin.auxMute[1]; return true;
    case kBehaviorParamId:
        value = static_cast<double>(plugin.behavior.behavior); return true;
    case kEventRateParamId: value = plugin.behavior.eventRate; return true;
    case kEventLengthParamId: value = plugin.behavior.length; return true;
    case kEventDensityParamId: value = plugin.behavior.density; return true;
    case kEventChaosParamId: value = plugin.behavior.chaos; return true;
    case kEventSlewParamId: value = plugin.behavior.slew; return true;
    case kEventChokeParamId: value = plugin.behavior.choke; return true;
    case kAuxABiasParamId:
        value = plugin.params.aux[0].effect.bias; return true;
    case kAuxBBiasParamId:
        value = plugin.params.aux[1].effect.bias; return true;
    case kOutputFormatParamId:
        value = plugin.outputFormat.load(std::memory_order_relaxed);
        return true;
    case kOutputRotationParamId:
        value = plugin.outputRotationDegrees.load(std::memory_order_relaxed);
        return true;
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

bool assignSnapshotParam(NoInputSurfaceSnapshot& snapshot,
    clap_id id, double value)
{
    ParamRange range;
    if (!paramRange(id, range)) return false;
    value = std::clamp(value, range.minimum, range.maximum);
    auto& params = snapshot.params;
    auto& behavior = snapshot.behavior;
    switch (id) {
    case kOutputParamId: params.outputGainDb = value; return true;
    case kCeilingParamId: params.ceilingDb = value; return true;
    case kLimiterParamId: params.limiterEnabled = value >= 0.5; return true;
    case kDcBlockParamId: params.dcBlockEnabled = value >= 0.5; return true;
    case kFeedbackParamId: params.feedback = value; return true;
    case kCouplingParamId: params.coupling = value; return true;
    case kPhaseParamId: params.phase = value; return true;
    case kDriftParamId: params.drift = value; return true;
    case kFormantParamId: params.formant = value; return true;
    case kQualityParamId: params.quality = std::lround(value); return true;
    case kAgencyParamId: params.agency = value; return true;
    case kSpaceParamId: params.space = value; return true;
    case kVarianceParamId: params.variance = value; return true;
    case kInternalToneParamId: params.internalTone = value; return true;
    case kHouseToneParamId: params.houseTone = value; return true;
    case kFlowParamId: params.flow = value; return true;
    case kSpreadParamId: params.spread = value; return true;
    case kVortexParamId: params.vortex = value; return true;
    case kMotionParamId: params.motion = value; return true;
    case kMotionShapeParamId:
        params.motionShape = s3g::matrixFlowShapeFromIndex(std::lround(value));
        return true;
    case kMotionRateParamId: params.motionRate = value; return true;
    case kMotionPhaseParamId: params.motionPhase = value; return true;
    case kAuxATypeParamId:
    case kAuxBTypeParamId: {
        const uint32_t bus = id == kAuxATypeParamId ? 0u : 1u;
        params.aux[bus].effect.type = static_cast<s3g::NoInputDistortionType>(
            static_cast<uint32_t>(std::lround(value)));
        return true;
    }
    case kAuxAGainParamId:
    case kAuxBGainParamId:
        params.aux[id == kAuxAGainParamId ? 0u : 1u].effect.gain = value;
        return true;
    case kAuxAToneParamId:
    case kAuxBToneParamId:
        params.aux[id == kAuxAToneParamId ? 0u : 1u].effect.tone = value;
        return true;
    case kAuxAReturnParamId:
    case kAuxBReturnParamId:
        params.aux[id == kAuxAReturnParamId ? 0u : 1u].returnGain = value;
        return true;
    case kAuxAFeedbackParamId:
    case kAuxBFeedbackParamId:
        params.aux[id == kAuxAFeedbackParamId ? 0u : 1u].feedback = value;
        return true;
    case kAuxAMuteParamId:
    case kAuxBMuteParamId:
        snapshot.auxMute[id == kAuxAMuteParamId ? 0u : 1u] = value >= 0.5;
        return true;
    case kBehaviorParamId:
        behavior.behavior = static_cast<s3g::NoInputMovementBehavior>(
            static_cast<uint32_t>(std::lround(value)));
        return true;
    case kEventRateParamId: behavior.eventRate = value; return true;
    case kEventLengthParamId: behavior.length = value; return true;
    case kEventDensityParamId: behavior.density = value; return true;
    case kEventChaosParamId: behavior.chaos = value; return true;
    case kEventSlewParamId: behavior.slew = value; return true;
    case kEventChokeParamId: behavior.choke = value; return true;
    case kBehaviorDepthParamId: snapshot.behaviorDepth = value; return true;
    case kAuxABiasParamId:
    case kAuxBBiasParamId:
        params.aux[id == kAuxABiasParamId ? 0u : 1u].effect.bias = value;
        return true;
    case kReactModeParamId:
        params.reactMode = static_cast<s3g::NoInputReactMode>(
            static_cast<uint32_t>(std::lround(value)));
        return true;
    case kReactDepthParamId: params.reactDepth = value; return true;
    case kReactThresholdParamId: params.reactThreshold = value; return true;
    case kReactAttackParamId: params.reactAttack = value; return true;
    case kReactReleaseParamId: params.reactRelease = value; return true;
    case kReactPolarityParamId: params.reactPolarity = value; return true;
    case kControllerHoldParamId: params.controllerHold = value >= 0.5; return true;
    case kSlowTimeParamId: params.slowTime = value >= 0.5; return true;
    case kClockSyncParamId: params.clockSync = value >= 0.5; return true;
    case kFieldDivisionParamId: params.fieldDivision = std::lround(value); return true;
    case kEventDivisionParamId: params.eventDivision = std::lround(value); return true;
    case kSurfaceXParamId: params.surfaceX = value; return true;
    case kSurfaceYParamId: params.surfaceY = value; return true;
    default: break;
    }

    uint32_t destination = 0u;
    uint32_t source = 0u;
    if (decodeMatrixParam(id, destination, source)) {
        params.matrix[destination * kChannelCount + source] = value;
        return true;
    }
    uint32_t lane = 0u;
    clap_id offset = 0u;
    if (!decodeLaneParam(id, lane, offset)) return false;
    auto& laneParams = params.lanes[lane];
    switch (offset) {
    case kLaneBodyOffset: laneParams.body = value; return true;
    case kLaneLossOffset: laneParams.loss = value; return true;
    case kLaneLevelOffset: laneParams.levelDb = value; return true;
    case kLaneMuteOffset: laneParams.mute = value >= 0.5; return true;
    case kLaneLowOffset: laneParams.lowDb = value; return true;
    case kLaneMidFrequencyOffset: laneParams.midFrequencyHz = value; return true;
    case kLaneMidGainOffset: laneParams.midGainDb = value; return true;
    case kLaneHighOffset: laneParams.highDb = value; return true;
    case kLaneAuxAOffset: laneParams.auxSend[0] = value; return true;
    case kLaneAuxBOffset: laneParams.auxSend[1] = value; return true;
    case kLaneTuneNoteOffset: laneParams.tuneNote = value; return true;
    case kLaneTuneCentsOffset: laneParams.tuneCents = value; return true;
    case kLanePitchLockOffset: laneParams.pitchLock = value >= 0.5; return true;
    case kLaneAuxTapAOffset:
    case kLaneAuxTapBOffset: {
        const uint32_t bus = offset == kLaneAuxTapAOffset ? 0u : 1u;
        laneParams.auxTap[bus] = static_cast<s3g::NoInputAuxTap>(
            static_cast<uint32_t>(std::lround(value)));
        return true;
    }
    case kLaneAuxReturnAOffset: laneParams.auxReturn[0] = value; return true;
    case kLaneAuxReturnBOffset: laneParams.auxReturn[1] = value; return true;
    default: break;
    }
    uint32_t slot = 0u;
    clap_id insertOffset = 0u;
    if (!decodeInsertOffset(offset, slot, insertOffset)) return false;
    auto& insert = laneParams.inserts[slot];
    switch (insertOffset) {
    case kInsertTypeOffset:
        insert.type = static_cast<s3g::NoInputDistortionType>(
            static_cast<uint32_t>(std::lround(value))); return true;
    case kInsertGainOffset: insert.gain = value; return true;
    case kInsertToneOffset: insert.tone = value; return true;
    case kInsertBiasOffset: insert.bias = value; return true;
    case kInsertLevelOffset: insert.levelDb = value; return true;
    case kInsertBypassOffset: insert.bypass = value >= 0.5; return true;
    default: return false;
    }
}

bool snapshotParamValue(const NoInputSurfaceSnapshot& snapshot,
    clap_id id, double& value)
{
    const auto& params = snapshot.params;
    const auto& behavior = snapshot.behavior;
    switch (id) {
    case kOutputParamId: value = params.outputGainDb; return true;
    case kCeilingParamId: value = params.ceilingDb; return true;
    case kLimiterParamId: value = params.limiterEnabled; return true;
    case kDcBlockParamId: value = params.dcBlockEnabled; return true;
    case kFeedbackParamId: value = params.feedback; return true;
    case kCouplingParamId: value = params.coupling; return true;
    case kPhaseParamId: value = params.phase; return true;
    case kDriftParamId: value = params.drift; return true;
    case kFormantParamId: value = params.formant; return true;
    case kQualityParamId: value = params.quality; return true;
    case kAgencyParamId: value = params.agency; return true;
    case kSpaceParamId: value = params.space; return true;
    case kVarianceParamId: value = params.variance; return true;
    case kInternalToneParamId: value = params.internalTone; return true;
    case kHouseToneParamId: value = params.houseTone; return true;
    case kFlowParamId: value = params.flow; return true;
    case kSpreadParamId: value = params.spread; return true;
    case kVortexParamId: value = params.vortex; return true;
    case kMotionParamId: value = params.motion; return true;
    case kMotionShapeParamId: value = static_cast<double>(params.motionShape); return true;
    case kMotionRateParamId: value = params.motionRate; return true;
    case kMotionPhaseParamId: value = params.motionPhase; return true;
    case kAuxATypeParamId: value = static_cast<double>(params.aux[0].effect.type); return true;
    case kAuxAGainParamId: value = params.aux[0].effect.gain; return true;
    case kAuxAToneParamId: value = params.aux[0].effect.tone; return true;
    case kAuxAReturnParamId: value = params.aux[0].returnGain; return true;
    case kAuxAFeedbackParamId: value = params.aux[0].feedback; return true;
    case kAuxBTypeParamId: value = static_cast<double>(params.aux[1].effect.type); return true;
    case kAuxBGainParamId: value = params.aux[1].effect.gain; return true;
    case kAuxBToneParamId: value = params.aux[1].effect.tone; return true;
    case kAuxBReturnParamId: value = params.aux[1].returnGain; return true;
    case kAuxBFeedbackParamId: value = params.aux[1].feedback; return true;
    case kAuxAMuteParamId: value = snapshot.auxMute[0]; return true;
    case kAuxBMuteParamId: value = snapshot.auxMute[1]; return true;
    case kBehaviorParamId: value = static_cast<double>(behavior.behavior); return true;
    case kEventRateParamId: value = behavior.eventRate; return true;
    case kEventLengthParamId: value = behavior.length; return true;
    case kEventDensityParamId: value = behavior.density; return true;
    case kEventChaosParamId: value = behavior.chaos; return true;
    case kEventSlewParamId: value = behavior.slew; return true;
    case kEventChokeParamId: value = behavior.choke; return true;
    case kBehaviorDepthParamId: value = snapshot.behaviorDepth; return true;
    case kAuxABiasParamId: value = params.aux[0].effect.bias; return true;
    case kAuxBBiasParamId: value = params.aux[1].effect.bias; return true;
    case kReactModeParamId: value = static_cast<double>(params.reactMode); return true;
    case kReactDepthParamId: value = params.reactDepth; return true;
    case kReactThresholdParamId: value = params.reactThreshold; return true;
    case kReactAttackParamId: value = params.reactAttack; return true;
    case kReactReleaseParamId: value = params.reactRelease; return true;
    case kReactPolarityParamId: value = params.reactPolarity; return true;
    case kControllerHoldParamId: value = params.controllerHold; return true;
    case kSlowTimeParamId: value = params.slowTime; return true;
    case kClockSyncParamId: value = params.clockSync; return true;
    case kFieldDivisionParamId: value = params.fieldDivision; return true;
    case kEventDivisionParamId: value = params.eventDivision; return true;
    case kSurfaceXParamId: value = params.surfaceX; return true;
    case kSurfaceYParamId: value = params.surfaceY; return true;
    default: break;
    }
    uint32_t destination = 0u;
    uint32_t source = 0u;
    if (decodeMatrixParam(id, destination, source)) {
        value = params.matrix[destination * kChannelCount + source];
        return true;
    }
    uint32_t lane = 0u;
    clap_id offset = 0u;
    if (!decodeLaneParam(id, lane, offset)) return false;
    const auto& laneParams = params.lanes[lane];
    switch (offset) {
    case kLaneBodyOffset: value = laneParams.body; return true;
    case kLaneLossOffset: value = laneParams.loss; return true;
    case kLaneLevelOffset: value = laneParams.levelDb; return true;
    case kLaneMuteOffset: value = laneParams.mute; return true;
    case kLaneLowOffset: value = laneParams.lowDb; return true;
    case kLaneMidFrequencyOffset: value = laneParams.midFrequencyHz; return true;
    case kLaneMidGainOffset: value = laneParams.midGainDb; return true;
    case kLaneHighOffset: value = laneParams.highDb; return true;
    case kLaneAuxAOffset: value = laneParams.auxSend[0]; return true;
    case kLaneAuxBOffset: value = laneParams.auxSend[1]; return true;
    case kLaneTuneNoteOffset: value = laneParams.tuneNote; return true;
    case kLaneTuneCentsOffset: value = laneParams.tuneCents; return true;
    case kLanePitchLockOffset: value = laneParams.pitchLock; return true;
    case kLaneAuxTapAOffset: value = static_cast<double>(laneParams.auxTap[0]); return true;
    case kLaneAuxTapBOffset: value = static_cast<double>(laneParams.auxTap[1]); return true;
    case kLaneAuxReturnAOffset: value = laneParams.auxReturn[0]; return true;
    case kLaneAuxReturnBOffset: value = laneParams.auxReturn[1]; return true;
    default: break;
    }
    uint32_t slot = 0u;
    clap_id insertOffset = 0u;
    if (!decodeInsertOffset(offset, slot, insertOffset)) return false;
    const auto& insert = laneParams.inserts[slot];
    switch (insertOffset) {
    case kInsertTypeOffset: value = static_cast<double>(insert.type); return true;
    case kInsertGainOffset: value = insert.gain; return true;
    case kInsertToneOffset: value = insert.tone; return true;
    case kInsertBiasOffset: value = insert.bias; return true;
    case kInsertLevelOffset: value = insert.levelDb; return true;
    case kInsertBypassOffset: value = insert.bypass; return true;
    default: return false;
    }
}

NoInputSurfaceSnapshot baseSnapshot(const Plugin& plugin)
{
    return { plugin.params, plugin.behavior, plugin.auxMute,
        plugin.behaviorDepth };
}

bool surfaceKeepsLive(clap_id id)
{
    if (id == kOutputParamId || id == kCeilingParamId
        || id == kLimiterParamId || id == kDcBlockParamId
        || id == kOutputFormatParamId || id == kOutputRotationParamId
        || id == kQualityParamId || id == kVarianceParamId
        || id == kHouseToneParamId || id == kAuxAMuteParamId
        || id == kAuxBMuteParamId || id == kSurfaceXParamId
        || id == kSurfaceYParamId) return true;
    uint32_t lane = 0u;
    clap_id offset = 0u;
    return decodeLaneParam(id, lane, offset)
        && (offset == kLaneLevelOffset || offset == kLaneMuteOffset);
}

bool surfaceMutates(clap_id id)
{
    uint32_t destination = 0u;
    uint32_t source = 0u;
    if (decodeMatrixParam(id, destination, source)) return true;

    switch (id) {
    case kDriftParamId:
    case kFormantParamId:
    case kAgencyParamId:
    case kSpaceParamId:
    case kInternalToneParamId:
    case kFlowParamId:
    case kSpreadParamId:
    case kVortexParamId:
    case kMotionParamId:
    case kMotionRateParamId:
    case kMotionPhaseParamId:
    case kAuxAReturnParamId:
    case kAuxBReturnParamId:
    case kEventRateParamId:
    case kEventLengthParamId:
    case kEventDensityParamId:
    case kEventChaosParamId:
    case kEventSlewParamId:
    case kEventChokeParamId:
    case kReactDepthParamId:
    case kReactThresholdParamId:
    case kReactAttackParamId:
    case kReactReleaseParamId:
    case kBehaviorDepthParamId:
        return true;
    default:
        break;
    }

    uint32_t lane = 0u;
    clap_id offset = 0u;
    if (!decodeLaneParam(id, lane, offset)) return false;
    return offset == kLaneAuxAOffset || offset == kLaneAuxBOffset
        || offset == kLaneAuxReturnAOffset
        || offset == kLaneAuxReturnBOffset;
}

NoInputSurfaceSnapshot surfaceSnapshot(const Plugin& plugin,
    float cursorX, float cursorY)
{
    const auto base = baseSnapshot(plugin);
    if (!plugin.surface.enabled || plugin.surface.cellCount < 2u) return base;
    const auto weights = s3g::parameterSurfaceWeights(
        plugin.surface, cursorX, cursorY);
    if (weights.activeCount < 2u) return base;
    auto result = base;
    if (plugin.surfaceTopologyMode == static_cast<uint32_t>(
            NoInputSurfaceTopologyMode::Cell)
        && plugin.surfaceTopologyCell < plugin.surface.cellCount
        && plugin.surface.cells[plugin.surfaceTopologyCell].active) {
        result = plugin.surface.cells[plugin.surfaceTopologyCell].params;
    }
    for (uint32_t index = 0u; index < kTotalParamCount; ++index) {
        const clap_id id = paramIdAtIndex(index);
        if (surfaceKeepsLive(id)) {
            double baseValue = 0.0;
            if (!snapshotParamValue(base, id, baseValue)) continue;
            assignSnapshotParam(result, id, baseValue);
        } else if (surfaceMutates(id)) {
            ParamRange range;
            double baseValue = 0.0;
            if (!paramRange(id, range) || range.stepped
                || !snapshotParamValue(base, id, baseValue)) continue;
            const float blended = s3g::parameterSurfaceBlend(
                plugin.surface, weights,
                [id](const NoInputSurfaceSnapshot& cell) {
                    double cellValue = 0.0;
                    snapshotParamValue(cell, id, cellValue);
                    return static_cast<float>(cellValue);
                }, static_cast<float>(baseValue));
            assignSnapshotParam(result, id, blended);
        }
    }
    return result;
}

int32_t paramIndexForId(clap_id id)
{
    for (uint32_t index = 0u; index < kTotalParamCount; ++index) {
        if (paramIdAtIndex(index) == id) return static_cast<int32_t>(index);
    }
    return -1;
}

bool uiParameterValue(const Plugin& plugin, clap_id id, double& value)
{
    const int32_t index = paramIndexForId(id);
    if (index < 0) return false;
    value = plugin.uiParamValue[static_cast<uint32_t>(index)].load(
        std::memory_order_relaxed);
    return true;
}

NoInputSurfaceSnapshot uiSnapshot(const Plugin& plugin)
{
    NoInputSurfaceSnapshot snapshot {};
    snapshot.params.seed = plugin.uiSeed.load(std::memory_order_relaxed);
    for (uint32_t index = 0u; index < kTotalParamCount; ++index) {
        const clap_id id = paramIdAtIndex(index);
        if (id == CLAP_INVALID_ID) continue;
        assignSnapshotParam(snapshot, id,
            plugin.uiParamValue[index].load(std::memory_order_relaxed));
    }
    return snapshot;
}

void publishUiParameter(Plugin& plugin, clap_id id, double value)
{
    const int32_t index = paramIndexForId(id);
    ParamRange range;
    if (index < 0 || !paramRange(id, range)) return;
    value = std::clamp(std::isfinite(value) ? value : range.defaultValue,
        range.minimum, range.maximum);
    if (range.stepped) value = std::round(value);
    plugin.uiParamValue[static_cast<uint32_t>(index)].store(value,
        std::memory_order_relaxed);
}

void publishUiSnapshot(Plugin& plugin,
    const NoInputSurfaceSnapshot& snapshot)
{
    plugin.uiSeed.store(snapshot.params.seed, std::memory_order_relaxed);
    for (uint32_t index = 0u; index < kTotalParamCount; ++index) {
        const clap_id id = paramIdAtIndex(index);
        double value = 0.0;
        if (id != CLAP_INVALID_ID
            && snapshotParamValue(snapshot, id, value)) {
            plugin.uiParamValue[index].store(value,
                std::memory_order_relaxed);
        }
    }
}

void publishUiBaseState(Plugin& plugin)
{
    publishUiSnapshot(plugin, baseSnapshot(plugin));
    publishUiParameter(plugin, kMatrixMidiModeParamId,
        plugin.matrixMidiMode.load(std::memory_order_relaxed));
    publishUiParameter(plugin, kMatrixMidiSignParamId,
        plugin.matrixMidiSign.load(std::memory_order_relaxed));
    publishUiParameter(plugin, kMatrixMidiRampParamId,
        plugin.matrixMidiRampMs.load(std::memory_order_relaxed));
    publishUiParameter(plugin, kOutputFormatParamId,
        plugin.outputFormat.load(std::memory_order_relaxed));
    publishUiParameter(plugin, kOutputRotationParamId,
        plugin.outputRotationDegrees.load(std::memory_order_relaxed));
}

void requestGuiCommandDrain(Plugin& plugin)
{
    if (!plugin.host) return;
    if (plugin.host->request_process) plugin.host->request_process(plugin.host);
    if (!plugin.host->get_extension) return;
    const auto* hostParams = static_cast<const clap_host_params_t*>(
        plugin.host->get_extension(plugin.host, CLAP_EXT_PARAMS));
    if (hostParams && hostParams->request_flush)
        hostParams->request_flush(plugin.host);
}

bool enqueueGuiCommand(Plugin& plugin, const GuiCommand& command)
{
    const uint32_t write = plugin.guiCommandWrite.load(
        std::memory_order_relaxed);
    const uint32_t read = plugin.guiCommandRead.load(
        std::memory_order_acquire);
    if (write - read >= kGuiCommandCapacity) {
        plugin.guiCommandDrops.fetch_add(1u, std::memory_order_relaxed);
        requestGuiCommandDrain(plugin);
        return false;
    }
    plugin.guiCommands[write % kGuiCommandCapacity] = command;
    plugin.guiCommandWrite.store(write + 1u, std::memory_order_release);
    requestGuiCommandDrain(plugin);
    return true;
}

bool dequeueGuiCommand(Plugin& plugin, GuiCommand& command)
{
    const uint32_t read = plugin.guiCommandRead.load(
        std::memory_order_relaxed);
    const uint32_t write = plugin.guiCommandWrite.load(
        std::memory_order_acquire);
    if (read == write) return false;
    command = plugin.guiCommands[read % kGuiCommandCapacity];
    plugin.guiCommandRead.store(read + 1u, std::memory_order_release);
    return true;
}

void markNrpnFeedbackIndexDirty(Plugin& plugin, uint32_t index)
{
    if (index >= kTotalParamCount) return;
    plugin.nrpnFeedbackDirty[index / 64u] |=
        uint64_t { 1u } << (index % 64u);
}

void markNrpnFeedbackDirty(Plugin& plugin, clap_id id)
{
    const int32_t index = paramIndexForId(id);
    if (index >= 0)
        markNrpnFeedbackIndexDirty(plugin, static_cast<uint32_t>(index));
}

void markAllNrpnFeedbackDirty(Plugin& plugin)
{
    plugin.nrpnFeedbackDirty.fill(~uint64_t { 0u });
    constexpr uint32_t remainder = kTotalParamCount % 64u;
    if constexpr (remainder != 0u) {
        plugin.nrpnFeedbackDirty.back() =
            (uint64_t { 1u } << remainder) - 1u;
    }
    plugin.nrpnFeedbackWordCursor = 0u;
}

void clearNrpnFeedbackDirty(Plugin& plugin, uint32_t index)
{
    if (index >= kTotalParamCount) return;
    plugin.nrpnFeedbackDirty[index / 64u] &=
        ~(uint64_t { 1u } << (index % 64u));
}

void markMatrixFeedbackDirty(Plugin& plugin, uint32_t index)
{
    if (index < s3g::kNoInputMixerMatrixCells)
        plugin.matrixFeedbackDirtyMask |= uint64_t { 1u } << index;
}

void markAllMatrixFeedbackDirty(Plugin& plugin)
{
    plugin.matrixFeedbackDirtyMask = ~uint64_t { 0u };
}

void trackMatrixFeedback(Plugin& plugin, uint32_t index)
{
    if (index < s3g::kNoInputMixerMatrixCells)
        plugin.matrixFeedbackTrackedMask |= uint64_t { 1u } << index;
}

double modulatedBaseValue(const Plugin& plugin, clap_id id)
{
    double value = 0.0;
    snapshotParamValue(baseSnapshot(plugin), id, value);
    const int32_t index = paramIndexForId(id);
    ParamRange range;
    if (index >= 0 && paramRange(id, range) && !range.stepped) {
        value = std::clamp(value + plugin.modulation[
            static_cast<uint32_t>(index)], range.minimum, range.maximum);
    }
    return value;
}

void snapSurfaceCursor(Plugin& plugin)
{
    plugin.effectiveSurfaceX.store(static_cast<float>(
        modulatedBaseValue(plugin, kSurfaceXParamId)),
        std::memory_order_relaxed);
    plugin.effectiveSurfaceY.store(static_cast<float>(
        modulatedBaseValue(plugin, kSurfaceYParamId)),
        std::memory_order_relaxed);
}

bool advanceSurfaceCursor(Plugin& plugin, float deltaSeconds)
{
    const float currentX = plugin.effectiveSurfaceX.load(std::memory_order_relaxed);
    const float currentY = plugin.effectiveSurfaceY.load(std::memory_order_relaxed);
    const float targetX = static_cast<float>(modulatedBaseValue(plugin, kSurfaceXParamId));
    const float targetY = static_cast<float>(modulatedBaseValue(plugin, kSurfaceYParamId));
    const float glideMs = plugin.surface.enabled && plugin.surface.cellCount >= 2u
        ? plugin.surface.glideMs : 0.0f;
    const float nextX = s3g::parameterSurfaceGlideValue(
        currentX, targetX, glideMs, deltaSeconds);
    const float nextY = s3g::parameterSurfaceGlideValue(
        currentY, targetY, glideMs, deltaSeconds);
    plugin.effectiveSurfaceX.store(nextX, std::memory_order_relaxed);
    plugin.effectiveSurfaceY.store(nextY, std::memory_order_relaxed);
    return std::fabs(nextX - currentX) > 1.0e-7f
        || std::fabs(nextY - currentY) > 1.0e-7f;
}

void syncMixerState(Plugin& plugin)
{
    auto effective = baseSnapshot(plugin);
    for (uint32_t index = 0u; index < kTotalParamCount; ++index) {
        const double amount = plugin.modulation[index];
        if (amount == 0.0) continue;
        const clap_id id = paramIdAtIndex(index);
        ParamRange range;
        if (!paramRange(id, range) || range.stepped) continue;
        double value = 0.0;
        if (!snapshotParamValue(effective, id, value)) continue;
        assignSnapshotParam(effective, id,
            std::clamp(value + amount, range.minimum, range.maximum));
    }
    plugin.effectiveParams = s3g::sanitizeNoInputMixerParams(effective.params);
    for (uint32_t index = 0u;
         index < s3g::kNoInputMixerMatrixCells; ++index) {
        plugin.midiMatrixBaseGain[index].store(
            plugin.effectiveParams.matrix[index],
            std::memory_order_relaxed);
    }
    plugin.effectiveBehavior = s3g::sanitizeNoInputMovementBehaviorParams(
        effective.behavior);
    plugin.effectiveBehaviorDepth = std::clamp(
        std::isfinite(effective.behaviorDepth)
            ? effective.behaviorDepth : 0.0f, 0.0f, 1.0f);
    plugin.effectiveAuxMute = effective.auxMute;
    auto& destination = controlMixer(plugin);
    destination.setMidiMatrixRampMs(plugin.matrixMidiRampMs.load(
        std::memory_order_relaxed));
    destination.setParameterSurfaceMutationEnabled(false);
    destination.setParams(plugin.effectiveParams);
    destination.setMovementBehaviorParams(plugin.effectiveBehavior);
    destination.setMovementBehaviorDepth(plugin.effectiveBehaviorDepth);
    destination.setTransport(plugin.transportTempoBpm,
        plugin.transportHasTempo);
    for (uint32_t bus = 0u; bus < 2u; ++bus) {
        destination.setAuxMuted(bus, plugin.effectiveAuxMute[bus] != 0u);
    }
}

void applyParam(Plugin& plugin, clap_id id, double value,
    bool publishUi = true, bool synchronize = true)
{
    ParamRange publishedRange;
    if (!paramRange(id, publishedRange)) return;
    if (id == kMatrixMidiModeParamId) {
        const uint32_t mode = static_cast<uint32_t>(std::clamp(
            std::lround(value), 0l,
            static_cast<long>(s3g::NoInputMatrixMidiMode::Count) - 1l));
        clearMidiMatrixGrid(plugin);
        plugin.matrixMidiMode.store(mode, std::memory_order_relaxed);
        plugin.matrixFeedbackSent.fill(false);
        markAllMatrixFeedbackDirty(plugin);
        markNrpnFeedbackDirty(plugin, id);
        if (publishUi) publishUiParameter(plugin, id, mode);
        if (synchronize) syncMixerState(plugin);
        return;
    }
    if (id == kMatrixMidiSignParamId) {
        const uint32_t sign = static_cast<uint32_t>(std::clamp(
            std::lround(value), 0l,
            static_cast<long>(s3g::NoInputMatrixMidiSign::Count) - 1l));
        plugin.matrixMidiSign.store(sign, std::memory_order_relaxed);
        markNrpnFeedbackDirty(plugin, id);
        if (publishUi) publishUiParameter(plugin, id, sign);
        return;
    }
    if (id == kMatrixMidiRampParamId) {
        const double finiteValue = std::isfinite(value) ? value
            : static_cast<double>(s3g::kNoInputMatrixMidiRampDefaultMs);
        const float milliseconds = static_cast<float>(std::clamp(
            finiteValue, static_cast<double>(
                s3g::kNoInputMatrixMidiRampMinimumMs), static_cast<double>(
                s3g::kNoInputMatrixMidiRampMaximumMs)));
        plugin.matrixMidiRampMs.store(milliseconds,
            std::memory_order_relaxed);
        plugin.mixer.setMidiMatrixRampMs(milliseconds);
        markNrpnFeedbackDirty(plugin, id);
        if (publishUi) publishUiParameter(plugin, id, milliseconds);
        return;
    }
    if (id == kOutputFormatParamId) {
        const uint32_t format = static_cast<uint32_t>(std::clamp(
            std::lround(value), 0l, 2l));
        plugin.outputFormat.store(format, std::memory_order_relaxed);
        markNrpnFeedbackDirty(plugin, id);
        if (publishUi) publishUiParameter(plugin, id, format);
        return;
    }
    if (id == kOutputRotationParamId) {
        const float rotation = s3g::sanitizeRingOutputRotation(
            static_cast<float>(value));
        plugin.outputRotationDegrees.store(rotation,
            std::memory_order_relaxed);
        markNrpnFeedbackDirty(plugin, id);
        if (publishUi) publishUiParameter(plugin, id, rotation);
        return;
    }
    auto snapshot = baseSnapshot(plugin);
    if (!assignSnapshotParam(snapshot, id, value)) return;
    plugin.params = snapshot.params;
    plugin.behavior = snapshot.behavior;
    plugin.auxMute = snapshot.auxMute;
    plugin.behaviorDepth = snapshot.behaviorDepth;
    if (synchronize) syncMixerState(plugin);
    markNrpnFeedbackDirty(plugin, id);
    uint32_t destination = 0u;
    uint32_t source = 0u;
    if (decodeMatrixParam(id, destination, source))
        markMatrixFeedbackDirty(plugin,
            destination * kChannelCount + source);
    if (publishUi) {
        double applied = 0.0;
        if (snapshotParamValue(baseSnapshot(plugin), id, applied))
            publishUiParameter(plugin, id, applied);
    }
}

bool paramValue(const Plugin& plugin, clap_id id, double& value)
{
    ParamRange publishedRange;
    if (!paramRange(id, publishedRange)) return false;
    if (id == kMatrixMidiModeParamId) {
        value = plugin.matrixMidiMode.load(std::memory_order_relaxed);
        return true;
    }
    if (id == kMatrixMidiSignParamId) {
        value = plugin.matrixMidiSign.load(std::memory_order_relaxed);
        return true;
    }
    if (id == kMatrixMidiRampParamId) {
        value = plugin.matrixMidiRampMs.load(std::memory_order_relaxed);
        return true;
    }
    if (id == kOutputFormatParamId) {
        value = plugin.outputFormat.load(std::memory_order_relaxed);
        return true;
    }
    if (id == kOutputRotationParamId) {
        value = plugin.outputRotationDegrees.load(std::memory_order_relaxed);
        return true;
    }
    return snapshotParamValue(baseSnapshot(plugin), id, value);
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
    for (auto& gate : plugin.behaviorRouteGate) {
        gate.store(1.0f, std::memory_order_relaxed);
    }
    for (auto& gate : plugin.reactRouteGate) {
        gate.store(1.0f, std::memory_order_relaxed);
    }
    for (auto& activity : plugin.auxActivity) {
        activity.store(0.0f, std::memory_order_relaxed);
    }
    plugin.minimumGovernor.store(1.0f, std::memory_order_relaxed);
    plugin.containmentState.store(
        static_cast<uint32_t>(s3g::NoInputContainmentState::Quiet),
        std::memory_order_relaxed);
}

void applyCompletePatch(Plugin& plugin, s3g::NoInputMixerParams params,
    float seedAmount, bool publishUi = true)
{
    params.outputGainDb = plugin.params.outputGainDb;
    plugin.params = s3g::sanitizeNoInputMixerParams(params);
    plugin.nrpnFeedbackSent.fill(false);
    markAllNrpnFeedbackDirty(plugin);
    clearMidiMatrixGrid(plugin);
    plugin.matrixFeedbackSent.fill(false);
    syncMixerState(plugin);
    // Complete patch recall deliberately starts a fresh network. Parameter
    // edits and state restores keep their normal DSP ramps.
    plugin.mixer.reset();
    plugin.mixer.reseed(plugin.params.seed, seedAmount);
    resetMeters(plugin);
    if (publishUi) publishUiBaseState(plugin);
}

void resetNrpnState(Plugin& plugin)
{
    plugin.nrpn = {};
}

bool emitMidiParamValue(const clap_output_events_t* events,
    uint32_t time, clap_id id, double value)
{
    if (!events || !events->try_push) return false;
    clap_event_param_value_t event {};
    event.header.size = sizeof(event);
    event.header.time = time;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_PARAM_VALUE;
    event.header.flags = CLAP_EVENT_IS_LIVE | CLAP_EVENT_DONT_RECORD;
    event.param_id = id;
    event.note_id = -1;
    event.port_index = -1;
    event.channel = -1;
    event.key = -1;
    event.value = value;
    return events->try_push(events, &event.header);
}

void emitAllMidiParamValues(const Plugin& plugin,
    const clap_output_events_t* events, uint32_t time)
{
    for (uint32_t index = 0u; index < kTotalParamCount; ++index) {
        const clap_id id = paramIdAtIndex(index);
        double value = 0.0;
        if (id != CLAP_INVALID_ID && paramValue(plugin, id, value)) {
            emitMidiParamValue(events, time, id, value);
        }
    }
}

double midiParamValueFrom14Bit(clap_id id, uint16_t rawValue,
    const ParamRange& range)
{
    const double normalized = static_cast<double>(rawValue)
        / 16383.0;
    double value = range.minimum
        + normalized * (range.maximum - range.minimum);
    uint32_t lane = 0u;
    clap_id offset = 0u;
    if (decodeLaneParam(id, lane, offset)
        && offset == kLaneMidFrequencyOffset
        && range.minimum > 0.0 && range.maximum > range.minimum) {
        value = range.minimum * std::pow(
            range.maximum / range.minimum, normalized);
    }
    if (range.stepped) value = std::round(value);
    return std::clamp(value, range.minimum, range.maximum);
}

uint16_t midiParamValueTo14Bit(clap_id id, double value,
    const ParamRange& range)
{
    value = std::clamp(value, range.minimum, range.maximum);
    double normalized = range.maximum > range.minimum
        ? (value - range.minimum) / (range.maximum - range.minimum)
        : 0.0;
    uint32_t lane = 0u;
    clap_id offset = 0u;
    if (decodeLaneParam(id, lane, offset)
        && offset == kLaneMidFrequencyOffset
        && range.minimum > 0.0 && range.maximum > range.minimum) {
        normalized = std::log(value / range.minimum)
            / std::log(range.maximum / range.minimum);
    }
    return static_cast<uint16_t>(std::clamp<long>(
        std::lround(std::clamp(normalized, 0.0, 1.0) * 16383.0),
        0l, 16383l));
}

void markNrpnFeedbackSent(Plugin& plugin, clap_id id, double value)
{
    const int32_t index = paramIndexForId(id);
    ParamRange range;
    if (index < 0 || !paramRange(id, range)) return;
    plugin.nrpnFeedbackValue[static_cast<uint32_t>(index)] =
        midiParamValueTo14Bit(id, value, range);
    plugin.nrpnFeedbackSent[static_cast<uint32_t>(index)] = true;
    clearNrpnFeedbackDirty(plugin, static_cast<uint32_t>(index));
}

bool emitNrpnFeedbackCc(const clap_output_events_t* events,
    uint32_t time, uint8_t controller, uint8_t value)
{
    if (!events || !events->try_push) return false;
    clap_event_midi_t event {};
    event.header.size = sizeof(event);
    event.header.time = time;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_MIDI;
    event.header.flags = CLAP_EVENT_DONT_RECORD;
    event.port_index = 0u;
    event.data[0] = static_cast<uint8_t>(0xb0u | kMidiControlChannel);
    event.data[1] = controller;
    event.data[2] = value;
    return events->try_push(events, &event.header);
}

bool emitNrpnFeedbackValue(const clap_output_events_t* events,
    uint32_t time, clap_id id, uint16_t value)
{
    return emitNrpnFeedbackCc(events, time, 99u,
            static_cast<uint8_t>((id >> 7u) & 0x7fu))
        && emitNrpnFeedbackCc(events, time, 98u,
            static_cast<uint8_t>(id & 0x7fu))
        && emitNrpnFeedbackCc(events, time, 6u,
            static_cast<uint8_t>((value >> 7u) & 0x7fu))
        && emitNrpnFeedbackCc(events, time, 38u,
            static_cast<uint8_t>(value & 0x7fu));
}

void emitNrpnFeedback(Plugin& plugin,
    const clap_output_events_t* events, uint32_t time)
{
    if (!plugin.nrpnFeedbackEnabled.load(std::memory_order_relaxed)
        || !events || !events->try_push) return;
    uint32_t emitted = 0u;
    uint32_t visitedWords = 0u;
    while (visitedWords < kNrpnFeedbackWordCount
        && emitted < kNrpnFeedbackParamsPerBlock) {
        const uint32_t word = plugin.nrpnFeedbackWordCursor
            % kNrpnFeedbackWordCount;
        uint64_t dirty = plugin.nrpnFeedbackDirty[word];
        while (dirty != 0u && emitted < kNrpnFeedbackParamsPerBlock) {
            const uint32_t bitIndex = static_cast<uint32_t>(
                __builtin_ctzll(dirty));
            const uint64_t bit = uint64_t { 1u } << bitIndex;
            const uint32_t index = word * 64u + bitIndex;
            dirty &= ~bit;
            if (index >= kTotalParamCount) {
                plugin.nrpnFeedbackDirty[word] &= ~bit;
                continue;
            }
            const clap_id id = paramIdAtIndex(index);
            double value = 0.0;
            ParamRange range;
            if (id == CLAP_INVALID_ID || !paramValue(plugin, id, value)
                || !paramRange(id, range)) {
                plugin.nrpnFeedbackDirty[word] &= ~bit;
                continue;
            }
            const uint16_t raw = midiParamValueTo14Bit(id, value, range);
            if (plugin.nrpnFeedbackSent[index]
                && plugin.nrpnFeedbackValue[index] == raw) {
                plugin.nrpnFeedbackDirty[word] &= ~bit;
                continue;
            }
            if (!emitNrpnFeedbackValue(events, time, id, raw)) return;
            plugin.nrpnFeedbackValue[index] = raw;
            plugin.nrpnFeedbackSent[index] = true;
            plugin.nrpnFeedbackDirty[word] &= ~bit;
            ++emitted;
        }
        plugin.nrpnFeedbackWordCursor = (word + 1u)
            % kNrpnFeedbackWordCount;
        ++visitedWords;
    }
}

void applyFeedbackConfigurationChange(Plugin& plugin)
{
    if (!plugin.feedbackConfigurationChanged.exchange(false,
            std::memory_order_acq_rel)) return;
    if (plugin.nrpnFeedbackEnabled.load(std::memory_order_relaxed)) {
        plugin.nrpnFeedbackSent.fill(false);
        markAllNrpnFeedbackDirty(plugin);
    }
    if (plugin.matrixFeedbackEnabled.load(std::memory_order_relaxed)) {
        plugin.matrixFeedbackSent.fill(false);
        markAllMatrixFeedbackDirty(plugin);
    }
}

void setMidiFeedbackEnabled(const clap_plugin_t* instance,
    bool nrpnEnabled, bool matrixEnabled)
{
    auto* plugin = self(instance);
    if (!plugin) return;
    const bool nrpnChanged = plugin->nrpnFeedbackEnabled.exchange(
        nrpnEnabled, std::memory_order_acq_rel) != nrpnEnabled;
    const bool matrixChanged = plugin->matrixFeedbackEnabled.exchange(
        matrixEnabled, std::memory_order_acq_rel) != matrixEnabled;
    if (nrpnChanged || matrixChanged) {
        plugin->feedbackConfigurationChanged.store(true,
            std::memory_order_release);
        requestGuiCommandDrain(*plugin);
    }
}

const s3g_nim_midi_feedback_t midiFeedbackExt {
    setMidiFeedbackEnabled,
};

bool applyMidiParam14Bit(Plugin& plugin, clap_id id, uint16_t rawValue,
    uint32_t time, const clap_output_events_t* events)
{
    ParamRange range;
    if (!paramRange(id, range)) return false;
    applyParam(plugin, id, midiParamValueFrom14Bit(id, rawValue, range));
    double applied = 0.0;
    if (!paramValue(plugin, id, applied)) return false;
    markNrpnFeedbackSent(plugin, id, applied);
    emitMidiParamValue(events, time, id, applied);
    return true;
}

void applyMidiToggle(Plugin& plugin, clap_id id, uint32_t time,
    const clap_output_events_t* events)
{
    double value = 0.0;
    if (!paramValue(plugin, id, value)) return;
    applyParam(plugin, id, value >= 0.5 ? 0.0 : 1.0);
    if (paramValue(plugin, id, value)) {
        emitMidiParamValue(events, time, id, value);
    }
}

uint32_t nextPatchSeed(const Plugin& plugin)
{
    uint32_t seed = plugin.params.seed * 1664525u + 1013904223u;
    return seed == 0u ? 1u : seed;
}

void applyMidiFactoryPreset(Plugin& plugin, uint32_t preset,
    uint32_t time, const clap_output_events_t* events)
{
    if (preset >= s3g::kNoInputMixerFactoryPresetCount) return;
    const uint32_t seed = nextPatchSeed(plugin);
    const float variance = plugin.params.variance;
    auto patch = s3g::noInputMixerFactoryPreset(preset);
    patch = s3g::variedNoInputMixerParams(patch, seed, variance);
    plugin.behavior = s3g::noInputMixerFactoryBehavior(preset);
    plugin.behaviorDepth = patch.motion;
    applyCompletePatch(plugin, patch, 0.68f);
    emitAllMidiParamValues(plugin, events, time);
}

void applyMidiRandom(Plugin& plugin, s3g::NoInputRandomEnergy energy,
    uint32_t time, const clap_output_events_t* events)
{
    const uint32_t seed = nextPatchSeed(plugin);
    plugin.behavior = s3g::randomizedNoInputMovementBehaviorParams(
        seed ^ 0x43564d58u, energy);
    const auto patch = s3g::randomizedNoInputMixerParams(seed, energy);
    plugin.behaviorDepth = patch.motion;
    applyCompletePatch(plugin, patch,
        s3g::noInputRandomSeedAmount(energy));
    emitAllMidiParamValues(plugin, events, time);
}

void applyUiSnapshotOnAudioThread(Plugin& plugin, float seedAmount)
{
    const auto snapshot = uiSnapshot(plugin);
    plugin.params = s3g::sanitizeNoInputMixerParams(snapshot.params);
    plugin.behavior = s3g::sanitizeNoInputMovementBehaviorParams(
        snapshot.behavior);
    plugin.auxMute = snapshot.auxMute;
    plugin.behaviorDepth = std::clamp(snapshot.behaviorDepth, 0.0f, 1.0f);

    double value = 0.0;
    if (uiParameterValue(plugin, kMatrixMidiModeParamId, value)) {
        plugin.matrixMidiMode.store(static_cast<uint32_t>(std::clamp(
            std::lround(value), 0l,
            static_cast<long>(s3g::NoInputMatrixMidiMode::Count) - 1l)),
            std::memory_order_relaxed);
    }
    if (uiParameterValue(plugin, kMatrixMidiSignParamId, value)) {
        plugin.matrixMidiSign.store(static_cast<uint32_t>(std::clamp(
            std::lround(value), 0l,
            static_cast<long>(s3g::NoInputMatrixMidiSign::Count) - 1l)),
            std::memory_order_relaxed);
    }
    if (uiParameterValue(plugin, kMatrixMidiRampParamId, value)) {
        plugin.matrixMidiRampMs.store(static_cast<float>(std::clamp(value,
            static_cast<double>(s3g::kNoInputMatrixMidiRampMinimumMs),
            static_cast<double>(s3g::kNoInputMatrixMidiRampMaximumMs))),
            std::memory_order_relaxed);
    }

    plugin.nrpnFeedbackSent.fill(false);
    markAllNrpnFeedbackDirty(plugin);
    clearMidiMatrixGrid(plugin);
    plugin.matrixFeedbackSent.fill(false);
    syncMixerState(plugin);
    plugin.mixer.reset();
    plugin.mixer.reseed(plugin.params.seed, seedAmount);
    resetMeters(plugin);
}

void drainGuiCommands(Plugin& plugin)
{
    constexpr uint32_t kMaximumCommandsPerBlock = 256u;
    bool needsSync = false;
    GuiCommand command;
    for (uint32_t count = 0u;
         count < kMaximumCommandsPerBlock
            && dequeueGuiCommand(plugin, command); ++count) {
        if (command.type == GuiCommandType::ParamValue) {
            applyParam(plugin, command.paramId, command.value,
                false, false);
            needsSync = true;
            continue;
        }
        if (needsSync) {
            syncMixerState(plugin);
            needsSync = false;
        }
        switch (command.type) {
        case GuiCommandType::FactoryPreset: {
            auto patch = s3g::noInputMixerFactoryPreset(command.argument);
            patch = s3g::variedNoInputMixerParams(
                patch, command.seed, static_cast<float>(command.value));
            plugin.behavior = s3g::noInputMixerFactoryBehavior(
                command.argument);
            plugin.behaviorDepth = patch.motion;
            applyCompletePatch(plugin, patch, 0.68f, false);
            break;
        }
        case GuiCommandType::RandomPatch: {
            const auto energy = static_cast<s3g::NoInputRandomEnergy>(
                std::min<uint32_t>(command.argument,
                    static_cast<uint32_t>(
                        s3g::NoInputRandomEnergy::Count) - 1u));
            plugin.behavior = s3g::randomizedNoInputMovementBehaviorParams(
                command.seed ^ 0x43564d58u, energy);
            const auto patch = s3g::randomizedNoInputMixerParams(
                command.seed, energy);
            plugin.behaviorDepth = patch.motion;
            applyCompletePatch(plugin, patch,
                s3g::noInputRandomSeedAmount(energy), false);
            break;
        }
        case GuiCommandType::ForgetPatch:
            applyCompletePatch(plugin,
                s3g::forgottenNoInputMixerParams(
                    plugin.params, command.seed), 0.62f, false);
            break;
        case GuiCommandType::NewSeed:
            plugin.params.seed = nextPatchSeed(plugin);
            plugin.uiSeed.store(plugin.params.seed,
                std::memory_order_relaxed);
            syncMixerState(plugin);
            controlMixer(plugin).reseed(plugin.params.seed, 0.70f);
            break;
        case GuiCommandType::SetSeed:
            plugin.params.seed = command.seed == 0u ? 1u : command.seed;
            syncMixerState(plugin);
            break;
        case GuiCommandType::ClearMatrix:
            clearMidiMatrixGrid(plugin);
            plugin.params.matrix.fill(0.0f);
            syncMixerState(plugin);
            markAllNrpnFeedbackDirty(plugin);
            break;
        case GuiCommandType::Panic:
            clearMidiMatrixGrid(plugin);
            plugin.mixer.panic();
            break;
        case GuiCommandType::KillLane:
            plugin.mixer.killLane(std::min<uint32_t>(
                command.argument, kChannelCount - 1u));
            break;
        case GuiCommandType::ApplyUiSnapshot:
            applyUiSnapshotOnAudioThread(plugin,
                static_cast<float>(command.value));
            break;
        case GuiCommandType::ParamValue:
            break;
        }
    }
    if (needsSync) syncMixerState(plugin);
}

void applyMidiCommand(Plugin& plugin, uint8_t note, uint32_t time,
    const clap_output_events_t* events)
{
    if (note >= kMidiLaneMuteNoteBase
        && note < kMidiLaneMuteNoteBase + kChannelCount) {
        applyMidiToggle(plugin, laneParamId(
            note - kMidiLaneMuteNoteBase, kLaneMuteOffset), time, events);
        return;
    }
    if (note >= kMidiLaneKillNoteBase
        && note < kMidiLaneKillNoteBase + kChannelCount) {
        plugin.mixer.killLane(note - kMidiLaneKillNoteBase);
        return;
    }
    const uint8_t insertBases[s3g::kNoInputMixerInsertSlots] = {
        kMidiInsertOneBypassNoteBase,
        kMidiInsertTwoBypassNoteBase,
        kMidiInsertThreeBypassNoteBase,
    };
    for (uint32_t slot = 0u; slot < s3g::kNoInputMixerInsertSlots;
         ++slot) {
        const uint8_t base = insertBases[slot];
        if (note >= base && note < base + kChannelCount) {
            applyMidiToggle(plugin, insertParamId(
                note - base, slot, kInsertBypassOffset), time, events);
            return;
        }
    }
    if (note >= kMidiAuxMuteNoteBase
        && note < kMidiAuxMuteNoteBase + 2u) {
        applyMidiToggle(plugin, note == kMidiAuxMuteNoteBase
            ? kAuxAMuteParamId : kAuxBMuteParamId, time, events);
        return;
    }
    if (note >= kMidiPitchLockNoteBase
        && note < kMidiPitchLockNoteBase + kChannelCount) {
        applyMidiToggle(plugin, laneParamId(
            note - kMidiPitchLockNoteBase, kLanePitchLockOffset),
            time, events);
        return;
    }

    switch (note) {
    case kMidiMatrixFlipNote:
        applyParam(plugin, kMatrixMidiModeParamId,
            static_cast<double>(s3g::NoInputMatrixMidiMode::Flip));
        emitMidiParamValue(events, time, kMatrixMidiModeParamId,
            static_cast<double>(s3g::NoInputMatrixMidiMode::Flip));
        return;
    case kMidiMatrixLatchNote:
        applyParam(plugin, kMatrixMidiModeParamId,
            static_cast<double>(s3g::NoInputMatrixMidiMode::Latch));
        emitMidiParamValue(events, time, kMatrixMidiModeParamId,
            static_cast<double>(s3g::NoInputMatrixMidiMode::Latch));
        return;
    case kMidiMatrixSignToggleNote:
        applyMidiToggle(plugin, kMatrixMidiSignParamId, time, events);
        return;
    case kMidiNewNote:
        plugin.params.seed = nextPatchSeed(plugin);
        plugin.uiSeed.store(plugin.params.seed, std::memory_order_relaxed);
        syncMixerState(plugin);
        controlMixer(plugin).reseed(plugin.params.seed, 0.70f);
        return;
    case kMidiForgetNote: {
        const uint32_t seed = nextPatchSeed(plugin);
        applyCompletePatch(plugin,
            s3g::forgottenNoInputMixerParams(plugin.params, seed), 0.62f);
        emitAllMidiParamValues(plugin, events, time);
        return;
    }
    case kMidiRandomLowNote:
        applyMidiRandom(plugin, s3g::NoInputRandomEnergy::Low,
            time, events);
        return;
    case kMidiRandomMidNote:
        applyMidiRandom(plugin, s3g::NoInputRandomEnergy::Mid,
            time, events);
        return;
    case kMidiRandomHighNote:
        applyMidiRandom(plugin, s3g::NoInputRandomEnergy::High,
            time, events);
        return;
    case kMidiReactDirectionToggleNote: {
        double direction = 1.0;
        paramValue(plugin, kReactPolarityParamId, direction);
        applyParam(plugin, kReactPolarityParamId,
            direction < 0.0 ? 1.0 : -1.0);
        if (paramValue(plugin, kReactPolarityParamId, direction)) {
            emitMidiParamValue(events, time, kReactPolarityParamId,
                direction);
        }
        return;
    }
    case kMidiPanicNote:
        clearMidiMatrixGrid(plugin);
        plugin.mixer.panic();
        return;
    case kMidiClearMatrixNote:
        clearMidiMatrixGrid(plugin);
        plugin.params.matrix.fill(0.0f);
        syncMixerState(plugin);
        for (uint32_t index = 0u;
             index < s3g::kNoInputMixerMatrixCells; ++index) {
            publishUiParameter(plugin, kMatrixParamBase + index, 0.0);
            markNrpnFeedbackDirty(plugin, kMatrixParamBase + index);
            emitMidiParamValue(events, time,
                kMatrixParamBase + index, 0.0);
        }
        return;
    default:
        return;
    }
}

bool applyCurrentNrpn(Plugin& plugin, uint32_t time,
    const clap_output_events_t* events)
{
    const auto& nrpn = plugin.nrpn;
    if (!nrpn.hasParameterMsb || !nrpn.hasParameterLsb
        || !nrpn.hasDataMsb
        || (nrpn.parameterMsb == 127u && nrpn.parameterLsb == 127u)) {
        return false;
    }
    const clap_id id = static_cast<clap_id>(
        (static_cast<uint16_t>(nrpn.parameterMsb) << 7u)
        | nrpn.parameterLsb);
    const uint16_t value = static_cast<uint16_t>(
        (static_cast<uint16_t>(nrpn.dataMsb) << 7u) | nrpn.dataLsb);
    return applyMidiParam14Bit(plugin, id, value, time, events);
}

void handleMidiCc(Plugin& plugin, uint8_t controller, uint8_t value,
    uint32_t time, const clap_output_events_t* events)
{
    auto& nrpn = plugin.nrpn;
    switch (controller) {
    case 99u:
        nrpn.parameterMsb = value;
        nrpn.hasParameterMsb = true;
        break;
    case 98u:
        nrpn.parameterLsb = value;
        nrpn.hasParameterLsb = true;
        break;
    case 6u:
        nrpn.dataMsb = value;
        nrpn.dataLsb = 0u;
        nrpn.hasDataMsb = true;
        applyCurrentNrpn(plugin, time, events);
        break;
    case 38u:
        nrpn.dataLsb = value;
        applyCurrentNrpn(plugin, time, events);
        break;
    case 100u:
    case 101u:
        nrpn.hasParameterMsb = false;
        nrpn.hasParameterLsb = false;
        break;
    default:
        break;
    }
}

void applyMidiMatrixLatchGain(Plugin& plugin, uint32_t index, float gain,
    uint32_t time, const clap_output_events_t* events)
{
    if (index >= s3g::kNoInputMixerMatrixCells) return;
    const uint32_t destination = index / kChannelCount;
    const uint32_t source = index % kChannelCount;
    // Start from the currently heard value before updating the saved matrix.
    // Releasing this temporary overlay after the state update lets the DSP
    // glide to the new persistent value and then retire the overlay.
    auto& destinationMixer = controlMixer(plugin);
    destinationMixer.setMidiMatrixConnection(destination, source, gain);
    trackMatrixFeedback(plugin, index);
    plugin.params.matrix[index] = gain;
    syncMixerState(plugin);
    destinationMixer.releaseMidiMatrixConnection(destination, source);
    publishUiParameter(plugin, kMatrixParamBase + index, gain);
    markNrpnFeedbackDirty(plugin, kMatrixParamBase + index);
    markMatrixFeedbackDirty(plugin, index);
    emitMidiParamValue(events, time, kMatrixParamBase + index, gain);
}

bool refineMidiMatrixLatchAttack(Plugin& plugin, uint32_t index,
    uint8_t velocity, uint32_t time, const clap_output_events_t* events)
{
    if (index >= s3g::kNoInputMixerMatrixCells
        || plugin.midiMatrixLatchCaptureFrames[index] == 0u
        || !plugin.midiMatrixGridHeld[index].load(
            std::memory_order_relaxed)
        || velocity <= plugin.midiMatrixLatchPeakVelocity[index]
        || std::abs(plugin.params.matrix[index]) <= 1.0e-7f) {
        return false;
    }
    plugin.midiMatrixLatchPeakVelocity[index] = velocity;
    const auto sign = static_cast<s3g::NoInputMatrixMidiSign>(
        plugin.midiMatrixLatchSign[index]);
    const float gain = s3g::noInputMatrixMidiGain(
        s3g::NoInputMatrixMidiMode::Latch, 0.0f, velocity, sign);
    applyMidiMatrixLatchGain(plugin, index, gain, time, events);
    return true;
}

bool applyMidiMatrixGridNote(Plugin& plugin, uint8_t channel, uint8_t note,
    uint8_t velocity, bool pressed, uint32_t time,
    const clap_output_events_t* events)
{
    uint32_t destination = 0u;
    uint32_t source = 0u;
    if (!s3g::decodeNoInputMatrixGridNote(
            channel, note, destination, source))
        return false;
    const uint32_t index = destination * kChannelCount + source;
    if (pressed) {
        plugin.selectedDestination.store(destination,
            std::memory_order_relaxed);
        plugin.selectedSource.store(source, std::memory_order_relaxed);
    }
    const auto mode = static_cast<s3g::NoInputMatrixMidiMode>(
        plugin.matrixMidiMode.load(std::memory_order_relaxed));
    if (mode == s3g::NoInputMatrixMidiMode::Latch) {
        const bool held = plugin.midiMatrixGridHeld[index].load(
            std::memory_order_relaxed);
        if (!pressed) {
            plugin.midiMatrixGridHeld[index].store(false,
                std::memory_order_relaxed);
            plugin.midiMatrixLatchPeakVelocity[index] = 0u;
            plugin.midiMatrixLatchCaptureFrames[index] = 0u;
            return true;
        }
        if (held) {
            // Older BU16 profiles sent pressure as repeated Note Ons. Accept
            // only a larger value during the short strike-capture window.
            refineMidiMatrixLatchAttack(
                plugin, index, velocity, time, events);
            return true;
        }
        plugin.midiMatrixGridHeld[index].store(true,
            std::memory_order_relaxed);
        float gain = 0.0f;
        if (std::abs(plugin.params.matrix[index]) <= 1.0e-7f) {
            const auto sign = static_cast<s3g::NoInputMatrixMidiSign>(
                plugin.matrixMidiSign.load(std::memory_order_relaxed));
            gain = s3g::noInputMatrixMidiGain(
                mode, 0.0f, velocity, sign);
            plugin.midiMatrixLatchPeakVelocity[index] = velocity;
            plugin.midiMatrixLatchSign[index] =
                static_cast<uint8_t>(sign);
            plugin.midiMatrixLatchCaptureFrames[index] =
                std::max<uint32_t>(1u, static_cast<uint32_t>(std::lround(
                    plugin.sampleRate * kMatrixLatchAttackCaptureSeconds)));
        } else {
            plugin.midiMatrixLatchPeakVelocity[index] = 0u;
            plugin.midiMatrixLatchCaptureFrames[index] = 0u;
        }
        applyMidiMatrixLatchGain(plugin, index, gain, time, events);
    } else if (pressed) {
        const float baseGain = plugin.midiMatrixBaseGain[index].load(
            std::memory_order_relaxed);
        const float gain = s3g::noInputMatrixMidiGain(
            mode, baseGain, velocity);
        auto& destinationMixer = controlMixer(plugin);
        destinationMixer.setMidiMatrixConnection(destination, source, gain);
        trackMatrixFeedback(plugin, index);
        plugin.midiMatrixGridGain[index].store(
            destinationMixer.effectiveMatrixGain(destination, source),
            std::memory_order_relaxed);
        plugin.midiMatrixGridHeld[index].store(true,
            std::memory_order_relaxed);
        plugin.midiMatrixGridActive[index].store(true,
            std::memory_order_relaxed);
    } else {
        controlMixer(plugin).releaseMidiMatrixConnection(destination, source);
        trackMatrixFeedback(plugin, index);
        plugin.midiMatrixGridHeld[index].store(false,
            std::memory_order_relaxed);
    }
    return true;
}

bool applyMidiMatrixGridPressure(Plugin& plugin, uint8_t channel,
    uint8_t note, uint8_t pressure, uint32_t time,
    const clap_output_events_t* events)
{
    uint32_t destination = 0u;
    uint32_t source = 0u;
    if (!s3g::decodeNoInputMatrixGridNote(
            channel, note, destination, source)) return false;
    const uint32_t index = destination * kChannelCount + source;
    const auto mode = static_cast<s3g::NoInputMatrixMidiMode>(
        plugin.matrixMidiMode.load(std::memory_order_relaxed));
    if (mode == s3g::NoInputMatrixMidiMode::Latch) {
        refineMidiMatrixLatchAttack(
            plugin, index, pressure, time, events);
        return true;
    }
    if (!plugin.midiMatrixGridHeld[index].load(
            std::memory_order_relaxed)) return true;
    const float baseGain = plugin.midiMatrixBaseGain[index].load(
        std::memory_order_relaxed);
    const float gain = s3g::noInputMatrixMidiGain(
        mode, baseGain, pressure);
    controlMixer(plugin).setMidiMatrixConnection(destination, source, gain);
    trackMatrixFeedback(plugin, index);
    return true;
}

void handleMidiEvent(Plugin& plugin, const clap_event_midi_t& midi,
    const clap_output_events_t* events)
{
    const uint8_t status = midi.data[0] & 0xf0u;
    const uint8_t channel = midi.data[0] & 0x0fu;
    if (status == 0x90u || status == 0x80u) {
        const uint8_t velocity = midi.data[2] & 0x7fu;
        const bool pressed = status == 0x90u && velocity != 0u;
        if (applyMidiMatrixGridNote(plugin, channel,
                midi.data[1] & 0x7fu, velocity, pressed,
                midi.header.time, events)) return;
    }
    if (status == 0xa0u
        && applyMidiMatrixGridPressure(plugin, channel,
            midi.data[1] & 0x7fu, midi.data[2] & 0x7fu,
            midi.header.time, events)) return;
    if (channel != kMidiControlChannel) return;
    if (status == 0xb0u) {
        handleMidiCc(plugin, midi.data[1] & 0x7fu,
            midi.data[2] & 0x7fu, midi.header.time, events);
    } else if (status == 0x90u && (midi.data[2] & 0x7fu) != 0u) {
        applyMidiCommand(plugin, midi.data[1] & 0x7fu,
            midi.header.time, events);
    } else if (status == 0xc0u) {
        applyMidiFactoryPreset(plugin, midi.data[1] & 0x7fu,
            midi.header.time, events);
    }
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
    p->active.store(true, std::memory_order_release);
    syncMixerState(*p);
    p->mixer.reseed(p->params.seed, 0.62f);
    resetMeters(*p);
    p->matrixFeedbackSent.fill(false);
    markAllMatrixFeedbackDirty(*p);
    p->nrpnFeedbackSent.fill(false);
    markAllNrpnFeedbackDirty(*p);
    resetNrpnState(*p);
    return true;
}

void deactivate(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    clearMidiMatrixGrid(*p);
    p->active.store(false, std::memory_order_release);
}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    syncMixerState(*p);
    p->mixer.reseed(p->params.seed, 0.62f);
    resetMeters(*p);
    resetNrpnState(*p);
    clearMidiMatrixGrid(*p);
    p->matrixFeedbackSent.fill(false);
    markAllMatrixFeedbackDirty(*p);
    p->nrpnFeedbackSent.fill(false);
    markAllNrpnFeedbackDirty(*p);
}

bool applyParamEvent(Plugin& plugin, const clap_event_header_t* event)
{
    if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) return false;
    if (event->type == CLAP_EVENT_PARAM_VALUE) {
        const auto* param =
            reinterpret_cast<const clap_event_param_value_t*>(event);
        applyParam(plugin, param->param_id, param->value);
        return true;
    }
    if (event->type == CLAP_EVENT_PARAM_MOD) {
        const auto* param =
            reinterpret_cast<const clap_event_param_mod_t*>(event);
        const int32_t index = paramIndexForId(param->param_id);
        ParamRange range;
        if (index < 0 || !paramRange(param->param_id, range)
            || range.stepped) return false;
        plugin.modulation[static_cast<uint32_t>(index)] = param->amount;
        syncMixerState(plugin);
        return true;
    }
    return false;
}

void applyInputEvent(Plugin& plugin, const clap_event_header_t* event,
    const clap_output_events_t* outputEvents)
{
    if (applyParamEvent(plugin, event) || !event
        || event->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
    if (event->type == CLAP_EVENT_MIDI) {
        const auto& midi = *reinterpret_cast<const clap_event_midi_t*>(event);
        handleMidiEvent(plugin, midi, outputEvents);
        const uint8_t status = midi.data[0] & 0xf0u;
        const uint8_t channel = midi.data[0] & 0x0fu;
        if (status == 0xb0u && channel == kMidiControlChannel
            && plugin.nrpnFeedbackEnabled.load(std::memory_order_relaxed)
            && outputEvents && outputEvents->try_push) {
            outputEvents->try_push(outputEvents, event);
        }
        return;
    }
    if (event->type == CLAP_EVENT_NOTE_ON) {
        const auto* note = reinterpret_cast<const clap_event_note_t*>(event);
        const uint8_t velocity = static_cast<uint8_t>(std::clamp<int32_t>(
            static_cast<int32_t>(std::lround(note->velocity * 127.0)),
            0, 127));
        if (note->channel >= 0 && note->channel <= 15
            && applyMidiMatrixGridNote(plugin,
                static_cast<uint8_t>(note->channel),
                static_cast<uint8_t>(std::clamp<int32_t>(note->key, 0, 127)),
                velocity, velocity != 0u, event->time,
                outputEvents)) return;
        if (note->channel == kMidiControlChannel && note->velocity > 0.0) {
            applyMidiCommand(plugin, static_cast<uint8_t>(
                std::clamp<int32_t>(note->key, 0, 127)),
                event->time, outputEvents);
        }
        return;
    }
    if (event->type == CLAP_EVENT_NOTE_OFF) {
        const auto* note = reinterpret_cast<const clap_event_note_t*>(event);
        if (note->channel >= 0 && note->channel <= 15) {
            applyMidiMatrixGridNote(plugin,
                static_cast<uint8_t>(note->channel),
                static_cast<uint8_t>(std::clamp<int32_t>(
                    note->key, 0, 127)), 0u, false, event->time,
                outputEvents);
        }
    }
}

void readInputEvents(Plugin& plugin, const clap_input_events_t* events,
    const clap_output_events_t* outputEvents)
{
    if (!events) return;
    const uint32_t count = events->size(events);
    for (uint32_t index = 0u; index < count; ++index) {
        const clap_event_header_t* event = events->get(events, index);
        applyInputEvent(plugin, event, outputEvents);
    }
}

clap_process_status process(const clap_plugin_t* plugin,
    const clap_process_t* process)
{
    auto* p = self(plugin);
    if (!process) return CLAP_PROCESS_CONTINUE;
    applyFeedbackConfigurationChange(*p);
    drainGuiCommands(*p);
    if (process->transport) {
        const bool hasTempo = (process->transport->flags
            & CLAP_TRANSPORT_HAS_TEMPO) != 0;
        const double tempo = hasTempo && std::isfinite(process->transport->tempo)
            ? process->transport->tempo : 120.0;
        p->transportTempoBpm = tempo;
        p->transportHasTempo = hasTempo;
        p->mixer.setTransport(tempo, hasTempo);
    } else {
        p->transportTempoBpm = 120.0;
        p->transportHasTempo = false;
        p->mixer.setTransport(120.0, false);
    }
    const uint32_t eventCount = process->in_events
        ? process->in_events->size(process->in_events) : 0u;
    uint32_t eventIndex = 0u;
    const auto applyEventsThrough = [&](uint32_t frame) {
        while (eventIndex < eventCount) {
            const auto* event = process->in_events->get(
                process->in_events, eventIndex);
            if (!event) {
                ++eventIndex;
                continue;
            }
            if (event->time > frame) break;
            applyInputEvent(*p, event, process->out_events);
            ++eventIndex;
        }
    };

    if (process->audio_outputs_count == 0u) {
        readInputEvents(*p, process->in_events, process->out_events);
        refreshMidiMatrixOverlayState(*p);
        advanceMidiMatrixLatchCapture(*p, process->frames_count);
        emitNrpnFeedback(*p, process->out_events,
            process->frames_count > 0u ? process->frames_count - 1u : 0u);
        emitMatrixFeedback(*p, process->out_events,
            process->frames_count > 0u ? process->frames_count - 1u : 0u,
            process->frames_count);
        return CLAP_PROCESS_CONTINUE;
    }
    const clap_audio_buffer_t& output = process->audio_outputs[0];
    if (!output.data32 && !output.data64) {
        readInputEvents(*p, process->in_events, process->out_events);
        refreshMidiMatrixOverlayState(*p);
        advanceMidiMatrixLatchCapture(*p, process->frames_count);
        emitNrpnFeedback(*p, process->out_events,
            process->frames_count > 0u ? process->frames_count - 1u : 0u);
        emitMatrixFeedback(*p, process->out_events,
            process->frames_count > 0u ? process->frames_count - 1u : 0u,
            process->frames_count);
        return CLAP_PROCESS_CONTINUE;
    }
    const uint32_t writableChannels = std::min<uint32_t>(
        output.channel_count, kChannelCount);
    std::array<float, kChannelCount> blockPeaks {};
    std::array<float, kChannelCount> renderedFrame {};
    float blockPeak = 0.0f;

    for (uint32_t frame = 0u; frame < process->frames_count; ++frame) {
        applyEventsThrough(frame);
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
        for (uint32_t lane = 0u; lane < kChannelCount; ++lane) {
            blockPeaks[lane] = std::max(blockPeaks[lane],
                std::abs(p->frame[lane]));
        }
        p->outputMixdown.configure(s3g::sanitizeRingOutputFormat(
            p->outputFormat.load(std::memory_order_relaxed)),
            p->outputRotationDegrees.load(std::memory_order_relaxed));
        p->outputMixdown.processFrame(p->frame.data(),
            renderedFrame.data());
        for (uint32_t lane = 0u; lane < writableChannels; ++lane) {
            const float value = renderedFrame[lane];
            if (output.data32 && output.data32[lane]) {
                output.data32[lane][frame] = value;
            }
            if (output.data64 && output.data64[lane]) {
                output.data64[lane][frame] = static_cast<double>(value);
            }
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
    const auto& meterMixer = controlMixer(*p);
    for (uint32_t lane = 0u; lane < kChannelCount; ++lane) {
        p->lanePeaks[lane].store(std::max(
            p->lanePeaks[lane].load(std::memory_order_relaxed) * 0.90f,
            blockPeaks[lane]), std::memory_order_relaxed);
        p->laneActivity[lane].store(meterMixer.laneActivity(lane),
            std::memory_order_relaxed);
    }
    p->networkActivity.store(meterMixer.networkActivity(),
        std::memory_order_relaxed);
    p->motionPhase.store(meterMixer.motionPhase(),
        std::memory_order_relaxed);
    for (uint32_t route = 0u;
         route < s3g::kNoInputMixerMatrixCells; ++route) {
        p->behaviorRouteGate[route].store(
            meterMixer.behaviorRouteGate(route), std::memory_order_relaxed);
        p->reactRouteGate[route].store(
            meterMixer.reactRouteGate(route), std::memory_order_relaxed);
    }
    for (uint32_t bus = 0u; bus < 2u; ++bus) {
        p->auxActivity[bus].store(meterMixer.auxActivity(bus),
            std::memory_order_relaxed);
    }
    p->minimumGovernor.store(p->mixer.minimumGovernor(),
        std::memory_order_relaxed);
    p->containmentState.store(
        static_cast<uint32_t>(p->mixer.containmentState()),
        std::memory_order_relaxed);
    refreshMidiMatrixOverlayState(*p);
    advanceMidiMatrixLatchCapture(*p, process->frames_count);
    emitNrpnFeedback(*p, process->out_events,
        process->frames_count > 0u ? process->frames_count - 1u : 0u);
    emitMatrixFeedback(*p, process->out_events,
        process->frames_count > 0u ? process->frames_count - 1u : 0u,
        process->frames_count);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (p && !p->active.load(std::memory_order_acquire))
        drainGuiCommands(*p);
}

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

uint32_t notePortsCount(const clap_plugin_t*, bool isInput)
{
    (void)isInput;
    return 1u;
}

bool notePortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_note_port_info_t* info)
{
    if (!info || index != 0u) return false;
    info->id = isInput ? 30u : 31u;
    info->supported_dialects =
        CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
    std::snprintf(info->name, sizeof(info->name), "%s",
        isInput ? "Controller MIDI In" : "Controller Feedback Out");
    return true;
}

const clap_plugin_note_ports_t notePorts {
    notePortsCount, notePortsGet,
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
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    if (range.stepped) info->flags |= CLAP_PARAM_IS_STEPPED;
    else if (id != kMatrixMidiRampParamId
        && id != kOutputRotationParamId)
        info->flags |= CLAP_PARAM_IS_MODULATABLE;
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
        "Mid Gain", "High", "Aux A Send", "Aux B Send", "Tune Note",
        "Tune Cents", "Pitch Lock", "Aux A Tap", "Aux B Tap",
        "Aux A Return To Lane", "Aux B Return To Lane",
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
    return value && uiParameterValue(*self(plugin), id, *value);
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value,
    char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
    if (id == kOutputFormatParamId) {
        std::snprintf(display, size, "%s", s3g::ringOutputFormatName(
            s3g::sanitizeRingOutputFormat(static_cast<uint32_t>(std::clamp(
                std::lround(value), 0l, 2l)))));
        return true;
    }
    if (id == kOutputRotationParamId) {
        std::snprintf(display, size, "%+.1f deg", value);
        return true;
    }
    if (id == kOutputParamId || id == kCeilingParamId) {
        std::snprintf(display, size, "%+.1f dB", value);
        return true;
    }
    if (id == kLimiterParamId || id == kDcBlockParamId
        || id == kAuxAMuteParamId || id == kAuxBMuteParamId
        || id == kControllerHoldParamId || id == kSlowTimeParamId
        || id == kClockSyncParamId) {
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
            static_cast<uint32_t>(std::clamp(std::lround(value), 0l,
                static_cast<long>(s3g::kMatrixFlowShapeCount) - 1l)));
        std::snprintf(display, size, "%s", s3g::matrixFlowShapeName(shape));
        return true;
    }
    if (id == kBehaviorParamId) {
        const uint32_t behavior = static_cast<uint32_t>(std::clamp(
            std::lround(value), 0l,
            static_cast<long>(s3g::kNoInputMovementBehaviorCount) - 1l));
        std::snprintf(display, size, "%s",
            s3g::noInputMovementBehaviorName(
                static_cast<s3g::NoInputMovementBehavior>(behavior)));
        return true;
    }
    if (id == kReactModeParamId) {
        const uint32_t mode = static_cast<uint32_t>(std::clamp(
            std::lround(value), 0l,
            static_cast<long>(s3g::NoInputReactMode::Count) - 1l));
        std::snprintf(display, size, "%s", s3g::noInputReactModeName(
            static_cast<s3g::NoInputReactMode>(mode)));
        return true;
    }
    if (id == kReactPolarityParamId) {
        std::snprintf(display, size, "%s",
            value < 0.0 ? "INVERT" : "NORMAL");
        return true;
    }
    if (id == kMatrixMidiModeParamId) {
        const uint32_t mode = static_cast<uint32_t>(std::clamp(
            std::lround(value), 0l,
            static_cast<long>(s3g::NoInputMatrixMidiMode::Count) - 1l));
        std::snprintf(display, size, "%s", s3g::noInputMatrixMidiModeName(
            static_cast<s3g::NoInputMatrixMidiMode>(mode)));
        return true;
    }
    if (id == kMatrixMidiSignParamId) {
        const uint32_t sign = static_cast<uint32_t>(std::clamp(
            std::lround(value), 0l,
            static_cast<long>(s3g::NoInputMatrixMidiSign::Count) - 1l));
        std::snprintf(display, size, "%s", s3g::noInputMatrixMidiSignName(
            static_cast<s3g::NoInputMatrixMidiSign>(sign)));
        return true;
    }
    if (id == kMatrixMidiRampParamId) {
        std::snprintf(display, size, "%.0f ms", value);
        return true;
    }
    if (id == kFieldDivisionParamId || id == kEventDivisionParamId) {
        std::snprintf(display, size, "%s", s3g::noInputClockDivisionName(
            static_cast<uint32_t>(std::lround(value))));
        return true;
    }
    if (id == kReactAttackParamId || id == kReactReleaseParamId) {
        const float milliseconds = id == kReactAttackParamId
            ? s3g::noInputReactAttackMs(static_cast<float>(value))
            : s3g::noInputReactReleaseMs(static_cast<float>(value));
        std::snprintf(display, size, milliseconds < 100.0f
            ? "%.1f ms" : "%.0f ms", milliseconds);
        return true;
    }
    if (id == kEventRateParamId) {
        const float hz = s3g::noInputMovementEventRateHz(
            static_cast<float>(value));
        std::snprintf(display, size, hz < 10.0f ? "%.2f Hz" : "%.1f Hz",
            hz);
        return true;
    }
    if (id == kEventLengthParamId || id == kEventSlewParamId) {
        const float milliseconds = id == kEventLengthParamId
            ? s3g::noInputMovementLengthMs(static_cast<float>(value))
            : s3g::noInputMovementSlewMs(static_cast<float>(value));
        std::snprintf(display, size, milliseconds < 10.0f
            ? "%.2f ms" : "%.1f ms", milliseconds);
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
        || id == kEventDensityParamId || id == kEventChaosParamId
        || id == kEventChokeParamId || id == kBehaviorDepthParamId
        || id == kReactDepthParamId
        || id == kReactThresholdParamId
        || (id >= kAuxAGainParamId && id <= kAuxAFeedbackParamId)
        || (id >= kAuxBGainParamId && id <= kAuxBFeedbackParamId)) {
        std::snprintf(display, size, "%.0f%%", value * 100.0);
        return true;
    }
    if (id == kInternalToneParamId || id == kHouseToneParamId
        || id == kVortexParamId || id == kAuxABiasParamId
        || id == kAuxBBiasParamId) {
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
        || offset == kLaneMidGainOffset || offset == kLaneHighOffset
        || offset == kLaneAuxReturnAOffset
        || offset == kLaneAuxReturnBOffset) {
        std::snprintf(display, size, "%+.1f dB", value);
        return true;
    }
    if (offset == kLaneMuteOffset || offset == kLanePitchLockOffset) {
        std::snprintf(display, size, "%s", value >= 0.5 ? "ON" : "OFF");
        return true;
    }
    if (offset == kLaneMidFrequencyOffset) {
        if (value >= 1000.0) std::snprintf(display, size, "%.2f kHz",
            value * 0.001);
        else std::snprintf(display, size, "%.0f Hz", value);
        return true;
    }
    if (offset == kLaneTuneNoteOffset) {
        std::snprintf(display, size, "%.2f MIDI", value);
        return true;
    }
    if (offset == kLaneTuneCentsOffset) {
        std::snprintf(display, size, "%+.1f ct", value);
        return true;
    }
    if (offset == kLaneAuxTapAOffset || offset == kLaneAuxTapBOffset) {
        const uint32_t tap = static_cast<uint32_t>(std::clamp(
            std::lround(value), 0l,
            static_cast<long>(s3g::NoInputAuxTap::Count) - 1l));
        std::snprintf(display, size, "%s", s3g::noInputAuxTapName(
            static_cast<s3g::NoInputAuxTap>(tap)));
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
    if (id == kOutputFormatParamId) {
        for (uint32_t format = 0u; format < s3g::kRingOutputFormatCount;
             ++format) {
            if (std::strcmp(display, s3g::ringOutputFormatName(
                    static_cast<s3g::RingOutputFormat>(format))) == 0) {
                *value = format;
                return true;
            }
        }
    }
    if (id == kMotionShapeParamId) {
        for (uint32_t shape = 0u;
             shape < s3g::kMatrixFlowShapeCount; ++shape) {
            if (std::strcmp(display, s3g::matrixFlowShapeName(
                    s3g::matrixFlowShapeFromIndex(shape))) == 0) {
                *value = shape;
                return true;
            }
        }
    }
    if (id == kMatrixMidiSignParamId) {
        for (uint32_t sign = 0u;
             sign < static_cast<uint32_t>(
                 s3g::NoInputMatrixMidiSign::Count); ++sign) {
            if (std::strcmp(display, s3g::noInputMatrixMidiSignName(
                    static_cast<s3g::NoInputMatrixMidiSign>(sign))) == 0) {
                *value = sign;
                return true;
            }
        }
    }
    if (id == kBehaviorParamId) {
        for (uint32_t behavior = 0u;
             behavior < s3g::kNoInputMovementBehaviorCount; ++behavior) {
            if (std::strcmp(display, s3g::noInputMovementBehaviorName(
                    static_cast<s3g::NoInputMovementBehavior>(behavior)))
                == 0) {
                *value = behavior;
                return true;
            }
        }
    }
    if (id == kReactModeParamId) {
        for (uint32_t mode = 0u;
             mode < static_cast<uint32_t>(s3g::NoInputReactMode::Count);
             ++mode) {
            if (std::strcmp(display, s3g::noInputReactModeName(
                    static_cast<s3g::NoInputReactMode>(mode))) == 0) {
                *value = mode;
                return true;
            }
        }
    }
    if (id == kReactPolarityParamId) {
        if (std::strcmp(display, "INVERT") == 0) {
            *value = -1.0;
            return true;
        }
        if (std::strcmp(display, "NORMAL") == 0) {
            *value = 1.0;
            return true;
        }
    }
    if (id == kMatrixMidiModeParamId) {
        for (uint32_t mode = 0u;
             mode < static_cast<uint32_t>(
                 s3g::NoInputMatrixMidiMode::Count); ++mode) {
            if (std::strcmp(display, s3g::noInputMatrixMidiModeName(
                    static_cast<s3g::NoInputMatrixMidiMode>(mode))) == 0) {
                *value = mode;
                return true;
            }
        }
    }
    if (id == kFieldDivisionParamId || id == kEventDivisionParamId) {
        for (uint32_t division = 0u;
             division < s3g::kNoInputClockDivisionCount; ++division) {
            if (std::strcmp(display,
                    s3g::noInputClockDivisionName(division)) == 0) {
                *value = division;
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
        if (offset == kLaneAuxTapAOffset
            || offset == kLaneAuxTapBOffset) {
            for (uint32_t tap = 0u;
                 tap < static_cast<uint32_t>(s3g::NoInputAuxTap::Count);
                 ++tap) {
                if (std::strcmp(display, s3g::noInputAuxTapName(
                        static_cast<s3g::NoInputAuxTap>(tap))) == 0) {
                    *value = tap;
                    return true;
                }
            }
        }
    }
    if (std::strcmp(display, "ON") == 0) { *value = 1.0; return true; }
    if (std::strcmp(display, "OFF") == 0) { *value = 0.0; return true; }
    if (id == kEventRateParamId) {
        const double hz = std::max(0.25, std::atof(display));
        *value = std::clamp(std::log(hz / 0.25) / std::log(320.0),
            0.0, 1.0);
        return true;
    }
    if (id == kEventLengthParamId) {
        const double milliseconds = std::max(0.5, std::atof(display));
        *value = std::clamp(std::log(milliseconds / 0.5)
            / std::log(500.0), 0.0, 1.0);
        return true;
    }
    if (id == kEventSlewParamId) {
        const double milliseconds = std::max(0.5, std::atof(display));
        *value = std::clamp(std::log(milliseconds / 0.5)
            / std::log(40.0), 0.0, 1.0);
        return true;
    }
    if (id == kReactAttackParamId) {
        const double milliseconds = std::max(0.5, std::atof(display));
        *value = std::clamp(std::log(milliseconds / 0.5)
            / std::log(4000.0), 0.0, 1.0);
        return true;
    }
    if (id == kReactReleaseParamId) {
        const double milliseconds = std::max(5.0, std::atof(display));
        *value = std::clamp(std::log(milliseconds / 5.0)
            / std::log(2000.0), 0.0, 1.0);
        return true;
    }
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
    const clap_input_events_t* in, const clap_output_events_t* out)
{
    auto& p = *self(plugin);
    applyFeedbackConfigurationChange(p);
    drainGuiCommands(p);
    readInputEvents(p, in, out);
    emitNrpnFeedback(p, out, 0u);
    emitMatrixFeedback(p, out, 0u, 0u);
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
    const auto visible = uiSnapshot(*p);
    state.params = visible.params;
    state.selectedLane = p->selectedLane.load(std::memory_order_relaxed);
    state.selectedSlot = p->selectedSlot.load(std::memory_order_relaxed);
    state.selectedSource = p->selectedSource.load(std::memory_order_relaxed);
    state.selectedDestination = p->selectedDestination.load(
        std::memory_order_relaxed);
    state.guiPage = p->guiPage.load(std::memory_order_relaxed);
    state.matrixMidiMode = p->matrixMidiMode.load(
        std::memory_order_relaxed);
    state.matrixMidiSign = p->matrixMidiSign.load(
        std::memory_order_relaxed);
    state.matrixMidiRampMs = p->matrixMidiRampMs.load(
        std::memory_order_relaxed);
    state.auxMute = visible.auxMute;
    state.behavior = visible.behavior;
    state.behaviorDepth = visible.behaviorDepth;
    state.surface = p->surface;
    state.surfaceTopologyMode = p->surfaceTopologyMode;
    state.surfaceTopologyCell = p->surfaceTopologyCell;
    double outputFormat = 0.0;
    double outputRotation = 0.0;
    uiParameterValue(*p, kOutputFormatParamId, outputFormat);
    uiParameterValue(*p, kOutputRotationParamId, outputRotation);
    state.outputFormat = static_cast<uint32_t>(std::clamp(
        std::lround(outputFormat), 0l, 2l));
    state.outputRotationDegrees = s3g::sanitizeRingOutputRotation(
        static_cast<float>(outputRotation));
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
    bool captureLegacyNearestAnchor = false;
    if (version == kStateVersion) {
        state.version = version;
        if (!readExact(reinterpret_cast<uint8_t*>(&state) + sizeof(version),
                sizeof(state) - sizeof(version))) {
            return false;
        }
    } else if (version == 14u) {
        Version14SavedState previous;
        previous.version = version;
        if (!readExact(reinterpret_cast<uint8_t*>(&previous)
                + sizeof(version), sizeof(previous) - sizeof(version))) {
            return false;
        }
        state.params = previous.params;
        state.selectedLane = previous.selectedLane;
        state.selectedSlot = previous.selectedSlot;
        state.selectedSource = previous.selectedSource;
        state.selectedDestination = previous.selectedDestination;
        state.guiPage = previous.guiPage;
        state.matrixMidiMode = previous.matrixMidiMode;
        state.matrixMidiSign = previous.matrixMidiSign;
        state.matrixMidiRampMs = previous.matrixMidiRampMs;
        state.auxMute = previous.auxMute;
        state.behavior = previous.behavior;
        state.behaviorDepth = previous.behaviorDepth;
        state.surface = previous.surface;
        state.surfaceTopologyMode = previous.surfaceTopologyMode;
        state.surfaceTopologyCell = previous.surfaceTopologyCell;
    } else if (version == 13u) {
        Version13SavedState previous;
        previous.version = version;
        if (!readExact(reinterpret_cast<uint8_t*>(&previous)
                + sizeof(version), sizeof(previous) - sizeof(version))) {
            return false;
        }
        const float blend = std::clamp(
            std::isfinite(previous.retiredBlend)
                ? previous.retiredBlend : 0.0f, 0.0f, 1.0f);
        const auto& dominant = previous.retiredStates[
            blend < 0.5f ? 0u : 1u];
        state.params = dominant.params;
        state.selectedLane = previous.selectedLane;
        state.selectedSlot = previous.selectedSlot;
        state.selectedSource = previous.selectedSource;
        state.selectedDestination = previous.selectedDestination;
        state.guiPage = previous.guiPage < kPageCount
            ? previous.guiPage : 0u;
        state.matrixMidiMode = previous.matrixMidiMode;
        state.matrixMidiSign = previous.matrixMidiSign;
        state.matrixMidiRampMs = previous.matrixMidiRampMs;
        state.auxMute = dominant.auxMute;
        state.behavior = dominant.behavior;
        state.behaviorDepth = dominant.behaviorDepth;
        state.surface = previous.surface;
        state.surfaceTopologyMode = previous.surfaceTopologyMode;
        state.surfaceTopologyCell = previous.surfaceTopologyCell;
    } else if (version == 12u) {
        Version12SavedState previous;
        previous.version = version;
        if (!readExact(reinterpret_cast<uint8_t*>(&previous)
                + sizeof(version), sizeof(previous) - sizeof(version))) {
            return false;
        }
        state.params = previous.params;
        state.selectedLane = previous.selectedLane;
        state.selectedSlot = previous.selectedSlot;
        state.selectedSource = previous.selectedSource;
        state.selectedDestination = previous.selectedDestination;
        state.guiPage = previous.guiPage;
        state.matrixMidiMode = previous.matrixMidiMode;
        state.matrixMidiSign = previous.matrixMidiSign;
        state.matrixMidiRampMs = previous.matrixMidiRampMs;
        state.auxMute = previous.auxMute;
        state.behavior = previous.behavior;
        state.behaviorDepth = previous.behaviorDepth;
        state.surface = previous.surface;
        state.surfaceTopologyMode = previous.surfaceTopologyMode;
        state.surfaceTopologyCell = previous.surfaceTopologyCell;
    } else if (version == 11u || version == 10u) {
        // Version ten used the same final fields, but CELL changed topology
        // dynamically and LOCK optionally carried a captured cell. Migrate
        // both models to a stable base or cell anchor.
        Version11SavedState previous;
        previous.version = version;
        if (!readExact(reinterpret_cast<uint8_t*>(&previous)
                + sizeof(version), sizeof(previous) - sizeof(version))) {
            return false;
        }
        state.params = previous.params;
        state.selectedLane = previous.selectedLane;
        state.selectedSlot = previous.selectedSlot;
        state.selectedSource = previous.selectedSource;
        state.selectedDestination = previous.selectedDestination;
        state.guiPage = previous.guiPage;
        state.matrixMidiMode = previous.matrixMidiMode;
        state.matrixMidiSign = previous.matrixMidiSign;
        state.matrixMidiRampMs = previous.matrixMidiRampMs;
        state.auxMute = previous.auxMute;
        state.behavior = previous.behavior;
        state.behaviorDepth = previous.behaviorDepth;
        state.surface = previous.surface;
        state.surfaceTopologyMode = previous.surfaceTopologyMode;
        state.surfaceTopologyCell = previous.surfaceTopologyCell;
        if (version == 10u && previous.surfaceTopologyMode == 0u) {
            state.surfaceTopologyMode =
                previous.surfaceTopologyCell != kNoInputSurfaceNoTopologyCell
                ? static_cast<uint32_t>(NoInputSurfaceTopologyMode::Cell)
                : static_cast<uint32_t>(NoInputSurfaceTopologyMode::Base);
        } else if (version == 10u) {
            state.surfaceTopologyMode = static_cast<uint32_t>(
                NoInputSurfaceTopologyMode::Cell);
            captureLegacyNearestAnchor = true;
        }
    } else if (version == 9u) {
        Version9SavedState previous;
        previous.version = version;
        if (!readExact(reinterpret_cast<uint8_t*>(&previous)
                + sizeof(version), sizeof(previous) - sizeof(version))) {
            return false;
        }
        state.params = previous.params;
        state.selectedLane = previous.selectedLane;
        state.selectedSlot = previous.selectedSlot;
        state.selectedSource = previous.selectedSource;
        state.selectedDestination = previous.selectedDestination;
        state.guiPage = previous.guiPage;
        state.matrixMidiMode = previous.matrixMidiMode;
        state.matrixMidiSign = previous.matrixMidiSign;
        state.matrixMidiRampMs = previous.matrixMidiRampMs;
        state.auxMute = previous.auxMute;
        state.behavior = previous.behavior;
        state.behaviorDepth = previous.behaviorDepth;
        state.surface = previous.surface;
    } else if (version == 8u) {
        Version8SavedState previous;
        previous.version = version;
        if (!readExact(reinterpret_cast<uint8_t*>(&previous)
                + sizeof(version), sizeof(previous) - sizeof(version))) {
            return false;
        }
        state.params = previous.params;
        state.selectedLane = previous.selectedLane;
        state.selectedSlot = previous.selectedSlot;
        state.selectedSource = previous.selectedSource;
        state.selectedDestination = previous.selectedDestination;
        state.guiPage = previous.guiPage;
        state.matrixMidiMode = previous.matrixMidiMode;
        state.matrixMidiSign = previous.matrixMidiSign;
        state.matrixMidiRampMs = previous.matrixMidiRampMs;
        state.auxMute = previous.auxMute;
        state.behavior = previous.behavior;
        state.behaviorDepth = previous.params.motion;
        state.surface = migrateLegacySurfaceV8(previous.surface);
    } else if (version == 7u) {
        Version7SavedState previous;
        previous.version = version;
        if (!readExact(reinterpret_cast<uint8_t*>(&previous)
                + sizeof(version), sizeof(previous) - sizeof(version))) {
            return false;
        }
        state.params = previous.params;
        state.selectedLane = previous.selectedLane;
        state.selectedSlot = previous.selectedSlot;
        state.selectedSource = previous.selectedSource;
        state.selectedDestination = previous.selectedDestination;
        state.guiPage = previous.guiPage;
        state.matrixMidiMode = previous.matrixMidiMode;
        state.matrixMidiSign = previous.matrixMidiSign;
        state.auxMute = previous.auxMute;
        state.behavior = previous.behavior;
        state.behaviorDepth = previous.params.motion;
        state.surface = migrateLegacySurfaceV8(previous.surface);
    } else if (version == 6u) {
        Version6SavedState previous;
        previous.version = version;
        if (!readExact(reinterpret_cast<uint8_t*>(&previous)
                + sizeof(version), sizeof(previous) - sizeof(version))) {
            return false;
        }
        state.params = previous.params;
        state.selectedLane = previous.selectedLane;
        state.selectedSlot = previous.selectedSlot;
        state.selectedSource = previous.selectedSource;
        state.selectedDestination = previous.selectedDestination;
        state.guiPage = previous.guiPage;
        state.matrixMidiMode = previous.matrixMidiMode;
        state.auxMute = previous.auxMute;
        state.behavior = previous.behavior;
        state.behaviorDepth = previous.params.motion;
        state.surface = migrateLegacySurfaceV8(previous.surface);
    } else if (version == 5u) {
        Version5SavedState previous;
        previous.version = version;
        if (!readExact(reinterpret_cast<uint8_t*>(&previous)
                + sizeof(version), sizeof(previous) - sizeof(version))) {
            return false;
        }
        state.params = previous.params;
        state.selectedLane = previous.selectedLane;
        state.selectedSlot = previous.selectedSlot;
        state.selectedSource = previous.selectedSource;
        state.selectedDestination = previous.selectedDestination;
        state.guiPage = previous.guiPage;
        state.auxMute = previous.auxMute;
        state.behavior = previous.behavior;
        state.behaviorDepth = previous.params.motion;
        state.surface = migrateLegacySurfaceV8(previous.surface);
    } else if (version == 4u) {
        Version4SavedState previous;
        previous.version = version;
        if (!readExact(reinterpret_cast<uint8_t*>(&previous)
                + sizeof(version), sizeof(previous) - sizeof(version))) {
            return false;
        }
        state.params = migrateV4Params(previous.params);
        state.selectedLane = previous.selectedLane;
        state.selectedSlot = previous.selectedSlot;
        state.selectedSource = previous.selectedSource;
        state.selectedDestination = previous.selectedDestination;
        state.guiPage = previous.guiPage;
        state.auxMute = previous.auxMute;
        state.behavior = previous.behavior;
    } else if (version == 3u) {
        Version3SavedState previous;
        previous.version = version;
        if (!readExact(reinterpret_cast<uint8_t*>(&previous)
                + sizeof(version), sizeof(previous) - sizeof(version))) {
            return false;
        }
        state.params = migrateV4Params(previous.params);
        state.selectedLane = previous.selectedLane;
        state.selectedSlot = previous.selectedSlot;
        state.selectedSource = previous.selectedSource;
        state.selectedDestination = previous.selectedDestination;
        state.guiPage = previous.guiPage;
        state.auxMute = previous.auxMute;
    } else if (version == 2u) {
        Version2SavedState previous;
        previous.version = version;
        if (!readExact(reinterpret_cast<uint8_t*>(&previous)
                + sizeof(version), sizeof(previous) - sizeof(version))) {
            return false;
        }
        state.params = migrateV4Params(previous.params);
        state.selectedLane = previous.selectedLane;
        state.selectedSlot = previous.selectedSlot;
        state.selectedSource = previous.selectedSource;
        state.selectedDestination = previous.selectedDestination;
        state.guiPage = previous.guiPage;
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
    if (version <= 4u) state.behaviorDepth = state.params.motion;
    if (version < 10u) {
        // Versions one through nine followed the nearest cell. Capture that
        // nearest cell once so reopening an older project preserves its
        // saved location without retaining boundary-triggered topology jumps.
        state.surfaceTopologyMode = static_cast<uint32_t>(
            NoInputSurfaceTopologyMode::Cell);
        captureLegacyNearestAnchor = true;
    }
    if (state.version != kStateVersion) {
        state.version = kStateVersion;
    }
    auto* p = self(plugin);
    NoInputSurfaceSnapshot loaded;
    loaded.params = s3g::sanitizeNoInputMixerParams(state.params);
    loaded.behavior = s3g::sanitizeNoInputMovementBehaviorParams(
        state.behavior);
    loaded.behaviorDepth = std::clamp(std::isfinite(state.behaviorDepth)
        ? state.behaviorDepth : loaded.params.motion, 0.0f, 1.0f);
    for (uint32_t bus = 0u; bus < 2u; ++bus) {
        loaded.auxMute[bus] = state.auxMute[bus] != 0u ? 1u : 0u;
    }
    p->surface = state.surface;
    s3g::sanitizeParameterSurface(p->surface);
    p->surfaceTopologyMode = std::min<uint32_t>(state.surfaceTopologyMode,
        static_cast<uint32_t>(NoInputSurfaceTopologyMode::Count) - 1u);
    p->surfaceTopologyCell = state.surfaceTopologyCell < p->surface.cellCount
            && p->surface.cells[state.surfaceTopologyCell].active
        ? state.surfaceTopologyCell : kNoInputSurfaceNoTopologyCell;
    if (captureLegacyNearestAnchor) {
        const auto weights = s3g::parameterSurfaceWeights(p->surface,
            loaded.params.surfaceX, loaded.params.surfaceY);
        p->surfaceTopologyCell = weights.activeCount > 0u
            ? weights.nearest : kNoInputSurfaceNoTopologyCell;
    }
    if (p->surfaceTopologyMode == static_cast<uint32_t>(
            NoInputSurfaceTopologyMode::Cell)
        && p->surfaceTopologyCell == kNoInputSurfaceNoTopologyCell) {
        p->surfaceTopologyMode = static_cast<uint32_t>(
            NoInputSurfaceTopologyMode::Base);
    }
    for (uint32_t index = 0u; index < p->surface.cellCount; ++index) {
        auto& snapshot = p->surface.cells[index].params;
        snapshot.params = s3g::sanitizeNoInputMixerParams(snapshot.params);
        snapshot.behavior = s3g::sanitizeNoInputMovementBehaviorParams(
            snapshot.behavior);
        snapshot.behaviorDepth = std::clamp(
            std::isfinite(snapshot.behaviorDepth)
                ? snapshot.behaviorDepth : snapshot.params.motion,
            0.0f, 1.0f);
        for (auto& muted : snapshot.auxMute) muted = muted != 0u;
    }
    p->selectedLane.store(std::min<uint32_t>(state.selectedLane,
        kChannelCount - 1u), std::memory_order_relaxed);
    p->selectedSlot.store(std::min<uint32_t>(state.selectedSlot,
        s3g::kNoInputMixerInsertSlots - 1u), std::memory_order_relaxed);
    p->selectedSource.store(std::min<uint32_t>(state.selectedSource,
        kChannelCount - 1u), std::memory_order_relaxed);
    p->selectedDestination.store(std::min<uint32_t>(
        state.selectedDestination, kChannelCount - 1u),
        std::memory_order_relaxed);
    p->guiPage.store(std::min<uint32_t>(state.guiPage, kPageCount - 1u),
        std::memory_order_relaxed);
    p->matrixMidiMode.store(std::min<uint32_t>(state.matrixMidiMode,
        static_cast<uint32_t>(s3g::NoInputMatrixMidiMode::Count) - 1u),
        std::memory_order_relaxed);
    p->matrixMidiSign.store(std::min<uint32_t>(state.matrixMidiSign,
        static_cast<uint32_t>(s3g::NoInputMatrixMidiSign::Count) - 1u),
        std::memory_order_relaxed);
    p->matrixMidiRampMs.store(std::clamp(
        std::isfinite(state.matrixMidiRampMs) ? state.matrixMidiRampMs
            : s3g::kNoInputMatrixMidiRampDefaultMs,
        s3g::kNoInputMatrixMidiRampMinimumMs,
        s3g::kNoInputMatrixMidiRampMaximumMs),
        std::memory_order_relaxed);
    p->outputFormat.store(static_cast<uint32_t>(
        s3g::sanitizeRingOutputFormat(state.outputFormat)),
        std::memory_order_relaxed);
    p->outputRotationDegrees.store(s3g::sanitizeRingOutputRotation(
        state.outputRotationDegrees), std::memory_order_relaxed);
    publishUiSnapshot(*p, loaded);
    publishUiParameter(*p, kMatrixMidiModeParamId,
        p->matrixMidiMode.load(std::memory_order_relaxed));
    publishUiParameter(*p, kMatrixMidiSignParamId,
        p->matrixMidiSign.load(std::memory_order_relaxed));
    publishUiParameter(*p, kMatrixMidiRampParamId,
        p->matrixMidiRampMs.load(std::memory_order_relaxed));
    publishUiParameter(*p, kOutputFormatParamId,
        p->outputFormat.load(std::memory_order_relaxed));
    publishUiParameter(*p, kOutputRotationParamId,
        p->outputRotationDegrees.load(std::memory_order_relaxed));
    if (p->active.load(std::memory_order_acquire)) {
        if (!enqueueGuiCommand(*p, { GuiCommandType::ApplyUiSnapshot,
                CLAP_INVALID_ID, 0.62, 0u, loaded.params.seed })) {
            return false;
        }
    } else {
        p->params = loaded.params;
        p->behavior = loaded.behavior;
        p->behaviorDepth = loaded.behaviorDepth;
        p->auxMute = loaded.auxMute;
        clearMidiMatrixGrid(*p);
        p->matrixFeedbackSent.fill(false);
        markAllMatrixFeedbackDirty(*p);
        p->nrpnFeedbackSent.fill(false);
        markAllNrpnFeedbackDirty(*p);
        syncMixerState(*p);
        p->mixer.reseed(p->params.seed, 0.62f);
        resetMeters(*p);
    }
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)

namespace {

enum OpenMenu : int {
    kMenuNone = -1,
    kMenuPreset = 0,
    kMenuRandomEnergy,
    kMenuLane,
    kMenuSource,
    kMenuDestination,
    kMenuSlot0,
    kMenuSlot1,
    kMenuSlot2,
    kMenuMotionShape,
    kMenuBehavior,
    kMenuQuality,
    kMenuMixerInsert,
    kMenuMixerAux,
    kMenuReactMode,
    kMenuFieldDivision,
    kMenuEventDivision,
    kMenuAuxTapA,
    kMenuAuxTapB,
    kMenuOutputFormat,
};

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

NSRect channelInsertMenuRect(uint32_t slot)
{
    const auto& panel =
        s3g::gui_layout::kNoInputMixerFamilyLayout.inserts;
    NSRect rect = processorMenuRect(panel, slot);
    rect.size.width -= 58.0;
    return rect;
}

NSRect channelInsertEditRect(uint32_t slot)
{
    const auto& panel =
        s3g::gui_layout::kNoInputMixerFamilyLayout.inserts;
    NSRect rect = processorMenuRect(panel, slot);
    rect.origin.x = NSMaxX(rect) - 52.0;
    rect.size.width = 52.0;
    return rect;
}

NSRect movementBankButtonRect(uint32_t bank)
{
    const auto& panel =
        s3g::gui_layout::kNoInputMixerFamilyLayout.movement;
    return NSMakeRect(panel.frame.x + panel.frame.width - 153.0
            + static_cast<CGFloat>(bank) * 48.0,
        panel.frame.y + 7.0, 44.0, 15.0);
}

NSRect fieldTabRect(uint32_t index)
{
    const auto& family = s3g::gui_layout::kNoInputMixerFamilyLayout;
    constexpr CGFloat width = 52.0;
    constexpr CGFloat gap = 3.0;
    return NSMakeRect(family.fieldPanel.x + family.fieldPanel.width
            - static_cast<CGFloat>(kPageCount) * width
            - static_cast<CGFloat>(kPageCount - 1u) * gap - 10.0
            + static_cast<CGFloat>(index) * (width + gap),
        family.fieldPanel.y + 4.0, width, 14.0);
}

NSRect mixerPopButtonRect()
{
    const auto& field =
        s3g::gui_layout::kNoInputMixerFamilyLayout.fieldPanel;
    return NSMakeRect(fieldTabRect(0u).origin.x - 56.0,
        field.y + 4.0, 48.0, 14.0);
}

NSRect widePageRect()
{
    return NSMakeRect(28.0, 78.0, 1300.0, 714.0);
}

NSRect auxPageColumnRect(uint32_t lane)
{
    const NSRect page = widePageRect();
    constexpr CGFloat gap = 8.0;
    const CGFloat width = (page.size.width - 28.0 - gap * 7.0) / 8.0;
    return NSMakeRect(page.origin.x + 14.0 + lane * (width + gap),
        page.origin.y + 54.0, width, page.size.height - 72.0);
}

NSRect auxPageTrackRect(uint32_t lane, uint32_t row)
{
    const NSRect column = auxPageColumnRect(lane);
    return NSMakeRect(column.origin.x + 12.0,
        column.origin.y + 70.0 + row * 84.0,
        column.size.width - 24.0, 11.0);
}

NSRect auxPageTapRect(uint32_t lane, uint32_t bus)
{
    NSRect rect = auxPageTrackRect(lane, bus == 0u ? 1u : 4u);
    rect.origin.y -= 5.0;
    rect.size.height = 21.0;
    return rect;
}

NSRect surfacePlotRect()
{
    const NSRect page = widePageRect();
    return NSMakeRect(page.origin.x + 14.0, page.origin.y + 116.0,
        page.size.width - 28.0, page.size.height - 134.0);
}

NSRect surfaceButtonRect(uint32_t index)
{
    const NSRect page = widePageRect();
    static constexpr CGFloat widths[] {
        62.0, 62.0, 54.0, 54.0, 54.0,
    };
    CGFloat x = page.origin.x + 14.0;
    for (uint32_t item = 0u; item < index; ++item) x += widths[item] + 6.0;
    return NSMakeRect(x, page.origin.y + 43.0, widths[index], 22.0);
}

NSRect surfaceCurveRect()
{
    const NSRect page = widePageRect();
    return NSMakeRect(page.origin.x + 14.0, page.origin.y + 76.0,
        112.0, 22.0);
}

NSRect surfaceFocusRect(uint32_t index)
{
    const NSRect page = widePageRect();
    return NSMakeRect(page.origin.x + 186.0
            + static_cast<CGFloat>(index) * 26.0,
        page.origin.y + 76.0, 22.0, 22.0);
}

NSRect surfaceGlideRect(uint32_t index)
{
    const NSRect page = widePageRect();
    return NSMakeRect(page.origin.x + 360.0
            + static_cast<CGFloat>(index) * 26.0,
        page.origin.y + 76.0, 22.0, 22.0);
}

NSRect surfaceTopologyRect()
{
    const NSRect page = widePageRect();
    return NSMakeRect(page.origin.x + 500.0, page.origin.y + 76.0,
        138.0, 22.0);
}

NSRect movementGlobalToggleRect(uint32_t index)
{
    const auto& panel = s3g::gui_layout::kNoInputMixerFamilyLayout.movement;
    const CGFloat x = s3g::gui_layout::processorControlX(panel.frame.x);
    const CGFloat width = 52.0;
    return NSMakeRect(x + index * (width + 5.0),
        s3g::gui_layout::rowY(panel, 8u) - 1.0, width, 17.0);
}

NSRect movementSectionRandomRect()
{
    const auto& panel = s3g::gui_layout::kNoInputMixerFamilyLayout.movement;
    return NSMakeRect(panel.frame.x + panel.frame.width - 199.0,
        panel.frame.y + 7.0, 40.0, 15.0);
}

NSRect reactDirectionButtonRect()
{
    const auto& panel = s3g::gui_layout::kNoInputMixerFamilyLayout.movement;
    return processorMenuRect(panel, 5u);
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

NSRect matrixMidiModeButtonRect(uint32_t index)
{
    const NSRect visual = patchVisualRect();
    return NSMakeRect(visual.origin.x + 18.0
            + static_cast<CGFloat>(index) * 96.0,
        visual.origin.y + 10.0, 90.0, 18.0);
}

NSRect matrixMidiSignButtonRect(uint32_t index)
{
    const NSRect visual = patchVisualRect();
    return NSMakeRect(visual.origin.x + 222.0
            + static_cast<CGFloat>(index) * 86.0,
        visual.origin.y + 10.0, 80.0, 18.0);
}

NSRect matrixMidiRampTrackRect()
{
    const NSRect visual = patchVisualRect();
    return NSMakeRect(visual.origin.x + 459.0,
        visual.origin.y + 17.0, 142.0, 4.0);
}

NSRect matrixMidiRampHitRect()
{
    const NSRect track = matrixMidiRampTrackRect();
    return NSMakeRect(track.origin.x - 42.0, track.origin.y - 9.0,
        track.size.width + 123.0, 22.0);
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

NSRect popupInsertMenuRect(NSRect strip, uint32_t slot)
{
    NSRect rect = popupInsertRect(strip, slot);
    rect.size.width -= 31.0;
    return rect;
}

NSRect popupInsertEditRect(NSRect strip, uint32_t slot)
{
    NSRect rect = popupInsertRect(strip, slot);
    rect.origin.x = NSMaxX(rect) - 28.0;
    rect.size.width = 28.0;
    return rect;
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

NSRect popupAuxTypeMenuRect(uint32_t bus)
{
    NSRect rect = popupAuxTypeRect(bus);
    rect.size.width = 146.0;
    return rect;
}

NSRect popupAuxTypeEditRect(uint32_t bus)
{
    NSRect rect = popupAuxTypeRect(bus);
    rect.origin.x += 150.0;
    rect.size.width = 54.0;
    return rect;
}

NSRect popupAuxMuteRect(uint32_t bus)
{
    const NSRect panel = popupAuxPanelRect();
    return NSMakeRect(panel.origin.x + 14.0,
        panel.origin.y + 58.0 + bus * 248.0, 82.0, 18.0);
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

void drawPolarityButton(NSRect rect, NSString* text, bool active,
    int rgb, NSDictionary* attrs)
{
    [mixerColor(active ? rgb : 0x151515, active ? 0.34 : 1.0) setFill];
    NSRectFill(rect);
    [mixerColor(active ? rgb : 0x555555) setStroke];
    NSFrameRect(rect);
    const NSSize size = [text sizeWithAttributes:attrs];
    [text drawAtPoint:NSMakePoint(
        rect.origin.x + (rect.size.width - size.width) * 0.5,
        rect.origin.y + (rect.size.height - size.height) * 0.5 - 0.5)
        withAttributes:attrs];
}

struct EffectControlLabels {
    const char* gain;
    const char* tone;
    const char* bias;
};

EffectControlLabels effectControlLabels(s3g::NoInputDistortionType type)
{
    using Type = s3g::NoInputDistortionType;
    switch (type) {
    case Type::Wool: return { "SUSTAIN", "CONTOUR", "ASYM" };
    case Type::Rat: return { "DIST", "FILTER", "BIAS" };
    case Type::ZoneA:
    case Type::ZoneB: return { "DIST", "FOCUS", "BIAS" };
    case Type::FuzzI: return { "GAIN", "SAG", "BIAS" };
    case Type::FuzzII: return { "GAIN", "GATE", "BIAS" };
    case Type::Diode: return { "GAIN", "SHAPE", "BIAS" };
    case Type::Ring: return { "DEPTH", "CARRIER", "BIAS" };
    case Type::Relay: return { "THRESH", "CHATTER", "ASYM" };
    case Type::Crush: return { "BITS", "RATE", "DITHER" };
    case Type::Splice: return { "MIX", "LENGTH", "DIRECTION" };
    case Type::Logic: return { "DEPTH", "THRESH", "BALANCE" };
    case Type::Shred: return { "DRIVE", "TILT", "ASYM" };
    case Type::Void: return { "DEPTH", "RECOVER", "SKEW" };
    case Type::Rotor: return { "DEPTH", "RATE", "SHAPE" };
    case Type::Phase: return { "DEPTH", "RATE", "CENTER" };
    case Type::Chorus: return { "MIX", "RATE", "REGEN" };
    case Type::Throat: return { "MIX", "VOWEL", "SHIFT" };
    case Type::Robot: return { "DEPTH", "CARRIER", "CHARACTER" };
    case Type::OctDown: return { "MIX", "FILTER", "TRACK" };
    case Type::OctUp: return { "MIX", "TONE", "SHAPE" };
    case Type::OctStack: return { "MIX", "TONE", "BALANCE" };
    case Type::Bypass:
    case Type::Count: return { "AMOUNT", "TONE", "BIAS" };
    }
    return { "AMOUNT", "TONE", "BIAS" };
}

NSRect effectEditorTrackRect(uint32_t row)
{
    return NSMakeRect(126.0, 88.0 + row * 40.0, 292.0, 12.0);
}

NSRect effectEditorToggleRect(uint32_t row)
{
    return NSMakeRect(126.0, 82.0 + row * 40.0, 292.0, 24.0);
}

} // namespace

@class S3GNoInputEffectView;

@interface S3GNoInputMixerView : NSView <NSWindowDelegate> {
    void* _plugin;
    clap_id _dragParam;
    NSTimer* _timer;
    int _openMenu;
    int _hoverMenuItem;
    NSRect _menuAnchor;
    uint32_t _effectMenuLane;
    uint32_t _effectMenuSlot;
    uint32_t _effectMenuBus;
    uint32_t _randomEnergyProfile;
    uint32_t _movementBank;
    char _titlePresetName[64];
    clap_id _mixerDragParam;
    NSRect _mixerDragRect;
    double _mixerDragMinimum;
    double _mixerDragMaximum;
    BOOL _mixerDragVertical;
    BOOL _mixerPopupChild;
    S3GNoInputMixerView* _mixerPopupOwner;
    NSPanel* _pagePanels[kPageCount];
    S3GNoInputMixerView* _pagePopupViews[kPageCount];
    NSPanel* _effectPanel;
    S3GNoInputEffectView* _effectEditor;
    uint32_t _lockedPage;
    BOOL _wiringGridMode;
    int _wireDragSource;
    NSPoint _wireDragPoint;
    BOOL _surfaceEdit;
    int _selectedSurfaceCell;
    int _surfaceDragCell;
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
- (void)openEffectEditorForLane:(uint32_t)lane slot:(uint32_t)slot;
- (void)openEffectEditorForAux:(uint32_t)bus;
- (void)hideEffectEditor;
- (void)destroyEffectEditor;
- (NSPanel*)effectPanel;
- (NSPanel*)mixerPanel;
- (NSPanel*)patchPanel;
- (NSPanel*)channelPanel;
- (NSPanel*)safetyPanel;
- (NSPanel*)auxPanel;
- (NSInteger)hoverMenuItem;
- (uint32_t)activePage;
- (void)navigatePageBy:(NSInteger)delta;
- (void)clearAllConnections;
- (void)updateMixerDrag:(NSPoint)point;
- (void)updateSurfaceDrag:(NSPoint)point;
- (void)beginMixerDrag:(clap_id)param rect:(NSRect)rect
    minimum:(double)minimum maximum:(double)maximum
    vertical:(BOOL)vertical point:(NSPoint)point;
- (void)drawPerformanceMixer:(Plugin*)plugin surface:(NSRect)surface;
- (void)drawAuxTopology:(Plugin*)plugin label:(NSDictionary*)label
    value:(NSDictionary*)value;
@end

@interface S3GNoInputEffectView : NSView {
    void* _plugin;
    S3GNoInputMixerView* _owner;
    BOOL _isAux;
    uint32_t _lane;
    uint32_t _slot;
    uint32_t _bus;
    clap_id _dragParam;
    NSRect _dragRect;
    double _dragMinimum;
    double _dragMaximum;
}
- (id)initWithPlugin:(void*)plugin owner:(S3GNoInputMixerView*)owner;
- (void)setLane:(uint32_t)lane slot:(uint32_t)slot;
- (void)setAux:(uint32_t)bus;
- (void)clearOwner;
@end

@implementation S3GNoInputEffectView

- (id)initWithPlugin:(void*)plugin owner:(S3GNoInputMixerView*)owner
{
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, 454.0, 292.0)];
    if (self) {
        _plugin = plugin;
        _owner = owner;
        _isAux = NO;
        _lane = 0u;
        _slot = 0u;
        _bus = 0u;
        _dragParam = CLAP_INVALID_ID;
        _dragRect = NSZeroRect;
        _dragMinimum = 0.0;
        _dragMaximum = 1.0;
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)setLane:(uint32_t)lane slot:(uint32_t)slot
{
    _isAux = NO;
    _lane = std::min<uint32_t>(lane, kChannelCount - 1u);
    _slot = std::min<uint32_t>(slot,
        s3g::kNoInputMixerInsertSlots - 1u);
    _dragParam = CLAP_INVALID_ID;
    [self setNeedsDisplay:YES];
}

- (void)setAux:(uint32_t)bus
{
    _isAux = YES;
    _bus = std::min<uint32_t>(bus, 1u);
    _dragParam = CLAP_INVALID_ID;
    [self setNeedsDisplay:YES];
}

- (void)clearOwner
{
    _owner = nil;
}

- (s3g::NoInputDistortionType)effectType
{
    auto* plugin = static_cast<Plugin*>(_plugin);
    if (!plugin) return s3g::NoInputDistortionType::Bypass;
    const auto visible = uiSnapshot(*plugin);
    return _isAux ? visible.params.aux[_bus].effect.type
        : visible.params.lanes[_lane].inserts[_slot].type;
}

- (uint32_t)controlCount
{
    return 5u;
}

- (clap_id)paramForRow:(uint32_t)row
{
    if (_isAux) {
        const clap_id ids[5] = {
            _bus == 0u ? kAuxAGainParamId : kAuxBGainParamId,
            _bus == 0u ? kAuxAToneParamId : kAuxBToneParamId,
            _bus == 0u ? kAuxABiasParamId : kAuxBBiasParamId,
            _bus == 0u ? kAuxAReturnParamId : kAuxBReturnParamId,
            _bus == 0u ? kAuxAFeedbackParamId : kAuxBFeedbackParamId,
        };
        return row < 5u ? ids[row] : CLAP_INVALID_ID;
    }
    const clap_id offsets[5] = {
        kInsertGainOffset, kInsertToneOffset, kInsertBiasOffset,
        kInsertLevelOffset, kInsertBypassOffset,
    };
    return row < 5u ? insertParamId(_lane, _slot, offsets[row])
                    : CLAP_INVALID_ID;
}

- (NSString*)labelForRow:(uint32_t)row
{
    const EffectControlLabels labels = effectControlLabels([self effectType]);
    if (row == 0u) return [NSString stringWithUTF8String:labels.gain];
    if (row == 1u) return [NSString stringWithUTF8String:labels.tone];
    if (row == 2u) return [NSString stringWithUTF8String:labels.bias];
    if (row == 3u) return _isAux ? @"RETURN" : @"LEVEL";
    return _isAux ? @"LOOP" : @"BYPASS";
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* plugin = static_cast<Plugin*>(_plugin);
    if (!plugin) return;
    const auto visible = uiSnapshot(*plugin);
    s3g::clap_gui::Style style;
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSDictionary* title = s3g::clap_gui::softTitleAttrs();
    NSDictionary* label = s3g::clap_gui::softLabelAttrs();
    NSDictionary* valueAttrs = s3g::clap_gui::softValueAttrs();

    [mixerColor(0x151515) setFill];
    NSRectFill(NSInsetRect([self bounds], 12.0, 12.0));
    [mixerColor(0x595959) setStroke];
    NSFrameRect(NSInsetRect([self bounds], 12.0, 12.0));
    [mixerColor(_isAux ? (_bus == 0u ? 0xc95e3b : 0x57bfc4)
                       : 0xb8b8b8) setFill];
    NSRectFill(NSMakeRect(12.0, 12.0, 5.0, 50.0));

    const auto type = [self effectType];
    NSString* typeName = [NSString stringWithUTF8String:
        s3g::noInputDistortionName(type)];
    [@"EFFECT EDITOR" drawAtPoint:NSMakePoint(28.0, 21.0)
        withAttributes:title];
    NSString* target = _isAux
        ? [NSString stringWithFormat:@"AUX %c  /  %@", 'A' + _bus,
            typeName]
        : [NSString stringWithFormat:@"LANE %u  /  SLOT %u  /  %@",
            _lane + 1u, _slot + 1u, typeName];
    [target drawAtPoint:NSMakePoint(28.0, 43.0) withAttributes:valueAttrs];

    for (uint32_t row = 0u; row < [self controlCount]; ++row) {
        const clap_id id = [self paramForRow:row];
        double current = 0.0;
        ParamRange range;
        if (!uiParameterValue(*plugin, id, current) || !paramRange(id, range))
            continue;
        NSString* name = [self labelForRow:row];
        [name drawAtPoint:NSMakePoint(28.0, 82.0 + row * 40.0)
            withAttributes:label];
        if (!_isAux && row == 4u) {
            drawFlatButton(effectEditorToggleRect(row),
                current >= 0.5 ? @"BYPASSED" : @"ACTIVE",
                current >= 0.5, valueAttrs);
            continue;
        }
        const NSRect track = effectEditorTrackRect(row);
        [mixerColor(0x080808) setFill]; NSRectFill(track);
        [mixerColor(0x454545) setStroke]; NSFrameRect(track);
        const CGFloat normalized = static_cast<CGFloat>(std::clamp(
            (current - range.minimum) / (range.maximum - range.minimum),
            0.0, 1.0));
        NSRect fill = NSInsetRect(track, 1.0, 1.0);
        fill.size.width *= normalized;
        [mixerColor(_isAux ? (_bus == 0u ? 0xc95e3b : 0x57bfc4)
                           : 0xb8b8b8, 0.88) setFill];
        NSRectFill(fill);
        char display[64] {};
        if (type == s3g::NoInputDistortionType::Splice && row == 0u) {
            std::snprintf(display, sizeof(display), "%.0f%% WET",
                (0.25 + current * 0.75) * 100.0);
        } else if (type == s3g::NoInputDistortionType::Splice
            && row == 1u) {
            const double maximumMs = std::min(80.0,
                8190.0 * 1000.0 / std::max(1.0, plugin->sampleRate));
            const double lengthMs = std::pow(maximumMs,
                current * current);
            std::snprintf(display, sizeof(display), "%.1f ms", lengthMs);
        } else if (type == s3g::NoInputDistortionType::Splice
            && row == 2u) {
            std::snprintf(display, sizeof(display), "%s",
                current < -0.12 ? "REVERSE"
                    : (current > 0.12 ? "FORWARD" : "ALTERNATE"));
        } else if (type == s3g::NoInputDistortionType::Rotor
            && row == 1u) {
            std::snprintf(display, sizeof(display), "%.2f Hz",
                0.08 * std::pow(562.5, current));
        } else if (type == s3g::NoInputDistortionType::Rotor
            && row == 2u) {
            std::snprintf(display, sizeof(display), "%s",
                current < -0.12 ? "NARROW"
                    : (current > 0.12 ? "WIDE" : "SINE"));
        } else if (type == s3g::NoInputDistortionType::Phase
            && row == 1u) {
            std::snprintf(display, sizeof(display), "%.2f Hz",
                0.03 * std::pow(266.6667, current));
        } else if (type == s3g::NoInputDistortionType::Phase
            && row == 2u) {
            std::snprintf(display, sizeof(display), "%.0f Hz",
                520.0 * std::pow(2.0, current * 2.0));
        } else if (type == s3g::NoInputDistortionType::Chorus
            && row == 1u) {
            std::snprintf(display, sizeof(display), "%.2f Hz",
                0.05 * std::pow(120.0, current));
        } else if (type == s3g::NoInputDistortionType::Chorus
            && row == 2u) {
            std::snprintf(display, sizeof(display), "%+.0f%%",
                current * 38.0);
        } else if (type == s3g::NoInputDistortionType::Throat
            && row == 2u) {
            std::snprintf(display, sizeof(display), "%+.1f st",
                current * 8.64);
        } else if (type == s3g::NoInputDistortionType::Robot
            && row == 1u) {
            std::snprintf(display, sizeof(display), "%.0f Hz",
                28.0 * std::pow(40.0, current));
        } else if (type == s3g::NoInputDistortionType::Robot
            && row == 2u) {
            std::snprintf(display, sizeof(display), "%.0f%% SQUARE",
                (current + 1.0) * 50.0);
        } else if (type == s3g::NoInputDistortionType::OctDown
            && row == 2u) {
            std::snprintf(display, sizeof(display), "%.3f",
                0.001 + (current + 1.0) * 0.0495);
        } else if (type == s3g::NoInputDistortionType::OctStack
            && row == 2u) {
            std::snprintf(display, sizeof(display), "%s",
                current < -0.12 ? "DOWN"
                    : (current > 0.12 ? "UP" : "EVEN"));
        } else {
            paramsValueToText(nullptr, id, current, display,
                sizeof(display));
        }
        NSString* text = [NSString stringWithUTF8String:display];
        const NSSize textSize = [text sizeWithAttributes:valueAttrs];
        [text drawAtPoint:NSMakePoint(NSMaxX(track) - textSize.width,
            track.origin.y - 18.0) withAttributes:valueAttrs];
    }
    [@"DRAG TRACKS  ·  DOUBLE-CLICK DEFAULT"
        drawAtPoint:NSMakePoint(28.0, 270.0) withAttributes:valueAttrs];
}

- (void)updateDrag:(NSPoint)point
{
    if (_dragParam == CLAP_INVALID_ID || !_owner) return;
    const double normalized = std::clamp(
        (point.x - NSMinX(_dragRect))
            / std::max(1.0, static_cast<double>(_dragRect.size.width)),
        0.0, 1.0);
    [_owner applyGuiParam:_dragParam
        value:_dragMinimum + normalized * (_dragMaximum - _dragMinimum)];
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    [[self window] makeFirstResponder:self];
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    auto* plugin = static_cast<Plugin*>(_plugin);
    if (!plugin || !_owner) return;
    for (uint32_t row = 0u; row < [self controlCount]; ++row) {
        const clap_id id = [self paramForRow:row];
        if (!_isAux && row == 4u) {
            if (!NSPointInRect(point, effectEditorToggleRect(row))) continue;
            double current = 0.0;
            if (uiParameterValue(*plugin, id, current)) {
                [_owner applyGuiParam:id value:current >= 0.5 ? 0.0 : 1.0];
            }
            return;
        }
        const NSRect track = effectEditorTrackRect(row);
        if (!NSPointInRect(point, NSInsetRect(track, -8.0, -8.0)))
            continue;
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(event,
                &plugin->plugin, id, &defaultValue)) {
            [_owner applyGuiParam:id value:defaultValue];
            _dragParam = CLAP_INVALID_ID;
            return;
        }
        ParamRange range;
        if (!paramRange(id, range)) return;
        _dragParam = id;
        _dragRect = track;
        _dragMinimum = range.minimum;
        _dragMaximum = range.maximum;
        [self updateDrag:point];
        return;
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    [self updateDrag:[self convertPoint:[event locationInWindow]
        fromView:nil]];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragParam = CLAP_INVALID_ID;
}

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
        _hoverMenuItem = -1;
        _menuAnchor = NSZeroRect;
        _effectMenuLane = 0u;
        _effectMenuSlot = 0u;
        _effectMenuBus = 0u;
        _randomEnergyProfile = static_cast<uint32_t>(
            s3g::NoInputRandomEnergy::Mid);
        _movementBank = 0u;
        _mixerDragParam = CLAP_INVALID_ID;
        _mixerDragRect = NSZeroRect;
        _mixerDragMinimum = 0.0;
        _mixerDragMaximum = 1.0;
        _mixerDragVertical = NO;
        _mixerPopupChild = NO;
        _mixerPopupOwner = nil;
        for (uint32_t page = 0u; page < kPageCount; ++page) {
            _pagePanels[page] = nil;
            _pagePopupViews[page] = nil;
        }
        _effectPanel = nil;
        _effectEditor = nil;
        _lockedPage = UINT32_MAX;
        _wiringGridMode = NO;
        _wireDragSource = -1;
        _wireDragPoint = NSZeroPoint;
        _surfaceEdit = NO;
        _selectedSurfaceCell = -1;
        _surfaceDragCell = -1;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
            "INIT");
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)updateTrackingAreas
{
    for (NSTrackingArea* area in [self trackingAreas]) {
        [self removeTrackingArea:area];
    }
    NSTrackingArea* area = [[[NSTrackingArea alloc]
        initWithRect:[self bounds]
        options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited
            | NSTrackingActiveInKeyWindow
        owner:self userInfo:nil] autorelease];
    [self addTrackingArea:area];
    [super updateTrackingAreas];
}

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    [[self window] setAcceptsMouseMovedEvents:YES];
}

- (uint32_t)activePage
{
    if (_lockedPage < kPageCount) return _lockedPage;
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
    const uint32_t next = static_cast<uint32_t>((current
        + (delta < 0 ? static_cast<NSInteger>(kPageCount) - 1 : 1))
        % static_cast<NSInteger>(kPageCount));
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
        S3GNoInputMixerView* owner = _mixerPopupChild
            && _mixerPopupOwner ? _mixerPopupOwner : self;
        if (owner->_effectEditor && owner->_effectPanel
            && [owner->_effectPanel isVisible])
            [owner->_effectEditor setNeedsDisplay:YES];
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
    double previous = 0.0;
    uiParameterValue(*plugin, param, previous);
    publishUiParameter(*plugin, param, value);
    double applied = value;
    uiParameterValue(*plugin, param, applied);
    if (!enqueueGuiCommand(*plugin, { GuiCommandType::ParamValue,
            param, applied, 0u, 0u })) {
        publishUiParameter(*plugin, param, previous);
        return;
    }
    [self markPatchCustom];
    [self setNeedsDisplay:YES];
    if (_mixerPopupChild && _mixerPopupOwner) {
        [_mixerPopupOwner setNeedsDisplay:YES];
        for (uint32_t page = 0u; page < kPageCount; ++page) {
            if (_mixerPopupOwner->_pagePopupViews[page])
                [_mixerPopupOwner->_pagePopupViews[page]
                    setNeedsDisplay:YES];
        }
    } else {
        for (uint32_t page = 0u; page < kPageCount; ++page) {
            if (_pagePopupViews[page])
                [_pagePopupViews[page] setNeedsDisplay:YES];
        }
    }
    S3GNoInputMixerView* owner = _mixerPopupChild
        && _mixerPopupOwner ? _mixerPopupOwner : self;
    if (owner->_effectEditor)
        [owner->_effectEditor setNeedsDisplay:YES];
}

- (void)clearAllConnections
{
    auto* plugin = static_cast<Plugin*>(_plugin);
    if (!plugin) return;
    for (uint32_t index = 0u;
         index < s3g::kNoInputMixerMatrixCells; ++index) {
        publishUiParameter(*plugin, kMatrixParamBase + index, 0.0);
    }
    if (!enqueueGuiCommand(*plugin, { GuiCommandType::ClearMatrix })) return;
    [self markPatchCustom];
    [self setNeedsDisplay:YES];
    S3GNoInputMixerView* owner = _mixerPopupChild
        && _mixerPopupOwner ? _mixerPopupOwner : self;
    [owner setNeedsDisplay:YES];
    for (uint32_t page = 0u; page < kPageCount; ++page) {
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

- (void)updateSurfaceDrag:(NSPoint)point
{
    auto* plugin = static_cast<Plugin*>(_plugin);
    if (!plugin || _surfaceDragCell == -1) return;
    const NSRect plot = surfacePlotRect();
    const float x = std::clamp<float>(
        (point.x - NSMinX(plot)) / plot.size.width, 0.0f, 1.0f);
    const float y = std::clamp<float>(
        (NSMaxY(plot) - point.y) / plot.size.height, 0.0f, 1.0f);
    if (_surfaceDragCell == -2) {
        [self applyGuiParam:kSurfaceXParamId value:x];
        [self applyGuiParam:kSurfaceYParamId value:y];
        return;
    }
    const uint32_t index = static_cast<uint32_t>(_surfaceDragCell);
    if (index >= plugin->surface.cellCount) return;
    plugin->surface.cells[index].x = x;
    plugin->surface.cells[index].y = y;
    [self setNeedsDisplay:YES];
}

- (NSPanel*)mixerPanel
{
    return _pagePanels[1u];
}

- (NSPanel*)patchPanel { return _pagePanels[0u]; }
- (NSPanel*)channelPanel { return _pagePanels[2u]; }
- (NSPanel*)safetyPanel { return _pagePanels[3u]; }
- (NSPanel*)auxPanel { return _pagePanels[4u]; }
- (NSInteger)hoverMenuItem { return _hoverMenuItem; }
- (NSPanel*)effectPanel { return _effectPanel; }

- (void)openEffectEditorForLane:(uint32_t)lane slot:(uint32_t)slot
{
    if (_mixerPopupChild && _mixerPopupOwner) {
        [_mixerPopupOwner openEffectEditorForLane:lane slot:slot];
        return;
    }
    if (!_effectPanel) {
        _effectPanel = [[NSPanel alloc] initWithContentRect:NSMakeRect(
                0.0, 0.0, 454.0, 292.0)
            styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                | NSWindowStyleMaskUtilityWindow)
            backing:NSBackingStoreBuffered defer:NO];
        [_effectPanel setReleasedWhenClosed:NO];
        // Lifetime-owned by the plugin view, but deliberately not an
        // NSWindow child. Child utility windows are tied to the host window's
        // Space and can disappear when dragged to another display.
        [_effectPanel setHidesOnDeactivate:NO];
        [_effectPanel setDelegate:self];
        _effectEditor = [[S3GNoInputEffectView alloc]
            initWithPlugin:_plugin owner:self];
        [_effectPanel setContentView:_effectEditor];
        [_effectPanel setContentSize:NSMakeSize(454.0, 292.0)];
        [_effectEditor release];

        NSWindow* parent = [self window];
        const NSRect parentFrame = parent ? [parent frame]
            : [[NSScreen mainScreen] visibleFrame];
        const NSRect panelFrame = [_effectPanel frame];
        NSScreen* screen = parent ? [parent screen] : [NSScreen mainScreen];
        const NSRect visible = screen ? [screen visibleFrame] : parentFrame;
        CGFloat x = NSMaxX(parentFrame) - panelFrame.size.width;
        CGFloat y = NSMinY(parentFrame) - panelFrame.size.height - 8.0;
        if (y < NSMinY(visible)) y = NSMaxY(parentFrame) + 8.0;
        x = std::clamp(x, NSMinX(visible),
            std::max(NSMinX(visible), NSMaxX(visible) - panelFrame.size.width));
        y = std::clamp(y, NSMinY(visible),
            std::max(NSMinY(visible), NSMaxY(visible) - panelFrame.size.height));
        [_effectPanel setFrameOrigin:NSMakePoint(x, y)];
    }
    [_effectEditor setLane:lane slot:slot];
    [_effectPanel setTitle:[NSString stringWithFormat:
        @"s3g EFFECT EDITOR — LANE %u / SLOT %u", lane + 1u, slot + 1u]];
    [_effectPanel makeKeyAndOrderFront:nil];
    [_effectPanel makeFirstResponder:_effectEditor];
}

- (void)openEffectEditorForAux:(uint32_t)bus
{
    if (_mixerPopupChild && _mixerPopupOwner) {
        [_mixerPopupOwner openEffectEditorForAux:bus];
        return;
    }
    // Construct and position the shared editor through the lane path.
    [self openEffectEditorForLane:0u slot:0u];
    bus = std::min<uint32_t>(bus, 1u);
    [_effectEditor setAux:bus];
    [_effectPanel setTitle:[NSString stringWithFormat:
        @"s3g EFFECT EDITOR — AUX %c", 'A' + bus]];
    [_effectPanel makeKeyAndOrderFront:nil];
    [_effectPanel makeFirstResponder:_effectEditor];
}

- (void)hideEffectEditor
{
    if (_mixerPopupChild && _mixerPopupOwner) {
        [_mixerPopupOwner hideEffectEditor];
        return;
    }
    if (!_effectPanel) return;
    [_effectPanel orderOut:nil];
}

- (void)destroyEffectEditor
{
    if (_mixerPopupChild && _mixerPopupOwner) return;
    if (!_effectPanel) return;
    [_effectEditor clearOwner];
    [_effectPanel setDelegate:nil];
    [_effectPanel orderOut:nil];
    [_effectPanel release];
    _effectPanel = nil;
    _effectEditor = nil;
}

- (void)openPagePopup:(uint32_t)page
{
    if (_mixerPopupChild || page >= kPageCount) return;
    static NSString* pageNames[kPageCount] = {
        @"PATCH", @"MIXER", @"CHANNEL", @"SAFETY", @"AUX",
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
        // Keep detached pages independent from the host NSWindow so a page
        // moved to another display stays in that display's active Space. The
        // owning plugin view still hides and destroys every panel through the
        // CLAP GUI lifecycle below.
        [_pagePanels[page] setHidesOnDeactivate:NO];
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
    auto* plugin = static_cast<Plugin*>(_plugin);
    if (plugin && plugin->guiPage.load(std::memory_order_relaxed) == page) {
        for (uint32_t candidate = 1u; candidate <= kPageCount; ++candidate) {
            const uint32_t next = (page + candidate) % kPageCount;
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
    [self hideEffectEditor];
    for (uint32_t page = 0u; page < kPageCount; ++page) {
        if (!_pagePanels[page]) continue;
        [_pagePopupViews[page] stopRefreshTimer];
        [_pagePanels[page] orderOut:nil];
    }
    [self setNeedsDisplay:YES];
}

- (void)destroyMixerPopup
{
    [self destroyEffectEditor];
    for (uint32_t page = 0u; page < kPageCount; ++page) {
        if (!_pagePanels[page]) continue;
        [_pagePopupViews[page] stopRefreshTimer];
        [_pagePanels[page] setDelegate:nil];
        [_pagePanels[page] orderOut:nil];
        [_pagePanels[page] release];
        _pagePanels[page] = nil;
        _pagePopupViews[page] = nil;
    }
}

- (void)windowWillClose:(NSNotification*)notification
{
    if ([notification object] == _effectPanel) {
        return;
    }
    for (uint32_t page = 0u; page < kPageCount; ++page) {
        if ([notification object] != _pagePanels[page]) continue;
        [_pagePopupViews[page] stopRefreshTimer];
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
    const auto visible = uiSnapshot(*plugin);
    [mixerColor(0x101010) setFill]; NSRectFill(rect);
    [mixerColor(0x454545) setStroke]; NSFrameRect(rect);
    const uint32_t matrixMidiMode = plugin->matrixMidiMode.load(
        std::memory_order_relaxed);
    drawFlatButton(matrixMidiModeButtonRect(0u), @"BU16 FLIP",
        matrixMidiMode == static_cast<uint32_t>(
            s3g::NoInputMatrixMidiMode::Flip), valueAttrs);
    drawFlatButton(matrixMidiModeButtonRect(1u), @"BU16 LATCH",
        matrixMidiMode == static_cast<uint32_t>(
            s3g::NoInputMatrixMidiMode::Latch), valueAttrs);
    const uint32_t matrixMidiSign = plugin->matrixMidiSign.load(
        std::memory_order_relaxed);
    drawPolarityButton(matrixMidiSignButtonRect(0u), @"NEW +",
        matrixMidiSign == static_cast<uint32_t>(
            s3g::NoInputMatrixMidiSign::Positive), 0xc95e3b, valueAttrs);
    drawPolarityButton(matrixMidiSignButtonRect(1u), @"NEW -",
        matrixMidiSign == static_cast<uint32_t>(
            s3g::NoInputMatrixMidiSign::Negative), 0x57bfc4, valueAttrs);
    const float matrixMidiRampMs = plugin->matrixMidiRampMs.load(
        std::memory_order_relaxed);
    const CGFloat rampNorm = std::sqrt(std::clamp<CGFloat>(
        (matrixMidiRampMs - s3g::kNoInputMatrixMidiRampMinimumMs)
            / (s3g::kNoInputMatrixMidiRampMaximumMs
                - s3g::kNoInputMatrixMidiRampMinimumMs),
        0.0, 1.0));
    const NSRect rampTrack = matrixMidiRampTrackRect();
    [@"RAMP" drawAtPoint:NSMakePoint(rampTrack.origin.x - 40.0,
        rampTrack.origin.y - 5.0) withAttributes:valueAttrs];
    [mixerColor(0x080808) setFill]; NSRectFill(rampTrack);
    [mixerColor(0x454545) setStroke]; NSFrameRect(rampTrack);
    [mixerColor(0xc95e3b, 0.88) setFill];
    NSRect rampFill = NSInsetRect(rampTrack, 1.0, 1.0);
    rampFill.size.width *= rampNorm;
    NSRectFill(rampFill);
    [mixerColor(0xe0e0e0) setFill];
    NSRectFill(NSMakeRect(rampTrack.origin.x
            + rampNorm * rampTrack.size.width - 1.5,
        rampTrack.origin.y - 2.0, 3.0, rampTrack.size.height + 4.0));
    [[NSString stringWithFormat:@"%.0f MS", matrixMidiRampMs]
        drawAtPoint:NSMakePoint(NSMaxX(rampTrack) + 8.0,
            rampTrack.origin.y - 5.0) withAttributes:valueAttrs];
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
            const float stored = displayedMatrixGain(*plugin, index);
            const CGFloat manual = std::abs(stored);
            if (manual <= 0.001f) continue;
            const NSPoint a = wiringPortPoint(false, source);
            const NSPoint b = wiringPortPoint(true, destination);
            NSPoint c1;
            NSPoint c2;
            wiringControlPoints(a, b, visible.params.vortex, c1, c2);
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
    const auto stages = routeStageReadout(*plugin, visible, selectedSource,
        selectedDestination);
    [[NSString stringWithFormat:
        @"BASE %.2f  →  FIELD %.2f  →  BEHAV %.2f  →  RESP %.2f  =  %.2f  ·  CHOKE %.2f",
        std::abs(stages.base), stages.field, stages.behavior,
        stages.response, stages.effective, stages.choke]
        drawAtPoint:NSMakePoint(rect.origin.x + 18.0,
            NSMaxY(rect) - 70.0) withAttributes:valueAttrs];
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
    const auto visible = uiSnapshot(*plugin);
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
    auto motionWeights = s3g::noInputMixerMotionWeights(visible.params,
        plugin->motionPhase.load(std::memory_order_relaxed));
    if (visible.behavior.behavior == s3g::NoInputMovementBehavior::Step) {
        for (uint32_t index = 0u;
             index < s3g::kNoInputMixerMatrixCells; ++index) {
            motionWeights[index] = s3g::lerp(motionWeights[index],
                plugin->behaviorRouteGate[index].load(
                    std::memory_order_relaxed), visible.behaviorDepth);
        }
    }
    std::array<float, kChannelCount> activeMotionPeak {};
    std::array<uint32_t, kChannelCount> activeMotionRouteCount {};
    for (uint32_t destination = 0u; destination < kChannelCount;
         ++destination) {
        for (uint32_t source = 0u; source < kChannelCount; ++source) {
            const uint32_t index = destination * kChannelCount + source;
            if (std::abs(displayedMatrixGain(*plugin, index)) <= 0.001f)
                continue;
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
            const float gain = displayedMatrixGain(*plugin,
                destination * kChannelCount + source);
            const CGFloat x = gridLeft + spacing * source;
            const CGFloat y = gridTop + spacing * destination;
            const NSRect node = NSMakeRect(x - 7.0, y - 7.0, 14.0, 14.0);
            [mixerColor(0x141414) setFill];
            NSRectFill(node);
            if (std::abs(gain) > 0.001f) {
                const uint32_t index =
                    destination * kChannelCount + source;
                CGFloat motion = s3g::noInputMixerMotionGainScale(
                    motionWeights[index], activeMotionPeak[source],
                    activeMotionRouteCount[source], visible.params.motion);
                if (s3g::noInputMovementBehaviorUsesAmplitude(
                        visible.behavior.behavior)) {
                    const float articulation =
                        plugin->behaviorRouteGate[index].load(
                            std::memory_order_relaxed);
                    motion *= s3g::lerp(1.0f, articulation,
                        visible.behaviorDepth);
                }
                if (visible.params.reactMode
                        != s3g::NoInputReactMode::Off
                    && visible.params.reactDepth > 1.0e-6f) {
                    const float response = 0.0316227766f
                        + plugin->reactRouteGate[index].load(
                            std::memory_order_relaxed) * 0.9683772234f;
                    motion *= s3g::lerp(1.0f, response,
                        visible.params.reactDepth);
                }
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
    constexpr CGFloat motionRailMargin = 18.0;
    const NSRect motionRail = NSMakeRect(
        gridLeft - motionRailMargin, rect.origin.y + 598.0,
        gridExtent + motionRailMargin * 2.0, 38.0);
    [mixerColor(0x0a0a0a) setFill]; NSRectFill(motionRail);
    [mixerColor(0x454545) setStroke]; NSFrameRect(motionRail);
    const float rateHz = visible.behavior.behavior
            == s3g::NoInputMovementBehavior::Glide
        ? s3g::noInputMixerMotionRateHz(visible.params.motionRate)
        : s3g::noInputMovementEventRateHz(visible.behavior.eventRate);
    [[NSString stringWithFormat:
        @"ROUTE GAIN · %@ / %@ %.0f%% · RESP %@ %.0f%% · FIELD %.0f%% · %.2f Hz",
        [NSString stringWithUTF8String:s3g::matrixFlowShapeName(
            visible.params.motionShape)],
        [NSString stringWithUTF8String:s3g::noInputMovementBehaviorName(
            visible.behavior.behavior)],
        visible.behaviorDepth * 100.0f,
        [NSString stringWithUTF8String:s3g::noInputReactModeName(
            visible.params.reactMode)],
        visible.params.reactDepth * 100.0f,
        visible.params.motion * 100.0f, rateHz]
        drawAtPoint:NSMakePoint(motionRail.origin.x + 8.0,
            motionRail.origin.y + 4.0) withAttributes:valueAttrs];
    const auto stages = routeStageReadout(*plugin, visible, selectedSource,
        selectedDestination);
    [[NSString stringWithFormat:
        @"S%u>D%u  BASE %.2f · FIELD %.2f · BEHAV %.2f · RESP %.2f · EFF %.2f · CHOKE %.2f",
        selectedSource + 1u, selectedDestination + 1u,
        std::abs(stages.base), stages.field, stages.behavior,
        stages.response, stages.effective, stages.choke]
        drawAtPoint:NSMakePoint(motionRail.origin.x + 8.0,
            motionRail.origin.y + 19.0) withAttributes:valueAttrs];
    [@"CLICK: PATCH / SELECT · CLICK AGAIN: DISSOLVE · OPTION: NEGATIVE"
        drawAtPoint:NSMakePoint(rect.origin.x + 14.0,
            NSMaxY(rect) - 19.0) withAttributes:valueAttrs];
}


- (void)drawPrimaryLanes:(Plugin*)plugin rect:(NSRect)rect
    label:(NSDictionary*)label valueAttrs:(NSDictionary*)valueAttrs
{
    const auto visible = uiSnapshot(*plugin);
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
        const auto& laneParams = visible.params.lanes[lane];
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
    case kMenuRandomEnergy: return randomButtonRect();
    case kMenuLane: return processorMenuRect(family.selectedLane, 0u);
    case kMenuSource: return processorMenuRect(family.crosspoint, 0u);
    case kMenuDestination: return processorMenuRect(family.crosspoint, 1u);
    case kMenuSlot0: return channelInsertMenuRect(0u);
    case kMenuSlot1: return channelInsertMenuRect(1u);
    case kMenuSlot2: return channelInsertMenuRect(2u);
    case kMenuMotionShape:
        return processorMenuRect(family.movement, 0u);
    case kMenuBehavior:
        return processorMenuRect(family.movement, 0u);
    case kMenuQuality:
        return NSMakeRect(family.containment.frame.x + 108.0,
            family.containment.frame.y + 35.0, 110.0, 15.0);
    case kMenuOutputFormat:
        return processorMenuRect(family.output, 2u);
    case kMenuReactMode:
        return processorMenuRect(family.movement, 0u);
    case kMenuFieldDivision:
        return processorMenuRect(family.movement, 5u);
    case kMenuEventDivision:
        return processorMenuRect(family.movement, 2u);
    case kMenuMixerInsert:
    case kMenuMixerAux:
    case kMenuAuxTapA:
    case kMenuAuxTapB:
        return _menuAnchor;
    default: return NSZeroRect;
    }
}

- (uint32_t)menuItemCount:(int)menu
{
    if (menu == kMenuPreset) return s3g::kNoInputMixerFactoryPresetCount;
    if (menu == kMenuRandomEnergy)
        return static_cast<uint32_t>(s3g::NoInputRandomEnergy::Count);
    if (menu == kMenuSlot0 || menu == kMenuSlot1 || menu == kMenuSlot2) {
        return s3g::kNoInputDistortionTypeCount;
    }
    if (menu == kMenuMixerInsert || menu == kMenuMixerAux) {
        return s3g::kNoInputDistortionTypeCount;
    }
    if (menu == kMenuReactMode)
        return static_cast<uint32_t>(s3g::NoInputReactMode::Count);
    if (menu == kMenuFieldDivision || menu == kMenuEventDivision)
        return s3g::kNoInputClockDivisionCount;
    if (menu == kMenuAuxTapA || menu == kMenuAuxTapB)
        return static_cast<uint32_t>(s3g::NoInputAuxTap::Count);
    if (menu == kMenuMotionShape) return s3g::kMatrixFlowShapeCount;
    if (menu == kMenuBehavior) return s3g::kNoInputMovementBehaviorCount;
    if (menu == kMenuQuality) return 3u;
    if (menu == kMenuOutputFormat) return s3g::kRingOutputFormatCount;
    if (menu == kMenuLane || menu == kMenuSource
        || menu == kMenuDestination) return kChannelCount;
    return 0u;
}

- (NSRect)menuDropdownRect:(int)menu
{
    const NSRect anchor = [self menuAnchorRect:menu];
    const CGFloat height = 18.0
        * static_cast<CGFloat>([self menuItemCount:menu]);
    CGFloat width = anchor.size.width;
    CGFloat x = anchor.origin.x;
    CGFloat y = NSMaxY(anchor) + 2.0;
    const bool effectMenu = menu == kMenuMixerInsert
        || menu == kMenuMixerAux
        || menu == kMenuReactMode
        || menu == kMenuFieldDivision || menu == kMenuEventDivision
        || menu == kMenuAuxTapA || menu == kMenuAuxTapB
        || menu == kMenuSlot0 || menu == kMenuSlot1
        || menu == kMenuSlot2;
    if (menu == kMenuRandomEnergy) width = std::max<CGFloat>(154.0, width);
    if (effectMenu) {
        if (menu == kMenuMixerInsert || menu == kMenuMixerAux)
            width = std::max<CGFloat>(148.0, width);
        x = std::clamp(x, NSMinX([self bounds]) + 2.0,
            NSMaxX([self bounds]) - width - 2.0);
        if (menu == kMenuMixerInsert
            || y + height > NSMaxY([self bounds]) - 2.0) {
            y = anchor.origin.y - height - 2.0;
        }
        y = std::clamp(y, NSMinY([self bounds]) + 2.0,
            NSMaxY([self bounds]) - height - 2.0);
    }
    return NSMakeRect(x, y, width, height);
}

- (void)drawOpenMenu:(Plugin*)plugin attrs:(NSDictionary*)attrs
    style:(const s3g::clap_gui::Style&)style
{
    if (_openMenu == kMenuNone) return;
    const auto visible = uiSnapshot(*plugin);
    NSString* laneItems[kChannelCount] = {
        @"L1", @"L2", @"L3", @"L4", @"L5", @"L6", @"L7", @"L8",
    };
    NSString* typeItems[s3g::kNoInputDistortionTypeCount] = {
        @"BYPASS", @"WOOL", @"RAT", @"ZONE A", @"ZONE B",
        @"FUZZ I", @"FUZZ II", @"DIODE", @"RING", @"RELAY",
        @"CRUSH", @"SPLICE", @"LOGIC", @"SHRED", @"VOID",
        @"ROTOR", @"PHASE", @"CHORUS", @"THROAT", @"ROBOT",
        @"OCT DOWN", @"OCT UP", @"OCT STACK",
    };
    NSString* qualityItems[3] = { @"1X", @"2X", @"4X" };
    NSString* randomEnergyItems[3] = {
        @"HIGH / QUICK", @"MID / MODERATE", @"LOW / SLOW",
    };
    NSString* shapeItems[s3g::kMatrixFlowShapeCount] = {
        @"FLOW", @"PULSE", @"CHASE", @"SWIRL", @"SCAT", @"HOLD",
        @"BLOOM", @"BRAID", @"ATTRACT",
    };
    NSString* behaviorItems[s3g::kNoInputMovementBehaviorCount] = {
        @"GLIDE", @"STEP", @"CUT", @"BURST", @"SCRAMBLE",
        @"RATCHET", @"CASCADE", @"ERODE",
    };
    NSString* reactItemsFull[5] = {
        @"OFF", @"FOLLOW", @"AVOID", @"EDGE", @"BALANCE",
    };
    NSString* divisionItems[s3g::kNoInputClockDivisionCount] = {
        @"1/64", @"1/32", @"1/16", @"1/8", @"1/4",
        @"1/2", @"1 BAR", @"2 BARS", @"4 BARS", @"8 BARS",
    };
    NSString* tapItems[4] = {
        @"RETURN", @"PRE EQ", @"POST EQ", @"POST INSERT",
    };
    NSString* outputFormatItems[s3g::kRingOutputFormatCount] = {
        @"8CH DIRECT", @"QUAD RING", @"STEREO RING",
    };
    NSString* presetItems[s3g::kNoInputMixerFactoryPresetCount] = {
        @"INIT", @"CIRCUIT LATTICE", @"RAIN FOREST", @"WOOL RING",
        @"RAT CAGE", @"ZONE WEB", @"NEGATIVE SPACE", @"RELAY BLOOM",
        @"OPEN HOUSE", @"MOBILE CIRCUIT", @"STATIC CHOIR",
        @"RAZOR CLOCK", @"SUBHARMONIC WELL", @"SPEECH CIRCUIT",
        @"SPLICE STORM", @"PHASE ORCHARD", @"LOGIC FLOCK",
        @"OCTAVE LADDER", @"AUX MIRROR", @"WALL ENGINE",
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
    } else if (_openMenu == kMenuRandomEnergy) {
        items = randomEnergyItems;
        count = static_cast<uint32_t>(s3g::NoInputRandomEnergy::Count);
        selected = static_cast<int>(_randomEnergyProfile);
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
        selected = static_cast<int>(visible.params.quality);
    } else if (_openMenu == kMenuOutputFormat) {
        items = outputFormatItems;
        count = s3g::kRingOutputFormatCount;
        double format = 0.0;
        uiParameterValue(*plugin, kOutputFormatParamId, format);
        selected = static_cast<int>(std::clamp(
            std::lround(format), 0l, 2l));
    } else if (_openMenu == kMenuMotionShape) {
        items = shapeItems;
        count = s3g::kMatrixFlowShapeCount;
        selected = static_cast<int>(visible.params.motionShape);
    } else if (_openMenu == kMenuBehavior) {
        items = behaviorItems;
        count = s3g::kNoInputMovementBehaviorCount;
        selected = static_cast<int>(visible.behavior.behavior);
    } else if (_openMenu == kMenuMixerInsert) {
        items = typeItems;
        count = s3g::kNoInputDistortionTypeCount;
        selected = static_cast<int>(visible.params.lanes[_effectMenuLane]
            .inserts[_effectMenuSlot].type);
    } else if (_openMenu == kMenuMixerAux) {
        items = typeItems;
        count = s3g::kNoInputDistortionTypeCount;
        selected = static_cast<int>(
            visible.params.aux[_effectMenuBus].effect.type);
    } else if (_openMenu == kMenuReactMode) {
        items = reactItemsFull;
        count = static_cast<uint32_t>(s3g::NoInputReactMode::Count);
        selected = static_cast<int>(visible.params.reactMode);
    } else if (_openMenu == kMenuFieldDivision
            || _openMenu == kMenuEventDivision) {
        items = divisionItems;
        count = s3g::kNoInputClockDivisionCount;
        selected = static_cast<int>(_openMenu == kMenuFieldDivision
            ? visible.params.fieldDivision : visible.params.eventDivision);
    } else if (_openMenu == kMenuAuxTapA || _openMenu == kMenuAuxTapB) {
        items = tapItems;
        count = static_cast<uint32_t>(s3g::NoInputAuxTap::Count);
        selected = static_cast<int>(visible.params.lanes[_effectMenuLane]
            .auxTap[_openMenu == kMenuAuxTapA ? 0u : 1u]);
    } else {
        items = typeItems;
        count = s3g::kNoInputDistortionTypeCount;
        const uint32_t lane = plugin->selectedLane.load(
            std::memory_order_relaxed);
        const uint32_t slot = static_cast<uint32_t>(
            _openMenu - kMenuSlot0);
        selected = static_cast<int>(visible.params.lanes[lane]
            .inserts[slot].type);
    }
    s3g::clap_gui::drawDropdownMenu([self menuDropdownRect:_openMenu],
        18.0, items, count, selected, _hoverMenuItem, attrs, style);
}

- (void)drawPerformanceMixer:(Plugin*)plugin surface:(NSRect)surface
{
    const auto visible = uiSnapshot(*plugin);
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
        const auto& laneParams = visible.params.lanes[lane];
        const float loop = visible.params.matrix[lane * 8u + lane];
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
            if ([name length] > 5u) name = [name substringToIndex:5u];
            drawFlatButton(popupInsertMenuRect(strip, slot),
                [NSString stringWithFormat:@"%u %@", slot + 1u, name],
                plugin->selectedLane.load(std::memory_order_relaxed) == lane
                    && plugin->selectedSlot.load(std::memory_order_relaxed)
                        == slot,
                value);
            drawFlatButton(popupInsertEditRect(strip, slot), @"EDIT", false,
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
            drawAtPoint:NSMakePoint(auxPanel.origin.x + 14.0, baseY - 8.0)
            withAttributes:title];
        const auto& aux = visible.params.aux[bus];
        drawFlatButton(popupAuxMuteRect(bus), @"MUTE ALL",
            visible.auxMute[bus] != 0u, value);
        drawFlatButton(popupAuxTypeMenuRect(bus),
            [NSString stringWithUTF8String:
                s3g::noInputDistortionName(aux.effect.type)], true, value);
        drawFlatButton(popupAuxTypeEditRect(bus), @"EDIT", false, value);
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
        (visible.params.internalTone + 1.0f) * 0.5f,
        (visible.params.houseTone + 1.0f) * 0.5f,
    };
    for (uint32_t row = 0u; row < 2u; ++row) {
        const NSRect track = popupToneRect(row);
        [toneLabels[row] drawAtPoint:NSMakePoint(auxPanel.origin.x + 14.0,
            track.origin.y - 4.0) withAttributes:value];
        drawHorizontal(@"", track, toneNorms[row], 0x929292);
    }
    [@"EFFECT MENU + EDIT · DRAG TRACKS · MUTE ALL: BUS"
        drawAtPoint:NSMakePoint(auxPanel.origin.x + 14.0,
            NSMaxY(auxPanel) - 24.0) withAttributes:value];
    [NSGraphicsContext restoreGraphicsState];
}

- (void)drawAuxTopology:(Plugin*)plugin label:(NSDictionary*)label
    value:(NSDictionary*)value
{
    const auto visible = uiSnapshot(*plugin);
    const NSRect page = widePageRect();
    [mixerColor(0x101010) setFill]; NSRectFill(page);
    [mixerColor(0x4b4b4b) setStroke]; NSFrameRect(page);
    [@"PER-LANE AUX TOPOLOGY · SOURCE TAP → SEND → AUX PROCESSOR → SIGNED LANE RETURN"
        drawAtPoint:NSMakePoint(page.origin.x + 14.0, page.origin.y + 14.0)
        withAttributes:label];
    const auto drawTrack = [&](NSRect track, CGFloat norm, int color) {
        [mixerColor(0x070707) setFill]; NSRectFill(track);
        [mixerColor(0x424242) setStroke]; NSFrameRect(track);
        NSRect fill = NSInsetRect(track, 1.0, 1.0);
        fill.size.width *= std::clamp<CGFloat>(norm, 0.0, 1.0);
        [mixerColor(color, 0.86) setFill]; NSRectFill(fill);
    };
    for (uint32_t lane = 0u; lane < kChannelCount; ++lane) {
        const NSRect column = auxPageColumnRect(lane);
        [mixerColor(plugin->selectedLane.load(std::memory_order_relaxed)
                == lane ? 0x222222 : 0x151515) setFill];
        NSRectFill(column);
        [mixerColor(0x4b4b4b) setStroke]; NSFrameRect(column);
        [[NSString stringWithFormat:@"LANE %u", lane + 1u]
            drawAtPoint:NSMakePoint(column.origin.x + 12.0,
                column.origin.y + 12.0) withAttributes:label];
        const auto& laneParams = visible.params.lanes[lane];
        const NSString* labels[6] = {
            @"SEND A", @"TAP A", @"RETURN A", @"SEND B", @"TAP B", @"RETURN B",
        };
        for (uint32_t row = 0u; row < 6u; ++row) {
            const NSRect track = auxPageTrackRect(lane, row);
            [labels[row] drawAtPoint:NSMakePoint(track.origin.x,
                track.origin.y - 19.0) withAttributes:value];
            if (row == 1u || row == 4u) {
                const uint32_t bus = row == 1u ? 0u : 1u;
                drawFlatButton(auxPageTapRect(lane, bus),
                    [NSString stringWithUTF8String:s3g::noInputAuxTapName(
                        laneParams.auxTap[bus])], true, value);
                continue;
            }
            const uint32_t bus = row >= 3u ? 1u : 0u;
            const bool isReturn = row == 2u || row == 5u;
            const float raw = isReturn ? laneParams.auxReturn[bus]
                : laneParams.auxSend[bus];
            drawTrack(track, isReturn ? (raw + 1.0f) * 0.5f : raw,
                bus == 0u ? 0xc95e3b : 0x57bfc4);
            [[NSString stringWithFormat:isReturn ? @"%+.2f" : @"%.0f%%",
                isReturn ? raw : raw * 100.0f]
                drawAtPoint:NSMakePoint(track.origin.x,
                    NSMaxY(track) + 5.0) withAttributes:value];
        }
    }
    [@"A RETURN IS A TRUE DESTINATION VECTOR: NEGATIVE VALUES INVERT PHASE. RETURN TAP PRESERVES THE ORIGINAL FEEDBACK-MIXER PATH."
        drawAtPoint:NSMakePoint(page.origin.x + 14.0,
            NSMaxY(page) - 22.0) withAttributes:value];
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* plugin = static_cast<Plugin*>(_plugin);
    if (!plugin) return;
    const auto visible = uiSnapshot(*plugin);
    double outputFormatValue = 0.0;
    double outputRotationValue = 0.0;
    uiParameterValue(*plugin, kOutputFormatParamId, outputFormatValue);
    uiParameterValue(*plugin, kOutputRotationParamId, outputRotationValue);
    const auto visibleOutputFormat = s3g::sanitizeRingOutputFormat(
        static_cast<uint32_t>(std::clamp(
            std::lround(outputFormatValue), 0l, 2l)));
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
    const uint32_t page = _lockedPage < kPageCount ? _lockedPage
        : plugin->guiPage.load(std::memory_order_relaxed);
    static NSString* pageNames[kPageCount] = {
        @"PATCH", @"MIXER", @"CHANNEL", @"SAFETY", @"AUX",
    };
    s3g::clap_gui::drawPanelHeader(pageNames[page], true,
        family.fieldPanel.x, family.fieldPanel.y, family.fieldPanel.width,
        s3g::gui_layout::kStandardMetrics.headerHeight, label, style);
    for (uint32_t index = 0u; index < kPageCount; ++index) {
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
    else if (page == 4u) [self drawAuxTopology:plugin label:label value:value];
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
    const auto& laneParams = visible.params.lanes[lane];
    if (page == 0u) {
        drawPanel(@"NETWORK", family.network);
        drawPanel(@"MOVEMENT", family.movement);
        drawFlatButton(movementSectionRandomRect(), @"RND", false, value);
        drawFlatButton(movementBankButtonRect(0u), @"FIELD",
            _movementBank == 0u, value);
        drawFlatButton(movementBankButtonRect(1u), @"BEHAV",
            _movementBank == 1u, value);
        drawFlatButton(movementBankButtonRect(2u), @"RESP",
            _movementBank == 2u, value);
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
            visible.params.outputGainDb]
        norm:(visible.params.outputGainDb + 60.0f) / 66.0f row:0u
        panel:family.output label:label valueAttrs:value style:style];
    [self drawSlider:@"CEIL"
        value:[NSString stringWithFormat:@"%+.1f dB",
            visible.params.ceilingDb]
        norm:(visible.params.ceilingDb + 18.0f) / 18.0f row:1u
        panel:family.output label:label valueAttrs:value style:style];
    s3g::clap_gui::drawProcessorMenu(@"FORMAT",
        [NSString stringWithUTF8String:s3g::ringOutputFormatName(
            visibleOutputFormat)],
        s3g::gui_layout::rowY(family.output, 2u),
        family.output.frame.x, family.output.frame.width,
        label, value, style);
    [self drawSlider:@"ROTATE"
        value:[NSString stringWithFormat:@"%+.1f°", outputRotationValue]
        norm:(outputRotationValue + 180.0) / 360.0 row:3u
        panel:family.output label:label valueAttrs:value style:style];
    s3g::clap_gui::drawToggle(@"LIMIT",
        visible.params.limiterEnabled != 0u,
        s3g::gui_layout::rowY(family.output, 4u), label, value, style,
        s3g::gui_layout::processorLabelX(family.output.frame.x),
        s3g::gui_layout::processorControlX(family.output.frame.x), 82.0);
    s3g::clap_gui::drawToggle(@"DC BLOCK",
        visible.params.dcBlockEnabled != 0u,
        s3g::gui_layout::rowY(family.output, 5u), label, value, style,
        s3g::gui_layout::processorLabelX(family.output.frame.x),
        s3g::gui_layout::processorControlX(family.output.frame.x), 82.0);
    }

    if (page == 0u) {
    [@"SEED" drawAtPoint:NSMakePoint(
        s3g::gui_layout::processorLabelX(family.network.frame.x),
        s3g::gui_layout::rowY(family.network, 0u) - 2.0)
        withAttributes:label];
    drawFlatButton(seedNewButtonRect(), @"NEW", false, value);
    drawFlatButton(randomButtonRect(), @"RANDOM V", false, value);
    drawFlatButton(forgetButtonRect(), @"FORGET", false, value);
    [self drawSlider:@"FDBK"
        value:[NSString stringWithFormat:@"%.0f%%",
            visible.params.feedback * 100.0f]
        norm:visible.params.feedback / 1.25f row:1u panel:family.network
        label:label valueAttrs:value style:style];
    [self drawSlider:@"COUPL"
        value:[NSString stringWithFormat:@"%.0f%%",
            visible.params.coupling * 100.0f]
        norm:visible.params.coupling / 1.25f row:2u panel:family.network
        label:label valueAttrs:value style:style];
    [self drawSlider:@"PHASE"
        value:[NSString stringWithFormat:@"%.0f%%",
            visible.params.phase * 100.0f]
        norm:visible.params.phase row:3u panel:family.network label:label
        valueAttrs:value style:style];
    [self drawSlider:@"DRIFT"
        value:[NSString stringWithFormat:@"%.0f%%",
            visible.params.drift * 100.0f]
        norm:visible.params.drift row:4u panel:family.network label:label
        valueAttrs:value style:style];
    [self drawSlider:@"FORMANT"
        value:[NSString stringWithFormat:@"%.0f%%",
            visible.params.formant * 100.0f]
        norm:visible.params.formant row:5u panel:family.network label:label
        valueAttrs:value style:style];
    [self drawSlider:@"AGENCY"
        value:[NSString stringWithFormat:@"%.0f%%",
            visible.params.agency * 100.0f]
        norm:visible.params.agency row:6u panel:family.network label:label
        valueAttrs:value style:style];
    [self drawSlider:@"SPACE"
        value:[NSString stringWithFormat:@"%.0f%%",
            visible.params.space * 100.0f]
        norm:visible.params.space row:7u panel:family.network label:label
        valueAttrs:value style:style];
    [self drawSlider:@"VARIANCE"
        value:[NSString stringWithFormat:@"%.0f%%",
            visible.params.variance * 100.0f]
        norm:visible.params.variance row:8u panel:family.network label:label
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
    [self drawSlider:@"TUNE"
        value:[NSString stringWithFormat:@"%.2f MIDI", laneParams.tuneNote]
        norm:(laneParams.tuneNote - 24.0f) / 84.0f row:3u
        panel:family.selectedLane label:label valueAttrs:value style:style];
    [self drawSlider:@"FINE"
        value:[NSString stringWithFormat:@"%+.1f ct", laneParams.tuneCents]
        norm:(laneParams.tuneCents + 100.0f) / 200.0f row:4u
        panel:family.selectedLane
        label:label valueAttrs:value style:style];
    [self drawSlider:@"LEVEL"
        value:[NSString stringWithFormat:@"%+.1f dB", laneParams.levelDb]
        norm:(laneParams.levelDb + 60.0f) / 72.0f row:5u
        panel:family.selectedLane
        label:label valueAttrs:value style:style];
    const CGFloat selectedButtonY = s3g::gui_layout::rowY(
        family.selectedLane, 6u) - 1.0;
    drawFlatButton(NSMakeRect(
        s3g::gui_layout::processorControlX(family.selectedLane.frame.x),
        selectedButtonY, 50.0, 17.0), @"LOCK",
        laneParams.pitchLock != 0u, value);
    drawFlatButton(NSMakeRect(
        s3g::gui_layout::processorControlX(family.selectedLane.frame.x)
            + 58.0,
        selectedButtonY, 50.0, 17.0), @"MUTE",
        laneParams.mute != 0u, value);
    drawFlatButton(NSMakeRect(
        s3g::gui_layout::processorControlX(family.selectedLane.frame.x)
            + 116.0,
        selectedButtonY, 50.0, 17.0), @"KILL", false, value);
    }

    if (page == 0u) {
    const uint32_t source = plugin->selectedSource.load(
        std::memory_order_relaxed);
    const uint32_t destination = plugin->selectedDestination.load(
        std::memory_order_relaxed);
    const uint32_t routeIndex = destination * kChannelCount + source;
    const float storedRoute = visible.params.matrix[routeIndex];
    const float displayedRoute = displayedMatrixGain(*plugin, routeIndex);
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
        value:[NSString stringWithFormat:@"%+.2f", displayedRoute]
        norm:(displayedRoute + 1.0f) * 0.5f row:2u panel:family.crosspoint
        label:label valueAttrs:value style:style];
    const CGFloat routeButtonX = s3g::gui_layout::processorControlX(
        family.crosspoint.frame.x);
    const CGFloat routeButtonY =
        s3g::gui_layout::rowY(family.crosspoint, 3u) - 1.0;
    drawFlatButton(NSMakeRect(routeButtonX,
        routeButtonY, 78.0, 17.0),
        storedRoute < 0.0f ? @"NEGATIVE" : @"POSITIVE",
        storedRoute != 0.0f, value);
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
        const NSRect menuRect = channelInsertMenuRect(insertSlot);
        s3g::clap_gui::drawMenu(
            [NSString stringWithFormat:@"SLOT %u", insertSlot + 1u],
            [NSString stringWithUTF8String:s3g::noInputDistortionName(
                insert.type)], s3g::gui_layout::rowY(
                family.inserts, insertSlot), label, value, style,
            s3g::gui_layout::processorLabelX(family.inserts.frame.x),
            menuRect.origin.x, menuRect.size.width);
        drawFlatButton(channelInsertEditRect(insertSlot), @"EDIT",
            insertSlot == slot, value);
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
    if (_movementBank == 0u) {
        s3g::clap_gui::drawProcessorMenu(@"SHAPE",
            [NSString stringWithUTF8String:s3g::matrixFlowShapeName(
                visible.params.motionShape)],
            s3g::gui_layout::rowY(family.movement, 0u),
            family.movement.frame.x, family.movement.frame.width,
            label, value, style);
        [self drawSlider:@"FLOW"
            value:[NSString stringWithFormat:@"%.0f%%", visible.params.flow * 100.0f]
            norm:visible.params.flow row:1u panel:family.movement label:label
            valueAttrs:value style:style];
        [self drawSlider:@"SPREAD"
            value:[NSString stringWithFormat:@"%.0f%%", visible.params.spread * 100.0f]
            norm:visible.params.spread row:2u panel:family.movement label:label
            valueAttrs:value style:style];
        [self drawSlider:@"VORTEX"
            value:[NSString stringWithFormat:@"%+.2f", visible.params.vortex]
            norm:(visible.params.vortex + 1.0f) * 0.5f row:3u
            panel:family.movement label:label valueAttrs:value style:style];
        [self drawSlider:@"DEPTH"
            value:[NSString stringWithFormat:@"%.0f%%", visible.params.motion * 100.0f]
            norm:visible.params.motion row:4u panel:family.movement label:label
            valueAttrs:value style:style];
        const float fieldRate = visible.params.clockSync
            ? s3g::noInputSyncedRateHz(visible.params.fieldDivision,
                plugin->transportTempoBpm)
            : s3g::noInputMixerMotionRateHz(visible.params.motionRate,
                visible.params.slowTime != 0u);
        if (visible.params.clockSync) {
            s3g::clap_gui::drawProcessorMenu(@"RATE",
                [NSString stringWithFormat:@"%@ · %.3f Hz",
                    [NSString stringWithUTF8String:s3g::noInputClockDivisionName(
                        visible.params.fieldDivision)], fieldRate],
                s3g::gui_layout::rowY(family.movement, 5u),
                family.movement.frame.x, family.movement.frame.width,
                label, value, style);
        } else {
            [self drawSlider:@"RATE"
                value:[NSString stringWithFormat:(fieldRate < 1.0f
                        ? @"%.3f Hz" : @"%.1f Hz"), fieldRate]
                norm:visible.params.motionRate row:5u panel:family.movement
                label:label valueAttrs:value style:style];
        }
        [self drawSlider:@"PHASE"
            value:[NSString stringWithFormat:@"%.0f%%", visible.params.motionPhase * 100.0f]
            norm:visible.params.motionPhase row:6u panel:family.movement label:label
            valueAttrs:value style:style];
    } else if (_movementBank == 1u) {
        const auto behavior = visible.behavior.behavior;
        const bool behaviorActive = behavior
            != s3g::NoInputMovementBehavior::Glide;
        const bool hasTimedWindow =
            s3g::noInputMovementBehaviorUsesLength(behavior);
        const bool hasDensity =
            s3g::noInputMovementBehaviorUsesDensity(behavior);
        s3g::clap_gui::drawProcessorMenu(@"BEHAV",
            [NSString stringWithUTF8String:s3g::noInputMovementBehaviorName(
                behavior)],
            s3g::gui_layout::rowY(family.movement, 0u),
            family.movement.frame.x, family.movement.frame.width,
            label, value, style);
        [self drawSlider:@"DEPTH"
            value:behaviorActive
                ? [NSString stringWithFormat:@"%.0f%%",
                    visible.behaviorDepth * 100.0f]
                : @"—"
            norm:(behaviorActive ? visible.behaviorDepth : 0.0f)
            row:1u panel:family.movement label:label
            valueAttrs:value style:style];
        const float eventRate = visible.params.clockSync
            ? s3g::noInputSyncedRateHz(visible.params.eventDivision,
                plugin->transportTempoBpm)
            : s3g::noInputMovementEventRateHz(visible.behavior.eventRate,
                visible.params.slowTime != 0u);
        if (behaviorActive && visible.params.clockSync) {
            s3g::clap_gui::drawProcessorMenu(@"EVENT",
                [NSString stringWithFormat:@"%@ · %.3f Hz",
                    [NSString stringWithUTF8String:s3g::noInputClockDivisionName(
                        visible.params.eventDivision)], eventRate],
                s3g::gui_layout::rowY(family.movement, 2u),
                family.movement.frame.x, family.movement.frame.width,
                label, value, style);
        } else {
            [self drawSlider:@"EVENT"
                value:behaviorActive
                    ? [NSString stringWithFormat:(eventRate < 10.0f
                            ? @"%.3f Hz" : @"%.1f Hz"), eventRate]
                    : @"—"
                norm:(behaviorActive ? visible.behavior.eventRate : 0.0f)
                row:2u panel:family.movement
                label:label valueAttrs:value style:style];
        }
        [self drawSlider:@"LENGTH"
            value:hasTimedWindow
                ? [NSString stringWithFormat:@"%.1f ms",
                    s3g::noInputMovementLengthMs(visible.behavior.length)]
                : @"—"
            norm:(hasTimedWindow ? visible.behavior.length : 0.0f)
            row:3u panel:family.movement
            label:label valueAttrs:value style:style];
        [self drawSlider:@"DENSITY"
            value:hasDensity
                ? [NSString stringWithFormat:@"%.0f%%",
                    visible.behavior.density * 100.0f]
                : @"—"
            norm:(hasDensity ? visible.behavior.density : 0.0f)
            row:4u panel:family.movement
            label:label valueAttrs:value style:style];
        [self drawSlider:@"CHAOS"
            value:behaviorActive
                ? [NSString stringWithFormat:@"%.0f%%",
                    visible.behavior.chaos * 100.0f]
                : @"—"
            norm:(behaviorActive ? visible.behavior.chaos : 0.0f)
            row:5u panel:family.movement
            label:label valueAttrs:value style:style];
        NSString* transitionLabel = behavior
                == s3g::NoInputMovementBehavior::Step
            ? @"TRANSITION"
            : (behavior == s3g::NoInputMovementBehavior::Scramble
                ? @"XFADE"
                : (behavior == s3g::NoInputMovementBehavior::Cascade
                    ? @"TRAIL"
                    : (behavior == s3g::NoInputMovementBehavior::Erode
                        ? @"SOFTEN" : @"EDGE")));
        [self drawSlider:transitionLabel
            value:behaviorActive
                ? [NSString stringWithFormat:@"%.2f ms",
                    s3g::noInputMovementSlewMs(visible.behavior.slew)]
                : @"—"
            norm:(behaviorActive ? visible.behavior.slew : 0.0f)
            row:6u panel:family.movement
            label:label valueAttrs:value style:style];
        [self drawSlider:@"CHOKE"
            value:behaviorActive
                ? [NSString stringWithFormat:@"%.0f%%",
                    visible.behavior.choke * 100.0f]
                : @"—"
            norm:(behaviorActive ? visible.behavior.choke : 0.0f)
            row:7u panel:family.movement
            label:label valueAttrs:value style:style];
    } else {
        s3g::clap_gui::drawProcessorMenu(@"MODE",
            [NSString stringWithUTF8String:s3g::noInputReactModeName(
                visible.params.reactMode)],
            s3g::gui_layout::rowY(family.movement, 0u),
            family.movement.frame.x, family.movement.frame.width,
            label, value, style);
        [self drawSlider:@"DEPTH"
            value:[NSString stringWithFormat:@"%.0f%%",
                visible.params.reactDepth * 100.0f]
            norm:visible.params.reactDepth row:1u panel:family.movement
            label:label valueAttrs:value style:style];
        const float thresholdDb = 20.0f * std::log10(std::max(1.0e-6f,
            s3g::noInputReactThreshold(visible.params.reactThreshold)));
        [self drawSlider:@"THRESH"
            value:[NSString stringWithFormat:@"%+.1f dB", thresholdDb]
            norm:visible.params.reactThreshold row:2u panel:family.movement
            label:label valueAttrs:value style:style];
        [self drawSlider:@"ATTACK"
            value:[NSString stringWithFormat:@"%.1f ms",
                s3g::noInputReactAttackMs(visible.params.reactAttack)]
            norm:visible.params.reactAttack row:3u panel:family.movement
            label:label valueAttrs:value style:style];
        [self drawSlider:@"RELEASE"
            value:[NSString stringWithFormat:@"%.0f ms",
                s3g::noInputReactReleaseMs(visible.params.reactRelease)]
            norm:visible.params.reactRelease row:4u panel:family.movement
            label:label valueAttrs:value style:style];
        [@"DIRECTION" drawAtPoint:NSMakePoint(
            s3g::gui_layout::processorLabelX(family.movement.frame.x),
            s3g::gui_layout::rowY(family.movement, 5u) - 2.0)
            withAttributes:label];
        drawFlatButton(reactDirectionButtonRect(),
            visible.params.reactPolarity < 0.0f ? @"INVERT" : @"NORMAL",
            visible.params.reactPolarity < 0.0f, value);
        const float listenLevel = std::max(1.0e-6f,
            plugin->laneActivity[lane].load(std::memory_order_relaxed));
        const float listenDb = 20.0f * std::log10(listenLevel);
        [self drawSlider:[NSString stringWithFormat:@"LISTEN L%u", lane + 1u]
            value:listenDb <= -59.9f ? @"−∞"
                : [NSString stringWithFormat:@"%+.1f dB", listenDb]
            norm:std::clamp((listenDb + 60.0f) / 60.0f, 0.0f, 1.0f)
            row:6u panel:family.movement label:label
            valueAttrs:value style:style];
    }
    drawFlatButton(movementGlobalToggleRect(0u), @"HOLD",
        visible.params.controllerHold != 0u, value);
    drawFlatButton(movementGlobalToggleRect(1u), @"SLOW",
        visible.params.slowTime != 0u, value);
    drawFlatButton(movementGlobalToggleRect(2u), @"SYNC",
        visible.params.clockSync != 0u, value);
    }

    if (page == 3u) {
    [@"QUALITY" drawAtPoint:NSMakePoint(
        family.containment.frame.x + 16.0,
        family.containment.frame.y + 34.0) withAttributes:label];
    s3g::clap_gui::drawMenu(@"",
        [NSString stringWithFormat:@"%uX", 1u << visible.params.quality],
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
    if (param == kOutputParamId || param == kCeilingParamId
        || param == kOutputRotationParamId)
        return &family.output;
    if ((param >= kFeedbackParamId && param <= kFormantParamId)
        || (param >= kAgencyParamId && param <= kVarianceParamId))
        return &family.network;
    if (param >= kFlowParamId && param <= kMotionPhaseParamId)
        return &family.movement;
    if (param >= kEventRateParamId && param <= kEventChokeParamId)
        return &family.movement;
    if (param == kBehaviorDepthParamId) return &family.movement;
    if (param >= kReactDepthParamId && param <= kReactPolarityParamId)
        return &family.movement;
    uint32_t destination = 0u;
    uint32_t source = 0u;
    if (decodeMatrixParam(param, destination, source))
        return &family.crosspoint;
    uint32_t lane = 0u;
    clap_id offset = 0u;
    if (!decodeLaneParam(param, lane, offset)) return nullptr;
    if (offset <= kLaneLevelOffset || offset == kLaneAuxAOffset
        || offset == kLaneAuxBOffset || offset == kLaneTuneNoteOffset
        || offset == kLaneTuneCentsOffset) return &family.selectedLane;
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
    if (_dragParam == kMatrixMidiRampParamId) {
        const NSRect track = matrixMidiRampTrackRect();
        const double normalized = std::clamp(
            (point.x - track.origin.x) / track.size.width, 0.0, 1.0);
        [self applyGuiParam:_dragParam value:
            s3g::kNoInputMatrixMidiRampMinimumMs
                + normalized * normalized
                    * (s3g::kNoInputMatrixMidiRampMaximumMs
                        - s3g::kNoInputMatrixMidiRampMinimumMs)];
        [self setNeedsDisplay:YES];
        return;
    }
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
            const auto previous = uiSnapshot(*plugin);
            uint32_t seed = previous.params.seed * 1664525u + 1013904223u;
            if (seed == 0u) seed = 1u;
            const float variance = previous.params.variance;
            auto patch = s3g::noInputMixerFactoryPreset(index);
            patch = s3g::variedNoInputMixerParams(
                patch, seed, variance);
            patch.outputGainDb = previous.params.outputGainDb;
            NoInputSurfaceSnapshot desired { patch,
                s3g::noInputMixerFactoryBehavior(index),
                previous.auxMute, patch.motion };
            publishUiSnapshot(*plugin, desired);
            if (!enqueueGuiCommand(*plugin, {
                    GuiCommandType::FactoryPreset, CLAP_INVALID_ID,
                    variance, index, seed })) {
                publishUiSnapshot(*plugin, previous);
                break;
            }
            std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                s3g::noInputMixerFactoryPresetName(index));
        }
        break;
    case kMenuRandomEnergy:
        if (index < static_cast<uint32_t>(s3g::NoInputRandomEnergy::Count)) {
            const auto energy = static_cast<s3g::NoInputRandomEnergy>(index);
            const auto previous = uiSnapshot(*plugin);
            uint32_t seed = previous.params.seed * 1664525u + 1013904223u;
            if (seed == 0u) seed = 1u;
            _randomEnergyProfile = index;
            auto patch = s3g::randomizedNoInputMixerParams(
                seed, energy);
            patch.outputGainDb = previous.params.outputGainDb;
            NoInputSurfaceSnapshot desired { patch,
                s3g::randomizedNoInputMovementBehaviorParams(
                    seed ^ 0x43564d58u, energy),
                previous.auxMute, patch.motion };
            publishUiSnapshot(*plugin, desired);
            if (!enqueueGuiCommand(*plugin, {
                    GuiCommandType::RandomPatch, CLAP_INVALID_ID, 0.0,
                    index, seed })) {
                publishUiSnapshot(*plugin, previous);
                break;
            }
            std::snprintf(_titlePresetName, sizeof(_titlePresetName),
                "RANDOM %s", energy == s3g::NoInputRandomEnergy::High
                    ? "HIGH" : (energy == s3g::NoInputRandomEnergy::Low
                        ? "LOW" : "MID"));
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
    case kMenuOutputFormat:
        [self applyGuiParam:kOutputFormatParamId value:index];
        break;
    case kMenuMotionShape:
        [self applyGuiParam:kMotionShapeParamId value:index];
        break;
    case kMenuBehavior:
        [self applyGuiParam:kBehaviorParamId value:index];
        break;
    case kMenuMixerInsert:
        plugin->selectedLane.store(_effectMenuLane,
            std::memory_order_relaxed);
        plugin->selectedSlot.store(_effectMenuSlot,
            std::memory_order_relaxed);
        [self applyGuiParam:insertParamId(_effectMenuLane,
                _effectMenuSlot, kInsertTypeOffset) value:index];
        break;
    case kMenuMixerAux:
        [self applyGuiParam:_effectMenuBus == 0u
                ? kAuxATypeParamId : kAuxBTypeParamId value:index];
        break;
    case kMenuReactMode:
        [self applyGuiParam:kReactModeParamId value:index];
        break;
    case kMenuFieldDivision:
        [self applyGuiParam:kFieldDivisionParamId value:index];
        break;
    case kMenuEventDivision:
        [self applyGuiParam:kEventDivisionParamId value:index];
        break;
    case kMenuAuxTapA:
    case kMenuAuxTapB:
        [self applyGuiParam:laneParamId(_effectMenuLane,
                _openMenu == kMenuAuxTapA
                    ? kLaneAuxTapAOffset : kLaneAuxTapBOffset)
            value:index];
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
    const auto visible = uiSnapshot(*plugin);
    const auto& family = s3g::gui_layout::kNoInputMixerFamilyLayout;


    if (_openMenu != kMenuNone) {
        const int hit = s3g::clap_gui::dropdownHitIndex(point,
            [self menuDropdownRect:_openMenu], 18.0,
            [self menuItemCount:_openMenu]);
        if (hit >= 0) [self applyMenuSelection:static_cast<uint32_t>(hit)];
        _openMenu = kMenuNone;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        if (hit >= 0) return;
    }

    const auto titleBand = s3g::gui_layout::matrixTitleBand(family.canvas);
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.presetMenu))) {
        _openMenu = kMenuPreset;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    if (s3g::clap_gui::handleProcessorTitleClick(point,
            &plugin->plugin, @"No Input Mixer", titleBand,
            _titlePresetName, sizeof(_titlePresetName),
            kOutputParamId)) {
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

    for (uint32_t page = 0u; page < kPageCount; ++page) {
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

    const uint32_t page = _lockedPage < kPageCount ? _lockedPage
        : plugin->guiPage.load(std::memory_order_relaxed);
    const NSRect plot = s3g::clap_gui::cocoaRect(family.fieldPlot);
    if (page == 4u && NSPointInRect(point, widePageRect())) {
        for (uint32_t lane = 0u; lane < kChannelCount; ++lane) {
            if (!NSPointInRect(point, auxPageColumnRect(lane))) continue;
            plugin->selectedLane.store(lane, std::memory_order_relaxed);
            for (uint32_t bus = 0u; bus < 2u; ++bus) {
                if (!NSPointInRect(point, auxPageTapRect(lane, bus))) continue;
                _effectMenuLane = lane;
                _menuAnchor = auxPageTapRect(lane, bus);
                _openMenu = bus == 0u ? kMenuAuxTapA : kMenuAuxTapB;
                _hoverMenuItem = -1;
                [self setNeedsDisplay:YES];
                return;
            }
            const uint32_t rows[4] = { 0u, 2u, 3u, 5u };
            const clap_id ids[4] = {
                laneParamId(lane, kLaneAuxAOffset),
                laneParamId(lane, kLaneAuxReturnAOffset),
                laneParamId(lane, kLaneAuxBOffset),
                laneParamId(lane, kLaneAuxReturnBOffset),
            };
            for (uint32_t item = 0u; item < 4u; ++item) {
                const NSRect track = auxPageTrackRect(lane, rows[item]);
                if (!NSPointInRect(point, NSInsetRect(track, -8.0, -12.0)))
                    continue;
                const bool signedReturn = item == 1u || item == 3u;
                [self beginMixerDrag:ids[item] rect:track
                    minimum:signedReturn ? -1.0 : 0.0 maximum:1.0
                    vertical:NO point:point];
                return;
            }
            [self setNeedsDisplay:YES];
            return;
        }
        return;
    }
    if (false && page == 5u && NSPointInRect(point, widePageRect())) {
        if (NSPointInRect(point, surfaceButtonRect(0u))) {
            _surfaceEdit = !_surfaceEdit;
            _surfaceDragCell = -1;
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(point, surfaceButtonRect(1u))) {
            if (plugin->surface.cellCount < 2u) NSBeep();
            else {
                plugin->surface.enabled = plugin->surface.enabled ? 0u : 1u;
            }
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(point, surfaceButtonRect(2u))) {
            if (s3g::addParameterSurfaceCell(plugin->surface,
                    uiSnapshot(*plugin), -1, _titlePresetName)) {
                _selectedSurfaceCell = static_cast<int>(
                    plugin->surface.cellCount) - 1;
            } else NSBeep();
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(point, surfaceButtonRect(3u))) {
            const uint32_t removed = _selectedSurfaceCell < 0
                ? kNoInputSurfaceNoTopologyCell
                : static_cast<uint32_t>(_selectedSurfaceCell);
            if (_selectedSurfaceCell < 0
                || !s3g::removeParameterSurfaceCell(plugin->surface,
                    removed)) NSBeep();
            else if (plugin->surfaceTopologyCell == removed) {
                plugin->surfaceTopologyCell = kNoInputSurfaceNoTopologyCell;
                plugin->surfaceTopologyMode = static_cast<uint32_t>(
                    NoInputSurfaceTopologyMode::Base);
            } else if (plugin->surfaceTopologyCell !=
                    kNoInputSurfaceNoTopologyCell
                && plugin->surfaceTopologyCell > removed) {
                --plugin->surfaceTopologyCell;
            }
            _selectedSurfaceCell = plugin->surface.cellCount == 0u ? -1
                : std::min(_selectedSurfaceCell,
                    static_cast<int>(plugin->surface.cellCount) - 1);
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(point, surfaceButtonRect(4u))) {
            if (_selectedSurfaceCell < 0
                || static_cast<uint32_t>(_selectedSurfaceCell)
                    >= plugin->surface.cellCount) NSBeep();
            else {
                auto& cell = plugin->surface.cells[
                    static_cast<uint32_t>(_selectedSurfaceCell)];
                cell.params = uiSnapshot(*plugin);
                cell.presetIndex = -1;
                std::snprintf(cell.name, sizeof(cell.name), "%s",
                    _titlePresetName);
            }
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(point, surfaceFocusRect(0u))
            || NSPointInRect(point, surfaceFocusRect(1u))) {
            plugin->surface.focus = std::clamp(plugin->surface.focus
                * (NSPointInRect(point, surfaceFocusRect(0u))
                    ? 0.8f : 1.25f), 0.25f, 8.0f);
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(point, surfaceCurveRect())) {
            plugin->surface.curve = static_cast<s3g::ParameterSurfaceCurve>(
                (static_cast<uint32_t>(plugin->surface.curve) + 1u)
                    % s3g::kParameterSurfaceCurveCount);
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(point, surfaceGlideRect(0u))
            || NSPointInRect(point, surfaceGlideRect(1u))) {
            plugin->surface.glideMs = s3g::parameterSurfaceSteppedGlide(
                plugin->surface.glideMs,
                NSPointInRect(point, surfaceGlideRect(0u)) ? -1 : 1);
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(point, surfaceTopologyRect())) {
            if (plugin->surfaceTopologyMode == static_cast<uint32_t>(
                    NoInputSurfaceTopologyMode::Base)) {
                const float x = plugin->active.load(std::memory_order_acquire)
                    ? plugin->effectiveSurfaceX.load(std::memory_order_relaxed)
                    : visible.params.surfaceX;
                const float y = plugin->active.load(std::memory_order_acquire)
                    ? plugin->effectiveSurfaceY.load(std::memory_order_relaxed)
                    : visible.params.surfaceY;
                const auto weights = s3g::parameterSurfaceWeights(
                    plugin->surface, x, y);
                plugin->surfaceTopologyCell = weights.activeCount > 0u
                    ? weights.nearest : kNoInputSurfaceNoTopologyCell;
                if (plugin->surfaceTopologyCell
                        != kNoInputSurfaceNoTopologyCell) {
                    plugin->surfaceTopologyMode = static_cast<uint32_t>(
                        NoInputSurfaceTopologyMode::Cell);
                } else {
                    NSBeep();
                }
            } else {
                plugin->surfaceTopologyMode = static_cast<uint32_t>(
                    NoInputSurfaceTopologyMode::Base);
            }
            [self setNeedsDisplay:YES];
            return;
        }
        const NSRect surfacePlot = surfacePlotRect();
        if (NSPointInRect(point, surfacePlot)) {
            if (_surfaceEdit) {
                CGFloat nearest = 22.0;
                _surfaceDragCell = -1;
                for (uint32_t index = 0u;
                     index < plugin->surface.cellCount; ++index) {
                    const auto& cell = plugin->surface.cells[index];
                    const NSPoint site = NSMakePoint(
                        surfacePlot.origin.x + cell.x * surfacePlot.size.width,
                        NSMaxY(surfacePlot) - cell.y * surfacePlot.size.height);
                    const CGFloat distance = std::hypot(
                        point.x - site.x, point.y - site.y);
                    if (distance >= nearest) continue;
                    nearest = distance;
                    _surfaceDragCell = static_cast<int>(index);
                }
                _selectedSurfaceCell = _surfaceDragCell;
            } else {
                _surfaceDragCell = -2;
            }
            [self updateSurfaceDrag:point];
            [self setNeedsDisplay:YES];
            return;
        }
        return;
    }
    if (NSPointInRect(point, plot)) {
        if (page == 0u) {
            for (uint32_t mode = 0u;
                 mode < static_cast<uint32_t>(
                     s3g::NoInputMatrixMidiMode::Count); ++mode) {
                if (!NSPointInRect(point, matrixMidiModeButtonRect(mode)))
                    continue;
                [self applyGuiParam:kMatrixMidiModeParamId value:mode];
                return;
            }
            for (uint32_t sign = 0u;
                 sign < static_cast<uint32_t>(
                     s3g::NoInputMatrixMidiSign::Count); ++sign) {
                if (!NSPointInRect(point, matrixMidiSignButtonRect(sign)))
                    continue;
                [self applyGuiParam:kMatrixMidiSignParamId value:sign];
                return;
            }
            if (NSPointInRect(point, matrixMidiRampHitRect())) {
                [self beginSlider:kMatrixMidiRampParamId
                    event:event point:point];
                return;
            }
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
                    const float current = visible.params.matrix[
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
                        const float stored = visible.params.matrix[
                            destination * kChannelCount + source];
                        if (std::abs(stored) <= 0.001f) continue;
                        const NSPoint a = wiringPortPoint(false, source);
                        const NSPoint b = wiringPortPoint(true, destination);
                        NSPoint c1;
                        NSPoint c2;
                        wiringControlPoints(a, b, visible.params.vortex,
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
                    const NSRect edit = actual(popupInsertEditRect(
                        popupStripRect(lane), slot));
                    if (NSPointInRect(point, edit)) {
                        plugin->selectedSlot.store(slot,
                            std::memory_order_relaxed);
                        [self openEffectEditorForLane:lane slot:slot];
                        [self setNeedsDisplay:YES];
                        return;
                    }
                    const NSRect menu = actual(popupInsertMenuRect(
                        popupStripRect(lane), slot));
                    if (!NSPointInRect(point, menu)) continue;
                    plugin->selectedSlot.store(slot,
                        std::memory_order_relaxed);
                    _effectMenuLane = lane;
                    _effectMenuSlot = slot;
                    _menuAnchor = menu;
                    _openMenu = kMenuMixerInsert;
                    _hoverMenuItem = -1;
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
                        value:(visible.params.lanes[lane].mute == 0u
                            ? 1.0 : 0.0)];
                    return;
                }
                [self setNeedsDisplay:YES];
                return;
            }
            for (uint32_t bus = 0u; bus < 2u; ++bus) {
                const NSRect muteRect = actual(popupAuxMuteRect(bus));
                if (NSPointInRect(point, muteRect)) {
                    [self applyGuiParam:bus == 0u ? kAuxAMuteParamId
                            : kAuxBMuteParamId
                        value:visible.auxMute[bus] == 0u ? 1.0 : 0.0];
                    return;
                }
                const NSRect editRect = actual(popupAuxTypeEditRect(bus));
                if (NSPointInRect(point, editRect)) {
                    [self openEffectEditorForAux:bus];
                    return;
                }
                const NSRect typeRect = actual(popupAuxTypeMenuRect(bus));
                if (NSPointInRect(point, typeRect)) {
                    _effectMenuBus = bus;
                    _menuAnchor = typeRect;
                    _openMenu = kMenuMixerAux;
                    _hoverMenuItem = -1;
                    [self setNeedsDisplay:YES];
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
            _hoverMenuItem = -1;
            [self setNeedsDisplay:YES];
            return true;
        }
        return false;
    };
    if (page == 2u) {
        const uint32_t lane = plugin->selectedLane.load(
            std::memory_order_relaxed);
        for (uint32_t slot = 0u;
             slot < s3g::kNoInputMixerInsertSlots; ++slot) {
            if (!NSPointInRect(point, channelInsertEditRect(slot))) continue;
            plugin->selectedSlot.store(slot, std::memory_order_relaxed);
            [self openEffectEditorForLane:lane slot:slot];
            [self setNeedsDisplay:YES];
            return;
        }
    }
    if (page == 0u) {
        for (uint32_t bank = 0u; bank < 3u; ++bank) {
            if (!NSPointInRect(point, movementBankButtonRect(bank))) continue;
            _movementBank = bank;
            _openMenu = kMenuNone;
            _hoverMenuItem = -1;
            [self setNeedsDisplay:YES];
            return;
        }
    }
    if ((page == 0u && (openMenuIfHit(kMenuSource)
            || openMenuIfHit(kMenuDestination)
            || (_movementBank == 0u && openMenuIfHit(kMenuMotionShape))
            || (_movementBank == 1u && openMenuIfHit(kMenuBehavior))
            || (_movementBank == 2u && openMenuIfHit(kMenuReactMode))
            || (_movementBank == 0u && visible.params.clockSync
                && openMenuIfHit(kMenuFieldDivision))
            || (_movementBank == 1u
                && visible.behavior.behavior
                    != s3g::NoInputMovementBehavior::Glide
                && visible.params.clockSync
                && openMenuIfHit(kMenuEventDivision))))
        || (page == 2u && (openMenuIfHit(kMenuLane)
            || openMenuIfHit(kMenuSlot0) || openMenuIfHit(kMenuSlot1)
            || openMenuIfHit(kMenuSlot2)))
        || (page == 3u && openMenuIfHit(kMenuQuality))) return;

    if (page == 0u && NSPointInRect(point, seedNewButtonRect())) {
        enqueueGuiCommand(*plugin, { GuiCommandType::NewSeed });
        [self markPatchCustom];
        [self setNeedsDisplay:YES];
        return;
    }
    if (page == 0u) {
        const auto visible = uiSnapshot(*plugin);
        const clap_id toggleIds[3] = {
            kControllerHoldParamId, kSlowTimeParamId, kClockSyncParamId,
        };
        const uint32_t toggleValues[3] = {
            visible.params.controllerHold,
            visible.params.slowTime,
            visible.params.clockSync,
        };
        for (uint32_t index = 0u; index < 3u; ++index) {
            if (!NSPointInRect(point,
                    movementGlobalToggleRect(index))) continue;
            [self applyGuiParam:toggleIds[index]
                value:toggleValues[index] == 0u ? 1.0 : 0.0];
            return;
        }
    }
    if (page == 0u && _movementBank == 2u
        && NSPointInRect(point, reactDirectionButtonRect())) {
        const auto visible = uiSnapshot(*plugin);
        [self applyGuiParam:kReactPolarityParamId
            value:visible.params.reactPolarity < 0.0f ? 1.0 : -1.0];
        return;
    }
    if (page == 0u && NSPointInRect(point,
            movementSectionRandomRect())) {
        const auto previous = uiSnapshot(*plugin);
        uint32_t seed = previous.params.seed * 1664525u + 1013904223u;
        if (seed == 0u) seed = 1u;
        plugin->uiSeed.store(seed, std::memory_order_relaxed);
        if (!enqueueGuiCommand(*plugin, { GuiCommandType::SetSeed,
                CLAP_INVALID_ID, 0.0, 0u, seed })) {
            plugin->uiSeed.store(previous.params.seed,
                std::memory_order_relaxed);
            return;
        }
        const auto energy = static_cast<s3g::NoInputRandomEnergy>(
            std::min<uint32_t>(_randomEnergyProfile,
                static_cast<uint32_t>(s3g::NoInputRandomEnergy::Count) - 1u));
        const auto randomParams = s3g::randomizedNoInputMixerParams(
            seed, energy);
        if (_movementBank == 0u) {
            [self applyGuiParam:kFlowParamId value:randomParams.flow];
            [self applyGuiParam:kSpreadParamId value:randomParams.spread];
            [self applyGuiParam:kVortexParamId value:randomParams.vortex];
            [self applyGuiParam:kMotionParamId value:randomParams.motion];
            [self applyGuiParam:kMotionShapeParamId
                value:static_cast<double>(randomParams.motionShape)];
            [self applyGuiParam:kMotionRateParamId
                value:randomParams.motionRate];
            [self applyGuiParam:kMotionPhaseParamId
                value:randomParams.motionPhase];
        } else if (_movementBank == 1u) {
            const auto randomBehavior =
                s3g::randomizedNoInputMovementBehaviorParams(
                    seed ^ 0x43564d58u, energy);
            [self applyGuiParam:kBehaviorParamId
                value:static_cast<double>(randomBehavior.behavior)];
            [self applyGuiParam:kBehaviorDepthParamId
                value:randomParams.motion];
            [self applyGuiParam:kEventRateParamId
                value:randomBehavior.eventRate];
            [self applyGuiParam:kEventLengthParamId
                value:randomBehavior.length];
            [self applyGuiParam:kEventDensityParamId
                value:randomBehavior.density];
            [self applyGuiParam:kEventChaosParamId
                value:randomBehavior.chaos];
            [self applyGuiParam:kEventSlewParamId
                value:randomBehavior.slew];
            [self applyGuiParam:kEventChokeParamId
                value:randomBehavior.choke];
        } else {
            [self applyGuiParam:kReactModeParamId
                value:static_cast<double>(randomParams.reactMode)];
            [self applyGuiParam:kReactDepthParamId
                value:randomParams.reactDepth];
            [self applyGuiParam:kReactThresholdParamId
                value:randomParams.reactThreshold];
            [self applyGuiParam:kReactAttackParamId
                value:randomParams.reactAttack];
            [self applyGuiParam:kReactReleaseParamId
                value:randomParams.reactRelease];
            [self applyGuiParam:kReactPolarityParamId
                value:randomParams.reactPolarity];
        }
        return;
    }
    if (page == 0u && NSPointInRect(point, randomButtonRect())) {
        _openMenu = kMenuRandomEnergy;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    if (page == 0u && NSPointInRect(point, forgetButtonRect())) {
        const auto previous = uiSnapshot(*plugin);
        uint32_t seed = previous.params.seed * 1664525u + 1013904223u;
        if (seed == 0u) seed = 1u;
        auto patch = s3g::forgottenNoInputMixerParams(
            previous.params, seed);
        patch.outputGainDb = previous.params.outputGainDb;
        NoInputSurfaceSnapshot desired { patch, previous.behavior,
            previous.auxMute, previous.behaviorDepth };
        publishUiSnapshot(*plugin, desired);
        if (!enqueueGuiCommand(*plugin, { GuiCommandType::ForgetPatch,
                CLAP_INVALID_ID, 0.0, 0u, seed })) {
            publishUiSnapshot(*plugin, previous);
            return;
        }
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
            "FORGOTTEN");
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
            family.panicButton))) {
        enqueueGuiCommand(*plugin, { GuiCommandType::Panic });
        return;
    }

    const uint32_t lane = plugin->selectedLane.load(
        std::memory_order_relaxed);
    const uint32_t slot = plugin->selectedSlot.load(
        std::memory_order_relaxed);
    if (page == 3u && NSPointInRect(point, s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(family.output, 4u)))) {
        [self applyGuiParam:kLimiterParamId
            value:(visible.params.limiterEnabled == 0u ? 1.0 : 0.0)];
        return;
    }
    if (page == 3u && NSPointInRect(point, s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(family.output, 5u)))) {
        [self applyGuiParam:kDcBlockParamId
            value:(visible.params.dcBlockEnabled == 0u ? 1.0 : 0.0)];
        return;
    }
    if (page == 2u && NSPointInRect(point, s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(family.selectedLane, 6u)))) {
        const CGFloat controlX = s3g::gui_layout::processorControlX(
            family.selectedLane.frame.x);
        if (point.x < controlX + 54.0) {
            [self applyGuiParam:laneParamId(lane, kLanePitchLockOffset)
                value:(visible.params.lanes[lane].pitchLock == 0u
                    ? 1.0 : 0.0)];
        } else if (point.x < controlX + 112.0) {
            [self applyGuiParam:laneParamId(lane, kLaneMuteOffset)
                value:(visible.params.lanes[lane].mute == 0u ? 1.0 : 0.0)];
        } else {
            enqueueGuiCommand(*plugin, { GuiCommandType::KillLane,
                CLAP_INVALID_ID, 0.0, lane, 0u });
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
        uiParameterValue(*plugin, id, route);
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
            value:(visible.params.lanes[lane].inserts[slot].bypass == 0u
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
    if (page == 3u && NSPointInRect(point,
            processorMenuRect(family.output, 2u))) {
        _openMenu = kMenuOutputFormat;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    if (page == 3u && NSPointInRect(point, s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(family.output, 3u)))) {
        [self beginSlider:kOutputRotationParamId event:event point:point];
        return;
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
        laneParamId(lane, kLaneTuneNoteOffset),
        laneParamId(lane, kLaneTuneCentsOffset),
        laneParamId(lane, kLaneLevelOffset),
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
    const clap_id fieldMovementIds[6] = {
        kFlowParamId, kSpreadParamId, kVortexParamId,
        kMotionParamId, kMotionRateParamId, kMotionPhaseParamId,
    };
    const clap_id behaviorMovementIds[7] = {
        kBehaviorDepthParamId, kEventRateParamId, kEventLengthParamId,
        kEventDensityParamId, kEventChaosParamId, kEventSlewParamId,
        kEventChokeParamId,
    };
    const clap_id reactMovementIds[4] = {
        kReactDepthParamId, kReactThresholdParamId, kReactAttackParamId,
        kReactReleaseParamId,
    };
    for (uint32_t row = 1u; row < 8u; ++row) {
        if (page == 0u && NSPointInRect(point, s3g::clap_gui::cocoaRect(
                s3g::gui_layout::sliderHitRect(family.movement, row)))) {
            if (_movementBank == 0u && row > 6u) return;
            if (_movementBank == 2u && row > 4u) return;
            if (_movementBank == 1u) {
                const auto behavior = visible.behavior.behavior;
                const bool active = behavior
                    != s3g::NoInputMovementBehavior::Glide;
                const bool timed =
                    s3g::noInputMovementBehaviorUsesLength(behavior);
                const bool density =
                    s3g::noInputMovementBehaviorUsesDensity(behavior);
                if (!active || (row == 3u && !timed)
                    || (row == 4u && !density)) return;
            }
            const clap_id movementParam = _movementBank == 0u
                ? fieldMovementIds[row - 1u]
                : (_movementBank == 1u ? behaviorMovementIds[row - 1u]
                    : reactMovementIds[row - 1u]);
            [self beginSlider:movementParam
                event:event point:point];
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
    } else if (_surfaceDragCell != -1) [self updateSurfaceDrag:point];
    else if (_mixerDragParam != CLAP_INVALID_ID) [self updateMixerDrag:point];
    else if (_dragParam != CLAP_INVALID_ID) [self updateSlider:point];
}

- (void)mouseUp:(NSEvent*)event
{
    if (_wireDragSource >= 0) {
        const NSPoint point = [self convertPoint:[event locationInWindow]
            fromView:nil];
        auto* plugin = static_cast<Plugin*>(_plugin);
        const auto visible = uiSnapshot(*plugin);
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
            const float current = visible.params.matrix[
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
    _surfaceDragCell = -1;
    _dragParam = CLAP_INVALID_ID;
    _mixerDragParam = CLAP_INVALID_ID;
}

- (void)mouseMoved:(NSEvent*)event
{
    if (_openMenu == kMenuNone) return;
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    const int next = s3g::clap_gui::dropdownHitIndex(point,
        [self menuDropdownRect:_openMenu], 18.0,
        [self menuItemCount:_openMenu]);
    if (next == _hoverMenuItem) return;
    _hoverMenuItem = next;
    [self setNeedsDisplay:YES];
}

- (void)mouseExited:(NSEvent*)event
{
    (void)event;
    if (_hoverMenuItem < 0) return;
    _hoverMenuItem = -1;
    [self setNeedsDisplay:YES];
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
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &notePorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
    if (std::strcmp(id, S3G_NIM_MIDI_FEEDBACK_EXTENSION_ID) == 0)
        return &midiFeedbackExt;
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
    "0.6.5",
    "Eight-channel zero-input feedback ecology with signed routing, per-lane EQ, and nonlinear inserts.",
    features,
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*,
    const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->surface.focus = kNoInputSurfaceDefaultFocus;
    p->surface.curve = s3g::ParameterSurfaceCurve::Soft;
    p->surface.glideMs = kNoInputSurfaceDefaultGlideMs;
    p->host = host;
    publishUiBaseState(*p);
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

#ifndef S3G_CLAP_ENTRY_SYMBOL
#define S3G_CLAP_ENTRY_SYMBOL clap_entry
#endif

extern "C" const clap_plugin_entry_t S3G_CLAP_ENTRY_SYMBOL {
    CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory,
};
