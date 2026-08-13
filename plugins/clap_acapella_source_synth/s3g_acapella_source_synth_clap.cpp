#include "s3g_acapella_source_synth.h"
#include "s3g_acapella_ensemble_synth.h"
#include "s3g_acapella_text_compiler.h"
#include "s3g_acapella_vocal_fx.h"
#include "s3g_math.h"
#include "../common/s3g_clap_gui_param_queue.h"
#include "../common/s3g_clap_state_stream.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/latency.h>
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
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <iterator>
#include <new>

namespace {

constexpr uint32_t kStateVersion = 26u;
constexpr uint32_t kOutputChannels = 2u;
constexpr uint32_t kGuiWidth = 1356u;
constexpr uint32_t kGuiHeight = 968u;
constexpr uint32_t kPhraseCapacity = 256u;

constexpr clap_id kPresetParamId = 1u;
constexpr clap_id kDeliveryParamId = 2u;
constexpr clap_id kVowelParamId = 3u;
constexpr clap_id kOnsetParamId = 4u;
constexpr clap_id kDurationParamId = 5u;
constexpr clap_id kTractParamId = 6u;
constexpr clap_id kBreathParamId = 7u;
constexpr clap_id kRoughnessParamId = 8u;
constexpr clap_id kBrightnessParamId = 9u;
constexpr clap_id kChestParamId = 10u;
constexpr clap_id kNasalParamId = 11u;
constexpr clap_id kOpenQuotientParamId = 12u;
constexpr clap_id kArticulationParamId = 13u;
constexpr clap_id kConsonantParamId = 14u;
constexpr clap_id kIntensityParamId = 15u;
constexpr clap_id kVibratoRateParamId = 16u;
constexpr clap_id kVibratoDepthParamId = 17u;
constexpr clap_id kPitchDriftParamId = 18u;
constexpr clap_id kGlideParamId = 19u;
constexpr clap_id kAttackParamId = 20u;
constexpr clap_id kReleaseParamId = 21u;
constexpr clap_id kScoopParamId = 22u;
constexpr clap_id kDeclinationParamId = 23u;
constexpr clap_id kOutputParamId = 24u;
constexpr clap_id kAuditionParamId = 25u;
constexpr clap_id kHarshnessParamId = 26u;
constexpr clap_id kFalseFoldParamId = 27u;
constexpr clap_id kThroatParamId = 28u;
constexpr clap_id kRetriggerParamId = 29u;
constexpr clap_id kOctaveDownParamId = 30u;
constexpr clap_id kOctaveUpParamId = 31u;
constexpr clap_id kFuzzDriveParamId = 32u;
constexpr clap_id kFuzzMixParamId = 33u;
constexpr clap_id kFuzzToneParamId = 34u;
constexpr clap_id kCompressionParamId = 35u;
constexpr clap_id kDeEssParamId = 36u;
constexpr clap_id kEchoMixParamId = 37u;
constexpr clap_id kEchoTimeParamId = 38u;
constexpr clap_id kWidthParamId = 39u;
constexpr clap_id kPolyphonyParamId = 40u;
constexpr clap_id kDoubleAmountParamId = 41u;
constexpr clap_id kDoubleDetuneParamId = 42u;
constexpr clap_id kDoubleTimingParamId = 43u;
constexpr clap_id kDoubleDirtParamId = 44u;
constexpr clap_id kDoubleWidthParamId = 45u;
constexpr clap_id kParallelCrushParamId = 46u;
constexpr clap_id kHybridBlendParamId = 47u;
constexpr clap_id kOnsetGuardParamId = 48u;
constexpr clap_id kWaveguideBlendParamId = 49u;
constexpr clap_id kCoarticulationParamId = 50u;
constexpr clap_id kGestureSequenceParamId = 51u;
constexpr clap_id kGestureRateParamId = 52u;
constexpr clap_id kGestureDepthParamId = 53u;
constexpr clap_id kGestureLoopParamId = 54u;
constexpr clap_id kGestureSyncParamId = 55u;
constexpr clap_id kGestureDivisionParamId = 56u;
constexpr clap_id kIntelligibilityParamId = 57u;
constexpr clap_id kEchoHeadsParamId = 58u;
constexpr clap_id kEchoClockParamId = 59u;
constexpr clap_id kEchoFeedbackParamId = 60u;
constexpr clap_id kEchoWearParamId = 61u;
constexpr clap_id kEchoFlutterParamId = 62u;
constexpr clap_id kEchoToneParamId = 63u;
constexpr clap_id kEchoSpreadParamId = 64u;
// IDs 65--86 remain stable for host automation compatibility. Version 17
// gives them an entirely new, non-FFT resonant voice-bank meaning.
constexpr clap_id kBankAmountParamId = 65u;
constexpr clap_id kBankModeParamId = 66u;
constexpr clap_id kCarrierShapeParamId = 67u;
constexpr clap_id kCarrierHarmonicsParamId = 68u;
constexpr clap_id kCarrierColorParamId = 69u;
constexpr clap_id kCarrierNoiseParamId = 70u;
constexpr clap_id kAnalysisBlendParamId = 71u;
constexpr clap_id kBankAttackParamId = 72u;
constexpr clap_id kBankReleaseParamId = 73u;
constexpr clap_id kBankResonanceParamId = 74u;
constexpr clap_id kBankDriveParamId = 75u;
constexpr clap_id kBandShiftParamId = 76u;
constexpr clap_id kBandStretchParamId = 77u;
constexpr clap_id kBandTiltParamId = 78u;
constexpr clap_id kSibilanceParamId = 79u;
constexpr clap_id kMatrixModeParamId = 80u;
constexpr clap_id kMatrixMorphParamId = 81u;
constexpr clap_id kBankStereoSpreadParamId = 82u;
constexpr clap_id kBankFreezeParamId = 83u;
constexpr clap_id kFreezeTriggerParamId = 84u;
constexpr clap_id kBankBlurParamId = 85u;
constexpr clap_id kBankGestureFollowParamId = 86u;
// Version 18 grows the resonant bank into the product's primary surface.
// Keep every new ID above the version-17 range so old automation never
// acquires a different meaning.
constexpr clap_id kBandLayoutParamId = 87u;
constexpr clap_id kVoicingModeParamId = 88u;
constexpr clap_id kVoicingThresholdParamId = 89u;
constexpr clap_id kVoicedLevelParamId = 90u;
constexpr clap_id kUnvoicedLevelParamId = 91u;
constexpr clap_id kVoicedTransitionParamId = 92u;
constexpr clap_id kUnvoicedTransitionParamId = 93u;
constexpr clap_id kOpenLevelParamId = 94u;
constexpr clap_id kCouplingParamId = 95u;
constexpr clap_id kArticulationThruParamId = 96u;
constexpr clap_id kStereoModeParamId = 97u;
// Version 19 reverses the dedicated input into the vocoder's modulator. Reuse
// the two retired external-carrier IDs as a coordinated breaking change while
// preserving the remaining expanded matrix surface and stable ID ordering.
constexpr clap_id kModulatorSourceParamId = 98u;
constexpr clap_id kMicGainParamId = 99u;
constexpr clap_id kCarrierPulseWidthParamId = 100u;
constexpr clap_id kCarrierLfoShapeParamId = 101u;
constexpr clap_id kCarrierLfoRateParamId = 102u;
constexpr clap_id kCarrierLfoDepthParamId = 103u;
constexpr clap_id kCarrierPwmDepthParamId = 104u;
constexpr clap_id kCarrierLfoSyncParamId = 105u;
constexpr clap_id kCarrierLfoDivisionParamId = 106u;
constexpr clap_id kCustomMatrixMorphParamId = 107u;
constexpr uint32_t kMatrixBands = 22u;
constexpr uint32_t kMatrixCells = kMatrixBands * kMatrixBands;
constexpr clap_id kBandTrimParamBase = 108u;
constexpr clap_id kMatrixAParamBase = kBandTrimParamBase + kMatrixBands;
constexpr clap_id kMatrixBParamBase = kMatrixAParamBase + kMatrixCells;
// Version 20 appends new controls after every existing matrix cell. Do not
// insert them before 1098: the 22 trims and 968 routing cells are already
// stable host-automation IDs.
constexpr clap_id kAnalysisSlopeParamId = 1098u;
constexpr clap_id kCarrierPitchSourceParamId = 1099u;
constexpr clap_id kPitchScaleRootParamId = 1100u;
constexpr clap_id kPitchScaleParamId = 1101u;
constexpr clap_id kPitchHoldParamId = 1102u;
// Version 23 appends the Mouth Model's fast-bank/LPC balance after every
// established analysis, pitch, trim, and routing automation ID.
constexpr clap_id kMouthFocusParamId = 1103u;
// Version 24 adds the literal speech-transfer path after all established
// routing and analysis IDs. These are deliberately independent controls rather
// than another macro so precision presets remain completely inspectable.
constexpr clap_id kTransferModeParamId = 1104u;
constexpr clap_id kVoiceFocusParamId = 1105u;
constexpr clap_id kAnalysisLevelerParamId = 1106u;
constexpr clap_id kConsonantColorParamId = 1107u;
constexpr clap_id kConsonantSpeedParamId = 1108u;
constexpr clap_id kCarrierDensityParamId = 1109u;
// Version 25 separates analyzer bandwidth from synthesis resonance and adds
// high-frequency residual plus input spectral-dynamics controls.
constexpr clap_id kAnalysisWidthParamId = 1110u;
constexpr clap_id kHfDetailModeParamId = 1111u;
constexpr clap_id kHfDetailLevelParamId = 1112u;
constexpr clap_id kHfDetailCutoffParamId = 1113u;
constexpr clap_id kAnalysisLowEqParamId = 1114u;
constexpr clap_id kAnalysisMidEqParamId = 1115u;
constexpr clap_id kAnalysisAirEqParamId = 1116u;
constexpr clap_id kAnalysisCompressionParamId = 1117u;
constexpr clap_id kAnalysisNoiseRejectParamId = 1118u;
constexpr clap_id kAnalysisSpectralBalanceParamId = 1119u;
constexpr uint32_t kScalarParamCount = 129u;
constexpr uint32_t kParamCount = kAnalysisSpectralBalanceParamId;
constexpr uint32_t kSavedParamCount = kParamCount - 1u;
constexpr double kCustomPreset = 31.0;

struct ParamDef {
    clap_id id;
    const char* name;
    const char* module;
    double minimum;
    double maximum;
    double defaultValue;
    bool stepped;
};

constexpr std::array<ParamDef, kScalarParamCount> kScalarParamDefs {{
    { kPresetParamId, "Matrix Profile", "Formant Matrix", 0.0, 31.0, 14.0, true },
    { kDeliveryParamId, "Phrasing", "Performance", 0.0, 1.0, 0.0, true },
    { kVowelParamId, "Vowel", "Syllable", 0.0, 5.0, 5.0, true },
    { kOnsetParamId, "Onset", "Syllable", 0.0, 24.0, 0.0, true },
    { kDurationParamId, "Prosody Horizon", "Syllable", 35.0, 2000.0, 280.0, false },
    { kTractParamId, "Tract Scale", "Voice", 0.70, 1.35, 1.0, false },
    { kBreathParamId, "Breath", "Voice", 0.0, 1.0, 0.18, false },
    { kRoughnessParamId, "Roughness", "Voice", 0.0, 1.0, 0.08, false },
    { kBrightnessParamId, "Brightness", "Voice", 0.0, 1.0, 0.48, false },
    { kChestParamId, "Chest", "Voice", 0.0, 1.0, 0.14, false },
    { kNasalParamId, "Nasal", "Voice", 0.0, 1.0, 0.08, false },
    { kOpenQuotientParamId, "Open Quotient", "Voice", 0.38, 0.78, 0.56, false },
    { kHarshnessParamId, "Fold Drive", "Source", 0.0, 1.0, 0.0, false },
    { kFalseFoldParamId, "False Folds", "Source", 0.0, 1.0, 0.0, false },
    { kThroatParamId, "Throat Resonance", "Source", 0.0, 1.0, 0.0, false },
    { kArticulationParamId, "Articulation", "Syllable", 0.0, 1.0, 0.72, false },
    { kConsonantParamId, "Consonant", "Syllable", 0.0, 1.0, 0.78, false },
    { kIntensityParamId, "Intensity", "Performance", 0.0, 1.0, 0.78, false },
    { kVibratoRateParamId, "Vibrato Rate", "Pitch", 0.05, 10.0, 5.2, false },
    { kVibratoDepthParamId, "Vibrato Depth", "Pitch", 0.0, 180.0, 24.0, false },
    { kPitchDriftParamId, "Pitch Drift", "Pitch", 0.0, 80.0, 5.0, false },
    { kGlideParamId, "Glide", "Pitch", 0.0, 500.0, 32.0, false },
    { kAttackParamId, "Attack", "Envelope", 0.25, 500.0, 12.0, false },
    { kReleaseParamId, "Release", "Envelope", 2.0, 3000.0, 85.0, false },
    { kRetriggerParamId, "Note Transition", "Envelope", 0.5, 30.0, 6.0, false },
    { kHybridBlendParamId, "Hybrid Blend", "Hybrid Source", 0.0, 1.0, 0.16, false },
    { kOnsetGuardParamId, "Onset Guard", "Hybrid Source", 2.0, 80.0, 14.0, false },
    { kWaveguideBlendParamId, "Waveguide Tract", "Articulation", 0.0, 1.0, 0.48, false },
    { kCoarticulationParamId, "Coarticulation", "Articulation", 0.0, 1.0, 0.68, false },
    { kGestureSequenceParamId, "Phoneme Score", "Phoneme Engine", 0.0, 5.0, 0.0, true },
    { kGestureRateParamId, "Phoneme Rate", "Phoneme Engine", 0.5, 20.0, 5.0, false },
    { kGestureDepthParamId, "Score Depth", "Phoneme Engine", 0.0, 1.0, 1.0, false },
    { kGestureLoopParamId, "Phrase Mode", "Phoneme Engine", 0.0, 1.0, 1.0, true },
    { kGestureSyncParamId, "Phrase Sync", "Phoneme Engine", 0.0, 2.0, 0.0, true },
    { kGestureDivisionParamId, "Phrase Division", "Phoneme Engine", 0.0, 11.0, 5.0, true },
    { kOctaveDownParamId, "Octave Down", "Shape FX", 0.0, 1.0, 0.0, false },
    { kOctaveUpParamId, "Octave Up", "Shape FX", 0.0, 1.0, 0.0, false },
    { kFuzzDriveParamId, "Fuzz Drive", "Shape FX", 0.0, 30.0, 0.0, false },
    { kFuzzMixParamId, "Fuzz Mix", "Shape FX", 0.0, 1.0, 0.0, false },
    { kFuzzToneParamId, "Fuzz Tone", "Shape FX", 700.0, 16000.0, 6500.0, false },
    { kCompressionParamId, "Serial Compression", "Dynamics", 0.0, 1.0, 0.22, false },
    { kParallelCrushParamId, "Parallel Crush", "Dynamics", 0.0, 1.0, 0.0, false },
    { kDeEssParamId, "De-Esser", "Dynamics", 0.0, 1.0, 0.10, false },
    { kEchoMixParamId, "Echo Mix", "Tape Echo", 0.0, 1.0, 0.0, false },
    { kEchoTimeParamId, "Echo Free Time", "Tape Echo", 20.0, 1800.0, 180.0, false },
    { kWidthParamId, "Stereo Width", "Shape FX", 0.0, 1.0, 0.0, false },
    { kPolyphonyParamId, "Polyphony", "Ensemble", 1.0, 8.0, 4.0, true },
    { kDoubleAmountParamId, "Double Amount", "Ensemble", 0.0, 1.0, 0.18, false },
    { kDoubleDetuneParamId, "Double Detune", "Ensemble", 0.0, 30.0, 7.0, false },
    { kDoubleTimingParamId, "Double Timing", "Ensemble", 0.0, 45.0, 18.0, false },
    { kDoubleDirtParamId, "Double Dirt", "Ensemble", 0.0, 1.0, 0.0, false },
    { kDoubleWidthParamId, "Double Width", "Ensemble", 0.0, 1.0, 0.82, false },
    { kScoopParamId, "Pitch Scoop", "Pitch", -4.0, 4.0, 0.45, false },
    { kDeclinationParamId, "Phrase Decline", "Pitch", -4.0, 6.0, 1.15, false },
    { kOutputParamId, "Output Gain", "Output", -24.0, 12.0, -3.0, false },
    { kAuditionParamId, "Audition", "Performance", 0.0, 1.0, 0.0, true },
    { kIntelligibilityParamId, "Definition", "Analysis", 0.0, 1.0, 0.78, false },
    { kEchoHeadsParamId, "Echo Heads", "Tape Echo", 0.0, 6.0, 6.0, true },
    { kEchoClockParamId, "Echo Clock", "Tape Echo", 0.0, 9.0, 5.0, true },
    { kEchoFeedbackParamId, "Echo Feedback", "Tape Echo", 0.0, 0.92, 0.34, false },
    { kEchoWearParamId, "Tape Wear", "Tape Echo", 0.0, 1.0, 0.18, false },
    { kEchoFlutterParamId, "Tape Flutter", "Tape Echo", 0.0, 1.0, 0.10, false },
    { kEchoToneParamId, "Echo Tone", "Tape Echo", -1.0, 1.0, -0.12, false },
    { kEchoSpreadParamId, "Head Spread", "Tape Echo", 0.0, 1.0, 0.58, false },
    { kBankAmountParamId, "Bank Mix", "Filter Bank", 0.0, 1.0, 1.0, false },
    { kBankModeParamId, "Bank Mode", "Filter Bank", 0.0, 2.0, 0.0, true },
    { kCarrierShapeParamId, "Carrier Shape", "Carrier", 0.0, 4.0, 1.0, true },
    { kCarrierHarmonicsParamId, "Carrier Harmonics", "Carrier", 0.0, 1.0, 0.94, false },
    { kCarrierColorParamId, "Carrier Color", "Carrier", -1.0, 1.0, 0.12, false },
    { kCarrierNoiseParamId, "Carrier Noise", "Carrier", 0.0, 1.0, 0.18, false },
    { kAnalysisBlendParamId, "Analysis / Phoneme", "Analysis", 0.0, 1.0, 0.0, false },
    { kBankAttackParamId, "Band Attack", "Analysis", 0.5, 120.0, 2.0, false },
    { kBankReleaseParamId, "Band Release", "Analysis", 5.0, 1500.0, 65.0, false },
    { kBankResonanceParamId, "Bank Resonance", "Resonator", 0.0, 1.0, 0.48, false },
    { kBankDriveParamId, "Bank Drive", "Resonator", 0.0, 24.0, 3.0, false },
    { kBandShiftParamId, "Band Shift", "Band Matrix", -24.0, 24.0, 0.0, false },
    { kBandStretchParamId, "Band Stretch", "Band Matrix", -1.0, 1.0, 0.0, false },
    { kBandTiltParamId, "Band Tilt", "Band Matrix", -1.0, 1.0, 0.0, false },
    { kSibilanceParamId, "Sibilance", "Analysis", 0.0, 1.0, 0.78, false },
    { kMatrixModeParamId, "Matrix Mode", "Band Matrix", 0.0, 5.0, 0.0, true },
    { kMatrixMorphParamId, "Matrix Depth", "Band Matrix", 0.0, 1.0, 1.0, false },
    { kBankStereoSpreadParamId, "Bank Stereo Spread", "Resonator", 0.0, 1.0, 0.0, false },
    { kBankFreezeParamId, "Envelope Freeze", "Gesture", 0.0, 1.0, 0.0, false },
    { kFreezeTriggerParamId, "Freeze Trigger", "Gesture", 0.0, 5.0, 2.0, true },
    { kBankBlurParamId, "Envelope Blur", "Gesture", 0.0, 2000.0, 4.0, false },
    { kBankGestureFollowParamId, "Gesture Follow", "Gesture", 0.0, 1.0, 0.82, false },
    { kBandLayoutParamId, "Band Layout", "Filter Bank", 0.0, 1.0, 0.0, true },
    { kVoicingModeParamId, "Voiced / Unvoiced Mode", "Voiced / Unvoiced", 0.0, 3.0, 3.0, true },
    { kVoicingThresholdParamId, "Voicing Threshold", "Voiced / Unvoiced", 0.0, 1.0, 0.44, false },
    { kVoicedLevelParamId, "Voiced Level", "Voiced / Unvoiced", 0.0, 1.0, 1.0, false },
    { kUnvoicedLevelParamId, "Unvoiced Level", "Voiced / Unvoiced", 0.0, 1.0, 0.82, false },
    { kVoicedTransitionParamId, "To Voiced", "Voiced / Unvoiced", 10.0, 250.0, 38.0, false },
    { kUnvoicedTransitionParamId, "To Unvoiced", "Voiced / Unvoiced", 10.0, 250.0, 16.0, false },
    { kOpenLevelParamId, "Open Level", "Filter Bank", 0.0, 1.0, 0.0, false },
    { kCouplingParamId, "Band Coupling", "Routing", -3.0, 3.0, 0.0, true },
    { kArticulationThruParamId, "Articulation Thru", "Analysis", 0.0, 1.0, 0.0, false },
    { kStereoModeParamId, "Stereo Pattern", "Filter Bank", 0.0, 2.0, 0.0, true },
    { kModulatorSourceParamId, "Modulator Source", "Modulator", 0.0, 2.0, 0.0, true },
    { kMicGainParamId, "Mic Gain", "Modulator", -24.0, 24.0, 0.0, false },
    { kCarrierPulseWidthParamId, "Pulse Width", "Carrier", 0.05, 0.95, 0.50, false },
    { kCarrierLfoShapeParamId, "Carrier LFO Shape", "Carrier LFO", 0.0, 1.0, 0.0, true },
    { kCarrierLfoRateParamId, "Carrier LFO Rate", "Carrier LFO", 0.02, 13.0, 0.22, false },
    { kCarrierLfoDepthParamId, "Carrier FM", "Carrier LFO", 0.0, 24.0, 0.0, false },
    { kCarrierPwmDepthParamId, "Carrier PWM", "Carrier LFO", 0.0, 1.0, 0.0, false },
    { kCarrierLfoSyncParamId, "Carrier LFO Sync", "Carrier LFO", 0.0, 1.0, 0.0, true },
    { kCarrierLfoDivisionParamId, "Carrier LFO Division", "Carrier LFO", 0.0, 11.0, 8.0, true },
    { kCustomMatrixMorphParamId, "Matrix A / B", "Routing", 0.0, 1.0, 0.0, false },
    { kAnalysisSlopeParamId, "Analysis Response", "Analysis", 0.0, 2.0, 1.0, true },
    { kCarrierPitchSourceParamId, "Carrier Pitch Source", "Pitch Tracking", 0.0, 1.0, 0.0, true },
    { kPitchScaleRootParamId, "Scale Root", "Pitch Tracking", 0.0, 11.0, 0.0, true },
    { kPitchScaleParamId, "Pitch Scale", "Pitch Tracking", 0.0,
        static_cast<double>(s3g::kAcapellaResonatorPitchScaleCount - 1u),
        1.0, true },
    { kPitchHoldParamId, "Pitch Hold", "Pitch Tracking", 20.0,
        s3g::kAcapellaResonatorInfinitePitchHoldMs, 350.0, false },
    { kMouthFocusParamId, "Mouth Focus", "Analysis", 0.0, 1.0, 0.80, false },
    { kTransferModeParamId, "Transfer Mode", "Analysis", 0.0, 1.0, 1.0, true },
    { kVoiceFocusParamId, "Voice Focus", "Analysis Input", -1.0, 1.0, 0.28, false },
    { kAnalysisLevelerParamId, "Analysis Leveler", "Analysis Input", 0.0, 1.0, 0.72, false },
    { kConsonantColorParamId, "Consonant Color", "Consonants", -1.0, 1.0, 0.35, false },
    { kConsonantSpeedParamId, "Consonant Speed", "Consonants", 0.0, 1.0, 0.18, false },
    { kCarrierDensityParamId, "Carrier Density", "Carrier", 0.0, 1.0, 0.58, false },
    { kAnalysisWidthParamId, "Analysis Width", "Analysis", 0.0, 1.0, 0.68, false },
    { kHfDetailModeParamId, "HF Detail Mode", "HF Detail", 0.0, 2.0, 1.0, true },
    { kHfDetailLevelParamId, "HF Detail Level", "HF Detail", 0.0, 1.0, 0.16, false },
    { kHfDetailCutoffParamId, "HF Detail Cutoff", "HF Detail", 2200.0, 9000.0, 4200.0, false },
    { kAnalysisLowEqParamId, "Analysis Low EQ", "Analysis Input", -12.0, 12.0, -1.5, false },
    { kAnalysisMidEqParamId, "Analysis Mid EQ", "Analysis Input", -12.0, 12.0, 2.0, false },
    { kAnalysisAirEqParamId, "Analysis Air EQ", "Analysis Input", -12.0, 12.0, 1.5, false },
    { kAnalysisCompressionParamId, "Analysis Compression", "Analysis Input", 0.0, 1.0, 0.42, false },
    { kAnalysisNoiseRejectParamId, "Analysis Noise Reject", "Analysis Input", 0.0, 1.0, 0.46, false },
    { kAnalysisSpectralBalanceParamId, "Analysis Spectral Balance", "Analysis Input", 0.0, 1.0, 0.32, false },
}};

const std::array<ParamDef, kParamCount> kParamDefs = [] {
    std::array<ParamDef, kParamCount> result {};
    for (const auto& def : kScalarParamDefs) {
        result[def.id - 1u] = def;
    }
    for (uint32_t band = 0u; band < kMatrixBands; ++band) {
        result[kBandTrimParamBase - 1u + band] = {
            kBandTrimParamBase + band, "Band Trim", "Band Levels",
            0.0, 2.0, 1.0, false,
        };
    }
    for (uint32_t destination = 0u; destination < kMatrixBands;
         ++destination) {
        for (uint32_t source = 0u; source < kMatrixBands; ++source) {
            const uint32_t cell = destination * kMatrixBands + source;
            const double identity = destination == source ? 1.0 : 0.0;
            result[kMatrixAParamBase - 1u + cell] = {
                kMatrixAParamBase + cell, "Route A", "Routing A",
                -1.0, 1.0, identity, false,
            };
            result[kMatrixBParamBase - 1u + cell] = {
                kMatrixBParamBase + cell, "Route B", "Routing B",
                -1.0, 1.0, identity, false,
            };
        }
    }
    return result;
}();

struct StateHeader {
    uint32_t version = kStateVersion;
    uint32_t reserved = 0u;
};

constexpr std::array<clap_id, kSavedParamCount> makeSavedParamIds()
{
    std::array<clap_id, kSavedParamCount> result {};
    uint32_t index = 0u;
    for (clap_id id = 1u; id <= kParamCount; ++id) {
        if (id != kAuditionParamId) result[index++] = id;
    }
    return result;
}

constexpr auto kSavedParamIds = makeSavedParamIds();

constexpr std::array<float, 12u> kCarrierLfoDivisionBeats {{
    0.125f, 1.0f / 6.0f, 0.25f, 0.375f,
    1.0f / 3.0f, 0.5f, 0.75f, 2.0f / 3.0f,
    1.0f, 1.5f, 2.0f, 4.0f,
}};

float carrierLfoDivisionBeats(double menuValue)
{
    const uint32_t index = std::min<uint32_t>(
        static_cast<uint32_t>(std::lround(menuValue)),
        static_cast<uint32_t>(kCarrierLfoDivisionBeats.size() - 1u));
    return kCarrierLfoDivisionBeats[index];
}

uint32_t carrierLfoDivisionIndex(float beats)
{
    uint32_t best = 0u;
    float bestDistance = std::numeric_limits<float>::max();
    for (uint32_t index = 0u; index < kCarrierLfoDivisionBeats.size();
         ++index) {
        const float distance = std::abs(beats - kCarrierLfoDivisionBeats[index]);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = index;
        }
    }
    return best;
}

