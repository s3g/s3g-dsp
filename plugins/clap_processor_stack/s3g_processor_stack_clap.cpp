#include "s3g_processor_stack.h"
#include "s3g_processor_stack_presets.h"
#include "s3g_processor_stack_score.h"
#include "../common/s3g_clap_gui_param_queue.h"
#include "../common/s3g_clap_state_stream.h"
#include "../common/s3g_drum_midi_receive.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/tail.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include "../common/s3g_clap_macos.h"
#include "../common/s3g_cocoa_gui.h"
#include "../common/s3g_gui_layout.h"
#endif

namespace {

constexpr uint32_t kStateMagic = 0x31545350u; // "PST1" little endian.
constexpr uint32_t kStateVersion = 13u;
constexpr uint32_t kVersionTwelveStateVersion = 12u;
constexpr uint32_t kVersionTwelveParamCount = 111u;
constexpr uint32_t kVersionElevenStateVersion = 11u;
constexpr uint32_t kVersionElevenParamCount = 105u;
constexpr uint32_t kVersionTenStateVersion = 10u;
constexpr uint32_t kVersionTenParamCount = 105u;
constexpr uint32_t kVersionNineStateVersion = 9u;
constexpr uint32_t kVersionNineParamCount = 100u;
constexpr uint32_t kVersionEightStateVersion = 8u;
constexpr uint32_t kVersionEightParamCount = 99u;
constexpr uint32_t kVersionSevenStateVersion = 7u;
constexpr uint32_t kVersionSevenParamCount = 58u;
constexpr uint32_t kVersionSixStateVersion = 6u;
constexpr uint32_t kVersionSixParamCount = 50u;
constexpr uint32_t kVersionFiveStateVersion = 5u;
constexpr uint32_t kVersionFiveParamCount = 46u;
constexpr uint32_t kVersionFourStateVersion = 4u;
constexpr uint32_t kVersionFourParamCount = 45u;
constexpr uint32_t kVersionThreeStateVersion = 3u;
constexpr uint32_t kVersionThreeParamCount = 43u;
constexpr uint32_t kVersionTwoStateVersion = 2u;
constexpr uint32_t kVersionTwoParamCount = 32u;
constexpr uint32_t kVersionOneStateVersion = 1u;
constexpr uint32_t kVersionOneParamCount = 27u;
constexpr uint32_t kOutputChannels = 2u;
constexpr uint32_t kGuiWidth = 980u;
constexpr uint32_t kGuiHeight = 774u;

enum ParamId : clap_id {
    kModeParamId = 1u,
    kShapeParamId,
    kWireParamId,
    kPickParamId,
    kDampingParamId,
    kGlideParamId,
    kCrookedParamId,
    kSpillParamId,
    kCircuitParamId,
    kBiteParamId,
    kPedalToneParamId,
    kBiasParamId,
    kStackParamId,
    kSagParamId,
    kFocusParamId,
    kConeParamId,
    kCabinetParamId,
    kMicParamId,
    kFeedbackParamId,
    kProximityParamId,
    kHarmonicParamId,
    kTrackingParamId,
    kPolarityParamId,
    kRootParamId,
    kChaosParamId,
    kOutputParamId,
    kMidiReceiveParamId,
    kArpPatternParamId,
    kScaleParamId,
    kArpRateParamId,
    kArpOctavesParamId,
    kArpGateParamId,
    kCustomLengthParamId,
    kCustomStep1ParamId,
    kCustomStep2ParamId,
    kCustomStep3ParamId,
    kCustomStep4ParamId,
    kCustomStep5ParamId,
    kCustomStep6ParamId,
    kCustomStep7ParamId,
    kCustomStep8ParamId,
    kPierceParamId,
    kSelfListenParamId,
    kTargetGlitchParamId,
    kGlitchRatchetParamId,
    kOverloadMaskParamId,
    kAttackParamId,
    kDecayParamId,
    kSustainParamId,
    kReleaseParamId,
    kPairAmountParamId,
    kPairRelationParamId,
    kPairLooseParamId,
    kPairSpreadParamId,
    kNeckAParamId,
    kBodyAParamId,
    kNeckBParamId,
    kBodyBParamId,
    kArpBRelationParamId,
    kArpPatternBParamId,
    kScaleBParamId,
    kArpRateBParamId,
    kArpOctavesBParamId,
    kArpGateBParamId,
    kArpPhaseBParamId,
    kCustomLengthBParamId,
    kCustomStepB1ParamId,
    kCustomStepB2ParamId,
    kCustomStepB3ParamId,
    kCustomStepB4ParamId,
    kCustomStepB5ParamId,
    kCustomStepB6ParamId,
    kCustomStepB7ParamId,
    kCustomStepB8ParamId,
    kLinkPedalParamId,
    kLinkAmplifierParamId,
    kLinkFeedbackParamId,
    kCircuitBParamId,
    kBiteBParamId,
    kPedalToneBParamId,
    kBiasBParamId,
    kStackBParamId,
    kSagBParamId,
    kFocusBParamId,
    kConeBParamId,
    kCabinetBParamId,
    kMicBParamId,
    kFeedbackBParamId,
    kProximityBParamId,
    kHarmonicBParamId,
    kTrackingBParamId,
    kPolarityBParamId,
    kRootBParamId,
    kChaosBParamId,
    kPierceBParamId,
    kSelfListenBParamId,
    kTargetGlitchBParamId,
    kGlitchRatchetBParamId,
    kOverloadMaskBParamId,
    kArpHostSyncParamId,
    kScoreEnableParamId,
    kScoreRateParamId,
    kScoreGateParamId,
    kScoreLengthParamId,
    kScoreBSourceParamId,
    kRigLevelAParamId,
    kRigLevelBParamId,
    kRigPanAParamId,
    kRigPanBParamId,
    kRigMuteAParamId,
    kRigMuteBParamId,
    kSpeakerAParamId,
    kSpeakerBParamId,
};

struct ParamDef {
    clap_id id;
    const char* name;
    const char* module;
    double minimum;
    double maximum;
    double defaultValue;
    bool stepped;
};

constexpr std::array<ParamDef, 113u> kParamDefs {{
    { kModeParamId, "Mode", "Play", 0.0, 2.0, 0.0, true },
    { kShapeParamId, "Shape", "Play", 0.0, 1.0, 0.58, false },
    { kWireParamId, "String", "Play", 0.0, 1.0, 0.56, false },
    { kPickParamId, "Pick", "Play", 0.0, 1.0, 0.72, false },
    { kDampingParamId, "Damp", "Play", 0.0, 1.0, 0.38, false },
    { kGlideParamId, "Glide", "Play", 0.0, 2000.0, 34.0, false },
    { kCrookedParamId, "Crooked", "Play", 0.0, 1.0, 0.36, false },
    { kSpillParamId, "Spill", "Play", 0.0, 1.0, 0.32, false },
    { kCircuitParamId, "Circuit", "Pedal", 0.0, 8.0, 2.0, true },
    { kBiteParamId, "Bite", "Pedal", 0.0, 1.0, 0.56, false },
    { kPedalToneParamId, "Tone", "Pedal", 0.0, 1.0, 0.54, false },
    { kBiasParamId, "Bias", "Pedal", 0.0, 1.0, 0.52, false },
    { kStackParamId, "Stack", "Amplifier", 0.0, 1.0, 0.62, false },
    { kSagParamId, "Sag", "Amplifier", 0.0, 1.0, 0.46, false },
    { kFocusParamId, "Focus", "Amplifier", 0.0, 1.0, 0.55, false },
    { kConeParamId, "Cone", "Amplifier", 0.0, 1.0, 0.64, false },
    { kCabinetParamId, "Cab", "Amplifier", 0.0, 1.0, 0.52, false },
    { kMicParamId, "Mic", "Amplifier", 0.0, 1.0, 0.34, false },
    { kFeedbackParamId, "Feedback", "Loop", 0.0, 1.0, 0.56, false },
    { kProximityParamId, "Proximity", "Loop", 0.0, 1.0, 0.58, false },
    { kHarmonicParamId, "Harmonic", "Loop", 0.0, 1.0, 0.42, false },
    { kTrackingParamId, "Track", "Loop", 0.0, 1.0, 0.72, false },
    { kPolarityParamId, "Polarity", "Loop", 0.0, 1.0, 0.78, false },
    { kRootParamId, "Root", "Loop", 0.0, 1.0, 0.28, false },
    { kChaosParamId, "Chaos", "Loop", 0.0, 1.0, 0.32, false },
    { kOutputParamId, "Output", "Output", -36.0, 6.0, -12.0, false },
    { kMidiReceiveParamId, "MIDI Receive", "Routing", 0.0, 16.0, 0.0, true },
    { kArpPatternParamId, "Arp Pattern", "Arpeggiator", 0.0, 6.0, 0.0, true },
    { kScaleParamId, "Scale Rule", "Arpeggiator", 0.0, 13.0, 1.0, true },
    { kArpRateParamId, "Arp Rate", "Arpeggiator", 0.0, 8.0, 2.0, true },
    { kArpOctavesParamId, "Arp Octaves", "Arpeggiator", 1.0, 4.0, 2.0, true },
    { kArpGateParamId, "Arp Gate", "Arpeggiator", 0.05, 1.0, 0.62, false },
    { kCustomLengthParamId, "Pattern Length", "Arpeggiator", 1.0, 8.0, 8.0, true },
    { kCustomStep1ParamId, "Pattern Step 1", "Arpeggiator", -9.0, 15.0, 0.0, true },
    { kCustomStep2ParamId, "Pattern Step 2", "Arpeggiator", -9.0, 15.0, 1.0, true },
    { kCustomStep3ParamId, "Pattern Step 3", "Arpeggiator", -9.0, 15.0, 2.0, true },
    { kCustomStep4ParamId, "Pattern Step 4", "Arpeggiator", -9.0, 15.0, 4.0, true },
    { kCustomStep5ParamId, "Pattern Step 5", "Arpeggiator", -9.0, 15.0, 3.0, true },
    { kCustomStep6ParamId, "Pattern Step 6", "Arpeggiator", -9.0, 15.0, 6.0, true },
    { kCustomStep7ParamId, "Pattern Step 7", "Arpeggiator", -9.0, 15.0, 5.0, true },
    { kCustomStep8ParamId, "Pattern Step 8", "Arpeggiator", -9.0, 15.0, 1.0, true },
    { kPierceParamId, "Pierce", "Loop", 0.0, 1.0, 0.68, false },
    { kSelfListenParamId, "Self Listen", "Loop", 0.0, 1.0, 0.72, false },
    { kTargetGlitchParamId, "Target Glitch", "Loop", 0.0, 1.0, 0.0, false },
    { kGlitchRatchetParamId, "Glitch Ratchet", "Loop", 0.0, 1.0, 0.46, false },
    { kOverloadMaskParamId, "Overload Mask", "Loop", 0.0, 1.0, 0.76, false },
    { kAttackParamId, "Attack", "Play", 0.0, 2000.0, 2.0, false },
    { kDecayParamId, "Decay", "Play", 5.0, 8000.0, 180.0, false },
    { kSustainParamId, "Sustain", "Play", 0.0, 1.0, 0.78, false },
    { kReleaseParamId, "Release", "Play", 5.0, 20000.0, 90.0, false },
    { kPairAmountParamId, "Dual", "Two Guitars", 0.0, 1.0, 0.0, false },
    { kPairRelationParamId, "Relation", "Two Guitars", 0.0, 4.0, 0.0, true },
    { kPairLooseParamId, "Loose", "Two Guitars", 0.0, 1.0, 0.24, false },
    { kPairSpreadParamId, "Spread", "Two Guitars", 0.0, 1.0, 0.72, false },
    { kNeckAParamId, "Neck A", "Two Guitars", 0.0, 3.0, 0.0, true },
    { kBodyAParamId, "Body A", "Two Guitars", 0.0, 3.0, 0.0, true },
    { kNeckBParamId, "Neck B", "Two Guitars", 0.0, 3.0, 2.0, true },
    { kBodyBParamId, "Body B", "Two Guitars", 0.0, 3.0, 1.0, true },
    { kArpBRelationParamId, "Arp B Relation", "Arpeggiator B", 0.0, 2.0, 0.0, true },
    { kArpPatternBParamId, "Arp Pattern B", "Arpeggiator B", 0.0, 6.0, 0.0, true },
    { kScaleBParamId, "Scale Rule B", "Arpeggiator B", 0.0, 13.0, 1.0, true },
    { kArpRateBParamId, "Arp Rate B", "Arpeggiator B", 0.0, 8.0, 2.0, true },
    { kArpOctavesBParamId, "Arp Octaves B", "Arpeggiator B", 1.0, 4.0, 2.0, true },
    { kArpGateBParamId, "Arp Gate B", "Arpeggiator B", 0.05, 1.0, 0.62, false },
    { kArpPhaseBParamId, "Arp Phase B", "Arpeggiator B", 0.0, 1.0, 0.50, false },
    { kCustomLengthBParamId, "Pattern Length B", "Arpeggiator B", 1.0, 8.0, 8.0, true },
    { kCustomStepB1ParamId, "Pattern B Step 1", "Arpeggiator B", -9.0, 15.0, 0.0, true },
    { kCustomStepB2ParamId, "Pattern B Step 2", "Arpeggiator B", -9.0, 15.0, 4.0, true },
    { kCustomStepB3ParamId, "Pattern B Step 3", "Arpeggiator B", -9.0, 15.0, 2.0, true },
    { kCustomStepB4ParamId, "Pattern B Step 4", "Arpeggiator B", -9.0, 15.0, 6.0, true },
    { kCustomStepB5ParamId, "Pattern B Step 5", "Arpeggiator B", -9.0, 15.0, 1.0, true },
    { kCustomStepB6ParamId, "Pattern B Step 6", "Arpeggiator B", -9.0, 15.0, 5.0, true },
    { kCustomStepB7ParamId, "Pattern B Step 7", "Arpeggiator B", -9.0, 15.0, 3.0, true },
    { kCustomStepB8ParamId, "Pattern B Step 8", "Arpeggiator B", -9.0, 15.0, 7.0, true },
    { kLinkPedalParamId, "Link Pedals", "Links", 0.0, 1.0, 1.0, true },
    { kLinkAmplifierParamId, "Link Amplifiers", "Links", 0.0, 1.0, 1.0, true },
    { kLinkFeedbackParamId, "Link Feedback", "Links", 0.0, 1.0, 1.0, true },
    { kCircuitBParamId, "Circuit B", "Pedal B", 0.0, 8.0, 2.0, true },
    { kBiteBParamId, "Bite B", "Pedal B", 0.0, 1.0, 0.56, false },
    { kPedalToneBParamId, "Tone B", "Pedal B", 0.0, 1.0, 0.54, false },
    { kBiasBParamId, "Bias B", "Pedal B", 0.0, 1.0, 0.52, false },
    { kStackBParamId, "Stack B", "Amplifier B", 0.0, 1.0, 0.62, false },
    { kSagBParamId, "Sag B", "Amplifier B", 0.0, 1.0, 0.46, false },
    { kFocusBParamId, "Focus B", "Amplifier B", 0.0, 1.0, 0.55, false },
    { kConeBParamId, "Cone B", "Amplifier B", 0.0, 1.0, 0.64, false },
    { kCabinetBParamId, "Cab B", "Amplifier B", 0.0, 1.0, 0.52, false },
    { kMicBParamId, "Mic B", "Amplifier B", 0.0, 1.0, 0.34, false },
    { kFeedbackBParamId, "Feedback B", "Loop B", 0.0, 1.0, 0.56, false },
    { kProximityBParamId, "Proximity B", "Loop B", 0.0, 1.0, 0.58, false },
    { kHarmonicBParamId, "Harmonic B", "Loop B", 0.0, 1.0, 0.42, false },
    { kTrackingBParamId, "Track B", "Loop B", 0.0, 1.0, 0.72, false },
    { kPolarityBParamId, "Polarity B", "Loop B", 0.0, 1.0, 0.78, false },
    { kRootBParamId, "Root B", "Loop B", 0.0, 1.0, 0.28, false },
    { kChaosBParamId, "Chaos B", "Loop B", 0.0, 1.0, 0.32, false },
    { kPierceBParamId, "Pierce B", "Loop B", 0.0, 1.0, 0.68, false },
    { kSelfListenBParamId, "Self Listen B", "Loop B", 0.0, 1.0, 0.72, false },
    { kTargetGlitchBParamId, "Target Glitch B", "Loop B", 0.0, 1.0, 0.0, false },
    { kGlitchRatchetBParamId, "Glitch Ratchet B", "Loop B", 0.0, 1.0, 0.46, false },
    { kOverloadMaskBParamId, "Overload Mask B", "Loop B", 0.0, 1.0, 0.76, false },
    { kArpHostSyncParamId, "Arp Host Sync", "Routing", 0.0, 1.0, 0.0, true },
    { kScoreEnableParamId, "Score Playback", "Stack Score", 0.0, 1.0, 0.0, true },
    { kScoreRateParamId, "Score Rate", "Stack Score", 0.0, 8.0, 2.0, true },
    { kScoreGateParamId, "Score Gate", "Stack Score", 0.05, 1.0, 0.72, false },
    { kScoreLengthParamId, "Arrangement Length", "Stack Score", 1.0, 8.0, 4.0, true },
    { kScoreBSourceParamId, "Player B Source", "Stack Score", 0.0, 1.0, 0.0, true },
    { kRigLevelAParamId, "Rig A Level", "Output Mixer", -36.0, 6.0, 0.0, false },
    { kRigLevelBParamId, "Rig B Level", "Output Mixer", -36.0, 6.0, 0.0, false },
    { kRigPanAParamId, "Rig A Pan", "Output Mixer", -1.0, 1.0, -1.0, false },
    { kRigPanBParamId, "Rig B Pan", "Output Mixer", -1.0, 1.0, 1.0, false },
    { kRigMuteAParamId, "Rig A Mute", "Output Mixer", 0.0, 1.0, 0.0, true },
    { kRigMuteBParamId, "Rig B Mute", "Output Mixer", 0.0, 1.0, 0.0, true },
    { kSpeakerAParamId, "Speaker A", "Amplifier", 0.0, 6.0, 2.0, true },
    { kSpeakerBParamId, "Speaker B", "Amplifier B", 0.0, 6.0, 2.0, true },
}};

constexpr uint32_t kSynthParamCount = 106u;
constexpr uint32_t kPublishedParamCount =
    static_cast<uint32_t>(kSpeakerBParamId) + 1u;

constexpr std::array<clap_id, kSynthParamCount> kSynthParamIds {{
    kModeParamId, kShapeParamId, kWireParamId, kPickParamId,
    kDampingParamId, kGlideParamId, kCrookedParamId, kSpillParamId,
    kCircuitParamId, kBiteParamId, kPedalToneParamId, kBiasParamId,
    kStackParamId, kSagParamId, kFocusParamId, kConeParamId,
    kCabinetParamId, kMicParamId, kFeedbackParamId, kProximityParamId,
    kHarmonicParamId, kTrackingParamId, kPolarityParamId, kRootParamId,
    kChaosParamId, kOutputParamId,
    kArpPatternParamId, kScaleParamId, kArpRateParamId,
    kArpOctavesParamId, kArpGateParamId,
    kCustomLengthParamId, kCustomStep1ParamId, kCustomStep2ParamId,
    kCustomStep3ParamId, kCustomStep4ParamId, kCustomStep5ParamId,
    kCustomStep6ParamId, kCustomStep7ParamId, kCustomStep8ParamId,
    kPierceParamId, kSelfListenParamId,
    kTargetGlitchParamId, kGlitchRatchetParamId,
    kOverloadMaskParamId,
    kAttackParamId, kDecayParamId, kSustainParamId, kReleaseParamId,
    kPairAmountParamId, kPairRelationParamId,
    kPairLooseParamId, kPairSpreadParamId,
    kNeckAParamId, kBodyAParamId, kNeckBParamId, kBodyBParamId,
    kArpBRelationParamId, kArpPatternBParamId, kScaleBParamId,
    kArpRateBParamId, kArpOctavesBParamId, kArpGateBParamId,
    kArpPhaseBParamId, kCustomLengthBParamId,
    kCustomStepB1ParamId, kCustomStepB2ParamId, kCustomStepB3ParamId,
    kCustomStepB4ParamId, kCustomStepB5ParamId, kCustomStepB6ParamId,
    kCustomStepB7ParamId, kCustomStepB8ParamId,
    kLinkPedalParamId, kLinkAmplifierParamId, kLinkFeedbackParamId,
    kCircuitBParamId, kBiteBParamId, kPedalToneBParamId, kBiasBParamId,
    kStackBParamId, kSagBParamId, kFocusBParamId, kConeBParamId,
    kCabinetBParamId, kMicBParamId,
    kFeedbackBParamId, kProximityBParamId, kHarmonicBParamId,
    kTrackingBParamId, kPolarityBParamId, kRootBParamId, kChaosBParamId,
    kPierceBParamId, kSelfListenBParamId, kTargetGlitchBParamId,
    kGlitchRatchetBParamId, kOverloadMaskBParamId,
    kRigLevelAParamId, kRigLevelBParamId,
    kRigPanAParamId, kRigPanBParamId,
    kRigMuteAParamId, kRigMuteBParamId,
    kSpeakerAParamId, kSpeakerBParamId,
}};

struct SavedStateHeader {
    uint32_t magic = kStateMagic;
    uint32_t version = kStateVersion;
    uint32_t valueCount = static_cast<uint32_t>(kParamDefs.size());
    uint32_t reserved = 0u;
};

struct SavedState {
    SavedStateHeader header {};
    std::array<double, kParamDefs.size()> values {};
    s3g::ProcessorStackScoreProgram score {};
};

struct VersionTenScoreProgram {
    std::array<int8_t, s3g::kProcessorStackScoreCellCount> cells {};
    std::array<uint8_t, s3g::kProcessorStackScoreArrangementSlots>
        arrangement {};
};

static_assert(sizeof(VersionTenScoreProgram) == 776u);
static_assert(sizeof(s3g::ProcessorStackScoreProgram) == 1800u);
static_assert(sizeof(SavedState) == 2720u);

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    const clap_host_tail_t* hostTail = nullptr;
    double sampleRate = 48000.0;
    double tempoBpm = 120.0;
    s3g::ProcessorStack engine {};
    s3g::ProcessorStackParams params {};
    double midiReceive = 0.0;
    bool scoreEnabled = false;
    s3g::ProcessorStackArpRate scoreRate =
        s3g::ProcessorStackArpRate::Sixteenth;
    float scoreGate = 0.72f;
    uint32_t scoreLength = 4u;
    bool scoreBRelatesToA = false;
    std::array<std::atomic<int32_t>, s3g::kProcessorStackScoreCellCount>
        scoreCells {};
    std::array<std::atomic<uint32_t>,
        s3g::kProcessorStackScoreArrangementSlots> scoreArrangement {};
    std::array<std::atomic<uint32_t>,
        s3g::kProcessorStackScoreLockCellCount> scoreLocks {};
    std::atomic<uint64_t> scoreRevision { 0u };
    std::atomic<int32_t> scoreVisualSection { -1 };
    std::atomic<int32_t> scoreVisualRow { -1 };
    std::atomic<int32_t> scoreVisualSlot { -1 };
    std::atomic<uint64_t> scoreVisualRevision { 0u };
    s3g::ProcessorStackScoreProgram scorePlaybackProgram {};
    int64_t scoreLastGlobalRow = std::numeric_limits<int64_t>::min();
    uint64_t scoreAppliedRevision = 0u;
    uint64_t scoreAppliedParameterRevision = 0u;
    int32_t scorePublishedSection = -1;
    int32_t scorePublishedRow = -1;
    int32_t scorePublishedSlot = -1;
    bool scoreGateClosed = true;
    bool scoreWasRunning = false;
    std::array<std::array<bool, 128u>, 16u> midiHeldNotes {};
    std::array<float, 16u> midiChannelPressure {};
    std::array<float, 16u> midiChannelBend {};
    std::array<float, 16u> midiChannelTimbre {};
    std::array<uint8_t, 16u> midiRpnMsb {};
    std::array<uint8_t, 16u> midiRpnLsb {};
    std::array<uint8_t, 16u> midiBendRangeSemitones {};
    std::array<uint8_t, 16u> midiBendRangeCents {};
    std::array<std::atomic<double>, kPublishedParamCount> publishedParams {};
    s3g::clap_gui::ParamEventQueue<> guiParamEvents {};
    std::atomic<bool> tailChangePending { false };
    std::atomic<uint64_t> parameterRevision { 0u };
    std::atomic<float> outputPeak { 0.0f };
    bool active = false;
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

void resetMidiExpressionState(Plugin& plugin)
{
    for (auto& channel : plugin.midiHeldNotes) channel.fill(false);
    plugin.midiChannelPressure.fill(0.0f);
    plugin.midiChannelBend.fill(0.0f);
    plugin.midiChannelTimbre.fill(0.5f);
    plugin.midiRpnMsb.fill(127u);
    plugin.midiRpnLsb.fill(127u);
    plugin.midiBendRangeSemitones.fill(2u);
    plugin.midiBendRangeCents.fill(0u);
}

s3g::ProcessorStackScoreProgram scoreProgramSnapshot(const Plugin& plugin)
{
    s3g::ProcessorStackScoreProgram program;
    for (size_t index = 0u; index < program.cells.size(); ++index) {
        program.cells[index] = static_cast<int8_t>(std::clamp(
            plugin.scoreCells[index].load(std::memory_order_acquire),
            static_cast<int32_t>(s3g::kProcessorStackScoreHold),
            static_cast<int32_t>(s3g::kProcessorStackScoreMaximumFret)));
    }
    for (size_t slot = 0u; slot < program.arrangement.size(); ++slot) {
        program.arrangement[slot] = static_cast<uint8_t>(std::min<uint32_t>(
            plugin.scoreArrangement[slot].load(std::memory_order_acquire),
            s3g::kProcessorStackScoreSectionCount - 1u));
    }
    for (size_t index = 0u; index < program.locks.size(); ++index) {
        const uint32_t packed = plugin.scoreLocks[index].load(
            std::memory_order_acquire);
        program.locks[index].control = static_cast<uint8_t>(packed & 0xffu);
        program.locks[index].reserved = 0u;
        program.locks[index].normalized = static_cast<uint16_t>(
            (packed >> 8u) & 0xffffu);
    }
    return program;
}

void notifyScoreChanged(Plugin& plugin)
{
    if (!plugin.host) return;
    if (plugin.host->request_process) {
        plugin.host->request_process(plugin.host);
    }
    if (!plugin.host->get_extension) return;
    const auto* state = static_cast<const clap_host_state_t*>(
        plugin.host->get_extension(plugin.host, CLAP_EXT_STATE));
    if (state && state->mark_dirty) state->mark_dirty(plugin.host);
}

void storeScoreProgram(Plugin& plugin,
    s3g::ProcessorStackScoreProgram program, bool notifyHost)
{
    program = s3g::sanitizeProcessorStackScoreProgram(program);
    for (size_t index = 0u; index < program.cells.size(); ++index) {
        plugin.scoreCells[index].store(
            program.cells[index], std::memory_order_release);
    }
    for (size_t slot = 0u; slot < program.arrangement.size(); ++slot) {
        plugin.scoreArrangement[slot].store(
            program.arrangement[slot], std::memory_order_release);
    }
    for (size_t index = 0u; index < program.locks.size(); ++index) {
        const auto lock = program.locks[index];
        const uint32_t packed = static_cast<uint32_t>(lock.control)
            | (static_cast<uint32_t>(lock.normalized) << 8u);
        plugin.scoreLocks[index].store(packed, std::memory_order_release);
    }
    plugin.scoreRevision.fetch_add(1u, std::memory_order_acq_rel);
    if (notifyHost) notifyScoreChanged(plugin);
}

void setScoreLock(Plugin& plugin, uint32_t section, uint32_t row,
    uint32_t player, uint32_t slot,
    s3g::ProcessorStackScoreLockControl control, double normalized)
{
    s3g::ProcessorStackScoreLockCell lock;
    lock.control = static_cast<uint8_t>(
        static_cast<uint32_t>(control)
            < s3g::kProcessorStackScoreLockControlCount
        ? control : s3g::ProcessorStackScoreLockControl::None);
    normalized = std::isfinite(normalized) ? normalized : 0.0;
    lock.normalized = static_cast<uint16_t>(std::lround(
        std::clamp(normalized, 0.0, 1.0) * 65535.0));
    const uint32_t packed = static_cast<uint32_t>(lock.control)
        | (static_cast<uint32_t>(lock.normalized) << 8u);
    const size_t index = s3g::processorStackScoreLockIndex(
        std::min(section, s3g::kProcessorStackScoreSectionCount - 1u),
        std::min(row, s3g::kProcessorStackScoreRowsPerSection - 1u),
        std::min(player, s3g::kProcessorStackScorePlayerCount - 1u),
        std::min(slot, s3g::kProcessorStackScoreLocksPerPlayer - 1u));
    plugin.scoreLocks[index].store(packed, std::memory_order_release);
    plugin.scoreRevision.fetch_add(1u, std::memory_order_acq_rel);
    notifyScoreChanged(plugin);
}

void setScoreCell(Plugin& plugin, uint32_t section, uint32_t row,
    uint32_t player, uint32_t string, int fret)
{
    const size_t index = s3g::processorStackScoreCellIndex(
        std::min(section, s3g::kProcessorStackScoreSectionCount - 1u),
        std::min(row, s3g::kProcessorStackScoreRowsPerSection - 1u),
        std::min(player, s3g::kProcessorStackScorePlayerCount - 1u),
        std::min(string, s3g::kProcessorStackScoreStringCount - 1u));
    plugin.scoreCells[index].store(std::clamp(fret,
        static_cast<int>(s3g::kProcessorStackScoreHold),
        static_cast<int>(s3g::kProcessorStackScoreMaximumFret)),
        std::memory_order_release);
    plugin.scoreRevision.fetch_add(1u, std::memory_order_acq_rel);
    notifyScoreChanged(plugin);
}

const ParamDef* paramDef(clap_id id)
{
    const auto found = std::find_if(kParamDefs.begin(), kParamDefs.end(),
        [id](const ParamDef& def) { return def.id == id; });
    return found == kParamDefs.end() ? nullptr : &*found;
}

double clampValue(const ParamDef& def, double value)
{
    value = std::isfinite(value) ? value : def.defaultValue;
    value = std::clamp(value, def.minimum, def.maximum);
    return def.stepped ? std::round(value) : value;
}

void publishParam(Plugin& plugin, clap_id id, double value)
{
    if (id >= kPublishedParamCount || !paramDef(id)) return;
    plugin.publishedParams[id].store(value, std::memory_order_release);
}

double paramValue(const Plugin& plugin, clap_id id)
{
    if (id >= kPublishedParamCount || !paramDef(id)) return 0.0;
    return plugin.publishedParams[id].load(std::memory_order_acquire);
}

double rawParamValue(const Plugin& plugin, clap_id id)
{
    const auto& params = plugin.params;
    switch (id) {
    case kModeParamId: return static_cast<double>(params.mode);
    case kShapeParamId: return params.shape;
    case kWireParamId: return params.wire;
    case kPickParamId: return params.pick;
    case kDampingParamId: return params.damping;
    case kGlideParamId: return params.glideMs;
    case kCrookedParamId: return params.crooked;
    case kSpillParamId: return params.spill;
    case kAttackParamId: return params.attackMs;
    case kDecayParamId: return params.decayMs;
    case kSustainParamId: return params.sustain;
    case kReleaseParamId: return params.releaseMs;
    case kPairAmountParamId: return params.pairAmount;
    case kPairRelationParamId:
        return static_cast<double>(params.pairRelation);
    case kPairLooseParamId: return params.pairLoose;
    case kPairSpreadParamId: return params.pairSpread;
    case kRigLevelAParamId: return params.rigLevelADb;
    case kRigLevelBParamId: return params.rigLevelBDb;
    case kRigPanAParamId: return params.rigPanA;
    case kRigPanBParamId: return params.rigPanB;
    case kRigMuteAParamId: return params.rigMuteA ? 1.0 : 0.0;
    case kRigMuteBParamId: return params.rigMuteB ? 1.0 : 0.0;
    case kSpeakerAParamId: return static_cast<double>(params.speaker);
    case kSpeakerBParamId: return static_cast<double>(params.speakerB);
    case kNeckAParamId: return static_cast<double>(params.neckA);
    case kBodyAParamId: return static_cast<double>(params.bodyA);
    case kNeckBParamId: return static_cast<double>(params.neckB);
    case kBodyBParamId: return static_cast<double>(params.bodyB);
    case kCircuitParamId: return static_cast<double>(params.circuit);
    case kBiteParamId: return params.bite;
    case kPedalToneParamId: return params.pedalTone;
    case kBiasParamId: return params.bias;
    case kStackParamId: return params.stack;
    case kSagParamId: return params.sag;
    case kFocusParamId: return params.focus;
    case kConeParamId: return params.cone;
    case kCabinetParamId: return params.cabinet;
    case kMicParamId: return params.mic;
    case kFeedbackParamId: return params.feedback;
    case kProximityParamId: return params.proximity;
    case kHarmonicParamId: return params.harmonic;
    case kTrackingParamId: return params.tracking;
    case kPolarityParamId: return params.polarity;
    case kRootParamId: return params.root;
    case kChaosParamId: return params.chaos;
    case kOutputParamId: return params.outputGainDb;
    case kMidiReceiveParamId: return plugin.midiReceive;
    case kArpPatternParamId: return static_cast<double>(params.arpPattern);
    case kScaleParamId: return static_cast<double>(params.scale);
    case kArpRateParamId: return static_cast<double>(params.arpRate);
    case kArpOctavesParamId: return params.arpOctaves;
    case kArpGateParamId: return params.arpGate;
    case kCustomLengthParamId: return params.customPatternLength;
    case kCustomStep1ParamId: return params.customPattern[0u];
    case kCustomStep2ParamId: return params.customPattern[1u];
    case kCustomStep3ParamId: return params.customPattern[2u];
    case kCustomStep4ParamId: return params.customPattern[3u];
    case kCustomStep5ParamId: return params.customPattern[4u];
    case kCustomStep6ParamId: return params.customPattern[5u];
    case kCustomStep7ParamId: return params.customPattern[6u];
    case kCustomStep8ParamId: return params.customPattern[7u];
    case kPierceParamId: return params.pierce;
    case kSelfListenParamId: return params.selfListen;
    case kTargetGlitchParamId: return params.targetGlitch;
    case kGlitchRatchetParamId: return params.glitchRatchet;
    case kOverloadMaskParamId: return params.overloadMask;
    case kArpBRelationParamId:
        return static_cast<double>(params.arpBRelation);
    case kArpPatternBParamId:
        return static_cast<double>(params.arpPatternB);
    case kScaleBParamId: return static_cast<double>(params.scaleB);
    case kArpRateBParamId: return static_cast<double>(params.arpRateB);
    case kArpOctavesBParamId: return params.arpOctavesB;
    case kArpGateBParamId: return params.arpGateB;
    case kArpPhaseBParamId: return params.arpPhaseB;
    case kCustomLengthBParamId: return params.customPatternLengthB;
    case kCustomStepB1ParamId: return params.customPatternB[0u];
    case kCustomStepB2ParamId: return params.customPatternB[1u];
    case kCustomStepB3ParamId: return params.customPatternB[2u];
    case kCustomStepB4ParamId: return params.customPatternB[3u];
    case kCustomStepB5ParamId: return params.customPatternB[4u];
    case kCustomStepB6ParamId: return params.customPatternB[5u];
    case kCustomStepB7ParamId: return params.customPatternB[6u];
    case kCustomStepB8ParamId: return params.customPatternB[7u];
    case kLinkPedalParamId: return params.linkPedal ? 1.0 : 0.0;
    case kLinkAmplifierParamId: return params.linkAmplifier ? 1.0 : 0.0;
    case kLinkFeedbackParamId: return params.linkFeedback ? 1.0 : 0.0;
    case kCircuitBParamId: return static_cast<double>(params.circuitB);
    case kBiteBParamId: return params.biteB;
    case kPedalToneBParamId: return params.pedalToneB;
    case kBiasBParamId: return params.biasB;
    case kStackBParamId: return params.stackB;
    case kSagBParamId: return params.sagB;
    case kFocusBParamId: return params.focusB;
    case kConeBParamId: return params.coneB;
    case kCabinetBParamId: return params.cabinetB;
    case kMicBParamId: return params.micB;
    case kFeedbackBParamId: return params.feedbackB;
    case kProximityBParamId: return params.proximityB;
    case kHarmonicBParamId: return params.harmonicB;
    case kTrackingBParamId: return params.trackingB;
    case kPolarityBParamId: return params.polarityB;
    case kRootBParamId: return params.rootB;
    case kChaosBParamId: return params.chaosB;
    case kPierceBParamId: return params.pierceB;
    case kSelfListenBParamId: return params.selfListenB;
    case kTargetGlitchBParamId: return params.targetGlitchB;
    case kGlitchRatchetBParamId: return params.glitchRatchetB;
    case kOverloadMaskBParamId: return params.overloadMaskB;
    case kArpHostSyncParamId: return params.arpHostSync ? 1.0 : 0.0;
    case kScoreEnableParamId: return plugin.scoreEnabled ? 1.0 : 0.0;
    case kScoreRateParamId:
        return static_cast<double>(plugin.scoreRate);
    case kScoreGateParamId: return plugin.scoreGate;
    case kScoreLengthParamId: return plugin.scoreLength;
    case kScoreBSourceParamId:
        return plugin.scoreBRelatesToA ? 1.0 : 0.0;
    default: return 0.0;
    }
}

void applyParam(Plugin& plugin, clap_id id, double value)
{
    const auto* def = paramDef(id);
    if (!def) return;
    value = clampValue(*def, value);
    if (id == kMidiReceiveParamId) {
        plugin.midiReceive = value;
        publishParam(plugin, id, value);
        plugin.parameterRevision.fetch_add(1u, std::memory_order_release);
        return;
    }
    if (id == kScoreEnableParamId || id == kScoreRateParamId
        || id == kScoreGateParamId || id == kScoreLengthParamId
        || id == kScoreBSourceParamId) {
        switch (id) {
        case kScoreEnableParamId: plugin.scoreEnabled = value >= 0.5; break;
        case kScoreRateParamId:
            plugin.scoreRate = static_cast<s3g::ProcessorStackArpRate>(
                static_cast<uint32_t>(std::lround(value)));
            break;
        case kScoreGateParamId:
            plugin.scoreGate = static_cast<float>(value);
            break;
        case kScoreLengthParamId:
            plugin.scoreLength = static_cast<uint32_t>(std::lround(value));
            break;
        case kScoreBSourceParamId:
            plugin.scoreBRelatesToA = value >= 0.5;
            break;
        default: break;
        }
        publishParam(plugin, id, rawParamValue(plugin, id));
        plugin.parameterRevision.fetch_add(1u, std::memory_order_release);
        if (plugin.host && plugin.host->request_process) {
            plugin.host->request_process(plugin.host);
        }
        return;
    }

    const float normalized = static_cast<float>(value);
    const bool tailChanged = id == kFeedbackParamId
        || id == kFeedbackBParamId || id == kSpillParamId
        || id == kReleaseParamId || id == kLinkFeedbackParamId;
    switch (id) {
    case kModeParamId:
        plugin.params.mode = static_cast<s3g::ProcessorStackMode>(
            static_cast<uint32_t>(std::lround(value)));
        break;
    case kShapeParamId: plugin.params.shape = normalized; break;
    case kWireParamId: plugin.params.wire = normalized; break;
    case kPickParamId: plugin.params.pick = normalized; break;
    case kDampingParamId: plugin.params.damping = normalized; break;
    case kGlideParamId: plugin.params.glideMs = normalized; break;
    case kCrookedParamId: plugin.params.crooked = normalized; break;
    case kSpillParamId: plugin.params.spill = normalized; break;
    case kAttackParamId: plugin.params.attackMs = normalized; break;
    case kDecayParamId: plugin.params.decayMs = normalized; break;
    case kSustainParamId: plugin.params.sustain = normalized; break;
    case kReleaseParamId: plugin.params.releaseMs = normalized; break;
    case kPairAmountParamId: plugin.params.pairAmount = normalized; break;
    case kPairRelationParamId:
        plugin.params.pairRelation =
            static_cast<s3g::ProcessorStackPairRelation>(
                static_cast<uint32_t>(std::lround(value)));
        break;
    case kPairLooseParamId: plugin.params.pairLoose = normalized; break;
    case kPairSpreadParamId: plugin.params.pairSpread = normalized; break;
    case kRigLevelAParamId: plugin.params.rigLevelADb = normalized; break;
    case kRigLevelBParamId: plugin.params.rigLevelBDb = normalized; break;
    case kRigPanAParamId: plugin.params.rigPanA = normalized; break;
    case kRigPanBParamId: plugin.params.rigPanB = normalized; break;
    case kRigMuteAParamId: plugin.params.rigMuteA = value >= 0.5; break;
    case kRigMuteBParamId: plugin.params.rigMuteB = value >= 0.5; break;
    case kSpeakerAParamId:
        plugin.params.speaker = static_cast<s3g::ProcessorStackSpeakerProfile>(
            static_cast<uint32_t>(std::lround(value)));
        break;
    case kSpeakerBParamId:
        plugin.params.speakerB = static_cast<s3g::ProcessorStackSpeakerProfile>(
            static_cast<uint32_t>(std::lround(value)));
        break;
    case kNeckAParamId:
        plugin.params.neckA = static_cast<s3g::ProcessorStackNeckMaterial>(
            static_cast<uint32_t>(std::lround(value)));
        break;
    case kBodyAParamId:
        plugin.params.bodyA = static_cast<s3g::ProcessorStackBodyMaterial>(
            static_cast<uint32_t>(std::lround(value)));
        break;
    case kNeckBParamId:
        plugin.params.neckB = static_cast<s3g::ProcessorStackNeckMaterial>(
            static_cast<uint32_t>(std::lround(value)));
        break;
    case kBodyBParamId:
        plugin.params.bodyB = static_cast<s3g::ProcessorStackBodyMaterial>(
            static_cast<uint32_t>(std::lround(value)));
        break;
    case kCircuitParamId:
        plugin.params.circuit = static_cast<s3g::ProcessorStackCircuit>(
            static_cast<uint32_t>(std::lround(value)));
        break;
    case kBiteParamId: plugin.params.bite = normalized; break;
    case kPedalToneParamId: plugin.params.pedalTone = normalized; break;
    case kBiasParamId: plugin.params.bias = normalized; break;
    case kStackParamId: plugin.params.stack = normalized; break;
    case kSagParamId: plugin.params.sag = normalized; break;
    case kFocusParamId: plugin.params.focus = normalized; break;
    case kConeParamId: plugin.params.cone = normalized; break;
    case kCabinetParamId: plugin.params.cabinet = normalized; break;
    case kMicParamId: plugin.params.mic = normalized; break;
    case kFeedbackParamId: plugin.params.feedback = normalized; break;
    case kProximityParamId: plugin.params.proximity = normalized; break;
    case kHarmonicParamId: plugin.params.harmonic = normalized; break;
    case kTrackingParamId: plugin.params.tracking = normalized; break;
    case kPolarityParamId: plugin.params.polarity = normalized; break;
    case kRootParamId: plugin.params.root = normalized; break;
    case kChaosParamId: plugin.params.chaos = normalized; break;
    case kOutputParamId: plugin.params.outputGainDb = normalized; break;
    case kArpPatternParamId:
        plugin.params.arpPattern = static_cast<s3g::ProcessorStackArpPattern>(
            static_cast<uint32_t>(std::lround(value)));
        break;
    case kScaleParamId:
        plugin.params.scale = static_cast<s3g::ProcessorStackScale>(
            static_cast<uint32_t>(std::lround(value)));
        break;
    case kArpRateParamId:
        plugin.params.arpRate = static_cast<s3g::ProcessorStackArpRate>(
            static_cast<uint32_t>(std::lround(value)));
        break;
    case kArpOctavesParamId:
        plugin.params.arpOctaves = static_cast<uint32_t>(std::lround(value));
        break;
    case kArpGateParamId: plugin.params.arpGate = normalized; break;
    case kCustomLengthParamId:
        plugin.params.customPatternLength = static_cast<uint32_t>(
            std::lround(value));
        break;
    case kCustomStep1ParamId:
        plugin.params.customPattern[0u] = static_cast<int32_t>(
            std::lround(value));
        break;
    case kCustomStep2ParamId:
        plugin.params.customPattern[1u] = static_cast<int32_t>(
            std::lround(value));
        break;
    case kCustomStep3ParamId:
        plugin.params.customPattern[2u] = static_cast<int32_t>(
            std::lround(value));
        break;
    case kCustomStep4ParamId:
        plugin.params.customPattern[3u] = static_cast<int32_t>(
            std::lround(value));
        break;
    case kCustomStep5ParamId:
        plugin.params.customPattern[4u] = static_cast<int32_t>(
            std::lround(value));
        break;
    case kCustomStep6ParamId:
        plugin.params.customPattern[5u] = static_cast<int32_t>(
            std::lround(value));
        break;
    case kCustomStep7ParamId:
        plugin.params.customPattern[6u] = static_cast<int32_t>(
            std::lround(value));
        break;
    case kCustomStep8ParamId:
        plugin.params.customPattern[7u] = static_cast<int32_t>(
            std::lround(value));
        break;
    case kPierceParamId: plugin.params.pierce = normalized; break;
    case kSelfListenParamId: plugin.params.selfListen = normalized; break;
    case kTargetGlitchParamId: plugin.params.targetGlitch = normalized; break;
    case kGlitchRatchetParamId: plugin.params.glitchRatchet = normalized; break;
    case kOverloadMaskParamId: plugin.params.overloadMask = normalized; break;
    case kArpBRelationParamId:
        plugin.params.arpBRelation = static_cast<
            s3g::ProcessorStackArpRelation>(
                static_cast<uint32_t>(std::lround(value)));
        break;
    case kArpPatternBParamId:
        plugin.params.arpPatternB = static_cast<
            s3g::ProcessorStackArpPattern>(
                static_cast<uint32_t>(std::lround(value)));
        break;
    case kScaleBParamId:
        plugin.params.scaleB = static_cast<s3g::ProcessorStackScale>(
            static_cast<uint32_t>(std::lround(value)));
        break;
    case kArpRateBParamId:
        plugin.params.arpRateB = static_cast<s3g::ProcessorStackArpRate>(
            static_cast<uint32_t>(std::lround(value)));
        break;
    case kArpOctavesBParamId:
        plugin.params.arpOctavesB = static_cast<uint32_t>(
            std::lround(value));
        break;
    case kArpGateBParamId: plugin.params.arpGateB = normalized; break;
    case kArpPhaseBParamId: plugin.params.arpPhaseB = normalized; break;
    case kCustomLengthBParamId:
        plugin.params.customPatternLengthB = static_cast<uint32_t>(
            std::lround(value));
        break;
    case kCustomStepB1ParamId:
        plugin.params.customPatternB[0u] = static_cast<int32_t>(
            std::lround(value));
        break;
    case kCustomStepB2ParamId:
        plugin.params.customPatternB[1u] = static_cast<int32_t>(
            std::lround(value));
        break;
    case kCustomStepB3ParamId:
        plugin.params.customPatternB[2u] = static_cast<int32_t>(
            std::lround(value));
        break;
    case kCustomStepB4ParamId:
        plugin.params.customPatternB[3u] = static_cast<int32_t>(
            std::lround(value));
        break;
    case kCustomStepB5ParamId:
        plugin.params.customPatternB[4u] = static_cast<int32_t>(
            std::lround(value));
        break;
    case kCustomStepB6ParamId:
        plugin.params.customPatternB[5u] = static_cast<int32_t>(
            std::lround(value));
        break;
    case kCustomStepB7ParamId:
        plugin.params.customPatternB[6u] = static_cast<int32_t>(
            std::lround(value));
        break;
    case kCustomStepB8ParamId:
        plugin.params.customPatternB[7u] = static_cast<int32_t>(
            std::lround(value));
        break;
    case kLinkPedalParamId: plugin.params.linkPedal = value >= 0.5; break;
    case kLinkAmplifierParamId:
        plugin.params.linkAmplifier = value >= 0.5;
        break;
    case kLinkFeedbackParamId:
        plugin.params.linkFeedback = value >= 0.5;
        break;
    case kCircuitBParamId:
        plugin.params.circuitB = static_cast<s3g::ProcessorStackCircuit>(
            static_cast<uint32_t>(std::lround(value)));
        break;
    case kBiteBParamId: plugin.params.biteB = normalized; break;
    case kPedalToneBParamId: plugin.params.pedalToneB = normalized; break;
    case kBiasBParamId: plugin.params.biasB = normalized; break;
    case kStackBParamId: plugin.params.stackB = normalized; break;
    case kSagBParamId: plugin.params.sagB = normalized; break;
    case kFocusBParamId: plugin.params.focusB = normalized; break;
    case kConeBParamId: plugin.params.coneB = normalized; break;
    case kCabinetBParamId: plugin.params.cabinetB = normalized; break;
    case kMicBParamId: plugin.params.micB = normalized; break;
    case kFeedbackBParamId: plugin.params.feedbackB = normalized; break;
    case kProximityBParamId: plugin.params.proximityB = normalized; break;
    case kHarmonicBParamId: plugin.params.harmonicB = normalized; break;
    case kTrackingBParamId: plugin.params.trackingB = normalized; break;
    case kPolarityBParamId: plugin.params.polarityB = normalized; break;
    case kRootBParamId: plugin.params.rootB = normalized; break;
    case kChaosBParamId: plugin.params.chaosB = normalized; break;
    case kPierceBParamId: plugin.params.pierceB = normalized; break;
    case kSelfListenBParamId: plugin.params.selfListenB = normalized; break;
    case kTargetGlitchBParamId:
        plugin.params.targetGlitchB = normalized;
        break;
    case kGlitchRatchetBParamId:
        plugin.params.glitchRatchetB = normalized;
        break;
    case kOverloadMaskBParamId:
        plugin.params.overloadMaskB = normalized;
        break;
    case kArpHostSyncParamId:
        plugin.params.arpHostSync = value >= 0.5;
        break;
    default: return;
    }
    plugin.engine.setParams(plugin.params);
    plugin.params = plugin.engine.params();
    publishParam(plugin, id, rawParamValue(plugin, id));
    plugin.parameterRevision.fetch_add(1u, std::memory_order_release);
    if (tailChanged) {
        plugin.tailChangePending.store(true, std::memory_order_release);
        if (plugin.host && plugin.host->request_process) {
            plugin.host->request_process(plugin.host);
        }
    }
}

bool validMidiChannel(int channel)
{
    return channel >= 0 && channel < 16;
}

bool noteHeldOnAnotherChannel(
    const Plugin& plugin, int key, int excludedChannel)
{
    key = std::clamp(key, 0, 127);
    for (int channel = 0; channel < 16; ++channel) {
        if (channel != excludedChannel
            && plugin.midiHeldNotes[static_cast<size_t>(channel)]
                [static_cast<size_t>(key)]) return true;
    }
    return false;
}

void applyChannelExpressionToNote(Plugin& plugin, int channel, int key)
{
    if (!validMidiChannel(channel)) return;
    const size_t index = static_cast<size_t>(channel);
    plugin.engine.setNotePressure(key, plugin.midiChannelPressure[index]);
    plugin.engine.setNoteTuningSemitones(key, plugin.midiChannelBend[index]);
    plugin.engine.setNoteTimbre(key, plugin.midiChannelTimbre[index]);
}

void setChannelPressure(Plugin& plugin, int channel, float pressure)
{
    if (!validMidiChannel(channel)) return;
    const size_t channelIndex = static_cast<size_t>(channel);
    pressure = std::clamp(std::isfinite(pressure) ? pressure : 0.0f,
        0.0f, 1.0f);
    plugin.midiChannelPressure[channelIndex] = pressure;
    for (int key = 0; key < 128; ++key) {
        if (plugin.midiHeldNotes[channelIndex][static_cast<size_t>(key)]) {
            plugin.engine.setNotePressure(key, pressure);
        }
    }
}

void setChannelBend(Plugin& plugin, int channel, float semitones)
{
    if (!validMidiChannel(channel)) return;
    const size_t channelIndex = static_cast<size_t>(channel);
    semitones = std::clamp(
        std::isfinite(semitones) ? semitones : 0.0f, -48.0f, 48.0f);
    plugin.midiChannelBend[channelIndex] = semitones;
    for (int key = 0; key < 128; ++key) {
        if (plugin.midiHeldNotes[channelIndex][static_cast<size_t>(key)]) {
            plugin.engine.setNoteTuningSemitones(key, semitones);
        }
    }
}

void setChannelTimbre(Plugin& plugin, int channel, float timbre)
{
    if (!validMidiChannel(channel)) return;
    const size_t channelIndex = static_cast<size_t>(channel);
    timbre = std::clamp(std::isfinite(timbre) ? timbre : 0.5f,
        0.0f, 1.0f);
    plugin.midiChannelTimbre[channelIndex] = timbre;
    for (int key = 0; key < 128; ++key) {
        if (plugin.midiHeldNotes[channelIndex][static_cast<size_t>(key)]) {
            plugin.engine.setNoteTimbre(key, timbre);
        }
    }
}

void noteOn(Plugin& plugin, int key, float velocity, int channel = -1)
{
    if (velocity <= 0.0f) {
        if (validMidiChannel(channel)) {
            plugin.midiHeldNotes[static_cast<size_t>(channel)]
                [static_cast<size_t>(std::clamp(key, 0, 127))] = false;
        }
        if (!noteHeldOnAnotherChannel(plugin, key, channel)) {
            plugin.engine.noteOff(key);
        }
        return;
    }
    plugin.engine.noteOn(key, velocity);
    if (validMidiChannel(channel)) {
        plugin.midiHeldNotes[static_cast<size_t>(channel)]
            [static_cast<size_t>(std::clamp(key, 0, 127))] = true;
        applyChannelExpressionToNote(plugin, channel, key);
    }
    plugin.active = true;
}

void noteOff(Plugin& plugin, int key, int channel = -1)
{
    key = std::clamp(key, 0, 127);
    if (validMidiChannel(channel)) {
        plugin.midiHeldNotes[static_cast<size_t>(channel)]
            [static_cast<size_t>(key)] = false;
    }
    if (!noteHeldOnAnotherChannel(plugin, key, channel)) {
        plugin.engine.noteOff(key);
    }
}

void allNotesOffChannel(Plugin& plugin, int channel)
{
    if (!validMidiChannel(channel)) return;
    auto& held = plugin.midiHeldNotes[static_cast<size_t>(channel)];
    for (int key = 0; key < 128; ++key) {
        if (!held[static_cast<size_t>(key)]) continue;
        held[static_cast<size_t>(key)] = false;
        if (!noteHeldOnAnotherChannel(plugin, key, channel)) {
            plugin.engine.noteOff(key);
        }
    }
}

void allNotesOff(Plugin& plugin)
{
    plugin.engine.allNotesOff();
    plugin.engine.setPressure(0.0f);
    for (auto& channel : plugin.midiHeldNotes) channel.fill(false);
}

void applyEvent(Plugin& plugin, const clap_event_header_t* event)
{
    if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
    if (event->type == CLAP_EVENT_PARAM_VALUE
        && event->size >= sizeof(clap_event_param_value_t)) {
        const auto* param = reinterpret_cast<
            const clap_event_param_value_t*>(event);
        applyParam(plugin, param->param_id, param->value);
        return;
    }
    if ((event->type == CLAP_EVENT_NOTE_ON
            || event->type == CLAP_EVENT_NOTE_OFF
            || event->type == CLAP_EVENT_NOTE_CHOKE)
        && event->size >= sizeof(clap_event_note_t)) {
        const auto* note = reinterpret_cast<const clap_event_note_t*>(event);
        if (!s3g::drum_midi::accepts(plugin.midiReceive, note->channel)) {
            return;
        }
        if (event->type == CLAP_EVENT_NOTE_ON && note->velocity > 0.0) {
            noteOn(plugin, note->key, static_cast<float>(note->velocity),
                note->channel);
        } else if (note->key < 0) {
            allNotesOff(plugin);
        } else {
            noteOff(plugin, note->key, note->channel);
        }
        return;
    }
    if (event->type == CLAP_EVENT_NOTE_EXPRESSION
        && event->size >= sizeof(clap_event_note_expression_t)) {
        const auto* expression = reinterpret_cast<
            const clap_event_note_expression_t*>(event);
        if (!s3g::drum_midi::accepts(
                plugin.midiReceive, expression->channel)) return;
        const int key = expression->key;
        const int channel = expression->channel;
        if (expression->expression_id == CLAP_NOTE_EXPRESSION_PRESSURE) {
            if (key >= 0) {
                plugin.engine.setNotePressure(
                    key, static_cast<float>(expression->value));
            } else if (validMidiChannel(channel)) {
                setChannelPressure(plugin, channel,
                    static_cast<float>(expression->value));
            } else {
                plugin.engine.setPressure(
                    static_cast<float>(expression->value));
            }
        } else if (expression->expression_id == CLAP_NOTE_EXPRESSION_TUNING) {
            if (key >= 0) {
                plugin.engine.setNoteTuningSemitones(
                    key, static_cast<float>(expression->value));
            } else if (validMidiChannel(channel)) {
                setChannelBend(plugin, channel,
                    static_cast<float>(expression->value));
            } else {
                plugin.engine.setPitchBendSemitones(
                    static_cast<float>(expression->value));
            }
        } else if (expression->expression_id
                == CLAP_NOTE_EXPRESSION_BRIGHTNESS) {
            if (key >= 0) {
                plugin.engine.setNoteTimbre(
                    key, static_cast<float>(expression->value));
            } else if (validMidiChannel(channel)) {
                setChannelTimbre(plugin, channel,
                    static_cast<float>(expression->value));
            }
        }
        return;
    }
    if (event->type != CLAP_EVENT_MIDI
        || event->size < sizeof(clap_event_midi_t)) return;
    const auto* midi = reinterpret_cast<const clap_event_midi_t*>(event);
    const uint8_t command = midi->data[0] & 0xf0u;
    const int channel = midi->data[0] & 0x0fu;
    if (!s3g::drum_midi::accepts(plugin.midiReceive, channel)) return;
    const int key = midi->data[1] & 0x7fu;
    if (command == 0x90u && midi->data[2] != 0u) {
        noteOn(plugin, key,
            static_cast<float>(midi->data[2]) / 127.0f, channel);
    } else if (command == 0x80u
        || (command == 0x90u && midi->data[2] == 0u)) {
        noteOff(plugin, key, channel);
    } else if (command == 0xa0u) {
        plugin.engine.setNotePressure(
            key, static_cast<float>(midi->data[2]) / 127.0f);
    } else if (command == 0xd0u) {
        setChannelPressure(plugin, channel,
            static_cast<float>(midi->data[1]) / 127.0f);
    } else if (command == 0xe0u) {
        const int bend = (static_cast<int>(midi->data[2]) << 7)
            | static_cast<int>(midi->data[1]);
        const size_t channelIndex = static_cast<size_t>(channel);
        const float range = static_cast<float>(
            plugin.midiBendRangeSemitones[channelIndex])
            + static_cast<float>(plugin.midiBendRangeCents[channelIndex])
                / 100.0f;
        setChannelBend(plugin, channel,
            static_cast<float>(bend - 8192) * (range / 8192.0f));
    } else if (command == 0xb0u) {
        const uint8_t controller = midi->data[1] & 0x7fu;
        const uint8_t value = midi->data[2] & 0x7fu;
        const size_t channelIndex = static_cast<size_t>(channel);
        if (controller == 74u) {
            setChannelTimbre(plugin, channel,
                static_cast<float>(value) / 127.0f);
        } else if (controller == 101u) {
            plugin.midiRpnMsb[channelIndex] = value;
        } else if (controller == 100u) {
            plugin.midiRpnLsb[channelIndex] = value;
        } else if (controller == 6u
            && plugin.midiRpnMsb[channelIndex] == 0u
            && plugin.midiRpnLsb[channelIndex] == 0u) {
            plugin.midiBendRangeSemitones[channelIndex] =
                std::min<uint8_t>(value, 48u);
        } else if (controller == 38u
            && plugin.midiRpnMsb[channelIndex] == 0u
            && plugin.midiRpnLsb[channelIndex] == 0u) {
            plugin.midiBendRangeCents[channelIndex] =
                std::min<uint8_t>(value, 99u);
        } else if (controller == 120u || controller == 123u) {
            allNotesOffChannel(plugin, channel);
        } else if (controller == 121u) {
            setChannelPressure(plugin, channel, 0.0f);
            setChannelBend(plugin, channel, 0.0f);
            setChannelTimbre(plugin, channel, 0.5f);
        }
    }
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

void requestGuiParamService(Plugin& plugin)
{
    if (plugin.hostParams && plugin.hostParams->request_flush) {
        plugin.hostParams->request_flush(plugin.host);
    } else if (plugin.host && plugin.host->request_process) {
        plugin.host->request_process(plugin.host);
    }
}

bool queueGuiParamEvent(Plugin& plugin,
    s3g::clap_gui::ParamEventKind kind, clap_id id, double value = 0.0)
{
    if (!plugin.guiParamEvents.push({ kind, id, value })) return false;
    requestGuiParamService(plugin);
    return true;
}

void queueGuiParamGestureBegin(Plugin& plugin, clap_id id)
{
    (void)queueGuiParamEvent(plugin,
        s3g::clap_gui::ParamEventKind::GestureBegin, id);
}

void queueGuiParamValue(Plugin& plugin, clap_id id, double value)
{
    if (const auto* def = paramDef(id)) {
        value = clampValue(*def, value);
        publishParam(plugin, id, value);
    }
    (void)queueGuiParamEvent(plugin,
        s3g::clap_gui::ParamEventKind::Value, id, value);
}

void queueGuiParamGestureEnd(Plugin& plugin, clap_id id)
{
    (void)queueGuiParamEvent(plugin,
        s3g::clap_gui::ParamEventKind::GestureEnd, id);
}

void queueGuiParamGesture(Plugin& plugin, clap_id id, double value)
{
    queueGuiParamGestureBegin(plugin, id);
    queueGuiParamValue(plugin, id, value);
    queueGuiParamGestureEnd(plugin, id);
}

std::array<double, kSynthParamCount> paramValues(
    const s3g::ProcessorStackParams& params)
{
    return {{
        static_cast<double>(params.mode), params.shape, params.wire,
        params.pick, params.damping, params.glideMs, params.crooked,
        params.spill, static_cast<double>(params.circuit), params.bite,
        params.pedalTone, params.bias, params.stack, params.sag,
        params.focus, params.cone, params.cabinet, params.mic,
        params.feedback, params.proximity, params.harmonic, params.tracking,
        params.polarity, params.root, params.chaos, params.outputGainDb,
        static_cast<double>(params.arpPattern),
        static_cast<double>(params.scale),
        static_cast<double>(params.arpRate),
        static_cast<double>(params.arpOctaves),
        params.arpGate,
        static_cast<double>(params.customPatternLength),
        static_cast<double>(params.customPattern[0u]),
        static_cast<double>(params.customPattern[1u]),
        static_cast<double>(params.customPattern[2u]),
        static_cast<double>(params.customPattern[3u]),
        static_cast<double>(params.customPattern[4u]),
        static_cast<double>(params.customPattern[5u]),
        static_cast<double>(params.customPattern[6u]),
        static_cast<double>(params.customPattern[7u]),
        params.pierce, params.selfListen,
        params.targetGlitch, params.glitchRatchet,
        params.overloadMask,
        params.attackMs, params.decayMs, params.sustain, params.releaseMs,
        params.pairAmount, static_cast<double>(params.pairRelation),
        params.pairLoose, params.pairSpread,
        static_cast<double>(params.neckA),
        static_cast<double>(params.bodyA),
        static_cast<double>(params.neckB),
        static_cast<double>(params.bodyB),
        static_cast<double>(params.arpBRelation),
        static_cast<double>(params.arpPatternB),
        static_cast<double>(params.scaleB),
        static_cast<double>(params.arpRateB),
        static_cast<double>(params.arpOctavesB), params.arpGateB,
        params.arpPhaseB,
        static_cast<double>(params.customPatternLengthB),
        static_cast<double>(params.customPatternB[0u]),
        static_cast<double>(params.customPatternB[1u]),
        static_cast<double>(params.customPatternB[2u]),
        static_cast<double>(params.customPatternB[3u]),
        static_cast<double>(params.customPatternB[4u]),
        static_cast<double>(params.customPatternB[5u]),
        static_cast<double>(params.customPatternB[6u]),
        static_cast<double>(params.customPatternB[7u]),
        params.linkPedal ? 1.0 : 0.0,
        params.linkAmplifier ? 1.0 : 0.0,
        params.linkFeedback ? 1.0 : 0.0,
        static_cast<double>(params.circuitB), params.biteB,
        params.pedalToneB, params.biasB, params.stackB, params.sagB,
        params.focusB, params.coneB, params.cabinetB, params.micB,
        params.feedbackB, params.proximityB, params.harmonicB,
        params.trackingB, params.polarityB, params.rootB, params.chaosB,
        params.pierceB, params.selfListenB, params.targetGlitchB,
        params.glitchRatchetB, params.overloadMaskB,
        params.rigLevelADb, params.rigLevelBDb,
        params.rigPanA, params.rigPanB,
        params.rigMuteA ? 1.0 : 0.0,
        params.rigMuteB ? 1.0 : 0.0,
        static_cast<double>(params.speaker),
        static_cast<double>(params.speakerB),
    }};
}

bool queueGuiParams(Plugin& plugin,
    const s3g::ProcessorStackParams& params)
{
    using Kind = s3g::clap_gui::ParamEventKind;
    const auto values = paramValues(params);
    std::array<s3g::clap_gui::ParamEvent, kSynthParamCount * 3u> events {};
    std::array<double, kSynthParamCount> clamped {};
    for (uint32_t index = 0u; index < kSynthParamCount; ++index) {
        const clap_id id = kSynthParamIds[index];
        clamped[index] = clampValue(*paramDef(id), values[index]);
        events[index * 3u] = { Kind::GestureBegin, id, 0.0 };
        events[index * 3u + 1u] = { Kind::Value, id, clamped[index] };
        events[index * 3u + 2u] = { Kind::GestureEnd, id, 0.0 };
    }
    if (!plugin.guiParamEvents.pushBatch(events.data(), events.size())) {
        return false;
    }
    for (uint32_t index = 0u; index < kSynthParamCount; ++index) {
        publishParam(plugin, kSynthParamIds[index], clamped[index]);
    }
    requestGuiParamService(plugin);
    return true;
}

bool pushGuiParamEvent(const clap_output_events_t* output,
    const s3g::clap_gui::ParamEvent& pending)
{
    if (!output || !output->try_push) return true;
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

void serviceGuiParamEvents(Plugin& plugin,
    const clap_output_events_t* output)
{
    s3g::clap_gui::ParamEvent pending {};
    while (plugin.guiParamEvents.peek(pending)) {
        if (!pushGuiParamEvent(output, pending)) break;
        if (pending.kind == s3g::clap_gui::ParamEventKind::Value) {
            applyParam(plugin, pending.paramId, pending.value);
        }
        plugin.guiParamEvents.pop();
    }
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
    resetMidiExpressionState(*instance);
    instance->sampleRate = std::clamp(sampleRate, 8000.0, 768000.0);
    instance->engine.prepare(instance->sampleRate);
    instance->engine.setParams(instance->params);
    instance->engine.setPressure(0.0f);
    instance->engine.setPitchBendSemitones(0.0f);
    instance->engine.setTempoBpm(120.0f);
    instance->engine.setHostTransportBeat(0.0, false);
    instance->engine.setScorePlaybackActive(false);
    instance->tempoBpm = 120.0;
    instance->scoreLastGlobalRow = std::numeric_limits<int64_t>::min();
    instance->scoreAppliedRevision = 0u;
    instance->scoreAppliedParameterRevision = 0u;
    instance->scoreGateClosed = true;
    instance->scoreWasRunning = false;
    instance->scorePublishedSection = -1;
    instance->scorePublishedRow = -1;
    instance->scorePublishedSlot = -1;
    instance->scoreVisualRevision.fetch_add(1u, std::memory_order_acq_rel);
    instance->scoreVisualSection.store(-1, std::memory_order_relaxed);
    instance->scoreVisualRow.store(-1, std::memory_order_relaxed);
    instance->scoreVisualSlot.store(-1, std::memory_order_relaxed);
    instance->scoreVisualRevision.fetch_add(1u, std::memory_order_release);
    instance->active = false;
    instance->outputPeak.store(0.0f, std::memory_order_relaxed);
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    resetMidiExpressionState(*instance);
    instance->engine.reset();
    instance->engine.setParams(instance->params);
    instance->engine.setHostTransportBeat(0.0, false);
    instance->engine.setScorePlaybackActive(false);
    instance->tempoBpm = 120.0;
    instance->scoreLastGlobalRow = std::numeric_limits<int64_t>::min();
    instance->scoreAppliedRevision = 0u;
    instance->scoreAppliedParameterRevision = 0u;
    instance->scoreGateClosed = true;
    instance->scoreWasRunning = false;
    instance->scorePublishedSection = -1;
    instance->scorePublishedRow = -1;
    instance->scorePublishedSlot = -1;
    instance->scoreVisualRevision.fetch_add(1u, std::memory_order_acq_rel);
    instance->scoreVisualSection.store(-1, std::memory_order_relaxed);
    instance->scoreVisualRow.store(-1, std::memory_order_relaxed);
    instance->scoreVisualSlot.store(-1, std::memory_order_relaxed);
    instance->scoreVisualRevision.fetch_add(1u, std::memory_order_release);
    instance->active = false;
    instance->outputPeak.store(0.0f, std::memory_order_relaxed);
}

struct ArpTransportClock {
    bool playing = false;
    bool hasPosition = false;
    double beat = 0.0;
    double tempo = 120.0;
    double tempoIncrement = 0.0;
};

void updateArpTransportClock(ArpTransportClock& clock,
    const clap_event_transport_t* transport, double fallbackTempo)
{
    if (!transport) {
        clock = {};
        clock.tempo = fallbackTempo;
        return;
    }
    clock.playing = (transport->flags & CLAP_TRANSPORT_IS_PLAYING) != 0u;
    clock.tempo = (transport->flags & CLAP_TRANSPORT_HAS_TEMPO) != 0u
            && std::isfinite(transport->tempo) && transport->tempo > 0.0
        ? transport->tempo : fallbackTempo;
    clock.tempoIncrement = std::isfinite(transport->tempo_inc)
        ? transport->tempo_inc : 0.0;
    if ((transport->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0u) {
        clock.beat = static_cast<double>(transport->song_pos_beats)
            / static_cast<double>(CLAP_BEATTIME_FACTOR);
        clock.hasPosition = std::isfinite(clock.beat);
    } else if ((transport->flags
            & CLAP_TRANSPORT_HAS_SECONDS_TIMELINE) != 0u) {
        const double seconds = static_cast<double>(
            transport->song_pos_seconds)
            / static_cast<double>(CLAP_SECTIME_FACTOR);
        clock.beat = seconds * clock.tempo / 60.0;
        clock.hasPosition = std::isfinite(clock.beat);
    }
}

void advanceArpTransportClock(ArpTransportClock& clock, double sampleRate)
{
    if (clock.playing && clock.hasPosition) {
        clock.beat += clock.tempo / (60.0 * sampleRate);
    }
    clock.tempo = std::max(1.0, clock.tempo + clock.tempoIncrement);
}

void publishScoreVisual(Plugin& plugin, int32_t section,
    int32_t row, int32_t slot)
{
    if (section == plugin.scorePublishedSection
        && row == plugin.scorePublishedRow
        && slot == plugin.scorePublishedSlot) return;
    plugin.scoreVisualRevision.fetch_add(1u, std::memory_order_acq_rel);
    plugin.scoreVisualSection.store(section, std::memory_order_relaxed);
    plugin.scoreVisualRow.store(row, std::memory_order_relaxed);
    plugin.scoreVisualSlot.store(slot, std::memory_order_relaxed);
    plugin.scorePublishedSection = section;
    plugin.scorePublishedRow = row;
    plugin.scorePublishedSlot = slot;
    plugin.scoreVisualRevision.fetch_add(1u, std::memory_order_release);
}

double scoreRowBeats(s3g::ProcessorStackArpRate rate)
{
    switch (rate) {
    case s3g::ProcessorStackArpRate::Eighth: return 0.5;
    case s3g::ProcessorStackArpRate::EighthTriplet: return 1.0 / 3.0;
    case s3g::ProcessorStackArpRate::Sixteenth: return 0.25;
    case s3g::ProcessorStackArpRate::SixteenthTriplet: return 1.0 / 6.0;
    case s3g::ProcessorStackArpRate::ThirtySecond: return 0.125;
    case s3g::ProcessorStackArpRate::SixtyFourth: return 0.0625;
    case s3g::ProcessorStackArpRate::Quarter: return 1.0;
    case s3g::ProcessorStackArpRate::Half: return 2.0;
    case s3g::ProcessorStackArpRate::Whole: return 4.0;
    case s3g::ProcessorStackArpRate::Count: break;
    }
    return 0.25;
}

void processStackScore(Plugin& plugin, const ArpTransportClock& clock)
{
    const bool running = plugin.scoreEnabled
        && clock.playing && clock.hasPosition;
    if (!running) {
        if (plugin.scoreWasRunning) {
            plugin.engine.setParams(plugin.params);
            plugin.engine.setScorePlaybackActive(false);
            publishScoreVisual(plugin, -1, -1, -1);
        } else {
            plugin.engine.setScorePlaybackActive(false);
        }
        plugin.scoreLastGlobalRow = std::numeric_limits<int64_t>::min();
        plugin.scoreGateClosed = true;
        plugin.scoreWasRunning = false;
        return;
    }
    plugin.engine.setScorePlaybackActive(true);

    const uint64_t revision = plugin.scoreRevision.load(
        std::memory_order_acquire);
    const uint64_t parameterRevision = plugin.parameterRevision.load(
        std::memory_order_acquire);
    const bool programChanged = !plugin.scoreWasRunning
        || revision != plugin.scoreAppliedRevision;
    if (programChanged) {
        plugin.scorePlaybackProgram = scoreProgramSnapshot(plugin);
        plugin.scoreAppliedRevision = revision;
    }
    const auto position = s3g::processorStackScorePosition(
        plugin.scorePlaybackProgram, clock.beat,
        scoreRowBeats(plugin.scoreRate), plugin.scoreLength);
    const bool newRow = !plugin.scoreWasRunning
        || position.globalRow != plugin.scoreLastGlobalRow;
    const bool paramsChanged = !plugin.scoreWasRunning
        || parameterRevision != plugin.scoreAppliedParameterRevision;
    if (newRow || programChanged || paramsChanged) {
        plugin.engine.setParams(s3g::processorStackScoreParamsForRow(
            plugin.scorePlaybackProgram, plugin.params,
            position.section, position.row));
        plugin.scoreAppliedParameterRevision = parameterRevision;
    }
    if (newRow) {
        const bool consecutive = plugin.scoreWasRunning
            && position.globalRow == plugin.scoreLastGlobalRow + 1;
        if (plugin.scoreWasRunning && !consecutive) {
            // A hold never crosses a host seek or discontinuity: it needs the
            // immediately preceding tracker row as its source.
            plugin.engine.scoreReleasePlayer(0u);
            plugin.engine.scoreReleasePlayer(1u);
        }
        std::array<int, s3g::kProcessorStackScoreStringCount> commandsA {};
        std::array<int, s3g::kProcessorStackScoreStringCount> commandsB {};
        const uint32_t countA = s3g::processorStackScoreStringCommands(
            plugin.scorePlaybackProgram,
            position.section, position.row, 0u,
            commandsA.data(), static_cast<uint32_t>(commandsA.size()));
        const uint32_t countB = s3g::processorStackScoreStringCommands(
            plugin.scorePlaybackProgram,
            position.section, position.row, 1u,
            commandsB.data(), static_cast<uint32_t>(commandsB.size()));
        plugin.engine.scorePlayerTabRow(
            0u, commandsA.data(), countA, 0.92f);
        if (plugin.scoreBRelatesToA) {
            plugin.engine.scoreRelatedTabRow(
                commandsA.data(), countA, 0.86f);
        } else {
            plugin.engine.scorePlayerTabRow(
                1u, commandsB.data(), countB, 0.86f);
        }
        plugin.scoreLastGlobalRow = position.globalRow;
        plugin.scoreGateClosed = false;
        plugin.scoreWasRunning = true;
        publishScoreVisual(plugin,
            static_cast<int32_t>(position.section),
            static_cast<int32_t>(position.row),
            static_cast<int32_t>(position.arrangementSlot));
    }
    if (!plugin.scoreGateClosed
        && position.fraction >= static_cast<double>(plugin.scoreGate)) {
        const double rowBeats = scoreRowBeats(plugin.scoreRate);
        const auto next = s3g::processorStackScorePosition(
            plugin.scorePlaybackProgram,
            static_cast<double>(position.globalRow + 1) * rowBeats,
            rowBeats, plugin.scoreLength);
        std::array<int, s3g::kProcessorStackScoreStringCount> nextA {};
        std::array<int, s3g::kProcessorStackScoreStringCount> nextB {};
        const uint32_t nextCountA = s3g::processorStackScoreStringCommands(
            plugin.scorePlaybackProgram, next.section, next.row, 0u,
            nextA.data(), static_cast<uint32_t>(nextA.size()));
        const uint32_t nextCountB = s3g::processorStackScoreStringCommands(
            plugin.scorePlaybackProgram, next.section, next.row, 1u,
            nextB.data(), static_cast<uint32_t>(nextB.size()));
        plugin.engine.scorePrepareNextTabRow(
            0u, nextA.data(), nextCountA);
        if (plugin.scoreBRelatesToA) {
            plugin.engine.scorePrepareNextTabRow(
                1u, nextA.data(), nextCountA);
        } else {
            plugin.engine.scorePrepareNextTabRow(
                1u, nextB.data(), nextCountB);
        }
        plugin.scoreGateClosed = true;
    }
}

clap_process_status process(const clap_plugin_t* plugin,
    const clap_process_t* processInfo)
{
    auto* instance = self(plugin);
    if (!processInfo) return CLAP_PROCESS_ERROR;
    ArpTransportClock transportClock;
    updateArpTransportClock(transportClock,
        processInfo->transport, instance->tempoBpm);
    instance->engine.setTempoBpm(static_cast<float>(transportClock.tempo));
    instance->engine.setHostTransportBeat(transportClock.beat,
        transportClock.playing && transportClock.hasPosition);
    if (instance->tailChangePending.exchange(
            false, std::memory_order_acq_rel)
        && instance->host && instance->hostTail
        && instance->hostTail->changed) {
        instance->hostTail->changed(instance->host);
    }
    serviceGuiParamEvents(*instance, processInfo->out_events);
    const clap_input_events_t* events = processInfo->in_events;
    const uint32_t eventCount = events ? events->size(events) : 0u;
    uint32_t eventIndex = 0u;

    if (processInfo->audio_outputs_count == 0u
        || !processInfo->audio_outputs) {
        instance->engine.setTempoBpm(static_cast<float>(transportClock.tempo));
        instance->engine.setHostTransportBeat(transportClock.beat,
            transportClock.playing && transportClock.hasPosition);
        while (eventIndex < eventCount) {
            const auto* event = events->get(events, eventIndex++);
            if (event && event->space_id == CLAP_CORE_EVENT_SPACE_ID
                && event->type == CLAP_EVENT_TRANSPORT
                && event->size >= sizeof(clap_event_transport_t)) {
                updateArpTransportClock(transportClock,
                    reinterpret_cast<const clap_event_transport_t*>(event),
                    instance->tempoBpm);
                instance->engine.setTempoBpm(
                    static_cast<float>(transportClock.tempo));
                instance->engine.setHostTransportBeat(transportClock.beat,
                    transportClock.playing && transportClock.hasPosition);
            } else {
                applyEvent(*instance, event);
            }
        }
        processStackScore(*instance, transportClock);
        instance->tempoBpm = std::clamp(transportClock.tempo, 20.0, 400.0);
        instance->active = instance->engine.active();
        return instance->active ? CLAP_PROCESS_CONTINUE : CLAP_PROCESS_SLEEP;
    }
    const auto& output = processInfo->audio_outputs[0u];
    if (output.channel_count == 0u || (!output.data32 && !output.data64)) {
        return CLAP_PROCESS_ERROR;
    }

    float blockPeak = 0.0f;
    for (uint32_t sample = 0u; sample < processInfo->frames_count; ++sample) {
        instance->engine.setTempoBpm(static_cast<float>(transportClock.tempo));
        instance->engine.setHostTransportBeat(transportClock.beat,
            transportClock.playing && transportClock.hasPosition);
        while (eventIndex < eventCount) {
            const auto* event = events->get(events, eventIndex);
            if (!event || event->time > sample) break;
            if (event->space_id == CLAP_CORE_EVENT_SPACE_ID
                && event->type == CLAP_EVENT_TRANSPORT
                && event->size >= sizeof(clap_event_transport_t)) {
                updateArpTransportClock(transportClock,
                    reinterpret_cast<const clap_event_transport_t*>(event),
                    instance->tempoBpm);
                instance->engine.setTempoBpm(
                    static_cast<float>(transportClock.tempo));
                instance->engine.setHostTransportBeat(transportClock.beat,
                    transportClock.playing && transportClock.hasPosition);
            } else {
                applyEvent(*instance, event);
            }
            ++eventIndex;
        }
        instance->engine.setTempoBpm(static_cast<float>(transportClock.tempo));
        instance->engine.setHostTransportBeat(transportClock.beat,
            transportClock.playing && transportClock.hasPosition);
        processStackScore(*instance, transportClock);
        float left = 0.0f;
        float right = 0.0f;
        instance->engine.processFrame(left, right);
        blockPeak = std::max(blockPeak,
            std::max(std::abs(left), std::abs(right)));
        for (uint32_t channel = 0u; channel < output.channel_count; ++channel) {
            const float value = channel == 0u ? left
                : (channel == 1u ? right : 0.0f);
            if (output.data32 && output.data32[channel]) {
                output.data32[channel][sample] = value;
            }
            if (output.data64 && output.data64[channel]) {
                output.data64[channel][sample] = value;
            }
        }
        advanceArpTransportClock(transportClock, instance->sampleRate);
    }
    while (eventIndex < eventCount) {
        const auto* event = events->get(events, eventIndex++);
        if (event && event->space_id == CLAP_CORE_EVENT_SPACE_ID
            && event->type == CLAP_EVENT_TRANSPORT
            && event->size >= sizeof(clap_event_transport_t)) {
            updateArpTransportClock(transportClock,
                reinterpret_cast<const clap_event_transport_t*>(event),
                instance->tempoBpm);
        } else {
            applyEvent(*instance, event);
        }
    }
    instance->tempoBpm = std::clamp(transportClock.tempo, 20.0, 400.0);
    instance->active = instance->engine.active();
    const float previous = instance->outputPeak.load(
        std::memory_order_relaxed);
    instance->outputPeak.store(instance->active
            ? std::max(blockPeak, previous * 0.84f) : 0.0f,
        std::memory_order_relaxed);
    return instance->active ? CLAP_PROCESS_CONTINUE : CLAP_PROCESS_SLEEP;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput)
{
    return isInput ? 0u : 1u;
}

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_audio_port_info_t* info)
{
    if (!info || isInput || index != 0u) return false;
    *info = {};
    info->id = 20u;
    std::strncpy(info->name, "Stereo Out", sizeof(info->name) - 1u);
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kOutputChannels;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
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
        | CLAP_NOTE_DIALECT_MIDI
        | CLAP_NOTE_DIALECT_MIDI_MPE;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::strncpy(info->name, "Processor Stack MIDI In",
        sizeof(info->name) - 1u);
    return true;
}

const clap_plugin_note_ports_t notePorts {
    notePortsCount, notePortsGet
};

uint32_t paramsCount(const clap_plugin_t*)
{
    return static_cast<uint32_t>(kParamDefs.size());
}

bool paramsGetInfo(const clap_plugin_t*, uint32_t index,
    clap_param_info_t* info)
{
    if (!info || index >= kParamDefs.size()) return false;
    const auto& def = kParamDefs[index];
    *info = {};
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    if (def.stepped) info->flags |= CLAP_PARAM_IS_STEPPED;
    std::strncpy(info->name, def.name, sizeof(info->name) - 1u);
    std::strncpy(info->module, def.module, sizeof(info->module) - 1u);
    info->min_value = def.minimum;
    info->max_value = def.maximum;
    info->default_value = def.defaultValue;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value || !paramDef(id)) return false;
    *value = paramValue(*self(plugin), id);
    return true;
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value,
    char* display, uint32_t size)
{
    if (!display || size == 0u || !paramDef(id)) return false;
    if (id == kModeParamId) {
        const auto mode = static_cast<s3g::ProcessorStackMode>(
            std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)),
                s3g::kProcessorStackModeCount - 1u));
        std::snprintf(display, size, "%s", s3g::processorStackModeName(mode));
    } else if (id == kCircuitParamId || id == kCircuitBParamId) {
        const auto circuit = static_cast<s3g::ProcessorStackCircuit>(
            std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)),
                s3g::kProcessorStackCircuitCount - 1u));
        std::snprintf(display, size, "%s",
            s3g::processorStackCircuitName(circuit));
    } else if (id == kSpeakerAParamId || id == kSpeakerBParamId) {
        const auto speaker = static_cast<s3g::ProcessorStackSpeakerProfile>(
            std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)),
                s3g::kProcessorStackSpeakerProfileCount - 1u));
        std::snprintf(display, size, "%s",
            s3g::processorStackSpeakerProfileName(speaker));
    } else if (id == kArpBRelationParamId) {
        const auto relation = static_cast<s3g::ProcessorStackArpRelation>(
            std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)),
                s3g::kProcessorStackArpRelationCount - 1u));
        std::snprintf(display, size, "%s",
            s3g::processorStackArpRelationName(relation));
    } else if (id == kArpPatternParamId || id == kArpPatternBParamId) {
        const auto pattern = static_cast<s3g::ProcessorStackArpPattern>(
            std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)),
                s3g::kProcessorStackArpPatternCount - 1u));
        std::snprintf(display, size, "%s",
            s3g::processorStackArpPatternName(pattern));
    } else if (id == kScaleParamId || id == kScaleBParamId) {
        const auto scale = static_cast<s3g::ProcessorStackScale>(
            std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)),
                s3g::kProcessorStackScaleCount - 1u));
        std::snprintf(display, size, "%s",
            s3g::processorStackScaleName(scale));
    } else if (id == kArpRateParamId || id == kArpRateBParamId) {
        const auto rate = static_cast<s3g::ProcessorStackArpRate>(
            std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)),
                s3g::kProcessorStackArpRateCount - 1u));
        std::snprintf(display, size, "%s",
            s3g::processorStackArpRateName(rate));
    } else if (id == kPairRelationParamId) {
        const auto relation = static_cast<s3g::ProcessorStackPairRelation>(
            std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)),
                s3g::kProcessorStackPairRelationCount - 1u));
        std::snprintf(display, size, "%s",
            s3g::processorStackPairRelationName(relation));
    } else if (id == kNeckAParamId || id == kNeckBParamId) {
        const auto material = static_cast<s3g::ProcessorStackNeckMaterial>(
            std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)),
                s3g::kProcessorStackNeckMaterialCount - 1u));
        std::snprintf(display, size, "%s",
            s3g::processorStackNeckMaterialName(material));
    } else if (id == kBodyAParamId || id == kBodyBParamId) {
        const auto material = static_cast<s3g::ProcessorStackBodyMaterial>(
            std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)),
                s3g::kProcessorStackBodyMaterialCount - 1u));
        std::snprintf(display, size, "%s",
            s3g::processorStackBodyMaterialName(material));
    } else if (id == kMidiReceiveParamId) {
        s3g::drum_midi::valueToText(value, display, size);
    } else if (id == kArpHostSyncParamId) {
        std::snprintf(display, size, "%s",
            value >= 0.5 ? "HOST SYNC" : "FREE");
    } else if (id == kScoreEnableParamId) {
        std::snprintf(display, size, "%s",
            value >= 0.5 ? "HOST PLAY" : "OFF");
    } else if (id == kScoreBSourceParamId) {
        std::snprintf(display, size, "%s",
            value >= 0.5 ? "RELATE A" : "EXPLICIT");
    } else if (id == kScoreRateParamId) {
        const auto rate = static_cast<s3g::ProcessorStackArpRate>(
            std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)),
                s3g::kProcessorStackArpRateCount - 1u));
        std::snprintf(display, size, "%s",
            s3g::processorStackArpRateName(rate));
    } else if (id == kLinkPedalParamId || id == kLinkAmplifierParamId
        || id == kLinkFeedbackParamId) {
        std::snprintf(display, size, "%s", value >= 0.5 ? "LINK" : "OWN");
    } else if (id == kGlideParamId || id == kAttackParamId
        || id == kDecayParamId || id == kReleaseParamId) {
        std::snprintf(display, size, "%.1f ms", value);
    } else if (id == kOutputParamId || id == kRigLevelAParamId
        || id == kRigLevelBParamId) {
        std::snprintf(display, size, "%+.1f dB", value);
    } else if (id == kRigPanAParamId || id == kRigPanBParamId) {
        if (std::abs(value) < 0.005) {
            std::snprintf(display, size, "C");
        } else {
            std::snprintf(display, size, "%c %.0f%%",
                value < 0.0 ? 'L' : 'R', std::abs(value) * 100.0);
        }
    } else if (id == kRigMuteAParamId || id == kRigMuteBParamId) {
        std::snprintf(display, size, "%s", value >= 0.5 ? "MUTED" : "LIVE");
    } else if (id == kArpOctavesParamId || id == kCustomLengthParamId
        || id == kArpOctavesBParamId || id == kCustomLengthBParamId) {
        std::snprintf(display, size, "%.0f", value);
    } else if (id == kScoreLengthParamId) {
        std::snprintf(display, size, "%.0f SECTIONS", value);
    } else if ((id >= kCustomStep1ParamId && id <= kCustomStep8ParamId)
        || (id >= kCustomStepB1ParamId && id <= kCustomStepB8ParamId)) {
        if (static_cast<int32_t>(std::lround(value))
                == s3g::kProcessorStackArpRest) {
            std::snprintf(display, size, "REST");
        } else {
            std::snprintf(display, size, "%+.0f", value);
        }
    } else if (id == kPolarityParamId || id == kPolarityBParamId) {
        std::snprintf(display, size, "%+.0f%%", (value - 0.5) * 200.0);
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
    if (id == kMidiReceiveParamId) {
        return s3g::drum_midi::textToValue(display, value);
    }
    if ((id >= kCustomStep1ParamId && id <= kCustomStep8ParamId)
            || (id >= kCustomStepB1ParamId && id <= kCustomStepB8ParamId)) {
        if (std::strcmp(display, "REST") == 0
            || std::strcmp(display, "R") == 0) {
            *value = static_cast<double>(s3g::kProcessorStackArpRest);
            return true;
        }
    }
    if (id == kArpHostSyncParamId) {
        if (std::strcmp(display, "HOST SYNC") == 0) {
            *value = 1.0;
            return true;
        }
        if (std::strcmp(display, "FREE") == 0) {
            *value = 0.0;
            return true;
        }
    }
    if (id == kScoreEnableParamId) {
        if (std::strcmp(display, "HOST PLAY") == 0) {
            *value = 1.0;
            return true;
        }
        if (std::strcmp(display, "OFF") == 0) {
            *value = 0.0;
            return true;
        }
    }
    if (id == kScoreBSourceParamId) {
        if (std::strcmp(display, "RELATE A") == 0) {
            *value = 1.0;
            return true;
        }
        if (std::strcmp(display, "EXPLICIT") == 0) {
            *value = 0.0;
            return true;
        }
    }
    if (id == kLinkPedalParamId || id == kLinkAmplifierParamId
        || id == kLinkFeedbackParamId) {
        if (std::strcmp(display, "LINK") == 0) {
            *value = 1.0;
            return true;
        }
        if (std::strcmp(display, "OWN") == 0) {
            *value = 0.0;
            return true;
        }
    }
    if (id == kRigMuteAParamId || id == kRigMuteBParamId) {
        if (std::strcmp(display, "MUTED") == 0
            || std::strcmp(display, "ON") == 0) {
            *value = 1.0;
            return true;
        }
        if (std::strcmp(display, "LIVE") == 0
            || std::strcmp(display, "OFF") == 0) {
            *value = 0.0;
            return true;
        }
    }
    if (id == kRigPanAParamId || id == kRigPanBParamId) {
        if (std::strcmp(display, "C") == 0) {
            *value = 0.0;
            return true;
        }
        const char direction = display[0];
        if (direction == 'L' || direction == 'R') {
            const char* number = display + 1;
            while (*number != '\0' && std::isspace(
                    static_cast<unsigned char>(*number)) != 0) ++number;
            errno = 0;
            char* end = nullptr;
            const double percent = std::strtod(number, &end);
            if (end != number && errno != ERANGE && std::isfinite(percent)) {
                while (*end != '\0' && std::isspace(
                        static_cast<unsigned char>(*end)) != 0) ++end;
                if (*end == '%') {
                    ++end;
                    while (*end != '\0' && std::isspace(
                            static_cast<unsigned char>(*end)) != 0) ++end;
                    if (*end == '\0') {
                        const double magnitude = std::clamp(
                            percent * 0.01, 0.0, 1.0);
                        *value = direction == 'L' ? -magnitude : magnitude;
                        return true;
                    }
                }
            }
        }
        return false;
    }
    if (id == kModeParamId) {
        for (uint32_t index = 0u; index < s3g::kProcessorStackModeCount;
             ++index) {
            const auto mode = static_cast<s3g::ProcessorStackMode>(index);
            if (std::strcmp(display, s3g::processorStackModeName(mode)) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kCircuitParamId || id == kCircuitBParamId) {
        for (uint32_t index = 0u; index < s3g::kProcessorStackCircuitCount;
             ++index) {
            const auto circuit = static_cast<s3g::ProcessorStackCircuit>(index);
            if (std::strcmp(display,
                    s3g::processorStackCircuitName(circuit)) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kSpeakerAParamId || id == kSpeakerBParamId) {
        for (uint32_t index = 0u;
             index < s3g::kProcessorStackSpeakerProfileCount; ++index) {
            const auto speaker = static_cast<
                s3g::ProcessorStackSpeakerProfile>(index);
            if (std::strcmp(display,
                    s3g::processorStackSpeakerProfileName(speaker)) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kArpBRelationParamId) {
        for (uint32_t index = 0u;
             index < s3g::kProcessorStackArpRelationCount; ++index) {
            const auto relation = static_cast<
                s3g::ProcessorStackArpRelation>(index);
            if (std::strcmp(display,
                    s3g::processorStackArpRelationName(relation)) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kArpPatternParamId || id == kArpPatternBParamId) {
        for (uint32_t index = 0u;
             index < s3g::kProcessorStackArpPatternCount; ++index) {
            const auto pattern = static_cast<
                s3g::ProcessorStackArpPattern>(index);
            if (std::strcmp(display,
                    s3g::processorStackArpPatternName(pattern)) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kScaleParamId || id == kScaleBParamId) {
        for (uint32_t index = 0u;
             index < s3g::kProcessorStackScaleCount; ++index) {
            const auto scale = static_cast<s3g::ProcessorStackScale>(index);
            if (std::strcmp(display, s3g::processorStackScaleName(scale))
                == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kArpRateParamId || id == kArpRateBParamId) {
        for (uint32_t index = 0u;
             index < s3g::kProcessorStackArpRateCount; ++index) {
            const auto rate = static_cast<s3g::ProcessorStackArpRate>(index);
            if (std::strcmp(display, s3g::processorStackArpRateName(rate))
                == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kScoreRateParamId) {
        for (uint32_t index = 0u;
             index < s3g::kProcessorStackArpRateCount; ++index) {
            const auto rate = static_cast<s3g::ProcessorStackArpRate>(index);
            if (std::strcmp(display, s3g::processorStackArpRateName(rate))
                == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kPairRelationParamId) {
        for (uint32_t index = 0u;
             index < s3g::kProcessorStackPairRelationCount; ++index) {
            const auto relation = static_cast<
                s3g::ProcessorStackPairRelation>(index);
            if (std::strcmp(display,
                    s3g::processorStackPairRelationName(relation)) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kNeckAParamId || id == kNeckBParamId) {
        for (uint32_t index = 0u;
             index < s3g::kProcessorStackNeckMaterialCount; ++index) {
            const auto material = static_cast<
                s3g::ProcessorStackNeckMaterial>(index);
            if (std::strcmp(display,
                    s3g::processorStackNeckMaterialName(material)) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kBodyAParamId || id == kBodyBParamId) {
        for (uint32_t index = 0u;
             index < s3g::kProcessorStackBodyMaterialCount; ++index) {
            const auto material = static_cast<
                s3g::ProcessorStackBodyMaterial>(index);
            if (std::strcmp(display,
                    s3g::processorStackBodyMaterialName(material)) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    errno = 0;
    char* end = nullptr;
    const double parsed = std::strtod(display, &end);
    if (end == display || errno == ERANGE || !std::isfinite(parsed)) {
        return false;
    }
    while (*end != '\0' && std::isspace(
            static_cast<unsigned char>(*end)) != 0) ++end;
    const char* suffix = end;
    size_t suffixLength = std::strlen(suffix);
    while (suffixLength > 0u && std::isspace(
            static_cast<unsigned char>(suffix[suffixLength - 1u])) != 0) {
        --suffixLength;
    }
    const auto suffixIs = [suffix, suffixLength](const char* expected) {
        return suffixLength == std::strlen(expected)
            && std::strncmp(suffix, expected, suffixLength) == 0;
    };
    double converted = parsed;
    if (suffixLength > 0u) {
        if ((id == kGlideParamId || id == kAttackParamId
                || id == kDecayParamId || id == kReleaseParamId)
            && suffixIs("ms")) {
        } else if ((id == kOutputParamId || id == kRigLevelAParamId
                || id == kRigLevelBParamId) && suffixIs("dB")) {
        } else if (id == kScoreLengthParamId
            && suffixIs("SECTIONS")) {
        } else if (suffixIs("%") && id != kGlideParamId
            && id != kAttackParamId && id != kDecayParamId
            && id != kReleaseParamId
            && id != kOutputParamId && id != kArpOctavesParamId
            && id != kCustomLengthParamId && id != kArpOctavesBParamId
            && id != kCustomLengthBParamId
            && !(id >= kCustomStep1ParamId
                && id <= kCustomStep8ParamId)
            && !(id >= kCustomStepB1ParamId
                && id <= kCustomStepB8ParamId)) {
            converted = id == kPolarityParamId || id == kPolarityBParamId
                ? converted * 0.005 + 0.5 : converted * 0.01;
        } else {
            return false;
        }
    }
    *value = clampValue(*def, converted);
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
        const auto* param = reinterpret_cast<
            const clap_event_param_value_t*>(event);
        applyParam(*instance, param->param_id, param->value);
    }
    serviceGuiParamEvents(*instance, output);
}

const clap_plugin_params_t paramsExt {
    paramsCount, paramsGetInfo, paramsGetValue,
    paramsValueToText, paramsTextToValue, paramsFlush
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    SavedState state;
    const auto* instance = self(plugin);
    for (uint32_t index = 0u; index < state.values.size(); ++index) {
        state.values[index] = paramValue(*instance, kParamDefs[index].id);
    }
    state.score = scoreProgramSnapshot(*instance);
    return s3g::clap_state::writeAll(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    SavedStateHeader header;
    std::array<double, kParamDefs.size()> values {};
    if (!s3g::clap_state::readAll(stream, &header, sizeof(header))) {
        return false;
    }
    const bool current = header.magic == kStateMagic
        && header.version == kStateVersion
        && header.valueCount == kParamDefs.size();
    const bool versionTwelve = header.magic == kStateMagic
        && header.version == kVersionTwelveStateVersion
        && header.valueCount == kVersionTwelveParamCount;
    const bool versionEleven = header.magic == kStateMagic
        && header.version == kVersionElevenStateVersion
        && header.valueCount == kVersionElevenParamCount;
    const bool versionTen = header.magic == kStateMagic
        && header.version == kVersionTenStateVersion
        && header.valueCount == kVersionTenParamCount;
    const bool versionNine = header.magic == kStateMagic
        && header.version == kVersionNineStateVersion
        && header.valueCount == kVersionNineParamCount;
    const bool versionEight = header.magic == kStateMagic
        && header.version == kVersionEightStateVersion
        && header.valueCount == kVersionEightParamCount;
    const bool versionSeven = header.magic == kStateMagic
        && header.version == kVersionSevenStateVersion
        && header.valueCount == kVersionSevenParamCount;
    const bool versionSix = header.magic == kStateMagic
        && header.version == kVersionSixStateVersion
        && header.valueCount == kVersionSixParamCount;
    const bool versionFive = header.magic == kStateMagic
        && header.version == kVersionFiveStateVersion
        && header.valueCount == kVersionFiveParamCount;
    const bool versionFour = header.magic == kStateMagic
        && header.version == kVersionFourStateVersion
        && header.valueCount == kVersionFourParamCount;
    const bool versionThree = header.magic == kStateMagic
        && header.version == kVersionThreeStateVersion
        && header.valueCount == kVersionThreeParamCount;
    const bool versionTwo = header.magic == kStateMagic
        && header.version == kVersionTwoStateVersion
        && header.valueCount == kVersionTwoParamCount;
    const bool versionOne = header.magic == kStateMagic
        && header.version == kVersionOneStateVersion
        && header.valueCount == kVersionOneParamCount;
    if (!current && !versionTwelve && !versionEleven
        && !versionTen && !versionNine
        && !versionEight && !versionSeven
        && !versionSix && !versionFive
        && !versionFour && !versionThree && !versionTwo && !versionOne) {
        return false;
    }
    if (!s3g::clap_state::readAll(stream, values.data(),
            static_cast<size_t>(header.valueCount) * sizeof(double))) {
        return false;
    }
    s3g::ProcessorStackScoreProgram score =
        s3g::makeDefaultProcessorStackScoreProgram();
    if ((current || versionTwelve || versionEleven)
        && !s3g::clap_state::readAll(
            stream, &score, sizeof(score))) {
        return false;
    }
    if (versionTen) {
        VersionTenScoreProgram previous;
        if (!s3g::clap_state::readAll(
                stream, &previous, sizeof(previous))) return false;
        score.cells = previous.cells;
        score.arrangement = previous.arrangement;
    }
    auto* instance = self(plugin);
    for (uint32_t index = 0u; index < values.size(); ++index) {
        const double value = index < header.valueCount
            ? values[index] : kParamDefs[index].defaultValue;
        applyParam(*instance, kParamDefs[index].id, value);
    }
    storeScoreProgram(*instance, score, false);
    resetMidiExpressionState(*instance);
    instance->engine.reset();
    instance->engine.setParams(instance->params);
    instance->engine.setScorePlaybackActive(false);
    instance->scoreLastGlobalRow = std::numeric_limits<int64_t>::min();
    instance->scoreAppliedRevision = 0u;
    instance->scoreAppliedParameterRevision = 0u;
    instance->scoreWasRunning = false;
    instance->scorePublishedSection = -1;
    instance->scorePublishedRow = -1;
    instance->scorePublishedSlot = -1;
    instance->scoreVisualRevision.fetch_add(1u, std::memory_order_acq_rel);
    instance->scoreVisualSection.store(-1, std::memory_order_relaxed);
    instance->scoreVisualRow.store(-1, std::memory_order_relaxed);
    instance->scoreVisualSlot.store(-1, std::memory_order_relaxed);
    instance->scoreVisualRevision.fetch_add(1u, std::memory_order_release);
    instance->active = false;
    instance->outputPeak.store(0.0f, std::memory_order_relaxed);
    if (instance->host && instance->hostParams
        && instance->hostParams->rescan) {
        instance->hostParams->rescan(
            instance->host, CLAP_PARAM_RESCAN_VALUES);
    }
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

uint32_t tailGet(const clap_plugin_t* plugin)
{
    const auto* instance = self(plugin);
    const double feedbackA = paramValue(*instance, kFeedbackParamId);
    const bool ownFeedbackB = paramValue(*instance, kPairAmountParamId)
            > 1.0e-4
        && paramValue(*instance, kLinkFeedbackParamId) < 0.5;
    const double feedback = ownFeedbackB
        ? std::max(feedbackA, paramValue(*instance, kFeedbackBParamId))
        : feedbackA;
    const double seconds = 0.25
        + paramValue(*instance, kSpillParamId) * 15.0
        + feedback * 1.0
        + paramValue(*instance, kReleaseParamId) * 0.001;
    return static_cast<uint32_t>(std::min<double>(
        std::numeric_limits<uint32_t>::max() - 1u,
        std::ceil(seconds * instance->sampleRate)));
}

const clap_plugin_tail_t tailExt { tailGet };

#if defined(__APPLE__)

constexpr clap_id kFactoryPresetMenuId = 0x7ffffff0u;

struct StackUiRow {
    clap_id id;
    const char* label;
    CGFloat panelX;
    CGFloat panelWidth;
    CGFloat y;
    uint8_t page;
};

struct StackUiPanel {
    const char* name;
    uint8_t page;
    s3g::gui_layout::Panel panel;
};

namespace layout = s3g::gui_layout;

constexpr CGFloat kLeftPanelX = 16.0;
constexpr CGFloat kRightPanelX = 506.0;
constexpr CGFloat kPanelWidth = 458.0;
constexpr CGFloat kMixerColumnGap = 8.0;
constexpr CGFloat kMixerColumnWidth =
    (kPanelWidth - kMixerColumnGap) * 0.5;
constexpr CGFloat kMixerBPanelX =
    kLeftPanelX + kMixerColumnWidth + kMixerColumnGap;
constexpr CGFloat kScoreRelationBPanelX =
    kRightPanelX + kMixerColumnWidth + kMixerColumnGap;
constexpr uint8_t kAllPages = 0xffu;
constexpr CGFloat kContentTop =
    s3g::gui_layout::kStandardMetrics.contentTop;
constexpr CGFloat kPageLeftTop = kContentTop
    + s3g::gui_layout::toolboxHeightForRows(1u)
    + s3g::gui_layout::kStandardMetrics.panelGap;
constexpr CGFloat kPatternPanelHeight = 132.0;
constexpr layout::Canvas kStackCanvas {
    static_cast<double>(kGuiWidth), static_cast<double>(kGuiHeight)
};
constexpr layout::Column kOutputColumn {
    kLeftPanelX, kPanelWidth, kContentTop
};
constexpr layout::Column kLeftColumn {
    kLeftPanelX, kPanelWidth, kPageLeftTop
};
constexpr layout::Column kRightColumn {
    kRightPanelX, kPanelWidth, kContentTop
};

constexpr auto kOutputPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Output,
    kOutputColumn, kContentTop, 1u);
constexpr auto kPlayPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Engine,
    kLeftColumn, kPageLeftTop, 12u);
constexpr auto kPairPanel = layout::fittedStackPanel(
    layout::PanelRole::Engine, kPlayPanel, 7u);
constexpr auto kRoutingPanel = layout::fittedStackPanel(
    layout::PanelRole::Utility, kPairPanel, 2u);
constexpr auto kArpAPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::EventTiming,
    kRightColumn, kContentTop, 6u);
constexpr auto kPatternAPanel = layout::stackPanel(
    layout::PanelRole::EventTiming, kArpAPanel,
    kPatternPanelHeight, 0u);
constexpr auto kArpBPanel = layout::fittedStackPanel(
    layout::PanelRole::EventTiming, kPatternAPanel, 8u);
constexpr auto kPatternBPanel = layout::stackPanel(
    layout::PanelRole::EventTiming, kArpBPanel,
    kPatternPanelHeight, 0u);

constexpr auto kMaterialAPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Engine,
    kLeftColumn, kPageLeftTop, 2u);
constexpr auto kPedalAPanel = layout::fittedStackPanel(
    layout::PanelRole::Projection, kMaterialAPanel, 4u);
constexpr auto kAmplifierAPanel = layout::fittedStackPanel(
    layout::PanelRole::Topology, kPedalAPanel, 7u);
constexpr auto kLoopAPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Output,
    kRightColumn, kContentTop, 12u);

constexpr auto kLinksPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Utility,
    kLeftColumn, kPageLeftTop, 3u);
constexpr auto kMaterialBPanel = layout::fittedStackPanel(
    layout::PanelRole::Engine, kLinksPanel, 2u);
constexpr auto kPedalBPanel = layout::fittedStackPanel(
    layout::PanelRole::Projection, kMaterialBPanel, 4u);
constexpr auto kAmplifierBPanel = layout::fittedStackPanel(
    layout::PanelRole::Topology, kPedalBPanel, 7u);
constexpr auto kLoopBPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Motion,
    kRightColumn, kContentTop, 12u);
constexpr auto kScoreTimingPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::EventTiming,
    kLeftColumn, kPageLeftTop, 4u);
constexpr auto kScoreRelationPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Utility,
    kRightColumn, kContentTop, 4u);

constexpr std::array kOutputPanels { kOutputPanel };
constexpr std::array kPlayLeftPanels {
    kPlayPanel, kPairPanel, kRoutingPanel,
};
constexpr std::array kPlayRightPanels {
    kArpAPanel, kPatternAPanel, kArpBPanel, kPatternBPanel,
};
constexpr std::array kRigALeftPanels {
    kMaterialAPanel, kPedalAPanel, kAmplifierAPanel,
};
constexpr std::array kRigARightPanels { kLoopAPanel };
constexpr std::array kRigBLeftPanels {
    kLinksPanel, kMaterialBPanel, kPedalBPanel, kAmplifierBPanel,
};
constexpr std::array kRigBRightPanels { kLoopBPanel };
constexpr std::array kScoreLeftPanels { kScoreTimingPanel };
constexpr std::array kScoreRightPanels { kScoreRelationPanel };

static_assert(layout::validateColumn(kOutputPanels, kStackCanvas));
static_assert(layout::validateColumn(kPlayLeftPanels, kStackCanvas, false));
static_assert(layout::validateColumn(kPlayRightPanels, kStackCanvas, false));
static_assert(layout::validateColumn(kRigALeftPanels, kStackCanvas, false));
static_assert(layout::validateColumn(kRigARightPanels, kStackCanvas, false));
static_assert(layout::validateColumn(kRigBLeftPanels, kStackCanvas, false));
static_assert(layout::validateColumn(kRigBRightPanels, kStackCanvas, false));
static_assert(layout::validateColumn(kScoreLeftPanels, kStackCanvas, false));
static_assert(layout::validateColumn(kScoreRightPanels, kStackCanvas, false));

constexpr std::array<StackUiPanel, 19u> kUiPanels {{
    { "OUTPUT", kAllPages, kOutputPanel },
    { "PLAY / ENVELOPE", 0u, kPlayPanel },
    { "PAIR / STEREO / OUTPUT MIXER", 0u, kPairPanel },
    { "ROUTING", 0u, kRoutingPanel },
    { "ARPEGGIATOR A", 0u, kArpAPanel },
    { "PATTERN A · RIGHT-CLICK REST", 0u, kPatternAPanel },
    { "ARPEGGIATOR B", 0u, kArpBPanel },
    { "PATTERN B · RIGHT-CLICK REST", 0u, kPatternBPanel },
    { "MATERIAL A", 1u, kMaterialAPanel },
    { "PEDAL A", 1u, kPedalAPanel },
    { "AMPLIFIER / SPEAKER A", 1u, kAmplifierAPanel },
    { "MIC FEEDBACK LOOP A", 1u, kLoopAPanel },
    { "LINKS", 2u, kLinksPanel },
    { "MATERIAL B", 2u, kMaterialBPanel },
    { "PEDAL B", 2u, kPedalBPanel },
    { "AMPLIFIER / SPEAKER B", 2u, kAmplifierBPanel },
    { "MIC FEEDBACK LOOP B", 2u, kLoopBPanel },
    { "SCORE CLOCK / RANDOMIZE", 3u, kScoreTimingPanel },
    { "SCORE SCALES / A-B RELATIONSHIP", 3u, kScoreRelationPanel },
}};

constexpr std::array<StackUiRow, 102u> kUiRows {{
    { kOutputParamId, "OUT", kLeftPanelX, kPanelWidth,
        layout::rowY(kOutputPanel, 0u), kAllPages },
    { kModeParamId, "MODE", kLeftPanelX, kPanelWidth,
        layout::rowY(kPlayPanel, 0u), 0u },
    { kShapeParamId, "SHAPE", kLeftPanelX, kPanelWidth,
        layout::rowY(kPlayPanel, 1u), 0u },
    { kWireParamId, "STRING", kLeftPanelX, kPanelWidth,
        layout::rowY(kPlayPanel, 2u), 0u },
    { kPickParamId, "PICK", kLeftPanelX, kPanelWidth,
        layout::rowY(kPlayPanel, 3u), 0u },
    { kDampingParamId, "DAMP", kLeftPanelX, kPanelWidth,
        layout::rowY(kPlayPanel, 4u), 0u },
    { kGlideParamId, "GLIDE", kLeftPanelX, kPanelWidth,
        layout::rowY(kPlayPanel, 5u), 0u },
    { kCrookedParamId, "CROOKED", kLeftPanelX, kPanelWidth,
        layout::rowY(kPlayPanel, 6u), 0u },
    { kSpillParamId, "SPILL", kLeftPanelX, kPanelWidth,
        layout::rowY(kPlayPanel, 7u), 0u },
    { kAttackParamId, "ATTACK", kLeftPanelX, kPanelWidth,
        layout::rowY(kPlayPanel, 8u), 0u },
    { kDecayParamId, "DECAY", kLeftPanelX, kPanelWidth,
        layout::rowY(kPlayPanel, 9u), 0u },
    { kSustainParamId, "SUSTAIN", kLeftPanelX, kPanelWidth,
        layout::rowY(kPlayPanel, 10u), 0u },
    { kReleaseParamId, "RELEASE", kLeftPanelX, kPanelWidth,
        layout::rowY(kPlayPanel, 11u), 0u },

    { kPairAmountParamId, "DUAL", kLeftPanelX, kPanelWidth,
        layout::rowY(kPairPanel, 0u), 0u },
    { kPairRelationParamId, "RELATION", kLeftPanelX, kPanelWidth,
        layout::rowY(kPairPanel, 1u), 0u },
    { kPairLooseParamId, "LOOSE", kLeftPanelX, kPanelWidth,
        layout::rowY(kPairPanel, 2u), 0u },
    { kPairSpreadParamId, "SPREAD", kLeftPanelX, kPanelWidth,
        layout::rowY(kPairPanel, 3u), 0u },
    { kRigLevelAParamId, "A LEVEL", kLeftPanelX, kMixerColumnWidth,
        layout::rowY(kPairPanel, 4u), 0u },
    { kRigLevelBParamId, "B LEVEL", kMixerBPanelX, kMixerColumnWidth,
        layout::rowY(kPairPanel, 4u), 0u },
    { kRigPanAParamId, "A PAN", kLeftPanelX, kMixerColumnWidth,
        layout::rowY(kPairPanel, 5u), 0u },
    { kRigPanBParamId, "B PAN", kMixerBPanelX, kMixerColumnWidth,
        layout::rowY(kPairPanel, 5u), 0u },
    { kRigMuteAParamId, "A MUTE", kLeftPanelX, kMixerColumnWidth,
        layout::rowY(kPairPanel, 6u), 0u },
    { kRigMuteBParamId, "B MUTE", kMixerBPanelX, kMixerColumnWidth,
        layout::rowY(kPairPanel, 6u), 0u },
    { kMidiReceiveParamId, "MIDI RECEIVE", kLeftPanelX, kPanelWidth,
        layout::rowY(kRoutingPanel, 0u), 0u },
    { kArpHostSyncParamId, "ARP SYNC", kLeftPanelX, kPanelWidth,
        layout::rowY(kRoutingPanel, 1u), 0u },

    { kArpPatternParamId, "PATTERN", kRightPanelX, kPanelWidth,
        layout::rowY(kArpAPanel, 0u), 0u },
    { kScaleParamId, "SCALE RULE", kRightPanelX, kPanelWidth,
        layout::rowY(kArpAPanel, 1u), 0u },
    { kArpRateParamId, "RATE", kRightPanelX, kPanelWidth,
        layout::rowY(kArpAPanel, 2u), 0u },
    { kArpOctavesParamId, "OCTAVES", kRightPanelX, kPanelWidth,
        layout::rowY(kArpAPanel, 3u), 0u },
    { kArpGateParamId, "GATE", kRightPanelX, kPanelWidth,
        layout::rowY(kArpAPanel, 4u), 0u },
    { kCustomLengthParamId, "LENGTH", kRightPanelX, kPanelWidth,
        layout::rowY(kArpAPanel, 5u), 0u },
    { kArpBRelationParamId, "RELATION", kRightPanelX, kPanelWidth,
        layout::rowY(kArpBPanel, 0u), 0u },
    { kArpPatternBParamId, "PATTERN", kRightPanelX, kPanelWidth,
        layout::rowY(kArpBPanel, 1u), 0u },
    { kScaleBParamId, "SCALE RULE", kRightPanelX, kPanelWidth,
        layout::rowY(kArpBPanel, 2u), 0u },
    { kArpRateBParamId, "RATE", kRightPanelX, kPanelWidth,
        layout::rowY(kArpBPanel, 3u), 0u },
    { kArpOctavesBParamId, "OCTAVES", kRightPanelX, kPanelWidth,
        layout::rowY(kArpBPanel, 4u), 0u },
    { kArpGateBParamId, "GATE", kRightPanelX, kPanelWidth,
        layout::rowY(kArpBPanel, 5u), 0u },
    { kArpPhaseBParamId, "PHASE", kRightPanelX, kPanelWidth,
        layout::rowY(kArpBPanel, 6u), 0u },
    { kCustomLengthBParamId, "LENGTH", kRightPanelX, kPanelWidth,
        layout::rowY(kArpBPanel, 7u), 0u },
    { kNeckAParamId, "NECK", kLeftPanelX, kPanelWidth,
        layout::rowY(kMaterialAPanel, 0u), 1u },
    { kBodyAParamId, "BODY", kLeftPanelX, kPanelWidth,
        layout::rowY(kMaterialAPanel, 1u), 1u },
    { kCircuitParamId, "CIRCUIT", kLeftPanelX, kPanelWidth,
        layout::rowY(kPedalAPanel, 0u), 1u },
    { kBiteParamId, "BITE", kLeftPanelX, kPanelWidth,
        layout::rowY(kPedalAPanel, 1u), 1u },
    { kPedalToneParamId, "TONE", kLeftPanelX, kPanelWidth,
        layout::rowY(kPedalAPanel, 2u), 1u },
    { kBiasParamId, "BIAS", kLeftPanelX, kPanelWidth,
        layout::rowY(kPedalAPanel, 3u), 1u },
    { kStackParamId, "STACK", kLeftPanelX, kPanelWidth,
        layout::rowY(kAmplifierAPanel, 0u), 1u },
    { kSagParamId, "SAG", kLeftPanelX, kPanelWidth,
        layout::rowY(kAmplifierAPanel, 1u), 1u },
    { kFocusParamId, "FOCUS", kLeftPanelX, kPanelWidth,
        layout::rowY(kAmplifierAPanel, 2u), 1u },
    { kSpeakerAParamId, "SPEAKER", kLeftPanelX, kPanelWidth,
        layout::rowY(kAmplifierAPanel, 3u), 1u },
    { kConeParamId, "CONE", kLeftPanelX, kPanelWidth,
        layout::rowY(kAmplifierAPanel, 4u), 1u },
    { kCabinetParamId, "CAB", kLeftPanelX, kPanelWidth,
        layout::rowY(kAmplifierAPanel, 5u), 1u },
    { kMicParamId, "MIC", kLeftPanelX, kPanelWidth,
        layout::rowY(kAmplifierAPanel, 6u), 1u },
    { kFeedbackParamId, "FEEDBACK", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopAPanel, 0u), 1u },
    { kProximityParamId, "PROXIMITY", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopAPanel, 1u), 1u },
    { kHarmonicParamId, "HARMONIC", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopAPanel, 2u), 1u },
    { kTrackingParamId, "TRACK", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopAPanel, 3u), 1u },
    { kPolarityParamId, "POLARITY", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopAPanel, 4u), 1u },
    { kRootParamId, "ROOT", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopAPanel, 5u), 1u },
    { kChaosParamId, "CHAOS", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopAPanel, 6u), 1u },
    { kPierceParamId, "PIERCE", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopAPanel, 7u), 1u },
    { kSelfListenParamId, "SELF LISTEN", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopAPanel, 8u), 1u },
    { kTargetGlitchParamId, "TARGET GLITCH", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopAPanel, 9u), 1u },
    { kGlitchRatchetParamId, "RATCHET", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopAPanel, 10u), 1u },
    { kOverloadMaskParamId, "OVERLOAD MASK", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopAPanel, 11u), 1u },

    { kLinkPedalParamId, "PEDALS", kLeftPanelX, kPanelWidth,
        layout::rowY(kLinksPanel, 0u), 2u },
    { kLinkAmplifierParamId, "AMPLIFIERS", kLeftPanelX, kPanelWidth,
        layout::rowY(kLinksPanel, 1u), 2u },
    { kLinkFeedbackParamId, "FEEDBACK", kLeftPanelX, kPanelWidth,
        layout::rowY(kLinksPanel, 2u), 2u },
    { kNeckBParamId, "NECK", kLeftPanelX, kPanelWidth,
        layout::rowY(kMaterialBPanel, 0u), 2u },
    { kBodyBParamId, "BODY", kLeftPanelX, kPanelWidth,
        layout::rowY(kMaterialBPanel, 1u), 2u },
    { kCircuitBParamId, "CIRCUIT", kLeftPanelX, kPanelWidth,
        layout::rowY(kPedalBPanel, 0u), 2u },
    { kBiteBParamId, "BITE", kLeftPanelX, kPanelWidth,
        layout::rowY(kPedalBPanel, 1u), 2u },
    { kPedalToneBParamId, "TONE", kLeftPanelX, kPanelWidth,
        layout::rowY(kPedalBPanel, 2u), 2u },
    { kBiasBParamId, "BIAS", kLeftPanelX, kPanelWidth,
        layout::rowY(kPedalBPanel, 3u), 2u },
    { kStackBParamId, "STACK", kLeftPanelX, kPanelWidth,
        layout::rowY(kAmplifierBPanel, 0u), 2u },
    { kSagBParamId, "SAG", kLeftPanelX, kPanelWidth,
        layout::rowY(kAmplifierBPanel, 1u), 2u },
    { kFocusBParamId, "FOCUS", kLeftPanelX, kPanelWidth,
        layout::rowY(kAmplifierBPanel, 2u), 2u },
    { kSpeakerBParamId, "SPEAKER", kLeftPanelX, kPanelWidth,
        layout::rowY(kAmplifierBPanel, 3u), 2u },
    { kConeBParamId, "CONE", kLeftPanelX, kPanelWidth,
        layout::rowY(kAmplifierBPanel, 4u), 2u },
    { kCabinetBParamId, "CAB", kLeftPanelX, kPanelWidth,
        layout::rowY(kAmplifierBPanel, 5u), 2u },
    { kMicBParamId, "MIC", kLeftPanelX, kPanelWidth,
        layout::rowY(kAmplifierBPanel, 6u), 2u },
    { kFeedbackBParamId, "FEEDBACK", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopBPanel, 0u), 2u },
    { kProximityBParamId, "PROXIMITY", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopBPanel, 1u), 2u },
    { kHarmonicBParamId, "HARMONIC", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopBPanel, 2u), 2u },
    { kTrackingBParamId, "TRACK", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopBPanel, 3u), 2u },
    { kPolarityBParamId, "POLARITY", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopBPanel, 4u), 2u },
    { kRootBParamId, "ROOT", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopBPanel, 5u), 2u },
    { kChaosBParamId, "CHAOS", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopBPanel, 6u), 2u },
    { kPierceBParamId, "PIERCE", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopBPanel, 7u), 2u },
    { kSelfListenBParamId, "SELF LISTEN", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopBPanel, 8u), 2u },
    { kTargetGlitchBParamId, "TARGET GLITCH", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopBPanel, 9u), 2u },
    { kGlitchRatchetBParamId, "RATCHET", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopBPanel, 10u), 2u },
    { kOverloadMaskBParamId, "OVERLOAD MASK", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopBPanel, 11u), 2u },
    { kScoreEnableParamId, "PLAYBACK", kLeftPanelX, kPanelWidth,
        layout::rowY(kScoreTimingPanel, 0u), 3u },
    { kScoreRateParamId, "ROW RATE", kLeftPanelX, kPanelWidth,
        layout::rowY(kScoreTimingPanel, 1u), 3u },
    { kScoreGateParamId, "ROW GATE", kLeftPanelX, kPanelWidth,
        layout::rowY(kScoreTimingPanel, 2u), 3u },
    { kScoreLengthParamId, "ARRANGE LENGTH", kLeftPanelX, kPanelWidth,
        layout::rowY(kScoreTimingPanel, 3u), 3u },
    { kScaleParamId, "SCALE A", kRightPanelX, kPanelWidth,
        layout::rowY(kScoreRelationPanel, 0u), 3u },
    { kScaleBParamId, "SCALE B", kRightPanelX, kPanelWidth,
        layout::rowY(kScoreRelationPanel, 1u), 3u },
    { kScoreBSourceParamId, "B SOURCE", kRightPanelX, kMixerColumnWidth,
        layout::rowY(kScoreRelationPanel, 2u), 3u },
    { kPairRelationParamId, "RELATION", kScoreRelationBPanelX,
        kMixerColumnWidth, layout::rowY(kScoreRelationPanel, 2u), 3u },
    { kPairAmountParamId, "DUAL", kRightPanelX, kMixerColumnWidth,
        layout::rowY(kScoreRelationPanel, 3u), 3u },
    { kPairSpreadParamId, "SPREAD", kScoreRelationBPanelX,
        kMixerColumnWidth, layout::rowY(kScoreRelationPanel, 3u), 3u },
}};

