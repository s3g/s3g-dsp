#include "s3g_psd_raw_field.h"
#include "s3g_mc_to_quad.h"
#include "s3g_mc_to_stereo.h"
#include "../common/s3g_drum_midi_receive.h"

#include <clap/clap.h>
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
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr uint32_t kOutputChannels = s3g::kPsdRawFieldChannels;
constexpr uint32_t kCodecModeCount = s3g::kPsdRawFieldCodecModeCount;
constexpr uint32_t kCodecModeMax = kCodecModeCount - 1u;
constexpr uint32_t kWaveHistory = 512;
constexpr uint32_t kStateVersion = 25;
constexpr uint32_t kWaveTracePreset = 12;
constexpr uint32_t kCustomPreset = 13;
constexpr float kInitBassTrace = 0.62f;
constexpr uint32_t kGuiWidth = 1356;
constexpr uint32_t kGuiHeight = 720;
constexpr double kLeftToolboxX = 18.0;
constexpr double kRightToolboxX = 448.0;
constexpr double kModToolboxX = 878.0;
constexpr double kToolboxWidth = 414.0;
constexpr double kModToolboxWidth = 460.0;
constexpr double kModCardInset = 12.0;
constexpr double kModCardGap = 8.0;
constexpr double kModOperatorWidth =
    (kModToolboxWidth - kModCardInset * 2.0 - kModCardGap * 2.0) / 3.0;
constexpr double kMod1ToolboxX = kModToolboxX + kModCardInset;
constexpr double kMod2ToolboxX = kMod1ToolboxX + kModOperatorWidth + kModCardGap;
constexpr double kMod3ToolboxX = kMod2ToolboxX + kModOperatorWidth + kModCardGap;
constexpr double kLeftControlX =
    s3g::gui_layout::processorControlX(kLeftToolboxX);
constexpr double kRightControlX =
    s3g::gui_layout::processorControlX(kRightToolboxX);
constexpr double kModControlX =
    s3g::gui_layout::processorControlX(kModToolboxX);
constexpr double kToolboxTrackWidth =
    s3g::gui_layout::processorTrackWidth(kToolboxWidth);
constexpr double kToolboxMenuWidth =
    s3g::gui_layout::processorMenuWidth(kToolboxWidth);
constexpr double kModToolboxTrackWidth =
    60.0;
constexpr double kModToolboxMenuWidth =
    s3g::gui_layout::processorMenuWidth(kModToolboxWidth);
constexpr double kModCompactControlInset = 38.0;
constexpr double kModCompactMenuWidth = kModOperatorWidth - kModCompactControlInset - 5.0;
constexpr double kModCompactValueXOffset = 102.0;
constexpr double kModCompactValueWidth = kModOperatorWidth - kModCompactValueXOffset - 3.0;
constexpr double kModSourceRowY = 346.0;
constexpr double kModTargetRowY = 370.0;
constexpr double kModRateRowY = 394.0;
constexpr double kModRatioRowY = 418.0;
constexpr double kModIndexRowY = 442.0;
constexpr double kModFeedbackRowY = 466.0;
constexpr double kModClockRowY = 484.0;
constexpr double compactModControlX(double cardX) { return cardX + kModCompactControlInset; }
constexpr double kLabChartX = 30.0;
constexpr double kLabChartWidth = 400.0;
constexpr double kLabCardStartX = 448.0;
constexpr double kLabCardGap = 10.0;
constexpr double kLabCardWidth = (kGuiWidth - 18.0 - kLabCardStartX - kLabCardGap * 2.0) / 3.0;
constexpr double kLabSourceRowY = 286.0;
constexpr double kLabTargetRowY = 312.0;
constexpr double kLabRateRowY = 338.0;
constexpr double kLabRatioRowY = 364.0;
constexpr double kLabIndexRowY = 390.0;
constexpr double kLabFeedbackRowY = 416.0;
constexpr double kLabClockRowY = 442.0;
constexpr double kLabEnvelopeRowY = 468.0;
constexpr double kBassReceiverPanelX = 30.0;
constexpr double kBassReceiverPanelWidth = 400.0;
constexpr double kBassControlPanelX = 448.0;
constexpr double kBassControlPanelWidth = 414.0;
constexpr double kBassPathPanelX = 878.0;
constexpr double kBassPathPanelWidth = 460.0;
constexpr double labCardX(uint32_t index)
{
    return kLabCardStartX + static_cast<double>(index) * (kLabCardWidth + kLabCardGap);
}
constexpr double kToolboxTop = 230.0;
constexpr double kPatchPanelY = 516.0;
constexpr double kPresetRowY = 552.0;
constexpr double kPerformanceRowY = 586.0;
constexpr double kMidiReceivePanelX = 300.0;
constexpr double kMidiReceivePanelWidth = 300.0;
constexpr double kEnvelopeX = 620.0;
constexpr double kEnvelopeY = 574.0;
constexpr double kEnvelopeWidth = 700.0;
constexpr double kEnvelopeHeight = 94.0;
constexpr double kOutputPanelX = 18.0;
constexpr double kOutputPanelWidth = 584.0;
constexpr double kOutputFormatRowY = 642.0;
constexpr double kOutputRotationRowY = 668.0;
constexpr std::size_t kSourcePathCapacity = 4096u;

// IDs retain their nearest earlier meaning so existing automation has the best possible migration path.
constexpr clap_id kScanRateParamId = 1;
constexpr clap_id kTextureParamId = 2;
constexpr clap_id kFoldParamId = 8;
constexpr clap_id kCodecRateParamId = 10;
constexpr clap_id kBitDepthParamId = 11;
constexpr clap_id kCodecDamageParamId = 14;
constexpr clap_id kCodecModeParamId = 15;
constexpr clap_id kGainParamId = 17;
constexpr clap_id kSeedParamId = 22;
constexpr clap_id kRandomizeFieldParamId = 23;
constexpr clap_id kGeometryParamId = 24;
constexpr clap_id kChaosParamId = 26;
constexpr clap_id kChannelSchemeParamId = 29;
constexpr clap_id kChannelSpreadParamId = 30;
constexpr clap_id kShredParamId = 42;
constexpr clap_id kResonanceParamId = 43;
constexpr clap_id kDriveParamId = 48;
constexpr clap_id kPresetParamId = 50;
constexpr clap_id kRandomizePatchParamId = 51;
constexpr clap_id kEvolveParamId = 52;
constexpr clap_id kMutateParamId = 53;
constexpr clap_id kUndoParamId = 54;
constexpr clap_id kRunParamId = 55;
constexpr clap_id kPerformanceModeParamId = 56;
constexpr clap_id kAttackParamId = 57;
constexpr clap_id kDecayParamId = 58;
constexpr clap_id kSustainParamId = 59;
constexpr clap_id kReleaseParamId = 60;
constexpr clap_id kCarrierTuneParamId = 61;
constexpr clap_id kModSourceParamId = 62;
constexpr clap_id kModTargetParamId = 63;
constexpr clap_id kModRateParamId = 64;
constexpr clap_id kModRatioParamId = 65;
constexpr clap_id kModIndexParamId = 66;
constexpr clap_id kModFeedbackParamId = 67;
constexpr clap_id kModClockLockParamId = 68;
constexpr clap_id kModAlgorithmParamId = 69;
constexpr clap_id kModSource2ParamId = 70;
constexpr clap_id kModRate2ParamId = 71;
constexpr clap_id kModRatio2ParamId = 72;
constexpr clap_id kModIndex2ParamId = 73;
constexpr clap_id kModFeedback2ParamId = 74;
constexpr clap_id kModClockLock2ParamId = 75;
constexpr clap_id kModTarget2ParamId = 76;
constexpr clap_id kModSource3ParamId = 77;
constexpr clap_id kModTarget3ParamId = 78;
constexpr clap_id kModRate3ParamId = 79;
constexpr clap_id kModRatio3ParamId = 80;
constexpr clap_id kModIndex3ParamId = 81;
constexpr clap_id kModFeedback3ParamId = 82;
constexpr clap_id kModClockLock3ParamId = 83;
constexpr clap_id kModEnvelope1ParamId = 84;
constexpr clap_id kModEnvelope2ParamId = 85;
constexpr clap_id kModEnvelope3ParamId = 86;
constexpr clap_id kModulationEnabledParamId = 87;
constexpr clap_id kBassReceiverParamId = 88;
constexpr clap_id kBassBodyParamId = 89;
constexpr clap_id kBassPunchParamId = 90;
constexpr clap_id kBassTraceParamId = 91;
constexpr clap_id kBassPitchTrackingParamId = 92;
constexpr clap_id kBassGlideParamId = 93;
constexpr clap_id kBassOctaveParamId = 94;
constexpr clap_id kBassLowWidthParamId = 95;
constexpr clap_id kBassFuzzParamId = 96;
constexpr clap_id kBassMetalParamId = 97;
constexpr clap_id kBassFeedbackParamId = 98;
constexpr clap_id kMidiReceiveParamId = 99;
constexpr clap_id kOutputFormatParamId = 100;
constexpr clap_id kOutputRotationParamId = 101;

enum class SourceInterpretation : uint32_t {
    Generated = 0u,
    RawBytes = 1u,
    Waveform = 2u,
};

enum class PerformanceMode : uint32_t {
    Free = 0u,
    Midi = 1u,
};

enum class OutputFormat : uint32_t {
    Direct8 = 0u,
    QuadRing = 1u,
    StereoRing = 2u,
};

const char* outputFormatName(OutputFormat format)
{
    switch (format) {
    case OutputFormat::Direct8: return "8CH DIRECT";
    case OutputFormat::QuadRing: return "QUAD RING";
    case OutputFormat::StereoRing: return "STEREO RING";
    }
    return "8CH DIRECT";
}

enum class EnvelopeStage : uint32_t {
    Idle = 0u,
    Attack,
    Decay,
    Sustain,
    Release,
};

struct LegacyParamsV13 {
    float scanRate, texture, geometry, chaos, fold, evolve;
    uint32_t channelScheme;
    float channelSpread;
    uint32_t codecMode;
    float codecRate, bitDepth, codecDamage, drive, shred, resonance, gainDb;
    uint32_t seed;
};
static_assert(sizeof(LegacyParamsV13) == 68u, "Unexpected version-13 parameter layout");

struct LegacyParamsV15 {
    float scanRate, texture, geometry, chaos, fold, evolve;
    uint32_t channelScheme;
    float channelSpread;
    uint32_t codecMode;
    float codecRate, bitDepth, codecDamage, drive, shred, resonance, gainDb;
    uint32_t seed;
    uint32_t fieldCodecMode;
};
static_assert(sizeof(LegacyParamsV15) == 72u, "Unexpected version-15 parameter layout");

struct LegacyParamsV16 {
    float scanRate, texture, geometry, chaos, fold, evolve;
    uint32_t channelScheme;
    float channelSpread;
    uint32_t codecMode;
    float codecRate, bitDepth, codecDamage, carrierTune, drive, shred, resonance, gainDb;
    uint32_t seed;
    uint32_t fieldCodecMode;
};
static_assert(sizeof(LegacyParamsV16) == 76u, "Unexpected version-16 parameter layout");

struct LegacyParamsV17 {
    float scanRate, texture, geometry, chaos, fold, evolve;
    uint32_t channelScheme;
    float channelSpread;
    uint32_t codecMode;
    float codecRate, bitDepth, codecDamage, carrierTune, drive, shred, resonance, gainDb;
    uint32_t seed;
    uint32_t fieldCodecMode;
    uint32_t modSource;
    uint32_t modTarget;
    float modRate, modRatio, modIndex, modFeedback;
    uint32_t modClockLock;
};
static_assert(sizeof(LegacyParamsV17) == 104u, "Unexpected version-17 parameter layout");

struct LegacyParamsV18 {
    float scanRate, texture, geometry, chaos, fold, evolve;
    uint32_t channelScheme;
    float channelSpread;
    uint32_t codecMode;
    float codecRate, bitDepth, codecDamage, carrierTune, drive, shred, resonance, gainDb;
    uint32_t seed;
    uint32_t fieldCodecMode;
    uint32_t modSource;
    uint32_t modTarget;
    float modRate, modRatio, modIndex, modFeedback;
    uint32_t modClockLock;
    uint32_t modAlgorithm;
    uint32_t modSource2;
    float modRate2, modRatio2, modIndex2, modFeedback2;
    uint32_t modClockLock2;
};
static_assert(sizeof(LegacyParamsV18) == 132u, "Unexpected version-18 parameter layout");

struct LegacyParamsV19 {
    float scanRate, texture, geometry, chaos, fold, evolve;
    uint32_t channelScheme;
    float channelSpread;
    uint32_t codecMode;
    float codecRate, bitDepth, codecDamage, carrierTune, drive, shred, resonance, gainDb;
    uint32_t seed;
    uint32_t fieldCodecMode;
    uint32_t modSource;
    uint32_t modTarget;
    float modRate, modRatio, modIndex, modFeedback;
    uint32_t modClockLock;
    uint32_t modAlgorithm;
    uint32_t modSource2;
    float modRate2, modRatio2, modIndex2, modFeedback2;
    uint32_t modClockLock2;
    uint32_t modTarget2, modSource3, modTarget3;
    float modRate3, modRatio3, modIndex3, modFeedback3;
    uint32_t modClockLock3;
};
static_assert(sizeof(LegacyParamsV19) == 164u, "Unexpected version-19 parameter layout");

struct LegacyParamsV20 {
    LegacyParamsV19 previous;
    uint32_t modEnvelope1, modEnvelope2, modEnvelope3;
};
static_assert(sizeof(LegacyParamsV20) == 176u, "Unexpected version-20 parameter layout");

struct LegacyParamsV21 {
    LegacyParamsV20 previous;
    uint32_t modulationEnabled;
};
static_assert(sizeof(LegacyParamsV21) == 180u, "Unexpected version-21 parameter layout");

struct LegacyParamsV22 {
    LegacyParamsV21 previous;
    uint32_t bassReceiver;
    float bassBody;
    float bassPunch;
    float bassTrace;
    uint32_t bassPitchTracking;
    float bassGlide;
    uint32_t bassOctave;
    float bassLowWidth;
};
static_assert(sizeof(LegacyParamsV22) == 212u,
    "Unexpected version-22 parameter layout");

struct LegacySavedStateV10 {
    uint32_t version = 10u;
    uint32_t selectedPreset = 0u;
    LegacyParamsV13 params {};
};
static_assert(sizeof(LegacySavedStateV10) == 76u, "Unexpected version-10 state layout");

struct LegacySavedStateV11 {
    uint32_t version = 11u;
    uint32_t selectedPreset = 0u;
    LegacyParamsV13 params {};
    uint32_t sourceMode = 0u;
    char sourcePath[kSourcePathCapacity] {};
};
static_assert(sizeof(LegacySavedStateV11) == 4176u, "Unexpected version-11 state layout");

struct LegacySavedStateV12 {
    uint32_t version = 12u;
    uint32_t selectedPreset = 0u;
    LegacyParamsV13 params {};
    uint32_t sourceMode = 0u;
    char sourcePath[kSourcePathCapacity] {};
};
static_assert(sizeof(LegacySavedStateV12) == 4176u, "Unexpected version-12 state layout");

struct LegacySavedStateV13 {
    uint32_t version = 13u;
    uint32_t selectedPreset = 0u;
    LegacyParamsV13 params {};
    uint32_t sourceMode = 0u;
    uint32_t runState = 1u;
    char sourcePath[kSourcePathCapacity] {};
};
static_assert(sizeof(LegacySavedStateV13) == 4180u, "Unexpected version-13 state layout");

struct LegacySavedStateV14 {
    uint32_t version = 14u;
    uint32_t selectedPreset = 0u;
    LegacyParamsV15 params {};
    uint32_t sourceMode = 0u;
    uint32_t runState = 1u;
    char sourcePath[kSourcePathCapacity] {};
};
static_assert(sizeof(LegacySavedStateV14) == 4184u, "Unexpected version-14 state layout");

struct LegacySavedStateV15 {
    uint32_t version = 15u;
    uint32_t selectedPreset = 0u;
    LegacyParamsV15 params {};
    uint32_t sourceMode = 0u;
    uint32_t runState = 1u;
    uint32_t performanceMode = static_cast<uint32_t>(PerformanceMode::Free);
    float attackMs = 12.0f;
    float decayMs = 280.0f;
    float sustain = 0.72f;
    float releaseMs = 850.0f;
    char sourcePath[kSourcePathCapacity] {};
};
static_assert(sizeof(LegacySavedStateV15) == 4204u, "Unexpected version-15 state layout");

struct LegacySavedStateV16 {
    uint32_t version = 16u;
    uint32_t selectedPreset = 0u;
    LegacyParamsV16 params {};
    uint32_t sourceMode = 0u;
    uint32_t runState = 1u;
    uint32_t performanceMode = static_cast<uint32_t>(PerformanceMode::Free);
    float attackMs = 12.0f;
    float decayMs = 280.0f;
    float sustain = 0.72f;
    float releaseMs = 850.0f;
    char sourcePath[kSourcePathCapacity] {};
};
static_assert(sizeof(LegacySavedStateV16) == 4208u, "Unexpected version-16 state layout");

struct LegacySavedStateV17 {
    uint32_t version = 17u;
    uint32_t selectedPreset = 0u;
    LegacyParamsV17 params {};
    uint32_t sourceMode = 0u;
    uint32_t runState = 1u;
    uint32_t performanceMode = static_cast<uint32_t>(PerformanceMode::Free);
    float attackMs = 12.0f;
    float decayMs = 280.0f;
    float sustain = 0.72f;
    float releaseMs = 850.0f;
    char sourcePath[kSourcePathCapacity] {};
};
static_assert(sizeof(LegacySavedStateV17) == 4236u, "Unexpected version-17 state layout");

struct LegacySavedStateV18 {
    uint32_t version = 18u;
    uint32_t selectedPreset = 0u;
    LegacyParamsV18 params {};
    uint32_t sourceMode = 0u;
    uint32_t runState = 1u;
    uint32_t performanceMode = static_cast<uint32_t>(PerformanceMode::Free);
    float attackMs = 12.0f;
    float decayMs = 280.0f;
    float sustain = 0.72f;
    float releaseMs = 850.0f;
    char sourcePath[kSourcePathCapacity] {};
};
static_assert(sizeof(LegacySavedStateV18) == 4264u, "Unexpected version-18 state layout");

struct LegacySavedStateV19 {
    uint32_t version = 19u;
    uint32_t selectedPreset = 0u;
    LegacyParamsV19 params {};
    uint32_t sourceMode = 0u;
    uint32_t runState = 1u;
    uint32_t performanceMode = static_cast<uint32_t>(PerformanceMode::Free);
    float attackMs = 12.0f;
    float decayMs = 280.0f;
    float sustain = 0.72f;
    float releaseMs = 850.0f;
    char sourcePath[kSourcePathCapacity] {};
};
static_assert(sizeof(LegacySavedStateV19) == 4296u, "Unexpected version-19 state layout");

struct LegacySavedStateV20 {
    uint32_t version = 20u;
    uint32_t selectedPreset = 0u;
    LegacyParamsV20 params {};
    uint32_t sourceMode = 0u;
    uint32_t runState = 1u;
    uint32_t performanceMode = static_cast<uint32_t>(PerformanceMode::Free);
    float attackMs = 12.0f;
    float decayMs = 280.0f;
    float sustain = 0.72f;
    float releaseMs = 850.0f;
    char sourcePath[kSourcePathCapacity] {};
};
static_assert(sizeof(LegacySavedStateV20) == 4308u, "Unexpected version-20 state layout");

struct LegacySavedStateV21 {
    uint32_t version = 21u;
    uint32_t selectedPreset = 0u;
    LegacyParamsV21 params {};
    uint32_t sourceMode = 0u;
    uint32_t runState = 1u;
    uint32_t performanceMode = static_cast<uint32_t>(PerformanceMode::Free);
    float attackMs = 12.0f;
    float decayMs = 280.0f;
    float sustain = 0.72f;
    float releaseMs = 850.0f;
    char sourcePath[kSourcePathCapacity] {};
};
static_assert(sizeof(LegacySavedStateV21) == 4312u, "Unexpected version-21 state layout");

struct LegacySavedStateV22 {
    uint32_t version = 22u;
    uint32_t selectedPreset = 0u;
    LegacyParamsV22 params {};
    uint32_t sourceMode = 0u;
    uint32_t runState = 1u;
    uint32_t performanceMode = static_cast<uint32_t>(PerformanceMode::Free);
    float attackMs = 12.0f;
    float decayMs = 280.0f;
    float sustain = 0.72f;
    float releaseMs = 850.0f;
    char sourcePath[kSourcePathCapacity] {};
};
static_assert(sizeof(LegacySavedStateV22) == 4344u,
    "Unexpected version-22 state layout");

struct LegacySavedStateV23 {
    uint32_t version = 23u;
    uint32_t selectedPreset = 0u;
    s3g::PsdRawFieldParams params {};
    uint32_t sourceMode = 0u;
    uint32_t runState = 1u;
    uint32_t performanceMode = static_cast<uint32_t>(PerformanceMode::Free);
    float attackMs = 12.0f;
    float decayMs = 280.0f;
    float sustain = 0.72f;
    float releaseMs = 850.0f;
    char sourcePath[kSourcePathCapacity] {};
};
static_assert(sizeof(LegacySavedStateV23) == 4356u,
    "Unexpected version-23 state layout");

struct LegacySavedStateV24 {
    uint32_t version = 24u;
    uint32_t selectedPreset = 0u;
    s3g::PsdRawFieldParams params {};
    uint32_t sourceMode = 0u;
    uint32_t runState = 1u;
    uint32_t performanceMode = static_cast<uint32_t>(PerformanceMode::Free);
    float attackMs = 12.0f;
    float decayMs = 280.0f;
    float sustain = 0.72f;
    float releaseMs = 850.0f;
    char sourcePath[kSourcePathCapacity] {};
    uint32_t midiReceive = 0u;
};
static_assert(sizeof(LegacySavedStateV24) == 4360u,
    "Unexpected version-24 state layout");

struct SavedState {
    uint32_t version = kStateVersion;
    uint32_t selectedPreset = 0u;
    s3g::PsdRawFieldParams params {};
    uint32_t sourceMode = 0u;
    uint32_t runState = 1u;
    uint32_t performanceMode = static_cast<uint32_t>(PerformanceMode::Free);
    float attackMs = 12.0f;
    float decayMs = 280.0f;
    float sustain = 0.72f;
    float releaseMs = 850.0f;
    char sourcePath[kSourcePathCapacity] {};
    uint32_t midiReceive = 0u;
    uint32_t outputFormat = static_cast<uint32_t>(OutputFormat::Direct8);
    float outputRotationDeg = 0.0f;
};
static_assert(sizeof(SavedState) == 4368u, "Unexpected version-25 state layout");

struct LegacyParamsV8 {
    float rawRate, strata, compression, masks, metadata, colorBleed, byteSkew, channelSpread, fold;
    float connections, connectionRate, periodChaos, pitchJumps, interpDisaster;
    float dcBlock, downsample, bitDepth, predictor, packetLoss, codebook;
    float feedback, feedbackTime, feedbackTone, feedbackCross;
    float stochastic, ampStep, durStep, stochasticMemory, tendency, rest;
    float shaper, shaperInput, shaperPressure, shaperShred, shaperFeedback;
    float shaperColor, shaperReact, shaperTune, shaperBody, shaperSpread;
    uint32_t codecMode, compandMode, channelScheme, sourceMode, ampDistribution, durDistribution;
    float gainDb;
    uint32_t seed;
};
static_assert(sizeof(LegacyParamsV8) == 192u, "Unexpected version-8 state layout");

struct LegacyParamsV9 {
    float scanRate, texture, geometry, chaos, fold;
    uint32_t channelScheme;
    float channelSpread;
    uint32_t codecMode;
    float codecRate, bitDepth, codecDamage, drive, shred, resonance, gainDb;
    uint32_t seed;
};
static_assert(sizeof(LegacyParamsV9) == 64u, "Unexpected version-9 state layout");

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0;
    s3g::PsdRawFieldParams params {};
    s3g::PsdRawFieldMorph field;
    std::shared_ptr<const s3g::PsdRawFieldSource> rawSource;
    std::string sourcePath;
    std::string sourceName;
    std::string sourceError;
    SourceInterpretation sourceInterpretation = SourceInterpretation::Generated;
    std::atomic<bool> playing { true };
    float runGain = 1.0f;
    PerformanceMode performanceMode = PerformanceMode::Free;
    float attackMs = 12.0f;
    float decayMs = 280.0f;
    float sustain = 0.72f;
    float releaseMs = 850.0f;
    double midiReceive = 0.0;
    OutputFormat outputFormat = OutputFormat::Direct8;
    float outputRotationDeg = 0.0f;
    EnvelopeStage envelopeStage = EnvelopeStage::Idle;
    float envelopeValue = 0.0f;
    bool envelopeGate = false;
    float activeVelocity = 1.0f;
    int32_t activeNote = -1;
    uint64_t noteOrderCounter = 0u;
    std::array<uint16_t, 128> heldNoteCount {};
    std::array<float, 128> heldVelocity {};
    std::array<uint64_t, 128> heldNoteOrder {};
    std::atomic<int32_t> displayNote { -1 };
    std::atomic<float> displayEnvelope { 0.0f };
    std::atomic<uint32_t> displayEnvelopeStage { static_cast<uint32_t>(EnvelopeStage::Idle) };
    uint32_t selectedPreset = 0u;
    s3g::PsdRawFieldParams undoParams {};
    std::shared_ptr<const s3g::PsdRawFieldSource> undoSource;
    std::string undoSourcePath;
    std::string undoSourceName;
    std::string undoSourceError;
    SourceInterpretation undoSourceInterpretation = SourceInterpretation::Generated;
    uint32_t undoPreset = 0u;
    bool hasUndo = false;
    std::vector<std::vector<float>> output32;
    std::vector<float*> outputPtrs;
    std::vector<float> modulationEnvelope;
    std::vector<float> renderGain;
    std::atomic<float> outputPeak { 0.0f };
    std::array<std::array<float, kWaveHistory>, kOutputChannels> waveHistory {};
    std::atomic<uint32_t> waveWrite { 0u };
#if defined(__APPLE__)
    void* guiView = nullptr;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
    bool guiVisible = false;
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

uint32_t hash32(uint32_t x)
{
    x ^= x >> 16u;
    x *= 0x7feb352du;
    x ^= x >> 15u;
    x *= 0x846ca68bu;
    x ^= x >> 16u;
    return x;
}

void resetMidiPerformance(Plugin& p)
{
    p.envelopeStage = EnvelopeStage::Idle;
    p.envelopeValue = 0.0f;
    p.envelopeGate = false;
    p.activeVelocity = 1.0f;
    p.activeNote = -1;
    p.noteOrderCounter = 0u;
    p.heldNoteCount.fill(0u);
    p.heldVelocity.fill(0.0f);
    p.heldNoteOrder.fill(0u);
    p.displayNote.store(-1, std::memory_order_relaxed);
    p.displayEnvelope.store(0.0f, std::memory_order_relaxed);
    p.displayEnvelopeStage.store(static_cast<uint32_t>(EnvelopeStage::Idle), std::memory_order_relaxed);
    p.field.setPitchRatio(1.0f);
}

void setActiveNote(Plugin& p, uint32_t key, float velocity)
{
    key = std::min<uint32_t>(key, 127u);
    p.activeNote = static_cast<int32_t>(key);
    p.activeVelocity = std::clamp(velocity, 0.0f, 1.0f);
    p.displayNote.store(static_cast<int32_t>(key), std::memory_order_relaxed);
    p.field.setPitchRatio(std::pow(2.0f, (static_cast<float>(key) - 60.0f) / 12.0f));
}

int32_t latestHeldNote(const Plugin& p)
{
    int32_t selected = -1;
    uint64_t newest = 0u;
    for (uint32_t key = 0u; key < p.heldNoteCount.size(); ++key) {
        if (p.heldNoteCount[key] > 0u && (selected < 0 || p.heldNoteOrder[key] > newest)) {
            selected = static_cast<int32_t>(key);
            newest = p.heldNoteOrder[key];
        }
    }
    return selected;
}

void midiNoteOn(Plugin& p, int32_t key, float velocity)
{
    if (p.performanceMode != PerformanceMode::Midi || key < 0 || key > 127 || velocity <= 0.0f) return;
    const uint32_t index = static_cast<uint32_t>(key);
    if (p.heldNoteCount[index] < std::numeric_limits<uint16_t>::max()) ++p.heldNoteCount[index];
    p.heldVelocity[index] = std::clamp(velocity, 0.0f, 1.0f);
    p.heldNoteOrder[index] = ++p.noteOrderCounter;
    setActiveNote(p, index, p.heldVelocity[index]);
    p.field.triggerBassExcite(p.heldVelocity[index]);
    p.envelopeGate = true;
    p.envelopeStage = EnvelopeStage::Attack;
}

void releaseMidiEnvelope(Plugin& p, bool immediate)
{
    p.envelopeGate = false;
    p.activeNote = -1;
    p.displayNote.store(-1, std::memory_order_relaxed);
    if (immediate) {
        p.envelopeStage = EnvelopeStage::Idle;
        p.envelopeValue = 0.0f;
        p.displayEnvelope.store(0.0f, std::memory_order_relaxed);
    } else if (p.envelopeStage != EnvelopeStage::Idle) {
        p.envelopeStage = EnvelopeStage::Release;
    }
}

void midiAllNotesOff(Plugin& p, bool immediate)
{
    p.heldNoteCount.fill(0u);
    p.heldNoteOrder.fill(0u);
    releaseMidiEnvelope(p, immediate);
}

void midiNoteOff(Plugin& p, int32_t key, bool immediate)
{
    if (p.performanceMode != PerformanceMode::Midi) return;
    if (key < 0 || key > 127) {
        midiAllNotesOff(p, immediate);
        return;
    }
    const uint32_t index = static_cast<uint32_t>(key);
    if (p.heldNoteCount[index] > 0u) --p.heldNoteCount[index];
    if (p.heldNoteCount[index] == 0u) p.heldNoteOrder[index] = 0u;
    if (p.activeNote != key || p.heldNoteCount[index] > 0u) return;

    const int32_t fallback = latestHeldNote(p);
    if (fallback >= 0) {
        const uint32_t fallbackIndex = static_cast<uint32_t>(fallback);
        setActiveNote(p, fallbackIndex, p.heldVelocity[fallbackIndex]);
    } else {
        releaseMidiEnvelope(p, immediate);
    }
}

float envelopeCoefficient(float milliseconds, double sampleRate)
{
    const float samples = std::max(1.0f,
        milliseconds * 0.001f * static_cast<float>(std::max(1.0, sampleRate)));
    return 1.0f - std::exp(-6.90775527898f / samples);
}

float processMidiEnvelope(Plugin& p, float attack, float decay, float release)
{
    switch (p.envelopeStage) {
    case EnvelopeStage::Attack:
        p.envelopeValue += (1.0f - p.envelopeValue) * attack;
        if (p.envelopeValue >= 0.999f) {
            p.envelopeValue = 1.0f;
            p.envelopeStage = EnvelopeStage::Decay;
        }
        break;
    case EnvelopeStage::Decay:
        p.envelopeValue += (p.sustain - p.envelopeValue) * decay;
        if (std::abs(p.envelopeValue - p.sustain) <= 0.001f) {
            p.envelopeValue = p.sustain;
            p.envelopeStage = EnvelopeStage::Sustain;
        }
        break;
    case EnvelopeStage::Sustain:
        p.envelopeValue = p.sustain;
        if (!p.envelopeGate) p.envelopeStage = EnvelopeStage::Release;
        break;
    case EnvelopeStage::Release:
        p.envelopeValue += (0.0f - p.envelopeValue) * release;
        if (p.envelopeValue <= 0.00005f) {
            p.envelopeValue = 0.0f;
            p.envelopeStage = EnvelopeStage::Idle;
        }
        break;
    case EnvelopeStage::Idle:
    default:
        p.envelopeValue = 0.0f;
        break;
    }
    return p.envelopeValue;
}

std::string sourceNameFromPath(const std::string& path)
{
    const std::size_t separator = path.find_last_of("/\\");
    if (separator == std::string::npos || separator + 1u >= path.size()) return path;
    return path.substr(separator + 1u);
}

// WAV structure is used only to derive an eight-lane byte field; playback remains deliberately lossy.
enum class WaveEncoding : uint32_t {
    Unsigned8,
    Signed16,
    Signed24,
    Signed32,
    Float32,
    Float64,
};

struct WaveFileInfo {
    uint64_t fileByteCount = 0u;
    uint64_t dataOffset = 0u;
    uint64_t dataByteCount = 0u;
    uint32_t sampleRate = 0u;
    uint32_t channelCount = 0u;
    uint32_t bitsPerSample = 0u;
    uint32_t blockAlign = 0u;
    uint32_t bytesPerSample = 0u;
    WaveEncoding encoding = WaveEncoding::Unsigned8;
};

uint16_t little16(const uint8_t* bytes)
{
    return static_cast<uint16_t>(bytes[0])
        | static_cast<uint16_t>(static_cast<uint16_t>(bytes[1]) << 8u);
}

uint32_t little32(const uint8_t* bytes)
{
    return static_cast<uint32_t>(bytes[0])
        | (static_cast<uint32_t>(bytes[1]) << 8u)
        | (static_cast<uint32_t>(bytes[2]) << 16u)
        | (static_cast<uint32_t>(bytes[3]) << 24u);
}

uint64_t little64(const uint8_t* bytes)
{
    return static_cast<uint64_t>(little32(bytes))
        | (static_cast<uint64_t>(little32(bytes + 4u)) << 32u);
}

bool inspectWaveFile(const std::string& path, WaveFileInfo& info, std::string& error)
{
    info = {};
    error.clear();
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        error = "FILE NOT FOUND";
        return false;
    }
    const std::streamoff end = input.tellg();
    if (end < 12) {
        error = "NOT A PCM WAVE FILE";
        return false;
    }
    info.fileByteCount = static_cast<uint64_t>(end);
    input.seekg(0, std::ios::beg);
    std::array<uint8_t, 12> riff {};
    input.read(reinterpret_cast<char*>(riff.data()), static_cast<std::streamsize>(riff.size()));
    if (input.gcount() != static_cast<std::streamsize>(riff.size())
        || std::memcmp(riff.data(), "RIFF", 4u) != 0
        || std::memcmp(riff.data() + 8u, "WAVE", 4u) != 0) {
        error = "NOT A PCM WAVE FILE";
        return false;
    }

    bool foundFormat = false;
    bool foundData = false;
    uint64_t offset = 12u;
    while (offset + 8u <= info.fileByteCount) {
        input.clear();
        input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        std::array<uint8_t, 8> chunk {};
        input.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
        if (input.gcount() != static_cast<std::streamsize>(chunk.size())) break;
        const uint64_t chunkSize = little32(chunk.data() + 4u);
        const uint64_t chunkData = offset + 8u;
        const uint64_t available = chunkData < info.fileByteCount ? info.fileByteCount - chunkData : 0u;

        if (std::memcmp(chunk.data(), "fmt ", 4u) == 0 && chunkSize >= 16u && available >= 16u) {
            std::array<uint8_t, 64> format {};
            const std::size_t readSize = static_cast<std::size_t>(std::min<uint64_t>(format.size(), std::min(chunkSize, available)));
            input.read(reinterpret_cast<char*>(format.data()), static_cast<std::streamsize>(readSize));
            if (input.gcount() == static_cast<std::streamsize>(readSize)) {
                uint16_t formatTag = little16(format.data());
                info.channelCount = little16(format.data() + 2u);
                info.sampleRate = little32(format.data() + 4u);
                info.blockAlign = little16(format.data() + 12u);
                info.bitsPerSample = little16(format.data() + 14u);
                if (formatTag == 0xfffeu && readSize >= 40u) formatTag = little16(format.data() + 24u);
                info.bytesPerSample = (info.bitsPerSample + 7u) / 8u;
                bool supported = true;
                if (formatTag == 1u) {
                    if (info.bitsPerSample == 8u) info.encoding = WaveEncoding::Unsigned8;
                    else if (info.bitsPerSample == 16u) info.encoding = WaveEncoding::Signed16;
                    else if (info.bitsPerSample == 24u) info.encoding = WaveEncoding::Signed24;
                    else if (info.bitsPerSample == 32u) info.encoding = WaveEncoding::Signed32;
                    else supported = false;
                } else if (formatTag == 3u) {
                    if (info.bitsPerSample == 32u) info.encoding = WaveEncoding::Float32;
                    else if (info.bitsPerSample == 64u) info.encoding = WaveEncoding::Float64;
                    else supported = false;
                } else {
                    supported = false;
                }
                foundFormat = supported && info.channelCount > 0u && info.channelCount <= 64u
                    && info.sampleRate > 0u && info.sampleRate <= 768000u
                    && info.blockAlign >= info.channelCount * info.bytesPerSample;
            }
        } else if (std::memcmp(chunk.data(), "data", 4u) == 0 && !foundData) {
            info.dataOffset = chunkData;
            info.dataByteCount = std::min(chunkSize, available);
            foundData = info.dataByteCount > 0u;
        }

        if (foundFormat && foundData) break;
        if (chunkSize > info.fileByteCount || chunkData + chunkSize < chunkData) break;
        const uint64_t next = chunkData + chunkSize + (chunkSize & 1u);
        if (next <= offset || next > info.fileByteCount) break;
        offset = next;
    }

    if (!foundFormat || !foundData || info.dataByteCount < info.blockAlign) {
        error = "UNSUPPORTED OR EMPTY WAVE DATA";
        return false;
    }
    return true;
}