struct PhraseState {
    uint32_t length = 0u;
    std::array<char, kPhraseCapacity> text {};
};

struct TextProgramMessage {
    s3g::AcapellaGestureProgram program {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    const clap_host_tail_t* hostTail = nullptr;
    double sampleRate = 48000.0;
    s3g::AcapellaEnsembleSynth ensemble {};
    s3g::AcapellaVocalEffects effects {};
    s3g::AcapellaSourceParams audioParams {};
    s3g::AcapellaEnsembleParams ensembleParams {};
    s3g::AcapellaVocalFxParams effectsParams {};
    s3g::AcapellaVowel vowel = s3g::AcapellaVowel::Schwa;
    s3g::AcapellaOnset onset = s3g::AcapellaOnset::None;
    float durationMs = 280.0f;
    float outputGain = s3g::dbToGain(-3.0f);
    float smoothedOutputGain = outputGain;
    bool audioAuditionGate = false;
    bool auditionVoice = false;
    std::atomic<bool> controlAuditionGate { false };
    std::array<std::atomic<double>, kParamCount> values {};
    std::atomic<uint32_t> pendingAuditions { 0u };
    std::array<std::atomic<unsigned char>, kPhraseCapacity> phrase {};
    std::atomic<uint32_t> phraseLength { 0u };
    std::atomic<uint32_t> textGestureCount { 0u };
    std::atomic<uint32_t> textWordCount { 0u };
    std::atomic<bool> textTruncated { false };
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<float> internalSpeechPeak { 0.0f };
    std::atomic<float> externalMicPeak { 0.0f };
    std::atomic<float> detectedPitchHz { 0.0f };
    std::atomic<float> pitchConfidence { 0.0f };
    std::atomic<bool> pitchActive { false };
    std::array<std::atomic<float>, kMatrixBands> analysisBandMeters {};
    std::array<std::atomic<float>, kMatrixBands> synthesisBandMeters {};
    std::atomic<bool> routingControlDirty { false };
    std::atomic<bool> tailChangePending { false };
    std::atomic<bool> pendingParamValuesRescan { false };
    std::atomic<uint32_t> publishedTailSamples { 1u };
    // State restoration and GUI phrase edits may legitimately arrive while
    // a host keeps the plug-in asleep. Retain a bounded burst of complete
    // compiled programs so the newest phrase is never rejected merely
    // because several migration/state messages preceded playback.
    s3g::clap_gui::SpscEventQueue<TextProgramMessage, 32u>
        textProgramEvents {};
    s3g::clap_gui::ParamEventQueue<> guiParamEvents {};
    s3g::AcapellaGestureProgram activeTextProgram {};
#if defined(__APPLE__)
    void* guiView = nullptr;
    bool guiVisible = false;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
#endif
};

uint32_t calculateTailSamples(const Plugin& plugin);
void publishTailSamplesOnAudioThread(Plugin& plugin);

Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}

clap_id bandTrimParamId(uint32_t band)
{
    return band < kMatrixBands
        ? kBandTrimParamBase + band : CLAP_INVALID_ID;
}

clap_id matrixParamId(bool sceneB, uint32_t destination, uint32_t source)
{
    if (destination >= kMatrixBands || source >= kMatrixBands) {
        return CLAP_INVALID_ID;
    }
    return (sceneB ? kMatrixBParamBase : kMatrixAParamBase)
        + destination * kMatrixBands + source;
}

bool decodeBandTrimParam(clap_id id, uint32_t& band)
{
    if (id < kBandTrimParamBase
        || id >= kBandTrimParamBase + kMatrixBands) return false;
    band = id - kBandTrimParamBase;
    return true;
}

bool decodeMatrixParam(clap_id id, bool& sceneB,
    uint32_t& destination, uint32_t& source)
{
    clap_id base = 0u;
    if (id >= kMatrixAParamBase
        && id < kMatrixAParamBase + kMatrixCells) {
        sceneB = false;
        base = kMatrixAParamBase;
    } else if (id >= kMatrixBParamBase
        && id < kMatrixBParamBase + kMatrixCells) {
        sceneB = true;
        base = kMatrixBParamBase;
    } else {
        return false;
    }
    const uint32_t cell = id - base;
    destination = cell / kMatrixBands;
    source = cell % kMatrixBands;
    return true;
}

bool isRoutingStorageParam(clap_id id)
{
    return id >= kBandTrimParamBase
        && id < kMatrixBParamBase + kMatrixCells;
}

const ParamDef* paramDef(clap_id id)
{
    return id >= 1u && id <= kParamCount
        ? &kParamDefs[id - 1u] : nullptr;
}

double clampValue(const ParamDef& def, double value)
{
    value = std::isfinite(value) ? value : def.defaultValue;
    value = std::clamp(value, def.minimum, def.maximum);
    return def.stepped ? std::round(value) : value;
}

void storeValue(Plugin& plugin, clap_id id, double value)
{
    if (id < 1u || id > kParamCount) return;
    plugin.values[id - 1u].store(value, std::memory_order_release);
}

double loadValue(const Plugin& plugin, clap_id id)
{
    if (id < 1u || id > kParamCount) return 0.0;
    return plugin.values[id - 1u].load(std::memory_order_acquire);
}

bool paramAffectsTail(clap_id id)
{
    return id == kPresetParamId || id == kReleaseParamId
        || id == kEchoMixParamId || id == kEchoTimeParamId
        || (id >= kEchoHeadsParamId && id <= kEchoSpreadParamId)
        || id == kBankAmountParamId || id == kBankReleaseParamId
        || id == kBankResonanceParamId || id == kBankFreezeParamId
        || id == kFreezeTriggerParamId || id == kBankBlurParamId;
}

void markTailChanged(Plugin& plugin)
{
    plugin.tailChangePending.store(true, std::memory_order_release);
    if (plugin.host && plugin.host->request_process) {
        plugin.host->request_process(plugin.host);
    }
}

// CLAP requires tail.changed() on the audio thread. GUI, state, and inactive
// params.flush updates therefore coalesce here until the next process block.
void deliverTailChangedOnAudioThread(Plugin& plugin)
{
    if (plugin.tailChangePending.exchange(false, std::memory_order_acq_rel)
        && plugin.host && plugin.hostTail && plugin.hostTail->changed) {
        plugin.hostTail->changed(plugin.host);
    }
}

uint32_t calculateTailSamples(const Plugin& plugin)
{
    // The source envelope reaches -60 dB in the labelled Release time and
    // retires the voice near -100 dB, so 5/3 of the label covers its active
    // lifetime (plus a small DC/retrigger margin).
    const double releaseSeconds = static_cast<double>(
        plugin.audioParams.releaseMs)
        * static_cast<double>(s3g::kAcapellaEnvelopeTailScale)
        * 0.001 + 0.05;
    const uint32_t effectTail = plugin.effects.tailSamples();
    const double delaySamples = static_cast<double>(effectTail);
    const double samples = releaseSeconds * plugin.sampleRate + delaySamples;
    return static_cast<uint32_t>(std::min<double>(
        std::numeric_limits<uint32_t>::max() - 1u,
        std::max(1.0, std::ceil(samples))));
}

void publishTailSamplesOnAudioThread(Plugin& plugin)
{
    const uint32_t value = calculateTailSamples(plugin);
    const uint32_t previous = plugin.publishedTailSamples.exchange(
        value, std::memory_order_acq_rel);
    if (previous != value) {
        plugin.tailChangePending.store(true, std::memory_order_release);
    }
}

PhraseState loadPhrase(const Plugin& plugin)
{
    PhraseState result;
    result.length = std::min<uint32_t>(
        plugin.phraseLength.load(std::memory_order_acquire),
        kPhraseCapacity - 1u);
    for (uint32_t index = 0u; index < result.length; ++index) {
        result.text[index] = static_cast<char>(plugin.phrase[index].load(
            std::memory_order_relaxed));
    }
    result.text[result.length] = '\0';
    return result;
}

void storePhrase(Plugin& plugin, const char* text)
{
    uint32_t length = 0u;
    if (text) {
        while (length + 1u < kPhraseCapacity && text[length] != '\0') {
            plugin.phrase[length].store(
                static_cast<unsigned char>(text[length]),
                std::memory_order_relaxed);
            ++length;
        }
    }
    plugin.phrase[length].store(0u, std::memory_order_relaxed);
    plugin.phraseLength.store(length, std::memory_order_release);
}

void requestGuiParamService(Plugin& plugin)
{
    if (plugin.hostParams && plugin.hostParams->request_flush) {
        plugin.hostParams->request_flush(plugin.host);
    }
    // A flush publishes the host-visible value but does not guarantee that a
    // sleeping audio effect will run another process block. Pitch Source is a
    // DSP topology change: explicitly wake process() so syncAudioParams() can
    // apply it before the next microphone onset.
    if (plugin.host && plugin.host->request_process) {
        plugin.host->request_process(plugin.host);
    }
}

void requestParamValuesRescan(Plugin& plugin)
{
    plugin.pendingParamValuesRescan.store(true, std::memory_order_release);
    if (plugin.host && plugin.host->request_callback) {
        plugin.host->request_callback(plugin.host);
    }
}

void markProfileCustom(Plugin& plugin)
{
    if (loadValue(plugin, kPresetParamId) == kCustomPreset) return;
    storeValue(plugin, kPresetParamId, kCustomPreset);
    requestParamValuesRescan(plugin);
}

void publishControlParam(Plugin& plugin, clap_id id, double value);