bool isUiMenuParam(clap_id id)
{
    return id == kModeParamId || id == kCircuitParamId
        || id == kCircuitBParamId
        || id == kSpeakerAParamId || id == kSpeakerBParamId
        || id == kMidiReceiveParamId || id == kArpHostSyncParamId
        || id == kScoreEnableParamId || id == kScoreRateParamId
        || id == kScoreLengthParamId || id == kScoreBSourceParamId
        || id == kArpPatternParamId
        || id == kArpPatternBParamId || id == kArpBRelationParamId
        || id == kScaleParamId || id == kScaleBParamId
        || id == kArpRateParamId || id == kArpRateBParamId
        || id == kArpOctavesParamId || id == kCustomLengthParamId
        || id == kArpOctavesBParamId || id == kCustomLengthBParamId
        || id == kPairRelationParamId
        || id == kNeckAParamId || id == kNeckBParamId
        || id == kBodyAParamId || id == kBodyBParamId
        || id == kLinkPedalParamId || id == kLinkAmplifierParamId
        || id == kLinkFeedbackParamId;
}

bool isUiToggleParam(clap_id id)
{
    return id == kRigMuteAParamId || id == kRigMuteBParamId;
}

uint32_t uiMenuItemCount(clap_id id)
{
    if (id == kModeParamId) return s3g::kProcessorStackModeCount;
    if (id == kCircuitParamId || id == kCircuitBParamId)
        return s3g::kProcessorStackCircuitCount;
    if (id == kSpeakerAParamId || id == kSpeakerBParamId)
        return s3g::kProcessorStackSpeakerProfileCount;
    if (id == kMidiReceiveParamId) return 17u;
    if (id == kArpHostSyncParamId) return 2u;
    if (id == kScoreEnableParamId || id == kScoreBSourceParamId) return 2u;
    if (id == kScoreRateParamId) return s3g::kProcessorStackArpRateCount;
    if (id == kScoreLengthParamId)
        return s3g::kProcessorStackScoreArrangementSlots;
    if (id == kArpBRelationParamId)
        return s3g::kProcessorStackArpRelationCount;
    if (id == kArpPatternParamId || id == kArpPatternBParamId)
        return s3g::kProcessorStackArpPatternCount;
    if (id == kScaleParamId || id == kScaleBParamId)
        return s3g::kProcessorStackScaleCount;
    if (id == kArpRateParamId || id == kArpRateBParamId)
        return s3g::kProcessorStackArpRateCount;
    if (id == kArpOctavesParamId || id == kArpOctavesBParamId) return 4u;
    if (id == kCustomLengthParamId || id == kCustomLengthBParamId) return 8u;
    if (id == kPairRelationParamId)
        return s3g::kProcessorStackPairRelationCount;
    if (id == kNeckAParamId || id == kNeckBParamId)
        return s3g::kProcessorStackNeckMaterialCount;
    if (id == kBodyAParamId || id == kBodyBParamId)
        return s3g::kProcessorStackBodyMaterialCount;
    if (id == kLinkPedalParamId || id == kLinkAmplifierParamId
        || id == kLinkFeedbackParamId) return 2u;
    return 0u;
}