float decodeWaveSample(const std::vector<uint8_t>& bytes, const WaveFileInfo& info, uint64_t frame, uint32_t channel)
{
    const uint64_t byteOffset = frame * info.blockAlign + static_cast<uint64_t>(channel) * info.bytesPerSample;
    if (byteOffset + info.bytesPerSample > bytes.size()) return 0.0f;
    const uint8_t* sample = bytes.data() + byteOffset;
    switch (info.encoding) {
    case WaveEncoding::Unsigned8:
        return (static_cast<float>(sample[0]) - 127.5f) * (1.0f / 127.5f);
    case WaveEncoding::Signed16: {
        const uint32_t value = little16(sample);
        const int32_t signedValue = value >= 0x8000u ? static_cast<int32_t>(value) - 0x10000 : static_cast<int32_t>(value);
        return static_cast<float>(signedValue) * (1.0f / 32768.0f);
    }
    case WaveEncoding::Signed24: {
        const uint32_t value = static_cast<uint32_t>(sample[0])
            | (static_cast<uint32_t>(sample[1]) << 8u)
            | (static_cast<uint32_t>(sample[2]) << 16u);
        const int32_t signedValue = (value & 0x800000u) != 0u
            ? static_cast<int32_t>(value | 0xff000000u)
            : static_cast<int32_t>(value);
        return static_cast<float>(signedValue) * (1.0f / 8388608.0f);
    }
    case WaveEncoding::Signed32: {
        const uint32_t value = little32(sample);
        const int64_t signedValue = value >= 0x80000000u
            ? static_cast<int64_t>(value) - 0x100000000ll
            : static_cast<int64_t>(value);
        return static_cast<float>(static_cast<double>(signedValue) * (1.0 / 2147483648.0));
    }
    case WaveEncoding::Float32: {
        const uint32_t bits = little32(sample);
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        return std::isfinite(value) ? std::clamp(value, -1.0f, 1.0f) : 0.0f;
    }
    case WaveEncoding::Float64: {
        const uint64_t bits = little64(sample);
        double value = 0.0;
        std::memcpy(&value, &bits, sizeof(value));
        return std::isfinite(value) ? static_cast<float>(std::clamp(value, -1.0, 1.0)) : 0.0f;
    }
    }
    return 0.0f;
}

bool readRawByteSource(
    const std::string& path,
    std::shared_ptr<const s3g::PsdRawFieldSource>& source,
    std::string& error)
{
    source.reset();
    error.clear();
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        error = "FILE NOT FOUND";
        return false;
    }
    const std::streamoff end = input.tellg();
    if (end <= 0) {
        error = end == 0 ? "EMPTY FILE" : "UNREADABLE FILE";
        return false;
    }
    const uint64_t originalByteCount = static_cast<uint64_t>(end);
    const std::size_t loadedByteCount = static_cast<std::size_t>(
        std::min<uint64_t>(originalByteCount, s3g::kPsdRawFieldMaxSourceBytes));
    std::vector<uint8_t> bytes;
    try {
        bytes.resize(loadedByteCount);
    } catch (...) {
        error = "NOT ENOUGH MEMORY";
        return false;
    }
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
        error = "READ ERROR";
        return false;
    }
    source = s3g::makePsdRawFieldSource(std::move(bytes), originalByteCount);
    if (!source) {
        error = "NOT ENOUGH MEMORY";
        return false;
    }
    return true;
}