void queueGuiParamValue(Plugin& plugin, clap_id id, double value)
{
    const ParamDef* def = paramDef(id);
    if (!def) return;
    value = clampValue(*def, value);
    // Make the control effective before asking the host to echo it back. A
    // sleeping instrument is allowed to defer params.flush(), so relying on
    // that callback made GUI controls (notably Compile -> Text Phrase) appear
    // inert until the next MIDI/process block.
    publishControlParam(plugin, id, value);
    const std::array<s3g::clap_gui::ParamEvent, 3u> events {{
        { s3g::clap_gui::ParamEventKind::GestureBegin, id, 0.0 },
        { s3g::clap_gui::ParamEventKind::Value, id, value },
        { s3g::clap_gui::ParamEventKind::GestureEnd, id, 0.0 },
    }};
    if (plugin.guiParamEvents.pushBatch(events.data(),
            static_cast<uint32_t>(events.size()))) {
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

bool beginGuiParamGesture(Plugin& plugin, clap_id id)
{
    return queueGuiParamEvent(plugin,
        s3g::clap_gui::ParamEventKind::GestureBegin, id);
}

bool updateGuiParamGesture(Plugin& plugin, clap_id id, double value)
{
    const ParamDef* def = paramDef(id);
    if (!def) return false;
    value = clampValue(*def, value);
    publishControlParam(plugin, id, value);
    return queueGuiParamEvent(plugin,
        s3g::clap_gui::ParamEventKind::Value, id, value);
}

bool endGuiParamGesture(Plugin& plugin, clap_id id)
{
    return queueGuiParamEvent(plugin,
        s3g::clap_gui::ParamEventKind::GestureEnd, id);
}

bool queueGuiMatrixValues(Plugin& plugin, bool sceneB,
    const std::array<float, kMatrixCells>& values)
{
    // A scene operation is one host-visible edit. Matrix Mode is its single
    // automation gesture; the 484 route parameters publish live values inside
    // that gesture rather than pretending the operation was 484 independent
    // mouse edits. Mode=Custom and every route value share one atomic batch.
    // Keeping it below the queue's 511-event usable capacity avoids a locally
    // custom matrix whose host transaction was dropped.
    constexpr uint32_t kModeEventCount = 3u;
    constexpr uint32_t kMatrixEventOffset = 2u;
    static_assert(kMatrixCells + kModeEventCount < 512u);
    std::array<s3g::clap_gui::ParamEvent,
        kMatrixCells + kModeEventCount> events {};
    events[0u] = {
        s3g::clap_gui::ParamEventKind::GestureBegin,
        kMatrixModeParamId, 0.0,
    };
    events[1u] = {
        s3g::clap_gui::ParamEventKind::Value,
        kMatrixModeParamId,
        static_cast<double>(s3g::AcapellaResonatorMatrixMode::Custom),
    };
    for (uint32_t destination = 0u; destination < kMatrixBands;
         ++destination) {
        for (uint32_t source = 0u; source < kMatrixBands; ++source) {
            const uint32_t cell = destination * kMatrixBands + source;
            const clap_id id = matrixParamId(sceneB, destination, source);
            const double value = std::clamp<double>(values[cell], -1.0, 1.0);
            events[kMatrixEventOffset + cell] = {
                s3g::clap_gui::ParamEventKind::Value, id, value,
            };
        }
    }
    events.back() = {
        s3g::clap_gui::ParamEventKind::GestureEnd,
        kMatrixModeParamId, 0.0,
    };
    if (!plugin.guiParamEvents.pushBatch(events.data(), events.size())) {
        return false;
    }
    for (uint32_t cell = 0u; cell < kMatrixCells; ++cell) {
        const auto& event = events[kMatrixEventOffset + cell];
        storeValue(plugin, event.paramId, event.value);
    }
    plugin.routingControlDirty.store(true, std::memory_order_release);
    publishControlParam(plugin, kMatrixModeParamId,
        static_cast<double>(s3g::AcapellaResonatorMatrixMode::Custom));
    requestGuiParamService(plugin);
    return true;
}

bool publishTextPhrase(Plugin& plugin, const char* text)
{
    storePhrase(plugin, text);
    const auto phrase = loadPhrase(plugin);
    const auto compiled = s3g::compileAcapellaText(phrase.text.data());
    plugin.textGestureCount.store(compiled.program.count,
        std::memory_order_release);
    plugin.textWordCount.store(compiled.program.wordCount,
        std::memory_order_release);
    plugin.textTruncated.store(compiled.program.truncated,
        std::memory_order_release);
    if (!plugin.textProgramEvents.push({ compiled.program })) return false;
    if (plugin.host && plugin.host->request_process) {
        plugin.host->request_process(plugin.host);
    }
    return true;
}

void serviceTextPrograms(Plugin& plugin)
{
    TextProgramMessage message;
    bool changed = false;
    while (plugin.textProgramEvents.peek(message)) {
        plugin.activeTextProgram = message.program;
        plugin.textProgramEvents.pop();
        changed = true;
    }
    if (changed) {
        plugin.ensemble.setTextGestureProgram(plugin.activeTextProgram);
    }
}

void storeVoiceParams(Plugin& plugin, const s3g::AcapellaSourceParams& params)
{
    storeValue(plugin, kDeliveryParamId,
        params.delivery == s3g::AcapellaDelivery::Rap ? 1.0 : 0.0);
    storeValue(plugin, kTractParamId, params.voice.tractScale);
    storeValue(plugin, kBreathParamId, params.voice.breath);
    storeValue(plugin, kRoughnessParamId, params.voice.roughness);
    storeValue(plugin, kBrightnessParamId, params.voice.brightness);
    storeValue(plugin, kChestParamId, params.voice.chest);
    storeValue(plugin, kNasalParamId, params.voice.nasal);
    storeValue(plugin, kOpenQuotientParamId, params.voice.openQuotient);
    storeValue(plugin, kHarshnessParamId, params.voice.harshness);
    storeValue(plugin, kFalseFoldParamId, params.voice.falseFold);
    storeValue(plugin, kThroatParamId, params.voice.throat);
    storeValue(plugin, kArticulationParamId, params.articulation);
    storeValue(plugin, kConsonantParamId, params.consonantStrength);
    storeValue(plugin, kIntensityParamId, params.intensity);
    storeValue(plugin, kVibratoRateParamId, params.vibratoRateHz);
    storeValue(plugin, kVibratoDepthParamId, params.vibratoDepthCents);
    storeValue(plugin, kPitchDriftParamId, params.pitchDriftCents);
    storeValue(plugin, kGlideParamId, params.glideMs);
    storeValue(plugin, kAttackParamId, params.attackMs);
    storeValue(plugin, kReleaseParamId, params.releaseMs);
    storeValue(plugin, kHybridBlendParamId, params.hybridBlend);
    storeValue(plugin, kOnsetGuardParamId, params.onsetGuardMs);
    storeValue(plugin, kWaveguideBlendParamId, params.waveguideBlend);
    storeValue(plugin, kCoarticulationParamId, params.coarticulation);
    storeValue(plugin, kIntelligibilityParamId, params.intelligibility);
    storeValue(plugin, kGestureSequenceParamId,
        static_cast<uint32_t>(params.gestureSequence));
    storeValue(plugin, kGestureRateParamId, params.gestureRateHz);
    storeValue(plugin, kGestureDepthParamId, params.gestureDepth);
    storeValue(plugin, kGestureLoopParamId, params.gestureLoop ? 1.0 : 0.0);
    storeValue(plugin, kGestureSyncParamId,
        static_cast<uint32_t>(params.gestureSync));
    storeValue(plugin, kGestureDivisionParamId,
        static_cast<uint32_t>(params.gestureDivision));
    storeValue(plugin, kRetriggerParamId, params.retriggerMs);
    storeValue(plugin, kScoopParamId, params.onsetScoopSemitones);
    storeValue(plugin, kDeclinationParamId, params.rapDeclinationSemitones);
}

void storeSequencerUpgradeDefaults(Plugin& plugin,
    s3g::AcapellaSourcePreset preset)
{
    const auto params = s3g::acapellaSourcePreset(preset);
    storeValue(plugin, kGestureSequenceParamId,
        static_cast<uint32_t>(params.gestureSequence));
    storeValue(plugin, kGestureRateParamId, params.gestureRateHz);
    storeValue(plugin, kGestureDepthParamId, params.gestureDepth);
    storeValue(plugin, kGestureLoopParamId, params.gestureLoop ? 1.0 : 0.0);
    storeValue(plugin, kGestureSyncParamId,
        static_cast<uint32_t>(params.gestureSync));
    storeValue(plugin, kGestureDivisionParamId,
        static_cast<uint32_t>(params.gestureDivision));
}

void storeWaveguideUpgradeDefaults(Plugin& plugin,
    s3g::AcapellaSourcePreset preset)
{
    const auto params = s3g::acapellaSourcePreset(preset);
    storeValue(plugin, kWaveguideBlendParamId, params.waveguideBlend);
    storeValue(plugin, kCoarticulationParamId, params.coarticulation);
    storeSequencerUpgradeDefaults(plugin, preset);
}

void storeHybridUpgradeDefaults(Plugin& plugin,
    s3g::AcapellaSourcePreset preset)
{
    const auto params = s3g::acapellaSourcePreset(preset);
    storeValue(plugin, kHybridBlendParamId, params.hybridBlend);
    storeValue(plugin, kOnsetGuardParamId, params.onsetGuardMs);
    storeWaveguideUpgradeDefaults(plugin, preset);
}

void storeEffectsParams(Plugin& plugin,
    const s3g::AcapellaVocalFxParams& params)
{
    // Definition is deliberately one macro across generated articulation,
    // live filter-bank analysis, and the clean post-shape preserve rail.
    storeValue(plugin, kIntelligibilityParamId, params.intelligibility);
    storeValue(plugin, kOctaveDownParamId, params.octaveDown);
    storeValue(plugin, kOctaveUpParamId, params.octaveUp);
    storeValue(plugin, kFuzzDriveParamId, params.fuzzDriveDb);
    storeValue(plugin, kFuzzMixParamId, params.fuzzMix);
    storeValue(plugin, kFuzzToneParamId, params.fuzzToneHz);
    storeValue(plugin, kCompressionParamId, params.compression);
    storeValue(plugin, kParallelCrushParamId, params.parallelCrush);
    storeValue(plugin, kDeEssParamId, params.deEss);
    storeValue(plugin, kEchoMixParamId, params.echoMix);
    storeValue(plugin, kEchoTimeParamId, params.echoTimeMs);
    storeValue(plugin, kWidthParamId, params.width);
    storeValue(plugin, kEchoHeadsParamId,
        static_cast<uint32_t>(params.echoHeads));
    storeValue(plugin, kEchoClockParamId,
        static_cast<uint32_t>(params.echoClock));
    storeValue(plugin, kEchoFeedbackParamId, params.echoFeedback);
    storeValue(plugin, kEchoWearParamId, params.echoWear);
    storeValue(plugin, kEchoFlutterParamId, params.echoFlutter);
    storeValue(plugin, kEchoToneParamId, params.echoTone);
    storeValue(plugin, kEchoSpreadParamId, params.echoSpread);
    storeValue(plugin, kBankAmountParamId, params.resonator.amount);
    storeValue(plugin, kBankModeParamId,
        static_cast<uint32_t>(params.resonator.mode));
    storeValue(plugin, kCarrierShapeParamId,
        static_cast<uint32_t>(params.resonator.carrierShape));
    storeValue(plugin, kCarrierHarmonicsParamId,
        params.resonator.carrierHarmonics);
    storeValue(plugin, kCarrierColorParamId, params.resonator.carrierColor);
    storeValue(plugin, kCarrierNoiseParamId, params.resonator.carrierNoise);
    storeValue(plugin, kAnalysisBlendParamId, params.resonator.analysisBlend);
    storeValue(plugin, kBankAttackParamId, params.resonator.attackMs);
    storeValue(plugin, kBankReleaseParamId, params.resonator.releaseMs);
    storeValue(plugin, kBankResonanceParamId, params.resonator.resonance);
    storeValue(plugin, kBankDriveParamId, params.resonator.driveDb);
    storeValue(plugin, kBandShiftParamId,
        params.resonator.bandShiftSemitones);
    storeValue(plugin, kBandStretchParamId, params.resonator.bandStretch);
    storeValue(plugin, kBandTiltParamId, params.resonator.tilt);
    storeValue(plugin, kSibilanceParamId, params.resonator.sibilance);
    storeValue(plugin, kMatrixModeParamId,
        static_cast<uint32_t>(params.resonator.matrixMode));
    storeValue(plugin, kMatrixMorphParamId, params.resonator.matrixMorph);
    storeValue(plugin, kBankStereoSpreadParamId,
        params.resonator.stereoSpread);
    storeValue(plugin, kBankFreezeParamId, params.resonator.freeze);
    storeValue(plugin, kFreezeTriggerParamId,
        static_cast<uint32_t>(params.resonator.freezeTrigger));
    storeValue(plugin, kBankBlurParamId, params.resonator.blurMs);
    storeValue(plugin, kBankGestureFollowParamId,
        params.resonator.gestureFollow);
    storeValue(plugin, kBandLayoutParamId,
        static_cast<uint32_t>(params.resonator.bandLayout));
    storeValue(plugin, kAnalysisSlopeParamId,
        static_cast<uint32_t>(params.resonator.analysisSlope));
    storeValue(plugin, kMouthFocusParamId, params.resonator.mouthFocus);
    storeValue(plugin, kTransferModeParamId,
        static_cast<uint32_t>(params.resonator.transferMode));
    storeValue(plugin, kVoiceFocusParamId, params.resonator.voiceFocus);
    storeValue(plugin, kAnalysisLevelerParamId,
        params.resonator.analysisLeveler);
    storeValue(plugin, kConsonantColorParamId,
        params.resonator.consonantColor);
    storeValue(plugin, kConsonantSpeedParamId,
        params.resonator.consonantSpeed);
    storeValue(plugin, kCarrierDensityParamId,
        params.resonator.carrierDensity);
    storeValue(plugin, kAnalysisWidthParamId,
        params.resonator.analysisWidth);
    storeValue(plugin, kHfDetailModeParamId,
        static_cast<uint32_t>(params.resonator.hfDetailMode));
    storeValue(plugin, kHfDetailLevelParamId,
        params.resonator.hfDetailLevel);
    storeValue(plugin, kHfDetailCutoffParamId,
        params.resonator.hfDetailCutoffHz);
    storeValue(plugin, kAnalysisLowEqParamId,
        params.resonator.analysisLowDb);
    storeValue(plugin, kAnalysisMidEqParamId,
        params.resonator.analysisMidDb);
    storeValue(plugin, kAnalysisAirEqParamId,
        params.resonator.analysisAirDb);
    storeValue(plugin, kAnalysisCompressionParamId,
        params.resonator.analysisCompression);
    storeValue(plugin, kAnalysisNoiseRejectParamId,
        params.resonator.analysisNoiseReject);
    storeValue(plugin, kAnalysisSpectralBalanceParamId,
        params.resonator.analysisSpectralBalance);
    storeValue(plugin, kVoicingModeParamId,
        static_cast<uint32_t>(params.resonator.voicingMode));
    storeValue(plugin, kVoicingThresholdParamId,
        params.resonator.voicingThreshold);
    storeValue(plugin, kVoicedLevelParamId, params.resonator.voicedLevel);
    storeValue(plugin, kUnvoicedLevelParamId, params.resonator.unvoicedLevel);
    storeValue(plugin, kVoicedTransitionParamId,
        params.resonator.voicedTransitionMs);
    storeValue(plugin, kUnvoicedTransitionParamId,
        params.resonator.unvoicedTransitionMs);
    storeValue(plugin, kOpenLevelParamId, params.resonator.openLevel);
    storeValue(plugin, kCouplingParamId, params.resonator.coupling);
    storeValue(plugin, kArticulationThruParamId,
        params.resonator.articulationThru);
    storeValue(plugin, kStereoModeParamId,
        static_cast<uint32_t>(params.resonator.stereoMode));
    storeValue(plugin, kModulatorSourceParamId,
        static_cast<uint32_t>(params.resonator.modulatorSource));
    storeValue(plugin, kMicGainParamId, params.resonator.micGainDb);
    storeValue(plugin, kCarrierPulseWidthParamId,
        params.resonator.pulseWidth);
    storeValue(plugin, kCarrierLfoShapeParamId,
        static_cast<uint32_t>(params.resonator.carrierLfoShape));
    storeValue(plugin, kCarrierLfoRateParamId,
        params.resonator.carrierLfoRateHz);
    storeValue(plugin, kCarrierLfoDepthParamId,
        params.resonator.carrierLfoDepthSemitones);
    storeValue(plugin, kCarrierPwmDepthParamId,
        params.resonator.carrierLfoPwmDepth);
    storeValue(plugin, kCarrierLfoSyncParamId,
        params.resonator.carrierLfoSync ? 1.0 : 0.0);
    storeValue(plugin, kCarrierLfoDivisionParamId,
        carrierLfoDivisionIndex(
            params.resonator.carrierLfoSyncDivisionBeats));
    storeValue(plugin, kCustomMatrixMorphParamId,
        params.resonator.customMatrixMorph);
    storeValue(plugin, kCarrierPitchSourceParamId,
        static_cast<uint32_t>(params.resonator.carrierPitchSource));
    storeValue(plugin, kPitchScaleRootParamId,
        params.resonator.pitchScaleRoot);
    storeValue(plugin, kPitchScaleParamId,
        static_cast<uint32_t>(params.resonator.pitchScale));
    storeValue(plugin, kPitchHoldParamId, params.resonator.pitchHoldMs);
    for (uint32_t band = 0u; band < kMatrixBands; ++band) {
        storeValue(plugin, bandTrimParamId(band),
            params.resonator.bandTrims[band]);
    }
    for (uint32_t destination = 0u; destination < kMatrixBands;
         ++destination) {
        for (uint32_t source = 0u; source < kMatrixBands; ++source) {
            const uint32_t cell = destination * kMatrixBands + source;
            storeValue(plugin, matrixParamId(false, destination, source),
                params.resonator.customMatrixA[cell]);
            storeValue(plugin, matrixParamId(true, destination, source),
                params.resonator.customMatrixB[cell]);
        }
    }
}

void storeEnsembleParams(Plugin& plugin,
    const s3g::AcapellaEnsembleParams& params)
{
    storeValue(plugin, kPolyphonyParamId, params.polyphony);
    storeValue(plugin, kDoubleAmountParamId, params.doubleAmount);
    storeValue(plugin, kDoubleDetuneParamId, params.doubleDetuneCents);
    storeValue(plugin, kDoubleTimingParamId, params.doubleTimingMs);
    storeValue(plugin, kDoubleDirtParamId, params.doubleDirt);
    storeValue(plugin, kDoubleWidthParamId, params.doubleWidth);
}

s3g::AcapellaSourcePreset resonatorProfileBase(uint32_t index)
{
    return s3g::acapellaResonatorProfileBase(index);
}

s3g::AcapellaVocalFxParams resonatorProfileEffects(uint32_t index,
    s3g::AcapellaVocalFxParams effects)
{
    return s3g::acapellaResonatorProfileEffects(index, effects);
}

void selectPreset(Plugin& plugin, uint32_t index)
{
    if (index >= static_cast<uint32_t>(kCustomPreset)) {
        storeValue(plugin, kPresetParamId, kCustomPreset);
        return;
    }
    const auto sourcePreset = index < 6u
        ? static_cast<s3g::AcapellaSourcePreset>(index)
        : resonatorProfileBase(index);
    auto params = s3g::acapellaSourcePreset(sourcePreset);
    // Voice-bank profiles are designed around phoneme, syllable, and word
    // boundaries. Point them at the compiled phrase by default so their
    // freeze triggers and gesture-follow controls are immediately operative.
    if (index >= s3g::kAcapellaResonatorProfileFirst && index <= 13u) {
        params.gestureSequence = s3g::AcapellaGestureSequence::Text;
        params.gestureDepth = 1.0f;
        params.gestureLoop = true;
    }
    storeVoiceParams(plugin, params);
    storeEffectsParams(plugin, resonatorProfileEffects(index,
        s3g::acapellaVocalFxPreset(sourcePreset)));
    auto ensemble = s3g::acapellaEnsemblePreset(sourcePreset);
    if (index == 10u || index == 13u || index == 23u) {
        ensemble.polyphony = index == 10u ? 6u : 8u;
        ensemble.doubleAmount = index == 10u ? 0.48f
                                             : (index == 13u ? 0.32f : 0.24f);
        ensemble.doubleDetuneCents = index == 10u ? 13.0f
                                                  : (index == 13u ? 7.0f : 5.0f);
        ensemble.doubleTimingMs = index == 10u ? 29.0f
                                               : (index == 13u ? 18.0f : 12.0f);
        ensemble.doubleWidth = 0.94f;
    }
    if (index == 26u || index == 28u || index == 29u) {
        ensemble.polyphony = index == 26u ? 6u : 8u;
        ensemble.doubleAmount = index == 28u ? 0.12f : 0.08f;
        ensemble.doubleDetuneCents = index == 28u ? 4.0f : 2.5f;
        ensemble.doubleTimingMs = index == 28u ? 8.0f : 5.0f;
        ensemble.doubleWidth = index == 29u ? 0.86f : 0.72f;
    }
    storeEnsembleParams(plugin, ensemble);
    storeValue(plugin, kPresetParamId, static_cast<double>(index));
    plugin.routingControlDirty.store(true, std::memory_order_release);
}

bool customisingParam(clap_id id)
{
    return id == kDeliveryParamId
        || (id >= kTractParamId && id <= kDeclinationParamId)
        || (id >= kHarshnessParamId && id <= kRetriggerParamId)
        || (id >= kOctaveDownParamId && id <= kWidthParamId)
        || (id >= kPolyphonyParamId && id <= kDoubleWidthParamId)
        || id == kParallelCrushParamId
        || id == kHybridBlendParamId
        || id == kOnsetGuardParamId
        || id == kWaveguideBlendParamId
        || id == kCoarticulationParamId
        || id == kIntelligibilityParamId
        || (id >= kEchoHeadsParamId && id <= kEchoSpreadParamId)
        || (id >= kBankAmountParamId && id <= kBankGestureFollowParamId)
        || (id >= kBandLayoutParamId && id <= kCustomMatrixMorphParamId)
        || (id >= kAnalysisSlopeParamId
            && id <= kAnalysisSpectralBalanceParamId)
        || (id >= kBandTrimParamBase
            && id < kMatrixBParamBase + kMatrixCells)
        || (id >= kGestureSequenceParamId && id <= kGestureDivisionParamId);
}

void publishControlParam(Plugin& plugin, clap_id id, double value)
{
    const auto* def = paramDef(id);
    if (!def) return;
    value = clampValue(*def, value);
    if (id == kPresetParamId) {
        selectPreset(plugin, static_cast<uint32_t>(value));
        markTailChanged(plugin);
        requestParamValuesRescan(plugin);
        return;
    }
    if (id == kAuditionParamId) {
        const bool gate = value >= 0.5;
        const bool previous = plugin.controlAuditionGate.exchange(
            gate, std::memory_order_acq_rel);
        if (gate && !previous) {
            plugin.pendingAuditions.fetch_add(1u, std::memory_order_relaxed);
            if (plugin.host && plugin.host->request_process) {
                plugin.host->request_process(plugin.host);
            }
        }
        storeValue(plugin, id, gate ? 1.0 : 0.0);
        return;
    }
    storeValue(plugin, id, value);
    if (isRoutingStorageParam(id)) {
        plugin.routingControlDirty.store(true, std::memory_order_release);
    }
    if (paramAffectsTail(id)) markTailChanged(plugin);
    if (customisingParam(id)) markProfileCustom(plugin);
    if (plugin.host && plugin.host->request_process) {
        plugin.host->request_process(plugin.host);
    }
}

void syncAudioParams(Plugin& plugin, bool loadRouting = true)
{
    auto params = plugin.audioParams;
    params.delivery = loadValue(plugin, kDeliveryParamId) >= 0.5
        ? s3g::AcapellaDelivery::Rap : s3g::AcapellaDelivery::Sung;
    params.voice.tractScale = static_cast<float>(loadValue(plugin, kTractParamId));
    params.voice.breath = static_cast<float>(loadValue(plugin, kBreathParamId));
    params.voice.roughness = static_cast<float>(loadValue(plugin, kRoughnessParamId));
    params.voice.brightness = static_cast<float>(loadValue(plugin, kBrightnessParamId));
    params.voice.chest = static_cast<float>(loadValue(plugin, kChestParamId));
    params.voice.nasal = static_cast<float>(loadValue(plugin, kNasalParamId));
    params.voice.openQuotient = static_cast<float>(loadValue(plugin, kOpenQuotientParamId));
    params.voice.harshness = static_cast<float>(loadValue(plugin, kHarshnessParamId));
    params.voice.falseFold = static_cast<float>(loadValue(plugin, kFalseFoldParamId));
    params.voice.throat = static_cast<float>(loadValue(plugin, kThroatParamId));
    params.articulation = static_cast<float>(loadValue(plugin, kArticulationParamId));
    params.consonantStrength = static_cast<float>(loadValue(plugin, kConsonantParamId));
    params.intensity = static_cast<float>(loadValue(plugin, kIntensityParamId));
    params.vibratoRateHz = static_cast<float>(loadValue(plugin, kVibratoRateParamId));
    params.vibratoDepthCents = static_cast<float>(loadValue(plugin, kVibratoDepthParamId));
    params.pitchDriftCents = static_cast<float>(loadValue(plugin, kPitchDriftParamId));
    params.glideMs = static_cast<float>(loadValue(plugin, kGlideParamId));
    params.attackMs = static_cast<float>(loadValue(plugin, kAttackParamId));
    params.releaseMs = static_cast<float>(loadValue(plugin, kReleaseParamId));
    params.hybridBlend = static_cast<float>(loadValue(plugin, kHybridBlendParamId));
    params.onsetGuardMs = static_cast<float>(loadValue(plugin, kOnsetGuardParamId));
    params.waveguideBlend = static_cast<float>(loadValue(plugin, kWaveguideBlendParamId));
    params.coarticulation = static_cast<float>(loadValue(plugin, kCoarticulationParamId));
    params.intelligibility = static_cast<float>(loadValue(plugin,
        kIntelligibilityParamId));
    params.gestureSequence = static_cast<s3g::AcapellaGestureSequence>(
        static_cast<uint32_t>(loadValue(plugin, kGestureSequenceParamId)));
    params.gestureRateHz = static_cast<float>(loadValue(plugin, kGestureRateParamId));
    params.gestureDepth = static_cast<float>(loadValue(plugin, kGestureDepthParamId));
    params.gestureLoop = loadValue(plugin, kGestureLoopParamId) >= 0.5;
    params.gestureSync = static_cast<s3g::AcapellaGestureSync>(
        static_cast<uint32_t>(loadValue(plugin, kGestureSyncParamId)));
    params.gestureDivision = static_cast<s3g::AcapellaGestureDivision>(
        static_cast<uint32_t>(loadValue(plugin, kGestureDivisionParamId)));
    params.retriggerMs = static_cast<float>(loadValue(plugin, kRetriggerParamId));
    params.onsetScoopSemitones = static_cast<float>(loadValue(plugin, kScoopParamId));
    params.rapDeclinationSemitones = static_cast<float>(loadValue(plugin, kDeclinationParamId));
    plugin.audioParams = s3g::sanitizeAcapellaSourceParams(params);
    plugin.vowel = static_cast<s3g::AcapellaVowel>(
        static_cast<uint32_t>(loadValue(plugin, kVowelParamId)));
    plugin.onset = static_cast<s3g::AcapellaOnset>(
        static_cast<uint32_t>(loadValue(plugin, kOnsetParamId)));
    plugin.durationMs = static_cast<float>(loadValue(plugin, kDurationParamId));
    plugin.outputGain = s3g::dbToGain(
        static_cast<float>(loadValue(plugin, kOutputParamId)));
    auto& effects = plugin.effectsParams;
    effects.octaveDown = static_cast<float>(loadValue(plugin, kOctaveDownParamId));
    effects.octaveUp = static_cast<float>(loadValue(plugin, kOctaveUpParamId));
    effects.fuzzDriveDb = static_cast<float>(loadValue(plugin, kFuzzDriveParamId));
    effects.fuzzMix = static_cast<float>(loadValue(plugin, kFuzzMixParamId));
    effects.fuzzToneHz = static_cast<float>(loadValue(plugin, kFuzzToneParamId));
    effects.compression = static_cast<float>(loadValue(plugin, kCompressionParamId));
    effects.parallelCrush = static_cast<float>(loadValue(plugin, kParallelCrushParamId));
    effects.deEss = static_cast<float>(loadValue(plugin, kDeEssParamId));
    effects.echoMix = static_cast<float>(loadValue(plugin, kEchoMixParamId));
    effects.echoTimeMs = static_cast<float>(loadValue(plugin, kEchoTimeParamId));
    effects.width = static_cast<float>(loadValue(plugin, kWidthParamId));
    effects.intelligibility = static_cast<float>(loadValue(plugin,
        kIntelligibilityParamId));
    effects.echoHeads = static_cast<s3g::DrumEchoHeadMode>(
        static_cast<uint32_t>(loadValue(plugin, kEchoHeadsParamId)));
    effects.echoClock = static_cast<s3g::DrumEchoClock>(
        static_cast<uint32_t>(loadValue(plugin, kEchoClockParamId)));
    effects.echoFeedback = static_cast<float>(loadValue(plugin,
        kEchoFeedbackParamId));
    effects.echoWear = static_cast<float>(loadValue(plugin,
        kEchoWearParamId));
    effects.echoFlutter = static_cast<float>(loadValue(plugin,
        kEchoFlutterParamId));
    effects.echoTone = static_cast<float>(loadValue(plugin,
        kEchoToneParamId));
    effects.echoSpread = static_cast<float>(loadValue(plugin,
        kEchoSpreadParamId));
    effects.resonator.amount = static_cast<float>(loadValue(plugin,
        kBankAmountParamId));
    effects.resonator.mode = static_cast<decltype(effects.resonator.mode)>(
        static_cast<uint32_t>(loadValue(plugin, kBankModeParamId)));
    effects.resonator.carrierShape = static_cast<
        decltype(effects.resonator.carrierShape)>(static_cast<uint32_t>(
            loadValue(plugin, kCarrierShapeParamId)));
    effects.resonator.carrierHarmonics = static_cast<float>(loadValue(plugin,
        kCarrierHarmonicsParamId));
    effects.resonator.carrierColor = static_cast<float>(loadValue(plugin,
        kCarrierColorParamId));
    effects.resonator.carrierNoise = static_cast<float>(loadValue(plugin,
        kCarrierNoiseParamId));
    effects.resonator.analysisBlend = static_cast<float>(loadValue(plugin,
        kAnalysisBlendParamId));
    effects.resonator.definition = static_cast<float>(loadValue(plugin,
        kIntelligibilityParamId));
    effects.resonator.attackMs = static_cast<float>(loadValue(plugin,
        kBankAttackParamId));
    effects.resonator.releaseMs = static_cast<float>(loadValue(plugin,
        kBankReleaseParamId));
    effects.resonator.resonance = static_cast<float>(loadValue(plugin,
        kBankResonanceParamId));
    effects.resonator.driveDb = static_cast<float>(loadValue(plugin,
        kBankDriveParamId));
    effects.resonator.bandShiftSemitones = static_cast<float>(loadValue(plugin,
        kBandShiftParamId));
    effects.resonator.bandStretch = static_cast<float>(loadValue(plugin,
        kBandStretchParamId));
    effects.resonator.tilt = static_cast<float>(loadValue(plugin,
        kBandTiltParamId));
    effects.resonator.sibilance = static_cast<float>(loadValue(plugin,
        kSibilanceParamId));
    effects.resonator.matrixMode = static_cast<
        decltype(effects.resonator.matrixMode)>(static_cast<uint32_t>(
            loadValue(plugin, kMatrixModeParamId)));
    effects.resonator.matrixMorph = static_cast<float>(loadValue(plugin,
        kMatrixMorphParamId));
    effects.resonator.stereoSpread = static_cast<float>(loadValue(plugin,
        kBankStereoSpreadParamId));
    effects.resonator.freeze = static_cast<float>(loadValue(plugin,
        kBankFreezeParamId));
    effects.resonator.freezeTrigger = static_cast<
        decltype(effects.resonator.freezeTrigger)>(static_cast<uint32_t>(
            loadValue(plugin, kFreezeTriggerParamId)));
    effects.resonator.blurMs = static_cast<float>(loadValue(plugin,
        kBankBlurParamId));
    effects.resonator.gestureFollow = static_cast<float>(loadValue(plugin,
        kBankGestureFollowParamId));
    effects.resonator.bandLayout = static_cast<
        decltype(effects.resonator.bandLayout)>(static_cast<uint32_t>(
            loadValue(plugin, kBandLayoutParamId)));
    effects.resonator.analysisSlope = static_cast<
        decltype(effects.resonator.analysisSlope)>(static_cast<uint32_t>(
            loadValue(plugin, kAnalysisSlopeParamId)));
    effects.resonator.mouthFocus = static_cast<float>(loadValue(plugin,
        kMouthFocusParamId));
    effects.resonator.transferMode = static_cast<
        decltype(effects.resonator.transferMode)>(static_cast<uint32_t>(
            loadValue(plugin, kTransferModeParamId)));
    effects.resonator.voiceFocus = static_cast<float>(loadValue(plugin,
        kVoiceFocusParamId));
    effects.resonator.analysisLeveler = static_cast<float>(loadValue(plugin,
        kAnalysisLevelerParamId));
    effects.resonator.consonantColor = static_cast<float>(loadValue(plugin,
        kConsonantColorParamId));
    effects.resonator.consonantSpeed = static_cast<float>(loadValue(plugin,
        kConsonantSpeedParamId));
    effects.resonator.carrierDensity = static_cast<float>(loadValue(plugin,
        kCarrierDensityParamId));
    effects.resonator.analysisWidth = static_cast<float>(loadValue(plugin,
        kAnalysisWidthParamId));
    effects.resonator.hfDetailMode = static_cast<
        decltype(effects.resonator.hfDetailMode)>(static_cast<uint32_t>(
            loadValue(plugin, kHfDetailModeParamId)));
    effects.resonator.hfDetailLevel = static_cast<float>(loadValue(plugin,
        kHfDetailLevelParamId));
    effects.resonator.hfDetailCutoffHz = static_cast<float>(loadValue(plugin,
        kHfDetailCutoffParamId));
    effects.resonator.analysisLowDb = static_cast<float>(loadValue(plugin,
        kAnalysisLowEqParamId));
    effects.resonator.analysisMidDb = static_cast<float>(loadValue(plugin,
        kAnalysisMidEqParamId));
    effects.resonator.analysisAirDb = static_cast<float>(loadValue(plugin,
        kAnalysisAirEqParamId));
    effects.resonator.analysisCompression = static_cast<float>(loadValue(
        plugin, kAnalysisCompressionParamId));
    effects.resonator.analysisNoiseReject = static_cast<float>(loadValue(
        plugin, kAnalysisNoiseRejectParamId));
    effects.resonator.analysisSpectralBalance = static_cast<float>(loadValue(
        plugin, kAnalysisSpectralBalanceParamId));
    effects.resonator.voicingMode = static_cast<
        decltype(effects.resonator.voicingMode)>(static_cast<uint32_t>(
            loadValue(plugin, kVoicingModeParamId)));
    effects.resonator.voicingThreshold = static_cast<float>(loadValue(plugin,
        kVoicingThresholdParamId));
    effects.resonator.voicedLevel = static_cast<float>(loadValue(plugin,
        kVoicedLevelParamId));
    effects.resonator.unvoicedLevel = static_cast<float>(loadValue(plugin,
        kUnvoicedLevelParamId));
    effects.resonator.voicedTransitionMs = static_cast<float>(loadValue(plugin,
        kVoicedTransitionParamId));
    effects.resonator.unvoicedTransitionMs = static_cast<float>(loadValue(plugin,
        kUnvoicedTransitionParamId));
    effects.resonator.openLevel = static_cast<float>(loadValue(plugin,
        kOpenLevelParamId));
    effects.resonator.coupling = static_cast<int32_t>(std::lround(loadValue(
        plugin, kCouplingParamId)));
    effects.resonator.articulationThru = static_cast<float>(loadValue(plugin,
        kArticulationThruParamId));
    effects.resonator.stereoMode = static_cast<
        decltype(effects.resonator.stereoMode)>(static_cast<uint32_t>(
            loadValue(plugin, kStereoModeParamId)));
    effects.resonator.modulatorSource = static_cast<
        decltype(effects.resonator.modulatorSource)>(static_cast<uint32_t>(
            loadValue(plugin, kModulatorSourceParamId)));
    effects.resonator.micGainDb = static_cast<float>(loadValue(plugin,
        kMicGainParamId));
    effects.resonator.pulseWidth = static_cast<float>(loadValue(plugin,
        kCarrierPulseWidthParamId));
    effects.resonator.carrierLfoShape = static_cast<
        decltype(effects.resonator.carrierLfoShape)>(static_cast<uint32_t>(
            loadValue(plugin, kCarrierLfoShapeParamId)));
    effects.resonator.carrierLfoRateHz = static_cast<float>(loadValue(plugin,
        kCarrierLfoRateParamId));
    effects.resonator.carrierLfoDepthSemitones = static_cast<float>(loadValue(
        plugin, kCarrierLfoDepthParamId));
    effects.resonator.carrierLfoPwmDepth = static_cast<float>(loadValue(plugin,
        kCarrierPwmDepthParamId));
    effects.resonator.carrierLfoSync = loadValue(plugin,
        kCarrierLfoSyncParamId) >= 0.5;
    effects.resonator.carrierLfoSyncDivisionBeats = carrierLfoDivisionBeats(
        loadValue(plugin, kCarrierLfoDivisionParamId));
    effects.resonator.customMatrixMorph = static_cast<float>(loadValue(plugin,
        kCustomMatrixMorphParamId));
    effects.resonator.carrierPitchSource = static_cast<
        decltype(effects.resonator.carrierPitchSource)>(
            static_cast<uint32_t>(loadValue(
                plugin, kCarrierPitchSourceParamId)));
    effects.resonator.pitchScaleRoot = static_cast<uint32_t>(std::lround(
        loadValue(plugin, kPitchScaleRootParamId)));
    effects.resonator.pitchScale = static_cast<
        decltype(effects.resonator.pitchScale)>(static_cast<uint32_t>(
            loadValue(plugin, kPitchScaleParamId)));
    effects.resonator.pitchHoldMs = static_cast<float>(loadValue(plugin,
        kPitchHoldParamId));
    if (loadRouting) {
        for (uint32_t band = 0u; band < kMatrixBands; ++band) {
            effects.resonator.bandTrims[band] = static_cast<float>(loadValue(
                plugin, bandTrimParamId(band)));
        }
        for (uint32_t destination = 0u; destination < kMatrixBands;
             ++destination) {
            for (uint32_t source = 0u; source < kMatrixBands; ++source) {
                const uint32_t cell = destination * kMatrixBands + source;
                effects.resonator.customMatrixA[cell] = static_cast<float>(
                    loadValue(plugin,
                        matrixParamId(false, destination, source)));
                effects.resonator.customMatrixB[cell] = static_cast<float>(
                    loadValue(plugin,
                        matrixParamId(true, destination, source)));
            }
        }
    }
    if (loadRouting) {
        plugin.effectsParams = s3g::sanitizeAcapellaVocalFxParams(effects);
    }
    auto ensemble = plugin.ensembleParams;
    ensemble.polyphony = static_cast<uint32_t>(loadValue(plugin, kPolyphonyParamId));
    ensemble.doubleAmount = static_cast<float>(loadValue(plugin, kDoubleAmountParamId));
    ensemble.doubleDetuneCents = static_cast<float>(loadValue(plugin, kDoubleDetuneParamId));
    ensemble.doubleTimingMs = static_cast<float>(loadValue(plugin, kDoubleTimingParamId));
    ensemble.doubleDirt = static_cast<float>(loadValue(plugin, kDoubleDirtParamId));
    ensemble.doubleWidth = static_cast<float>(loadValue(plugin, kDoubleWidthParamId));
    plugin.ensembleParams = s3g::sanitizeAcapellaEnsembleParams(ensemble);
    plugin.ensemble.setSourceParams(plugin.audioParams);
    plugin.ensemble.setParams(plugin.ensembleParams);
    if (loadRouting) {
        plugin.effects.setParams(plugin.effectsParams);
    } else {
        plugin.effects.setRealtimeControlParams(plugin.effectsParams);
    }
}

float midiFrequency(int16_t key)
{
    return 440.0f * std::exp2((static_cast<float>(key) - 69.0f) / 12.0f);
}

void triggerVoice(Plugin& plugin, int16_t key, float velocity,
    int32_t noteId = -1, int16_t channel = -1, bool audition = false)
{
    // The large routing arrays are synchronized once per dirty process block.
    // A note edge only needs the scalar performance controls refreshed.
    syncAudioParams(plugin, false);
    plugin.ensemble.trigger({
        { plugin.vowel, plugin.onset, midiFrequency(key), velocity,
            plugin.durationMs },
        audition ? -2 : noteId,
        channel,
        key,
    });
    plugin.auditionVoice = audition;
}

void applyRoutingParamToAudioState(Plugin& plugin, clap_id id);

bool applyAudioParam(Plugin& plugin, clap_id id, double value)
{
    const auto* def = paramDef(id);
    if (!def) return false;
    value = clampValue(*def, value);
    if (id == kPresetParamId) {
        selectPreset(plugin, static_cast<uint32_t>(value));
        markTailChanged(plugin);
        requestParamValuesRescan(plugin);
        return true;
    }
    if (id == kAuditionParamId) {
        const bool gate = value >= 0.5;
        storeValue(plugin, id, gate ? 1.0 : 0.0);
        if (gate && !plugin.audioAuditionGate) {
            triggerVoice(plugin, 60, 0.82f, -2, -1, true);
        }
        if (!gate && plugin.audioAuditionGate && plugin.auditionVoice) {
            plugin.ensemble.release(-2, -1, 60);
            plugin.auditionVoice = false;
        }
        plugin.audioAuditionGate = gate;
        return false;
    }
    storeValue(plugin, id, value);
    if (isRoutingStorageParam(id)) {
        // Matrix/trim events update only their addressed cell below instead of
        // reloading 990 atomics for every point in an automation gesture.
        applyRoutingParamToAudioState(plugin, id);
        markProfileCustom(plugin);
        return false;
    }
    if (paramAffectsTail(id)) markTailChanged(plugin);
    if (customisingParam(id)) markProfileCustom(plugin);
    return true;
}

void applyRoutingParamToAudioState(Plugin& plugin, clap_id id)
{
    uint32_t band = 0u;
    bool sceneB = false;
    uint32_t destination = 0u;
    uint32_t source = 0u;
    if (decodeBandTrimParam(id, band)) {
        const float value = static_cast<float>(loadValue(plugin, id));
        plugin.effectsParams.resonator.bandTrims[band] = value;
        plugin.effects.setResonatorBandTrim(band, value);
        return;
    }
    if (decodeMatrixParam(id, sceneB, destination, source)) {
        const uint32_t cell = destination * kMatrixBands + source;
        auto& matrix = sceneB
            ? plugin.effectsParams.resonator.customMatrixB
            : plugin.effectsParams.resonator.customMatrixA;
        const float value = static_cast<float>(loadValue(plugin, id));
        matrix[cell] = value;
        plugin.effects.setResonatorCustomMatrixCell(
            sceneB, destination, source, value);
    }
}

bool applyEvent(Plugin& plugin, const clap_event_header_t* event)
{
    if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) return false;
    if (event->type == CLAP_EVENT_PARAM_VALUE
        && event->size >= sizeof(clap_event_param_value_t)) {
        const auto* param = reinterpret_cast<const clap_event_param_value_t*>(event);
        return applyAudioParam(plugin, param->param_id, param->value);
    }
    if ((event->type == CLAP_EVENT_NOTE_ON
            || event->type == CLAP_EVENT_NOTE_OFF
            || event->type == CLAP_EVENT_NOTE_CHOKE)
        && event->size >= sizeof(clap_event_note_t)) {
        const auto* note = reinterpret_cast<const clap_event_note_t*>(event);
        // Unlike raw MIDI, CLAP explicitly defines velocity-zero NOTE_ON as a
        // valid note-on rather than an encoded note-off.
        const bool noteOn = event->type == CLAP_EVENT_NOTE_ON;
        if (noteOn) {
            triggerVoice(plugin, note->key,
                static_cast<float>(note->velocity), note->note_id,
                note->channel);
        } else if (note->key < 0 && note->note_id < 0) {
            plugin.ensemble.releaseAll();
        } else {
            plugin.ensemble.release(note->note_id, note->channel, note->key);
        }
        return false;
    }
    if (event->type == CLAP_EVENT_MIDI
        && event->size >= sizeof(clap_event_midi_t)) {
        const auto* midi = reinterpret_cast<const clap_event_midi_t*>(event);
        const uint8_t status = midi->data[0] & 0xf0u;
        const int16_t channel = static_cast<int16_t>(midi->data[0] & 0x0fu);
        const int16_t key = static_cast<int16_t>(midi->data[1] & 0x7fu);
        const uint8_t velocity = midi->data[2] & 0x7fu;
        if (status == 0x90u && velocity > 0u) {
            triggerVoice(plugin, key, static_cast<float>(velocity) / 127.0f,
                -1, channel);
        } else if (status == 0x80u || status == 0x90u) {
            plugin.ensemble.release(-1, channel, key);
        }
    }
    return false;
}

bool init(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (instance->host && instance->host->get_extension) {
        instance->hostParams = static_cast<const clap_host_params_t*>(
            instance->host->get_extension(instance->host, CLAP_EXT_PARAMS));
        instance->hostTail = static_cast<const clap_host_tail_t*>(
            instance->host->get_extension(instance->host, CLAP_EXT_TAIL));
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
    instance->sampleRate = std::clamp(sampleRate, 8000.0, 192000.0);
    syncAudioParams(*instance);
    instance->ensemble.prepare(instance->sampleRate);
    instance->ensemble.setTextGestureProgram(instance->activeTextProgram);
    instance->ensemble.setSourceParams(instance->audioParams);
    instance->ensemble.setParams(instance->ensembleParams);
    instance->effects.prepare(instance->sampleRate);
    instance->effects.setParams(instance->effectsParams);
    publishTailSamplesOnAudioThread(*instance);
    instance->smoothedOutputGain = instance->outputGain;
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    instance->ensemble.reset();
    instance->effects.reset();
    instance->audioAuditionGate = false;
    instance->auditionVoice = false;
    instance->controlAuditionGate.store(false, std::memory_order_release);
    instance->pendingAuditions.store(0u, std::memory_order_relaxed);
    storeValue(*instance, kAuditionParamId, 0.0);
    instance->smoothedOutputGain = instance->outputGain;
    instance->outputPeak.store(0.0f, std::memory_order_relaxed);
    instance->internalSpeechPeak.store(0.0f, std::memory_order_relaxed);
    instance->externalMicPeak.store(0.0f, std::memory_order_relaxed);
    instance->detectedPitchHz.store(0.0f, std::memory_order_relaxed);
    instance->pitchConfidence.store(0.0f, std::memory_order_relaxed);
    instance->pitchActive.store(false, std::memory_order_relaxed);
    for (auto& meter : instance->analysisBandMeters) {
        meter.store(0.0f, std::memory_order_relaxed);
    }
    for (auto& meter : instance->synthesisBandMeters) {
        meter.store(0.0f, std::memory_order_relaxed);
    }
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
    s3g::clap_gui::ParamEvent pending;
    while (plugin.guiParamEvents.peek(pending)) {
        if (!pushGuiParamEvent(output, pending)) break;
        plugin.guiParamEvents.pop();
    }
    // GUI values are already published on the producer path. A true result
    // tells callers that host output capacity ended before the matching
    // transaction (possibly its GestureEnd) was drained.
    return plugin.guiParamEvents.peek(pending);
}

void updateGestureTransport(Plugin& plugin, const clap_process_t& processData)
{
    double tempo = 120.0;
    double beat = 0.0;
    bool tempoValid = false;
    bool beatValid = false;
    bool playing = false;
    if (processData.transport) {
        const auto& transport = *processData.transport;
        tempoValid = (transport.flags & CLAP_TRANSPORT_HAS_TEMPO) != 0u
            && std::isfinite(transport.tempo) && transport.tempo > 0.0;
        if (tempoValid) tempo = transport.tempo;
        beatValid = (transport.flags
            & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0u;
        if (beatValid) {
            beat = static_cast<double>(transport.song_pos_beats)
                / static_cast<double>(CLAP_BEATTIME_FACTOR);
        }
        playing = (transport.flags & CLAP_TRANSPORT_IS_PLAYING) != 0u;
    }
    plugin.ensemble.setGestureTransport(tempo, beat, tempoValid,
        beatValid, playing);
    plugin.effects.setTempo(tempo, tempoValid);
}

float audioInputSample(const clap_audio_buffer_t* input,
    uint32_t channel, uint32_t frame)
{
    if (!input || channel >= input->channel_count) return 0.0f;
    double value = 0.0;
    if (input->data32 && input->data32[channel]) {
        value = static_cast<double>(input->data32[channel][frame]);
    } else if (input->data64 && input->data64[channel]) {
        value = input->data64[channel][frame];
    } else {
        return 0.0f;
    }
    return std::isfinite(value)
        ? static_cast<float>(std::clamp(value, -4.0, 4.0)) : 0.0f;
}

bool audioInputChannelAvailable(const clap_audio_buffer_t* input,
    uint32_t channel)
{
    return input && channel < input->channel_count
        && ((input->data32 && input->data32[channel])
            || (input->data64 && input->data64[channel]));
}

clap_process_status process(const clap_plugin_t* plugin,
    const clap_process_t* processData)
{
    auto* instance = self(plugin);
    if (!processData) return CLAP_PROCESS_ERROR;
    serviceTextPrograms(*instance);
    const bool guiParamEventsPending = serviceGuiParamEvents(
        *instance, processData->out_events);
    const bool routingWasDirty = instance->routingControlDirty.exchange(
        false, std::memory_order_acq_rel);
    syncAudioParams(*instance, routingWasDirty);
    updateGestureTransport(*instance, *processData);
    uint32_t auditions = instance->pendingAuditions.exchange(
        0u, std::memory_order_relaxed);
    const bool auditionGate = loadValue(*instance, kAuditionParamId) >= 0.5;
    if (auditionGate && !instance->audioAuditionGate && auditions == 0u) {
        auditions = 1u;
    }
    while (auditions-- > 0u) {
        triggerVoice(*instance, 60, 0.82f, -2, -1, true);
    }
    if (!auditionGate && instance->audioAuditionGate
        && instance->auditionVoice) {
        instance->ensemble.release(-2, -1, 60);
        instance->auditionVoice = false;
    }
    instance->audioAuditionGate = auditionGate;

    const clap_input_events_t* events = processData->in_events;
    const uint32_t eventCount = events ? events->size(events) : 0u;
    uint32_t eventIndex = 0u;
    if (processData->audio_outputs_count == 0u || !processData->audio_outputs) {
        bool paramsChanged = false;
        while (eventIndex < eventCount) {
            paramsChanged |= applyEvent(
                *instance, events->get(events, eventIndex++));
        }
        if (paramsChanged) {
            const bool loadRouting = instance->routingControlDirty.exchange(
                false, std::memory_order_acq_rel);
            syncAudioParams(*instance, loadRouting);
        }
        publishTailSamplesOnAudioThread(*instance);
        deliverTailChangedOnAudioThread(*instance);
        const auto source = instance->effectsParams.resonator.modulatorSource;
        const bool liveMicMonitoring = source
                == s3g::AcapellaResonatorModulatorSource::ExternalMic
            || source == s3g::AcapellaResonatorModulatorSource::Blend;
        return instance->ensemble.active() || instance->effects.active()
                || guiParamEventsPending || liveMicMonitoring
            ? CLAP_PROCESS_CONTINUE : CLAP_PROCESS_SLEEP;
    }

    const auto& output = processData->audio_outputs[0u];
    const uint32_t channels = std::min<uint32_t>(
        output.channel_count, kOutputChannels);
    if (channels == 0u || (!output.data32 && !output.data64)) {
        return CLAP_PROCESS_ERROR;
    }
    const clap_audio_buffer_t* externalMic =
        processData->audio_inputs_count > 0u && processData->audio_inputs
        ? &processData->audio_inputs[0u] : nullptr;
    const bool micLeftAvailable = audioInputChannelAvailable(
        externalMic, 0u);
    const bool micRightAvailable = audioInputChannelAvailable(
        externalMic, 1u);
    const bool externalMicAvailable = micLeftAvailable || micRightAvailable;
    const float gainCoefficient = 1.0f - std::exp(
        -1.0f / (0.010f * static_cast<float>(instance->sampleRate)));
    float blockPeak = 0.0f;
    float blockInternalSpeechPeak = 0.0f;
    float blockMicPeak = 0.0f;
    // Scalar targets are internally smoothed, so coalesce dense automation
    // onto a short control quantum instead of rebuilding every synthesis
    // parameter aggregate multiple times per sample. Eight samples is at
    // most 1 ms at the lowest supported rate and preserves event order; note
    // edges still refresh their performance state immediately in
    // triggerVoice(). Matrix cells and trims use their dedicated RT setters.
    constexpr uint32_t kParameterSyncQuantum = 8u;
    bool pendingParamSync = false;
    for (uint32_t frame = 0u; frame < processData->frames_count; ++frame) {
        while (eventIndex < eventCount) {
            const auto* event = events->get(events, eventIndex);
            if (!event || event->time > frame) break;
            pendingParamSync |= applyEvent(*instance, event);
            ++eventIndex;
        }
        const bool controlBoundary = (frame % kParameterSyncQuantum) == 0u
            || frame + 1u == processData->frames_count;
        if (pendingParamSync && controlBoundary) {
            const bool loadRouting = instance->routingControlDirty.exchange(
                false, std::memory_order_acq_rel);
            syncAudioParams(*instance, loadRouting);
            pendingParamSync = false;
        }
        instance->smoothedOutputGain += (instance->outputGain
            - instance->smoothedOutputGain) * gainCoefficient;
        const auto ensemble = instance->ensemble.processFrame();
        // A live mic owns the articulation, so its MIDI carrier must not
        // disappear during a rest in the internal phrase. Internal Speech
        // and Blend retain the text engine's phoneme/boundary metadata.
        const auto modulatorSource = instance->effectsParams.resonator
            .modulatorSource;
        auto gesture = instance->ensemble.resonatorGesture();
        if (modulatorSource
            == s3g::AcapellaResonatorModulatorSource::ExternalMic) {
            gesture = instance->ensemble.midiCarrierGesture();
        } else if (modulatorSource
                == s3g::AcapellaResonatorModulatorSource::Blend
            && externalMicAvailable) {
            const auto carrier = instance->ensemble.midiCarrierGesture();
            const bool internalSpeechRest = !gesture.active
                || gesture.phoneme == s3g::AcapellaPhoneme::Silence
                || (gesture.flags & s3g::kAcapellaForcedRest) != 0u;
            if (internalSpeechRest) {
                // During a text rest, let the mic continue to articulate the
                // held chord with neutral metadata.
                gesture = carrier;
            } else {
                // Preserve speech phoneme/boundary metadata while decoupling
                // the oscillator envelope from internal articulation rests.
                gesture.voiceCount = carrier.voiceCount;
                gesture.voiceFrequencyHz = carrier.voiceFrequencyHz;
                gesture.voiceGain = carrier.voiceGain;
                gesture.voiceInstanceIds = carrier.voiceInstanceIds;
            }
        }
        instance->effects.setResonatorGesture(gesture);
        const float micLeft = micLeftAvailable
            ? audioInputSample(externalMic, 0u, frame)
            : micRightAvailable
                ? audioInputSample(externalMic, 1u, frame) : 0.0f;
        const float micRight = micRightAvailable
            ? audioInputSample(externalMic, 1u, frame) : micLeft;
        blockInternalSpeechPeak = std::max(blockInternalSpeechPeak,
            std::max(std::abs(ensemble.left), std::abs(ensemble.right)));
        blockMicPeak = std::max(blockMicPeak,
            std::max(std::abs(micLeft), std::abs(micRight)));
        const auto vocal = instance->effects.processFrameStereo(
            ensemble.left, ensemble.right, micLeft, micRight,
            externalMicAvailable);
        const float samples[kOutputChannels] {
            vocal.left * instance->smoothedOutputGain,
            vocal.right * instance->smoothedOutputGain,
        };
        blockPeak = std::max(blockPeak,
            std::max(std::abs(samples[0]), std::abs(samples[1])));
        for (uint32_t channel = 0u; channel < channels; ++channel) {
            if (output.data32 && output.data32[channel]) {
                output.data32[channel][frame] = samples[channel];
            }
            if (output.data64 && output.data64[channel]) {
                output.data64[channel][frame] = samples[channel];
            }
        }
        for (uint32_t channel = channels;
             channel < output.channel_count; ++channel) {
            if (output.data32 && output.data32[channel]) {
                output.data32[channel][frame] = 0.0f;
            }
            if (output.data64 && output.data64[channel]) {
                output.data64[channel][frame] = 0.0;
            }
        }
    }
    while (eventIndex < eventCount) {
        pendingParamSync |= applyEvent(
            *instance, events->get(events, eventIndex++));
    }
    if (pendingParamSync) {
        const bool loadRouting = instance->routingControlDirty.exchange(
            false, std::memory_order_acq_rel);
        syncAudioParams(*instance, loadRouting);
    }
    publishTailSamplesOnAudioThread(*instance);
    deliverTailChangedOnAudioThread(*instance);
    instance->outputPeak.store(blockPeak, std::memory_order_relaxed);
    instance->internalSpeechPeak.store(blockInternalSpeechPeak,
        std::memory_order_relaxed);
    instance->externalMicPeak.store(blockMicPeak, std::memory_order_relaxed);
    const auto meters = instance->effects.resonatorMeterSnapshot();
    for (uint32_t band = 0u; band < kMatrixBands; ++band) {
        instance->analysisBandMeters[band].store(meters.analysis[band],
            std::memory_order_relaxed);
        instance->synthesisBandMeters[band].store(meters.synthesis[band],
            std::memory_order_relaxed);
    }
    instance->detectedPitchHz.store(meters.detectedPitchHz,
        std::memory_order_relaxed);
    instance->pitchConfidence.store(meters.pitchConfidence,
        std::memory_order_relaxed);
    instance->pitchActive.store(meters.pitchActive,
        std::memory_order_relaxed);
    // A live-input effect must never depend on the current callback's buffer
    // availability to remain awake. REAPER and other hosts may temporarily
    // omit an input buffer while changing routing, anticipative processing, or
    // record-monitor state. Returning SLEEP in that callback can strand the
    // effect: the interface continues receiving audio, but neither the MIC
    // meter nor pitch tracker is processed again. External Mic and connected
    // Blend therefore remain continuous monitoring modes even through an
    // absent/zero input interval.
    const auto monitoredSource
        = instance->effectsParams.resonator.modulatorSource;
    const bool liveMicMonitoring = monitoredSource
            == s3g::AcapellaResonatorModulatorSource::ExternalMic
        || monitoredSource
            == s3g::AcapellaResonatorModulatorSource::Blend;
    return instance->ensemble.active() || instance->effects.active()
            || guiParamEventsPending || liveMicMonitoring
        ? CLAP_PROCESS_CONTINUE : CLAP_PROCESS_SLEEP;
}

void onMainThread(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (instance->pendingParamValuesRescan.exchange(false,
            std::memory_order_acq_rel)
        && instance->hostParams && instance->hostParams->rescan) {
        instance->hostParams->rescan(
            instance->host, CLAP_PARAM_RESCAN_VALUES);
    }
    s3g::clap_gui::ParamEvent pending;
    if (instance->guiParamEvents.peek(pending)) {
        requestGuiParamService(*instance);
    }
}

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput)
{
    (void)isInput;
    return 1u;
}

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_audio_port_info_t* info)
{
    if (!info || index != 0u) return false;
    *info = {};
    info->id = isInput ? 10u : 20u;
    std::strncpy(info->name,
        isInput ? "Modulator In" : "Formant Matrix Out",
        sizeof(info->name) - 1u);
    info->flags = CLAP_AUDIO_PORT_IS_MAIN
        | CLAP_AUDIO_PORT_SUPPORTS_64BITS;
    info->channel_count = kOutputChannels;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = isInput ? 20u : 10u;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

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
    std::strncpy(info->name, "MIDI In", sizeof(info->name) - 1u);
    return true;
}

const clap_plugin_note_ports_t notePorts { notePortsCount, notePortsGet };

uint32_t paramsCount(const clap_plugin_t*) { return kParamCount; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index,
    clap_param_info_t* info)
{
    if (!info || index >= kParamDefs.size()) return false;
    const auto& def = kParamDefs[index];
    *info = {};
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    if (def.stepped) info->flags |= CLAP_PARAM_IS_STEPPED;
    uint32_t band = 0u;
    bool sceneB = false;
    uint32_t destination = 0u;
    uint32_t source = 0u;
    if (decodeBandTrimParam(def.id, band)) {
        std::snprintf(info->name, sizeof(info->name),
            "Band %02u Trim", band + 1u);
    } else if (decodeMatrixParam(def.id, sceneB, destination, source)) {
        std::snprintf(info->name, sizeof(info->name),
            "%c B%02u to B%02u", sceneB ? 'B' : 'A',
            source + 1u, destination + 1u);
    } else {
        std::strncpy(info->name, def.name, sizeof(info->name) - 1u);
    }
    std::strncpy(info->module, def.module, sizeof(info->module) - 1u);
    info->min_value = def.minimum;
    info->max_value = def.maximum;
    info->default_value = def.defaultValue;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value || !paramDef(id)) return false;
    *value = loadValue(*self(plugin), id);
    return true;
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value,
    char* display, uint32_t size)
{
    if (!display || size == 0u || !paramDef(id)) return false;
    if (id == kPresetParamId) {
        constexpr const char* names[] {
            "Speech Matrix", "Rhythmic Bands", "Breath Carrier", "Pressed Filter",
            "Folded Formant", "Sub Coupling", "Vowel Suspension",
            "Breath Mirror", "Formant Loom", "Resonant Rain",
            "Carrier Choir", "Consonant Shadow", "Moving Scar",
            "Chord Glass", "Classic Mic", "Formant Glide",
            "Fixed Circuit", "Glass Harmony", "Public Address",
            "Pocket Radio", "Low Persona", "Bright Persona",
            "Broken Relay", "Vocal Alloy", "Mouth Circuit",
            "Impulse Matrix", "Gated Bank", "Pulse Bank",
            "Rhythm Transfer", "Shift Morph", "Spectral Drone", "Custom"
        };
        const uint32_t index = std::min<uint32_t>(31u,
            static_cast<uint32_t>(std::round(value)));
        std::snprintf(display, size, "%s", names[index]);
    } else if (id == kDeliveryParamId) {
        std::snprintf(display, size, "%s",
            value >= 0.5 ? "Rhythmic" : "Sustained");
    } else if (id == kVowelParamId) {
        constexpr const char* names[] { "A", "E", "I", "O", "U", "Schwa" };
        const uint32_t index = std::min<uint32_t>(5u,
            static_cast<uint32_t>(std::round(value)));
        std::snprintf(display, size, "%s", names[index]);
    } else if (id == kOnsetParamId) {
        constexpr const char* names[] {
            "None", "B", "Ch", "D", "Dh", "F", "G", "H", "J", "K",
            "L", "M", "N", "P", "R", "S", "Sh", "T", "Th", "V",
            "W", "Y", "Z", "Ng", "Zh"
        };
        const uint32_t index = std::min<uint32_t>(24u,
            static_cast<uint32_t>(std::round(value)));
        std::snprintf(display, size, "%s", names[index]);
    } else if (id == kGestureSequenceParamId) {
        constexpr const char* names[] {
            "Off", "Vowel Orbit", "Resonant Chant", "Noise Arc", "Consonant Grid",
            "Text Phrase"
        };
        const uint32_t index = std::min<uint32_t>(5u,
            static_cast<uint32_t>(std::round(value)));
        std::snprintf(display, size, "%s", names[index]);
    } else if (id == kGestureSyncParamId) {
        constexpr const char* names[] {
            "Free", "Note Sync", "Transport Sync"
        };
        const uint32_t index = std::min<uint32_t>(2u,
            static_cast<uint32_t>(std::round(value)));
        std::snprintf(display, size, "%s", names[index]);
    } else if (id == kGestureDivisionParamId) {
        constexpr const char* names[] {
            "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8",
            "1/8D", "1/4T", "1/4", "1/4D", "1/2", "1/1"
        };
        const uint32_t index = std::min<uint32_t>(11u,
            static_cast<uint32_t>(std::round(value)));
        std::snprintf(display, size, "%s", names[index]);
    } else if (id == kEchoHeadsParamId) {
        std::snprintf(display, size, "%s", s3g::drumEchoHeadModeName(
            static_cast<s3g::DrumEchoHeadMode>(std::clamp<uint32_t>(
                static_cast<uint32_t>(std::round(value)), 0u,
                s3g::kDrumEchoHeadModeCount - 1u))));
    } else if (id == kEchoClockParamId) {
        std::snprintf(display, size, "%s", s3g::drumEchoClockName(
            static_cast<s3g::DrumEchoClock>(std::clamp<uint32_t>(
                static_cast<uint32_t>(std::round(value)), 0u,
                s3g::kDrumEchoClockCount - 1u))));
    } else if (id == kBankModeParamId) {
        constexpr const char* names[] { "Vocoder", "Hybrid", "Filter Bank" };
        const uint32_t index = std::min<uint32_t>(2u,
            static_cast<uint32_t>(std::round(value)));
        std::snprintf(display, size, "%s", names[index]);
    } else if (id == kModulatorSourceParamId) {
        constexpr const char* names[] {
            "External Mic", "Internal Speech", "Blend"
        };
        const uint32_t index = std::min<uint32_t>(2u,
            static_cast<uint32_t>(std::round(value)));
        std::snprintf(display, size, "%s", names[index]);
    } else if (id == kCarrierShapeParamId) {
        constexpr const char* names[] {
            "Glottal", "Saw", "Pulse", "Fold", "Noise"
        };
        const uint32_t index = std::min<uint32_t>(4u,
            static_cast<uint32_t>(std::round(value)));
        std::snprintf(display, size, "%s", names[index]);
    } else if (id == kMatrixModeParamId) {
        constexpr const char* names[] {
            "Identity", "Rotate", "Mirror", "Chord", "Sparse", "Custom"
        };
        const uint32_t index = std::min<uint32_t>(5u,
            static_cast<uint32_t>(std::round(value)));
        std::snprintf(display, size, "%s", names[index]);
    } else if (id == kFreezeTriggerParamId) {
        constexpr const char* names[] {
            "Continuous", "Note", "Phoneme", "Syllable", "Word", "Rest"
        };
        const uint32_t index = std::min<uint32_t>(5u,
            static_cast<uint32_t>(std::round(value)));
        std::snprintf(display, size, "%s", names[index]);
    } else if (id == kBandLayoutParamId) {
        std::snprintf(display, size, "%s",
            value >= 0.5 ? "Wide 16" : "Speech 22");
    } else if (id == kAnalysisSlopeParamId) {
        constexpr const char* names[] {
            "4 Pole", "8 Pole", "Mouth Model"
        };
        const uint32_t index = std::min<uint32_t>(2u,
            static_cast<uint32_t>(std::round(value)));
        std::snprintf(display, size, "%s", names[index]);
    } else if (id == kTransferModeParamId) {
        std::snprintf(display, size, "%s",
            value >= 0.5 ? "Precision" : "Expressive");
    } else if (id == kHfDetailModeParamId) {
        constexpr const char* names[] {
            "Synthetic", "Switched", "Direct"
        };
        const uint32_t index = std::min<uint32_t>(2u,
            static_cast<uint32_t>(std::round(value)));
        std::snprintf(display, size, "%s", names[index]);
    } else if (id == kCarrierPitchSourceParamId) {
        std::snprintf(display, size, "%s",
            value >= 0.5 ? "Voice Pitch" : "MIDI");
    } else if (id == kPitchScaleRootParamId) {
        constexpr const char* names[] {
            "C", "C#", "D", "D#", "E", "F",
            "F#", "G", "G#", "A", "A#", "B"
        };
        const uint32_t index = std::min<uint32_t>(11u,
            static_cast<uint32_t>(std::round(value)));
        std::snprintf(display, size, "%s", names[index]);
    } else if (id == kPitchScaleParamId) {
        const auto scale = static_cast<s3g::AcapellaResonatorPitchScale>(
            std::min<uint32_t>(
                static_cast<uint32_t>(std::round(value)),
                s3g::kAcapellaResonatorPitchScaleCount - 1u));
        std::snprintf(display, size, "%s",
            s3g::acapellaResonatorPitchScaleName(scale));
    } else if (id == kVoicingModeParamId) {
        constexpr const char* names[] { "Tonal", "Noise", "Blend", "Detect" };
        const uint32_t index = std::min<uint32_t>(3u,
            static_cast<uint32_t>(std::round(value)));
        std::snprintf(display, size, "%s", names[index]);
    } else if (id == kStereoModeParamId) {
        constexpr const char* names[] { "Mono", "Spread", "Odd / Even" };
        const uint32_t index = std::min<uint32_t>(2u,
            static_cast<uint32_t>(std::round(value)));
        std::snprintf(display, size, "%s", names[index]);
    } else if (id == kCarrierLfoShapeParamId) {
        std::snprintf(display, size, "%s",
            value >= 0.5 ? "Square" : "Triangle");
    } else if (id == kCarrierLfoSyncParamId) {
        std::snprintf(display, size, "%s",
            value >= 0.5 ? "Host Sync" : "Free");
    } else if (id == kCarrierLfoDivisionParamId) {
        constexpr const char* names[] {
            "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8",
            "1/8D", "1/4T", "1/4", "1/4D", "1/2", "1/1"
        };
        const uint32_t index = std::min<uint32_t>(11u,
            static_cast<uint32_t>(std::round(value)));
        std::snprintf(display, size, "%s", names[index]);
    } else if (id == kPitchHoldParamId
        && value >= s3g::kAcapellaResonatorInfinitePitchHoldMs - 0.5) {
        std::snprintf(display, size, "Infinite");
    } else if (id == kDurationParamId || id == kGlideParamId
        || id == kAttackParamId || id == kReleaseParamId
        || id == kRetriggerParamId || id == kEchoTimeParamId
        || id == kDoubleTimingParamId || id == kOnsetGuardParamId
        || id == kBankAttackParamId || id == kBankReleaseParamId
        || id == kBankBlurParamId || id == kVoicedTransitionParamId
        || id == kUnvoicedTransitionParamId || id == kPitchHoldParamId) {
        std::snprintf(display, size, "%.1f ms", value);
    } else if (id == kVibratoRateParamId || id == kGestureRateParamId
        || id == kCarrierLfoRateParamId) {
        std::snprintf(display, size, "%.2f Hz", value);
    } else if (id == kVibratoDepthParamId || id == kPitchDriftParamId
        || id == kDoubleDetuneParamId) {
        std::snprintf(display, size, "%.1f ct", value);
    } else if (id == kScoopParamId || id == kDeclinationParamId
        || id == kBandShiftParamId || id == kCarrierLfoDepthParamId) {
        std::snprintf(display, size, "%+.2f st", value);
    } else if (id == kOutputParamId || id == kFuzzDriveParamId
        || id == kBankDriveParamId || id == kMicGainParamId) {
        std::snprintf(display, size, "%+.1f dB", value);
    } else if (id == kFuzzToneParamId) {
        std::snprintf(display, size, "%.0f Hz", value);
    } else if (id == kCouplingParamId) {
        std::snprintf(display, size, "%+.0f bands", value);
    } else if (id >= kMatrixAParamBase
        && id < kMatrixBParamBase + kMatrixCells) {
        std::snprintf(display, size, "%+.3f", value);
    } else if (id >= kBandTrimParamBase
        && id < kBandTrimParamBase + kMatrixBands) {
        std::snprintf(display, size, "%.0f%%", value * 100.0);
    } else if (id == kCarrierPulseWidthParamId) {
        std::snprintf(display, size, "%.0f%%", value * 100.0);
    } else if (id == kCarrierPwmDepthParamId) {
        std::snprintf(display, size, "%.0f%%", value * 100.0);
    } else if (id == kCarrierColorParamId
        || id == kBandStretchParamId || id == kBandTiltParamId) {
        std::snprintf(display, size, "%+.0f%%", value * 100.0);
    } else if (id == kPolyphonyParamId) {
        std::snprintf(display, size, "%.0f voices", value);
    } else if (id == kGestureLoopParamId) {
        std::snprintf(display, size, "%s",
            value >= 0.5 ? "Loop While Held" : "One Shot");
    } else if (id == kAuditionParamId) {
        std::snprintf(display, size, "%s", value >= 0.5 ? "On" : "Off");
    } else if (id == kTractParamId || id == kOpenQuotientParamId) {
        std::snprintf(display, size, "%.3f", value);
    } else {
        std::snprintf(display, size, "%.0f%%", value * 100.0);
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id,
    const char* display, double* value)
{
    const auto* def = paramDef(id);
    if (!display || !value || !def) return false;
    if (id == kPitchHoldParamId
        && (std::strcmp(display, "Infinite") == 0
            || std::strcmp(display, "infinite") == 0
            || std::strcmp(display, "Inf") == 0
            || std::strcmp(display, "inf") == 0)) {
        *value = s3g::kAcapellaResonatorInfinitePitchHoldMs;
        return true;
    }
    if (id == kPresetParamId) {
        constexpr const char* names[] {
            "Speech Matrix", "Rhythmic Bands", "Breath Carrier", "Pressed Filter",
            "Folded Formant", "Sub Coupling", "Vowel Suspension",
            "Breath Mirror", "Formant Loom", "Resonant Rain",
            "Carrier Choir", "Consonant Shadow", "Moving Scar",
            "Chord Glass", "Classic Mic", "Formant Glide",
            "Fixed Circuit", "Glass Harmony", "Public Address",
            "Pocket Radio", "Low Persona", "Bright Persona",
            "Broken Relay", "Vocal Alloy", "Mouth Circuit",
            "Impulse Matrix", "Gated Bank", "Pulse Bank",
            "Rhythm Transfer", "Shift Morph", "Spectral Drone", "Custom"
        };
        for (uint32_t index = 0u; index < 32u; ++index) {
            if (std::strcmp(display, names[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    } else if (id == kDeliveryParamId) {
        if (std::strstr(display, "Rhythmic")
            || std::strstr(display, "rhythmic")) {
            *value = 1.0;
            return true;
        }
        if (std::strstr(display, "Sustained")
            || std::strstr(display, "sustained")) {
            *value = 0.0;
            return true;
        }
    } else if (id == kVowelParamId) {
        constexpr const char* names[] { "A", "E", "I", "O", "U", "Schwa" };
        for (uint32_t index = 0u; index < 6u; ++index) {
            if (std::strcmp(display, names[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    } else if (id == kOnsetParamId) {
        constexpr const char* names[] {
            "None", "B", "Ch", "D", "Dh", "F", "G", "H", "J", "K",
            "L", "M", "N", "P", "R", "S", "Sh", "T", "Th", "V",
            "W", "Y", "Z", "Ng", "Zh"
        };
        for (uint32_t index = 0u; index < 25u; ++index) {
            if (std::strcmp(display, names[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    } else if (id == kGestureSequenceParamId) {
        constexpr const char* names[] {
            "Off", "Vowel Orbit", "Resonant Chant", "Noise Arc", "Consonant Grid",
            "Text Phrase"
        };
        for (uint32_t index = 0u; index < 6u; ++index) {
            if (std::strcmp(display, names[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    } else if (id == kGestureSyncParamId) {
        constexpr const char* names[] {
            "Free", "Note Sync", "Transport Sync"
        };
        for (uint32_t index = 0u; index < 3u; ++index) {
            if (std::strcmp(display, names[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    } else if (id == kGestureDivisionParamId) {
        constexpr const char* names[] {
            "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8",
            "1/8D", "1/4T", "1/4", "1/4D", "1/2", "1/1"
        };
        for (uint32_t index = 0u; index < 12u; ++index) {
            if (std::strcmp(display, names[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    } else if (id == kGestureLoopParamId) {
        if (std::strcmp(display, "Loop While Held") == 0
            || std::strcmp(display, "Loop") == 0) {
            *value = 1.0;
            return true;
        }
        if (std::strcmp(display, "One Shot") == 0) {
            *value = 0.0;
            return true;
        }
    } else if (id == kEchoHeadsParamId) {
        for (uint32_t index = 0u; index < s3g::kDrumEchoHeadModeCount;
             ++index) {
            if (std::strcmp(display, s3g::drumEchoHeadModeName(
                    static_cast<s3g::DrumEchoHeadMode>(index))) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    } else if (id == kEchoClockParamId) {
        for (uint32_t index = 0u; index < s3g::kDrumEchoClockCount; ++index) {
            if (std::strcmp(display, s3g::drumEchoClockName(
                    static_cast<s3g::DrumEchoClock>(index))) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    } else if (id == kBankModeParamId) {
        constexpr const char* names[] { "Vocoder", "Hybrid", "Filter Bank" };
        for (uint32_t index = 0u; index < 3u; ++index) {
            if (std::strcmp(display, names[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    } else if (id == kModulatorSourceParamId) {
        constexpr const char* names[] {
            "External Mic", "Internal Speech", "Blend"
        };
        for (uint32_t index = 0u; index < 3u; ++index) {
            if (std::strcmp(display, names[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    } else if (id == kCarrierShapeParamId) {
        constexpr const char* names[] {
            "Glottal", "Saw", "Pulse", "Fold", "Noise"
        };
        for (uint32_t index = 0u; index < 5u; ++index) {
            if (std::strcmp(display, names[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    } else if (id == kMatrixModeParamId) {
        constexpr const char* names[] {
            "Identity", "Rotate", "Mirror", "Chord", "Sparse", "Custom"
        };
        for (uint32_t index = 0u; index < 6u; ++index) {
            if (std::strcmp(display, names[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    } else if (id == kFreezeTriggerParamId) {
        constexpr const char* names[] {
            "Continuous", "Note", "Phoneme", "Syllable", "Word", "Rest"
        };
        for (uint32_t index = 0u; index < 6u; ++index) {
            if (std::strcmp(display, names[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    } else if (id == kBandLayoutParamId) {
        if (std::strcmp(display, "Speech 22") == 0) {
            *value = 0.0;
            return true;
        }
        if (std::strcmp(display, "Wide 16") == 0) {
            *value = 1.0;
            return true;
        }
    } else if (id == kAnalysisSlopeParamId) {
        if (std::strcmp(display, "4 Pole") == 0) {
            *value = 0.0;
            return true;
        }
        if (std::strcmp(display, "8 Pole") == 0) {
            *value = 1.0;
            return true;
        }
        if (std::strcmp(display, "Mouth Model") == 0) {
            *value = 2.0;
            return true;
        }
    } else if (id == kTransferModeParamId) {
        if (std::strcmp(display, "Expressive") == 0) {
            *value = 0.0;
            return true;
        }
        if (std::strcmp(display, "Precision") == 0) {
            *value = 1.0;
            return true;
        }
    } else if (id == kHfDetailModeParamId) {
        constexpr const char* names[] {
            "Synthetic", "Switched", "Direct"
        };
        for (uint32_t index = 0u; index < 3u; ++index) {
            if (std::strcmp(display, names[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    } else if (id == kCarrierPitchSourceParamId) {
        if (std::strcmp(display, "MIDI") == 0) {
            *value = 0.0;
            return true;
        }
        if (std::strcmp(display, "Voice Pitch") == 0) {
            *value = 1.0;
            return true;
        }
    } else if (id == kPitchScaleRootParamId) {
        constexpr const char* names[] {
            "C", "C#", "D", "D#", "E", "F",
            "F#", "G", "G#", "A", "A#", "B"
        };
        for (uint32_t index = 0u; index < 12u; ++index) {
            if (std::strcmp(display, names[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    } else if (id == kPitchScaleParamId) {
        if (std::strcmp(display, "Continuous") == 0
            || std::strcmp(display, "CONTINUOUS") == 0) {
            *value = 0.0;
            return true;
        }
        if (std::strcmp(display, "Natural Minor") == 0
            || std::strcmp(display, "NATURAL MINOR") == 0) {
            *value = static_cast<double>(
                s3g::AcapellaResonatorPitchScale::NaturalMinor);
            return true;
        }
        uint32_t sharedScale = 0u;
        if (s3g::musicalScaleValueFromText(display, sharedScale)) {
            *value = static_cast<double>(
                s3g::acapellaResonatorPitchScaleValue(sharedScale));
            return true;
        }
    } else if (id == kVoicingModeParamId) {
        constexpr const char* names[] { "Tonal", "Noise", "Blend", "Detect" };
        for (uint32_t index = 0u; index < 4u; ++index) {
            if (std::strcmp(display, names[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    } else if (id == kStereoModeParamId) {
        constexpr const char* names[] { "Mono", "Spread", "Odd / Even" };
        for (uint32_t index = 0u; index < 3u; ++index) {
            if (std::strcmp(display, names[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    } else if (id == kCarrierLfoShapeParamId) {
        if (std::strcmp(display, "Triangle") == 0) {
            *value = 0.0;
            return true;
        }
        if (std::strcmp(display, "Square") == 0) {
            *value = 1.0;
            return true;
        }
    } else if (id == kCarrierLfoSyncParamId) {
        if (std::strcmp(display, "Free") == 0) {
            *value = 0.0;
            return true;
        }
        if (std::strcmp(display, "Host Sync") == 0) {
            *value = 1.0;
            return true;
        }
    } else if (id == kCarrierLfoDivisionParamId) {
        constexpr const char* names[] {
            "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8",
            "1/8D", "1/4T", "1/4", "1/4D", "1/2", "1/1"
        };
        for (uint32_t index = 0u; index < 12u; ++index) {
            if (std::strcmp(display, names[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    } else if (id == kAuditionParamId) {
        if (std::strcmp(display, "On") == 0) {
            *value = 1.0;
            return true;
        }
        if (std::strcmp(display, "Off") == 0) {
            *value = 0.0;
            return true;
        }
    }
    *value = std::atof(display);
    if (std::strchr(display, '%') && id != kOutputParamId) *value *= 0.01;
    *value = clampValue(*def, *value);
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
        const auto* param = reinterpret_cast<const clap_event_param_value_t*>(event);
        publishControlParam(*instance, param->param_id, param->value);
    }
    if (serviceGuiParamEvents(*instance, output)) {
        // A host output list may be smaller than a 22x22 scene transaction.
        // Continue on a later main-thread turn rather than recursively asking
        // for another flush from inside params.flush().
        if (instance->host && instance->host->request_callback) {
            instance->host->request_callback(instance->host);
        } else if (instance->host && instance->host->request_process) {
            instance->host->request_process(instance->host);
        }
    }
}

const clap_plugin_params_t paramsExt {
    paramsCount, paramsGetInfo, paramsGetValue,
    paramsValueToText, paramsTextToValue, paramsFlush
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    StateHeader header;
    std::array<double, kSavedParamCount> values {};
    const auto* instance = self(plugin);
    for (uint32_t index = 0u; index < values.size(); ++index) {
        values[index] = loadValue(*instance, kSavedParamIds[index]);
    }
    const PhraseState phrase = loadPhrase(*instance);
    return s3g::clap_state::writeAll(stream, &header, sizeof(header))
        && s3g::clap_state::writeAll(stream, values.data(),
            sizeof(values))
        && s3g::clap_state::writeAll(stream, &phrase, sizeof(phrase));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    StateHeader header;
    if (!s3g::clap_state::readAll(stream, &header, sizeof(header))) {
        return false;
    }
    auto* instance = self(plugin);
    if (header.version == 1u) {
        // Version 1 stored the original parameter IDs 1..24 in ID order.
        std::array<double, 24u> values {};
        if (!s3g::clap_state::readAll(stream, values.data(),
                sizeof(values))) return false;
        for (uint32_t index = 0u; index < values.size(); ++index) {
            const clap_id id = index + 1u;
            const auto* def = paramDef(id);
            double value = values[index];
            // Version 1 used profile slot 4 for Custom. Version 2 inserts the
            // two extreme profiles before Custom, now slot 6.
            if (id == kPresetParamId && value >= 3.5) {
                value = kCustomPreset;
            }
            if (def) storeValue(*instance, id, clampValue(*def, value));
        }
        const uint32_t oldProfile = static_cast<uint32_t>(std::round(
            std::clamp(values[0], 0.0, 4.0)));
        if (oldProfile < 4u) {
            const auto preset = static_cast<s3g::AcapellaSourcePreset>(
                oldProfile);
            storeEffectsParams(*instance, s3g::acapellaVocalFxPreset(
                preset));
            storeEnsembleParams(*instance, s3g::acapellaEnsemblePreset(
                preset));
            storeHybridUpgradeDefaults(*instance, preset);
        }
    } else if (header.version == 2u) {
        std::array<double, 28u> values {};
        if (!s3g::clap_state::readAll(stream, values.data(),
                sizeof(values))) return false;
        for (uint32_t index = 0u; index < values.size(); ++index) {
            const clap_id id = kSavedParamIds[index];
            const auto* def = paramDef(id);
            if (def) storeValue(*instance, id,
                clampValue(*def, values[index]));
        }
        const uint32_t oldProfile = static_cast<uint32_t>(std::round(
            std::clamp(values[0], 0.0, 6.0)));
        if (oldProfile < 6u) {
            const auto preset = static_cast<s3g::AcapellaSourcePreset>(
                oldProfile);
            storeEffectsParams(*instance, s3g::acapellaVocalFxPreset(
                preset));
            storeEnsembleParams(*instance, s3g::acapellaEnsemblePreset(
                preset));
            storeHybridUpgradeDefaults(*instance, preset);
        }
    } else if (header.version == 3u) {
        std::array<double, 38u> values {};
        if (!s3g::clap_state::readAll(stream, values.data(),
                sizeof(values))) return false;
        for (uint32_t index = 0u; index < values.size(); ++index) {
            const clap_id id = kSavedParamIds[index];
            const auto* def = paramDef(id);
            if (def) storeValue(*instance, id,
                clampValue(*def, values[index]));
        }
        const uint32_t oldProfile = static_cast<uint32_t>(std::round(
            std::clamp(values[0], 0.0, 6.0)));
        if (oldProfile < 6u) {
            const auto preset = static_cast<s3g::AcapellaSourcePreset>(
                oldProfile);
            storeEnsembleParams(*instance,
                s3g::acapellaEnsemblePreset(preset));
            storeHybridUpgradeDefaults(*instance, preset);
        }
    } else if (header.version == 4u) {
        std::array<double, 45u> values {};
        if (!s3g::clap_state::readAll(stream, values.data(),
                sizeof(values))) return false;
        for (uint32_t index = 0u; index < values.size(); ++index) {
            const clap_id id = kSavedParamIds[index];
            const auto* def = paramDef(id);
            if (def) storeValue(*instance, id,
                clampValue(*def, values[index]));
        }
        const uint32_t oldProfile = static_cast<uint32_t>(std::round(
            std::clamp(values[0], 0.0, 6.0)));
        if (oldProfile < 6u) {
            storeHybridUpgradeDefaults(*instance,
                static_cast<s3g::AcapellaSourcePreset>(oldProfile));
        }
    } else if (header.version == 5u) {
        std::array<double, 47u> values {};
        if (!s3g::clap_state::readAll(stream, values.data(),
                sizeof(values))) return false;
        for (uint32_t index = 0u; index < values.size(); ++index) {
            const clap_id id = kSavedParamIds[index];
            const auto* def = paramDef(id);
            if (def) storeValue(*instance, id,
                clampValue(*def, values[index]));
        }
        const uint32_t oldProfile = static_cast<uint32_t>(std::round(
            std::clamp(values[0], 0.0, 6.0)));
        if (oldProfile < 6u) {
            storeWaveguideUpgradeDefaults(*instance,
                static_cast<s3g::AcapellaSourcePreset>(oldProfile));
        }
    } else if (header.version == 6u) {
        std::array<double, 49u> values {};
        if (!s3g::clap_state::readAll(stream, values.data(),
                sizeof(values))) return false;
        for (uint32_t index = 0u; index < values.size(); ++index) {
            const clap_id id = kSavedParamIds[index];
            const auto* def = paramDef(id);
            if (def) storeValue(*instance, id,
                clampValue(*def, values[index]));
        }
        const uint32_t oldProfile = static_cast<uint32_t>(std::round(
            std::clamp(values[0], 0.0, 6.0)));
        if (oldProfile < 6u) {
            storeSequencerUpgradeDefaults(*instance,
                static_cast<s3g::AcapellaSourcePreset>(oldProfile));
        }
    } else if (header.version == 7u) {
        std::array<double, 53u> values {};
        if (!s3g::clap_state::readAll(stream, values.data(),
                sizeof(values))) return false;
        for (uint32_t index = 0u; index < values.size(); ++index) {
            const clap_id id = kSavedParamIds[index];
            const auto* def = paramDef(id);
            if (def) storeValue(*instance, id,
                clampValue(*def, values[index]));
        }
        storeValue(*instance, kGestureSyncParamId, 0.0);
        storeValue(*instance, kGestureDivisionParamId, 5.0);
    } else if (header.version == 8u) {
        std::array<double, 55u> values {};
        if (!s3g::clap_state::readAll(stream, values.data(),
                sizeof(values))) return false;
        for (uint32_t index = 0u; index < values.size(); ++index) {
            const clap_id id = kSavedParamIds[index];
            const auto* def = paramDef(id);
            if (def) storeValue(*instance, id,
                clampValue(*def, values[index]));
        }
        storeValue(*instance, kIntelligibilityParamId, 0.78);
        PhraseState phrase;
        if (!s3g::clap_state::readAll(stream, &phrase,
                sizeof(phrase))) return false;
        phrase.length = std::min<uint32_t>(phrase.length,
            kPhraseCapacity - 1u);
        phrase.text[phrase.length] = '\0';
        if (!publishTextPhrase(*instance, phrase.text.data())) return false;
    } else if (header.version == 10u) {
        constexpr uint32_t oldSavedParamCount = 63u;
        std::array<double, oldSavedParamCount> values {};
        if (!s3g::clap_state::readAll(stream, values.data(),
                sizeof(values))) return false;
        for (uint32_t index = 0u; index < values.size(); ++index) {
            const clap_id id = kSavedParamIds[index];
            const auto* def = paramDef(id);
            if (def) storeValue(*instance, id,
                clampValue(*def, values[index]));
        }
        PhraseState phrase;
        if (!s3g::clap_state::readAll(stream, &phrase,
                sizeof(phrase))) return false;
        phrase.length = std::min<uint32_t>(phrase.length,
            kPhraseCapacity - 1u);
        phrase.text[phrase.length] = '\0';
        if (!publishTextPhrase(*instance, phrase.text.data())) return false;
    } else if (header.version == 11u) {
        constexpr uint32_t oldSavedParamCount = 76u;
        std::array<double, oldSavedParamCount> values {};
        if (!s3g::clap_state::readAll(stream, values.data(),
                sizeof(values))) return false;
        for (uint32_t index = 0u; index < values.size(); ++index) {
            const clap_id id = kSavedParamIds[index];
            const auto* def = paramDef(id);
            if (def) storeValue(*instance, id,
                clampValue(*def, values[index]));
        }
        PhraseState phrase;
        if (!s3g::clap_state::readAll(stream, &phrase,
                sizeof(phrase))) return false;
        phrase.length = std::min<uint32_t>(phrase.length,
            kPhraseCapacity - 1u);
        phrase.text[phrase.length] = '\0';
        if (!publishTextPhrase(*instance, phrase.text.data())) return false;
    } else if (header.version == 12u) {
        constexpr uint32_t oldSavedParamCount = 77u;
        std::array<double, oldSavedParamCount> values {};
        if (!s3g::clap_state::readAll(stream, values.data(),
                sizeof(values))) return false;
        for (uint32_t index = 0u; index < values.size(); ++index) {
            const clap_id id = kSavedParamIds[index];
            const auto* def = paramDef(id);
            if (def) storeValue(*instance, id,
                clampValue(*def, values[index]));
        }
        PhraseState phrase;
        if (!s3g::clap_state::readAll(stream, &phrase,
                sizeof(phrase))) return false;
        phrase.length = std::min<uint32_t>(phrase.length,
            kPhraseCapacity - 1u);
        phrase.text[phrase.length] = '\0';
        if (!publishTextPhrase(*instance, phrase.text.data())) return false;
    } else if (header.version == 13u) {
        constexpr uint32_t oldSavedParamCount = 81u;
        std::array<double, oldSavedParamCount> values {};
        if (!s3g::clap_state::readAll(stream, values.data(),
                sizeof(values))) return false;
        for (uint32_t index = 0u; index < values.size(); ++index) {
            const clap_id id = kSavedParamIds[index];
            const auto* def = paramDef(id);
            if (def) storeValue(*instance, id,
                clampValue(*def, values[index]));
        }
        PhraseState phrase;
        if (!s3g::clap_state::readAll(stream, &phrase,
                sizeof(phrase))) return false;
        phrase.length = std::min<uint32_t>(phrase.length,
            kPhraseCapacity - 1u);
        phrase.text[phrase.length] = '\0';
        if (!publishTextPhrase(*instance, phrase.text.data())) return false;
    } else if (header.version == 14u || header.version == 15u
        || header.version == 16u || header.version == 17u) {
        // These releases stored IDs 1--86 except the momentary Audition
        // control. Version 18 may append the expanded formant-matrix surface,
        // so preserve the exact historical payload width here.
        constexpr uint32_t oldSavedParamCount = 85u;
        std::array<double, oldSavedParamCount> values {};
        if (!s3g::clap_state::readAll(stream, values.data(),
                sizeof(values))) return false;
        for (uint32_t index = 0u; index < values.size(); ++index) {
            const clap_id id = kSavedParamIds[index];
            const auto* def = paramDef(id);
            if (def) storeValue(*instance, id,
                clampValue(*def, values[index]));
        }
        PhraseState phrase;
        if (!s3g::clap_state::readAll(stream, &phrase,
                sizeof(phrase))) return false;
        phrase.length = std::min<uint32_t>(phrase.length,
            kPhraseCapacity - 1u);
        phrase.text[phrase.length] = '\0';
        if (!publishTextPhrase(*instance, phrase.text.data())) return false;
    } else if (header.version == 18u || header.version == 19u) {
        // Versions 18 and 19 ended at routing ID 1097. Version 20 appends
        // analysis and pitch controls, so preserve the historical payload
        // width instead of consuming PhraseState as parameter doubles.
        constexpr uint32_t oldSavedParamCount = 1096u;
        std::array<double, oldSavedParamCount> values {};
        if (!s3g::clap_state::readAll(stream, values.data(),
                sizeof(values))) return false;
        for (uint32_t index = 0u; index < values.size(); ++index) {
            const clap_id id = kSavedParamIds[index];
            const auto* def = paramDef(id);
            if (def) storeValue(*instance, id,
                clampValue(*def, values[index]));
        }
        PhraseState phrase;
        if (!s3g::clap_state::readAll(stream, &phrase,
                sizeof(phrase))) return false;
        phrase.length = std::min<uint32_t>(phrase.length,
            kPhraseCapacity - 1u);
        phrase.text[phrase.length] = '\0';
        if (!publishTextPhrase(*instance, phrase.text.data())) return false;
    } else if (header.version >= 20u && header.version <= 22u) {
        // Versions 20--22 ended at Pitch Hold (ID 1102). Version 23 appends
        // Mouth Focus, so retain the exact old width and do not consume the
        // first eight bytes of PhraseState as a parameter.
        constexpr uint32_t oldSavedParamCount = 1101u;
        std::array<double, oldSavedParamCount> values {};
        if (!s3g::clap_state::readAll(stream, values.data(),
                sizeof(values))) return false;
        for (uint32_t index = 0u; index < values.size(); ++index) {
            const clap_id id = kSavedParamIds[index];
            const auto* def = paramDef(id);
            if (def) storeValue(*instance, id,
                clampValue(*def, values[index]));
        }
        PhraseState phrase;
        if (!s3g::clap_state::readAll(stream, &phrase,
                sizeof(phrase))) return false;
        phrase.length = std::min<uint32_t>(phrase.length,
            kPhraseCapacity - 1u);
        phrase.text[phrase.length] = '\0';
        if (!publishTextPhrase(*instance, phrase.text.data())) return false;
    } else if (header.version == 23u) {
        // Version 23 ended at Mouth Focus (ID 1103). Version 24 appends the
        // precision-transfer controls without consuming PhraseState bytes.
        constexpr uint32_t oldSavedParamCount = 1102u;
        std::array<double, oldSavedParamCount> values {};
        if (!s3g::clap_state::readAll(stream, values.data(),
                sizeof(values))) return false;
        for (uint32_t index = 0u; index < values.size(); ++index) {
            const clap_id id = kSavedParamIds[index];
            const auto* def = paramDef(id);
            if (def) storeValue(*instance, id,
                clampValue(*def, values[index]));
        }
        PhraseState phrase;
        if (!s3g::clap_state::readAll(stream, &phrase,
                sizeof(phrase))) return false;
        phrase.length = std::min<uint32_t>(phrase.length,
            kPhraseCapacity - 1u);
        phrase.text[phrase.length] = '\0';
        if (!publishTextPhrase(*instance, phrase.text.data())) return false;
    } else if (header.version == 24u) {
        // Version 24 ended at Carrier Density (ID 1109). Version 25 appends
        // calibrated analyzer-width, HF-detail, and input-conditioning
        // controls without consuming the following PhraseState bytes.
        constexpr uint32_t oldSavedParamCount = 1108u;
        std::array<double, oldSavedParamCount> values {};
        if (!s3g::clap_state::readAll(stream, values.data(),
                sizeof(values))) return false;
        for (uint32_t index = 0u; index < values.size(); ++index) {
            const clap_id id = kSavedParamIds[index];
            const auto* def = paramDef(id);
            if (def) storeValue(*instance, id,
                clampValue(*def, values[index]));
        }
        PhraseState phrase;
        if (!s3g::clap_state::readAll(stream, &phrase,
                sizeof(phrase))) return false;
        phrase.length = std::min<uint32_t>(phrase.length,
            kPhraseCapacity - 1u);
        phrase.text[phrase.length] = '\0';
        if (!publishTextPhrase(*instance, phrase.text.data())) return false;
    } else if (header.version == 25u
        || header.version == kStateVersion) {
        std::array<double, kSavedParamCount> values {};
        if (!s3g::clap_state::readAll(stream, values.data(),
                sizeof(values))) return false;
        for (uint32_t index = 0u; index < values.size(); ++index) {
            const clap_id id = kSavedParamIds[index];
            const auto* def = paramDef(id);
            if (def) storeValue(*instance, id,
                clampValue(*def, values[index]));
        }
        PhraseState phrase;
        if (!s3g::clap_state::readAll(stream, &phrase,
                sizeof(phrase))) return false;
        phrase.length = std::min<uint32_t>(phrase.length,
            kPhraseCapacity - 1u);
        phrase.text[phrase.length] = '\0';
        if (!publishTextPhrase(*instance, phrase.text.data())) return false;
    } else {
        return false;
    }
    // IDs 65--86 previously represented incompatible FFT transports. Preserve
    // voice, phrase, echo, and ensemble state, but initialize the new voice
    // bank from its own safe defaults instead of reinterpreting old values.
    if (header.version <= 16u) {
        for (clap_id id = kBankAmountParamId;
             id <= kBankGestureFollowParamId; ++id) {
            if (const auto* def = paramDef(id)) {
                storeValue(*instance, id, def->defaultValue);
            }
        }
        if (loadValue(*instance, kPresetParamId) >= 6.0) {
            storeValue(*instance, kPresetParamId, kCustomPreset);
        }
    }
    if (header.version <= 17u) {
        for (clap_id id = kBandLayoutParamId; id <= kParamCount; ++id) {
            if (const auto* def = paramDef(id)) {
                storeValue(*instance, id, def->defaultValue);
            }
        }
    }
    // Version 19 reverses the dedicated audio input from carrier to
    // modulator. IDs 98 and 99 therefore cannot safely inherit their v18
    // external-carrier values. Retain the old self-contained sound and move
    // the former Custom slot out of the new Classic Mic factory slot.
    if (header.version <= 18u) {
        if (loadValue(*instance, kPresetParamId) >= 13.5) {
            storeValue(*instance, kPresetParamId, kCustomPreset);
        }
        storeValue(*instance, kModulatorSourceParamId,
            static_cast<uint32_t>(
                s3g::AcapellaResonatorModulatorSource::InternalSpeech));
        storeValue(*instance, kMicGainParamId, 0.0);
    }
    if (header.version <= 19u) {
        for (clap_id id = kAnalysisSlopeParamId;
             id <= kPitchHoldParamId; ++id) {
            if (const auto* def = paramDef(id)) {
                storeValue(*instance, id, def->defaultValue);
            }
        }
    }
    // Mouth Focus did not exist before format 23. Its implicit old behavior
    // was fully focused LPC, so use 100% for migration rather than the gentler
    // 80% new-instance default.
    if (header.version <= 22u) {
        storeValue(*instance, kMouthFocusParamId, 1.0);
    }
    // Format 23 predates the literal transfer path. Preserve its sound by
    // migrating to neutral conditioning/density and Expressive transfer; new
    // factory profiles deliberately opt into the clearer behavior.
    if (header.version <= 23u) {
        storeValue(*instance, kTransferModeParamId, 0.0);
        storeValue(*instance, kVoiceFocusParamId, 0.0);
        storeValue(*instance, kAnalysisLevelerParamId, 0.0);
        storeValue(*instance, kConsonantColorParamId, 0.0);
        storeValue(*instance, kConsonantSpeedParamId, 0.35);
        storeValue(*instance, kCarrierDensityParamId, 0.0);
    }
    // Format 24 used a fixed per-stage analysis Q. Map it onto the new Width
    // control so each saved pole topology retains that calibration, and keep
    // every genuinely new conditioning path neutral. New instances and new
    // factory profiles deliberately use the clearer non-neutral front end.
    if (header.version <= 24u) {
        const uint32_t response = static_cast<uint32_t>(std::lround(
            loadValue(*instance, kAnalysisSlopeParamId)));
        storeValue(*instance, kAnalysisWidthParamId,
            response == 0u ? 0.79 : 0.37);
        storeValue(*instance, kHfDetailModeParamId,
            static_cast<uint32_t>(
                s3g::AcapellaResonatorHfDetailMode::Synthetic));
        storeValue(*instance, kHfDetailLevelParamId, 0.0);
        storeValue(*instance, kHfDetailCutoffParamId, 4300.0);
        storeValue(*instance, kAnalysisLowEqParamId, 0.0);
        storeValue(*instance, kAnalysisMidEqParamId, 0.0);
        storeValue(*instance, kAnalysisAirEqParamId, 0.0);
        storeValue(*instance, kAnalysisCompressionParamId, 0.0);
        storeValue(*instance, kAnalysisNoiseRejectParamId, 0.0);
        storeValue(*instance, kAnalysisSpectralBalanceParamId, 0.0);
    }
    // Version 20 used profile slot 15 for Custom. Version 21 appends nine
    // matrix-first factory profiles before Custom without reinterpreting any
    // existing routing or effect parameter.
    if (header.version == 20u
        && std::fabs(loadValue(*instance, kPresetParamId) - 15.0) < 0.5) {
        storeValue(*instance, kPresetParamId, kCustomPreset);
    }
    // Version 21 used slot 24 for Custom. Version 22 inserts Mouth Circuit
    // immediately before it without reinterpreting a saved custom patch.
    if (header.version == 21u
        && std::fabs(loadValue(*instance, kPresetParamId) - 24.0) < 0.5) {
        storeValue(*instance, kPresetParamId, kCustomPreset);
    }
    // Version 25 used slot 25 for Custom. Version 26 appends six
    // Spectravox-informed bank profiles without turning a saved custom patch
    // into the first new factory profile.
    if (header.version <= 25u
        && std::fabs(loadValue(*instance, kPresetParamId) - 25.0) < 0.5) {
        storeValue(*instance, kPresetParamId, kCustomPreset);
    }
    storeValue(*instance, kAuditionParamId, 0.0);
    instance->controlAuditionGate.store(false, std::memory_order_release);
    instance->routingControlDirty.store(true, std::memory_order_release);
    markTailChanged(*instance);
    requestParamValuesRescan(*instance);
    if (instance->host && instance->host->request_process) {
        instance->host->request_process(instance->host);
    }
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

uint32_t latencyGet(const clap_plugin_t* plugin)
{
    return self(plugin)->effects.latencySamples();
}

const clap_plugin_latency_t latencyExt { latencyGet };

uint32_t tailGet(const clap_plugin_t* plugin)
{
    return self(plugin)->publishedTailSamples.load(std::memory_order_acquire);
}

const clap_plugin_tail_t tailExt { tailGet };

#if defined(__APPLE__)
} // namespace
#if 0
constexpr auto kArticulatorCanvas =
    s3g::gui_layout::Canvas {
        static_cast<double>(kGuiWidth), static_cast<double>(kGuiHeight)
    };
constexpr s3g::gui_layout::Column kArticulatorFirstColumn {
    644.0, 344.0, 42.0
};
constexpr s3g::gui_layout::Column kArticulatorSecondColumn {
    1000.0, 344.0, 42.0
};
constexpr auto kArticulatorPhrasePanel = s3g::gui_layout::makePanel(
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::EventTiming,
    { 12.0, 620.0, 42.0 }, 42.0, 914.0, 0u);
constexpr auto kArticulatorOutputPanel = s3g::gui_layout::fittedPanel(
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Output,
    kArticulatorFirstColumn, 42.0, 1u);
constexpr auto kArticulatorSourcePanel =
    s3g::gui_layout::fittedStackPanel(
        s3g::gui_layout::PanelRole::Source,
        kArticulatorOutputPanel, 10u);
constexpr auto kArticulatorArticulationPanel =
    s3g::gui_layout::fittedStackPanel(
        s3g::gui_layout::PanelRole::ToneShape,
        kArticulatorSourcePanel, 7u);
constexpr auto kArticulatorEnsemblePanel =
    s3g::gui_layout::fittedStackPanel(
        s3g::gui_layout::PanelRole::Relationships,
        kArticulatorArticulationPanel, 6u);
constexpr auto kArticulatorPerformancePanel =
    s3g::gui_layout::fittedPanel(
        s3g::gui_layout::PluginClass::EffectProcessor,
        s3g::gui_layout::PanelRole::Envelope,
        kArticulatorSecondColumn, 42.0, 13u);
constexpr auto kArticulatorShapePanel =
    s3g::gui_layout::fittedStackPanel(
        s3g::gui_layout::PanelRole::ToneShape,
        kArticulatorPerformancePanel, 9u);
constexpr auto kArticulatorEchoPanel =
    s3g::gui_layout::fittedStackPanel(
        s3g::gui_layout::PanelRole::EventTiming,
        kArticulatorShapePanel, 9u);

constexpr clap_id kOutputGuiParams[] {
    kOutputParamId,
};
constexpr clap_id kSourceGuiParams[] {
    kDeliveryParamId, kVowelParamId, kOnsetParamId, kDurationParamId,
    kTractParamId, kBreathParamId, kOpenQuotientParamId,
    kHarshnessParamId, kFalseFoldParamId, kThroatParamId,
};
constexpr clap_id kArticulationGuiParams[] {
    kArticulationParamId, kConsonantParamId, kWaveguideBlendParamId,
    kCoarticulationParamId, kHybridBlendParamId, kOnsetGuardParamId,
    kIntensityParamId,
};
constexpr clap_id kEnsembleGuiParams[] {
    kPolyphonyParamId, kDoubleAmountParamId, kDoubleDetuneParamId,
    kDoubleTimingParamId, kDoubleDirtParamId, kDoubleWidthParamId,
};
constexpr clap_id kPerformanceGuiParams[] {
    kRoughnessParamId, kBrightnessParamId, kChestParamId, kNasalParamId,
    kVibratoRateParamId, kVibratoDepthParamId, kPitchDriftParamId,
    kGlideParamId, kScoopParamId, kDeclinationParamId,
    kAttackParamId, kReleaseParamId, kRetriggerParamId,
};
constexpr clap_id kShapeGuiParams[] {
    kOctaveDownParamId, kOctaveUpParamId, kFuzzDriveParamId,
    kFuzzMixParamId, kFuzzToneParamId, kCompressionParamId,
    kParallelCrushParamId, kDeEssParamId, kWidthParamId,
};
constexpr clap_id kEchoGuiParams[] {
    kEchoHeadsParamId, kEchoClockParamId, kEchoTimeParamId,
    kEchoFeedbackParamId, kEchoWearParamId, kEchoFlutterParamId,
    kEchoToneParamId, kEchoSpreadParamId, kEchoMixParamId,
};
constexpr clap_id kVoiceBankGuiParams[] {
    kBankAmountParamId, kBankModeParamId,
    kCarrierShapeParamId, kCarrierHarmonicsParamId,
    kCarrierColorParamId, kCarrierNoiseParamId,
    kAnalysisBlendParamId, kBankAttackParamId, kBankReleaseParamId,
    kBankResonanceParamId, kBankDriveParamId,
    kBandShiftParamId, kBandStretchParamId,
    kBandTiltParamId, kSibilanceParamId,
    kMatrixModeParamId, kMatrixMorphParamId,
    kBankStereoSpreadParamId, kBankFreezeParamId,
    kFreezeTriggerParamId, kBankBlurParamId,
    kBankGestureFollowParamId,
};

struct ArticulatorGuiGroup {
    const s3g::gui_layout::Panel* panel;
    const clap_id* params;
    uint32_t count;
    const char* title;
};

constexpr ArticulatorGuiGroup kArticulatorGuiGroups[] {
    { &kArticulatorOutputPanel, kOutputGuiParams,
        static_cast<uint32_t>(std::size(kOutputGuiParams)), "OUTPUT" },
    { &kArticulatorSourcePanel, kSourceGuiParams,
        static_cast<uint32_t>(std::size(kSourceGuiParams)), "SOURCE" },
    { &kArticulatorArticulationPanel, kArticulationGuiParams,
        static_cast<uint32_t>(std::size(kArticulationGuiParams)),
        "ARTICULATION" },
    { &kArticulatorEnsemblePanel, kEnsembleGuiParams,
        static_cast<uint32_t>(std::size(kEnsembleGuiParams)), "ENSEMBLE" },
    { &kArticulatorPerformancePanel, kPerformanceGuiParams,
        static_cast<uint32_t>(std::size(kPerformanceGuiParams)),
        "PERFORMANCE / ENVELOPE" },
    { &kArticulatorShapePanel, kShapeGuiParams,
        static_cast<uint32_t>(std::size(kShapeGuiParams)), "SHAPE / DYNAMICS" },
    { &kArticulatorEchoPanel, kEchoGuiParams,
        static_cast<uint32_t>(std::size(kEchoGuiParams)), "MULTI-HEAD TAPE" },
};

static_assert(s3g::gui_layout::rectFitsCanvas(
    kArticulatorPhrasePanel.frame, kArticulatorCanvas));
static_assert(s3g::gui_layout::rectFitsCanvas(
    kArticulatorEnsemblePanel.frame, kArticulatorCanvas));
static_assert(s3g::gui_layout::rectFitsCanvas(
    kArticulatorEchoPanel.frame, kArticulatorCanvas));

bool articulatorGuiLocation(clap_id id,
    const s3g::gui_layout::Panel*& panel, uint32_t& row)
{
    for (const auto& group : kArticulatorGuiGroups) {
        for (uint32_t index = 0u; index < group.count; ++index) {
            if (group.params[index] == id) {
                panel = group.panel;
                row = index;
                return true;
            }
        }
    }
    return false;
}

const char* articulatorGuiLabel(clap_id id)
{
    switch (id) {
    case kOutputParamId: return "OUT";
    case kDeliveryParamId: return "PHRAS";
    case kVowelParamId: return "VOWEL";
    case kOnsetParamId: return "ONSET";
    case kDurationParamId: return "HORIZ";
    case kTractParamId: return "TRACT";
    case kBreathParamId: return "BREATH";
    case kOpenQuotientParamId: return "OPEN";
    case kHarshnessParamId: return "FOLD";
    case kFalseFoldParamId: return "F-FLD";
    case kThroatParamId: return "THROAT";
    case kArticulationParamId: return "ARTIC";
    case kConsonantParamId: return "CONS";
    case kWaveguideBlendParamId: return "WAVE";
    case kCoarticulationParamId: return "COART";
    case kHybridBlendParamId: return "HYBR";
    case kOnsetGuardParamId: return "GUARD";
    case kIntensityParamId: return "INT";
    case kPolyphonyParamId: return "VOICES";
    case kDoubleAmountParamId: return "DBL";
    case kDoubleDetuneParamId: return "DETUNE";
    case kDoubleTimingParamId: return "TIMING";
    case kDoubleDirtParamId: return "DIRT";
    case kDoubleWidthParamId: return "WIDTH";
    case kRoughnessParamId: return "ROUGH";
    case kBrightnessParamId: return "BRIGHT";
    case kChestParamId: return "CHEST";
    case kNasalParamId: return "NASAL";
    case kVibratoRateParamId: return "VIB RT";
    case kVibratoDepthParamId: return "VIB DP";
    case kPitchDriftParamId: return "DRIFT";
    case kGlideParamId: return "GLIDE";
    case kScoopParamId: return "SCOOP";
    case kDeclinationParamId: return "DECL";
    case kAttackParamId: return "ATTACK";
    case kReleaseParamId: return "RELEASE";
    case kRetriggerParamId: return "XFADE";
    case kOctaveDownParamId: return "SUB";
    case kOctaveUpParamId: return "OCT UP";
    case kFuzzDriveParamId: return "DRIVE";
    case kFuzzMixParamId: return "FUZZ";
    case kFuzzToneParamId: return "TONE";
    case kCompressionParamId: return "COMP";
    case kParallelCrushParamId: return "CRUSH";
    case kDeEssParamId: return "DE-ESS";
    case kWidthParamId: return "WIDTH";
    case kEchoHeadsParamId: return "HEADS";
    case kEchoClockParamId: return "CLOCK";
    case kEchoTimeParamId: return "TIME";
    case kEchoFeedbackParamId: return "FDBK";
    case kEchoWearParamId: return "WEAR";
    case kEchoFlutterParamId: return "FLUT";
    case kEchoToneParamId: return "TONE";
    case kEchoSpreadParamId: return "SPRD";
    case kEchoMixParamId: return "MIX";
    case kBankAmountParamId: return "AMOUNT";
    case kBankModeParamId: return "MODE";
    case kCarrierShapeParamId: return "CARRIER";
    case kCarrierHarmonicsParamId: return "HARM";
    case kCarrierColorParamId: return "COLOR";
    case kCarrierNoiseParamId: return "NOISE";
    case kAnalysisBlendParamId: return "AN / PH";
    case kBankAttackParamId: return "ATTACK";
    case kBankReleaseParamId: return "RELEASE";
    case kBankResonanceParamId: return "RESON";
    case kBankDriveParamId: return "DRIVE";
    case kBandShiftParamId: return "SHIFT";
    case kBandStretchParamId: return "STRETCH";
    case kBandTiltParamId: return "TILT";
    case kSibilanceParamId: return "SIBIL";
    case kMatrixModeParamId: return "MATRIX";
    case kMatrixMorphParamId: return "MORPH";
    case kBankStereoSpreadParamId: return "STEREO";
    case kBankFreezeParamId: return "FREEZE";
    case kFreezeTriggerParamId: return "TRIGGER";
    case kBankBlurParamId: return "BLUR";
    case kBankGestureFollowParamId: return "G-FOLLOW";
    default: return "";
    }
}

const char* articulatorPhonemeLabel(s3g::AcapellaPhoneme phoneme)
{
    constexpr const char* labels[s3g::kAcapellaPhonemeCount] {
        "-", "IY", "IH", "EH", "AE", "AA", "AO", "UH", "UW",
        "AH", "AX", "ER", "P", "B", "T", "D", "K", "G", "F",
        "V", "TH", "DH", "S", "Z", "SH", "ZH", "HH", "CH",
        "JH", "M", "N", "NG", "L", "R", "W", "Y",
    };
    return labels[std::min<uint32_t>(
        static_cast<uint32_t>(phoneme), s3g::kAcapellaPhonemeCount - 1u)];
}

constexpr CGFloat kScoreFirstRowY = 736.0;
constexpr CGFloat kScoreRowPitch = 26.0;

NSRect articulatorCompileRect()
{
    return NSMakeRect(532.0, 77.0, 84.0, 20.0);
}

NSRect articulatorPageRect()
{
    return NSMakeRect(526.0, 45.0, 90.0, 20.0);
}

bool articulatorVoiceBankLocation(clap_id id, uint32_t& column,
    uint32_t& row)
{
    for (uint32_t index = 0u; index < std::size(kVoiceBankGuiParams);
         ++index) {
        if (kVoiceBankGuiParams[index] != id) continue;
        column = index < 11u ? 0u : 1u;
        row = index < 11u ? index : index - 11u;
        return true;
    }
    return false;
}

CGFloat articulatorVoiceBankRowY(uint32_t row)
{
    return 92.0 + static_cast<CGFloat>(row) * 30.0;
}

CGFloat articulatorVoiceBankTrackX(uint32_t column)
{
    return column == 0u ? 112.0 : 408.0;
}

NSRect articulatorVoiceBankHitRect(clap_id id)
{
    uint32_t column = 0u;
    uint32_t row = 0u;
    if (!articulatorVoiceBankLocation(id, column, row)) return NSZeroRect;
    return NSMakeRect(column == 0u ? 20.0 : 316.0,
        articulatorVoiceBankRowY(row) - 8.0, 292.0, 24.0);
}

NSRect articulatorScoreMenuRect(clap_id id)
{
    uint32_t row = 0u;
    switch (id) {
    case kGestureSequenceParamId: row = 0u; break;
    case kGestureLoopParamId: row = 1u; break;
    case kGestureSyncParamId: row = 2u; break;
    case kGestureDivisionParamId: row = 3u; break;
    default: return NSZeroRect;
    }
    return NSMakeRect(112.0,
        kScoreFirstRowY + static_cast<CGFloat>(row) * kScoreRowPitch - 1.0,
        180.0, 15.0);
}

NSRect articulatorScoreSliderHitRect(clap_id id)
{
    uint32_t row = 0u;
    switch (id) {
    case kGestureRateParamId: row = 0u; break;
    case kGestureDepthParamId: row = 1u; break;
    case kIntelligibilityParamId: row = 2u; break;
    default: return NSZeroRect;
    }
    return NSMakeRect(316.0,
        kScoreFirstRowY + static_cast<CGFloat>(row) * kScoreRowPitch - 8.0,
        300.0, 24.0);
}

NSRect articulatorAuditionRect()
{
    return NSMakeRect(316.0,
        kScoreFirstRowY + 3.0 * kScoreRowPitch - 8.0, 300.0, 24.0);
}

NSRect articulatorProcessorMenuRect(
    const s3g::gui_layout::Panel& panel, uint32_t row)
{
    return NSMakeRect(
        s3g::gui_layout::processorControlX(panel.frame.x),
        s3g::gui_layout::rowY(panel, row) - 1.0,
        s3g::gui_layout::processorMenuWidth(panel.frame.width), 15.0);
}

@interface S3GProcessorArticulatorView : NSView <NSTextFieldDelegate> {
@private
    Plugin* _plugin;
    NSTextField* _phraseField;
    NSTimer* _timer;
    int _dragParam;
    int _openMenu;
    int _hoverMenuItem;
    NSPoint _menuOrigin;
    CGFloat _menuWidth;
    char _titlePresetName[64];
    BOOL _voiceBankPage;
}
- (instancetype)initWithPlugin:(Plugin*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
@end

@implementation S3GProcessorArticulatorView

- (instancetype)initWithPlugin:(Plugin*)plugin
{
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _timer = nil;
        _dragParam = -1;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuOrigin = NSZeroPoint;
        _menuWidth = 180.0;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "INIT");
        _voiceBankPage = NO;
        _phraseField = [[NSTextField alloc] initWithFrame:
            NSMakeRect(28.0, 76.0, 492.0, 24.0)];
        s3g::clap_gui::styleNumberTextField(
            _phraseField, 12.0, NSTextAlignmentLeft);
        [_phraseField setPlaceholderString:@"type a phrase   | = rest"];
        [_phraseField setDelegate:self];
        [_phraseField setTarget:self];
        [_phraseField setAction:@selector(commitPhrase:)];
        const PhraseState phrase = plugin ? loadPhrase(*plugin) : PhraseState {};
        [_phraseField setStringValue:[NSString stringWithUTF8String:
            phrase.text.data()]];
        [self addSubview:_phraseField];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)dealloc
{
    [self stopRefreshTimer];
    [_phraseField release];
    [super dealloc];
}

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    NSArray* existing = [[self trackingAreas] copy];
    for (NSTrackingArea* area in existing) [self removeTrackingArea:area];
    [existing release];
    NSTrackingArea* area = [[NSTrackingArea alloc]
        initWithRect:NSZeroRect
        options:NSTrackingMouseMoved | NSTrackingActiveInKeyWindow
            | NSTrackingInVisibleRect
        owner:self userInfo:nil];
    [self addTrackingArea:area];
    [area release];
}

- (BOOL)phraseIsEditing
{
    NSResponder* first = [[self window] firstResponder];
    return first == _phraseField || first == [_phraseField currentEditor];
}

- (void)drawPanel:(NSString*)title
    panel:(const s3g::gui_layout::Panel&)panel
    style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawPanelFrame(panel, style);
    s3g::clap_gui::drawPanelHeader(
        title, true, panel,
        s3g::clap_gui::softLabelAttrs(), style);
}

- (void)drawParam:(clap_id)id row:(uint32_t)row
    panel:(const s3g::gui_layout::Panel&)panel
    style:(const s3g::clap_gui::Style&)style
{
    const ParamDef* def = paramDef(id);
    if (!def) return;
    const double value = loadValue(*_plugin, id);
    char text[64] {};
    paramsValueToText(&_plugin->plugin, id, value, text, sizeof(text));
    NSString* label = [NSString stringWithUTF8String:articulatorGuiLabel(id)];
    NSString* display = [[NSString stringWithUTF8String:text] uppercaseString];
    const CGFloat y = s3g::gui_layout::rowY(panel, row);
    if (def->stepped) {
        s3g::clap_gui::drawProcessorMenu(label, display, y,
            panel.frame.x, panel.frame.width,
            s3g::clap_gui::softLabelAttrs(),
            s3g::clap_gui::softValueAttrs(), style);
    } else {
        const CGFloat norm = static_cast<CGFloat>(std::clamp(
            (value - def->minimum) / std::max(1.0e-9,
                def->maximum - def->minimum), 0.0, 1.0));
        s3g::clap_gui::drawProcessorSlider(label, display, norm, y,
            panel.frame.x, panel.frame.width,
            s3g::clap_gui::softLabelAttrs(),
            s3g::clap_gui::softValueAttrs(), style);
    }
}

- (void)drawScoreControls:(const s3g::clap_gui::Style&)style
{
    NSDictionary* labels = s3g::clap_gui::softLabelAttrs();
    NSDictionary* values = s3g::clap_gui::softValueAttrs();
    constexpr clap_id menus[] {
        kGestureSequenceParamId, kGestureLoopParamId,
        kGestureSyncParamId, kGestureDivisionParamId,
    };
    constexpr const char* menuLabels[] { "SCORE", "MODE", "SYNC", "DIV" };
    for (uint32_t row = 0u; row < std::size(menus); ++row) {
        char text[64] {};
        paramsValueToText(&_plugin->plugin, menus[row],
            loadValue(*_plugin, menus[row]), text, sizeof(text));
        s3g::clap_gui::drawMenu(
            [NSString stringWithUTF8String:menuLabels[row]],
            [[NSString stringWithUTF8String:text] uppercaseString],
            kScoreFirstRowY + static_cast<CGFloat>(row) * kScoreRowPitch,
            labels, values, style, 28.0, 112.0, 180.0);
    }

    constexpr clap_id sliders[] {
        kGestureRateParamId, kGestureDepthParamId, kIntelligibilityParamId,
    };
    constexpr const char* sliderLabels[] { "RATE", "DEPTH", "INTEL" };
    for (uint32_t row = 0u; row < std::size(sliders); ++row) {
        const ParamDef* def = paramDef(sliders[row]);
        const double value = loadValue(*_plugin, sliders[row]);
        char text[64] {};
        paramsValueToText(&_plugin->plugin, sliders[row], value,
            text, sizeof(text));
        const CGFloat norm = static_cast<CGFloat>(std::clamp(
            (value - def->minimum) / (def->maximum - def->minimum),
            0.0, 1.0));
        s3g::clap_gui::drawSlider(
            [NSString stringWithUTF8String:sliderLabels[row]],
            [[NSString stringWithUTF8String:text] uppercaseString],
            norm,
            kScoreFirstRowY + static_cast<CGFloat>(row) * kScoreRowPitch,
            labels, values, style, 324.0, 414.0, 560.0, 140.0, 48.0);
    }
    s3g::clap_gui::drawToggle(@"AUD",
        loadValue(*_plugin, kAuditionParamId) >= 0.5,
        kScoreFirstRowY + 3.0 * kScoreRowPitch,
        labels, values, style, 324.0, 414.0, 194.0);
}

- (void)drawScoreGrid:(const s3g::clap_gui::Style&)style
{
    const PhraseState phrase = loadPhrase(*_plugin);
    const auto compiled = s3g::compileAcapellaText(phrase.text.data());
    NSDictionary* labels = s3g::clap_gui::softLabelAttrs();
    NSDictionary* values = s3g::clap_gui::softValueAttrs();
    NSString* contextStatus = compiled.contextualWordCount > 0u
        ? [NSString stringWithFormat:@"  //  %u CTX",
            compiled.contextualWordCount]
        : @"";
    [[NSString stringWithFormat:@"%u WORDS  //  %u EVENTS%@%@",
        compiled.program.wordCount, compiled.program.count,
        contextStatus,
        compiled.program.truncated ? @"  //  TRUNCATED" : @""]
        drawAtPoint:NSMakePoint(28.0, 112.0) withAttributes:values];
    [@"| REST  //  || 2X"
        drawAtPoint:NSMakePoint(486.0, 112.0) withAttributes:values];

    constexpr CGFloat cellWidth = 45.0;
    constexpr CGFloat cellHeight = 61.0;
    constexpr CGFloat pitchX = 49.0;
    constexpr CGFloat pitchY = 69.0;
    for (uint32_t index = 0u; index < s3g::kAcapellaTextGestureCapacity;
         ++index) {
        const uint32_t column = index % 12u;
        const uint32_t row = index / 12u;
        const NSRect cell = NSMakeRect(
            28.0 + static_cast<CGFloat>(column) * pitchX,
            142.0 + static_cast<CGFloat>(row) * pitchY,
            cellWidth, cellHeight);
        const bool populated = index < compiled.program.count;
        const auto phoneme = populated
            ? compiled.program.steps[index].phoneme
            : s3g::AcapellaPhoneme::Silence;
        const bool forcedRest = populated
            && (compiled.program.steps[index].flags
                & s3g::kAcapellaForcedRest) != 0u;
        const bool vowel = populated && s3g::acapellaPhonemeIsVowel(phoneme);
        [s3g::clap_gui::color(populated
            ? (forcedRest ? 0x302d26
                : (vowel ? 0x292d30 : 0x222528))
            : 0x181a1c) setFill];
        NSRectFill(cell);
        [s3g::clap_gui::color(forcedRest ? 0x747064
            : (populated ? 0x454a4e : 0x272a2d)) setStroke];
        NSFrameRect(cell);
        if (!populated) continue;
        const auto& step = compiled.program.steps[index];
        if ((step.flags & s3g::kAcapellaWordStart) != 0u) {
            [style.accent setFill];
            NSRectFill(NSMakeRect(cell.origin.x, cell.origin.y, 2.0,
                cell.size.height));
        }
        if (step.stress > 0u) {
            [s3g::clap_gui::color(
                step.stress > 1u ? 0xb4b4b4 : 0x6f7478) setFill];
            NSRectFill(NSMakeRect(cell.origin.x + 5.0,
                cell.origin.y + 6.0,
                (cell.size.width - 10.0)
                    * (step.stress > 1u ? 1.0 : 0.55), 2.0));
        }
        NSString* symbol = forcedRest ? @"REST"
            : [NSString stringWithUTF8String:
                articulatorPhonemeLabel(phoneme)];
        const NSSize size = [symbol sizeWithAttributes:labels];
        [symbol drawAtPoint:NSMakePoint(
            cell.origin.x + (cell.size.width - size.width) * 0.5,
            cell.origin.y + 24.0) withAttributes:labels];
        NSString* duration = forcedRest
            ? [NSString stringWithFormat:@"%.2gx",
                static_cast<double>(step.durationScale)]
            : [NSString stringWithFormat:@"%.2g",
                static_cast<double>(step.durationScale)];
        const NSSize durationSize = [duration sizeWithAttributes:values];
        [duration drawAtPoint:NSMakePoint(
            cell.origin.x + (cell.size.width - durationSize.width) * 0.5,
            cell.origin.y + 42.0) withAttributes:values];
    }
}

- (void)drawVoiceBankControls:(const s3g::clap_gui::Style&)style
{
    NSDictionary* labels = s3g::clap_gui::softLabelAttrs();
    NSDictionary* values = s3g::clap_gui::softValueAttrs();
    [@"CARRIER / ANALYSIS" drawAtPoint:NSMakePoint(28.0, 72.0)
        withAttributes:values];
    [@"RESONATORS / MATRIX / GESTURE" drawAtPoint:NSMakePoint(324.0, 72.0)
        withAttributes:values];
    for (clap_id id : kVoiceBankGuiParams) {
        uint32_t column = 0u;
        uint32_t row = 0u;
        if (!articulatorVoiceBankLocation(id, column, row)) continue;
        const ParamDef* def = paramDef(id);
        const double value = loadValue(*_plugin, id);
        const CGFloat norm = static_cast<CGFloat>(std::clamp(
            (value - def->minimum) / (def->maximum - def->minimum),
            0.0, 1.0));
        char text[64] {};
        paramsValueToText(&_plugin->plugin, id, value, text, sizeof(text));
        const CGFloat labelX = column == 0u ? 28.0 : 324.0;
        const CGFloat trackX = articulatorVoiceBankTrackX(column);
        const CGFloat valueX = column == 0u ? 230.0 : 526.0;
        NSString* label = [NSString stringWithUTF8String:
            articulatorGuiLabel(id)];
        NSString* display = [[NSString stringWithUTF8String:text]
            uppercaseString];
        if (def->stepped) {
            s3g::clap_gui::drawMenu(label, display,
                articulatorVoiceBankRowY(row), labels, values, style,
                labelX, trackX, 176.0);
        } else {
            s3g::clap_gui::drawSlider(label, display, norm,
                articulatorVoiceBankRowY(row), labels, values, style,
                labelX, trackX, valueX, 108.0, 58.0);
        }
    }

    [style.strip setFill];
    NSRectFill(NSMakeRect(28.0, 420.0, 560.0, 2.0));
    [@"ARTICULATOR  >  16-BAND ANALYSIS  >  ENVELOPE MATRIX  >  RESONATORS"
        drawAtPoint:NSMakePoint(28.0, 434.0) withAttributes:values];
    [@"PHONEME TARGETS + MEASURED ENERGY  //  SHAPED CONSONANT NOISE"
        drawAtPoint:NSMakePoint(28.0, 454.0) withAttributes:labels];
    [@"16 RESONANT CHANNELS  //  PROCEDURAL CARRIER  //  NO SAMPLES"
        drawAtPoint:NSMakePoint(28.0, 474.0) withAttributes:labels];

    const double amount = loadValue(*_plugin, kBankAmountParamId);
    const double mode = loadValue(*_plugin, kBankModeParamId) / 2.0;
    const double harmonics = loadValue(*_plugin, kCarrierHarmonicsParamId);
    const double analysis = loadValue(*_plugin, kAnalysisBlendParamId);
    const double resonance = loadValue(*_plugin, kBankResonanceParamId);
    const double tilt = loadValue(*_plugin, kBandTiltParamId);
    const double morph = loadValue(*_plugin, kMatrixMorphParamId);
    const double freeze = loadValue(*_plugin, kBankFreezeParamId);
    const bool fieldActive = amount > 0.01;
    constexpr uint32_t kDisplayBands = 16u;
    for (uint32_t band = 0u; band < kDisplayBands; ++band) {
        const CGFloat x = 30.0 + static_cast<CGFloat>(band) * 34.5;
        const double normalizedBand = static_cast<double>(band)
            / static_cast<double>(kDisplayBands - 1u);
        const double vowelShape = 0.28 + 0.72 * std::abs(std::sin(
            static_cast<double>(band) * 0.79 + analysis * 2.1
                + mode * 1.3));
        const double tilted = std::clamp(vowelShape
            * (1.0 + tilt * (normalizedBand - 0.5)), 0.08, 1.20);
        const CGFloat height = static_cast<CGFloat>((12.0 + 55.0 * tilted)
            * (0.50 + 0.24 * harmonics + 0.26 * amount));
        const CGFloat hold = static_cast<CGFloat>(freeze * 7.0
            + resonance * 5.0 + morph * 4.0);
        [s3g::clap_gui::color(fieldActive
            ? (band % 4u == 0u ? 0x8c765f : 0x5d5147)
            : (band % 4u == 0u ? 0x71777a : 0x3f4548))
            setFill];
        NSRectFill(NSMakeRect(x, 530.0 - height - hold,
            8.0, height + hold));
    }
    [fieldActive ? @"16-BAND VOICE BANK ACTIVE"
        : @"VOICE BANK BYPASSED // DIRECT ARTICULATOR"
        drawAtPoint:NSMakePoint(28.0, 548.0)
        withAttributes:values];
}

- (void)drawOpenMenu:(const s3g::clap_gui::Style&)style
{
    const ParamDef* def = paramDef(static_cast<clap_id>(_openMenu));
    if (!def || !def->stepped) return;
    const uint32_t count = static_cast<uint32_t>(
        std::lround(def->maximum - def->minimum)) + 1u;
    std::array<NSString*, 32u> items {};
    for (uint32_t index = 0u; index < count && index < items.size(); ++index) {
        char text[64] {};
        paramsValueToText(&_plugin->plugin, def->id,
            def->minimum + static_cast<double>(index),
            text, sizeof(text));
        items[index] = [[NSString stringWithUTF8String:text] uppercaseString];
    }
    const NSRect rect = NSMakeRect(_menuOrigin.x, _menuOrigin.y,
        _menuWidth, 18.0 * static_cast<CGFloat>(count));
    const int selected = static_cast<int>(std::lround(
        loadValue(*_plugin, def->id) - def->minimum));
    s3g::clap_gui::drawDropdownMenu(rect, 18.0, items.data(), count,
        selected, _hoverMenuItem, s3g::clap_gui::softValueAttrs(), style);
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    if (!_plugin) return;
    s3g::clap_gui::Style style;
    [style.bg setFill];
    NSRectFill([self bounds]);

    char profile[64] {};
    paramsValueToText(&_plugin->plugin, kPresetParamId,
        loadValue(*_plugin, kPresetParamId), profile, sizeof(profile));
    const auto titleBand = s3g::gui_layout::encoderTitleBand(
        kArticulatorCanvas);
    s3g::clap_gui::drawProcessorTitleBand(
        @"s3g PROCESSOR ARTICULATOR",
        [[NSString stringWithUTF8String:profile] uppercaseString],
        s3g::clap_gui::peakDbText(
            _plugin->outputPeak.load(std::memory_order_relaxed)),
        titleBand, s3g::clap_gui::softTitleAttrs(),
        s3g::clap_gui::softLabelAttrs(),
        s3g::clap_gui::softValueAttrs(), style);

    [self drawPanel:_voiceBankPage ? @"16-BAND VOICE BANK" : @"PHONEME SCORE"
        panel:kArticulatorPhrasePanel style:style];
    if (_voiceBankPage) {
        [self drawVoiceBankControls:style];
    } else {
        [self drawScoreGrid:style];
        [self drawScoreControls:style];
        s3g::clap_gui::drawHeaderActionButton(
            articulatorCompileRect(), articulatorCompileRect(),
            @"COMPILE", s3g::clap_gui::softLabelAttrs(), style);
    }
    s3g::clap_gui::drawHeaderActionButton(
        articulatorPageRect(), articulatorPageRect(),
        _voiceBankPage ? @"SCORE" : @"BANK",
        s3g::clap_gui::softLabelAttrs(), style);

    for (const auto& group : kArticulatorGuiGroups) {
        [self drawPanel:[NSString stringWithUTF8String:group.title]
            panel:*group.panel style:style];
        for (uint32_t row = 0u; row < group.count; ++row) {
            [self drawParam:group.params[row] row:row
                panel:*group.panel style:style];
        }
    }
    if (_openMenu > 0) [self drawOpenMenu:style];
}

- (void)openMenuForParam:(clap_id)id box:(NSRect)box
{
    const ParamDef* def = paramDef(id);
    if (!def || !def->stepped) return;
    if (id == kPresetParamId) {
        if ([self phraseIsEditing]) {
            [self commitPhrase:_phraseField];
            [[self window] makeFirstResponder:self];
        }
        // NSTextField is a child view and would otherwise composite after
        // this view's custom dropdown. Hide it only while the title preset
        // overlay crosses the phrase-entry row.
        [_phraseField setHidden:YES];
    }
    const uint32_t count = static_cast<uint32_t>(
        std::lround(def->maximum - def->minimum)) + 1u;
    const CGFloat height = 18.0 * static_cast<CGFloat>(count);
    const CGFloat below = NSMaxY(box) + 3.0;
    const CGFloat y = below + height <= static_cast<CGFloat>(kGuiHeight) - 8.0
        ? below : std::max<CGFloat>(34.0, box.origin.y - height - 3.0);
    _openMenu = static_cast<int>(id);
    _hoverMenuItem = -1;
    _menuOrigin = NSMakePoint(box.origin.x, y);
    _menuWidth = box.size.width;
    [self setNeedsDisplay:YES];
}

- (uint32_t)openMenuItemCount
{
    const ParamDef* def = paramDef(static_cast<clap_id>(_openMenu));
    return def && def->stepped
        ? static_cast<uint32_t>(
            std::lround(def->maximum - def->minimum)) + 1u
        : 0u;
}

- (NSRect)openMenuRect
{
    return NSMakeRect(_menuOrigin.x, _menuOrigin.y, _menuWidth,
        18.0 * static_cast<CGFloat>([self openMenuItemCount]));
}

- (void)updateMenuHover:(NSPoint)point
{
    const uint32_t count = [self openMenuItemCount];
    if (count == 0u) return;
    const int hover = s3g::clap_gui::dropdownHitIndex(
        point, [self openMenuRect], 18.0, count);
    if (hover != _hoverMenuItem) {
        _hoverMenuItem = hover;
        [self setNeedsDisplay:YES];
    }
}

- (void)updateSlider:(NSPoint)point
{
    const clap_id id = static_cast<clap_id>(_dragParam);
    const ParamDef* def = paramDef(id);
    if (!def || def->stepped) return;
    CGFloat trackX = 0.0;
    CGFloat trackWidth = 0.0;
    if (id == kGestureRateParamId || id == kGestureDepthParamId
        || id == kIntelligibilityParamId) {
        trackX = 414.0;
        trackWidth = 140.0;
    } else if (id >= kBankAmountParamId
        && id <= kBankGestureFollowParamId) {
        uint32_t column = 0u;
        uint32_t row = 0u;
        if (!articulatorVoiceBankLocation(id, column, row)) return;
        (void)row;
        trackX = articulatorVoiceBankTrackX(column);
        trackWidth = 108.0;
    } else {
        const s3g::gui_layout::Panel* panel = nullptr;
        uint32_t row = 0u;
        if (!articulatorGuiLocation(id, panel, row)) return;
        (void)row;
        trackX = static_cast<CGFloat>(
            s3g::gui_layout::processorControlX(panel->frame.x));
        trackWidth = static_cast<CGFloat>(
            s3g::gui_layout::processorTrackWidth(panel->frame.width));
    }
    const double norm = std::clamp(
        static_cast<double>((point.x - trackX) / trackWidth), 0.0, 1.0);
    queueGuiParamValue(*_plugin, id,
        def->minimum + norm * (def->maximum - def->minimum));
    [self setNeedsDisplay:YES];
}

- (void)beginSlider:(clap_id)id event:(NSEvent*)event point:(NSPoint)point
{
    double resetValue = 0.0;
    if (s3g::clap_gui::sliderDoubleClickDefault(
            event, &_plugin->plugin, id, &resetValue)) {
        _dragParam = -1;
    } else {
        _dragParam = static_cast<int>(id);
        [self updateSlider:point];
    }
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:
        [event locationInWindow] fromView:nil];
    const auto titleBand = s3g::gui_layout::encoderTitleBand(
        kArticulatorCanvas);

    if (_openMenu > 0) {
        const ParamDef* def = paramDef(static_cast<clap_id>(_openMenu));
        const int hit = s3g::clap_gui::dropdownHitIndex(
            point, [self openMenuRect], 18.0, [self openMenuItemCount]);
        if (hit >= 0 && def) {
            queueGuiParamValue(*_plugin, def->id,
                def->minimum + static_cast<double>(hit));
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
        [_phraseField setHidden:_voiceBankPage];
        [self setNeedsDisplay:YES];
        return;
    }

    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.presetMenu))) {
        [self openMenuForParam:kPresetParamId
            box:s3g::clap_gui::cocoaRect(titleBand.presetMenu)];
        return;
    }
    if (s3g::clap_gui::handleProcessorTitleClick(
            point, &_plugin->plugin, @"Formant Matrix",
            titleBand, _titlePresetName, sizeof(_titlePresetName),
            kOutputParamId)) {
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, articulatorPageRect())) {
        if (!_voiceBankPage && [self phraseIsEditing]) {
            [self commitPhrase:_phraseField];
            [[self window] makeFirstResponder:self];
        }
        _voiceBankPage = !_voiceBankPage;
        [_phraseField setHidden:_voiceBankPage];
        _openMenu = 0;
        [self setNeedsDisplay:YES];
        return;
    }
    if (!_voiceBankPage && NSPointInRect(point, articulatorCompileRect())) {
        [self commitPhrase:self];
        return;
    }

    if (_voiceBankPage) {
        for (clap_id id : kVoiceBankGuiParams) {
            if (NSPointInRect(point, articulatorVoiceBankHitRect(id))) {
                const ParamDef* def = paramDef(id);
                if (def && def->stepped) {
                    [self openMenuForParam:id
                        box:articulatorVoiceBankHitRect(id)];
                } else {
                    [self beginSlider:id event:event point:point];
                }
                return;
            }
        }
    }

    if (!_voiceBankPage) {
        constexpr clap_id scoreMenus[] {
            kGestureSequenceParamId, kGestureLoopParamId,
            kGestureSyncParamId, kGestureDivisionParamId,
        };
        for (clap_id id : scoreMenus) {
            const NSRect box = articulatorScoreMenuRect(id);
            if (NSPointInRect(point, box)) {
                [self openMenuForParam:id box:box];
                return;
            }
        }
        constexpr clap_id scoreSliders[] {
            kGestureRateParamId, kGestureDepthParamId,
            kIntelligibilityParamId,
        };
        for (clap_id id : scoreSliders) {
            if (NSPointInRect(point, articulatorScoreSliderHitRect(id))) {
                [self beginSlider:id event:event point:point];
                return;
            }
        }
        if (NSPointInRect(point, articulatorAuditionRect())) {
            queueGuiParamValue(*_plugin, kAuditionParamId,
                loadValue(*_plugin, kAuditionParamId) >= 0.5 ? 0.0 : 1.0);
            [self setNeedsDisplay:YES];
            return;
        }
    }

    for (const auto& group : kArticulatorGuiGroups) {
        for (uint32_t row = 0u; row < group.count; ++row) {
            const clap_id id = group.params[row];
            const ParamDef* def = paramDef(id);
            if (def && def->stepped) {
                const NSRect box = articulatorProcessorMenuRect(
                    *group.panel, row);
                if (NSPointInRect(point, box)) {
                    [self openMenuForParam:id box:box];
                    return;
                }
            } else if (NSPointInRect(point,
                    s3g::clap_gui::cocoaRect(
                        s3g::gui_layout::sliderHitRect(
                            *group.panel, row)))) {
                [self beginSlider:id event:event point:point];
                return;
            }
        }
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:
        [event locationInWindow] fromView:nil];
    [self updateMenuHover:point];
    if (_dragParam > 0) [self updateSlider:point];
}

- (void)mouseMoved:(NSEvent*)event
{
    [self updateMenuHover:[self convertPoint:
        [event locationInWindow] fromView:nil]];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragParam = -1;
}

- (void)commitPhrase:(id)sender
{
    (void)sender;
    if (!_plugin) return;
    if (publishTextPhrase(*_plugin, [[_phraseField stringValue] UTF8String])) {
        queueGuiParamValue(*_plugin, kGestureSequenceParamId, 5.0);
    } else {
        NSBeep();
    }
    [self setNeedsDisplay:YES];
}

- (void)controlTextDidBeginEditing:(NSNotification*)notification
{
    if ([notification object] == _phraseField) {
        s3g::clap_gui::styleNumberTextEditor(_phraseField);
    }
}

- (void)controlTextDidEndEditing:(NSNotification*)notification
{
    if ([notification object] == _phraseField) [self commitPhrase:_phraseField];
}

- (BOOL)control:(NSControl*)control textView:(NSTextView*)textView
    doCommandBySelector:(SEL)selector
{
    (void)textView;
    if (control == _phraseField && selector == @selector(insertNewline:)) {
        [self commitPhrase:_phraseField];
        [[self window] makeFirstResponder:self];
        return YES;
    }
    return NO;
}

- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    if ([self isHidden] || !_plugin
        || !s3g::clap_support::hostAppIsActive()) return;
    if (![self phraseIsEditing]) {
        const PhraseState phrase = loadPhrase(*_plugin);
        NSString* current = [NSString stringWithUTF8String:phrase.text.data()];
        if (![[_phraseField stringValue] isEqualToString:current]) {
            [_phraseField setStringValue:current];
        }
    }
    [self setNeedsDisplay:YES];
}

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

@end
#endif

#include "s3g_formant_matrix_gui.inc"

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating)
{
    return !isFloating && api
        && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*, const char** api,
    bool* isFloating)
{
    if (!api || !isFloating) return false;
    *api = CLAP_WINDOW_API_COCOA;
    *isFloating = false;
    return true;
}

bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating)
{
    if (!guiIsApiSupported(plugin, api, isFloating)) return false;
    auto* instance = self(plugin);
    if (instance->guiView) return true;
    instance->guiView = [[S3GFormantMatrixView alloc]
        initWithPlugin:instance];
    if (!instance->guiView) return false;
    if (!s3g::clap_gui::createResponsiveViewport(instance->guiViewport,
            static_cast<NSView*>(instance->guiView), kGuiWidth, kGuiHeight,
            480u, 360u)) {
        [static_cast<NSView*>(instance->guiView) release];
        instance->guiView = nullptr;
        return false;
    }
    return true;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance->guiView) return;
    instance->guiVisible = false;
    [static_cast<S3GFormantMatrixView*>(instance->guiView)
        stopRefreshTimer];
    s3g::clap_gui::destroyResponsiveViewport(instance->guiViewport,
        instance->guiView);
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* width,
    uint32_t* height)
{
    return s3g::clap_gui::getResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height,
        480u, 360u);
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
        480u, 360u);
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
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*)
{
    return false;
}
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance->guiView || !s3g::clap_gui::setResponsiveViewportHidden(
            instance->guiViewport, false)) return false;
    instance->guiVisible = true;
    [static_cast<S3GFormantMatrixView*>(instance->guiView)
        startRefreshTimer];
    return true;
}
bool guiHide(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance->guiView) return false;
    instance->guiVisible = false;
    [static_cast<S3GFormantMatrixView*>(instance->guiView)
        stopRefreshTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(
        instance->guiViewport, true);
}

const clap_plugin_gui_t guiExt {
    guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy,
    guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints,
    guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient,
    guiSuggestTitle, guiShow, guiHide,
};
#endif

const void* getExtension(const clap_plugin_t*, const char* id)
{
    if (!id) return nullptr;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &notePorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
    if (std::strcmp(id, CLAP_EXT_LATENCY) == 0) return &latencyExt;
    if (std::strcmp(id, CLAP_EXT_TAIL) == 0) return &tailExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_FILTER,
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.formant-matrix",
    "s3g Processor Formant Matrix",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "5.9.0",
    "Stereo vocoder and resonant filter matrix with external mic or built-in sample-free speech modulation, procedural MIDI carriers, polyphony, and text-to-phoneme scoring.",
    features
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* instance = new (std::nothrow) Plugin();
    if (!instance) return nullptr;
    instance->host = host;
    for (const auto& def : kParamDefs) storeValue(*instance, def.id, def.defaultValue);
    // Formant Matrix is an input effect first. New instances therefore open
    // in the strict mic-vocoder topology; Internal Speech remains a selectable
    // self-contained modulator rather than silently replacing an absent mic.
    selectPreset(*instance, 14u);
    constexpr const char* defaultPhrase = "hello worlds";
    storePhrase(*instance, defaultPhrase);
    const auto compiled = s3g::compileAcapellaText(defaultPhrase);
    instance->activeTextProgram = compiled.program;
    instance->textGestureCount.store(compiled.program.count,
        std::memory_order_relaxed);
    instance->textWordCount.store(compiled.program.wordCount,
        std::memory_order_relaxed);
    instance->textTruncated.store(compiled.program.truncated,
        std::memory_order_relaxed);
    instance->ensemble.setTextGestureProgram(instance->activeTextProgram);
    syncAudioParams(*instance);
    instance->publishedTailSamples.store(
        calculateTailSamples(*instance), std::memory_order_release);
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
    return pluginId && std::strcmp(pluginId, descriptor.id) == 0
        ? create(host) : nullptr;
}

const clap_plugin_factory_t factory {
    factoryGetPluginCount,
    factoryGetPluginDescriptor,
    factoryCreatePlugin
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
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory
};