bool isLogUiParam(clap_id id)
{
    return id == kGlideParamId || id == kAttackParamId
        || id == kDecayParamId || id == kReleaseParamId;
}

double uiNormalizedValue(clap_id id, double value)
{
    const auto* def = paramDef(id);
    if (!def) return 0.0;
    if (isLogUiParam(id)) {
        return std::clamp(std::log1p(value - def->minimum)
            / std::log1p(def->maximum - def->minimum), 0.0, 1.0);
    }
    return std::clamp((value - def->minimum)
        / std::max(1.0e-12, def->maximum - def->minimum), 0.0, 1.0);
}

double uiValueFromNormalized(clap_id id, double normalized)
{
    const auto* def = paramDef(id);
    if (!def) return 0.0;
    normalized = std::clamp(normalized, 0.0, 1.0);
    const double value = isLogUiParam(id)
        ? def->minimum + std::expm1(
            std::log1p(def->maximum - def->minimum) * normalized)
        : def->minimum + (def->maximum - def->minimum) * normalized;
    return clampValue(*def, value);
}

s3g::ProcessorStackParams publishedParamsSnapshot(const Plugin& plugin)
{
    s3g::ProcessorStackParams params;
    params.mode = static_cast<s3g::ProcessorStackMode>(
        static_cast<uint32_t>(std::lround(paramValue(plugin, kModeParamId))));
    params.shape = static_cast<float>(paramValue(plugin, kShapeParamId));
    params.wire = static_cast<float>(paramValue(plugin, kWireParamId));
    params.pick = static_cast<float>(paramValue(plugin, kPickParamId));
    params.damping = static_cast<float>(paramValue(plugin, kDampingParamId));
    params.glideMs = static_cast<float>(paramValue(plugin, kGlideParamId));
    params.crooked = static_cast<float>(paramValue(plugin, kCrookedParamId));
    params.spill = static_cast<float>(paramValue(plugin, kSpillParamId));
    params.attackMs = static_cast<float>(paramValue(plugin, kAttackParamId));
    params.decayMs = static_cast<float>(paramValue(plugin, kDecayParamId));
    params.sustain = static_cast<float>(paramValue(plugin, kSustainParamId));
    params.releaseMs = static_cast<float>(paramValue(plugin, kReleaseParamId));
    params.pairAmount = static_cast<float>(
        paramValue(plugin, kPairAmountParamId));
    params.pairRelation = static_cast<s3g::ProcessorStackPairRelation>(
        static_cast<uint32_t>(std::lround(
            paramValue(plugin, kPairRelationParamId))));
    params.pairLoose = static_cast<float>(
        paramValue(plugin, kPairLooseParamId));
    params.pairSpread = static_cast<float>(
        paramValue(plugin, kPairSpreadParamId));
    params.rigLevelADb = static_cast<float>(
        paramValue(plugin, kRigLevelAParamId));
    params.rigLevelBDb = static_cast<float>(
        paramValue(plugin, kRigLevelBParamId));
    params.rigPanA = static_cast<float>(
        paramValue(plugin, kRigPanAParamId));
    params.rigPanB = static_cast<float>(
        paramValue(plugin, kRigPanBParamId));
    params.rigMuteA = paramValue(plugin, kRigMuteAParamId) >= 0.5;
    params.rigMuteB = paramValue(plugin, kRigMuteBParamId) >= 0.5;
    params.speaker = static_cast<s3g::ProcessorStackSpeakerProfile>(
        static_cast<uint32_t>(std::lround(
            paramValue(plugin, kSpeakerAParamId))));
    params.speakerB = static_cast<s3g::ProcessorStackSpeakerProfile>(
        static_cast<uint32_t>(std::lround(
            paramValue(plugin, kSpeakerBParamId))));
    params.neckA = static_cast<s3g::ProcessorStackNeckMaterial>(
        static_cast<uint32_t>(std::lround(paramValue(plugin, kNeckAParamId))));
    params.bodyA = static_cast<s3g::ProcessorStackBodyMaterial>(
        static_cast<uint32_t>(std::lround(paramValue(plugin, kBodyAParamId))));
    params.neckB = static_cast<s3g::ProcessorStackNeckMaterial>(
        static_cast<uint32_t>(std::lround(paramValue(plugin, kNeckBParamId))));
    params.bodyB = static_cast<s3g::ProcessorStackBodyMaterial>(
        static_cast<uint32_t>(std::lround(paramValue(plugin, kBodyBParamId))));
    params.arpHostSync = paramValue(plugin, kArpHostSyncParamId) >= 0.5;
    params.arpPattern = static_cast<s3g::ProcessorStackArpPattern>(
        static_cast<uint32_t>(std::lround(
            paramValue(plugin, kArpPatternParamId))));
    params.scale = static_cast<s3g::ProcessorStackScale>(
        static_cast<uint32_t>(std::lround(paramValue(plugin, kScaleParamId))));
    params.arpRate = static_cast<s3g::ProcessorStackArpRate>(
        static_cast<uint32_t>(std::lround(
            paramValue(plugin, kArpRateParamId))));
    params.arpOctaves = static_cast<uint32_t>(std::lround(
        paramValue(plugin, kArpOctavesParamId)));
    params.arpGate = static_cast<float>(paramValue(plugin, kArpGateParamId));
    params.customPatternLength = static_cast<uint32_t>(std::lround(
        paramValue(plugin, kCustomLengthParamId)));
    for (uint32_t index = 0u; index < params.customPattern.size(); ++index) {
        params.customPattern[index] = static_cast<int32_t>(std::lround(
            paramValue(plugin, kCustomStep1ParamId + index)));
    }
    params.pierce = static_cast<float>(paramValue(plugin, kPierceParamId));
    params.selfListen = static_cast<float>(
        paramValue(plugin, kSelfListenParamId));
    params.targetGlitch = static_cast<float>(
        paramValue(plugin, kTargetGlitchParamId));
    params.glitchRatchet = static_cast<float>(
        paramValue(plugin, kGlitchRatchetParamId));
    params.overloadMask = static_cast<float>(
        paramValue(plugin, kOverloadMaskParamId));
    params.circuit = static_cast<s3g::ProcessorStackCircuit>(
        static_cast<uint32_t>(std::lround(
            paramValue(plugin, kCircuitParamId))));
    params.bite = static_cast<float>(paramValue(plugin, kBiteParamId));
    params.pedalTone = static_cast<float>(
        paramValue(plugin, kPedalToneParamId));
    params.bias = static_cast<float>(paramValue(plugin, kBiasParamId));
    params.stack = static_cast<float>(paramValue(plugin, kStackParamId));
    params.sag = static_cast<float>(paramValue(plugin, kSagParamId));
    params.focus = static_cast<float>(paramValue(plugin, kFocusParamId));
    params.cone = static_cast<float>(paramValue(plugin, kConeParamId));
    params.cabinet = static_cast<float>(paramValue(plugin, kCabinetParamId));
    params.mic = static_cast<float>(paramValue(plugin, kMicParamId));
    params.feedback = static_cast<float>(
        paramValue(plugin, kFeedbackParamId));
    params.proximity = static_cast<float>(
        paramValue(plugin, kProximityParamId));
    params.harmonic = static_cast<float>(
        paramValue(plugin, kHarmonicParamId));
    params.tracking = static_cast<float>(paramValue(plugin, kTrackingParamId));
    params.polarity = static_cast<float>(paramValue(plugin, kPolarityParamId));
    params.root = static_cast<float>(paramValue(plugin, kRootParamId));
    params.chaos = static_cast<float>(paramValue(plugin, kChaosParamId));
    params.arpBRelation = static_cast<s3g::ProcessorStackArpRelation>(
        static_cast<uint32_t>(std::lround(
            paramValue(plugin, kArpBRelationParamId))));
    params.arpPatternB = static_cast<s3g::ProcessorStackArpPattern>(
        static_cast<uint32_t>(std::lround(
            paramValue(plugin, kArpPatternBParamId))));
    params.scaleB = static_cast<s3g::ProcessorStackScale>(
        static_cast<uint32_t>(std::lround(
            paramValue(plugin, kScaleBParamId))));
    params.arpRateB = static_cast<s3g::ProcessorStackArpRate>(
        static_cast<uint32_t>(std::lround(
            paramValue(plugin, kArpRateBParamId))));
    params.arpOctavesB = static_cast<uint32_t>(std::lround(
        paramValue(plugin, kArpOctavesBParamId)));
    params.arpGateB = static_cast<float>(
        paramValue(plugin, kArpGateBParamId));
    params.arpPhaseB = static_cast<float>(
        paramValue(plugin, kArpPhaseBParamId));
    params.customPatternLengthB = static_cast<uint32_t>(std::lround(
        paramValue(plugin, kCustomLengthBParamId)));
    for (uint32_t index = 0u; index < params.customPatternB.size(); ++index) {
        params.customPatternB[index] = static_cast<int32_t>(std::lround(
            paramValue(plugin, kCustomStepB1ParamId + index)));
    }
    params.linkPedal = paramValue(plugin, kLinkPedalParamId) >= 0.5;
    params.linkAmplifier = paramValue(plugin, kLinkAmplifierParamId) >= 0.5;
    params.linkFeedback = paramValue(plugin, kLinkFeedbackParamId) >= 0.5;
    params.circuitB = static_cast<s3g::ProcessorStackCircuit>(
        static_cast<uint32_t>(std::lround(
            paramValue(plugin, kCircuitBParamId))));
    params.biteB = static_cast<float>(paramValue(plugin, kBiteBParamId));
    params.pedalToneB = static_cast<float>(
        paramValue(plugin, kPedalToneBParamId));
    params.biasB = static_cast<float>(paramValue(plugin, kBiasBParamId));
    params.stackB = static_cast<float>(paramValue(plugin, kStackBParamId));
    params.sagB = static_cast<float>(paramValue(plugin, kSagBParamId));
    params.focusB = static_cast<float>(paramValue(plugin, kFocusBParamId));
    params.coneB = static_cast<float>(paramValue(plugin, kConeBParamId));
    params.cabinetB = static_cast<float>(
        paramValue(plugin, kCabinetBParamId));
    params.micB = static_cast<float>(paramValue(plugin, kMicBParamId));
    params.feedbackB = static_cast<float>(
        paramValue(plugin, kFeedbackBParamId));
    params.proximityB = static_cast<float>(
        paramValue(plugin, kProximityBParamId));
    params.harmonicB = static_cast<float>(
        paramValue(plugin, kHarmonicBParamId));
    params.trackingB = static_cast<float>(
        paramValue(plugin, kTrackingBParamId));
    params.polarityB = static_cast<float>(
        paramValue(plugin, kPolarityBParamId));
    params.rootB = static_cast<float>(paramValue(plugin, kRootBParamId));
    params.chaosB = static_cast<float>(paramValue(plugin, kChaosBParamId));
    params.pierceB = static_cast<float>(paramValue(plugin, kPierceBParamId));
    params.selfListenB = static_cast<float>(
        paramValue(plugin, kSelfListenBParamId));
    params.targetGlitchB = static_cast<float>(
        paramValue(plugin, kTargetGlitchBParamId));
    params.glitchRatchetB = static_cast<float>(
        paramValue(plugin, kGlitchRatchetBParamId));
    params.overloadMaskB = static_cast<float>(
        paramValue(plugin, kOverloadMaskBParamId));
    params.outputGainDb = static_cast<float>(
        paramValue(plugin, kOutputParamId));
    return s3g::sanitizeProcessorStackParams(params);
}