bool readWaveformSource(
    const std::string& path,
    std::shared_ptr<const s3g::PsdRawFieldSource>& source,
    std::string& error)
{
    source.reset();
    WaveFileInfo info {};
    if (!inspectWaveFile(path, info, error)) return false;
    const uint64_t totalFrames = info.dataByteCount / info.blockAlign;
    const uint64_t maxFieldFrames = s3g::kPsdRawFieldMaxSourceBytes / kOutputChannels;
    const uint64_t maxInputFrames = s3g::kPsdRawFieldMaxSourceBytes / info.blockAlign;
    const uint64_t loadedFrames = std::min({ totalFrames, maxFieldFrames, maxInputFrames });
    if (loadedFrames == 0u) {
        error = "EMPTY WAVE DATA";
        return false;
    }

    std::vector<uint8_t> inputBytes;
    std::vector<uint8_t> fieldBytes;
    try {
        inputBytes.resize(static_cast<std::size_t>(loadedFrames * info.blockAlign));
        fieldBytes.resize(static_cast<std::size_t>(loadedFrames * kOutputChannels));
    } catch (...) {
        error = "NOT ENOUGH MEMORY";
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    input.seekg(static_cast<std::streamoff>(info.dataOffset), std::ios::beg);
    input.read(reinterpret_cast<char*>(inputBytes.data()), static_cast<std::streamsize>(inputBytes.size()));
    if (input.gcount() != static_cast<std::streamsize>(inputBytes.size())) {
        error = "WAVE DATA READ ERROR";
        return false;
    }

    const uint64_t peakStride = std::max<uint64_t>(1u, loadedFrames / 250000u);
    float peak = 0.0f;
    for (uint64_t frame = 0u; frame < loadedFrames; frame += peakStride) {
        for (uint32_t channel = 0u; channel < info.channelCount; ++channel) {
            peak = std::max(peak, std::abs(decodeWaveSample(inputBytes, info, frame, channel)));
        }
    }
    const float preparationGain = peak > 0.0001f
        ? std::clamp(0.82f / peak, 1.0f, 8.0f)
        : 1.0f;
    // Repeated source channels receive short prime-spaced offsets so every output lane has motion.
    constexpr std::array<uint32_t, kOutputChannels> laneOffsets { 0u, 13u, 29u, 61u, 127u, 251u, 509u, 1021u };
    for (uint64_t frame = 0u; frame < loadedFrames; ++frame) {
        for (uint32_t lane = 0u; lane < kOutputChannels; ++lane) {
            const uint32_t sourceChannel = lane % info.channelCount;
            const uint32_t repetition = std::min<uint32_t>(lane / info.channelCount, kOutputChannels - 1u);
            const uint64_t sourceFrame = (frame + laneOffsets[repetition]) % loadedFrames;
            const float sample = std::clamp(
                decodeWaveSample(inputBytes, info, sourceFrame, sourceChannel) * preparationGain,
                -1.0f,
                1.0f);
            fieldBytes[static_cast<std::size_t>(frame * kOutputChannels + lane)] = static_cast<uint8_t>(
                std::round((sample * 0.5f + 0.5f) * 255.0f));
        }
    }

    s3g::PsdRawFieldWaveformInfo waveform {};
    waveform.sampleRate = info.sampleRate;
    waveform.channelCount = info.channelCount;
    waveform.bitsPerSample = info.bitsPerSample;
    waveform.sourceFrameCount = totalFrames;
    waveform.loadedFrameCount = loadedFrames;
    waveform.sourceDataByteCount = info.dataByteCount;
    waveform.truncated = loadedFrames < totalFrames;
    source = s3g::makePsdRawFieldSource(std::move(fieldBytes), info.fileByteCount, waveform);
    if (!source) {
        error = "NOT ENOUGH MEMORY";
        return false;
    }
    error.clear();
    return true;
}

bool readSource(
    const std::string& path,
    SourceInterpretation interpretation,
    std::shared_ptr<const s3g::PsdRawFieldSource>& source,
    std::string& error)
{
    if (interpretation == SourceInterpretation::Waveform) {
        return readWaveformSource(path, source, error);
    }
    return readRawByteSource(path, source, error);
}

void markHostStateDirty(Plugin& p)
{
    if (!p.host || !p.host->get_extension) return;
    const auto* state = static_cast<const clap_host_state_t*>(p.host->get_extension(p.host, CLAP_EXT_STATE));
    if (state && state->mark_dirty) state->mark_dirty(p.host);
}

s3g::PsdRawFieldParams migrateLegacyParams(const LegacyParamsV13& old)
{
    s3g::PsdRawFieldParams result {};
    result.scanRate = old.scanRate;
    result.texture = old.texture;
    result.geometry = old.geometry;
    result.chaos = old.chaos;
    result.fold = old.fold;
    result.evolve = old.evolve;
    result.channelScheme = static_cast<s3g::PsdRawFieldChannelScheme>(std::min(old.channelScheme, 4u));
    result.channelSpread = old.channelSpread;
    result.codecMode = static_cast<s3g::PsdRawFieldCodecMode>(std::min(old.codecMode, kCodecModeMax));
    result.codecRate = old.codecRate;
    result.bitDepth = old.bitDepth;
    result.codecDamage = old.codecDamage;
    result.drive = old.drive;
    result.shred = old.shred;
    result.resonance = old.resonance;
    result.gainDb = old.gainDb;
    result.seed = old.seed;
    result.fieldCodecMode = result.codecMode;
    return result;
}

s3g::PsdRawFieldParams migrateLegacyParams(const LegacyParamsV15& old)
{
    s3g::PsdRawFieldParams result {};
    result.scanRate = old.scanRate;
    result.texture = old.texture;
    result.geometry = old.geometry;
    result.chaos = old.chaos;
    result.fold = old.fold;
    result.evolve = old.evolve;
    result.channelScheme = static_cast<s3g::PsdRawFieldChannelScheme>(std::min(old.channelScheme, 4u));
    result.channelSpread = old.channelSpread;
    result.codecMode = static_cast<s3g::PsdRawFieldCodecMode>(std::min(old.codecMode, kCodecModeMax));
    result.codecRate = old.codecRate;
    result.bitDepth = old.bitDepth;
    result.codecDamage = old.codecDamage;
    result.carrierTune = 0.0f;
    result.drive = old.drive;
    result.shred = old.shred;
    result.resonance = old.resonance;
    result.gainDb = old.gainDb;
    result.seed = old.seed;
    result.fieldCodecMode = static_cast<s3g::PsdRawFieldCodecMode>(
        std::min(old.fieldCodecMode, kCodecModeMax));
    return result;
}

s3g::PsdRawFieldParams migrateLegacyParams(const LegacyParamsV16& old)
{
    s3g::PsdRawFieldParams result {};
    result.scanRate = old.scanRate;
    result.texture = old.texture;
    result.geometry = old.geometry;
    result.chaos = old.chaos;
    result.fold = old.fold;
    result.evolve = old.evolve;
    result.channelScheme = static_cast<s3g::PsdRawFieldChannelScheme>(std::min(old.channelScheme, 4u));
    result.channelSpread = old.channelSpread;
    result.codecMode = static_cast<s3g::PsdRawFieldCodecMode>(std::min(old.codecMode, kCodecModeMax));
    result.codecRate = old.codecRate;
    result.bitDepth = old.bitDepth;
    result.codecDamage = old.codecDamage;
    result.carrierTune = old.carrierTune;
    result.drive = old.drive;
    result.shred = old.shred;
    result.resonance = old.resonance;
    result.gainDb = old.gainDb;
    result.seed = old.seed;
    result.fieldCodecMode = static_cast<s3g::PsdRawFieldCodecMode>(
        std::min(old.fieldCodecMode, kCodecModeMax));
    return result;
}

s3g::PsdRawFieldParams migrateLegacyParams(const LegacyParamsV17& old)
{
    s3g::PsdRawFieldParams result {};
    result.scanRate = old.scanRate;
    result.texture = old.texture;
    result.geometry = old.geometry;
    result.chaos = old.chaos;
    result.fold = old.fold;
    result.evolve = old.evolve;
    result.channelScheme = static_cast<s3g::PsdRawFieldChannelScheme>(std::min(old.channelScheme, 4u));
    result.channelSpread = old.channelSpread;
    result.codecMode = static_cast<s3g::PsdRawFieldCodecMode>(std::min(old.codecMode, kCodecModeMax));
    result.codecRate = old.codecRate;
    result.bitDepth = old.bitDepth;
    result.codecDamage = old.codecDamage;
    result.carrierTune = old.carrierTune;
    result.drive = old.drive;
    result.shred = old.shred;
    result.resonance = old.resonance;
    result.gainDb = old.gainDb;
    result.seed = old.seed;
    result.fieldCodecMode = static_cast<s3g::PsdRawFieldCodecMode>(
        std::min(old.fieldCodecMode, kCodecModeMax));
    result.modSource = static_cast<s3g::PsdRawFieldModSource>(std::min(
        old.modSource, s3g::kPsdRawFieldModSourceCount - 1u));
    result.modTarget = static_cast<s3g::PsdRawFieldModTarget>(std::min(
        old.modTarget, s3g::kPsdRawFieldModTargetCount - 1u));
    result.modRate = old.modRate;
    result.modRatio = old.modRatio;
    result.modIndex = old.modIndex;
    result.modFeedback = old.modFeedback;
    result.modClockLock = std::min(old.modClockLock, 1u);
    return result;
}

s3g::PsdRawFieldParams migrateLegacyParams(const LegacyParamsV18& old)
{
    LegacyParamsV17 firstOperator {};
    std::memcpy(&firstOperator, &old, sizeof(firstOperator));
    s3g::PsdRawFieldParams result = migrateLegacyParams(firstOperator);
    result.modAlgorithm = static_cast<s3g::PsdRawFieldModAlgorithm>(std::min(
        old.modAlgorithm, s3g::kPsdRawFieldModAlgorithmCount - 1u));
    result.modSource2 = static_cast<s3g::PsdRawFieldModSource>(std::min(
        old.modSource2, s3g::kPsdRawFieldModSourceCount - 1u));
    result.modRate2 = old.modRate2;
    result.modRatio2 = old.modRatio2;
    result.modIndex2 = old.modIndex2;
    result.modFeedback2 = old.modFeedback2;
    result.modClockLock2 = std::min(old.modClockLock2, 1u);
    switch (result.modAlgorithm) {
    case s3g::PsdRawFieldModAlgorithm::Relay:
        result.modTarget = s3g::PsdRawFieldModTarget::Carrier;
        result.modTarget2 = s3g::PsdRawFieldModTarget::Off;
        break;
    case s3g::PsdRawFieldModAlgorithm::Multiplex:
        result.modTarget = s3g::PsdRawFieldModTarget::Carrier;
        result.modTarget2 = s3g::PsdRawFieldModTarget::Carrier;
        break;
    case s3g::PsdRawFieldModAlgorithm::CrossedMachines:
        result.modTarget = s3g::PsdRawFieldModTarget::Carrier;
        result.modTarget2 = s3g::PsdRawFieldModTarget::Clock;
        break;
    case s3g::PsdRawFieldModAlgorithm::Regenerator:
        result.modTarget = s3g::PsdRawFieldModTarget::Carrier;
        result.modTarget2 = old.modTarget == static_cast<uint32_t>(s3g::PsdRawFieldModTarget::Clock)
            ? s3g::PsdRawFieldModTarget::Clock
            : s3g::PsdRawFieldModTarget::Damage;
        break;
    case s3g::PsdRawFieldModAlgorithm::Transcode:
        result.modTarget = s3g::PsdRawFieldModTarget::Off;
        result.modTarget2 = s3g::PsdRawFieldModTarget::Data;
        break;
    case s3g::PsdRawFieldModAlgorithm::Broadcast:
    default:
        result.modTarget2 = s3g::PsdRawFieldModTarget::Off;
        break;
    }
    return result;
}

s3g::PsdRawFieldParams migrateLegacyParams(const LegacyParamsV19& old)
{
    s3g::PsdRawFieldParams result {};
    static_assert(sizeof(old) < sizeof(result), "Version-19 parameters must be a prefix");
    std::memcpy(&result, &old, sizeof(old));
    result.modEnvelope1 = 0u;
    result.modEnvelope2 = 0u;
    result.modEnvelope3 = 0u;
    return result;
}

s3g::PsdRawFieldParams migrateLegacyParams(const LegacyParamsV20& old)
{
    s3g::PsdRawFieldParams result {};
    static_assert(sizeof(old) < sizeof(result), "Version-20 parameters must be a prefix");
    std::memcpy(&result, &old, sizeof(old));
    result.modulationEnabled = 1u;
    return result;
}

s3g::PsdRawFieldParams migrateLegacyParams(const LegacyParamsV21& old)
{
    s3g::PsdRawFieldParams result {};
    static_assert(sizeof(old) < sizeof(result), "Version-21 parameters must be a prefix");
    std::memcpy(&result, &old, sizeof(old));
    result.bassReceiver = s3g::PsdRawFieldBassReceiver::Direct;
    result.bassBody = 0.0f;
    result.bassPunch = 0.0f;
    result.bassTrace = 1.0f;
    result.bassPitchTracking = s3g::PsdRawFieldPitchTracking::Scan;
    result.bassGlide = 0.0f;
    result.bassOctave = s3g::PsdRawFieldBassOctave::MinusOne;
    result.bassLowWidth = 1.0f;
    return result;
}

s3g::PsdRawFieldParams migrateLegacyParams(const LegacyParamsV22& old)
{
    s3g::PsdRawFieldParams result {};
    static_assert(sizeof(old) < sizeof(result),
        "Version-22 parameters must be a prefix");
    std::memcpy(&result, &old, sizeof(old));
    result.bassFuzz = 0.0f;
    result.bassMetal = 0.55f;
    result.bassFeedback = 0.0f;
    return result;
}

s3g::PsdRawFieldParams migrateLegacyParams(const LegacyParamsV8& old)
{
    s3g::PsdRawFieldParams result {};
    result.scanRate = std::clamp(old.rawRate, 0.0f, 1.0f);
    result.texture = std::clamp(old.strata * 0.24f + old.compression * 0.24f + old.masks * 0.20f
        + old.colorBleed * 0.17f + old.metadata * 0.15f, 0.0f, 1.0f);
    const float sourceAmount = old.sourceMode == 0u ? 0.0f : (old.sourceMode == 1u ? 1.0f : old.stochastic);
    result.geometry = std::clamp(std::max(old.connections, sourceAmount), 0.0f, 1.0f);
    result.chaos = std::clamp(old.periodChaos * 0.24f + old.pitchJumps * 0.22f
        + old.interpDisaster * 0.24f + old.ampStep * 0.15f + old.durStep * 0.15f, 0.0f, 1.0f);
    result.fold = std::clamp(old.fold, 0.0f, 1.0f);
    result.channelScheme = static_cast<s3g::PsdRawFieldChannelScheme>(std::min(old.channelScheme, 4u));
    result.channelSpread = std::clamp(old.channelSpread, 0.0f, 1.0f);

    if (old.codecMode == 4u) result.codecMode = s3g::PsdRawFieldCodecMode::CelpScramble;
    else if (old.codecMode == 3u && old.compandMode == 2u) result.codecMode = s3g::PsdRawFieldCodecMode::ALaw;
    else if (old.codecMode == 3u && old.compandMode == 1u) result.codecMode = s3g::PsdRawFieldCodecMode::MuLaw;
    else result.codecMode = static_cast<s3g::PsdRawFieldCodecMode>(std::min(old.codecMode, 2u));
    result.codecRate = std::clamp(old.downsample, 0.0f, 1.0f);
    result.bitDepth = std::clamp(old.bitDepth, 2.0f, 16.0f);
    result.codecDamage = std::clamp(std::max({ old.packetLoss * 2.5f, old.codebook * 0.75f, old.predictor * 0.45f }), 0.0f, 1.0f);

    result.drive = std::clamp(old.shaperInput * (0.25f + old.shaper * 0.75f), 0.0f, 1.0f);
    result.shred = std::clamp(old.shaper * 0.35f + old.shaperShred * 0.45f + old.shaperPressure * 0.20f, 0.0f, 1.0f);
    result.resonance = std::clamp(old.shaperFeedback * old.shaper, 0.0f, 1.0f);
    result.gainDb = std::clamp(old.gainDb, -60.0f, 6.0f);
    result.seed = old.seed ? old.seed : 0x50434431u;
    result.fieldCodecMode = result.codecMode;
    return result;
}

s3g::PsdRawFieldParams migrateLegacyParams(const LegacyParamsV9& old)
{
    s3g::PsdRawFieldParams result {};
    result.scanRate = old.scanRate;
    result.texture = old.texture;
    result.geometry = old.geometry;
    result.chaos = old.chaos;
    result.fold = old.fold;
    result.evolve = 0.0f;
    result.channelScheme = static_cast<s3g::PsdRawFieldChannelScheme>(std::min(old.channelScheme, 4u));
    result.channelSpread = old.channelSpread;
    result.codecMode = static_cast<s3g::PsdRawFieldCodecMode>(std::min(old.codecMode, 5u));
    result.codecRate = old.codecRate;
    result.bitDepth = old.bitDepth;
    result.codecDamage = old.codecDamage;
    result.drive = old.drive;
    result.shred = old.shred;
    result.resonance = old.resonance;
    result.gainDb = old.gainDb;
    result.seed = old.seed;
    result.fieldCodecMode = result.codecMode;
    return result;
}

s3g::PsdRawFieldParams presetParams(uint32_t preset)
{
    s3g::PsdRawFieldParams result {};
    if (preset == 0u) result.bassTrace = kInitBassTrace;
    switch (preset) {
    case 1u: // Sub Clock
        result.scanRate = 0.16f; result.texture = 0.38f; result.geometry = 0.70f; result.chaos = 0.28f;
        result.fold = 0.18f; result.evolve = 0.08f; result.codecMode = s3g::PsdRawFieldCodecMode::MuLaw;
        result.codecRate = 0.48f; result.bitDepth = 9.0f; result.codecDamage = 0.08f;
        result.modSource = s3g::PsdRawFieldModSource::Triangle;
        result.modTarget = s3g::PsdRawFieldModTarget::Clock;
        result.modRate = 0.12f; result.modRatio = 1.0f; result.modIndex = 0.24f; result.modFeedback = 0.04f;
        result.modAlgorithm = s3g::PsdRawFieldModAlgorithm::Broadcast;
        result.modSource2 = s3g::PsdRawFieldModSource::Sine; result.modTarget2 = s3g::PsdRawFieldModTarget::Body;
        result.modRate2 = 0.56f; result.modRatio2 = 0.5f; result.modIndex2 = 0.42f; result.modFeedback2 = 0.04f;
        result.modSource3 = s3g::PsdRawFieldModSource::Gate; result.modTarget3 = s3g::PsdRawFieldModTarget::Strike;
        result.modRate3 = 0.08f; result.modRatio3 = 1.0f; result.modIndex3 = 0.32f; result.modFeedback3 = 0.02f;
        result.drive = 0.58f; result.shred = 0.12f; result.resonance = 0.72f; result.gainDb = -12.5f;
        break;
    case 2u: // Scar Drum
        result.scanRate = 0.36f; result.texture = 0.46f; result.geometry = 0.42f; result.chaos = 0.38f;
        result.fold = 0.24f; result.evolve = 0.12f; result.codecMode = s3g::PsdRawFieldCodecMode::CelpScramble;
        result.codecRate = 0.48f; result.bitDepth = 6.0f; result.codecDamage = 0.24f;
        result.modSource = s3g::PsdRawFieldModSource::Sine;
        result.modTarget = s3g::PsdRawFieldModTarget::Fold;
        result.modRate = 0.58f; result.modRatio = 0.5f; result.modIndex = 0.20f; result.modFeedback = 0.08f;
        result.modAlgorithm = s3g::PsdRawFieldModAlgorithm::Regenerator;
        result.modSource2 = s3g::PsdRawFieldModSource::Feedback;
        result.modTarget2 = s3g::PsdRawFieldModTarget::Ring;
        result.modRate2 = 0.34f; result.modRatio2 = 1.0f; result.modIndex2 = 0.34f; result.modFeedback2 = 0.48f;
        result.modSource3 = s3g::PsdRawFieldModSource::Sync; result.modTarget3 = s3g::PsdRawFieldModTarget::Strike;
        result.modRate3 = 0.16f; result.modRatio3 = 0.5f; result.modIndex3 = 0.46f; result.modFeedback3 = 0.10f;
        result.drive = 0.62f; result.shred = 0.20f; result.resonance = 0.42f; result.gainDb = -14.0f;
        break;
    case 3u: // Fax Body
        result.scanRate = 0.30f; result.texture = 0.34f; result.geometry = 0.76f; result.chaos = 0.34f;
        result.fold = 0.16f; result.evolve = 0.10f; result.channelScheme = s3g::PsdRawFieldChannelScheme::Divergent;
        result.channelSpread = 0.96f; result.codecMode = s3g::PsdRawFieldCodecMode::HfFax;
        result.codecRate = 0.42f; result.bitDepth = 8.0f; result.codecDamage = 0.10f; result.carrierTune = -24.0f;
        result.modSource = s3g::PsdRawFieldModSource::Field;
        result.modTarget = s3g::PsdRawFieldModTarget::Body;
        result.modRate = 0.31f; result.modRatio = 1.0f; result.modIndex = 0.34f; result.modFeedback = 0.08f;
        result.modAlgorithm = s3g::PsdRawFieldModAlgorithm::CrossedMachines;
        result.modSource2 = s3g::PsdRawFieldModSource::Apt;
        result.modTarget2 = s3g::PsdRawFieldModTarget::Clock;
        result.modRate2 = 0.22f; result.modRatio2 = 0.5f; result.modIndex2 = 0.20f; result.modFeedback2 = 0.06f;
        result.modClockLock2 = 1u;
        result.modSource3 = s3g::PsdRawFieldModSource::Triangle; result.modTarget3 = s3g::PsdRawFieldModTarget::Ring;
        result.modRate3 = 0.30f; result.modRatio3 = 2.0f; result.modIndex3 = 0.32f; result.modFeedback3 = 0.08f;
        result.drive = 0.54f; result.shred = 0.10f; result.resonance = 0.78f; result.gainDb = -14.0f;
        break;
    case 4u: // Gated Breaks
        result.scanRate = 0.26f; result.texture = 0.42f; result.geometry = 0.84f; result.chaos = 0.40f;
        result.fold = 0.32f; result.evolve = 0.10f; result.channelScheme = s3g::PsdRawFieldChannelScheme::Shuffled;
        result.channelSpread = 0.86f; result.codecMode = s3g::PsdRawFieldCodecMode::ModemFsk;
        result.codecRate = 0.38f; result.bitDepth = 7.0f; result.codecDamage = 0.16f;
        result.modSource = s3g::PsdRawFieldModSource::Sine;
        result.modTarget = s3g::PsdRawFieldModTarget::Carrier;
        result.modRate = 0.60f; result.modRatio = 1.0f; result.modIndex = 0.22f; result.modFeedback = 0.08f;
        result.modAlgorithm = s3g::PsdRawFieldModAlgorithm::Relay;
        result.modSource2 = s3g::PsdRawFieldModSource::Gate;
        result.modTarget2 = s3g::PsdRawFieldModTarget::Strike;
        result.modRate2 = 0.14f; result.modRatio2 = 0.5f; result.modIndex2 = 0.52f; result.modFeedback2 = 0.06f;
        result.modSource3 = s3g::PsdRawFieldModSource::Morse; result.modTarget3 = s3g::PsdRawFieldModTarget::Clock;
        result.modRate3 = 0.20f; result.modRatio3 = 0.5f; result.modIndex3 = 0.18f; result.modFeedback3 = 0.08f;
        result.drive = 0.66f; result.shred = 0.20f; result.resonance = 0.32f; result.gainDb = -12.5f;
        break;
    case 5u: // Sync Metal
        result.scanRate = 0.48f; result.texture = 0.30f; result.geometry = 0.72f; result.chaos = 0.26f;
        result.fold = 0.30f; result.evolve = 0.04f; result.channelScheme = s3g::PsdRawFieldChannelScheme::Planes;
        result.channelSpread = 0.78f; result.codecMode = s3g::PsdRawFieldCodecMode::Hellschreiber;
        result.codecRate = 0.26f; result.bitDepth = 10.0f; result.codecDamage = 0.05f;
        result.modSource = s3g::PsdRawFieldModSource::Sync;
        result.modTarget = s3g::PsdRawFieldModTarget::Strike;
        result.modRate = 0.44f; result.modRatio = 2.0f; result.modIndex = 0.36f; result.modFeedback = 0.08f;
        result.modClockLock = 1u;
        result.modAlgorithm = s3g::PsdRawFieldModAlgorithm::Multiplex;
        result.modSource2 = s3g::PsdRawFieldModSource::Hellschreiber;
        result.modTarget2 = s3g::PsdRawFieldModTarget::Fold;
        result.modRate2 = 0.36f; result.modRatio2 = 1.5f; result.modIndex2 = 0.28f; result.modFeedback2 = 0.10f;
        result.modClockLock2 = 1u;
        result.modSource3 = s3g::PsdRawFieldModSource::Sync; result.modTarget3 = s3g::PsdRawFieldModTarget::Clock;
        result.modRate3 = 0.22f; result.modRatio3 = 0.75f; result.modIndex3 = 0.18f; result.modFeedback3 = 0.06f;
        result.modClockLock3 = 1u;
        result.drive = 0.62f; result.shred = 0.16f; result.resonance = 0.48f; result.gainDb = -13.5f;
        break;
    case 6u: // Baudot Drum
        result.scanRate = 0.34f; result.texture = 0.44f; result.geometry = 0.34f; result.chaos = 0.36f;
        result.fold = 0.22f; result.evolve = 0.18f; result.codecMode = s3g::PsdRawFieldCodecMode::MuLaw;
        result.codecRate = 0.48f; result.bitDepth = 8.0f; result.codecDamage = 0.14f;
        result.modSource = s3g::PsdRawFieldModSource::Morse;
        result.modTarget = s3g::PsdRawFieldModTarget::Off;
        result.modRate = 0.18f; result.modRatio = 0.5f; result.modIndex = 0.58f; result.modFeedback = 0.10f;
        result.modClockLock = 1u;
        result.modAlgorithm = s3g::PsdRawFieldModAlgorithm::Transcode;
        result.modSource2 = s3g::PsdRawFieldModSource::BaudotRtty;
        result.modTarget2 = s3g::PsdRawFieldModTarget::Off;
        result.modRate2 = 0.28f; result.modRatio2 = 1.0f; result.modIndex2 = 0.40f; result.modFeedback2 = 0.10f;
        result.modClockLock2 = 1u;
        result.modSource3 = s3g::PsdRawFieldModSource::HfFax; result.modTarget3 = s3g::PsdRawFieldModTarget::Strike;
        result.modRate3 = 0.24f; result.modRatio3 = 1.0f; result.modIndex3 = 0.38f; result.modFeedback3 = 0.08f;
        result.modClockLock3 = 1u;
        result.drive = 0.60f; result.shred = 0.14f; result.resonance = 0.34f; result.gainDb = -12.0f;
        break;
    case 7u: // Delta Knock
        result.scanRate = 0.28f; result.texture = 0.40f; result.geometry = 0.78f; result.chaos = 0.34f;
        result.fold = 0.34f; result.evolve = 0.08f; result.channelScheme = s3g::PsdRawFieldChannelScheme::Planes;
        result.channelSpread = 0.82f; result.codecMode = s3g::PsdRawFieldCodecMode::DeltaPcm;
        result.codecRate = 0.48f; result.bitDepth = 6.0f; result.codecDamage = 0.22f;
        result.modSource = s3g::PsdRawFieldModSource::Triangle;
        result.modTarget = s3g::PsdRawFieldModTarget::Fold;
        result.modRate = 0.22f; result.modRatio = 4.0f; result.modIndex = 0.34f; result.modFeedback = 0.10f;
        result.modAlgorithm = s3g::PsdRawFieldModAlgorithm::Broadcast;
        result.modSource2 = s3g::PsdRawFieldModSource::Gate; result.modTarget2 = s3g::PsdRawFieldModTarget::Strike;
        result.modRate2 = 0.14f; result.modRatio2 = 0.5f; result.modIndex2 = 0.46f; result.modFeedback2 = 0.06f;
        result.modSource3 = s3g::PsdRawFieldModSource::Sync; result.modTarget3 = s3g::PsdRawFieldModTarget::Scan;
        result.modRate3 = 0.30f; result.modRatio3 = 2.0f; result.modIndex3 = 0.18f; result.modFeedback3 = 0.04f;
        result.drive = 0.64f; result.shred = 0.18f; result.resonance = 0.28f; result.gainDb = -13.0f;
        break;
    case 8u: // ADPCM Sub
        result.scanRate = 0.32f; result.texture = 0.30f; result.geometry = 0.72f; result.chaos = 0.30f;
        result.fold = 0.14f; result.evolve = 0.12f; result.channelScheme = s3g::PsdRawFieldChannelScheme::Divergent;
        result.channelSpread = 0.94f; result.codecMode = s3g::PsdRawFieldCodecMode::Adpcm;
        result.codecRate = 0.40f; result.bitDepth = 7.0f; result.codecDamage = 0.18f;
        result.modSource = s3g::PsdRawFieldModSource::Sine;
        result.modTarget = s3g::PsdRawFieldModTarget::Body;
        result.modRate = 0.56f; result.modRatio = 1.0f; result.modIndex = 0.42f; result.modFeedback = 0.12f;
        result.modAlgorithm = s3g::PsdRawFieldModAlgorithm::Regenerator;
        result.modSource2 = s3g::PsdRawFieldModSource::Feedback;
        result.modTarget2 = s3g::PsdRawFieldModTarget::Ring;
        result.modRate2 = 0.40f; result.modRatio2 = 1.0f; result.modIndex2 = 0.36f; result.modFeedback2 = 0.52f;
        result.modSource3 = s3g::PsdRawFieldModSource::Gate; result.modTarget3 = s3g::PsdRawFieldModTarget::Strike;
        result.modRate3 = 0.12f; result.modRatio3 = 0.5f; result.modIndex3 = 0.18f; result.modFeedback3 = 0.08f;
        result.drive = 0.54f; result.shred = 0.10f; result.resonance = 0.82f; result.gainDb = -15.0f;
        break;
    case 9u: // Morse Body
        result.scanRate = 0.30f; result.texture = 0.32f; result.geometry = 0.58f; result.chaos = 0.28f;
        result.fold = 0.12f; result.evolve = 0.12f; result.channelScheme = s3g::PsdRawFieldChannelScheme::Planes;
        result.channelSpread = 0.95f; result.codecMode = s3g::PsdRawFieldCodecMode::CelpScramble;
        result.codecRate = 0.32f; result.bitDepth = 8.0f; result.codecDamage = 0.12f;
        result.modSource = s3g::PsdRawFieldModSource::Morse;
        result.modTarget = s3g::PsdRawFieldModTarget::Off;
        result.modRate = 0.18f; result.modRatio = 0.5f; result.modIndex = 0.64f; result.modFeedback = 0.10f;
        result.modClockLock = 1u;
        result.modAlgorithm = s3g::PsdRawFieldModAlgorithm::Transcode;
        result.modSource2 = s3g::PsdRawFieldModSource::HfFax;
        result.modTarget2 = s3g::PsdRawFieldModTarget::Off;
        result.modRate2 = 0.26f; result.modRatio2 = 1.0f; result.modIndex2 = 0.36f; result.modFeedback2 = 0.08f;
        result.modClockLock2 = 1u;
        result.modSource3 = s3g::PsdRawFieldModSource::Sstv; result.modTarget3 = s3g::PsdRawFieldModTarget::Body;
        result.modRate3 = 0.24f; result.modRatio3 = 1.0f; result.modIndex3 = 0.44f; result.modFeedback3 = 0.08f;
        result.modClockLock3 = 1u;
        result.drive = 0.52f; result.shred = 0.08f; result.resonance = 0.78f; result.gainDb = -15.0f;
        break;
    case 10u: // Spark Impact
        result.scanRate = 0.08f; result.texture = 0.40f; result.geometry = 0.70f; result.chaos = 0.22f;
        result.fold = 0.28f; result.evolve = 0.05f; result.channelScheme = s3g::PsdRawFieldChannelScheme::Parallel;
        result.channelSpread = 0.46f; result.codecMode = s3g::PsdRawFieldCodecMode::SparkCw;
        result.codecRate = 0.56f; result.bitDepth = 8.0f; result.codecDamage = 0.06f;
        result.modSource = s3g::PsdRawFieldModSource::Sine;
        result.modTarget = s3g::PsdRawFieldModTarget::Carrier;
        result.modRate = 0.62f; result.modRatio = 2.0f; result.modIndex = 0.20f; result.modFeedback = 0.10f;
        result.modAlgorithm = s3g::PsdRawFieldModAlgorithm::Relay;
        result.modSource2 = s3g::PsdRawFieldModSource::SparkCw;
        result.modTarget2 = s3g::PsdRawFieldModTarget::Strike;
        result.modRate2 = 0.12f; result.modRatio2 = 0.5f; result.modIndex2 = 0.54f; result.modFeedback2 = 0.18f;
        result.modSource3 = s3g::PsdRawFieldModSource::Gate; result.modTarget3 = s3g::PsdRawFieldModTarget::Fold;
        result.modRate3 = 0.10f; result.modRatio3 = 0.5f; result.modIndex3 = 0.30f; result.modFeedback3 = 0.06f;
        result.drive = 0.66f; result.shred = 0.14f; result.resonance = 0.38f; result.gainDb = -12.0f;
        break;
    case 11u: // Wide Fax Bass
        result.scanRate = 0.42f; result.texture = 0.34f; result.geometry = 0.48f; result.chaos = 0.32f;
        result.fold = 0.14f; result.evolve = 0.18f; result.channelScheme = s3g::PsdRawFieldChannelScheme::Shuffled;
        result.channelSpread = 1.0f; result.codecMode = s3g::PsdRawFieldCodecMode::HfFax;
        result.codecRate = 0.38f; result.bitDepth = 8.0f; result.codecDamage = 0.08f; result.carrierTune = -24.0f;
        result.modSource = s3g::PsdRawFieldModSource::HfFax;
        result.modTarget = s3g::PsdRawFieldModTarget::Body;
        result.modRate = 0.34f; result.modRatio = 2.0f; result.modIndex = 0.34f; result.modFeedback = 0.10f;
        result.modClockLock = 1u;
        result.modAlgorithm = s3g::PsdRawFieldModAlgorithm::Multiplex;
        result.modSource2 = s3g::PsdRawFieldModSource::Sstv;
        result.modTarget2 = s3g::PsdRawFieldModTarget::Ring;
        result.modRate2 = 0.30f; result.modRatio2 = 1.0f; result.modIndex2 = 0.38f; result.modFeedback2 = 0.10f;
        result.modClockLock2 = 1u;
        result.modSource3 = s3g::PsdRawFieldModSource::Sync; result.modTarget3 = s3g::PsdRawFieldModTarget::Clock;
        result.modRate3 = 0.20f; result.modRatio3 = 0.5f; result.modIndex3 = 0.14f; result.modFeedback3 = 0.05f;
        result.modClockLock3 = 1u;
        result.drive = 0.52f; result.shred = 0.08f; result.resonance = 0.76f; result.gainDb = -14.0f;
        break;
    case kWaveTracePreset: // Wave Trace
        result.scanRate = 0.50f; result.texture = 0.22f; result.geometry = 0.20f; result.chaos = 0.18f;
        result.fold = 0.08f; result.evolve = 0.0f; result.channelScheme = s3g::PsdRawFieldChannelScheme::Deinterleave;
        result.channelSpread = 0.92f; result.codecMode = s3g::PsdRawFieldCodecMode::RawPcm;
        result.codecRate = 0.0f; result.bitDepth = 12.0f; result.codecDamage = 0.0f;
        result.modSource = s3g::PsdRawFieldModSource::Field;
        result.modTarget = s3g::PsdRawFieldModTarget::Body;
        result.modRate = 0.30f; result.modRatio = 1.0f; result.modIndex = 0.14f; result.modFeedback = 0.04f;
        result.modAlgorithm = s3g::PsdRawFieldModAlgorithm::CrossedMachines;
        result.modSource2 = s3g::PsdRawFieldModSource::Sync;
        result.modTarget2 = s3g::PsdRawFieldModTarget::Clock;
        result.modRate2 = 0.18f; result.modRatio2 = 0.5f; result.modIndex2 = 0.12f; result.modFeedback2 = 0.03f;
        result.modSource3 = s3g::PsdRawFieldModSource::Gate; result.modTarget3 = s3g::PsdRawFieldModTarget::Strike;
        result.modRate3 = 0.20f; result.modRatio3 = 1.0f; result.modIndex3 = 0.16f; result.modFeedback3 = 0.03f;
        result.drive = 0.36f; result.shred = 0.06f; result.resonance = 0.24f; result.gainDb = -8.0f;
        break;
    case 0u:
    default:
        break;
    }
    if (preset != 0u) {
        result.modEnvelope1 = result.modAlgorithm == s3g::PsdRawFieldModAlgorithm::CrossedMachines
                || result.modAlgorithm == s3g::PsdRawFieldModAlgorithm::Transcode
            ? 1u : 0u;
        result.modEnvelope2 = 1u;
        result.modEnvelope3 = 1u;
    }
    auto bass = [&](s3g::PsdRawFieldBassReceiver receiver,
                    float body, float punch, float trace,
                    s3g::PsdRawFieldPitchTracking tracking,
                    float glide, s3g::PsdRawFieldBassOctave octave,
                    float lowWidth) {
        result.bassReceiver = receiver;
        result.bassBody = body;
        result.bassPunch = punch;
        result.bassTrace = trace;
        result.bassPitchTracking = tracking;
        result.bassGlide = glide;
        result.bassOctave = octave;
        result.bassLowWidth = lowWidth;
    };
    switch (preset) {
    case 1u: bass(s3g::PsdRawFieldBassReceiver::Divide, 0.82f, 0.18f, 0.32f,
        s3g::PsdRawFieldPitchTracking::BodyAndScan, 0.12f,
        s3g::PsdRawFieldBassOctave::MinusTwo, 0.05f); break;
    case 2u: bass(s3g::PsdRawFieldBassReceiver::Error, 0.70f, 0.86f, 0.45f,
        s3g::PsdRawFieldPitchTracking::Body, 0.05f,
        s3g::PsdRawFieldBassOctave::MinusOne, 0.12f); break;
    case 3u: bass(s3g::PsdRawFieldBassReceiver::Demod, 0.76f, 0.20f, 0.30f,
        s3g::PsdRawFieldPitchTracking::BodyAndScan, 0.18f,
        s3g::PsdRawFieldBassOctave::MinusTwo, 0.18f); break;
    case 4u: bass(s3g::PsdRawFieldBassReceiver::Direct, 0.64f, 0.74f, 0.52f,
        s3g::PsdRawFieldPitchTracking::BodyAndScan, 0.04f,
        s3g::PsdRawFieldBassOctave::MinusOne, 0.20f); break;
    case 5u: bass(s3g::PsdRawFieldBassReceiver::Divide, 0.56f, 0.62f, 0.58f,
        s3g::PsdRawFieldPitchTracking::Body, 0.08f,
        s3g::PsdRawFieldBassOctave::MinusOne, 0.30f); break;
    case 6u: bass(s3g::PsdRawFieldBassReceiver::Demod, 0.66f, 0.78f, 0.42f,
        s3g::PsdRawFieldPitchTracking::Body, 0.04f,
        s3g::PsdRawFieldBassOctave::MinusTwo, 0.10f); break;
    case 7u: bass(s3g::PsdRawFieldBassReceiver::Error, 0.68f, 0.82f, 0.46f,
        s3g::PsdRawFieldPitchTracking::Body, 0.06f,
        s3g::PsdRawFieldBassOctave::MinusOne, 0.18f); break;
    case 8u: bass(s3g::PsdRawFieldBassReceiver::Direct, 0.88f, 0.22f, 0.22f,
        s3g::PsdRawFieldPitchTracking::BodyAndScan, 0.28f,
        s3g::PsdRawFieldBassOctave::MinusTwo, 0.03f); break;
    case 9u: bass(s3g::PsdRawFieldBassReceiver::Demod, 0.76f, 0.48f, 0.38f,
        s3g::PsdRawFieldPitchTracking::Body, 0.12f,
        s3g::PsdRawFieldBassOctave::MinusTwo, 0.12f); break;
    case 10u: bass(s3g::PsdRawFieldBassReceiver::Error, 0.62f, 0.92f, 0.50f,
        s3g::PsdRawFieldPitchTracking::Body, 0.03f,
        s3g::PsdRawFieldBassOctave::MinusOne, 0.10f); break;
    case 11u: bass(s3g::PsdRawFieldBassReceiver::Divide, 0.82f, 0.24f, 0.35f,
        s3g::PsdRawFieldPitchTracking::BodyAndScan, 0.16f,
        s3g::PsdRawFieldBassOctave::MinusTwo, 0.18f); break;
    case kWaveTracePreset: bass(s3g::PsdRawFieldBassReceiver::Direct,
        0.42f, 0.15f, 0.72f, s3g::PsdRawFieldPitchTracking::BodyAndScan,
        0.10f, s3g::PsdRawFieldBassOctave::MinusOne, 0.45f); break;
    case 0u:
    default: break;
    }
    auto character = [&](float fuzz, float metal, float feedback) {
        result.bassFuzz = fuzz;
        result.bassMetal = metal;
        result.bassFeedback = feedback;
    };
    switch (preset) {
    case 1u: character(0.42f, 0.34f, 0.36f); break;
    case 2u: character(0.66f, 0.70f, 0.24f); break;
    case 3u: character(0.38f, 0.28f, 0.46f); break;
    case 4u: character(0.58f, 0.54f, 0.32f); break;
    case 5u: character(0.50f, 0.40f, 0.38f); break;
    case 6u: character(0.68f, 0.72f, 0.28f); break;
    case 7u: character(0.72f, 0.80f, 0.42f); break;
    case 8u: character(0.44f, 0.24f, 0.58f); break;
    case 9u: character(0.62f, 0.66f, 0.44f); break;
    case 10u: character(0.80f, 0.86f, 0.30f); break;
    case 11u: character(0.76f, 0.84f, 0.56f); break;
    case kWaveTracePreset: character(0.28f, 0.48f, 0.18f); break;
    case 0u:
    default: break;
    }
    result.fieldCodecMode = result.codecMode;
    if (preset != 0u) result.seed = hash32(0x50434431u ^ (preset * 0x9e3779b9u));
    return result;
}

void copyModulationCircuit(s3g::PsdRawFieldParams& target,
    const s3g::PsdRawFieldParams& source)
{
    target.modSource = source.modSource;
    target.modTarget = source.modTarget;
    target.modRate = source.modRate;
    target.modRatio = source.modRatio;
    target.modIndex = source.modIndex;
    target.modFeedback = source.modFeedback;
    target.modClockLock = source.modClockLock;
    target.modAlgorithm = source.modAlgorithm;
    target.modSource2 = source.modSource2;
    target.modTarget2 = source.modTarget2;
    target.modRate2 = source.modRate2;
    target.modRatio2 = source.modRatio2;
    target.modIndex2 = source.modIndex2;
    target.modFeedback2 = source.modFeedback2;
    target.modClockLock2 = source.modClockLock2;
    target.modSource3 = source.modSource3;
    target.modTarget3 = source.modTarget3;
    target.modRate3 = source.modRate3;
    target.modRatio3 = source.modRatio3;
    target.modIndex3 = source.modIndex3;
    target.modFeedback3 = source.modFeedback3;
    target.modClockLock3 = source.modClockLock3;
    target.modEnvelope1 = source.modEnvelope1;
    target.modEnvelope2 = source.modEnvelope2;
    target.modEnvelope3 = source.modEnvelope3;
}

bool paramsDiffer(const s3g::PsdRawFieldParams& a, const s3g::PsdRawFieldParams& b)
{
    return std::memcmp(&a, &b, sizeof(a)) != 0;
}

void saveUndo(Plugin& p)
{
    p.undoParams = p.params;
    p.undoSource = p.rawSource;
    p.undoSourcePath = p.sourcePath;
    p.undoSourceName = p.sourceName;
    p.undoSourceError = p.sourceError;
    p.undoSourceInterpretation = p.sourceInterpretation;
    p.undoPreset = p.selectedPreset;
    p.hasUndo = true;
}

void installRawSource(
    Plugin& p,
    std::shared_ptr<const s3g::PsdRawFieldSource> source,
    const std::string& path,
    SourceInterpretation interpretation,
    bool remember)
{
    if (!source) return;
    if (remember) saveUndo(p);
    p.rawSource = std::move(source);
    p.sourcePath = path;
    p.sourceName = sourceNameFromPath(path);
    p.sourceError.clear();
    p.sourceInterpretation = interpretation;
    if (p.rawSource->waveform && p.selectedPreset == 0u) {
        p.params = presetParams(kWaveTracePreset);
        p.selectedPreset = kWaveTracePreset;
    } else {
        p.selectedPreset = kCustomPreset;
    }
    p.field.transitionToSource(p.rawSource, p.params, 1.15f);
}

void useGeneratedField(Plugin& p, uint32_t salt, bool remember)
{
    if (remember) saveUndo(p);
    p.rawSource.reset();
    p.sourcePath.clear();
    p.sourceName.clear();
    p.sourceError.clear();
    p.sourceInterpretation = SourceInterpretation::Generated;
    p.params.fieldCodecMode = p.params.codecMode;
    p.params.seed = hash32(p.params.seed ^ salt ^ 0x6d2b79f5u);
    p.selectedPreset = kCustomPreset;
    p.field.transitionToSource({}, p.params, 1.15f);
}

void transitionPatch(Plugin& p, const s3g::PsdRawFieldParams& next, uint32_t preset, float seconds, bool remember)
{
    if (!paramsDiffer(p.params, next)) {
        p.selectedPreset = preset;
        return;
    }
    if (remember) saveUndo(p);
    p.params = next;
    p.selectedPreset = preset;
    p.field.transitionTo(p.params, seconds);
}

void applyPreset(Plugin& p, uint32_t preset)
{
    preset = std::min(preset, kCustomPreset);
    if (preset == kCustomPreset) { p.selectedPreset = kCustomPreset; return; }
    auto next = presetParams(preset);
    next.gainDb = p.params.gainDb;
    transitionPatch(p, next, preset, 0.90f, true);
}

void applyCuratedAlgorithm(Plugin& p, uint32_t algorithm)
{
    static constexpr uint32_t circuits[s3g::kPsdRawFieldModAlgorithmCount][2] = {
        { 1u, 7u },   // Broadcast
        { 4u, 10u },  // Relay
        { 5u, 11u },  // Multiplex
        { 3u, 12u },  // Crossed Machines
        { 2u, 8u },   // Regenerator
        { 6u, 9u }    // Transcode
    };
    algorithm = std::min<uint32_t>(algorithm, s3g::kPsdRawFieldModAlgorithmCount - 1u);
    const uint32_t variation = hash32(p.params.seed ^ (algorithm * 0x9e3779b9u)) & 1u;
    s3g::PsdRawFieldParams next = p.params;
    copyModulationCircuit(next, presetParams(circuits[algorithm][variation]));
    transitionPatch(p, next, kCustomPreset, 0.45f, true);
}

float curatedTargetIndexLimit(s3g::PsdRawFieldModTarget target)
{
    switch (target) {
    case s3g::PsdRawFieldModTarget::Body: return 0.70f;
    case s3g::PsdRawFieldModTarget::Ring: return 0.62f;
    case s3g::PsdRawFieldModTarget::Strike: return 0.70f;
    case s3g::PsdRawFieldModTarget::Fold: return 0.48f;
    case s3g::PsdRawFieldModTarget::Scan: return 0.28f;
    case s3g::PsdRawFieldModTarget::Clock: return 0.30f;
    case s3g::PsdRawFieldModTarget::Carrier: return 0.30f;
    case s3g::PsdRawFieldModTarget::Deviation: return 0.24f;
    case s3g::PsdRawFieldModTarget::Data:
    case s3g::PsdRawFieldModTarget::Damage: return 0.22f;
    case s3g::PsdRawFieldModTarget::Off:
    default: return 0.68f;
    }
}

void applyCuratedGuardrails(s3g::PsdRawFieldParams& params)
{
    params.scanRate = std::clamp(params.scanRate, 0.06f, 0.68f);
    params.texture = std::clamp(params.texture, 0.16f, 0.68f);
    params.geometry = std::clamp(params.geometry, 0.20f, 0.92f);
    params.chaos = std::clamp(params.chaos, 0.10f, 0.62f);
    params.fold = std::clamp(params.fold, 0.04f, 0.52f);
    params.evolve = std::clamp(params.evolve, 0.0f, 0.28f);
    params.codecRate = std::clamp(params.codecRate, 0.08f, 0.68f);
    params.bitDepth = std::clamp(params.bitDepth, 5.0f, 12.0f);
    params.codecDamage = std::clamp(params.codecDamage, 0.0f, 0.36f);
    params.drive = std::clamp(params.drive, 0.34f, 0.78f);
    params.shred = std::clamp(params.shred, 0.04f, 0.42f);
    params.resonance = std::clamp(params.resonance, 0.16f, 0.88f);
    params.bassBody = std::clamp(params.bassBody, 0.42f, 0.92f);
    params.bassPunch = std::clamp(params.bassPunch, 0.08f, 0.92f);
    params.bassTrace = std::clamp(params.bassTrace, 0.18f, 0.78f);
    params.bassGlide = std::clamp(params.bassGlide, 0.0f, 0.55f);
    params.bassLowWidth = std::clamp(params.bassLowWidth, 0.02f, 0.65f);
    params.bassFuzz = std::clamp(params.bassFuzz, 0.14f, 0.86f);
    params.bassMetal = std::clamp(params.bassMetal, 0.12f, 0.94f);
    params.bassFeedback = std::clamp(params.bassFeedback, 0.08f, 0.72f);
    if (params.bassPitchTracking == s3g::PsdRawFieldPitchTracking::Scan) {
        params.bassPitchTracking = s3g::PsdRawFieldPitchTracking::Body;
    }
    if (params.bassOctave == s3g::PsdRawFieldBassOctave::Unison) {
        params.bassOctave = s3g::PsdRawFieldBassOctave::MinusOne;
    }

    auto guardOperator = [](s3g::PsdRawFieldModSource source,
                             s3g::PsdRawFieldModTarget target,
                             float& index,
                             float& feedback) {
        index = std::clamp(index, 0.06f, curatedTargetIndexLimit(target));
        const float feedbackLimit = source == s3g::PsdRawFieldModSource::Feedback
            ? 0.55f : 0.48f;
        feedback = std::clamp(feedback, 0.0f, feedbackLimit);
    };
    guardOperator(params.modSource, params.modTarget, params.modIndex, params.modFeedback);
    guardOperator(params.modSource2, params.modTarget2, params.modIndex2, params.modFeedback2);
    guardOperator(params.modSource3, params.modTarget3, params.modIndex3, params.modFeedback3);
}

void randomizePatch(Plugin& p, uint32_t salt)
{
    auto random01 = [&salt]() {
        salt = hash32(salt + 0x9e3779b9u);
        return static_cast<float>(salt & 0xffffu) / 65535.0f;
    };
    auto signedRandom = [&random01]() { return random01() * 2.0f - 1.0f; };
    s3g::PsdRawFieldParams next = p.params;
    next.seed = hash32(next.seed ^ salt);
    const uint32_t circuitPreset = 1u + std::min<uint32_t>(11u,
        static_cast<uint32_t>(random01() * 12.0f));
    const s3g::PsdRawFieldParams recipe = presetParams(circuitPreset);
    copyModulationCircuit(next, recipe);
    next.scanRate = std::clamp(recipe.scanRate + signedRandom() * 0.08f, 0.0f, 1.0f);
    next.texture = std::clamp(recipe.texture + signedRandom() * 0.10f, 0.0f, 1.0f);
    next.geometry = std::clamp(recipe.geometry + signedRandom() * 0.10f, 0.0f, 1.0f);
    next.chaos = std::clamp(recipe.chaos + signedRandom() * 0.10f, 0.0f, 1.0f);
    next.fold = std::clamp(recipe.fold + signedRandom() * 0.08f, 0.0f, 1.0f);
    next.evolve = std::clamp(recipe.evolve + signedRandom() * 0.06f, 0.0f, 1.0f);
    next.channelScheme = static_cast<s3g::PsdRawFieldChannelScheme>(
        1u + static_cast<uint32_t>(random01() * 3.999f));
    next.channelSpread = std::clamp(recipe.channelSpread + signedRandom() * 0.10f, 0.45f, 1.0f);
    next.codecMode = recipe.codecMode;
    next.codecRate = std::clamp(recipe.codecRate + signedRandom() * 0.08f, 0.0f, 1.0f);
    next.bitDepth = std::clamp(std::round(recipe.bitDepth + signedRandom() * 2.0f), 2.0f, 16.0f);
    next.codecDamage = std::clamp(recipe.codecDamage + signedRandom() * 0.08f, 0.0f, 1.0f);
    next.carrierTune = std::clamp(recipe.carrierTune + signedRandom() * 3.0f, -24.0f, 12.0f);
    next.bassReceiver = recipe.bassReceiver;
    next.bassBody = std::clamp(recipe.bassBody + signedRandom() * 0.08f, 0.0f, 1.0f);
    next.bassPunch = std::clamp(recipe.bassPunch + signedRandom() * 0.10f, 0.0f, 1.0f);
    next.bassTrace = std::clamp(recipe.bassTrace + signedRandom() * 0.09f, 0.0f, 1.0f);
    next.bassPitchTracking = recipe.bassPitchTracking;
    next.bassGlide = std::clamp(recipe.bassGlide + signedRandom() * 0.06f, 0.0f, 1.0f);
    next.bassOctave = recipe.bassOctave;
    next.bassLowWidth = std::clamp(recipe.bassLowWidth + signedRandom() * 0.08f, 0.0f, 1.0f);
    next.bassFuzz = std::clamp(recipe.bassFuzz + signedRandom() * 0.10f,
        0.0f, 1.0f);
    next.bassMetal = std::clamp(recipe.bassMetal + signedRandom() * 0.10f,
        0.0f, 1.0f);
    next.bassFeedback = std::clamp(
        recipe.bassFeedback + signedRandom() * 0.08f, 0.0f, 0.78f);
    if (random01() < 0.14f) {
        next.bassReceiver = static_cast<s3g::PsdRawFieldBassReceiver>(
            static_cast<uint32_t>(random01() * 3.999f));
    }
    if (!p.rawSource) next.fieldCodecMode = next.codecMode;
    auto varyRate = [&random01](float value) {
        return std::clamp(value + (random01() * 2.0f - 1.0f) * 0.055f, 0.0f, 1.0f);
    };
    auto varyRatio = [&random01](float value) {
        return std::clamp(value * std::pow(2.0f, (random01() * 2.0f - 1.0f) * 0.22f),
            0.125f, 16.0f);
    };
    auto varyIndex = [&random01](float value) {
        return std::clamp(value + (random01() * 2.0f - 1.0f) * 0.06f, 0.06f, 0.78f);
    };
    next.modRate = varyRate(next.modRate);
    next.modRate2 = varyRate(next.modRate2);
    next.modRate3 = varyRate(next.modRate3);
    next.modRatio = varyRatio(next.modRatio);
    next.modRatio2 = varyRatio(next.modRatio2);
    next.modRatio3 = varyRatio(next.modRatio3);
    next.modIndex = varyIndex(next.modIndex);
    next.modIndex2 = varyIndex(next.modIndex2);
    next.modIndex3 = varyIndex(next.modIndex3);
    next.modFeedback = std::clamp(next.modFeedback + signedRandom() * 0.05f, 0.0f, 0.72f);
    next.modFeedback2 = std::clamp(next.modFeedback2 + signedRandom() * 0.05f, 0.0f, 0.72f);
    next.modFeedback3 = std::clamp(next.modFeedback3 + signedRandom() * 0.05f, 0.0f, 0.72f);
    next.drive = std::clamp(recipe.drive + signedRandom() * 0.08f, 0.0f, 1.0f);
    next.shred = std::clamp(recipe.shred + signedRandom() * 0.07f, 0.0f, 1.0f);
    next.resonance = std::clamp(recipe.resonance + signedRandom() * 0.08f, 0.0f, 1.0f);
    applyCuratedGuardrails(next);
    transitionPatch(p, next, kCustomPreset, 0.90f, true);
}

void mutatePatch(Plugin& p, uint32_t salt)
{
    auto random01 = [&salt]() {
        salt = hash32(salt + 0x85ebca6bu);
        return static_cast<float>(salt & 0xffffu) / 65535.0f;
    };
    auto signedRandom = [&random01]() { return random01() * 2.0f - 1.0f; };
    s3g::PsdRawFieldParams next = p.params;
    next.scanRate = std::clamp(next.scanRate + signedRandom() * 0.07f, 0.0f, 1.0f);
    next.texture = std::clamp(next.texture + signedRandom() * 0.10f, 0.0f, 1.0f);
    next.geometry = std::clamp(next.geometry + signedRandom() * 0.09f, 0.0f, 1.0f);
    next.chaos = std::clamp(next.chaos + signedRandom() * 0.11f, 0.0f, 1.0f);
    next.fold = std::clamp(next.fold + signedRandom() * 0.09f, 0.0f, 1.0f);
    next.evolve = std::clamp(next.evolve + signedRandom() * 0.06f, 0.0f, 1.0f);
    next.channelSpread = std::clamp(next.channelSpread + signedRandom() * 0.07f, 0.0f, 1.0f);
    next.codecRate = std::clamp(next.codecRate + signedRandom() * 0.09f, 0.0f, 1.0f);
    next.codecDamage = std::clamp(next.codecDamage + signedRandom() * 0.10f, 0.0f, 1.0f);
    next.carrierTune = std::clamp(next.carrierTune + signedRandom() * 2.0f, -24.0f, 24.0f);
    next.bassBody = std::clamp(next.bassBody + signedRandom() * 0.055f, 0.0f, 1.0f);
    next.bassPunch = std::clamp(next.bassPunch + signedRandom() * 0.075f, 0.0f, 1.0f);
    next.bassTrace = std::clamp(next.bassTrace + signedRandom() * 0.06f, 0.0f, 1.0f);
    next.bassGlide = std::clamp(next.bassGlide + signedRandom() * 0.045f, 0.0f, 1.0f);
    next.bassLowWidth = std::clamp(next.bassLowWidth + signedRandom() * 0.055f, 0.0f, 1.0f);
    next.bassFuzz = std::clamp(
        next.bassFuzz + signedRandom() * 0.075f, 0.0f, 1.0f);
    next.bassMetal = std::clamp(
        next.bassMetal + signedRandom() * 0.075f, 0.0f, 1.0f);
    next.bassFeedback = std::clamp(
        next.bassFeedback + signedRandom() * 0.060f, 0.0f, 0.85f);
    if (random01() < 0.06f) {
        next.bassReceiver = static_cast<s3g::PsdRawFieldBassReceiver>(
            static_cast<uint32_t>(random01() * 3.999f));
    }
    if (random01() < 0.05f) {
        next.bassOctave = random01() < 0.58f
            ? s3g::PsdRawFieldBassOctave::MinusTwo
            : s3g::PsdRawFieldBassOctave::MinusOne;
    }
    if (random01() < 0.08f) {
        const uint32_t circuitPreset = 1u + std::min<uint32_t>(11u,
            static_cast<uint32_t>(random01() * 12.0f));
        copyModulationCircuit(next, presetParams(circuitPreset));
    } else {
        next.modRate = std::clamp(next.modRate + signedRandom() * 0.055f, 0.0f, 1.0f);
        next.modRatio = std::clamp(next.modRatio * std::pow(2.0f, signedRandom() * 0.25f), 0.125f, 16.0f);
        next.modIndex = std::clamp(next.modIndex + signedRandom() * 0.07f, 0.06f, 0.84f);
        next.modFeedback = std::clamp(next.modFeedback + signedRandom() * 0.055f, 0.0f, 0.78f);
        next.modRate2 = std::clamp(next.modRate2 + signedRandom() * 0.055f, 0.0f, 1.0f);
        next.modRatio2 = std::clamp(next.modRatio2 * std::pow(2.0f, signedRandom() * 0.25f), 0.125f, 16.0f);
        next.modIndex2 = std::clamp(next.modIndex2 + signedRandom() * 0.07f, 0.06f, 0.84f);
        next.modFeedback2 = std::clamp(next.modFeedback2 + signedRandom() * 0.055f, 0.0f, 0.78f);
        next.modRate3 = std::clamp(next.modRate3 + signedRandom() * 0.055f, 0.0f, 1.0f);
        next.modRatio3 = std::clamp(next.modRatio3 * std::pow(2.0f, signedRandom() * 0.25f), 0.125f, 16.0f);
        next.modIndex3 = std::clamp(next.modIndex3 + signedRandom() * 0.07f, 0.06f, 0.84f);
        next.modFeedback3 = std::clamp(next.modFeedback3 + signedRandom() * 0.055f, 0.0f, 0.78f);
    }
    next.drive = std::clamp(next.drive + signedRandom() * 0.07f, 0.0f, 1.0f);
    next.shred = std::clamp(next.shred + signedRandom() * 0.08f, 0.0f, 1.0f);
    next.resonance = std::clamp(next.resonance + signedRandom() * 0.06f, 0.0f, 1.0f);
    if (random01() < 0.28f) next.bitDepth = std::clamp(next.bitDepth + (random01() < 0.5f ? -1.0f : 1.0f), 2.0f, 16.0f);
    if (random01() < 0.12f) {
        const int mode = std::clamp(
            static_cast<int>(next.codecMode) + (random01() < 0.5f ? -1 : 1),
            0,
            static_cast<int>(kCodecModeMax));
        next.codecMode = static_cast<s3g::PsdRawFieldCodecMode>(mode);
    }
    if (random01() < 0.10f) {
        next.channelScheme = static_cast<s3g::PsdRawFieldChannelScheme>(static_cast<uint32_t>(random01() * 4.999f));
    }
    applyCuratedGuardrails(next);
    if (!p.rawSource) next.fieldCodecMode = next.codecMode;
    transitionPatch(p, next, kCustomPreset, 0.70f, true);
}

void undoPatch(Plugin& p)
{
    if (!p.hasUndo) return;
    const auto restore = p.undoParams;
    const auto restoreSource = p.undoSource;
    const std::string restoreSourcePath = p.undoSourcePath;
    const std::string restoreSourceName = p.undoSourceName;
    const std::string restoreSourceError = p.undoSourceError;
    const SourceInterpretation restoreSourceInterpretation = p.undoSourceInterpretation;
    const uint32_t restorePreset = p.undoPreset;
    p.hasUndo = false;
    p.params = restore;
    p.rawSource = restoreSource;
    p.sourcePath = restoreSourcePath;
    p.sourceName = restoreSourceName;
    p.sourceError = restoreSourceError;
    p.sourceInterpretation = restoreSourceInterpretation;
    p.selectedPreset = restorePreset;
    p.field.transitionToSource(p.rawSource, p.params, 0.70f);
}

void applyParam(Plugin& p, clap_id id, double value)
{
    if (id == kOutputFormatParamId) {
        const double finiteValue = std::isfinite(value) ? value : 0.0;
        p.outputFormat = static_cast<OutputFormat>(static_cast<uint32_t>(
            std::clamp(std::round(finiteValue), 0.0, 2.0)));
        return;
    }
    if (id == kOutputRotationParamId) {
        p.outputRotationDeg = std::isfinite(value)
            ? static_cast<float>(std::clamp(value, -180.0, 180.0))
            : 0.0f;
        return;
    }
    if (id == kMidiReceiveParamId) {
        const double next = static_cast<double>(
            s3g::drum_midi::receiveChannel(value));
        if (next != p.midiReceive) {
            p.midiReceive = next;
            resetMidiPerformance(p);
        }
        return;
    }
    if (id == kRunParamId) {
        p.playing.store(value >= 0.5, std::memory_order_relaxed);
        return;
    }
    if (id == kPerformanceModeParamId) {
        const auto next = static_cast<PerformanceMode>(static_cast<uint32_t>(
            std::clamp(std::round(value), 0.0, 1.0)));
        if (next != p.performanceMode) {
            p.performanceMode = next;
            resetMidiPerformance(p);
        }
        return;
    }
    if (id == kAttackParamId) {
        p.attackMs = static_cast<float>(std::clamp(value, 1.0, 5000.0));
        return;
    }
    if (id == kDecayParamId) {
        p.decayMs = static_cast<float>(std::clamp(value, 5.0, 8000.0));
        return;
    }
    if (id == kSustainParamId) {
        p.sustain = static_cast<float>(std::clamp(value, 0.0, 1.0));
        return;
    }
    if (id == kReleaseParamId) {
        p.releaseMs = static_cast<float>(std::clamp(value, 5.0, 12000.0));
        return;
    }
    const s3g::PsdRawFieldParams before = p.params;
    bool changed = true;
    bool structural = false;
    float transitionSeconds = 0.45f;
    switch (id) {
    case kScanRateParamId: p.params.scanRate = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kTextureParamId: p.params.texture = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kGeometryParamId: p.params.geometry = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kChaosParamId: p.params.chaos = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kFoldParamId: p.params.fold = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kEvolveParamId: p.params.evolve = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kChannelSchemeParamId:
        p.params.channelScheme = static_cast<s3g::PsdRawFieldChannelScheme>(static_cast<uint32_t>(std::clamp(std::round(value), 0.0, 4.0)));
        structural = true;
        break;
    case kChannelSpreadParamId: p.params.channelSpread = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kCodecModeParamId:
        p.params.codecMode = static_cast<s3g::PsdRawFieldCodecMode>(static_cast<uint32_t>(
            std::clamp(std::round(value), 0.0, static_cast<double>(kCodecModeMax))));
        structural = true;
        break;
    case kCodecRateParamId: p.params.codecRate = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kBitDepthParamId:
        p.params.bitDepth = static_cast<float>(std::clamp(std::round(value), 2.0, 16.0));
        structural = true;
        break;
    case kCodecDamageParamId: p.params.codecDamage = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kCarrierTuneParamId: p.params.carrierTune = static_cast<float>(std::clamp(value, -24.0, 24.0)); break;
    case kModSourceParamId:
        p.params.modSource = static_cast<s3g::PsdRawFieldModSource>(static_cast<uint32_t>(
            std::clamp(std::round(value), 0.0, static_cast<double>(s3g::kPsdRawFieldModSourceCount - 1u))));
        structural = true;
        break;
    case kModTargetParamId:
        p.params.modTarget = static_cast<s3g::PsdRawFieldModTarget>(static_cast<uint32_t>(
            std::clamp(std::round(value), 0.0, static_cast<double>(s3g::kPsdRawFieldModTargetCount - 1u))));
        break;
    case kModRateParamId: p.params.modRate = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kModRatioParamId: p.params.modRatio = static_cast<float>(std::clamp(value, 0.125, 16.0)); break;
    case kModIndexParamId: p.params.modIndex = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kModFeedbackParamId: p.params.modFeedback = static_cast<float>(std::clamp(value, 0.0, 0.98)); break;
    case kModClockLockParamId: p.params.modClockLock = value >= 0.5 ? 1u : 0u; break;
    case kModAlgorithmParamId:
        p.params.modAlgorithm = static_cast<s3g::PsdRawFieldModAlgorithm>(static_cast<uint32_t>(
            std::clamp(std::round(value), 0.0,
                static_cast<double>(s3g::kPsdRawFieldModAlgorithmCount - 1u))));
        structural = true;
        break;
    case kModSource2ParamId:
        p.params.modSource2 = static_cast<s3g::PsdRawFieldModSource>(static_cast<uint32_t>(
            std::clamp(std::round(value), 0.0,
                static_cast<double>(s3g::kPsdRawFieldModSourceCount - 1u))));
        structural = true;
        break;
    case kModRate2ParamId: p.params.modRate2 = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kModRatio2ParamId: p.params.modRatio2 = static_cast<float>(std::clamp(value, 0.125, 16.0)); break;
    case kModIndex2ParamId: p.params.modIndex2 = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kModFeedback2ParamId: p.params.modFeedback2 = static_cast<float>(std::clamp(value, 0.0, 0.98)); break;
    case kModClockLock2ParamId: p.params.modClockLock2 = value >= 0.5 ? 1u : 0u; break;
    case kModTarget2ParamId:
        p.params.modTarget2 = static_cast<s3g::PsdRawFieldModTarget>(static_cast<uint32_t>(
            std::clamp(std::round(value), 0.0, static_cast<double>(s3g::kPsdRawFieldModTargetCount - 1u))));
        break;
    case kModSource3ParamId:
        p.params.modSource3 = static_cast<s3g::PsdRawFieldModSource>(static_cast<uint32_t>(
            std::clamp(std::round(value), 0.0, static_cast<double>(s3g::kPsdRawFieldModSourceCount - 1u))));
        structural = true;
        break;
    case kModTarget3ParamId:
        p.params.modTarget3 = static_cast<s3g::PsdRawFieldModTarget>(static_cast<uint32_t>(
            std::clamp(std::round(value), 0.0, static_cast<double>(s3g::kPsdRawFieldModTargetCount - 1u))));
        break;
    case kModRate3ParamId: p.params.modRate3 = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kModRatio3ParamId: p.params.modRatio3 = static_cast<float>(std::clamp(value, 0.125, 16.0)); break;
    case kModIndex3ParamId: p.params.modIndex3 = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kModFeedback3ParamId: p.params.modFeedback3 = static_cast<float>(std::clamp(value, 0.0, 0.98)); break;
    case kModClockLock3ParamId: p.params.modClockLock3 = value >= 0.5 ? 1u : 0u; break;
    case kModEnvelope1ParamId: p.params.modEnvelope1 = value >= 0.5 ? 1u : 0u; break;
    case kModEnvelope2ParamId: p.params.modEnvelope2 = value >= 0.5 ? 1u : 0u; break;
    case kModEnvelope3ParamId: p.params.modEnvelope3 = value >= 0.5 ? 1u : 0u; break;
    case kModulationEnabledParamId:
        p.params.modulationEnabled = value >= 0.5 ? 1u : 0u;
        structural = true;
        transitionSeconds = 0.08f;
        break;
    case kBassReceiverParamId:
        p.params.bassReceiver = static_cast<s3g::PsdRawFieldBassReceiver>(static_cast<uint32_t>(
            std::clamp(std::round(value), 0.0,
                static_cast<double>(s3g::kPsdRawFieldBassReceiverCount - 1u))));
        break;
    case kBassBodyParamId: p.params.bassBody = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kBassPunchParamId: p.params.bassPunch = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kBassTraceParamId: p.params.bassTrace = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kBassPitchTrackingParamId:
        p.params.bassPitchTracking = static_cast<s3g::PsdRawFieldPitchTracking>(static_cast<uint32_t>(
            std::clamp(std::round(value), 0.0,
                static_cast<double>(s3g::kPsdRawFieldPitchTrackingCount - 1u))));
        break;
    case kBassGlideParamId: p.params.bassGlide = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kBassOctaveParamId:
        p.params.bassOctave = static_cast<s3g::PsdRawFieldBassOctave>(static_cast<uint32_t>(
            std::clamp(std::round(value), 0.0,
                static_cast<double>(s3g::kPsdRawFieldBassOctaveCount - 1u))));
        break;
    case kBassLowWidthParamId: p.params.bassLowWidth = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kBassFuzzParamId: p.params.bassFuzz = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kBassMetalParamId: p.params.bassMetal = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kBassFeedbackParamId: p.params.bassFeedback = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kDriveParamId: p.params.drive = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kShredParamId: p.params.shred = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kResonanceParamId: p.params.resonance = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kGainParamId: p.params.gainDb = static_cast<float>(std::clamp(value, -60.0, 6.0)); break;
    case kPresetParamId:
        applyPreset(p, static_cast<uint32_t>(std::clamp(std::round(value), 0.0, static_cast<double>(kCustomPreset))));
        return;
    case kRandomizePatchParamId:
        if (value > 0.0) randomizePatch(p, hash32(p.params.seed ^ static_cast<uint32_t>(std::round(value * 4294967295.0))));
        return;
    case kMutateParamId:
        if (value > 0.0) mutatePatch(p, hash32(p.params.seed ^ static_cast<uint32_t>(std::round(value * 4294967295.0)) ^ 0x27d4eb2du));
        return;
    case kUndoParamId:
        if (value > 0.0) undoPatch(p);
        return;
    case kSeedParamId: {
        const uint32_t nextSeed = static_cast<uint32_t>(std::clamp(value, 1.0, 4294967295.0));
        if (nextSeed == p.params.seed) return;
        saveUndo(p);
        p.params.seed = nextSeed;
        p.selectedPreset = kCustomPreset;
        p.field.transitionTo(p.params, 1.15f);
        return;
    }
    case kRandomizeFieldParamId:
        if (value <= 0.0) return;
        useGeneratedField(p, static_cast<uint32_t>(std::round(value * 4294967295.0)), true);
        return;
    default:
        changed = false;
        break;
    }
    if (changed && paramsDiffer(before, p.params)) {
        p.selectedPreset = kCustomPreset;
        if (structural) p.field.transitionTo(p.params, transitionSeconds);
        else p.field.setParams(p.params);
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

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t maxFrames)
{
    auto* p = self(plugin);
    p->sampleRate = sampleRate;
    p->maxFrames = std::max<uint32_t>(1u, maxFrames);
    p->output32.assign(kOutputChannels, std::vector<float>(p->maxFrames, 0.0f));
    p->outputPtrs.assign(kOutputChannels, nullptr);
    p->modulationEnvelope.assign(p->maxFrames, 1.0f);
    p->renderGain.assign(p->maxFrames, 1.0f);
    for (uint32_t ch = 0; ch < kOutputChannels; ++ch) p->outputPtrs[ch] = p->output32[ch].data();
    p->field.setParams(p->params);
    p->field.setSource(p->rawSource);
    p->field.prepare(sampleRate);
    resetMidiPerformance(*p);
    p->runGain = p->playing.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    resetMidiPerformance(*p);
    p->field.reset();
    p->runGain = p->playing.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
}

void readParamEvents(Plugin& p, const clap_input_events_t* in)
{
    if (!in) return;
    const uint32_t count = in->size(in);
    for (uint32_t i = 0; i < count; ++i) {
        const clap_event_header_t* event = in->get(in, i);
        if (event && event->space_id == CLAP_CORE_EVENT_SPACE_ID && event->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param = reinterpret_cast<const clap_event_param_value_t*>(event);
            applyParam(p, param->param_id, param->value);
        }
    }
}

void handleMidiMessage(Plugin& p, const clap_event_midi_t& midi)
{
    const uint8_t status = midi.data[0] & 0xf0u;
    const uint8_t channel = midi.data[0] & 0x0fu;
    if (!s3g::drum_midi::accepts(p.midiReceive, channel)) return;
    const int32_t key = static_cast<int32_t>(midi.data[1] & 0x7fu);
    const uint8_t value = midi.data[2] & 0x7fu;
    if (status == 0x90u && value > 0u) {
        midiNoteOn(p, key, static_cast<float>(value) * (1.0f / 127.0f));
    } else if (status == 0x80u || (status == 0x90u && value == 0u)) {
        midiNoteOff(p, key, false);
    } else if (status == 0xb0u && (midi.data[1] == 120u || midi.data[1] == 123u)) {
        midiAllNotesOff(p, midi.data[1] == 120u);
    }
}

void handleCoreEvent(Plugin& p, const clap_event_header_t* event)
{
    if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
    switch (event->type) {
    case CLAP_EVENT_PARAM_VALUE: {
        const auto* param = reinterpret_cast<const clap_event_param_value_t*>(event);
        applyParam(p, param->param_id, param->value);
        break;
    }
    case CLAP_EVENT_NOTE_ON: {
        const auto* note = reinterpret_cast<const clap_event_note_t*>(event);
        if (!s3g::drum_midi::accepts(p.midiReceive, note->channel)) break;
        if (note->velocity > 0.0) midiNoteOn(p, note->key, static_cast<float>(note->velocity));
        else midiNoteOff(p, note->key, false);
        break;
    }
    case CLAP_EVENT_NOTE_OFF: {
        const auto* note = reinterpret_cast<const clap_event_note_t*>(event);
        if (!s3g::drum_midi::accepts(p.midiReceive, note->channel)) break;
        midiNoteOff(p, note->key, false);
        break;
    }
    case CLAP_EVENT_NOTE_CHOKE:
    case CLAP_EVENT_NOTE_END: {
        const auto* note = reinterpret_cast<const clap_event_note_t*>(event);
        if (!s3g::drum_midi::accepts(p.midiReceive, note->channel)) break;
        midiNoteOff(p, note->key, true);
        break;
    }
    case CLAP_EVENT_MIDI:
        handleMidiMessage(p, *reinterpret_cast<const clap_event_midi_t*>(event));
        break;
    default:
        break;
    }
}

void renderSegment(Plugin& p, uint32_t offset, uint32_t frames)
{
    if (frames == 0u) return;
    for (uint32_t ch = 0u; ch < kOutputChannels; ++ch) {
        p.outputPtrs[ch] = p.output32[ch].data() + offset;
    }

    const bool playing = p.playing.load(std::memory_order_relaxed);
    const bool midiMode = p.performanceMode == PerformanceMode::Midi;
    const bool voiceActive = !midiMode || p.envelopeGate || p.envelopeStage != EnvelopeStage::Idle;
    const bool transportActive = playing || p.runGain > 0.0f;
    const float runStep = 1.0f / static_cast<float>(std::max(1.0, p.sampleRate * 0.020));
    const float attack = envelopeCoefficient(p.attackMs, p.sampleRate);
    const float decay = envelopeCoefficient(p.decayMs, p.sampleRate);
    const float release = envelopeCoefficient(p.releaseMs, p.sampleRate);
    const float velocityGain = 0.15f + 0.85f * std::sqrt(std::clamp(p.activeVelocity, 0.0f, 1.0f));
    for (uint32_t i = 0u; i < frames; ++i) {
        float runGate = 0.0f;
        if (playing) {
            p.runGain = std::min(1.0f, p.runGain + runStep);
            runGate = p.runGain;
        } else if (p.runGain > 0.0f) {
            runGate = p.runGain;
            p.runGain = std::max(0.0f, p.runGain - runStep);
        }
        const float envelope = midiMode ? processMidiEnvelope(p, attack, decay, release) : 1.0f;
        p.modulationEnvelope[offset + i] = std::clamp(envelope, 0.0f, 1.0f);
        p.renderGain[offset + i] = runGate * (midiMode ? envelope * velocityGain : 1.0f);
    }

    if (voiceActive && transportActive) {
        p.field.process(p.outputPtrs.data(), kOutputChannels, frames,
            midiMode ? p.modulationEnvelope.data() + offset : nullptr);
        for (uint32_t i = 0u; i < frames; ++i) {
            const float gain = p.renderGain[offset + i];
            for (uint32_t ch = 0u; ch < kOutputChannels; ++ch) {
                p.output32[ch][offset + i] *= gain;
            }
        }
    } else {
        for (uint32_t ch = 0u; ch < kOutputChannels; ++ch) {
            std::fill(p.output32[ch].begin() + offset, p.output32[ch].begin() + offset + frames, 0.0f);
        }
    }

    if (p.outputFormat != OutputFormat::Direct8) {
        s3g::McStereoParams foldParams {};
        foldParams.inputChannels = kOutputChannels;
        foldParams.rotationDegrees = p.outputRotationDeg;
        foldParams.layout = s3g::McStereoLayout::RingProjection;
        foldParams.autogain = s3g::McStereoAutogain::PowerSqrtN;
        foldParams.outputGainDb = 0.0f;
        for (uint32_t i = 0u; i < frames; ++i) {
            std::array<float, kOutputChannels> direct {};
            for (uint32_t ch = 0u; ch < kOutputChannels; ++ch) {
                direct[ch] = p.output32[ch][offset + i];
                p.output32[ch][offset + i] = 0.0f;
            }
            if (p.outputFormat == OutputFormat::QuadRing) {
                std::array<float, 4> quad {};
                s3g::processMcToQuadFrame(
                    direct.data(), kOutputChannels, quad.data(), foldParams);
                for (uint32_t ch = 0u; ch < quad.size(); ++ch) {
                    p.output32[ch][offset + i] = quad[ch];
                }
            } else {
                std::array<float, 2> stereo {};
                s3g::processMcToStereoFrame(
                    direct.data(), kOutputChannels, stereo.data(), foldParams);
                p.output32[0][offset + i] = stereo[0];
                p.output32[1][offset + i] = stereo[1];
            }
        }
    }
    p.displayEnvelope.store(midiMode ? p.envelopeValue : 1.0f, std::memory_order_relaxed);
    p.displayEnvelopeStage.store(static_cast<uint32_t>(
        midiMode ? p.envelopeStage : EnvelopeStage::Idle), std::memory_order_relaxed);
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* proc)
{
    auto* p = self(plugin);
    if (!proc) return CLAP_PROCESS_ERROR;
    if (proc->audio_outputs_count == 0u) {
        if (proc->in_events) {
            const uint32_t count = proc->in_events->size(proc->in_events);
            for (uint32_t i = 0u; i < count; ++i) handleCoreEvent(*p, proc->in_events->get(proc->in_events, i));
        }
        return CLAP_PROCESS_CONTINUE;
    }
    const auto& output = proc->audio_outputs[0];
    const uint32_t frames = std::min(proc->frames_count, p->maxFrames);
    if (frames == 0u || output.channel_count < kOutputChannels) return CLAP_PROCESS_CONTINUE;

    uint32_t rendered = 0u;
    if (proc->in_events) {
        const uint32_t count = proc->in_events->size(proc->in_events);
        for (uint32_t i = 0u; i < count; ++i) {
            const clap_event_header_t* event = proc->in_events->get(proc->in_events, i);
            if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
            const uint32_t eventFrame = std::max(rendered, std::min(event->time, frames));
            renderSegment(*p, rendered, eventFrame - rendered);
            handleCoreEvent(*p, event);
            rendered = eventFrame;
        }
    }
    renderSegment(*p, rendered, frames - rendered);

    float blockPeak = 0.0f;
    uint32_t waveWrite = p->waveWrite.load(std::memory_order_relaxed);
    for (uint32_t ch = 0; ch < kOutputChannels; ++ch) {
        for (uint32_t i = 0; i < frames; ++i) {
            const float value = p->output32[ch][i];
            if (output.data32 && output.data32[ch]) output.data32[ch][i] = value;
            if (output.data64 && output.data64[ch]) output.data64[ch][i] = static_cast<double>(value);
            blockPeak = std::max(blockPeak, std::abs(value));
        }
    }
    const uint32_t historyStep = std::max<uint32_t>(1u, frames / 64u);
    for (uint32_t i = 0; i < frames; i += historyStep) {
        const uint32_t index = waveWrite++ & (kWaveHistory - 1u);
        for (uint32_t ch = 0; ch < kOutputChannels; ++ch) p->waveHistory[ch][index] = p->output32[ch][i];
    }
    p->waveWrite.store(waveWrite, std::memory_order_relaxed);
    for (uint32_t ch = kOutputChannels; ch < output.channel_count; ++ch) {
        if (output.data32 && output.data32[ch]) std::fill(output.data32[ch], output.data32[ch] + frames, 0.0f);
        if (output.data64 && output.data64[ch]) std::fill(output.data64[ch], output.data64[ch] + frames, 0.0);
    }
    p->outputPeak.store(std::max(p->outputPeak.load(std::memory_order_relaxed) * 0.9f, blockPeak), std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 0u : 1u; }
bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (isInput || index != 0u || !info) return false;
    info->id = 20;
    std::snprintf(info->name, sizeof(info->name), "8ch Fault");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kOutputChannels;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

uint32_t notePortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 1u : 0u; }
bool notePortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_note_port_info_t* info)
{
    if (!isInput || index != 0u || !info) return false;
    info->id = 30u;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::strncpy(info->name, "MIDI In", sizeof(info->name));
    info->name[sizeof(info->name) - 1u] = '\0';
    return true;
}
const clap_plugin_note_ports_t notePorts { notePortsCount, notePortsGet };

struct ParamDef { clap_id id; const char* name; double min; double max; double def; };
constexpr ParamDef kParamDefs[] {
    { kScanRateParamId, "Scan Rate", 0.0, 1.0, 0.44 },
    { kTextureParamId, "Data Texture", 0.0, 1.0, 0.62 },
    { kGeometryParamId, "Geometry", 0.0, 1.0, 0.64 },
    { kChaosParamId, "Chaos", 0.0, 1.0, 0.58 },
    { kFoldParamId, "Fold", 0.0, 1.0, 0.68 },
    { kEvolveParamId, "Field Evolve", 0.0, 1.0, 0.0 },
    { kChannelSchemeParamId, "Channel Routing", 0.0, 4.0, 1.0 },
    { kChannelSpreadParamId, "Channel Spread", 0.0, 1.0, 0.72 },
    { kCodecModeParamId, "Codec", 0.0, static_cast<double>(kCodecModeMax), 3.0 },
    { kCodecRateParamId, "Codec Rate", 0.0, 1.0, 0.35 },
    { kBitDepthParamId, "Bit Depth", 2.0, 16.0, 8.0 },
    { kCodecDamageParamId, "Codec Damage", 0.0, 1.0, 0.28 },
    { kCarrierTuneParamId, "Carrier Tune", -24.0, 24.0, 0.0 },
    { kModSourceParamId, "M1 Source", 0.0, static_cast<double>(s3g::kPsdRawFieldModSourceCount - 1u), 0.0 },
    { kModTargetParamId, "M1 Destination", 0.0, static_cast<double>(s3g::kPsdRawFieldModTargetCount - 1u), 0.0 },
    { kModRateParamId, "M1 Rate", 0.0, 1.0, 0.35 },
    { kModRatioParamId, "M1 Ratio", 0.125, 16.0, 1.0 },
    { kModIndexParamId, "M1 Index", 0.0, 1.0, 0.0 },
    { kModFeedbackParamId, "M1 Feedback", 0.0, 0.98, 0.0 },
    { kModClockLockParamId, "M1 Clock", 0.0, 1.0, 0.0 },
    { kModAlgorithmParamId, "Modulation Algorithm", 0.0, static_cast<double>(s3g::kPsdRawFieldModAlgorithmCount - 1u), 0.0 },
    { kModSource2ParamId, "M2 Source", 0.0, static_cast<double>(s3g::kPsdRawFieldModSourceCount - 1u), 0.0 },
    { kModRate2ParamId, "M2 Rate", 0.0, 1.0, 0.35 },
    { kModRatio2ParamId, "M2 Ratio", 0.125, 16.0, 1.0 },
    { kModIndex2ParamId, "M2 Index", 0.0, 1.0, 0.0 },
    { kModFeedback2ParamId, "M2 Feedback", 0.0, 0.98, 0.0 },
    { kModClockLock2ParamId, "M2 Clock", 0.0, 1.0, 0.0 },
    { kModTarget2ParamId, "M2 Destination", 0.0, static_cast<double>(s3g::kPsdRawFieldModTargetCount - 1u), 5.0 },
    { kModSource3ParamId, "M3 Source", 0.0, static_cast<double>(s3g::kPsdRawFieldModSourceCount - 1u), 0.0 },
    { kModTarget3ParamId, "M3 Destination", 0.0, static_cast<double>(s3g::kPsdRawFieldModTargetCount - 1u), 5.0 },
    { kModRate3ParamId, "M3 Rate", 0.0, 1.0, 0.35 },
    { kModRatio3ParamId, "M3 Ratio", 0.125, 16.0, 1.0 },
    { kModIndex3ParamId, "M3 Index", 0.0, 1.0, 0.0 },
    { kModFeedback3ParamId, "M3 Feedback", 0.0, 0.98, 0.0 },
    { kModClockLock3ParamId, "M3 Clock", 0.0, 1.0, 0.0 },
    { kModEnvelope1ParamId, "M1 Envelope", 0.0, 1.0, 0.0 },
    { kModEnvelope2ParamId, "M2 Envelope", 0.0, 1.0, 0.0 },
    { kModEnvelope3ParamId, "M3 Envelope", 0.0, 1.0, 0.0 },
    { kModulationEnabledParamId, "Modulation Enabled", 0.0, 1.0, 1.0 },
    { kDriveParamId, "Drive", 0.0, 1.0, 0.68 },
    { kShredParamId, "Shred", 0.0, 1.0, 0.58 },
    { kResonanceParamId, "Resonance", 0.0, 1.0, 0.18 },
    { kGainParamId, "Output Gain", -60.0, 6.0, -12.0 },
    { kRunParamId, "Run", 0.0, 1.0, 1.0 },
    { kPresetParamId, "Preset", 0.0, 13.0, 0.0 },
    { kRandomizePatchParamId, "Randomize Patch", 0.0, 1.0, 0.0 },
    { kMutateParamId, "Mutate Patch", 0.0, 1.0, 0.0 },
    { kUndoParamId, "Undo Patch", 0.0, 1.0, 0.0 },
    { kSeedParamId, "Field Seed", 1.0, 4294967295.0, 1346589745.0 },
    { kRandomizeFieldParamId, "Randomize Field", 0.0, 1.0, 0.0 },
    { kPerformanceModeParamId, "Performance Mode", 0.0, 1.0, 0.0 },
    { kMidiReceiveParamId, "MIDI Receive", 0.0, 16.0, 0.0 },
    { kOutputFormatParamId, "Output Format", 0.0, 2.0, 0.0 },
    { kOutputRotationParamId, "Output Rotation", -180.0, 180.0, 0.0 },
    { kAttackParamId, "Attack", 1.0, 5000.0, 12.0 },
    { kDecayParamId, "Decay", 5.0, 8000.0, 280.0 },
    { kSustainParamId, "Sustain", 0.0, 1.0, 0.72 },
    { kReleaseParamId, "Release", 5.0, 12000.0, 850.0 },
    { kBassReceiverParamId, "Receiver", 0.0,
        static_cast<double>(s3g::kPsdRawFieldBassReceiverCount - 1u), 0.0 },
    { kBassBodyParamId, "Body", 0.0, 1.0, 0.0 },
    { kBassPunchParamId, "Excite", 0.0, 1.0, 0.0 },
    { kBassTraceParamId, "Trace", 0.0, 1.0, kInitBassTrace },
    { kBassPitchTrackingParamId, "Pitch Tracking", 0.0,
        static_cast<double>(s3g::kPsdRawFieldPitchTrackingCount - 1u), 0.0 },
    { kBassGlideParamId, "Glide", 0.0, 1.0, 0.0 },
    { kBassOctaveParamId, "Octave", 0.0,
        static_cast<double>(s3g::kPsdRawFieldBassOctaveCount - 1u), 1.0 },
    { kBassLowWidthParamId, "Low Width", 0.0, 1.0, 1.0 },
    { kBassFuzzParamId, "Fuzz", 0.0, 1.0, 0.0 },
    { kBassMetalParamId, "Metal", 0.0, 1.0, 0.55 },
    { kBassFeedbackParamId, "Feedback", 0.0, 1.0, 0.0 },
};

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(std::size(kParamDefs)); }
bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    const auto& def = kParamDefs[index];
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    if (def.id == kCodecModeParamId || def.id == kChannelSchemeParamId || def.id == kBitDepthParamId
        || def.id == kPresetParamId || def.id == kRandomizeFieldParamId || def.id == kRandomizePatchParamId
        || def.id == kMutateParamId || def.id == kUndoParamId || def.id == kSeedParamId
        || def.id == kRunParamId || def.id == kPerformanceModeParamId
        || def.id == kModSourceParamId || def.id == kModTargetParamId
        || def.id == kModClockLockParamId || def.id == kModAlgorithmParamId
        || def.id == kModSource2ParamId || def.id == kModClockLock2ParamId
        || def.id == kModTarget2ParamId || def.id == kModSource3ParamId
        || def.id == kModTarget3ParamId || def.id == kModClockLock3ParamId
        || def.id == kModEnvelope1ParamId || def.id == kModEnvelope2ParamId
        || def.id == kModEnvelope3ParamId || def.id == kModulationEnabledParamId
        || def.id == kBassReceiverParamId || def.id == kBassPitchTrackingParamId
        || def.id == kBassOctaveParamId || def.id == kMidiReceiveParamId
        || def.id == kOutputFormatParamId) {
        info->flags |= CLAP_PARAM_IS_STEPPED;
    }
    std::strncpy(info->name, def.name, sizeof(info->name));
    info->name[sizeof(info->name) - 1u] = '\0';
    const bool performance = (def.id >= kPerformanceModeParamId
        && def.id <= kReleaseParamId) || def.id == kMidiReceiveParamId;
    const bool bassCore = def.id >= kBassReceiverParamId
        && def.id <= kBassFeedbackParamId;
    const bool output = def.id == kOutputFormatParamId
        || def.id == kOutputRotationParamId;
    const char* module = performance ? "Performance"
        : (output ? "Output" : (bassCore ? "Bass Core" : "Processor Fault"));
    std::strncpy(info->module, module, sizeof(info->module));
    info->module[sizeof(info->module) - 1u] = '\0';
    info->min_value = def.min;
    info->max_value = def.max;
    info->default_value = def.def;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    const auto* instance = self(plugin);
    const auto& p = instance->params;
    switch (id) {
    case kScanRateParamId: *value = p.scanRate; return true;
    case kTextureParamId: *value = p.texture; return true;
    case kGeometryParamId: *value = p.geometry; return true;
    case kChaosParamId: *value = p.chaos; return true;
    case kFoldParamId: *value = p.fold; return true;
    case kEvolveParamId: *value = p.evolve; return true;
    case kChannelSchemeParamId: *value = static_cast<uint32_t>(p.channelScheme); return true;
    case kChannelSpreadParamId: *value = p.channelSpread; return true;
    case kCodecModeParamId: *value = static_cast<uint32_t>(p.codecMode); return true;
    case kCodecRateParamId: *value = p.codecRate; return true;
    case kBitDepthParamId: *value = p.bitDepth; return true;
    case kCodecDamageParamId: *value = p.codecDamage; return true;
    case kCarrierTuneParamId: *value = p.carrierTune; return true;
    case kModSourceParamId: *value = static_cast<uint32_t>(p.modSource); return true;
    case kModTargetParamId: *value = static_cast<uint32_t>(p.modTarget); return true;
    case kModRateParamId: *value = p.modRate; return true;
    case kModRatioParamId: *value = p.modRatio; return true;
    case kModIndexParamId: *value = p.modIndex; return true;
    case kModFeedbackParamId: *value = p.modFeedback; return true;
    case kModClockLockParamId: *value = p.modClockLock; return true;
    case kModAlgorithmParamId: *value = static_cast<uint32_t>(p.modAlgorithm); return true;
    case kModSource2ParamId: *value = static_cast<uint32_t>(p.modSource2); return true;
    case kModRate2ParamId: *value = p.modRate2; return true;
    case kModRatio2ParamId: *value = p.modRatio2; return true;
    case kModIndex2ParamId: *value = p.modIndex2; return true;
    case kModFeedback2ParamId: *value = p.modFeedback2; return true;
    case kModClockLock2ParamId: *value = p.modClockLock2; return true;
    case kModTarget2ParamId: *value = static_cast<uint32_t>(p.modTarget2); return true;
    case kModSource3ParamId: *value = static_cast<uint32_t>(p.modSource3); return true;
    case kModTarget3ParamId: *value = static_cast<uint32_t>(p.modTarget3); return true;
    case kModRate3ParamId: *value = p.modRate3; return true;
    case kModRatio3ParamId: *value = p.modRatio3; return true;
    case kModIndex3ParamId: *value = p.modIndex3; return true;
    case kModFeedback3ParamId: *value = p.modFeedback3; return true;
    case kModClockLock3ParamId: *value = p.modClockLock3; return true;
    case kModEnvelope1ParamId: *value = p.modEnvelope1; return true;
    case kModEnvelope2ParamId: *value = p.modEnvelope2; return true;
    case kModEnvelope3ParamId: *value = p.modEnvelope3; return true;
    case kModulationEnabledParamId: *value = p.modulationEnabled; return true;
    case kBassReceiverParamId: *value = static_cast<uint32_t>(p.bassReceiver); return true;
    case kBassBodyParamId: *value = p.bassBody; return true;
    case kBassPunchParamId: *value = p.bassPunch; return true;
    case kBassTraceParamId: *value = p.bassTrace; return true;
    case kBassPitchTrackingParamId: *value = static_cast<uint32_t>(p.bassPitchTracking); return true;
    case kBassGlideParamId: *value = p.bassGlide; return true;
    case kBassOctaveParamId: *value = static_cast<uint32_t>(p.bassOctave); return true;
    case kBassLowWidthParamId: *value = p.bassLowWidth; return true;
    case kBassFuzzParamId: *value = p.bassFuzz; return true;
    case kBassMetalParamId: *value = p.bassMetal; return true;
    case kBassFeedbackParamId: *value = p.bassFeedback; return true;
    case kDriveParamId: *value = p.drive; return true;
    case kShredParamId: *value = p.shred; return true;
    case kResonanceParamId: *value = p.resonance; return true;
    case kGainParamId: *value = p.gainDb; return true;
    case kRunParamId: *value = instance->playing.load(std::memory_order_relaxed) ? 1.0 : 0.0; return true;
    case kPerformanceModeParamId: *value = static_cast<uint32_t>(instance->performanceMode); return true;
    case kMidiReceiveParamId: *value = instance->midiReceive; return true;
    case kOutputFormatParamId: *value = static_cast<uint32_t>(instance->outputFormat); return true;
    case kOutputRotationParamId: *value = instance->outputRotationDeg; return true;
    case kAttackParamId: *value = instance->attackMs; return true;
    case kDecayParamId: *value = instance->decayMs; return true;
    case kSustainParamId: *value = instance->sustain; return true;
    case kReleaseParamId: *value = instance->releaseMs; return true;
    case kPresetParamId: *value = instance->selectedPreset; return true;
    case kRandomizePatchParamId:
    case kMutateParamId:
    case kUndoParamId:
    case kRandomizeFieldParamId: *value = 0.0; return true;
    case kSeedParamId: *value = p.seed; return true;
    default: return false;
    }
}

