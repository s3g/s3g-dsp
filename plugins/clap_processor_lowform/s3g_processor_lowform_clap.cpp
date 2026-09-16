#include "../common/s3g_windows_processor_float_mode.h"
#include "s3g_processor_lowform.h"
#include "s3g_processor_lowform_presets.h"
#include "s3g_processor_stack.h"
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

#if defined(__APPLE__) && !defined(S3G_ENABLE_VSTGUI_CANVAS_GUI)
#import <Cocoa/Cocoa.h>
#include "../common/s3g_clap_macos.h"
#include "../common/s3g_cocoa_gui.h"
#include "../common/s3g_gui_layout.h"
#endif

#if defined(S3G_ENABLE_VSTGUI_CANVAS_GUI)
#include "../common/s3g_stereo_processor_drawing.h"
#include "../common/s3g_clap_gui_param_queue.h"
#define S3G_STEREO_KIND 2
#endif

namespace {
#if defined(S3G_ENABLE_VSTGUI_CANVAS_GUI)
constexpr const char* portablePresetDirectory="Processor Lowform";
struct Plugin;
void destroyPortableGui(Plugin&);
#endif

// Keep the pre-release SBW2 payload tag so test presets made before the
// Processor Lowform rename remain loadable.
constexpr uint32_t kStateMagic = 0x32574253u;
constexpr uint32_t kStateVersion = 9u;
constexpr uint32_t kRaveStateVersion = 8u;
constexpr uint32_t kArticulationStateVersion = 7u;
constexpr uint32_t kAssignableMotionStateVersion = 6u;
constexpr uint32_t kExpressionStateVersion = 5u;
constexpr uint32_t kDirectControlsStateVersion = 4u;
constexpr uint32_t kMacroStateVersion = 3u;
constexpr uint32_t kModalLegacyStateVersion = 2u;
constexpr uint32_t kLegacyStateVersion = 1u;
constexpr uint32_t kLegacyParamCount = 61u;
constexpr uint32_t kPreviousParamCount = 64u;
constexpr uint32_t kExpressionParamCount = 65u;
constexpr uint32_t kAssignableMotionParamCount = 81u;
constexpr uint32_t kOutputChannels = 2u;
constexpr uint32_t kGuiWidth = 1100u;
constexpr uint32_t kGuiHeight = 720u;

enum ParamId : clap_id {
    kOutputParamId = 1u,
    kVoiceModeParamId,
    kGlideParamId,
    kFoundationWaveParamId,
    kFoundationOctaveParamId,
    kFoundationLevelParamId,
    kPitchPunchParamId,
    kPunchTimeParamId,
    kFoundationReturnParamId,
    kBodyEngineParamId,
    kBodyLevelParamId,
    kBodyControlAParamId,
    kBodyControlBParamId,
    kBodyWidthParamId,
    kTextureModeParamId,
    kTextureLevelParamId,
    kTextureColorParamId,
    kTextureTrackParamId,
    kFilterTypeParamId,
    kCutoffParamId,
    kResonanceParamId,
    kFilterDriveParamId,
    kKeyTrackParamId,
    kFoundationFilterParamId,
    kBodyFilterParamId,
    kTextureFilterParamId,
    kAttackParamId,
    kDecayParamId,
    kSustainParamId,
    kReleaseParamId,
    kFilterEnvelopeParamId,
    kFilterDecayParamId,
    kMotionClockParamId,
    kMod1ShapeParamId,
    kMod1RateParamId,
    kMod1DepthParamId,
    kMod1TargetParamId,
    kMod2ShapeParamId,
    kMod2RateParamId,
    kMod2DepthParamId,
    kMod2TargetParamId,
    kMod3ShapeParamId,
    kMod3RateParamId,
    kMod3DepthParamId,
    kMod3TargetParamId,
    kModalDriveParamId,
    kModWheelAmountParamId,
    kDynamicsBiteParamId,
    kTextureWidthParamId,
    kTubeParamId,
    kShredCircuitParamId,
    kShredParamId,
    kShredFeedbackParamId,
    kShredColorParamId,
    kShredMixParamId,
    kDynamicsParamId,
    kSaturationParamId,
    kClipParamId,
    kTiltParamId,
    kMaximizerParamId,
    kMidiReceiveParamId,
    kBodyControlCParamId,
    kBodyControlDParamId,
    kBodyControlEParamId,
    kExpressionModeParamId,
    kTransposeParamId,
    kArpPatternParamId,
    kArpScaleParamId,
    kArpSyncParamId,
    kArpRateParamId,
    kArpOctavesParamId,
    kArpGateParamId,
    kArpLengthParamId,
    kArpStep1ParamId,
    kArpStep2ParamId,
    kArpStep3ParamId,
    kArpStep4ParamId,
    kArpStep5ParamId,
    kArpStep6ParamId,
    kArpStep7ParamId,
    kArpStep8ParamId,
    kMod1SecondaryTargetParamId,
    kMod1SecondaryDepthParamId,
    kMod2SecondaryTargetParamId,
    kMod2SecondaryDepthParamId,
    kMod3SecondaryTargetParamId,
    kMod3SecondaryDepthParamId,
    kVelocityTargetParamId,
    kVelocityDepthParamId,
    kPressureTargetParamId,
    kPressureDepthParamId,
    kTimbreTargetParamId,
    kTimbreDepthParamId,
    kBodyDecayParamId,
    kTextureAttackParamId,
    kTextureDecayParamId,
    kArpAccent1ParamId,
    kArpAccent2ParamId,
    kArpAccent3ParamId,
    kArpAccent4ParamId,
    kArpAccent5ParamId,
    kArpAccent6ParamId,
    kArpAccent7ParamId,
    kArpAccent8ParamId,
    kArpGateMode1ParamId,
    kArpGateMode2ParamId,
    kArpGateMode3ParamId,
    kArpGateMode4ParamId,
    kArpGateMode5ParamId,
    kArpGateMode6ParamId,
    kArpGateMode7ParamId,
    kArpGateMode8ParamId,
    kArpOctave1ParamId,
    kArpOctave2ParamId,
    kArpOctave3ParamId,
    kArpOctave4ParamId,
    kArpOctave5ParamId,
    kArpOctave6ParamId,
    kArpOctave7ParamId,
    kArpOctave8ParamId,
};

constexpr uint32_t kParamCount = 120u;
constexpr uint32_t kPublishedParamCount = kParamCount + 1u;

struct ParamDef {
    clap_id id;
    const char* name;
    const char* module;
    double minimum;
    double maximum;
    double defaultValue;
    bool stepped;
};

constexpr std::array<ParamDef, kParamCount> kParamDefs {{
    { kOutputParamId, "Output", "Finish", -36.0, 6.0, -12.0, false },
    { kVoiceModeParamId, "Voice Mode", "Foundation", 0.0, 1.0, 1.0, true },
    { kGlideParamId, "Glide", "Foundation", 0.0, 2000.0, 28.0, false },
    { kFoundationWaveParamId, "Wave", "Foundation", 0.0, 2.0, 0.0, true },
    { kFoundationOctaveParamId, "Octave", "Foundation", -1.0, 0.0, 0.0, true },
    { kFoundationLevelParamId, "Level", "Foundation", 0.0, 1.30, 0.93, false },
    { kPitchPunchParamId, "Pitch Punch", "Foundation", -24.0, 24.0, 3.0, false },
    { kPunchTimeParamId, "Punch Time", "Foundation", 5.0, 500.0, 48.0, false },
    { kFoundationReturnParamId, "Foundation Return", "Foundation", 0.0, 1.0, 0.72, false },
    { kBodyEngineParamId, "Engine", "Body", 0.0, 7.0, 0.0, true },
    { kBodyLevelParamId, "Level", "Body", 0.0, 1.16, 0.62, false },
    { kBodyControlAParamId, "Engine Control A", "Body", 0.0, 1.0, 0.42, false },
    { kBodyControlBParamId, "Engine Control B", "Body", 0.0, 1.0, 0.48, false },
    { kBodyWidthParamId, "Width", "Body", 0.0, 1.0, 0.47, false },
    { kTextureModeParamId, "Mode", "Texture", 0.0, 4.0, 0.0, true },
    { kTextureLevelParamId, "Level", "Texture", 0.0, 1.0, 0.0, false },
    { kTextureColorParamId, "Color", "Texture", 0.0, 1.0, 0.52, false },
    { kTextureTrackParamId, "Pitch Track", "Texture", 0.0, 1.0, 0.72, false },
    { kFilterTypeParamId, "Type", "Shape", 0.0, 5.0, 2.0, true },
    { kCutoffParamId, "Cutoff", "Shape", 20.0, 20000.0, 820.0, false },
    { kResonanceParamId, "Resonance", "Shape", 0.0, 1.0, 0.18, false },
    { kFilterDriveParamId, "Drive", "Shape", 0.0, 1.0, 0.31, false },
    { kKeyTrackParamId, "Key Track", "Shape", 0.0, 1.0, 0.34, false },
    { kFoundationFilterParamId, "Foundation Route", "Shape", 0.0, 1.0, 0.12, false },
    { kBodyFilterParamId, "Body Route", "Shape", 0.0, 1.0, 1.0, false },
    { kTextureFilterParamId, "Texture Route", "Shape", 0.0, 1.0, 1.0, false },
    { kAttackParamId, "Attack", "Envelope", 0.0005, 2.0, 0.004, false },
    { kDecayParamId, "Decay", "Envelope", 0.005, 5.0, 0.22, false },
    { kSustainParamId, "Sustain", "Envelope", 0.0, 1.0, 0.78, false },
    { kReleaseParamId, "Release", "Envelope", 0.005, 8.0, 0.30, false },
    { kFilterEnvelopeParamId, "Filter Envelope", "Envelope", -6.0, 6.0, 1.4, false },
    { kFilterDecayParamId, "Filter Decay", "Envelope", 0.005, 5.0, 0.18, false },
    { kMotionClockParamId, "Clock", "Motion", 0.0, 1.0, 0.0, true },
    { kMod1ShapeParamId, "Shape", "Mod 1", 0.0, 6.0, 1.0, true },
    { kMod1RateParamId, "Rate", "Mod 1", 0.0, 1.0, 0.25, false },
    { kMod1DepthParamId, "Depth", "Mod 1", -1.0, 1.0, 0.0, false },
    { kMod1TargetParamId, "Target", "Mod 1", 0.0, 28.0, 7.0, true },
    { kMod2ShapeParamId, "Shape", "Mod 2", 0.0, 6.0, 2.0, true },
    { kMod2RateParamId, "Rate", "Mod 2", 0.0, 1.0, 0.47, false },
    { kMod2DepthParamId, "Depth", "Mod 2", -1.0, 1.0, 0.0, false },
    { kMod2TargetParamId, "Target", "Mod 2", 0.0, 28.0, 3.0, true },
    { kMod3ShapeParamId, "Shape", "Mod 3", 0.0, 6.0, 5.0, true },
    { kMod3RateParamId, "Rate", "Mod 3", 0.0, 1.0, 0.60, false },
    { kMod3DepthParamId, "Depth", "Mod 3", -1.0, 1.0, 0.0, false },
    { kMod3TargetParamId, "Target", "Mod 3", 0.0, 28.0, 5.0, true },
    { kModalDriveParamId, "Modal Drive", "Body/MODAL", 0.0, 1.0, 0.25, false },
    { kModWheelAmountParamId, "Mod Wheel Amount", "Motion", 0.0, 1.0, 0.75, false },
    { kDynamicsBiteParamId, "Bite", "Finish", 0.0, 1.0, 0.03, false },
    { kTextureWidthParamId, "Width", "Texture", 0.0, 1.0, 0.47, false },
    { kTubeParamId, "Tube", "Finish", 0.0, 1.0, 0.20, false },
    { kShredCircuitParamId, "Circuit", "Shred", 0.0, 7.0, 0.0, true },
    { kShredParamId, "Shred", "Shred", 0.0, 1.0, 0.06, false },
    { kShredFeedbackParamId, "Feedback", "Shred", 0.0, 1.0, 0.0, false },
    { kShredColorParamId, "Color", "Shred", 0.0, 1.0, 0.55, false },
    { kShredMixParamId, "Mix", "Shred", 0.0, 1.0, 0.0, false },
    { kDynamicsParamId, "Dynamics", "Finish", 0.0, 1.0, 0.26, false },
    { kSaturationParamId, "Saturation", "Finish", 0.0, 1.0, 0.12, false },
    { kClipParamId, "Clip", "Finish", 0.0, 1.0, 0.0, false },
    { kTiltParamId, "Tilt", "Finish", -1.0, 1.0, 0.0, false },
    { kMaximizerParamId, "Maximizer", "Finish", 0.0, 1.0, 0.24, false },
    { kMidiReceiveParamId, "MIDI Receive", "Routing", 0.0, 16.0, 0.0, true },
    { kBodyControlCParamId, "Engine Control C", "Body", 0.0, 1.0, 0.40, false },
    { kBodyControlDParamId, "Engine Control D", "Body", 0.0, 1.0, 0.24, false },
    { kBodyControlEParamId, "Engine Control E", "Body", 0.0, 1.0, 0.59, false },
    { kExpressionModeParamId, "Expression Mode", "Routing", 0.0, 1.0, 0.0, true },
    { kTransposeParamId, "Transpose", "Routing", -24.0, 24.0, 0.0, true },
    { kArpPatternParamId, "Arp Pattern", "Arpeggiator", 0.0, 6.0, 0.0, true },
    { kArpScaleParamId, "Arp Scale", "Arpeggiator", 0.0, 13.0, 1.0, true },
    { kArpSyncParamId, "Arp Sync", "Arpeggiator", 0.0, 1.0, 0.0, true },
    { kArpRateParamId, "Arp Rate", "Arpeggiator", 0.0, 8.0, 2.0, true },
    { kArpOctavesParamId, "Arp Octaves", "Arpeggiator", 1.0, 4.0, 2.0, true },
    { kArpGateParamId, "Arp Gate", "Arpeggiator", 0.05, 1.0, 0.62, false },
    { kArpLengthParamId, "Arp Length", "Arpeggiator", 1.0, 8.0, 8.0, true },
    { kArpStep1ParamId, "Arp Step 1", "Arpeggiator", -9.0, 15.0, 0.0, true },
    { kArpStep2ParamId, "Arp Step 2", "Arpeggiator", -9.0, 15.0, 1.0, true },
    { kArpStep3ParamId, "Arp Step 3", "Arpeggiator", -9.0, 15.0, 2.0, true },
    { kArpStep4ParamId, "Arp Step 4", "Arpeggiator", -9.0, 15.0, 4.0, true },
    { kArpStep5ParamId, "Arp Step 5", "Arpeggiator", -9.0, 15.0, 3.0, true },
    { kArpStep6ParamId, "Arp Step 6", "Arpeggiator", -9.0, 15.0, 6.0, true },
    { kArpStep7ParamId, "Arp Step 7", "Arpeggiator", -9.0, 15.0, 5.0, true },
    { kArpStep8ParamId, "Arp Step 8", "Arpeggiator", -9.0, 15.0, 1.0, true },
    { kMod1SecondaryTargetParamId, "Target B", "Mod 1", 0.0, 28.0, 0.0, true },
    { kMod1SecondaryDepthParamId, "Depth B", "Mod 1", -1.0, 1.0, 0.0, false },
    { kMod2SecondaryTargetParamId, "Target B", "Mod 2", 0.0, 28.0, 0.0, true },
    { kMod2SecondaryDepthParamId, "Depth B", "Mod 2", -1.0, 1.0, 0.0, false },
    { kMod3SecondaryTargetParamId, "Target B", "Mod 3", 0.0, 28.0, 0.0, true },
    { kMod3SecondaryDepthParamId, "Depth B", "Mod 3", -1.0, 1.0, 0.0, false },
    { kVelocityTargetParamId, "Target", "Expression/Velocity", 0.0, 28.0, 1.0, true },
    { kVelocityDepthParamId, "Depth", "Expression/Velocity", -1.0, 1.0, 0.55, false },
    { kPressureTargetParamId, "Target", "Expression/Pressure", 0.0, 28.0, 4.0, true },
    { kPressureDepthParamId, "Depth", "Expression/Pressure", -1.0, 1.0, 0.44, false },
    { kTimbreTargetParamId, "Target", "Expression/Timbre", 0.0, 28.0, 3.0, true },
    { kTimbreDepthParamId, "Depth", "Expression/Timbre", -1.0, 1.0, 0.60, false },
    { kBodyDecayParamId, "Body Decay", "Layer Envelopes", 0.02, 12.0, 12.0, false },
    { kTextureAttackParamId, "Texture Attack", "Layer Envelopes", 0.0, 2.0, 0.0, false },
    { kTextureDecayParamId, "Texture Decay", "Layer Envelopes", 0.02, 12.0, 12.0, false },
    { kArpAccent1ParamId, "Arp Accent 1", "Arpeggiator/Accent", 0.0, 1.0, 1.0, false },
    { kArpAccent2ParamId, "Arp Accent 2", "Arpeggiator/Accent", 0.0, 1.0, 1.0, false },
    { kArpAccent3ParamId, "Arp Accent 3", "Arpeggiator/Accent", 0.0, 1.0, 1.0, false },
    { kArpAccent4ParamId, "Arp Accent 4", "Arpeggiator/Accent", 0.0, 1.0, 1.0, false },
    { kArpAccent5ParamId, "Arp Accent 5", "Arpeggiator/Accent", 0.0, 1.0, 1.0, false },
    { kArpAccent6ParamId, "Arp Accent 6", "Arpeggiator/Accent", 0.0, 1.0, 1.0, false },
    { kArpAccent7ParamId, "Arp Accent 7", "Arpeggiator/Accent", 0.0, 1.0, 1.0, false },
    { kArpAccent8ParamId, "Arp Accent 8", "Arpeggiator/Accent", 0.0, 1.0, 1.0, false },
    { kArpGateMode1ParamId, "Arp Gate Mode 1", "Arpeggiator/Gate", 0.0, 1.0, 0.0, true },
    { kArpGateMode2ParamId, "Arp Gate Mode 2", "Arpeggiator/Gate", 0.0, 1.0, 0.0, true },
    { kArpGateMode3ParamId, "Arp Gate Mode 3", "Arpeggiator/Gate", 0.0, 1.0, 0.0, true },
    { kArpGateMode4ParamId, "Arp Gate Mode 4", "Arpeggiator/Gate", 0.0, 1.0, 0.0, true },
    { kArpGateMode5ParamId, "Arp Gate Mode 5", "Arpeggiator/Gate", 0.0, 1.0, 0.0, true },
    { kArpGateMode6ParamId, "Arp Gate Mode 6", "Arpeggiator/Gate", 0.0, 1.0, 0.0, true },
    { kArpGateMode7ParamId, "Arp Gate Mode 7", "Arpeggiator/Gate", 0.0, 1.0, 0.0, true },
    { kArpGateMode8ParamId, "Arp Gate Mode 8", "Arpeggiator/Gate", 0.0, 1.0, 0.0, true },
    { kArpOctave1ParamId, "Arp Octave 1", "Arpeggiator/Octave", -2.0, 2.0, 0.0, true },
    { kArpOctave2ParamId, "Arp Octave 2", "Arpeggiator/Octave", -2.0, 2.0, 0.0, true },
    { kArpOctave3ParamId, "Arp Octave 3", "Arpeggiator/Octave", -2.0, 2.0, 0.0, true },
    { kArpOctave4ParamId, "Arp Octave 4", "Arpeggiator/Octave", -2.0, 2.0, 0.0, true },
    { kArpOctave5ParamId, "Arp Octave 5", "Arpeggiator/Octave", -2.0, 2.0, 0.0, true },
    { kArpOctave6ParamId, "Arp Octave 6", "Arpeggiator/Octave", -2.0, 2.0, 0.0, true },
    { kArpOctave7ParamId, "Arp Octave 7", "Arpeggiator/Octave", -2.0, 2.0, 0.0, true },
    { kArpOctave8ParamId, "Arp Octave 8", "Arpeggiator/Octave", -2.0, 2.0, 0.0, true },
}};

static_assert(kParamDefs.back().id == kParamCount);

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

struct LowformArpParams {
    s3g::ProcessorStackArpPattern pattern =
        s3g::ProcessorStackArpPattern::Off;
    s3g::ProcessorStackScale scale = s3g::ProcessorStackScale::Phrygian;
    bool hostSync = false;
    s3g::ProcessorStackArpRate rate =
        s3g::ProcessorStackArpRate::Sixteenth;
    uint32_t octaves = 2u;
    float gate = 0.62f;
    uint32_t length = 8u;
    std::array<int32_t, 8u> steps {{ 0, 1, 2, 4, 3, 6, 5, 1 }};
    std::array<float, 8u> accents {{
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f }};
    std::array<uint8_t, 8u> gateModes {};
    std::array<int32_t, 8u> stepOctaves {};
};

struct HeldInputNote {
    int key = -1;
    int32_t noteId = -1;
    int16_t channel = -1;
    float velocity = 0.0f;
    float pressure = 0.0f;
    float timbre = 0.0f;
    float tuning = 0.0f;
    uint64_t age = 0u;
    bool hasPressure = false;
    bool hasTimbre = false;
    bool hasTuning = false;
    bool held = false;
};

constexpr int64_t kUnprimedArpHostStep =
    std::numeric_limits<int64_t>::min();

struct LowformArpState {
    double phaseSamples = 0.0;
    int64_t stepIndex = 0;
    uint64_t stepCount = 0u;
    uint64_t sourceAge = 0u;
    int currentKey = -1;
    int32_t currentNoteId = -1;
    int16_t currentChannel = -1;
    int64_t hostStep = kUnprimedArpHostStep;
    bool gateOpen = false;
    bool currentTie = false;
    bool needsTrigger = false;
    bool usedHostClock = false;
};

struct Plugin {
#if defined(S3G_ENABLE_VSTGUI_CANVAS_GUI)
 s3g::portable_gui::foundation::EditorHost* portableGuiEditor=nullptr;
 uint32_t portableGuiWidth=kGuiWidth, portableGuiHeight=kGuiHeight;
 bool portableGuiVisible=false;
#endif
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    const clap_host_tail_t* hostTail = nullptr;
    double sampleRate = 48000.0;
    s3g::Lowform engine {};
    s3g::LowformParams params {};
    double midiReceive = 0.0;
    double expressionMode = 0.0;
    double transpose = 0.0;
    LowformArpParams arpParams {};
    std::array<HeldInputNote, 32u> heldInputNotes {};
    LowformArpState arpState {};
    uint64_t heldInputAge = 0u;
    double transportFallbackBeat = 0.0;
    std::array<float, 16u> midiChannelPressure {};
    std::array<float, 16u> midiChannelTimbre {};
    std::array<float, 16u> midiChannelTuning {};
    std::array<uint8_t, 16u> midiRpnMsb {};
    std::array<uint8_t, 16u> midiRpnLsb {};
    std::array<uint8_t, 16u> midiBendRangeSemitones {};
    std::array<uint8_t, 16u> midiBendRangeCents {};
    std::array<std::atomic<double>, kPublishedParamCount> publishedParams {};
    s3g::clap_gui::ParamEventQueue<512u> guiParamEvents {};
    std::atomic<uint64_t> parameterRevision { 0u };
    std::atomic<float> outputPeak { 0.0f };
    std::array<std::atomic<float>, s3g::kBassModCount> modActivity {};
    std::atomic<bool> tailChangePending { false };
    std::atomic<bool> parameterInfoRescanPending { false };
    bool active = false;
#if defined(__APPLE__) && !defined(S3G_ENABLE_VSTGUI_CANVAS_GUI)
    void* guiView = nullptr;
    bool guiVisible = false;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
#endif
};

Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}