float randomUnit(uint32_t& state)
{
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return static_cast<float>(state & 0x00ffffffu) / 16777215.0f;
}

s3g::ProcessorStackParams safeRandomParams(
    const Plugin& plugin, uint32_t seed)
{
    auto params = publishedParamsSnapshot(plugin);
    // Preserve the performer's audition level and established two-guitar frame.
    // RANDOM varies what the rigs play and how they sound, not their pairing.
    params.mode = static_cast<s3g::ProcessorStackMode>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed) * 3.0f), 2u));
    params.shape = randomUnit(seed);
    params.wire = 0.34f + randomUnit(seed) * 0.64f;
    params.pick = 0.34f + randomUnit(seed) * 0.66f;
    params.damping = 0.18f + randomUnit(seed) * 0.68f;
    params.glideMs = randomUnit(seed) * randomUnit(seed) * 280.0f;
    params.crooked = randomUnit(seed) * 0.94f;
    params.spill = randomUnit(seed) * 0.78f;
    params.attackMs = randomUnit(seed) * randomUnit(seed) * 620.0f;
    params.decayMs = 24.0f + randomUnit(seed) * randomUnit(seed) * 2800.0f;
    params.sustain = 0.04f + randomUnit(seed) * 0.94f;
    params.releaseMs = 18.0f
        + randomUnit(seed) * randomUnit(seed) * 5200.0f;
    params.neckA = static_cast<s3g::ProcessorStackNeckMaterial>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackNeckMaterialCount)),
            s3g::kProcessorStackNeckMaterialCount - 1u));
    params.bodyA = static_cast<s3g::ProcessorStackBodyMaterial>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackBodyMaterialCount)),
            s3g::kProcessorStackBodyMaterialCount - 1u));
    params.neckB = static_cast<s3g::ProcessorStackNeckMaterial>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackNeckMaterialCount)),
            s3g::kProcessorStackNeckMaterialCount - 1u));
    params.bodyB = static_cast<s3g::ProcessorStackBodyMaterial>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackBodyMaterialCount)),
            s3g::kProcessorStackBodyMaterialCount - 1u));
    params.arpPattern = static_cast<s3g::ProcessorStackArpPattern>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackArpPatternCount)),
            s3g::kProcessorStackArpPatternCount - 1u));
    params.scale = static_cast<s3g::ProcessorStackScale>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackScaleCount)),
            s3g::kProcessorStackScaleCount - 1u));
    params.arpRate = static_cast<s3g::ProcessorStackArpRate>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackArpRateCount)),
            s3g::kProcessorStackArpRateCount - 1u));
    params.arpOctaves = 1u + std::min<uint32_t>(3u,
        static_cast<uint32_t>(randomUnit(seed) * 4.0f));
    params.arpGate = 0.22f + randomUnit(seed) * 0.68f;
    params.customPatternLength = 3u + std::min<uint32_t>(5u,
        static_cast<uint32_t>(randomUnit(seed) * 6.0f));
    for (auto& step : params.customPattern) {
        step = -4 + static_cast<int32_t>(randomUnit(seed) * 16.0f);
    }
    params.pierce = 0.34f + randomUnit(seed) * 0.66f;
    params.selfListen = 0.42f + randomUnit(seed) * 0.58f;
    params.targetGlitch = randomUnit(seed) * 0.88f;
    params.glitchRatchet = 0.16f + randomUnit(seed) * 0.76f;
    params.overloadMask = 0.58f + randomUnit(seed) * 0.42f;
    params.circuit = static_cast<s3g::ProcessorStackCircuit>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackCircuitCount)),
            s3g::kProcessorStackCircuitCount - 1u));
    params.bite = 0.22f + randomUnit(seed) * 0.70f;
    params.pedalTone = 0.20f + randomUnit(seed) * 0.68f;
    params.bias = 0.16f + randomUnit(seed) * 0.72f;
    params.stack = 0.34f + randomUnit(seed) * 0.58f;
    params.sag = randomUnit(seed) * 0.88f;
    params.focus = 0.16f + randomUnit(seed) * 0.76f;
    params.speaker = static_cast<s3g::ProcessorStackSpeakerProfile>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackSpeakerProfileCount)),
            s3g::kProcessorStackSpeakerProfileCount - 1u));
    params.cone = 0.28f + randomUnit(seed) * 0.68f;
    params.cabinet = randomUnit(seed);
    params.mic = randomUnit(seed) * 0.88f;
    params.feedback = 0.18f + randomUnit(seed) * 0.70f;
    params.proximity = 0.18f + randomUnit(seed) * 0.72f;
    params.harmonic = randomUnit(seed);
    params.tracking = 0.18f + randomUnit(seed) * 0.78f;
    params.polarity = 0.12f + randomUnit(seed) * 0.76f;
    params.root = randomUnit(seed) * 0.58f;
    params.chaos = randomUnit(seed) * 0.84f;
    params.arpBRelation = static_cast<s3g::ProcessorStackArpRelation>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackArpRelationCount)),
            s3g::kProcessorStackArpRelationCount - 1u));
    params.arpPatternB = static_cast<s3g::ProcessorStackArpPattern>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackArpPatternCount)),
            s3g::kProcessorStackArpPatternCount - 1u));
    params.scaleB = static_cast<s3g::ProcessorStackScale>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackScaleCount)),
            s3g::kProcessorStackScaleCount - 1u));
    params.arpRateB = static_cast<s3g::ProcessorStackArpRate>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackArpRateCount)),
            s3g::kProcessorStackArpRateCount - 1u));
    params.arpOctavesB = 1u + std::min<uint32_t>(3u,
        static_cast<uint32_t>(randomUnit(seed) * 4.0f));
    params.arpGateB = 0.22f + randomUnit(seed) * 0.68f;
    params.arpPhaseB = randomUnit(seed);
    params.customPatternLengthB = 3u + std::min<uint32_t>(5u,
        static_cast<uint32_t>(randomUnit(seed) * 6.0f));
    for (auto& step : params.customPatternB) {
        step = -4 + static_cast<int32_t>(randomUnit(seed) * 16.0f);
    }
    params.linkPedal = randomUnit(seed) > 0.62f;
    params.linkAmplifier = randomUnit(seed) > 0.62f;
    params.linkFeedback = randomUnit(seed) > 0.62f;
    params.circuitB = static_cast<s3g::ProcessorStackCircuit>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackCircuitCount)),
            s3g::kProcessorStackCircuitCount - 1u));
    params.biteB = 0.22f + randomUnit(seed) * 0.70f;
    params.pedalToneB = 0.20f + randomUnit(seed) * 0.68f;
    params.biasB = 0.16f + randomUnit(seed) * 0.72f;
    params.stackB = 0.34f + randomUnit(seed) * 0.58f;
    params.sagB = randomUnit(seed) * 0.88f;
    params.focusB = 0.16f + randomUnit(seed) * 0.76f;
    params.speakerB = static_cast<s3g::ProcessorStackSpeakerProfile>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackSpeakerProfileCount)),
            s3g::kProcessorStackSpeakerProfileCount - 1u));
    params.coneB = 0.28f + randomUnit(seed) * 0.68f;
    params.cabinetB = randomUnit(seed);
    params.micB = randomUnit(seed) * 0.88f;
    params.feedbackB = 0.18f + randomUnit(seed) * 0.70f;
    params.proximityB = 0.18f + randomUnit(seed) * 0.72f;
    params.harmonicB = randomUnit(seed);
    params.trackingB = 0.18f + randomUnit(seed) * 0.78f;
    params.polarityB = 0.12f + randomUnit(seed) * 0.76f;
    params.rootB = randomUnit(seed) * 0.58f;
    params.chaosB = randomUnit(seed) * 0.84f;
    params.pierceB = 0.34f + randomUnit(seed) * 0.66f;
    params.selfListenB = 0.42f + randomUnit(seed) * 0.58f;
    params.targetGlitchB = randomUnit(seed) * 0.88f;
    params.glitchRatchetB = 0.16f + randomUnit(seed) * 0.76f;
    params.overloadMaskB = 0.58f + randomUnit(seed) * 0.42f;
    return s3g::sanitizeProcessorStackParams(params);
}