const char* codecModeName(uint32_t mode)
{
    switch (mode) {
    case 1u: return "DELTA";
    case 2u: return "ADPCM";
    case 3u: return "MU-LAW";
    case 4u: return "A-LAW";
    case 5u: return "CELP";
    case 6u: return "DISC";
    case 7u: return "CVSD";
    case 8u: return "SUBBAND";
    case 9u: return "LPC";
    case 10u: return "TRANSFORM";
    case 11u: return "PREDICT";
    case 12u: return "MODEM";
    case 13u: return "FAX";
    case 14u: return "SIGMA 1-BIT";
    case 15u: return "HYBRID";
    case 16u: return "APT";
    case 17u: return "HF FAX";
    case 18u: return "HELL";
    case 19u: return "MORSE";
    case 20u: return "SPARK CW";
    case 21u: return "BAUDOT RTTY";
    case 22u: return "SSTV";
    case 0u:
    default: return "PCM";
    }
}

const char* modSourceName(uint32_t source)
{
    switch (static_cast<s3g::PsdRawFieldModSource>(source)) {
    case s3g::PsdRawFieldModSource::Sine: return "SINE";
    case s3g::PsdRawFieldModSource::Triangle: return "TRIANGLE";
    case s3g::PsdRawFieldModSource::Noise: return "NOISE";
    case s3g::PsdRawFieldModSource::Field: return "FIELD";
    case s3g::PsdRawFieldModSource::Sync: return "SYNC";
    case s3g::PsdRawFieldModSource::Gate: return "GATE";
    case s3g::PsdRawFieldModSource::Apt: return "APT";
    case s3g::PsdRawFieldModSource::HfFax: return "HF FAX";
    case s3g::PsdRawFieldModSource::Hellschreiber: return "HELL";
    case s3g::PsdRawFieldModSource::Morse: return "MORSE";
    case s3g::PsdRawFieldModSource::SparkCw: return "SPARK CW";
    case s3g::PsdRawFieldModSource::BaudotRtty: return "BAUDOT RTTY";
    case s3g::PsdRawFieldModSource::Sstv: return "SSTV";
    case s3g::PsdRawFieldModSource::Feedback: return "FEEDBACK";
    case s3g::PsdRawFieldModSource::Off:
    default: return "OFF";
    }
}

const char* modTargetName(uint32_t target)
{
    switch (static_cast<s3g::PsdRawFieldModTarget>(target)) {
    case s3g::PsdRawFieldModTarget::Deviation: return "DEVIATION";
    case s3g::PsdRawFieldModTarget::Clock: return "CLOCK";
    case s3g::PsdRawFieldModTarget::Data: return "DATA";
    case s3g::PsdRawFieldModTarget::Damage: return "DAMAGE";
    case s3g::PsdRawFieldModTarget::Body: return "BODY";
    case s3g::PsdRawFieldModTarget::Ring: return "RING";
    case s3g::PsdRawFieldModTarget::Strike: return "STRIKE";
    case s3g::PsdRawFieldModTarget::Fold: return "FOLD";
    case s3g::PsdRawFieldModTarget::Scan: return "SCAN";
    case s3g::PsdRawFieldModTarget::Off: return "OFF";
    case s3g::PsdRawFieldModTarget::Carrier:
    default: return "CARRIER";
    }
}

const char* modAlgorithmName(uint32_t algorithm)
{
    switch (static_cast<s3g::PsdRawFieldModAlgorithm>(algorithm)) {
    case s3g::PsdRawFieldModAlgorithm::Relay: return "RELAY";
    case s3g::PsdRawFieldModAlgorithm::Multiplex: return "MULTIPLEX";
    case s3g::PsdRawFieldModAlgorithm::CrossedMachines: return "CROSSED MACHINES";
    case s3g::PsdRawFieldModAlgorithm::Regenerator: return "REGENERATOR";
    case s3g::PsdRawFieldModAlgorithm::Transcode: return "TRANSCODE";
    case s3g::PsdRawFieldModAlgorithm::Broadcast:
    default: return "BROADCAST";
    }
}

const char* modAlgorithmDiagram(uint32_t algorithm)
{
    switch (static_cast<s3g::PsdRawFieldModAlgorithm>(algorithm)) {
    case s3g::PsdRawFieldModAlgorithm::Relay: return "M3  →  M2  →  M1  →  DESTS";
    case s3g::PsdRawFieldModAlgorithm::Multiplex: return "M1 / M2 / M3 TIME-SLOTS  →  DESTS";
    case s3g::PsdRawFieldModAlgorithm::CrossedMachines: return "M1  ↻  M2  ↻  M3  →  DESTS";
    case s3g::PsdRawFieldModAlgorithm::Regenerator: return "CODEC  ↺  M3  →  M2  →  M1";
    case s3g::PsdRawFieldModAlgorithm::Transcode: return "DATA  →  M1  →  M2  →  M3  →  CODEC";
    case s3g::PsdRawFieldModAlgorithm::Broadcast:
    default: return "M1  |  M2  |  M3  →  DESTS";
    }
}

const char* modTargetShortName(s3g::PsdRawFieldModTarget target)
{
    switch (target) {
    case s3g::PsdRawFieldModTarget::Carrier: return "CARR";
    case s3g::PsdRawFieldModTarget::Deviation: return "DEV";
    case s3g::PsdRawFieldModTarget::Clock: return "CLK";
    case s3g::PsdRawFieldModTarget::Data: return "DATA";
    case s3g::PsdRawFieldModTarget::Damage: return "DMG";
    case s3g::PsdRawFieldModTarget::Body: return "BODY";
    case s3g::PsdRawFieldModTarget::Ring: return "RING";
    case s3g::PsdRawFieldModTarget::Strike: return "HIT";
    case s3g::PsdRawFieldModTarget::Fold: return "FOLD";
    case s3g::PsdRawFieldModTarget::Scan: return "SCAN";
    case s3g::PsdRawFieldModTarget::Off:
    default: return "OFF";
    }
}

const char* modClockName(uint32_t lock) { return lock != 0u ? "LOCK" : "FREE"; }
const char* modEnvelopeName(uint32_t follow) { return follow != 0u ? "ADSR" : "FIXED"; }

const char* bassReceiverName(uint32_t receiver)
{
    switch (static_cast<s3g::PsdRawFieldBassReceiver>(receiver)) {
    case s3g::PsdRawFieldBassReceiver::Demod: return "DEMOD";
    case s3g::PsdRawFieldBassReceiver::Divide: return "DIVIDE";
    case s3g::PsdRawFieldBassReceiver::Error: return "ERROR";
    case s3g::PsdRawFieldBassReceiver::Direct:
    default: return "DIRECT";
    }
}

const char* bassPitchTrackingName(uint32_t tracking)
{
    switch (static_cast<s3g::PsdRawFieldPitchTracking>(tracking)) {
    case s3g::PsdRawFieldPitchTracking::Body: return "BODY";
    case s3g::PsdRawFieldPitchTracking::BodyAndScan: return "BODY + SCAN";
    case s3g::PsdRawFieldPitchTracking::Scan:
    default: return "SCAN";
    }
}

const char* bassOctaveName(uint32_t octave)
{
    switch (static_cast<s3g::PsdRawFieldBassOctave>(octave)) {
    case s3g::PsdRawFieldBassOctave::MinusTwo: return "-2 OCT";
    case s3g::PsdRawFieldBassOctave::Unison: return "UNISON";
    case s3g::PsdRawFieldBassOctave::MinusOne:
    default: return "-1 OCT";
    }
}

const char* performanceModeName(uint32_t mode) { return mode == 1u ? "MIDI" : "FREE"; }

const char* channelSchemeName(uint32_t mode)
{
    switch (mode) {
    case 1u: return "DEINTERLEAVE";
    case 2u: return "PLANES";
    case 3u: return "SHUFFLED";
    case 4u: return "DIVERGENT";
    case 0u:
    default: return "PARALLEL";
    }
}

const char* presetName(uint32_t preset)
{
    switch (preset) {
    case 1u: return "SUB CLOCK";
    case 2u: return "SCAR DRUM";
    case 3u: return "FAX BODY";
    case 4u: return "GATED BREAKS";
    case 5u: return "SYNC METAL";
    case 6u: return "BAUDOT DRUM";
    case 7u: return "DELTA KNOCK";
    case 8u: return "ADPCM SUB";
    case 9u: return "MORSE BODY";
    case 10u: return "SPARK IMPACT";
    case 11u: return "WIDE FAX BASS";
    case kWaveTracePreset: return "WAVE TRACE";
    case kCustomPreset: return "CUSTOM";
    case 0u:
    default: return "INIT";
    }
}

double scanBytesPerSecond(double normalized, double sampleRate)
{
    return 0.00002 * std::pow(375000.0, std::clamp(normalized, 0.0, 1.0)) * sampleRate;
}

double codecUpdatesPerSecond(double normalized, double sampleRate)
{
    return sampleRate / std::pow(2.0, std::clamp(normalized, 0.0, 1.0) * 14.0);
}

double evolveEventsPerSecond(double normalized)
{
    normalized = std::clamp(normalized, 0.0, 1.0);
    return normalized <= 0.0001 ? 0.0 : 1.0 / (12.0 + (0.35 - 12.0) * normalized * normalized);
}

double modulatorRateHz(double normalized)
{
    return 0.05 * std::pow(160000.0, std::clamp(normalized, 0.0, 1.0));
}