bool validMidiChannel(int channel)
{
    return channel >= 0 && channel < 16;
}

void resetMidiExpressionState(Plugin& p)
{
    p.midiChannelPressure.fill(0.0f);
    p.midiChannelTimbre.fill(0.0f);
    p.midiChannelTuning.fill(0.0f);
    p.midiRpnMsb.fill(127u);
    p.midiRpnLsb.fill(127u);
    p.midiBendRangeSemitones.fill(2u);
    p.midiBendRangeCents.fill(0u);
    p.heldInputNotes.fill(HeldInputNote {});
    p.arpState = LowformArpState {};
    p.heldInputAge = 0u;
    p.engine.setPitchBend(0.0f);
    p.engine.setModWheel(0.0f);
}

bool usesQuNexusGenOneExpression(const Plugin& p)
{
    return p.expressionMode >= 0.5;
}

const char* expressionModeName(uint32_t index)
{
    return index == 0u ? "STANDARD MPE" : "QUNEXUS GEN 1";
}

void applyMidiChannelExpressionToVoice(Plugin& p, int key, int channel)
{
    if (!validMidiChannel(channel)) return;
    const size_t index = static_cast<size_t>(channel);
    p.engine.setPressure(key, -1, static_cast<int16_t>(channel),
        p.midiChannelPressure[index]);
    p.engine.setTimbre(key, -1, static_cast<int16_t>(channel),
        p.midiChannelTimbre[index]);
    p.engine.setTuning(key, -1, static_cast<int16_t>(channel),
        p.midiChannelTuning[index]);
}

void setMidiChannelPressure(Plugin& p, int channel, float value)
{
    if (!validMidiChannel(channel)) return;
    value = std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
    p.midiChannelPressure[static_cast<size_t>(channel)] = value;
    p.engine.setPressure(-1, -1, static_cast<int16_t>(channel), value);
}

void setMidiChannelTimbre(Plugin& p, int channel, float value)
{
    if (!validMidiChannel(channel)) return;
    value = std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
    p.midiChannelTimbre[static_cast<size_t>(channel)] = value;
    p.engine.setTimbre(-1, -1, static_cast<int16_t>(channel), value);
}

void setMidiChannelTuning(Plugin& p, int channel, float semitones)
{
    if (!validMidiChannel(channel)) return;
    semitones = std::clamp(
        std::isfinite(semitones) ? semitones : 0.0f, -48.0f, 48.0f);
    p.midiChannelTuning[static_cast<size_t>(channel)] = semitones;
    p.engine.setTuning(-1, -1, static_cast<int16_t>(channel), semitones);
}

bool arpeggiatorEnabled(const Plugin& p)
{
    return p.arpParams.pattern != s3g::ProcessorStackArpPattern::Off;
}

bool monoVoiceMode(const Plugin& p)
{
    return static_cast<s3g::BassVoiceMode>(std::clamp<long>(
        std::lround(p.params.voiceMode), 0, 1))
        == s3g::BassVoiceMode::Mono;
}

bool isArpeggiatorParam(clap_id id)
{
    return (id >= kArpPatternParamId && id <= kArpStep8ParamId)
        || (id >= kArpAccent1ParamId && id <= kArpOctave8ParamId);
}

bool isPresetPreservedParam(clap_id id)
{
    return id == kMidiReceiveParamId || id == kExpressionModeParamId
        || id == kTransposeParamId
        || (id >= kArpPatternParamId && id <= kArpStep8ParamId)
        || (id >= kVelocityTargetParamId && id <= kTimbreDepthParamId)
        || (id >= kArpAccent1ParamId && id <= kArpOctave8ParamId);
}

int transposedInputKey(const Plugin& p, int key)
{
    return std::clamp(key + static_cast<int>(std::lround(p.transpose)),
        0, 127);
}

bool heldNoteMatches(const HeldInputNote& note, int key,
    int32_t noteId, int16_t channel)
{
    if (!note.held) return false;
    const bool channelMatches = channel < 0 || note.channel == channel;
    const bool noteMatches = noteId >= 0
        ? note.noteId == noteId && (key < 0 || note.key == key)
        : (key < 0 || note.key == key);
    return channelMatches && noteMatches;
}

HeldInputNote* findHeldInputNote(Plugin& p, int key,
    int32_t noteId, int16_t channel)
{
    const auto found = std::find_if(p.heldInputNotes.begin(),
        p.heldInputNotes.end(), [&](const HeldInputNote& note) {
            return heldNoteMatches(note, key, noteId, channel);
        });
    return found == p.heldInputNotes.end() ? nullptr : &*found;
}

HeldInputNote* latestHeldInputNote(Plugin& p)
{
    HeldInputNote* latest = nullptr;
    for (auto& note : p.heldInputNotes) {
        if (note.held && (!latest || note.age > latest->age)) latest = &note;
    }
    return latest;
}

void applyHeldExpressionToVoice(Plugin& p, const HeldInputNote& source,
    int voiceKey)
{
    applyMidiChannelExpressionToVoice(p, voiceKey, source.channel);
    if (source.hasPressure)
        p.engine.setPressure(voiceKey, source.noteId, source.channel,
            source.pressure);
    if (source.hasTimbre)
        p.engine.setTimbre(voiceKey, source.noteId, source.channel,
            source.timbre);
    if (source.hasTuning)
        p.engine.setTuning(voiceKey, source.noteId, source.channel,
            source.tuning);
}

HeldInputNote& rememberHeldInputNote(Plugin& p, int key,
    int32_t noteId, int16_t channel, float velocity)
{
    HeldInputNote* slot = findHeldInputNote(p, key, noteId, channel);
    if (!slot) {
        const auto free = std::find_if(p.heldInputNotes.begin(),
            p.heldInputNotes.end(), [](const HeldInputNote& note) {
                return !note.held;
            });
        if (free != p.heldInputNotes.end()) slot = &*free;
        else slot = &*std::min_element(p.heldInputNotes.begin(),
            p.heldInputNotes.end(), [](const HeldInputNote& a,
                                      const HeldInputNote& b) {
                return a.age < b.age;
            });
    }
    *slot = HeldInputNote {};
    slot->key = std::clamp(key, 0, 127);
    slot->noteId = noteId;
    slot->channel = channel;
    slot->velocity = std::clamp(
        std::isfinite(velocity) ? velocity : 1.0f, 0.0f, 1.0f);
    slot->age = ++p.heldInputAge;
    slot->held = true;
    return *slot;
}

void releaseArpeggiatorVoice(Plugin& p)
{
    if (p.arpState.gateOpen) {
        p.engine.noteOff(p.arpState.currentKey, p.arpState.currentNoteId,
            p.arpState.currentChannel);
    }
    p.arpState.gateOpen = false;
    p.arpState.currentKey = -1;
    p.arpState.currentNoteId = -1;
    p.arpState.currentChannel = -1;
    p.arpState.sourceAge = 0u;
    p.arpState.currentTie = false;
}

void resetArpeggiatorRuntime(Plugin& p, bool trigger)
{
    const uint64_t stepCount = p.arpState.stepCount;
    releaseArpeggiatorVoice(p);
    p.arpState = LowformArpState {};
    p.arpState.stepCount = stepCount;
    p.arpState.needsTrigger = trigger && arpeggiatorEnabled(p)
        && latestHeldInputNote(p);
}

void retriggerDirectHeldNotes(Plugin& p)
{
    std::array<HeldInputNote*, 32u> ordered {};
    uint32_t count = 0u;
    for (auto& note : p.heldInputNotes)
        if (note.held) ordered[count++] = &note;
    std::sort(ordered.begin(), ordered.begin() + count,
        [](const HeldInputNote* a, const HeldInputNote* b) {
            return a->age < b->age;
        });
    for (uint32_t index = 0u; index < count; ++index) {
        const auto& source = *ordered[index];
        const int voiceKey = transposedInputKey(p, source.key);
        p.engine.noteOn(voiceKey, source.velocity,
            source.noteId, source.channel);
        applyHeldExpressionToVoice(p, source, voiceKey);
    }
}

void revoiceHeldInput(Plugin& p)
{
    p.engine.allNotesOff();
    if (arpeggiatorEnabled(p)) resetArpeggiatorRuntime(p, true);
    else {
        resetArpeggiatorRuntime(p, false);
        retriggerDirectHeldNotes(p);
    }
}

void clearHeldInputNotes(Plugin& p, int16_t channel = -1)
{
    for (auto& note : p.heldInputNotes) {
        if (note.held && (channel < 0 || note.channel == channel))
            note = HeldInputNote {};
    }
    if (arpeggiatorEnabled(p)) resetArpeggiatorRuntime(p, true);
}

const ParamDef* paramDef(clap_id id)
{
    return id >= 1u && id <= kParamCount ? &kParamDefs[id - 1u] : nullptr;
}

double clampValue(const ParamDef& def, double value)
{
    value = std::isfinite(value) ? value : def.defaultValue;
    value = std::clamp(value, def.minimum, def.maximum);
    return def.stepped ? std::round(value) : value;
}

void publishParam(Plugin& p, clap_id id, double value)
{
    if (paramDef(id)) p.publishedParams[id].store(value,
        std::memory_order_release);
}

double paramValue(const Plugin& p, clap_id id)
{
    return paramDef(id) ? p.publishedParams[id].load(
        std::memory_order_acquire) : 0.0;
}

s3g::BassBodyEngine currentBodyEngine(const Plugin& p)
{
    return static_cast<s3g::BassBodyEngine>(std::clamp<int>(
        static_cast<int>(std::lround(paramValue(p, kBodyEngineParamId))),
        0, 7));
}

bool isBodyControlParam(clap_id id)
{
    return id == kBodyControlAParamId || id == kBodyControlBParamId
        || id == kBodyControlCParamId || id == kBodyControlDParamId
        || id == kBodyControlEParamId;
}

const char* bodyControlName(s3g::BassBodyEngine engine, clap_id id)
{
    switch (engine) {
    case s3g::BassBodyEngine::Pressure:
        if (id == kBodyControlAParamId) return "Shape";
        if (id == kBodyControlBParamId) return "Pressure";
        if (id == kBodyControlCParamId) return "Pulse Width";
        if (id == kBodyControlDParamId) return "Cross";
        return "Drive";
    case s3g::BassBodyEngine::Swarm:
        if (id == kBodyControlAParamId) return "Drive";
        if (id == kBodyControlBParamId) return "Detune";
        if (id == kBodyControlCParamId) return "Voices";
        if (id == kBodyControlDParamId) return "Drift";
        return "Tone";
    case s3g::BassBodyEngine::Modal:
        if (id == kBodyControlAParamId) return "Material";
        if (id == kBodyControlBParamId) return "Position";
        if (id == kBodyControlCParamId) return "Decay";
        if (id == kBodyControlDParamId) return "Sustain";
        return "Damping";
    case s3g::BassBodyEngine::Acid:
        if (id == kBodyControlAParamId) return "Wave";
        if (id == kBodyControlBParamId) return "Resonance";
        if (id == kBodyControlCParamId) return "Pulse Width";
        if (id == kBodyControlDParamId) return "Drive";
        return "Cutoff Offset";
    case s3g::BassBodyEngine::Rave:
        if (id == kBodyControlAParamId) return "Wave";
        if (id == kBodyControlBParamId) return "Sub";
        if (id == kBodyControlCParamId) return "Smear";
        if (id == kBodyControlDParamId) return "Noise";
        return "Drive";
    case s3g::BassBodyEngine::Phase:
        if (id == kBodyControlAParamId) return "Carrier";
        if (id == kBodyControlBParamId) return "Ratio";
        if (id == kBodyControlCParamId) return "Index";
        if (id == kBodyControlDParamId) return "Feedback";
        return "Fold";
    case s3g::BassBodyEngine::Throat:
        if (id == kBodyControlAParamId) return "Vowel";
        if (id == kBodyControlBParamId) return "Shift";
        if (id == kBodyControlCParamId) return "Focus";
        if (id == kBodyControlDParamId) return "Source";
        return "Drive";
    case s3g::BassBodyEngine::Sync:
        if (id == kBodyControlAParamId) return "Wave";
        if (id == kBodyControlBParamId) return "Ratio";
        if (id == kBodyControlCParamId) return "Sweep";
        if (id == kBodyControlDParamId) return "Decay";
        return "Ring";
    }
    return "Engine Control";
}

const char* bodyModTargetName(s3g::BassBodyEngine engine,
    s3g::BassModTarget target)
{
    const auto controlTarget = [engine](clap_id id) {
        switch (engine) {
        case s3g::BassBodyEngine::Pressure:
            if (id == kBodyControlAParamId) return "BODY / SHAPE";
            if (id == kBodyControlBParamId) return "BODY / PRESSURE";
            if (id == kBodyControlCParamId) return "BODY / PULSE WIDTH";
            if (id == kBodyControlDParamId) return "BODY / CROSS";
            return "BODY / DRIVE";
        case s3g::BassBodyEngine::Swarm:
            if (id == kBodyControlAParamId) return "BODY / DRIVE";
            if (id == kBodyControlBParamId) return "BODY / DETUNE";
            if (id == kBodyControlCParamId) return "BODY / VOICES";
            if (id == kBodyControlDParamId) return "BODY / DRIFT";
            return "BODY / TONE";
        case s3g::BassBodyEngine::Modal:
            if (id == kBodyControlAParamId) return "BODY / MATERIAL";
            if (id == kBodyControlBParamId) return "BODY / POSITION";
            if (id == kBodyControlCParamId) return "BODY / DECAY";
            if (id == kBodyControlDParamId) return "BODY / SUSTAIN";
            return "BODY / DAMPING";
        case s3g::BassBodyEngine::Acid:
            if (id == kBodyControlAParamId) return "BODY / WAVE";
            if (id == kBodyControlBParamId) return "BODY / RESONANCE";
            if (id == kBodyControlCParamId) return "BODY / PULSE WIDTH";
            if (id == kBodyControlDParamId) return "BODY / DRIVE";
            return "BODY / CUTOFF OFFSET";
        case s3g::BassBodyEngine::Rave:
            if (id == kBodyControlAParamId) return "BODY / WAVE";
            if (id == kBodyControlBParamId) return "BODY / SUB";
            if (id == kBodyControlCParamId) return "BODY / SMEAR";
            if (id == kBodyControlDParamId) return "BODY / NOISE";
            return "BODY / DRIVE";
        case s3g::BassBodyEngine::Phase:
            if (id == kBodyControlAParamId) return "BODY / CARRIER";
            if (id == kBodyControlBParamId) return "BODY / RATIO";
            if (id == kBodyControlCParamId) return "BODY / INDEX";
            if (id == kBodyControlDParamId) return "BODY / FEEDBACK";
            return "BODY / FOLD";
        case s3g::BassBodyEngine::Throat:
            if (id == kBodyControlAParamId) return "BODY / VOWEL";
            if (id == kBodyControlBParamId) return "BODY / SHIFT";
            if (id == kBodyControlCParamId) return "BODY / FOCUS";
            if (id == kBodyControlDParamId) return "BODY / SOURCE";
            return "BODY / DRIVE";
        case s3g::BassBodyEngine::Sync:
            if (id == kBodyControlAParamId) return "BODY / WAVE";
            if (id == kBodyControlBParamId) return "BODY / RATIO";
            if (id == kBodyControlCParamId) return "BODY / SWEEP";
            if (id == kBodyControlDParamId) return "BODY / DECAY";
            return "BODY / RING";
        }
        return "BODY / CONTROL";
    };
    if (target == s3g::BassModTarget::BodyMorph)
        return controlTarget(kBodyControlAParamId);
    if (target == s3g::BassModTarget::BodyDensity)
        return controlTarget(kBodyControlBParamId);
    if (target == s3g::BassModTarget::BodyControlC)
        return controlTarget(kBodyControlCParamId);
    if (target == s3g::BassModTarget::BodyControlD)
        return controlTarget(kBodyControlDParamId);
    if (target == s3g::BassModTarget::BodyControlE)
        return controlTarget(kBodyControlEParamId);
    return s3g::bassModTargetName(target);
}

// Menu order is deliberately independent from the stable numeric target
// values stored by hosts and projects. Keep related destinations contiguous
// here without changing the meaning of existing automation values 0–19.
constexpr std::array<s3g::BassModTarget, s3g::kBassModTargetCount>
    kBassModTargetMenuOrder {{
        s3g::BassModTarget::Off,
        s3g::BassModTarget::Amp,
        s3g::BassModTarget::Pitch,
        s3g::BassModTarget::FoundationLevel,
        s3g::BassModTarget::BodyLevel,
        s3g::BassModTarget::BodyMorph,
        s3g::BassModTarget::BodyDensity,
        s3g::BassModTarget::BodyControlC,
        s3g::BassModTarget::BodyControlD,
        s3g::BassModTarget::BodyControlE,
        s3g::BassModTarget::BodyWidth,
        s3g::BassModTarget::Width,
        s3g::BassModTarget::TextureLevel,
        s3g::BassModTarget::TextureColor,
        s3g::BassModTarget::TextureTrack,
        s3g::BassModTarget::TextureWidth,
        s3g::BassModTarget::Cutoff,
        s3g::BassModTarget::Resonance,
        s3g::BassModTarget::FilterDrive,
        s3g::BassModTarget::KeyTrack,
        s3g::BassModTarget::AmplifierTube,
        s3g::BassModTarget::ShredAmount,
        s3g::BassModTarget::ShredFeedback,
        s3g::BassModTarget::ShredColor,
        s3g::BassModTarget::ShredMix,
        s3g::BassModTarget::DynamicsAmount,
        s3g::BassModTarget::DynamicsBite,
        s3g::BassModTarget::DynamicsSaturation,
        s3g::BassModTarget::DynamicsTilt,
    }};

constexpr uint32_t kModTargetMenuColumns = 2u;