NSRect stackPageButtonRect(uint8_t page)
{
    const NSRect panel = s3g::clap_gui::cocoaRect(kOutputPanel.frame);
    constexpr CGFloat width = 58.0;
    constexpr CGFloat height = 15.0;
    constexpr CGFloat gap = 4.0;
    constexpr CGFloat totalWidth = width * 4.0 + gap * 3.0;
    return NSMakeRect(NSMaxX(panel) - 8.0 - totalWidth
            + static_cast<CGFloat>(page) * (width + gap),
        panel.origin.y + 3.0, width, height);
}

NSRect stackCopyButtonRect(uint8_t index)
{
    const NSRect panel = s3g::clap_gui::cocoaRect(kLinksPanel.frame);
    constexpr CGFloat width = 88.0;
    constexpr CGFloat height = 15.0;
    constexpr CGFloat gap = 4.0;
    constexpr CGFloat totalWidth = width * 2.0 + gap;
    return NSMakeRect(NSMaxX(panel) - 8.0 - totalWidth
            + static_cast<CGFloat>(index) * (width + gap),
        panel.origin.y + 3.0, width, height);
}

constexpr uint32_t kStackScoreRandomActionCount = 5u;

NSRect stackScoreRandomActionButtonRect(uint32_t action)
{
    const NSRect panel = s3g::clap_gui::cocoaRect(kScoreTimingPanel.frame);
    constexpr CGFloat width = 50.0;
    constexpr CGFloat height = 15.0;
    constexpr CGFloat gap = 3.0;
    constexpr CGFloat totalWidth = width * kStackScoreRandomActionCount
        + gap * (kStackScoreRandomActionCount - 1u);
    return NSMakeRect(NSMaxX(panel) - 8.0 - totalWidth
            + static_cast<CGFloat>(std::min<uint32_t>(action,
                kStackScoreRandomActionCount - 1u)) * (width + gap),
        panel.origin.y + 3.0, width, height);
}