std::string rateText(double value, const char* unit)
{
    char text[32] {};
    if (value >= 1000.0) std::snprintf(text, sizeof(text), "%.1f k%s", value / 1000.0, unit);
    else if (value >= 100.0) std::snprintf(text, sizeof(text), "%.0f %s", value, unit);
    else if (value >= 10.0) std::snprintf(text, sizeof(text), "%.1f %s", value, unit);
    else std::snprintf(text, sizeof(text), "%.2f %s", value, unit);
    return text;
}

std::string waveformSpeedText(double normalized)
{
    const double speed = std::pow(2.0, (std::clamp(normalized, 0.0, 1.0) - 0.5) * 16.0);
    char text[32] {};
    if (speed < 0.1) std::snprintf(text, sizeof(text), "%.3fx", speed);
    else if (speed < 10.0) std::snprintf(text, sizeof(text), "%.2fx", speed);
    else std::snprintf(text, sizeof(text), "%.1fx", speed);
    return text;
}

std::string evolveText(double normalized)
{
    const double rate = evolveEventsPerSecond(normalized);
    if (rate <= 0.0) return "OFF";
    char text[32] {};
    if (rate < 1.0) std::snprintf(text, sizeof(text), "%.4f Hz", rate);
    else std::snprintf(text, sizeof(text), "%.2f Hz", rate);
    return text;
}

std::string envelopeTimeText(double milliseconds)
{
    char text[32] {};
    if (milliseconds >= 1000.0) std::snprintf(text, sizeof(text), "%.4f s", milliseconds * 0.001);
    else std::snprintf(text, sizeof(text), "%.3f ms", milliseconds);
    return text;
}

double codecCarrierNominal(uint32_t mode)
{
    switch (static_cast<s3g::PsdRawFieldCodecMode>(mode)) {
    case s3g::PsdRawFieldCodecMode::ModemFsk: return 1805.0;
    case s3g::PsdRawFieldCodecMode::FaxQam: return 1700.0;
    case s3g::PsdRawFieldCodecMode::Apt: return 2400.0;
    case s3g::PsdRawFieldCodecMode::HfFax: return 1900.0;
    case s3g::PsdRawFieldCodecMode::Hellschreiber: return 900.0;
    case s3g::PsdRawFieldCodecMode::Morse:
    case s3g::PsdRawFieldCodecMode::SparkCw: return 700.0;
    case s3g::PsdRawFieldCodecMode::BaudotRtty: return 1700.0;
    case s3g::PsdRawFieldCodecMode::Sstv: return 1900.0;
    default: return 0.0;
    }
}

std::string carrierTuneText(double semitones, uint32_t mode)
{
    char text[32] {};
    const double nominal = codecCarrierNominal(mode);
    if (nominal <= 0.0) std::snprintf(text, sizeof(text), "%+.1f st", semitones);
    else std::snprintf(text, sizeof(text), "%.0f Hz", nominal * std::pow(2.0, semitones / 12.0));
    return text;
}

bool paramsValueToText(const clap_plugin_t* plugin, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
    const double sampleRate = plugin ? self(plugin)->sampleRate : 48000.0;
    if (id == kScanRateParamId) {
        const auto* instance = plugin ? self(plugin) : nullptr;
        const bool waveform = instance && instance->rawSource && instance->rawSource->waveform;
        std::snprintf(display, size, "%s", waveform
            ? waveformSpeedText(value).c_str()
            : rateText(scanBytesPerSecond(value, sampleRate), "B/s").c_str());
    }
    else if (id == kCodecRateParamId) std::snprintf(display, size, "%s", rateText(codecUpdatesPerSecond(value, sampleRate), "Hz").c_str());
    else if (id == kCarrierTuneParamId) {
        const uint32_t mode = plugin
            ? static_cast<uint32_t>(self(plugin)->params.codecMode)
            : static_cast<uint32_t>(s3g::PsdRawFieldCodecMode::Apt);
        std::snprintf(display, size, "%s", carrierTuneText(value, mode).c_str());
    }
    else if (id == kModSourceParamId || id == kModSource2ParamId || id == kModSource3ParamId) std::snprintf(display, size, "%s", modSourceName(static_cast<uint32_t>(
        std::clamp(std::round(value), 0.0, static_cast<double>(s3g::kPsdRawFieldModSourceCount - 1u)))));
    else if (id == kModAlgorithmParamId) std::snprintf(display, size, "%s", modAlgorithmName(static_cast<uint32_t>(
        std::clamp(std::round(value), 0.0, static_cast<double>(s3g::kPsdRawFieldModAlgorithmCount - 1u)))));
    else if (id == kModTargetParamId || id == kModTarget2ParamId || id == kModTarget3ParamId) std::snprintf(display, size, "%s", modTargetName(static_cast<uint32_t>(
        std::clamp(std::round(value), 0.0, static_cast<double>(s3g::kPsdRawFieldModTargetCount - 1u)))));
    else if (id == kModClockLockParamId || id == kModClockLock2ParamId || id == kModClockLock3ParamId) std::snprintf(display, size, "%s", modClockName(
        static_cast<uint32_t>(std::clamp(std::round(value), 0.0, 1.0))));
    else if (id == kModEnvelope1ParamId || id == kModEnvelope2ParamId || id == kModEnvelope3ParamId) std::snprintf(display, size, "%s", modEnvelopeName(
        static_cast<uint32_t>(std::clamp(std::round(value), 0.0, 1.0))));
    else if (id == kModRateParamId || id == kModRate2ParamId || id == kModRate3ParamId) std::snprintf(display, size, "%s", rateText(modulatorRateHz(value), "Hz").c_str());
    else if (id == kModRatioParamId || id == kModRatio2ParamId || id == kModRatio3ParamId) std::snprintf(display, size, "%.3fx", value);
    else if (id == kModIndexParamId || id == kModIndex2ParamId || id == kModIndex3ParamId) std::snprintf(display, size, "%.1f%%", value * 100.0);
    else if (id == kModFeedbackParamId || id == kModFeedback2ParamId || id == kModFeedback3ParamId) std::snprintf(display, size, "%.1f%%", value * (100.0 / 0.98));
    else if (id == kEvolveParamId) std::snprintf(display, size, "%s", evolveText(value).c_str());
    else if (id == kGainParamId) std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kRunParamId) std::snprintf(display, size, "%s", value >= 0.5 ? "PLAY" : "STOP");
    else if (id == kModulationEnabledParamId) std::snprintf(display, size, "%s", value >= 0.5 ? "MOD ON" : "MOD OFF");
    else if (id == kBassReceiverParamId) std::snprintf(display, size, "%s", bassReceiverName(
        static_cast<uint32_t>(std::clamp(std::round(value), 0.0,
            static_cast<double>(s3g::kPsdRawFieldBassReceiverCount - 1u)))));
    else if (id == kBassPitchTrackingParamId) std::snprintf(display, size, "%s", bassPitchTrackingName(
        static_cast<uint32_t>(std::clamp(std::round(value), 0.0,
            static_cast<double>(s3g::kPsdRawFieldPitchTrackingCount - 1u)))));
    else if (id == kBassOctaveParamId) std::snprintf(display, size, "%s", bassOctaveName(
        static_cast<uint32_t>(std::clamp(std::round(value), 0.0,
            static_cast<double>(s3g::kPsdRawFieldBassOctaveCount - 1u)))));
    else if (id == kBassBodyParamId || id == kBassPunchParamId || id == kBassTraceParamId
        || id == kBassGlideParamId || id == kBassLowWidthParamId
        || id == kBassFuzzParamId || id == kBassMetalParamId
        || id == kBassFeedbackParamId) {
        std::snprintf(display, size, "%.1f%%", value * 100.0);
    }
    else if (id == kPerformanceModeParamId) std::snprintf(display, size, "%s", performanceModeName(
        static_cast<uint32_t>(std::clamp(std::round(value), 0.0, 1.0))));
    else if (id == kMidiReceiveParamId) {
        s3g::drum_midi::valueToText(value, display, size);
    }
    else if (id == kOutputFormatParamId) {
        std::snprintf(display, size, "%s", outputFormatName(
            static_cast<OutputFormat>(static_cast<uint32_t>(
                std::clamp(std::round(value), 0.0, 2.0)))));
    }
    else if (id == kOutputRotationParamId) {
        std::snprintf(display, size, "%+.1f deg", value);
    }
    else if (id == kAttackParamId || id == kDecayParamId || id == kReleaseParamId) {
        std::snprintf(display, size, "%s", envelopeTimeText(value).c_str());
    }
    else if (id == kSustainParamId) std::snprintf(display, size, "%.1f%%", value * 100.0);
    else if (id == kSeedParamId) std::snprintf(display, size, "%08X", static_cast<uint32_t>(value));
    else if (id == kRandomizeFieldParamId || id == kRandomizePatchParamId || id == kMutateParamId || id == kUndoParamId) std::snprintf(display, size, "TRIG");
    else if (id == kBitDepthParamId) std::snprintf(display, size, "%.0f bit", value);
    else if (id == kCodecModeParamId) std::snprintf(display, size, "%s", codecModeName(static_cast<uint32_t>(
        std::clamp(std::round(value), 0.0, static_cast<double>(kCodecModeMax)))));
    else if (id == kChannelSchemeParamId) std::snprintf(display, size, "%s", channelSchemeName(static_cast<uint32_t>(std::clamp(std::round(value), 0.0, 4.0))));
    else if (id == kPresetParamId) std::snprintf(display, size, "%s", presetName(static_cast<uint32_t>(std::clamp(std::round(value), 0.0, static_cast<double>(kCustomPreset)))));
    else std::snprintf(display, size, "%.0f%%", value * 100.0);
    return true;
}

bool paramsTextToValue(const clap_plugin_t* plugin, clap_id id, const char* display, double* value)
{
    if (!display || !value) return false;
    const double sampleRate = plugin ? self(plugin)->sampleRate : 48000.0;
    const double numeric = std::strtod(display, nullptr);
    if (id == kRunParamId) {
        if (std::strcmp(display, "PLAY") == 0 || std::strcmp(display, "RUN") == 0
            || std::strcmp(display, "ON") == 0) *value = 1.0;
        else if (std::strcmp(display, "STOP") == 0 || std::strcmp(display, "OFF") == 0) *value = 0.0;
        else *value = numeric >= 0.5 ? 1.0 : 0.0;
    } else if (id == kModulationEnabledParamId) {
        if (std::strcmp(display, "MOD ON") == 0 || std::strcmp(display, "ON") == 0) *value = 1.0;
        else if (std::strcmp(display, "MOD OFF") == 0 || std::strcmp(display, "OFF") == 0) *value = 0.0;
        else *value = numeric >= 0.5 ? 1.0 : 0.0;
    } else if (id == kBassReceiverParamId) {
        for (uint32_t receiver = 0u; receiver < s3g::kPsdRawFieldBassReceiverCount; ++receiver) {
            if (std::strcmp(display, bassReceiverName(receiver)) == 0) { *value = receiver; return true; }
        }
        return false;
    } else if (id == kBassPitchTrackingParamId) {
        for (uint32_t tracking = 0u; tracking < s3g::kPsdRawFieldPitchTrackingCount; ++tracking) {
            if (std::strcmp(display, bassPitchTrackingName(tracking)) == 0) { *value = tracking; return true; }
        }
        return false;
    } else if (id == kBassOctaveParamId) {
        for (uint32_t octave = 0u; octave < s3g::kPsdRawFieldBassOctaveCount; ++octave) {
            if (std::strcmp(display, bassOctaveName(octave)) == 0) { *value = octave; return true; }
        }
        return false;
    } else if (id == kBassBodyParamId || id == kBassPunchParamId || id == kBassTraceParamId
        || id == kBassGlideParamId || id == kBassLowWidthParamId
        || id == kBassFuzzParamId || id == kBassMetalParamId
        || id == kBassFeedbackParamId) {
        *value = std::clamp(std::strchr(display, '%') ? numeric / 100.0 : numeric, 0.0, 1.0);
    } else if (id == kPerformanceModeParamId) {
        if (std::strcmp(display, "MIDI") == 0) *value = 1.0;
        else if (std::strcmp(display, "FREE") == 0) *value = 0.0;
        else *value = numeric >= 0.5 ? 1.0 : 0.0;
    } else if (id == kMidiReceiveParamId) {
        return s3g::drum_midi::textToValue(display, value);
    } else if (id == kOutputFormatParamId) {
        for (uint32_t format = 0u; format < 3u; ++format) {
            if (std::strcmp(display, outputFormatName(
                    static_cast<OutputFormat>(format))) == 0) {
                *value = static_cast<double>(format);
                return true;
            }
        }
        return false;
    } else if (id == kOutputRotationParamId) {
        *value = std::clamp(numeric, -180.0, 180.0);
    } else if (id == kAttackParamId || id == kDecayParamId || id == kReleaseParamId) {
        *value = numeric;
        if (!std::strstr(display, "ms") && std::strchr(display, 's')) *value *= 1000.0;
    } else if (id == kSustainParamId) {
        *value = std::strchr(display, '%') ? numeric / 100.0 : numeric;
    } else if (id == kScanRateParamId) {
        double rate = numeric;
        if (std::strchr(display, 'k')) rate *= 1000.0;
        *value = std::clamp(std::log(std::max(rate, 1.0e-12) / (0.00002 * sampleRate)) / std::log(375000.0), 0.0, 1.0);
    } else if (id == kCodecRateParamId) {
        double rate = numeric;
        if (std::strchr(display, 'k')) rate *= 1000.0;
        *value = std::clamp(std::log2(sampleRate / std::max(rate, 1.0e-12)) / 14.0, 0.0, 1.0);
    } else if (id == kCarrierTuneParamId) {
        const uint32_t mode = plugin
            ? static_cast<uint32_t>(self(plugin)->params.codecMode)
            : static_cast<uint32_t>(s3g::PsdRawFieldCodecMode::Apt);
        const double nominal = codecCarrierNominal(mode);
        if (nominal > 0.0 && (std::strstr(display, "Hz") || std::strstr(display, "hz"))) {
            *value = std::clamp(12.0 * std::log2(std::max(numeric, 1.0) / nominal), -24.0, 24.0);
        } else {
            *value = std::clamp(numeric, -24.0, 24.0);
        }
    } else if (id == kModSourceParamId || id == kModSource2ParamId || id == kModSource3ParamId) {
        for (uint32_t source = 0u; source < s3g::kPsdRawFieldModSourceCount; ++source) {
            if (std::strcmp(display, modSourceName(source)) == 0) { *value = source; return true; }
        }
        return false;
    } else if (id == kModAlgorithmParamId) {
        for (uint32_t algorithm = 0u; algorithm < s3g::kPsdRawFieldModAlgorithmCount; ++algorithm) {
            if (std::strcmp(display, modAlgorithmName(algorithm)) == 0) { *value = algorithm; return true; }
        }
        return false;
    } else if (id == kModTargetParamId || id == kModTarget2ParamId || id == kModTarget3ParamId) {
        for (uint32_t target = 0u; target < s3g::kPsdRawFieldModTargetCount; ++target) {
            if (std::strcmp(display, modTargetName(target)) == 0) { *value = target; return true; }
        }
        return false;
    } else if (id == kModClockLockParamId || id == kModClockLock2ParamId || id == kModClockLock3ParamId) {
        if (std::strcmp(display, "LOCK") == 0) *value = 1.0;
        else if (std::strcmp(display, "FREE") == 0) *value = 0.0;
        else *value = numeric >= 0.5 ? 1.0 : 0.0;
    } else if (id == kModEnvelope1ParamId || id == kModEnvelope2ParamId || id == kModEnvelope3ParamId) {
        if (std::strcmp(display, "ADSR") == 0 || std::strcmp(display, "FOLLOW") == 0) *value = 1.0;
        else if (std::strcmp(display, "FIXED") == 0 || std::strcmp(display, "OFF") == 0) *value = 0.0;
        else *value = numeric >= 0.5 ? 1.0 : 0.0;
    } else if (id == kModRateParamId || id == kModRate2ParamId || id == kModRate3ParamId) {
        double rate = numeric;
        if (std::strchr(display, 'k')) rate *= 1000.0;
        *value = std::clamp(std::log(std::max(rate, 0.05) / 0.05) / std::log(160000.0), 0.0, 1.0);
    } else if (id == kModRatioParamId || id == kModRatio2ParamId || id == kModRatio3ParamId) {
        *value = std::clamp(numeric, 0.125, 16.0);
    } else if (id == kModIndexParamId || id == kModIndex2ParamId || id == kModIndex3ParamId) {
        *value = std::clamp(std::strchr(display, '%') ? numeric / 100.0 : numeric, 0.0, 1.0);
    } else if (id == kModFeedbackParamId || id == kModFeedback2ParamId || id == kModFeedback3ParamId) {
        *value = std::clamp(std::strchr(display, '%') ? numeric * (0.98 / 100.0) : numeric, 0.0, 0.98);
    } else if (id == kEvolveParamId) {
        if (std::strcmp(display, "OFF") == 0) *value = 0.0;
        else {
            double rate = numeric;
            if (std::strchr(display, 'k')) rate *= 1000.0;
            const double interval = 1.0 / std::max(rate, 1.0e-12);
            const double squared = (12.0 - interval) / 11.65;
            *value = squared <= 0.0 ? 0.0002 : std::sqrt(std::clamp(squared, 0.0, 1.0));
        }
    } else if (id == kCodecModeParamId) {
        for (uint32_t mode = 0u; mode < kCodecModeCount; ++mode) {
            if (std::strcmp(display, codecModeName(mode)) == 0) { *value = mode; return true; }
        }
        return false;
    } else if (id == kChannelSchemeParamId) {
        for (uint32_t mode = 0u; mode <= 4u; ++mode) {
            if (std::strcmp(display, channelSchemeName(mode)) == 0) { *value = mode; return true; }
        }
        return false;
    } else if (id == kPresetParamId) {
        for (uint32_t preset = 0u; preset <= kCustomPreset; ++preset) {
            if (std::strcmp(display, presetName(preset)) == 0) { *value = preset; return true; }
        }
        return false;
    } else if (id == kSeedParamId) {
        *value = static_cast<double>(std::strtoull(display, nullptr, 16));
    } else if (id == kGainParamId || id == kBitDepthParamId) {
        *value = numeric;
    } else if (id == kRandomizeFieldParamId || id == kRandomizePatchParamId || id == kMutateParamId || id == kUndoParamId) {
        *value = 0.0;
    } else {
        *value = numeric / 100.0;
    }
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*)
{
    readParamEvents(*self(plugin), in);
}
const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool writeFully(const clap_ostream_t* stream, const void* data, size_t size)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t offset = 0u;
    while (offset < size) {
        const int64_t written = stream->write(stream, bytes + offset, size - offset);
        if (written <= 0) return false;
        offset += static_cast<size_t>(written);
    }
    return true;
}

bool readFully(const clap_istream_t* stream, void* data, size_t size)
{
    auto* bytes = static_cast<uint8_t*>(data);
    size_t offset = 0u;
    while (offset < size) {
        const int64_t read = stream->read(stream, bytes + offset, size - offset);
        if (read <= 0) return false;
        offset += static_cast<size_t>(read);
    }
    return true;
}

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    SavedState state {};
    const auto* p = self(plugin);
    state.params = p->params;
    state.selectedPreset = p->selectedPreset;
    state.runState = p->playing.load(std::memory_order_relaxed) ? 1u : 0u;
    state.performanceMode = static_cast<uint32_t>(p->performanceMode);
    state.attackMs = p->attackMs;
    state.decayMs = p->decayMs;
    state.sustain = p->sustain;
    state.releaseMs = p->releaseMs;
    state.midiReceive = static_cast<uint32_t>(
        s3g::drum_midi::receiveChannel(p->midiReceive));
    state.outputFormat = static_cast<uint32_t>(p->outputFormat);
    state.outputRotationDeg = p->outputRotationDeg;
    if (!p->sourcePath.empty()) {
        state.sourceMode = static_cast<uint32_t>(p->sourceInterpretation);
        std::snprintf(state.sourcePath, sizeof(state.sourcePath), "%s", p->sourcePath.c_str());
    }
    return writeFully(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    auto* p = self(plugin);
    uint32_t version = 0u;
    if (!readFully(stream, &version, sizeof(version))) return false;
    bool restoredPlaying = true;
    p->performanceMode = PerformanceMode::Free;
    p->attackMs = 12.0f;
    p->decayMs = 280.0f;
    p->sustain = 0.72f;
    p->releaseMs = 850.0f;
    p->midiReceive = 0.0;
    p->outputFormat = OutputFormat::Direct8;
    p->outputRotationDeg = 0.0f;
    if (version == kStateVersion) {
        SavedState state {};
        state.version = version;
        constexpr size_t offset = sizeof(state.version);
        if (!readFully(stream, reinterpret_cast<uint8_t*>(&state) + offset, sizeof(state) - offset)) return false;
        p->params = state.params;
        p->selectedPreset = std::min(state.selectedPreset, kCustomPreset);
        restoredPlaying = state.runState != 0u;
        p->performanceMode = static_cast<PerformanceMode>(std::min(state.performanceMode, 1u));
        p->attackMs = std::clamp(state.attackMs, 1.0f, 5000.0f);
        p->decayMs = std::clamp(state.decayMs, 5.0f, 8000.0f);
        p->sustain = std::clamp(state.sustain, 0.0f, 1.0f);
        p->releaseMs = std::clamp(state.releaseMs, 5.0f, 12000.0f);
        p->midiReceive = static_cast<double>(
            s3g::drum_midi::receiveChannel(state.midiReceive));
        p->outputFormat = static_cast<OutputFormat>(
            std::min(state.outputFormat, 2u));
        p->outputRotationDeg = std::isfinite(state.outputRotationDeg)
            ? std::clamp(state.outputRotationDeg, -180.0f, 180.0f)
            : 0.0f;
        p->rawSource.reset();
        p->sourcePath.clear();
        p->sourceName.clear();
        p->sourceError.clear();
        p->sourceInterpretation = SourceInterpretation::Generated;
        if ((state.sourceMode == static_cast<uint32_t>(SourceInterpretation::RawBytes)
                || state.sourceMode == static_cast<uint32_t>(SourceInterpretation::Waveform))
            && state.sourcePath[0] != '\0') {
            std::size_t pathLength = 0u;
            while (pathLength < sizeof(state.sourcePath) && state.sourcePath[pathLength] != '\0') ++pathLength;
            p->sourcePath.assign(state.sourcePath, pathLength);
            p->sourceName = sourceNameFromPath(p->sourcePath);
            p->sourceInterpretation = static_cast<SourceInterpretation>(state.sourceMode);
            readSource(p->sourcePath, p->sourceInterpretation, p->rawSource, p->sourceError);
        }
    } else if (version == 24u) {
        LegacySavedStateV24 state {};
        state.version = version;
        constexpr size_t offset = sizeof(state.version);
        if (!readFully(stream, reinterpret_cast<uint8_t*>(&state) + offset,
                sizeof(state) - offset)) return false;
        p->params = state.params;
        p->selectedPreset = std::min(state.selectedPreset, kCustomPreset);
        restoredPlaying = state.runState != 0u;
        p->performanceMode = static_cast<PerformanceMode>(
            std::min(state.performanceMode, 1u));
        p->attackMs = std::clamp(state.attackMs, 1.0f, 5000.0f);
        p->decayMs = std::clamp(state.decayMs, 5.0f, 8000.0f);
        p->sustain = std::clamp(state.sustain, 0.0f, 1.0f);
        p->releaseMs = std::clamp(state.releaseMs, 5.0f, 12000.0f);
        p->midiReceive = static_cast<double>(
            s3g::drum_midi::receiveChannel(state.midiReceive));
        p->rawSource.reset();
        p->sourcePath.clear();
        p->sourceName.clear();
        p->sourceError.clear();
        p->sourceInterpretation = SourceInterpretation::Generated;
        if ((state.sourceMode
                    == static_cast<uint32_t>(SourceInterpretation::RawBytes)
                || state.sourceMode
                    == static_cast<uint32_t>(SourceInterpretation::Waveform))
            && state.sourcePath[0] != '\0') {
            std::size_t pathLength = 0u;
            while (pathLength < sizeof(state.sourcePath)
                && state.sourcePath[pathLength] != '\0') ++pathLength;
            p->sourcePath.assign(state.sourcePath, pathLength);
            p->sourceName = sourceNameFromPath(p->sourcePath);
            p->sourceInterpretation =
                static_cast<SourceInterpretation>(state.sourceMode);
            readSource(p->sourcePath, p->sourceInterpretation,
                p->rawSource, p->sourceError);
        }
    } else if (version == 23u) {
        LegacySavedStateV23 state {};
        state.version = version;
        constexpr size_t offset = sizeof(state.version);
        if (!readFully(stream, reinterpret_cast<uint8_t*>(&state) + offset,
                sizeof(state) - offset)) return false;
        p->params = state.params;
        p->selectedPreset = std::min(state.selectedPreset, kCustomPreset);
        restoredPlaying = state.runState != 0u;
        p->performanceMode = static_cast<PerformanceMode>(
            std::min(state.performanceMode, 1u));
        p->attackMs = std::clamp(state.attackMs, 1.0f, 5000.0f);
        p->decayMs = std::clamp(state.decayMs, 5.0f, 8000.0f);
        p->sustain = std::clamp(state.sustain, 0.0f, 1.0f);
        p->releaseMs = std::clamp(state.releaseMs, 5.0f, 12000.0f);
        p->rawSource.reset();
        p->sourcePath.clear();
        p->sourceName.clear();
        p->sourceError.clear();
        p->sourceInterpretation = SourceInterpretation::Generated;
        if ((state.sourceMode
                    == static_cast<uint32_t>(SourceInterpretation::RawBytes)
                || state.sourceMode
                    == static_cast<uint32_t>(SourceInterpretation::Waveform))
            && state.sourcePath[0] != '\0') {
            std::size_t pathLength = 0u;
            while (pathLength < sizeof(state.sourcePath)
                && state.sourcePath[pathLength] != '\0') ++pathLength;
            p->sourcePath.assign(state.sourcePath, pathLength);
            p->sourceName = sourceNameFromPath(p->sourcePath);
            p->sourceInterpretation =
                static_cast<SourceInterpretation>(state.sourceMode);
            readSource(p->sourcePath, p->sourceInterpretation,
                p->rawSource, p->sourceError);
        }
    } else if (version == 22u) {
        LegacySavedStateV22 state {};
        state.version = version;
        constexpr size_t offset = sizeof(state.version);
        if (!readFully(stream, reinterpret_cast<uint8_t*>(&state) + offset,
                sizeof(state) - offset)) return false;
        p->params = migrateLegacyParams(state.params);
        p->selectedPreset = std::min(state.selectedPreset, kCustomPreset);
        restoredPlaying = state.runState != 0u;
        p->performanceMode = static_cast<PerformanceMode>(
            std::min(state.performanceMode, 1u));
        p->attackMs = std::clamp(state.attackMs, 1.0f, 5000.0f);
        p->decayMs = std::clamp(state.decayMs, 5.0f, 8000.0f);
        p->sustain = std::clamp(state.sustain, 0.0f, 1.0f);
        p->releaseMs = std::clamp(state.releaseMs, 5.0f, 12000.0f);
        p->rawSource.reset();
        p->sourcePath.clear();
        p->sourceName.clear();
        p->sourceError.clear();
        p->sourceInterpretation = SourceInterpretation::Generated;
        if ((state.sourceMode
                    == static_cast<uint32_t>(SourceInterpretation::RawBytes)
                || state.sourceMode
                    == static_cast<uint32_t>(SourceInterpretation::Waveform))
            && state.sourcePath[0] != '\0') {
            std::size_t pathLength = 0u;
            while (pathLength < sizeof(state.sourcePath)
                && state.sourcePath[pathLength] != '\0') ++pathLength;
            p->sourcePath.assign(state.sourcePath, pathLength);
            p->sourceName = sourceNameFromPath(p->sourcePath);
            p->sourceInterpretation =
                static_cast<SourceInterpretation>(state.sourceMode);
            readSource(p->sourcePath, p->sourceInterpretation,
                p->rawSource, p->sourceError);
        }
    } else if (version == 21u) {
        LegacySavedStateV21 state {};
        state.version = version;
        constexpr size_t offset = sizeof(state.version);
        if (!readFully(stream, reinterpret_cast<uint8_t*>(&state) + offset, sizeof(state) - offset)) return false;
        p->params = migrateLegacyParams(state.params);
        p->selectedPreset = std::min(state.selectedPreset, kCustomPreset);
        restoredPlaying = state.runState != 0u;
        p->performanceMode = static_cast<PerformanceMode>(std::min(state.performanceMode, 1u));
        p->attackMs = std::clamp(state.attackMs, 1.0f, 5000.0f);
        p->decayMs = std::clamp(state.decayMs, 5.0f, 8000.0f);
        p->sustain = std::clamp(state.sustain, 0.0f, 1.0f);
        p->releaseMs = std::clamp(state.releaseMs, 5.0f, 12000.0f);
        p->rawSource.reset();
        p->sourcePath.clear();
        p->sourceName.clear();
        p->sourceError.clear();
        p->sourceInterpretation = SourceInterpretation::Generated;
        if ((state.sourceMode == static_cast<uint32_t>(SourceInterpretation::RawBytes)
                || state.sourceMode == static_cast<uint32_t>(SourceInterpretation::Waveform))
            && state.sourcePath[0] != '\0') {
            std::size_t pathLength = 0u;
            while (pathLength < sizeof(state.sourcePath) && state.sourcePath[pathLength] != '\0') ++pathLength;
            p->sourcePath.assign(state.sourcePath, pathLength);
            p->sourceName = sourceNameFromPath(p->sourcePath);
            p->sourceInterpretation = static_cast<SourceInterpretation>(state.sourceMode);
            readSource(p->sourcePath, p->sourceInterpretation, p->rawSource, p->sourceError);
        }
    } else if (version == 20u) {
        LegacySavedStateV20 state {};
        state.version = version;
        constexpr size_t offset = sizeof(state.version);
        if (!readFully(stream, reinterpret_cast<uint8_t*>(&state) + offset, sizeof(state) - offset)) return false;
        p->params = migrateLegacyParams(state.params);
        p->selectedPreset = std::min(state.selectedPreset, kCustomPreset);
        restoredPlaying = state.runState != 0u;
        p->performanceMode = static_cast<PerformanceMode>(std::min(state.performanceMode, 1u));
        p->attackMs = std::clamp(state.attackMs, 1.0f, 5000.0f);
        p->decayMs = std::clamp(state.decayMs, 5.0f, 8000.0f);
        p->sustain = std::clamp(state.sustain, 0.0f, 1.0f);
        p->releaseMs = std::clamp(state.releaseMs, 5.0f, 12000.0f);
        p->rawSource.reset();
        p->sourcePath.clear();
        p->sourceName.clear();
        p->sourceError.clear();
        p->sourceInterpretation = SourceInterpretation::Generated;
        if ((state.sourceMode == static_cast<uint32_t>(SourceInterpretation::RawBytes)
                || state.sourceMode == static_cast<uint32_t>(SourceInterpretation::Waveform))
            && state.sourcePath[0] != '\0') {
            std::size_t pathLength = 0u;
            while (pathLength < sizeof(state.sourcePath) && state.sourcePath[pathLength] != '\0') ++pathLength;
            p->sourcePath.assign(state.sourcePath, pathLength);
            p->sourceName = sourceNameFromPath(p->sourcePath);
            p->sourceInterpretation = static_cast<SourceInterpretation>(state.sourceMode);
            readSource(p->sourcePath, p->sourceInterpretation, p->rawSource, p->sourceError);
        }
    } else if (version == 19u) {
        LegacySavedStateV19 state {};
        state.version = version;
        constexpr size_t offset = sizeof(state.version);
        if (!readFully(stream, reinterpret_cast<uint8_t*>(&state) + offset, sizeof(state) - offset)) return false;
        p->params = migrateLegacyParams(state.params);
        p->selectedPreset = std::min(state.selectedPreset, kCustomPreset);
        restoredPlaying = state.runState != 0u;
        p->performanceMode = static_cast<PerformanceMode>(std::min(state.performanceMode, 1u));
        p->attackMs = std::clamp(state.attackMs, 1.0f, 5000.0f);
        p->decayMs = std::clamp(state.decayMs, 5.0f, 8000.0f);
        p->sustain = std::clamp(state.sustain, 0.0f, 1.0f);
        p->releaseMs = std::clamp(state.releaseMs, 5.0f, 12000.0f);
        p->rawSource.reset();
        p->sourcePath.clear();
        p->sourceName.clear();
        p->sourceError.clear();
        p->sourceInterpretation = SourceInterpretation::Generated;
        if ((state.sourceMode == static_cast<uint32_t>(SourceInterpretation::RawBytes)
                || state.sourceMode == static_cast<uint32_t>(SourceInterpretation::Waveform))
            && state.sourcePath[0] != '\0') {
            std::size_t pathLength = 0u;
            while (pathLength < sizeof(state.sourcePath) && state.sourcePath[pathLength] != '\0') ++pathLength;
            p->sourcePath.assign(state.sourcePath, pathLength);
            p->sourceName = sourceNameFromPath(p->sourcePath);
            p->sourceInterpretation = static_cast<SourceInterpretation>(state.sourceMode);
            readSource(p->sourcePath, p->sourceInterpretation, p->rawSource, p->sourceError);
        }
    } else if (version == 18u) {
        LegacySavedStateV18 state {};
        state.version = version;
        constexpr size_t offset = sizeof(state.version);
        if (!readFully(stream, reinterpret_cast<uint8_t*>(&state) + offset, sizeof(state) - offset)) return false;
        p->params = migrateLegacyParams(state.params);
        p->selectedPreset = std::min(state.selectedPreset, kCustomPreset);
        restoredPlaying = state.runState != 0u;
        p->performanceMode = static_cast<PerformanceMode>(std::min(state.performanceMode, 1u));
        p->attackMs = std::clamp(state.attackMs, 1.0f, 5000.0f);
        p->decayMs = std::clamp(state.decayMs, 5.0f, 8000.0f);
        p->sustain = std::clamp(state.sustain, 0.0f, 1.0f);
        p->releaseMs = std::clamp(state.releaseMs, 5.0f, 12000.0f);
        p->rawSource.reset();
        p->sourcePath.clear();
        p->sourceName.clear();
        p->sourceError.clear();
        p->sourceInterpretation = SourceInterpretation::Generated;
        if ((state.sourceMode == static_cast<uint32_t>(SourceInterpretation::RawBytes)
                || state.sourceMode == static_cast<uint32_t>(SourceInterpretation::Waveform))
            && state.sourcePath[0] != '\0') {
            std::size_t pathLength = 0u;
            while (pathLength < sizeof(state.sourcePath) && state.sourcePath[pathLength] != '\0') ++pathLength;
            p->sourcePath.assign(state.sourcePath, pathLength);
            p->sourceName = sourceNameFromPath(p->sourcePath);
            p->sourceInterpretation = static_cast<SourceInterpretation>(state.sourceMode);
            readSource(p->sourcePath, p->sourceInterpretation, p->rawSource, p->sourceError);
        }
    } else if (version == 17u) {
        LegacySavedStateV17 state {};
        state.version = version;
        constexpr size_t offset = sizeof(state.version);
        if (!readFully(stream, reinterpret_cast<uint8_t*>(&state) + offset, sizeof(state) - offset)) return false;
        p->params = migrateLegacyParams(state.params);
        p->selectedPreset = std::min(state.selectedPreset, kCustomPreset);
        restoredPlaying = state.runState != 0u;
        p->performanceMode = static_cast<PerformanceMode>(std::min(state.performanceMode, 1u));
        p->attackMs = std::clamp(state.attackMs, 1.0f, 5000.0f);
        p->decayMs = std::clamp(state.decayMs, 5.0f, 8000.0f);
        p->sustain = std::clamp(state.sustain, 0.0f, 1.0f);
        p->releaseMs = std::clamp(state.releaseMs, 5.0f, 12000.0f);
        p->rawSource.reset();
        p->sourcePath.clear();
        p->sourceName.clear();
        p->sourceError.clear();
        p->sourceInterpretation = SourceInterpretation::Generated;
        if ((state.sourceMode == static_cast<uint32_t>(SourceInterpretation::RawBytes)
                || state.sourceMode == static_cast<uint32_t>(SourceInterpretation::Waveform))
            && state.sourcePath[0] != '\0') {
            std::size_t pathLength = 0u;
            while (pathLength < sizeof(state.sourcePath) && state.sourcePath[pathLength] != '\0') ++pathLength;
            p->sourcePath.assign(state.sourcePath, pathLength);
            p->sourceName = sourceNameFromPath(p->sourcePath);
            p->sourceInterpretation = static_cast<SourceInterpretation>(state.sourceMode);
            readSource(p->sourcePath, p->sourceInterpretation, p->rawSource, p->sourceError);
        }
    } else if (version == 16u) {
        LegacySavedStateV16 state {};
        state.version = version;
        constexpr size_t offset = sizeof(state.version);
        if (!readFully(stream, reinterpret_cast<uint8_t*>(&state) + offset, sizeof(state) - offset)) return false;
        p->params = migrateLegacyParams(state.params);
        p->selectedPreset = std::min(state.selectedPreset, kCustomPreset);
        restoredPlaying = state.runState != 0u;
        p->performanceMode = static_cast<PerformanceMode>(std::min(state.performanceMode, 1u));
        p->attackMs = std::clamp(state.attackMs, 1.0f, 5000.0f);
        p->decayMs = std::clamp(state.decayMs, 5.0f, 8000.0f);
        p->sustain = std::clamp(state.sustain, 0.0f, 1.0f);
        p->releaseMs = std::clamp(state.releaseMs, 5.0f, 12000.0f);
        p->rawSource.reset();
        p->sourcePath.clear();
        p->sourceName.clear();
        p->sourceError.clear();
        p->sourceInterpretation = SourceInterpretation::Generated;
        if ((state.sourceMode == static_cast<uint32_t>(SourceInterpretation::RawBytes)
                || state.sourceMode == static_cast<uint32_t>(SourceInterpretation::Waveform))
            && state.sourcePath[0] != '\0') {
            std::size_t pathLength = 0u;
            while (pathLength < sizeof(state.sourcePath) && state.sourcePath[pathLength] != '\0') ++pathLength;
            p->sourcePath.assign(state.sourcePath, pathLength);
            p->sourceName = sourceNameFromPath(p->sourcePath);
            p->sourceInterpretation = static_cast<SourceInterpretation>(state.sourceMode);
            readSource(p->sourcePath, p->sourceInterpretation, p->rawSource, p->sourceError);
        }
    } else if (version == 15u) {
        LegacySavedStateV15 state {};
        state.version = version;
        constexpr size_t offset = sizeof(state.version);
        if (!readFully(stream, reinterpret_cast<uint8_t*>(&state) + offset, sizeof(state) - offset)) return false;
        p->params = migrateLegacyParams(state.params);
        p->selectedPreset = std::min(state.selectedPreset, kCustomPreset);
        restoredPlaying = state.runState != 0u;
        p->performanceMode = static_cast<PerformanceMode>(std::min(state.performanceMode, 1u));
        p->attackMs = std::clamp(state.attackMs, 1.0f, 5000.0f);
        p->decayMs = std::clamp(state.decayMs, 5.0f, 8000.0f);
        p->sustain = std::clamp(state.sustain, 0.0f, 1.0f);
        p->releaseMs = std::clamp(state.releaseMs, 5.0f, 12000.0f);
        p->rawSource.reset();
        p->sourcePath.clear();
        p->sourceName.clear();
        p->sourceError.clear();
        p->sourceInterpretation = SourceInterpretation::Generated;
        if ((state.sourceMode == static_cast<uint32_t>(SourceInterpretation::RawBytes)
                || state.sourceMode == static_cast<uint32_t>(SourceInterpretation::Waveform))
            && state.sourcePath[0] != '\0') {
            std::size_t pathLength = 0u;
            while (pathLength < sizeof(state.sourcePath) && state.sourcePath[pathLength] != '\0') ++pathLength;
            p->sourcePath.assign(state.sourcePath, pathLength);
            p->sourceName = sourceNameFromPath(p->sourcePath);
            p->sourceInterpretation = static_cast<SourceInterpretation>(state.sourceMode);
            readSource(p->sourcePath, p->sourceInterpretation, p->rawSource, p->sourceError);
        }
    } else if (version == 14u) {
        LegacySavedStateV14 state {};
        state.version = version;
        constexpr size_t offset = sizeof(state.version);
        if (!readFully(stream, reinterpret_cast<uint8_t*>(&state) + offset, sizeof(state) - offset)) return false;
        p->params = migrateLegacyParams(state.params);
        p->selectedPreset = std::min(state.selectedPreset, kCustomPreset);
        restoredPlaying = state.runState != 0u;
        p->rawSource.reset();
        p->sourcePath.clear();
        p->sourceName.clear();
        p->sourceError.clear();
        p->sourceInterpretation = SourceInterpretation::Generated;
        if ((state.sourceMode == static_cast<uint32_t>(SourceInterpretation::RawBytes)
                || state.sourceMode == static_cast<uint32_t>(SourceInterpretation::Waveform))
            && state.sourcePath[0] != '\0') {
            std::size_t pathLength = 0u;
            while (pathLength < sizeof(state.sourcePath) && state.sourcePath[pathLength] != '\0') ++pathLength;
            p->sourcePath.assign(state.sourcePath, pathLength);
            p->sourceName = sourceNameFromPath(p->sourcePath);
            p->sourceInterpretation = static_cast<SourceInterpretation>(state.sourceMode);
            readSource(p->sourcePath, p->sourceInterpretation, p->rawSource, p->sourceError);
        }
    } else if (version == 13u) {
        LegacySavedStateV13 state {};
        state.version = version;
        constexpr size_t offset = sizeof(state.version);
        if (!readFully(stream, reinterpret_cast<uint8_t*>(&state) + offset, sizeof(state) - offset)) return false;
        p->params = migrateLegacyParams(state.params);
        p->selectedPreset = std::min(state.selectedPreset, kCustomPreset);
        restoredPlaying = state.runState != 0u;
        p->rawSource.reset();
        p->sourcePath.clear();
        p->sourceName.clear();
        p->sourceError.clear();
        p->sourceInterpretation = SourceInterpretation::Generated;
        if ((state.sourceMode == static_cast<uint32_t>(SourceInterpretation::RawBytes)
                || state.sourceMode == static_cast<uint32_t>(SourceInterpretation::Waveform))
            && state.sourcePath[0] != '\0') {
            std::size_t pathLength = 0u;
            while (pathLength < sizeof(state.sourcePath) && state.sourcePath[pathLength] != '\0') ++pathLength;
            p->sourcePath.assign(state.sourcePath, pathLength);
            p->sourceName = sourceNameFromPath(p->sourcePath);
            p->sourceInterpretation = static_cast<SourceInterpretation>(state.sourceMode);
            readSource(p->sourcePath, p->sourceInterpretation, p->rawSource, p->sourceError);
        }
    } else if (version == 12u) {
        LegacySavedStateV12 state {};
        state.version = version;
        constexpr size_t offset = sizeof(state.version);
        if (!readFully(stream, reinterpret_cast<uint8_t*>(&state) + offset, sizeof(state) - offset)) return false;
        p->params = migrateLegacyParams(state.params);
        p->selectedPreset = std::min(state.selectedPreset, kCustomPreset);
        p->rawSource.reset();
        p->sourcePath.clear();
        p->sourceName.clear();
        p->sourceError.clear();
        p->sourceInterpretation = SourceInterpretation::Generated;
        if ((state.sourceMode == static_cast<uint32_t>(SourceInterpretation::RawBytes)
                || state.sourceMode == static_cast<uint32_t>(SourceInterpretation::Waveform))
            && state.sourcePath[0] != '\0') {
            std::size_t pathLength = 0u;
            while (pathLength < sizeof(state.sourcePath) && state.sourcePath[pathLength] != '\0') ++pathLength;
            p->sourcePath.assign(state.sourcePath, pathLength);
            p->sourceName = sourceNameFromPath(p->sourcePath);
            p->sourceInterpretation = static_cast<SourceInterpretation>(state.sourceMode);
            readSource(p->sourcePath, p->sourceInterpretation, p->rawSource, p->sourceError);
        }
    } else if (version == 11u) {
        LegacySavedStateV11 state {};
        state.version = version;
        constexpr size_t offset = sizeof(state.version);
        if (!readFully(stream, reinterpret_cast<uint8_t*>(&state) + offset, sizeof(state) - offset)) return false;
        p->params = migrateLegacyParams(state.params);
        p->selectedPreset = state.selectedPreset == 12u
            ? kCustomPreset
            : std::min(state.selectedPreset, 11u);
        p->rawSource.reset();
        p->sourcePath.clear();
        p->sourceName.clear();
        p->sourceError.clear();
        p->sourceInterpretation = SourceInterpretation::Generated;
        if (state.sourceMode == 1u && state.sourcePath[0] != '\0') {
            std::size_t pathLength = 0u;
            while (pathLength < sizeof(state.sourcePath) && state.sourcePath[pathLength] != '\0') ++pathLength;
            p->sourcePath.assign(state.sourcePath, pathLength);
            p->sourceName = sourceNameFromPath(p->sourcePath);
            p->sourceInterpretation = SourceInterpretation::RawBytes;
            readSource(p->sourcePath, p->sourceInterpretation, p->rawSource, p->sourceError);
        }
    } else if (version == 10u) {
        LegacySavedStateV10 state {};
        state.version = version;
        constexpr size_t offset = sizeof(state.version);
        if (!readFully(stream, reinterpret_cast<uint8_t*>(&state) + offset, sizeof(state) - offset)) return false;
        p->params = migrateLegacyParams(state.params);
        p->selectedPreset = state.selectedPreset == 12u
            ? kCustomPreset
            : std::min(state.selectedPreset, 11u);
        p->rawSource.reset();
        p->sourcePath.clear();
        p->sourceName.clear();
        p->sourceError.clear();
        p->sourceInterpretation = SourceInterpretation::Generated;
    } else if (version == 9u) {
        uint32_t selectedPreset = 0u;
        LegacyParamsV9 legacy {};
        if (!readFully(stream, &selectedPreset, sizeof(selectedPreset))) return false;
        if (!readFully(stream, &legacy, sizeof(legacy))) return false;
        p->params = migrateLegacyParams(legacy);
        p->selectedPreset = selectedPreset == 5u ? kCustomPreset : std::min(selectedPreset, 4u);
        p->rawSource.reset();
        p->sourcePath.clear();
        p->sourceName.clear();
        p->sourceError.clear();
        p->sourceInterpretation = SourceInterpretation::Generated;
    } else if (version == 8u) {
        LegacyParamsV8 legacy {};
        if (!readFully(stream, &legacy, sizeof(legacy))) return false;
        p->params = migrateLegacyParams(legacy);
        p->selectedPreset = kCustomPreset;
        p->rawSource.reset();
        p->sourcePath.clear();
        p->sourceName.clear();
        p->sourceError.clear();
        p->sourceInterpretation = SourceInterpretation::Generated;
    } else {
        return false;
    }
    p->hasUndo = false;
    p->undoSource.reset();
    p->undoSourceInterpretation = SourceInterpretation::Generated;
    p->playing.store(restoredPlaying, std::memory_order_relaxed);
    p->runGain = restoredPlaying ? 1.0f : 0.0f;
    resetMidiPerformance(*p);
    p->field.setSource(p->rawSource);
    p->field.setParams(p->params);
    p->field.reset();
    return true;
}
const clap_plugin_state_t stateExt { stateSave, stateLoad };