std::array<double, kParamCount> valuesFromParams(
    const s3g::LowformParams& p, double midiReceive, double expressionMode,
    double transpose, const LowformArpParams& arp)
{
    return {{
        p.outputGainDb, p.voiceMode, p.glideMs,
        p.foundationWave, p.foundationOctave, p.foundationLevel,
        p.pitchPunchSemitones, p.punchTimeMs, p.foundationReturn,
        p.bodyEngine, p.bodyLevel, p.bodyControlA, p.bodyControlB, p.bodyWidth,
        p.textureMode, p.textureLevel, p.textureColor, p.textureTrack,
        p.filterType, p.cutoffHz, p.resonance, p.filterDrive, p.keyTrack,
        p.foundationFilter, p.bodyFilter, p.textureFilter,
        p.attackSeconds, p.decaySeconds, p.sustain, p.releaseSeconds,
        p.filterEnvelopeOctaves, p.filterDecaySeconds, p.motionClock,
        p.mods[0].shape, p.mods[0].rate, p.mods[0].depth, p.mods[0].target,
        p.mods[1].shape, p.mods[1].rate, p.mods[1].depth, p.mods[1].target,
        p.mods[2].shape, p.mods[2].rate, p.mods[2].depth, p.mods[2].target,
        p.modalDrive, p.modWheelAmount, p.dynamicsBite, p.textureWidth,
        p.tube, p.shredCircuit, p.shred, p.shredFeedback,
        p.shredColor, p.shredMix, p.dynamics, p.saturation, p.clip,
        p.tilt, p.maximizer, midiReceive,
        p.bodyControlC, p.bodyControlD, p.bodyControlE,
        expressionMode,
        transpose,
        static_cast<double>(arp.pattern),
        static_cast<double>(arp.scale),
        arp.hostSync ? 1.0 : 0.0,
        static_cast<double>(arp.rate),
        static_cast<double>(arp.octaves),
        arp.gate,
        static_cast<double>(arp.length),
        static_cast<double>(arp.steps[0u]),
        static_cast<double>(arp.steps[1u]),
        static_cast<double>(arp.steps[2u]),
        static_cast<double>(arp.steps[3u]),
        static_cast<double>(arp.steps[4u]),
        static_cast<double>(arp.steps[5u]),
        static_cast<double>(arp.steps[6u]),
        static_cast<double>(arp.steps[7u]),
        p.mods[0].secondaryTarget, p.mods[0].secondaryDepth,
        p.mods[1].secondaryTarget, p.mods[1].secondaryDepth,
        p.mods[2].secondaryTarget, p.mods[2].secondaryDepth,
        p.expressionRoutes[0].target, p.expressionRoutes[0].depth,
        p.expressionRoutes[1].target, p.expressionRoutes[1].depth,
        p.expressionRoutes[2].target, p.expressionRoutes[2].depth,
        p.bodyDecaySeconds, p.textureAttackSeconds, p.textureDecaySeconds,
        arp.accents[0u], arp.accents[1u], arp.accents[2u], arp.accents[3u],
        arp.accents[4u], arp.accents[5u], arp.accents[6u], arp.accents[7u],
        static_cast<double>(arp.gateModes[0u]),
        static_cast<double>(arp.gateModes[1u]),
        static_cast<double>(arp.gateModes[2u]),
        static_cast<double>(arp.gateModes[3u]),
        static_cast<double>(arp.gateModes[4u]),
        static_cast<double>(arp.gateModes[5u]),
        static_cast<double>(arp.gateModes[6u]),
        static_cast<double>(arp.gateModes[7u]),
        static_cast<double>(arp.stepOctaves[0u]),
        static_cast<double>(arp.stepOctaves[1u]),
        static_cast<double>(arp.stepOctaves[2u]),
        static_cast<double>(arp.stepOctaves[3u]),
        static_cast<double>(arp.stepOctaves[4u]),
        static_cast<double>(arp.stepOctaves[5u]),
        static_cast<double>(arp.stepOctaves[6u]),
        static_cast<double>(arp.stepOctaves[7u]),
    }};
}

double rawParamValue(const Plugin& p, clap_id id)
{
    if (!paramDef(id)) return 0.0;
    return valuesFromParams(
        p.params, p.midiReceive, p.expressionMode, p.transpose,
        p.arpParams)[id - 1u];
}

void notifyTailChanged(Plugin& p)
{
    if (p.host && p.hostTail && p.hostTail->changed)
        p.hostTail->changed(p.host);
}

void applyParam(Plugin& p, clap_id id, double value)
{
    const auto* def = paramDef(id);
    if (!def) return;
    value = clampValue(*def, value);
    if (id == kMidiReceiveParamId) {
        p.midiReceive = value;
        publishParam(p, id, value);
        p.parameterRevision.fetch_add(1u, std::memory_order_release);
        return;
    }
    if (id == kExpressionModeParamId) {
        const bool changed = value != p.expressionMode;
        p.expressionMode = value;
        if (changed) {
            for (int channel = 0; channel < 16; ++channel)
                setMidiChannelPressure(p, channel, 0.0f);
            p.engine.setModWheel(0.0f);
        }
        publishParam(p, id, value);
        p.parameterRevision.fetch_add(1u, std::memory_order_release);
        return;
    }
    if (id == kTransposeParamId || isArpeggiatorParam(id)) {
        const bool wasEnabled = arpeggiatorEnabled(p);
        if (id == kTransposeParamId) p.transpose = value;
        else if (id == kArpPatternParamId)
            p.arpParams.pattern = static_cast<s3g::ProcessorStackArpPattern>(
                static_cast<uint32_t>(std::lround(value)));
        else if (id == kArpScaleParamId)
            p.arpParams.scale = static_cast<s3g::ProcessorStackScale>(
                static_cast<uint32_t>(std::lround(value)));
        else if (id == kArpSyncParamId) p.arpParams.hostSync = value >= 0.5;
        else if (id == kArpRateParamId)
            p.arpParams.rate = static_cast<s3g::ProcessorStackArpRate>(
                static_cast<uint32_t>(std::lround(value)));
        else if (id == kArpOctavesParamId)
            p.arpParams.octaves = static_cast<uint32_t>(std::lround(value));
        else if (id == kArpGateParamId)
            p.arpParams.gate = static_cast<float>(value);
        else if (id == kArpLengthParamId)
            p.arpParams.length = static_cast<uint32_t>(std::lround(value));
        else if (id >= kArpStep1ParamId && id <= kArpStep8ParamId)
            p.arpParams.steps[id - kArpStep1ParamId] =
                static_cast<int32_t>(std::lround(value));
        else if (id >= kArpAccent1ParamId && id <= kArpAccent8ParamId)
            p.arpParams.accents[id - kArpAccent1ParamId] =
                static_cast<float>(value);
        else if (id >= kArpGateMode1ParamId && id <= kArpGateMode8ParamId)
            p.arpParams.gateModes[id - kArpGateMode1ParamId] =
                static_cast<uint8_t>(std::lround(value));
        else if (id >= kArpOctave1ParamId && id <= kArpOctave8ParamId)
            p.arpParams.stepOctaves[id - kArpOctave1ParamId] =
                static_cast<int32_t>(std::lround(value));
        const bool structuralArpChange = isArpeggiatorParam(id)
            && id != kArpGateParamId
            && !(id >= kArpAccent1ParamId && id <= kArpAccent8ParamId);
        if (id == kTransposeParamId
            || (structuralArpChange
                && (wasEnabled || arpeggiatorEnabled(p))))
            revoiceHeldInput(p);
        publishParam(p, id, rawParamValue(p, id));
        p.parameterRevision.fetch_add(1u, std::memory_order_release);
        if (p.host && p.host->request_process) p.host->request_process(p.host);
        return;
    }
    const float v = static_cast<float>(value);
    switch (id) {
    case kOutputParamId: p.params.outputGainDb = v; break;
    case kVoiceModeParamId: p.params.voiceMode = v; break;
    case kGlideParamId: p.params.glideMs = v; break;
    case kFoundationWaveParamId: p.params.foundationWave = v; break;
    case kFoundationOctaveParamId: p.params.foundationOctave = v; break;
    case kFoundationLevelParamId: p.params.foundationLevel = v; break;
    case kPitchPunchParamId: p.params.pitchPunchSemitones = v; break;
    case kPunchTimeParamId: p.params.punchTimeMs = v; break;
    case kFoundationReturnParamId: p.params.foundationReturn = v; break;
    case kBodyEngineParamId:
        p.params.bodyEngine = v;
        p.parameterInfoRescanPending.store(true, std::memory_order_release);
        if (p.hostParams && p.host && p.host->request_callback)
            p.host->request_callback(p.host);
        break;
    case kBodyLevelParamId: p.params.bodyLevel = v; break;
    case kBodyControlAParamId: p.params.bodyControlA = v; break;
    case kBodyControlBParamId: p.params.bodyControlB = v; break;
    case kBodyWidthParamId: p.params.bodyWidth = v; break;
    case kBodyControlCParamId: p.params.bodyControlC = v; break;
    case kBodyControlDParamId: p.params.bodyControlD = v; break;
    case kBodyControlEParamId: p.params.bodyControlE = v; break;
    case kTextureModeParamId: p.params.textureMode = v; break;
    case kTextureLevelParamId: p.params.textureLevel = v; break;
    case kTextureColorParamId: p.params.textureColor = v; break;
    case kTextureTrackParamId: p.params.textureTrack = v; break;
    case kFilterTypeParamId: p.params.filterType = v; break;
    case kCutoffParamId: p.params.cutoffHz = v; break;
    case kResonanceParamId: p.params.resonance = v; break;
    case kFilterDriveParamId: p.params.filterDrive = v; break;
    case kKeyTrackParamId: p.params.keyTrack = v; break;
    case kFoundationFilterParamId: p.params.foundationFilter = v; break;
    case kBodyFilterParamId: p.params.bodyFilter = v; break;
    case kTextureFilterParamId: p.params.textureFilter = v; break;
    case kAttackParamId: p.params.attackSeconds = v; break;
    case kDecayParamId: p.params.decaySeconds = v; break;
    case kSustainParamId: p.params.sustain = v; break;
    case kReleaseParamId: p.params.releaseSeconds = v; break;
    case kFilterEnvelopeParamId: p.params.filterEnvelopeOctaves = v; break;
    case kFilterDecayParamId: p.params.filterDecaySeconds = v; break;
    case kMotionClockParamId: p.params.motionClock = v; break;
    case kMod1ShapeParamId: p.params.mods[0].shape = v; break;
    case kMod1RateParamId: p.params.mods[0].rate = v; break;
    case kMod1DepthParamId: p.params.mods[0].depth = v; break;
    case kMod1TargetParamId: p.params.mods[0].target = v; break;
    case kMod2ShapeParamId: p.params.mods[1].shape = v; break;
    case kMod2RateParamId: p.params.mods[1].rate = v; break;
    case kMod2DepthParamId: p.params.mods[1].depth = v; break;
    case kMod2TargetParamId: p.params.mods[1].target = v; break;
    case kMod3ShapeParamId: p.params.mods[2].shape = v; break;
    case kMod3RateParamId: p.params.mods[2].rate = v; break;
    case kMod3DepthParamId: p.params.mods[2].depth = v; break;
    case kMod3TargetParamId: p.params.mods[2].target = v; break;
    case kMod1SecondaryTargetParamId:
        p.params.mods[0].secondaryTarget = v; break;
    case kMod1SecondaryDepthParamId:
        p.params.mods[0].secondaryDepth = v; break;
    case kMod2SecondaryTargetParamId:
        p.params.mods[1].secondaryTarget = v; break;
    case kMod2SecondaryDepthParamId:
        p.params.mods[1].secondaryDepth = v; break;
    case kMod3SecondaryTargetParamId:
        p.params.mods[2].secondaryTarget = v; break;
    case kMod3SecondaryDepthParamId:
        p.params.mods[2].secondaryDepth = v; break;
    case kVelocityTargetParamId:
        p.params.expressionRoutes[0].target = v; break;
    case kVelocityDepthParamId:
        p.params.expressionRoutes[0].depth = v; break;
    case kPressureTargetParamId:
        p.params.expressionRoutes[1].target = v; break;
    case kPressureDepthParamId:
        p.params.expressionRoutes[1].depth = v; break;
    case kTimbreTargetParamId:
        p.params.expressionRoutes[2].target = v; break;
    case kTimbreDepthParamId:
        p.params.expressionRoutes[2].depth = v; break;
    case kBodyDecayParamId: p.params.bodyDecaySeconds = v; break;
    case kTextureAttackParamId: p.params.textureAttackSeconds = v; break;
    case kTextureDecayParamId: p.params.textureDecaySeconds = v; break;
    case kModalDriveParamId: p.params.modalDrive = v; break;
    case kModWheelAmountParamId: p.params.modWheelAmount = v; break;
    case kDynamicsBiteParamId: p.params.dynamicsBite = v; break;
    case kTextureWidthParamId: p.params.textureWidth = v; break;
    case kTubeParamId: p.params.tube = v; break;
    case kShredCircuitParamId: p.params.shredCircuit = v; break;
    case kShredParamId: p.params.shred = v; break;
    case kShredFeedbackParamId: p.params.shredFeedback = v; break;
    case kShredColorParamId: p.params.shredColor = v; break;
    case kShredMixParamId: p.params.shredMix = v; break;
    case kDynamicsParamId: p.params.dynamics = v; break;
    case kSaturationParamId: p.params.saturation = v; break;
    case kClipParamId: p.params.clip = v; break;
    case kTiltParamId: p.params.tilt = v; break;
    case kMaximizerParamId: p.params.maximizer = v; break;
    default: return;
    }
    p.engine.setParams(p.params);
    p.params = p.engine.params();
    publishParam(p, id, rawParamValue(p, id));
    p.parameterRevision.fetch_add(1u, std::memory_order_release);
    if (id == kReleaseParamId || id == kShredFeedbackParamId
        || id == kShredMixParamId) {
        p.tailChangePending.store(true, std::memory_order_release);
        if (p.host && p.host->request_process) p.host->request_process(p.host);
    }
}

bool init(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (p->host && p->host->get_extension) {
        p->hostParams = static_cast<const clap_host_params_t*>(
            p->host->get_extension(p->host, CLAP_EXT_PARAMS));
        p->hostTail = static_cast<const clap_host_tail_t*>(
            p->host->get_extension(p->host, CLAP_EXT_TAIL));
    }
    return true;
}

void requestGuiParamService(Plugin& p)
{
    if (p.hostParams && p.hostParams->request_flush)
        p.hostParams->request_flush(p.host);
    else if (p.host && p.host->request_process)
        p.host->request_process(p.host);
}

bool queueGuiParamEvent(Plugin& p, s3g::clap_gui::ParamEventKind kind,
    clap_id id, double value = 0.0)
{
    if (!p.guiParamEvents.push({ kind, id, value })) return false;
    requestGuiParamService(p);
    return true;
}

void queueGuiParamGestureBegin(Plugin& p, clap_id id)
{
    (void)queueGuiParamEvent(p,
        s3g::clap_gui::ParamEventKind::GestureBegin, id);
}

void queueGuiParamValue(Plugin& p, clap_id id, double value)
{
    if (const auto* def = paramDef(id)) {
        value = clampValue(*def, value);
        publishParam(p, id, value);
    }
    (void)queueGuiParamEvent(p,
        s3g::clap_gui::ParamEventKind::Value, id, value);
}

void queueGuiParamGestureEnd(Plugin& p, clap_id id)
{
    (void)queueGuiParamEvent(p,
        s3g::clap_gui::ParamEventKind::GestureEnd, id);
}

void queueGuiParamGesture(Plugin& p, clap_id id, double value)
{
    queueGuiParamGestureBegin(p, id);
    queueGuiParamValue(p, id, value);
    queueGuiParamGestureEnd(p, id);
}

bool queueGuiParams(Plugin& p, const s3g::LowformParams& params)
{
    using Kind = s3g::clap_gui::ParamEventKind;
    const auto values = valuesFromParams(
        params, p.midiReceive, p.expressionMode, p.transpose, p.arpParams);
    std::array<s3g::clap_gui::ParamEvent, kParamCount * 3u> events {};
    uint32_t eventIndex = 0u;
    for (uint32_t index = 0u; index < kParamCount; ++index) {
        const clap_id id = kParamDefs[index].id;
        if (isPresetPreservedParam(id)) continue;
        const double value = clampValue(kParamDefs[index], values[index]);
        events[eventIndex++] = { Kind::GestureBegin, id, 0.0 };
        events[eventIndex++] = { Kind::Value, id, value };
        events[eventIndex++] = { Kind::GestureEnd, id, 0.0 };
    }
    if (!p.guiParamEvents.pushBatch(events.data(), eventIndex)) return false;
    for (uint32_t index = 0u; index < kParamCount; ++index) {
        if (isPresetPreservedParam(kParamDefs[index].id)) continue;
        publishParam(p, kParamDefs[index].id,
            clampValue(kParamDefs[index], values[index]));
    }
    requestGuiParamService(p);
    return true;
}

bool pushGuiParamEvent(const clap_output_events_t* output,
    const s3g::clap_gui::ParamEvent& pending)
{
    if (!output || !output->try_push) return true;
    if (pending.kind == s3g::clap_gui::ParamEventKind::Value) {
        clap_event_param_value_t event {};
        event.header.size = sizeof(event);
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.param_id = pending.paramId;
        event.note_id = event.port_index = event.channel = event.key = -1;
        event.value = pending.value;
        return output->try_push(output, &event.header);
    }
    clap_event_param_gesture_t event {};
    event.header.size = sizeof(event);
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = pending.kind
            == s3g::clap_gui::ParamEventKind::GestureBegin
        ? CLAP_EVENT_PARAM_GESTURE_BEGIN : CLAP_EVENT_PARAM_GESTURE_END;
    event.header.flags = CLAP_EVENT_IS_LIVE;
    event.param_id = pending.paramId;
    return output->try_push(output, &event.header);
}

void serviceGuiParamEvents(Plugin& p, const clap_output_events_t* output)
{
    s3g::clap_gui::ParamEvent pending {};
    while (p.guiParamEvents.peek(pending)) {
        if (!pushGuiParamEvent(output, pending)) break;
        if (pending.kind == s3g::clap_gui::ParamEventKind::Value)
            applyParam(p, pending.paramId, pending.value);
        p.guiParamEvents.pop();
    }
}

#if defined(__APPLE__) && !defined(S3G_ENABLE_VSTGUI_CANVAS_GUI)
void guiDestroy(const clap_plugin_t* plugin);
#endif

void destroy(const clap_plugin_t* plugin)
{
#if defined(S3G_ENABLE_VSTGUI_CANVAS_GUI)
 destroyPortableGui(*self(plugin));
#endif
#if defined(__APPLE__) && !defined(S3G_ENABLE_VSTGUI_CANVAS_GUI)
    guiDestroy(plugin);
#endif
    delete self(plugin);
}

bool activate(const clap_plugin_t* plugin, double sampleRate,
    uint32_t, uint32_t)
{
    auto* p = self(plugin);
    p->sampleRate = std::clamp(sampleRate, 8000.0, 768000.0);
    p->engine.prepare(p->sampleRate);
    p->engine.setParams(p->params);
    resetMidiExpressionState(*p);
    p->transportFallbackBeat = 0.0;
    p->active = false;
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
    for (auto& activity : p->modActivity)
        activity.store(0.0f, std::memory_order_relaxed);
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->engine.reset();
    p->engine.setParams(p->params);
    resetMidiExpressionState(*p);
    p->transportFallbackBeat = 0.0;
    p->active = false;
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
    for (auto& activity : p->modActivity)
        activity.store(0.0f, std::memory_order_relaxed);
}

void startInputNote(Plugin& p, int key, float velocity,
    int32_t noteId, int16_t channel)
{
    auto& source = rememberHeldInputNote(
        p, key, noteId, channel, velocity);
    if (arpeggiatorEnabled(p)) {
        resetArpeggiatorRuntime(p, true);
        return;
    }
    const int voiceKey = transposedInputKey(p, source.key);
    p.engine.noteOn(voiceKey, source.velocity, source.noteId, source.channel);
    applyHeldExpressionToVoice(p, source, voiceKey);
}

void releaseInputNote(Plugin& p, int key,
    int32_t noteId, int16_t channel)
{
    if (key < 0 && noteId < 0) {
        clearHeldInputNotes(p, channel);
        if (!arpeggiatorEnabled(p)) p.engine.noteOff(-1, -1, channel);
        return;
    }
    HeldInputNote* latestBefore = latestHeldInputNote(p);
    auto* source = findHeldInputNote(p, key, noteId, channel);
    // Some hosts/controllers do not preserve CLAP note IDs, or rotate the
    // member channel too early, on note-off. First fall back to the physical
    // key on the same channel. Only the arp may relax the channel as a final
    // anti-latch measure; direct POLY/MONO playing retains exact MPE identity.
    if (!source && key >= 0)
        source = findHeldInputNote(p, key, -1, channel);
    if (!source && key >= 0 && arpeggiatorEnabled(p))
        source = findHeldInputNote(p, key, -1, -1);
    if (!source) {
        HeldInputNote* sole = nullptr;
        for (auto& candidate : p.heldInputNotes) {
            if (!candidate.held
                || (channel >= 0 && candidate.channel != channel)) continue;
            if (sole) {
                sole = nullptr;
                break;
            }
            sole = &candidate;
        }
        source = sole;
    }
    const bool releasedLatest = source && latestBefore
        && source->age == latestBefore->age;
    const int sourceKey = source ? source->key : key;
    const int32_t sourceNoteId = source ? source->noteId : noteId;
    const int16_t sourceChannel = source ? source->channel : channel;
    if (source) *source = HeldInputNote {};
    if (arpeggiatorEnabled(p)) {
        // The direction selector arms the arp; held input is its gate. The
        // final release must disarm scheduling as well as close the voice.
        // Releasing an older held note must not restart the current phrase.
        auto* remaining = latestHeldInputNote(p);
        if (!remaining) resetArpeggiatorRuntime(p, false);
        else if (releasedLatest) resetArpeggiatorRuntime(p, true);
        return;
    }
    if (monoVoiceMode(p) && releasedLatest) {
        // Conventional last-note priority: when the newest key is released,
        // fall back legato to the previously held key instead of dropping the
        // mono voice. This also preserves Glide and per-channel expression.
        if (const auto* remaining = latestHeldInputNote(p)) {
            const int voiceKey = transposedInputKey(p, remaining->key);
            p.engine.noteOn(voiceKey, remaining->velocity,
                remaining->noteId, remaining->channel);
            applyHeldExpressionToVoice(p, *remaining, voiceKey);
            return;
        }
    }
    p.engine.noteOff(sourceKey >= 0
            ? transposedInputKey(p, sourceKey) : -1,
        sourceNoteId, sourceChannel);
}

enum class HeldExpression : uint8_t { Pressure, Timbre, Tuning };