const layout::Panel& stackPatternPanel(uint8_t player)
{
    return player == 0u ? kPatternAPanel : kPatternBPanel;
}

NSRect stackPatternFieldRect(uint8_t player)
{
    const NSRect panel = s3g::clap_gui::cocoaRect(
        stackPatternPanel(player).frame);
    return NSMakeRect(panel.origin.x + 16.0, panel.origin.y + 31.0,
        panel.size.width - 32.0, 72.0);
}

NSRect stackPatternStepRect(uint8_t player, uint32_t step)
{
    const NSRect field = stackPatternFieldRect(player);
    const CGFloat stepWidth = field.size.width / 8.0;
    return NSMakeRect(field.origin.x + stepWidth
            * static_cast<CGFloat>(std::min<uint32_t>(step, 7u)),
        field.origin.y, stepWidth, field.size.height);
}

clap_id stackPatternStepParam(uint8_t player, uint32_t step)
{
    return (player == 0u ? kCustomStep1ParamId : kCustomStepB1ParamId)
        + std::min<uint32_t>(step, 7u);
}

clap_id stackPatternLengthParam(uint8_t player)
{
    return player == 0u ? kCustomLengthParamId : kCustomLengthBParamId;
}

int stackPatternStepAtX(uint8_t player, CGFloat x)
{
    const NSRect field = stackPatternFieldRect(player);
    if (x < NSMinX(field) || x > NSMaxX(field)) return -1;
    const CGFloat normalized = std::clamp(
        (x - NSMinX(field)) / field.size.width,
        static_cast<CGFloat>(0.0), static_cast<CGFloat>(0.999999));
    return static_cast<int>(std::floor(normalized * 8.0));
}

constexpr CGFloat kScoreGridTop = 252.0;
constexpr CGFloat kScoreGridLeft = 16.0;
constexpr CGFloat kScoreGridWidth = 948.0;
constexpr CGFloat kScoreGridHeaderHeight = 34.0;
constexpr CGFloat kScoreGridRowHeight = 24.0;
constexpr CGFloat kScoreRowNumberWidth = 38.0;
constexpr CGFloat kScorePlayerGap = 12.0;
constexpr CGFloat kScoreLockCellWidth = 43.0;

NSRect stackScoreGridRect()
{
    return NSMakeRect(kScoreGridLeft, kScoreGridTop, kScoreGridWidth,
        kScoreGridHeaderHeight
            + kScoreGridRowHeight
                * s3g::kProcessorStackScoreRowsPerSection);
}

CGFloat stackScorePlayerWidth()
{
    return (kScoreGridWidth - kScoreRowNumberWidth - kScorePlayerGap) * 0.5;
}

CGFloat stackScorePlayerX(uint32_t player)
{
    return kScoreGridLeft + kScoreRowNumberWidth
        + static_cast<CGFloat>(std::min(player, 1u))
            * (stackScorePlayerWidth() + kScorePlayerGap);
}

CGFloat stackScoreStringAreaWidth()
{
    return stackScorePlayerWidth()
        - kScoreLockCellWidth * s3g::kProcessorStackScoreLocksPerPlayer;
}

CGFloat stackScoreStringAreaX(uint32_t player)
{
    return stackScorePlayerX(player)
        + (player == 0u ? 0.0
            : kScoreLockCellWidth
                * s3g::kProcessorStackScoreLocksPerPlayer);
}

NSRect stackScoreCellRect(uint32_t player, uint32_t string, uint32_t row)
{
    const CGFloat cellWidth = stackScoreStringAreaWidth()
        / s3g::kProcessorStackScoreStringCount;
    return NSMakeRect(stackScoreStringAreaX(player)
            + static_cast<CGFloat>(string) * cellWidth,
        kScoreGridTop + kScoreGridHeaderHeight
            + static_cast<CGFloat>(row) * kScoreGridRowHeight,
        cellWidth, kScoreGridRowHeight);
}

NSRect stackScoreLockRect(uint32_t player, uint32_t slot, uint32_t row)
{
    player = std::min(player, 1u);
    slot = std::min(slot, s3g::kProcessorStackScoreLocksPerPlayer - 1u);
    const CGFloat x = player == 0u
        ? stackScoreStringAreaX(player) + stackScoreStringAreaWidth()
            + static_cast<CGFloat>(slot) * kScoreLockCellWidth
        : stackScorePlayerX(player)
            + static_cast<CGFloat>(slot) * kScoreLockCellWidth;
    return NSMakeRect(x,
        kScoreGridTop + kScoreGridHeaderHeight
            + static_cast<CGFloat>(row) * kScoreGridRowHeight,
        kScoreLockCellWidth, kScoreGridRowHeight);
}

NSRect stackScoreRowRect(uint32_t row)
{
    return NSMakeRect(kScoreGridLeft,
        kScoreGridTop + kScoreGridHeaderHeight
            + static_cast<CGFloat>(std::min(row,
                s3g::kProcessorStackScoreRowsPerSection - 1u))
                * kScoreGridRowHeight,
        kScoreGridWidth, kScoreGridRowHeight);
}

bool stackScoreCellAtPoint(NSPoint point, uint32_t& player,
    uint32_t& string, uint32_t& row)
{
    for (uint32_t candidatePlayer = 0u;
         candidatePlayer < s3g::kProcessorStackScorePlayerCount;
         ++candidatePlayer) {
        for (uint32_t candidateString = 0u;
             candidateString < s3g::kProcessorStackScoreStringCount;
             ++candidateString) {
            for (uint32_t candidateRow = 0u;
                 candidateRow < s3g::kProcessorStackScoreRowsPerSection;
                 ++candidateRow) {
                if (!NSPointInRect(point, stackScoreCellRect(candidatePlayer,
                        candidateString, candidateRow))) continue;
                player = candidatePlayer;
                string = candidateString;
                row = candidateRow;
                return true;
            }
        }
    }
    return false;
}

bool stackScoreLockAtPoint(NSPoint point, uint32_t& player,
    uint32_t& slot, uint32_t& row)
{
    for (uint32_t candidatePlayer = 0u;
         candidatePlayer < s3g::kProcessorStackScorePlayerCount;
         ++candidatePlayer) {
        for (uint32_t candidateSlot = 0u;
             candidateSlot < s3g::kProcessorStackScoreLocksPerPlayer;
             ++candidateSlot) {
            for (uint32_t candidateRow = 0u;
                 candidateRow < s3g::kProcessorStackScoreRowsPerSection;
                 ++candidateRow) {
                if (!NSPointInRect(point, stackScoreLockRect(
                        candidatePlayer, candidateSlot, candidateRow))) {
                    continue;
                }
                player = candidatePlayer;
                slot = candidateSlot;
                row = candidateRow;
                return true;
            }
        }
    }
    return false;
}

NSRect stackScoreSectionButtonRect(uint32_t section)
{
    const NSRect relationship = s3g::clap_gui::cocoaRect(
        kScoreRelationPanel.frame);
    constexpr CGFloat gap = 7.0;
    const CGFloat width = (relationship.size.width
        - gap * (s3g::kProcessorStackScoreSectionCount - 1u))
        / s3g::kProcessorStackScoreSectionCount;
    return NSMakeRect(relationship.origin.x
            + static_cast<CGFloat>(section) * (width + gap),
        NSMaxY(relationship) + 9.0, width, 30.0);
}

clap_id stackScoreLockParamId(uint32_t player,
    s3g::ProcessorStackScoreLockControl control)
{
    const bool b = player != 0u;
    using Control = s3g::ProcessorStackScoreLockControl;
    switch (control) {
    case Control::Neck: return b ? kNeckBParamId : kNeckAParamId;
    case Control::Body: return b ? kBodyBParamId : kBodyAParamId;
    case Control::Circuit: return b ? kCircuitBParamId : kCircuitParamId;
    case Control::Bite: return b ? kBiteBParamId : kBiteParamId;
    case Control::Tone: return b ? kPedalToneBParamId : kPedalToneParamId;
    case Control::Bias: return b ? kBiasBParamId : kBiasParamId;
    case Control::Stack: return b ? kStackBParamId : kStackParamId;
    case Control::Sag: return b ? kSagBParamId : kSagParamId;
    case Control::Focus: return b ? kFocusBParamId : kFocusParamId;
    case Control::Speaker: return b ? kSpeakerBParamId : kSpeakerAParamId;
    case Control::Cone: return b ? kConeBParamId : kConeParamId;
    case Control::Cabinet: return b ? kCabinetBParamId : kCabinetParamId;
    case Control::Mic: return b ? kMicBParamId : kMicParamId;
    case Control::Feedback: return b ? kFeedbackBParamId : kFeedbackParamId;
    case Control::Proximity: return b ? kProximityBParamId : kProximityParamId;
    case Control::Harmonic: return b ? kHarmonicBParamId : kHarmonicParamId;
    case Control::Tracking: return b ? kTrackingBParamId : kTrackingParamId;
    case Control::Polarity: return b ? kPolarityBParamId : kPolarityParamId;
    case Control::Root: return b ? kRootBParamId : kRootParamId;
    case Control::Chaos: return b ? kChaosBParamId : kChaosParamId;
    case Control::Pierce: return b ? kPierceBParamId : kPierceParamId;
    case Control::SelfListen: return b ? kSelfListenBParamId : kSelfListenParamId;
    case Control::TargetGlitch: return b ? kTargetGlitchBParamId : kTargetGlitchParamId;
    case Control::Ratchet: return b ? kGlitchRatchetBParamId : kGlitchRatchetParamId;
    case Control::OverloadMask: return b ? kOverloadMaskBParamId : kOverloadMaskParamId;
    case Control::None:
    case Control::Count:
        return CLAP_INVALID_ID;
    }
    return CLAP_INVALID_ID;
}

s3g::ProcessorStackScoreLockCell stackScoreLockCell(
    const Plugin& plugin, uint32_t section, uint32_t row,
    uint32_t player, uint32_t slot)
{
    const size_t index = s3g::processorStackScoreLockIndex(
        section, row, player, slot);
    const uint32_t packed = plugin.scoreLocks[index].load(
        std::memory_order_acquire);
    s3g::ProcessorStackScoreLockCell lock;
    lock.control = static_cast<uint8_t>(packed & 0xffu);
    lock.normalized = static_cast<uint16_t>((packed >> 8u) & 0xffffu);
    return lock;
}

NSRect stackScoreArrangementRect()
{
    return NSMakeRect(kScoreGridLeft,
        kScoreGridTop + kScoreGridHeaderHeight
            + kScoreGridRowHeight
                * s3g::kProcessorStackScoreRowsPerSection + 12.0,
        kScoreGridWidth, 58.0);
}

NSRect stackScoreArrangementSlotRect(uint32_t slot)
{
    const NSRect panel = stackScoreArrangementRect();
    constexpr CGFloat left = 128.0;
    constexpr CGFloat gap = 5.0;
    const CGFloat width = (panel.size.width - left - 14.0
        - gap * (s3g::kProcessorStackScoreArrangementSlots - 1u))
        / s3g::kProcessorStackScoreArrangementSlots;
    return NSMakeRect(panel.origin.x + left
            + static_cast<CGFloat>(slot) * (width + gap),
        panel.origin.y + 27.0, width, 20.0);
}

NSRect stackScoreEntryLegendRect()
{
    const NSRect arrangement = stackScoreArrangementRect();
    return NSMakeRect(arrangement.origin.x, NSMaxY(arrangement) + 6.0,
        arrangement.size.width, 22.0);
}

void drawStackPatternMultislider(uint8_t player,
                                 const Plugin& plugin,
                                 int dragPlayer,
                                 int dragStep,
                                 NSDictionary* valueAttrs,
                                 const s3g::clap_gui::Style& style)
{
    const NSRect field = stackPatternFieldRect(player);
    [style.strip setFill];
    NSRectFill(field);
    [style.grid setStroke];
    NSFrameRect(field);

    const uint32_t length = std::clamp<uint32_t>(static_cast<uint32_t>(
        std::lround(paramValue(plugin, stackPatternLengthParam(player)))),
        1u, 8u);

    const NSRect panel = s3g::clap_gui::cocoaRect(
        stackPatternPanel(player).frame);
    for (uint32_t step = 0u; step < 8u; ++step) {
        const NSRect cell = stackPatternStepRect(player, step);
        const clap_id id = stackPatternStepParam(player, step);
        const double value = paramValue(plugin, id);
        const bool rest = static_cast<int32_t>(std::lround(value))
            == s3g::kProcessorStackArpRest;
        const CGFloat rawValueY = NSMaxY(field) - static_cast<CGFloat>(
            uiNormalizedValue(id, value)) * field.size.height;
        const CGFloat valueY = std::clamp(rawValueY,
            NSMinY(field) + 1.0, NSMaxY(field) - 1.0);
        const bool selected = dragPlayer == static_cast<int>(player)
            && dragStep == static_cast<int>(step);
        NSColor* lineColor = selected ? style.text
            : (step < length ? style.accent : style.grid);
        if (rest) {
            [[lineColor colorWithAlphaComponent:selected ? 0.36 : 0.22]
                setFill];
            NSRectFill(NSInsetRect(cell, 5.0, 5.0));
        } else {
            [lineColor setFill];
            NSRectFill(NSMakeRect(cell.origin.x + 5.0,
                std::floor(valueY), cell.size.width - 10.0, 2.0));
        }

        NSString* stepText = rest
            ? [NSString stringWithFormat:@"%u REST", step + 1u]
            : [NSString stringWithFormat:@"%u %+d", step + 1u,
                static_cast<int>(std::lround(value))];
        s3g::clap_gui::drawCenteredTextToFit(stepText,
            NSMakeRect(cell.origin.x + 2.0, panel.origin.y + 108.0,
                cell.size.width - 4.0, 14.0), valueAttrs);
    }
}

bool queueRigCopy(Plugin& plugin, bool aToB)
{
    static constexpr std::array<clap_id, 25u> rigA {{
        kNeckAParamId, kBodyAParamId,
        kCircuitParamId, kBiteParamId, kPedalToneParamId, kBiasParamId,
        kStackParamId, kSagParamId, kFocusParamId, kSpeakerAParamId,
        kConeParamId,
        kCabinetParamId, kMicParamId,
        kFeedbackParamId, kProximityParamId, kHarmonicParamId,
        kTrackingParamId, kPolarityParamId, kRootParamId, kChaosParamId,
        kPierceParamId, kSelfListenParamId, kTargetGlitchParamId,
        kGlitchRatchetParamId, kOverloadMaskParamId,
    }};
    static constexpr std::array<clap_id, 25u> rigB {{
        kNeckBParamId, kBodyBParamId,
        kCircuitBParamId, kBiteBParamId, kPedalToneBParamId, kBiasBParamId,
        kStackBParamId, kSagBParamId, kFocusBParamId, kSpeakerBParamId,
        kConeBParamId,
        kCabinetBParamId, kMicBParamId,
        kFeedbackBParamId, kProximityBParamId, kHarmonicBParamId,
        kTrackingBParamId, kPolarityBParamId, kRootBParamId, kChaosBParamId,
        kPierceBParamId, kSelfListenBParamId, kTargetGlitchBParamId,
        kGlitchRatchetBParamId, kOverloadMaskBParamId,
    }};
    using Kind = s3g::clap_gui::ParamEventKind;
    std::array<s3g::clap_gui::ParamEvent, rigA.size() * 3u> events {};
    const auto& source = aToB ? rigA : rigB;
    const auto& destination = aToB ? rigB : rigA;
    for (uint32_t index = 0u; index < source.size(); ++index) {
        const double value = paramValue(plugin, source[index]);
        events[index * 3u] = {
            Kind::GestureBegin, destination[index], 0.0 };
        events[index * 3u + 1u] = {
            Kind::Value, destination[index], value };
        events[index * 3u + 2u] = {
            Kind::GestureEnd, destination[index], 0.0 };
    }
    if (!plugin.guiParamEvents.pushBatch(events.data(), events.size())) {
        return false;
    }
    for (uint32_t index = 0u; index < source.size(); ++index) {
        publishParam(plugin, destination[index],
            paramValue(plugin, source[index]));
    }
    requestGuiParamService(plugin);
    return true;
}

} // namespace

@interface S3GProcessorStackView : NSView {
    void* _plugin;
    int _dragParam;
    int _dragPatternPlayer;
    int _dragPatternStep;
    int _factoryPresetIndex;
    uint8_t _page;
    clap_id _openMenu;
    int _hoverMenuItem;
    uint32_t _menuItemCount;
    uint32_t _scoreEditSection;
    uint32_t _scoreSelectedPlayer;
    uint32_t _scoreSelectedString;
    uint32_t _scoreSelectedRow;
    BOOL _dragScoreLock;
    uint32_t _scoreLockPlayer;
    uint32_t _scoreLockSlot;
    uint32_t _scoreLockRow;
    NSPoint _scoreLockDragStartPoint;
    double _scoreLockDragStartNormalized;
    int _scorePendingFret;
    NSTimeInterval _scorePendingTime;
    int _scoreRandomFeedback;
    NSTimeInterval _scoreRandomFeedbackUntil;
    uint64_t _observedParamRevision;
    uint64_t _observedScoreVisualRevision;
    int32_t _pendingScoreSection;
    int32_t _pendingScoreRow;
    int32_t _pendingScoreSlot;
    int32_t _presentedScoreSection;
    int32_t _presentedScoreRow;
    int32_t _presentedScoreSlot;
    BOOL _scoreVisualPrimed;
    NSTimer* _timer;
    char _presetName[64];
}
- (id)initWithPlugin:(void*)plugin;
- (void)setDocumentationPage:(NSUInteger)page;
- (void)loadDocumentationScore;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)applyFactoryPreset:(int)index;
- (void)markCustomPreset;
- (NSRect)openMenuRect;
- (void)drawOpenMenu:(NSDictionary*)attrs
    style:(const s3g::clap_gui::Style&)style;
- (void)drawScore:(NSDictionary*)labelAttrs
    values:(NSDictionary*)valueAttrs
    style:(const s3g::clap_gui::Style&)style;
- (NSMenu*)scoreLockMenuForPlayer:(uint32_t)player
    row:(uint32_t)row slot:(uint32_t)slot;
- (void)scoreLockSelected:(NSMenuItem*)sender;
- (void)updateDraggedScoreLock:(NSPoint)point;
@end

@implementation S3GProcessorStackView

- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (!self) return nil;
    _plugin = plugin;
    _dragParam = -1;
    _dragPatternPlayer = -1;
    _dragPatternStep = -1;
    _page = 0u;
    _openMenu = CLAP_INVALID_ID;
    _hoverMenuItem = -1;
    _menuItemCount = 0u;
    _scoreEditSection = 0u;
    _scoreSelectedPlayer = 0u;
    _scoreSelectedString = 0u;
    _scoreSelectedRow = 0u;
    _dragScoreLock = NO;
    _scoreLockPlayer = 0u;
    _scoreLockSlot = 0u;
    _scoreLockRow = 0u;
    _scoreLockDragStartPoint = NSZeroPoint;
    _scoreLockDragStartNormalized = 0.0;
    _scorePendingFret = -1;
    _scorePendingTime = 0.0;
    _scoreRandomFeedback = -1;
    _scoreRandomFeedbackUntil = 0.0;
    _timer = nil;
    auto* instance = static_cast<Plugin*>(_plugin);
    _observedParamRevision = instance
        ? instance->parameterRevision.load(std::memory_order_acquire) : 0u;
    _observedScoreVisualRevision = instance
        ? instance->scoreVisualRevision.load(std::memory_order_acquire) : 0u;
    _pendingScoreSection = _pendingScoreRow = _pendingScoreSlot = -1;
    _presentedScoreSection = _presentedScoreRow = _presentedScoreSlot = -1;
    _scoreVisualPrimed = NO;
    _factoryPresetIndex = instance
        ? s3g::processorStackFactoryPresetIndex(
            publishedParamsSnapshot(*instance)) : 0;
    std::snprintf(_presetName, sizeof(_presetName), "%s",
        _factoryPresetIndex >= 0
            ? s3g::processorStackFactoryPresetInfo(
                static_cast<uint32_t>(_factoryPresetIndex)).name
            : "CUSTOM");
    return self;
}

- (void)setDocumentationPage:(NSUInteger)page
{
    _page = static_cast<uint8_t>(std::min<NSUInteger>(page, 3u));
    _openMenu = CLAP_INVALID_ID;
    _hoverMenuItem = -1;
    [self setNeedsDisplay:YES];
}

- (void)loadDocumentationScore
{
    auto* instance = static_cast<Plugin*>(_plugin);
    if (!instance) return;
    const auto params = publishedParamsSnapshot(*instance);
    auto program = s3g::generateProcessorStackScore(
        0x53434f52u, params.scale, params.scaleB);
    s3g::setProcessorStackScoreCell(program, 0u, 2u, 0u, 0u,
        s3g::kProcessorStackScoreHold);
    s3g::setProcessorStackScoreCell(program, 0u, 3u, 0u, 0u,
        s3g::kProcessorStackScoreHold);
    s3g::setProcessorStackScoreCell(program, 0u, 3u, 1u, 1u,
        s3g::kProcessorStackScoreHold);
    program = s3g::randomizeProcessorStackScoreLocks(
        program, 0u, 0x4c4f434bu);
    storeScoreProgram(*instance, program, false);
    _scoreEditSection = 0u;
    _scoreSelectedPlayer = 0u;
    _scoreSelectedString = 0u;
    _scoreSelectedRow = 3u;
    _presentedScoreSection = _pendingScoreSection = 0;
    _presentedScoreRow = _pendingScoreRow = 3;
    _presentedScoreSlot = _pendingScoreSlot = 0;
    _scoreVisualPrimed = YES;
    _scoreRandomFeedback = 1;
    _scoreRandomFeedbackUntil =
        [NSDate timeIntervalSinceReferenceDate] + 60.0;
    [self setNeedsDisplay:YES];
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)dealloc
{
    [self stopRefreshTimer];
    [super dealloc];
}

- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [NSTimer timerWithTimeInterval:1.0 / 60.0
        target:self selector:@selector(refresh:) userInfo:nil repeats:YES];
    _timer.tolerance = 1.0 / 240.0;
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}

- (void)stopRefreshTimer
{
    if (!_timer) return;
    [_timer invalidate];
    _timer = nil;
}

- (void)applyFactoryPreset:(int)index
{
    auto* instance = static_cast<Plugin*>(_plugin);
    if (!instance) return;
    index = std::clamp(index, 0,
        static_cast<int>(s3g::kProcessorStackFactoryPresetCount - 1u));
    if (!queueGuiParams(*instance, s3g::processorStackFactoryPreset(
            static_cast<uint32_t>(index)))) {
        NSBeep();
        return;
    }
    _factoryPresetIndex = index;
    std::snprintf(_presetName, sizeof(_presetName), "%s",
        s3g::processorStackFactoryPresetInfo(
            static_cast<uint32_t>(index)).name);
    [self setNeedsDisplay:YES];
}

- (void)markCustomPreset
{
    _factoryPresetIndex = -1;
    std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
}