double normalizedParam(const s3g::PsdRawFieldParams& p, clap_id id)
{
    switch (id) {
    case kScanRateParamId: return p.scanRate;
    case kTextureParamId: return p.texture;
    case kGeometryParamId: return p.geometry;
    case kChaosParamId: return p.chaos;
    case kFoldParamId: return p.fold;
    case kEvolveParamId: return p.evolve;
    case kChannelSchemeParamId: return static_cast<double>(static_cast<uint32_t>(p.channelScheme)) / 4.0;
    case kChannelSpreadParamId: return p.channelSpread;
    case kCodecModeParamId:
        return static_cast<double>(static_cast<uint32_t>(p.codecMode)) / static_cast<double>(kCodecModeMax);
    case kCodecRateParamId: return p.codecRate;
    case kBitDepthParamId: return (p.bitDepth - 2.0f) / 14.0f;
    case kCodecDamageParamId: return p.codecDamage;
    case kCarrierTuneParamId: return (p.carrierTune + 24.0f) / 48.0f;
    case kModSourceParamId: return static_cast<double>(static_cast<uint32_t>(p.modSource))
        / static_cast<double>(s3g::kPsdRawFieldModSourceCount - 1u);
    case kModTargetParamId: return static_cast<double>(static_cast<uint32_t>(p.modTarget))
        / static_cast<double>(s3g::kPsdRawFieldModTargetCount - 1u);
    case kModRateParamId: return p.modRate;
    case kModRatioParamId: return std::log2(p.modRatio / 0.125f) / 7.0;
    case kModIndexParamId: return p.modIndex;
    case kModFeedbackParamId: return p.modFeedback / 0.98f;
    case kModClockLockParamId: return p.modClockLock;
    case kModAlgorithmParamId: return static_cast<double>(static_cast<uint32_t>(p.modAlgorithm))
        / static_cast<double>(s3g::kPsdRawFieldModAlgorithmCount - 1u);
    case kModSource2ParamId: return static_cast<double>(static_cast<uint32_t>(p.modSource2))
        / static_cast<double>(s3g::kPsdRawFieldModSourceCount - 1u);
    case kModRate2ParamId: return p.modRate2;
    case kModRatio2ParamId: return std::log2(p.modRatio2 / 0.125f) / 7.0;
    case kModIndex2ParamId: return p.modIndex2;
    case kModFeedback2ParamId: return p.modFeedback2 / 0.98f;
    case kModClockLock2ParamId: return p.modClockLock2;
    case kModTarget2ParamId: return static_cast<double>(static_cast<uint32_t>(p.modTarget2))
        / static_cast<double>(s3g::kPsdRawFieldModTargetCount - 1u);
    case kModSource3ParamId: return static_cast<double>(static_cast<uint32_t>(p.modSource3))
        / static_cast<double>(s3g::kPsdRawFieldModSourceCount - 1u);
    case kModTarget3ParamId: return static_cast<double>(static_cast<uint32_t>(p.modTarget3))
        / static_cast<double>(s3g::kPsdRawFieldModTargetCount - 1u);
    case kModRate3ParamId: return p.modRate3;
    case kModRatio3ParamId: return std::log2(p.modRatio3 / 0.125f) / 7.0;
    case kModIndex3ParamId: return p.modIndex3;
    case kModFeedback3ParamId: return p.modFeedback3 / 0.98f;
    case kModClockLock3ParamId: return p.modClockLock3;
    case kModEnvelope1ParamId: return p.modEnvelope1;
    case kModEnvelope2ParamId: return p.modEnvelope2;
    case kModEnvelope3ParamId: return p.modEnvelope3;
    case kModulationEnabledParamId: return p.modulationEnabled;
    case kBassReceiverParamId: return static_cast<double>(static_cast<uint32_t>(p.bassReceiver))
        / static_cast<double>(s3g::kPsdRawFieldBassReceiverCount - 1u);
    case kBassBodyParamId: return p.bassBody;
    case kBassPunchParamId: return p.bassPunch;
    case kBassTraceParamId: return p.bassTrace;
    case kBassPitchTrackingParamId:
        return static_cast<double>(static_cast<uint32_t>(p.bassPitchTracking))
            / static_cast<double>(s3g::kPsdRawFieldPitchTrackingCount - 1u);
    case kBassGlideParamId: return p.bassGlide;
    case kBassOctaveParamId: return static_cast<double>(static_cast<uint32_t>(p.bassOctave))
        / static_cast<double>(s3g::kPsdRawFieldBassOctaveCount - 1u);
    case kBassLowWidthParamId: return p.bassLowWidth;
    case kBassFuzzParamId: return p.bassFuzz;
    case kBassMetalParamId: return p.bassMetal;
    case kBassFeedbackParamId: return p.bassFeedback;
    case kDriveParamId: return p.drive;
    case kShredParamId: return p.shred;
    case kResonanceParamId: return p.resonance;
    case kGainParamId: return (p.gainDb + 60.0f) / 66.0f;
    case kSeedParamId: return static_cast<double>((p.seed >> 8u) & 0xffffu) / 65535.0;
    default: return 0.5;
    }
}

double normalizedEnvelopeTime(double value, double minimum, double maximum)
{
    value = std::clamp(value, minimum, maximum);
    return std::log(value / minimum) / std::log(maximum / minimum);
}

double normalizedPerformanceParam(const Plugin& p, clap_id id)
{
    switch (id) {
    case kAttackParamId: return normalizedEnvelopeTime(p.attackMs, 1.0, 5000.0);
    case kDecayParamId: return normalizedEnvelopeTime(p.decayMs, 5.0, 8000.0);
    case kSustainParamId: return p.sustain;
    case kReleaseParamId: return normalizedEnvelopeTime(p.releaseMs, 5.0, 12000.0);
    case kOutputRotationParamId: return (p.outputRotationDeg + 180.0) / 360.0;
    default: return 0.0;
    }
}

double denormalizedEnvelopeTime(double normalized, double minimum, double maximum)
{
    return minimum * std::pow(maximum / minimum, std::clamp(normalized, 0.0, 1.0));
}

void applyNormalizedParam(Plugin& p, clap_id id, double normalized)
{
    normalized = std::clamp(normalized, 0.0, 1.0);
    switch (id) {
    case kBitDepthParamId: applyParam(p, id, std::round(2.0 + normalized * 14.0)); break;
    case kCodecModeParamId: applyParam(p, id, std::round(normalized * static_cast<double>(kCodecModeMax))); break;
    case kChannelSchemeParamId: applyParam(p, id, std::round(normalized * 4.0)); break;
    case kGainParamId: applyParam(p, id, -60.0 + normalized * 66.0); break;
    case kCarrierTuneParamId: applyParam(p, id, -24.0 + normalized * 48.0); break;
    case kModSourceParamId: applyParam(p, id, std::round(normalized
        * static_cast<double>(s3g::kPsdRawFieldModSourceCount - 1u))); break;
    case kModTargetParamId: applyParam(p, id, std::round(normalized
        * static_cast<double>(s3g::kPsdRawFieldModTargetCount - 1u))); break;
    case kModRatioParamId: applyParam(p, id, 0.125 * std::pow(2.0, normalized * 7.0)); break;
    case kModFeedbackParamId: applyParam(p, id, normalized * 0.98); break;
    case kModClockLockParamId: applyParam(p, id, normalized >= 0.5 ? 1.0 : 0.0); break;
    case kModAlgorithmParamId: applyParam(p, id, std::round(normalized
        * static_cast<double>(s3g::kPsdRawFieldModAlgorithmCount - 1u))); break;
    case kModSource2ParamId: applyParam(p, id, std::round(normalized
        * static_cast<double>(s3g::kPsdRawFieldModSourceCount - 1u))); break;
    case kModRatio2ParamId: applyParam(p, id, 0.125 * std::pow(2.0, normalized * 7.0)); break;
    case kModFeedback2ParamId: applyParam(p, id, normalized * 0.98); break;
    case kModClockLock2ParamId: applyParam(p, id, normalized >= 0.5 ? 1.0 : 0.0); break;
    case kModTarget2ParamId: applyParam(p, id, std::round(normalized
        * static_cast<double>(s3g::kPsdRawFieldModTargetCount - 1u))); break;
    case kModSource3ParamId: applyParam(p, id, std::round(normalized
        * static_cast<double>(s3g::kPsdRawFieldModSourceCount - 1u))); break;
    case kModTarget3ParamId: applyParam(p, id, std::round(normalized
        * static_cast<double>(s3g::kPsdRawFieldModTargetCount - 1u))); break;
    case kModRatio3ParamId: applyParam(p, id, 0.125 * std::pow(2.0, normalized * 7.0)); break;
    case kModFeedback3ParamId: applyParam(p, id, normalized * 0.98); break;
    case kModClockLock3ParamId: applyParam(p, id, normalized >= 0.5 ? 1.0 : 0.0); break;
    case kModEnvelope1ParamId:
    case kModEnvelope2ParamId:
    case kModEnvelope3ParamId:
    case kModulationEnabledParamId:
        applyParam(p, id, normalized >= 0.5 ? 1.0 : 0.0);
        break;
    case kBassReceiverParamId: applyParam(p, id, std::round(normalized
        * static_cast<double>(s3g::kPsdRawFieldBassReceiverCount - 1u))); break;
    case kBassPitchTrackingParamId: applyParam(p, id, std::round(normalized
        * static_cast<double>(s3g::kPsdRawFieldPitchTrackingCount - 1u))); break;
    case kBassOctaveParamId: applyParam(p, id, std::round(normalized
        * static_cast<double>(s3g::kPsdRawFieldBassOctaveCount - 1u))); break;
    case kBassBodyParamId:
    case kBassPunchParamId:
    case kBassTraceParamId:
    case kBassGlideParamId:
    case kBassLowWidthParamId:
    case kBassFuzzParamId:
    case kBassMetalParamId:
    case kBassFeedbackParamId:
        applyParam(p, id, normalized);
        break;
    case kSeedParamId: applyParam(p, id, 1.0 + normalized * 4294967294.0); break;
    case kAttackParamId: applyParam(p, id, denormalizedEnvelopeTime(normalized, 1.0, 5000.0)); break;
    case kDecayParamId: applyParam(p, id, denormalizedEnvelopeTime(normalized, 5.0, 8000.0)); break;
    case kSustainParamId: applyParam(p, id, normalized); break;
    case kReleaseParamId: applyParam(p, id, denormalizedEnvelopeTime(normalized, 5.0, 12000.0)); break;
    case kOutputRotationParamId: applyParam(p, id, -180.0 + normalized * 360.0); break;
    default: applyParam(p, id, normalized); break;
    }
}