void cacheHeldExpression(Plugin& p, int key, int32_t noteId,
    int16_t channel, HeldExpression expression, float value)
{
    for (auto& source : p.heldInputNotes) {
        if (!heldNoteMatches(source, key, noteId, channel)) continue;
        if (expression == HeldExpression::Pressure) {
            source.pressure = std::clamp(value, 0.0f, 1.0f);
            source.hasPressure = true;
        } else if (expression == HeldExpression::Timbre) {
            source.timbre = std::clamp(value, 0.0f, 1.0f);
            source.hasTimbre = true;
        } else {
            source.tuning = std::clamp(value, -48.0f, 48.0f);
            source.hasTuning = true;
        }
    }
}

void applyHeldOrDirectExpression(Plugin& p, int key, int32_t noteId,
    int16_t channel, HeldExpression expression, float value)
{
    cacheHeldExpression(p, key, noteId, channel, expression, value);
    if (arpeggiatorEnabled(p)) {
        auto* source = latestHeldInputNote(p);
        if (!p.arpState.gateOpen || !source
            || p.arpState.sourceAge != source->age
            || !heldNoteMatches(*source, key, noteId, channel)) return;
        key = p.arpState.currentKey;
        noteId = p.arpState.currentNoteId;
        channel = p.arpState.currentChannel;
    } else if (key >= 0) {
        key = transposedInputKey(p, key);
    }
    if (expression == HeldExpression::Pressure)
        p.engine.setPressure(key, noteId, channel, value);
    else if (expression == HeldExpression::Timbre)
        p.engine.setTimbre(key, noteId, channel, value);
    else
        p.engine.setTuning(key, noteId, channel, value);
}

void applyNoteExpression(Plugin& p, const clap_event_note_expression_t& e)
{
    if (!s3g::drum_midi::accepts(p.midiReceive, e.channel)) return;
    const float value = static_cast<float>(e.value);
    if (e.expression_id == CLAP_NOTE_EXPRESSION_PRESSURE)
        applyHeldOrDirectExpression(p, e.key, e.note_id, e.channel,
            HeldExpression::Pressure, value);
    else if (e.expression_id == CLAP_NOTE_EXPRESSION_BRIGHTNESS)
        applyHeldOrDirectExpression(p, e.key, e.note_id, e.channel,
            HeldExpression::Timbre, value);
    else if (e.expression_id == CLAP_NOTE_EXPRESSION_TUNING)
        applyHeldOrDirectExpression(p, e.key, e.note_id, e.channel,
            HeldExpression::Tuning, value);
}

void applyEvent(Plugin& p, const clap_event_header_t* event)
{
    if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
    if (event->type == CLAP_EVENT_PARAM_VALUE
        && event->size >= sizeof(clap_event_param_value_t)) {
        const auto* value = reinterpret_cast<const clap_event_param_value_t*>(event);
        applyParam(p, value->param_id, value->value);
        return;
    }
    if ((event->type == CLAP_EVENT_NOTE_ON
            || event->type == CLAP_EVENT_NOTE_OFF
            || event->type == CLAP_EVENT_NOTE_CHOKE)
        && event->size >= sizeof(clap_event_note_t)) {
        const auto* note = reinterpret_cast<const clap_event_note_t*>(event);
        if (!s3g::drum_midi::accepts(p.midiReceive, note->channel)) return;
        if (event->type == CLAP_EVENT_NOTE_ON && note->velocity > 0.0) {
            startInputNote(p, note->key, static_cast<float>(note->velocity),
                note->note_id, note->channel);
        } else if (event->type == CLAP_EVENT_NOTE_CHOKE && note->key < 0) {
            clearHeldInputNotes(p, note->channel);
            p.engine.allSoundOff(note->channel);
        } else
            releaseInputNote(p, note->key, note->note_id, note->channel);
        return;
    }
    if (event->type == CLAP_EVENT_NOTE_EXPRESSION
        && event->size >= sizeof(clap_event_note_expression_t)) {
        applyNoteExpression(p,
            *reinterpret_cast<const clap_event_note_expression_t*>(event));
        return;
    }
    if (event->type != CLAP_EVENT_MIDI
        || event->size < sizeof(clap_event_midi_t)) return;
    const auto* midi = reinterpret_cast<const clap_event_midi_t*>(event);
    const uint8_t status = midi->data[0] & 0xf0u;
    const int16_t channel = midi->data[0] & 0x0fu;
    if (!s3g::drum_midi::accepts(p.midiReceive, channel)) return;
    const int key = midi->data[1] & 0x7fu;
    if (status == 0x90u && midi->data[2] != 0u) {
        startInputNote(p, key, midi->data[2] / 127.0f, -1, channel);
    } else if (status == 0x80u || (status == 0x90u && midi->data[2] == 0u))
        releaseInputNote(p, key, -1, channel);
    else if (status == 0xd0u)
        setMidiChannelPressure(p, channel, midi->data[1] / 127.0f);
    else if (status == 0xa0u)
        applyHeldOrDirectExpression(p, key, -1, channel,
            HeldExpression::Pressure, midi->data[2] / 127.0f);
    else if (status == 0xe0u) {
        const int bend = static_cast<int>(midi->data[1])
            | (static_cast<int>(midi->data[2]) << 7);
        const size_t channelIndex = static_cast<size_t>(channel);
        const float range = static_cast<float>(
            p.midiBendRangeSemitones[channelIndex])
            + static_cast<float>(p.midiBendRangeCents[channelIndex]) / 100.0f;
        setMidiChannelTuning(p, channel,
            static_cast<float>(bend - 8192) * (range / 8192.0f));
    } else if (status == 0xb0u) {
        const uint8_t controller = midi->data[1] & 0x7fu;
        const uint8_t value = midi->data[2] & 0x7fu;
        const size_t channelIndex = static_cast<size_t>(channel);
        if (controller == 1u) {
            if (usesQuNexusGenOneExpression(p))
                setMidiChannelPressure(p, channel, value / 127.0f);
            else
                p.engine.setModWheel(value / 127.0f);
        }
        else if (controller == 74u)
            setMidiChannelTimbre(p, channel, value / 127.0f);
        else if (controller == 101u)
            p.midiRpnMsb[channelIndex] = value;
        else if (controller == 100u)
            p.midiRpnLsb[channelIndex] = value;
        else if (controller == 6u && p.midiRpnMsb[channelIndex] == 0u
            && p.midiRpnLsb[channelIndex] == 0u)
            p.midiBendRangeSemitones[channelIndex] =
                std::min<uint8_t>(value, 48u);
        else if (controller == 38u && p.midiRpnMsb[channelIndex] == 0u
            && p.midiRpnLsb[channelIndex] == 0u)
            p.midiBendRangeCents[channelIndex] =
                std::min<uint8_t>(value, 99u);
        else if (controller == 120u) {
            clearHeldInputNotes(p, channel);
            p.engine.allSoundOff(channel);
        }
        else if (controller == 123u) {
            clearHeldInputNotes(p, channel);
            p.engine.noteOff(-1, -1, channel);
        }
        else if (controller == 121u) {
            setMidiChannelPressure(p, channel, 0.0f);
            setMidiChannelTimbre(p, channel, 0.0f);
            setMidiChannelTuning(p, channel, 0.0f);
            p.engine.setModWheel(0.0f);
            p.midiRpnMsb[channelIndex] = 127u;
            p.midiRpnLsb[channelIndex] = 127u;
        }
    }
}

struct TransportClock {
    bool playing = false;
    bool hasPosition = false;
    double beat = 0.0;
    double tempo = 120.0;
    double tempoIncrement = 0.0;
};

void updateTransportClock(TransportClock& clock,
    const clap_event_transport_t* transport, double fallbackBeat)
{
    clock = {};
    clock.beat = std::isfinite(fallbackBeat) ? fallbackBeat : 0.0;
    if (!transport) return;
    clock.playing = (transport->flags & CLAP_TRANSPORT_IS_PLAYING) != 0u;
    if ((transport->flags & CLAP_TRANSPORT_HAS_TEMPO) != 0u
        && std::isfinite(transport->tempo) && transport->tempo > 0.0)
        clock.tempo = transport->tempo;
    clock.tempoIncrement = std::isfinite(transport->tempo_inc)
        ? transport->tempo_inc : 0.0;
    if ((transport->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0u) {
        clock.beat = static_cast<double>(transport->song_pos_beats)
            / static_cast<double>(CLAP_BEATTIME_FACTOR);
        clock.hasPosition = std::isfinite(clock.beat);
    } else if ((transport->flags & CLAP_TRANSPORT_HAS_SECONDS_TIMELINE) != 0u) {
        clock.beat = static_cast<double>(transport->song_pos_seconds)
            / static_cast<double>(CLAP_SECTIME_FACTOR) * clock.tempo / 60.0;
        clock.hasPosition = std::isfinite(clock.beat);
    }
}

void advanceTransportClock(TransportClock& clock, double sampleRate)
{
    if (clock.playing) clock.beat += clock.tempo / (60.0 * sampleRate);
    clock.tempo = std::max(1.0, clock.tempo + clock.tempoIncrement);
}

uint32_t positiveModulo(int64_t value, uint32_t modulus)
{
    if (modulus == 0u) return 0u;
    int64_t remainder = value % static_cast<int64_t>(modulus);
    if (remainder < 0) remainder += static_cast<int64_t>(modulus);
    return static_cast<uint32_t>(remainder);
}

int64_t floorDivide(int64_t value, int64_t divisor)
{
    const int64_t quotient = value / divisor;
    const int64_t remainder = value % divisor;
    return quotient - (remainder < 0 ? 1 : 0);
}

int signedScaleSemitone(s3g::ProcessorStackScale scale, int degree)
{
    const int count = static_cast<int>(
        s3g::processorStackScaleDegreeCount(scale));
    int octave = degree / count;
    int index = degree % count;
    if (index < 0) {
        index += count;
        --octave;
    }
    return s3g::processorStackScaleSemitone(
        scale, static_cast<uint32_t>(index)) + octave * 12;
}

double arpStepBeats(s3g::ProcessorStackArpRate rate)
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

uint32_t arpSequencePosition(int64_t step,
    const LowformArpParams& arp)
{
    const uint32_t degrees = s3g::processorStackScaleDegreeCount(arp.scale);
    const uint32_t length = std::max(1u, degrees * arp.octaves);
    const uint32_t position = positiveModulo(step, length);
    switch (arp.pattern) {
    case s3g::ProcessorStackArpPattern::Down:
        return length - 1u - position;
    case s3g::ProcessorStackArpPattern::Pendulum: {
        if (length <= 1u) return 0u;
        const uint32_t cycle = length * 2u - 2u;
        const uint32_t pendulum = positiveModulo(step, cycle);
        return pendulum < length ? pendulum : cycle - pendulum;
    }
    case s3g::ProcessorStackArpPattern::Pedal:
        return positiveModulo(step, 2u) == 0u ? 0u
            : 1u + positiveModulo(floorDivide(step, 2),
                std::max(1u, length - 1u));
    case s3g::ProcessorStackArpPattern::Scramble:
        return (positiveModulo(step, length) * 5u
            + positiveModulo(floorDivide(step, 3), length)) % length;
    case s3g::ProcessorStackArpPattern::Custom:
    case s3g::ProcessorStackArpPattern::Off:
    case s3g::ProcessorStackArpPattern::Up:
    case s3g::ProcessorStackArpPattern::Count:
        return position;
    }
    return position;
}

int arpeggiatedKey(const Plugin& p, int64_t step,
    const HeldInputNote& source)
{
    const auto& arp = p.arpParams;
    const uint32_t slot = positiveModulo(step, arp.length);
    const int root = transposedInputKey(p, source.key);
    if (arp.pattern == s3g::ProcessorStackArpPattern::Custom) {
        const int degree = arp.steps[slot];
        if (degree == s3g::kProcessorStackArpRest) return -1;
        int result = root + signedScaleSemitone(arp.scale, degree)
            + 12 * arp.stepOctaves[slot];
        while (result > 127) result -= 12;
        while (result < 0) result += 12;
        return std::clamp(result, 0, 127);
    }
    const uint32_t degrees = s3g::processorStackScaleDegreeCount(arp.scale);
    const uint32_t position = arpSequencePosition(step, arp);
    int result = root + s3g::processorStackScaleSemitone(
        arp.scale, position % degrees)
        + 12 * static_cast<int>(position / degrees)
        + 12 * arp.stepOctaves[slot];
    while (result > 127) result -= 12;
    while (result < 0) result += 12;
    return std::clamp(result, 0, 127);
}

void triggerArpeggiatorStep(Plugin& p)
{
    auto* source = latestHeldInputNote(p);
    if (!source) {
        releaseArpeggiatorVoice(p);
        return;
    }
    const uint32_t slot = positiveModulo(
        p.arpState.stepIndex, p.arpParams.length);
    const int voiceKey = arpeggiatedKey(p, p.arpState.stepIndex, *source);
    const float velocity = std::clamp(source->velocity
        * p.arpParams.accents[slot], 0.0f, 1.0f);
    const bool tie = p.arpParams.gateModes[slot] != 0u;
    if (voiceKey >= 0) {
        const bool continued = p.arpState.gateOpen
            && p.arpState.currentTie
            && p.arpState.sourceAge == source->age
            && p.engine.retuneNote(p.arpState.currentKey,
                p.arpState.currentNoteId, p.arpState.currentChannel,
                voiceKey, velocity);
        if (!continued) {
            releaseArpeggiatorVoice(p);
            p.engine.noteOn(voiceKey, velocity,
                source->noteId, source->channel);
            applyHeldExpressionToVoice(p, *source, voiceKey);
        }
        p.arpState.currentKey = voiceKey;
        p.arpState.currentNoteId = source->noteId;
        p.arpState.currentChannel = source->channel;
        p.arpState.sourceAge = source->age;
        p.arpState.gateOpen = true;
        p.arpState.currentTie = tie;
    } else releaseArpeggiatorVoice(p);
    ++p.arpState.stepIndex;
    ++p.arpState.stepCount;
}

struct ArpHostPosition {
    int64_t step = 0;
    double fraction = 0.0;
};

ArpHostPosition arpHostPosition(const Plugin& p,
    const TransportClock& clock)
{
    const double position = clock.beat / arpStepBeats(p.arpParams.rate);
    const double floored = std::floor(position);
    ArpHostPosition result;
    result.step = floored <= static_cast<double>(
            std::numeric_limits<int64_t>::min())
        ? std::numeric_limits<int64_t>::min() + 1
        : (floored >= static_cast<double>(
                std::numeric_limits<int64_t>::max())
            ? std::numeric_limits<int64_t>::max() - 1
            : static_cast<int64_t>(floored));
    result.fraction = std::clamp(position - floored, 0.0, 1.0);
    return result;
}

bool isArpHostBoundary(const Plugin& p, const TransportClock& clock,
    const ArpHostPosition& position)
{
    const double beatsPerSample = clock.tempo / (60.0 * p.sampleRate);
    const double tolerance = std::max(1.0e-9,
        beatsPerSample / arpStepBeats(p.arpParams.rate) * 1.5);
    return position.fraction <= tolerance;
}

void processArpeggiator(Plugin& p, const TransportClock& clock)
{
    if (!arpeggiatorEnabled(p) || !latestHeldInputNote(p)) {
        releaseArpeggiatorVoice(p);
        return;
    }
    const bool hostClock = p.arpParams.hostSync
        && clock.playing && clock.hasPosition;
    if (hostClock != p.arpState.usedHostClock) {
        releaseArpeggiatorVoice(p);
        p.arpState.phaseSamples = 0.0;
        p.arpState.hostStep = kUnprimedArpHostStep;
        p.arpState.needsTrigger = true;
        p.arpState.usedHostClock = hostClock;
    }
    if (p.arpState.needsTrigger) {
        p.arpState.needsTrigger = false;
        p.arpState.phaseSamples = 0.0;
        if (hostClock) {
            const auto position = arpHostPosition(p, clock);
            p.arpState.hostStep = position.step;
            p.arpState.stepIndex = position.step;
            if (isArpHostBoundary(p, clock, position))
                triggerArpeggiatorStep(p);
        } else {
            p.arpState.stepIndex = 0;
            triggerArpeggiatorStep(p);
        }
        return;
    }
    if (hostClock) {
        const auto position = arpHostPosition(p, clock);
        if (position.step != p.arpState.hostStep) {
            p.arpState.hostStep = position.step;
            p.arpState.stepIndex = position.step;
            if (isArpHostBoundary(p, clock, position))
                triggerArpeggiatorStep(p);
            return;
        }
        if (p.arpState.gateOpen
            && !p.arpState.currentTie
            && position.fraction >= p.arpParams.gate)
            releaseArpeggiatorVoice(p);
        return;
    }
    const double stepSamples = std::max(1.0,
        p.sampleRate * 60.0 / std::max(1.0, clock.tempo)
            * arpStepBeats(p.arpParams.rate));
    p.arpState.phaseSamples += 1.0;
    if (p.arpState.gateOpen
        && !p.arpState.currentTie
        && p.arpState.phaseSamples >= stepSamples * p.arpParams.gate)
        releaseArpeggiatorVoice(p);
    if (p.arpState.phaseSamples >= stepSamples) {
        p.arpState.phaseSamples -= stepSamples;
        triggerArpeggiatorStep(p);
    }
}

void applyProcessEvent(Plugin& p, const clap_event_header_t* event,
    TransportClock& clock)
{
    if (event && event->space_id == CLAP_CORE_EVENT_SPACE_ID
        && event->type == CLAP_EVENT_TRANSPORT
        && event->size >= sizeof(clap_event_transport_t)) {
        updateTransportClock(clock,
            reinterpret_cast<const clap_event_transport_t*>(event), clock.beat);
        return;
    }
    applyEvent(p, event);
}

clap_process_status process(const clap_plugin_t* plugin,
    const clap_process_t* processData)
{
    auto* p = self(plugin);
    if (!processData) return CLAP_PROCESS_ERROR;
    if (p->tailChangePending.exchange(false, std::memory_order_acq_rel))
        notifyTailChanged(*p);
    serviceGuiParamEvents(*p, processData->out_events);
    TransportClock clock;
    updateTransportClock(clock, processData->transport,
        p->transportFallbackBeat);
    const auto* events = processData->in_events;
    const uint32_t eventCount = events ? events->size(events) : 0u;
    uint32_t eventIndex = 0u;
    if (processData->audio_outputs_count == 0u
        || !processData->audio_outputs) {
        while (eventIndex < eventCount)
            applyProcessEvent(*p, events->get(events, eventIndex++), clock);
        p->active = p->engine.active();
        return p->active ? CLAP_PROCESS_CONTINUE : CLAP_PROCESS_SLEEP;
    }
    const auto& output = processData->audio_outputs[0u];
    if (output.channel_count == 0u || (!output.data32 && !output.data64))
        return CLAP_PROCESS_ERROR;

    p->engine.beginBlock();
    float blockPeak = 0.0f;
    for (uint32_t sample = 0u; sample < processData->frames_count; ++sample) {
        while (eventIndex < eventCount) {
            const auto* event = events->get(events, eventIndex);
            if (!event || event->time > sample) break;
            applyProcessEvent(*p, event, clock);
            ++eventIndex;
        }
        processArpeggiator(*p, clock);
        p->engine.setTransport(clock.beat, clock.tempo, clock.playing);
        float left = 0.0f;
        float right = 0.0f;
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    {
        const s3g::clap_detail::ScopedWindowsProcessorFloatMode floatMode;
#endif
        p->engine.processFrame(left, right);
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    }
#endif
        blockPeak = std::max(blockPeak,
            std::max(std::fabs(left), std::fabs(right)));
        for (uint32_t channel = 0u; channel < output.channel_count; ++channel) {
            const float value = channel == 0u ? left
                : (channel == 1u ? right : 0.0f);
            if (output.data32 && output.data32[channel])
                output.data32[channel][sample] = value;
            if (output.data64 && output.data64[channel])
                output.data64[channel][sample] = value;
        }
        advanceTransportClock(clock, p->sampleRate);
    }
    while (eventIndex < eventCount)
        applyProcessEvent(*p, events->get(events, eventIndex++), clock);
    p->transportFallbackBeat = clock.beat;
    p->active = p->engine.active();
    for (uint32_t i = 0u; i < p->modActivity.size(); ++i)
        p->modActivity[i].store(p->active ? p->engine.modActivity(i) : 0.0f,
            std::memory_order_relaxed);
    const float oldPeak = p->outputPeak.load(std::memory_order_relaxed);
    p->outputPeak.store(p->active ? std::max(blockPeak, oldPeak * 0.84f) : 0.0f,
        std::memory_order_relaxed);
    return p->active ? CLAP_PROCESS_CONTINUE : CLAP_PROCESS_SLEEP;
}

void onMainThread(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (p->parameterInfoRescanPending.exchange(false,
            std::memory_order_acq_rel)
        && p->host && p->hostParams && p->hostParams->rescan) {
        p->hostParams->rescan(p->host,
            CLAP_PARAM_RESCAN_INFO | CLAP_PARAM_RESCAN_TEXT);
    }
}

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
        | CLAP_NOTE_DIALECT_MIDI
        | CLAP_NOTE_DIALECT_MIDI_MPE;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::strncpy(info->name, "Processor Lowform MIDI In",
        sizeof(info->name) - 1u);
    return true;
}

const clap_plugin_note_ports_t notePorts { notePortsCount, notePortsGet };

uint32_t paramsCount(const clap_plugin_t*) { return kParamCount; }

bool paramsGetInfo(const clap_plugin_t* plugin, uint32_t index,
    clap_param_info_t* info)
{
    if (!info || index >= kParamDefs.size()) return false;
    const auto& def = kParamDefs[index];
    *info = {};
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    if (def.stepped) info->flags |= CLAP_PARAM_IS_STEPPED;
    const bool bodyControl = plugin && isBodyControlParam(def.id);
    const auto engine = bodyControl
        ? currentBodyEngine(*self(plugin)) : s3g::BassBodyEngine::Pressure;
    std::strncpy(info->name, bodyControl
        ? bodyControlName(engine, def.id) : def.name,
        sizeof(info->name) - 1u);
    if (bodyControl) {
        std::snprintf(info->module, sizeof(info->module), "Body/%s",
            s3g::bassBodyEngineName(engine));
    } else {
        std::strncpy(info->module, def.module, sizeof(info->module) - 1u);
    }
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

bool isModShapeParam(clap_id id)
{
    return id == kMod1ShapeParamId || id == kMod2ShapeParamId
        || id == kMod3ShapeParamId;
}

bool isModRateParam(clap_id id)
{
    return id == kMod1RateParamId || id == kMod2RateParamId
        || id == kMod3RateParamId;
}

bool isModDepthParam(clap_id id)
{
    return id == kMod1DepthParamId || id == kMod2DepthParamId
        || id == kMod3DepthParamId
        || id == kMod1SecondaryDepthParamId
        || id == kMod2SecondaryDepthParamId
        || id == kMod3SecondaryDepthParamId
        || id == kVelocityDepthParamId || id == kPressureDepthParamId
        || id == kTimbreDepthParamId;
}

bool isModTargetParam(clap_id id)
{
    return id == kMod1TargetParamId || id == kMod2TargetParamId
        || id == kMod3TargetParamId
        || id == kMod1SecondaryTargetParamId
        || id == kMod2SecondaryTargetParamId
        || id == kMod3SecondaryTargetParamId
        || id == kVelocityTargetParamId || id == kPressureTargetParamId
        || id == kTimbreTargetParamId;
}

bool isArpPitchStepParam(clap_id id)
{
    return id >= kArpStep1ParamId && id <= kArpStep8ParamId;
}

bool isArpAccentParam(clap_id id)
{
    return id >= kArpAccent1ParamId && id <= kArpAccent8ParamId;
}

bool isArpGateModeParam(clap_id id)
{
    return id >= kArpGateMode1ParamId && id <= kArpGateMode8ParamId;
}

bool isArpOctaveStepParam(clap_id id)
{
    return id >= kArpOctave1ParamId && id <= kArpOctave8ParamId;
}

bool isLayerEnvelopeTimeParam(clap_id id)
{
    return id == kBodyDecayParamId || id == kTextureAttackParamId
        || id == kTextureDecayParamId;
}

bool paramsValueToText(const clap_plugin_t* plugin, clap_id id, double value,
    char* display, uint32_t size)
{
    if (!display || size == 0u || !paramDef(id)) return false;
    if (id == kVoiceModeParamId)
        std::snprintf(display, size, "%s", s3g::bassVoiceModeName(
            static_cast<s3g::BassVoiceMode>(std::lround(value))));
    else if (id == kFoundationWaveParamId)
        std::snprintf(display, size, "%s", s3g::bassFoundationWaveName(
            static_cast<s3g::BassFoundationWave>(std::lround(value))));
    else if (id == kFoundationOctaveParamId)
        std::snprintf(display, size, "%+.0f OCT", value);
    else if (id == kBodyEngineParamId)
        std::snprintf(display, size, "%s", s3g::bassBodyEngineName(
            static_cast<s3g::BassBodyEngine>(std::lround(value))));
    else if (isBodyControlParam(id)) {
        const auto engine = plugin
            ? currentBodyEngine(*self(plugin)) : s3g::BassBodyEngine::Pressure;
        if (engine == s3g::BassBodyEngine::Swarm
            && id == kBodyControlBParamId)
            std::snprintf(display, size, "%.1f ct", value * 40.0);
        else if (engine == s3g::BassBodyEngine::Swarm
            && id == kBodyControlCParamId)
            std::snprintf(display, size, "%u VOICES",
                1u + static_cast<uint32_t>(std::lround(value * 6.0)));
        else if (engine == s3g::BassBodyEngine::Modal
            && id == kBodyControlCParamId) {
            const double seconds = 0.080 * std::pow(43.75, value);
            if (seconds < 1.0)
                std::snprintf(display, size, "%.0f ms", seconds * 1000.0);
            else std::snprintf(display, size, "%.2g s", seconds);
        }
        else if (engine == s3g::BassBodyEngine::Pressure
            && id == kBodyControlCParamId)
            std::snprintf(display, size, "%.0f%%", 12.0 + value * 76.0);
        else if (engine == s3g::BassBodyEngine::Pressure
            && id == kBodyControlEParamId)
            std::snprintf(display, size, "%.2fx", 1.0 + value * 5.2);
        else if (engine == s3g::BassBodyEngine::Acid
            && id == kBodyControlCParamId)
            std::snprintf(display, size, "%.0f%%", 10.0 + value * 80.0);
        else if (engine == s3g::BassBodyEngine::Acid
            && id == kBodyControlEParamId)
            std::snprintf(display, size, "%+.2f OCT", -4.0 + value * 8.0);
        else if (engine == s3g::BassBodyEngine::Phase
            && id == kBodyControlBParamId)
            std::snprintf(display, size, "%.2fx",
                0.5 * std::pow(16.0, value));
        else if (engine == s3g::BassBodyEngine::Phase
            && id == kBodyControlCParamId)
            std::snprintf(display, size, "%.2f rad", value * value * 7.5);
        else if (engine == s3g::BassBodyEngine::Phase
            && id == kBodyControlEParamId)
            std::snprintf(display, size, "%.2fx", 1.0 + value * 5.5);
        else if (engine == s3g::BassBodyEngine::Throat
            && id == kBodyControlBParamId)
            std::snprintf(display, size, "%+.2f OCT",
                (value - 0.5) * 2.5);
        else if (engine == s3g::BassBodyEngine::Throat
            && id == kBodyControlEParamId)
            std::snprintf(display, size, "%.2fx", 1.0 + value * 5.0);
        else if (engine == s3g::BassBodyEngine::Sync
            && id == kBodyControlBParamId)
            std::snprintf(display, size, "%.2fx", 1.0 + value * 7.0);
        else if (engine == s3g::BassBodyEngine::Sync
            && id == kBodyControlCParamId)
            std::snprintf(display, size, "+%.2f OCT",
                value * value * 4.5);
        else if (engine == s3g::BassBodyEngine::Sync
            && id == kBodyControlDParamId) {
            const double seconds = 0.015 * std::pow(80.0, value);
            if (seconds < 1.0)
                std::snprintf(display, size, "%.0f ms", seconds * 1000.0);
            else std::snprintf(display, size, "%.2f s", seconds);
        }
        else std::snprintf(display, size, "%.0f%%", value * 100.0);
    }
    else if (id == kTextureModeParamId)
        std::snprintf(display, size, "%s", s3g::bassTextureModeName(
            static_cast<s3g::BassTextureMode>(std::lround(value))));
    else if (id == kFilterTypeParamId)
        std::snprintf(display, size, "%s", s3g::bassFilterTypeName(
            static_cast<s3g::BassFilterType>(std::lround(value))));
    else if (id == kMotionClockParamId)
        std::snprintf(display, size, "%s", s3g::bassMotionClockName(
            static_cast<s3g::BassMotionClock>(std::lround(value))));
    else if (isModShapeParam(id))
        std::snprintf(display, size, "%s", s3g::bassModShapeName(
            static_cast<s3g::BassModShape>(std::lround(value))));
    else if (isModTargetParam(id)) {
        const auto engine = plugin
            ? currentBodyEngine(*self(plugin)) : s3g::BassBodyEngine::Pressure;
        std::snprintf(display, size, "%s", bodyModTargetName(engine,
            static_cast<s3g::BassModTarget>(std::lround(value))));
    }
    else if (id == kShredCircuitParamId)
        std::snprintf(display, size, "%s", s3g::bassShredCircuitName(
            static_cast<s3g::BassShredCircuit>(std::lround(value))));
    else if (id == kMidiReceiveParamId)
        s3g::drum_midi::valueToText(value, display, size);
    else if (id == kExpressionModeParamId)
        std::snprintf(display, size, "%s", expressionModeName(
            static_cast<uint32_t>(std::clamp<long>(
                std::lround(value), 0, 1))));
    else if (id == kArpPatternParamId)
        std::snprintf(display, size, "%s",
            s3g::processorStackArpPatternName(static_cast<
                s3g::ProcessorStackArpPattern>(std::clamp<long>(
                    std::lround(value), 0,
                    s3g::kProcessorStackArpPatternCount - 1u))));
    else if (id == kArpScaleParamId)
        std::snprintf(display, size, "%s",
            s3g::processorStackScaleName(static_cast<
                s3g::ProcessorStackScale>(std::clamp<long>(
                    std::lround(value), 0,
                    s3g::kProcessorStackScaleCount - 1u))));
    else if (id == kArpSyncParamId)
        std::snprintf(display, size, "%s",
            value >= 0.5 ? "HOST SYNC" : "FREE");
    else if (id == kArpRateParamId)
        std::snprintf(display, size, "%s",
            s3g::processorStackArpRateName(static_cast<
                s3g::ProcessorStackArpRate>(std::clamp<long>(
                    std::lround(value), 0,
                    s3g::kProcessorStackArpRateCount - 1u))));
    else if (id == kArpOctavesParamId)
        std::snprintf(display, size, "%.0f OCT", value);
    else if (id == kArpLengthParamId)
        std::snprintf(display, size, "%.0f STEPS", value);
    else if (isArpPitchStepParam(id)) {
        if (static_cast<int32_t>(std::lround(value))
                == s3g::kProcessorStackArpRest)
            std::snprintf(display, size, "REST");
        else
            std::snprintf(display, size, "%+.0f", value);
    }
    else if (isArpAccentParam(id))
        std::snprintf(display, size, "%.0f%%", value * 100.0);
    else if (isArpGateModeParam(id))
        std::snprintf(display, size, "%s", value >= 0.5 ? "TIE" : "GATE");
    else if (isArpOctaveStepParam(id))
        std::snprintf(display, size, "%+.0f", value);
    else if (isModRateParam(id)) {
        const bool synced = plugin && paramValue(*self(plugin),
            kMotionClockParamId) >= 0.5;
        if (synced) {
            const uint32_t division = std::min<uint32_t>(15u,
                static_cast<uint32_t>(std::lround(value * 15.0)));
            std::snprintf(display, size, "%s",
                s3g::kBassMotionDivisionNames[division]);
        } else {
            const double hz = 0.03 * std::pow(800.0,
                std::clamp(value, 0.0, 1.0));
            std::snprintf(display, size, "%.2f Hz", hz);
        }
    } else if (isLayerEnvelopeTimeParam(id)) {
        if ((id == kBodyDecayParamId || id == kTextureDecayParamId)
            && value >= 11.999)
            std::snprintf(display, size, "HOLD");
        else if (value < 1.0)
            std::snprintf(display, size, "%.1f ms", value * 1000.0);
        else std::snprintf(display, size, "%.3g s", value);
    } else if (id == kAttackParamId || id == kDecayParamId
        || id == kReleaseParamId || id == kFilterDecayParamId) {
        if (value < 1.0) std::snprintf(display, size, "%.1f ms", value * 1000.0);
        else std::snprintf(display, size, "%.3g s", value);
    } else if (id == kGlideParamId || id == kPunchTimeParamId)
        std::snprintf(display, size, "%.1f ms", value);
    else if (id == kTransposeParamId)
        std::snprintf(display, size, "%+.0f st", value);
    else if (id == kPitchPunchParamId || id == kFilterEnvelopeParamId)
        std::snprintf(display, size, "%+.2f st", value);
    else if (id == kCutoffParamId) {
        if (value >= 1000.0) std::snprintf(display, size, "%.2f kHz", value / 1000.0);
        else std::snprintf(display, size, "%.0f Hz", value);
    } else if (id == kOutputParamId)
        std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kTiltParamId || isModDepthParam(id))
        std::snprintf(display, size, "%+.0f%%", value * 100.0);
    else
        std::snprintf(display, size, "%.0f%%", value * 100.0);
    return true;
}

bool paramsTextToValue(const clap_plugin_t* plugin, clap_id id,
    const char* display, double* value)
{
    const auto* def = paramDef(id);
    if (!display || !value || !def) return false;
    const auto bodyEngine = plugin
        ? currentBodyEngine(*self(plugin)) : s3g::BassBodyEngine::Pressure;
    if (id == kMidiReceiveParamId)
        return s3g::drum_midi::textToValue(display, value);
    if (id == kExpressionModeParamId) {
        for (uint32_t i = 0u; i < 2u; ++i) {
            if (std::strcmp(display, expressionModeName(i)) == 0) {
                *value = static_cast<double>(i);
                return true;
            }
        }
    }
    if (isArpPitchStepParam(id)
        && (std::strcmp(display, "REST") == 0
            || std::strcmp(display, "R") == 0)) {
        *value = static_cast<double>(s3g::kProcessorStackArpRest);
        return true;
    }
    if ((id == kBodyDecayParamId || id == kTextureDecayParamId)
        && std::strcmp(display, "HOLD") == 0) {
        *value = def->maximum;
        return true;
    }
    if (isArpGateModeParam(id)) {
        if (std::strcmp(display, "TIE") == 0) {
            *value = 1.0;
            return true;
        }
        if (std::strcmp(display, "GATE") == 0) {
            *value = 0.0;
            return true;
        }
    }
    if (id == kArpSyncParamId) {
        if (std::strcmp(display, "HOST SYNC") == 0) {
            *value = 1.0;
            return true;
        }
        if (std::strcmp(display, "FREE") == 0) {
            *value = 0.0;
            return true;
        }
    }
    const auto matchEnum = [&](uint32_t count, auto name) {
        for (uint32_t i = 0u; i < count; ++i) {
            if (std::strcmp(display, name(i)) == 0) {
                *value = static_cast<double>(i);
                return true;
            }
        }
        return false;
    };
    if (id == kVoiceModeParamId && matchEnum(2u, [](uint32_t i) {
            return s3g::bassVoiceModeName(static_cast<s3g::BassVoiceMode>(i)); })) return true;
    if (id == kFoundationWaveParamId && matchEnum(3u, [](uint32_t i) {
            return s3g::bassFoundationWaveName(static_cast<s3g::BassFoundationWave>(i)); })) return true;
    if (id == kBodyEngineParamId && matchEnum(8u, [](uint32_t i) {
            return s3g::bassBodyEngineName(static_cast<s3g::BassBodyEngine>(i)); })) return true;
    if (id == kTextureModeParamId && matchEnum(5u, [](uint32_t i) {
            return s3g::bassTextureModeName(static_cast<s3g::BassTextureMode>(i)); })) return true;
    if (id == kFilterTypeParamId && matchEnum(6u, [](uint32_t i) {
            return s3g::bassFilterTypeName(static_cast<s3g::BassFilterType>(i)); })) return true;
    if (id == kMotionClockParamId && matchEnum(2u, [](uint32_t i) {
            return s3g::bassMotionClockName(static_cast<s3g::BassMotionClock>(i)); })) return true;
    if (id == kArpPatternParamId
        && matchEnum(s3g::kProcessorStackArpPatternCount, [](uint32_t i) {
            return s3g::processorStackArpPatternName(static_cast<
                s3g::ProcessorStackArpPattern>(i)); })) return true;
    if (id == kArpScaleParamId
        && matchEnum(s3g::kProcessorStackScaleCount, [](uint32_t i) {
            return s3g::processorStackScaleName(static_cast<
                s3g::ProcessorStackScale>(i)); })) return true;
    if (id == kArpRateParamId
        && matchEnum(s3g::kProcessorStackArpRateCount, [](uint32_t i) {
            return s3g::processorStackArpRateName(static_cast<
                s3g::ProcessorStackArpRate>(i)); })) return true;
    if (isModShapeParam(id) && matchEnum(s3g::kBassModShapeCount, [](uint32_t i) {
            return s3g::bassModShapeName(static_cast<s3g::BassModShape>(i)); })) return true;
    if (isModTargetParam(id)
        && matchEnum(s3g::kBassModTargetCount, [bodyEngine](uint32_t i) {
            return bodyModTargetName(bodyEngine,
                static_cast<s3g::BassModTarget>(i)); })) return true;
    if (id == kShredCircuitParamId
        && matchEnum(s3g::kBassShredCircuitCount, [](uint32_t i) {
            return s3g::bassShredCircuitName(static_cast<s3g::BassShredCircuit>(i)); })) return true;
    if (isModRateParam(id) && plugin
        && paramValue(*self(plugin), kMotionClockParamId) >= 0.5) {
        for (uint32_t i = 0u; i < s3g::kBassMotionDivisionNames.size(); ++i) {
            if (std::strcmp(display, s3g::kBassMotionDivisionNames[i]) == 0) {
                *value = static_cast<double>(i) / 15.0;
                return true;
            }
        }
    }

    errno = 0;
    char* end = nullptr;
    const double parsed = std::strtod(display, &end);
    if (end == display || errno == ERANGE || !std::isfinite(parsed)) return false;
    while (*end && std::isspace(static_cast<unsigned char>(*end))) ++end;
    double converted = parsed;
    if (isBodyControlParam(id)
        && bodyEngine == s3g::BassBodyEngine::Swarm
        && id == kBodyControlBParamId)
        converted = parsed / 40.0;
    else if (isBodyControlParam(id)
        && bodyEngine == s3g::BassBodyEngine::Swarm
        && id == kBodyControlCParamId)
        converted = (parsed - 1.0) / 6.0;
    else if (isBodyControlParam(id)
        && bodyEngine == s3g::BassBodyEngine::Modal
        && id == kBodyControlCParamId) {
        double seconds = parsed;
        if (std::strncmp(end, "ms", 2u) == 0) seconds *= 0.001;
        converted = std::log(std::max(0.080, seconds) / 0.080)
            / std::log(43.75);
    }
    else if (isBodyControlParam(id)
        && bodyEngine == s3g::BassBodyEngine::Pressure
        && id == kBodyControlCParamId)
        converted = (parsed - 12.0) / 76.0;
    else if (isBodyControlParam(id)
        && bodyEngine == s3g::BassBodyEngine::Pressure
        && id == kBodyControlEParamId)
        converted = (parsed - 1.0) / 5.2;
    else if (isBodyControlParam(id)
        && bodyEngine == s3g::BassBodyEngine::Acid
        && id == kBodyControlCParamId)
        converted = (parsed - 10.0) / 80.0;
    else if (isBodyControlParam(id)
        && bodyEngine == s3g::BassBodyEngine::Acid
        && id == kBodyControlEParamId)
        converted = (parsed + 4.0) / 8.0;
    else if (isBodyControlParam(id)
        && bodyEngine == s3g::BassBodyEngine::Phase
        && id == kBodyControlBParamId)
        converted = std::log(std::max(0.5, parsed) / 0.5)
            / std::log(16.0);
    else if (isBodyControlParam(id)
        && bodyEngine == s3g::BassBodyEngine::Phase
        && id == kBodyControlCParamId)
        converted = std::sqrt(std::max(0.0, parsed) / 7.5);
    else if (isBodyControlParam(id)
        && bodyEngine == s3g::BassBodyEngine::Phase
        && id == kBodyControlEParamId)
        converted = (parsed - 1.0) / 5.5;
    else if (isBodyControlParam(id)
        && bodyEngine == s3g::BassBodyEngine::Throat
        && id == kBodyControlBParamId)
        converted = parsed / 2.5 + 0.5;
    else if (isBodyControlParam(id)
        && bodyEngine == s3g::BassBodyEngine::Throat
        && id == kBodyControlEParamId)
        converted = (parsed - 1.0) / 5.0;
    else if (isBodyControlParam(id)
        && bodyEngine == s3g::BassBodyEngine::Sync
        && id == kBodyControlBParamId)
        converted = (parsed - 1.0) / 7.0;
    else if (isBodyControlParam(id)
        && bodyEngine == s3g::BassBodyEngine::Sync
        && id == kBodyControlCParamId)
        converted = std::sqrt(std::max(0.0, parsed) / 4.5);
    else if (isBodyControlParam(id)
        && bodyEngine == s3g::BassBodyEngine::Sync
        && id == kBodyControlDParamId) {
        double seconds = parsed;
        if (std::strncmp(end, "ms", 2u) == 0) seconds *= 0.001;
        converted = std::log(std::max(0.015, seconds) / 0.015)
            / std::log(80.0);
    }
    else if (isModRateParam(id) && std::strncmp(end, "Hz", 2u) == 0)
        converted = std::log(std::max(0.03, parsed) / 0.03)
            / std::log(800.0);
    else if (*end == '%') converted *= 0.01;
    else if (std::strncmp(end, "ms", 2u) == 0
        && (id == kAttackParamId || id == kDecayParamId
            || id == kReleaseParamId || id == kFilterDecayParamId
            || isLayerEnvelopeTimeParam(id)))
        converted *= 0.001;
    else if (std::strncmp(end, "kHz", 3u) == 0 && id == kCutoffParamId)
        converted *= 1000.0;
    *value = clampValue(*def, converted);
    return true;
}

void paramsFlush(const clap_plugin_t* plugin,
    const clap_input_events_t* input, const clap_output_events_t* output)
{
    auto* p = self(plugin);
    const uint32_t count = input ? input->size(input) : 0u;
    for (uint32_t i = 0u; i < count; ++i) {
        const auto* event = input->get(input, i);
        if (event && event->space_id == CLAP_CORE_EVENT_SPACE_ID
            && event->type == CLAP_EVENT_PARAM_VALUE
            && event->size >= sizeof(clap_event_param_value_t)) {
            const auto* value = reinterpret_cast<const clap_event_param_value_t*>(event);
            applyParam(*p, value->param_id, value->value);
        }
    }
    serviceGuiParamEvents(*p, output);
}

const clap_plugin_params_t paramsExt {
    paramsCount, paramsGetInfo, paramsGetValue,
    paramsValueToText, paramsTextToValue, paramsFlush
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    SavedState state;
    const auto* p = self(plugin);
    for (uint32_t i = 0u; i < kParamCount; ++i)
        state.values[i] = paramValue(*p, kParamDefs[i].id);
    return s3g::clap_state::writeAll(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    SavedState state;
    if (!s3g::clap_state::readAll(stream, &state.header,
            sizeof(state.header))) return false;
    const bool current = state.header.magic == kStateMagic
        && state.header.version == kStateVersion
        && state.header.valueCount == kParamCount;
    const bool raveState = state.header.magic == kStateMagic
        && state.header.version == kRaveStateVersion
        && state.header.valueCount == kParamCount;
    const bool articulationState = state.header.magic == kStateMagic
        && state.header.version == kArticulationStateVersion
        && state.header.valueCount == kParamCount;
    const bool assignableMotion = state.header.magic == kStateMagic
        && state.header.version == kAssignableMotionStateVersion
        && state.header.valueCount == kAssignableMotionParamCount;
    const bool expressionState = state.header.magic == kStateMagic
        && state.header.version == kExpressionStateVersion
        && state.header.valueCount == kExpressionParamCount;
    const bool directControls = state.header.magic == kStateMagic
        && state.header.version == kDirectControlsStateVersion
        && state.header.valueCount == kPreviousParamCount;
    const bool macroState = state.header.magic == kStateMagic
        && state.header.version == kMacroStateVersion
        && state.header.valueCount == kPreviousParamCount;
    const bool modalLegacy = state.header.magic == kStateMagic
        && state.header.version == kModalLegacyStateVersion
        && state.header.valueCount == kPreviousParamCount;
    const bool legacy = state.header.magic == kStateMagic
        && state.header.version == kLegacyStateVersion
        && state.header.valueCount == kLegacyParamCount;
    if (!current && !raveState && !articulationState
        && !assignableMotion && !expressionState
        && !directControls && !macroState && !modalLegacy && !legacy)
        return false;
    for (uint32_t i = 0u; i < kParamCount; ++i)
        state.values[i] = kParamDefs[i].defaultValue;
    if (!s3g::clap_state::readAll(stream, state.values.data(),
            static_cast<size_t>(state.header.valueCount)
                * sizeof(state.values[0u]))) return false;

    if (legacy) {
        // These three slots did not exist in version 1. Start its semantic
        // migrations from the historical defaults rather than version 4's
        // already-compensated Pressure Drive default.
        state.values[kBodyControlCParamId - 1u] = 0.40;
        state.values[kBodyControlDParamId - 1u] = 0.24;
        state.values[kBodyControlEParamId - 1u] = 0.50;
    }

    if (macroState || modalLegacy || legacy) {
        // Versions 1-3 stored Weight, Motion, Edge, and Space in IDs 46-49.
        // Bake their effective values into the direct controls, then reuse
        // those stable IDs for Modal Drive, Mod Wheel Amount, Bite, and
        // Texture Width.
        const double oldWeight = std::clamp(
            state.values[kModalDriveParamId - 1u], 0.0, 1.0);
        const double oldMotion = std::clamp(
            state.values[kModWheelAmountParamId - 1u], 0.0, 1.0);
        const double oldEdge = std::clamp(
            state.values[kDynamicsBiteParamId - 1u], 0.0, 1.0);
        const double oldSpace = std::clamp(
            state.values[kTextureWidthParamId - 1u], 0.0, 1.0);
        state.values[kFoundationLevelParamId - 1u] = std::clamp(
            state.values[kFoundationLevelParamId - 1u]
                * (0.72 + oldWeight * 0.58), 0.0, 1.30);
        state.values[kBodyLevelParamId - 1u] = std::clamp(
            state.values[kBodyLevelParamId - 1u]
                * (1.16 - oldWeight * 0.32), 0.0, 1.16);
        const double effectiveWidth = std::clamp(
            state.values[kBodyWidthParamId - 1u] + oldSpace * 0.45,
            0.0, 1.0);
        state.values[kBodyWidthParamId - 1u] = effectiveWidth;
        state.values[kTextureWidthParamId - 1u] = effectiveWidth;
        const double motionScale = 0.25 + oldMotion * 1.50;
        for (const clap_id depthId : { kMod1DepthParamId,
                kMod2DepthParamId, kMod3DepthParamId }) {
            state.values[depthId - 1u] = std::clamp(
                state.values[depthId - 1u] * motionScale, -1.0, 1.0);
        }
        state.values[kFilterDriveParamId - 1u] = std::clamp(
            state.values[kFilterDriveParamId - 1u]
                + oldEdge * 0.50, 0.0, 1.0);
        state.values[kShredParamId - 1u] = std::clamp(
            state.values[kShredParamId - 1u]
                + oldEdge * 0.24, 0.0, 1.0);
        const auto oldEngine = static_cast<s3g::BassBodyEngine>(
            std::clamp<long>(std::lround(
                state.values[kBodyEngineParamId - 1u]), 0, 3));
        if (oldEngine == s3g::BassBodyEngine::Pressure) {
            state.values[kBodyControlEParamId - 1u] = std::clamp(
                state.values[kBodyControlEParamId - 1u]
                    + oldEdge * (1.8 / 5.2), 0.0, 1.0);
        } else if (oldEngine == s3g::BassBodyEngine::Acid) {
            state.values[kBodyControlDParamId - 1u] = std::clamp(
                state.values[kBodyControlDParamId - 1u]
                    + oldEdge / 3.0, 0.0, 1.0);
        }
        state.values[kModalDriveParamId - 1u] = oldEdge;
        state.values[kModWheelAmountParamId - 1u] = 0.75;
        state.values[kDynamicsBiteParamId - 1u] = oldEdge * 0.12;
    }

    // Versions 1 and 2 used Modes / Strike in the Modal slots. Convert them
    // to a useful center-to-rim Position and restrained driven Sustain while
    // approximately retaining the old modal decay time.
    if ((modalLegacy || legacy)
        && std::lround(state.values[kBodyEngineParamId - 1u])
            == static_cast<long>(s3g::BassBodyEngine::Modal)) {
        const double oldModes = std::clamp(
            state.values[kBodyControlBParamId - 1u], 0.0, 1.0);
        const double oldDecay = std::clamp(
            state.values[kBodyControlCParamId - 1u], 0.0, 1.0);
        const double oldStrike = std::clamp(
            state.values[kBodyControlDParamId - 1u], 0.0, 1.0);
        const double oldDecaySeconds = 0.055 * std::pow(90.0, oldDecay);
        state.values[kBodyControlBParamId - 1u] = 0.26 + oldModes * 0.48;
        state.values[kBodyControlCParamId - 1u] = std::clamp(
            std::log(std::max(0.080, oldDecaySeconds) / 0.080)
                / std::log(43.75), 0.0, 1.0);
        state.values[kBodyControlDParamId - 1u] = 0.10 + oldStrike * 0.30;
    }
    auto* p = self(plugin);
    p->params = s3g::LowformParams {};
    p->midiReceive = 0.0;
    p->expressionMode = 0.0;
    p->transpose = 0.0;
    p->arpParams = LowformArpParams {};
    for (uint32_t i = 0u; i < kParamCount; ++i)
        applyParam(*p, kParamDefs[i].id, state.values[i]);
    p->engine.reset();
    p->engine.setParams(p->params);
    resetMidiExpressionState(*p);
    p->active = false;
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
    for (auto& activity : p->modActivity)
        activity.store(0.0f, std::memory_order_relaxed);
    if (p->host && p->hostParams && p->hostParams->rescan)
        p->hostParams->rescan(p->host, CLAP_PARAM_RESCAN_VALUES);
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

uint32_t tailGet(const clap_plugin_t* plugin)
{
    const auto* p = self(plugin);
    const double seconds = paramValue(*p, kReleaseParamId) + 0.05
        + paramValue(*p, kShredFeedbackParamId)
            * paramValue(*p, kShredMixParamId) * 2.0;
    return static_cast<uint32_t>(std::min<double>(
        std::numeric_limits<uint32_t>::max() - 1u,
        std::ceil(seconds * p->sampleRate)));
}

const clap_plugin_tail_t tailExt { tailGet };

#if defined(__APPLE__) && !defined(S3G_ENABLE_VSTGUI_CANVAS_GUI)

constexpr clap_id kFactoryPresetMenuId = 0x7ffffff0u;

enum class GuiPage : uint8_t { Build = 0u, Motion };
enum class ArpEditLane : uint8_t { Pitch = 0u, Accent, Gate, Octave };

namespace layout = s3g::gui_layout;

struct UiRow {
    clap_id id;
    const char* label;
    GuiPage page;
    CGFloat panelX;
    CGFloat panelWidth;
    CGFloat y;
};

struct UiPanel {
    const char* title;
    GuiPage page;
    layout::Panel panel;
};

constexpr CGFloat kColumnWidth = 348.0;
constexpr layout::Canvas kCanvas {
    static_cast<double>(kGuiWidth), static_cast<double>(kGuiHeight)
};
constexpr layout::Column kColumnOne { 16.0, kColumnWidth, 75.0 };
constexpr layout::Column kColumnTwo { 376.0, kColumnWidth, 75.0 };
constexpr layout::Column kColumnThree { 736.0, kColumnWidth, 75.0 };

constexpr layout::Panel kWorkspacePanel {
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Utility,
    { 16.0, 42.0, 1068.0, 21.0 }, 36.0, 26.0, 0u
};

constexpr auto kOutputPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Output,
    kColumnOne, 75.0, 4u);
constexpr auto kFoundationPanel = layout::fittedStackPanel(
    layout::PanelRole::Engine, kOutputPanel, 8u);
constexpr auto kEnvelopePanel = layout::fittedStackPanel(
    layout::PanelRole::Envelope, kFoundationPanel, 6u);
constexpr auto kBodyPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Source,
    kColumnTwo, 75.0, 9u);
constexpr auto kTexturePanel = layout::fittedStackPanel(
    layout::PanelRole::Relationships, kBodyPanel, 5u);
constexpr auto kShapePanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::ToneShape,
    kColumnThree, 75.0, 8u);

constexpr auto kClockPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Motion,
    kColumnOne, 75.0, 1u);
constexpr auto kPerformancePanel = layout::fittedStackPanel(
    layout::PanelRole::Relationships, kClockPanel, 1u);
constexpr auto kExpressionPanel = layout::fittedStackPanel(
    layout::PanelRole::Relationships, kPerformancePanel, 6u);
constexpr auto kAuxRoutesPanel = layout::fittedStackPanel(
    layout::PanelRole::Modulation, kExpressionPanel, 6u);
constexpr auto kLayerEnvelopesPanel = layout::fittedStackPanel(
    layout::PanelRole::Envelope, kAuxRoutesPanel, 3u);
constexpr auto kModOnePanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Modulation,
    kColumnTwo, 75.0, 4u);
constexpr auto kModTwoPanel = layout::fittedStackPanel(
    layout::PanelRole::Modulation, kModOnePanel, 4u);
constexpr auto kModThreePanel = layout::fittedStackPanel(
    layout::PanelRole::Modulation, kModTwoPanel, 4u);
constexpr auto kArpeggiatorPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::EventTiming,
    kColumnThree, 75.0, 7u);
constexpr auto kArpPatternPanel = layout::stackPanel(
    layout::PanelRole::EventTiming, kArpeggiatorPanel, 132.0, 0u);

constexpr auto kAmpPanel = layout::fittedStackPanel(
    layout::PanelRole::ToneShape, kShapePanel, 1u);
constexpr auto kShredPanel = layout::fittedStackPanel(
    layout::PanelRole::Projection, kAmpPanel, 5u);
constexpr auto kDynamicsPanel = layout::fittedStackPanel(
    layout::PanelRole::Topology, kTexturePanel, 6u);
constexpr std::array kBuildFirstColumnPanels {
    kOutputPanel,
    kFoundationPanel,
    kEnvelopePanel,
};
constexpr std::array kBuildSecondColumnPanels {
    kBodyPanel,
    kTexturePanel,
    kDynamicsPanel,
};
constexpr std::array kBuildThirdColumnPanels {
    kShapePanel,
    kAmpPanel,
    kShredPanel,
};
constexpr std::array kMotionSecondColumnPanels {
    kModOnePanel,
    kModTwoPanel,
    kModThreePanel,
};
constexpr std::array kMotionFirstColumnPanels {
    kClockPanel,
    kPerformancePanel,
    kExpressionPanel,
    kAuxRoutesPanel,
    kLayerEnvelopesPanel,
};
constexpr std::array kMotionThirdColumnPanels {
    kArpeggiatorPanel,
    kArpPatternPanel,
};
constexpr std::array<UiPanel, 19u> kUiPanels {{
    { "OUTPUT + ROUTING", GuiPage::Build, kOutputPanel },
    { "FOUNDATION", GuiPage::Build, kFoundationPanel },
    { "ENVELOPE", GuiPage::Build, kEnvelopePanel },
    { "BODY ENGINE", GuiPage::Build, kBodyPanel },
    { "TEXTURE", GuiPage::Build, kTexturePanel },
    { "SHAPE", GuiPage::Build, kShapePanel },
    { "CLOCK", GuiPage::Motion, kClockPanel },
    { "PERFORMANCE", GuiPage::Motion, kPerformancePanel },
    { "EXPRESSION ROUTING", GuiPage::Motion, kExpressionPanel },
    { "SECONDARY MOD ROUTES", GuiPage::Motion, kAuxRoutesPanel },
    { "LAYER ENVELOPES", GuiPage::Motion, kLayerEnvelopesPanel },
    { "MODULATOR 1", GuiPage::Motion, kModOnePanel },
    { "MODULATOR 2", GuiPage::Motion, kModTwoPanel },
    { "MODULATOR 3", GuiPage::Motion, kModThreePanel },
    { "ARPEGGIATOR", GuiPage::Motion, kArpeggiatorPanel },
    { "ARP STEP EDITOR", GuiPage::Motion, kArpPatternPanel },
    { "AMPLIFIER", GuiPage::Build, kAmpPanel },
    { "STEREO SHRED", GuiPage::Build, kShredPanel },
    { "DYNAMICS + CEILING", GuiPage::Build, kDynamicsPanel },
}};

constexpr std::array<UiRow, kParamCount - 32u> kUiRows {{
    { kVoiceModeParamId, "VOICE MODE", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kFoundationPanel, 0u) },
    { kGlideParamId, "GLIDE", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kFoundationPanel, 1u) },
    { kFoundationWaveParamId, "WAVE", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kFoundationPanel, 2u) },
    { kFoundationOctaveParamId, "OCTAVE", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kFoundationPanel, 3u) },
    { kFoundationLevelParamId, "LEVEL", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kFoundationPanel, 4u) },
    { kPitchPunchParamId, "PITCH PUNCH", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kFoundationPanel, 5u) },
    { kPunchTimeParamId, "PUNCH TIME", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kFoundationPanel, 6u) },
    { kFoundationReturnParamId, "LOW RETURN", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kFoundationPanel, 7u) },

    { kAttackParamId, "ATTACK", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kEnvelopePanel, 0u) },
    { kDecayParamId, "DECAY", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kEnvelopePanel, 1u) },
    { kSustainParamId, "SUSTAIN", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kEnvelopePanel, 2u) },
    { kReleaseParamId, "RELEASE", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kEnvelopePanel, 3u) },
    { kFilterEnvelopeParamId, "FILTER ENV", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kEnvelopePanel, 4u) },
    { kFilterDecayParamId, "FILTER DECAY", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kEnvelopePanel, 5u) },

    { kBodyEngineParamId, "ENGINE", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kBodyPanel, 0u) },
    { kBodyLevelParamId, "LEVEL", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kBodyPanel, 1u) },
    { kBodyControlAParamId, "CONTROL A", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kBodyPanel, 2u) },
    { kBodyControlBParamId, "CONTROL B", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kBodyPanel, 3u) },
    { kBodyControlCParamId, "CONTROL C", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kBodyPanel, 4u) },
    { kBodyControlDParamId, "CONTROL D", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kBodyPanel, 5u) },
    { kBodyControlEParamId, "CONTROL E", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kBodyPanel, 6u) },
    { kModalDriveParamId, "MODAL DRIVE", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kBodyPanel, 7u) },
    { kBodyWidthParamId, "WIDTH", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kBodyPanel, 8u) },

    { kTextureModeParamId, "MODE", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kTexturePanel, 0u) },
    { kTextureLevelParamId, "LEVEL", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kTexturePanel, 1u) },
    { kTextureColorParamId, "COLOR", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kTexturePanel, 2u) },
    { kTextureTrackParamId, "PITCH TRACK", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kTexturePanel, 3u) },
    { kTextureWidthParamId, "WIDTH", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kTexturePanel, 4u) },

    { kFilterTypeParamId, "TYPE", GuiPage::Build, 736.0,
        kColumnWidth, layout::rowY(kShapePanel, 0u) },
    { kCutoffParamId, "CUTOFF", GuiPage::Build, 736.0,
        kColumnWidth, layout::rowY(kShapePanel, 1u) },
    { kResonanceParamId, "RESONANCE", GuiPage::Build, 736.0,
        kColumnWidth, layout::rowY(kShapePanel, 2u) },
    { kFilterDriveParamId, "DRIVE", GuiPage::Build, 736.0,
        kColumnWidth, layout::rowY(kShapePanel, 3u) },
    { kKeyTrackParamId, "KEY TRACK", GuiPage::Build, 736.0,
        kColumnWidth, layout::rowY(kShapePanel, 4u) },
    { kFoundationFilterParamId, "FOUND ROUTE", GuiPage::Build, 736.0,
        kColumnWidth, layout::rowY(kShapePanel, 5u) },
    { kBodyFilterParamId, "BODY ROUTE", GuiPage::Build, 736.0,
        kColumnWidth, layout::rowY(kShapePanel, 6u) },
    { kTextureFilterParamId, "TEXTURE ROUTE", GuiPage::Build, 736.0,
        kColumnWidth, layout::rowY(kShapePanel, 7u) },

    { kMotionClockParamId, "CLOCK", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kClockPanel, 0u) },
    { kModWheelAmountParamId, "MOD WHEEL", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kPerformancePanel, 0u) },

    { kVelocityTargetParamId, "VEL TARGET", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kExpressionPanel, 0u) },
    { kVelocityDepthParamId, "VEL DEPTH", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kExpressionPanel, 1u) },
    { kPressureTargetParamId, "PRESS TARGET", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kExpressionPanel, 2u) },
    { kPressureDepthParamId, "PRESS DEPTH", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kExpressionPanel, 3u) },
    { kTimbreTargetParamId, "TIMBRE TGT", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kExpressionPanel, 4u) },
    { kTimbreDepthParamId, "TIMBRE DEPTH", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kExpressionPanel, 5u) },

    { kMod1SecondaryTargetParamId, "M1 TARGET B", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kAuxRoutesPanel, 0u) },
    { kMod1SecondaryDepthParamId, "M1 DEPTH B", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kAuxRoutesPanel, 1u) },
    { kMod2SecondaryTargetParamId, "M2 TARGET B", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kAuxRoutesPanel, 2u) },
    { kMod2SecondaryDepthParamId, "M2 DEPTH B", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kAuxRoutesPanel, 3u) },
    { kMod3SecondaryTargetParamId, "M3 TARGET B", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kAuxRoutesPanel, 4u) },
    { kMod3SecondaryDepthParamId, "M3 DEPTH B", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kAuxRoutesPanel, 5u) },

    { kBodyDecayParamId, "BODY DECAY", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kLayerEnvelopesPanel, 0u) },
    { kTextureAttackParamId, "TEX ATTACK", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kLayerEnvelopesPanel, 1u) },
    { kTextureDecayParamId, "TEX DECAY", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kLayerEnvelopesPanel, 2u) },

    { kMod1ShapeParamId, "SHAPE", GuiPage::Motion, 376.0,
        kColumnWidth, layout::rowY(kModOnePanel, 0u) },
    { kMod1RateParamId, "RATE / DIV", GuiPage::Motion, 376.0,
        kColumnWidth, layout::rowY(kModOnePanel, 1u) },
    { kMod1DepthParamId, "DEPTH", GuiPage::Motion, 376.0,
        kColumnWidth, layout::rowY(kModOnePanel, 2u) },
    { kMod1TargetParamId, "TARGET", GuiPage::Motion, 376.0,
        kColumnWidth, layout::rowY(kModOnePanel, 3u) },
    { kMod2ShapeParamId, "SHAPE", GuiPage::Motion, 376.0,
        kColumnWidth, layout::rowY(kModTwoPanel, 0u) },
    { kMod2RateParamId, "RATE / DIV", GuiPage::Motion, 376.0,
        kColumnWidth, layout::rowY(kModTwoPanel, 1u) },
    { kMod2DepthParamId, "DEPTH", GuiPage::Motion, 376.0,
        kColumnWidth, layout::rowY(kModTwoPanel, 2u) },
    { kMod2TargetParamId, "TARGET", GuiPage::Motion, 376.0,
        kColumnWidth, layout::rowY(kModTwoPanel, 3u) },
    { kMod3ShapeParamId, "SHAPE", GuiPage::Motion, 376.0,
        kColumnWidth, layout::rowY(kModThreePanel, 0u) },
    { kMod3RateParamId, "RATE / DIV", GuiPage::Motion, 376.0,
        kColumnWidth, layout::rowY(kModThreePanel, 1u) },
    { kMod3DepthParamId, "DEPTH", GuiPage::Motion, 376.0,
        kColumnWidth, layout::rowY(kModThreePanel, 2u) },
    { kMod3TargetParamId, "TARGET", GuiPage::Motion, 376.0,
        kColumnWidth, layout::rowY(kModThreePanel, 3u) },

    { kArpPatternParamId, "PATTERN", GuiPage::Motion, 736.0,
        kColumnWidth, layout::rowY(kArpeggiatorPanel, 0u) },
    { kArpScaleParamId, "SCALE RULE", GuiPage::Motion, 736.0,
        kColumnWidth, layout::rowY(kArpeggiatorPanel, 1u) },
    { kArpSyncParamId, "SYNC", GuiPage::Motion, 736.0,
        kColumnWidth, layout::rowY(kArpeggiatorPanel, 2u) },
    { kArpRateParamId, "RATE", GuiPage::Motion, 736.0,
        kColumnWidth, layout::rowY(kArpeggiatorPanel, 3u) },
    { kArpOctavesParamId, "OCTAVES", GuiPage::Motion, 736.0,
        kColumnWidth, layout::rowY(kArpeggiatorPanel, 4u) },
    { kArpGateParamId, "GATE", GuiPage::Motion, 736.0,
        kColumnWidth, layout::rowY(kArpeggiatorPanel, 5u) },
    { kArpLengthParamId, "LENGTH", GuiPage::Motion, 736.0,
        kColumnWidth, layout::rowY(kArpeggiatorPanel, 6u) },

    { kTubeParamId, "TUBE", GuiPage::Build, 736.0,
        kColumnWidth, layout::rowY(kAmpPanel, 0u) },
    { kShredCircuitParamId, "CIRCUIT", GuiPage::Build, 736.0,
        kColumnWidth, layout::rowY(kShredPanel, 0u) },
    { kShredParamId, "SHRED", GuiPage::Build, 736.0,
        kColumnWidth, layout::rowY(kShredPanel, 1u) },
    { kShredFeedbackParamId, "FEEDBACK", GuiPage::Build, 736.0,
        kColumnWidth, layout::rowY(kShredPanel, 2u) },
    { kShredColorParamId, "COLOR", GuiPage::Build, 736.0,
        kColumnWidth, layout::rowY(kShredPanel, 3u) },
    { kShredMixParamId, "MIX", GuiPage::Build, 736.0,
        kColumnWidth, layout::rowY(kShredPanel, 4u) },

    { kDynamicsParamId, "DYNAMICS", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kDynamicsPanel, 0u) },
    { kDynamicsBiteParamId, "BITE", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kDynamicsPanel, 1u) },
    { kSaturationParamId, "SATURATION", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kDynamicsPanel, 2u) },
    { kClipParamId, "CLIP", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kDynamicsPanel, 3u) },
    { kTiltParamId, "TILT", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kDynamicsPanel, 4u) },
    { kMaximizerParamId, "MAXIMIZER", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kDynamicsPanel, 5u) },

    { kOutputParamId, "OUTPUT", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kOutputPanel, 0u) },
    { kMidiReceiveParamId, "MIDI RECEIVE", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kOutputPanel, 1u) },
    { kTransposeParamId, "TRANSPOSE", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kOutputPanel, 2u) },
    { kExpressionModeParamId, "EXPRESSION MODE", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kOutputPanel, 3u) },
}};

static_assert(layout::rectFitsCanvas(kWorkspacePanel.frame, kCanvas));
static_assert(layout::validateColumn(kBuildFirstColumnPanels, kCanvas));
static_assert(layout::validateColumn(
    kBuildSecondColumnPanels, kCanvas, false));
static_assert(layout::validateColumn(
    kBuildThirdColumnPanels, kCanvas, false));
static_assert(layout::validateColumn(
    kMotionFirstColumnPanels, kCanvas, false));
static_assert(layout::validateColumn(
    kMotionSecondColumnPanels, kCanvas, false));
static_assert(layout::validateColumn(
    kMotionThirdColumnPanels, kCanvas, false));
static_assert(layout::rolesFollowTemplate(
    kBuildFirstColumnPanels, layout::kProceduralEncoderTemplate, true));
static_assert(kUiRows.size() + 32u == kParamCount);

bool uiRowVisible(const UiRow& row, GuiPage page, const Plugin& p)
{
    if (row.page != page) return false;
    return row.id != kModalDriveParamId
        || currentBodyEngine(p) == s3g::BassBodyEngine::Modal;
}

CGFloat uiRowY(const UiRow& row, const Plugin& p)
{
    if (row.id == kBodyWidthParamId
        && currentBodyEngine(p) != s3g::BassBodyEngine::Modal)
        return layout::rowY(kBodyPanel, 7u);
    return row.y;
}

constexpr bool uiPanelVisible(const UiPanel& panel, GuiPage page)
{
    return panel.page == page;
}

bool isEngineMenuParam(const Plugin& p, clap_id id)
{
    const auto engine = currentBodyEngine(p);
    return engine == s3g::BassBodyEngine::Swarm
        && id == kBodyControlCParamId;
}

bool isUiMenuParam(const Plugin& p, clap_id id)
{
    return id == kVoiceModeParamId || id == kFoundationWaveParamId
        || id == kFoundationOctaveParamId || id == kBodyEngineParamId
        || id == kTextureModeParamId || id == kFilterTypeParamId
        || id == kMotionClockParamId || isModShapeParam(id)
        || isModTargetParam(id) || id == kShredCircuitParamId
        || id == kMidiReceiveParamId || id == kExpressionModeParamId
        || id == kArpPatternParamId || id == kArpScaleParamId
        || id == kArpSyncParamId || id == kArpRateParamId
        || id == kArpOctavesParamId || id == kArpLengthParamId
        || isEngineMenuParam(p, id);
}

uint32_t uiMenuItemCount(const Plugin& p, clap_id id)
{
    if (isEngineMenuParam(p, id)) return 7u;
    if (id == kVoiceModeParamId) return 2u;
    if (id == kFoundationWaveParamId) return 3u;
    if (id == kFoundationOctaveParamId) return 2u;
    if (id == kBodyEngineParamId) return 8u;
    if (id == kTextureModeParamId) return 5u;
    if (id == kFilterTypeParamId) return 6u;
    if (id == kMotionClockParamId) return 2u;
    if (isModShapeParam(id)) return s3g::kBassModShapeCount;
    if (isModTargetParam(id)) return s3g::kBassModTargetCount;
    if (id == kShredCircuitParamId) return s3g::kBassShredCircuitCount;
    if (id == kMidiReceiveParamId) return 17u;
    if (id == kExpressionModeParamId) return 2u;
    if (id == kArpPatternParamId)
        return s3g::kProcessorStackArpPatternCount;
    if (id == kArpScaleParamId) return s3g::kProcessorStackScaleCount;
    if (id == kArpSyncParamId) return 2u;
    if (id == kArpRateParamId) return s3g::kProcessorStackArpRateCount;
    if (id == kArpOctavesParamId) return 4u;
    if (id == kArpLengthParamId) return 8u;
    return 0u;
}

double uiMenuItemValue(const Plugin& p, clap_id id, uint32_t index)
{
    const uint32_t count = uiMenuItemCount(p, id);
    if (isEngineMenuParam(p, id) && count > 1u)
        return static_cast<double>(index) / static_cast<double>(count - 1u);
    if (isModTargetParam(id) && index < kBassModTargetMenuOrder.size())
        return static_cast<double>(kBassModTargetMenuOrder[index]);
    const auto* def = paramDef(id);
    return def ? def->minimum + static_cast<double>(index)
        : static_cast<double>(index);
}

int uiMenuSelectedIndex(const Plugin& p, clap_id id)
{
    if (isEngineMenuParam(p, id)) {
        return static_cast<int>(std::lround(paramValue(p, id)
            * static_cast<double>(uiMenuItemCount(p, id) - 1u)));
    }
    if (isModTargetParam(id)) {
        const auto target = static_cast<s3g::BassModTarget>(std::lround(
            paramValue(p, id)));
        const auto found = std::find(kBassModTargetMenuOrder.begin(),
            kBassModTargetMenuOrder.end(), target);
        return found == kBassModTargetMenuOrder.end() ? -1
            : static_cast<int>(std::distance(
                kBassModTargetMenuOrder.begin(), found));
    }
    const auto* def = paramDef(id);
    return static_cast<int>(std::lround(paramValue(p, id)
        - (def ? def->minimum : 0.0)));
}

bool isLogUiParam(clap_id id)
{
    return id == kCutoffParamId || id == kAttackParamId
        || id == kDecayParamId || id == kReleaseParamId
        || id == kFilterDecayParamId || id == kPunchTimeParamId
        || id == kBodyDecayParamId || id == kTextureDecayParamId;
}

double uiNormalizedValue(clap_id id, double value)
{
    const auto* def = paramDef(id);
    if (!def) return 0.0;
    if (isLogUiParam(id) && def->minimum > 0.0)
        return std::clamp(std::log(value / def->minimum)
            / std::log(def->maximum / def->minimum), 0.0, 1.0);
    return std::clamp((value - def->minimum)
        / std::max(1.0e-12, def->maximum - def->minimum), 0.0, 1.0);
}

double uiValueFromNormalized(clap_id id, double normalized)
{
    const auto* def = paramDef(id);
    if (!def) return 0.0;
    normalized = std::clamp(normalized, 0.0, 1.0);
    const double value = isLogUiParam(id)
        ? def->minimum * std::pow(def->maximum / def->minimum, normalized)
        : def->minimum + (def->maximum - def->minimum) * normalized;
    return clampValue(*def, value);
}

NSRect lowformArpPatternFieldRect()
{
    const NSRect panel = s3g::clap_gui::cocoaRect(
        kArpPatternPanel.frame);
    return NSMakeRect(panel.origin.x + 16.0, panel.origin.y + 31.0,
        panel.size.width - 32.0, 72.0);
}

NSRect lowformArpPatternStepRect(uint32_t step)
{
    const NSRect field = lowformArpPatternFieldRect();
    const CGFloat stepWidth = field.size.width / 8.0;
    return NSMakeRect(field.origin.x + stepWidth
            * static_cast<CGFloat>(std::min<uint32_t>(step, 7u)),
        field.origin.y, stepWidth, field.size.height);
}

clap_id lowformArpPatternStepParam(uint32_t step)
{
    return kArpStep1ParamId + std::min<uint32_t>(step, 7u);
}

clap_id lowformArpEditorParam(ArpEditLane lane, uint32_t step)
{
    step = std::min<uint32_t>(step, 7u);
    switch (lane) {
    case ArpEditLane::Pitch: return kArpStep1ParamId + step;
    case ArpEditLane::Accent: return kArpAccent1ParamId + step;
    case ArpEditLane::Gate: return kArpGateMode1ParamId + step;
    case ArpEditLane::Octave: return kArpOctave1ParamId + step;
    }
    return kArpStep1ParamId + step;
}

NSRect lowformArpLaneButtonRect(uint32_t lane)
{
    const NSRect panel = s3g::clap_gui::cocoaRect(kArpPatternPanel.frame);
    return NSMakeRect(panel.origin.x + 126.0 + lane * 52.0,
        panel.origin.y + 3.0, 48.0, 17.0);
}

int lowformArpPatternStepAtX(CGFloat x)
{
    const NSRect field = lowformArpPatternFieldRect();
    if (x < NSMinX(field) || x > NSMaxX(field)) return -1;
    const CGFloat normalized = std::clamp(
        (x - NSMinX(field)) / field.size.width,
        static_cast<CGFloat>(0.0), static_cast<CGFloat>(0.999999));
    return static_cast<int>(std::floor(normalized * 8.0));
}

void drawLowformArpPattern(const Plugin& plugin, ArpEditLane lane,
    int dragStep, NSDictionary* valueAttrs,
    const s3g::clap_gui::Style& style)
{
    constexpr std::array<const char*, 4u> laneNames {{
        "PITCH", "ACCENT", "GATE", "OCT" }};
    const NSRect panel = s3g::clap_gui::cocoaRect(
        kArpPatternPanel.frame);
    for (uint32_t i = 0u; i < laneNames.size(); ++i)
        s3g::clap_gui::drawToolboxHeaderButton(
            lowformArpLaneButtonRect(i), panel,
            [NSString stringWithUTF8String:laneNames[i]],
            static_cast<uint32_t>(lane) == i, valueAttrs, style);

    const NSRect field = lowformArpPatternFieldRect();
    [style.strip setFill];
    NSRectFill(field);
    [style.grid setStroke];
    NSFrameRect(field);

    const uint32_t length = std::clamp<uint32_t>(static_cast<uint32_t>(
        std::lround(paramValue(plugin, kArpLengthParamId))), 1u, 8u);
    for (uint32_t step = 0u; step < 8u; ++step) {
        const NSRect cell = lowformArpPatternStepRect(step);
        const clap_id id = lowformArpEditorParam(lane, step);
        const double value = paramValue(plugin, id);
        const bool rest = lane == ArpEditLane::Pitch
            && static_cast<int32_t>(std::lround(value))
            == s3g::kProcessorStackArpRest;
        const CGFloat rawValueY = NSMaxY(field) - static_cast<CGFloat>(
            uiNormalizedValue(id, value)) * field.size.height;
        const CGFloat valueY = std::clamp(rawValueY,
            NSMinY(field) + 1.0, NSMaxY(field) - 1.0);
        const bool selected = dragStep == static_cast<int>(step);
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
        char valueText[32] {};
        paramsValueToText(&plugin.plugin, id, value,
            valueText, sizeof(valueText));
        NSString* stepText = [NSString stringWithFormat:@"%u %s",
            step + 1u, valueText];
        s3g::clap_gui::drawCenteredTextToFit(stepText,
            NSMakeRect(cell.origin.x + 2.0, panel.origin.y + 108.0,
                cell.size.width - 4.0, 14.0), valueAttrs);
    }
}

void drawLowformModActivity(const Plugin& plugin, uint32_t index,
    const layout::Panel& modPanel, const s3g::clap_gui::Style& style)
{
    const NSRect panel = s3g::clap_gui::cocoaRect(modPanel.frame);
    const NSRect meter = NSMakeRect(NSMaxX(panel) - 76.0,
        panel.origin.y + 10.0, 60.0, 4.0);
    [style.strip setFill];
    NSRectFill(meter);
    [style.grid setStroke];
    NSFrameRect(meter);
    const CGFloat center = NSMidX(meter);
    const CGFloat amount = static_cast<CGFloat>(std::clamp(
        plugin.modActivity[index].load(std::memory_order_relaxed),
        -1.0f, 1.0f)) * (meter.size.width * 0.5 - 1.0);
    [style.accent setFill];
    NSRectFill(NSMakeRect(std::min(center, center + amount),
        meter.origin.y + 1.0, std::fabs(amount), meter.size.height - 2.0));
    [style.text setFill];
    NSRectFill(NSMakeRect(center, meter.origin.y, 1.0, meter.size.height));
}

int factoryPresetIndex(const Plugin& p)
{
    for (uint32_t preset = 0u;
         preset < s3g::kLowformFactoryPresetCount; ++preset) {
        const auto expected = valuesFromParams(
            s3g::lowformFactoryPreset(preset), p.midiReceive,
            p.expressionMode, p.transpose, p.arpParams);
        bool match = true;
        for (uint32_t i = 0u; i < kParamCount; ++i) {
            if (isPresetPreservedParam(kParamDefs[i].id)) continue;
            const double actual = paramValue(p, kParamDefs[i].id);
            if (std::fabs(actual - expected[i]) > 1.0e-5) {
                match = false;
                break;
            }
        }
        if (match) return static_cast<int>(preset);
    }
    return -1;
}

float randomUnit(uint32_t& state)
{
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return static_cast<float>(state & 0x00ffffffu) / 16777215.0f;
}

s3g::LowformParams safeRandomParams(const Plugin& plugin,
    uint32_t seed)
{
    s3g::LowformParams p;
    p.outputGainDb = static_cast<float>(paramValue(plugin, kOutputParamId));
    p.voiceMode = randomUnit(seed) < 0.28f
        ? static_cast<float>(s3g::BassVoiceMode::Mono)
        : static_cast<float>(s3g::BassVoiceMode::Poly);
    p.glideMs = randomUnit(seed) * 240.0f;
    p.foundationWave = std::floor(randomUnit(seed) * 3.0f);
    p.foundationOctave = randomUnit(seed) < 0.18f ? -1.0f : 0.0f;
    p.foundationLevel = 0.72f + randomUnit(seed) * 0.48f;
    p.pitchPunchSemitones = randomUnit(seed) < 0.55f ? 0.0f
        : -3.0f + randomUnit(seed) * 12.0f;
    p.punchTimeMs = 18.0f + randomUnit(seed) * 150.0f;
    p.foundationReturn = 0.58f + randomUnit(seed) * 0.40f;
    p.bodyEngine = std::floor(randomUnit(seed) * 8.0f);
    p.bodyLevel = 0.38f + randomUnit(seed) * 0.67f;
    p.bodyControlA = randomUnit(seed);
    p.bodyControlB = 0.16f + randomUnit(seed) * 0.76f;
    p.bodyControlC = randomUnit(seed);
    p.bodyControlD = randomUnit(seed);
    p.bodyControlE = randomUnit(seed);
    p.bodyWidth = randomUnit(seed) * 0.72f;
    p.textureMode = randomUnit(seed) < 0.32f ? 0.0f
        : 1.0f + std::floor(randomUnit(seed) * 4.0f);
    p.textureLevel = p.textureMode == 0.0f ? 0.0f
        : 0.08f + randomUnit(seed) * 0.40f;
    p.textureColor = 0.15f + randomUnit(seed) * 0.75f;
    p.textureTrack = 0.35f + randomUnit(seed) * 0.65f;
    p.textureWidth = randomUnit(seed);
    p.filterType = 1.0f + std::floor(randomUnit(seed) * 5.0f);
    p.cutoffHz = std::pow(2.0f, std::log2(180.0f)
        + randomUnit(seed) * (std::log2(5600.0f) - std::log2(180.0f)));
    p.resonance = randomUnit(seed) * 0.48f;
    p.filterDrive = randomUnit(seed) * 0.52f;
    p.keyTrack = randomUnit(seed) * 0.82f;
    p.foundationFilter = randomUnit(seed) * 0.32f;
    p.bodyFilter = 0.58f + randomUnit(seed) * 0.42f;
    p.textureFilter = 0.48f + randomUnit(seed) * 0.52f;
    p.attackSeconds = std::pow(10.0f, -3.2f + randomUnit(seed) * 2.4f);
    p.decaySeconds = 0.04f + randomUnit(seed) * 0.80f;
    p.sustain = 0.42f + randomUnit(seed) * 0.58f;
    p.releaseSeconds = 0.06f + randomUnit(seed) * 1.2f;
    p.filterEnvelopeOctaves = -1.0f + randomUnit(seed) * 4.8f;
    p.filterDecaySeconds = 0.05f + randomUnit(seed) * 0.85f;
    // Clock is a performance preference, not a sound-design dice result.
    p.motionClock = static_cast<float>(paramValue(
        plugin, kMotionClockParamId));
    for (auto& mod : p.mods) {
        if (randomUnit(seed) < 0.30f) {
            mod.shape = 0.0f;
            mod.depth = 0.0f;
            mod.target = 0.0f;
            mod.secondaryDepth = 0.0f;
            mod.secondaryTarget = 0.0f;
        } else {
            mod.shape = 1.0f + std::floor(randomUnit(seed)
                * static_cast<float>(s3g::kBassModShapeCount - 1u));
            mod.rate = randomUnit(seed);
            mod.depth = -0.52f + randomUnit(seed) * 1.04f;
            mod.target = 1.0f + std::floor(randomUnit(seed)
                * static_cast<float>(s3g::kBassModTargetCount - 1u));
            if (randomUnit(seed) < 0.42f) {
                mod.secondaryDepth = -0.36f + randomUnit(seed) * 0.72f;
                mod.secondaryTarget = 1.0f + std::floor(randomUnit(seed)
                    * static_cast<float>(s3g::kBassModTargetCount - 1u));
            } else {
                mod.secondaryDepth = 0.0f;
                mod.secondaryTarget = 0.0f;
            }
        }
    }
    p.bodyDecaySeconds = randomUnit(seed) < 0.48f ? 12.0f
        : 0.08f + randomUnit(seed) * 2.4f;
    p.textureAttackSeconds = randomUnit(seed) < 0.62f ? 0.0f
        : randomUnit(seed) * 0.75f;
    p.textureDecaySeconds = randomUnit(seed) < 0.48f ? 12.0f
        : 0.06f + randomUnit(seed) * 2.8f;
    p.modalDrive = randomUnit(seed) * 0.72f;
    p.modWheelAmount = randomUnit(seed);
    p.dynamicsBite = randomUnit(seed) * 0.30f;
    p.tube = randomUnit(seed) * 0.62f;
    p.shred = randomUnit(seed) < 0.48f ? 0.0f
        : 0.12f + randomUnit(seed) * 0.56f;
    p.shredCircuit = p.shred > 0.0f
        ? std::floor(randomUnit(seed) * s3g::kBassShredCircuitCount) : 0.0f;
    p.shredFeedback = p.shred > 0.0f ? randomUnit(seed) * 0.36f : 0.0f;
    p.shredColor = 0.22f + randomUnit(seed) * 0.62f;
    p.shredMix = p.shred > 0.0f ? 0.12f + randomUnit(seed) * 0.46f : 0.0f;
    p.dynamics = 0.12f + randomUnit(seed) * 0.52f;
    p.saturation = randomUnit(seed) * 0.38f;
    p.clip = randomUnit(seed) < 0.75f ? 0.0f : randomUnit(seed) * 0.30f;
    p.tilt = -0.35f + randomUnit(seed) * 0.70f;
    p.maximizer = 0.12f + randomUnit(seed) * 0.48f;
    return p;
}

} // namespace

@interface S3GLowformView : NSView {
    void* _plugin;
    int _dragParam;
    int _dragPatternStep;
    ArpEditLane _arpEditLane;
    int _factoryPresetIndex;
    clap_id _openMenu;
    int _hoverMenuItem;
    uint32_t _menuItemCount;
    uint64_t _observedParamRevision;
    GuiPage _page;
    NSTimer* _timer;
    char _presetName[64];
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)applyFactoryPreset:(int)index;
- (void)markCustomPreset;
- (int)workspacePage;
- (NSRect)openMenuRect;
- (void)drawOpenMenu:(NSDictionary*)attrs
    style:(const s3g::clap_gui::Style&)style;
- (void)updateDraggedPattern:(NSPoint)point;
@end

@implementation S3GLowformView

- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (!self) return nil;
    _plugin = plugin;
    _dragParam = -1;
    _dragPatternStep = -1;
    _arpEditLane = ArpEditLane::Pitch;
    _openMenu = CLAP_INVALID_ID;
    _hoverMenuItem = -1;
    _menuItemCount = 0u;
    _page = GuiPage::Build;
    _timer = nil;
    auto* p = static_cast<Plugin*>(_plugin);
    _observedParamRevision = p ? p->parameterRevision.load(
        std::memory_order_acquire) : 0u;
    _factoryPresetIndex = p ? factoryPresetIndex(*p) : -1;
    std::snprintf(_presetName, sizeof(_presetName), "%s",
        _factoryPresetIndex >= 0
            ? s3g::lowformFactoryPresetInfo(
                static_cast<uint32_t>(_factoryPresetIndex)).name
            : "CUSTOM");
    return self;
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
    _timer = [NSTimer timerWithTimeInterval:1.0 / 30.0
        target:self selector:@selector(refresh:) userInfo:nil repeats:YES];
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
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    index = std::clamp(index, 0,
        static_cast<int>(s3g::kLowformFactoryPresetCount - 1u));
    if (!queueGuiParams(*p, s3g::lowformFactoryPreset(
            static_cast<uint32_t>(index)))) {
        NSBeep();
        return;
    }
    _factoryPresetIndex = index;
    std::snprintf(_presetName, sizeof(_presetName), "%s",
        s3g::lowformFactoryPresetInfo(
            static_cast<uint32_t>(index)).name);
    [self setNeedsDisplay:YES];
}

- (void)markCustomPreset
{
    _factoryPresetIndex = -1;
    std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
}

- (int)workspacePage
{
    return static_cast<int>(_page);
}

- (NSRect)openMenuRect
{
    NSRect anchor = NSZeroRect;
    if (_openMenu == kFactoryPresetMenuId) {
        anchor = s3g::clap_gui::cocoaRect(
            s3g::clap_gui::encoderTitleBand(kGuiWidth, kGuiHeight).presetMenu);
    } else {
        auto* p = static_cast<Plugin*>(_plugin);
        if (!p) return NSZeroRect;
        const auto row = std::find_if(kUiRows.begin(), kUiRows.end(),
            [=](const UiRow& candidate) {
                return candidate.id == _openMenu
                    && uiRowVisible(candidate, _page, *p);
            });
        if (row == kUiRows.end()) return NSZeroRect;
        anchor = NSMakeRect(layout::processorControlX(row->panelX),
            uiRowY(*row, *p) - 1.0,
            layout::processorMenuWidth(row->panelWidth), 15.0);
    }
    const bool multiColumnTargets = isModTargetParam(_openMenu);
    const uint32_t rows = multiColumnTargets
        ? s3g::clap_gui::multiColumnMenuRows(
            _menuItemCount, kModTargetMenuColumns)
        : _menuItemCount;
    const CGFloat height = 18.0 * rows;
    const CGFloat width = anchor.size.width
        * (multiColumnTargets ? kModTargetMenuColumns : 1u);
    CGFloat y = NSMaxY(anchor) + 2.0;
    if (y + height > kGuiHeight) y = anchor.origin.y - 2.0 - height;
    CGFloat x = anchor.origin.x;
    if (x + width > kGuiWidth - 16.0) x = kGuiWidth - 16.0 - width;
    return NSMakeRect(x, y, width, height);
}

- (void)drawOpenMenu:(NSDictionary*)attrs
    style:(const s3g::clap_gui::Style&)style
{
    if (_openMenu == CLAP_INVALID_ID || _menuItemCount == 0u) return;
    NSString* items[32] {};
    const uint32_t count = std::min<uint32_t>(_menuItemCount, 32u);
    auto* p = static_cast<Plugin*>(_plugin);
    if (_openMenu == kFactoryPresetMenuId) {
        for (uint32_t i = 0u; i < count; ++i)
            items[i] = [NSString stringWithUTF8String:
                s3g::lowformFactoryPresetInfo(i).name];
    } else if (_openMenu == kFoundationOctaveParamId) {
        items[0] = @"-1 OCT";
        items[1] = @"+0 OCT";
    } else {
        for (uint32_t i = 0u; i < count; ++i) {
            const double value = uiMenuItemValue(*p, _openMenu, i);
            char text[64] {};
            paramsValueToText(&p->plugin, _openMenu, value,
                text, sizeof(text));
            items[i] = [NSString stringWithUTF8String:text];
        }
    }
    const int selected = _openMenu == kFactoryPresetMenuId
        ? _factoryPresetIndex
        : uiMenuSelectedIndex(*p, _openMenu);
    if (isModTargetParam(_openMenu)) {
        s3g::clap_gui::drawMultiColumnDropdownMenu(
            [self openMenuRect], 18.0, items, count,
            kModTargetMenuColumns, selected, _hoverMenuItem, attrs, style);
    } else {
        s3g::clap_gui::drawDropdownMenu([self openMenuRect], 18.0,
            items, count, selected, _hoverMenuItem, attrs, style);
    }
}

- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    auto* p = static_cast<Plugin*>(_plugin);
    if (p) {
        const uint64_t revision = p->parameterRevision.load(
            std::memory_order_acquire);
        if (revision != _observedParamRevision) {
            _observedParamRevision = revision;
            _factoryPresetIndex = factoryPresetIndex(*p);
            std::snprintf(_presetName, sizeof(_presetName), "%s",
                _factoryPresetIndex >= 0
                    ? s3g::lowformFactoryPresetInfo(
                        static_cast<uint32_t>(_factoryPresetIndex)).name
                    : "CUSTOM");
        }
    }
    if (![self isHidden] && p && s3g::clap_support::hostAppIsActive())
        [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    s3g::clap_gui::Style style;
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSDictionary* titleAttrs = s3g::clap_gui::softTitleAttrs();
    NSDictionary* labelAttrs = s3g::clap_gui::softLabelAttrs();
    NSDictionary* valueAttrs = s3g::clap_gui::softValueAttrs();
    const auto titleBand = s3g::clap_gui::encoderTitleBand(
        kGuiWidth, kGuiHeight);
    s3g::clap_gui::drawEncoderTitleBand(@"s3g PROCESSOR LOWFORM 2",
        [NSString stringWithUTF8String:_presetName],
        s3g::clap_gui::peakDbText(
            p->outputPeak.load(std::memory_order_relaxed)),
        titleBand, titleAttrs, labelAttrs, valueAttrs, style);

    s3g::clap_gui::drawPanelFrame(kWorkspacePanel, style);
    s3g::clap_gui::drawPanelHeader(@"WORKSPACE", true, kWorkspacePanel,
        labelAttrs, style);
    const NSRect header = s3g::clap_gui::cocoaRect(kWorkspacePanel.frame);
    constexpr std::array<const char*, 2u> names {{ "BUILD", "MOTION" }};
    for (uint32_t i = 0u; i < names.size(); ++i) {
        const NSRect button = NSMakeRect(904.0 + i * 82.0, 44.0, 74.0, 17.0);
        s3g::clap_gui::drawToolboxHeaderButton(button, header,
            [NSString stringWithUTF8String:names[i]],
            static_cast<uint32_t>(_page) == i, valueAttrs, style);
    }

    for (const auto& panel : kUiPanels) {
        if (!uiPanelVisible(panel, _page)) continue;
        s3g::clap_gui::drawPanelFrame(panel.panel, style);
        NSString* panelTitle = panel.panel.frame.x == kBodyPanel.frame.x
                && panel.panel.frame.y == kBodyPanel.frame.y
                && panel.page == GuiPage::Build
            ? [NSString stringWithFormat:@"%s ENGINE",
                s3g::bassBodyEngineName(currentBodyEngine(*p))]
            : [NSString stringWithUTF8String:panel.title];
        s3g::clap_gui::drawPanelHeader(panelTitle, true, panel.panel,
            labelAttrs, style);
    }
    for (const auto& row : kUiRows) {
        if (!uiRowVisible(row, _page, *p)) continue;
        const double value = paramValue(*p, row.id);
        char text[64] {};
        paramsValueToText(&p->plugin, row.id, value, text, sizeof(text));
        const bool bodyControl = isBodyControlParam(row.id);
        NSString* label = [NSString stringWithUTF8String:bodyControl
            ? bodyControlName(currentBodyEngine(*p), row.id) : row.label];
        if (bodyControl) label = [label uppercaseString];
        NSString* display = [NSString stringWithUTF8String:text];
        if (isUiMenuParam(*p, row.id))
            s3g::clap_gui::drawProcessorMenu(label, display, row.y,
                row.panelX, row.panelWidth, labelAttrs, valueAttrs, style);
        else
            s3g::clap_gui::drawProcessorSlider(label, display,
                static_cast<CGFloat>(uiNormalizedValue(row.id, value)),
                uiRowY(row, *p), row.panelX, row.panelWidth,
                labelAttrs, valueAttrs, style);
    }
    if (_page == GuiPage::Motion) {
        drawLowformModActivity(*p, 0u, kModOnePanel, style);
        drawLowformModActivity(*p, 1u, kModTwoPanel, style);
        drawLowformModActivity(*p, 2u, kModThreePanel, style);
        drawLowformArpPattern(*p, _arpEditLane,
            _dragPatternStep, valueAttrs, style);
    }
    [self drawOpenMenu:valueAttrs style:style];
}

- (void)updateDraggedParam:(NSPoint)point
{
    if (_dragParam <= 0) return;
    const clap_id id = static_cast<clap_id>(_dragParam);
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    const auto row = std::find_if(kUiRows.begin(), kUiRows.end(),
        [=](const UiRow& candidate) {
            return candidate.id == id && uiRowVisible(candidate, _page, *p);
        });
    if (row == kUiRows.end()) return;
    const double x = layout::processorControlX(row->panelX);
    const double width = layout::processorTrackWidth(row->panelWidth);
    const double normalized = std::clamp((point.x - x) / width, 0.0, 1.0);
    queueGuiParamValue(*p, id, uiValueFromNormalized(id, normalized));
    [self setNeedsDisplay:YES];
}

- (void)updateDraggedPattern:(NSPoint)point
{
    const int nextStep = lowformArpPatternStepAtX(point.x);
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    if (nextStep >= 0 && nextStep != _dragPatternStep) {
        if (_dragPatternStep >= 0)
            queueGuiParamGestureEnd(*p, lowformArpEditorParam(
                _arpEditLane, static_cast<uint32_t>(_dragPatternStep)));
        _dragPatternStep = nextStep;
        queueGuiParamGestureBegin(*p, lowformArpEditorParam(
            _arpEditLane, static_cast<uint32_t>(_dragPatternStep)));
    }
    if (_dragPatternStep < 0) return;
    const NSRect field = lowformArpPatternFieldRect();
    const double normalized = std::clamp(
        (NSMaxY(field) - point.y) / field.size.height, 0.0, 1.0);
    const clap_id id = lowformArpEditorParam(_arpEditLane,
        static_cast<uint32_t>(_dragPatternStep));
    const double nextValue = uiValueFromNormalized(id, normalized);
    // The graph is stepped. Do not flood the GUI/audio queue with hundreds
    // of identical mouse-drag values while transport is stopped.
    if (paramValue(*p, id) != nextValue)
        queueGuiParamValue(*p, id, nextValue);
    [self setNeedsDisplay:YES];
}

- (void)rightMouseDown:(NSEvent*)event
{
    if (_page != GuiPage::Motion) {
        [super rightMouseDown:event];
        return;
    }
    const NSPoint point = [self convertPoint:event.locationInWindow
        fromView:nil];
    if (!NSPointInRect(point, lowformArpPatternFieldRect())) {
        [super rightMouseDown:event];
        return;
    }
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    const int step = lowformArpPatternStepAtX(point.x);
    if (step < 0) return;
    queueGuiParamGesture(*p,
        lowformArpPatternStepParam(static_cast<uint32_t>(step)),
        static_cast<double>(s3g::kProcessorStackArpRest));
    _dragPatternStep = -1;
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    const auto titleBand = s3g::clap_gui::encoderTitleBand(
        kGuiWidth, kGuiHeight);
    if (_openMenu != CLAP_INVALID_ID) {
        const int hit = isModTargetParam(_openMenu)
            ? s3g::clap_gui::multiColumnDropdownHitIndex(
                point, [self openMenuRect], 18.0, _menuItemCount,
                kModTargetMenuColumns)
            : s3g::clap_gui::dropdownHitIndex(
                point, [self openMenuRect], 18.0, _menuItemCount);
        if (hit >= 0) {
            if (_openMenu == kFactoryPresetMenuId) {
                [self applyFactoryPreset:hit];
            } else {
                const double value = uiMenuItemValue(
                    *p, _openMenu, static_cast<uint32_t>(hit));
                queueGuiParamGesture(*p, _openMenu, value);
                if (_openMenu != kOutputParamId
                    && !isPresetPreservedParam(_openMenu))
                    [self markCustomPreset];
            }
        }
        _openMenu = CLAP_INVALID_ID;
        _hoverMenuItem = -1;
        _menuItemCount = 0u;
        [self setNeedsDisplay:YES];
        return;
    }
    for (uint32_t i = 0u; i < 2u; ++i) {
        const NSRect button = NSMakeRect(904.0 + i * 82.0, 44.0, 74.0, 17.0);
        if (NSPointInRect(point, button)) {
            _page = static_cast<GuiPage>(i);
            [self setNeedsDisplay:YES];
            return;
        }
    }
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.presetMenu))) {
        _openMenu = kFactoryPresetMenuId;
        _hoverMenuItem = -1;
        _menuItemCount = s3g::kLowformFactoryPresetCount;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.loadButton))) {
        NSString* name = nil;
        if (s3g::clap_gui::loadPluginStatePresetPreservingParam(
                &p->plugin, @"Processor Lowform", kOutputParamId, &name)) {
            [self markCustomPreset];
            std::snprintf(_presetName, sizeof(_presetName), "%s",
                name ? [name UTF8String] : "CUSTOM");
        } else NSBeep();
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.saveButton))) {
        NSString* name = nil;
        if (s3g::clap_gui::savePluginStatePreset(
                &p->plugin, @"Processor Lowform", &name))
            std::snprintf(_presetName, sizeof(_presetName), "%s",
                name ? [name UTF8String] : "CUSTOM");
        else NSBeep();
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.randomButton))) {
        if (queueGuiParams(*p, safeRandomParams(*p, arc4random())))
            [self markCustomPreset];
        else NSBeep();
        [self setNeedsDisplay:YES];
        return;
    }
    if (_page == GuiPage::Motion) {
        for (uint32_t lane = 0u; lane < 4u; ++lane) {
            if (NSPointInRect(point, lowformArpLaneButtonRect(lane))) {
                _arpEditLane = static_cast<ArpEditLane>(lane);
                _dragPatternStep = -1;
                [self setNeedsDisplay:YES];
                return;
            }
        }
    }
    if (_page == GuiPage::Motion
        && NSPointInRect(point, lowformArpPatternFieldRect())) {
        const int step = lowformArpPatternStepAtX(point.x);
        if (step < 0) return;
        const clap_id id = lowformArpEditorParam(_arpEditLane,
            static_cast<uint32_t>(step));
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
                event, &p->plugin, id, &defaultValue)) {
            queueGuiParamGesture(*p, id, defaultValue);
            _dragPatternStep = -1;
        } else {
            _dragPatternStep = step;
            queueGuiParamGestureBegin(*p, id);
            [self updateDraggedPattern:point];
        }
        [self setNeedsDisplay:YES];
        return;
    }
    for (const auto& row : kUiRows) {
        if (!uiRowVisible(row, _page, *p)) continue;
        const CGFloat rowY = uiRowY(row, *p);
        const NSRect hit = NSMakeRect(
            row.panelX + layout::kStandardMetrics.hitInset, rowY - 9.0,
            row.panelWidth - layout::kStandardMetrics.hitInset * 2.0,
            layout::kStandardMetrics.hitHeight);
        if (!NSPointInRect(point, hit)) continue;
        if (isUiMenuParam(*p, row.id)) {
            _openMenu = row.id;
            _hoverMenuItem = -1;
            _menuItemCount = uiMenuItemCount(*p, row.id);
            [self setNeedsDisplay:YES];
            return;
        }
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
                event, &p->plugin, row.id, &defaultValue)) {
            queueGuiParamGesture(*p, row.id, defaultValue);
            _dragParam = -1;
        } else {
            _dragParam = static_cast<int>(row.id);
            queueGuiParamGestureBegin(*p, row.id);
            [self updateDraggedParam:point];
        }
        if (row.id != kOutputParamId
            && !isPresetPreservedParam(row.id))
            [self markCustomPreset];
        return;
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    if (_dragPatternStep >= 0)
        [self updateDraggedPattern:[self convertPoint:
            [event locationInWindow] fromView:nil]];
    else if (_dragParam > 0)
        [self updateDraggedParam:[self convertPoint:
            [event locationInWindow] fromView:nil]];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    if (_dragParam > 0)
        queueGuiParamGestureEnd(*static_cast<Plugin*>(_plugin),
            static_cast<clap_id>(_dragParam));
    if (_dragPatternStep >= 0)
        queueGuiParamGestureEnd(*static_cast<Plugin*>(_plugin),
            lowformArpEditorParam(_arpEditLane,
                static_cast<uint32_t>(_dragPatternStep)));
    _dragParam = -1;
    _dragPatternStep = -1;
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
    const int hover = isModTargetParam(_openMenu)
        ? s3g::clap_gui::multiColumnDropdownHitIndex(
            point, [self openMenuRect], 18.0, _menuItemCount,
            kModTargetMenuColumns)
        : s3g::clap_gui::dropdownHitIndex(
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
    auto* p = self(plugin);
    if (p->guiView) return true;
    p->guiView = [[S3GLowformView alloc] initWithPlugin:p];
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
    if (!p || !p->guiView) return;
    p->guiVisible = false;
    [static_cast<S3GLowformView*>(p->guiView) stopRefreshTimer];
    s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView);
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
    auto* p = self(plugin);
    return s3g::clap_gui::setResponsiveViewportParent(
        p->guiViewport, static_cast<NSView*>(window->cocoa), p->host);
}

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}

bool guiShow(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(
            p->guiViewport, false)) return false;
    p->guiVisible = true;
    [static_cast<S3GLowformView*>(p->guiView) startRefreshTimer];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    p->guiVisible = false;
    [static_cast<S3GLowformView*>(p->guiView) stopRefreshTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true);
}

const clap_plugin_gui_t guiExt {
    guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy,
    guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints,
    guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient,
    guiSuggestTitle, guiShow, guiHide
};

#endif

#if defined(S3G_ENABLE_VSTGUI_CANVAS_GUI)
#include "../common/s3g_stereo_processor_lowform_canvas.inc"
#endif

const void* getExtension(const clap_plugin_t*, const char* id)
{
#if defined(S3G_ENABLE_VSTGUI_CANVAS_GUI)
 if (id && std::strcmp(id,CLAP_EXT_GUI)==0) return &portableGui;
#endif
    if (!id) return nullptr;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &notePorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
    if (std::strcmp(id, CLAP_EXT_TAIL) == 0) return &tailExt;
#if defined(__APPLE__) && !defined(S3G_ENABLE_VSTGUI_CANVAS_GUI)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SYNTHESIZER,
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.processor-lowform",
    "s3g Processor Lowform 2",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.3.0",
    "An eight-voice bass instrument with MIDI transpose, a scale arpeggiator, eight body engines, generated texture, assignable motion, and a low-safe finish chain.",
    features
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    for (const auto& def : kParamDefs) applyParam(*p, def.id, def.defaultValue);
    resetMidiExpressionState(*p);
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
    p->plugin.get_extension = getExtension;
    p->plugin.on_main_thread = onMainThread;
    return &p->plugin;
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

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory
};