- (NSRect)openMenuRect
{
    NSRect anchor = NSZeroRect;
    if (_openMenu == kFactoryPresetMenuId) {
        const auto band = s3g::clap_gui::encoderTitleBand(
            kGuiWidth, kGuiHeight);
        anchor = s3g::clap_gui::cocoaRect(band.presetMenu);
    } else {
        const auto row = std::find_if(kUiRows.begin(), kUiRows.end(),
            [=](const StackUiRow& candidate) {
                return candidate.id == _openMenu
                    && (candidate.page == _page
                        || candidate.page == kAllPages);
            });
        if (row == kUiRows.end() || !isUiMenuParam(row->id)) {
            return NSZeroRect;
        }
        anchor = NSMakeRect(
            s3g::gui_layout::processorControlX(row->panelX), row->y - 1.0,
            s3g::gui_layout::processorMenuWidth(row->panelWidth), 15.0);
    }
    const CGFloat menuHeight = 18.0 * _menuItemCount;
    CGFloat menuY = NSMaxY(anchor) + 2.0;
    if (menuY + menuHeight > kGuiHeight) {
        menuY = anchor.origin.y - 2.0 - menuHeight;
    }
    return NSMakeRect(anchor.origin.x, menuY,
        anchor.size.width, menuHeight);
}

- (void)drawOpenMenu:(NSDictionary*)attrs
    style:(const s3g::clap_gui::Style&)style
{
    if (_openMenu == CLAP_INVALID_ID || _menuItemCount == 0u) return;
    NSString* items[32] {};
    const uint32_t count = std::min<uint32_t>(_menuItemCount, 32u);
    if (_openMenu == kFactoryPresetMenuId) {
        for (uint32_t index = 0u; index < count; ++index) {
            items[index] = [NSString stringWithUTF8String:
                s3g::processorStackFactoryPresetInfo(index).name];
        }
    } else {
        for (uint32_t index = 0u; index < count; ++index) {
            char text[64] {};
            const auto* def = paramDef(_openMenu);
            const double menuValue = def
                ? def->minimum + static_cast<double>(index)
                : static_cast<double>(index);
            if (paramsValueToText(
                    &static_cast<Plugin*>(_plugin)->plugin, _openMenu,
                    menuValue, text, sizeof(text))) {
                items[index] = [NSString stringWithUTF8String:text];
            } else {
                items[index] = @"—";
            }
        }
    }
    const int selected = _openMenu == kFactoryPresetMenuId
        ? _factoryPresetIndex
        : static_cast<int>(std::lround(paramValue(
            *static_cast<Plugin*>(_plugin), _openMenu)
                - paramDef(_openMenu)->minimum));
    s3g::clap_gui::drawDropdownMenu([self openMenuRect], 18.0,
        items, count, selected, _hoverMenuItem, attrs, style);
}

- (NSMenu*)scoreLockMenuForPlayer:(uint32_t)player
    row:(uint32_t)row slot:(uint32_t)slot
{
    auto* instance = static_cast<Plugin*>(_plugin);
    if (!instance) return nil;
    const auto current = stackScoreLockCell(
        *instance, _scoreEditSection, row, player, slot);
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@"STACK ROW LOCK"];
    menu.autoenablesItems = NO;
    menu.font = [NSFont monospacedSystemFontOfSize:10.0
        weight:NSFontWeightMedium];
    NSMenuItem* heading = [[NSMenuItem alloc] initWithTitle:
        [NSString stringWithFormat:@"PLAYER %c · ROW %02u · LOCK %u",
            static_cast<char>('A' + player), row + 1u, slot + 1u]
        action:nil keyEquivalent:@""];
    heading.enabled = NO;
    [menu addItem:heading];
    if (player == 1u) {
        NSMenuItem* note = [[NSMenuItem alloc] initWithTitle:
            @"B locks use that rig independently for this row"
            action:nil keyEquivalent:@""];
        note.enabled = NO;
        [menu addItem:note];
    }
    [menu addItem:NSMenuItem.separatorItem];

    const auto addControl = [&](NSString* title,
                                s3g::ProcessorStackScoreLockControl control,
                                BOOL headingItem) {
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
            action:headingItem ? nil : @selector(scoreLockSelected:)
            keyEquivalent:@""];
        item.target = self;
        item.enabled = !headingItem;
        if (!headingItem) {
            item.representedObject = @{
                @"player": @(player), @"row": @(row), @"slot": @(slot),
                @"control": @(static_cast<uint32_t>(control)),
            };
            item.state = current.control == static_cast<uint8_t>(control)
                ? NSControlStateValueOn : NSControlStateValueOff;
        }
        [menu addItem:item];
    };
    addControl(@"---   CLEAR", s3g::ProcessorStackScoreLockControl::None,
        NO);

    struct LockGroup {
        const char* name;
        s3g::ProcessorStackScoreLockControl first;
        s3g::ProcessorStackScoreLockControl last;
    };
    constexpr std::array<LockGroup, 5u> groups {{
        { "MATERIAL", s3g::ProcessorStackScoreLockControl::Neck,
            s3g::ProcessorStackScoreLockControl::Body },
        { "PEDAL", s3g::ProcessorStackScoreLockControl::Circuit,
            s3g::ProcessorStackScoreLockControl::Bias },
        { "AMPLIFIER / SPEAKER", s3g::ProcessorStackScoreLockControl::Stack,
            s3g::ProcessorStackScoreLockControl::Mic },
        { "MIC FEEDBACK LOOP", s3g::ProcessorStackScoreLockControl::Feedback,
            s3g::ProcessorStackScoreLockControl::OverloadMask },
        { "SPEAKER PROFILE", s3g::ProcessorStackScoreLockControl::Speaker,
            s3g::ProcessorStackScoreLockControl::Speaker },
    }};
    for (const auto& group : groups) {
        [menu addItem:NSMenuItem.separatorItem];
        addControl([NSString stringWithUTF8String:group.name],
            s3g::ProcessorStackScoreLockControl::None, YES);
        for (uint32_t raw = static_cast<uint32_t>(group.first);
             raw <= static_cast<uint32_t>(group.last); ++raw) {
            const auto control = static_cast<
                s3g::ProcessorStackScoreLockControl>(raw);
            const clap_id id = stackScoreLockParamId(player, control);
            char valueText[64] {};
            paramsValueToText(&instance->plugin, id,
                paramValue(*instance, id), valueText, sizeof(valueText));
            NSString* title = [NSString stringWithFormat:@"%-16s  %s",
                s3g::processorStackScoreLockControlName(control), valueText];
            addControl(title, control, NO);
        }
    }
    return menu;
}

- (void)scoreLockSelected:(NSMenuItem*)sender
{
    auto* instance = static_cast<Plugin*>(_plugin);
    NSDictionary* value = sender.representedObject;
    if (!instance || ![value isKindOfClass:NSDictionary.class]) return;
    const uint32_t player = [value[@"player"] unsignedIntValue];
    const uint32_t row = [value[@"row"] unsignedIntValue];
    const uint32_t slot = [value[@"slot"] unsignedIntValue];
    const auto control = static_cast<s3g::ProcessorStackScoreLockControl>(
        [value[@"control"] unsignedIntValue]);
    double normalized = 0.0;
    const auto existing = stackScoreLockCell(
        *instance, _scoreEditSection, row, player, slot);
    if (existing.control == static_cast<uint8_t>(control)) {
        normalized = s3g::processorStackScoreLockNormalized(existing);
    } else if (control != s3g::ProcessorStackScoreLockControl::None) {
        const clap_id id = stackScoreLockParamId(player, control);
        const double base = uiNormalizedValue(id, paramValue(*instance, id));
        if (control == s3g::ProcessorStackScoreLockControl::Neck
            || control == s3g::ProcessorStackScoreLockControl::Body) {
            const uint32_t current = static_cast<uint32_t>(
                std::lround(base * 3.0));
            normalized = static_cast<double>((current + 1u) % 4u) / 3.0;
        } else if (control
                == s3g::ProcessorStackScoreLockControl::Circuit) {
            const uint32_t current = static_cast<uint32_t>(
                std::lround(base * 7.0));
            normalized = static_cast<double>((current + 3u) % 8u) / 7.0;
        } else if (control
                == s3g::ProcessorStackScoreLockControl::Speaker) {
            const uint32_t current = static_cast<uint32_t>(
                std::lround(base * 6.0));
            normalized = static_cast<double>((current + 2u) % 7u) / 6.0;
        } else {
            // A newly assigned lock should be audible immediately rather than
            // beginning as a neutral copy of the base slider.
            normalized = std::clamp(
                base < 0.5 ? base + 0.36 : base - 0.36, 0.08, 0.92);
        }
    }
    setScoreLock(*instance, _scoreEditSection, row,
        player, slot, control, normalized);
    _scoreLockPlayer = player;
    _scoreLockRow = row;
    _scoreLockSlot = slot;
    [self setNeedsDisplayInRect:NSInsetRect(
        stackScoreLockRect(player, slot, row), -1.0, -1.0)];
}

- (void)updateDraggedScoreLock:(NSPoint)point
{
    if (!_dragScoreLock) return;
    auto* instance = static_cast<Plugin*>(_plugin);
    if (!instance) return;
    const auto lock = stackScoreLockCell(*instance, _scoreEditSection,
        _scoreLockRow, _scoreLockPlayer, _scoreLockSlot);
    if (lock.control == static_cast<uint8_t>(
            s3g::ProcessorStackScoreLockControl::None)) return;
    const double normalized = std::clamp(_scoreLockDragStartNormalized
        + static_cast<double>(_scoreLockDragStartPoint.y - point.y) / 120.0,
        0.0, 1.0);
    setScoreLock(*instance, _scoreEditSection, _scoreLockRow,
        _scoreLockPlayer, _scoreLockSlot,
        static_cast<s3g::ProcessorStackScoreLockControl>(lock.control),
        normalized);
    [self setNeedsDisplayInRect:NSInsetRect(stackScoreLockRect(
        _scoreLockPlayer, _scoreLockSlot, _scoreLockRow), -1.0, -1.0)];
}

- (void)drawScore:(NSDictionary*)labelAttrs
    values:(NSDictionary*)valueAttrs
    style:(const s3g::clap_gui::Style&)style
{
    auto* instance = static_cast<Plugin*>(_plugin);
    if (!instance) return;
    const bool scoreBRelatesToA = paramValue(
        *instance, kScoreBSourceParamId) >= 0.5;
    const uint32_t scoreLength = static_cast<uint32_t>(std::clamp(
        std::lround(paramValue(*instance, kScoreLengthParamId)),
        1l, static_cast<long>(s3g::kProcessorStackScoreArrangementSlots)));
    const NSRect grid = stackScoreGridRect();
    NSColor* const scoreTeal = [NSColor colorWithSRGBRed:0.055
        green:0.255 blue:0.275 alpha:0.34];
    NSColor* const scoreTealStrong = [NSColor colorWithSRGBRed:0.075
        green:0.34 blue:0.36 alpha:0.62];
    // Edit and lock fills stay deliberately darker than the playhead teal.
    // Their light-gray text is the information; color only marks context.
    NSColor* const scoreEntryFill = [NSColor colorWithSRGBRed:0.025
        green:0.115 blue:0.125 alpha:0.96];
    NSColor* const scoreLockFill = [NSColor colorWithSRGBRed:0.025
        green:0.085 blue:0.095 alpha:0.96];
    [style.cellBg setFill];
    NSRectFill(grid);
    [style.grid setStroke];
    NSFrameRect(grid);
    [style.strip setFill];
    NSRectFill(NSMakeRect(grid.origin.x, grid.origin.y,
        grid.size.width, kScoreGridHeaderHeight));

    for (uint32_t section = 0u;
         section < s3g::kProcessorStackScoreSectionCount; ++section) {
        const NSRect button = stackScoreSectionButtonRect(section);
        [(section == _scoreEditSection ? scoreTealStrong : style.cellBg)
            setFill];
        NSRectFill(button);
        [(section == _scoreEditSection ? style.accent : style.grid)
            setStroke];
        NSFrameRect(button);
        NSString* text = [NSString stringWithFormat:@"SECTION %c",
            static_cast<char>('A' + section)];
        s3g::clap_gui::drawCenteredTextToFit(text,
            NSMakeRect(button.origin.x + 4.0, button.origin.y + 7.0,
                button.size.width - 8.0, 16.0),
            section == _scoreEditSection ? labelAttrs : valueAttrs);
    }

    const CGFloat playerWidth = stackScorePlayerWidth();
    const CGFloat cellWidth = stackScoreStringAreaWidth()
        / s3g::kProcessorStackScoreStringCount;
    static NSString* const strings[6] {
        @"E", @"A", @"D", @"G", @"B", @"e",
    };
    for (uint32_t player = 0u;
         player < s3g::kProcessorStackScorePlayerCount; ++player) {
        const CGFloat playerX = stackScorePlayerX(player);
        NSString* title = player == 0u ? @"PLAYER A · TAB"
            : (scoreBRelatesToA
                ? @"PLAYER B · RELATE A" : @"PLAYER B · EXPLICIT TAB");
        s3g::clap_gui::drawCenteredTextToFit(title,
            NSMakeRect(playerX, kScoreGridTop + 1.0,
                playerWidth, 14.0), labelAttrs);
        for (uint32_t string = 0u;
             string < s3g::kProcessorStackScoreStringCount; ++string) {
            s3g::clap_gui::drawCenteredTextToFit(strings[string],
                NSMakeRect(stackScoreStringAreaX(player)
                        + static_cast<CGFloat>(string) * cellWidth,
                    kScoreGridTop + 17.0, cellWidth, 14.0), valueAttrs);
        }
        for (uint32_t slot = 0u;
             slot < s3g::kProcessorStackScoreLocksPerPlayer; ++slot) {
            const NSRect lock = stackScoreLockRect(player, slot, 0u);
            NSString* lockTitle = [NSString stringWithFormat:
                @"L%u", slot + 1u];
            s3g::clap_gui::drawCenteredTextToFit(lockTitle,
                NSMakeRect(lock.origin.x, kScoreGridTop + 17.0,
                    lock.size.width, 14.0), valueAttrs);
        }
    }

    const int32_t playingSection = _presentedScoreSection;
    const int32_t playingRow = _presentedScoreRow;
    for (uint32_t row = 0u;
         row < s3g::kProcessorStackScoreRowsPerSection; ++row) {
        const CGFloat y = kScoreGridTop + kScoreGridHeaderHeight
            + static_cast<CGFloat>(row) * kScoreGridRowHeight;
        if ((row % 4u) == 0u) {
            [[style.strip colorWithAlphaComponent:0.82] setFill];
            NSRectFill(NSMakeRect(grid.origin.x, y,
                grid.size.width, kScoreGridRowHeight));
        }
        if (playingSection == static_cast<int32_t>(_scoreEditSection)
            && playingRow == static_cast<int32_t>(row)) {
            [scoreTeal setFill];
            NSRectFill(NSMakeRect(grid.origin.x, y,
                grid.size.width, kScoreGridRowHeight));
        }
        NSString* rowText = [NSString stringWithFormat:@"%02u", row + 1u];
        s3g::clap_gui::drawCenteredTextToFit(rowText,
            NSMakeRect(grid.origin.x + 2.0, y + 4.0,
                kScoreRowNumberWidth - 4.0, 14.0),
            row == _scoreSelectedRow ? labelAttrs : valueAttrs);
        for (uint32_t player = 0u;
             player < s3g::kProcessorStackScorePlayerCount; ++player) {
            for (uint32_t string = 0u;
                 string < s3g::kProcessorStackScoreStringCount; ++string) {
                const NSRect cell = stackScoreCellRect(player, string, row);
                const bool selected = player == _scoreSelectedPlayer
                    && string == _scoreSelectedString
                    && row == _scoreSelectedRow;
                if (selected) {
                    [scoreEntryFill setFill];
                    NSRectFill(NSInsetRect(cell, 1.0, 1.0));
                }
                [style.grid setStroke];
                NSFrameRect(cell);
                if (selected) {
                    [[style.accent colorWithAlphaComponent:0.68] setStroke];
                    NSFrameRect(NSInsetRect(cell, 1.0, 1.0));
                }
                const size_t index = s3g::processorStackScoreCellIndex(
                    _scoreEditSection, row, player, string);
                const int fret = instance->scoreCells[index].load(
                    std::memory_order_acquire);
                NSString* text = fret == s3g::kProcessorStackScoreHold
                    ? @"~" : fret >= 0
                        ? [NSString stringWithFormat:@"%d", fret] : @"—";
                NSDictionary* attrs = player == 1u
                        && scoreBRelatesToA
                    ? valueAttrs : (fret != s3g::kProcessorStackScoreRest
                        ? labelAttrs : valueAttrs);
                s3g::clap_gui::drawCenteredTextToFit(text,
                    NSMakeRect(cell.origin.x, cell.origin.y + 4.0,
                        cell.size.width, 14.0), attrs);
            }
            for (uint32_t slot = 0u;
                 slot < s3g::kProcessorStackScoreLocksPerPlayer; ++slot) {
                const NSRect cell = stackScoreLockRect(player, slot, row);
                const auto lock = stackScoreLockCell(*instance,
                    _scoreEditSection, row, player, slot);
                const auto control = static_cast<
                    s3g::ProcessorStackScoreLockControl>(std::min<uint32_t>(
                        lock.control,
                        s3g::kProcessorStackScoreLockControlCount - 1u));
                if (control != s3g::ProcessorStackScoreLockControl::None) {
                    [scoreLockFill setFill];
                    NSRectFill(NSInsetRect(cell, 1.0, 1.0));
                }
                [style.grid setStroke];
                NSFrameRect(cell);
                NSString* text = @"---";
                if (control != s3g::ProcessorStackScoreLockControl::None) {
                    const double normalized =
                        s3g::processorStackScoreLockNormalized(lock);
                    const bool material = control
                            == s3g::ProcessorStackScoreLockControl::Neck
                        || control
                            == s3g::ProcessorStackScoreLockControl::Body;
                    const bool circuit = control
                        == s3g::ProcessorStackScoreLockControl::Circuit;
                    const bool speaker = control
                        == s3g::ProcessorStackScoreLockControl::Speaker;
                    const int displayValue = static_cast<int>(std::lround(
                        normalized * (circuit ? 7.0
                            : material ? 3.0 : speaker ? 6.0 : 99.0)));
                    text = [NSString stringWithFormat:@"%s%02d",
                        s3g::processorStackScoreLockControlShortName(control),
                        displayValue];
                }
                s3g::clap_gui::drawCenteredTextToFit(text,
                    NSMakeRect(cell.origin.x + 2.0, cell.origin.y + 4.0,
                        cell.size.width - 4.0, 14.0),
                    control == s3g::ProcessorStackScoreLockControl::None
                        ? valueAttrs : labelAttrs);
            }
        }
    }

    const NSRect arrangement = stackScoreArrangementRect();
    s3g::clap_gui::drawPanelFrame(arrangement.origin.x,
        arrangement.origin.y, arrangement.size.width,
        arrangement.size.height, style);
    s3g::clap_gui::drawPanelHeader(
        @"ARRANGEMENT · CLICK SLOT: SECTION · RIGHT-CLICK L1/L2: CONTROL · DRAG: VALUE",
        true, arrangement.origin.x, arrangement.origin.y,
        arrangement.size.width, 21.0, labelAttrs, style);
    const int32_t playingSlot = _presentedScoreSlot;
    for (uint32_t slot = 0u;
         slot < s3g::kProcessorStackScoreArrangementSlots; ++slot) {
        const NSRect rect = stackScoreArrangementSlotRect(slot);
        const bool enabled = slot < scoreLength;
        const bool playing = playingSlot == static_cast<int32_t>(slot);
        [(playing ? scoreTealStrong : enabled ? style.cellBg : style.bg)
            setFill];
        NSRectFill(rect);
        [style.grid setStroke];
        NSFrameRect(rect);
        const uint32_t section = std::min<uint32_t>(
            instance->scoreArrangement[slot].load(
                std::memory_order_acquire),
            s3g::kProcessorStackScoreSectionCount - 1u);
        NSString* text = [NSString stringWithFormat:@"%u · %c",
            slot + 1u, static_cast<char>('A' + section)];
        s3g::clap_gui::drawCenteredTextToFit(text,
            NSMakeRect(rect.origin.x, rect.origin.y + 3.0,
                rect.size.width, 14.0),
            playing ? labelAttrs : valueAttrs);
    }

    const NSRect legend = stackScoreEntryLegendRect();
    [style.strip setFill];
    NSRectFill(legend);
    [style.grid setStroke];
    NSFrameRect(legend);
    s3g::clap_gui::drawCenteredTextToFit(
        @"NOTE ENTRY · 0–24 FRET · H / ~ HOLD · - / . / DELETE REST · R REATTACK ABOVE · [ / ] FRET - / + · RETURN NEXT ROW · TAB PLAYER",
        NSInsetRect(legend, 8.0, 3.0), labelAttrs);
}

- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    auto* instance = static_cast<Plugin*>(_plugin);
    BOOL parameterChanged = NO;
    if (instance) {
        const uint64_t revision = instance->parameterRevision.load(
            std::memory_order_acquire);
        if (revision != _observedParamRevision) {
            _observedParamRevision = revision;
            parameterChanged = YES;
            _factoryPresetIndex = s3g::processorStackFactoryPresetIndex(
                publishedParamsSnapshot(*instance));
            std::snprintf(_presetName, sizeof(_presetName), "%s",
                _factoryPresetIndex >= 0
                    ? s3g::processorStackFactoryPresetInfo(
                        static_cast<uint32_t>(_factoryPresetIndex)).name
                    : "CUSTOM");
        }
    }
    if (!instance || !self.window || !self.window.visible
        || self.window.miniaturized || [self isHiddenOrHasHiddenAncestor]
        || !s3g::clap_support::hostAppIsActive()) return;
    if (_scoreRandomFeedback >= 0) {
        const uint32_t action = static_cast<uint32_t>(_scoreRandomFeedback);
        const bool expired = [NSDate timeIntervalSinceReferenceDate]
            >= _scoreRandomFeedbackUntil;
        if (_page == 3u) {
            [self setNeedsDisplayInRect:NSInsetRect(
                stackScoreRandomActionButtonRect(action), -1.0, -1.0)];
        }
        if (expired) _scoreRandomFeedback = -1;
    }
    if (parameterChanged || _page != 3u) {
        [self setNeedsDisplay:YES];
        return;
    }

    int32_t capturedSection = -1;
    int32_t capturedRow = -1;
    int32_t capturedSlot = -1;
    for (uint32_t attempt = 0u; attempt < 4u; ++attempt) {
        const uint64_t before = instance->scoreVisualRevision.load(
            std::memory_order_acquire);
        if ((before & 1u) != 0u) continue;
        const int32_t section = instance->scoreVisualSection.load(
            std::memory_order_relaxed);
        const int32_t row = instance->scoreVisualRow.load(
            std::memory_order_relaxed);
        const int32_t slot = instance->scoreVisualSlot.load(
            std::memory_order_relaxed);
        const uint64_t after = instance->scoreVisualRevision.load(
            std::memory_order_acquire);
        if (before != after || (after & 1u) != 0u) continue;
        _observedScoreVisualRevision = after;
        capturedSection = section;
        capturedRow = row;
        capturedSlot = slot;
        break;
    }

    const int32_t nextSection = _scoreVisualPrimed
        ? _pendingScoreSection : _presentedScoreSection;
    const int32_t nextRow = _scoreVisualPrimed
        ? _pendingScoreRow : _presentedScoreRow;
    const int32_t nextSlot = _scoreVisualPrimed
        ? _pendingScoreSlot : _presentedScoreSlot;
    _pendingScoreSection = capturedSection;
    _pendingScoreRow = capturedRow;
    _pendingScoreSlot = capturedSlot;
    _scoreVisualPrimed = YES;

    if (nextSection != _presentedScoreSection
        || nextRow != _presentedScoreRow) {
        if (_presentedScoreSection == static_cast<int32_t>(_scoreEditSection)
            && _presentedScoreRow >= 0) {
            [self setNeedsDisplayInRect:NSInsetRect(stackScoreRowRect(
                static_cast<uint32_t>(_presentedScoreRow)), -1.0, -1.0)];
        }
        if (nextSection == static_cast<int32_t>(_scoreEditSection)
            && nextRow >= 0) {
            [self setNeedsDisplayInRect:NSInsetRect(stackScoreRowRect(
                static_cast<uint32_t>(nextRow)), -1.0, -1.0)];
        }
        _presentedScoreSection = nextSection;
        _presentedScoreRow = nextRow;
    }
    if (nextSlot != _presentedScoreSlot) {
        if (_presentedScoreSlot >= 0) {
            [self setNeedsDisplayInRect:NSInsetRect(
                stackScoreArrangementSlotRect(
                    static_cast<uint32_t>(_presentedScoreSlot)),
                -1.0, -1.0)];
        }
        if (nextSlot >= 0) {
            [self setNeedsDisplayInRect:NSInsetRect(
                stackScoreArrangementSlotRect(
                    static_cast<uint32_t>(nextSlot)), -1.0, -1.0)];
        }
        _presentedScoreSlot = nextSlot;
    }
    [self setNeedsDisplayInRect:NSMakeRect(0.0, 0.0, kGuiWidth, 34.0)];
}

- (void)drawRect:(NSRect)dirty
{
    auto* instance = static_cast<Plugin*>(_plugin);
    if (!instance) return;
    s3g::clap_gui::Style style;
    [style.bg setFill];
    NSRectFill(dirty);
    NSDictionary* titleAttrs = s3g::clap_gui::softTitleAttrs();
    NSDictionary* labelAttrs = s3g::clap_gui::softLabelAttrs();
    NSDictionary* valueAttrs = s3g::clap_gui::softValueAttrs();
    const auto titleBand = s3g::clap_gui::encoderTitleBand(
        kGuiWidth, kGuiHeight);
    s3g::clap_gui::drawEncoderTitleBand(
        @"s3g PROCESSOR STACK",
        [NSString stringWithUTF8String:_presetName],
        s3g::clap_gui::peakDbText(
            instance->outputPeak.load(std::memory_order_relaxed)),
        titleBand, titleAttrs, labelAttrs, valueAttrs, style);

    const auto drawPanel = [&](NSString* name, const layout::Panel& panel) {
        const NSRect rect = s3g::clap_gui::cocoaRect(panel.frame);
        s3g::clap_gui::drawPanelFrame(rect.origin.x, rect.origin.y,
            rect.size.width, rect.size.height, style);
        s3g::clap_gui::drawPanelHeader(name, true, rect.origin.x,
            rect.origin.y, rect.size.width,
            s3g::gui_layout::kStandardMetrics.headerHeight,
            labelAttrs, style);
    };
    for (const auto& panel : kUiPanels) {
        if (panel.page != _page && panel.page != kAllPages) continue;
        drawPanel([NSString stringWithUTF8String:panel.name], panel.panel);
    }

    static NSString* const pageLabels[4] {
        @"PLAY", @"RIG A", @"RIG B", @"SCORE",
    };
    const NSRect outputHeader = s3g::clap_gui::cocoaRect(
        kOutputPanel.frame);
    for (uint8_t page = 0u; page < 4u; ++page) {
        s3g::clap_gui::drawHeaderButton(stackPageButtonRect(page),
            outputHeader, pageLabels[page], page == _page,
            valueAttrs, style);
    }
    if (_page == 2u) {
        const NSRect linksHeader = s3g::clap_gui::cocoaRect(
            kLinksPanel.frame);
        s3g::clap_gui::drawHeaderActionButton(stackCopyButtonRect(0u),
            linksHeader, @"COPY A > B", valueAttrs, style);
        s3g::clap_gui::drawHeaderActionButton(stackCopyButtonRect(1u),
            linksHeader, @"COPY B > A", valueAttrs, style);
    }
    if (_page == 3u) {
        const NSRect timingHeader = s3g::clap_gui::cocoaRect(
            kScoreTimingPanel.frame);
        static NSString* const labels[kStackScoreRandomActionCount] {
            @"FORM", @"LEAD", @"RIFF", @"LOCKS", @"THIN",
        };
        const NSTimeInterval now = [NSDate timeIntervalSinceReferenceDate];
        for (uint32_t action = 0u;
             action < kStackScoreRandomActionCount; ++action) {
            const NSRect button = stackScoreRandomActionButtonRect(action);
            const bool pressed = _scoreRandomFeedback
                    == static_cast<int>(action)
                && now < _scoreRandomFeedbackUntil;
            if (!pressed) {
                s3g::clap_gui::drawHeaderActionButton(button,
                    timingHeader, labels[action], valueAttrs, style);
                continue;
            }
            [[NSColor colorWithSRGBRed:0.025 green:0.15
                blue:0.16 alpha:0.98] setFill];
            NSRectFill(button);
            [style.accent setStroke];
            NSFrameRect(button);
            s3g::clap_gui::drawCenteredTextToFit(labels[action],
                NSInsetRect(button, 2.0, 1.0), labelAttrs);
        }
    }

    for (const auto& row : kUiRows) {
        if (row.page != _page && row.page != kAllPages) continue;
        const double value = paramValue(*instance, row.id);
        char text[64] {};
        paramsValueToText(&instance->plugin,
            row.id, value, text, sizeof(text));
        NSString* label = [NSString stringWithUTF8String:row.label];
        NSString* display = [NSString stringWithUTF8String:text];
        if (isUiToggleParam(row.id)) {
            s3g::clap_gui::drawToggle(label, value >= 0.5, row.y,
                labelAttrs, valueAttrs, style,
                s3g::gui_layout::processorLabelX(row.panelX),
                s3g::gui_layout::processorControlX(row.panelX), 64.0);
        } else if (isUiMenuParam(row.id)) {
            s3g::clap_gui::drawProcessorMenu(label, display, row.y,
                row.panelX, row.panelWidth,
                labelAttrs, valueAttrs, style);
        } else {
            s3g::clap_gui::drawProcessorSlider(label, display,
                static_cast<CGFloat>(uiNormalizedValue(row.id, value)),
                row.y, row.panelX, row.panelWidth,
                labelAttrs, valueAttrs, style);
        }
    }
    if (_page == 0u) {
        drawStackPatternMultislider(0u, *instance,
            _dragPatternPlayer, _dragPatternStep, valueAttrs, style);
        drawStackPatternMultislider(1u, *instance,
            _dragPatternPlayer, _dragPatternStep, valueAttrs, style);
    }
    if (_page == 3u) {
        [self drawScore:labelAttrs values:valueAttrs style:style];
    }
    [self drawOpenMenu:valueAttrs style:style];
}

- (void)updateDraggedParam:(NSPoint)point
{
    if (_dragParam <= 0) return;
    const clap_id id = static_cast<clap_id>(_dragParam);
    const auto row = std::find_if(kUiRows.begin(), kUiRows.end(),
        [=](const StackUiRow& candidate) {
            return candidate.id == id
                && (candidate.page == _page
                    || candidate.page == kAllPages);
        });
    if (row == kUiRows.end()) return;
    const double controlX = s3g::gui_layout::processorControlX(row->panelX);
    const double trackWidth =
        s3g::gui_layout::processorTrackWidth(row->panelWidth);
    const double normalized = std::clamp(
        (point.x - controlX) / trackWidth, 0.0, 1.0);
    auto* instance = static_cast<Plugin*>(_plugin);
    queueGuiParamValue(*instance, id,
        uiValueFromNormalized(id, normalized));
    [self setNeedsDisplay:YES];
}

- (void)updateDraggedPattern:(NSPoint)point
{
    if (_dragPatternPlayer < 0 || _dragPatternPlayer > 1) return;
    const uint8_t player = static_cast<uint8_t>(_dragPatternPlayer);
    const int nextStep = stackPatternStepAtX(player, point.x);
    auto* instance = static_cast<Plugin*>(_plugin);
    if (!instance) return;
    if (nextStep >= 0 && nextStep != _dragPatternStep) {
        if (_dragPatternStep >= 0) {
            queueGuiParamGestureEnd(*instance, stackPatternStepParam(
                player, static_cast<uint32_t>(_dragPatternStep)));
        }
        _dragPatternStep = nextStep;
        queueGuiParamGestureBegin(*instance, stackPatternStepParam(
            player, static_cast<uint32_t>(_dragPatternStep)));
    }
    if (_dragPatternStep < 0) return;
    const NSRect field = stackPatternFieldRect(player);
    const double normalized = std::clamp(
        (NSMaxY(field) - point.y) / field.size.height, 0.0, 1.0);
    const clap_id id = stackPatternStepParam(
        player, static_cast<uint32_t>(_dragPatternStep));
    queueGuiParamValue(*instance, id,
        uiValueFromNormalized(id, normalized));
    [self markCustomPreset];
    [self setNeedsDisplay:YES];
}

- (void)rightMouseDown:(NSEvent*)event
{
    if (_page == 0u) {
        const NSPoint point = [self convertPoint:event.locationInWindow
            fromView:nil];
        auto* instance = static_cast<Plugin*>(_plugin);
        if (!instance) return;
        for (uint8_t player = 0u; player < 2u; ++player) {
            if (!NSPointInRect(point, stackPatternFieldRect(player))) {
                continue;
            }
            const int step = stackPatternStepAtX(player, point.x);
            if (step < 0) return;
            const clap_id id = stackPatternStepParam(
                player, static_cast<uint32_t>(step));
            queueGuiParamGesture(*instance, id,
                static_cast<double>(s3g::kProcessorStackArpRest));
            _dragPatternPlayer = -1;
            _dragPatternStep = -1;
            [self markCustomPreset];
            [self setNeedsDisplay:YES];
            return;
        }
        [super rightMouseDown:event];
        return;
    }
    if (_page != 3u) {
        [super rightMouseDown:event];
        return;
    }
    const NSPoint point = [self convertPoint:event.locationInWindow
        fromView:nil];
    uint32_t player = 0u;
    uint32_t slot = 0u;
    uint32_t row = 0u;
    if (!stackScoreLockAtPoint(point, player, slot, row)) {
        [super rightMouseDown:event];
        return;
    }
    _scoreSelectedPlayer = player;
    _scoreSelectedRow = row;
    _scoreLockPlayer = player;
    _scoreLockSlot = slot;
    _scoreLockRow = row;
    _scorePendingFret = -1;
    [self.window makeFirstResponder:self];
    NSMenu* menu = [self scoreLockMenuForPlayer:player row:row slot:slot];
    if (menu) [NSMenu popUpContextMenu:menu withEvent:event forView:self];
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    auto* instance = static_cast<Plugin*>(_plugin);
    if (!instance) return;
    const auto titleBand = s3g::clap_gui::encoderTitleBand(
        kGuiWidth, kGuiHeight);
    if (_openMenu != CLAP_INVALID_ID) {
        const int hit = s3g::clap_gui::dropdownHitIndex(point,
            [self openMenuRect], 18.0, _menuItemCount);
        if (hit >= 0) {
            if (_openMenu == kFactoryPresetMenuId) {
                [self applyFactoryPreset:hit];
            } else {
                const clap_id menuParam = _openMenu;
                const auto* def = paramDef(menuParam);
                queueGuiParamGesture(*instance, menuParam,
                    (def ? def->minimum : 0.0) + static_cast<double>(hit));
                if (menuParam != kMidiReceiveParamId
                    && menuParam != kArpHostSyncParamId
                    && menuParam != kScoreEnableParamId
                    && menuParam != kScoreRateParamId
                    && menuParam != kScoreLengthParamId
                    && menuParam != kScoreBSourceParamId) {
                    [self markCustomPreset];
                }
            }
        }
        _openMenu = CLAP_INVALID_ID;
        _hoverMenuItem = -1;
        _menuItemCount = 0u;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.presetMenu))) {
        _openMenu = kFactoryPresetMenuId;
        _hoverMenuItem = -1;
        _menuItemCount = s3g::kProcessorStackFactoryPresetCount;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.loadButton))) {
        NSString* name = nil;
        if (s3g::clap_gui::loadPluginStatePresetPreservingParam(
                &instance->plugin, @"Processor Stack", kOutputParamId,
                &name)) {
            [self markCustomPreset];
            std::snprintf(_presetName, sizeof(_presetName), "%s",
                name ? [name UTF8String] : "CUSTOM");
        } else {
            NSBeep();
        }
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.saveButton))) {
        NSString* name = nil;
        if (s3g::clap_gui::savePluginStatePreset(
                &instance->plugin, @"Processor Stack", &name)) {
            std::snprintf(_presetName, sizeof(_presetName), "%s",
                name ? [name UTF8String] : "CUSTOM");
        } else {
            NSBeep();
        }
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.randomButton))) {
        if (queueGuiParams(*instance,
                safeRandomParams(*instance, arc4random()))) {
            [self markCustomPreset];
        } else {
            NSBeep();
        }
        [self setNeedsDisplay:YES];
        return;
    }

    for (uint8_t page = 0u; page < 4u; ++page) {
        if (!NSPointInRect(point, stackPageButtonRect(page))) continue;
        if (page == 3u && _page != 3u) {
            _scoreVisualPrimed = NO;
            _pendingScoreSection = _pendingScoreRow = _pendingScoreSlot = -1;
            _presentedScoreSection = _presentedScoreRow
                = _presentedScoreSlot = -1;
        }
        _page = page;
        _dragParam = -1;
        _dragPatternPlayer = -1;
        _dragPatternStep = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    if (_page == 2u) {
        for (uint8_t copy = 0u; copy < 2u; ++copy) {
            if (!NSPointInRect(point, stackCopyButtonRect(copy))) continue;
            if (queueRigCopy(*instance, copy == 0u)) {
                [self markCustomPreset];
            } else {
                NSBeep();
            }
            [self setNeedsDisplay:YES];
            return;
        }
    }

    if (_page == 3u) {
        for (uint32_t action = 0u;
             action < kStackScoreRandomActionCount; ++action) {
            if (!NSPointInRect(point,
                    stackScoreRandomActionButtonRect(action))) continue;
            const auto current = scoreProgramSnapshot(*instance);
            const auto scoreParams = publishedParamsSnapshot(*instance);
            s3g::ProcessorStackScoreProgram generated = current;
            const uint32_t seed = arc4random();
            switch (action) {
            case 0u:
                generated = s3g::generateProcessorStackScore(
                    seed, scoreParams.scale, scoreParams.scaleB);
                // FORM changes the complete tablature and arrangement but
                // keeps detailed control sequencing authored in the lanes.
                generated.locks = current.locks;
                _scoreEditSection = 0u;
                break;
            case 1u:
                generated = s3g::randomizeProcessorStackScoreLead(
                    current, _scoreEditSection, seed,
                    scoreParams.scale, scoreParams.scaleB);
                break;
            case 2u:
                generated = s3g::randomizeProcessorStackScoreRiff(
                    current, _scoreEditSection, seed,
                    scoreParams.scale, scoreParams.scaleB);
                break;
            case 3u:
                // LOCKS changes only the two per-player lane values in the
                // selected section. Notes, form, and base parameters survive.
                generated = s3g::randomizeProcessorStackScoreLocks(
                    current, _scoreEditSection, seed);
                break;
            case 4u:
                // THIN removes attack events across every section and player.
                // Arrangement slots and the two parameter-lock lanes survive.
                generated = s3g::randomizeProcessorStackScoreThin(
                    current, seed);
                break;
            default:
                break;
            }
            storeScoreProgram(*instance, generated, true);
            _scoreSelectedPlayer = 0u;
            _scoreSelectedString = 0u;
            _scoreSelectedRow = 0u;
            _scorePendingFret = -1;
            _scoreRandomFeedback = static_cast<int>(action);
            _scoreRandomFeedbackUntil =
                [NSDate timeIntervalSinceReferenceDate] + 0.22;
            [self.window makeFirstResponder:self];
            [self setNeedsDisplay:YES];
            return;
        }
        for (uint32_t section = 0u;
             section < s3g::kProcessorStackScoreSectionCount; ++section) {
            if (!NSPointInRect(point,
                    stackScoreSectionButtonRect(section))) continue;
            _scoreEditSection = section;
            _scorePendingFret = -1;
            [[self window] makeFirstResponder:self];
            [self setNeedsDisplay:YES];
            return;
        }
        for (uint32_t slot = 0u;
             slot < s3g::kProcessorStackScoreArrangementSlots; ++slot) {
            if (!NSPointInRect(point,
                    stackScoreArrangementSlotRect(slot))) continue;
            const uint32_t current = std::min<uint32_t>(
                instance->scoreArrangement[slot].load(
                    std::memory_order_acquire),
                s3g::kProcessorStackScoreSectionCount - 1u);
            const bool backwards = (event.modifierFlags
                & NSEventModifierFlagShift) != 0u;
            const uint32_t next = backwards
                ? (current + s3g::kProcessorStackScoreSectionCount - 1u)
                    % s3g::kProcessorStackScoreSectionCount
                : (current + 1u) % s3g::kProcessorStackScoreSectionCount;
            instance->scoreArrangement[slot].store(
                next, std::memory_order_release);
            instance->scoreRevision.fetch_add(
                1u, std::memory_order_acq_rel);
            notifyScoreChanged(*instance);
            [self setNeedsDisplay:YES];
            return;
        }
        uint32_t lockPlayer = 0u;
        uint32_t lockSlot = 0u;
        uint32_t lockRow = 0u;
        if (stackScoreLockAtPoint(
                point, lockPlayer, lockSlot, lockRow)) {
            _scoreSelectedPlayer = lockPlayer;
            _scoreSelectedRow = lockRow;
            _scoreLockPlayer = lockPlayer;
            _scoreLockSlot = lockSlot;
            _scoreLockRow = lockRow;
            _scorePendingFret = -1;
            const auto lock = stackScoreLockCell(*instance,
                _scoreEditSection, lockRow, lockPlayer, lockSlot);
            if (event.clickCount >= 2) {
                setScoreLock(*instance, _scoreEditSection, lockRow,
                    lockPlayer, lockSlot,
                    s3g::ProcessorStackScoreLockControl::None, 0.0);
                _dragScoreLock = NO;
            } else if (lock.control != static_cast<uint8_t>(
                    s3g::ProcessorStackScoreLockControl::None)) {
                _dragScoreLock = YES;
                _scoreLockDragStartPoint = point;
                _scoreLockDragStartNormalized =
                    s3g::processorStackScoreLockNormalized(lock);
            }
            [self.window makeFirstResponder:self];
            [self setNeedsDisplayInRect:NSInsetRect(stackScoreLockRect(
                lockPlayer, lockSlot, lockRow), -1.0, -1.0)];
            return;
        }
        uint32_t player = 0u;
        uint32_t string = 0u;
        uint32_t row = 0u;
        if (stackScoreCellAtPoint(point, player, string, row)) {
            _scoreSelectedPlayer = player;
            _scoreSelectedString = string;
            _scoreSelectedRow = row;
            _scorePendingFret = -1;
            if (event.clickCount >= 2) {
                setScoreCell(*instance, _scoreEditSection, row,
                    player, string, s3g::kProcessorStackScoreRest);
            }
            [[self window] makeFirstResponder:self];
            [self setNeedsDisplay:YES];
            return;
        }
    }

    if (_page == 0u) {
        for (uint8_t player = 0u; player < 2u; ++player) {
            if (!NSPointInRect(point, stackPatternFieldRect(player))) {
                continue;
            }
            const int step = stackPatternStepAtX(player, point.x);
            if (step < 0) return;
            const clap_id id = stackPatternStepParam(
                player, static_cast<uint32_t>(step));
            double defaultValue = 0.0;
            if (s3g::clap_gui::sliderDoubleClickDefault(
                    event, &instance->plugin, id, &defaultValue)) {
                queueGuiParamGesture(*instance, id, defaultValue);
                _dragPatternPlayer = -1;
                _dragPatternStep = -1;
            } else {
                _dragPatternPlayer = static_cast<int>(player);
                _dragPatternStep = step;
                queueGuiParamGestureBegin(*instance, id);
                [self updateDraggedPattern:point];
            }
            [self markCustomPreset];
            [self setNeedsDisplay:YES];
            return;
        }
    }

    for (const auto& row : kUiRows) {
        if (row.page != _page && row.page != kAllPages) continue;
        const NSRect hit = NSMakeRect(
            row.panelX + s3g::gui_layout::kStandardMetrics.hitInset,
            row.y - 9.0,
            row.panelWidth
                - s3g::gui_layout::kStandardMetrics.hitInset * 2.0,
            s3g::gui_layout::kStandardMetrics.hitHeight);
        if (!NSPointInRect(point, hit)) continue;
        if (isUiToggleParam(row.id)) {
            const double next = paramValue(*instance, row.id) >= 0.5
                ? 0.0 : 1.0;
            queueGuiParamGesture(*instance, row.id, next);
            [self markCustomPreset];
            [self setNeedsDisplay:YES];
            return;
        }
        if (isUiMenuParam(row.id)) {
            _openMenu = row.id;
            _hoverMenuItem = -1;
            _menuItemCount = uiMenuItemCount(row.id);
            [self setNeedsDisplay:YES];
            return;
        }
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
                event, &instance->plugin, row.id, &defaultValue)) {
            queueGuiParamGesture(*instance, row.id, defaultValue);
            _dragParam = -1;
        } else {
            _dragParam = static_cast<int>(row.id);
            queueGuiParamGestureBegin(*instance, row.id);
            [self updateDraggedParam:point];
        }
        if (row.id != kOutputParamId && row.id != kMidiReceiveParamId
            && row.id != kArpHostSyncParamId
            && row.id != kScoreEnableParamId
            && row.id != kScoreRateParamId
            && row.id != kScoreGateParamId
            && row.id != kScoreLengthParamId
            && row.id != kScoreBSourceParamId) {
            [self markCustomPreset];
        }
        return;
    }
}

- (void)keyDown:(NSEvent*)event
{
    if (_page != 3u) {
        [super keyDown:event];
        return;
    }
    auto* instance = static_cast<Plugin*>(_plugin);
    if (!instance) return;
    NSString* key = event.charactersIgnoringModifiers.lowercaseString;
    const auto modifiers = event.modifierFlags
        & (NSEventModifierFlagCommand | NSEventModifierFlagControl
            | NSEventModifierFlagOption);
    const bool trackerCopyModifier = modifiers == NSEventModifierFlagControl
        || modifiers == NSEventModifierFlagCommand;
    if (trackerCopyModifier && [key isEqualToString:@"c"]) {
        NSMutableString* text = [NSMutableString string];
        for (uint32_t row = 0u;
             row < s3g::kProcessorStackScoreRowsPerSection; ++row) {
            for (uint32_t player = 0u;
                 player < s3g::kProcessorStackScorePlayerCount; ++player) {
                for (uint32_t string = 0u;
                     string < s3g::kProcessorStackScoreStringCount;
                     ++string) {
                    const size_t index = s3g::processorStackScoreCellIndex(
                        _scoreEditSection, row, player, string);
                    const int fret = instance->scoreCells[index].load(
                        std::memory_order_acquire);
                    [text appendString:fret == s3g::kProcessorStackScoreHold
                        ? @"~" : fret >= 0
                            ? [NSString stringWithFormat:@"%d", fret] : @"-"];
                    if (player != 1u || string + 1u
                            < s3g::kProcessorStackScoreStringCount) {
                        [text appendString:@"\t"];
                    }
                }
            }
            if (row + 1u < s3g::kProcessorStackScoreRowsPerSection) {
                [text appendString:@"\n"];
            }
        }
        NSPasteboard* pasteboard = NSPasteboard.generalPasteboard;
        [pasteboard clearContents];
        [pasteboard setString:text forType:NSPasteboardTypeString];
        return;
    }
    if (trackerCopyModifier && [key isEqualToString:@"v"]) {
        NSString* source = [NSPasteboard.generalPasteboard
            stringForType:NSPasteboardTypeString];
        if (!source) return;
        auto program = scoreProgramSnapshot(*instance);
        NSArray<NSString*>* lines = [source componentsSeparatedByCharactersInSet:
            NSCharacterSet.newlineCharacterSet];
        uint32_t row = 0u;
        for (NSString* line in lines) {
            if (row >= s3g::kProcessorStackScoreRowsPerSection) break;
            NSArray<NSString*>* raw = [line componentsSeparatedByCharactersInSet:
                NSCharacterSet.whitespaceCharacterSet];
            NSMutableArray<NSString*>* tokens = [NSMutableArray array];
            for (NSString* token in raw) {
                if (token.length > 0u) [tokens addObject:token];
            }
            if (tokens.count == 0u) continue;
            const NSUInteger limit = std::min<NSUInteger>(tokens.count,
                s3g::kProcessorStackScorePlayerCount
                    * s3g::kProcessorStackScoreStringCount);
            for (NSUInteger column = 0u; column < limit; ++column) {
                NSString* token = tokens[column];
                int fret = s3g::kProcessorStackScoreRest;
                if ([token isEqualToString:@"~"]
                    || [token caseInsensitiveCompare:@"h"]
                        == NSOrderedSame
                    || [token isEqualToString:@"="]) {
                    fret = s3g::kProcessorStackScoreHold;
                } else if (![token isEqualToString:@"-"]
                    && ![token isEqualToString:@"—"]
                    && ![token isEqualToString:@"."]) {
                    NSScanner* scanner = [NSScanner scannerWithString:token];
                    NSInteger parsed = -1;
                    if (![scanner scanInteger:&parsed] || !scanner.isAtEnd
                        || parsed < s3g::kProcessorStackScoreMinimumFret
                        || parsed > s3g::kProcessorStackScoreMaximumFret) {
                        continue;
                    }
                    fret = static_cast<int>(parsed);
                }
                const uint32_t player = static_cast<uint32_t>(column)
                    / s3g::kProcessorStackScoreStringCount;
                const uint32_t string = static_cast<uint32_t>(column)
                    % s3g::kProcessorStackScoreStringCount;
                s3g::setProcessorStackScoreCell(program,
                    _scoreEditSection, row, player, string, fret);
            }
            ++row;
        }
        storeScoreProgram(*instance, program, true);
        [self setNeedsDisplay:YES];
        return;
    }
    if (modifiers != 0u) {
        [super keyDown:event];
        return;
    }

    const auto advanceRow = [&]() {
        _scoreSelectedRow = (_scoreSelectedRow + 1u)
            % s3g::kProcessorStackScoreRowsPerSection;
        _scorePendingFret = -1;
    };
    if (event.keyCode == 123u || event.keyCode == 124u) {
        int column = static_cast<int>(_scoreSelectedPlayer
            * s3g::kProcessorStackScoreStringCount + _scoreSelectedString);
        column += event.keyCode == 123u ? -1 : 1;
        const int columnCount = static_cast<int>(
            s3g::kProcessorStackScorePlayerCount
                * s3g::kProcessorStackScoreStringCount);
        column = (column + columnCount) % columnCount;
        _scoreSelectedPlayer = static_cast<uint32_t>(column)
            / s3g::kProcessorStackScoreStringCount;
        _scoreSelectedString = static_cast<uint32_t>(column)
            % s3g::kProcessorStackScoreStringCount;
        _scorePendingFret = -1;
    } else if (event.keyCode == 125u || event.keyCode == 126u) {
        const int direction = event.keyCode == 125u ? 1 : -1;
        const int rows = static_cast<int>(
            s3g::kProcessorStackScoreRowsPerSection);
        _scoreSelectedRow = static_cast<uint32_t>(
            (static_cast<int>(_scoreSelectedRow) + direction + rows) % rows);
        _scorePendingFret = -1;
    } else if (event.keyCode == 48u) {
        _scoreSelectedPlayer = 1u - _scoreSelectedPlayer;
        _scorePendingFret = -1;
    } else if (key.length == 1u
        && [key characterAtIndex:0u] >= '0'
        && [key characterAtIndex:0u] <= '9') {
        const int digit = static_cast<int>([key characterAtIndex:0u] - '0');
        const bool append = _scorePendingFret >= 0
            && event.timestamp - _scorePendingTime < 0.8;
        const int candidate = append ? _scorePendingFret * 10 + digit : digit;
        if (candidate <= s3g::kProcessorStackScoreMaximumFret) {
            setScoreCell(*instance, _scoreEditSection, _scoreSelectedRow,
                _scoreSelectedPlayer, _scoreSelectedString, candidate);
            if (append) advanceRow();
            else {
                _scorePendingFret = candidate;
                _scorePendingTime = event.timestamp;
            }
        } else NSBeep();
    } else if ([key isEqualToString:@"-"]
        || [key isEqualToString:@"."]
        || event.keyCode == 51u || event.keyCode == 117u) {
        setScoreCell(*instance, _scoreEditSection, _scoreSelectedRow,
            _scoreSelectedPlayer, _scoreSelectedString,
            s3g::kProcessorStackScoreRest);
        advanceRow();
    } else if ([key isEqualToString:@"h"]
        || [key isEqualToString:@"~"]
        || [key isEqualToString:@"="]) {
        setScoreCell(*instance, _scoreEditSection, _scoreSelectedRow,
            _scoreSelectedPlayer, _scoreSelectedString,
            s3g::kProcessorStackScoreHold);
        advanceRow();
    } else if ([key isEqualToString:@"r"]) {
        int repeated = s3g::kProcessorStackScoreRest;
        for (uint32_t distance = 1u;
             distance <= s3g::kProcessorStackScoreRowsPerSection;
             ++distance) {
            const uint32_t sourceRow = (_scoreSelectedRow
                + s3g::kProcessorStackScoreRowsPerSection - distance)
                % s3g::kProcessorStackScoreRowsPerSection;
            const size_t sourceIndex = s3g::processorStackScoreCellIndex(
                _scoreEditSection, sourceRow, _scoreSelectedPlayer,
                _scoreSelectedString);
            repeated = instance->scoreCells[sourceIndex].load(
                std::memory_order_acquire);
            if (repeated != s3g::kProcessorStackScoreHold) break;
        }
        if (repeated == s3g::kProcessorStackScoreHold) {
            repeated = s3g::kProcessorStackScoreRest;
        }
        setScoreCell(*instance, _scoreEditSection, _scoreSelectedRow,
            _scoreSelectedPlayer, _scoreSelectedString, repeated);
        advanceRow();
    } else if ([key isEqualToString:@"["]
        || [key isEqualToString:@"]"]) {
        const size_t index = s3g::processorStackScoreCellIndex(
            _scoreEditSection, _scoreSelectedRow, _scoreSelectedPlayer,
            _scoreSelectedString);
        int fret = instance->scoreCells[index].load(
            std::memory_order_acquire);
        if (fret < 0) fret = 0;
        fret += [key isEqualToString:@"["] ? -1 : 1;
        setScoreCell(*instance, _scoreEditSection, _scoreSelectedRow,
            _scoreSelectedPlayer, _scoreSelectedString, fret);
        _scorePendingFret = -1;
    } else if ([key isEqualToString:@"\r"]) {
        advanceRow();
    } else {
        [super keyDown:event];
        return;
    }
    [self setNeedsDisplay:YES];
}

- (void)mouseDragged:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:
        [event locationInWindow] fromView:nil];
    if (_dragScoreLock) {
        [self updateDraggedScoreLock:point];
    } else if (_dragPatternPlayer >= 0) {
        [self updateDraggedPattern:point];
    } else if (_dragParam > 0) {
        [self updateDraggedParam:point];
    }
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    if (_dragParam > 0) {
        queueGuiParamGestureEnd(*static_cast<Plugin*>(_plugin),
            static_cast<clap_id>(_dragParam));
    }
    if (_dragPatternPlayer >= 0 && _dragPatternStep >= 0) {
        queueGuiParamGestureEnd(*static_cast<Plugin*>(_plugin),
            stackPatternStepParam(static_cast<uint8_t>(_dragPatternPlayer),
                static_cast<uint32_t>(_dragPatternStep)));
    }
    _dragParam = -1;
    _dragPatternPlayer = -1;
    _dragPatternStep = -1;
    _dragScoreLock = NO;
}

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    [[self window] setAcceptsMouseMovedEvents:YES];
}

- (void)mouseMoved:(NSEvent*)event
{
    if (_openMenu == CLAP_INVALID_ID) return;
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    const int hover = s3g::clap_gui::dropdownHitIndex(
        point, [self openMenuRect], 18.0, _menuItemCount);
    if (hover != _hoverMenuItem) {
        _hoverMenuItem = hover;
        [self setNeedsDisplay:YES];
    }
}

@end

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
    instance->guiView = [[S3GProcessorStackView alloc] initWithPlugin:instance];
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
    if (!instance || !instance->guiView) return;
    instance->guiVisible = false;
    [static_cast<S3GProcessorStackView*>(instance->guiView) stopRefreshTimer];
    s3g::clap_gui::destroyResponsiveViewport(
        instance->guiViewport, instance->guiView);
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
    if (!instance->guiView || !s3g::clap_gui::setResponsiveViewportHidden(
            instance->guiViewport, false)) return false;
    instance->guiVisible = true;
    [static_cast<S3GProcessorStackView*>(instance->guiView) startRefreshTimer];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance->guiView) return false;
    instance->guiVisible = false;
    [static_cast<S3GProcessorStackView*>(instance->guiView) stopRefreshTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(
        instance->guiViewport, true);
}

const clap_plugin_gui_t guiExt {
    guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy,
    guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints,
    guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient,
    guiSuggestTitle, guiShow, guiHide
};

#endif

const void* getExtension(const clap_plugin_t*, const char* id)
{
    if (!id) return nullptr;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &notePorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
    if (std::strcmp(id, CLAP_EXT_TAIL) == 0) return &tailExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SYNTHESIZER,
    CLAP_PLUGIN_FEATURE_STEREO,
    CLAP_PLUGIN_FEATURE_DISTORTION,
    nullptr
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.processor-stack",
    "s3g Processor Stack",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.9.0",
    "A dual-guitar string synthesizer with independent arpeggiators, distortion pedals, amplifiers, speakers, and governed microphone-feedback loops.",
    features
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* instance = new (std::nothrow) Plugin();
    if (!instance) return nullptr;
    instance->host = host;
    resetMidiExpressionState(*instance);
    storeScoreProgram(*instance,
        s3g::makeDefaultProcessorStackScoreProgram(), false);
    for (const auto& def : kParamDefs) {
        applyParam(*instance, def.id, def.defaultValue);
    }
    for (const auto& def : kParamDefs) {
        publishParam(*instance, def.id, rawParamValue(*instance, def.id));
    }
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