std::string byteCountText(uint64_t bytes)
{
    char text[32] {};
    if (bytes >= 1024u * 1024u) {
        std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else if (bytes >= 1024u) {
        std::snprintf(text, sizeof(text), "%.1f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        std::snprintf(text, sizeof(text), "%llu B", static_cast<unsigned long long>(bytes));
    }
    return text;
}

std::string sourceStatusText(const Plugin& p)
{
    if (!p.rawSource) {
        if (p.sourcePath.empty()) {
            return std::string("SOURCE GENERATED [")
                + codecModeName(static_cast<uint32_t>(p.params.fieldCodecMode)) + "] | 64.0 KB";
        }
        return (p.sourceError.empty() ? std::string("SOURCE MISSING") : p.sourceError)
            + " | " + p.sourceName;
    }
    if (p.rawSource->waveform) {
        char format[80] {};
        std::snprintf(format, sizeof(format), "WAVE %u>8 | %.1fK %u-BIT | ",
            p.rawSource->sourceChannelCount,
            static_cast<double>(p.rawSource->sourceSampleRate) / 1000.0,
            p.rawSource->sourceBitsPerSample);
        std::string status = format + p.sourceName;
        if (p.rawSource->truncated) status += " | PARTIAL";
        return status;
    }
    std::string status = "RAW | " + p.sourceName + " | " + byteCountText(p.rawSource->originalByteCount);
    if (p.rawSource->truncated) status += " | FIRST 64 MB";
    return status;
}

std::string compactEnvelopeTimeText(float milliseconds)
{
    char text[24] {};
    if (milliseconds >= 1000.0f) std::snprintf(text, sizeof(text), "%.2fs", milliseconds * 0.001f);
    else if (milliseconds >= 100.0f) std::snprintf(text, sizeof(text), "%.0fms", milliseconds);
    else std::snprintf(text, sizeof(text), "%.1fms", milliseconds);
    return text;
}

std::string midiNoteName(int32_t key)
{
    if (key < 0 || key > 127) return "WAITING";
    constexpr const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    char text[16] {};
    std::snprintf(text, sizeof(text), "%s%d", names[key % 12], key / 12 - 1);
    return text;
}

} // namespace

#if defined(__APPLE__)
namespace {

struct EnvelopeGraphGeometry {
    NSRect frame;
    CGFloat top;
    CGFloat bottom;
    NSPoint start;
    NSPoint attack;
    NSPoint decay;
    NSPoint sustain;
    NSPoint release;
};

EnvelopeGraphGeometry envelopeGraphGeometry(const Plugin& plugin)
{
    EnvelopeGraphGeometry graph {};
    graph.frame = NSMakeRect(kEnvelopeX, kEnvelopeY, kEnvelopeWidth, kEnvelopeHeight);
    graph.top = graph.frame.origin.y + 10.0;
    graph.bottom = NSMaxY(graph.frame) - 10.0;
    const CGFloat sustainY = graph.bottom
        - static_cast<CGFloat>(std::clamp(plugin.sustain, 0.0f, 1.0f)) * (graph.bottom - graph.top);
    graph.start = NSMakePoint(kEnvelopeX + 14.0, graph.bottom);
    graph.attack = NSMakePoint(
        kEnvelopeX + 54.0
            + static_cast<CGFloat>(normalizedPerformanceParam(plugin, kAttackParamId)) * 126.0,
        graph.top);
    graph.decay = NSMakePoint(
        kEnvelopeX + 220.0
            + static_cast<CGFloat>(normalizedPerformanceParam(plugin, kDecayParamId)) * 140.0,
        sustainY);
    graph.sustain = NSMakePoint(kEnvelopeX + 460.0, sustainY);
    graph.release = NSMakePoint(
        kEnvelopeX + 520.0
            + static_cast<CGFloat>(normalizedPerformanceParam(plugin, kReleaseParamId)) * 160.0,
        graph.bottom);
    return graph;
}

CGFloat squaredDistance(NSPoint a, NSPoint b)
{
    const CGFloat x = a.x - b.x;
    const CGFloat y = a.y - b.y;
    return x * x + y * y;
}

NSRect midiReceiveDropdownRect()
{
    constexpr CGFloat itemHeight = 18.0;
    constexpr CGFloat itemCount = 17.0;
    return NSMakeRect(
        s3g::gui_layout::processorControlX(kMidiReceivePanelX),
        kPerformanceRowY - 3.0 - itemHeight * itemCount,
        s3g::gui_layout::processorMenuWidth(kMidiReceivePanelWidth),
        itemHeight * itemCount);
}

NSRect outputFormatDropdownRect()
{
    constexpr CGFloat itemHeight = 18.0;
    constexpr CGFloat itemCount = 3.0;
    return NSMakeRect(
        s3g::gui_layout::processorControlX(kOutputPanelX),
        kOutputFormatRowY + 14.0,
        s3g::gui_layout::processorMenuWidth(kOutputPanelWidth),
        itemHeight * itemCount);
}

} // namespace

@interface S3GPsdRawFieldView : NSView {
    void* _plugin;
    int _dragSlider;
    int _openMenu;
    int _hoverMenuItem;
    int _editorPage;
    NSTimer* _timer;
    NSTrackingArea* _trackingArea;
    char _titlePresetName[64];
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)drawRow:(NSString*)name value:(NSString*)value norm:(CGFloat)norm panelX:(CGFloat)panelX panelWidth:(CGFloat)panelWidth y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small;
- (void)drawMenuControl:(NSString*)name value:(NSString*)value panelX:(CGFloat)panelX panelWidth:(CGFloat)panelWidth y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small style:(const s3g::clap_gui::Style&)style;
- (void)drawPerformanceMode:(Plugin*)plugin attrs:(NSDictionary*)attrs small:(NSDictionary*)small style:(const s3g::clap_gui::Style&)style;
- (void)drawEnvelopeEditor:(Plugin*)plugin attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style;
- (void)drawTransportButton:(NSString*)label rect:(NSRect)rect attrs:(NSDictionary*)attrs active:(BOOL)active;
- (void)drawAlgorithmChart:(Plugin*)plugin rect:(NSRect)rect attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style;
- (void)drawWaveforms:(Plugin*)plugin style:(s3g::clap_gui::Style&)style;
- (void)drawOpenMenu:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style;
- (void)chooseRawFile;
- (void)updateMenuHover:(NSPoint)point;
- (void)updateSlider:(NSPoint)point;
@end

@implementation S3GPsdRawFieldView
- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragSlider = -1;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _editorPage = 0;
        _timer = nil;
        _trackingArea = nil;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "CURRENT");
    }
    return self;
}
- (BOOL)isFlipped { return YES; }
- (void)dealloc
{
    [self stopRefreshTimer];
    if (_trackingArea) {
        [self removeTrackingArea:_trackingArea];
        [_trackingArea release];
    }
    [super dealloc];
}
- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    if (_trackingArea) {
        [self removeTrackingArea:_trackingArea];
        [_trackingArea release];
    }
    _trackingArea = [[NSTrackingArea alloc] initWithRect:[self bounds]
        options:(NSTrackingMouseMoved | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect)
        owner:self
        userInfo:nil];
    [self addTrackingArea:_trackingArea];
}
- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [NSTimer timerWithTimeInterval:1.0 / 20.0 target:self selector:@selector(refresh:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}
- (void)stopRefreshTimer { if (_timer) { [_timer invalidate]; _timer = nil; } }
- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    if (![self isHidden] && _plugin && s3g::clap_support::hostAppIsActive()) [self setNeedsDisplay:YES];
}
- (void)drawRow:(NSString*)name value:(NSString*)value norm:(CGFloat)norm panelX:(CGFloat)panelX panelWidth:(CGFloat)panelWidth y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small
{
    s3g::clap_gui::Style style;
    s3g::clap_gui::drawProcessorSlider(
        name, value, norm, y, panelX, panelWidth, attrs, small, style);
}
- (void)drawMenuControl:(NSString*)name value:(NSString*)value panelX:(CGFloat)panelX panelWidth:(CGFloat)panelWidth y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawProcessorMenu(
        name, value, y, panelX, panelWidth, attrs, small, style);
}
- (void)drawPerformanceMode:(Plugin*)plugin attrs:(NSDictionary*)attrs small:(NSDictionary*)small style:(const s3g::clap_gui::Style&)style
{
    [@"MODE" drawAtPoint:NSMakePoint(
        s3g::gui_layout::processorLabelX(kLeftToolboxX), kPerformanceRowY - 5.0)
        withAttributes:attrs];
    const NSRect freeRect = NSMakeRect(140.0, kPerformanceRowY - 10.0, 74.0, 22.0);
    const NSRect midiRect = NSMakeRect(214.0, kPerformanceRowY - 10.0, 74.0, 22.0);
    const bool midi = plugin->performanceMode == PerformanceMode::Midi;
    for (uint32_t item = 0u; item < 2u; ++item) {
        const NSRect rect = item == 0u ? freeRect : midiRect;
        const bool active = item == (midi ? 1u : 0u);
        NSColor* fill = active
            ? [NSColor colorWithCalibratedRed:0.13 green:0.31 blue:0.24 alpha:1.0]
            : [NSColor colorWithCalibratedWhite:0.12 alpha:1.0];
        [fill setFill];
        NSRectFill(rect);
        [(active ? style.text : style.grid) setStroke];
        NSFrameRect(rect);
        NSString* label = item == 0u ? @"FREE" : @"MIDI";
        const NSSize size = [label sizeWithAttributes:small];
        [label drawAtPoint:NSMakePoint(NSMidX(rect) - size.width * 0.5, NSMidY(rect) - size.height * 0.5)
            withAttributes:small];
    }
    char receiveText[16] {};
    s3g::drum_midi::valueToText(
        plugin->midiReceive, receiveText, sizeof(receiveText));
    s3g::clap_gui::drawProcessorMenu(@"MIDI RECEIVE",
        [NSString stringWithUTF8String:receiveText], kPerformanceRowY,
        kMidiReceivePanelX, kMidiReceivePanelWidth,
        attrs, small, style);
    const std::string note = midi ? midiNoteName(plugin->displayNote.load(std::memory_order_relaxed)) : "CONTINUOUS";
    [[NSString stringWithFormat:@"NOTE %s   ·   ENV %.0f%%   ·   FIELD %08X",
        note.c_str(),
        plugin->displayEnvelope.load(std::memory_order_relaxed) * 100.0f,
        plugin->params.seed]
        drawAtPoint:NSMakePoint(
            s3g::gui_layout::processorLabelX(kLeftToolboxX),
            kPerformanceRowY + 25.0)
        withAttributes:small];
}
- (void)drawEnvelopeEditor:(Plugin*)plugin attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    const EnvelopeGraphGeometry graph = envelopeGraphGeometry(*plugin);
    [style.strip setFill];
    NSRectFill(graph.frame);
    [style.grid setStroke];
    NSFrameRect(graph.frame);

    NSBezierPath* grid = [NSBezierPath bezierPath];
    for (uint32_t division = 1u; division < 4u; ++division) {
        const CGFloat y = graph.top
            + (graph.bottom - graph.top) * static_cast<CGFloat>(division) * 0.25;
        [grid moveToPoint:NSMakePoint(NSMinX(graph.frame) + 1.0, y)];
        [grid lineToPoint:NSMakePoint(NSMaxX(graph.frame) - 1.0, y)];
    }
    [grid setLineWidth:0.5];
    [grid stroke];

    NSBezierPath* area = [NSBezierPath bezierPath];
    [area moveToPoint:graph.start];
    [area lineToPoint:graph.attack];
    [area lineToPoint:graph.decay];
    [area lineToPoint:graph.sustain];
    [area lineToPoint:graph.release];
    [area closePath];
    [[style.fill colorWithAlphaComponent:0.18] setFill];
    [area fill];

    NSBezierPath* envelope = [NSBezierPath bezierPath];
    [envelope moveToPoint:graph.start];
    [envelope lineToPoint:graph.attack];
    [envelope lineToPoint:graph.decay];
    [envelope lineToPoint:graph.sustain];
    [envelope lineToPoint:graph.release];
    [style.text setStroke];
    [envelope setLineWidth:1.5];
    [envelope stroke];

    const bool midi = plugin->performanceMode == PerformanceMode::Midi;
    const float level = std::clamp(plugin->displayEnvelope.load(std::memory_order_relaxed), 0.0f, 1.0f);
    if (midi && level > 0.0001f) {
        const CGFloat y = graph.bottom - static_cast<CGFloat>(level) * (graph.bottom - graph.top);
        CGFloat dash[] = { 3.0, 4.0 };
        NSBezierPath* levelLine = [NSBezierPath bezierPath];
        [levelLine moveToPoint:NSMakePoint(NSMinX(graph.frame) + 1.0, y)];
        [levelLine lineToPoint:NSMakePoint(NSMaxX(graph.frame) - 1.0, y)];
        [levelLine setLineDash:dash count:2 phase:0.0];
        [[NSColor colorWithCalibratedRed:0.34 green:0.78 blue:0.55 alpha:0.72] setStroke];
        [levelLine setLineWidth:1.0];
        [levelLine stroke];
    }

    const auto displayedStage = static_cast<EnvelopeStage>(
        plugin->displayEnvelopeStage.load(std::memory_order_relaxed));
    auto drawHandle = [&](NSPoint point, int dragId, EnvelopeStage stage) {
        const bool active = _dragSlider == dragId || (midi && displayedStage == stage);
        NSBezierPath* handle = [NSBezierPath bezierPathWithOvalInRect:
            NSMakeRect(point.x - 5.0, point.y - 5.0, 10.0, 10.0)];
        NSColor* fill = active
            ? [NSColor colorWithCalibratedRed:0.19 green:0.55 blue:0.36 alpha:1.0]
            : style.cellBg;
        [fill setFill];
        [handle fill];
        [(active ? style.text : style.dim) setStroke];
        [handle setLineWidth:1.0];
        [handle stroke];
    };
    drawHandle(graph.attack, 201, EnvelopeStage::Attack);
    drawHandle(graph.decay, 202, EnvelopeStage::Decay);
    drawHandle(graph.sustain, 203, EnvelopeStage::Sustain);
    drawHandle(graph.release, 204, EnvelopeStage::Release);

    const std::string attackText = compactEnvelopeTimeText(plugin->attackMs);
    const std::string decayText = compactEnvelopeTimeText(plugin->decayMs);
    const std::string releaseText = compactEnvelopeTimeText(plugin->releaseMs);
    [[NSString stringWithFormat:@"A  %s", attackText.c_str()]
        drawAtPoint:NSMakePoint(kEnvelopeX + 14.0, 678.0) withAttributes:attrs];
    [[NSString stringWithFormat:@"D  %s", decayText.c_str()]
        drawAtPoint:NSMakePoint(kEnvelopeX + 200.0, 678.0) withAttributes:attrs];
    [[NSString stringWithFormat:@"S  %.0f%%", plugin->sustain * 100.0f]
        drawAtPoint:NSMakePoint(kEnvelopeX + 390.0, 678.0) withAttributes:attrs];
    [[NSString stringWithFormat:@"R  %s", releaseText.c_str()]
        drawAtPoint:NSMakePoint(kEnvelopeX + 575.0, 678.0) withAttributes:attrs];
}
- (void)drawButton:(NSString*)label rect:(NSRect)rect attrs:(NSDictionary*)attrs
{
    [[NSColor colorWithCalibratedWhite:0.18 alpha:1.0] setFill];
    NSRectFill(rect);
    [[NSColor colorWithCalibratedWhite:0.72 alpha:1.0] setStroke];
    NSFrameRect(rect);
    const NSSize size = [label sizeWithAttributes:attrs];
    [label drawAtPoint:NSMakePoint(NSMidX(rect) - size.width * 0.5, NSMidY(rect) - size.height * 0.5) withAttributes:attrs];
}
- (void)drawTransportButton:(NSString*)label rect:(NSRect)rect attrs:(NSDictionary*)attrs active:(BOOL)active
{
    NSColor* fill = [NSColor colorWithCalibratedWhite:0.18 alpha:1.0];
    if (active) {
        fill = [label isEqualToString:@"STOP"]
            ? [NSColor colorWithCalibratedRed:0.42 green:0.17 blue:0.15 alpha:1.0]
            : [NSColor colorWithCalibratedRed:0.12 green:0.34 blue:0.25 alpha:1.0];
    }
    [fill setFill];
    NSRectFill(rect);
    [[NSColor colorWithCalibratedWhite:(active ? 0.90 : 0.58) alpha:1.0] setStroke];
    NSFrameRect(rect);
    const NSSize size = [label sizeWithAttributes:attrs];
    [label drawAtPoint:NSMakePoint(NSMidX(rect) - size.width * 0.5, NSMidY(rect) - size.height * 0.5) withAttributes:attrs];
}
- (void)drawAlgorithmChart:(Plugin*)plugin rect:(NSRect)rect attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    [style.strip setFill];
    NSRectFill(rect);
    [style.grid setStroke];
    NSFrameRect(rect);

    auto node = [&](CGFloat cx, CGFloat cy, NSString* label, bool output = false) {
        const NSRect box = NSMakeRect(cx - 29.0, cy - 13.0, 58.0, 26.0);
        NSColor* fill = output
            ? [NSColor colorWithCalibratedRed:0.12 green:0.34 blue:0.25 alpha:1.0]
            : [NSColor colorWithCalibratedRed:0.12 green:0.22 blue:0.29 alpha:1.0];
        [fill setFill];
        NSRectFill(box);
        [style.dim setStroke];
        NSFrameRect(box);
        const NSSize size = [label sizeWithAttributes:attrs];
        [label drawAtPoint:NSMakePoint(NSMidX(box) - size.width * 0.5,
            NSMidY(box) - size.height * 0.5) withAttributes:attrs];
    };
    auto arrow = [&](NSPoint from, NSPoint to) {
        const CGFloat angle = std::atan2(to.y - from.y, to.x - from.x);
        const CGFloat trim = 31.0;
        from.x += std::cos(angle) * trim;
        from.y += std::sin(angle) * trim;
        to.x -= std::cos(angle) * trim;
        to.y -= std::sin(angle) * trim;
        NSBezierPath* path = [NSBezierPath bezierPath];
        [path moveToPoint:from];
        [path lineToPoint:to];
        [style.text setStroke];
        [path setLineWidth:1.2];
        [path stroke];
        NSBezierPath* head = [NSBezierPath bezierPath];
        [head moveToPoint:to];
        [head lineToPoint:NSMakePoint(to.x - std::cos(angle - 0.55) * 7.0,
            to.y - std::sin(angle - 0.55) * 7.0)];
        [head lineToPoint:NSMakePoint(to.x - std::cos(angle + 0.55) * 7.0,
            to.y - std::sin(angle + 0.55) * 7.0)];
        [head closePath];
        [style.text setFill];
        [head fill];
    };
    auto operatorLabel = [&](uint32_t index, s3g::PsdRawFieldModTarget target) {
        return [NSString stringWithFormat:@"M%u:%s", index, modTargetShortName(target)];
    };

    const CGFloat left = NSMinX(rect);
    const CGFloat top = NSMinY(rect);
    const CGFloat width = NSWidth(rect);
    const CGFloat height = NSHeight(rect);
    auto point = [&](CGFloat nx, CGFloat ny) {
        return NSMakePoint(left + nx * width, top + ny * height);
    };
    const auto algorithm = plugin->params.modAlgorithm;
    NSString* m1 = operatorLabel(1u, plugin->params.modTarget);
    NSString* m2 = operatorLabel(2u, plugin->params.modTarget2);
    NSString* m3 = operatorLabel(3u, plugin->params.modTarget3);

    if (algorithm == s3g::PsdRawFieldModAlgorithm::Relay
        || algorithm == s3g::PsdRawFieldModAlgorithm::Transcode
        || algorithm == s3g::PsdRawFieldModAlgorithm::Regenerator) {
        const bool transcode = algorithm == s3g::PsdRawFieldModAlgorithm::Transcode;
        const bool regenerate = algorithm == s3g::PsdRawFieldModAlgorithm::Regenerator;
        const NSPoint a = point(0.10, 0.54);
        const NSPoint b = point(0.31, 0.54);
        const NSPoint c = point(0.52, 0.54);
        const NSPoint d = point(0.73, 0.54);
        const NSPoint e = point(0.91, 0.54);
        node(a.x, a.y, transcode ? @"DATA" : (regenerate ? @"CODEC" : m3), regenerate || transcode);
        node(b.x, b.y, transcode ? m1 : (regenerate ? m3 : m2));
        node(c.x, c.y, transcode ? m2 : (regenerate ? m2 : m1));
        node(d.x, d.y, transcode ? m3 : (regenerate ? m1 : @"DESTS"), !transcode);
        node(e.x, e.y, @"CODEC", true);
        arrow(a, b); arrow(b, c); arrow(c, d); arrow(d, e);
        if (regenerate) {
            [[NSString stringWithUTF8String:"↺"] drawAtPoint:NSMakePoint(
                NSMidX(rect) - 5.0, NSMaxY(rect) - 27.0) withAttributes:attrs];
        }
    } else if (algorithm == s3g::PsdRawFieldModAlgorithm::CrossedMachines) {
        const NSPoint a = point(0.24, 0.28);
        const NSPoint b = point(0.24, 0.76);
        const NSPoint c = point(0.56, 0.52);
        const NSPoint out = point(0.86, 0.52);
        node(a.x, a.y, m1); node(b.x, b.y, m2); node(c.x, c.y, m3);
        node(out.x, out.y, @"DESTS", true);
        arrow(a, b); arrow(b, c); arrow(c, a); arrow(c, out);
    } else {
        const bool multiplex = algorithm == s3g::PsdRawFieldModAlgorithm::Multiplex;
        const NSPoint a = point(0.18, 0.25);
        const NSPoint b = point(0.18, 0.52);
        const NSPoint c = point(0.18, 0.79);
        const NSPoint bus = point(0.58, 0.52);
        const NSPoint out = point(0.86, 0.52);
        node(a.x, a.y, m1); node(b.x, b.y, m2); node(c.x, c.y, m3);
        node(bus.x, bus.y, multiplex ? @"MUX" : @"BUS");
        node(out.x, out.y, @"DESTS", true);
        arrow(a, bus); arrow(b, bus); arrow(c, bus); arrow(bus, out);
    }
    if (plugin->params.modulationEnabled == 0u) {
        [[NSColor colorWithCalibratedWhite:0.04 alpha:0.68] setFill];
        NSRectFillUsingOperation(rect, NSCompositingOperationSourceOver);
        NSString* bypassed = @"MODULATION BYPASSED";
        const NSSize size = [bypassed sizeWithAttributes:attrs];
        [bypassed drawAtPoint:NSMakePoint(NSMidX(rect) - size.width * 0.5,
            NSMidY(rect) - size.height * 0.5) withAttributes:attrs];
    }
}
- (void)drawWaveforms:(Plugin*)plugin style:(s3g::clap_gui::Style&)style
{
    const CGFloat x = 18.0;
    const CGFloat y = 42.0;
    const CGFloat w = kGuiWidth - 36.0;
    const CGFloat h = 170.0;
    s3g::clap_gui::drawPanelFrame(x, y, w, h, style);
    NSDictionary* labelAttrs = s3g::clap_gui::softLabelAttrs();
    NSDictionary* small = s3g::clap_gui::softValueAttrs();
    [@"8 CHANNEL OUTPUT" drawAtPoint:NSMakePoint(
        x + s3g::gui_layout::kStandardMetrics.headerLabelInset, y + 7.0)
        withAttributes:labelAttrs];
    NSMutableParagraphStyle* paragraph = [[[NSMutableParagraphStyle alloc] init] autorelease];
    [paragraph setLineBreakMode:NSLineBreakByTruncatingMiddle];
    NSMutableDictionary* sourceAttrs = [NSMutableDictionary dictionaryWithDictionary:small];
    [sourceAttrs setObject:paragraph forKey:NSParagraphStyleAttributeName];
    const std::string sourceStatus = sourceStatusText(*plugin);
    [[NSString stringWithUTF8String:sourceStatus.c_str()] drawInRect:NSMakeRect(x + 148.0, y + 7.0, 820.0, 18.0)
        withAttributes:sourceAttrs];
    const bool playing = plugin->playing.load(std::memory_order_relaxed);
    [self drawTransportButton:@"PLAY" rect:NSMakeRect(1000.0, y + 7.0, 46.0, 18.0) attrs:small active:playing];
    [self drawTransportButton:@"STOP" rect:NSMakeRect(1054.0, y + 7.0, 48.0, 18.0) attrs:small active:!playing];
    [self drawButton:@"OPEN ANY" rect:NSMakeRect(1110.0, y + 7.0, 104.0, 18.0) attrs:small];
    [self drawButton:@"GEN FIELD" rect:NSMakeRect(1224.0, y + 7.0, 108.0, 18.0) attrs:small];

    const CGFloat originX = x + 12.0;
    const CGFloat originY = y + 30.0;
    const CGFloat traceW = w - 24.0;
    const CGFloat traceH = (h - 44.0) / static_cast<CGFloat>(kOutputChannels);
    const uint32_t write = plugin->waveWrite.load(std::memory_order_relaxed);
    for (uint32_t ch = 0; ch < kOutputChannels; ++ch) {
        const CGFloat top = originY + static_cast<CGFloat>(ch) * traceH;
        const CGFloat mid = top + traceH * 0.5;
        [[NSColor colorWithCalibratedWhite:0.13 alpha:1.0] setFill];
        NSRectFill(NSMakeRect(originX, top + 1.0, traceW, traceH - 2.0));
        [[NSColor colorWithCalibratedWhite:0.25 alpha:1.0] setStroke];
        NSBezierPath* center = [NSBezierPath bezierPath];
        [center moveToPoint:NSMakePoint(originX, mid)];
        [center lineToPoint:NSMakePoint(originX + traceW, mid)];
        [center setLineWidth:0.5];
        [center stroke];

        const CGFloat hue = 0.53 + static_cast<CGFloat>(ch) * 0.045;
        [[NSColor colorWithCalibratedHue:hue saturation:0.55 brightness:0.92 alpha:0.96] setStroke];
        NSBezierPath* path = [NSBezierPath bezierPath];
        float peak = 0.0001f;
        for (uint32_t i = 0; i < kWaveHistory; ++i) {
            peak = std::max(peak, std::abs(plugin->waveHistory[ch][(write + i) & (kWaveHistory - 1u)]));
        }
        const float displayGain = std::min(24.0f, 0.92f / peak);
        for (uint32_t i = 0; i < kWaveHistory; ++i) {
            const uint32_t index = (write + i) & (kWaveHistory - 1u);
            const float sample = std::clamp(plugin->waveHistory[ch][index] * displayGain, -1.0f, 1.0f);
            const CGFloat px = originX + static_cast<CGFloat>(i) * traceW / static_cast<CGFloat>(kWaveHistory - 1u);
            const CGFloat py = mid - static_cast<CGFloat>(sample) * traceH * 0.43;
            if (i == 0u) [path moveToPoint:NSMakePoint(px, py)];
            else [path lineToPoint:NSMakePoint(px, py)];
        }
        [path setLineWidth:1.0];
        [path stroke];
        [[NSString stringWithFormat:@"%u", ch + 1u] drawAtPoint:NSMakePoint(originX + 4.0, top + 2.0) withAttributes:small];
    }
}
- (void)chooseRawFile
{
    auto* p = static_cast<Plugin*>(_plugin);
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setCanChooseFiles:YES];
    [panel setCanChooseDirectories:NO];
    [panel setAllowsMultipleSelection:NO];
    [panel setResolvesAliases:YES];
    [panel setTitle:@"Open Any Source"];
    [panel setPrompt:@"Open Any"];
    NSView* accessory = [[[NSView alloc] initWithFrame:NSMakeRect(0, 0, 310, 28)] autorelease];
    NSButton* waveformButton = [[[NSButton alloc] initWithFrame:NSMakeRect(0, 2, 310, 22)] autorelease];
    [waveformButton setButtonType:NSButtonTypeSwitch];
    [waveformButton setTitle:@"Decode WAVE data into eight lanes"];
    [waveformButton setState:NSControlStateValueOn];
    [accessory addSubview:waveformButton];
    [panel setAccessoryView:accessory];
    if ([panel runModal] != NSModalResponseOK) return;
    NSString* pathString = [[[panel URLs] firstObject] path];
    if (!pathString) return;
    const char* filePath = [pathString fileSystemRepresentation];
    if (!filePath) return;

    std::shared_ptr<const s3g::PsdRawFieldSource> source;
    std::string error;
    SourceInterpretation interpretation = [waveformButton state] == NSControlStateValueOn
        ? SourceInterpretation::Waveform
        : SourceInterpretation::RawBytes;
    bool loaded = readSource(filePath, interpretation, source, error);
    if (!loaded && interpretation == SourceInterpretation::Waveform) {
        interpretation = SourceInterpretation::RawBytes;
        loaded = readSource(filePath, interpretation, source, error);
    }
    if (!loaded) {
        NSAlert* alert = [[[NSAlert alloc] init] autorelease];
        [alert setMessageText:@"Could not open raw source"];
        [alert setInformativeText:[NSString stringWithUTF8String:error.c_str()]];
        [alert runModal];
        return;
    }
    installRawSource(*p, std::move(source), filePath, interpretation, true);
    markHostStateDirty(*p);
    [self setNeedsDisplay:YES];
}
- (void)drawOpenMenu:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    auto* p = static_cast<Plugin*>(_plugin);
    if (_openMenu == 1) {
        NSString* const items[] = {
            @"INIT", @"SUB CLOCK", @"SCAR DRUM", @"FAX BODY", @"GATED BREAKS", @"SYNC METAL",
            @"BAUDOT DRUM", @"DELTA KNOCK", @"ADPCM SUB", @"MORSE BODY", @"SPARK IMPACT",
            @"WIDE FAX BASS", @"WAVE TRACE", @"CUSTOM"
        };
        s3g::clap_gui::drawDropdownMenu(NSMakeRect(kLeftControlX, 315.0, kToolboxMenuWidth, 18.0 * 14.0), 18.0, items, 14,
            static_cast<int>(std::min(p->selectedPreset, kCustomPreset)), _hoverMenuItem, attrs, style);
    } else if (_openMenu == 2) {
        NSString* const items[] = { @"PARALLEL", @"DEINTERLEAVE", @"PLANES", @"SHUFFLED", @"DIVERGENT" };
        s3g::clap_gui::drawDropdownMenu(NSMakeRect(kLeftControlX, 489.0, kToolboxMenuWidth, 18.0 * 5.0), 18.0, items, 5,
            static_cast<int>(p->params.channelScheme), _hoverMenuItem, attrs, style);
    } else if (_openMenu == 3) {
        NSString* const items[] = {
            @"PCM", @"DELTA", @"ADPCM", @"MU-LAW", @"A-LAW", @"CELP", @"DISC", @"CVSD",
            @"SUBBAND", @"LPC", @"TRANSFORM", @"PREDICT", @"MODEM", @"FAX", @"SIGMA 1-BIT", @"HYBRID", @"APT",
            @"HF FAX", @"HELL", @"MORSE", @"SPARK CW", @"BAUDOT RTTY", @"SSTV"
        };
        s3g::clap_gui::drawDropdownMenu(NSMakeRect(kRightControlX, 281.0, kToolboxMenuWidth, 18.0 * kCodecModeCount), 18.0,
            items, kCodecModeCount,
            static_cast<int>(p->params.codecMode), _hoverMenuItem, attrs, style);
    } else if (_openMenu == 4) {
        NSString* const items[] = {
            @"BROADCAST", @"RELAY", @"MULTIPLEX", @"CROSSED MACHINES", @"REGENERATOR", @"TRANSCODE"
        };
        const CGFloat panelX = _editorPage == 0 ? kModToolboxX : kLabChartX;
        const CGFloat panelWidth = _editorPage == 0 ? kModToolboxWidth : kLabChartWidth;
        s3g::clap_gui::drawDropdownMenu(NSMakeRect(
            s3g::gui_layout::processorControlX(panelX), 281.0,
            s3g::gui_layout::processorMenuWidth(panelWidth),
            18.0 * s3g::kPsdRawFieldModAlgorithmCount), 18.0, items, s3g::kPsdRawFieldModAlgorithmCount,
            static_cast<int>(p->params.modAlgorithm), _hoverMenuItem, attrs, style);
    } else if (_openMenu == 5 || _openMenu == 7 || _openMenu == 9) {
        NSString* const items[] = {
            @"OFF", @"SINE", @"TRIANGLE", @"NOISE", @"FIELD", @"SYNC", @"GATE", @"APT",
            @"HF FAX", @"HELL", @"MORSE", @"SPARK CW", @"BAUDOT RTTY", @"SSTV", @"FEEDBACK"
        };
        const uint32_t index = static_cast<uint32_t>((_openMenu - 5) / 2);
        const CGFloat cardX = labCardX(index);
        const int selected = _openMenu == 5 ? static_cast<int>(p->params.modSource)
            : (_openMenu == 7 ? static_cast<int>(p->params.modSource2)
                              : static_cast<int>(p->params.modSource3));
        s3g::clap_gui::drawDropdownMenu(NSMakeRect(
            s3g::gui_layout::processorControlX(cardX), kLabSourceRowY + 14.0,
            s3g::gui_layout::processorMenuWidth(kLabCardWidth),
            18.0 * s3g::kPsdRawFieldModSourceCount), 18.0, items, s3g::kPsdRawFieldModSourceCount,
            selected, _hoverMenuItem, attrs, style);
    } else if (_openMenu == 6 || _openMenu == 8 || _openMenu == 10) {
        NSString* const items[] = {
            @"CARRIER", @"DEVIATION", @"CLOCK", @"DATA", @"DAMAGE", @"OFF",
            @"BODY", @"RING", @"STRIKE", @"FOLD", @"SCAN"
        };
        const uint32_t index = static_cast<uint32_t>((_openMenu - 6) / 2);
        const CGFloat cardX = labCardX(index);
        const int selected = _openMenu == 6 ? static_cast<int>(p->params.modTarget)
            : (_openMenu == 8 ? static_cast<int>(p->params.modTarget2)
                              : static_cast<int>(p->params.modTarget3));
        s3g::clap_gui::drawDropdownMenu(NSMakeRect(
            s3g::gui_layout::processorControlX(cardX), kLabTargetRowY + 14.0,
            s3g::gui_layout::processorMenuWidth(kLabCardWidth),
            18.0 * s3g::kPsdRawFieldModTargetCount), 18.0, items, s3g::kPsdRawFieldModTargetCount,
            selected, _hoverMenuItem, attrs, style);
    } else if (_openMenu >= 11 && _openMenu <= 13) {
        NSString* const items[] = { @"FIXED", @"ADSR" };
        const uint32_t index = static_cast<uint32_t>(_openMenu - 11);
        const uint32_t selected[] = {
            p->params.modEnvelope1, p->params.modEnvelope2, p->params.modEnvelope3
        };
        const CGFloat cardX = labCardX(index);
        s3g::clap_gui::drawDropdownMenu(NSMakeRect(
            s3g::gui_layout::processorControlX(cardX), kLabEnvelopeRowY + 14.0,
            s3g::gui_layout::processorMenuWidth(kLabCardWidth), 36.0),
            18.0, items, 2u, static_cast<int>(selected[index]), _hoverMenuItem, attrs, style);
    } else if (_openMenu == 14) {
        NSString* const items[] = { @"DIRECT", @"DEMOD", @"DIVIDE", @"ERROR" };
        s3g::clap_gui::drawDropdownMenu(NSMakeRect(
            s3g::gui_layout::processorControlX(kBassReceiverPanelX), 281.0,
            s3g::gui_layout::processorMenuWidth(kBassReceiverPanelWidth), 72.0),
            18.0, items, 4u, static_cast<int>(p->params.bassReceiver),
            _hoverMenuItem, attrs, style);
    } else if (_openMenu == 15) {
        NSString* const items[] = { @"SCAN", @"BODY", @"BODY + SCAN" };
        s3g::clap_gui::drawDropdownMenu(NSMakeRect(
            s3g::gui_layout::processorControlX(kBassReceiverPanelX), 307.0,
            s3g::gui_layout::processorMenuWidth(kBassReceiverPanelWidth), 54.0),
            18.0, items, 3u, static_cast<int>(p->params.bassPitchTracking),
            _hoverMenuItem, attrs, style);
    } else if (_openMenu == 16) {
        NSString* const items[] = { @"-2 OCT", @"-1 OCT", @"0 OCT" };
        s3g::clap_gui::drawDropdownMenu(NSMakeRect(
            s3g::gui_layout::processorControlX(kBassReceiverPanelX), 333.0,
            s3g::gui_layout::processorMenuWidth(kBassReceiverPanelWidth), 54.0),
            18.0, items, 3u, static_cast<int>(p->params.bassOctave),
            _hoverMenuItem, attrs, style);
    } else if (_openMenu == 17) {
        NSString* items[17] {};
        for (uint32_t index = 0u; index < 17u; ++index) {
            char text[16] {};
            s3g::drum_midi::valueToText(
                static_cast<double>(index), text, sizeof(text));
            items[index] = [NSString stringWithUTF8String:text];
        }
        s3g::clap_gui::drawDropdownMenu(midiReceiveDropdownRect(), 18.0,
            items, 17u,
            s3g::drum_midi::receiveChannel(p->midiReceive),
            _hoverMenuItem, attrs, style);
    } else if (_openMenu == 18) {
        NSString* const items[] = {
            @"8CH DIRECT", @"QUAD RING", @"STEREO RING"
        };
        s3g::clap_gui::drawDropdownMenu(outputFormatDropdownRect(), 18.0,
            items, 3u, static_cast<int>(p->outputFormat),
            _hoverMenuItem, attrs, style);
    }
}
- (void)updateMenuHover:(NSPoint)point
{
    NSRect rect = NSZeroRect;
    uint32_t count = 0u;
    if (_openMenu == 1) { rect = NSMakeRect(kLeftControlX, 315.0, kToolboxMenuWidth, 18.0 * 14.0); count = 14u; }
    else if (_openMenu == 2) { rect = NSMakeRect(kLeftControlX, 489.0, kToolboxMenuWidth, 18.0 * 5.0); count = 5u; }
    else if (_openMenu == 3) {
        rect = NSMakeRect(kRightControlX, 281.0, kToolboxMenuWidth, 18.0 * kCodecModeCount);
        count = kCodecModeCount;
    }
    else if (_openMenu == 4) {
        const CGFloat panelX = _editorPage == 0 ? kModToolboxX : kLabChartX;
        const CGFloat panelWidth = _editorPage == 0 ? kModToolboxWidth : kLabChartWidth;
        rect = NSMakeRect(s3g::gui_layout::processorControlX(panelX), 281.0,
            s3g::gui_layout::processorMenuWidth(panelWidth),
            18.0 * s3g::kPsdRawFieldModAlgorithmCount);
        count = s3g::kPsdRawFieldModAlgorithmCount;
    }
    else if (_openMenu == 5 || _openMenu == 7 || _openMenu == 9) {
        const CGFloat cardX = labCardX(static_cast<uint32_t>((_openMenu - 5) / 2));
        rect = NSMakeRect(s3g::gui_layout::processorControlX(cardX), kLabSourceRowY + 14.0,
            s3g::gui_layout::processorMenuWidth(kLabCardWidth),
            18.0 * s3g::kPsdRawFieldModSourceCount);
        count = s3g::kPsdRawFieldModSourceCount;
    }
    else if (_openMenu == 6 || _openMenu == 8 || _openMenu == 10) {
        const CGFloat cardX = labCardX(static_cast<uint32_t>((_openMenu - 6) / 2));
        rect = NSMakeRect(s3g::gui_layout::processorControlX(cardX), kLabTargetRowY + 14.0,
            s3g::gui_layout::processorMenuWidth(kLabCardWidth),
            18.0 * s3g::kPsdRawFieldModTargetCount);
        count = s3g::kPsdRawFieldModTargetCount;
    }
    else if (_openMenu >= 11 && _openMenu <= 13) {
        const CGFloat cardX = labCardX(static_cast<uint32_t>(_openMenu - 11));
        rect = NSMakeRect(s3g::gui_layout::processorControlX(cardX), kLabEnvelopeRowY + 14.0,
            s3g::gui_layout::processorMenuWidth(kLabCardWidth), 36.0);
        count = 2u;
    }
    else if (_openMenu == 14) {
        rect = NSMakeRect(s3g::gui_layout::processorControlX(kBassReceiverPanelX),
            281.0, s3g::gui_layout::processorMenuWidth(kBassReceiverPanelWidth), 72.0);
        count = 4u;
    }
    else if (_openMenu == 15) {
        rect = NSMakeRect(s3g::gui_layout::processorControlX(kBassReceiverPanelX),
            307.0, s3g::gui_layout::processorMenuWidth(kBassReceiverPanelWidth), 54.0);
        count = 3u;
    }
    else if (_openMenu == 16) {
        rect = NSMakeRect(s3g::gui_layout::processorControlX(kBassReceiverPanelX),
            333.0, s3g::gui_layout::processorMenuWidth(kBassReceiverPanelWidth), 54.0);
        count = 3u;
    }
    else if (_openMenu == 17) {
        rect = midiReceiveDropdownRect();
        count = 17u;
    }
    else if (_openMenu == 18) {
        rect = outputFormatDropdownRect();
        count = 3u;
    }
    if (count == 0u) return;
    const int next = s3g::clap_gui::dropdownHitIndex(point, rect, 18.0, count);
    if (next != _hoverMenuItem) {
        _hoverMenuItem = next;
        [self setNeedsDisplay:YES];
    }
}
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSDictionary* titleAttrs = s3g::clap_gui::softTitleAttrs();
    NSDictionary* labels = s3g::clap_gui::softLabelAttrs();
    NSDictionary* values = s3g::clap_gui::softValueAttrs();
    s3g::clap_gui::drawProcessorTitleBand(
        @"s3g PROCESSOR FAULT 8CH",
        [NSString stringWithUTF8String:_titlePresetName],
        s3g::clap_gui::peakDbText(
            p->outputPeak.load(std::memory_order_relaxed)),
        s3g::clap_gui::encoderTitleBand(kGuiWidth, kGuiHeight),
        titleAttrs, labels, values, style);
    [self drawWaveforms:p style:style];

    if (_editorPage == 0) {
        s3g::clap_gui::drawPanelFrame(
            kLeftToolboxX, kToolboxTop, kToolboxWidth,
            s3g::gui_layout::toolboxHeightForRows(9), style);
        s3g::clap_gui::drawPanelHeader(@"FIELD / SPACE / OUTPUT", true,
            kLeftToolboxX, kToolboxTop, kToolboxWidth, 21, labels, style);
        s3g::clap_gui::drawPanelFrame(
            kRightToolboxX, kToolboxTop, kToolboxWidth,
            s3g::gui_layout::toolboxHeightForRows(9), style);
        s3g::clap_gui::drawPanelHeader(@"CODEC / SHAPE", true,
            kRightToolboxX, kToolboxTop, kToolboxWidth, 21, labels, style);
        s3g::clap_gui::drawPanelFrame(
            kModToolboxX, kToolboxTop, kModToolboxWidth,
            s3g::gui_layout::toolboxHeightForRows(9), style);
        s3g::clap_gui::drawPanelHeader(@"MODULATION / QUICK VIEW", true,
            kModToolboxX, kToolboxTop, kModToolboxWidth, 21, labels, style);
    } else if (_editorPage == 1) {
        s3g::clap_gui::drawPanelFrame(
            18.0, kToolboxTop, kGuiWidth - 36.0,
            s3g::gui_layout::toolboxHeightForRows(9), style);
        s3g::clap_gui::drawPanelHeader(@"MOD LAB / THREE OPERATORS", true,
            18.0, kToolboxTop, kGuiWidth - 36.0, 21, labels, style);
    } else {
        s3g::clap_gui::drawPanelFrame(
            18.0, kToolboxTop, kGuiWidth - 36.0,
            s3g::gui_layout::toolboxHeightForRows(9), style);
        s3g::clap_gui::drawPanelHeader(@"BASS LAB / CODEC-DRIVEN LOW CORE", true,
            18.0, kToolboxTop, kGuiWidth - 36.0, 21, labels, style);
    }
    auto drawPageTab = [&](NSString* title, NSRect rect, bool active) {
        NSColor* fill = active
            ? [NSColor colorWithCalibratedRed:0.13 green:0.31 blue:0.24 alpha:1.0]
            : [NSColor colorWithCalibratedWhite:0.12 alpha:1.0];
        [fill setFill];
        NSRectFill(rect);
        [(active ? style.text : style.grid) setStroke];
        NSFrameRect(rect);
        const NSSize size = [title sizeWithAttributes:values];
        [title drawAtPoint:NSMakePoint(NSMidX(rect) - size.width * 0.5,
            NSMidY(rect) - size.height * 0.5) withAttributes:values];
    };
    drawPageTab(@"SOUND", NSMakeRect(1066.0, 232.0, 76.0, 17.0), _editorPage == 0);
    drawPageTab(@"BASS LAB", NSMakeRect(1148.0, 232.0, 88.0, 17.0), _editorPage == 2);
    drawPageTab(@"MOD LAB", NSMakeRect(1242.0, 232.0, 88.0, 17.0), _editorPage == 1);
    const bool modulationEnabled = p->params.modulationEnabled != 0u;
    [self drawTransportButton:(modulationEnabled ? @"MOD ON" : @"MOD OFF")
        rect:NSMakeRect(950.0, 232.0, 108.0, 17.0) attrs:values active:modulationEnabled];
    s3g::clap_gui::drawPanelFrame(18, kPatchPanelY, kGuiWidth - 36.0, 188.0, style);
    s3g::clap_gui::drawPanelHeader(@"PATCH / PERFORMANCE", true,
        18, kPatchPanelY, kGuiWidth - 36.0, 21, labels, style);

    const auto& params = p->params;
    const std::string scanText = p->rawSource && p->rawSource->waveform
        ? waveformSpeedText(params.scanRate)
        : rateText(scanBytesPerSecond(params.scanRate, p->sampleRate), "B/s");
    const std::string codecRateText = rateText(codecUpdatesPerSecond(params.codecRate, p->sampleRate), "Hz");
    const std::string carrierText = carrierTuneText(params.carrierTune, static_cast<uint32_t>(params.codecMode));
    const std::string fieldEvolveText = evolveText(params.evolve);
    if (_editorPage == 0) {
    [self drawRow:@"OUT" value:[NSString stringWithFormat:@"%+.1f dB", params.gainDb] norm:normalizedParam(params, kGainParamId) panelX:kLeftToolboxX panelWidth:kToolboxWidth y:266 attrs:labels small:values];
    [self drawRow:@"SCAN" value:[NSString stringWithUTF8String:scanText.c_str()] norm:params.scanRate panelX:kLeftToolboxX panelWidth:kToolboxWidth y:292 attrs:labels small:values];
    [self drawRow:@"TEXTURE" value:[NSString stringWithFormat:@"%.0f%%", params.texture * 100.0f] norm:params.texture panelX:kLeftToolboxX panelWidth:kToolboxWidth y:318 attrs:labels small:values];
    [self drawRow:@"GEOMETRY" value:[NSString stringWithFormat:@"%.0f%%", params.geometry * 100.0f] norm:params.geometry panelX:kLeftToolboxX panelWidth:kToolboxWidth y:344 attrs:labels small:values];
    [self drawRow:@"CHAOS" value:[NSString stringWithFormat:@"%.0f%%", params.chaos * 100.0f] norm:params.chaos panelX:kLeftToolboxX panelWidth:kToolboxWidth y:370 attrs:labels small:values];
    [self drawRow:@"FOLD" value:[NSString stringWithFormat:@"%.0f%%", params.fold * 100.0f] norm:params.fold panelX:kLeftToolboxX panelWidth:kToolboxWidth y:396 attrs:labels small:values];
    [self drawRow:@"EVOLVE" value:[NSString stringWithUTF8String:fieldEvolveText.c_str()] norm:params.evolve panelX:kLeftToolboxX panelWidth:kToolboxWidth y:422 attrs:labels small:values];
    [self drawMenuControl:@"ROUTE" value:[NSString stringWithUTF8String:channelSchemeName(static_cast<uint32_t>(params.channelScheme))] panelX:kLeftToolboxX panelWidth:kToolboxWidth y:448 attrs:labels small:values style:style];
    [self drawRow:@"SPREAD" value:[NSString stringWithFormat:@"%.0f%%", params.channelSpread * 100.0f] norm:params.channelSpread panelX:kLeftToolboxX panelWidth:kToolboxWidth y:474 attrs:labels small:values];

    [self drawMenuControl:@"CODEC" value:[NSString stringWithUTF8String:codecModeName(static_cast<uint32_t>(params.codecMode))] panelX:kRightToolboxX panelWidth:kToolboxWidth y:266 attrs:labels small:values style:style];
    [self drawRow:@"RATE" value:[NSString stringWithUTF8String:codecRateText.c_str()] norm:params.codecRate panelX:kRightToolboxX panelWidth:kToolboxWidth y:292 attrs:labels small:values];
    [self drawRow:@"BITS" value:[NSString stringWithFormat:@"%.0f", params.bitDepth] norm:normalizedParam(params, kBitDepthParamId) panelX:kRightToolboxX panelWidth:kToolboxWidth y:318 attrs:labels small:values];
    [self drawRow:@"CARRIER" value:[NSString stringWithUTF8String:carrierText.c_str()] norm:normalizedParam(params, kCarrierTuneParamId) panelX:kRightToolboxX panelWidth:kToolboxWidth y:344 attrs:labels small:values];
    [self drawRow:@"DAMAGE" value:[NSString stringWithFormat:@"%.0f%%", params.codecDamage * 100.0f] norm:params.codecDamage panelX:kRightToolboxX panelWidth:kToolboxWidth y:370 attrs:labels small:values];
    [self drawRow:@"DRIVE" value:[NSString stringWithFormat:@"%.0f%%", params.drive * 100.0f] norm:params.drive panelX:kRightToolboxX panelWidth:kToolboxWidth y:396 attrs:labels small:values];
    [self drawRow:@"SHRED" value:[NSString stringWithFormat:@"%.0f%%", params.shred * 100.0f] norm:params.shred panelX:kRightToolboxX panelWidth:kToolboxWidth y:422 attrs:labels small:values];
    [self drawRow:@"RESONANCE" value:[NSString stringWithFormat:@"%.0f%%", params.resonance * 100.0f] norm:params.resonance panelX:kRightToolboxX panelWidth:kToolboxWidth y:448 attrs:labels small:values];
    [self drawRow:@"BASS CORE" value:[NSString stringWithFormat:@"%.0f%%", params.bassBody * 100.0f] norm:params.bassBody panelX:kRightToolboxX panelWidth:kToolboxWidth y:474 attrs:labels small:values];

    [self drawMenuControl:@"CURATED CIRCUIT" value:[NSString stringWithUTF8String:modAlgorithmName(static_cast<uint32_t>(params.modAlgorithm))] panelX:kModToolboxX panelWidth:kModToolboxWidth y:266 attrs:labels small:values style:style];
    [self drawAlgorithmChart:p rect:NSMakeRect(kModToolboxX + 12.0, 292.0,
        kModToolboxWidth - 24.0, 124.0) attrs:values style:style];
    auto drawSummary = [&](uint32_t number, s3g::PsdRawFieldModSource source,
                           s3g::PsdRawFieldModTarget target, uint32_t envelope, CGFloat y) {
        NSString* summary = [NSString stringWithFormat:@"M%u   %s  →  %s   ·   %s",
            number, modSourceName(static_cast<uint32_t>(source)),
            modTargetName(static_cast<uint32_t>(target)), modEnvelopeName(envelope)];
        [summary drawAtPoint:NSMakePoint(kModToolboxX + 18.0, y) withAttributes:values];
    };
    drawSummary(1u, params.modSource, params.modTarget, params.modEnvelope1, 428.0);
    drawSummary(2u, params.modSource2, params.modTarget2, params.modEnvelope2, 450.0);
    drawSummary(3u, params.modSource3, params.modTarget3, params.modEnvelope3, 472.0);
    [@"MOD LAB: RATE · RATIO · INDEX · FEEDBACK · CLOCK · ENVELOPE"
        drawAtPoint:NSMakePoint(kModToolboxX + 18.0, 492.0) withAttributes:labels];
    } else if (_editorPage == 1) {
        [self drawMenuControl:@"ALGORITHM" value:[NSString stringWithUTF8String:modAlgorithmName(static_cast<uint32_t>(params.modAlgorithm))]
            panelX:kLabChartX panelWidth:kLabChartWidth y:266 attrs:labels small:values style:style];
        [self drawAlgorithmChart:p rect:NSMakeRect(kLabChartX, 294.0, kLabChartWidth, 190.0)
            attrs:values style:style];
        auto drawLabOperator = [&](uint32_t number,
                                   s3g::PsdRawFieldModSource source,
                                   s3g::PsdRawFieldModTarget target,
                                   float rate, float ratio, float index, float feedback,
                                   uint32_t clock, uint32_t envelope,
                                   clap_id ratioId, clap_id feedbackId) {
            const CGFloat cardX = labCardX(number - 1u);
            s3g::clap_gui::drawPanelFrame(cardX, 258.0, kLabCardWidth, 234.0, style);
            [style.strip setFill];
            NSRectFill(NSMakeRect(cardX + 1.0, 259.0, kLabCardWidth - 2.0, 18.0));
            NSString* title = [NSString stringWithFormat:@"M%u OPERATOR", number];
            const NSSize titleSize = [title sizeWithAttributes:labels];
            [title drawAtPoint:NSMakePoint(cardX + (kLabCardWidth - titleSize.width) * 0.5, 263.0)
                withAttributes:labels];
            [self drawMenuControl:@"SOURCE" value:[NSString stringWithUTF8String:modSourceName(static_cast<uint32_t>(source))]
                panelX:cardX panelWidth:kLabCardWidth y:kLabSourceRowY attrs:labels small:values style:style];
            [self drawMenuControl:@"DEST" value:[NSString stringWithUTF8String:modTargetName(static_cast<uint32_t>(target))]
                panelX:cardX panelWidth:kLabCardWidth y:kLabTargetRowY attrs:labels small:values style:style];
            const std::string rateValue = rateText(modulatorRateHz(rate), "Hz");
            [self drawRow:@"RATE" value:[NSString stringWithUTF8String:rateValue.c_str()] norm:rate
                panelX:cardX panelWidth:kLabCardWidth y:kLabRateRowY attrs:labels small:values];
            [self drawRow:@"RATIO" value:[NSString stringWithFormat:@"%.2fx", ratio] norm:normalizedParam(params, ratioId)
                panelX:cardX panelWidth:kLabCardWidth y:kLabRatioRowY attrs:labels small:values];
            [self drawRow:@"INDEX" value:[NSString stringWithFormat:@"%.0f%%", index * 100.0f] norm:index
                panelX:cardX panelWidth:kLabCardWidth y:kLabIndexRowY attrs:labels small:values];
            [self drawRow:@"FEEDBACK" value:[NSString stringWithFormat:@"%.0f%%", feedback * (100.0f / 0.98f)] norm:normalizedParam(params, feedbackId)
                panelX:cardX panelWidth:kLabCardWidth y:kLabFeedbackRowY attrs:labels small:values];
            [self drawMenuControl:@"CLOCK" value:[NSString stringWithUTF8String:modClockName(clock)]
                panelX:cardX panelWidth:kLabCardWidth y:kLabClockRowY attrs:labels small:values style:style];
            [self drawMenuControl:@"ENVELOPE" value:[NSString stringWithUTF8String:modEnvelopeName(envelope)]
                panelX:cardX panelWidth:kLabCardWidth y:kLabEnvelopeRowY attrs:labels small:values style:style];
        };
        drawLabOperator(1u, params.modSource, params.modTarget, params.modRate, params.modRatio,
            params.modIndex, params.modFeedback, params.modClockLock, params.modEnvelope1,
            kModRatioParamId, kModFeedbackParamId);
        drawLabOperator(2u, params.modSource2, params.modTarget2, params.modRate2, params.modRatio2,
            params.modIndex2, params.modFeedback2, params.modClockLock2, params.modEnvelope2,
            kModRatio2ParamId, kModFeedback2ParamId);
        drawLabOperator(3u, params.modSource3, params.modTarget3, params.modRate3, params.modRatio3,
            params.modIndex3, params.modFeedback3, params.modClockLock3, params.modEnvelope3,
            kModRatio3ParamId, kModFeedback3ParamId);
    } else {
        s3g::clap_gui::drawPanelFrame(kBassReceiverPanelX, 258.0,
            kBassReceiverPanelWidth, 234.0, style);
        s3g::clap_gui::drawPanelHeader(@"RECEIVER / PITCH", true,
            kBassReceiverPanelX, 258.0, kBassReceiverPanelWidth, 21, labels, style);
        [self drawMenuControl:@"RECEIVER" value:[NSString stringWithUTF8String:bassReceiverName(
            static_cast<uint32_t>(params.bassReceiver))]
            panelX:kBassReceiverPanelX panelWidth:kBassReceiverPanelWidth y:286
            attrs:labels small:values style:style];
        [self drawMenuControl:@"KEY TRACK" value:[NSString stringWithUTF8String:bassPitchTrackingName(
            static_cast<uint32_t>(params.bassPitchTracking))]
            panelX:kBassReceiverPanelX panelWidth:kBassReceiverPanelWidth y:312
            attrs:labels small:values style:style];
        [self drawMenuControl:@"OCTAVE" value:[NSString stringWithUTF8String:bassOctaveName(
            static_cast<uint32_t>(params.bassOctave))]
            panelX:kBassReceiverPanelX panelWidth:kBassReceiverPanelWidth y:338
            attrs:labels small:values style:style];
        [@"DIRECT   codec waveform clocks the divider"
            drawAtPoint:NSMakePoint(kBassReceiverPanelX + 18.0, 378.0) withAttributes:values];
        [@"DEMOD    envelope weights divided crossings"
            drawAtPoint:NSMakePoint(kBassReceiverPanelX + 18.0, 400.0) withAttributes:values];
        [@"DIVIDE   carrier crossings generate subharmonics"
            drawAtPoint:NSMakePoint(kBassReceiverPanelX + 18.0, 422.0) withAttributes:values];
        [@"ERROR    damage gates divided codec crossings"
            drawAtPoint:NSMakePoint(kBassReceiverPanelX + 18.0, 444.0) withAttributes:values];

        s3g::clap_gui::drawPanelFrame(kBassControlPanelX, 258.0,
            kBassControlPanelWidth, 234.0, style);
        s3g::clap_gui::drawPanelHeader(@"SUBHARMONIC / HIGH GAIN", true,
            kBassControlPanelX, 258.0, kBassControlPanelWidth, 21, labels, style);
        [self drawRow:@"BODY" value:[NSString stringWithFormat:@"%.0f%%", params.bassBody * 100.0f]
            norm:params.bassBody panelX:kBassControlPanelX panelWidth:kBassControlPanelWidth y:286 attrs:labels small:values];
        [self drawRow:@"EXCITE" value:[NSString stringWithFormat:@"%.0f%%", params.bassPunch * 100.0f]
            norm:params.bassPunch panelX:kBassControlPanelX panelWidth:kBassControlPanelWidth y:312 attrs:labels small:values];
        [self drawRow:@"TRACE" value:[NSString stringWithFormat:@"%.0f%%", params.bassTrace * 100.0f]
            norm:params.bassTrace panelX:kBassControlPanelX panelWidth:kBassControlPanelWidth y:338 attrs:labels small:values];
        [self drawRow:@"FUZZ" value:[NSString stringWithFormat:@"%.0f%%", params.bassFuzz * 100.0f]
            norm:params.bassFuzz panelX:kBassControlPanelX panelWidth:kBassControlPanelWidth y:364 attrs:labels small:values];
        [self drawRow:@"METAL" value:[NSString stringWithFormat:@"%.0f%%", params.bassMetal * 100.0f]
            norm:params.bassMetal panelX:kBassControlPanelX panelWidth:kBassControlPanelWidth y:390 attrs:labels small:values];
        [self drawRow:@"FEEDBACK" value:[NSString stringWithFormat:@"%.0f%%", params.bassFeedback * 100.0f]
            norm:params.bassFeedback panelX:kBassControlPanelX panelWidth:kBassControlPanelWidth y:416 attrs:labels small:values];
        [self drawRow:@"GLIDE" value:[NSString stringWithFormat:@"%.0f%%", params.bassGlide * 100.0f]
            norm:params.bassGlide panelX:kBassControlPanelX panelWidth:kBassControlPanelWidth y:442 attrs:labels small:values];
        [self drawRow:@"LOW WIDTH" value:[NSString stringWithFormat:@"%.0f%%", params.bassLowWidth * 100.0f]
            norm:params.bassLowWidth panelX:kBassControlPanelX panelWidth:kBassControlPanelWidth y:468 attrs:labels small:values];

        s3g::clap_gui::drawPanelFrame(kBassPathPanelX, 258.0,
            kBassPathPanelWidth, 234.0, style);
        s3g::clap_gui::drawPanelHeader(@"RECONSTRUCTION PATH", true,
            kBassPathPanelX, 258.0, kBassPathPanelWidth, 21, labels, style);
        [@"CODEC / MODULATION  →  RECEIVER  →  DIVIDER"
            drawAtPoint:NSMakePoint(kBassPathPanelX + 24.0, 300.0) withAttributes:values];
        [@"MIDI ROOT  →  GLIDE / OCTAVE  →  MODAL FILTER"
            drawAtPoint:NSMakePoint(kBassPathPanelX + 24.0, 326.0) withAttributes:values];
        [@"LOW ANCHOR  ───────────────────────────┐"
            drawAtPoint:NSMakePoint(kBassPathPanelX + 24.0, 352.0) withAttributes:values];
        [@"UPPER BODY  →  FUZZ  →  METAL  →  FEEDBACK"
            drawAtPoint:NSMakePoint(kBassPathPanelX + 24.0, 378.0) withAttributes:values];
        [@"                                  ↓"
            drawAtPoint:NSMakePoint(kBassPathPanelX + 24.0, 404.0) withAttributes:values];
        [@"TRACE MIX  →  LOW WIDTH  →  8CH OUTPUT"
            drawAtPoint:NSMakePoint(kBassPathPanelX + 24.0, 430.0) withAttributes:values];
        [@"The clean low anchor remains outside the feedback loop."
            drawAtPoint:NSMakePoint(kBassPathPanelX + 24.0, 462.0) withAttributes:labels];
    }

    [self drawMenuControl:@"PRESET" value:[NSString stringWithUTF8String:presetName(p->selectedPreset)] panelX:kLeftToolboxX panelWidth:kToolboxWidth y:kPresetRowY attrs:labels small:values style:style];
    [self drawButton:@"CURATED RANDOM" rect:NSMakeRect(466, kPresetRowY - 7.0, 126, 24) attrs:values];
    [self drawButton:@"MUTATE" rect:NSMakeRect(604, kPresetRowY - 7.0, 90, 24) attrs:values];
    [self drawButton:@"UNDO" rect:NSMakeRect(706, kPresetRowY - 7.0, 90, 24) attrs:values];
    [self drawPerformanceMode:p attrs:labels small:values style:style];
    [self drawEnvelopeEditor:p attrs:values style:style];
    [self drawMenuControl:@"FORMAT"
        value:[NSString stringWithUTF8String:outputFormatName(p->outputFormat)]
        panelX:kOutputPanelX panelWidth:kOutputPanelWidth y:kOutputFormatRowY
        attrs:labels small:values style:style];
    [self drawRow:@"ROTATE"
        value:[NSString stringWithFormat:@"%+.1f deg", p->outputRotationDeg]
        norm:normalizedPerformanceParam(*p, kOutputRotationParamId)
        panelX:kOutputPanelX panelWidth:kOutputPanelWidth y:kOutputRotationRowY
        attrs:labels small:values];
    [self drawOpenMenu:values style:style];
}
- (void)updateSlider:(NSPoint)point
{
    auto* p = static_cast<Plugin*>(_plugin);
    if (_dragSlider >= 1 && _dragSlider <= 8) {
        static const clap_id ids[] = {
            kScanRateParamId, kTextureParamId, kGeometryParamId, kChaosParamId,
            kFoldParamId, kEvolveParamId, kChannelSpreadParamId, kGainParamId
        };
        const double normalized = std::clamp(
            (point.x - kLeftControlX) / kToolboxTrackWidth, 0.0, 1.0);
        applyNormalizedParam(*p, ids[_dragSlider - 1], normalized);
    } else if (_dragSlider >= 101 && _dragSlider <= 108) {
        static const clap_id ids[] = {
            kCodecRateParamId, kBitDepthParamId, kCarrierTuneParamId, kCodecDamageParamId,
            kDriveParamId, kShredParamId, kResonanceParamId, kBassBodyParamId
        };
        const double normalized = std::clamp(
            (point.x - kRightControlX) / kToolboxTrackWidth, 0.0, 1.0);
        applyNormalizedParam(*p, ids[_dragSlider - 101], normalized);
    } else if (_dragSlider >= 301 && _dragSlider <= 304) {
        static const clap_id ids[] = {
            kModRateParamId, kModRatioParamId, kModIndexParamId, kModFeedbackParamId
        };
        const double normalized = std::clamp(
            (point.x - s3g::gui_layout::processorControlX(labCardX(0u)))
                / s3g::gui_layout::processorTrackWidth(kLabCardWidth), 0.0, 1.0);
        applyNormalizedParam(*p, ids[_dragSlider - 301], normalized);
    } else if (_dragSlider >= 401 && _dragSlider <= 404) {
        static const clap_id ids[] = {
            kModRate2ParamId, kModRatio2ParamId, kModIndex2ParamId, kModFeedback2ParamId
        };
        const double normalized = std::clamp(
            (point.x - s3g::gui_layout::processorControlX(labCardX(1u)))
                / s3g::gui_layout::processorTrackWidth(kLabCardWidth), 0.0, 1.0);
        applyNormalizedParam(*p, ids[_dragSlider - 401], normalized);
    } else if (_dragSlider >= 501 && _dragSlider <= 504) {
        static const clap_id ids[] = {
            kModRate3ParamId, kModRatio3ParamId, kModIndex3ParamId, kModFeedback3ParamId
        };
        const double normalized = std::clamp(
            (point.x - s3g::gui_layout::processorControlX(labCardX(2u)))
                / s3g::gui_layout::processorTrackWidth(kLabCardWidth), 0.0, 1.0);
        applyNormalizedParam(*p, ids[_dragSlider - 501], normalized);
    } else if (_dragSlider >= 601 && _dragSlider <= 608) {
        static const clap_id ids[] = {
            kBassBodyParamId, kBassPunchParamId, kBassTraceParamId,
            kBassFuzzParamId, kBassMetalParamId, kBassFeedbackParamId,
            kBassGlideParamId, kBassLowWidthParamId
        };
        const double normalized = std::clamp(
            (point.x - s3g::gui_layout::processorControlX(kBassControlPanelX))
                / s3g::gui_layout::processorTrackWidth(kBassControlPanelWidth), 0.0, 1.0);
        applyNormalizedParam(*p, ids[_dragSlider - 601], normalized);
    } else if (_dragSlider == 701) {
        const double normalized = std::clamp(
            (point.x - s3g::gui_layout::processorControlX(kOutputPanelX))
                / s3g::gui_layout::processorTrackWidth(kOutputPanelWidth),
            0.0, 1.0);
        applyNormalizedParam(*p, kOutputRotationParamId, normalized);
    } else if (_dragSlider >= 201 && _dragSlider <= 204) {
        const EnvelopeGraphGeometry graph = envelopeGraphGeometry(*p);
        const double sustain = std::clamp(
            static_cast<double>((graph.bottom - point.y) / (graph.bottom - graph.top)), 0.0, 1.0);
        if (_dragSlider == 201) {
            applyNormalizedParam(*p, kAttackParamId,
                std::clamp(static_cast<double>((point.x - (kEnvelopeX + 54.0)) / 126.0), 0.0, 1.0));
        } else if (_dragSlider == 202) {
            applyNormalizedParam(*p, kDecayParamId,
                std::clamp(static_cast<double>((point.x - (kEnvelopeX + 220.0)) / 140.0), 0.0, 1.0));
            applyNormalizedParam(*p, kSustainParamId, sustain);
        } else if (_dragSlider == 203) {
            applyNormalizedParam(*p, kSustainParamId, sustain);
        } else {
            applyNormalizedParam(*p, kReleaseParamId,
                std::clamp(static_cast<double>((point.x - (kEnvelopeX + 520.0)) / 160.0), 0.0, 1.0));
        }
        markHostStateDirty(*p);
    }
    [self setNeedsDisplay:YES];
}
- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    auto* p = static_cast<Plugin*>(_plugin);
    const auto titleBand = s3g::clap_gui::encoderTitleBand(kGuiWidth, kGuiHeight);
    if (s3g::clap_gui::handleProcessorTitleClick(
            point, &p->plugin, @"Processor Fault", titleBand,
            _titlePresetName, sizeof(_titlePresetName), kGainParamId)) {
        [self setNeedsDisplay:YES];
        return;
    }
    if (_openMenu != 0) {
        NSRect rect = NSZeroRect;
        uint32_t count = 0u;
        clap_id id = CLAP_INVALID_ID;
        if (_openMenu == 1) { rect = NSMakeRect(kLeftControlX, 315.0, kToolboxMenuWidth, 18.0 * 14.0); count = 14u; id = kPresetParamId; }
        else if (_openMenu == 2) { rect = NSMakeRect(kLeftControlX, 489.0, kToolboxMenuWidth, 18.0 * 5.0); count = 5u; id = kChannelSchemeParamId; }
        else if (_openMenu == 3) {
            rect = NSMakeRect(kRightControlX, 281.0, kToolboxMenuWidth, 18.0 * kCodecModeCount);
            count = kCodecModeCount;
            id = kCodecModeParamId;
        }
        else if (_openMenu == 4) {
            const CGFloat panelX = _editorPage == 0 ? kModToolboxX : kLabChartX;
            const CGFloat panelWidth = _editorPage == 0 ? kModToolboxWidth : kLabChartWidth;
            rect = NSMakeRect(s3g::gui_layout::processorControlX(panelX), 281.0,
                s3g::gui_layout::processorMenuWidth(panelWidth),
                18.0 * s3g::kPsdRawFieldModAlgorithmCount);
            count = s3g::kPsdRawFieldModAlgorithmCount;
            id = kModAlgorithmParamId;
        }
        else if (_openMenu == 5 || _openMenu == 7 || _openMenu == 9) {
            const CGFloat cardX = labCardX(static_cast<uint32_t>((_openMenu - 5) / 2));
            rect = NSMakeRect(s3g::gui_layout::processorControlX(cardX), kLabSourceRowY + 14.0,
                s3g::gui_layout::processorMenuWidth(kLabCardWidth),
                18.0 * s3g::kPsdRawFieldModSourceCount);
            count = s3g::kPsdRawFieldModSourceCount;
            id = _openMenu == 5 ? kModSourceParamId
                : (_openMenu == 7 ? kModSource2ParamId : kModSource3ParamId);
        }
        else if (_openMenu == 6 || _openMenu == 8 || _openMenu == 10) {
            const CGFloat cardX = labCardX(static_cast<uint32_t>((_openMenu - 6) / 2));
            rect = NSMakeRect(s3g::gui_layout::processorControlX(cardX), kLabTargetRowY + 14.0,
                s3g::gui_layout::processorMenuWidth(kLabCardWidth),
                18.0 * s3g::kPsdRawFieldModTargetCount);
            count = s3g::kPsdRawFieldModTargetCount;
            id = _openMenu == 6 ? kModTargetParamId
                : (_openMenu == 8 ? kModTarget2ParamId : kModTarget3ParamId);
        }
        else if (_openMenu >= 11 && _openMenu <= 13) {
            const uint32_t index = static_cast<uint32_t>(_openMenu - 11);
            const CGFloat cardX = labCardX(index);
            rect = NSMakeRect(s3g::gui_layout::processorControlX(cardX), kLabEnvelopeRowY + 14.0,
                s3g::gui_layout::processorMenuWidth(kLabCardWidth), 36.0);
            count = 2u;
            constexpr clap_id ids[] = {
                kModEnvelope1ParamId, kModEnvelope2ParamId, kModEnvelope3ParamId
            };
            id = ids[index];
        }
        else if (_openMenu == 14) {
            rect = NSMakeRect(s3g::gui_layout::processorControlX(kBassReceiverPanelX),
                281.0, s3g::gui_layout::processorMenuWidth(kBassReceiverPanelWidth), 72.0);
            count = 4u;
            id = kBassReceiverParamId;
        }
        else if (_openMenu == 15) {
            rect = NSMakeRect(s3g::gui_layout::processorControlX(kBassReceiverPanelX),
                307.0, s3g::gui_layout::processorMenuWidth(kBassReceiverPanelWidth), 54.0);
            count = 3u;
            id = kBassPitchTrackingParamId;
        }
        else if (_openMenu == 16) {
            rect = NSMakeRect(s3g::gui_layout::processorControlX(kBassReceiverPanelX),
                333.0, s3g::gui_layout::processorMenuWidth(kBassReceiverPanelWidth), 54.0);
            count = 3u;
            id = kBassOctaveParamId;
        }
        else if (_openMenu == 17) {
            rect = midiReceiveDropdownRect();
            count = 17u;
            id = kMidiReceiveParamId;
        }
        else if (_openMenu == 18) {
            rect = outputFormatDropdownRect();
            count = 3u;
            id = kOutputFormatParamId;
        }
        const int hit = s3g::clap_gui::dropdownHitIndex(point, rect, 18.0, count);
        if (hit >= 0 && id != CLAP_INVALID_ID) {
            if (id == kModAlgorithmParamId && _editorPage == 0)
                applyCuratedAlgorithm(*p, static_cast<uint32_t>(hit));
            else
                applyParam(*p, id, hit);
            markHostStateDirty(*p);
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(1066.0, 232.0, 76.0, 17.0))) {
        _editorPage = 0;
        _dragSlider = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(1148.0, 232.0, 88.0, 17.0))) {
        _editorPage = 2;
        _dragSlider = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(1242.0, 232.0, 88.0, 17.0))) {
        _editorPage = 1;
        _dragSlider = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(950.0, 232.0, 108.0, 17.0))) {
        applyParam(*p, kModulationEnabledParamId,
            p->params.modulationEnabled != 0u ? 0.0 : 1.0);
        markHostStateDirty(*p);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(kLeftControlX, kPresetRowY - 1.0, kToolboxMenuWidth, 16))) { _openMenu = 1; [self setNeedsDisplay:YES]; return; }
    if (_editorPage == 0) {
        if (NSPointInRect(point, NSMakeRect(kLeftControlX, 447.0, kToolboxMenuWidth, 18))) { _openMenu = 2; [self setNeedsDisplay:YES]; return; }
        if (NSPointInRect(point, NSMakeRect(kRightControlX, 265.0, kToolboxMenuWidth, 16))) { _openMenu = 3; [self setNeedsDisplay:YES]; return; }
        if (NSPointInRect(point, NSMakeRect(kModControlX, 265.0, kModToolboxMenuWidth, 18))) { _openMenu = 4; [self setNeedsDisplay:YES]; return; }
    } else if (_editorPage == 1) {
        const CGFloat algorithmX = s3g::gui_layout::processorControlX(kLabChartX);
        const CGFloat algorithmWidth = s3g::gui_layout::processorMenuWidth(kLabChartWidth);
        if (NSPointInRect(point, NSMakeRect(algorithmX, 265.0, algorithmWidth, 18.0))) {
            _openMenu = 4;
            [self setNeedsDisplay:YES];
            return;
        }
        constexpr clap_id clockIds[] = {
            kModClockLockParamId, kModClockLock2ParamId, kModClockLock3ParamId
        };
        for (int operatorIndex = 0; operatorIndex < 3; ++operatorIndex) {
            const CGFloat cardX = labCardX(static_cast<uint32_t>(operatorIndex));
            const CGFloat controlX = s3g::gui_layout::processorControlX(cardX);
            const CGFloat menuWidth = s3g::gui_layout::processorMenuWidth(kLabCardWidth);
            if (NSPointInRect(point, NSMakeRect(controlX, kLabSourceRowY - 1.0, menuWidth, 18.0))) {
                _openMenu = 5 + operatorIndex * 2;
                [self setNeedsDisplay:YES];
                return;
            }
            if (NSPointInRect(point, NSMakeRect(controlX, kLabTargetRowY - 1.0, menuWidth, 18.0))) {
                _openMenu = 6 + operatorIndex * 2;
                [self setNeedsDisplay:YES];
                return;
            }
            if (NSPointInRect(point, NSMakeRect(controlX, kLabClockRowY - 1.0, menuWidth, 18.0))) {
                const uint32_t locks[] = {
                    p->params.modClockLock, p->params.modClockLock2, p->params.modClockLock3
                };
                applyParam(*p, clockIds[operatorIndex], locks[operatorIndex] == 0u ? 1.0 : 0.0);
                markHostStateDirty(*p);
                [self setNeedsDisplay:YES];
                return;
            }
            if (NSPointInRect(point, NSMakeRect(controlX, kLabEnvelopeRowY - 1.0, menuWidth, 18.0))) {
                _openMenu = 11 + operatorIndex;
                [self setNeedsDisplay:YES];
                return;
            }
        }
    } else {
        const CGFloat receiverControlX = s3g::gui_layout::processorControlX(kBassReceiverPanelX);
        const CGFloat receiverMenuWidth = s3g::gui_layout::processorMenuWidth(kBassReceiverPanelWidth);
        if (NSPointInRect(point, NSMakeRect(receiverControlX, 285.0, receiverMenuWidth, 18.0))) {
            _openMenu = 14;
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(point, NSMakeRect(receiverControlX, 311.0, receiverMenuWidth, 18.0))) {
            _openMenu = 15;
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(point, NSMakeRect(receiverControlX, 337.0, receiverMenuWidth, 18.0))) {
            _openMenu = 16;
            [self setNeedsDisplay:YES];
            return;
        }
    }
    if (NSPointInRect(point, NSMakeRect(
            kMidiReceivePanelX + s3g::gui_layout::kStandardMetrics.hitInset,
            kPerformanceRowY - 9.0,
            kMidiReceivePanelWidth
                - s3g::gui_layout::kStandardMetrics.hitInset * 2.0,
            s3g::gui_layout::kStandardMetrics.hitHeight))) {
        _openMenu = 17;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(
            kOutputPanelX + s3g::gui_layout::kStandardMetrics.hitInset,
            kOutputFormatRowY - 9.0,
            kOutputPanelWidth
                - s3g::gui_layout::kStandardMetrics.hitInset * 2.0,
            s3g::gui_layout::kStandardMetrics.hitHeight))) {
        _openMenu = 18;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(
            kOutputPanelX, kOutputRotationRowY - 9.0,
            kOutputPanelWidth, 24.0))) {
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
                event, &p->plugin, kOutputRotationParamId, &defaultValue)) {
            applyParam(*p, kOutputRotationParamId, defaultValue);
            markHostStateDirty(*p);
            _dragSlider = -1;
        } else {
            _dragSlider = 701;
            [self updateSlider:point];
        }
        return;
    }
    if (NSPointInRect(point, NSMakeRect(140, kPerformanceRowY - 10.0, 148, 22))) {
        applyParam(*p, kPerformanceModeParamId,
            point.x < 214.0 ? static_cast<double>(PerformanceMode::Free) : static_cast<double>(PerformanceMode::Midi));
        markHostStateDirty(*p);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(1000, 49, 46, 18))) {
        applyParam(*p, kRunParamId, 1.0);
        markHostStateDirty(*p);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(1054, 49, 48, 18))) {
        applyParam(*p, kRunParamId, 0.0);
        markHostStateDirty(*p);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(1110, 49, 104, 18))) {
        [self chooseRawFile];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(1224, 49, 108, 18))) {
        applyParam(*p, kRandomizeFieldParamId, std::fmod(static_cast<double>(p->params.seed) * 0.61803398875 + 0.37, 1.0));
        markHostStateDirty(*p);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(466, kPresetRowY - 7.0, 126, 24))) {
        applyParam(*p, kRandomizePatchParamId, std::fmod(static_cast<double>(p->params.seed) * 0.754877666 + 0.21, 1.0));
        markHostStateDirty(*p);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(604, kPresetRowY - 7.0, 90, 24))) {
        applyParam(*p, kMutateParamId, std::fmod(static_cast<double>(p->params.seed) * 0.569840291 + 0.43, 1.0));
        markHostStateDirty(*p);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(706, kPresetRowY - 7.0, 90, 24))) {
        applyParam(*p, kUndoParamId, 1.0);
        markHostStateDirty(*p);
        [self setNeedsDisplay:YES];
        return;
    }

    const EnvelopeGraphGeometry graph = envelopeGraphGeometry(*p);
    if (NSPointInRect(point, graph.frame)) {
        const std::array<NSPoint, 4> handles {
            graph.attack, graph.decay, graph.sustain, graph.release
        };
        int selected = -1;
        CGFloat bestDistance = 14.0 * 14.0;
        for (int index = 0; index < static_cast<int>(handles.size()); ++index) {
            const CGFloat distance = squaredDistance(point, handles[index]);
            if (distance <= bestDistance) {
                selected = index;
                bestDistance = distance;
            }
        }
        if (selected < 0) {
            if (point.x < kEnvelopeX + 190.0) selected = 0;
            else if (point.x < kEnvelopeX + 410.0) selected = 1;
            else if (point.x < kEnvelopeX + 500.0) selected = 2;
            else selected = 3;
        }
        _dragSlider = selected + 201;
        [self updateSlider:point];
        return;
    }

    if (_editorPage == 0) {
    static const CGFloat leftRows[] = { 292, 318, 344, 370, 396, 422, 474, 266 };
    static const clap_id leftIds[] = {
        kScanRateParamId, kTextureParamId, kGeometryParamId, kChaosParamId,
        kFoldParamId, kEvolveParamId, kChannelSpreadParamId, kGainParamId
    };
    for (int i = 0; i < 8; ++i) {
        if (NSPointInRect(point, NSMakeRect(
                kLeftToolboxX, leftRows[i] - 9.0, kToolboxWidth, 24))) {
            double defaultValue = 0.0;
            if (s3g::clap_gui::sliderDoubleClickDefault(
                    event, &p->plugin, leftIds[i], &defaultValue)) {
                applyParam(*p, leftIds[i], defaultValue);
                _dragSlider = -1;
            } else {
                _dragSlider = i + 1;
                [self updateSlider:point];
            }
            return;
        }
    }
    static const CGFloat rightRows[] = { 292, 318, 344, 370, 396, 422, 448, 474 };
    static const clap_id rightIds[] = {
        kCodecRateParamId, kBitDepthParamId, kCarrierTuneParamId, kCodecDamageParamId,
        kDriveParamId, kShredParamId, kResonanceParamId, kBassBodyParamId
    };
    for (int i = 0; i < 8; ++i) {
        if (NSPointInRect(point, NSMakeRect(
                kRightToolboxX, rightRows[i] - 9.0, kToolboxWidth, 24))) {
            double defaultValue = 0.0;
            if (s3g::clap_gui::sliderDoubleClickDefault(
                    event, &p->plugin, rightIds[i], &defaultValue)) {
                applyParam(*p, rightIds[i], defaultValue);
                if (rightIds[i] == kBassBodyParamId) markHostStateDirty(*p);
                _dragSlider = -1;
            } else {
                _dragSlider = i + 101;
                [self updateSlider:point];
            }
            return;
        }
    }
    }
    if (_editorPage == 2) {
    static const CGFloat bassRows[] = {
        286, 312, 338, 364, 390, 416, 442, 468
    };
    static const clap_id bassIds[] = {
        kBassBodyParamId, kBassPunchParamId, kBassTraceParamId,
        kBassFuzzParamId, kBassMetalParamId, kBassFeedbackParamId,
        kBassGlideParamId, kBassLowWidthParamId
    };
    for (int i = 0; i < 8; ++i) {
        if (NSPointInRect(point, NSMakeRect(
                kBassControlPanelX, bassRows[i] - 9.0, kBassControlPanelWidth, 24))) {
            double defaultValue = 0.0;
            if (s3g::clap_gui::sliderDoubleClickDefault(
                    event, &p->plugin, bassIds[i], &defaultValue)) {
                applyParam(*p, bassIds[i], defaultValue);
                markHostStateDirty(*p);
                _dragSlider = -1;
            } else {
                _dragSlider = i + 601;
                [self updateSlider:point];
            }
            return;
        }
    }
    }
    if (_editorPage == 1) {
    static const CGFloat modRows[] = { kLabRateRowY, kLabRatioRowY, kLabIndexRowY, kLabFeedbackRowY };
    static const clap_id modIds[] = {
        kModRateParamId, kModRatioParamId, kModIndexParamId, kModFeedbackParamId
    };
    for (int i = 0; i < 4; ++i) {
        if (NSPointInRect(point, NSMakeRect(
                labCardX(0u), modRows[i] - 9.0, kLabCardWidth, 22))) {
            double defaultValue = 0.0;
            if (s3g::clap_gui::sliderDoubleClickDefault(
                    event, &p->plugin, modIds[i], &defaultValue)) {
                applyParam(*p, modIds[i], defaultValue);
                _dragSlider = -1;
            } else {
                _dragSlider = i + 301;
                [self updateSlider:point];
            }
            return;
        }
    }
    static const clap_id mod2Ids[] = {
        kModRate2ParamId, kModRatio2ParamId, kModIndex2ParamId, kModFeedback2ParamId
    };
    for (int i = 0; i < 4; ++i) {
        if (NSPointInRect(point, NSMakeRect(
                labCardX(1u), modRows[i] - 9.0, kLabCardWidth, 22))) {
            double defaultValue = 0.0;
            if (s3g::clap_gui::sliderDoubleClickDefault(
                    event, &p->plugin, mod2Ids[i], &defaultValue)) {
                applyParam(*p, mod2Ids[i], defaultValue);
                _dragSlider = -1;
            } else {
                _dragSlider = i + 401;
                [self updateSlider:point];
            }
            return;
        }
    }
    static const clap_id mod3Ids[] = {
        kModRate3ParamId, kModRatio3ParamId, kModIndex3ParamId, kModFeedback3ParamId
    };
    for (int i = 0; i < 4; ++i) {
        if (NSPointInRect(point, NSMakeRect(
                labCardX(2u), modRows[i] - 9.0, kLabCardWidth, 22))) {
            double defaultValue = 0.0;
            if (s3g::clap_gui::sliderDoubleClickDefault(
                    event, &p->plugin, mod3Ids[i], &defaultValue)) {
                applyParam(*p, mod3Ids[i], defaultValue);
                _dragSlider = -1;
            } else {
                _dragSlider = i + 501;
                [self updateSlider:point];
            }
            return;
        }
    }
    }
}
- (void)mouseDragged:(NSEvent*)event
{
    if (_dragSlider > 0) [self updateSlider:[self convertPoint:[event locationInWindow] fromView:nil]];
}
- (void)mouseMoved:(NSEvent*)event
{
    [self updateMenuHover:[self convertPoint:[event locationInWindow] fromView:nil]];
}
- (void)resetCursorRects
{
    [super resetCursorRects];
    [self addCursorRect:NSMakeRect(kEnvelopeX, kEnvelopeY, kEnvelopeWidth, kEnvelopeHeight)
        cursor:[NSCursor crosshairCursor]];
}
- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    if (_dragSlider == 108 || (_dragSlider >= 601 && _dragSlider <= 608)
        || _dragSlider == 701) {
        markHostStateDirty(*static_cast<Plugin*>(_plugin));
    }
    _dragSlider = -1;
}
@end

namespace {
bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating)
{
    return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating)
{
    if (!api || !isFloating) return false;
    *api = CLAP_WINDOW_API_COCOA;
    *isFloating = false;
    return true;
}
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating)
{
    if (!guiIsApiSupported(plugin, api, isFloating)) return false;
    auto* p = self(plugin);
    if (p->guiView) return true;
    p->guiView = [[S3GPsdRawFieldView alloc] initWithPlugin:p];
    if (!p->guiView) return false;
    if (!s3g::clap_gui::createResponsiveViewport(p->guiViewport,
            static_cast<NSView*>(p->guiView), kGuiWidth, kGuiHeight)) {
        [static_cast<NSView*>(p->guiView) release]; p->guiView = nullptr; return false;
    }
    return true;
}
void guiDestroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (p->guiView) {
        p->guiVisible = false;
        auto* view = static_cast<S3GPsdRawFieldView*>(p->guiView);
        [view stopRefreshTimer];
        s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView);
    }
}
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height)
{
    return s3g::clap_gui::getResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height);
}
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) { return s3g::clap_gui::getResponsiveResizeHints(hints); }
bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height) { return s3g::clap_gui::adjustResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height); }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t width, uint32_t height)
{
    return s3g::clap_gui::setResponsiveViewportSize(self(plugin)->guiViewport, width, height);
}
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    if (!window || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0 || !window->cocoa) return false;
    auto* p = self(plugin);
    return s3g::clap_gui::setResponsiveViewportParent(p->guiViewport,
        static_cast<NSView*>(window->cocoa), p->host);
}
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, false)) return false;
    p->guiVisible = true;
    [static_cast<S3GPsdRawFieldView*>(p->guiView) startRefreshTimer];
    return true;
}
bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    p->guiVisible = false;
    [static_cast<S3GPsdRawFieldView*>(p->guiView) stopRefreshTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true);
}
const clap_plugin_gui_t guiExt {
    guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale,
    guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize,
    guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide
};
#endif

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &notePorts;
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
    CLAP_PLUGIN_FEATURE_SURROUND,
    nullptr
};
const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.fault",
    "s3g Processor Fault 8ch",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.19.0",
    "Eight-channel free-running or MIDI-playable byte geometry, codec damage, and receiver-driven bass synthesis.",
    features
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->params.bassTrace = kInitBassTrace;
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
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory*, uint32_t index)
{
    return index == 0u ? &descriptor : nullptr;
}
const clap_plugin_factory_t factory { factoryGetPluginCount, factoryGetPluginDescriptor, createPlugin };
bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId)
{
    return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr;
}

} // namespace

extern "C" const clap_plugin_entry_t clap_entry { CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory };
