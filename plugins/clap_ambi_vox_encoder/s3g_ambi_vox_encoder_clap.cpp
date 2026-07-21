#include "s3g_ambi_vox_encoder.h"
#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_realtime.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include <Accelerate/Accelerate.h>
#include "../common/s3g_clap_macos.h"
#include "../common/s3g_cocoa_gui.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(S3G_HAS_WORLD)
#include <world/cheaptrick.h>
#include <world/codec.h>
#include <world/d4c.h>
#include <world/dio.h>
#include <world/harvest.h>
#include <world/stonemask.h>
#include <world/synthesis.h>
#endif

namespace {

constexpr uint32_t kOutputChannels = s3g::kAmbiVotMaxChannels;
constexpr uint32_t kStateVersion = 22;
constexpr uint32_t kGuiW = 1160;
constexpr uint32_t kGuiH = 920;
constexpr uint32_t kVoxPhraseMaxChars = 256;
constexpr uint32_t kVoxLyricsMaxChars = 4096;
constexpr uint32_t kVoxLyricsMaxCues = 32;
constexpr uint32_t kVoxLoadedNameMaxChars = 96;
constexpr uint32_t kVoxCompiledMaxFrames = 512;
constexpr float kVoxVoiceTimeStepMaxMs = 1000.0f;
constexpr uint32_t kVoxPvocFftSize = 512u;
constexpr uint32_t kVoxPvocHalfSize = kVoxPvocFftSize / 2u;
constexpr uint32_t kVoxPvocBins = kVoxPvocHalfSize + 1u;
constexpr uint32_t kVoxPvocHopSize = kVoxPvocFftSize / 4u;
constexpr uint32_t kVoxWorldPitchAnchorCount = 5u;
constexpr uint32_t kVoxWorldPitchAnchorCenter = 2u;
constexpr uint32_t kVoxWorldSpectralDimensions = 50u;
constexpr uint32_t kVoxWorldRuntimeEnvelopeBins = kVoxPvocBins;
constexpr uint32_t kVoxFactoryPresetCount = 13u;
constexpr uint32_t kVoxFactoryPresetOrder = 3u;
constexpr std::array<int, kVoxWorldPitchAnchorCount> kVoxWorldPitchAnchorSemitones {
    -24, -12, 0, 12, 24
};
constexpr uint8_t kVoxBankEventRest = 1u << 0u;
constexpr uint8_t kVoxBankEventWordEnd = 1u << 1u;
constexpr uint8_t kVoxBankEventVowel = 1u << 2u;

constexpr clap_id kOrderParamId = 1;
constexpr clap_id kVoicesParamId = 2;
constexpr clap_id kModeParamId = 3;
constexpr clap_id kPresetParamId = 4;
constexpr clap_id kBaseNoteParamId = 5;
constexpr clap_id kTuneParamId = 6;
constexpr clap_id kVectorXParamId = 7;
constexpr clap_id kVectorYParamId = 8;
constexpr clap_id kScanParamId = 9;
constexpr clap_id kScanRateParamId = 10;
constexpr clap_id kMorphParamId = 11;
constexpr clap_id kDetuneParamId = 12;
constexpr clap_id kSpreadParamId = 13;
constexpr clap_id kMotionRateParamId = 14;
constexpr clap_id kAttackParamId = 15;
constexpr clap_id kReleaseParamId = 16;
constexpr clap_id kOutputParamId = 17;
constexpr clap_id kMotionSceneParamId = 18;
constexpr clap_id kMotionClockParamId = 19;
constexpr clap_id kSyncDivisionParamId = 20;
constexpr clap_id kMotionAmountParamId = 21;
constexpr clap_id kCoherenceParamId = 22;
constexpr clap_id kChaosParamId = 23;
constexpr clap_id kLinkParamId = 24;
constexpr clap_id kSmoothParamId = 25;
constexpr clap_id kCenterAzimuthParamId = 26;
constexpr clap_id kCenterElevationParamId = 27;
constexpr clap_id kCenterDistanceParamId = 28;
constexpr clap_id kNeighborRadiusParamId = 29;
constexpr clap_id kRequiredNeighborsParamId = 30;
constexpr clap_id kDecayParamId = 31;
constexpr clap_id kSustainParamId = 32;
constexpr clap_id kScaleParamId = 33;
constexpr clap_id kPitchSpreadParamId = 34;
constexpr clap_id kHarmonicsParamId = 35;
constexpr clap_id kSubharmonicsParamId = 36;
constexpr clap_id kScoreModeParamId = 37;
constexpr clap_id kScoreDurationParamId = 38;
constexpr clap_id kScoreDepthParamId = 39;
constexpr clap_id kRaspParamId = 40;
constexpr clap_id kBreathParamId = 41;
constexpr clap_id kThroatParamId = 42;
constexpr clap_id kDriveParamId = 43;
constexpr clap_id kJitterParamId = 44;
constexpr clap_id kVowelSpreadParamId = 45;
constexpr clap_id kPitchScoopParamId = 46;
constexpr clap_id kAttackShapeParamId = 47;
constexpr clap_id kArticulationParamId = 48;
constexpr clap_id kConsonantParamId = 49;
constexpr clap_id kPlosiveParamId = 50;
constexpr clap_id kSibilanceParamId = 51;
constexpr clap_id kPhraseRateParamId = 52;
constexpr clap_id kSpeechModeParamId = 53;
constexpr clap_id kCircuitModeParamId = 54;
constexpr clap_id kFormantMacroParamId = 55;
constexpr clap_id kBendMacroParamId = 56;
constexpr clap_id kCrushMacroParamId = 57;
constexpr clap_id kWorldRateParamId = 58;
constexpr clap_id kWorldPitchParamId = 59;
constexpr clap_id kWorldLoopStartParamId = 60;
constexpr clap_id kWorldLoopEndParamId = 61;
constexpr clap_id kWorldVoiceSpreadParamId = 62;
constexpr clap_id kWorldFreezeParamId = 63;
constexpr clap_id kWorldScrubParamId = 64;
constexpr clap_id kWorldVoicingParamId = 65;
constexpr clap_id kWorldVoiceDeviationParamId = 66;
constexpr clap_id kPvocStretchParamId = 67;
constexpr clap_id kPvocTransientParamId = 68;
constexpr clap_id kPortamentoParamId = 69;
constexpr clap_id kVibratoDepthParamId = 70;
constexpr clap_id kVibratoRateParamId = 71;
constexpr clap_id kTransitionParamId = 72;
constexpr clap_id kWorldAirColorParamId = 73;
constexpr clap_id kOrchestrationParamId = 74;
constexpr clap_id kContourModeParamId = 75;
constexpr clap_id kPhraseSpreadParamId = 76;
constexpr clap_id kLyricModeParamId = 77;
constexpr clap_id kLyricCueBeatsParamId = 78;
constexpr clap_id kLyricCueNoteParamId = 79;
constexpr clap_id kLyricCueChannelParamId = 80;

struct LegacyParamsV1 {
    uint32_t order = 3;
    uint32_t voices = 8;
    s3g::AmbiVotMode mode = s3g::AmbiVotMode::Free;
    s3g::AmbiVotPreset preset = s3g::AmbiVotPreset::Classic;
    float baseNote = 48.0f;
    float tuneCents = 0.0f;
    float vectorX = 0.20f;
    float vectorY = 0.55f;
    float scan = 0.30f;
    float scanRateHz = 0.045f;
    float morph = 1.0f;
    float detune = 0.10f;
    float spread = 0.65f;
    float driftHz = 0.025f;
    float attackMs = 35.0f;
    float releaseMs = 650.0f;
    float outputGainDb = -18.0f;
};

struct SavedStateV1 {
    uint32_t version = 1;
    LegacyParamsV1 params {};
};

struct AmbiVotParamsV2 {
    uint32_t order = 3;
    uint32_t voices = 8;
    s3g::AmbiVotMode mode = s3g::AmbiVotMode::Free;
    s3g::AmbiVotPreset preset = s3g::AmbiVotPreset::Classic;
    float baseNote = 48.0f;
    float tuneCents = 0.0f;
    float vectorX = 0.20f;
    float vectorY = 0.55f;
    float scan = 0.30f;
    float scanRate = 1.0f;
    float morph = 1.0f;
    float detune = 0.10f;
    float motionSpread = 0.65f;
    float motionRateHz = 0.045f;
    float attackMs = 35.0f;
    float releaseMs = 650.0f;
    float outputGainDb = -18.0f;
    s3g::AmbiVotMotionScene motionScene = s3g::AmbiVotMotionScene::Flow;
    s3g::AmbiVotMotionClock motionClock = s3g::AmbiVotMotionClock::Free;
    float syncDivisionBeats = 8.0f;
    float motionAmount = 0.72f;
    float motionCoherence = 0.62f;
    float motionChaos = 0.18f;
    float motionLink = 0.72f;
    float motionSmooth = 0.72f;
    float centerAzimuthDeg = 0.0f;
    float centerElevationDeg = 0.0f;
    float centerDistance = 1.0f;
};

struct SavedStateV2 {
    uint32_t version = 2;
    AmbiVotParamsV2 params {};
    int32_t guiPage = 0;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
};

struct AmbiVotParamsV3 {
    uint32_t order = 3;
    uint32_t voices = 8;
    s3g::AmbiVotMode mode = s3g::AmbiVotMode::Free;
    s3g::AmbiVotPreset preset = s3g::AmbiVotPreset::Classic;
    float baseNote = 48.0f;
    float tuneCents = 0.0f;
    float vectorX = 0.20f;
    float vectorY = 0.55f;
    float scan = 0.30f;
    float scanRate = 1.0f;
    float morph = 1.0f;
    float detune = 0.10f;
    float motionSpread = 0.65f;
    float motionRateHz = 0.045f;
    float attackMs = 35.0f;
    float releaseMs = 650.0f;
    float outputGainDb = -18.0f;
    s3g::AmbiVotMotionScene motionScene = s3g::AmbiVotMotionScene::Flow;
    s3g::AmbiVotMotionClock motionClock = s3g::AmbiVotMotionClock::Free;
    float syncDivisionBeats = 8.0f;
    float motionAmount = 0.72f;
    float motionCoherence = 0.62f;
    float motionChaos = 0.18f;
    float motionLink = 0.72f;
    float motionSmooth = 0.72f;
    float centerAzimuthDeg = 0.0f;
    float centerElevationDeg = 0.0f;
    float centerDistance = 1.0f;
    float neighborRadius = 0.90f;
    uint32_t requiredNeighbors = 1;
};

struct SavedStateV3 {
    uint32_t version = 3;
    AmbiVotParamsV3 params {};
    int32_t guiPage = 0;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
};

struct AmbiVotParamsV4 {
    uint32_t order = 3;
    uint32_t voices = 8;
    s3g::AmbiVotMode mode = s3g::AmbiVotMode::Free;
    s3g::AmbiVotPreset preset = s3g::AmbiVotPreset::Classic;
    float baseNote = 48.0f;
    float tuneCents = 0.0f;
    float vectorX = 0.20f;
    float vectorY = 0.55f;
    float scan = 0.30f;
    float scanRate = 1.0f;
    float morph = 1.0f;
    float detune = 0.10f;
    float motionSpread = 0.65f;
    float motionRateHz = 0.045f;
    float attackMs = 35.0f;
    float decayMs = 220.0f;
    float sustain = 0.68f;
    float releaseMs = 650.0f;
    float outputGainDb = -18.0f;
    s3g::AmbiVotMotionScene motionScene = s3g::AmbiVotMotionScene::Flow;
    s3g::AmbiVotMotionClock motionClock = s3g::AmbiVotMotionClock::Free;
    float syncDivisionBeats = 8.0f;
    float motionAmount = 0.72f;
    float motionCoherence = 0.62f;
    float motionChaos = 0.18f;
    float motionLink = 0.72f;
    float motionSmooth = 0.72f;
    float centerAzimuthDeg = 0.0f;
    float centerElevationDeg = 0.0f;
    float centerDistance = 1.0f;
    float neighborRadius = 0.90f;
    uint32_t requiredNeighbors = 1;
};

struct SavedStateV4 {
    uint32_t version = 4;
    AmbiVotParamsV4 params {};
    int32_t guiPage = 0;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
};

struct SavedStateV5 {
    uint32_t version = 5;
    s3g::AmbiVotParams params {};
    s3g::AmbiVotVectorScore score = s3g::ambiVotDefaultScore();
    int32_t guiPage = 0;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
};

struct VoxParamsV7 {
    float rasp = 0.42f;
    float breath = 0.18f;
    float throat = 0.56f;
    float drive = 0.48f;
    float jitter = 0.12f;
    float vowelSpread = 0.22f;
    float pitchScoop = 0.18f;
    float attackShape = 0.62f;
};

enum class VoxSpeechMode : uint32_t {
    Texture = 0,
    Speak = 1,
    Sing = 2,
};

const char* voxSpeechModeName(VoxSpeechMode mode)
{
    switch (mode) {
    case VoxSpeechMode::Texture: return "TEXTURE";
    case VoxSpeechMode::Speak: return "SPEAK";
    case VoxSpeechMode::Sing: return "SING";
    }
    return "TEXTURE";
}

enum class VoxCircuitMode : uint32_t {
    Clean = 0,
    Bent = 1,
    Chip = 2,
    Broken = 3,
};

const char* voxCircuitModeName(VoxCircuitMode mode)
{
    switch (mode) {
    case VoxCircuitMode::Clean: return "CLEAN";
    case VoxCircuitMode::Bent: return "BENT";
    case VoxCircuitMode::Chip: return "CHIP";
    case VoxCircuitMode::Broken: return "BROKEN";
    }
    return "CLEAN";
}

enum class VoxLyricMode : uint32_t {
    Loop = 0,
    MidiStep = 1,
    MidiCue = 2,
    Transport = 3,
    Auto = 4,
};

const char* voxLyricModeName(VoxLyricMode mode)
{
    switch (mode) {
    case VoxLyricMode::Loop: return "LOOP";
    case VoxLyricMode::MidiStep: return "MIDI STEP";
    case VoxLyricMode::MidiCue: return "MIDI CUE";
    case VoxLyricMode::Transport: return "TRANSPORT";
    case VoxLyricMode::Auto: return "AUTO";
    }
    return "LOOP";
}

struct VoxLyricParams {
    VoxLyricMode mode = VoxLyricMode::Loop;
    float cueBeats = 16.0f;
    uint32_t cueBaseNote = 24u;
    uint32_t cueChannel = 16u;
};

struct VoxParamsV9 {
    float rasp = 0.34f;
    float breath = 0.14f;
    float throat = 0.52f;
    float drive = 0.30f;
    float jitter = 0.12f;
    float vowelSpread = 0.22f;
    float pitchScoop = 0.12f;
    float attackShape = 0.50f;
    float articulation = 0.36f;
    float consonant = 0.18f;
    float plosive = 0.04f;
    float sibilance = 0.18f;
    float phraseRate = 0.34f;
};

struct VoxParamsV10 {
    float rasp = 0.34f;
    float breath = 0.14f;
    float throat = 0.52f;
    float drive = 0.30f;
    float jitter = 0.12f;
    float vowelSpread = 0.22f;
    float pitchScoop = 0.12f;
    float attackShape = 0.50f;
    float articulation = 0.36f;
    float consonant = 0.18f;
    float plosive = 0.04f;
    float sibilance = 0.18f;
    float phraseRate = 0.34f;
    VoxSpeechMode speechMode = VoxSpeechMode::Speak;
};

struct VoxParamsV11 {
    float rasp = 0.34f;
    float breath = 0.14f;
    float throat = 0.52f;
    float drive = 0.30f;
    float jitter = 0.12f;
    float vowelSpread = 0.22f;
    float pitchScoop = 0.12f;
    float attackShape = 0.50f;
    float articulation = 0.36f;
    float consonant = 0.18f;
    float plosive = 0.04f;
    float sibilance = 0.18f;
    float phraseRate = 0.34f;
    VoxSpeechMode speechMode = VoxSpeechMode::Speak;
    VoxCircuitMode circuitMode = VoxCircuitMode::Clean;
    float formantMacro = 0.0f;
    float bendMacro = 0.0f;
    float crushMacro = 0.0f;
};

struct VoxParamsV12 {
    float rasp = 0.34f;
    float breath = 0.14f;
    float throat = 0.52f;
    float drive = 0.30f;
    float jitter = 0.12f;
    float vowelSpread = 0.22f;
    float pitchScoop = 0.12f;
    float attackShape = 0.50f;
    float articulation = 0.36f;
    float consonant = 0.18f;
    float plosive = 0.04f;
    float sibilance = 0.18f;
    float phraseRate = 0.34f;
    VoxSpeechMode speechMode = VoxSpeechMode::Speak;
    VoxCircuitMode circuitMode = VoxCircuitMode::Clean;
    float formantMacro = 0.0f;
    float bendMacro = 0.0f;
    float crushMacro = 0.0f;
    float worldRate = 0.50f;
    float worldPitchCents = 0.0f;
    float worldLoopStart = 0.0f;
    float worldLoopEnd = 1.0f;
};

struct VoxParamsV13 {
    float rasp = 0.34f;
    float breath = 0.14f;
    float throat = 0.52f;
    float drive = 0.30f;
    float jitter = 0.12f;
    float vowelSpread = 0.22f;
    float pitchScoop = 0.12f;
    float attackShape = 0.50f;
    float articulation = 0.36f;
    float consonant = 0.18f;
    float plosive = 0.04f;
    float sibilance = 0.18f;
    float phraseRate = 0.34f;
    VoxSpeechMode speechMode = VoxSpeechMode::Speak;
    VoxCircuitMode circuitMode = VoxCircuitMode::Clean;
    float formantMacro = 0.0f;
    float bendMacro = 0.0f;
    float crushMacro = 0.0f;
    float worldRate = 0.50f;
    float worldPitchCents = 0.0f;
    float worldLoopStart = 0.0f;
    float worldLoopEnd = 1.0f;
    float worldVoiceSpread = 0.35f;
};

struct VoxParamsV14 {
    float rasp = 0.34f;
    float breath = 0.14f;
    float throat = 0.52f;
    float drive = 0.30f;
    float jitter = 0.12f;
    float vowelSpread = 0.22f;
    float pitchScoop = 0.12f;
    float attackShape = 0.50f;
    float articulation = 0.36f;
    float consonant = 0.18f;
    float plosive = 0.04f;
    float sibilance = 0.18f;
    float phraseRate = 0.34f;
    VoxSpeechMode speechMode = VoxSpeechMode::Speak;
    VoxCircuitMode circuitMode = VoxCircuitMode::Clean;
    float formantMacro = 0.0f;
    float bendMacro = 0.0f;
    float crushMacro = 0.0f;
    float worldRate = 0.50f;
    float worldPitchCents = 0.0f;
    float worldLoopStart = 0.0f;
    float worldLoopEnd = 1.0f;
    float worldVoiceSpread = 0.35f;
    float worldFreeze = 0.0f;
    float worldScrub = 0.0f;
    float worldVoicing = 0.50f;
};

struct VoxParamsV15 {
    float rasp = 0.34f;
    float breath = 0.14f;
    float throat = 0.52f;
    float drive = 0.30f;
    float jitter = 0.12f;
    float vowelSpread = 0.22f;
    float pitchScoop = 0.12f;
    float attackShape = 0.50f;
    float articulation = 0.36f;
    float consonant = 0.18f;
    float plosive = 0.04f;
    float sibilance = 0.18f;
    float phraseRate = 0.34f;
    VoxSpeechMode speechMode = VoxSpeechMode::Speak;
    VoxCircuitMode circuitMode = VoxCircuitMode::Clean;
    float formantMacro = 0.0f;
    float bendMacro = 0.0f;
    float crushMacro = 0.0f;
    float worldRate = 0.50f;
    float worldPitchCents = 0.0f;
    float worldLoopStart = 0.0f;
    float worldLoopEnd = 1.0f;
    float worldVoiceSpread = 0.08f;
    float worldFreeze = 0.0f;
    float worldScrub = 0.0f;
    float worldVoicing = 0.50f;
    float worldVoiceDeviation = 0.0f;
};

struct VoxParamsV16 {
    float rasp = 0.34f;
    float breath = 0.14f;
    float throat = 0.52f;
    float drive = 0.30f;
    float jitter = 0.12f;
    float vowelSpread = 0.22f;
    float pitchScoop = 0.12f;
    float attackShape = 0.50f;
    float articulation = 0.36f;
    float consonant = 0.18f;
    float plosive = 0.04f;
    float sibilance = 0.18f;
    float phraseRate = 0.34f;
    VoxSpeechMode speechMode = VoxSpeechMode::Speak;
    VoxCircuitMode circuitMode = VoxCircuitMode::Clean;
    float formantMacro = 0.0f;
    float bendMacro = 0.0f;
    float crushMacro = 0.0f;
    float worldRate = 0.50f;
    float worldPitchCents = 0.0f;
    float worldLoopStart = 0.0f;
    float worldLoopEnd = 1.0f;
    float worldVoiceSpread = 0.08f;
    float worldFreeze = 0.0f;
    float worldScrub = 0.0f;
    float worldVoicing = 0.50f;
    float worldVoiceDeviation = 0.0f;
    float pvocStretch = 1.0f;
    float pvocTransient = 0.65f;
};

struct VoxParamsV18 {
    float rasp = 0.0f;
    float breath = 0.0f;
    float throat = 0.52f;
    float drive = 0.0f;
    float jitter = 0.12f;
    float vowelSpread = 0.22f;
    float pitchScoop = 0.12f;
    float attackShape = 0.50f;
    float articulation = 0.36f;
    float consonant = 0.18f;
    float plosive = 0.04f;
    float sibilance = 0.18f;
    float phraseRate = 0.34f;
    VoxSpeechMode speechMode = VoxSpeechMode::Speak;
    VoxCircuitMode circuitMode = VoxCircuitMode::Clean;
    float formantMacro = 0.0f;
    float bendMacro = 0.0f;
    float crushMacro = 0.0f;
    float worldRate = 0.50f;
    float worldPitchCents = 0.0f;
    float worldLoopStart = 0.0f;
    float worldLoopEnd = 1.0f;
    float worldVoiceSpread = 0.0f;
    float worldFreeze = 0.0f;
    float worldScrub = 0.0f;
    float worldVoicing = 0.50f;
    float worldVoiceDeviation = 0.0f;
    float pvocStretch = 1.0f;
    float pvocTransient = 0.65f;
    float portamento = 0.18f;
    float vibratoDepth = 0.12f;
    float vibratoRateHz = 5.2f;
    float transition = 0.65f;
};

struct VoxParamsV20 {
    float rasp = 0.0f;
    float breath = 0.0f;
    float throat = 0.52f;
    float drive = 0.0f;
    float jitter = 0.12f;
    float vowelSpread = 0.22f;
    float pitchScoop = 0.12f;
    float attackShape = 0.50f;
    float articulation = 0.36f;
    float consonant = 0.18f;
    float plosive = 0.04f;
    float sibilance = 0.18f;
    float phraseRate = 0.34f;
    VoxSpeechMode speechMode = VoxSpeechMode::Speak;
    VoxCircuitMode circuitMode = VoxCircuitMode::Clean;
    float formantMacro = 0.0f;
    float bendMacro = 0.0f;
    float crushMacro = 0.0f;
    float worldRate = 0.50f;
    float worldPitchCents = 0.0f;
    float worldLoopStart = 0.0f;
    float worldLoopEnd = 1.0f;
    float worldVoiceSpread = 0.0f;
    float worldFreeze = 0.0f;
    float worldScrub = 0.0f;
    float worldVoicing = 0.50f;
    float worldVoiceDeviation = 0.0f;
    float pvocStretch = 1.0f;
    float pvocTransient = 0.65f;
    float portamento = 0.18f;
    float vibratoDepth = 0.12f;
    float vibratoRateHz = 5.2f;
    float transition = 0.65f;
    float worldAirColor = 0.0f;
    s3g::AmbiVoxOrchestration orchestration = s3g::AmbiVoxOrchestration::Individual;
    s3g::AmbiVoxContourMode contourMode = s3g::AmbiVoxContourMode::Original;
};

struct VoxParams {
    float rasp = 0.0f;
    float breath = 0.0f;
    float throat = 0.52f;
    float drive = 0.0f;
    float jitter = 0.12f;
    float vowelSpread = 0.22f;
    float pitchScoop = 0.12f;
    float attackShape = 0.50f;
    float articulation = 0.36f;
    float consonant = 0.18f;
    float plosive = 0.04f;
    float sibilance = 0.18f;
    float phraseRate = 0.34f;
    VoxSpeechMode speechMode = VoxSpeechMode::Speak;
    VoxCircuitMode circuitMode = VoxCircuitMode::Clean;
    float formantMacro = 0.0f;
    float bendMacro = 0.0f;
    float crushMacro = 0.0f;
    float worldRate = 0.50f;
    float worldPitchCents = 0.0f;
    float worldLoopStart = 0.0f;
    float worldLoopEnd = 1.0f;
    float worldVoiceSpread = 0.0f;
    float worldFreeze = 0.0f;
    float worldScrub = 0.0f;
    float worldVoicing = 0.50f;
    float worldVoiceDeviation = 0.0f;
    float pvocStretch = 1.0f;
    float pvocTransient = 0.65f;
    float portamento = 0.18f;
    float vibratoDepth = 0.12f;
    float vibratoRateHz = 5.2f;
    float transition = 0.65f;
    float worldAirColor = 0.0f;
    s3g::AmbiVoxOrchestration orchestration = s3g::AmbiVoxOrchestration::Individual;
    s3g::AmbiVoxContourMode contourMode = s3g::AmbiVoxContourMode::Original;
    float phraseSpread = 0.0f;
};

struct SavedStateV6 {
    uint32_t version = 6;
    s3g::AmbiVotParams params {};
    s3g::AmbiVotVectorScore score = s3g::ambiVotDefaultScore();
    int32_t guiPage = 0;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
    uint32_t hasUserAtlas = 0u;
    std::array<float, s3g::kAmbiVotAtlasSampleCount> userAtlas {};
};

struct SavedStateV7 {
    uint32_t version = 7;
    s3g::AmbiVotParams params {};
    s3g::AmbiVotVectorScore score = s3g::ambiVotDefaultScore();
    VoxParamsV7 vox {};
    int32_t guiPage = 0;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
    uint32_t hasUserAtlas = 0u;
    std::array<float, s3g::kAmbiVotAtlasSampleCount> userAtlas {};
};

struct SavedStateV8 {
    uint32_t version = 8;
    s3g::AmbiVotParams params {};
    s3g::AmbiVotVectorScore score = s3g::ambiVotDefaultScore();
    VoxParamsV9 vox {};
    int32_t guiPage = 0;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
    uint32_t hasUserAtlas = 0u;
    std::array<float, s3g::kAmbiVotAtlasSampleCount> userAtlas {};
};

struct SavedStateV9 {
    uint32_t version = 9;
    s3g::AmbiVotParams params {};
    s3g::AmbiVotVectorScore score = s3g::ambiVotDefaultScore();
    VoxParamsV9 vox {};
    std::array<uint8_t, kVoxPhraseMaxChars> phrase {};
    uint32_t phraseLength = 0u;
    int32_t guiPage = 0;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
    uint32_t hasUserAtlas = 0u;
    std::array<float, s3g::kAmbiVotAtlasSampleCount> userAtlas {};
};

struct SavedStateV10 {
    uint32_t version = 10;
    s3g::AmbiVotParams params {};
    s3g::AmbiVotVectorScore score = s3g::ambiVotDefaultScore();
    VoxParamsV10 vox {};
    std::array<uint8_t, kVoxPhraseMaxChars> phrase {};
    uint32_t phraseLength = 0u;
    int32_t guiPage = 0;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
    uint32_t hasUserAtlas = 0u;
    std::array<float, s3g::kAmbiVotAtlasSampleCount> userAtlas {};
};

struct SavedStateV11 {
    uint32_t version = 11;
    s3g::AmbiVotParams params {};
    s3g::AmbiVotVectorScore score = s3g::ambiVotDefaultScore();
    VoxParamsV11 vox {};
    std::array<uint8_t, kVoxPhraseMaxChars> phrase {};
    uint32_t phraseLength = 0u;
    int32_t guiPage = 0;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
    uint32_t hasUserAtlas = 0u;
    std::array<float, s3g::kAmbiVotAtlasSampleCount> userAtlas {};
};

struct SavedStateV12 {
    uint32_t version = 12;
    s3g::AmbiVotParams params {};
    s3g::AmbiVotVectorScore score = s3g::ambiVotDefaultScore();
    VoxParamsV12 vox {};
    std::array<uint8_t, kVoxPhraseMaxChars> phrase {};
    uint32_t phraseLength = 0u;
    int32_t guiPage = 0;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
    uint32_t hasUserAtlas = 0u;
    std::array<float, s3g::kAmbiVotAtlasSampleCount> userAtlas {};
};

struct SavedStateV13 {
    uint32_t version = 13;
    s3g::AmbiVotParams params {};
    s3g::AmbiVotVectorScore score = s3g::ambiVotDefaultScore();
    VoxParamsV13 vox {};
    std::array<uint8_t, kVoxPhraseMaxChars> phrase {};
    uint32_t phraseLength = 0u;
    int32_t guiPage = 0;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
    uint32_t hasUserAtlas = 0u;
    std::array<float, s3g::kAmbiVotAtlasSampleCount> userAtlas {};
};

struct SavedStateV14 {
    uint32_t version = 14;
    s3g::AmbiVotParams params {};
    s3g::AmbiVotVectorScore score = s3g::ambiVotDefaultScore();
    VoxParamsV14 vox {};
    std::array<uint8_t, kVoxPhraseMaxChars> phrase {};
    uint32_t phraseLength = 0u;
    int32_t guiPage = 0;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
    uint32_t hasUserAtlas = 0u;
    std::array<float, s3g::kAmbiVotAtlasSampleCount> userAtlas {};
};

struct SavedStateV15 {
    uint32_t version = 15;
    s3g::AmbiVotParams params {};
    s3g::AmbiVotVectorScore score = s3g::ambiVotDefaultScore();
    VoxParamsV15 vox {};
    std::array<uint8_t, kVoxPhraseMaxChars> phrase {};
    uint32_t phraseLength = 0u;
    int32_t guiPage = 0;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
    uint32_t hasUserAtlas = 0u;
    std::array<float, s3g::kAmbiVotAtlasSampleCount> userAtlas {};
};

struct SavedStateV16 {
    uint32_t version = 16;
    s3g::AmbiVotParams params {};
    s3g::AmbiVotVectorScore score = s3g::ambiVotDefaultScore();
    VoxParamsV16 vox {};
    std::array<uint8_t, kVoxPhraseMaxChars> phrase {};
    uint32_t phraseLength = 0u;
    int32_t guiPage = 0;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
    uint32_t hasUserAtlas = 0u;
    std::array<float, s3g::kAmbiVotAtlasSampleCount> userAtlas {};
};

struct SavedStateV18 {
    uint32_t version = 18;
    s3g::AmbiVotParams params {};
    s3g::AmbiVotVectorScore score = s3g::ambiVotDefaultScore();
    VoxParamsV18 vox {};
    std::array<uint8_t, kVoxPhraseMaxChars> phrase {};
    uint32_t phraseLength = 0u;
    int32_t guiPage = 0;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
    uint32_t hasUserAtlas = 0u;
    std::array<float, s3g::kAmbiVotAtlasSampleCount> userAtlas {};
};

struct SavedStateV19 {
    uint32_t version = 19u;
    s3g::AmbiVotParams params {};
    s3g::AmbiVotVectorScore score = s3g::ambiVotDefaultScore();
    VoxParamsV20 vox {};
    std::array<uint8_t, kVoxPhraseMaxChars> phrase {};
    uint32_t phraseLength = 0u;
    int32_t guiPage = 0;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
    uint32_t hasUserAtlas = 0u;
    std::array<float, s3g::kAmbiVotAtlasSampleCount> userAtlas {};
};

struct SavedStateV20 {
    uint32_t version = 20u;
    s3g::AmbiVotParams params {};
    s3g::AmbiVotVectorScore score = s3g::ambiVotDefaultScore();
    VoxParamsV20 vox {};
    std::array<uint8_t, kVoxPhraseMaxChars> phrase {};
    uint32_t phraseLength = 0u;
    int32_t guiPage = 0;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
    uint32_t hasUserAtlas = 0u;
    std::array<float, s3g::kAmbiVotAtlasSampleCount> userAtlas {};
    int32_t factoryPresetIndex = 0;
    std::array<char, 64> presetName {};
};

struct SavedStateV21 {
    uint32_t version = 21u;
    s3g::AmbiVotParams params {};
    s3g::AmbiVotVectorScore score = s3g::ambiVotDefaultScore();
    VoxParams vox {};
    std::array<uint8_t, kVoxPhraseMaxChars> phrase {};
    uint32_t phraseLength = 0u;
    int32_t guiPage = 0;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
    uint32_t hasUserAtlas = 0u;
    std::array<float, s3g::kAmbiVotAtlasSampleCount> userAtlas {};
    int32_t factoryPresetIndex = 0;
    std::array<char, 64> presetName {};
};

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::AmbiVotParams params {};
    s3g::AmbiVotVectorScore score = s3g::ambiVotDefaultScore();
    VoxParams vox {};
    VoxLyricParams lyric {};
    std::array<uint8_t, kVoxPhraseMaxChars> phrase {};
    uint32_t phraseLength = 0u;
    std::array<uint8_t, kVoxLyricsMaxChars> lyrics {};
    uint32_t lyricsLength = 0u;
    int32_t guiPage = 0;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
    uint32_t hasUserAtlas = 0u;
    std::array<float, s3g::kAmbiVotAtlasSampleCount> userAtlas {};
    int32_t factoryPresetIndex = 0;
    std::array<char, 64> presetName {};
};

struct VoxPresetSnapshot {
    s3g::AmbiVotParams params {};
    VoxParams vox {};
    VoxLyricParams lyric {};
    s3g::AmbiVotVectorScore score = s3g::ambiVotDefaultScore();
};

struct VoxPresetMailbox {
    static constexpr uint32_t kCapacity = 4u;
    std::array<VoxPresetSnapshot, kCapacity> values {};
    std::atomic<uint32_t> readIndex { 0u };
    std::atomic<uint32_t> writeIndex { 0u };

    bool push(const VoxPresetSnapshot& value)
    {
        const uint32_t write = writeIndex.load(std::memory_order_relaxed);
        const uint32_t next = (write + 1u) % kCapacity;
        if (next == readIndex.load(std::memory_order_acquire)) return false;
        values[write] = value;
        writeIndex.store(next, std::memory_order_release);
        return true;
    }

    bool pop(VoxPresetSnapshot& value)
    {
        const uint32_t read = readIndex.load(std::memory_order_relaxed);
        if (read == writeIndex.load(std::memory_order_acquire)) return false;
        value = values[read];
        readIndex.store((read + 1u) % kCapacity, std::memory_order_release);
        return true;
    }
};

struct VoxWorldParameterData {
    int sampleRate = 0;
    int fftSize = 0;
    double framePeriodMs = 5.0;
    uint32_t frameCount = 0u;
    uint32_t spectralDimensions = 0u;
    uint32_t aperiodicityDimensions = 0u;
    std::vector<float> codedSpectralEnvelope;
    std::vector<float> codedAperiodicity;
    std::vector<float> runtimeLogEnvelope;

    bool ready() const
    {
        return sampleRate > 0 && frameCount > 0u
            && runtimeLogEnvelope.size()
                == static_cast<size_t>(frameCount) * kVoxWorldRuntimeEnvelopeBins;
    }
};

struct VoxWorldSample {
    int sampleRate = 0;
    int baseMidi = 60;
    double framePeriodMs = 5.0;
    std::vector<float> samples;
    std::vector<float> f0;
    std::vector<float> frameEnergy;
    VoxWorldParameterData parameters;
    std::string name;
};

struct VoxVoicebankAudio {
    int sampleRate = 0;
    int baseMidi = 60;
    std::vector<float> samples;
    std::array<std::vector<float>, kVoxWorldPitchAnchorCount - 1u> worldPitchVariants;
    std::vector<float> worldF0;
    std::vector<float> worldFrameEnergy;
    VoxWorldParameterData worldParameters;
    double worldFramePeriodMs = 5.0;
    bool worldResynthesized = false;
};

struct VoxPvocSpectralControl {
    const VoxWorldParameterData* parameters = nullptr;
    float formant = 0.0f;
    float periodicity = 0.5f;
    float airColor = 0.0f;
};

struct VoxVoicebankEntry {
    std::string alias;
    std::string fileKey;
    std::string searchKey;
    std::shared_ptr<const VoxVoicebankAudio> audio;
    double startSample = 0.0;
    double endSample = 0.0;
    double fixedSample = 0.0;
    double preutterSample = 0.0;
    double overlapSample = 0.0;
    double loopStart = 0.0;
    double loopEnd = 0.0;
};

struct VoxVoicebank {
    std::string name;
    std::vector<VoxVoicebankEntry> entries;
    std::unordered_map<std::string, std::vector<std::string>> pronunciations;
};

struct VoxPvocVoiceState {
    std::array<float, kVoxPvocBins> previousPhase {};
    std::array<float, kVoxPvocBins> synthesisPhase {};
    std::array<float, kVoxPvocBins> previousMagnitude {};
    std::array<float, kVoxPvocFftSize> outputRing {};
    const float* sourceIdentity = nullptr;
    double analysisPosition = 0.0;
    uint32_t outputReadPosition = 0u;
    uint32_t samplesUntilHop = 0u;
    uint64_t spectralFrameCounter = 0u;
    bool primed = false;
};

class VoxPvocProcessor {
public:
    ~VoxPvocProcessor()
    {
#if defined(__APPLE__)
        if (fftSetup_) vDSP_destroy_fftsetup(fftSetup_);
#endif
    }

    VoxPvocProcessor() = default;
    VoxPvocProcessor(const VoxPvocProcessor&) = delete;
    VoxPvocProcessor& operator=(const VoxPvocProcessor&) = delete;

    bool prepare()
    {
        if (ready_) return true;
        for (uint32_t i = 0u; i < kVoxPvocFftSize; ++i) {
            window_[i] = 0.5f - 0.5f * std::cos(
                2.0f * s3g::kPi * static_cast<float>(i) / static_cast<float>(kVoxPvocFftSize));
        }
#if defined(__APPLE__)
        fftSetup_ = vDSP_create_fftsetup(9u, kFFTRadix2);
        ready_ = fftSetup_ != nullptr;
#else
        ready_ = false;
#endif
        return ready_;
    }

    void reset(VoxPvocVoiceState& state, const float* sourceIdentity, double position) const
    {
        state.previousPhase.fill(0.0f);
        state.synthesisPhase.fill(0.0f);
        state.previousMagnitude.fill(0.0f);
        state.outputRing.fill(0.0f);
        state.sourceIdentity = sourceIdentity;
        state.analysisPosition = position;
        state.outputReadPosition = 0u;
        state.samplesUntilHop = 0u;
        state.spectralFrameCounter = 0u;
        state.primed = false;
    }

    float process(VoxPvocVoiceState& state,
                  const std::vector<float>& samples,
                  double rangeStart,
                  double rangeEnd,
                  float sourceRateRatio,
                  float timelineRate,
                  float stretch,
                  float pitchRatio,
                  float transientPreserve,
                  float freeze,
                  float scrub,
                  bool loop = true,
                  const VoxPvocSpectralControl& spectral = {})
    {
        if (samples.empty()) return 0.0f;
        rangeStart = std::clamp(rangeStart, 0.0, static_cast<double>(samples.size() - 1u));
        rangeEnd = std::clamp(rangeEnd, rangeStart + 1.0, static_cast<double>(samples.size()));
        if (state.sourceIdentity != samples.data()) reset(state, samples.data(), rangeStart);

        if (!ready_) {
            const double rangeLength = std::max(1.0, rangeEnd - rangeStart);
            const double scrubPosition = rangeStart + rangeLength * static_cast<double>(std::clamp(scrub, 0.0f, 1.0f));
            const double freezeMix = static_cast<double>(std::clamp(freeze, 0.0f, 1.0f));
            const double position = state.analysisPosition + (scrubPosition - state.analysisPosition) * freezeMix;
            const float value = loop
                ? sampleAt(samples, position, rangeStart, rangeEnd)
                : sampleAtOneShot(samples, position, rangeStart, rangeEnd);
            const double nextPosition = state.analysisPosition
                + static_cast<double>(sourceRateRatio * timelineRate / std::max(0.25f, stretch))
                    * (1.0 - static_cast<double>(std::clamp(freeze, 0.0f, 1.0f)));
            state.analysisPosition = loop
                ? wrapPosition(nextPosition, rangeStart, rangeEnd)
                : nextPosition;
            return value;
        }

        if (state.samplesUntilHop == 0u) {
            synthesize(state, samples, rangeStart, rangeEnd, sourceRateRatio, timelineRate,
                       stretch, pitchRatio, transientPreserve, freeze, scrub, loop, spectral);
            state.samplesUntilHop = kVoxPvocHopSize;
        }
        const uint32_t readPosition = state.outputReadPosition;
        const float value = state.outputRing[readPosition];
        state.outputRing[readPosition] = 0.0f;
        state.outputReadPosition = (readPosition + 1u) % kVoxPvocFftSize;
        --state.samplesUntilHop;
        return s3g::flushDenormal(value);
    }

private:
    static double wrapPosition(double position, double start, double end)
    {
        const double length = std::max(1.0, end - start);
        position = start + std::fmod(position - start, length);
        if (position < start) position += length;
        return position;
    }

    static float sampleAt(const std::vector<float>& samples, double position, double start, double end)
    {
        position = wrapPosition(position, start, end);
        const size_t i0 = std::min(samples.size() - 1u, static_cast<size_t>(position));
        const double next = wrapPosition(static_cast<double>(i0) + 1.0, start, end);
        const size_t i1 = std::min(samples.size() - 1u, static_cast<size_t>(next));
        return s3g::lerp(samples[i0], samples[i1], static_cast<float>(position - std::floor(position)));
    }

    static float sampleAtOneShot(const std::vector<float>& samples, double position, double start, double end)
    {
        if (position < start || position >= end || position < 0.0
            || position >= static_cast<double>(samples.size())) return 0.0f;
        const size_t i0 = std::min(samples.size() - 1u, static_cast<size_t>(position));
        const size_t i1 = std::min(samples.size() - 1u, i0 + 1u);
        const float next = static_cast<double>(i1) < end ? samples[i1] : 0.0f;
        return s3g::lerp(samples[i0], next, static_cast<float>(position - std::floor(position)));
    }

    static float wrapPhase(float phase)
    {
        return std::remainder(phase, 2.0f * s3g::kPi);
    }

    static float worldFrameCoordinate(const VoxWorldParameterData& parameters,
                                      double samplePosition)
    {
        if (parameters.frameCount == 0u || parameters.sampleRate <= 0) return 0.0f;
        const double frame = (samplePosition / static_cast<double>(parameters.sampleRate))
            * (1000.0 / std::max(0.001, parameters.framePeriodMs));
        double wrapped = std::fmod(std::max(0.0, frame), static_cast<double>(parameters.frameCount));
        if (wrapped < 0.0) wrapped += static_cast<double>(parameters.frameCount);
        return static_cast<float>(wrapped);
    }

    static float worldFrameValue(const std::vector<float>& values,
                                 uint32_t dimensions,
                                 uint32_t frameCount,
                                 float frame,
                                 float dimension)
    {
        if (values.empty() || dimensions == 0u || frameCount == 0u) return 0.0f;
        frame = std::clamp(frame, 0.0f, static_cast<float>(frameCount - 1u));
        dimension = std::clamp(dimension, 0.0f, static_cast<float>(dimensions - 1u));
        const uint32_t frame0 = static_cast<uint32_t>(frame);
        const uint32_t frame1 = std::min<uint32_t>(frame0 + 1u, frameCount - 1u);
        const uint32_t dim0 = static_cast<uint32_t>(dimension);
        const uint32_t dim1 = std::min<uint32_t>(dim0 + 1u, dimensions - 1u);
        const float frameMix = frame - static_cast<float>(frame0);
        const float dimMix = dimension - static_cast<float>(dim0);
        const auto at = [&](uint32_t f, uint32_t d) {
            const size_t index = static_cast<size_t>(f) * dimensions + d;
            return index < values.size() ? values[index] : 0.0f;
        };
        return s3g::lerp(
            s3g::lerp(at(frame0, dim0), at(frame0, dim1), dimMix),
            s3g::lerp(at(frame1, dim0), at(frame1, dim1), dimMix),
            frameMix);
    }

    static float worldAperiodicity(const VoxWorldParameterData& parameters,
                                   float frame,
                                   float frequencyHz,
                                   float airColor)
    {
        if (parameters.aperiodicityDimensions == 0u
            || parameters.codedAperiodicity.empty()) return 0.5f;
        float averageDb = 0.0f;
        for (uint32_t dimension = 0u;
             dimension < parameters.aperiodicityDimensions; ++dimension) {
            averageDb += worldFrameValue(parameters.codedAperiodicity,
                parameters.aperiodicityDimensions, parameters.frameCount,
                frame, static_cast<float>(dimension));
        }
        averageDb /= static_cast<float>(parameters.aperiodicityDimensions);
        // WORLD marks unvoiced frames by coding a coarse aperiodicity near
        // 0 dB across every band.
        if (averageDb > -0.5f) return 1.0f;
        constexpr float intervalHz = 3000.0f;
        const float nyquist = std::max(1.0f, static_cast<float>(parameters.sampleRate) * 0.5f);
        float db = -60.0f;
        if (frequencyHz <= intervalHz) {
            const float first = worldFrameValue(parameters.codedAperiodicity,
                parameters.aperiodicityDimensions, parameters.frameCount, frame, 0.0f);
            db = s3g::lerp(-60.0f, first, std::clamp(frequencyHz / intervalHz, 0.0f, 1.0f));
        } else {
            const float position = frequencyHz / intervalHz - 1.0f;
            const float last = static_cast<float>(parameters.aperiodicityDimensions - 1u);
            if (position <= last) {
                db = worldFrameValue(parameters.codedAperiodicity,
                    parameters.aperiodicityDimensions, parameters.frameCount, frame, position);
            } else {
                const float tailStartHz = intervalHz * static_cast<float>(parameters.aperiodicityDimensions);
                const float tail = std::clamp((frequencyHz - tailStartHz)
                    / std::max(1.0f, nyquist - tailStartHz), 0.0f, 1.0f);
                const float lastDb = worldFrameValue(parameters.codedAperiodicity,
                    parameters.aperiodicityDimensions, parameters.frameCount, frame, last);
                db = s3g::lerp(lastDb, 0.0f, tail);
            }
        }
        const float brightness = frequencyHz / nyquist - 0.35f;
        db += std::clamp(airColor, -1.0f, 1.0f) * brightness * 18.0f;
        return std::clamp(std::pow(10.0f, std::clamp(db, -60.0f, 0.0f) / 20.0f), 0.001f, 1.0f);
    }

    static float randomPhase(uint64_t frame, uint32_t bin)
    {
        uint64_t hash = frame * 0x9e3779b97f4a7c15ull
            ^ (static_cast<uint64_t>(bin) + 1ull) * 0xbf58476d1ce4e5b9ull;
        hash ^= hash >> 30u;
        hash *= 0xbf58476d1ce4e5b9ull;
        hash ^= hash >> 27u;
        hash *= 0x94d049bb133111ebull;
        hash ^= hash >> 31u;
        const float unit = static_cast<float>(hash & 0x00ffffffu)
            / static_cast<float>(0x01000000u);
        return unit * 2.0f * s3g::kPi - s3g::kPi;
    }

    void synthesize(VoxPvocVoiceState& state,
                    const std::vector<float>& samples,
                    double rangeStart,
                    double rangeEnd,
                    float sourceRateRatio,
                    float timelineRate,
                    float stretch,
                    float pitchRatio,
                    float transientPreserve,
                    float freeze,
                    float scrub,
                    bool loop,
                    const VoxPvocSpectralControl& spectral)
    {
#if defined(__APPLE__)
        sourceRateRatio = std::clamp(sourceRateRatio, 0.125f, 8.0f);
        timelineRate = std::clamp(timelineRate, 0.125f, 4.0f);
        stretch = std::clamp(stretch, 0.25f, 4.0f);
        pitchRatio = std::clamp(pitchRatio, 0.125f, 8.0f);
        freeze = std::clamp(freeze, 0.0f, 1.0f);
        transientPreserve = std::clamp(transientPreserve, 0.0f, 1.0f);
        const double rangeLength = std::max(1.0, rangeEnd - rangeStart);
        const double scrubPosition = rangeStart + rangeLength * static_cast<double>(std::clamp(scrub, 0.0f, 1.0f));
        const double framePosition = state.analysisPosition
            + (scrubPosition - state.analysisPosition) * static_cast<double>(freeze);
        const float analysisHop = std::max(0.125f,
            static_cast<float>(kVoxPvocHopSize) * sourceRateRatio * timelineRate / stretch);

        for (uint32_t i = 0u; i < kVoxPvocFftSize; ++i) {
            const double position = framePosition + static_cast<double>(i);
            frame_[i] = (loop
                ? sampleAt(samples, position, rangeStart, rangeEnd)
                : sampleAtOneShot(samples, position, rangeStart, rangeEnd)) * window_[i];
        }

        DSPSplitComplex split { splitReal_.data(), splitImag_.data() };
        vDSP_ctoz(reinterpret_cast<const DSPComplex*>(frame_.data()), 2, &split, 1, kVoxPvocHalfSize);
        vDSP_fft_zrip(fftSetup_, &split, 1, 9u, FFT_FORWARD);
        inputReal_[0] = splitReal_[0];
        inputImag_[0] = 0.0f;
        inputReal_[kVoxPvocHalfSize] = splitImag_[0];
        inputImag_[kVoxPvocHalfSize] = 0.0f;
        for (uint32_t bin = 1u; bin < kVoxPvocHalfSize; ++bin) {
            inputReal_[bin] = splitReal_[bin];
            inputImag_[bin] = splitImag_[bin];
        }

        float positiveFlux = 0.0f;
        float magnitudeSum = 0.0f;
        for (uint32_t bin = 0u; bin < kVoxPvocBins; ++bin) {
            const float magnitude = std::hypot(inputReal_[bin], inputImag_[bin]);
            inputMagnitude_[bin] = magnitude;
            inputPhase_[bin] = std::atan2(inputImag_[bin], inputReal_[bin]);
            positiveFlux += std::max(0.0f, magnitude - state.previousMagnitude[bin]);
            magnitudeSum += magnitude;
        }
        const float flux = positiveFlux / std::max(0.000001f, magnitudeSum);
        const float transientThreshold = s3g::lerp(0.72f, 0.08f, transientPreserve);
        const bool resetPhases = !state.primed || flux > transientThreshold;

        outputReal_.fill(0.0f);
        outputImag_.fill(0.0f);
        const float frequencyScale = pitchRatio * sourceRateRatio;
        const float magnitudeScale = 1.0f / std::sqrt(std::max(0.25f, frequencyScale));
        const bool hasWorldParameters = spectral.parameters && spectral.parameters->ready();
        const float worldFrame = hasWorldParameters
            ? worldFrameCoordinate(*spectral.parameters, framePosition)
            : 0.0f;
        const float formantScale = std::pow(2.0f, std::clamp(spectral.formant, -1.0f, 1.0f) * 0.5f);
        const float periodicityShift = (std::clamp(spectral.periodicity, 0.0f, 1.0f) - 0.5f) * 2.0f;
        for (uint32_t bin = 0u; bin < kVoxPvocBins; ++bin) {
            const float omega = 2.0f * s3g::kPi * static_cast<float>(bin) / static_cast<float>(kVoxPvocFftSize);
            const float delta = wrapPhase(inputPhase_[bin] - state.previousPhase[bin] - omega * analysisHop);
            const float trueOmega = omega + delta / analysisHop;
            if (resetPhases) state.synthesisPhase[bin] = inputPhase_[bin];
            else state.synthesisPhase[bin] += trueOmega * frequencyScale * static_cast<float>(kVoxPvocHopSize);
            state.synthesisPhase[bin] = wrapPhase(state.synthesisPhase[bin]);
            state.previousPhase[bin] = inputPhase_[bin];
            state.previousMagnitude[bin] = inputMagnitude_[bin];

            const float target = static_cast<float>(bin) * frequencyScale;
            const uint32_t lower = static_cast<uint32_t>(target);
            if (lower >= kVoxPvocBins) continue;
            const uint32_t upper = std::min<uint32_t>(lower + 1u, kVoxPvocBins - 1u);
            const float fraction = target - static_cast<float>(lower);
            float magnitude = inputMagnitude_[bin] * magnitudeScale;
            float outputPhase = state.synthesisPhase[bin];
            if (hasWorldParameters) {
                const float envelopeBin = static_cast<float>(bin);
                const float warpedBin = envelopeBin / formantScale;
                const float originalEnvelope = worldFrameValue(
                    spectral.parameters->runtimeLogEnvelope,
                    kVoxWorldRuntimeEnvelopeBins, spectral.parameters->frameCount,
                    worldFrame, envelopeBin);
                const float warpedEnvelope = worldFrameValue(
                    spectral.parameters->runtimeLogEnvelope,
                    kVoxWorldRuntimeEnvelopeBins, spectral.parameters->frameCount,
                    worldFrame, warpedBin);
                const float formantGain = std::clamp(
                    std::exp(0.5f * (warpedEnvelope - originalEnvelope)), 0.125f, 8.0f);
                magnitude *= formantGain;

                const float frequencyHz = static_cast<float>(bin)
                    * static_cast<float>(spectral.parameters->sampleRate)
                    / static_cast<float>(kVoxPvocFftSize);
                const float aperiodicity = worldAperiodicity(
                    *spectral.parameters, worldFrame, frequencyHz, spectral.airColor);
                const float nyquist = std::max(1.0f,
                    static_cast<float>(spectral.parameters->sampleRate) * 0.5f);
                const float airTiltDb = std::clamp(spectral.airColor, -1.0f, 1.0f)
                    * (frequencyHz / nyquist - 0.35f) * aperiodicity * 12.0f;
                magnitude *= std::pow(10.0f, airTiltDb / 20.0f);
                if (periodicityShift > 0.0f) {
                    const float periodicGain = std::sqrt(std::max(0.001f,
                        1.0f - aperiodicity * aperiodicity));
                    magnitude *= s3g::lerp(1.0f, periodicGain,
                        std::min(1.0f, periodicityShift * 0.82f));
                } else if (periodicityShift < 0.0f) {
                    const float randomAmount = std::min(0.96f,
                        -periodicityShift * (1.0f - aperiodicity * aperiodicity));
                    const float targetPhase = randomPhase(state.spectralFrameCounter, bin);
                    outputPhase += wrapPhase(targetPhase - outputPhase) * randomAmount;
                }
            }
            const float real = magnitude * std::cos(outputPhase);
            const float imag = magnitude * std::sin(outputPhase);
            outputReal_[lower] += real * (1.0f - fraction);
            outputImag_[lower] += imag * (1.0f - fraction);
            if (upper != lower) {
                outputReal_[upper] += real * fraction;
                outputImag_[upper] += imag * fraction;
            }
        }
        outputImag_[0] = 0.0f;
        outputImag_[kVoxPvocHalfSize] = 0.0f;
        splitReal_[0] = outputReal_[0];
        splitImag_[0] = outputReal_[kVoxPvocHalfSize];
        for (uint32_t bin = 1u; bin < kVoxPvocHalfSize; ++bin) {
            splitReal_[bin] = outputReal_[bin];
            splitImag_[bin] = outputImag_[bin];
        }
        vDSP_fft_zrip(fftSetup_, &split, 1, 9u, FFT_INVERSE);
        vDSP_ztoc(&split, 1, reinterpret_cast<DSPComplex*>(frame_.data()), 2, kVoxPvocHalfSize);
        constexpr float synthesisScale = (4.0f / 12.0f) / static_cast<float>(kVoxPvocFftSize);
        for (uint32_t i = 0u; i < kVoxPvocFftSize; ++i) {
            const uint32_t outputPosition = (state.outputReadPosition + i) % kVoxPvocFftSize;
            state.outputRing[outputPosition] += frame_[i] * window_[i] * synthesisScale;
        }
        const double nextPosition = state.analysisPosition
            + static_cast<double>(analysisHop) * (1.0 - static_cast<double>(freeze) * 0.999);
        state.analysisPosition = loop
            ? wrapPosition(nextPosition, rangeStart, rangeEnd)
            : nextPosition;
        ++state.spectralFrameCounter;
        state.primed = true;
#else
        (void)state;
        (void)samples;
        (void)rangeStart;
        (void)rangeEnd;
        (void)sourceRateRatio;
        (void)timelineRate;
        (void)stretch;
        (void)pitchRatio;
        (void)transientPreserve;
        (void)freeze;
        (void)scrub;
        (void)loop;
        (void)spectral;
#endif
    }

#if defined(__APPLE__)
    FFTSetup fftSetup_ = nullptr;
#endif
    bool ready_ = false;
    std::array<float, kVoxPvocFftSize> window_ {};
    std::array<float, kVoxPvocFftSize> frame_ {};
    std::array<float, kVoxPvocHalfSize> splitReal_ {};
    std::array<float, kVoxPvocHalfSize> splitImag_ {};
    std::array<float, kVoxPvocBins> inputReal_ {};
    std::array<float, kVoxPvocBins> inputImag_ {};
    std::array<float, kVoxPvocBins> inputMagnitude_ {};
    std::array<float, kVoxPvocBins> inputPhase_ {};
    std::array<float, kVoxPvocBins> outputReal_ {};
    std::array<float, kVoxPvocBins> outputImag_ {};
};

struct VoxVoiceDelayLine {
    std::vector<float> samples;
    size_t writePosition = 0u;
    double delayA = 0.0;
    double delayB = 0.0;
    double pendingDelay = 0.0;
    float crossfade = 1.0f;
    float crossfadeStep = 1.0f;
    bool hasPendingDelay = false;

    void prepare(size_t capacity, double sampleRate)
    {
        samples.assign(std::max<size_t>(4u, capacity), 0.0f);
        writePosition = 0u;
        delayA = 0.0;
        delayB = 0.0;
        pendingDelay = 0.0;
        crossfade = 1.0f;
        crossfadeStep = 1.0f / std::max(1.0f, static_cast<float>(sampleRate) * 0.120f);
        hasPendingDelay = false;
    }

    double maximumDelay() const
    {
        return samples.size() > 3u ? static_cast<double>(samples.size() - 3u) : 0.0;
    }

    void setDelay(double delaySamples)
    {
        const double target = std::clamp(delaySamples, 0.0, maximumDelay());
        if (crossfade < 1.0f) {
            pendingDelay = target;
            hasPendingDelay = std::fabs(pendingDelay - delayB) >= 0.5;
            return;
        }
        if (std::fabs(target - delayB) < 0.5) return;
        delayA = delayB;
        delayB = target;
        crossfade = 0.0f;
    }

    float readAt(double delaySamples) const
    {
        if (samples.empty()) return 0.0f;
        double read = static_cast<double>(writePosition) - delaySamples;
        const double size = static_cast<double>(samples.size());
        read = std::fmod(read, size);
        if (read < 0.0) read += size;
        const size_t i0 = static_cast<size_t>(read);
        const size_t i1 = (i0 + 1u) % samples.size();
        const float a = samples[i0];
        const float b = samples[i1];
        return s3g::lerp(a, b, static_cast<float>(read - std::floor(read)));
    }

    float process(float input)
    {
        if (samples.empty()) return input;
        samples[writePosition] = std::clamp(input, -1.2f, 1.2f);
        const float a = readAt(delayA);
        const float b = readAt(delayB);
        const float mix = crossfade * crossfade * (3.0f - 2.0f * crossfade);
        const float output = s3g::lerp(a, b, mix);
        writePosition = (writePosition + 1u) % samples.size();
        crossfade = std::min(1.0f, crossfade + crossfadeStep);
        if (crossfade >= 1.0f) {
            delayA = delayB;
            if (hasPendingDelay) {
                const double next = pendingDelay;
                hasPendingDelay = false;
                if (std::fabs(next - delayB) >= 0.5) {
                    delayB = next;
                    crossfade = 0.0f;
                }
            }
        }
        return s3g::flushDenormal(output);
    }

    void reset()
    {
        std::fill(samples.begin(), samples.end(), 0.0f);
        writePosition = 0u;
        delayA = delayB;
        pendingDelay = delayB;
        crossfade = 1.0f;
        hasPendingDelay = false;
    }
};

struct VoxVoiceDelayBank {
    double sampleRate = 48000.0;
    float requestedStep = 0.0f;
    float stepCapacity = 0.0f;
    uint32_t voiceCapacity = 0u;
    std::array<VoxVoiceDelayLine, s3g::kAmbiVoxMaxVoices> lines;
};

struct VoxLyricSynthEvent {
    uint8_t symbol = 0u;
    uint8_t duration = 1u;
    float energy = 0.0f;
    float pitchMul = 1.0f;
    float noise = 0.0f;
    float plosive = 0.0f;
    float f1 = 600.0f;
    float f2 = 1500.0f;
    float f3 = 2800.0f;
};

struct VoxLyricBankEvent {
    uint16_t index = 0u;
    uint8_t duration = 1u;
    uint8_t flags = kVoxBankEventRest;
};

struct VoxLyricCue {
    std::string text;
    std::vector<VoxLyricSynthEvent> synthEvents;
    std::vector<VoxLyricBankEvent> bankEvents;
};

struct VoxLyricScore {
    std::vector<VoxLyricCue> cues;
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    double sampleRate = 48000.0;
    s3g::AmbiVotEncoder engine {};
    VoxPvocProcessor pvoc {};
    s3g::AmbiVotParams params {};
    VoxParams vox {};
    VoxLyricParams lyric {};
    VoxPresetMailbox presetMailbox {};
    std::atomic<bool> presetRescanPending { false };
    int32_t factoryPresetIndex = 0;
    char presetName[64] {};
    std::array<float, kOutputChannels> lastOutputSample {};
    std::array<float, kOutputChannels> presetTransitionOffset {};
    uint32_t presetTransitionFrames = 0u;
    uint32_t presetTransitionRemaining = 0u;
    bool presetTransitionNeedsInit = false;
    std::array<std::shared_ptr<const s3g::AmbiVotTableBank>, 4> presetBanks {};
    std::shared_ptr<const s3g::AmbiVotTableBank> userBank;
    std::shared_ptr<const VoxWorldSample> worldSample;
    std::shared_ptr<const VoxVoicebank> voicebank;
    std::shared_ptr<const VoxLyricScore> lyricScore;
    std::shared_ptr<const VoxLyricScore> activeLyricScore;
    std::shared_ptr<VoxVoiceDelayBank> voiceDelayBank;
    std::atomic<float> requestedDelayStepCapacity { 0.0f };
    std::atomic<uint32_t> requestedDelayVoiceCapacity { 0u };
    std::atomic<bool> voiceDelayResizePending { false };
    std::atomic<uint32_t> scoreNodeCount { 8u };
    std::atomic<uint32_t> scoreSustainStart { 2u };
    std::atomic<uint32_t> scoreSustainEnd { 6u };
    std::array<std::atomic<float>, s3g::kAmbiVotMaxScoreNodes> scoreTime {};
    std::array<std::atomic<float>, s3g::kAmbiVotMaxScoreNodes> scoreU {};
    std::array<std::atomic<float>, s3g::kAmbiVotMaxScoreNodes> scoreV {};
    std::array<std::atomic<uint32_t>, s3g::kAmbiVotMaxScoreNodes> scoreCurve {};
    std::array<std::atomic<uint8_t>, kVoxCompiledMaxFrames> voxCompiledSymbol {};
    std::array<std::atomic<uint8_t>, kVoxCompiledMaxFrames> voxCompiledDuration {};
    std::array<std::atomic<float>, kVoxCompiledMaxFrames> voxCompiledEnergy {};
    std::array<std::atomic<float>, kVoxCompiledMaxFrames> voxCompiledPitchMul {};
    std::array<std::atomic<float>, kVoxCompiledMaxFrames> voxCompiledNoise {};
    std::array<std::atomic<float>, kVoxCompiledMaxFrames> voxCompiledPlosive {};
    std::array<std::atomic<float>, kVoxCompiledMaxFrames> voxCompiledF1 {};
    std::array<std::atomic<float>, kVoxCompiledMaxFrames> voxCompiledF2 {};
    std::array<std::atomic<float>, kVoxCompiledMaxFrames> voxCompiledF3 {};
    std::atomic<uint32_t> voxCompiledCount { 0u };
    std::array<std::atomic<uint16_t>, kVoxCompiledMaxFrames> voxBankCompiledIndex {};
    std::array<std::atomic<uint8_t>, kVoxCompiledMaxFrames> voxBankCompiledDuration {};
    std::array<std::atomic<uint8_t>, kVoxCompiledMaxFrames> voxBankCompiledFlags {};
    std::atomic<uint32_t> voxBankCompiledCount { 0u };
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<uint32_t> lastMidiNote { 0u };
    std::atomic<float> lastMidiVelocity { 0.0f };
    uint32_t voxNoiseState = 0x51f15eedu;
    float voxConsonantEnv = 0.0f;
    float voxConsonantLevel = 0.0f;
    float voxPlosiveEnv = 0.0f;
    float voxPlosiveLevel = 0.0f;
    float voxPhrasePhase = 0.0f;
    float voxSibilantState = 0.0f;
    float voxFricativeState = 0.0f;
    float voxPhraseU = 0.72f;
    float voxPhraseV = 0.64f;
    uint32_t voxPhraseIndex = 0u;
    uint32_t voxPhraseCurrentIndex = 0u;
    uint32_t voxPhraseHold = 0u;
    uint32_t voxBankPhraseIndex = 0u;
    uint32_t voxBankPhraseHold = 0u;
    uint32_t voxBankCurrentEntry = 0u;
    std::array<uint32_t, s3g::kAmbiVoxMaxVoices> voxBankVoicePhraseIndex {};
    std::array<uint32_t, s3g::kAmbiVoxMaxVoices> voxBankVoiceCurrentPhraseIndex {};
    std::array<uint32_t, s3g::kAmbiVoxMaxVoices> voxBankVoicePhraseHold {};
    std::array<uint32_t, s3g::kAmbiVoxMaxVoices> voxBankVoiceCurrentEntry {};
    std::array<double, s3g::kAmbiVoxMaxVoices> voxBankVoicePhraseSamplesRemaining {};
    std::array<double, s3g::kAmbiVoxMaxVoices> voxBankVoicePhraseSamplesTotal {};
    std::array<uint32_t, s3g::kAmbiVoxMaxVoices> voxBankVoiceNextEntry {};
    std::array<uint8_t, s3g::kAmbiVoxMaxVoices> voxBankVoiceCurrentFlags {};
    std::array<uint8_t, s3g::kAmbiVoxMaxVoices> voxBankVoiceNextFlags {};
    std::array<uint8_t, s3g::kAmbiVoxMaxVoices> voxBankVoiceNextPitchAnchor {};
    std::array<bool, s3g::kAmbiVoxMaxVoices> voxBankVoiceTransitionActive {};
    std::atomic<bool> voxBankTimingResetRequested { false };
    std::atomic<bool> voxWorldTimingResetRequested { false };
    float voxPhraseBreathEnv = 0.0f;
    float voxPhraseVowelEnv = 0.0f;
    float voxPhraseVowelU = 0.72f;
    float voxPhraseVowelV = 0.64f;
    float voxFrameEnergy = 0.75f;
    float voxFramePitchMul = 1.0f;
    float voxFrameNoise = 0.0f;
    float voxFramePlosive = 0.0f;
    float voxFrameF1 = 600.0f;
    float voxFrameF2 = 1500.0f;
    float voxFrameF3 = 2800.0f;
    uint8_t voxFrameSymbol = 0u;
    float voxCarrierEnv = 0.0f;
    float voxOutputGainSmooth = 0.08f;
    float voxLimiterGain = 1.0f;
    float voxWorldRateSmooth = 0.50f;
    float voxWorldPitchSmooth = 0.0f;
    float voxWorldLoopStartSmooth = 0.0f;
    float voxWorldLoopEndSmooth = 1.0f;
    float voxWorldVoiceSpreadSmooth = 0.0f;
    float voxWorldVoiceDeviationSmooth = 0.0f;
    float voxWorldFormantSmooth = 0.0f;
    float voxWorldAirColorSmooth = 0.0f;
    float voxWorldFlutterSmooth = 0.0f;
    float voxWorldDegradeSmooth = 0.0f;
    float voxWorldEdgeSmooth = 0.34f;
    float voxWorldAirSmooth = 0.14f;
    float voxWorldDriveSmooth = 0.30f;
    float voxWorldFreezeSmooth = 0.0f;
    float voxWorldScrubSmooth = 0.0f;
    float voxWorldVoicingSmooth = 0.50f;
    float voxPvocStretchSmooth = 1.0f;
    float voxPvocTransientSmooth = 0.65f;
    std::array<float, s3g::kAmbiVoxMaxVoices> voxNeighborEnv {};
    std::array<float, s3g::kAmbiVoxMaxVoices> voxWorldVoiceGainSmooth {};
    std::array<float, s3g::kAmbiVoxMaxVoices> voxPitchRatioSmooth {};
    std::array<float, s3g::kAmbiVoxMaxVoices> voxContourRatioSmooth {};
    std::array<double, s3g::kAmbiVoxMaxVoices> voxWorldPhase {};
    std::array<VoxPvocVoiceState, s3g::kAmbiVoxMaxVoices> voxPvocVoice {};
    std::array<VoxPvocVoiceState, s3g::kAmbiVoxMaxVoices> voxPvocNextVoice {};
    std::array<uint8_t, s3g::kAmbiVoxMaxVoices> voxBankWorldPitchAnchor {};
    std::array<float, s3g::kAmbiVoxMaxVoices> voxTargetNoteSmooth {};
    std::array<float, s3g::kAmbiVoxMaxVoices> voxVibratoPhase {};
    std::array<int16_t, s3g::kAmbiVoxMaxVoices> voxMidiNote {};
    std::array<int16_t, s3g::kAmbiVoxMaxVoices> voxMidiChannel {};
    std::array<int32_t, s3g::kAmbiVoxMaxVoices> voxMidiNoteId {};
    std::array<float, s3g::kAmbiVoxMaxVoices> voxMidiVelocity {};
    std::array<uint64_t, s3g::kAmbiVoxMaxVoices> voxMidiAge {};
    std::array<bool, s3g::kAmbiVoxMaxVoices> voxMidiGate {};
    std::array<bool, s3g::kAmbiVoxMaxVoices> voxMidiRetrigger {};
    uint64_t voxMidiAgeCounter = 1u;
    std::array<float, s3g::kAmbiVoxMaxVoices> voxLpcPhase {};
    std::array<float, s3g::kAmbiVoxMaxVoices> voxLpcTilt {};
    std::array<float, s3g::kAmbiVoxMaxVoices> voxLpcPulse {};
    std::array<float, s3g::kAmbiVoxMaxVoices> voxLpcF0 {};
    std::array<float, s3g::kAmbiVoxMaxVoices> voxLpcOut {};
    std::array<float, s3g::kAmbiVoxMaxVoices> voxFormant1Hz {};
    std::array<float, s3g::kAmbiVoxMaxVoices> voxFormant2Hz {};
    std::array<float, s3g::kAmbiVoxMaxVoices> voxFormant3Hz {};
    std::array<float, s3g::kAmbiVoxMaxVoices> voxFormant1Low {};
    std::array<float, s3g::kAmbiVoxMaxVoices> voxFormant1Band {};
    std::array<float, s3g::kAmbiVoxMaxVoices> voxFormant2Low {};
    std::array<float, s3g::kAmbiVoxMaxVoices> voxFormant2Band {};
    std::array<float, s3g::kAmbiVoxMaxVoices> voxFormant3Low {};
    std::array<float, s3g::kAmbiVoxMaxVoices> voxFormant3Band {};
    std::array<std::atomic<uint8_t>, kVoxPhraseMaxChars> voxPhrase {};
    std::atomic<uint32_t> voxPhraseLength { 0u };
    std::array<std::atomic<uint8_t>, kVoxLyricsMaxChars> voxLyrics {};
    std::atomic<uint32_t> voxLyricsLength { 0u };
    std::atomic<uint32_t> requestedLyricCue { 0u };
    std::atomic<uint32_t> guiLyricCue { 0u };
    std::atomic<bool> lyricCueResetRequested { true };
    std::atomic<bool> lyricAutoClockResetRequested { true };
    uint32_t lyricRuntimeCue = 0u;
    uint32_t lyricMidiStepCue = 0u;
    bool lyricMidiStepStarted = false;
    double lyricAutoSamplesElapsed = 0.0;
    std::array<std::atomic<uint8_t>, kVoxLoadedNameMaxChars> voxLoadedName {};
    std::atomic<uint32_t> voxLoadedNameLength { 0u };
#if defined(__APPLE__)
    void* guiView = nullptr;
    std::atomic<bool> guiVisible { false };
    std::array<std::atomic<float>, s3g::kAmbiVoxMaxVoices> guiAzimuth {};
    std::array<std::atomic<float>, s3g::kAmbiVoxMaxVoices> guiElevation {};
    std::array<std::atomic<float>, s3g::kAmbiVoxMaxVoices> guiDistance {};
    std::array<std::atomic<float>, s3g::kAmbiVoxMaxVoices> guiU {};
    std::array<std::atomic<float>, s3g::kAmbiVoxMaxVoices> guiV {};
    std::array<std::atomic<uint32_t>, s3g::kAmbiVoxMaxVoices> guiNeighborCount {};
    std::array<std::atomic<uint32_t>, s3g::kAmbiVoxMaxVoices> guiNeighborGate {};
    std::atomic<float> guiMotionPhase { 0.0f };
    std::atomic<uint32_t> guiVoxPhraseEvent { 0u };
    std::atomic<float> guiVoxPhraseProgress { 0.0f };
    std::array<std::atomic<float>, s3g::kAmbiVoxMaxVoices> guiVoxPhrasePhase {};
    int guiPage = 0;
    int guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

struct VoxFactoryPresetDefinition {
    VoxPresetSnapshot snapshot {};
    const char* name = "VOX DEFAULT";
    const char* phrase = "ra ka ta sa sorrow ash";
};

VoxPresetSnapshot makeVoxDefaultSnapshot()
{
    VoxPresetSnapshot preset {};
    preset.params.order = kVoxFactoryPresetOrder;
    preset.params.preset = s3g::AmbiVotPreset::User;
    preset.params.vectorX = 0.72f;
    preset.params.vectorY = 0.64f;
    preset.params.morph = 0.82f;
    preset.params.detune = 0.18f;
    preset.params.pitchSpread = 0.58f;
    preset.params.subharmonicAmount = 0.32f;
    preset.params.attackMs = 45.0f;
    preset.params.releaseMs = 950.0f;
    preset.params.outputGainDb = -6.0f;
    return preset;
}

VoxFactoryPresetDefinition voxFactoryPreset(uint32_t index)
{
    VoxFactoryPresetDefinition preset {};
    preset.snapshot = makeVoxDefaultSnapshot();
    auto& params = preset.snapshot.params;
    auto& vox = preset.snapshot.vox;

    switch (std::min<uint32_t>(index, kVoxFactoryPresetCount - 1u)) {
    case 1u:
        preset.name = "SOLO SPEAK";
        preset.phrase = "ra ka ta sa sorrow ash";
        params.voices = 1u;
        params.motionAmount = 0.22f;
        params.motionSpread = 0.12f;
        params.motionCoherence = 0.92f;
        params.outputGainDb = -6.0f;
        vox.speechMode = VoxSpeechMode::Speak;
        vox.orchestration = s3g::AmbiVoxOrchestration::Individual;
        vox.contourMode = s3g::AmbiVoxContourMode::Original;
        vox.worldVoicing = 0.68f;
        vox.transition = 0.78f;
        break;
    case 2u:
        preset.name = "CLOSE UNISON";
        preset.phrase = "ah ra no mi ah ra no mi";
        params.voices = 8u;
        params.pitchSpread = 0.0f;
        params.detune = 0.04f;
        params.motionSpread = 0.34f;
        params.motionCoherence = 0.88f;
        params.outputGainDb = -6.0f;
        vox.speechMode = VoxSpeechMode::Sing;
        vox.orchestration = s3g::AmbiVoxOrchestration::Unison;
        vox.contourMode = s3g::AmbiVoxContourMode::Reduced;
        vox.worldVoiceSpread = 0.006f;
        vox.worldVoiceDeviation = 0.35f;
        vox.worldVoicing = 0.76f;
        break;
    case 3u:
        preset.name = "SATB CHORALE";
        preset.phrase = "a ve lu men a ve lu men";
        params.voices = 8u;
        params.baseNote = 48.0f;
        params.scale = s3g::AmbiVotScale::Major;
        params.pitchSpread = 0.64f;
        params.detune = 0.08f;
        params.motionSpread = 0.58f;
        params.motionCoherence = 0.76f;
        params.outputGainDb = -6.0f;
        vox.speechMode = VoxSpeechMode::Sing;
        vox.orchestration = s3g::AmbiVoxOrchestration::Chorale;
        vox.contourMode = s3g::AmbiVoxContourMode::Reduced;
        vox.worldVoiceSpread = 0.018f;
        vox.worldVoiceDeviation = 0.24f;
        vox.worldVoicing = 0.82f;
        vox.formantMacro = -0.08f;
        vox.phraseSpread = 0.12f;
        break;
    case 4u:
        preset.name = "DOUBLE CHOIR";
        preset.phrase = "o ra li a o ra li a";
        params.voices = 16u;
        params.scale = s3g::AmbiVotScale::Minor;
        params.pitchSpread = 0.78f;
        params.detune = 0.15f;
        params.motionSpread = 0.86f;
        params.motionCoherence = 0.62f;
        params.outputGainDb = -6.0f;
        vox.speechMode = VoxSpeechMode::Sing;
        vox.orchestration = s3g::AmbiVoxOrchestration::Chorus;
        vox.contourMode = s3g::AmbiVoxContourMode::Reduced;
        vox.worldVoiceSpread = 0.025f;
        vox.worldVoiceDeviation = 0.42f;
        vox.worldVoicing = 0.74f;
        vox.phraseSpread = 0.24f;
        break;
    case 5u:
        preset.name = "WIDE CHORUS";
        preset.phrase = "ha ya he yo ha ya he yo";
        params.voices = 12u;
        params.pitchSpread = 0.92f;
        params.detune = 0.24f;
        params.motionSpread = 0.98f;
        params.motionCoherence = 0.38f;
        params.motionChaos = 0.28f;
        params.outputGainDb = -6.0f;
        vox.speechMode = VoxSpeechMode::Sing;
        vox.orchestration = s3g::AmbiVoxOrchestration::Chorus;
        vox.contourMode = s3g::AmbiVoxContourMode::Original;
        vox.worldVoiceSpread = 0.032f;
        vox.worldVoiceDeviation = 0.58f;
        vox.worldVoicing = 0.64f;
        vox.phraseSpread = 0.10f;
        break;
    case 6u:
        preset.name = "FOUR PART ROUND";
        preset.phrase = "ka na ri o so la mi re";
        params.voices = 12u;
        params.motionScene = s3g::AmbiVotMotionScene::Orbit;
        params.motionRateHz = 0.032f;
        params.motionSpread = 0.82f;
        params.motionCoherence = 0.54f;
        params.outputGainDb = -6.0f;
        vox.speechMode = VoxSpeechMode::Speak;
        vox.orchestration = s3g::AmbiVoxOrchestration::Round;
        vox.contourMode = s3g::AmbiVoxContourMode::Reduced;
        vox.phraseRate = 0.25f;
        vox.worldVoiceSpread = 0.012f;
        vox.worldVoiceDeviation = 0.18f;
        vox.transition = 0.82f;
        vox.phraseSpread = 1.0f;
        break;
    case 7u:
        preset.name = "SEMITONE CLUSTER";
        preset.phrase = "u o a e i u o a e i";
        params.voices = 16u;
        params.baseNote = 55.0f;
        params.scale = s3g::AmbiVotScale::Chromatic;
        params.pitchSpread = 0.0f;
        params.detune = 0.03f;
        params.motionRateHz = 0.018f;
        params.motionSpread = 0.72f;
        params.motionCoherence = 0.70f;
        params.outputGainDb = -6.0f;
        vox.speechMode = VoxSpeechMode::Sing;
        vox.orchestration = s3g::AmbiVoxOrchestration::Cluster;
        vox.contourMode = s3g::AmbiVoxContourMode::Flat;
        vox.worldVoicing = 0.86f;
        break;
    case 8u:
        preset.name = "AIR CHOIR";
        preset.phrase = "sha ha sa he shu ho";
        params.voices = 12u;
        params.motionSpread = 0.88f;
        params.motionCoherence = 0.48f;
        params.outputGainDb = -6.0f;
        vox.speechMode = VoxSpeechMode::Sing;
        vox.orchestration = s3g::AmbiVoxOrchestration::Chorus;
        vox.contourMode = s3g::AmbiVoxContourMode::Reduced;
        vox.worldVoicing = 0.32f;
        vox.worldAirColor = 0.72f;
        vox.formantMacro = 0.24f;
        vox.pvocTransient = 0.34f;
        break;
    case 9u:
        preset.name = "DARK VOWELS";
        preset.phrase = "u o um oh u o um oh";
        params.voices = 8u;
        params.baseNote = 40.0f;
        params.scale = s3g::AmbiVotScale::HarmonicMinor;
        params.pitchSpread = 0.52f;
        params.motionRateHz = 0.014f;
        params.motionSpread = 0.56f;
        params.outputGainDb = -6.0f;
        vox.speechMode = VoxSpeechMode::Sing;
        vox.orchestration = s3g::AmbiVoxOrchestration::Chorale;
        vox.contourMode = s3g::AmbiVoxContourMode::Reduced;
        vox.formantMacro = -0.68f;
        vox.worldAirColor = -0.52f;
        vox.worldVoicing = 0.86f;
        break;
    case 10u:
        preset.name = "FROZEN TEXTURE";
        preset.phrase = "a e i o u";
        params.voices = 16u;
        params.motionRateHz = 0.009f;
        params.motionSpread = 0.94f;
        params.motionCoherence = 0.28f;
        params.motionChaos = 0.34f;
        params.outputGainDb = -6.0f;
        vox.speechMode = VoxSpeechMode::Texture;
        vox.orchestration = s3g::AmbiVoxOrchestration::Individual;
        vox.worldRate = 0.12f;
        vox.worldFreeze = 0.82f;
        vox.worldScrub = 0.37f;
        vox.pvocStretch = 2.80f;
        vox.pvocTransient = 0.20f;
        break;
    case 11u:
        preset.name = "ORBITING CHORUS";
        preset.phrase = "la ri a la ri a la ri a";
        params.voices = 12u;
        params.motionScene = s3g::AmbiVotMotionScene::Orbit;
        params.motionRateHz = 0.080f;
        params.motionAmount = 1.0f;
        params.motionSpread = 0.96f;
        params.motionCoherence = 0.44f;
        params.centerElevationDeg = 15.0f;
        params.outputGainDb = -6.0f;
        vox.speechMode = VoxSpeechMode::Sing;
        vox.orchestration = s3g::AmbiVoxOrchestration::Chorus;
        vox.contourMode = s3g::AmbiVoxContourMode::Reduced;
        vox.worldVoiceSpread = 0.020f;
        vox.worldVoiceDeviation = 0.32f;
        break;
    case 12u:
        preset.name = "FULL 3OA CHOIR";
        preset.phrase = "a ve o ra lu men a ve o ra";
        params.voices = 16u;
        params.scale = s3g::AmbiVotScale::Major;
        params.pitchSpread = 0.72f;
        params.detune = 0.12f;
        params.motionSpread = 0.90f;
        params.motionCoherence = 0.58f;
        params.motionSmooth = 0.88f;
        params.outputGainDb = -6.0f;
        vox.speechMode = VoxSpeechMode::Sing;
        vox.orchestration = s3g::AmbiVoxOrchestration::Chorale;
        vox.contourMode = s3g::AmbiVoxContourMode::Reduced;
        vox.worldVoiceSpread = 0.016f;
        vox.worldVoiceDeviation = 0.26f;
        vox.worldVoicing = 0.78f;
        vox.phraseSpread = 0.16f;
        break;
    case 0u:
    default:
        break;
    }
    params.order = kVoxFactoryPresetOrder;
    return preset;
}

void resetVoxMidiVoices(Plugin& plugin)
{
    plugin.voxMidiNote.fill(-1);
    plugin.voxMidiChannel.fill(-1);
    plugin.voxMidiNoteId.fill(-1);
    plugin.voxMidiVelocity.fill(0.0f);
    plugin.voxMidiAge.fill(0u);
    plugin.voxMidiGate.fill(false);
    plugin.voxMidiRetrigger.fill(false);
    plugin.voxMidiAgeCounter = 1u;
    plugin.lastMidiNote.store(0u, std::memory_order_relaxed);
    plugin.lastMidiVelocity.store(0.0f, std::memory_order_relaxed);
}

uint32_t allocateVoxMidiVoice(Plugin& plugin, int16_t channel, int32_t noteId, int16_t note, float velocity)
{
    const uint32_t voiceCount = std::clamp<uint32_t>(plugin.params.voices, 1u, s3g::kAmbiVoxMaxVoices);
    uint32_t selected = 0u;
    bool foundIdle = false;
    uint64_t oldestAge = std::numeric_limits<uint64_t>::max();
    for (uint32_t voice = 0u; voice < voiceCount; ++voice) {
        if (!plugin.voxMidiGate[voice]) {
            selected = voice;
            foundIdle = true;
            break;
        }
        if (plugin.voxMidiAge[voice] < oldestAge) {
            oldestAge = plugin.voxMidiAge[voice];
            selected = voice;
        }
    }
    (void)foundIdle;
    plugin.voxMidiNote[selected] = note;
    plugin.voxMidiChannel[selected] = channel;
    plugin.voxMidiNoteId[selected] = noteId;
    plugin.voxMidiVelocity[selected] = std::clamp(velocity, 0.0f, 1.0f);
    plugin.voxMidiAge[selected] = plugin.voxMidiAgeCounter++;
    plugin.voxMidiGate[selected] = true;
    plugin.voxMidiRetrigger[selected] = true;
    plugin.lastMidiNote.store(static_cast<uint32_t>(std::max<int16_t>(0, note)), std::memory_order_relaxed);
    plugin.lastMidiVelocity.store(std::clamp(velocity, 0.0f, 1.0f), std::memory_order_relaxed);
    return selected;
}

void refreshLastVoxMidiNote(Plugin& plugin)
{
    uint64_t newestAge = 0u;
    uint32_t newestNote = 0u;
    float newestVelocity = 0.0f;
    for (uint32_t voice = 0u; voice < s3g::kAmbiVoxMaxVoices; ++voice) {
        if (plugin.voxMidiGate[voice] && plugin.voxMidiAge[voice] >= newestAge) {
            newestAge = plugin.voxMidiAge[voice];
            newestNote = static_cast<uint32_t>(std::max<int16_t>(0, plugin.voxMidiNote[voice]));
            newestVelocity = plugin.voxMidiVelocity[voice];
        }
    }
    plugin.lastMidiNote.store(newestNote, std::memory_order_relaxed);
    plugin.lastMidiVelocity.store(newestVelocity, std::memory_order_relaxed);
}

void releaseVoxMidiVoice(Plugin& plugin, int16_t channel, int32_t noteId, int16_t note)
{
    for (uint32_t voice = 0u; voice < s3g::kAmbiVoxMaxVoices; ++voice) {
        if (!plugin.voxMidiGate[voice] || plugin.voxMidiNote[voice] != note) continue;
        if (channel >= 0 && plugin.voxMidiChannel[voice] != channel) continue;
        if (noteId >= 0 && plugin.voxMidiNoteId[voice] >= 0 && plugin.voxMidiNoteId[voice] != noteId) continue;
        plugin.voxMidiGate[voice] = false;
        plugin.voxMidiVelocity[voice] = 0.0f;
    }
    refreshLastVoxMidiNote(plugin);
}

bool voxVoiceHasMidiPitch(const Plugin& plugin, uint32_t voice)
{
    if (voice >= s3g::kAmbiVoxMaxVoices || plugin.params.mode == s3g::AmbiVotMode::Free) return false;
    if (plugin.voxMidiNote[voice] < 0) return false;
    return plugin.voxMidiGate[voice] || plugin.params.mode == s3g::AmbiVotMode::Midi;
}

float voxVoiceTargetNote(const Plugin& plugin, uint32_t voice)
{
    const bool ensembleMidi = plugin.vox.orchestration != s3g::AmbiVoxOrchestration::Individual
        && plugin.params.mode != s3g::AmbiVotMode::Free
        && plugin.lastMidiVelocity.load(std::memory_order_relaxed) > 0.0f;
    const float root = ensembleMidi
        ? static_cast<float>(plugin.lastMidiNote.load(std::memory_order_relaxed))
        : plugin.params.baseNote;
    float scaleNote = root;
    int deviationSeedNote = static_cast<int>(std::lround(root));
    if (ensembleMidi) {
        const float orchestrated = s3g::ambiVoxOrchestratedFreeNote(
            plugin.vox.orchestration, root, plugin.params.scale, voice);
        scaleNote = root + (orchestrated - root)
            * std::clamp(plugin.params.pitchSpread, 0.0f, 2.0f);
        deviationSeedNote = static_cast<int>(std::lround(root));
    } else if (voxVoiceHasMidiPitch(plugin, voice)) {
        const float sourceNote = static_cast<float>(plugin.voxMidiNote[voice]);
        const float quantized = s3g::ambiVotQuantizeToScale(sourceNote, root, plugin.params.scale);
        const float spreadCandidate = root + (quantized - root) * std::clamp(plugin.params.pitchSpread, 0.0f, 2.0f);
        scaleNote = s3g::ambiVotQuantizeToScale(spreadCandidate, root, plugin.params.scale);
        deviationSeedNote = static_cast<int>(std::lround(sourceNote));
    } else if (plugin.vox.orchestration != s3g::AmbiVoxOrchestration::Individual) {
        const float orchestrated = s3g::ambiVoxOrchestratedFreeNote(
            plugin.vox.orchestration, root, plugin.params.scale, voice);
        scaleNote = root + (orchestrated - root)
            * std::clamp(plugin.params.pitchSpread, 0.0f, 2.0f);
    } else {
        const int relationshipDegree = static_cast<int>(voice) - static_cast<int>(plugin.params.voices / 2u);
        const int spreadDegree = static_cast<int>(std::lround(
            static_cast<float>(relationshipDegree) * std::clamp(plugin.params.pitchSpread, 0.0f, 2.0f)));
        scaleNote = root + static_cast<float>(s3g::ambiVotScaleDegreeSemitones(plugin.params.scale, spreadDegree));
    }
    const float cents = plugin.params.tuneCents
        + s3g::ambiVotPitchDeviation(voice, deviationSeedNote, plugin.params.detune)
        + s3g::ambiVoxOrchestrationDetuneCents(plugin.vox.orchestration, voice);
    return std::clamp(scaleNote + cents / 100.0f, -24.0f, 132.0f);
}

float voxVoiceExpressiveNote(Plugin& plugin, uint32_t voice, bool singing)
{
    if (voice >= s3g::kAmbiVoxMaxVoices) return plugin.params.baseNote;
    const float target = voxVoiceTargetNote(plugin, voice);
    const float portamentoMs = 2.0f + std::pow(std::clamp(plugin.vox.portamento, 0.0f, 1.0f), 2.0f) * 750.0f;
    const float coefficient = 1.0f - std::exp(-1.0f
        / std::max(1.0f, portamentoMs * 0.001f * static_cast<float>(plugin.sampleRate)));
    if (!std::isfinite(plugin.voxTargetNoteSmooth[voice])) plugin.voxTargetNoteSmooth[voice] = target;
    plugin.voxTargetNoteSmooth[voice] += (target - plugin.voxTargetNoteSmooth[voice]) * coefficient;
    const float vibratoRate = std::clamp(plugin.vox.vibratoRateHz, 0.1f, 12.0f);
    plugin.voxVibratoPhase[voice] = s3g::ambiVotFract(
        plugin.voxVibratoPhase[voice] + vibratoRate / static_cast<float>(plugin.sampleRate));
    const float modeScale = singing ? 1.0f : 0.22f;
    const float vibratoSemitones = std::sin(plugin.voxVibratoPhase[voice] * 2.0f * s3g::kPi)
        * std::clamp(plugin.vox.vibratoDepth, 0.0f, 1.0f) * modeScale;
    return plugin.voxTargetNoteSmooth[voice] + vibratoSemitones;
}

float voxVoiceActivityTarget(const Plugin& plugin, uint32_t voice, uint32_t activeVoices)
{
    if (voice >= activeVoices) return 0.0f;
    if (plugin.params.mode != s3g::AmbiVotMode::Midi) return 1.0f;
    if (plugin.vox.orchestration != s3g::AmbiVoxOrchestration::Individual) {
        return plugin.lastMidiVelocity.load(std::memory_order_relaxed);
    }
    return plugin.voxMidiGate[voice] ? std::max(0.08f, plugin.voxMidiVelocity[voice]) : 0.0f;
}

float voxVoiceTimeDeviation(uint32_t voice)
{
    uint32_t hash = (voice + 1u) * 0x9e3779b9u;
    hash ^= hash >> 16u;
    hash *= 0x7feb352du;
    hash ^= hash >> 15u;
    return static_cast<float>(hash & 0xffffu) / 32767.5f - 1.0f;
}

double voxVoiceTimeOffsetSamples(float stepAmount,
                                 float deviationAmount,
                                 double sampleRate,
                                 uint32_t voice,
                                 s3g::AmbiVoxOrchestration orchestration)
{
    if (sampleRate <= 0.0) return 0.0;
    const double stepMs = static_cast<double>(std::clamp(stepAmount, 0.0f, 1.0f))
        * static_cast<double>(kVoxVoiceTimeStepMaxMs);
    const double deviationMs = static_cast<double>(voxVoiceTimeDeviation(voice))
        * stepMs * static_cast<double>(std::clamp(deviationAmount, 0.0f, 1.0f));
    const double ensembleMs = static_cast<double>(
        s3g::ambiVoxOrchestrationDelayMs(orchestration, voice));
    const double offsetMs = std::max(0.0,
        static_cast<double>(voice) * stepMs + deviationMs + ensembleMs);
    return sampleRate * offsetMs * 0.001;
}

float voxDelayStepCapacity(float requestedStep, uint32_t voices, double sampleRate)
{
    float bucket = 0.0625f;
    while (bucket < requestedStep && bucket < 1.0f) bucket *= 2.0f;
    bucket = std::clamp(bucket, 0.0625f, 1.0f);
    const double voiceCount = static_cast<double>(std::max<uint32_t>(1u, voices));
    const double triangular = voiceCount * (voiceCount + 1.0) * 0.5;
    constexpr double maximumDelayBytes = 256.0 * 1024.0 * 1024.0;
    const double maximumSamples = maximumDelayBytes / static_cast<double>(sizeof(float));
    const double memoryLimitedStep = maximumSamples
        / std::max(1.0, sampleRate * triangular);
    return static_cast<float>(std::clamp(
        std::min<double>(bucket, memoryLimitedStep), 0.001, 1.0));
}

std::shared_ptr<VoxVoiceDelayBank> makeVoiceDelayBank(double sampleRate,
                                                       uint32_t voices,
                                                       float requestedStep)
{
    auto bank = std::make_shared<VoxVoiceDelayBank>();
    bank->sampleRate = std::max(8000.0, sampleRate);
    bank->voiceCapacity = std::clamp<uint32_t>(voices, 1u, s3g::kAmbiVoxMaxVoices);
    // Reserve the complete timing range once so moving TIME STEP never replaces
    // a live delay line with an empty buffer.
    (void)requestedStep;
    constexpr float capacityStep = 1.0f;
    bank->requestedStep = capacityStep;
    bank->stepCapacity = voxDelayStepCapacity(capacityStep, bank->voiceCapacity, bank->sampleRate);
    for (uint32_t voice = 0u; voice < s3g::kAmbiVoxMaxVoices; ++voice) {
        const double seconds = voice < bank->voiceCapacity
            ? static_cast<double>(voice + 1u) * static_cast<double>(bank->stepCapacity) + 0.040
            : 0.0;
        const size_t capacity = static_cast<size_t>(std::ceil(seconds * bank->sampleRate)) + 4u;
        bank->lines[voice].prepare(capacity, bank->sampleRate);
    }
    return bank;
}

void requestVoiceDelayResize(Plugin& plugin)
{
    const uint32_t voices = std::clamp<uint32_t>(plugin.params.voices, 1u, s3g::kAmbiVoxMaxVoices);
    const float requestedStep = std::clamp(plugin.vox.worldVoiceSpread, 0.0f, 1.0f);
    const auto current = std::atomic_load_explicit(&plugin.voiceDelayBank, std::memory_order_acquire);
    if (current && current->voiceCapacity >= voices
        && current->requestedStep + 0.0001f >= requestedStep
        && std::fabs(current->sampleRate - plugin.sampleRate) < 0.5) {
        return;
    }
    plugin.requestedDelayVoiceCapacity.store(voices, std::memory_order_relaxed);
    plugin.requestedDelayStepCapacity.store(requestedStep, std::memory_order_relaxed);
    if (!plugin.voiceDelayResizePending.exchange(true, std::memory_order_acq_rel)
        && plugin.host && plugin.host->request_callback) {
        plugin.host->request_callback(plugin.host);
    }
}

std::shared_ptr<VoxVoiceDelayBank> prepareVoiceDelays(Plugin& plugin, uint32_t voices)
{
    auto bank = std::atomic_load_explicit(&plugin.voiceDelayBank, std::memory_order_acquire);
    if (!bank || bank->voiceCapacity < voices
        || bank->requestedStep + 0.0001f < plugin.vox.worldVoiceSpread
        || std::fabs(bank->sampleRate - plugin.sampleRate) >= 0.5) {
        requestVoiceDelayResize(plugin);
    }
    if (!bank) return nullptr;
    for (uint32_t voice = 0u; voice < std::min<uint32_t>(voices, bank->voiceCapacity); ++voice) {
        bank->lines[voice].setDelay(voxVoiceTimeOffsetSamples(
            plugin.voxWorldVoiceSpreadSmooth, plugin.voxWorldVoiceDeviationSmooth,
            plugin.sampleRate, voice, plugin.vox.orchestration));
    }
    return bank;
}

float processVoiceDelay(const std::shared_ptr<VoxVoiceDelayBank>& bank,
                        uint32_t voice,
                        float value)
{
    if (!bank || voice >= bank->voiceCapacity) return value;
    return bank->lines[voice].process(value);
}

void syncWorldSmoothing(Plugin& plugin)
{
    plugin.voxWorldRateSmooth = std::clamp(plugin.vox.worldRate, 0.0f, 1.0f);
    plugin.voxWorldPitchSmooth = std::clamp(plugin.vox.worldPitchCents, -2400.0f, 2400.0f);
    plugin.voxWorldLoopStartSmooth = std::clamp(plugin.vox.worldLoopStart, 0.0f, 1.0f);
    plugin.voxWorldLoopEndSmooth = std::clamp(plugin.vox.worldLoopEnd, 0.0f, 1.0f);
    plugin.voxWorldVoiceSpreadSmooth = std::clamp(plugin.vox.worldVoiceSpread, 0.0f, 1.0f);
    plugin.voxWorldVoiceDeviationSmooth = std::clamp(plugin.vox.worldVoiceDeviation, 0.0f, 1.0f);
    plugin.voxWorldFormantSmooth = std::clamp(plugin.vox.formantMacro, -1.0f, 1.0f);
    plugin.voxWorldAirColorSmooth = std::clamp(plugin.vox.worldAirColor, -1.0f, 1.0f);
    plugin.voxWorldFlutterSmooth = std::clamp(plugin.vox.bendMacro, 0.0f, 1.0f);
    plugin.voxWorldDegradeSmooth = std::clamp(plugin.vox.crushMacro, 0.0f, 1.0f);
    plugin.voxWorldEdgeSmooth = std::clamp(plugin.vox.rasp, 0.0f, 1.0f);
    plugin.voxWorldAirSmooth = std::clamp(plugin.vox.breath, 0.0f, 1.0f);
    plugin.voxWorldDriveSmooth = std::clamp(plugin.vox.drive, 0.0f, 1.0f);
    plugin.voxWorldFreezeSmooth = std::clamp(plugin.vox.worldFreeze, 0.0f, 1.0f);
    plugin.voxWorldScrubSmooth = std::clamp(plugin.vox.worldScrub, 0.0f, 1.0f);
    plugin.voxWorldVoicingSmooth = std::clamp(plugin.vox.worldVoicing, 0.0f, 1.0f);
    plugin.voxPvocStretchSmooth = std::clamp(plugin.vox.pvocStretch, 0.25f, 4.0f);
    plugin.voxPvocTransientSmooth = std::clamp(plugin.vox.pvocTransient, 0.0f, 1.0f);
}

s3g::AmbiVotVectorScore loadScore(const Plugin& plugin)
{
    s3g::AmbiVotVectorScore score {};
    score.nodeCount = plugin.scoreNodeCount.load(std::memory_order_acquire);
    score.sustainStart = plugin.scoreSustainStart.load(std::memory_order_relaxed);
    score.sustainEnd = plugin.scoreSustainEnd.load(std::memory_order_relaxed);
    for (uint32_t i = 0; i < s3g::kAmbiVotMaxScoreNodes; ++i) {
        score.nodes[i].time = plugin.scoreTime[i].load(std::memory_order_relaxed);
        score.nodes[i].u = plugin.scoreU[i].load(std::memory_order_relaxed);
        score.nodes[i].v = plugin.scoreV[i].load(std::memory_order_relaxed);
        score.nodes[i].curve = static_cast<s3g::AmbiVotScoreCurve>(
            plugin.scoreCurve[i].load(std::memory_order_relaxed));
    }
    s3g::ambiVotNormalizeScore(score);
    return score;
}

void storeScore(Plugin& plugin, s3g::AmbiVotVectorScore score)
{
    s3g::ambiVotNormalizeScore(score);
    for (uint32_t i = 0; i < s3g::kAmbiVotMaxScoreNodes; ++i) {
        plugin.scoreTime[i].store(score.nodes[i].time, std::memory_order_relaxed);
        plugin.scoreU[i].store(score.nodes[i].u, std::memory_order_relaxed);
        plugin.scoreV[i].store(score.nodes[i].v, std::memory_order_relaxed);
        plugin.scoreCurve[i].store(static_cast<uint32_t>(score.nodes[i].curve), std::memory_order_relaxed);
    }
    plugin.scoreSustainStart.store(score.sustainStart, std::memory_order_relaxed);
    plugin.scoreSustainEnd.store(score.sustainEnd, std::memory_order_relaxed);
    plugin.scoreNodeCount.store(score.nodeCount, std::memory_order_release);
}

bool queueVoxPreset(Plugin& plugin, const VoxPresetSnapshot& preset)
{
    if (!plugin.presetMailbox.push(preset)) return false;
    if (plugin.hostParams && plugin.hostParams->request_flush) {
        plugin.hostParams->request_flush(plugin.host);
    } else if (plugin.host && plugin.host->request_process) {
        plugin.host->request_process(plugin.host);
    }
    return true;
}

void applyPendingVoxPreset(Plugin& plugin)
{
    VoxPresetSnapshot pending {};
    VoxPresetSnapshot latest {};
    bool hasPreset = false;
    while (plugin.presetMailbox.pop(pending)) {
        latest = pending;
        hasPreset = true;
    }
    if (!hasPreset) return;

    plugin.params = latest.params;
    plugin.vox = latest.vox;
    plugin.lyric = latest.lyric;
    plugin.lyric.mode = static_cast<VoxLyricMode>(
        std::clamp<uint32_t>(static_cast<uint32_t>(plugin.lyric.mode), 0u, 4u));
    plugin.lyric.cueBeats = std::clamp(plugin.lyric.cueBeats, 0.25f, 64.0f);
    plugin.lyric.cueBaseNote = std::clamp<uint32_t>(plugin.lyric.cueBaseNote, 0u, 127u);
    plugin.lyric.cueChannel = std::clamp<uint32_t>(plugin.lyric.cueChannel, 1u, 16u);
    plugin.params.voices = std::clamp<uint32_t>(
        plugin.params.voices, 1u, s3g::kAmbiVoxMaxVoices);
    plugin.engine.setParams(plugin.params);
    plugin.params = plugin.engine.params();
    storeScore(plugin, latest.score);
    plugin.engine.setScore(loadScore(plugin));
    plugin.voxBankTimingResetRequested.store(true, std::memory_order_release);
    plugin.voxWorldTimingResetRequested.store(true, std::memory_order_release);
    plugin.lyricMidiStepStarted = false;
    plugin.lyricAutoSamplesElapsed = 0.0;
    plugin.lyricAutoClockResetRequested.store(false, std::memory_order_release);
    plugin.lyricCueResetRequested.store(true, std::memory_order_release);
    requestVoiceDelayResize(plugin);
    plugin.presetTransitionFrames = std::max<uint32_t>(1u,
        static_cast<uint32_t>(std::lround(plugin.sampleRate * 0.012)));
    plugin.presetTransitionRemaining = plugin.presetTransitionFrames;
    plugin.presetTransitionNeedsInit = true;
    plugin.presetRescanPending.store(true, std::memory_order_release);
    if (plugin.host && plugin.host->request_callback) plugin.host->request_callback(plugin.host);
}

void applyVoxPresetTransition(Plugin& plugin, float* const* outputs,
                              uint32_t channels, uint32_t frames)
{
    if (!outputs || frames == 0u) return;
    if (plugin.presetTransitionNeedsInit) {
        for (uint32_t channel = 0u; channel < channels; ++channel) {
            plugin.presetTransitionOffset[channel] = outputs[channel]
                ? plugin.lastOutputSample[channel] - outputs[channel][0]
                : 0.0f;
        }
        plugin.presetTransitionNeedsInit = false;
    }
    for (uint32_t frame = 0u;
         frame < frames && plugin.presetTransitionRemaining > 0u; ++frame) {
        const float fade = static_cast<float>(plugin.presetTransitionRemaining)
            / static_cast<float>(std::max<uint32_t>(1u, plugin.presetTransitionFrames));
        for (uint32_t channel = 0u; channel < channels; ++channel) {
            if (outputs[channel]) outputs[channel][frame]
                += plugin.presetTransitionOffset[channel] * fade;
        }
        --plugin.presetTransitionRemaining;
    }
    if (plugin.presetTransitionRemaining == 0u) {
        plugin.presetTransitionOffset.fill(0.0f);
    }
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        plugin.lastOutputSample[channel] = outputs[channel]
            ? outputs[channel][frames - 1u] : 0.0f;
    }
    for (uint32_t channel = channels; channel < kOutputChannels; ++channel) {
        plugin.lastOutputSample[channel] = 0.0f;
    }
}

std::shared_ptr<const s3g::AmbiVotTableBank> makePresetBank(s3g::AmbiVotPreset preset)
{
    return std::make_shared<s3g::AmbiVotTableBank>(s3g::ambiVotPresetBank(preset));
}

std::shared_ptr<const s3g::AmbiVotTableBank> restoreUserAtlas(
    const std::array<float, s3g::kAmbiVotAtlasSampleCount>& atlas)
{
    auto bank = std::make_shared<s3g::AmbiVotTableBank>();
    bank->user = true;
    bank->exactAtlas = true;
    for (uint32_t table = 0u; table < s3g::kAmbiVotTableCount; ++table) {
        std::copy_n(atlas.begin() + table * s3g::kAmbiVotTableSize,
                    s3g::kAmbiVotTableSize, bank->tables[table].begin());
    }
    s3g::ambiVotBuildBandTables(*bank);
    return bank;
}

std::shared_ptr<const s3g::AmbiVotTableBank> activeBank(Plugin& plugin)
{
    if (plugin.params.preset == s3g::AmbiVotPreset::User) {
        auto user = std::atomic_load_explicit(&plugin.userBank, std::memory_order_acquire);
        if (user) return user;
    }
    const uint32_t index = std::min<uint32_t>(
        static_cast<uint32_t>(plugin.params.preset),
        static_cast<uint32_t>(s3g::AmbiVotPreset::Formant));
    return plugin.presetBanks[index];
}

std::string loadVoxPhrase(const Plugin& plugin)
{
    const uint32_t length = std::min<uint32_t>(
        plugin.voxPhraseLength.load(std::memory_order_acquire), kVoxPhraseMaxChars - 1u);
    std::string text;
    text.reserve(length);
    for (uint32_t i = 0; i < length; ++i) {
        const uint8_t ch = plugin.voxPhrase[i].load(std::memory_order_relaxed);
        if (ch == 0u) break;
        text.push_back(static_cast<char>(ch));
    }
    return text;
}

std::string loadVoxLyrics(const Plugin& plugin)
{
    const uint32_t length = std::min<uint32_t>(
        plugin.voxLyricsLength.load(std::memory_order_acquire), kVoxLyricsMaxChars - 1u);
    std::string text;
    text.reserve(length);
    for (uint32_t i = 0; i < length; ++i) {
        const uint8_t ch = plugin.voxLyrics[i].load(std::memory_order_relaxed);
        if (ch == 0u) break;
        text.push_back(static_cast<char>(ch));
    }
    return text;
}

std::string trimVoxLyricLine(const std::string& line)
{
    size_t first = 0u;
    while (first < line.size()
        && std::isspace(static_cast<unsigned char>(line[first]))) ++first;
    size_t last = line.size();
    while (last > first
        && std::isspace(static_cast<unsigned char>(line[last - 1u]))) --last;
    return line.substr(first, last - first);
}

std::vector<std::string> splitVoxLyricCues(const std::string& text)
{
    std::vector<std::string> cues;
    std::string line;
    const auto flush = [&]() {
        const std::string trimmed = trimVoxLyricLine(line);
        if (!trimmed.empty() && cues.size() < kVoxLyricsMaxCues) cues.push_back(trimmed);
        line.clear();
    };
    for (const char ch : text) {
        if (ch == '\n' || ch == '\r') {
            if (ch == '\n' || !line.empty()) flush();
        } else {
            line.push_back(ch);
        }
    }
    flush();
    return cues;
}

void storeVoxLyricsText(Plugin& plugin, const char* text)
{
    const size_t length = text
        ? std::min(std::strlen(text), static_cast<size_t>(kVoxLyricsMaxChars - 1u))
        : 0u;
    for (uint32_t i = 0; i < kVoxLyricsMaxChars; ++i) {
        const uint8_t ch = i < length ? static_cast<uint8_t>(text[i]) : 0u;
        plugin.voxLyrics[i].store(ch, std::memory_order_relaxed);
    }
    plugin.voxLyricsLength.store(static_cast<uint32_t>(length), std::memory_order_release);
}

void rebuildVoxLyricScore(Plugin& plugin);
void requestVoxLyricCue(Plugin& plugin, uint32_t cue);
uint32_t voxLyricCueCount(const Plugin& plugin);

std::string voxLyricCueText(const Plugin& plugin, uint32_t cueIndex)
{
    const auto cues = splitVoxLyricCues(loadVoxLyrics(plugin));
    return cueIndex < cues.size() ? cues[cueIndex] : std::string();
}

void replaceVoxLyricCueText(Plugin& plugin, uint32_t cueIndex, const char* text)
{
    auto cues = splitVoxLyricCues(loadVoxLyrics(plugin));
    if (cues.empty()) cues.push_back(loadVoxPhrase(plugin));
    cueIndex = std::min<uint32_t>(cueIndex, static_cast<uint32_t>(cues.size() - 1u));
    cues[cueIndex] = trimVoxLyricLine(text ? text : "");
    std::string lyrics;
    for (const auto& cue : cues) {
        if (cue.empty()) continue;
        if (!lyrics.empty()) lyrics.push_back('\n');
        lyrics += cue;
    }
    if (lyrics.empty()) lyrics = " ";
    storeVoxLyricsText(plugin, lyrics.c_str());
    rebuildVoxLyricScore(plugin);
    requestVoxLyricCue(plugin, std::min<uint32_t>(cueIndex,
        std::max<uint32_t>(1u, voxLyricCueCount(plugin)) - 1u));
}

std::string loadVoxLoadedName(const Plugin& plugin)
{
    const uint32_t length = std::min<uint32_t>(
        plugin.voxLoadedNameLength.load(std::memory_order_acquire), kVoxLoadedNameMaxChars - 1u);
    std::string text;
    text.reserve(length);
    for (uint32_t i = 0; i < length; ++i) {
        const uint8_t ch = plugin.voxLoadedName[i].load(std::memory_order_relaxed);
        if (ch == 0u) break;
        text.push_back(static_cast<char>(ch));
    }
    return text;
}

void storeVoxLoadedName(Plugin& plugin, const char* text)
{
    const size_t length = text ? std::min(std::strlen(text), static_cast<size_t>(kVoxLoadedNameMaxChars - 1u)) : 0u;
    for (uint32_t i = 0; i < kVoxLoadedNameMaxChars; ++i) {
        const uint8_t ch = i < length ? static_cast<uint8_t>(text[i]) : 0u;
        plugin.voxLoadedName[i].store(ch, std::memory_order_relaxed);
    }
    plugin.voxLoadedNameLength.store(static_cast<uint32_t>(length), std::memory_order_release);
}

void compileVoxPhrase(Plugin& plugin);
void compileVoicebankPhrase(Plugin& plugin, const VoxVoicebank& bank);
void rebuildVoxLyricScore(Plugin& plugin);

void storeVoxPhrase(Plugin& plugin, const char* text)
{
    const size_t length = text ? std::min(std::strlen(text), static_cast<size_t>(kVoxPhraseMaxChars - 1u)) : 0u;
    for (uint32_t i = 0; i < kVoxPhraseMaxChars; ++i) {
        const uint8_t ch = i < length ? static_cast<uint8_t>(text[i]) : 0u;
        plugin.voxPhrase[i].store(ch, std::memory_order_relaxed);
    }
    plugin.voxPhraseLength.store(static_cast<uint32_t>(length), std::memory_order_release);
    compileVoxPhrase(plugin);
    if (const auto bank = std::atomic_load_explicit(&plugin.voicebank, std::memory_order_acquire)) {
        compileVoicebankPhrase(plugin, *bank);
    }
}

enum VoxPhoneme : uint8_t {
    kVoxRest = 0,
    kVoxA = 'a',
    kVoxE = 'e',
    kVoxI = 'i',
    kVoxO = 'o',
    kVoxU = 'u',
    kVoxY = 'y',
    kVoxP = 'p',
    kVoxT = 't',
    kVoxK = 'k',
    kVoxS = 's',
    kVoxF = 'f',
    kVoxH = 'h',
    kVoxR = 'r',
    kVoxL = 'l',
    kVoxM = 'm',
    kVoxN = 'n',
    kVoxW = 'w',
    kVoxSh = 1,
    kVoxCh = 2,
    kVoxTh = 3,
    kVoxNg = 4,
    kVoxAr = 5,
    kVoxEr = 6,
    kVoxOr = 7,
    kVoxOo = 8,
    kVoxEe = 9,
    kVoxAi = 10,
    kVoxOw = 11,
    kVoxOi = 12,
};

struct VoxSpeechGlyph {
    float u = 0.72f;
    float v = 0.64f;
    float consonant = 0.35f;
    float plosive = 0.0f;
    float sibilance = 0.0f;
    float breath = 0.0f;
    uint32_t advance = 1u;
    uint32_t hold = 0u;
    bool vowel = false;
    bool rest = false;
};

struct VoxLpcFrame {
    float energy = 0.75f;
    float pitchMul = 1.0f;
    float noise = 0.0f;
    float plosive = 0.0f;
    float f1 = 600.0f;
    float f2 = 1500.0f;
    float f3 = 2800.0f;
};

VoxLpcFrame voxAllophoneFrame(uint8_t symbol)
{
    VoxLpcFrame frame {};
    frame.energy = 0.78f;
    frame.pitchMul = 1.0f;
    frame.noise = 0.04f;
    frame.plosive = 0.0f;
    frame.f1 = 600.0f;
    frame.f2 = 1300.0f;
    frame.f3 = 2500.0f;
    switch (symbol) {
    case kVoxA: frame.energy = 0.95f; frame.f1 = 730.0f; frame.f2 = 1090.0f; frame.f3 = 2440.0f; break;
    case kVoxE: frame.energy = 0.90f; frame.f1 = 530.0f; frame.f2 = 1840.0f; frame.f3 = 2480.0f; break;
    case kVoxI: frame.energy = 0.88f; frame.f1 = 390.0f; frame.f2 = 1990.0f; frame.f3 = 2550.0f; break;
    case kVoxO: frame.energy = 0.92f; frame.f1 = 570.0f; frame.f2 = 840.0f; frame.f3 = 2410.0f; break;
    case kVoxU: frame.energy = 0.86f; frame.f1 = 300.0f; frame.f2 = 870.0f; frame.f3 = 2240.0f; break;
    case kVoxY: frame.energy = 0.82f; frame.f1 = 310.0f; frame.f2 = 2050.0f; frame.f3 = 2700.0f; break;
    case kVoxEe: frame.energy = 0.90f; frame.f1 = 270.0f; frame.f2 = 2290.0f; frame.f3 = 3010.0f; break;
    case kVoxOo: frame.energy = 0.88f; frame.f1 = 300.0f; frame.f2 = 870.0f; frame.f3 = 2240.0f; break;
    case kVoxAi: frame.energy = 0.92f; frame.f1 = 520.0f; frame.f2 = 1750.0f; frame.f3 = 2600.0f; frame.pitchMul = 1.02f; break;
    case kVoxOw: frame.energy = 0.90f; frame.f1 = 430.0f; frame.f2 = 920.0f; frame.f3 = 2350.0f; frame.pitchMul = 0.98f; break;
    case kVoxOi: frame.energy = 0.90f; frame.f1 = 440.0f; frame.f2 = 1600.0f; frame.f3 = 2550.0f; break;
    case kVoxAr: frame.energy = 0.92f; frame.f1 = 650.0f; frame.f2 = 1100.0f; frame.f3 = 1750.0f; break;
    case kVoxEr: frame.energy = 0.86f; frame.f1 = 490.0f; frame.f2 = 1350.0f; frame.f3 = 1690.0f; break;
    case kVoxOr: frame.energy = 0.90f; frame.f1 = 500.0f; frame.f2 = 800.0f; frame.f3 = 1850.0f; break;
    case kVoxM: frame.energy = 0.62f; frame.noise = 0.02f; frame.f1 = 250.0f; frame.f2 = 1050.0f; frame.f3 = 2200.0f; break;
    case kVoxN: case kVoxNg: frame.energy = 0.58f; frame.noise = 0.03f; frame.f1 = 280.0f; frame.f2 = 1500.0f; frame.f3 = 2450.0f; break;
    case kVoxL: frame.energy = 0.68f; frame.noise = 0.04f; frame.f1 = 360.0f; frame.f2 = 1200.0f; frame.f3 = 2600.0f; break;
    case kVoxR: frame.energy = 0.66f; frame.noise = 0.05f; frame.f1 = 420.0f; frame.f2 = 1150.0f; frame.f3 = 1650.0f; break;
    case kVoxW: frame.energy = 0.64f; frame.noise = 0.04f; frame.f1 = 300.0f; frame.f2 = 760.0f; frame.f3 = 2200.0f; break;
    case kVoxS: frame.energy = 0.46f; frame.noise = 1.00f; frame.f1 = 3200.0f; frame.f2 = 5200.0f; frame.f3 = 7600.0f; break;
    case kVoxF: frame.energy = 0.40f; frame.noise = 0.86f; frame.f1 = 1200.0f; frame.f2 = 3300.0f; frame.f3 = 5200.0f; break;
    case kVoxSh: frame.energy = 0.50f; frame.noise = 0.96f; frame.f1 = 1800.0f; frame.f2 = 2850.0f; frame.f3 = 4600.0f; break;
    case kVoxTh: frame.energy = 0.36f; frame.noise = 0.76f; frame.f1 = 1100.0f; frame.f2 = 2600.0f; frame.f3 = 4200.0f; break;
    case kVoxP: frame.energy = 0.34f; frame.noise = 0.52f; frame.plosive = 0.90f; frame.f1 = 650.0f; frame.f2 = 1200.0f; frame.f3 = 2500.0f; break;
    case kVoxT: frame.energy = 0.36f; frame.noise = 0.64f; frame.plosive = 0.70f; frame.f1 = 900.0f; frame.f2 = 2800.0f; frame.f3 = 4200.0f; break;
    case kVoxK: case kVoxCh: frame.energy = 0.38f; frame.noise = 0.70f; frame.plosive = 0.78f; frame.f1 = 1200.0f; frame.f2 = 2400.0f; frame.f3 = 3600.0f; break;
    case kVoxH: frame.energy = 0.42f; frame.noise = 0.62f; frame.f1 = 600.0f; frame.f2 = 1400.0f; frame.f3 = 2800.0f; break;
    case kVoxRest: frame.energy = 0.0f; frame.noise = 0.0f; break;
    default: break;
    }
    return frame;
}

char voxLowerAscii(uint8_t ch)
{
    return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch + ('a' - 'A')) : static_cast<char>(ch);
}

VoxSpeechGlyph voxGlyphForChar(uint8_t raw)
{
    const char ch = voxLowerAscii(raw);
    VoxSpeechGlyph glyph {};
    switch (ch) {
    case 'a': glyph.u = 0.18f; glyph.v = 0.44f; glyph.vowel = true; glyph.hold = 1u; break;
    case 'e': glyph.u = 0.86f; glyph.v = 0.30f; glyph.vowel = true; glyph.hold = 1u; break;
    case 'i': glyph.u = 0.78f; glyph.v = 0.12f; glyph.vowel = true; glyph.hold = 1u; break;
    case 'o': glyph.u = 0.30f; glyph.v = 0.72f; glyph.vowel = true; glyph.hold = 1u; break;
    case 'u': glyph.u = 0.56f; glyph.v = 0.88f; glyph.vowel = true; glyph.hold = 1u; break;
    case 'y': glyph.u = 0.78f; glyph.v = 0.56f; glyph.vowel = true; glyph.hold = 1u; break;
    case 'p': case 'b':
        glyph.consonant = 0.68f; glyph.plosive = 0.62f; glyph.u = 0.20f; glyph.v = 0.35f; break;
    case 't': case 'd':
        glyph.consonant = 0.62f; glyph.plosive = 0.36f; glyph.sibilance = 0.25f; glyph.u = 0.70f; glyph.v = 0.25f; break;
    case 'k': case 'g': case 'q':
        glyph.consonant = 0.66f; glyph.plosive = 0.44f; glyph.u = 0.38f; glyph.v = 0.72f; break;
    case 's': case 'z': case 'x':
        glyph.consonant = 0.92f; glyph.sibilance = 1.0f; glyph.breath = 0.24f; glyph.u = 0.88f; glyph.v = 0.18f; break;
    case 'f': case 'v':
        glyph.consonant = 0.70f; glyph.sibilance = 0.72f; glyph.breath = 0.30f; glyph.u = 0.68f; glyph.v = 0.38f; break;
    case 'h':
        glyph.consonant = 0.42f; glyph.sibilance = 0.50f; glyph.breath = 0.48f; glyph.u = 0.48f; glyph.v = 0.80f; break;
    case 'r':
        glyph.consonant = 0.48f; glyph.plosive = 0.18f; glyph.u = 0.42f; glyph.v = 0.68f; break;
    case 'l': case 'm': case 'n': case 'w':
        glyph.consonant = 0.36f; glyph.u = 0.52f; glyph.v = 0.58f; break;
    case ' ':
    case '\t':
    case '\n':
    case '.':
    case ',':
    case ';':
    case ':':
    case '!':
    case '?':
    case '-':
        glyph.rest = true;
        glyph.consonant = 0.0f;
        break;
    default:
        glyph.consonant = 0.28f;
        break;
    }
    return glyph;
}

VoxSpeechGlyph voxGlyphForSymbol(uint8_t symbol)
{
    switch (symbol) {
    case kVoxRest: {
        VoxSpeechGlyph glyph {};
        glyph.rest = true;
        glyph.consonant = 0.0f;
        glyph.hold = 1u;
        return glyph;
    }
    case kVoxSh: {
        VoxSpeechGlyph glyph {};
        glyph.consonant = 0.95f; glyph.sibilance = 0.92f; glyph.breath = 0.42f;
        glyph.u = 0.82f; glyph.v = 0.24f; return glyph;
    }
    case kVoxCh: {
        VoxSpeechGlyph glyph {};
        glyph.consonant = 0.78f; glyph.plosive = 0.24f; glyph.sibilance = 0.62f;
        glyph.u = 0.76f; glyph.v = 0.28f; return glyph;
    }
    case kVoxTh: {
        VoxSpeechGlyph glyph {};
        glyph.consonant = 0.70f; glyph.sibilance = 0.55f; glyph.breath = 0.36f;
        glyph.u = 0.70f; glyph.v = 0.34f; return glyph;
    }
    case kVoxNg: {
        VoxSpeechGlyph glyph {};
        glyph.consonant = 0.32f; glyph.u = 0.38f; glyph.v = 0.76f; return glyph;
    }
    case kVoxOo: {
        VoxSpeechGlyph glyph {};
        glyph.u = 0.54f; glyph.v = 0.92f; glyph.vowel = true; glyph.hold = 3u; return glyph;
    }
    case kVoxEe: {
        VoxSpeechGlyph glyph {};
        glyph.u = 0.90f; glyph.v = 0.14f; glyph.vowel = true; glyph.hold = 3u; return glyph;
    }
    case kVoxAi: {
        VoxSpeechGlyph glyph {};
        glyph.u = 0.78f; glyph.v = 0.22f; glyph.vowel = true; glyph.hold = 2u; return glyph;
    }
    case kVoxOw: {
        VoxSpeechGlyph glyph {};
        glyph.u = 0.42f; glyph.v = 0.86f; glyph.vowel = true; glyph.hold = 2u; return glyph;
    }
    case kVoxOi: {
        VoxSpeechGlyph glyph {};
        glyph.u = 0.66f; glyph.v = 0.52f; glyph.vowel = true; glyph.hold = 2u; return glyph;
    }
    case kVoxEr: {
        VoxSpeechGlyph glyph {};
        glyph.u = 0.46f; glyph.v = 0.62f; glyph.vowel = true; glyph.hold = 2u; return glyph;
    }
    case kVoxAr: {
        VoxSpeechGlyph glyph {};
        glyph.u = 0.20f; glyph.v = 0.62f; glyph.vowel = true; glyph.hold = 2u; return glyph;
    }
    case kVoxOr: {
        VoxSpeechGlyph glyph {};
        glyph.u = 0.34f; glyph.v = 0.78f; glyph.vowel = true; glyph.hold = 2u; return glyph;
    }
    default:
        return voxGlyphForChar(symbol);
    }
}

VoxSpeechGlyph voxGlyphForPhrase(const Plugin& plugin, uint32_t index)
{
    const uint32_t length = std::min<uint32_t>(
        plugin.voxPhraseLength.load(std::memory_order_acquire), kVoxPhraseMaxChars - 1u);
    const uint8_t raw0 = index < length ? plugin.voxPhrase[index].load(std::memory_order_relaxed) : 0u;
    const uint8_t raw1 = index + 1u < length ? plugin.voxPhrase[index + 1u].load(std::memory_order_relaxed) : 0u;
    const char a = voxLowerAscii(raw0);
    const char b = voxLowerAscii(raw1);
    VoxSpeechGlyph glyph {};

    if (a == 's' && b == 'h') {
        glyph.consonant = 0.95f; glyph.sibilance = 0.92f; glyph.breath = 0.42f;
        glyph.u = 0.82f; glyph.v = 0.24f; glyph.advance = 2u; glyph.hold = 1u; return glyph;
    }
    if (a == 'c' && b == 'h') {
        glyph.consonant = 0.78f; glyph.plosive = 0.24f; glyph.sibilance = 0.62f;
        glyph.u = 0.76f; glyph.v = 0.28f; glyph.advance = 2u; return glyph;
    }
    if (a == 't' && b == 'h') {
        glyph.consonant = 0.70f; glyph.sibilance = 0.55f; glyph.breath = 0.36f;
        glyph.u = 0.70f; glyph.v = 0.34f; glyph.advance = 2u; return glyph;
    }
    if ((a == 'p' && b == 'h') || (a == 'g' && b == 'h')) {
        glyph.consonant = 0.66f; glyph.sibilance = 0.68f; glyph.breath = 0.34f;
        glyph.u = 0.66f; glyph.v = 0.40f; glyph.advance = 2u; return glyph;
    }
    if (a == 'n' && b == 'g') {
        glyph.consonant = 0.32f; glyph.u = 0.38f; glyph.v = 0.76f;
        glyph.advance = 2u; glyph.hold = 1u; return glyph;
    }
    if (a == 'o' && b == 'o') {
        glyph.u = 0.54f; glyph.v = 0.92f; glyph.vowel = true; glyph.advance = 2u; glyph.hold = 3u; return glyph;
    }
    if (a == 'e' && b == 'e') {
        glyph.u = 0.90f; glyph.v = 0.14f; glyph.vowel = true; glyph.advance = 2u; glyph.hold = 3u; return glyph;
    }
    if (a == 'a' && (b == 'i' || b == 'y')) {
        glyph.u = 0.78f; glyph.v = 0.22f; glyph.vowel = true; glyph.advance = 2u; glyph.hold = 2u; return glyph;
    }
    if (a == 'o' && (b == 'w' || b == 'u')) {
        glyph.u = 0.42f; glyph.v = 0.86f; glyph.vowel = true; glyph.advance = 2u; glyph.hold = 2u; return glyph;
    }
    if (a == 'o' && b == 'i') {
        glyph.u = 0.66f; glyph.v = 0.52f; glyph.vowel = true; glyph.advance = 2u; glyph.hold = 2u; return glyph;
    }
    if (a == 'e' && b == 'r') {
        glyph.u = 0.46f; glyph.v = 0.62f; glyph.vowel = true; glyph.advance = 2u; glyph.hold = 2u; return glyph;
    }
    if (a == 'a' && b == 'r') {
        glyph.u = 0.20f; glyph.v = 0.62f; glyph.vowel = true; glyph.advance = 2u; glyph.hold = 2u; return glyph;
    }
    if (a == 'o' && b == 'r') {
        glyph.u = 0.34f; glyph.v = 0.78f; glyph.vowel = true; glyph.advance = 2u; glyph.hold = 2u; return glyph;
    }
    return voxGlyphForChar(raw0);
}

float voxUnitNoise(uint32_t& state)
{
    state = state * 1664525u + 1013904223u;
    const uint32_t bits = (state >> 9u) | 0x3f800000u;
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value * 2.0f - 3.0f;
}

float voxSoftSat(float value)
{
    return value / (1.0f + std::fabs(value));
}

float voxBandpass(float input, float frequency, float resonance, double sampleRate, float& low, float& band)
{
    const float safeFrequency = std::clamp(frequency, 80.0f, static_cast<float>(sampleRate) * 0.42f);
    const float f = 2.0f * std::sin(s3g::kPi * safeFrequency / static_cast<float>(sampleRate));
    const float damp = std::clamp(resonance, 0.35f, 1.70f);
    low += f * band;
    const float high = input - low - damp * band;
    band += f * high;
    low = s3g::flushDenormal(std::clamp(low, -8.0f, 8.0f));
    band = s3g::flushDenormal(std::clamp(band, -8.0f, 8.0f));
    return band;
}

VoxLpcFrame voxLpcFrameForState(const Plugin& plugin)
{
    VoxLpcFrame frame {};
    const float u = std::clamp(plugin.voxPhraseVowelU, 0.0f, 1.0f);
    const float v = std::clamp(plugin.voxPhraseVowelV, 0.0f, 1.0f);
    const float vowel = std::clamp(plugin.voxPhraseVowelEnv, 0.0f, 1.0f);
    const float consonant = std::clamp(plugin.voxConsonantLevel, 0.0f, 1.0f);
    const float breath = std::clamp(plugin.voxPhraseBreathEnv + plugin.vox.breath * 0.32f, 0.0f, 1.0f);
    frame.energy = std::clamp(plugin.voxFrameEnergy + vowel * 0.18f - consonant * 0.10f + plugin.vox.drive * 0.10f, 0.0f, 1.0f);
    frame.pitchMul = std::clamp(plugin.voxFramePitchMul + (u - 0.5f) * plugin.vox.pitchScoop * 0.08f - consonant * 0.025f, 0.72f, 1.32f);
    frame.noise = std::clamp(plugin.voxFrameNoise + breath * 0.35f + consonant * 0.28f, 0.0f, 1.0f);
    frame.plosive = std::clamp(plugin.voxFramePlosive + plugin.voxPlosiveLevel * plugin.vox.plosive, 0.0f, 1.0f);
    frame.f1 = std::clamp(plugin.voxFrameF1 + (v - 0.5f) * 90.0f, 120.0f, 1800.0f);
    frame.f2 = std::clamp(plugin.voxFrameF2 + (u - 0.5f) * 220.0f, 420.0f, 4200.0f);
    frame.f3 = std::clamp(plugin.voxFrameF3 + plugin.vox.sibilance * 420.0f, 900.0f, 6200.0f);
    return frame;
}

VoxLpcFrame voxLpcFrameForVoice(const Plugin& plugin, uint32_t voice, uint32_t voices)
{
    const uint32_t length = std::min<uint32_t>(
        plugin.voxCompiledCount.load(std::memory_order_acquire), kVoxCompiledMaxFrames);
    const uint32_t offset = s3g::ambiVoxPhraseSpreadIndex(
        plugin.vox.orchestration, voice, voices, length, plugin.vox.phraseSpread);
    if (length < 2u || offset == 0u) return voxLpcFrameForState(plugin);

    const uint32_t index = (plugin.voxPhraseCurrentIndex + offset) % length;
    const uint8_t symbol = plugin.voxCompiledSymbol[index].load(std::memory_order_relaxed);
    const VoxSpeechGlyph glyph = voxGlyphForSymbol(symbol);
    VoxLpcFrame frame {};
    frame.energy = plugin.voxCompiledEnergy[index].load(std::memory_order_relaxed);
    frame.pitchMul = plugin.voxCompiledPitchMul[index].load(std::memory_order_relaxed);
    frame.noise = plugin.voxCompiledNoise[index].load(std::memory_order_relaxed);
    frame.plosive = plugin.voxCompiledPlosive[index].load(std::memory_order_relaxed);
    frame.f1 = plugin.voxCompiledF1[index].load(std::memory_order_relaxed);
    frame.f2 = plugin.voxCompiledF2[index].load(std::memory_order_relaxed);
    frame.f3 = plugin.voxCompiledF3[index].load(std::memory_order_relaxed);
    if (glyph.rest) {
        frame.energy = 0.0f;
        frame.noise = 0.0f;
        frame.plosive = 0.0f;
        return frame;
    }

    const float consonant = std::clamp(
        glyph.consonant * plugin.vox.articulation + plugin.vox.consonant * 0.18f,
        0.0f, 1.0f);
    const float breath = std::clamp(glyph.breath + plugin.vox.breath * 0.32f, 0.0f, 1.0f);
    frame.energy = std::clamp(frame.energy
        + (glyph.vowel ? 0.14f : 0.0f) - consonant * 0.08f
        + plugin.vox.drive * 0.10f, 0.0f, 1.0f);
    frame.pitchMul = std::clamp(frame.pitchMul
        + (glyph.u - 0.5f) * plugin.vox.pitchScoop * 0.08f
        - consonant * 0.025f, 0.72f, 1.32f);
    frame.noise = std::clamp(frame.noise + breath * 0.35f + consonant * 0.28f, 0.0f, 1.0f);
    frame.plosive = std::clamp(frame.plosive + glyph.plosive * plugin.vox.plosive, 0.0f, 1.0f);
    frame.f1 = std::clamp(frame.f1 + (glyph.v - 0.5f) * 90.0f, 120.0f, 1800.0f);
    frame.f2 = std::clamp(frame.f2 + (glyph.u - 0.5f) * 220.0f, 420.0f, 4200.0f);
    frame.f3 = std::clamp(frame.f3 + plugin.vox.sibilance * 420.0f, 900.0f, 6200.0f);
    return frame;
}

s3g::AmbiVotParams voxRenderParams(const s3g::AmbiVotParams& source, const VoxParams& vox)
{
    s3g::AmbiVotParams params = source;
    params.detune = std::clamp(source.detune + vox.jitter * 0.34f + vox.rasp * 0.04f + vox.consonant * 0.05f, 0.0f, 1.0f);
    params.pitchSpread = std::clamp(source.pitchSpread + vox.pitchScoop * 0.38f + vox.articulation * 0.03f, 0.0f, 2.0f);
    params.morph = std::clamp(source.morph + (vox.vowelSpread - 0.5f) * 0.30f + vox.rasp * 0.08f + vox.articulation * 0.06f, 0.0f, 1.0f);
    params.vectorX = std::clamp(source.vectorX + (vox.throat - 0.5f) * 0.22f, 0.0f, 1.0f);
    params.vectorY = std::clamp(source.vectorY + (vox.rasp - 0.5f) * 0.18f + vox.breath * 0.08f, 0.0f, 1.0f);
    params.scan = std::clamp(source.scan + vox.vowelSpread * 0.14f + vox.phraseRate * 0.04f, 0.0f, 1.0f);
    params.harmonicAmount = std::clamp(source.harmonicAmount + vox.drive * 0.18f, 0.0f, 1.0f);
    params.subharmonicAmount = std::clamp(source.subharmonicAmount + vox.throat * 0.16f, 0.0f, 1.0f);
    params.attackMs = std::clamp(source.attackMs * (0.35f + vox.attackShape * 1.45f), 1.0f, 2000.0f);
    return params;
}

void triggerVoxConsonant(Plugin& plugin, float strength)
{
    const float amount = std::clamp(strength, 0.0f, 1.0f) * std::clamp(plugin.vox.articulation, 0.0f, 1.0f);
    plugin.voxConsonantEnv = std::max(plugin.voxConsonantEnv, amount);
    plugin.voxPlosiveEnv = std::max(plugin.voxPlosiveEnv, amount * std::clamp(plugin.vox.plosive, 0.0f, 1.0f));
}

void appendCompiledPhoneme(std::array<uint8_t, kVoxCompiledMaxFrames>& symbols,
                           std::array<uint8_t, kVoxCompiledMaxFrames>& durations,
                           std::array<VoxLpcFrame, kVoxCompiledMaxFrames>& frames,
                           uint32_t& count,
                           uint8_t symbol,
                           uint8_t duration)
{
    if (count >= kVoxCompiledMaxFrames) return;
    symbols[count] = symbol;
    durations[count] = std::max<uint8_t>(1u, duration);
    const VoxSpeechGlyph glyph = voxGlyphForSymbol(symbol);
    frames[count] = voxAllophoneFrame(symbol);
    frames[count].noise = std::clamp(frames[count].noise + glyph.breath * 0.16f, 0.0f, 1.0f);
    ++count;
}

bool appendDictionaryWord(const std::string& word,
                          std::array<uint8_t, kVoxCompiledMaxFrames>& symbols,
                          std::array<uint8_t, kVoxCompiledMaxFrames>& durations,
                          std::array<VoxLpcFrame, kVoxCompiledMaxFrames>& frames,
                          uint32_t& count)
{
    struct Entry { const char* word; std::initializer_list<uint8_t> phonemes; };
    static constexpr Entry entries[] {
        { "black", { kVoxP, kVoxL, kVoxA, kVoxK } },
        { "metal", { kVoxM, kVoxE, kVoxT, kVoxA, kVoxL } },
        { "voice", { kVoxF, kVoxOi, kVoxS } },
        { "vox", { kVoxF, kVoxO, kVoxK, kVoxS } },
        { "sorrow", { kVoxS, kVoxOr, kVoxOw } },
        { "ash", { kVoxA, kVoxSh } },
        { "roar", { kVoxR, kVoxOr } },
        { "speak", { kVoxS, kVoxP, kVoxEe, kVoxK } },
        { "spell", { kVoxS, kVoxP, kVoxE, kVoxL } },
        { "hello", { kVoxH, kVoxE, kVoxL, kVoxOw } },
        { "world", { kVoxW, kVoxEr, kVoxL, kVoxT } },
        { "dark", { kVoxT, kVoxAr, kVoxK } },
        { "fire", { kVoxF, kVoxAi, kVoxEr } },
        { "throat", { kVoxTh, kVoxR, kVoxOw, kVoxT } },
        { "blood", { kVoxP, kVoxL, kVoxU, kVoxT } },
        { "night", { kVoxN, kVoxAi, kVoxT } },
        { "ra", { kVoxR, kVoxA } },
        { "ka", { kVoxK, kVoxA } },
        { "ta", { kVoxT, kVoxA } },
        { "sa", { kVoxS, kVoxA } },
        { "sha", { kVoxSh, kVoxA } },
    };
    for (const auto& entry : entries) {
        if (word != entry.word) continue;
        for (const uint8_t symbol : entry.phonemes) {
            const VoxSpeechGlyph glyph = voxGlyphForSymbol(symbol);
            appendCompiledPhoneme(symbols, durations, frames, count, symbol,
                glyph.vowel ? static_cast<uint8_t>(3u + glyph.hold) : static_cast<uint8_t>(1u + glyph.hold));
        }
        return true;
    }
    return false;
}

void appendRuleWord(const std::string& word,
                    std::array<uint8_t, kVoxCompiledMaxFrames>& symbols,
                    std::array<uint8_t, kVoxCompiledMaxFrames>& durations,
                    std::array<VoxLpcFrame, kVoxCompiledMaxFrames>& frames,
                    uint32_t& count)
{
    for (size_t i = 0; i < word.size();) {
        const char a = word[i];
        const char b = i + 1u < word.size() ? word[i + 1u] : 0;
        uint8_t symbol = static_cast<uint8_t>(a);
        size_t advance = 1u;
        if (a == 's' && b == 'h') { symbol = kVoxSh; advance = 2u; }
        else if (a == 'c' && b == 'h') { symbol = kVoxCh; advance = 2u; }
        else if (a == 't' && b == 'h') { symbol = kVoxTh; advance = 2u; }
        else if (a == 'n' && b == 'g') { symbol = kVoxNg; advance = 2u; }
        else if (a == 'o' && b == 'o') { symbol = kVoxOo; advance = 2u; }
        else if (a == 'e' && b == 'e') { symbol = kVoxEe; advance = 2u; }
        else if (a == 'a' && (b == 'i' || b == 'y')) { symbol = kVoxAi; advance = 2u; }
        else if (a == 'o' && (b == 'w' || b == 'u')) { symbol = kVoxOw; advance = 2u; }
        else if (a == 'o' && b == 'i') { symbol = kVoxOi; advance = 2u; }
        else if (a == 'e' && b == 'r') { symbol = kVoxEr; advance = 2u; }
        else if (a == 'a' && b == 'r') { symbol = kVoxAr; advance = 2u; }
        else if (a == 'o' && b == 'r') { symbol = kVoxOr; advance = 2u; }
        else if (a == 'c') symbol = (b == 'e' || b == 'i' || b == 'y') ? kVoxS : kVoxK;
        else if (a == 'q') symbol = kVoxK;
        else if (a == 'j') symbol = kVoxCh;
        else if (a == 'v') symbol = kVoxF;
        else if (a == 'd') symbol = kVoxT;
        else if (a == 'g') symbol = kVoxK;
        else if (a == 'b') symbol = kVoxP;
        else if (a == 'z' || a == 'x') symbol = kVoxS;
        const VoxSpeechGlyph glyph = voxGlyphForSymbol(symbol);
        if (!glyph.rest) {
            appendCompiledPhoneme(symbols, durations, frames, count, symbol,
                glyph.vowel ? static_cast<uint8_t>(3u + glyph.hold) : static_cast<uint8_t>(1u + glyph.hold));
        }
        i += advance;
    }
}

std::vector<VoxLyricSynthEvent> compileVoxLyricSynthEvents(const std::string& text)
{
    std::array<uint8_t, kVoxCompiledMaxFrames> symbols {};
    std::array<uint8_t, kVoxCompiledMaxFrames> durations {};
    std::array<VoxLpcFrame, kVoxCompiledMaxFrames> frames {};
    uint32_t count = 0u;
    std::string word;
    auto flushWord = [&]() {
        if (word.empty()) return;
        if (!appendDictionaryWord(word, symbols, durations, frames, count)) appendRuleWord(word, symbols, durations, frames, count);
        word.clear();
    };
    for (const char raw : text) {
        const unsigned char c = static_cast<unsigned char>(raw);
        if (std::isalpha(c)) {
            word.push_back(static_cast<char>(std::tolower(c)));
        } else {
            flushWord();
            if (std::isspace(c) || raw == '-' || raw == ',' || raw == ';' || raw == ':') {
                appendCompiledPhoneme(symbols, durations, frames, count, kVoxRest, 2u);
            } else if (raw == '.' || raw == '!' || raw == '?') {
                appendCompiledPhoneme(symbols, durations, frames, count, kVoxRest, 5u);
            }
        }
    }
    flushWord();
    if (count == 0u) appendCompiledPhoneme(symbols, durations, frames, count, kVoxRest, 4u);

    std::vector<VoxLyricSynthEvent> result;
    result.reserve(count);
    for (uint32_t i = 0u; i < count; ++i) {
        result.push_back({ symbols[i], durations[i], frames[i].energy, frames[i].pitchMul,
            frames[i].noise, frames[i].plosive, frames[i].f1, frames[i].f2, frames[i].f3 });
    }
    return result;
}

void publishVoxLyricSynthEvents(Plugin& plugin,
                                const std::vector<VoxLyricSynthEvent>& events)
{
    const uint32_t count = std::min<uint32_t>(
        static_cast<uint32_t>(events.size()), kVoxCompiledMaxFrames);
    for (uint32_t i = 0; i < kVoxCompiledMaxFrames; ++i) {
        const VoxLyricSynthEvent fallback {};
        const auto& event = i < count ? events[i] : fallback;
        plugin.voxCompiledSymbol[i].store(i < count ? event.symbol : kVoxRest, std::memory_order_relaxed);
        plugin.voxCompiledDuration[i].store(event.duration, std::memory_order_relaxed);
        plugin.voxCompiledEnergy[i].store(event.energy, std::memory_order_relaxed);
        plugin.voxCompiledPitchMul[i].store(event.pitchMul, std::memory_order_relaxed);
        plugin.voxCompiledNoise[i].store(event.noise, std::memory_order_relaxed);
        plugin.voxCompiledPlosive[i].store(event.plosive, std::memory_order_relaxed);
        plugin.voxCompiledF1[i].store(event.f1, std::memory_order_relaxed);
        plugin.voxCompiledF2[i].store(event.f2, std::memory_order_relaxed);
        plugin.voxCompiledF3[i].store(event.f3, std::memory_order_relaxed);
    }
    plugin.voxPhraseIndex = 0u;
    plugin.voxPhraseCurrentIndex = 0u;
    plugin.voxPhraseHold = 0u;
    plugin.voxCompiledCount.store(count, std::memory_order_release);
}

void compileVoxPhrase(Plugin& plugin)
{
    publishVoxLyricSynthEvents(plugin, compileVoxLyricSynthEvents(loadVoxPhrase(plugin)));
}

bool storeVoxEncodedBytes(Plugin& plugin, const std::vector<uint8_t>& bytes)
{
    if (bytes.empty()) return false;
    std::array<uint8_t, kVoxCompiledMaxFrames> symbols {};
    std::array<uint8_t, kVoxCompiledMaxFrames> durations {};
    std::array<VoxLpcFrame, kVoxCompiledMaxFrames> frames {};
    uint32_t count = 0u;
    for (size_t i = 0; i < bytes.size() && count < kVoxCompiledMaxFrames;) {
        const uint8_t b0 = bytes[i++];
        const uint8_t b1 = i < bytes.size() ? bytes[i++] : 0u;
        const uint8_t b2 = i < bytes.size() ? bytes[i++] : 0u;
        const uint8_t b3 = i < bytes.size() ? bytes[i++] : 0u;
        const uint8_t energyBits = b0 >> 4u;
        const uint8_t pitchBits = ((b0 & 0x0fu) << 2u) | (b1 >> 6u);
        const uint8_t kA = b1 & 0x3fu;
        const uint8_t kB = b2 >> 2u;
        const uint8_t kC = ((b2 & 0x03u) << 4u) | (b3 >> 4u);
        const uint8_t flags = b3 & 0x0fu;
        VoxLpcFrame frame {};
        const float energy = static_cast<float>(energyBits) / 15.0f;
        const float pitch = static_cast<float>(pitchBits) / 63.0f;
        const float a = static_cast<float>(kA) / 63.0f;
        const float b = static_cast<float>(kB) / 63.0f;
        const float c = static_cast<float>(kC) / 63.0f;
        const bool rest = energyBits == 0u;
        const bool unvoiced = pitchBits < 3u || (flags & 0x01u) != 0u;
        frame.energy = rest ? 0.0f : std::clamp(0.12f + energy * 0.98f, 0.0f, 1.0f);
        frame.pitchMul = std::clamp(0.32f + pitch * 2.25f, 0.30f, 2.75f);
        frame.noise = unvoiced ? std::clamp(0.30f + c * 0.95f, 0.0f, 1.0f) : c * 0.34f;
        frame.plosive = ((flags & 0x02u) != 0u) ? 0.88f : 0.0f;
        frame.f1 = 150.0f + a * 1450.0f;
        frame.f2 = 480.0f + b * 3300.0f - a * 320.0f;
        frame.f3 = 1200.0f + c * 5000.0f;
        uint8_t symbol = kVoxA;
        if (rest) symbol = kVoxRest;
        else if (unvoiced) symbol = (flags & 0x02u) != 0u ? kVoxT : kVoxS;
        else if (a < 0.28f && b > 0.58f) symbol = kVoxEe;
        else if (a > 0.66f && b < 0.42f) symbol = kVoxOo;
        else if (a > 0.54f) symbol = kVoxO;
        else if (b > 0.66f) symbol = kVoxI;
        symbols[count] = symbol;
        durations[count] = rest ? 3u : 2u;
        frames[count] = frame;
        ++count;
    }
    if (count == 0u) return false;
    for (uint32_t i = 0; i < kVoxCompiledMaxFrames; ++i) {
        plugin.voxCompiledSymbol[i].store(i < count ? symbols[i] : kVoxRest, std::memory_order_relaxed);
        plugin.voxCompiledDuration[i].store(i < count ? durations[i] : 1u, std::memory_order_relaxed);
        plugin.voxCompiledEnergy[i].store(i < count ? frames[i].energy : 0.0f, std::memory_order_relaxed);
        plugin.voxCompiledPitchMul[i].store(i < count ? frames[i].pitchMul : 1.0f, std::memory_order_relaxed);
        plugin.voxCompiledNoise[i].store(i < count ? frames[i].noise : 0.0f, std::memory_order_relaxed);
        plugin.voxCompiledPlosive[i].store(i < count ? frames[i].plosive : 0.0f, std::memory_order_relaxed);
        plugin.voxCompiledF1[i].store(i < count ? frames[i].f1 : 600.0f, std::memory_order_relaxed);
        plugin.voxCompiledF2[i].store(i < count ? frames[i].f2 : 1500.0f, std::memory_order_relaxed);
        plugin.voxCompiledF3[i].store(i < count ? frames[i].f3 : 2800.0f, std::memory_order_relaxed);
    }
    plugin.voxPhraseIndex = 0u;
    plugin.voxPhraseCurrentIndex = 0u;
    plugin.voxPhraseHold = 0u;
    std::atomic_store_explicit(&plugin.worldSample, std::shared_ptr<const VoxWorldSample> {}, std::memory_order_release);
    plugin.voxCompiledCount.store(count, std::memory_order_release);
    return true;
}

std::string voxNormalizeToken(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (const unsigned char ch : text) {
        if (std::isalnum(ch)) out.push_back(static_cast<char>(std::tolower(ch)));
    }
    return out;
}

int findVoicebankEntry(const VoxVoicebank& bank, const std::string& token)
{
    if (token.empty()) return -1;
    const std::string normalized = voxNormalizeToken(token);
    if (normalized.empty()) return -1;
    for (size_t i = 0; i < bank.entries.size(); ++i) {
        if (bank.entries[i].fileKey == normalized || bank.entries[i].searchKey == normalized) return static_cast<int>(i);
    }
    for (size_t i = 0; i < bank.entries.size(); ++i) {
        if (voxNormalizeToken(bank.entries[i].alias) == normalized) return static_cast<int>(i);
    }
    return -1;
}

int findVoicebankEntryWithVariants(const VoxVoicebank& bank, const std::string& token)
{
    if (const int direct = findVoicebankEntry(bank, token); direct >= 0) return direct;
    if (!token.empty() && token[0] == 'l') {
        std::string rolled = token;
        rolled[0] = 'r';
        if (const int mapped = findVoicebankEntry(bank, rolled); mapped >= 0) return mapped;
    }
    struct Variant { const char* from; const char* to; };
    static constexpr Variant variants[] {
        { "si", "shi" }, { "zi", "ji" }, { "ti", "chi" }, { "tu", "tsu" },
        { "hu", "fu" }, { "du", "zu" }, { "di", "ji" }, { "ly", "ry" },
        { "l", "r" }, { "wu", "u" }, { "ye", "e" }, { "wi", "i" },
    };
    for (const auto& variant : variants) {
        if (token == variant.from) {
            if (const int mapped = findVoicebankEntry(bank, variant.to); mapped >= 0) return mapped;
        }
        if (token == variant.to) {
            if (const int mapped = findVoicebankEntry(bank, variant.from); mapped >= 0) return mapped;
        }
    }
    return -1;
}

bool appendVoicebankSequence(const VoxVoicebank& bank,
                             const std::initializer_list<const char*> tokens,
                             std::array<uint16_t, kVoxCompiledMaxFrames>& indices,
                             std::array<uint8_t, kVoxCompiledMaxFrames>& durations,
                             uint32_t& count)
{
    std::array<uint16_t, 16> local {};
    uint32_t localCount = 0u;
    for (const char* token : tokens) {
        const int entryIndex = findVoicebankEntryWithVariants(bank, token ? token : "");
        if (entryIndex < 0 || localCount >= local.size()) return false;
        local[localCount++] = static_cast<uint16_t>(entryIndex);
    }
    for (uint32_t i = 0; i < localCount && count < kVoxCompiledMaxFrames; ++i) {
        indices[count] = local[i];
        durations[count] = 3u;
        ++count;
    }
    return true;
}

bool appendVoicebankSequence(const VoxVoicebank& bank,
                             const std::vector<std::string>& tokens,
                             std::array<uint16_t, kVoxCompiledMaxFrames>& indices,
                             std::array<uint8_t, kVoxCompiledMaxFrames>& durations,
                             uint32_t& count)
{
    if (tokens.empty()) return false;
    std::vector<uint16_t> local;
    local.reserve(tokens.size());
    for (const auto& token : tokens) {
        const int entryIndex = findVoicebankEntryWithVariants(bank, token);
        if (entryIndex < 0) return false;
        local.push_back(static_cast<uint16_t>(entryIndex));
    }
    for (const uint16_t entryIndex : local) {
        if (count >= kVoxCompiledMaxFrames) break;
        indices[count] = entryIndex;
        durations[count] = 3u;
        ++count;
    }
    return true;
}

bool appendVoicebankDictionaryWord(const VoxVoicebank& bank,
                                   const std::string& word,
                                   std::array<uint16_t, kVoxCompiledMaxFrames>& indices,
                                   std::array<uint8_t, kVoxCompiledMaxFrames>& durations,
                                   uint32_t& count)
{
    if (const auto override = bank.pronunciations.find(word);
        override != bank.pronunciations.end()) {
        return appendVoicebankSequence(bank, override->second, indices, durations, count);
    }
    if (word == "black") return appendVoicebankSequence(bank, { "bu", "ra", "ku" }, indices, durations, count);
    if (word == "metal") return appendVoicebankSequence(bank, { "me", "ta", "ru" }, indices, durations, count);
    if (word == "vocal" || word == "vocals" || word == "voice" || word == "voices") return appendVoicebankSequence(bank, { "bo", "ka", "ru" }, indices, durations, count);
    if (word == "hello") return appendVoicebankSequence(bank, { "he", "ro" }, indices, durations, count);
    if (word == "world") return appendVoicebankSequence(bank, { "wa", "ru", "do" }, indices, durations, count);
    if (word == "sorrow") return appendVoicebankSequence(bank, { "so", "ro" }, indices, durations, count);
    if (word == "ash") return appendVoicebankSequence(bank, { "a", "shi" }, indices, durations, count);
    if (word == "fire") return appendVoicebankSequence(bank, { "fa", "i", "ya" }, indices, durations, count);
    if (word == "dark") return appendVoicebankSequence(bank, { "da", "ku" }, indices, durations, count);
    if (word == "night") return appendVoicebankSequence(bank, { "na", "i", "to" }, indices, durations, count);
    if (word == "blood") return appendVoicebankSequence(bank, { "bu", "ra", "do" }, indices, durations, count);
    if (word == "moon") return appendVoicebankSequence(bank, { "mu", "n" }, indices, durations, count);
    if (word == "scream") return appendVoicebankSequence(bank, { "su", "ku", "ri", "mu" }, indices, durations, count);
    if (word == "death") return appendVoicebankSequence(bank, { "de", "su" }, indices, durations, count);
    if (word == "love") return appendVoicebankSequence(bank, { "ra", "bu" }, indices, durations, count);
    return false;
}

std::vector<std::string> voxEnglishRomaji(const std::string& word)
{
    std::vector<std::string> result;
    const auto vowelFor = [](const std::string& text, size_t& advance) -> std::string {
        if (text.rfind("ee", 0u) == 0u || text.rfind("ea", 0u) == 0u
            || text.rfind("ie", 0u) == 0u) { advance = 2u; return "i"; }
        if (text.rfind("oo", 0u) == 0u || text.rfind("ou", 0u) == 0u) {
            advance = 2u; return "u";
        }
        if (text.rfind("ai", 0u) == 0u || text.rfind("ay", 0u) == 0u
            || text.rfind("ei", 0u) == 0u) { advance = 2u; return "e"; }
        if (text.rfind("oi", 0u) == 0u || text.rfind("oy", 0u) == 0u) {
            advance = 2u; return "o";
        }
        if (text.empty()) return {};
        advance = 1u;
        switch (text[0]) {
        case 'a': return "a";
        case 'e': return "e";
        case 'i': case 'y': return "i";
        case 'o': return "o";
        case 'u': return "u";
        default: return {};
        }
    };
    const auto onsetFor = [](const std::string& text, size_t& advance) -> std::string {
        struct Digraph { const char* spelling; const char* onset; };
        static constexpr Digraph digraphs[] {
            { "sh", "sh" }, { "ch", "ch" }, { "th", "s" }, { "ph", "f" },
            { "wh", "w" }, { "qu", "k" }, { "ng", "n" }, { "zh", "j" },
        };
        for (const auto& digraph : digraphs) {
            if (text.rfind(digraph.spelling, 0u) == 0u) {
                advance = 2u;
                return digraph.onset;
            }
        }
        if (text.empty()) return {};
        advance = 1u;
        switch (text[0]) {
        case 'b': case 'd': case 'f': case 'g': case 'h': case 'j': case 'k':
        case 'm': case 'n': case 'p': case 'r': case 's': case 't': case 'w': case 'z':
            return std::string(1u, text[0]);
        case 'c': case 'q': return "k";
        case 'l': return "r";
        case 'v': return "b";
        case 'x': return "s";
        default: return {};
        }
    };

    size_t position = 0u;
    while (position < word.size()) {
        size_t vowelAdvance = 0u;
        const std::string directVowel = vowelFor(word.substr(position), vowelAdvance);
        if (!directVowel.empty()) {
            result.push_back(directVowel);
            position += vowelAdvance;
            continue;
        }
        size_t onsetAdvance = 0u;
        std::string onset = onsetFor(word.substr(position), onsetAdvance);
        if (onset.empty()) {
            ++position;
            continue;
        }
        position += onsetAdvance;
        size_t nextVowelAdvance = 0u;
        const std::string vowel = vowelFor(word.substr(position), nextVowelAdvance);
        if (!vowel.empty()) {
            result.push_back(onset + vowel);
            position += nextVowelAdvance;
        } else if (onset == "n" && position >= word.size()) {
            result.push_back("n");
        } else {
            result.push_back(onset + "u");
        }
    }
    return result;
}

void appendVoicebankToken(const VoxVoicebank& bank,
                          const std::string& token,
                          std::array<uint16_t, kVoxCompiledMaxFrames>& indices,
                          std::array<uint8_t, kVoxCompiledMaxFrames>& durations,
                          uint32_t& count)
{
    const int entryIndex = findVoicebankEntryWithVariants(bank, token);
    if (entryIndex < 0 || count >= kVoxCompiledMaxFrames) return;
    indices[count] = static_cast<uint16_t>(entryIndex);
    durations[count] = 3u;
    ++count;
}

void appendVoicebankWord(const VoxVoicebank& bank,
                         const std::string& rawWord,
                         std::array<uint16_t, kVoxCompiledMaxFrames>& indices,
                         std::array<uint8_t, kVoxCompiledMaxFrames>& durations,
                         uint32_t& count)
{
    const std::string word = voxNormalizeToken(rawWord);
    if (word.empty()) return;
    if (appendVoicebankDictionaryWord(bank, word, indices, durations, count)) return;
    if (findVoicebankEntryWithVariants(bank, word) >= 0) {
        appendVoicebankToken(bank, word, indices, durations, count);
        return;
    }
    if (appendVoicebankSequence(bank, voxEnglishRomaji(word), indices, durations, count)) return;
    size_t i = 0;
    while (i < word.size() && count < kVoxCompiledMaxFrames) {
        if (i + 1u < word.size() && word[i] == word[i + 1u]) {
            ++i;
            continue;
        }
        int bestIndex = -1;
        size_t bestLength = 0;
        const size_t maxLength = std::min<size_t>(3u, word.size() - i);
        for (size_t length = maxLength; length >= 1u; --length) {
            const std::string candidate = word.substr(i, length);
            bestIndex = findVoicebankEntryWithVariants(bank, candidate);
            if (bestIndex >= 0) {
                bestLength = length;
                break;
            }
            if (length == 1u) break;
        }
        if (bestIndex < 0 && i + 2u < word.size() && word[i] == 's' && word[i + 1u] == 'h') {
            bestIndex = findVoicebankEntryWithVariants(bank, std::string("sh") + word[i + 2u]);
            bestLength = bestIndex >= 0 ? 3u : 0u;
        }
        if (bestIndex < 0 && i + 2u < word.size() && word[i] == 'c' && word[i + 1u] == 'h') {
            bestIndex = findVoicebankEntryWithVariants(bank, std::string("ch") + word[i + 2u]);
            bestLength = bestIndex >= 0 ? 3u : 0u;
        }
        if (bestIndex < 0) {
            ++i;
            continue;
        }
        indices[count] = static_cast<uint16_t>(bestIndex);
        durations[count] = bestLength >= 2u ? 3u : 2u;
        ++count;
        i += bestLength;
    }
}

double voxVoicebankPhraseDurationSamples(const Plugin& plugin,
                                         uint32_t duration,
                                         const VoxVoicebankEntry* entry = nullptr,
                                         uint8_t flags = 0u)
{
    const float rate = std::clamp(plugin.vox.phraseRate, 0.0f, 1.0f);
    const float stretch = std::clamp(plugin.vox.pvocStretch, 0.25f, 4.0f);
    if ((flags & kVoxBankEventRest) != 0u) {
        const double baseMs = duration >= 5u ? 340.0 : (duration >= 2u ? 145.0 : 58.0);
        const double rateScale = 1.25 - static_cast<double>(rate) * 0.55;
        return std::max(1.0, plugin.sampleRate * baseMs * rateScale * 0.001);
    }

    double result = 1.0;
    if (plugin.vox.speechMode == VoxSpeechMode::Sing) {
        const float hz = 0.70f + rate * 2.80f;
        result = static_cast<double>(std::max<uint32_t>(1u, duration))
            * plugin.sampleRate * static_cast<double>(stretch) / static_cast<double>(hz);
    } else if (entry && entry->audio && entry->audio->sampleRate > 0) {
        const double naturalSeconds = std::max(0.0, entry->endSample - entry->startSample)
            / static_cast<double>(entry->audio->sampleRate);
        const double pacedSeconds = std::clamp(naturalSeconds, 0.12, 0.48)
            * (1.36 - static_cast<double>(rate) * 0.72)
            * (static_cast<double>(std::max<uint32_t>(1u, duration)) / 3.0)
            * static_cast<double>(stretch);
        result = plugin.sampleRate * pacedSeconds;
    } else {
        const float hz = 3.0f + rate * 6.0f;
        result = static_cast<double>(std::max<uint32_t>(1u, duration))
            * plugin.sampleRate * static_cast<double>(stretch) / static_cast<double>(hz);
    }
    if (entry && entry->audio && entry->audio->sampleRate > 0
        && (flags & kVoxBankEventRest) == 0u) {
        const double fixedSource = std::max(entry->fixedSample, entry->preutterSample)
            - entry->startSample;
        const double fixedHost = std::max(0.0, fixedSource) * plugin.sampleRate
            / static_cast<double>(entry->audio->sampleRate);
        const double release = plugin.sampleRate
            * ((flags & kVoxBankEventVowel) != 0u ? 0.080 : 0.030);
        result = std::max(result, fixedHost + release);
    }
    return result;
}

const std::vector<float>& voxVoicebankSource(const VoxVoicebankEntry& entry, uint32_t anchor)
{
    static const std::vector<float> empty;
    if (!entry.audio) return empty;
    const auto& audio = *entry.audio;
    anchor = std::min<uint32_t>(anchor, kVoxWorldPitchAnchorCount - 1u);
    if (!audio.worldResynthesized || anchor == kVoxWorldPitchAnchorCenter) return audio.samples;
    const uint32_t variant = anchor < kVoxWorldPitchAnchorCenter ? anchor : anchor - 1u;
    return variant < audio.worldPitchVariants.size() && !audio.worldPitchVariants[variant].empty()
        ? audio.worldPitchVariants[variant]
        : audio.samples;
}

uint32_t voxVoicebankPitchAnchor(const Plugin& plugin, const VoxVoicebankEntry& entry, uint32_t voice)
{
    if (!entry.audio || !entry.audio->worldResynthesized) return kVoxWorldPitchAnchorCenter;
    const float targetSemitones = voxVoiceTargetNote(plugin, voice) - static_cast<float>(entry.audio->baseMidi)
        + std::clamp(plugin.vox.worldPitchCents, -2400.0f, 2400.0f) / 100.0f;
    uint32_t best = kVoxWorldPitchAnchorCenter;
    float bestDistance = std::numeric_limits<float>::max();
    for (uint32_t anchor = 0u; anchor < kVoxWorldPitchAnchorCount; ++anchor) {
        const auto& source = voxVoicebankSource(entry, anchor);
        if (source.empty()) continue;
        const float distance = std::fabs(targetSemitones
            - static_cast<float>(kVoxWorldPitchAnchorSemitones[anchor]));
        if (distance < bestDistance) {
            bestDistance = distance;
            best = anchor;
        }
    }
    return best;
}

void resetVoicebankPvoc(Plugin& plugin, const VoxVoicebankEntry& entry, uint32_t voice, double position)
{
    if (voice >= s3g::kAmbiVoxMaxVoices) return;
    const uint32_t anchor = voxVoicebankPitchAnchor(plugin, entry, voice);
    plugin.voxBankWorldPitchAnchor[voice] = static_cast<uint8_t>(anchor);
    const auto& source = voxVoicebankSource(entry, anchor);
    plugin.voxWorldPhase[voice] = position;
    plugin.pvoc.reset(plugin.voxPvocVoice[voice], source.empty() ? nullptr : source.data(), position);
}

void resetVoicebankTimelines(Plugin& plugin, const VoxVoicebank& bank);

bool voxVoicebankEntryHasVowel(const VoxVoicebankEntry& entry)
{
    const std::string token = !entry.alias.empty() ? voxNormalizeToken(entry.alias) : entry.searchKey;
    if (token.empty()) return false;
    const char ending = token.back();
    return ending == 'a' || ending == 'e' || ending == 'i' || ending == 'o' || ending == 'u';
}

std::vector<VoxLyricBankEvent> compileVoxLyricBankEvents(
    const std::string& text, const VoxVoicebank& bank)
{
    std::array<uint16_t, kVoxCompiledMaxFrames> indices {};
    std::array<uint8_t, kVoxCompiledMaxFrames> durations {};
    std::array<uint8_t, kVoxCompiledMaxFrames> flags {};
    uint32_t count = 0u;
    std::string word;
    auto flushWord = [&]() {
        if (!word.empty()) {
            const uint32_t before = count;
            appendVoicebankWord(bank, word, indices, durations, count);
            if (count > before) flags[count - 1u] |= kVoxBankEventWordEnd;
            word.clear();
        }
    };
    const auto appendRest = [&](uint8_t duration) {
        if (count == 0u) return;
        if ((flags[count - 1u] & kVoxBankEventRest) != 0u) {
            durations[count - 1u] = std::max(durations[count - 1u], duration);
            flags[count - 1u] |= kVoxBankEventWordEnd;
            return;
        }
        if (count >= kVoxCompiledMaxFrames) return;
        indices[count] = 0u;
        durations[count] = duration;
        flags[count] = kVoxBankEventRest | kVoxBankEventWordEnd;
        ++count;
    };
    for (const char raw : text) {
        const unsigned char c = static_cast<unsigned char>(raw);
        if (std::isalnum(c)) {
            word.push_back(static_cast<char>(std::tolower(c)));
        } else {
            flushWord();
            if (raw == '.' || raw == '!' || raw == '?') appendRest(5u);
            else if (raw == ',' || raw == ';' || raw == ':') appendRest(2u);
            else if (std::isspace(c)) appendRest(1u);
        }
    }
    flushWord();
    appendRest(2u);
    if (count == 0u) {
        const int fallback = findVoicebankEntry(bank, "a");
        if (fallback >= 0) {
            indices[count] = static_cast<uint16_t>(fallback);
            durations[count] = 3u;
            ++count;
        }
    }
    for (uint32_t i = 0u; i < count; ++i) {
        if ((flags[i] & kVoxBankEventRest) != 0u || bank.entries.empty()) continue;
        const uint32_t entryIndex = std::min<uint32_t>(indices[i], static_cast<uint32_t>(bank.entries.size() - 1u));
        if (voxVoicebankEntryHasVowel(bank.entries[entryIndex])) flags[i] |= kVoxBankEventVowel;
    }

    std::vector<VoxLyricBankEvent> result;
    result.reserve(count);
    for (uint32_t i = 0u; i < count; ++i) {
        result.push_back({ indices[i], durations[i], flags[i] });
    }
    return result;
}

void publishVoxLyricBankEvents(Plugin& plugin,
                               const std::vector<VoxLyricBankEvent>& events,
                               const VoxVoicebank* bank)
{
    const uint32_t count = std::min<uint32_t>(
        static_cast<uint32_t>(events.size()), kVoxCompiledMaxFrames);
    for (uint32_t i = 0; i < kVoxCompiledMaxFrames; ++i) {
        const VoxLyricBankEvent fallback {};
        const auto& event = i < count ? events[i] : fallback;
        plugin.voxBankCompiledIndex[i].store(event.index, std::memory_order_relaxed);
        plugin.voxBankCompiledDuration[i].store(event.duration, std::memory_order_relaxed);
        plugin.voxBankCompiledFlags[i].store(event.flags, std::memory_order_relaxed);
    }
    plugin.voxBankCompiledCount.store(count, std::memory_order_release);
    if (bank) resetVoicebankTimelines(plugin, *bank);
}

void compileVoicebankPhrase(Plugin& plugin, const VoxVoicebank& bank)
{
    publishVoxLyricBankEvents(
        plugin, compileVoxLyricBankEvents(loadVoxPhrase(plugin), bank), &bank);
}

std::shared_ptr<const VoxLyricScore> makeVoxLyricScore(Plugin& plugin)
{
    std::string lyrics = loadVoxLyrics(plugin);
    auto cueTexts = splitVoxLyricCues(lyrics);
    if (cueTexts.empty()) cueTexts.push_back(loadVoxPhrase(plugin));
    const auto bank = std::atomic_load_explicit(&plugin.voicebank, std::memory_order_acquire);
    auto score = std::make_shared<VoxLyricScore>();
    score->cues.reserve(cueTexts.size());
    for (auto& text : cueTexts) {
        VoxLyricCue cue {};
        cue.text = std::move(text);
        cue.synthEvents = compileVoxLyricSynthEvents(cue.text);
        if (bank) cue.bankEvents = compileVoxLyricBankEvents(cue.text, *bank);
        score->cues.push_back(std::move(cue));
    }
    return score;
}

void rebuildVoxLyricScore(Plugin& plugin)
{
    auto score = makeVoxLyricScore(plugin);
    const uint32_t count = static_cast<uint32_t>(score->cues.size());
    const uint32_t requested = count > 0u
        ? plugin.requestedLyricCue.load(std::memory_order_relaxed) % count
        : 0u;
    plugin.requestedLyricCue.store(requested, std::memory_order_relaxed);
    std::atomic_store_explicit(&plugin.lyricScore, std::move(score), std::memory_order_release);
    plugin.lyricAutoClockResetRequested.store(true, std::memory_order_release);
    plugin.lyricCueResetRequested.store(true, std::memory_order_release);
    if (plugin.host && plugin.host->request_process) plugin.host->request_process(plugin.host);
}

void requestVoxLyricCue(Plugin& plugin, uint32_t cue)
{
    const auto score = std::atomic_load_explicit(&plugin.lyricScore, std::memory_order_acquire);
    const uint32_t count = score ? static_cast<uint32_t>(score->cues.size()) : 0u;
    plugin.requestedLyricCue.store(count > 0u ? cue % count : 0u, std::memory_order_release);
    plugin.lyricAutoClockResetRequested.store(true, std::memory_order_release);
    plugin.lyricCueResetRequested.store(true, std::memory_order_release);
    if (plugin.host && plugin.host->request_process) plugin.host->request_process(plugin.host);
}

void publishVoxLyricCue(Plugin& plugin, const VoxLyricCue& cue, uint32_t cueIndex)
{
    const size_t phraseLength = std::min(
        cue.text.size(), static_cast<size_t>(kVoxPhraseMaxChars - 1u));
    for (uint32_t i = 0u; i < kVoxPhraseMaxChars; ++i) {
        plugin.voxPhrase[i].store(i < phraseLength
            ? static_cast<uint8_t>(cue.text[i]) : 0u, std::memory_order_relaxed);
    }
    plugin.voxPhraseLength.store(static_cast<uint32_t>(phraseLength), std::memory_order_release);
    publishVoxLyricSynthEvents(plugin, cue.synthEvents);
    const auto bank = std::atomic_load_explicit(&plugin.voicebank, std::memory_order_acquire);
    publishVoxLyricBankEvents(plugin, cue.bankEvents, bank.get());
    plugin.voxPhrasePhase = 0.0f;
    plugin.voxPhraseBreathEnv = 0.0f;
    plugin.voxPhraseVowelEnv = 0.0f;
    plugin.voxBankTimingResetRequested.store(true, std::memory_order_release);
    plugin.voxWorldTimingResetRequested.store(true, std::memory_order_release);
    plugin.lyricRuntimeCue = cueIndex;
    plugin.guiLyricCue.store(cueIndex, std::memory_order_release);
}

void applyPendingVoxLyricCue(Plugin& plugin)
{
    auto score = std::atomic_load_explicit(&plugin.lyricScore, std::memory_order_acquire);
    if (!score || score->cues.empty()) return;
    const bool scoreChanged = score.get() != plugin.activeLyricScore.get();
    const bool resetRequested = plugin.lyricCueResetRequested.exchange(
        false, std::memory_order_acq_rel);
    const uint32_t cue = plugin.requestedLyricCue.load(std::memory_order_acquire)
        % static_cast<uint32_t>(score->cues.size());
    if (!scoreChanged && !resetRequested && cue == plugin.lyricRuntimeCue) return;
    plugin.activeLyricScore = std::move(score);
    publishVoxLyricCue(plugin, plugin.activeLyricScore->cues[cue], cue);
}

std::string resolvedVoicebankPhrase(const Plugin& plugin, const VoxVoicebank& bank)
{
    std::string result;
    const uint32_t count = std::min<uint32_t>(
        plugin.voxBankCompiledCount.load(std::memory_order_acquire), kVoxCompiledMaxFrames);
    for (uint32_t i = 0u; i < count && result.size() < 44u; ++i) {
        const uint8_t flags = plugin.voxBankCompiledFlags[i].load(std::memory_order_relaxed);
        if (!result.empty()) result += ' ';
        if ((flags & kVoxBankEventRest) != 0u) {
            result += "|";
            continue;
        }
        if (bank.entries.empty()) continue;
        const uint32_t entryIndex = std::min<uint32_t>(
            plugin.voxBankCompiledIndex[i].load(std::memory_order_relaxed),
            static_cast<uint32_t>(bank.entries.size() - 1u));
        const auto& entry = bank.entries[entryIndex];
        result += !entry.fileKey.empty() ? entry.fileKey
            : (!entry.searchKey.empty() ? entry.searchKey : entry.alias);
        if ((flags & kVoxBankEventWordEnd) != 0u) result += "/";
    }
    if (count > 0u && result.size() >= 44u) result += "...";
    return result;
}

void resetVoicebankTimelines(Plugin& plugin, const VoxVoicebank& bank)
{
    const uint32_t count = std::min<uint32_t>(
        plugin.voxBankCompiledCount.load(std::memory_order_acquire), kVoxCompiledMaxFrames);
    plugin.voxBankPhraseIndex = 0u;
    plugin.voxBankPhraseHold = 0u;
    plugin.voxBankCurrentEntry = count > 0u
        ? plugin.voxBankCompiledIndex[0].load(std::memory_order_relaxed)
        : 0u;
    const uint32_t activeVoices = std::clamp<uint32_t>(
        plugin.params.voices, 1u, s3g::kAmbiVoxMaxVoices);
    for (uint32_t voice = 0; voice < s3g::kAmbiVoxMaxVoices; ++voice) {
        const uint32_t initialPhraseIndex = s3g::ambiVoxPhraseSpreadIndex(
            plugin.vox.orchestration, voice, activeVoices, count,
            plugin.vox.phraseSpread);
        const uint32_t entryIndex = count > 0u && !bank.entries.empty()
            ? std::min<uint32_t>(
                plugin.voxBankCompiledIndex[initialPhraseIndex].load(std::memory_order_relaxed),
                static_cast<uint32_t>(bank.entries.size() - 1u))
            : 0u;
        plugin.voxBankVoicePhraseIndex[voice] = count > 0u ? (initialPhraseIndex + 1u) % count : 0u;
        plugin.voxBankVoiceCurrentPhraseIndex[voice] = initialPhraseIndex;
        plugin.voxBankVoicePhraseHold[voice] = 0u;
        plugin.voxBankVoiceCurrentEntry[voice] = entryIndex;
        plugin.voxWorldVoiceGainSmooth[voice] = voice < activeVoices
            ? 1.0f
            : 0.0f;
        const uint32_t duration = count > 0u
            ? plugin.voxBankCompiledDuration[initialPhraseIndex].load(std::memory_order_relaxed)
            : 1u;
        const uint8_t flags = count > 0u
            ? plugin.voxBankCompiledFlags[initialPhraseIndex].load(std::memory_order_relaxed)
            : kVoxBankEventRest;
        const double eventSamples = voxVoicebankPhraseDurationSamples(plugin, duration,
            count > 0u && !bank.entries.empty() ? &bank.entries[entryIndex] : nullptr, flags);
        plugin.voxBankVoicePhraseSamplesRemaining[voice] = eventSamples;
        plugin.voxBankVoicePhraseSamplesTotal[voice] = eventSamples;
        plugin.voxBankVoiceCurrentFlags[voice] = flags;
        plugin.voxBankVoiceTransitionActive[voice] = false;
        plugin.pvoc.reset(plugin.voxPvocNextVoice[voice], nullptr, 0.0);
        if (count > 0u && !bank.entries.empty() && (flags & kVoxBankEventRest) == 0u) {
            resetVoicebankPvoc(plugin, bank.entries[entryIndex], voice, bank.entries[entryIndex].startSample);
        } else {
            plugin.voxBankWorldPitchAnchor[voice] = static_cast<uint8_t>(kVoxWorldPitchAnchorCenter);
            plugin.voxWorldPhase[voice] = 0.0;
            plugin.pvoc.reset(plugin.voxPvocVoice[voice], nullptr, 0.0);
        }
    }
    plugin.voxBankTimingResetRequested.store(false, std::memory_order_release);
}

void retriggerVoicebankVoice(Plugin& plugin, const VoxVoicebank& bank, uint32_t voice)
{
    const uint32_t count = std::min<uint32_t>(
        plugin.voxBankCompiledCount.load(std::memory_order_acquire), kVoxCompiledMaxFrames);
    if (voice >= s3g::kAmbiVoxMaxVoices || count == 0u || bank.entries.empty()) return;
    const uint32_t activeVoices = std::clamp<uint32_t>(
        plugin.params.voices, 1u, s3g::kAmbiVoxMaxVoices);
    const uint32_t phraseIndex = s3g::ambiVoxPhraseSpreadIndex(
        plugin.vox.orchestration, voice, activeVoices, count,
        plugin.vox.phraseSpread);
    const uint32_t entryIndex = std::min<uint32_t>(
        plugin.voxBankCompiledIndex[phraseIndex].load(std::memory_order_relaxed),
        static_cast<uint32_t>(bank.entries.size() - 1u));
    const uint32_t duration = plugin.voxBankCompiledDuration[phraseIndex].load(std::memory_order_relaxed);
    const uint8_t flags = plugin.voxBankCompiledFlags[phraseIndex].load(std::memory_order_relaxed);
    const auto& entry = bank.entries[entryIndex];
    plugin.voxBankVoicePhraseIndex[voice] = count > 0u ? (phraseIndex + 1u) % count : 0u;
    plugin.voxBankVoiceCurrentPhraseIndex[voice] = phraseIndex;
    plugin.voxBankVoiceCurrentEntry[voice] = entryIndex;
    const double eventSamples = voxVoicebankPhraseDurationSamples(plugin, duration, &entry, flags);
    plugin.voxBankVoicePhraseSamplesRemaining[voice] = eventSamples;
    plugin.voxBankVoicePhraseSamplesTotal[voice] = eventSamples;
    plugin.voxBankVoiceCurrentFlags[voice] = flags;
    plugin.voxBankVoiceTransitionActive[voice] = false;
    plugin.pvoc.reset(plugin.voxPvocNextVoice[voice], nullptr, 0.0);
    if ((flags & kVoxBankEventRest) == 0u) resetVoicebankPvoc(plugin, entry, voice, entry.startSample);
    else plugin.pvoc.reset(plugin.voxPvocVoice[voice], nullptr, 0.0);
}

void advanceVoicebankVoice(Plugin& plugin, const VoxVoicebank& bank, uint32_t voice)
{
    const uint32_t length = std::min<uint32_t>(
        plugin.voxBankCompiledCount.load(std::memory_order_acquire), kVoxCompiledMaxFrames);
    if (length == 0u || bank.entries.empty() || voice >= s3g::kAmbiVoxMaxVoices) return;
    const uint32_t phraseIndex = plugin.voxBankVoicePhraseIndex[voice] % length;
    const uint32_t entryIndex = std::min<uint32_t>(
        plugin.voxBankCompiledIndex[phraseIndex].load(std::memory_order_relaxed),
        static_cast<uint32_t>(bank.entries.size() - 1u));
    const uint32_t duration = plugin.voxBankCompiledDuration[phraseIndex].load(std::memory_order_relaxed);
    const uint8_t flags = plugin.voxBankCompiledFlags[phraseIndex].load(std::memory_order_relaxed);
    plugin.voxBankVoiceCurrentEntry[voice] = entryIndex;
    plugin.voxBankVoiceCurrentPhraseIndex[voice] = phraseIndex;
    plugin.voxBankVoicePhraseIndex[voice] = (phraseIndex + 1u) % length;
    const auto& entry = bank.entries[entryIndex];
    const double eventSamples = voxVoicebankPhraseDurationSamples(plugin, duration, &entry, flags);
    plugin.voxBankVoicePhraseSamplesRemaining[voice] = eventSamples;
    plugin.voxBankVoicePhraseSamplesTotal[voice] = eventSamples;
    plugin.voxBankVoiceCurrentFlags[voice] = flags;
    const double sourceSize = entry.audio ? static_cast<double>(entry.audio->samples.size()) : 0.0;
    const double position = std::clamp(entry.startSample, 0.0, sourceSize);
    const bool canCommitTransition = plugin.voxBankVoiceTransitionActive[voice]
        && plugin.voxBankVoiceNextEntry[voice] == entryIndex
        && plugin.voxBankVoiceNextFlags[voice] == flags
        && (flags & kVoxBankEventRest) == 0u;
    if (canCommitTransition) {
        plugin.voxPvocVoice[voice] = plugin.voxPvocNextVoice[voice];
        plugin.voxBankWorldPitchAnchor[voice] = plugin.voxBankVoiceNextPitchAnchor[voice];
        plugin.voxWorldPhase[voice] = plugin.voxPvocVoice[voice].analysisPosition;
    } else if ((flags & kVoxBankEventRest) == 0u) {
        resetVoicebankPvoc(plugin, entry, voice, position);
    } else {
        plugin.pvoc.reset(plugin.voxPvocVoice[voice], nullptr, 0.0);
    }
    plugin.voxBankVoiceTransitionActive[voice] = false;
    plugin.pvoc.reset(plugin.voxPvocNextVoice[voice], nullptr, 0.0);
}

double voxVoicebankSourceDurationAtHostRate(const Plugin& plugin,
                                            const VoxVoicebankEntry& entry,
                                            double sourceSamples)
{
    if (!entry.audio || entry.audio->sampleRate <= 0 || plugin.sampleRate <= 0.0) return 0.0;
    return std::max(0.0, sourceSamples) * plugin.sampleRate
        / static_cast<double>(entry.audio->sampleRate);
}

double voxVoicebankTransitionSamples(const Plugin& plugin, const VoxVoicebankEntry& next)
{
    const double preutter = voxVoicebankSourceDurationAtHostRate(
        plugin, next, next.preutterSample - next.startSample);
    const double overlap = voxVoicebankSourceDurationAtHostRate(
        plugin, next, next.overlapSample - next.startSample);
    const double natural = std::max(preutter, overlap);
    const double minimum = plugin.sampleRate * 0.012;
    return std::max(minimum, natural)
        * static_cast<double>(0.20f + std::clamp(plugin.vox.transition, 0.0f, 1.0f) * 0.80f);
}

void prepareVoicebankTransition(Plugin& plugin, const VoxVoicebank& bank, uint32_t voice)
{
    const uint32_t length = std::min<uint32_t>(
        plugin.voxBankCompiledCount.load(std::memory_order_acquire), kVoxCompiledMaxFrames);
    if (length == 0u || bank.entries.empty() || voice >= s3g::kAmbiVoxMaxVoices
        || plugin.voxBankVoiceTransitionActive[voice]) return;
    const uint32_t phraseIndex = plugin.voxBankVoicePhraseIndex[voice] % length;
    const uint8_t flags = plugin.voxBankCompiledFlags[phraseIndex].load(std::memory_order_relaxed);
    const uint32_t entryIndex = std::min<uint32_t>(
        plugin.voxBankCompiledIndex[phraseIndex].load(std::memory_order_relaxed),
        static_cast<uint32_t>(bank.entries.size() - 1u));
    plugin.voxBankVoiceNextEntry[voice] = entryIndex;
    plugin.voxBankVoiceNextFlags[voice] = flags;
    plugin.voxBankVoiceTransitionActive[voice] = true;
    if ((flags & kVoxBankEventRest) != 0u) {
        plugin.pvoc.reset(plugin.voxPvocNextVoice[voice], nullptr, 0.0);
        return;
    }
    const auto& entry = bank.entries[entryIndex];
    const uint32_t anchor = voxVoicebankPitchAnchor(plugin, entry, voice);
    plugin.voxBankVoiceNextPitchAnchor[voice] = static_cast<uint8_t>(anchor);
    const auto& source = voxVoicebankSource(entry, anchor);
    plugin.pvoc.reset(plugin.voxPvocNextVoice[voice],
        source.empty() ? nullptr : source.data(), entry.startSample);
}

double pendingVoicebankTransitionSamples(const Plugin& plugin,
                                         const VoxVoicebank& bank,
                                         uint32_t voice)
{
    const uint32_t length = std::min<uint32_t>(
        plugin.voxBankCompiledCount.load(std::memory_order_acquire), kVoxCompiledMaxFrames);
    if (length == 0u || bank.entries.empty() || voice >= s3g::kAmbiVoxMaxVoices) return 0.0;
    const uint32_t phraseIndex = plugin.voxBankVoicePhraseIndex[voice] % length;
    const uint8_t flags = plugin.voxBankCompiledFlags[phraseIndex].load(std::memory_order_relaxed);
    if ((flags & kVoxBankEventRest) != 0u) return plugin.sampleRate * 0.012;
    const uint32_t entryIndex = std::min<uint32_t>(
        plugin.voxBankCompiledIndex[phraseIndex].load(std::memory_order_relaxed),
        static_cast<uint32_t>(bank.entries.size() - 1u));
    return voxVoicebankTransitionSamples(plugin, bank.entries[entryIndex]);
}

void advanceVoxPhrase(Plugin& plugin, float strength)
{
    const uint32_t length = std::min<uint32_t>(
        plugin.voxCompiledCount.load(std::memory_order_acquire), kVoxCompiledMaxFrames);
    if (length == 0u) {
        triggerVoxConsonant(plugin, strength);
        return;
    }
    if (plugin.voxPhraseHold > 0u) {
        --plugin.voxPhraseHold;
        plugin.voxPhraseU = s3g::lerp(plugin.voxPhraseU, plugin.voxPhraseU, 0.20f);
        plugin.voxPhraseV = s3g::lerp(plugin.voxPhraseV, plugin.voxPhraseV, 0.20f);
        return;
    }
    const uint32_t index = plugin.voxPhraseIndex % length;
    const uint8_t symbol = plugin.voxCompiledSymbol[index].load(std::memory_order_relaxed);
    const uint32_t duration = std::max<uint32_t>(1u, plugin.voxCompiledDuration[index].load(std::memory_order_relaxed));
    const VoxSpeechGlyph glyph = voxGlyphForSymbol(symbol);
    plugin.voxFrameEnergy = plugin.voxCompiledEnergy[index].load(std::memory_order_relaxed);
    plugin.voxFramePitchMul = plugin.voxCompiledPitchMul[index].load(std::memory_order_relaxed);
    plugin.voxFrameNoise = plugin.voxCompiledNoise[index].load(std::memory_order_relaxed);
    plugin.voxFramePlosive = plugin.voxCompiledPlosive[index].load(std::memory_order_relaxed);
    plugin.voxFrameF1 = plugin.voxCompiledF1[index].load(std::memory_order_relaxed);
    plugin.voxFrameF2 = plugin.voxCompiledF2[index].load(std::memory_order_relaxed);
    plugin.voxFrameF3 = plugin.voxCompiledF3[index].load(std::memory_order_relaxed);
    plugin.voxFrameSymbol = symbol;
    plugin.voxPhraseCurrentIndex = index;
    plugin.voxPhraseIndex = (index + 1u) % length;
    if (glyph.rest) {
        plugin.voxPhraseHold = duration;
        return;
    }
    const float vowelWeight = glyph.vowel ? 0.88f : 0.34f;
    plugin.voxPhraseU = s3g::lerp(plugin.voxPhraseU, glyph.u, vowelWeight);
    plugin.voxPhraseV = s3g::lerp(plugin.voxPhraseV, glyph.v, vowelWeight);
    plugin.voxPhraseVowelU = plugin.voxPhraseU;
    plugin.voxPhraseVowelV = plugin.voxPhraseV;
    plugin.voxPhraseHold = duration;
    const float articulation = std::clamp(plugin.vox.articulation, 0.0f, 1.0f);
    const float amount = std::clamp(strength, 0.0f, 1.0f) * articulation;
    if (glyph.vowel || glyph.hold > 0u) {
        plugin.voxPhraseVowelEnv = std::max(plugin.voxPhraseVowelEnv, amount * (glyph.vowel ? 1.0f : 0.42f));
    }
    plugin.voxConsonantEnv = std::max(plugin.voxConsonantEnv, amount * glyph.consonant);
    plugin.voxPlosiveEnv = std::max(plugin.voxPlosiveEnv, amount * glyph.plosive * std::clamp(plugin.vox.plosive, 0.0f, 1.0f));
    plugin.voxPhraseBreathEnv = std::max(plugin.voxPhraseBreathEnv, amount * glyph.breath);
    plugin.voxSibilantState += glyph.sibilance * std::clamp(plugin.vox.sibilance, 0.0f, 1.0f) * 0.006f;
}

s3g::AmbiVotParams applyVoxPhraseToRenderParams(const Plugin& plugin, s3g::AmbiVotParams params)
{
    const uint32_t length = plugin.voxPhraseLength.load(std::memory_order_acquire);
    if (length == 0u) return params;
    const float amount = std::clamp(plugin.vox.articulation * 0.85f + plugin.vox.vowelSpread * 0.35f, 0.0f, 0.96f);
    params.vectorX = std::clamp(s3g::lerp(params.vectorX, plugin.voxPhraseU, amount), 0.0f, 1.0f);
    params.vectorY = std::clamp(s3g::lerp(params.vectorY, plugin.voxPhraseV, amount), 0.0f, 1.0f);
    params.morph = std::clamp(params.morph + plugin.voxPhraseVowelEnv * 0.22f, 0.0f, 1.0f);
    params.scan = std::clamp(params.scan + std::fabs(plugin.voxPhraseU - 0.5f) * plugin.voxPhraseVowelEnv * 0.20f, 0.0f, 1.0f);
    return params;
}

float voxWorldFrameValueAt(const std::vector<float>& values, int sampleRate,
                           double framePeriodMs, double samplePhase)
{
    if (values.empty() || sampleRate <= 0) return 0.0f;
    const double frame = (samplePhase / static_cast<double>(sampleRate))
        * (1000.0 / std::max(0.001, framePeriodMs));
    const double wrapped = std::fmod(std::max(0.0, frame), static_cast<double>(values.size()));
    const size_t i0 = static_cast<size_t>(wrapped);
    const size_t i1 = std::min(values.size() - 1u, i0 + 1u);
    const float frac = static_cast<float>(wrapped - static_cast<double>(i0));
    return s3g::lerp(values[i0], values[i1], frac);
}

float voxWorldFrameValueAt(const std::vector<float>& values, const VoxWorldSample& sample, double samplePhase)
{
    return voxWorldFrameValueAt(values, sample.sampleRate, sample.framePeriodMs, samplePhase);
}

float voxWorldContourCorrection(const std::vector<float>& f0,
                                int sampleRate,
                                double framePeriodMs,
                                double samplePhase,
                                int baseMidi,
                                s3g::AmbiVoxContourMode mode)
{
    if (mode == s3g::AmbiVoxContourMode::Original) return 1.0f;
    const float frequency = voxWorldFrameValueAt(f0, sampleRate, framePeriodMs, samplePhase);
    if (frequency <= 24.0f) return 1.0f;
    const float baseFrequency = 440.0f * std::pow(2.0f,
        (static_cast<float>(baseMidi) - 69.0f) / 12.0f);
    const float sourceRatio = std::clamp(frequency / std::max(20.0f, baseFrequency), 0.25f, 4.0f);
    float desiredRatio = sourceRatio;
    if (mode == s3g::AmbiVoxContourMode::Reduced) {
        desiredRatio = std::sqrt(sourceRatio);
    } else if (mode == s3g::AmbiVoxContourMode::Flat) {
        desiredRatio = 1.0f;
    } else if (mode == s3g::AmbiVoxContourMode::Quantized) {
        desiredRatio = std::pow(2.0f,
            std::round(12.0f * std::log2(sourceRatio)) / 12.0f);
    }
    return std::clamp(desiredRatio / sourceRatio, 0.25f, 4.0f);
}

VoxPvocSpectralControl voxWorldSpectralControl(const Plugin& plugin,
                                               const VoxWorldParameterData& parameters)
{
    VoxPvocSpectralControl control {};
    control.parameters = parameters.ready() ? &parameters : nullptr;
    control.formant = plugin.voxWorldFormantSmooth;
    control.periodicity = plugin.voxWorldVoicingSmooth;
    control.airColor = plugin.voxWorldAirColorSmooth;
    return control;
}

float voxVoicebankPitchRatio(const Plugin& plugin,
                             const VoxVoicebankEntry& entry,
                             uint32_t anchor,
                             float targetNote)
{
    if (!entry.audio) return 1.0f;
    const auto& audio = *entry.audio;
    const float anchorCents = audio.worldResynthesized
        ? static_cast<float>(kVoxWorldPitchAnchorSemitones[
            std::min<uint32_t>(anchor, kVoxWorldPitchAnchorCount - 1u)]) * 100.0f
        : 0.0f;
    return std::pow(2.0f,
        (plugin.voxWorldPitchSmooth + (targetNote - static_cast<float>(audio.baseMidi)) * 100.0f
            - anchorCents) / 1200.0f);
}

float processVoicebankEntry(Plugin& plugin,
                            VoxPvocVoiceState& state,
                            const VoxVoicebankEntry& entry,
                            uint32_t voice,
                            uint32_t anchor,
                            float pitchRatio,
                            VoxSpeechMode speechMode,
                            uint8_t eventFlags)
{
    if (!entry.audio || entry.audio->sampleRate <= 0) return 0.0f;
    const auto& source = voxVoicebankSource(entry, anchor);
    if (source.empty()) return 0.0f;
    const double sourceLength = static_cast<double>(source.size());
    const double eventStart = std::clamp(entry.startSample, 0.0, sourceLength - 1.0);
    const double eventEnd = std::clamp(
        entry.endSample > entry.startSample ? entry.endSample : sourceLength,
        eventStart + 1.0, sourceLength);
    const bool phraseMode = speechMode != VoxSpeechMode::Texture;
    const bool sustainMode = speechMode == VoxSpeechMode::Sing;
    const double phase = state.analysisPosition;
    const bool consonant = phraseMode && phase < std::max(entry.fixedSample, eventStart + 1.0);
    double rangeStart = eventStart;
    double rangeEnd = eventEnd;
    if (sustainMode && !consonant) {
        const double sustainStart = (eventFlags & kVoxBankEventVowel) != 0u
            ? std::max(entry.fixedSample, entry.loopStart)
            : entry.fixedSample;
        rangeStart = std::clamp(sustainStart, eventStart, eventEnd - 1.0);
        rangeEnd = std::clamp(entry.loopEnd > rangeStart ? entry.loopEnd : eventEnd,
            rangeStart + 1.0, eventEnd);
    } else if (!phraseMode) {
        rangeStart = sourceLength * static_cast<double>(
            std::clamp(plugin.vox.worldLoopStart, 0.0f, 1.0f));
        rangeEnd = sourceLength * static_cast<double>(
            std::clamp(plugin.vox.worldLoopEnd, 0.0f, 1.0f));
        if (rangeEnd <= rangeStart + 8.0) {
            rangeStart = std::clamp(entry.loopStart, 0.0, sourceLength - 1.0);
            rangeEnd = std::clamp(entry.loopEnd > entry.loopStart ? entry.loopEnd : sourceLength,
                rangeStart + 1.0, sourceLength);
        }
    }
    const float sourceRateRatio = static_cast<float>(entry.audio->sampleRate)
        / static_cast<float>(plugin.sampleRate);
    const float timelineRate = phraseMode
        ? 1.0f
        : 0.25f + plugin.voxWorldRateSmooth * 1.75f;
    const float stretch = speechMode == VoxSpeechMode::Sing
        ? 1.0f
        : plugin.voxPvocStretchSmooth;
    const float contourTarget = voxWorldContourCorrection(
        entry.audio->worldF0, entry.audio->sampleRate,
        entry.audio->worldFramePeriodMs, state.analysisPosition,
        entry.audio->baseMidi, plugin.vox.contourMode);
    const float contourSmooth = 1.0f - std::exp(-1.0f
        / std::max(1.0f, 0.045f * static_cast<float>(plugin.sampleRate)));
    const uint32_t contourVoice = std::min<uint32_t>(voice, s3g::kAmbiVoxMaxVoices - 1u);
    plugin.voxContourRatioSmooth[contourVoice] += (contourTarget
        - plugin.voxContourRatioSmooth[contourVoice]) * contourSmooth;
    const VoxPvocSpectralControl spectral = voxWorldSpectralControl(
        plugin, entry.audio->worldParameters);
    return plugin.pvoc.process(state, source, rangeStart, rangeEnd,
        sourceRateRatio, timelineRate, stretch,
        pitchRatio * plugin.voxContourRatioSmooth[contourVoice],
        plugin.voxPvocTransientSmooth,
        phraseMode ? 0.0f : plugin.voxWorldFreezeSmooth,
        phraseMode ? 0.0f : plugin.voxWorldScrubSmooth,
        speechMode != VoxSpeechMode::Speak, spectral);
}

void resetWorldPhases(Plugin& plugin)
{
    if (const auto bank = std::atomic_load_explicit(&plugin.voicebank, std::memory_order_acquire)) {
        resetVoicebankTimelines(plugin, *bank);
        plugin.voxPhrasePhase = 0.0f;
        return;
    }
    const auto sample = std::atomic_load_explicit(&plugin.worldSample, std::memory_order_acquire);
    const double length = sample && !sample->samples.empty() ? static_cast<double>(sample->samples.size()) : 0.0;
    const double loopStart = length * static_cast<double>(std::clamp(plugin.vox.worldLoopStart, 0.0f, 1.0f));
    const double loopEnd = length * static_cast<double>(std::clamp(plugin.vox.worldLoopEnd, 0.0f, 1.0f));
    const double loopLength = std::max(0.0, loopEnd - loopStart);
    const uint32_t activeVoices = std::clamp<uint32_t>(
        plugin.params.voices, 1u, s3g::kAmbiVoxMaxVoices);
    for (uint32_t voice = 0; voice < s3g::kAmbiVoxMaxVoices; ++voice) {
        const float phase = s3g::ambiVoxPhraseSpreadPhase(
            plugin.vox.orchestration, voice, activeVoices, plugin.vox.phraseSpread);
        const double position = loopStart + loopLength * static_cast<double>(phase);
        plugin.voxWorldPhase[voice] = position;
        plugin.pvoc.reset(plugin.voxPvocVoice[voice],
            sample && !sample->samples.empty() ? sample->samples.data() : nullptr, position);
    }
}

bool applyWorldPostProcess(Plugin& plugin, const VoxWorldSample& sample, float* const* outputs, uint32_t channels, uint32_t frames)
{
    if (channels == 0u || !outputs || sample.samples.empty() || sample.sampleRate <= 0) return false;
    if (plugin.voxWorldTimingResetRequested.exchange(false, std::memory_order_acq_rel)) {
        resetWorldPhases(plugin);
    }
    const auto vox = plugin.vox;
    const VoxSpeechMode speechMode = vox.speechMode;
    const VoxCircuitMode circuitMode = vox.circuitMode;
    uint32_t noiseState = plugin.voxNoiseState;
    const float outputGainTarget = std::min(s3g::dbToGain(plugin.params.outputGainDb), s3g::dbToGain(12.0f));
    const float outputGainSmooth = 1.0f - std::exp(-1.0f / (0.020f * static_cast<float>(plugin.sampleRate)));
    const float toneSmooth = 1.0f - std::exp(-1.0f / (0.045f * static_cast<float>(plugin.sampleRate)));
    const float timelineSmooth = 1.0f - std::exp(-1.0f / (0.140f * static_cast<float>(plugin.sampleRate)));
    const float voiceSmooth = 1.0f - std::exp(-1.0f / (0.090f * static_cast<float>(plugin.sampleRate)));
    const float limiterRelease = 1.0f - std::exp(-1.0f / (0.120f * static_cast<float>(plugin.sampleRate)));
    const uint32_t activeVoices = std::clamp<uint32_t>(plugin.params.voices, 1u, s3g::kAmbiVoxMaxVoices);
    const float delayBlockSmooth = 1.0f - std::exp(-static_cast<float>(frames)
        / (0.180f * static_cast<float>(plugin.sampleRate)));
    plugin.voxWorldVoiceSpreadSmooth += (std::clamp(vox.worldVoiceSpread, 0.0f, 1.0f)
        - plugin.voxWorldVoiceSpreadSmooth) * delayBlockSmooth;
    plugin.voxWorldVoiceDeviationSmooth += (std::clamp(vox.worldVoiceDeviation, 0.0f, 1.0f)
        - plugin.voxWorldVoiceDeviationSmooth) * delayBlockSmooth;
    const auto voiceDelays = prepareVoiceDelays(plugin, activeVoices);
    const double sampleLength = static_cast<double>(sample.samples.size());
    const float baseRate = static_cast<float>(sample.sampleRate) / static_cast<float>(plugin.sampleRate);
    const auto& motionPoints = plugin.engine.motionPoints();
    std::array<std::array<float, s3g::kAmbiSpeakerDecoderMaxChannels>, s3g::kAmbiVoxMaxVoices> basis {};
    std::array<float, s3g::kAmbiVoxMaxVoices> distanceGains {};
    for (uint32_t voice = 0; voice < s3g::kAmbiVoxMaxVoices; ++voice) {
        const auto& point = motionPoints[voice];
        basis[voice] = s3g::acnSn3dBasis7(s3g::directionFromAed(point.azimuthDeg, point.elevationDeg));
        distanceGains[voice] = speechMode == VoxSpeechMode::Speak ? 1.0f : 1.0f / std::max(0.35f, point.distance);
    }
    for (uint32_t frame = 0; frame < frames; ++frame) {
        for (uint32_t ch = 0; ch < channels; ++ch) {
            if (outputs[ch]) outputs[ch][frame] = 0.0f;
        }
        plugin.voxOutputGainSmooth += (outputGainTarget - plugin.voxOutputGainSmooth) * outputGainSmooth;
        plugin.voxWorldRateSmooth += (std::clamp(vox.worldRate, 0.0f, 1.0f) - plugin.voxWorldRateSmooth) * timelineSmooth;
        plugin.voxWorldPitchSmooth += (std::clamp(vox.worldPitchCents, -2400.0f, 2400.0f) - plugin.voxWorldPitchSmooth) * timelineSmooth;
        plugin.voxWorldLoopStartSmooth += (std::clamp(vox.worldLoopStart, 0.0f, 1.0f) - plugin.voxWorldLoopStartSmooth) * timelineSmooth;
        plugin.voxWorldLoopEndSmooth += (std::clamp(vox.worldLoopEnd, 0.0f, 1.0f) - plugin.voxWorldLoopEndSmooth) * timelineSmooth;
        if (plugin.voxWorldLoopEndSmooth <= plugin.voxWorldLoopStartSmooth + 0.01f) {
            const float center = std::clamp((plugin.voxWorldLoopStartSmooth + plugin.voxWorldLoopEndSmooth) * 0.5f, 0.005f, 0.995f);
            plugin.voxWorldLoopStartSmooth = std::max(0.0f, center - 0.005f);
            plugin.voxWorldLoopEndSmooth = std::min(1.0f, center + 0.005f);
        }
        plugin.voxWorldFreezeSmooth += (std::clamp(vox.worldFreeze, 0.0f, 1.0f) - plugin.voxWorldFreezeSmooth) * timelineSmooth;
        plugin.voxWorldScrubSmooth += (std::clamp(vox.worldScrub, 0.0f, 1.0f) - plugin.voxWorldScrubSmooth) * timelineSmooth;
        plugin.voxWorldFormantSmooth += (std::clamp(vox.formantMacro, -1.0f, 1.0f) - plugin.voxWorldFormantSmooth) * toneSmooth;
        plugin.voxWorldAirColorSmooth += (std::clamp(vox.worldAirColor, -1.0f, 1.0f) - plugin.voxWorldAirColorSmooth) * toneSmooth;
        plugin.voxWorldFlutterSmooth += (std::clamp(vox.bendMacro, 0.0f, 1.0f) - plugin.voxWorldFlutterSmooth) * toneSmooth;
        plugin.voxWorldDegradeSmooth += (std::clamp(vox.crushMacro, 0.0f, 1.0f) - plugin.voxWorldDegradeSmooth) * toneSmooth;
        plugin.voxWorldEdgeSmooth += (std::clamp(vox.rasp, 0.0f, 1.0f) - plugin.voxWorldEdgeSmooth) * toneSmooth;
        plugin.voxWorldAirSmooth += (std::clamp(vox.breath, 0.0f, 1.0f) - plugin.voxWorldAirSmooth) * toneSmooth;
        plugin.voxWorldDriveSmooth += (std::clamp(vox.drive, 0.0f, 1.0f) - plugin.voxWorldDriveSmooth) * toneSmooth;
        plugin.voxWorldVoicingSmooth += (std::clamp(vox.worldVoicing, 0.0f, 1.0f) - plugin.voxWorldVoicingSmooth) * toneSmooth;
        plugin.voxPvocStretchSmooth += (std::clamp(vox.pvocStretch, 0.25f, 4.0f) - plugin.voxPvocStretchSmooth) * timelineSmooth;
        plugin.voxPvocTransientSmooth += (std::clamp(vox.pvocTransient, 0.0f, 1.0f) - plugin.voxPvocTransientSmooth) * toneSmooth;
        double loopStart = sampleLength * static_cast<double>(plugin.voxWorldLoopStartSmooth);
        double loopEnd = sampleLength * static_cast<double>(plugin.voxWorldLoopEndSmooth);
        if (loopEnd <= loopStart + 8.0) {
            loopStart = 0.0;
            loopEnd = sampleLength;
        }
        const float worldRate = 0.25f + plugin.voxWorldRateSmooth * 1.75f;
        const float bend = plugin.voxWorldFlutterSmooth;
        const float crush = plugin.voxWorldDegradeSmooth;
        const float edge = plugin.voxWorldEdgeSmooth;
        const float air = plugin.voxWorldAirSmooth;
        const float drive = plugin.voxWorldDriveSmooth;
        const float freeze = plugin.voxWorldFreezeSmooth;
        float voiceGainEnergy = 0.0f;
        for (uint32_t voice = 0; voice < s3g::kAmbiVoxMaxVoices; ++voice) {
            const float target = voxVoiceActivityTarget(plugin, voice, activeVoices);
            plugin.voxWorldVoiceGainSmooth[voice] += (target - plugin.voxWorldVoiceGainSmooth[voice]) * voiceSmooth;
            voiceGainEnergy += plugin.voxWorldVoiceGainSmooth[voice] * plugin.voxWorldVoiceGainSmooth[voice];
        }
        const float voiceGain = (speechMode == VoxSpeechMode::Speak ? 0.96f : 0.72f) / std::sqrt(std::max(1.0f, voiceGainEnergy));
        for (uint32_t voice = 0; voice < s3g::kAmbiVoxMaxVoices; ++voice) {
            const float voiceFade = plugin.voxWorldVoiceGainSmooth[voice];
            if (voiceFade <= 0.00005f
                && (voice >= activeVoices || (plugin.params.mode == s3g::AmbiVotMode::Midi && !plugin.voxMidiGate[voice]))) continue;
            const float voiceSkew = static_cast<float>(voice) / static_cast<float>(std::max<uint32_t>(1u, activeVoices - 1u));
            const float bent = circuitMode == VoxCircuitMode::Clean ? 1.0f
                : 1.0f + bend * (0.010f * std::sin(static_cast<float>(plugin.voxWorldPhase[voice]) * 0.003f + voiceSkew * 5.0f)
                    + (circuitMode == VoxCircuitMode::Broken ? voxUnitNoise(noiseState) * 0.015f : 0.0f));
            if (plugin.voxPvocVoice[voice].sourceIdentity != sample.samples.data() || plugin.voxMidiRetrigger[voice]) {
                plugin.pvoc.reset(plugin.voxPvocVoice[voice], sample.samples.data(), loopStart);
                plugin.voxMidiRetrigger[voice] = false;
            }
            const float targetNote = voxVoiceExpressiveNote(
                plugin, voice, speechMode == VoxSpeechMode::Sing);
            const float targetPitchRatio = std::pow(2.0f,
                (plugin.voxWorldPitchSmooth + (targetNote - static_cast<float>(sample.baseMidi)) * 100.0f) / 1200.0f);
            plugin.voxPitchRatioSmooth[voice] += (targetPitchRatio - plugin.voxPitchRatioSmooth[voice]) * toneSmooth;
            const float contourTarget = voxWorldContourCorrection(
                sample.f0, sample.sampleRate, sample.framePeriodMs,
                plugin.voxPvocVoice[voice].analysisPosition,
                sample.baseMidi, vox.contourMode);
            plugin.voxContourRatioSmooth[voice] += (contourTarget
                - plugin.voxContourRatioSmooth[voice]) * toneSmooth;
            const VoxPvocSpectralControl spectral = voxWorldSpectralControl(
                plugin, sample.parameters);
            float value = plugin.pvoc.process(
                plugin.voxPvocVoice[voice], sample.samples, loopStart, loopEnd,
                baseRate, worldRate, plugin.voxPvocStretchSmooth,
                plugin.voxPitchRatioSmooth[voice] * plugin.voxContourRatioSmooth[voice] * bent,
                plugin.voxPvocTransientSmooth,
                freeze, plugin.voxWorldScrubSmooth, true, spectral);
            const double phase = plugin.voxPvocVoice[voice].analysisPosition;
            plugin.voxWorldPhase[voice] = phase;
            const float airNoise = voxUnitNoise(noiseState) * air * (0.018f + 0.055f * std::fabs(value));
            value += airNoise;
            const float driven = voxSoftSat(value * (1.0f + drive * 3.0f));
            value = s3g::lerp(value, driven, edge * 0.40f);
            if (circuitMode != VoxCircuitMode::Clean || crush > 0.0001f) {
                const float totalCrush = std::clamp(crush + (circuitMode == VoxCircuitMode::Chip ? 0.22f : 0.0f)
                    + (circuitMode == VoxCircuitMode::Broken ? bend * 0.36f : 0.0f), 0.0f, 1.0f);
                const float steps = std::max(8.0f, 512.0f * std::pow(1.0f / 64.0f, totalCrush));
                value = std::round(value * steps) / steps;
            }
            const float out = processVoiceDelay(voiceDelays, voice,
                value * voiceGain * voiceFade * distanceGains[voice] * plugin.voxOutputGainSmooth);
            for (uint32_t ch = 0; ch < channels; ++ch) {
                if (!outputs[ch]) continue;
                const float encoded = ch < basis[voice].size() ? basis[voice][ch] : 0.0f;
                outputs[ch][frame] = s3g::flushDenormal(std::clamp(outputs[ch][frame] + out * encoded, -1.2f, 1.2f));
            }
        }
        float framePeak = 0.0f;
        for (uint32_t ch = 0; ch < channels; ++ch) {
            if (outputs[ch]) framePeak = std::max(framePeak, std::fabs(outputs[ch][frame]));
        }
        const float limiterTarget = framePeak > 0.89f ? 0.89f / std::max(0.000001f, framePeak) : 1.0f;
        if (limiterTarget < plugin.voxLimiterGain) plugin.voxLimiterGain = limiterTarget;
        else plugin.voxLimiterGain += (limiterTarget - plugin.voxLimiterGain) * limiterRelease;
        for (uint32_t ch = 0; ch < channels; ++ch) {
            if (outputs[ch]) outputs[ch][frame] = s3g::flushDenormal(std::clamp(outputs[ch][frame] * plugin.voxLimiterGain, -0.98f, 0.98f));
        }
    }
    plugin.voxNoiseState = noiseState;
#if defined(__APPLE__)
    const double guiLoopStart = sampleLength * static_cast<double>(plugin.voxWorldLoopStartSmooth);
    const double guiLoopEnd = sampleLength * static_cast<double>(plugin.voxWorldLoopEndSmooth);
    const double guiLoopLength = std::max(1.0, guiLoopEnd - guiLoopStart);
    for (uint32_t voice = 0u; voice < s3g::kAmbiVoxMaxVoices; ++voice) {
        double phase = std::fmod(plugin.voxWorldPhase[voice] - guiLoopStart, guiLoopLength);
        if (phase < 0.0) phase += guiLoopLength;
        plugin.guiVoxPhrasePhase[voice].store(
            static_cast<float>(phase / guiLoopLength), std::memory_order_relaxed);
    }
#endif
    return true;
}

bool applyVoicebankPostProcess(Plugin& plugin, const VoxVoicebank& bank, float* const* outputs, uint32_t channels, uint32_t frames)
{
    if (channels == 0u || !outputs || bank.entries.empty()) return false;
    if (plugin.voxBankTimingResetRequested.exchange(false, std::memory_order_acq_rel)) {
        resetVoicebankTimelines(plugin, bank);
    }
    const auto vox = plugin.vox;
    const VoxSpeechMode speechMode = vox.speechMode;
    uint32_t noiseState = plugin.voxNoiseState;
    const float outputGainTarget = std::min(s3g::dbToGain(plugin.params.outputGainDb), s3g::dbToGain(12.0f));
    const float outputGainSmooth = 1.0f - std::exp(-1.0f / (0.020f * static_cast<float>(plugin.sampleRate)));
    const float timelineSmooth = 1.0f - std::exp(-1.0f / (0.120f * static_cast<float>(plugin.sampleRate)));
    const float toneSmooth = 1.0f - std::exp(-1.0f / (0.045f * static_cast<float>(plugin.sampleRate)));
    const float voiceSmooth = 1.0f - std::exp(-1.0f / (0.090f * static_cast<float>(plugin.sampleRate)));
    const float limiterRelease = 1.0f - std::exp(-1.0f / (0.120f * static_cast<float>(plugin.sampleRate)));
    const uint32_t activeVoices = std::clamp<uint32_t>(plugin.params.voices, 1u, s3g::kAmbiVoxMaxVoices);
    const float delayBlockSmooth = 1.0f - std::exp(-static_cast<float>(frames)
        / (0.180f * static_cast<float>(plugin.sampleRate)));
    plugin.voxWorldVoiceSpreadSmooth += (std::clamp(vox.worldVoiceSpread, 0.0f, 1.0f)
        - plugin.voxWorldVoiceSpreadSmooth) * delayBlockSmooth;
    plugin.voxWorldVoiceDeviationSmooth += (std::clamp(vox.worldVoiceDeviation, 0.0f, 1.0f)
        - plugin.voxWorldVoiceDeviationSmooth) * delayBlockSmooth;
    const auto voiceDelays = prepareVoiceDelays(plugin, activeVoices);
    const auto& motionPoints = plugin.engine.motionPoints();
    std::array<std::array<float, s3g::kAmbiSpeakerDecoderMaxChannels>, s3g::kAmbiVoxMaxVoices> basis {};
    std::array<float, s3g::kAmbiVoxMaxVoices> distanceGains {};
    for (uint32_t voice = 0; voice < s3g::kAmbiVoxMaxVoices; ++voice) {
        const auto& point = motionPoints[voice];
        basis[voice] = s3g::acnSn3dBasis7(s3g::directionFromAed(point.azimuthDeg, point.elevationDeg));
        distanceGains[voice] = speechMode == VoxSpeechMode::Speak ? 1.0f : 1.0f / std::max(0.35f, point.distance);
    }
    for (uint32_t frame = 0; frame < frames; ++frame) {
        for (uint32_t ch = 0; ch < channels; ++ch) {
            if (outputs[ch]) outputs[ch][frame] = 0.0f;
        }
        plugin.voxOutputGainSmooth += (outputGainTarget - plugin.voxOutputGainSmooth) * outputGainSmooth;
        plugin.voxWorldRateSmooth += (std::clamp(vox.worldRate, 0.0f, 1.0f) - plugin.voxWorldRateSmooth) * timelineSmooth;
        plugin.voxWorldPitchSmooth += (std::clamp(vox.worldPitchCents, -2400.0f, 2400.0f) - plugin.voxWorldPitchSmooth) * timelineSmooth;
        plugin.voxWorldFreezeSmooth += (std::clamp(vox.worldFreeze, 0.0f, 1.0f) - plugin.voxWorldFreezeSmooth) * timelineSmooth;
        plugin.voxWorldScrubSmooth += (std::clamp(vox.worldScrub, 0.0f, 1.0f) - plugin.voxWorldScrubSmooth) * timelineSmooth;
        plugin.voxWorldFormantSmooth += (std::clamp(vox.formantMacro, -1.0f, 1.0f) - plugin.voxWorldFormantSmooth) * toneSmooth;
        plugin.voxWorldAirColorSmooth += (std::clamp(vox.worldAirColor, -1.0f, 1.0f) - plugin.voxWorldAirColorSmooth) * toneSmooth;
        plugin.voxWorldFlutterSmooth += (std::clamp(vox.bendMacro, 0.0f, 1.0f) - plugin.voxWorldFlutterSmooth) * toneSmooth;
        plugin.voxWorldEdgeSmooth += (std::clamp(vox.rasp, 0.0f, 1.0f) - plugin.voxWorldEdgeSmooth) * toneSmooth;
        plugin.voxWorldAirSmooth += (std::clamp(vox.breath, 0.0f, 1.0f) - plugin.voxWorldAirSmooth) * toneSmooth;
        plugin.voxWorldDriveSmooth += (std::clamp(vox.drive, 0.0f, 1.0f) - plugin.voxWorldDriveSmooth) * toneSmooth;
        plugin.voxWorldVoicingSmooth += (std::clamp(vox.worldVoicing, 0.0f, 1.0f) - plugin.voxWorldVoicingSmooth) * toneSmooth;
        plugin.voxPvocStretchSmooth += (std::clamp(vox.pvocStretch, 0.25f, 4.0f) - plugin.voxPvocStretchSmooth) * timelineSmooth;
        plugin.voxPvocTransientSmooth += (std::clamp(vox.pvocTransient, 0.0f, 1.0f) - plugin.voxPvocTransientSmooth) * toneSmooth;
        float voiceGainEnergy = 0.0f;
        for (uint32_t voice = 0; voice < s3g::kAmbiVoxMaxVoices; ++voice) {
            const float target = voxVoiceActivityTarget(plugin, voice, activeVoices);
            plugin.voxWorldVoiceGainSmooth[voice] += (target - plugin.voxWorldVoiceGainSmooth[voice]) * voiceSmooth;
            voiceGainEnergy += plugin.voxWorldVoiceGainSmooth[voice] * plugin.voxWorldVoiceGainSmooth[voice];
        }
        const float voiceGain = (speechMode == VoxSpeechMode::Speak ? 0.96f : 0.72f) / std::sqrt(std::max(1.0f, voiceGainEnergy));
        for (uint32_t voice = 0; voice < s3g::kAmbiVoxMaxVoices; ++voice) {
            const float voiceFade = plugin.voxWorldVoiceGainSmooth[voice];
            if (voiceFade <= 0.00005f
                && (voice >= activeVoices || (plugin.params.mode == s3g::AmbiVotMode::Midi && !plugin.voxMidiGate[voice]))) continue;
            if (plugin.voxMidiRetrigger[voice]) {
                if (speechMode == VoxSpeechMode::Texture) {
                    plugin.pvoc.reset(plugin.voxPvocVoice[voice], nullptr, 0.0);
                } else {
                    retriggerVoicebankVoice(plugin, bank, voice);
                }
                plugin.voxMidiRetrigger[voice] = false;
            }
            double transitionWindow = 0.0;
            if (speechMode != VoxSpeechMode::Texture) {
                if (plugin.voxBankVoicePhraseSamplesRemaining[voice] <= 0.0) {
                    advanceVoicebankVoice(plugin, bank, voice);
                }
                transitionWindow = pendingVoicebankTransitionSamples(plugin, bank, voice);
                if (plugin.voxBankVoicePhraseSamplesRemaining[voice] <= transitionWindow) {
                    prepareVoicebankTransition(plugin, bank, voice);
                }
                plugin.voxBankVoicePhraseSamplesRemaining[voice] = std::max(
                    0.0, plugin.voxBankVoicePhraseSamplesRemaining[voice] - 1.0);
            }
            const uint8_t currentFlags = speechMode == VoxSpeechMode::Texture
                ? kVoxBankEventVowel
                : plugin.voxBankVoiceCurrentFlags[voice];
            if ((currentFlags & kVoxBankEventRest) != 0u) {
                const float out = processVoiceDelay(voiceDelays, voice, 0.0f);
                for (uint32_t ch = 0; ch < channels; ++ch) {
                    if (!outputs[ch]) continue;
                    const float encoded = ch < basis[voice].size() ? basis[voice][ch] : 0.0f;
                    outputs[ch][frame] = s3g::flushDenormal(std::clamp(
                        outputs[ch][frame] + out * encoded, -1.2f, 1.2f));
                }
                continue;
            }
            const size_t entryIndex = speechMode == VoxSpeechMode::Texture
                ? static_cast<size_t>(voice % bank.entries.size())
                : static_cast<size_t>(std::min<uint32_t>(plugin.voxBankVoiceCurrentEntry[voice], static_cast<uint32_t>(bank.entries.size() - 1u)));
            const auto& entry = bank.entries[entryIndex];
            if (!entry.audio || entry.audio->samples.empty() || entry.audio->sampleRate <= 0) continue;
            const auto& audio = *entry.audio;
            uint32_t pitchAnchor = std::min<uint32_t>(
                plugin.voxBankWorldPitchAnchor[voice], kVoxWorldPitchAnchorCount - 1u);
            const auto* source = &voxVoicebankSource(entry, pitchAnchor);
            if (plugin.voxPvocVoice[voice].sourceIdentity != source->data()) {
                pitchAnchor = voxVoicebankPitchAnchor(plugin, entry, voice);
                plugin.voxBankWorldPitchAnchor[voice] = static_cast<uint8_t>(pitchAnchor);
                source = &voxVoicebankSource(entry, pitchAnchor);
                plugin.pvoc.reset(plugin.voxPvocVoice[voice], source->data(), entry.startSample);
            }
            if (source->empty()) continue;
            const float targetNote = voxVoiceExpressiveNote(
                plugin, voice, speechMode == VoxSpeechMode::Sing);
            const float targetPitchRatio = voxVoicebankPitchRatio(
                plugin, entry, pitchAnchor, targetNote);
            plugin.voxPitchRatioSmooth[voice] += (targetPitchRatio - plugin.voxPitchRatioSmooth[voice]) * toneSmooth;
            float value = processVoicebankEntry(plugin, plugin.voxPvocVoice[voice],
                entry, voice, pitchAnchor, plugin.voxPitchRatioSmooth[voice],
                speechMode, currentFlags);
            const double phase = plugin.voxPvocVoice[voice].analysisPosition;
            plugin.voxWorldPhase[voice] = phase;
            if (speechMode != VoxSpeechMode::Texture) {
                const double elapsed = std::max(0.0,
                    plugin.voxBankVoicePhraseSamplesTotal[voice]
                        - plugin.voxBankVoicePhraseSamplesRemaining[voice]);
                const float attackEnv = static_cast<float>(std::clamp(
                    elapsed / std::max(1.0, plugin.sampleRate * 0.012), 0.0, 1.0));
                value *= std::sin(attackEnv * s3g::kPi * 0.5f);
                if (plugin.voxBankVoiceTransitionActive[voice] && transitionWindow > 1.0) {
                    const float transitionMix = static_cast<float>(std::clamp(
                        1.0 - plugin.voxBankVoicePhraseSamplesRemaining[voice] / transitionWindow,
                        0.0, 1.0));
                    const float smoothMix = transitionMix * transitionMix * (3.0f - 2.0f * transitionMix);
                    if ((plugin.voxBankVoiceNextFlags[voice] & kVoxBankEventRest) != 0u) {
                        value *= 1.0f - smoothMix;
                    } else {
                        const uint32_t nextIndex = std::min<uint32_t>(
                            plugin.voxBankVoiceNextEntry[voice],
                            static_cast<uint32_t>(bank.entries.size() - 1u));
                        const auto& next = bank.entries[nextIndex];
                        const uint32_t nextAnchor = plugin.voxBankVoiceNextPitchAnchor[voice];
                        const float nextPitchRatio = voxVoicebankPitchRatio(
                            plugin, next, nextAnchor, targetNote);
                        const float nextValue = processVoicebankEntry(plugin,
                            plugin.voxPvocNextVoice[voice], next, voice, nextAnchor,
                            nextPitchRatio, speechMode, plugin.voxBankVoiceNextFlags[voice]);
                        value = s3g::lerp(value, nextValue, smoothMix);
                    }
                }
            }
            value += voxUnitNoise(noiseState) * plugin.voxWorldAirSmooth * (0.012f + 0.035f * std::fabs(value));
            const float driven = voxSoftSat(value * (1.0f + plugin.voxWorldDriveSmooth * 2.2f));
            value = s3g::lerp(value, driven, plugin.voxWorldEdgeSmooth * 0.28f);
            const float out = processVoiceDelay(voiceDelays, voice,
                value * voiceGain * voiceFade * distanceGains[voice] * plugin.voxOutputGainSmooth);
            for (uint32_t ch = 0; ch < channels; ++ch) {
                if (!outputs[ch]) continue;
                const float encoded = ch < basis[voice].size() ? basis[voice][ch] : 0.0f;
                outputs[ch][frame] = s3g::flushDenormal(std::clamp(outputs[ch][frame] + out * encoded, -1.2f, 1.2f));
            }
        }
        float framePeak = 0.0f;
        for (uint32_t ch = 0; ch < channels; ++ch) {
            if (outputs[ch]) framePeak = std::max(framePeak, std::fabs(outputs[ch][frame]));
        }
        const float limiterTarget = framePeak > 0.89f ? 0.89f / std::max(0.000001f, framePeak) : 1.0f;
        if (limiterTarget < plugin.voxLimiterGain) plugin.voxLimiterGain = limiterTarget;
        else plugin.voxLimiterGain += (limiterTarget - plugin.voxLimiterGain) * limiterRelease;
        for (uint32_t ch = 0; ch < channels; ++ch) {
            if (outputs[ch]) outputs[ch][frame] = s3g::flushDenormal(std::clamp(outputs[ch][frame] * plugin.voxLimiterGain, -0.98f, 0.98f));
        }
    }
    plugin.voxNoiseState = noiseState;
#if defined(__APPLE__)
    const uint32_t phraseCount = std::min<uint32_t>(
        plugin.voxBankCompiledCount.load(std::memory_order_acquire), kVoxCompiledMaxFrames);
    if (phraseCount > 0u) {
        plugin.guiVoxPhraseEvent.store(
            plugin.voxBankVoiceCurrentPhraseIndex[0] % phraseCount, std::memory_order_relaxed);
        const double total = std::max(1.0, plugin.voxBankVoicePhraseSamplesTotal[0]);
        plugin.guiVoxPhraseProgress.store(static_cast<float>(std::clamp(
            1.0 - plugin.voxBankVoicePhraseSamplesRemaining[0] / total, 0.0, 1.0)),
            std::memory_order_relaxed);
        for (uint32_t voice = 0u; voice < s3g::kAmbiVoxMaxVoices; ++voice) {
            const double voiceTotal = std::max(1.0, plugin.voxBankVoicePhraseSamplesTotal[voice]);
            const float eventProgress = static_cast<float>(std::clamp(
                1.0 - plugin.voxBankVoicePhraseSamplesRemaining[voice] / voiceTotal,
                0.0, 1.0));
            const float phrasePhase = (static_cast<float>(
                plugin.voxBankVoiceCurrentPhraseIndex[voice] % phraseCount) + eventProgress)
                / static_cast<float>(phraseCount);
            plugin.guiVoxPhrasePhase[voice].store(
                s3g::ambiVotFract(phrasePhase), std::memory_order_relaxed);
        }
    } else {
        plugin.guiVoxPhraseEvent.store(0u, std::memory_order_relaxed);
        plugin.guiVoxPhraseProgress.store(0.0f, std::memory_order_relaxed);
        for (auto& phase : plugin.guiVoxPhrasePhase) {
            phase.store(0.0f, std::memory_order_relaxed);
        }
    }
#endif
    return true;
}

void applyVoxPostProcess(Plugin& plugin, float* const* outputs, uint32_t channels, uint32_t frames)
{
    if (const auto bank = std::atomic_load_explicit(&plugin.voicebank, std::memory_order_acquire)) {
        if (applyVoicebankPostProcess(plugin, *bank, outputs, channels, frames)) return;
    }
    if (const auto world = std::atomic_load_explicit(&plugin.worldSample, std::memory_order_acquire)) {
        if (applyWorldPostProcess(plugin, *world, outputs, channels, frames)) return;
    }
    const auto vox = plugin.vox;
    const VoxSpeechMode speechMode = vox.speechMode;
    const bool speakMode = speechMode == VoxSpeechMode::Speak;
    const bool singMode = speechMode == VoxSpeechMode::Sing;
    const VoxCircuitMode circuitMode = vox.circuitMode;
    const float formantMacro = std::clamp(vox.formantMacro, -1.0f, 1.0f);
    const float bendMacro = std::clamp(vox.bendMacro, 0.0f, 1.0f);
    const float crushMacro = std::clamp(vox.crushMacro, 0.0f, 1.0f);
    const float rasp = std::clamp(vox.rasp * (speakMode ? 0.55f : 1.0f), 0.0f, 1.0f);
    const float breath = std::clamp(vox.breath * (speakMode ? 0.70f : 1.0f), 0.0f, 1.0f);
    const float throat = std::clamp(vox.throat + (speakMode ? 0.08f : 0.0f), 0.0f, 1.0f);
    const float drive = std::clamp(vox.drive * (speakMode ? 0.45f : 1.0f), 0.0f, 1.0f);
    const float articulation = std::clamp(vox.articulation + (speakMode ? 0.46f : (singMode ? 0.18f : 0.0f)), 0.0f, 1.0f);
    const float consonant = std::clamp(vox.consonant + (speakMode ? 0.44f : (singMode ? 0.18f : 0.0f)), 0.0f, 1.0f);
    const float plosive = std::clamp(vox.plosive + (speakMode ? 0.20f : (singMode ? 0.08f : 0.0f)), 0.0f, 1.0f);
    const float sibilance = std::clamp(vox.sibilance + (speakMode ? 0.34f : (singMode ? 0.12f : 0.0f)), 0.0f, 1.0f);
    const float phraseRate = std::clamp(vox.phraseRate * (speakMode ? 0.58f : 1.0f), 0.0f, 1.0f);
    if (channels == 0u || !outputs) return;

    uint32_t noiseState = plugin.voxNoiseState;
    const float consonantAttack = 1.0f - std::exp(-1.0f / (0.014f * static_cast<float>(plugin.sampleRate)));
    const float plosiveAttack = 1.0f - std::exp(-1.0f / (0.010f * static_cast<float>(plugin.sampleRate)));
    const float consonantDecay = std::exp(-1.0f / ((0.115f + articulation * 0.070f) * static_cast<float>(plugin.sampleRate)));
    const float plosiveDecay = std::exp(-1.0f / ((0.060f + plosive * 0.040f) * static_cast<float>(plugin.sampleRate)));
    const float vowelDecay = std::exp(-1.0f / ((0.180f + articulation * 0.120f) * static_cast<float>(plugin.sampleRate)));
    const float carrierAttack = 1.0f - std::exp(-1.0f / (0.018f * static_cast<float>(plugin.sampleRate)));
    const float carrierRelease = 1.0f - std::exp(-1.0f / (0.180f * static_cast<float>(plugin.sampleRate)));
    const float phraseHz = speakMode ? (4.5f + phraseRate * 10.5f) : (0.55f + phraseRate * 8.45f);
    const float phraseStep = phraseHz / static_cast<float>(plugin.sampleRate);
    const float f0Smooth = 1.0f - std::exp(-1.0f / (0.050f * static_cast<float>(plugin.sampleRate)));
    const float formantSmooth = 1.0f - std::exp(-1.0f / (0.035f * static_cast<float>(plugin.sampleRate)));
    const float outSmooth = 1.0f - std::exp(-1.0f / (0.004f * static_cast<float>(plugin.sampleRate)));
    const float consonantMixBase = std::clamp(consonant * 0.75f + sibilance * 0.25f, 0.0f, 1.0f);
    const float outputGainTarget = std::min(s3g::dbToGain(plugin.params.outputGainDb), s3g::dbToGain(12.0f));
    const float outputGainSmooth = 1.0f - std::exp(-1.0f / (0.020f * static_cast<float>(plugin.sampleRate)));
    const float limiterRelease = 1.0f - std::exp(-1.0f / (0.120f * static_cast<float>(plugin.sampleRate)));
    const uint32_t activeVoices = std::clamp<uint32_t>(plugin.params.voices, 1u, s3g::kAmbiVoxMaxVoices);
    const uint32_t renderVoices = activeVoices;
    const float voiceGain = (speakMode ? 0.92f : (singMode ? 0.72f : 1.0f)) / std::sqrt(static_cast<float>(activeVoices));
    const bool midiMode = plugin.params.mode == s3g::AmbiVotMode::Midi;
    bool midiActive = false;
    for (uint32_t voice = 0u; voice < activeVoices; ++voice) midiActive = midiActive || plugin.voxMidiGate[voice];
    const auto& motionPoints = plugin.engine.motionPoints();
    std::array<std::array<float, s3g::kAmbiSpeakerDecoderMaxChannels>, s3g::kAmbiVoxMaxVoices> voiceBasis {};
    std::array<float, s3g::kAmbiVoxMaxVoices> distanceGains {};
    std::array<float, s3g::kAmbiVoxMaxVoices> targetF0 {};
    for (uint32_t voice = 0; voice < renderVoices; ++voice) {
        const auto& point = motionPoints[voice];
        voiceBasis[voice] = s3g::acnSn3dBasis7(s3g::directionFromAed(point.azimuthDeg, point.elevationDeg));
        distanceGains[voice] = speakMode ? 1.0f : 1.0f / std::max(0.35f, point.distance);
        targetF0[voice] = std::clamp(
            s3g::ambiVotMidiToHz(voxVoiceTargetNote(plugin, voice)),
            20.0f, static_cast<float>(plugin.sampleRate) * 0.42f);
    }
    std::array<VoxLpcFrame, s3g::kAmbiVoxMaxVoices> voiceLpcFrames {};
    const auto refreshVoiceLpcFrames = [&]() {
        for (uint32_t voice = 0u; voice < renderVoices; ++voice) {
            voiceLpcFrames[voice] = voxLpcFrameForVoice(plugin, voice, activeVoices);
        }
    };
    refreshVoiceLpcFrames();
    for (uint32_t frame = 0; frame < frames; ++frame) {
        const float freeCarrier = midiMode ? 0.0f : 0.86f;
        const float carrierTarget = midiMode ? (midiActive ? 0.86f : 0.0f) : freeCarrier;
        plugin.voxCarrierEnv += (carrierTarget - plugin.voxCarrierEnv)
            * (carrierTarget > plugin.voxCarrierEnv ? carrierAttack : carrierRelease);
        const float carrierGate = std::clamp((plugin.voxCarrierEnv - 0.004f) * 1.18f, 0.0f, 1.0f);
        const float phraseGate = carrierGate;
        if (plugin.params.mode != s3g::AmbiVotMode::Midi && articulation > 0.001f && phraseGate > 0.02f) {
            plugin.voxPhrasePhase += phraseStep;
            if (plugin.voxPhrasePhase >= 1.0f) {
                plugin.voxPhrasePhase -= std::floor(plugin.voxPhrasePhase);
                advanceVoxPhrase(plugin, phraseGate * (0.38f + 0.62f * phraseRate));
                refreshVoiceLpcFrames();
            }
        }
        const float noise = voxUnitNoise(noiseState);
        plugin.voxConsonantLevel += (plugin.voxConsonantEnv - plugin.voxConsonantLevel) * consonantAttack;
        plugin.voxPlosiveLevel += (plugin.voxPlosiveEnv - plugin.voxPlosiveLevel) * plosiveAttack;
        plugin.voxFricativeState += (noise - plugin.voxFricativeState) * 0.10f;
        const float fricative = noise - plugin.voxFricativeState;
        const float sibilant = fricative - plugin.voxSibilantState * 0.45f;
        plugin.voxSibilantState = noise;
        for (uint32_t ch = 0; ch < channels; ++ch) {
            if (!outputs[ch]) continue;
            outputs[ch][frame] = 0.0f;
        }
        plugin.voxOutputGainSmooth += (outputGainTarget - plugin.voxOutputGainSmooth) * outputGainSmooth;
        if (carrierGate > 0.0001f) {
            for (uint32_t voice = 0; voice < renderVoices; ++voice) {
                const VoxLpcFrame& lpcFrame = voiceLpcFrames[voice];
                const float voiceSkew = static_cast<float>(voice) / static_cast<float>(std::max<uint32_t>(1u, activeVoices - 1u));
                const float bendSign = (voice & 1u) == 0u ? 1.0f : -1.0f;
                const float circuitPitch = circuitMode == VoxCircuitMode::Bent
                    ? 1.0f + bendSign * bendMacro * (0.012f + 0.020f * voiceSkew)
                    : (circuitMode == VoxCircuitMode::Chip ? 1.0f + std::round((voiceSkew - 0.5f) * 6.0f) * bendMacro * 0.015f
                        : (circuitMode == VoxCircuitMode::Broken ? 1.0f + bendSign * bendMacro * 0.045f * voxUnitNoise(noiseState) : 1.0f));
                plugin.voxLpcF0[voice] += (targetF0[voice] * lpcFrame.pitchMul * circuitPitch - plugin.voxLpcF0[voice]) * f0Smooth;
                const float phaseStep = plugin.voxLpcF0[voice] / static_cast<float>(plugin.sampleRate);
                plugin.voxLpcPhase[voice] += phaseStep;
                if (plugin.voxLpcPhase[voice] >= 1.0f) {
                    plugin.voxLpcPhase[voice] -= std::floor(plugin.voxLpcPhase[voice]);
                    plugin.voxLpcPulse[voice] = 1.0f;
                }
                plugin.voxLpcPulse[voice] *= 0.992f - rasp * 0.032f;
                const float phase = plugin.voxLpcPhase[voice];
                const float glottalSine = std::sin(2.0f * s3g::kPi * phase);
                const float glottalSaw = 1.0f - 2.0f * phase;
                const float glottalPulse = plugin.voxLpcPulse[voice] - 0.18f;
                float voiced = 0.42f * glottalSine + 0.24f * glottalSaw + 0.48f * glottalPulse;
                plugin.voxLpcTilt[voice] += (voiced - plugin.voxLpcTilt[voice]) * (0.035f + throat * 0.055f);
                voiced = s3g::flushDenormal(voiced - plugin.voxLpcTilt[voice] * (0.34f + throat * 0.28f));
                const float consonantMix = speakMode
                    ? std::clamp(lpcFrame.noise * 1.08f + consonantMixBase * plugin.voxConsonantLevel * 0.50f, 0.0f, 1.0f)
                    : std::clamp(consonantMixBase * plugin.voxConsonantLevel + lpcFrame.noise * 0.35f, 0.0f, 1.0f);
                const float voiceNoise = voxUnitNoise(noiseState);
                const float unvoiced = (sibilant * 0.6f + voiceNoise * 0.4f) * (0.28f + sibilance * 0.42f)
                    + voiceNoise * breath * 0.16f;
                const float plosiveHit = lpcFrame.plosive * (0.18f + throat * 0.24f);
                const float voicedWeight = speakMode ? std::clamp(1.0f - lpcFrame.noise * 1.18f, 0.0f, 1.0f) : (1.0f - consonantMix * 0.62f);
                const float excitation = carrierGate * lpcFrame.energy * (voicedWeight * voiced
                    + consonantMix * unvoiced + plosiveHit);
                const float vowelEnv = carrierGate * (0.62f + std::clamp(plugin.voxPhraseVowelEnv * articulation, 0.0f, 1.0f) * 0.38f);
                const float formantScale = std::pow(2.0f, formantMacro * 0.36f);
                const float bendFormant = circuitMode == VoxCircuitMode::Clean ? 1.0f
                    : (1.0f + bendMacro * (0.10f * std::sin(plugin.voxLpcPhase[voice] * 2.0f * s3g::kPi + voiceSkew * 4.7f)
                        + (circuitMode == VoxCircuitMode::Broken ? 0.06f * voxUnitNoise(noiseState) : 0.0f)));
                const float f1Target = lpcFrame.f1 * formantScale * bendFormant * (speakMode ? 1.0f : (0.96f + voiceSkew * 0.08f));
                const float f2Target = lpcFrame.f2 * formantScale * (2.0f - bendFormant) * (speakMode ? 1.0f : (0.94f + voiceSkew * 0.12f));
                const float f3Target = lpcFrame.f3 * formantScale * (circuitMode == VoxCircuitMode::Chip ? (1.0f + bendMacro * 0.18f) : 1.0f) * (speakMode ? 1.0f : (0.96f + voiceSkew * 0.10f));
                plugin.voxFormant1Hz[voice] += (f1Target - plugin.voxFormant1Hz[voice]) * formantSmooth;
                plugin.voxFormant2Hz[voice] += (f2Target - plugin.voxFormant2Hz[voice]) * formantSmooth;
                plugin.voxFormant3Hz[voice] += (f3Target - plugin.voxFormant3Hz[voice]) * formantSmooth;
                const float bp1 = voxBandpass(excitation, plugin.voxFormant1Hz[voice], 0.72f + throat * 0.16f, plugin.sampleRate, plugin.voxFormant1Low[voice], plugin.voxFormant1Band[voice]);
                const float bp2 = voxBandpass(excitation, plugin.voxFormant2Hz[voice], 0.62f, plugin.sampleRate, plugin.voxFormant2Low[voice], plugin.voxFormant2Band[voice]);
                const float bp3 = voxBandpass(excitation + unvoiced * 0.04f, plugin.voxFormant3Hz[voice], 0.58f, plugin.sampleRate, plugin.voxFormant3Low[voice], plugin.voxFormant3Band[voice]);
                float lpcVoice = (bp1 * (1.05f + plugin.voxPhraseVowelV * 0.35f)
                    + bp2 * (0.82f + plugin.voxPhraseVowelU * 0.32f)
                    + bp3 * (0.24f + sibilance * 0.24f)
                    + excitation * (0.18f + throat * 0.12f)) * vowelEnv;
                lpcVoice += carrierGate * unvoiced * plugin.voxPhraseBreathEnv * (0.025f + sibilance * 0.055f);
                lpcVoice = voxSoftSat(lpcVoice * (2.20f + drive * 3.6f + rasp * 1.7f));
                if (circuitMode != VoxCircuitMode::Clean || crushMacro > 0.0001f) {
                    const float totalCrush = std::clamp(crushMacro + (circuitMode == VoxCircuitMode::Chip ? 0.30f : 0.0f)
                        + (circuitMode == VoxCircuitMode::Broken ? 0.46f * bendMacro : 0.0f), 0.0f, 1.0f);
                    const float steps = std::max(4.0f, 256.0f * std::pow(1.0f / 64.0f, totalCrush));
                    lpcVoice = std::round(lpcVoice * steps) / steps;
                }
                plugin.voxLpcOut[voice] += (lpcVoice - plugin.voxLpcOut[voice]) * outSmooth;
                const float midiVoiceGain = voxVoiceActivityTarget(plugin, voice, activeVoices);
                lpcVoice = plugin.voxLpcOut[voice] * voiceGain * midiVoiceGain * distanceGains[voice]
                    * plugin.voxOutputGainSmooth * (speakMode ? 2.25f : 2.90f);
                for (uint32_t ch = 0; ch < channels; ++ch) {
                    if (!outputs[ch]) continue;
                    const float encoded = ch < voiceBasis[voice].size() ? voiceBasis[voice][ch] : 0.0f;
                    outputs[ch][frame] = s3g::flushDenormal(std::clamp(outputs[ch][frame] + lpcVoice * encoded, -1.2f, 1.2f));
                }
            }
        }
        float framePeak = 0.0f;
        for (uint32_t ch = 0; ch < channels; ++ch) {
            if (outputs[ch]) framePeak = std::max(framePeak, std::fabs(outputs[ch][frame]));
        }
        const float limiterTarget = framePeak > 0.89f ? 0.89f / std::max(0.000001f, framePeak) : 1.0f;
        if (limiterTarget < plugin.voxLimiterGain) plugin.voxLimiterGain = limiterTarget;
        else plugin.voxLimiterGain += (limiterTarget - plugin.voxLimiterGain) * limiterRelease;
        for (uint32_t ch = 0; ch < channels; ++ch) {
            if (outputs[ch]) outputs[ch][frame] = s3g::flushDenormal(std::clamp(outputs[ch][frame] * plugin.voxLimiterGain, -0.98f, 0.98f));
        }
        plugin.voxConsonantEnv *= consonantDecay;
        plugin.voxPlosiveEnv *= plosiveDecay;
        plugin.voxPhraseBreathEnv *= consonantDecay;
        plugin.voxPhraseVowelEnv *= vowelDecay;
        if (carrierGate <= 0.001f) {
            plugin.voxConsonantEnv *= 0.94f;
            plugin.voxPlosiveEnv *= 0.92f;
            plugin.voxPhraseBreathEnv *= 0.94f;
            plugin.voxPhraseVowelEnv *= 0.97f;
        }
        plugin.voxConsonantLevel *= 0.99985f;
        plugin.voxPlosiveLevel *= 0.99980f;
        if (plugin.voxConsonantEnv < 0.000001f) plugin.voxConsonantEnv = 0.0f;
        if (plugin.voxPlosiveEnv < 0.000001f) plugin.voxPlosiveEnv = 0.0f;
        if (plugin.voxPhraseBreathEnv < 0.000001f) plugin.voxPhraseBreathEnv = 0.0f;
        if (plugin.voxPhraseVowelEnv < 0.000001f) plugin.voxPhraseVowelEnv = 0.0f;
    }
    plugin.voxNoiseState = noiseState;
#if defined(__APPLE__)
    const uint32_t phraseCount = std::min<uint32_t>(
        plugin.voxCompiledCount.load(std::memory_order_acquire), kVoxCompiledMaxFrames);
    for (uint32_t voice = 0u; voice < s3g::kAmbiVoxMaxVoices; ++voice) {
        const uint32_t offset = s3g::ambiVoxPhraseSpreadIndex(
            plugin.vox.orchestration, voice, activeVoices, phraseCount,
            plugin.vox.phraseSpread);
        const float phase = phraseCount > 0u
            ? (static_cast<float>((plugin.voxPhraseCurrentIndex + offset) % phraseCount)
                + plugin.voxPhrasePhase) / static_cast<float>(phraseCount)
            : 0.0f;
        plugin.guiVoxPhrasePhase[voice].store(
            s3g::ambiVotFract(phase), std::memory_order_relaxed);
    }
#endif
}

bool writeExact(const clap_ostream_t* stream, const void* data, size_t size)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t done = 0;
    while (done < size) {
        const int64_t count = stream->write(stream, bytes + done, size - done);
        if (count <= 0) return false;
        done += static_cast<size_t>(count);
    }
    return true;
}

bool readExact(const clap_istream_t* stream, void* data, size_t size)
{
    auto* bytes = static_cast<uint8_t*>(data);
    size_t done = 0;
    while (done < size) {
        const int64_t count = stream->read(stream, bytes + done, size - done);
        if (count <= 0) return false;
        done += static_cast<size_t>(count);
    }
    return true;
}

s3g::AmbiVotParams migrateLegacy(const LegacyParamsV1& old)
{
    s3g::AmbiVotParams params {};
    params.order = old.order;
    params.voices = old.voices;
    params.mode = old.mode;
    params.preset = old.preset;
    params.baseNote = old.baseNote;
    params.tuneCents = old.tuneCents;
    params.vectorX = old.vectorX;
    params.vectorY = old.vectorY;
    params.scan = old.scan;
    params.scanRate = 1.0f;
    params.morph = old.morph;
    params.detune = old.detune;
    params.motionSpread = old.spread;
    params.motionRateHz = std::max(0.001f, std::fabs(old.driftHz));
    params.attackMs = old.attackMs;
    params.decayMs = 220.0f;
    params.sustain = 1.0f;
    params.releaseMs = old.releaseMs;
    params.outputGainDb = old.outputGainDb;
    params.scoreMode = s3g::AmbiVotScoreMode::Off;
    return params;
}

s3g::AmbiVotParams migrateV2(const AmbiVotParamsV2& old)
{
    s3g::AmbiVotParams params {};
    params.order = old.order;
    params.voices = old.voices;
    params.mode = old.mode;
    params.preset = old.preset;
    params.baseNote = old.baseNote;
    params.tuneCents = old.tuneCents;
    params.vectorX = old.vectorX;
    params.vectorY = old.vectorY;
    params.scan = old.scan;
    params.scanRate = old.scanRate;
    params.morph = old.morph;
    params.detune = old.detune;
    params.motionSpread = old.motionSpread;
    params.motionRateHz = old.motionRateHz;
    params.attackMs = old.attackMs;
    params.decayMs = 220.0f;
    params.sustain = 1.0f;
    params.releaseMs = old.releaseMs;
    params.outputGainDb = old.outputGainDb;
    params.motionScene = old.motionScene;
    params.motionClock = old.motionClock;
    params.syncDivisionBeats = old.syncDivisionBeats;
    params.motionAmount = old.motionAmount;
    params.motionCoherence = old.motionCoherence;
    params.motionChaos = old.motionChaos;
    params.motionLink = old.motionLink;
    params.motionSmooth = old.motionSmooth;
    params.centerAzimuthDeg = old.centerAzimuthDeg;
    params.centerElevationDeg = old.centerElevationDeg;
    params.centerDistance = old.centerDistance;
    params.scoreMode = s3g::AmbiVotScoreMode::Off;
    return params;
}

s3g::AmbiVotParams migrateV3(const AmbiVotParamsV3& old)
{
    s3g::AmbiVotParams params {};
    params.order = old.order;
    params.voices = old.voices;
    params.mode = old.mode;
    params.preset = old.preset;
    params.baseNote = old.baseNote;
    params.tuneCents = old.tuneCents;
    params.vectorX = old.vectorX;
    params.vectorY = old.vectorY;
    params.scan = old.scan;
    params.scanRate = old.scanRate;
    params.morph = old.morph;
    params.detune = old.detune;
    params.motionSpread = old.motionSpread;
    params.motionRateHz = old.motionRateHz;
    params.attackMs = old.attackMs;
    params.decayMs = 220.0f;
    params.sustain = 1.0f;
    params.releaseMs = old.releaseMs;
    params.outputGainDb = old.outputGainDb;
    params.motionScene = old.motionScene;
    params.motionClock = old.motionClock;
    params.syncDivisionBeats = old.syncDivisionBeats;
    params.motionAmount = old.motionAmount;
    params.motionCoherence = old.motionCoherence;
    params.motionChaos = old.motionChaos;
    params.motionLink = old.motionLink;
    params.motionSmooth = old.motionSmooth;
    params.centerAzimuthDeg = old.centerAzimuthDeg;
    params.centerElevationDeg = old.centerElevationDeg;
    params.centerDistance = old.centerDistance;
    params.neighborRadius = old.neighborRadius;
    params.requiredNeighbors = old.requiredNeighbors;
    params.scoreMode = s3g::AmbiVotScoreMode::Off;
    return params;
}

s3g::AmbiVotParams migrateV4(const AmbiVotParamsV4& old)
{
    s3g::AmbiVotParams params {};
    params.order = old.order;
    params.voices = old.voices;
    params.mode = old.mode;
    params.preset = old.preset;
    params.baseNote = old.baseNote;
    params.tuneCents = old.tuneCents;
    params.vectorX = old.vectorX;
    params.vectorY = old.vectorY;
    params.scan = old.scan;
    params.scanRate = old.scanRate;
    params.morph = old.morph;
    params.detune = old.detune;
    params.motionSpread = old.motionSpread;
    params.motionRateHz = old.motionRateHz;
    params.attackMs = old.attackMs;
    params.decayMs = old.decayMs;
    params.sustain = old.sustain;
    params.releaseMs = old.releaseMs;
    params.outputGainDb = old.outputGainDb;
    params.motionScene = old.motionScene;
    params.motionClock = old.motionClock;
    params.syncDivisionBeats = old.syncDivisionBeats;
    params.motionAmount = old.motionAmount;
    params.motionCoherence = old.motionCoherence;
    params.motionChaos = old.motionChaos;
    params.motionLink = old.motionLink;
    params.motionSmooth = old.motionSmooth;
    params.centerAzimuthDeg = old.centerAzimuthDeg;
    params.centerElevationDeg = old.centerElevationDeg;
    params.centerDistance = old.centerDistance;
    params.neighborRadius = old.neighborRadius;
    params.requiredNeighbors = old.requiredNeighbors;
    params.scoreMode = s3g::AmbiVotScoreMode::Off;
    return params;
}

void migrateVoxV9(Plugin& plugin, const VoxParamsV9& old)
{
    plugin.vox.rasp = old.rasp;
    plugin.vox.breath = old.breath;
    plugin.vox.throat = old.throat;
    plugin.vox.drive = old.drive;
    plugin.vox.jitter = old.jitter;
    plugin.vox.vowelSpread = old.vowelSpread;
    plugin.vox.pitchScoop = old.pitchScoop;
    plugin.vox.attackShape = old.attackShape;
    plugin.vox.articulation = old.articulation;
    plugin.vox.consonant = old.consonant;
    plugin.vox.plosive = old.plosive;
    plugin.vox.sibilance = old.sibilance;
    plugin.vox.phraseRate = old.phraseRate;
    plugin.vox.speechMode = VoxSpeechMode::Texture;
    plugin.vox.circuitMode = VoxCircuitMode::Clean;
    plugin.vox.formantMacro = 0.0f;
    plugin.vox.bendMacro = 0.0f;
    plugin.vox.crushMacro = 0.0f;
    plugin.vox.worldRate = 0.50f;
    plugin.vox.worldPitchCents = 0.0f;
    plugin.vox.worldLoopStart = 0.0f;
    plugin.vox.worldLoopEnd = 1.0f;
    plugin.vox.worldVoiceSpread = 0.08f;
    plugin.vox.worldFreeze = 0.0f;
    plugin.vox.worldScrub = 0.0f;
    plugin.vox.worldVoicing = 0.50f;
    plugin.vox.worldVoiceDeviation = 0.0f;
}

void migrateVoxV10(Plugin& plugin, const VoxParamsV10& old)
{
    plugin.vox.rasp = old.rasp;
    plugin.vox.breath = old.breath;
    plugin.vox.throat = old.throat;
    plugin.vox.drive = old.drive;
    plugin.vox.jitter = old.jitter;
    plugin.vox.vowelSpread = old.vowelSpread;
    plugin.vox.pitchScoop = old.pitchScoop;
    plugin.vox.attackShape = old.attackShape;
    plugin.vox.articulation = old.articulation;
    plugin.vox.consonant = old.consonant;
    plugin.vox.plosive = old.plosive;
    plugin.vox.sibilance = old.sibilance;
    plugin.vox.phraseRate = old.phraseRate;
    plugin.vox.speechMode = old.speechMode;
    plugin.vox.circuitMode = VoxCircuitMode::Clean;
    plugin.vox.formantMacro = 0.0f;
    plugin.vox.bendMacro = 0.0f;
    plugin.vox.crushMacro = 0.0f;
    plugin.vox.worldRate = 0.50f;
    plugin.vox.worldPitchCents = 0.0f;
    plugin.vox.worldLoopStart = 0.0f;
    plugin.vox.worldLoopEnd = 1.0f;
    plugin.vox.worldVoiceSpread = 0.08f;
    plugin.vox.worldFreeze = 0.0f;
    plugin.vox.worldScrub = 0.0f;
    plugin.vox.worldVoicing = 0.50f;
    plugin.vox.worldVoiceDeviation = 0.0f;
}

void migrateVoxV11(Plugin& plugin, const VoxParamsV11& old)
{
    plugin.vox.rasp = old.rasp;
    plugin.vox.breath = old.breath;
    plugin.vox.throat = old.throat;
    plugin.vox.drive = old.drive;
    plugin.vox.jitter = old.jitter;
    plugin.vox.vowelSpread = old.vowelSpread;
    plugin.vox.pitchScoop = old.pitchScoop;
    plugin.vox.attackShape = old.attackShape;
    plugin.vox.articulation = old.articulation;
    plugin.vox.consonant = old.consonant;
    plugin.vox.plosive = old.plosive;
    plugin.vox.sibilance = old.sibilance;
    plugin.vox.phraseRate = old.phraseRate;
    plugin.vox.speechMode = old.speechMode;
    plugin.vox.circuitMode = old.circuitMode;
    plugin.vox.formantMacro = old.formantMacro;
    plugin.vox.bendMacro = old.bendMacro;
    plugin.vox.crushMacro = old.crushMacro;
    plugin.vox.worldRate = 0.50f;
    plugin.vox.worldPitchCents = 0.0f;
    plugin.vox.worldLoopStart = 0.0f;
    plugin.vox.worldLoopEnd = 1.0f;
    plugin.vox.worldVoiceSpread = 0.08f;
    plugin.vox.worldFreeze = 0.0f;
    plugin.vox.worldScrub = 0.0f;
    plugin.vox.worldVoicing = 0.50f;
    plugin.vox.worldVoiceDeviation = 0.0f;
}

void migrateVoxV12(Plugin& plugin, const VoxParamsV12& old)
{
    plugin.vox.rasp = old.rasp;
    plugin.vox.breath = old.breath;
    plugin.vox.throat = old.throat;
    plugin.vox.drive = old.drive;
    plugin.vox.jitter = old.jitter;
    plugin.vox.vowelSpread = old.vowelSpread;
    plugin.vox.pitchScoop = old.pitchScoop;
    plugin.vox.attackShape = old.attackShape;
    plugin.vox.articulation = old.articulation;
    plugin.vox.consonant = old.consonant;
    plugin.vox.plosive = old.plosive;
    plugin.vox.sibilance = old.sibilance;
    plugin.vox.phraseRate = old.phraseRate;
    plugin.vox.speechMode = old.speechMode;
    plugin.vox.circuitMode = old.circuitMode;
    plugin.vox.formantMacro = old.formantMacro;
    plugin.vox.bendMacro = old.bendMacro;
    plugin.vox.crushMacro = old.crushMacro;
    plugin.vox.worldRate = old.worldRate;
    plugin.vox.worldPitchCents = old.worldPitchCents;
    plugin.vox.worldLoopStart = old.worldLoopStart;
    plugin.vox.worldLoopEnd = old.worldLoopEnd;
    plugin.vox.worldVoiceSpread = 0.08f;
    plugin.vox.worldFreeze = 0.0f;
    plugin.vox.worldScrub = 0.0f;
    plugin.vox.worldVoicing = 0.50f;
    plugin.vox.worldVoiceDeviation = 0.0f;
}

void migrateVoxV13(Plugin& plugin, const VoxParamsV13& old)
{
    plugin.vox.rasp = old.rasp;
    plugin.vox.breath = old.breath;
    plugin.vox.throat = old.throat;
    plugin.vox.drive = old.drive;
    plugin.vox.jitter = old.jitter;
    plugin.vox.vowelSpread = old.vowelSpread;
    plugin.vox.pitchScoop = old.pitchScoop;
    plugin.vox.attackShape = old.attackShape;
    plugin.vox.articulation = old.articulation;
    plugin.vox.consonant = old.consonant;
    plugin.vox.plosive = old.plosive;
    plugin.vox.sibilance = old.sibilance;
    plugin.vox.phraseRate = old.phraseRate;
    plugin.vox.speechMode = old.speechMode;
    plugin.vox.circuitMode = old.circuitMode;
    plugin.vox.formantMacro = old.formantMacro;
    plugin.vox.bendMacro = old.bendMacro;
    plugin.vox.crushMacro = old.crushMacro;
    plugin.vox.worldRate = old.worldRate;
    plugin.vox.worldPitchCents = old.worldPitchCents;
    plugin.vox.worldLoopStart = old.worldLoopStart;
    plugin.vox.worldLoopEnd = old.worldLoopEnd;
    plugin.vox.worldVoiceSpread = std::clamp(old.worldVoiceSpread * 0.25f, 0.0f, 1.0f);
    plugin.vox.worldFreeze = 0.0f;
    plugin.vox.worldScrub = 0.0f;
    plugin.vox.worldVoicing = 0.50f;
    plugin.vox.worldVoiceDeviation = 0.0f;
}

void migrateVoxV14(Plugin& plugin, const VoxParamsV14& old)
{
    plugin.vox.rasp = old.rasp;
    plugin.vox.breath = old.breath;
    plugin.vox.throat = old.throat;
    plugin.vox.drive = old.drive;
    plugin.vox.jitter = old.jitter;
    plugin.vox.vowelSpread = old.vowelSpread;
    plugin.vox.pitchScoop = old.pitchScoop;
    plugin.vox.attackShape = old.attackShape;
    plugin.vox.articulation = old.articulation;
    plugin.vox.consonant = old.consonant;
    plugin.vox.plosive = old.plosive;
    plugin.vox.sibilance = old.sibilance;
    plugin.vox.phraseRate = old.phraseRate;
    plugin.vox.speechMode = old.speechMode;
    plugin.vox.circuitMode = old.circuitMode;
    plugin.vox.formantMacro = old.formantMacro;
    plugin.vox.bendMacro = old.bendMacro;
    plugin.vox.crushMacro = old.crushMacro;
    plugin.vox.worldRate = old.worldRate;
    plugin.vox.worldPitchCents = old.worldPitchCents;
    plugin.vox.worldLoopStart = old.worldLoopStart;
    plugin.vox.worldLoopEnd = old.worldLoopEnd;
    plugin.vox.worldVoiceSpread = std::clamp(old.worldVoiceSpread * 0.25f, 0.0f, 1.0f);
    plugin.vox.worldFreeze = old.worldFreeze;
    plugin.vox.worldScrub = old.worldScrub;
    plugin.vox.worldVoicing = old.worldVoicing;
    plugin.vox.worldVoiceDeviation = 0.0f;
}

void migrateVoxV15(Plugin& plugin, const VoxParamsV15& old)
{
    plugin.vox.rasp = old.rasp;
    plugin.vox.breath = old.breath;
    plugin.vox.throat = old.throat;
    plugin.vox.drive = old.drive;
    plugin.vox.jitter = old.jitter;
    plugin.vox.vowelSpread = old.vowelSpread;
    plugin.vox.pitchScoop = old.pitchScoop;
    plugin.vox.attackShape = old.attackShape;
    plugin.vox.articulation = old.articulation;
    plugin.vox.consonant = old.consonant;
    plugin.vox.plosive = old.plosive;
    plugin.vox.sibilance = old.sibilance;
    plugin.vox.phraseRate = old.phraseRate;
    plugin.vox.speechMode = old.speechMode;
    plugin.vox.circuitMode = old.circuitMode;
    plugin.vox.formantMacro = old.formantMacro;
    plugin.vox.bendMacro = old.bendMacro;
    plugin.vox.crushMacro = old.crushMacro;
    plugin.vox.worldRate = old.worldRate;
    plugin.vox.worldPitchCents = old.worldPitchCents;
    plugin.vox.worldLoopStart = old.worldLoopStart;
    plugin.vox.worldLoopEnd = old.worldLoopEnd;
    plugin.vox.worldVoiceSpread = old.worldVoiceSpread;
    plugin.vox.worldFreeze = old.worldFreeze;
    plugin.vox.worldScrub = old.worldScrub;
    plugin.vox.worldVoicing = old.worldVoicing;
    plugin.vox.worldVoiceDeviation = old.worldVoiceDeviation;
    plugin.vox.pvocStretch = 1.0f;
    plugin.vox.pvocTransient = 0.65f;
}

void migrateVoxV16(Plugin& plugin, const VoxParamsV16& old)
{
    plugin.vox.rasp = old.rasp;
    plugin.vox.breath = old.breath;
    plugin.vox.throat = old.throat;
    plugin.vox.drive = old.drive;
    plugin.vox.jitter = old.jitter;
    plugin.vox.vowelSpread = old.vowelSpread;
    plugin.vox.pitchScoop = old.pitchScoop;
    plugin.vox.attackShape = old.attackShape;
    plugin.vox.articulation = old.articulation;
    plugin.vox.consonant = old.consonant;
    plugin.vox.plosive = old.plosive;
    plugin.vox.sibilance = old.sibilance;
    plugin.vox.phraseRate = old.phraseRate;
    plugin.vox.speechMode = old.speechMode;
    plugin.vox.circuitMode = old.circuitMode;
    plugin.vox.formantMacro = old.formantMacro;
    plugin.vox.bendMacro = old.bendMacro;
    plugin.vox.crushMacro = old.crushMacro;
    plugin.vox.worldRate = old.worldRate;
    plugin.vox.worldPitchCents = old.worldPitchCents;
    plugin.vox.worldLoopStart = old.worldLoopStart;
    plugin.vox.worldLoopEnd = old.worldLoopEnd;
    plugin.vox.worldVoiceSpread = old.worldVoiceSpread;
    plugin.vox.worldFreeze = old.worldFreeze;
    plugin.vox.worldScrub = old.worldScrub;
    plugin.vox.worldVoicing = old.worldVoicing;
    plugin.vox.worldVoiceDeviation = old.worldVoiceDeviation;
    plugin.vox.pvocStretch = old.pvocStretch;
    plugin.vox.pvocTransient = old.pvocTransient;
    plugin.vox.portamento = 0.18f;
    plugin.vox.vibratoDepth = 0.12f;
    plugin.vox.vibratoRateHz = 5.2f;
    plugin.vox.transition = 0.65f;
}

void migrateVoxV18(Plugin& plugin, const VoxParamsV18& old)
{
    plugin.vox.rasp = old.rasp;
    plugin.vox.breath = old.breath;
    plugin.vox.throat = old.throat;
    plugin.vox.drive = old.drive;
    plugin.vox.jitter = old.jitter;
    plugin.vox.vowelSpread = old.vowelSpread;
    plugin.vox.pitchScoop = old.pitchScoop;
    plugin.vox.attackShape = old.attackShape;
    plugin.vox.articulation = old.articulation;
    plugin.vox.consonant = old.consonant;
    plugin.vox.plosive = old.plosive;
    plugin.vox.sibilance = old.sibilance;
    plugin.vox.phraseRate = old.phraseRate;
    plugin.vox.speechMode = old.speechMode;
    plugin.vox.circuitMode = old.circuitMode;
    plugin.vox.formantMacro = old.formantMacro;
    plugin.vox.bendMacro = old.bendMacro;
    plugin.vox.crushMacro = old.crushMacro;
    plugin.vox.worldRate = old.worldRate;
    plugin.vox.worldPitchCents = old.worldPitchCents;
    plugin.vox.worldLoopStart = old.worldLoopStart;
    plugin.vox.worldLoopEnd = old.worldLoopEnd;
    plugin.vox.worldVoiceSpread = old.worldVoiceSpread;
    plugin.vox.worldFreeze = old.worldFreeze;
    plugin.vox.worldScrub = old.worldScrub;
    plugin.vox.worldVoicing = old.worldVoicing;
    plugin.vox.worldVoiceDeviation = old.worldVoiceDeviation;
    plugin.vox.pvocStretch = old.pvocStretch;
    plugin.vox.pvocTransient = old.pvocTransient;
    plugin.vox.portamento = old.portamento;
    plugin.vox.vibratoDepth = old.vibratoDepth;
    plugin.vox.vibratoRateHz = old.vibratoRateHz;
    plugin.vox.transition = old.transition;
    plugin.vox.worldAirColor = 0.0f;
    plugin.vox.orchestration = s3g::AmbiVoxOrchestration::Individual;
    plugin.vox.contourMode = s3g::AmbiVoxContourMode::Original;
}

void migrateVoxV20(Plugin& plugin, const VoxParamsV20& old)
{
    plugin.vox.rasp = old.rasp;
    plugin.vox.breath = old.breath;
    plugin.vox.throat = old.throat;
    plugin.vox.drive = old.drive;
    plugin.vox.jitter = old.jitter;
    plugin.vox.vowelSpread = old.vowelSpread;
    plugin.vox.pitchScoop = old.pitchScoop;
    plugin.vox.attackShape = old.attackShape;
    plugin.vox.articulation = old.articulation;
    plugin.vox.consonant = old.consonant;
    plugin.vox.plosive = old.plosive;
    plugin.vox.sibilance = old.sibilance;
    plugin.vox.phraseRate = old.phraseRate;
    plugin.vox.speechMode = old.speechMode;
    plugin.vox.circuitMode = old.circuitMode;
    plugin.vox.formantMacro = old.formantMacro;
    plugin.vox.bendMacro = old.bendMacro;
    plugin.vox.crushMacro = old.crushMacro;
    plugin.vox.worldRate = old.worldRate;
    plugin.vox.worldPitchCents = old.worldPitchCents;
    plugin.vox.worldLoopStart = old.worldLoopStart;
    plugin.vox.worldLoopEnd = old.worldLoopEnd;
    plugin.vox.worldVoiceSpread = old.worldVoiceSpread;
    plugin.vox.worldFreeze = old.worldFreeze;
    plugin.vox.worldScrub = old.worldScrub;
    plugin.vox.worldVoicing = old.worldVoicing;
    plugin.vox.worldVoiceDeviation = old.worldVoiceDeviation;
    plugin.vox.pvocStretch = old.pvocStretch;
    plugin.vox.pvocTransient = old.pvocTransient;
    plugin.vox.portamento = old.portamento;
    plugin.vox.vibratoDepth = old.vibratoDepth;
    plugin.vox.vibratoRateHz = old.vibratoRateHz;
    plugin.vox.transition = old.transition;
    plugin.vox.worldAirColor = old.worldAirColor;
    plugin.vox.orchestration = old.orchestration;
    plugin.vox.contourMode = old.contourMode;
    plugin.vox.phraseSpread = old.orchestration == s3g::AmbiVoxOrchestration::Round
        ? 1.0f : 0.0f;
}

void applyParam(Plugin& plugin, clap_id id, double value)
{
    switch (id) {
    case kOrderParamId: plugin.params.order = static_cast<uint32_t>(std::lround(value)); break;
    case kVoicesParamId:
        plugin.params.voices = std::clamp<uint32_t>(
            static_cast<uint32_t>(std::lround(value)), 1u, s3g::kAmbiVoxMaxVoices);
        plugin.voxBankTimingResetRequested.store(true, std::memory_order_release);
        plugin.voxWorldTimingResetRequested.store(true, std::memory_order_release);
        requestVoiceDelayResize(plugin);
        break;
    case kModeParamId: plugin.params.mode = static_cast<s3g::AmbiVotMode>(static_cast<uint32_t>(std::lround(value))); break;
    case kPresetParamId: plugin.params.preset = static_cast<s3g::AmbiVotPreset>(static_cast<uint32_t>(std::lround(value))); break;
    case kBaseNoteParamId: plugin.params.baseNote = static_cast<float>(value); break;
    case kTuneParamId: plugin.params.tuneCents = static_cast<float>(value); break;
    case kVectorXParamId: plugin.params.vectorX = static_cast<float>(value); break;
    case kVectorYParamId: plugin.params.vectorY = static_cast<float>(value); break;
    case kScanParamId: plugin.params.scan = static_cast<float>(value); break;
    case kScanRateParamId: plugin.params.scanRate = static_cast<float>(value); break;
    case kMorphParamId: plugin.params.morph = static_cast<float>(value); break;
    case kDetuneParamId: plugin.params.detune = static_cast<float>(value); break;
    case kScaleParamId: plugin.params.scale = static_cast<s3g::AmbiVotScale>(static_cast<uint32_t>(std::lround(value))); break;
    case kPitchSpreadParamId: plugin.params.pitchSpread = static_cast<float>(value); break;
    case kHarmonicsParamId: plugin.params.harmonicAmount = static_cast<float>(value); break;
    case kSubharmonicsParamId: plugin.params.subharmonicAmount = static_cast<float>(value); break;
    case kSpreadParamId: plugin.params.motionSpread = static_cast<float>(value); break;
    case kMotionRateParamId: plugin.params.motionRateHz = static_cast<float>(value); break;
    case kAttackParamId: plugin.params.attackMs = static_cast<float>(value); break;
    case kDecayParamId: plugin.params.decayMs = static_cast<float>(value); break;
    case kSustainParamId: plugin.params.sustain = static_cast<float>(value); break;
    case kReleaseParamId: plugin.params.releaseMs = static_cast<float>(value); break;
    case kOutputParamId: plugin.params.outputGainDb = static_cast<float>(value); break;
    case kMotionSceneParamId: plugin.params.motionScene = static_cast<s3g::AmbiVotMotionScene>(static_cast<uint32_t>(std::lround(value))); break;
    case kMotionClockParamId: plugin.params.motionClock = static_cast<s3g::AmbiVotMotionClock>(static_cast<uint32_t>(std::lround(value))); break;
    case kSyncDivisionParamId: plugin.params.syncDivisionBeats = static_cast<float>(value); break;
    case kMotionAmountParamId: plugin.params.motionAmount = static_cast<float>(value); break;
    case kCoherenceParamId: plugin.params.motionCoherence = static_cast<float>(value); break;
    case kChaosParamId: plugin.params.motionChaos = static_cast<float>(value); break;
    case kLinkParamId: plugin.params.motionLink = static_cast<float>(value); break;
    case kSmoothParamId: plugin.params.motionSmooth = static_cast<float>(value); break;
    case kCenterAzimuthParamId: plugin.params.centerAzimuthDeg = static_cast<float>(value); break;
    case kCenterElevationParamId: plugin.params.centerElevationDeg = static_cast<float>(value); break;
    case kCenterDistanceParamId: plugin.params.centerDistance = static_cast<float>(value); break;
    case kNeighborRadiusParamId: plugin.params.neighborRadius = static_cast<float>(value); break;
    case kRequiredNeighborsParamId: plugin.params.requiredNeighbors = static_cast<uint32_t>(std::lround(value)); break;
    case kScoreModeParamId: plugin.params.scoreMode = static_cast<s3g::AmbiVotScoreMode>(static_cast<uint32_t>(std::lround(value))); break;
    case kScoreDurationParamId: plugin.params.scoreDurationSec = static_cast<float>(value); break;
    case kScoreDepthParamId: plugin.params.scoreDepth = static_cast<float>(value); break;
    case kRaspParamId: plugin.vox.rasp = std::clamp(static_cast<float>(value), 0.0f, 1.0f); break;
    case kBreathParamId: plugin.vox.breath = std::clamp(static_cast<float>(value), 0.0f, 1.0f); break;
    case kThroatParamId: plugin.vox.throat = std::clamp(static_cast<float>(value), 0.0f, 1.0f); break;
    case kDriveParamId: plugin.vox.drive = std::clamp(static_cast<float>(value), 0.0f, 1.0f); break;
    case kJitterParamId: plugin.vox.jitter = std::clamp(static_cast<float>(value), 0.0f, 1.0f); break;
    case kVowelSpreadParamId: plugin.vox.vowelSpread = std::clamp(static_cast<float>(value), 0.0f, 1.0f); break;
    case kPitchScoopParamId: plugin.vox.pitchScoop = std::clamp(static_cast<float>(value), 0.0f, 1.0f); break;
    case kAttackShapeParamId: plugin.vox.attackShape = std::clamp(static_cast<float>(value), 0.0f, 1.0f); break;
    case kArticulationParamId: plugin.vox.articulation = std::clamp(static_cast<float>(value), 0.0f, 1.0f); break;
    case kConsonantParamId: plugin.vox.consonant = std::clamp(static_cast<float>(value), 0.0f, 1.0f); break;
    case kPlosiveParamId: plugin.vox.plosive = std::clamp(static_cast<float>(value), 0.0f, 1.0f); break;
    case kSibilanceParamId: plugin.vox.sibilance = std::clamp(static_cast<float>(value), 0.0f, 1.0f); break;
    case kPhraseRateParamId: plugin.vox.phraseRate = std::clamp(static_cast<float>(value), 0.0f, 1.0f); break;
    case kSpeechModeParamId:
        plugin.vox.speechMode = static_cast<VoxSpeechMode>(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 2u));
        plugin.voxBankTimingResetRequested.store(true, std::memory_order_release);
        break;
    case kCircuitModeParamId: plugin.vox.circuitMode = static_cast<VoxCircuitMode>(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 3u)); break;
    case kFormantMacroParamId: plugin.vox.formantMacro = std::clamp(static_cast<float>(value), -1.0f, 1.0f); break;
    case kBendMacroParamId: plugin.vox.bendMacro = std::clamp(static_cast<float>(value), 0.0f, 1.0f); break;
    case kCrushMacroParamId: plugin.vox.crushMacro = std::clamp(static_cast<float>(value), 0.0f, 1.0f); break;
    case kWorldRateParamId: plugin.vox.worldRate = std::clamp(static_cast<float>(value), 0.0f, 1.0f); break;
    case kWorldPitchParamId: plugin.vox.worldPitchCents = std::clamp(static_cast<float>(value), -2400.0f, 2400.0f); break;
    case kWorldLoopStartParamId:
        plugin.vox.worldLoopStart = std::clamp(static_cast<float>(value), 0.0f, 1.0f);
        if (plugin.vox.worldLoopStart >= plugin.vox.worldLoopEnd - 0.01f) {
            plugin.vox.worldLoopEnd = std::min(1.0f, plugin.vox.worldLoopStart + 0.01f);
        }
        break;
    case kWorldLoopEndParamId:
        plugin.vox.worldLoopEnd = std::clamp(static_cast<float>(value), 0.0f, 1.0f);
        if (plugin.vox.worldLoopEnd <= plugin.vox.worldLoopStart + 0.01f) {
            plugin.vox.worldLoopStart = std::max(0.0f, plugin.vox.worldLoopEnd - 0.01f);
        }
        break;
    case kWorldVoiceSpreadParamId:
        plugin.vox.worldVoiceSpread = std::clamp(static_cast<float>(value), 0.0f, 1.0f);
        requestVoiceDelayResize(plugin);
        break;
    case kWorldVoiceDeviationParamId:
        plugin.vox.worldVoiceDeviation = std::clamp(static_cast<float>(value), 0.0f, 1.0f);
        break;
    case kWorldFreezeParamId: plugin.vox.worldFreeze = std::clamp(static_cast<float>(value), 0.0f, 1.0f); break;
    case kWorldScrubParamId: plugin.vox.worldScrub = std::clamp(static_cast<float>(value), 0.0f, 1.0f); break;
    case kWorldVoicingParamId: plugin.vox.worldVoicing = std::clamp(static_cast<float>(value), 0.0f, 1.0f); break;
    case kPvocStretchParamId: plugin.vox.pvocStretch = std::clamp(static_cast<float>(value), 0.25f, 4.0f); break;
    case kPvocTransientParamId: plugin.vox.pvocTransient = std::clamp(static_cast<float>(value), 0.0f, 1.0f); break;
    case kPortamentoParamId: plugin.vox.portamento = std::clamp(static_cast<float>(value), 0.0f, 1.0f); break;
    case kVibratoDepthParamId: plugin.vox.vibratoDepth = std::clamp(static_cast<float>(value), 0.0f, 1.0f); break;
    case kVibratoRateParamId: plugin.vox.vibratoRateHz = std::clamp(static_cast<float>(value), 0.1f, 12.0f); break;
    case kTransitionParamId: plugin.vox.transition = std::clamp(static_cast<float>(value), 0.0f, 1.0f); break;
    case kWorldAirColorParamId: plugin.vox.worldAirColor = std::clamp(static_cast<float>(value), -1.0f, 1.0f); break;
    case kOrchestrationParamId:
        plugin.vox.orchestration = static_cast<s3g::AmbiVoxOrchestration>(
            std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 5u));
        plugin.voxBankTimingResetRequested.store(true, std::memory_order_release);
        plugin.voxWorldTimingResetRequested.store(true, std::memory_order_release);
        break;
    case kContourModeParamId:
        plugin.vox.contourMode = static_cast<s3g::AmbiVoxContourMode>(
            std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 3u));
        break;
    case kPhraseSpreadParamId:
        {
            const float spread = std::clamp(static_cast<float>(value), 0.0f, 1.0f);
            if (std::fabs(spread - plugin.vox.phraseSpread) > 0.00001f) {
                plugin.vox.phraseSpread = spread;
                plugin.voxBankTimingResetRequested.store(true, std::memory_order_release);
                plugin.voxWorldTimingResetRequested.store(true, std::memory_order_release);
            }
        }
        break;
    case kLyricModeParamId:
        plugin.lyric.mode = static_cast<VoxLyricMode>(
            std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 4u));
        plugin.lyricMidiStepStarted = false;
        plugin.lyricMidiStepCue = plugin.requestedLyricCue.load(std::memory_order_relaxed);
        plugin.lyricAutoSamplesElapsed = 0.0;
        plugin.lyricAutoClockResetRequested.store(false, std::memory_order_release);
        plugin.lyricCueResetRequested.store(true, std::memory_order_release);
        break;
    case kLyricCueBeatsParamId:
        plugin.lyric.cueBeats = std::clamp(static_cast<float>(value), 0.25f, 64.0f);
        break;
    case kLyricCueNoteParamId:
        plugin.lyric.cueBaseNote = std::clamp<uint32_t>(
            static_cast<uint32_t>(std::lround(value)), 0u, 127u);
        break;
    case kLyricCueChannelParamId:
        plugin.lyric.cueChannel = std::clamp<uint32_t>(
            static_cast<uint32_t>(std::lround(value)), 1u, 16u);
        break;
    default: return;
    }
    plugin.engine.setParams(plugin.params);
    plugin.params = plugin.engine.params();
}

uint32_t voxLyricCueCount(const Plugin& plugin)
{
    const auto score = std::atomic_load_explicit(&plugin.lyricScore, std::memory_order_acquire);
    return score ? static_cast<uint32_t>(score->cues.size()) : 0u;
}

bool handleVoxLyricMidiNote(Plugin& plugin, int16_t channel, int16_t note, bool noteOn)
{
    if (plugin.lyric.mode != VoxLyricMode::MidiStep
        && plugin.lyric.mode != VoxLyricMode::MidiCue) return false;
    const int16_t cueChannel = static_cast<int16_t>(
        std::clamp<uint32_t>(plugin.lyric.cueChannel, 1u, 16u) - 1u);
    if (channel != cueChannel) return false;
    if (!noteOn) return true;
    const uint32_t count = voxLyricCueCount(plugin);
    if (count == 0u) return true;
    uint32_t cue = plugin.requestedLyricCue.load(std::memory_order_relaxed) % count;
    if (plugin.lyric.mode == VoxLyricMode::MidiStep) {
        cue = plugin.lyricMidiStepStarted ? (plugin.lyricMidiStepCue + 1u) % count : 0u;
        plugin.lyricMidiStepStarted = true;
        plugin.lyricMidiStepCue = cue;
    } else {
        const int32_t offset = static_cast<int32_t>(note)
            - static_cast<int32_t>(plugin.lyric.cueBaseNote);
        if (offset >= 0 && static_cast<uint32_t>(offset) < count) {
            cue = static_cast<uint32_t>(offset);
        } else {
            return true;
        }
    }
    plugin.requestedLyricCue.store(cue, std::memory_order_release);
    plugin.lyricCueResetRequested.store(true, std::memory_order_release);
    return true;
}

void readEvents(Plugin& plugin, const clap_input_events_t* events)
{
    if (!events) return;
    const uint32_t count = events->size(events);
    for (uint32_t i = 0; i < count; ++i) {
        const clap_event_header_t* event = events->get(events, i);
        if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
        if (event->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param = reinterpret_cast<const clap_event_param_value_t*>(event);
            applyParam(plugin, param->param_id, param->value);
        } else if (event->type == CLAP_EVENT_NOTE_ON || event->type == CLAP_EVENT_NOTE_OFF || event->type == CLAP_EVENT_NOTE_CHOKE) {
            const auto* note = reinterpret_cast<const clap_event_note_t*>(event);
            const bool noteOn = event->type == CLAP_EVENT_NOTE_ON && note->velocity > 0.0;
            if (handleVoxLyricMidiNote(plugin, note->channel, note->key, noteOn)) continue;
            if (event->type == CLAP_EVENT_NOTE_ON && note->velocity > 0.0) {
                plugin.engine.noteOn(note->key, static_cast<float>(note->velocity));
                allocateVoxMidiVoice(plugin, note->channel, note->note_id, note->key,
                    static_cast<float>(note->velocity));
                advanceVoxPhrase(plugin, static_cast<float>(note->velocity));
            } else {
                plugin.engine.noteOff(note->key);
                releaseVoxMidiVoice(plugin, note->channel, note->note_id, note->key);
            }
        } else if (event->type == CLAP_EVENT_MIDI) {
            const auto* midi = reinterpret_cast<const clap_event_midi_t*>(event);
            const uint8_t status = midi->data[0] & 0xf0u;
            const int16_t channel = static_cast<int16_t>(midi->data[0] & 0x0fu);
            const int16_t note = static_cast<int16_t>(midi->data[1] & 0x7fu);
            const float velocity = static_cast<float>(midi->data[2] & 0x7fu) / 127.0f;
            const bool noteOn = status == 0x90u && velocity > 0.0f;
            const bool noteOff = status == 0x80u || (status == 0x90u && velocity <= 0.0f);
            if ((noteOn || noteOff)
                && handleVoxLyricMidiNote(plugin, channel, note, noteOn)) continue;
            if (status == 0x90u && velocity > 0.0f) {
                plugin.engine.noteOn(note, velocity);
                allocateVoxMidiVoice(plugin, channel, -1, note, velocity);
                advanceVoxPhrase(plugin, velocity);
            } else if (status == 0x80u || (status == 0x90u && velocity <= 0.0f)) {
                plugin.engine.noteOff(note);
                releaseVoxMidiVoice(plugin, channel, -1, note);
            } else if (status == 0xb0u && (midi->data[1] == 120u || midi->data[1] == 123u)) {
                for (int midiNote = 0; midiNote < 128; ++midiNote) plugin.engine.noteOff(midiNote);
                for (uint32_t voice = 0u; voice < s3g::kAmbiVoxMaxVoices; ++voice) {
                    plugin.voxMidiGate[voice] = false;
                    plugin.voxMidiVelocity[voice] = 0.0f;
                }
                refreshLastVoxMidiNote(plugin);
            }
        }
    }
}

void updateVoxLyricTransport(Plugin& plugin, const clap_event_transport_t* transport)
{
    if (plugin.lyric.mode != VoxLyricMode::Transport || !transport
        || (transport->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) == 0) return;
    const uint32_t count = voxLyricCueCount(plugin);
    if (count == 0u) return;
    const double beats = static_cast<double>(transport->song_pos_beats)
        / static_cast<double>(CLAP_BEATTIME_FACTOR);
    const double cueBeats = std::max(0.25, static_cast<double>(plugin.lyric.cueBeats));
    const int64_t window = static_cast<int64_t>(std::floor(beats / cueBeats));
    const int64_t wrapped = ((window % static_cast<int64_t>(count))
        + static_cast<int64_t>(count)) % static_cast<int64_t>(count);
    const uint32_t cue = static_cast<uint32_t>(wrapped);
    if (cue == plugin.requestedLyricCue.load(std::memory_order_relaxed)) return;
    plugin.requestedLyricCue.store(cue, std::memory_order_release);
    plugin.lyricCueResetRequested.store(true, std::memory_order_release);
}

void updateVoxLyricAuto(Plugin& plugin,
                        const clap_event_transport_t* transport,
                        uint32_t frames)
{
    if (plugin.lyric.mode != VoxLyricMode::Auto) {
        plugin.lyricAutoSamplesElapsed = 0.0;
        return;
    }
    const uint32_t count = voxLyricCueCount(plugin);
    if (count == 0u || frames == 0u || plugin.sampleRate <= 0.0) return;

    if (plugin.lyricAutoClockResetRequested.exchange(
            false, std::memory_order_acq_rel)) {
        plugin.lyricAutoSamplesElapsed = 0.0;
    }
    double tempo = 120.0;
    if (transport && (transport->flags & CLAP_TRANSPORT_HAS_TEMPO) != 0
        && std::isfinite(transport->tempo) && transport->tempo > 0.0) {
        tempo = std::clamp(transport->tempo, 1.0, 1000.0);
    }
    const double cueSamples = std::max(1.0,
        plugin.sampleRate * std::max(0.25, static_cast<double>(plugin.lyric.cueBeats))
            * 60.0 / tempo);
    plugin.lyricAutoSamplesElapsed += static_cast<double>(frames);
    const uint64_t advances = static_cast<uint64_t>(
        std::floor(plugin.lyricAutoSamplesElapsed / cueSamples));
    if (advances == 0u) return;

    plugin.lyricAutoSamplesElapsed -= static_cast<double>(advances) * cueSamples;
    const uint32_t current = plugin.requestedLyricCue.load(std::memory_order_relaxed) % count;
    const uint32_t cue = (current + static_cast<uint32_t>(advances % count)) % count;
    plugin.requestedLyricCue.store(cue, std::memory_order_release);
    plugin.lyricCueResetRequested.store(true, std::memory_order_release);
}

void updateTransportPhase(Plugin& plugin, const clap_event_transport_t* transport)
{
    if (plugin.params.motionClock != s3g::AmbiVotMotionClock::Sync) {
        plugin.engine.useFreePhase();
        return;
    }
    if (transport && (transport->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0) {
        const double beats = static_cast<double>(transport->song_pos_beats) / static_cast<double>(CLAP_BEATTIME_FACTOR);
        const double division = std::max(0.25, static_cast<double>(plugin.params.syncDivisionBeats));
        double phase = std::fmod(beats / division, 1.0);
        if (phase < 0.0) phase += 1.0;
        plugin.engine.setExternalPhase(static_cast<float>(phase));
    } else {
        plugin.engine.useFreePhase();
    }
}

#if defined(__APPLE__)
void publishMotionSnapshot(Plugin& plugin)
{
    if (!plugin.guiVisible.load(std::memory_order_relaxed)) return;
    const auto& points = plugin.engine.motionPoints();
    const auto& neighborCounts = plugin.engine.neighborCounts();
    const auto& neighborGates = plugin.engine.neighborGates();
    for (uint32_t i = 0; i < s3g::kAmbiVoxMaxVoices; ++i) {
        plugin.guiAzimuth[i].store(points[i].azimuthDeg, std::memory_order_relaxed);
        plugin.guiElevation[i].store(points[i].elevationDeg, std::memory_order_relaxed);
        plugin.guiDistance[i].store(points[i].distance, std::memory_order_relaxed);
        plugin.guiU[i].store(points[i].u, std::memory_order_relaxed);
        plugin.guiV[i].store(points[i].v, std::memory_order_relaxed);
        plugin.guiNeighborCount[i].store(neighborCounts[i], std::memory_order_relaxed);
        plugin.guiNeighborGate[i].store(neighborGates[i], std::memory_order_relaxed);
    }
    plugin.guiMotionPhase.store(plugin.engine.motionPhase(), std::memory_order_relaxed);
}

void guiDestroy(const clap_plugin_t* plugin);
#endif

bool init(const clap_plugin_t* plugin)
{
    auto* state = self(plugin);
    if (state->host && state->host->get_extension) {
        state->hostParams = static_cast<const clap_host_params_t*>(
            state->host->get_extension(state->host, CLAP_EXT_PARAMS));
    }
    return true;
}

void destroy(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    guiDestroy(plugin);
#endif
    delete self(plugin);
}

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t)
{
    auto* state = self(plugin);
    state->sampleRate = sampleRate;
    try {
        auto delays = makeVoiceDelayBank(sampleRate,
            s3g::kAmbiVoxMaxVoices,
            std::clamp(state->vox.worldVoiceSpread, 0.0f, 1.0f));
        std::atomic_store_explicit(&state->voiceDelayBank, std::move(delays), std::memory_order_release);
        state->voiceDelayResizePending.store(false, std::memory_order_release);
    } catch (const std::bad_alloc&) {
        std::atomic_store_explicit(&state->voiceDelayBank,
            std::shared_ptr<VoxVoiceDelayBank> {}, std::memory_order_release);
    }
    state->pvoc.prepare();
    state->engine.prepare(sampleRate);
    state->engine.setParams(state->params);
    state->engine.setScore(loadScore(*state));
    state->lastOutputSample.fill(0.0f);
    state->presetTransitionOffset.fill(0.0f);
    state->presetTransitionFrames = 0u;
    state->presetTransitionRemaining = 0u;
    state->presetTransitionNeedsInit = false;
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* state = self(plugin);
    state->engine.reset();
    state->voxConsonantEnv = 0.0f;
    state->voxConsonantLevel = 0.0f;
    state->voxPlosiveEnv = 0.0f;
    state->voxPlosiveLevel = 0.0f;
    state->voxPhrasePhase = 0.0f;
    state->voxSibilantState = 0.0f;
    state->voxFricativeState = 0.0f;
    state->voxPhraseU = state->params.vectorX;
    state->voxPhraseV = state->params.vectorY;
    state->voxPhraseIndex = 0u;
    state->voxPhraseCurrentIndex = 0u;
    state->voxPhraseHold = 0u;
    state->voxPhraseBreathEnv = 0.0f;
    state->voxPhraseVowelEnv = 0.0f;
    state->voxPhraseVowelU = state->params.vectorX;
    state->voxPhraseVowelV = state->params.vectorY;
    state->voxFrameEnergy = 0.75f;
    state->voxFramePitchMul = 1.0f;
    state->voxFrameNoise = 0.0f;
    state->voxFramePlosive = 0.0f;
    state->voxFrameF1 = 600.0f;
    state->voxFrameF2 = 1500.0f;
    state->voxFrameF3 = 2800.0f;
    state->voxFrameSymbol = 0u;
    state->voxCarrierEnv = 0.0f;
    state->voxOutputGainSmooth = s3g::dbToGain(state->params.outputGainDb);
    state->voxLimiterGain = 1.0f;
    syncWorldSmoothing(*state);
    const uint32_t activeVoices = std::clamp<uint32_t>(state->params.voices, 1u, s3g::kAmbiVoxMaxVoices);
    resetVoxMidiVoices(*state);
    for (uint32_t voice = 0; voice < s3g::kAmbiVoxMaxVoices; ++voice) {
        state->voxWorldPhase[voice] = static_cast<double>(voice) * 997.0;
        state->voxWorldVoiceGainSmooth[voice] = voice < activeVoices ? 1.0f : 0.0f;
        state->voxPitchRatioSmooth[voice] = 1.0f;
        state->voxContourRatioSmooth[voice] = 1.0f;
        state->pvoc.reset(state->voxPvocVoice[voice], nullptr, 0.0);
        state->pvoc.reset(state->voxPvocNextVoice[voice], nullptr, 0.0);
        state->voxTargetNoteSmooth[voice] = voxVoiceTargetNote(*state, voice);
        state->voxVibratoPhase[voice] = s3g::ambiVotFract(static_cast<float>(voice) * 0.3819660113f);
        state->voxNeighborEnv[voice] = 1.0f;
        state->voxLpcPhase[voice] = s3g::ambiVotFract(static_cast<float>(voice) * 0.6180339887f);
        state->voxLpcTilt[voice] = 0.0f;
        state->voxLpcPulse[voice] = 0.0f;
        state->voxLpcF0[voice] = 110.0f;
        state->voxLpcOut[voice] = 0.0f;
        state->voxFormant1Hz[voice] = 600.0f;
        state->voxFormant2Hz[voice] = 1500.0f;
        state->voxFormant3Hz[voice] = 2800.0f;
        state->voxFormant1Low[voice] = 0.0f;
        state->voxFormant1Band[voice] = 0.0f;
        state->voxFormant2Low[voice] = 0.0f;
        state->voxFormant2Band[voice] = 0.0f;
        state->voxFormant3Low[voice] = 0.0f;
        state->voxFormant3Band[voice] = 0.0f;
    }
    if (auto delays = std::atomic_load_explicit(&state->voiceDelayBank, std::memory_order_acquire)) {
        for (auto& line : delays->lines) line.reset();
    }
    state->voxBankTimingResetRequested.store(true, std::memory_order_release);
    state->voxWorldTimingResetRequested.store(true, std::memory_order_release);
    state->lyricMidiStepStarted = false;
    state->lyricMidiStepCue = state->requestedLyricCue.load(std::memory_order_relaxed);
    state->lyricAutoSamplesElapsed = 0.0;
    state->lyricAutoClockResetRequested.store(true, std::memory_order_release);
    state->lyricCueResetRequested.store(true, std::memory_order_release);
    state->outputPeak.store(0.0f, std::memory_order_relaxed);
    state->lastOutputSample.fill(0.0f);
    state->presetTransitionOffset.fill(0.0f);
    state->presetTransitionRemaining = 0u;
    state->presetTransitionNeedsInit = false;
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* processData)
{
    auto* state = self(plugin);
    if (!processData) return CLAP_PROCESS_CONTINUE;
    applyPendingVoxPreset(*state);
    readEvents(*state, processData->in_events);
    updateVoxLyricTransport(*state, processData->transport);
    updateVoxLyricAuto(*state, processData->transport, processData->frames_count);
    applyPendingVoxLyricCue(*state);
    updateTransportPhase(*state, processData->transport);
    if (processData->audio_outputs_count == 0u || !processData->audio_outputs) return CLAP_PROCESS_CONTINUE;

    auto& output = processData->audio_outputs[0];
    const uint32_t frames = processData->frames_count;
    if (!output.data32) {
        if (output.data64) {
            for (uint32_t ch = 0; ch < output.channel_count; ++ch) {
                if (output.data64[ch]) std::fill(output.data64[ch], output.data64[ch] + frames, 0.0);
            }
        }
        return CLAP_PROCESS_CONTINUE;
    }

    float* outputs[kOutputChannels] {};
    const uint32_t outputChannels = std::min<uint32_t>(output.channel_count, kOutputChannels);
    for (uint32_t ch = 0; ch < outputChannels; ++ch) outputs[ch] = output.data32[ch];
    s3g::AmbiVotParams motionParams = state->params;
    motionParams.scoreMode = s3g::AmbiVotScoreMode::Off;
    motionParams.scoreDepth = 0.0f;
    state->engine.setParams(motionParams);
    state->engine.advanceMotionOnly(frames);
    applyVoxPostProcess(*state, outputs, outputChannels, frames);
    applyVoxPresetTransition(*state, outputs, outputChannels, frames);
    s3g::clearAudioBufferFromChannel(output, outputChannels, frames);

    float peak = 0.0f;
    for (uint32_t ch = 0; ch < outputChannels; ++ch) {
        if (!output.data32[ch]) continue;
        for (uint32_t frame = 0; frame < frames; ++frame) peak = std::max(peak, std::fabs(output.data32[ch][frame]));
    }
    state->outputPeak.store(std::max(state->outputPeak.load(std::memory_order_relaxed) * 0.92f, peak), std::memory_order_relaxed);
#if defined(__APPLE__)
    publishMotionSnapshot(*state);
#endif
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t* plugin)
{
    auto* state = self(plugin);
    if (state->presetRescanPending.exchange(false, std::memory_order_acq_rel)
        && state->hostParams && state->hostParams->rescan) {
        state->hostParams->rescan(state->host, CLAP_PARAM_RESCAN_VALUES);
    }
    if (!state->voiceDelayResizePending.exchange(false, std::memory_order_acq_rel)) return;
    const uint32_t voices = s3g::kAmbiVoxMaxVoices;
    const float step = std::clamp(
        state->requestedDelayStepCapacity.load(std::memory_order_relaxed), 0.0f, 1.0f);
    try {
        auto delays = makeVoiceDelayBank(state->sampleRate, voices, step);
        std::atomic_store_explicit(&state->voiceDelayBank, std::move(delays), std::memory_order_release);
    } catch (const std::bad_alloc&) {
        return;
    }
    requestVoiceDelayResize(*state);
}

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 0u : 1u; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (isInput || index != 0u || !info) return false;
    info->id = 20;
    std::strncpy(info->name, "7OA ACN/SN3D Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kOutputChannels;
    info->port_type = CLAP_PORT_AMBISONIC;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

uint32_t notePortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 1u : 0u; }

bool notePortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_note_port_info_t* info)
{
    if (!isInput || index != 0u || !info) return false;
    info->id = 30;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::strncpy(info->name, "MIDI In", sizeof(info->name));
    return true;
}
const clap_plugin_note_ports_t notePorts { notePortsCount, notePortsGet };

struct ParamDef {
    clap_id id;
    const char* name;
    double min;
    double max;
    double def;
    bool stepped;
};

constexpr ParamDef kParams[] {
    { kOrderParamId, "Order", 1.0, 7.0, 3.0, true },
    { kVoicesParamId, "Voices", 1.0, static_cast<double>(s3g::kAmbiVoxMaxVoices), 8.0, true },
    { kModeParamId, "Trigger Mode", 0.0, 2.0, 0.0, true },
    { kBaseNoteParamId, "Pitch Root", 12.0, 96.0, 48.0, false },
    { kTuneParamId, "Legacy Pitch Tune", -1200.0, 1200.0, 0.0, false },
    { kScaleParamId, "Pitch Scale", 0.0, 5.0, 0.0, true },
    { kPitchSpreadParamId, "Pitch Spread", 0.0, 2.0, 1.0, false },
    { kDetuneParamId, "Pitch Deviation", 0.0, 1.0, 0.10, false },
    { kSpreadParamId, "Motion Spread", 0.0, 1.0, 0.65, false },
    { kMotionRateParamId, "Motion Rate", 0.001, 2.0, 0.045, false },
    { kOutputParamId, "Output", -60.0, 12.0, -6.0, false },
    { kMotionSceneParamId, "Motion Scene", 0.0, 4.0, 2.0, true },
    { kMotionClockParamId, "Motion Clock", 0.0, 1.0, 0.0, true },
    { kSyncDivisionParamId, "Sync Division", 0.25, 64.0, 8.0, false },
    { kMotionAmountParamId, "Motion Amount", 0.0, 1.0, 0.72, false },
    { kCoherenceParamId, "Motion Coherence", 0.0, 1.0, 0.62, false },
    { kChaosParamId, "Motion Chaos", 0.0, 1.0, 0.18, false },
    { kLinkParamId, "Space Timbre Link", 0.0, 1.0, 0.72, false },
    { kSmoothParamId, "Motion Smooth", 0.0, 1.0, 0.72, false },
    { kCenterAzimuthParamId, "Center Azimuth", -180.0, 180.0, 0.0, false },
    { kCenterElevationParamId, "Center Elevation", -90.0, 90.0, 0.0, false },
    { kCenterDistanceParamId, "Center Distance", 0.15, 2.0, 1.0, false },
    { kRaspParamId, "Legacy WORLD Edge", 0.0, 1.0, 0.0, false },
    { kBreathParamId, "Legacy WORLD Air", 0.0, 1.0, 0.0, false },
    { kDriveParamId, "Legacy WORLD Drive", 0.0, 1.0, 0.0, false },
    { kSpeechModeParamId, "Voice Mode", 0.0, 2.0, 1.0, true },
    { kPhraseRateParamId, "Phrase Pace", 0.0, 1.0, 0.34, false },
    { kCircuitModeParamId, "Legacy Circuit", 0.0, 3.0, 0.0, true },
    { kFormantMacroParamId, "WORLD Formant", -1.0, 1.0, 0.0, false },
    { kBendMacroParamId, "Legacy WORLD Flutter", 0.0, 1.0, 0.0, false },
    { kCrushMacroParamId, "Legacy WORLD Degrade", 0.0, 1.0, 0.0, false },
    { kWorldRateParamId, "Texture Speed", 0.0, 1.0, 0.50, false },
    { kWorldPitchParamId, "Pitch Transpose", -2400.0, 2400.0, 0.0, false },
    { kWorldLoopStartParamId, "Texture Loop Start", 0.0, 1.0, 0.0, false },
    { kWorldLoopEndParamId, "Texture Loop End", 0.0, 1.0, 1.0, false },
    { kWorldVoiceSpreadParamId, "Voice Delay Step", 0.0, 1.0, 0.0, false },
    { kWorldVoiceDeviationParamId, "Voice Delay Deviation", 0.0, 1.0, 0.0, false },
    { kWorldFreezeParamId, "Texture Freeze", 0.0, 1.0, 0.0, false },
    { kWorldScrubParamId, "Texture Position", 0.0, 1.0, 0.0, false },
    { kWorldVoicingParamId, "WORLD Periodicity", 0.0, 1.0, 0.50, false },
    { kPvocStretchParamId, "Playback Stretch", 0.25, 4.0, 1.0, false },
    { kPvocTransientParamId, "Playback Transient", 0.0, 1.0, 0.65, false },
    { kPortamentoParamId, "Pitch Glide", 0.0, 1.0, 0.18, false },
    { kVibratoDepthParamId, "Pitch Vibrato Depth", 0.0, 1.0, 0.12, false },
    { kVibratoRateParamId, "Pitch Vibrato Speed", 0.1, 12.0, 5.2, false },
    { kTransitionParamId, "Phrase Blend", 0.0, 1.0, 0.65, false },
    { kWorldAirColorParamId, "WORLD Air Color", -1.0, 1.0, 0.0, false },
    { kOrchestrationParamId, "Ensemble", 0.0, 5.0, 0.0, true },
    { kContourModeParamId, "Pitch Contour", 0.0, 3.0, 0.0, true },
    { kPhraseSpreadParamId, "Phrase Spread", 0.0, 1.0, 0.0, false },
    { kLyricModeParamId, "Lyrics Mode", 0.0, 4.0, 0.0, true },
    { kLyricCueBeatsParamId, "Lyrics Cue Beats", 0.25, 64.0, 16.0, false },
    { kLyricCueNoteParamId, "Lyrics Cue Base Note", 0.0, 127.0, 24.0, true },
    { kLyricCueChannelParamId, "Lyrics Cue Channel", 1.0, 16.0, 16.0, true },
};

constexpr bool isLegacyHiddenParam(clap_id id)
{
    switch (id) {
    case kTuneParamId:
    case kRaspParamId:
    case kBreathParamId:
    case kDriveParamId:
    case kCircuitModeParamId:
    case kBendMacroParamId:
    case kCrushMacroParamId:
        return true;
    default:
        return false;
    }
}

const ParamDef* paramDef(clap_id id)
{
    for (const auto& definition : kParams) {
        if (definition.id == id) return &definition;
    }
    return nullptr;
}

uint32_t publicPresetParameterCount()
{
    uint32_t count = 0u;
    for (const auto& definition : kParams) {
        if (!isLegacyHiddenParam(definition.id)) ++count;
    }
    return count;
}

bool assignVoxPresetParameter(VoxPresetSnapshot& preset, clap_id id, double rawValue)
{
    const auto* definition = paramDef(id);
    if (!definition || isLegacyHiddenParam(id) || !std::isfinite(rawValue)) return false;
    const double value = std::clamp(rawValue, definition->min, definition->max);
    auto& params = preset.params;
    auto& vox = preset.vox;
    auto& lyric = preset.lyric;
    switch (id) {
    case kOrderParamId: params.order = static_cast<uint32_t>(std::lround(value)); break;
    case kVoicesParamId: params.voices = static_cast<uint32_t>(std::lround(value)); break;
    case kModeParamId: params.mode = static_cast<s3g::AmbiVotMode>(static_cast<uint32_t>(std::lround(value))); break;
    case kBaseNoteParamId: params.baseNote = static_cast<float>(value); break;
    case kScaleParamId: params.scale = static_cast<s3g::AmbiVotScale>(static_cast<uint32_t>(std::lround(value))); break;
    case kPitchSpreadParamId: params.pitchSpread = static_cast<float>(value); break;
    case kDetuneParamId: params.detune = static_cast<float>(value); break;
    case kSpreadParamId: params.motionSpread = static_cast<float>(value); break;
    case kMotionRateParamId: params.motionRateHz = static_cast<float>(value); break;
    case kOutputParamId: params.outputGainDb = static_cast<float>(value); break;
    case kMotionSceneParamId: params.motionScene = static_cast<s3g::AmbiVotMotionScene>(static_cast<uint32_t>(std::lround(value))); break;
    case kMotionClockParamId: params.motionClock = static_cast<s3g::AmbiVotMotionClock>(static_cast<uint32_t>(std::lround(value))); break;
    case kSyncDivisionParamId: params.syncDivisionBeats = static_cast<float>(value); break;
    case kMotionAmountParamId: params.motionAmount = static_cast<float>(value); break;
    case kCoherenceParamId: params.motionCoherence = static_cast<float>(value); break;
    case kChaosParamId: params.motionChaos = static_cast<float>(value); break;
    case kLinkParamId: params.motionLink = static_cast<float>(value); break;
    case kSmoothParamId: params.motionSmooth = static_cast<float>(value); break;
    case kCenterAzimuthParamId: params.centerAzimuthDeg = static_cast<float>(value); break;
    case kCenterElevationParamId: params.centerElevationDeg = static_cast<float>(value); break;
    case kCenterDistanceParamId: params.centerDistance = static_cast<float>(value); break;
    case kSpeechModeParamId: vox.speechMode = static_cast<VoxSpeechMode>(static_cast<uint32_t>(std::lround(value))); break;
    case kPhraseRateParamId: vox.phraseRate = static_cast<float>(value); break;
    case kFormantMacroParamId: vox.formantMacro = static_cast<float>(value); break;
    case kWorldRateParamId: vox.worldRate = static_cast<float>(value); break;
    case kWorldPitchParamId: vox.worldPitchCents = static_cast<float>(value); break;
    case kWorldLoopStartParamId: vox.worldLoopStart = static_cast<float>(value); break;
    case kWorldLoopEndParamId: vox.worldLoopEnd = static_cast<float>(value); break;
    case kWorldVoiceSpreadParamId: vox.worldVoiceSpread = static_cast<float>(value); break;
    case kWorldVoiceDeviationParamId: vox.worldVoiceDeviation = static_cast<float>(value); break;
    case kWorldFreezeParamId: vox.worldFreeze = static_cast<float>(value); break;
    case kWorldScrubParamId: vox.worldScrub = static_cast<float>(value); break;
    case kWorldVoicingParamId: vox.worldVoicing = static_cast<float>(value); break;
    case kPvocStretchParamId: vox.pvocStretch = static_cast<float>(value); break;
    case kPvocTransientParamId: vox.pvocTransient = static_cast<float>(value); break;
    case kPortamentoParamId: vox.portamento = static_cast<float>(value); break;
    case kVibratoDepthParamId: vox.vibratoDepth = static_cast<float>(value); break;
    case kVibratoRateParamId: vox.vibratoRateHz = static_cast<float>(value); break;
    case kTransitionParamId: vox.transition = static_cast<float>(value); break;
    case kWorldAirColorParamId: vox.worldAirColor = static_cast<float>(value); break;
    case kOrchestrationParamId:
        vox.orchestration = static_cast<s3g::AmbiVoxOrchestration>(
            static_cast<uint32_t>(std::lround(value)));
        break;
    case kContourModeParamId:
        vox.contourMode = static_cast<s3g::AmbiVoxContourMode>(
            static_cast<uint32_t>(std::lround(value)));
        break;
    case kPhraseSpreadParamId: vox.phraseSpread = static_cast<float>(value); break;
    case kLyricModeParamId:
        lyric.mode = static_cast<VoxLyricMode>(
            std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 4u));
        break;
    case kLyricCueBeatsParamId: lyric.cueBeats = static_cast<float>(value); break;
    case kLyricCueNoteParamId: lyric.cueBaseNote = static_cast<uint32_t>(std::lround(value)); break;
    case kLyricCueChannelParamId: lyric.cueChannel = static_cast<uint32_t>(std::lround(value)); break;
    default: return false;
    }
    if (vox.worldLoopStart >= vox.worldLoopEnd - 0.01f) {
        if (id == kWorldLoopStartParamId) {
            vox.worldLoopEnd = std::min(1.0f, vox.worldLoopStart + 0.01f);
        } else if (id == kWorldLoopEndParamId) {
            vox.worldLoopStart = std::max(0.0f, vox.worldLoopEnd - 0.01f);
        }
    }
    return true;
}

const char* paramModule(clap_id id)
{
    if (isLegacyHiddenParam(id)) return "Legacy";
    switch (id) {
    case kOrderParamId:
    case kVoicesParamId:
    case kModeParamId:
    case kSpeechModeParamId:
        return "Encoder";
    case kBaseNoteParamId:
    case kTuneParamId:
    case kScaleParamId:
    case kPitchSpreadParamId:
    case kDetuneParamId:
    case kWorldPitchParamId:
    case kPortamentoParamId:
    case kVibratoDepthParamId:
    case kVibratoRateParamId:
        return "Pitch";
    case kPhraseRateParamId:
    case kTransitionParamId:
    case kPhraseSpreadParamId:
        return "Phrase Playback";
    case kLyricModeParamId:
    case kLyricCueBeatsParamId:
    case kLyricCueNoteParamId:
    case kLyricCueChannelParamId:
        return "Lyrics";
    case kWorldRateParamId:
    case kWorldLoopStartParamId:
    case kWorldLoopEndParamId:
    case kWorldFreezeParamId:
    case kWorldScrubParamId:
        return "Texture Playback";
    case kWorldVoiceSpreadParamId:
    case kWorldVoiceDeviationParamId:
    case kWorldVoicingParamId:
    case kFormantMacroParamId:
    case kWorldAirColorParamId:
    case kContourModeParamId:
    case kPvocStretchParamId:
    case kPvocTransientParamId:
        return "Voice Playback";
    case kOrchestrationParamId:
        return "Ensemble";
    case kOutputParamId:
        return "Output";
    default:
        return "Motion";
    }
}

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(std::size(kParams)); }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    const auto& def = kParams[index];
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE
        | (def.stepped ? CLAP_PARAM_IS_STEPPED : 0)
        | (isLegacyHiddenParam(def.id) ? CLAP_PARAM_IS_HIDDEN : 0);
    std::strncpy(info->name, def.name, sizeof(info->name));
    std::strncpy(info->module, paramModule(def.id), sizeof(info->module));
    info->min_value = def.min;
    info->max_value = def.max;
    info->default_value = def.def;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    const auto* state = self(plugin);
    const auto params = state->params;
    const auto vox = state->vox;
    const auto lyric = state->lyric;
    switch (id) {
    case kOrderParamId: *value = params.order; return true;
    case kVoicesParamId: *value = params.voices; return true;
    case kModeParamId: *value = static_cast<uint32_t>(params.mode); return true;
    case kPresetParamId: *value = static_cast<uint32_t>(params.preset); return true;
    case kBaseNoteParamId: *value = params.baseNote; return true;
    case kTuneParamId: *value = params.tuneCents; return true;
    case kVectorXParamId: *value = params.vectorX; return true;
    case kVectorYParamId: *value = params.vectorY; return true;
    case kScanParamId: *value = params.scan; return true;
    case kScanRateParamId: *value = params.scanRate; return true;
    case kMorphParamId: *value = params.morph; return true;
    case kDetuneParamId: *value = params.detune; return true;
    case kScaleParamId: *value = static_cast<uint32_t>(params.scale); return true;
    case kPitchSpreadParamId: *value = params.pitchSpread; return true;
    case kHarmonicsParamId: *value = params.harmonicAmount; return true;
    case kSubharmonicsParamId: *value = params.subharmonicAmount; return true;
    case kSpreadParamId: *value = params.motionSpread; return true;
    case kMotionRateParamId: *value = params.motionRateHz; return true;
    case kAttackParamId: *value = params.attackMs; return true;
    case kDecayParamId: *value = params.decayMs; return true;
    case kSustainParamId: *value = params.sustain; return true;
    case kReleaseParamId: *value = params.releaseMs; return true;
    case kOutputParamId: *value = params.outputGainDb; return true;
    case kMotionSceneParamId: *value = static_cast<uint32_t>(params.motionScene); return true;
    case kMotionClockParamId: *value = static_cast<uint32_t>(params.motionClock); return true;
    case kSyncDivisionParamId: *value = params.syncDivisionBeats; return true;
    case kMotionAmountParamId: *value = params.motionAmount; return true;
    case kCoherenceParamId: *value = params.motionCoherence; return true;
    case kChaosParamId: *value = params.motionChaos; return true;
    case kLinkParamId: *value = params.motionLink; return true;
    case kSmoothParamId: *value = params.motionSmooth; return true;
    case kCenterAzimuthParamId: *value = params.centerAzimuthDeg; return true;
    case kCenterElevationParamId: *value = params.centerElevationDeg; return true;
    case kCenterDistanceParamId: *value = params.centerDistance; return true;
    case kNeighborRadiusParamId: *value = params.neighborRadius; return true;
    case kRequiredNeighborsParamId: *value = params.requiredNeighbors; return true;
    case kScoreModeParamId: *value = static_cast<uint32_t>(params.scoreMode); return true;
    case kScoreDurationParamId: *value = params.scoreDurationSec; return true;
    case kScoreDepthParamId: *value = params.scoreDepth; return true;
    case kRaspParamId: *value = vox.rasp; return true;
    case kBreathParamId: *value = vox.breath; return true;
    case kThroatParamId: *value = vox.throat; return true;
    case kDriveParamId: *value = vox.drive; return true;
    case kJitterParamId: *value = vox.jitter; return true;
    case kVowelSpreadParamId: *value = vox.vowelSpread; return true;
    case kPitchScoopParamId: *value = vox.pitchScoop; return true;
    case kAttackShapeParamId: *value = vox.attackShape; return true;
    case kArticulationParamId: *value = vox.articulation; return true;
    case kConsonantParamId: *value = vox.consonant; return true;
    case kPlosiveParamId: *value = vox.plosive; return true;
    case kSibilanceParamId: *value = vox.sibilance; return true;
    case kPhraseRateParamId: *value = vox.phraseRate; return true;
    case kSpeechModeParamId: *value = static_cast<uint32_t>(vox.speechMode); return true;
    case kCircuitModeParamId: *value = static_cast<uint32_t>(vox.circuitMode); return true;
    case kFormantMacroParamId: *value = vox.formantMacro; return true;
    case kBendMacroParamId: *value = vox.bendMacro; return true;
    case kCrushMacroParamId: *value = vox.crushMacro; return true;
    case kWorldRateParamId: *value = vox.worldRate; return true;
    case kWorldPitchParamId: *value = vox.worldPitchCents; return true;
    case kWorldLoopStartParamId: *value = vox.worldLoopStart; return true;
    case kWorldLoopEndParamId: *value = vox.worldLoopEnd; return true;
    case kWorldVoiceSpreadParamId: *value = vox.worldVoiceSpread; return true;
    case kWorldVoiceDeviationParamId: *value = vox.worldVoiceDeviation; return true;
    case kWorldFreezeParamId: *value = vox.worldFreeze; return true;
    case kWorldScrubParamId: *value = vox.worldScrub; return true;
    case kWorldVoicingParamId: *value = vox.worldVoicing; return true;
    case kPvocStretchParamId: *value = vox.pvocStretch; return true;
    case kPvocTransientParamId: *value = vox.pvocTransient; return true;
    case kPortamentoParamId: *value = vox.portamento; return true;
    case kVibratoDepthParamId: *value = vox.vibratoDepth; return true;
    case kVibratoRateParamId: *value = vox.vibratoRateHz; return true;
    case kTransitionParamId: *value = vox.transition; return true;
    case kWorldAirColorParamId: *value = vox.worldAirColor; return true;
    case kOrchestrationParamId: *value = static_cast<uint32_t>(vox.orchestration); return true;
    case kContourModeParamId: *value = static_cast<uint32_t>(vox.contourMode); return true;
    case kPhraseSpreadParamId: *value = vox.phraseSpread; return true;
    case kLyricModeParamId: *value = static_cast<uint32_t>(lyric.mode); return true;
    case kLyricCueBeatsParamId: *value = lyric.cueBeats; return true;
    case kLyricCueNoteParamId: *value = lyric.cueBaseNote; return true;
    case kLyricCueChannelParamId: *value = lyric.cueChannel; return true;
    default: return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
    if (id == kOrderParamId) std::snprintf(display, size, "%.0fOA", value);
    else if (id == kModeParamId) std::snprintf(display, size, "%s", s3g::ambiVotModeName(static_cast<s3g::AmbiVotMode>(static_cast<uint32_t>(std::lround(value)))));
    else if (id == kPresetParamId) std::snprintf(display, size, "%s", s3g::ambiVotPresetName(static_cast<s3g::AmbiVotPreset>(static_cast<uint32_t>(std::lround(value)))));
    else if (id == kMotionSceneParamId) std::snprintf(display, size, "%s", s3g::ambiVotMotionSceneName(static_cast<s3g::AmbiVotMotionScene>(static_cast<uint32_t>(std::lround(value)))));
    else if (id == kMotionClockParamId) std::snprintf(display, size, "%s", s3g::ambiVotMotionClockName(static_cast<s3g::AmbiVotMotionClock>(static_cast<uint32_t>(std::lround(value)))));
    else if (id == kScaleParamId) std::snprintf(display, size, "%s", s3g::ambiVotScaleName(static_cast<s3g::AmbiVotScale>(static_cast<uint32_t>(std::lround(value)))));
    else if (id == kScoreModeParamId) std::snprintf(display, size, "%s", s3g::ambiVotScoreModeName(static_cast<s3g::AmbiVotScoreMode>(static_cast<uint32_t>(std::lround(value)))));
    else if (id == kSpeechModeParamId) std::snprintf(display, size, "%s", voxSpeechModeName(static_cast<VoxSpeechMode>(static_cast<uint32_t>(std::lround(value)))));
    else if (id == kCircuitModeParamId) std::snprintf(display, size, "%s", voxCircuitModeName(static_cast<VoxCircuitMode>(static_cast<uint32_t>(std::lround(value)))));
    else if (id == kOrchestrationParamId) std::snprintf(display, size, "%s", s3g::ambiVoxOrchestrationName(static_cast<s3g::AmbiVoxOrchestration>(static_cast<uint32_t>(std::lround(value)))));
    else if (id == kContourModeParamId) std::snprintf(display, size, "%s", s3g::ambiVoxContourModeName(static_cast<s3g::AmbiVoxContourMode>(static_cast<uint32_t>(std::lround(value)))));
    else if (id == kLyricModeParamId) std::snprintf(display, size, "%s", voxLyricModeName(static_cast<VoxLyricMode>(static_cast<uint32_t>(std::lround(value)))));
    else if (id == kTuneParamId) std::snprintf(display, size, "%+.0f ct", value);
    else if (id == kDetuneParamId) std::snprintf(display, size, "%.0f ct", value * 100.0);
    else if (id == kPitchSpreadParamId) std::snprintf(display, size, "%.0f%%", value * 100.0);
    else if (id == kMotionRateParamId) std::snprintf(display, size, "%.3f Hz", value);
    else if (id == kScanRateParamId) std::snprintf(display, size, "%+.2fx", value);
    else if (id == kSyncDivisionParamId) std::snprintf(display, size, "%.2f beats", value);
    else if (id == kLyricCueBeatsParamId) std::snprintf(display, size, "%.2f beats", value);
    else if (id == kScoreDurationParamId) std::snprintf(display, size, "%.2f s", value);
    else if (id == kAttackParamId || id == kDecayParamId || id == kReleaseParamId) std::snprintf(display, size, "%.0f ms", value);
    else if (id == kOutputParamId) std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kCenterAzimuthParamId || id == kCenterElevationParamId) std::snprintf(display, size, "%+.0f deg", value);
    else if (id == kNeighborRadiusParamId) std::snprintf(display, size, "%.2f", value);
    else if (id == kFormantMacroParamId || id == kWorldAirColorParamId) std::snprintf(display, size, "%+.0f%%", value * 100.0);
    else if (id == kWorldRateParamId) std::snprintf(display, size, "%.2fx", 0.25 + value * 1.75);
    else if (id == kWorldPitchParamId) std::snprintf(display, size, "%+.0f ct", value);
    else if (id == kWorldVoiceSpreadParamId) std::snprintf(display, size, "%.0f ms", value * kVoxVoiceTimeStepMaxMs);
    else if (id == kPvocStretchParamId) std::snprintf(display, size, "%.2fx", value);
    else if (id == kPortamentoParamId) std::snprintf(display, size, "%.0f ms", 2.0 + value * value * 750.0);
    else if (id == kVibratoDepthParamId) std::snprintf(display, size, "%.0f ct", value * 100.0);
    else if (id == kVibratoRateParamId) std::snprintf(display, size, "%.2f Hz", value);
    else if (id == kMotionAmountParamId || id == kSpreadParamId || id == kCoherenceParamId || id == kChaosParamId || id == kLinkParamId || id == kSmoothParamId || id == kScanParamId || id == kMorphParamId || id == kSustainParamId || id == kHarmonicsParamId || id == kSubharmonicsParamId || id == kScoreDepthParamId || id == kRaspParamId || id == kBreathParamId || id == kThroatParamId || id == kDriveParamId || id == kJitterParamId || id == kVowelSpreadParamId || id == kPitchScoopParamId || id == kAttackShapeParamId || id == kArticulationParamId || id == kConsonantParamId || id == kPlosiveParamId || id == kSibilanceParamId || id == kPhraseRateParamId || id == kBendMacroParamId || id == kCrushMacroParamId || id == kWorldLoopStartParamId || id == kWorldLoopEndParamId || id == kWorldVoiceDeviationParamId || id == kWorldFreezeParamId || id == kWorldScrubParamId || id == kWorldVoicingParamId || id == kPvocTransientParamId || id == kTransitionParamId || id == kPhraseSpreadParamId) std::snprintf(display, size, "%.0f%%", value * 100.0);
    else if (id == kOrderParamId || id == kVoicesParamId || id == kRequiredNeighborsParamId || id == kBaseNoteParamId || id == kLyricCueNoteParamId || id == kLyricCueChannelParamId) std::snprintf(display, size, "%.0f", value);
    else std::snprintf(display, size, "%.2f", value);
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* display, double* value)
{
    if (!display || !value) return false;
    std::string text(display);
    for (char& ch : text) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (id == kModeParamId) {
        if (text.find("both") != std::string::npos) *value = 2.0;
        else if (text.find("midi") != std::string::npos) *value = 1.0;
        else *value = 0.0;
        return true;
    }
    if (id == kMotionSceneParamId) {
        if (text.find("orbit") != std::string::npos) *value = 1.0;
        else if (text.find("flow") != std::string::npos) *value = 2.0;
        else if (text.find("path") != std::string::npos) *value = 3.0;
        else if (text.find("pulse") != std::string::npos) *value = 4.0;
        else *value = 0.0;
        return true;
    }
    if (id == kMotionClockParamId) {
        *value = text.find("sync") != std::string::npos ? 1.0 : 0.0;
        return true;
    }
    if (id == kLyricModeParamId) {
        if (text.find("auto") != std::string::npos) *value = 4.0;
        else if (text.find("transport") != std::string::npos) *value = 3.0;
        else if (text.find("cue") != std::string::npos) *value = 2.0;
        else if (text.find("step") != std::string::npos) *value = 1.0;
        else *value = 0.0;
        return true;
    }
    if (id == kScaleParamId) {
        if (text.find("harm") != std::string::npos) *value = 5.0;
        else if (text.find("whole") != std::string::npos) *value = 4.0;
        else if (text.find("penta") != std::string::npos) *value = 3.0;
        else if (text.find("minor") != std::string::npos) *value = 2.0;
        else if (text.find("major") != std::string::npos) *value = 1.0;
        else *value = 0.0;
        return true;
    }
    if (id == kSpeechModeParamId) {
        if (text.find("speak") != std::string::npos) *value = 1.0;
        else if (text.find("sing") != std::string::npos || text.find("chorus") != std::string::npos) *value = 2.0;
        else *value = 0.0;
        return true;
    }
    if (id == kOrchestrationParamId) {
        if (text.find("unison") != std::string::npos) *value = 1.0;
        else if (text.find("chorale") != std::string::npos) *value = 2.0;
        else if (text.find("chorus") != std::string::npos) *value = 3.0;
        else if (text.find("round") != std::string::npos) *value = 4.0;
        else if (text.find("cluster") != std::string::npos) *value = 5.0;
        else *value = 0.0;
        return true;
    }
    if (id == kContourModeParamId) {
        if (text.find("reduc") != std::string::npos) *value = 1.0;
        else if (text.find("flat") != std::string::npos) *value = 2.0;
        else if (text.find("quant") != std::string::npos) *value = 3.0;
        else *value = 0.0;
        return true;
    }
    if (id == kCircuitModeParamId) {
        if (text.find("bent") != std::string::npos) *value = 1.0;
        else if (text.find("chip") != std::string::npos) *value = 2.0;
        else if (text.find("broken") != std::string::npos) *value = 3.0;
        else *value = 0.0;
        return true;
    }
    if (id == kWorldVoiceSpreadParamId) {
        const double parsed = std::atof(display);
        *value = text.find('%') != std::string::npos
            ? parsed * 0.01
            : parsed / static_cast<double>(kVoxVoiceTimeStepMaxMs);
        return true;
    }
    if (id == kWorldVoiceDeviationParamId) {
        const double parsed = std::atof(display);
        *value = std::strchr(display, '%') || std::fabs(parsed) > 1.0
            ? parsed * 0.01
            : parsed;
        return true;
    }
    if (id == kWorldRateParamId) {
        *value = (std::atof(display) - 0.25) / 1.75;
        return true;
    }
    if (id == kDetuneParamId) {
        *value = std::atof(display) * 0.01;
        return true;
    }
    if (id == kPortamentoParamId) {
        const double parsed = std::atof(display);
        *value = text.find('%') != std::string::npos
            ? parsed * 0.01
            : std::sqrt(std::max(0.0, parsed - 2.0) / 750.0);
        return true;
    }
    if (id == kVibratoDepthParamId) {
        const double parsed = std::atof(display);
        *value = parsed * 0.01;
        return true;
    }
    if (id == kVibratoRateParamId) {
        *value = std::atof(display);
        return true;
    }
    if (id == kFormantMacroParamId || id == kWorldAirColorParamId) {
        const double parsed = std::atof(display);
        *value = text.find('%') != std::string::npos || std::fabs(parsed) > 1.0
            ? parsed * 0.01
            : parsed;
        return true;
    }
    switch (id) {
    case kPitchSpreadParamId:
    case kMotionAmountParamId:
    case kSpreadParamId:
    case kCoherenceParamId:
    case kChaosParamId:
    case kLinkParamId:
    case kSmoothParamId:
    case kRaspParamId:
    case kBreathParamId:
    case kDriveParamId:
    case kPhraseRateParamId:
    case kBendMacroParamId:
    case kCrushMacroParamId:
    case kWorldLoopStartParamId:
    case kWorldLoopEndParamId:
    case kWorldFreezeParamId:
    case kWorldScrubParamId:
    case kWorldVoicingParamId:
    case kPvocTransientParamId:
    case kTransitionParamId:
    case kPhraseSpreadParamId:
        *value = std::atof(display) * 0.01;
        return true;
    default:
        break;
    }
    *value = std::atof(display);
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* events, const clap_output_events_t*)
{
    auto* state = self(plugin);
    applyPendingVoxPreset(*state);
    readEvents(*state, events);
}
const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    auto* state = self(plugin);
    SavedState saved {};
    saved.params = state->params;
    saved.score = loadScore(*state);
    saved.vox = state->vox;
    saved.lyric = state->lyric;
    saved.factoryPresetIndex = state->factoryPresetIndex;
    std::strncpy(saved.presetName.data(), state->presetName, saved.presetName.size() - 1u);
    saved.phraseLength = std::min<uint32_t>(
        state->voxPhraseLength.load(std::memory_order_acquire), kVoxPhraseMaxChars - 1u);
    for (uint32_t i = 0; i < saved.phraseLength; ++i) {
        saved.phrase[i] = state->voxPhrase[i].load(std::memory_order_relaxed);
    }
    saved.lyricsLength = std::min<uint32_t>(
        state->voxLyricsLength.load(std::memory_order_acquire), kVoxLyricsMaxChars - 1u);
    for (uint32_t i = 0; i < saved.lyricsLength; ++i) {
        saved.lyrics[i] = state->voxLyrics[i].load(std::memory_order_relaxed);
    }
    auto userBank = std::atomic_load_explicit(&state->userBank, std::memory_order_acquire);
    if (userBank) {
        saved.hasUserAtlas = 1u;
        for (uint32_t table = 0; table < s3g::kAmbiVotTableCount; ++table) {
            std::copy(userBank->tables[table].begin(),
                      userBank->tables[table].end(),
                      saved.userAtlas.begin() + table * s3g::kAmbiVotTableSize);
        }
    }
#if defined(__APPLE__)
    saved.guiPage = state->guiPage;
    saved.guiViewMode = state->guiViewMode;
    saved.guiViewAzDeg = state->guiViewAzDeg;
    saved.guiViewElDeg = state->guiViewElDeg;
    saved.guiViewZoom = state->guiViewZoom;
#endif
    return writeExact(stream, &saved, sizeof(saved));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    uint32_t version = 0;
    if (!readExact(stream, &version, sizeof(version))) return false;
    auto* state = self(plugin);
    std::shared_ptr<const s3g::AmbiVotTableBank> restoredUserBank;
    int32_t loadedPresetIndex = -1;
    char loadedPresetName[64] {};
    std::snprintf(loadedPresetName, sizeof(loadedPresetName), "%s", "CUSTOM");
    state->vox = VoxParams {};
    state->lyric = VoxLyricParams {};
    if (version == 1u) {
        SavedStateV1 legacy {};
        legacy.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&legacy) + sizeof(version), sizeof(legacy) - sizeof(version))) return false;
        state->params = migrateLegacy(legacy.params);
    } else if (version == 2u) {
        SavedStateV2 legacy {};
        legacy.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&legacy) + sizeof(version), sizeof(legacy) - sizeof(version))) return false;
        state->params = migrateV2(legacy.params);
#if defined(__APPLE__)
        state->guiPage = std::clamp(legacy.guiPage, 0, 2);
        state->guiViewMode = std::clamp(legacy.guiViewMode, -1, 2);
        state->guiViewAzDeg = std::clamp(legacy.guiViewAzDeg, -180.0f, 180.0f);
        state->guiViewElDeg = std::clamp(legacy.guiViewElDeg, -85.0f, 85.0f);
        state->guiViewZoom = std::clamp(legacy.guiViewZoom, 0.55f, 2.4f);
#endif
    } else if (version == 3u) {
        SavedStateV3 legacy {};
        legacy.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&legacy) + sizeof(version), sizeof(legacy) - sizeof(version))) return false;
        state->params = migrateV3(legacy.params);
#if defined(__APPLE__)
        state->guiPage = std::clamp(legacy.guiPage, 0, 2);
        state->guiViewMode = std::clamp(legacy.guiViewMode, -1, 2);
        state->guiViewAzDeg = std::clamp(legacy.guiViewAzDeg, -180.0f, 180.0f);
        state->guiViewElDeg = std::clamp(legacy.guiViewElDeg, -85.0f, 85.0f);
        state->guiViewZoom = std::clamp(legacy.guiViewZoom, 0.55f, 2.4f);
#endif
    } else if (version == 4u) {
        SavedStateV4 legacy {};
        legacy.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&legacy) + sizeof(version), sizeof(legacy) - sizeof(version))) return false;
        state->params = migrateV4(legacy.params);
#if defined(__APPLE__)
        state->guiPage = std::clamp(legacy.guiPage, 0, 2);
        state->guiViewMode = std::clamp(legacy.guiViewMode, -1, 2);
        state->guiViewAzDeg = std::clamp(legacy.guiViewAzDeg, -180.0f, 180.0f);
        state->guiViewElDeg = std::clamp(legacy.guiViewElDeg, -85.0f, 85.0f);
        state->guiViewZoom = std::clamp(legacy.guiViewZoom, 0.55f, 2.4f);
#endif
    } else if (version == 5u) {
        SavedStateV5 legacy {};
        legacy.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&legacy) + sizeof(version), sizeof(legacy) - sizeof(version))) return false;
        state->params = legacy.params;
        storeScore(*state, legacy.score);
#if defined(__APPLE__)
        state->guiPage = std::clamp(legacy.guiPage, 0, 2);
        state->guiViewMode = std::clamp(legacy.guiViewMode, -1, 2);
        state->guiViewAzDeg = std::clamp(legacy.guiViewAzDeg, -180.0f, 180.0f);
        state->guiViewElDeg = std::clamp(legacy.guiViewElDeg, -85.0f, 85.0f);
        state->guiViewZoom = std::clamp(legacy.guiViewZoom, 0.55f, 2.4f);
#endif
    } else if (version == 6u) {
        SavedStateV6 legacy {};
        legacy.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&legacy) + sizeof(version), sizeof(legacy) - sizeof(version))) return false;
        state->params = legacy.params;
        storeScore(*state, legacy.score);
        if (legacy.hasUserAtlas != 0u) {
            restoredUserBank = restoreUserAtlas(legacy.userAtlas);
        }
#if defined(__APPLE__)
        state->guiPage = std::clamp(legacy.guiPage, 0, 2);
        state->guiViewMode = std::clamp(legacy.guiViewMode, -1, 2);
        state->guiViewAzDeg = std::clamp(legacy.guiViewAzDeg, -180.0f, 180.0f);
        state->guiViewElDeg = std::clamp(legacy.guiViewElDeg, -85.0f, 85.0f);
        state->guiViewZoom = std::clamp(legacy.guiViewZoom, 0.55f, 2.4f);
#endif
    } else if (version == 7u) {
        SavedStateV7 legacy {};
        legacy.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&legacy) + sizeof(version), sizeof(legacy) - sizeof(version))) return false;
        state->params = legacy.params;
        storeScore(*state, legacy.score);
        state->vox.rasp = legacy.vox.rasp;
        state->vox.breath = legacy.vox.breath;
        state->vox.throat = legacy.vox.throat;
        state->vox.drive = legacy.vox.drive;
        state->vox.jitter = legacy.vox.jitter;
        state->vox.vowelSpread = legacy.vox.vowelSpread;
        state->vox.pitchScoop = legacy.vox.pitchScoop;
        state->vox.attackShape = legacy.vox.attackShape;
        if (legacy.hasUserAtlas != 0u) {
            restoredUserBank = restoreUserAtlas(legacy.userAtlas);
        }
#if defined(__APPLE__)
        state->guiPage = std::clamp(legacy.guiPage, 0, 2);
        state->guiViewMode = std::clamp(legacy.guiViewMode, -1, 2);
        state->guiViewAzDeg = std::clamp(legacy.guiViewAzDeg, -180.0f, 180.0f);
        state->guiViewElDeg = std::clamp(legacy.guiViewElDeg, -85.0f, 85.0f);
        state->guiViewZoom = std::clamp(legacy.guiViewZoom, 0.55f, 2.4f);
#endif
    } else if (version == 8u) {
        SavedStateV8 legacy {};
        legacy.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&legacy) + sizeof(version), sizeof(legacy) - sizeof(version))) return false;
        state->params = legacy.params;
        storeScore(*state, legacy.score);
        migrateVoxV9(*state, legacy.vox);
        if (legacy.hasUserAtlas != 0u) {
            restoredUserBank = restoreUserAtlas(legacy.userAtlas);
        }
#if defined(__APPLE__)
        state->guiPage = std::clamp(legacy.guiPage, 0, 2);
        state->guiViewMode = std::clamp(legacy.guiViewMode, -1, 2);
        state->guiViewAzDeg = std::clamp(legacy.guiViewAzDeg, -180.0f, 180.0f);
        state->guiViewElDeg = std::clamp(legacy.guiViewElDeg, -85.0f, 85.0f);
        state->guiViewZoom = std::clamp(legacy.guiViewZoom, 0.55f, 2.4f);
#endif
    } else if (version == 9u) {
        SavedStateV9 legacy {};
        legacy.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&legacy) + sizeof(version), sizeof(legacy) - sizeof(version))) return false;
        state->params = legacy.params;
        storeScore(*state, legacy.score);
        migrateVoxV9(*state, legacy.vox);
        const uint32_t phraseLength = std::min<uint32_t>(legacy.phraseLength, kVoxPhraseMaxChars - 1u);
        for (uint32_t i = 0; i < kVoxPhraseMaxChars; ++i) {
            state->voxPhrase[i].store(i < phraseLength ? legacy.phrase[i] : 0u, std::memory_order_relaxed);
        }
        state->voxPhraseLength.store(phraseLength, std::memory_order_release);
        state->voxPhraseIndex = 0u;
        if (legacy.hasUserAtlas != 0u) {
            restoredUserBank = restoreUserAtlas(legacy.userAtlas);
        }
#if defined(__APPLE__)
        state->guiPage = std::clamp(legacy.guiPage, 0, 2);
        state->guiViewMode = std::clamp(legacy.guiViewMode, -1, 2);
        state->guiViewAzDeg = std::clamp(legacy.guiViewAzDeg, -180.0f, 180.0f);
        state->guiViewElDeg = std::clamp(legacy.guiViewElDeg, -85.0f, 85.0f);
        state->guiViewZoom = std::clamp(legacy.guiViewZoom, 0.55f, 2.4f);
#endif
    } else if (version == 10u) {
        SavedStateV10 legacy {};
        legacy.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&legacy) + sizeof(version), sizeof(legacy) - sizeof(version))) return false;
        state->params = legacy.params;
        storeScore(*state, legacy.score);
        migrateVoxV10(*state, legacy.vox);
        const uint32_t phraseLength = std::min<uint32_t>(legacy.phraseLength, kVoxPhraseMaxChars - 1u);
        for (uint32_t i = 0; i < kVoxPhraseMaxChars; ++i) {
            state->voxPhrase[i].store(i < phraseLength ? legacy.phrase[i] : 0u, std::memory_order_relaxed);
        }
        state->voxPhraseLength.store(phraseLength, std::memory_order_release);
        state->voxPhraseIndex = 0u;
        if (legacy.hasUserAtlas != 0u) {
            restoredUserBank = restoreUserAtlas(legacy.userAtlas);
        }
#if defined(__APPLE__)
        state->guiPage = std::clamp(legacy.guiPage, 0, 2);
        state->guiViewMode = std::clamp(legacy.guiViewMode, -1, 2);
        state->guiViewAzDeg = std::clamp(legacy.guiViewAzDeg, -180.0f, 180.0f);
        state->guiViewElDeg = std::clamp(legacy.guiViewElDeg, -85.0f, 85.0f);
        state->guiViewZoom = std::clamp(legacy.guiViewZoom, 0.55f, 2.4f);
#endif
    } else if (version == 11u) {
        SavedStateV11 legacy {};
        legacy.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&legacy) + sizeof(version), sizeof(legacy) - sizeof(version))) return false;
        state->params = legacy.params;
        storeScore(*state, legacy.score);
        migrateVoxV11(*state, legacy.vox);
        const uint32_t phraseLength = std::min<uint32_t>(legacy.phraseLength, kVoxPhraseMaxChars - 1u);
        for (uint32_t i = 0; i < kVoxPhraseMaxChars; ++i) {
            state->voxPhrase[i].store(i < phraseLength ? legacy.phrase[i] : 0u, std::memory_order_relaxed);
        }
        state->voxPhraseLength.store(phraseLength, std::memory_order_release);
        state->voxPhraseIndex = 0u;
        if (legacy.hasUserAtlas != 0u) {
            restoredUserBank = restoreUserAtlas(legacy.userAtlas);
        }
#if defined(__APPLE__)
        state->guiPage = std::clamp(legacy.guiPage, 0, 2);
        state->guiViewMode = std::clamp(legacy.guiViewMode, -1, 2);
        state->guiViewAzDeg = std::clamp(legacy.guiViewAzDeg, -180.0f, 180.0f);
        state->guiViewElDeg = std::clamp(legacy.guiViewElDeg, -85.0f, 85.0f);
        state->guiViewZoom = std::clamp(legacy.guiViewZoom, 0.55f, 2.4f);
#endif
    } else if (version == 12u) {
        SavedStateV12 legacy {};
        legacy.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&legacy) + sizeof(version), sizeof(legacy) - sizeof(version))) return false;
        state->params = legacy.params;
        storeScore(*state, legacy.score);
        migrateVoxV12(*state, legacy.vox);
        const uint32_t phraseLength = std::min<uint32_t>(legacy.phraseLength, kVoxPhraseMaxChars - 1u);
        for (uint32_t i = 0; i < kVoxPhraseMaxChars; ++i) {
            state->voxPhrase[i].store(i < phraseLength ? legacy.phrase[i] : 0u, std::memory_order_relaxed);
        }
        state->voxPhraseLength.store(phraseLength, std::memory_order_release);
        state->voxPhraseIndex = 0u;
        if (legacy.hasUserAtlas != 0u) {
            restoredUserBank = restoreUserAtlas(legacy.userAtlas);
        }
#if defined(__APPLE__)
        state->guiPage = std::clamp(legacy.guiPage, 0, 2);
        state->guiViewMode = std::clamp(legacy.guiViewMode, -1, 2);
        state->guiViewAzDeg = std::clamp(legacy.guiViewAzDeg, -180.0f, 180.0f);
        state->guiViewElDeg = std::clamp(legacy.guiViewElDeg, -85.0f, 85.0f);
        state->guiViewZoom = std::clamp(legacy.guiViewZoom, 0.55f, 2.4f);
#endif
    } else if (version == 13u) {
        SavedStateV13 legacy {};
        legacy.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&legacy) + sizeof(version), sizeof(legacy) - sizeof(version))) return false;
        state->params = legacy.params;
        storeScore(*state, legacy.score);
        migrateVoxV13(*state, legacy.vox);
        const uint32_t phraseLength = std::min<uint32_t>(legacy.phraseLength, kVoxPhraseMaxChars - 1u);
        for (uint32_t i = 0; i < kVoxPhraseMaxChars; ++i) {
            state->voxPhrase[i].store(i < phraseLength ? legacy.phrase[i] : 0u, std::memory_order_relaxed);
        }
        state->voxPhraseLength.store(phraseLength, std::memory_order_release);
        state->voxPhraseIndex = 0u;
        if (legacy.hasUserAtlas != 0u) {
            restoredUserBank = restoreUserAtlas(legacy.userAtlas);
        }
#if defined(__APPLE__)
        state->guiPage = std::clamp(legacy.guiPage, 0, 2);
        state->guiViewMode = std::clamp(legacy.guiViewMode, -1, 2);
        state->guiViewAzDeg = std::clamp(legacy.guiViewAzDeg, -180.0f, 180.0f);
        state->guiViewElDeg = std::clamp(legacy.guiViewElDeg, -85.0f, 85.0f);
        state->guiViewZoom = std::clamp(legacy.guiViewZoom, 0.55f, 2.4f);
#endif
    } else if (version == 14u) {
        SavedStateV14 legacy {};
        legacy.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&legacy) + sizeof(version), sizeof(legacy) - sizeof(version))) return false;
        state->params = legacy.params;
        storeScore(*state, legacy.score);
        migrateVoxV14(*state, legacy.vox);
        const uint32_t phraseLength = std::min<uint32_t>(legacy.phraseLength, kVoxPhraseMaxChars - 1u);
        for (uint32_t i = 0; i < kVoxPhraseMaxChars; ++i) {
            state->voxPhrase[i].store(i < phraseLength ? legacy.phrase[i] : 0u, std::memory_order_relaxed);
        }
        state->voxPhraseLength.store(phraseLength, std::memory_order_release);
        state->voxPhraseIndex = 0u;
        if (legacy.hasUserAtlas != 0u) {
            restoredUserBank = restoreUserAtlas(legacy.userAtlas);
        }
#if defined(__APPLE__)
        state->guiPage = std::clamp(legacy.guiPage, 0, 2);
        state->guiViewMode = std::clamp(legacy.guiViewMode, -1, 2);
        state->guiViewAzDeg = std::clamp(legacy.guiViewAzDeg, -180.0f, 180.0f);
        state->guiViewElDeg = std::clamp(legacy.guiViewElDeg, -85.0f, 85.0f);
        state->guiViewZoom = std::clamp(legacy.guiViewZoom, 0.55f, 2.4f);
#endif
    } else if (version == 15u) {
        SavedStateV15 legacy {};
        legacy.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&legacy) + sizeof(version), sizeof(legacy) - sizeof(version))) return false;
        state->params = legacy.params;
        storeScore(*state, legacy.score);
        migrateVoxV15(*state, legacy.vox);
        const uint32_t phraseLength = std::min<uint32_t>(legacy.phraseLength, kVoxPhraseMaxChars - 1u);
        for (uint32_t i = 0; i < kVoxPhraseMaxChars; ++i) {
            state->voxPhrase[i].store(i < phraseLength ? legacy.phrase[i] : 0u, std::memory_order_relaxed);
        }
        state->voxPhraseLength.store(phraseLength, std::memory_order_release);
        state->voxPhraseIndex = 0u;
        if (legacy.hasUserAtlas != 0u) {
            restoredUserBank = restoreUserAtlas(legacy.userAtlas);
        }
#if defined(__APPLE__)
        state->guiPage = std::clamp(legacy.guiPage, 0, 2);
        state->guiViewMode = std::clamp(legacy.guiViewMode, -1, 2);
        state->guiViewAzDeg = std::clamp(legacy.guiViewAzDeg, -180.0f, 180.0f);
        state->guiViewElDeg = std::clamp(legacy.guiViewElDeg, -85.0f, 85.0f);
        state->guiViewZoom = std::clamp(legacy.guiViewZoom, 0.55f, 2.4f);
#endif
    } else if (version == 16u) {
        SavedStateV16 legacy {};
        legacy.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&legacy) + sizeof(version), sizeof(legacy) - sizeof(version))) return false;
        state->params = legacy.params;
        storeScore(*state, legacy.score);
        migrateVoxV16(*state, legacy.vox);
        const uint32_t phraseLength = std::min<uint32_t>(legacy.phraseLength, kVoxPhraseMaxChars - 1u);
        for (uint32_t i = 0; i < kVoxPhraseMaxChars; ++i) {
            state->voxPhrase[i].store(i < phraseLength ? legacy.phrase[i] : 0u, std::memory_order_relaxed);
        }
        state->voxPhraseLength.store(phraseLength, std::memory_order_release);
        state->voxPhraseIndex = 0u;
        if (legacy.hasUserAtlas != 0u) {
            restoredUserBank = restoreUserAtlas(legacy.userAtlas);
        }
#if defined(__APPLE__)
        state->guiPage = std::clamp(legacy.guiPage, 0, 2);
        state->guiViewMode = std::clamp(legacy.guiViewMode, -1, 2);
        state->guiViewAzDeg = std::clamp(legacy.guiViewAzDeg, -180.0f, 180.0f);
        state->guiViewElDeg = std::clamp(legacy.guiViewElDeg, -85.0f, 85.0f);
        state->guiViewZoom = std::clamp(legacy.guiViewZoom, 0.55f, 2.4f);
#endif
    } else if (version == 17u || version == 18u) {
        SavedStateV18 saved {};
        saved.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&saved) + sizeof(version), sizeof(saved) - sizeof(version))) return false;
        state->params = saved.params;
        storeScore(*state, saved.score);
        migrateVoxV18(*state, saved.vox);
        const uint32_t phraseLength = std::min<uint32_t>(saved.phraseLength, kVoxPhraseMaxChars - 1u);
        for (uint32_t i = 0; i < kVoxPhraseMaxChars; ++i) {
            state->voxPhrase[i].store(i < phraseLength ? saved.phrase[i] : 0u, std::memory_order_relaxed);
        }
        state->voxPhraseLength.store(phraseLength, std::memory_order_release);
        state->voxPhraseIndex = 0u;
        if (saved.hasUserAtlas != 0u) {
            restoredUserBank = restoreUserAtlas(saved.userAtlas);
        }
#if defined(__APPLE__)
        state->guiPage = std::clamp(saved.guiPage, 0, 2);
        state->guiViewMode = std::clamp(saved.guiViewMode, -1, 2);
        state->guiViewAzDeg = std::clamp(saved.guiViewAzDeg, -180.0f, 180.0f);
        state->guiViewElDeg = std::clamp(saved.guiViewElDeg, -85.0f, 85.0f);
        state->guiViewZoom = std::clamp(saved.guiViewZoom, 0.55f, 2.4f);
#endif
    } else if (version == 19u) {
        SavedStateV19 saved {};
        saved.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&saved) + sizeof(version), sizeof(saved) - sizeof(version))) return false;
        state->params = saved.params;
        storeScore(*state, saved.score);
        migrateVoxV20(*state, saved.vox);
        const uint32_t phraseLength = std::min<uint32_t>(saved.phraseLength, kVoxPhraseMaxChars - 1u);
        for (uint32_t i = 0; i < kVoxPhraseMaxChars; ++i) {
            state->voxPhrase[i].store(i < phraseLength ? saved.phrase[i] : 0u, std::memory_order_relaxed);
        }
        state->voxPhraseLength.store(phraseLength, std::memory_order_release);
        state->voxPhraseIndex = 0u;
        if (saved.hasUserAtlas != 0u) {
            restoredUserBank = restoreUserAtlas(saved.userAtlas);
        }
#if defined(__APPLE__)
        state->guiPage = std::clamp(saved.guiPage, 0, 2);
        state->guiViewMode = std::clamp(saved.guiViewMode, -1, 2);
        state->guiViewAzDeg = std::clamp(saved.guiViewAzDeg, -180.0f, 180.0f);
        state->guiViewElDeg = std::clamp(saved.guiViewElDeg, -85.0f, 85.0f);
        state->guiViewZoom = std::clamp(saved.guiViewZoom, 0.55f, 2.4f);
#endif
    } else if (version == 20u) {
        SavedStateV20 saved {};
        saved.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&saved) + sizeof(version), sizeof(saved) - sizeof(version))) return false;
        state->params = saved.params;
        storeScore(*state, saved.score);
        migrateVoxV20(*state, saved.vox);
        const uint32_t phraseLength = std::min<uint32_t>(saved.phraseLength, kVoxPhraseMaxChars - 1u);
        for (uint32_t i = 0; i < kVoxPhraseMaxChars; ++i) {
            state->voxPhrase[i].store(i < phraseLength ? saved.phrase[i] : 0u, std::memory_order_relaxed);
        }
        state->voxPhraseLength.store(phraseLength, std::memory_order_release);
        state->voxPhraseIndex = 0u;
        loadedPresetIndex = saved.factoryPresetIndex;
        std::strncpy(loadedPresetName, saved.presetName.data(), sizeof(loadedPresetName) - 1u);
        if (saved.hasUserAtlas != 0u) {
            restoredUserBank = restoreUserAtlas(saved.userAtlas);
        }
#if defined(__APPLE__)
        state->guiPage = std::clamp(saved.guiPage, 0, 2);
        state->guiViewMode = std::clamp(saved.guiViewMode, -1, 2);
        state->guiViewAzDeg = std::clamp(saved.guiViewAzDeg, -180.0f, 180.0f);
        state->guiViewElDeg = std::clamp(saved.guiViewElDeg, -85.0f, 85.0f);
        state->guiViewZoom = std::clamp(saved.guiViewZoom, 0.55f, 2.4f);
#endif
    } else if (version == 21u) {
        SavedStateV21 saved {};
        saved.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&saved) + sizeof(version), sizeof(saved) - sizeof(version))) return false;
        state->params = saved.params;
        storeScore(*state, saved.score);
        state->vox = saved.vox;
        const uint32_t phraseLength = std::min<uint32_t>(saved.phraseLength, kVoxPhraseMaxChars - 1u);
        for (uint32_t i = 0; i < kVoxPhraseMaxChars; ++i) {
            state->voxPhrase[i].store(i < phraseLength ? saved.phrase[i] : 0u, std::memory_order_relaxed);
        }
        state->voxPhraseLength.store(phraseLength, std::memory_order_release);
        state->voxPhraseIndex = 0u;
        loadedPresetIndex = saved.factoryPresetIndex;
        std::strncpy(loadedPresetName, saved.presetName.data(), sizeof(loadedPresetName) - 1u);
        if (saved.hasUserAtlas != 0u) restoredUserBank = restoreUserAtlas(saved.userAtlas);
#if defined(__APPLE__)
        state->guiPage = std::clamp(saved.guiPage, 0, 2);
        state->guiViewMode = std::clamp(saved.guiViewMode, -1, 2);
        state->guiViewAzDeg = std::clamp(saved.guiViewAzDeg, -180.0f, 180.0f);
        state->guiViewElDeg = std::clamp(saved.guiViewElDeg, -85.0f, 85.0f);
        state->guiViewZoom = std::clamp(saved.guiViewZoom, 0.55f, 2.4f);
#endif
    } else if (version == kStateVersion) {
        SavedState saved {};
        saved.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&saved) + sizeof(version), sizeof(saved) - sizeof(version))) return false;
        state->params = saved.params;
        storeScore(*state, saved.score);
        state->vox = saved.vox;
        state->lyric = saved.lyric;
        const uint32_t phraseLength = std::min<uint32_t>(saved.phraseLength, kVoxPhraseMaxChars - 1u);
        for (uint32_t i = 0; i < kVoxPhraseMaxChars; ++i) {
            state->voxPhrase[i].store(i < phraseLength ? saved.phrase[i] : 0u, std::memory_order_relaxed);
        }
        state->voxPhraseLength.store(phraseLength, std::memory_order_release);
        state->voxPhraseIndex = 0u;
        const uint32_t lyricsLength = std::min<uint32_t>(
            saved.lyricsLength, kVoxLyricsMaxChars - 1u);
        for (uint32_t i = 0; i < kVoxLyricsMaxChars; ++i) {
            state->voxLyrics[i].store(
                i < lyricsLength ? saved.lyrics[i] : 0u, std::memory_order_relaxed);
        }
        state->voxLyricsLength.store(lyricsLength, std::memory_order_release);
        loadedPresetIndex = saved.factoryPresetIndex;
        std::strncpy(loadedPresetName, saved.presetName.data(), sizeof(loadedPresetName) - 1u);
        if (saved.hasUserAtlas != 0u) {
            restoredUserBank = restoreUserAtlas(saved.userAtlas);
        }
#if defined(__APPLE__)
        state->guiPage = std::clamp(saved.guiPage, 0, 3);
        state->guiViewMode = std::clamp(saved.guiViewMode, -1, 2);
        state->guiViewAzDeg = std::clamp(saved.guiViewAzDeg, -180.0f, 180.0f);
        state->guiViewElDeg = std::clamp(saved.guiViewElDeg, -85.0f, 85.0f);
        state->guiViewZoom = std::clamp(saved.guiViewZoom, 0.55f, 2.4f);
#endif
    } else {
        return false;
    }
    if (version < 19u) {
        state->vox.worldPitchCents = std::clamp(
            state->vox.worldPitchCents + state->params.tuneCents, -2400.0f, 2400.0f);
        state->params.tuneCents = 0.0f;
        state->vox.circuitMode = VoxCircuitMode::Clean;
        state->vox.formantMacro = 0.0f;
        state->vox.bendMacro = 0.0f;
        state->vox.crushMacro = 0.0f;
        state->vox.rasp = 0.0f;
        state->vox.breath = 0.0f;
        state->vox.drive = 0.0f;
    }
    state->params.voices = std::clamp<uint32_t>(
        state->params.voices, 1u, s3g::kAmbiVoxMaxVoices);
    state->vox.worldAirColor = std::clamp(state->vox.worldAirColor, -1.0f, 1.0f);
    state->vox.phraseSpread = std::clamp(state->vox.phraseSpread, 0.0f, 1.0f);
    state->vox.orchestration = static_cast<s3g::AmbiVoxOrchestration>(
        std::clamp<uint32_t>(static_cast<uint32_t>(state->vox.orchestration), 0u, 5u));
    state->vox.contourMode = static_cast<s3g::AmbiVoxContourMode>(
        std::clamp<uint32_t>(static_cast<uint32_t>(state->vox.contourMode), 0u, 3u));
    state->lyric.mode = static_cast<VoxLyricMode>(
        std::clamp<uint32_t>(static_cast<uint32_t>(state->lyric.mode), 0u, 4u));
    state->lyric.cueBeats = std::clamp(state->lyric.cueBeats, 0.25f, 64.0f);
    state->lyric.cueBaseNote = std::clamp<uint32_t>(state->lyric.cueBaseNote, 0u, 127u);
    state->lyric.cueChannel = std::clamp<uint32_t>(state->lyric.cueChannel, 1u, 16u);
    if (version < kStateVersion) {
        const std::string phrase = loadVoxPhrase(*state);
        storeVoxLyricsText(*state, phrase.c_str());
    }
    state->factoryPresetIndex = (loadedPresetIndex >= 0
            && loadedPresetIndex < static_cast<int32_t>(kVoxFactoryPresetCount))
        ? loadedPresetIndex : -1;
    if (loadedPresetName[0] == '\0') {
        const char* fallback = state->factoryPresetIndex >= 0
            ? voxFactoryPreset(static_cast<uint32_t>(state->factoryPresetIndex)).name
            : "CUSTOM";
        std::snprintf(loadedPresetName, sizeof(loadedPresetName), "%s", fallback);
    }
    std::strncpy(state->presetName, loadedPresetName, sizeof(state->presetName) - 1u);
    state->presetName[sizeof(state->presetName) - 1u] = '\0';
    compileVoxPhrase(*state);
    rebuildVoxLyricScore(*state);
    std::atomic_store_explicit(&state->userBank, std::move(restoredUserBank), std::memory_order_release);
    syncWorldSmoothing(*state);
    resetVoxMidiVoices(*state);
    state->voxBankTimingResetRequested.store(true, std::memory_order_release);
    state->voxWorldTimingResetRequested.store(true, std::memory_order_release);
    for (uint32_t voice = 0u; voice < s3g::kAmbiVoxMaxVoices; ++voice) {
        state->voxPitchRatioSmooth[voice] = 1.0f;
        state->voxContourRatioSmooth[voice] = 1.0f;
        state->pvoc.reset(state->voxPvocVoice[voice], nullptr, 0.0);
        state->pvoc.reset(state->voxPvocNextVoice[voice], nullptr, 0.0);
        state->voxTargetNoteSmooth[voice] = voxVoiceTargetNote(*state, voice);
        state->voxVibratoPhase[voice] = s3g::ambiVotFract(static_cast<float>(voice) * 0.3819660113f);
    }
    state->engine.setParams(state->params);
    state->engine.setScore(loadScore(*state));
    state->params = state->engine.params();
    return true;
}
const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)

static NSColor* votColor(int rgb, double alpha = 1.0)
{
    return s3g::clap_gui::color(rgb, alpha);
}

static float votLinearToSrgb(float value)
{
    const float x = std::clamp(value, 0.0f, 1.0f);
    return x <= 0.0031308f ? x * 12.92f : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}

static NSColor* votPointColor(float azimuthDeg, float elevationDeg, float distance, bool selected)
{
    const float hue = std::fmod((azimuthDeg / 360.0f) + 1.0f, 1.0f);
    const float light = std::clamp((std::clamp(elevationDeg, -90.0f, 90.0f) + 90.0f) / 180.0f, 0.30f, 0.86f);
    const float chroma = std::clamp(distance / 2.0f, 0.10f, 1.0f) * 0.34f;
    const float a = std::cos(hue * 2.0f * static_cast<float>(M_PI)) * chroma;
    const float b = std::sin(hue * 2.0f * static_cast<float>(M_PI)) * chroma;
    const float l3 = light + 0.3963377774f * a + 0.2158037573f * b;
    const float m3 = light - 0.1055613458f * a - 0.0638541728f * b;
    const float s3 = light - 0.0894841775f * a - 1.2914855480f * b;
    const float l = l3 * l3 * l3;
    const float m = m3 * m3 * m3;
    const float s = s3 * s3 * s3;
    float r = votLinearToSrgb(4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s);
    float g = votLinearToSrgb(-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s);
    float blue = votLinearToSrgb(-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s);
    const float grayMix = selected ? 0.06f : 0.16f;
    r = r * (1.0f - grayMix) + 0.72f * grayMix;
    g = g * (1.0f - grayMix) + 0.72f * grayMix;
    blue = blue * (1.0f - grayMix) + 0.72f * grayMix;
    return [NSColor colorWithCalibratedRed:r green:g blue:blue alpha:selected ? 1.0 : 0.88];
}

static uint32_t voxEnsembleCohort(s3g::AmbiVoxOrchestration orchestration,
                                  uint32_t voice,
                                  uint32_t voices)
{
    if (orchestration == s3g::AmbiVoxOrchestration::Unison) return 0u;
    if (orchestration == s3g::AmbiVoxOrchestration::Chorale
        || orchestration == s3g::AmbiVoxOrchestration::Chorus
        || orchestration == s3g::AmbiVoxOrchestration::Round) {
        return voice % std::min<uint32_t>(std::max<uint32_t>(1u, voices), 4u);
    }
    return voice;
}

static NSString* voxEnsembleRoleLabel(s3g::AmbiVoxOrchestration orchestration,
                                      uint32_t voice,
                                      uint32_t voices)
{
    const uint32_t cohort = voxEnsembleCohort(orchestration, voice, voices);
    switch (orchestration) {
    case s3g::AmbiVoxOrchestration::Unison:
        return @"U";
    case s3g::AmbiVoxOrchestration::Chorale: {
        static NSString* labels[] = { @"S", @"A", @"T", @"B" };
        return labels[cohort % 4u];
    }
    case s3g::AmbiVoxOrchestration::Chorus: {
        static NSString* labels[] = { @"U1", @"U2", @"+8", @"-8" };
        return labels[cohort % 4u];
    }
    case s3g::AmbiVoxOrchestration::Round:
        return [NSString stringWithFormat:@"R%u", cohort + 1u];
    case s3g::AmbiVoxOrchestration::Cluster:
        if (voice == 0u) return @"0";
        return [NSString stringWithFormat:(voice & 1u) != 0u ? @"+%u" : @"-%u",
            (voice + 1u) / 2u];
    case s3g::AmbiVoxOrchestration::Individual:
    default:
        return @"";
    }
}

static NSColor* voxEnsembleRoleColor(s3g::AmbiVoxOrchestration orchestration,
                                     uint32_t voice,
                                     uint32_t voices)
{
    if (orchestration == s3g::AmbiVoxOrchestration::Individual) return votColor(0x8a8a8a);
    static constexpr int colors[] = { 0xc99c9c, 0xc2b074, 0x83aaa4, 0x929dbd };
    const uint32_t cohort = voxEnsembleCohort(orchestration, voice, voices);
    return votColor(colors[cohort % 4u]);
}

static std::vector<float> readWavMono(NSURL* url, int* sampleRateOut = nullptr)
{
    std::vector<float> result;
    if (sampleRateOut) *sampleRateOut = 0;
    NSData* data = [NSData dataWithContentsOfURL:url];
    if (!data || [data length] < 44) return result;
    const uint8_t* bytes = static_cast<const uint8_t*>([data bytes]);
    const size_t size = [data length];
    const auto u16 = [&](size_t offset) -> uint16_t {
        return offset + 1 < size ? static_cast<uint16_t>(bytes[offset] | (bytes[offset + 1] << 8)) : 0u;
    };
    const auto u32 = [&](size_t offset) -> uint32_t {
        return offset + 3 < size
            ? static_cast<uint32_t>(bytes[offset] | (bytes[offset + 1] << 8) | (bytes[offset + 2] << 16) | (bytes[offset + 3] << 24))
            : 0u;
    };
    if (std::memcmp(bytes, "RIFF", 4) != 0 || std::memcmp(bytes + 8, "WAVE", 4) != 0) return result;
    uint16_t audioFormat = 0;
    uint16_t channels = 0;
    uint16_t bits = 0;
    uint32_t sampleRate = 0;
    size_t dataOffset = 0;
    uint32_t dataSize = 0;
    for (size_t offset = 12; offset + 8 <= size;) {
        const uint32_t chunkSize = u32(offset + 4);
        if (offset + 8 + chunkSize > size) break;
        if (std::memcmp(bytes + offset, "fmt ", 4) == 0 && chunkSize >= 16) {
            audioFormat = u16(offset + 8);
            channels = u16(offset + 10);
            sampleRate = u32(offset + 12);
            bits = u16(offset + 22);
        } else if (std::memcmp(bytes + offset, "data", 4) == 0) {
            dataOffset = offset + 8;
            dataSize = chunkSize;
        }
        offset += 8 + chunkSize + (chunkSize & 1u);
    }
    if (dataOffset == 0 || channels == 0) return result;
    if (sampleRateOut) *sampleRateOut = static_cast<int>(sampleRate);
    const uint32_t bytesPerSample = bits / 8u;
    if (bytesPerSample == 0) return result;
    const uint32_t frameBytes = bytesPerSample * channels;
    const uint32_t frames = dataSize / std::max<uint32_t>(1u, frameBytes);
    result.reserve(frames);
    for (uint32_t frame = 0; frame < frames; ++frame) {
        const size_t offset = dataOffset + static_cast<size_t>(frame) * frameBytes;
        float sum = 0.0f;
        for (uint32_t channel = 0; channel < channels; ++channel) {
            const size_t p = offset + channel * bytesPerSample;
            float value = 0.0f;
            if (audioFormat == 3 && bits == 32 && p + 3 < size) {
                std::memcpy(&value, bytes + p, sizeof(float));
            } else if (audioFormat == 1 && bits == 16 && p + 1 < size) {
                const int16_t sample = static_cast<int16_t>(bytes[p] | (bytes[p + 1] << 8));
                value = static_cast<float>(sample) / 32768.0f;
            } else if (audioFormat == 1 && bits == 24 && p + 2 < size) {
                int32_t sample = static_cast<int32_t>(bytes[p] | (bytes[p + 1] << 8) | (bytes[p + 2] << 16));
                if ((sample & 0x800000) != 0) sample |= ~0xffffff;
                value = static_cast<float>(sample) / 8388608.0f;
            } else if (audioFormat == 1 && bits == 32 && p + 3 < size) {
                int32_t sample = 0;
                std::memcpy(&sample, bytes + p, sizeof(int32_t));
                value = static_cast<float>(sample) / 2147483648.0f;
            }
            sum += value;
        }
        result.push_back(sum / static_cast<float>(channels));
    }
    return result;
}

#if defined(S3G_HAS_WORLD)
static VoxWorldParameterData makeWorldParameterData(
    const std::vector<std::vector<double>>& spectrogramStorage,
    const std::vector<std::vector<double>>& aperiodicityStorage,
    int sampleRate,
    int fftSize,
    double framePeriodMs)
{
    VoxWorldParameterData result {};
    if (spectrogramStorage.empty() || aperiodicityStorage.size() != spectrogramStorage.size()
        || sampleRate <= 0 || fftSize <= 0) return result;
    result.sampleRate = sampleRate;
    result.fftSize = fftSize;
    result.framePeriodMs = framePeriodMs;
    result.frameCount = static_cast<uint32_t>(spectrogramStorage.size());
    result.spectralDimensions = std::min<uint32_t>(
        kVoxWorldSpectralDimensions, static_cast<uint32_t>(fftSize / 2));
    result.aperiodicityDimensions = static_cast<uint32_t>(
        std::max(0, GetNumberOfAperiodicities(sampleRate)));

    std::vector<double*> spectrogram(result.frameCount);
    std::vector<double*> aperiodicity(result.frameCount);
    for (uint32_t frame = 0u; frame < result.frameCount; ++frame) {
        spectrogram[frame] = const_cast<double*>(spectrogramStorage[frame].data());
        aperiodicity[frame] = const_cast<double*>(aperiodicityStorage[frame].data());
    }

    if (result.spectralDimensions > 0u) {
        std::vector<std::vector<double>> codedStorage(result.frameCount,
            std::vector<double>(result.spectralDimensions));
        std::vector<double*> coded(result.frameCount);
        for (uint32_t frame = 0u; frame < result.frameCount; ++frame) {
            coded[frame] = codedStorage[frame].data();
        }
        CodeSpectralEnvelope(spectrogram.data(), static_cast<int>(result.frameCount),
            sampleRate, fftSize, static_cast<int>(result.spectralDimensions), coded.data());
        result.codedSpectralEnvelope.resize(
            static_cast<size_t>(result.frameCount) * result.spectralDimensions);
        for (uint32_t frame = 0u; frame < result.frameCount; ++frame) {
            for (uint32_t dimension = 0u; dimension < result.spectralDimensions; ++dimension) {
                result.codedSpectralEnvelope[static_cast<size_t>(frame)
                    * result.spectralDimensions + dimension]
                    = static_cast<float>(codedStorage[frame][dimension]);
            }
        }
    }

    if (result.aperiodicityDimensions > 0u) {
        std::vector<std::vector<double>> codedStorage(result.frameCount,
            std::vector<double>(result.aperiodicityDimensions));
        std::vector<double*> coded(result.frameCount);
        for (uint32_t frame = 0u; frame < result.frameCount; ++frame) {
            coded[frame] = codedStorage[frame].data();
        }
        CodeAperiodicity(aperiodicity.data(), static_cast<int>(result.frameCount),
            sampleRate, fftSize, coded.data());
        result.codedAperiodicity.resize(
            static_cast<size_t>(result.frameCount) * result.aperiodicityDimensions);
        for (uint32_t frame = 0u; frame < result.frameCount; ++frame) {
            for (uint32_t dimension = 0u; dimension < result.aperiodicityDimensions; ++dimension) {
                result.codedAperiodicity[static_cast<size_t>(frame)
                    * result.aperiodicityDimensions + dimension]
                    = static_cast<float>(codedStorage[frame][dimension]);
            }
        }
    }

    const uint32_t sourceBins = static_cast<uint32_t>(fftSize / 2 + 1);
    result.runtimeLogEnvelope.resize(
        static_cast<size_t>(result.frameCount) * kVoxWorldRuntimeEnvelopeBins);
    for (uint32_t frame = 0u; frame < result.frameCount; ++frame) {
        for (uint32_t bin = 0u; bin < kVoxWorldRuntimeEnvelopeBins; ++bin) {
            const float sourcePosition = static_cast<float>(bin)
                * static_cast<float>(sourceBins - 1u)
                / static_cast<float>(kVoxWorldRuntimeEnvelopeBins - 1u);
            const uint32_t source0 = static_cast<uint32_t>(sourcePosition);
            const uint32_t source1 = std::min<uint32_t>(source0 + 1u, sourceBins - 1u);
            const double power = s3g::lerp(
                spectrogramStorage[frame][source0], spectrogramStorage[frame][source1],
                sourcePosition - static_cast<float>(source0));
            result.runtimeLogEnvelope[static_cast<size_t>(frame)
                * kVoxWorldRuntimeEnvelopeBins + bin]
                = static_cast<float>(std::log(std::max(1.0e-12, power)));
        }
    }
    return result;
}

static bool rebuildWorldRuntimeEnvelope(VoxWorldParameterData& parameters)
{
    if (parameters.frameCount == 0u || parameters.sampleRate <= 0
        || parameters.fftSize <= 0 || parameters.spectralDimensions == 0u
        || parameters.codedSpectralEnvelope.size()
            != static_cast<size_t>(parameters.frameCount) * parameters.spectralDimensions) return false;
    std::vector<std::vector<double>> codedStorage(parameters.frameCount,
        std::vector<double>(parameters.spectralDimensions));
    std::vector<double*> coded(parameters.frameCount);
    std::vector<std::vector<double>> decodedStorage(parameters.frameCount,
        std::vector<double>(static_cast<size_t>(parameters.fftSize / 2 + 1)));
    std::vector<double*> decoded(parameters.frameCount);
    for (uint32_t frame = 0u; frame < parameters.frameCount; ++frame) {
        for (uint32_t dimension = 0u; dimension < parameters.spectralDimensions; ++dimension) {
            codedStorage[frame][dimension] = parameters.codedSpectralEnvelope[
                static_cast<size_t>(frame) * parameters.spectralDimensions + dimension];
        }
        coded[frame] = codedStorage[frame].data();
        decoded[frame] = decodedStorage[frame].data();
    }
    DecodeSpectralEnvelope(coded.data(), static_cast<int>(parameters.frameCount),
        parameters.sampleRate, parameters.fftSize,
        static_cast<int>(parameters.spectralDimensions), decoded.data());
    const uint32_t sourceBins = static_cast<uint32_t>(parameters.fftSize / 2 + 1);
    parameters.runtimeLogEnvelope.resize(
        static_cast<size_t>(parameters.frameCount) * kVoxWorldRuntimeEnvelopeBins);
    for (uint32_t frame = 0u; frame < parameters.frameCount; ++frame) {
        for (uint32_t bin = 0u; bin < kVoxWorldRuntimeEnvelopeBins; ++bin) {
            const float sourcePosition = static_cast<float>(bin)
                * static_cast<float>(sourceBins - 1u)
                / static_cast<float>(kVoxWorldRuntimeEnvelopeBins - 1u);
            const uint32_t source0 = static_cast<uint32_t>(sourcePosition);
            const uint32_t source1 = std::min<uint32_t>(source0 + 1u, sourceBins - 1u);
            const double power = s3g::lerp(decodedStorage[frame][source0],
                decodedStorage[frame][source1], sourcePosition - static_cast<float>(source0));
            parameters.runtimeLogEnvelope[static_cast<size_t>(frame)
                * kVoxWorldRuntimeEnvelopeBins + bin]
                = static_cast<float>(std::log(std::max(1.0e-12, power)));
        }
    }
    return true;
}

static std::shared_ptr<VoxWorldSample> makeWorldSampleFromWave(
    const std::vector<float>& wave, int sampleRate, const std::string& name,
    std::array<std::vector<float>, kVoxWorldPitchAnchorCount - 1u>* pitchVariants = nullptr)
{
    if (wave.size() < 256u || sampleRate <= 0) return nullptr;
    std::vector<double> x(wave.size());
    for (size_t i = 0; i < wave.size(); ++i) x[i] = static_cast<double>(std::clamp(wave[i], -1.0f, 1.0f));

    DioOption dio {};
    InitializeDioOption(&dio);
    dio.frame_period = 5.0;
    const int f0Length = GetSamplesForDIO(sampleRate, static_cast<int>(x.size()), dio.frame_period);
    if (f0Length <= 0) return nullptr;
    std::vector<double> temporalPositions(static_cast<size_t>(f0Length));
    std::vector<double> f0(static_cast<size_t>(f0Length));
    std::vector<double> refinedF0(static_cast<size_t>(f0Length));
    Dio(x.data(), static_cast<int>(x.size()), sampleRate, &dio, temporalPositions.data(), f0.data());
    StoneMask(x.data(), static_cast<int>(x.size()), sampleRate, temporalPositions.data(), f0.data(), f0Length, refinedF0.data());

    // File and voicebank analysis is offline, so Harvest's stronger voiced
    // continuity is worth its additional analysis cost. DIO/StoneMask remains
    // a per-frame fallback for unusual material.
    HarvestOption harvest {};
    InitializeHarvestOption(&harvest);
    harvest.frame_period = dio.frame_period;
    harvest.f0_floor = dio.f0_floor;
    harvest.f0_ceil = dio.f0_ceil;
    const int harvestLength = GetSamplesForHarvest(
        sampleRate, static_cast<int>(x.size()), harvest.frame_period);
    if (harvestLength == f0Length) {
        std::vector<double> harvestPositions(static_cast<size_t>(harvestLength));
        std::vector<double> harvestF0(static_cast<size_t>(harvestLength));
        Harvest(x.data(), static_cast<int>(x.size()), sampleRate, &harvest,
            harvestPositions.data(), harvestF0.data());
        for (int frame = 0; frame < f0Length; ++frame) {
            if (harvestF0[static_cast<size_t>(frame)] > 0.0) {
                refinedF0[static_cast<size_t>(frame)] = harvestF0[static_cast<size_t>(frame)];
            }
        }
    }

    CheapTrickOption cheapTrick {};
    InitializeCheapTrickOption(sampleRate, &cheapTrick);
    cheapTrick.fft_size = GetFFTSizeForCheapTrick(sampleRate, &cheapTrick);
    const int bins = cheapTrick.fft_size / 2 + 1;
    std::vector<std::vector<double>> spectrogramStorage(static_cast<size_t>(f0Length), std::vector<double>(static_cast<size_t>(bins)));
    std::vector<double*> spectrogram(static_cast<size_t>(f0Length));
    for (int i = 0; i < f0Length; ++i) spectrogram[static_cast<size_t>(i)] = spectrogramStorage[static_cast<size_t>(i)].data();
    CheapTrick(x.data(), static_cast<int>(x.size()), sampleRate, temporalPositions.data(), refinedF0.data(), f0Length, &cheapTrick, spectrogram.data());

    D4COption d4c {};
    InitializeD4COption(&d4c);
    std::vector<std::vector<double>> aperiodicityStorage(static_cast<size_t>(f0Length), std::vector<double>(static_cast<size_t>(bins)));
    std::vector<double*> aperiodicity(static_cast<size_t>(f0Length));
    for (int i = 0; i < f0Length; ++i) aperiodicity[static_cast<size_t>(i)] = aperiodicityStorage[static_cast<size_t>(i)].data();
    D4C(x.data(), static_cast<int>(x.size()), sampleRate, temporalPositions.data(), refinedF0.data(), f0Length, cheapTrick.fft_size, &d4c, aperiodicity.data());

    auto result = std::make_shared<VoxWorldSample>();
    result->sampleRate = sampleRate;
    result->framePeriodMs = dio.frame_period;
    result->name = name;
    result->parameters = makeWorldParameterData(spectrogramStorage,
        aperiodicityStorage, sampleRate, cheapTrick.fft_size, dio.frame_period);
    result->f0.resize(static_cast<size_t>(f0Length));
    result->frameEnergy.resize(static_cast<size_t>(f0Length));
    std::vector<float> voicedF0;
    voicedF0.reserve(refinedF0.size());
    for (size_t frame = 0u; frame < refinedF0.size(); ++frame) {
        const float frequency = static_cast<float>(std::max(0.0, refinedF0[frame]));
        result->f0[frame] = frequency;
        if (frequency >= 40.0f && frequency <= 1600.0f) voicedF0.push_back(frequency);
    }
    if (!voicedF0.empty()) {
        const auto middle = voicedF0.begin() + static_cast<std::ptrdiff_t>(voicedF0.size() / 2u);
        std::nth_element(voicedF0.begin(), middle, voicedF0.end());
        result->baseMidi = std::clamp(
            static_cast<int>(std::lround(69.0 + 12.0 * std::log2(static_cast<double>(*middle) / 440.0))),
            0, 127);
    }

    const auto renderPitchAnchor = [&](int semitoneOffset, std::vector<float>& destination) {
        std::vector<double> synthesisF0 = refinedF0;
        const double ratio = std::pow(2.0, static_cast<double>(semitoneOffset) / 12.0);
        if (semitoneOffset != 0) {
            for (double& frequency : synthesisF0) {
                if (frequency > 0.0) frequency = std::clamp(frequency * ratio, 20.0, 2400.0);
            }
        }
        std::vector<double> y(x.size());
        Synthesis(synthesisF0.data(), f0Length, spectrogram.data(), aperiodicity.data(),
            cheapTrick.fft_size, dio.frame_period, sampleRate,
            static_cast<int>(y.size()), y.data());
        destination.resize(y.size());
        float peak = 0.0f;
        for (size_t i = 0u; i < y.size(); ++i) {
            const float value = std::isfinite(y[i])
                ? static_cast<float>(std::clamp(y[i], -1.0, 1.0))
                : 0.0f;
            destination[i] = value;
            peak = std::max(peak, std::fabs(value));
        }
        if (peak > 0.86f) {
            const float gain = 0.86f / peak;
            for (float& value : destination) value *= gain;
        }
    };

    renderPitchAnchor(0, result->samples);
    if (pitchVariants) {
        for (uint32_t anchor = 0u; anchor < kVoxWorldPitchAnchorCount; ++anchor) {
            if (anchor == kVoxWorldPitchAnchorCenter) continue;
            const uint32_t variant = anchor < kVoxWorldPitchAnchorCenter ? anchor : anchor - 1u;
            renderPitchAnchor(kVoxWorldPitchAnchorSemitones[anchor], (*pitchVariants)[variant]);
        }
    }

    const int frameRadius = std::max(1,
        static_cast<int>(0.5 * dio.frame_period * static_cast<double>(sampleRate) / 1000.0));
    for (int frame = 0; frame < f0Length; ++frame) {
        const int center = static_cast<int>(std::round(
            temporalPositions[static_cast<size_t>(frame)] * static_cast<double>(sampleRate)));
        double sum = 0.0;
        int count = 0;
        for (int offset = -frameRadius; offset <= frameRadius; ++offset) {
            const int sampleIndex = center + offset;
            if (sampleIndex < 0 || sampleIndex >= static_cast<int>(result->samples.size())) continue;
            const double value = result->samples[static_cast<size_t>(sampleIndex)];
            sum += value * value;
            ++count;
        }
        result->frameEnergy[static_cast<size_t>(frame)] = count > 0
            ? static_cast<float>(std::sqrt(sum / static_cast<double>(count)))
            : 0.0f;
    }
    return result;
}

struct VoxWorldCacheHeader {
    std::array<char, 8> magic { 'S', '3', 'G', 'V', 'W', 'L', 'D', '4' };
    uint32_t version = 4u;
    int32_t sampleRate = 0;
    int32_t baseMidi = 60;
    int32_t fftSize = 0;
    uint32_t frameCount = 0u;
    uint32_t spectralDimensions = 0u;
    uint32_t aperiodicityDimensions = 0u;
    double framePeriodMs = 5.0;
    uint64_t sampleCount = 0u;
    uint64_t f0Count = 0u;
    uint64_t energyCount = 0u;
    uint64_t spectralCount = 0u;
    uint64_t aperiodicityCount = 0u;
    std::array<uint64_t, kVoxWorldPitchAnchorCount - 1u> variantCount {};
};

static uint64_t voxCacheHash(const std::string& text)
{
    uint64_t hash = 1469598103934665603ull;
    for (const unsigned char byte : text) {
        hash ^= static_cast<uint64_t>(byte);
        hash *= 1099511628211ull;
    }
    return hash;
}

static std::string voxWorldCachePath(NSURL* wavURL)
{
    if (!wavURL) return {};
    NSString* path = [wavURL path];
    NSDictionary* attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:path error:nil];
    const unsigned long long fileSize = attrs ? [attrs fileSize] : 0ull;
    const NSTimeInterval modified = attrs && attrs[NSFileModificationDate]
        ? [attrs[NSFileModificationDate] timeIntervalSince1970]
        : 0.0;
    const char* utf8 = [path UTF8String];
    const std::string identity = std::string(utf8 ? utf8 : "") + "|"
        + std::to_string(fileSize) + "|" + std::to_string(modified) + "|world-cache-v4";
    NSArray<NSString*>* cacheRoots = NSSearchPathForDirectoriesInDomains(
        NSCachesDirectory, NSUserDomainMask, YES);
    if ([cacheRoots count] == 0u) return {};
    NSString* folder = [[cacheRoots firstObject]
        stringByAppendingPathComponent:@"org.s3g.s3g-dsp/AmbiVoxWorld"];
    if (![[NSFileManager defaultManager] createDirectoryAtPath:folder
        withIntermediateDirectories:YES attributes:nil error:nil]) return {};
    NSString* file = [NSString stringWithFormat:@"%016llx.s3gworld",
        static_cast<unsigned long long>(voxCacheHash(identity))];
    const char* cachePath = [[folder stringByAppendingPathComponent:file] UTF8String];
    return cachePath ? cachePath : "";
}

template <typename T>
static bool voxReadCacheVector(std::ifstream& stream, std::vector<T>& values,
                               uint64_t count, uint64_t maximum)
{
    if (count > maximum) return false;
    values.resize(static_cast<size_t>(count));
    if (values.empty()) return true;
    return static_cast<bool>(stream.read(reinterpret_cast<char*>(values.data()),
        static_cast<std::streamsize>(values.size() * sizeof(T))));
}

template <typename T>
static bool voxWriteCacheVector(std::ofstream& stream, const std::vector<T>& values)
{
    if (values.empty()) return true;
    stream.write(reinterpret_cast<const char*>(values.data()),
        static_cast<std::streamsize>(values.size() * sizeof(T)));
    return static_cast<bool>(stream);
}

static std::shared_ptr<const VoxVoicebankAudio> readVoxWorldCache(NSURL* wavURL)
{
    const std::string path = voxWorldCachePath(wavURL);
    if (path.empty()) return nullptr;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return nullptr;
    VoxWorldCacheHeader header {};
    if (!stream.read(reinterpret_cast<char*>(&header), sizeof(header))) return nullptr;
    const std::array<char, 8> expected { 'S', '3', 'G', 'V', 'W', 'L', 'D', '4' };
    if (header.magic != expected || header.version != 4u
        || header.sampleRate < 8000 || header.sampleRate > 384000
        || header.fftSize < 256 || header.fftSize > 32768
        || header.frameCount == 0u || header.frameCount > 2000000u
        || header.spectralDimensions == 0u || header.spectralDimensions > 256u
        || header.aperiodicityDimensions == 0u || header.aperiodicityDimensions > 16u
        || header.sampleCount < 32u || header.sampleCount > 40000000u) return nullptr;
    auto audio = std::make_shared<VoxVoicebankAudio>();
    audio->sampleRate = header.sampleRate;
    audio->baseMidi = std::clamp(header.baseMidi, 0, 127);
    audio->worldFramePeriodMs = std::clamp(header.framePeriodMs, 1.0, 20.0);
    if (!voxReadCacheVector(stream, audio->samples, header.sampleCount, 40000000u)) return nullptr;
    for (uint32_t variant = 0u; variant < audio->worldPitchVariants.size(); ++variant) {
        if (!voxReadCacheVector(stream, audio->worldPitchVariants[variant],
            header.variantCount[variant], 40000000u)) return nullptr;
        if (audio->worldPitchVariants[variant].size() != audio->samples.size()) return nullptr;
    }
    if (!voxReadCacheVector(stream, audio->worldF0, header.f0Count, 2000000u)
        || !voxReadCacheVector(stream, audio->worldFrameEnergy, header.energyCount, 2000000u)) return nullptr;
    if (audio->worldF0.size() != header.frameCount
        || audio->worldFrameEnergy.size() != header.frameCount) return nullptr;
    auto& parameters = audio->worldParameters;
    parameters.sampleRate = header.sampleRate;
    parameters.fftSize = header.fftSize;
    parameters.framePeriodMs = audio->worldFramePeriodMs;
    parameters.frameCount = header.frameCount;
    parameters.spectralDimensions = header.spectralDimensions;
    parameters.aperiodicityDimensions = header.aperiodicityDimensions;
    if (header.spectralCount != static_cast<uint64_t>(header.frameCount)
            * header.spectralDimensions
        || header.aperiodicityCount != static_cast<uint64_t>(header.frameCount)
            * header.aperiodicityDimensions
        || !voxReadCacheVector(stream, parameters.codedSpectralEnvelope,
            header.spectralCount, 100000000u)
        || !voxReadCacheVector(stream, parameters.codedAperiodicity,
            header.aperiodicityCount, 32000000u)
        || !rebuildWorldRuntimeEnvelope(parameters)) return nullptr;
    audio->worldResynthesized = true;
    return audio;
}

static void writeVoxWorldCache(NSURL* wavURL, const VoxVoicebankAudio& audio)
{
    if (!audio.worldResynthesized || audio.samples.empty()
        || !audio.worldParameters.ready()
        || audio.worldF0.size() != audio.worldParameters.frameCount
        || audio.worldFrameEnergy.size() != audio.worldParameters.frameCount) return;
    const std::string path = voxWorldCachePath(wavURL);
    if (path.empty()) return;
    const std::string temporary = path + ".tmp";
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) return;
    VoxWorldCacheHeader header {};
    header.sampleRate = audio.sampleRate;
    header.baseMidi = audio.baseMidi;
    header.fftSize = audio.worldParameters.fftSize;
    header.frameCount = audio.worldParameters.frameCount;
    header.spectralDimensions = audio.worldParameters.spectralDimensions;
    header.aperiodicityDimensions = audio.worldParameters.aperiodicityDimensions;
    header.framePeriodMs = audio.worldFramePeriodMs;
    header.sampleCount = audio.samples.size();
    header.f0Count = audio.worldF0.size();
    header.energyCount = audio.worldFrameEnergy.size();
    header.spectralCount = audio.worldParameters.codedSpectralEnvelope.size();
    header.aperiodicityCount = audio.worldParameters.codedAperiodicity.size();
    for (uint32_t variant = 0u; variant < audio.worldPitchVariants.size(); ++variant) {
        header.variantCount[variant] = audio.worldPitchVariants[variant].size();
    }
    stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
    bool ok = static_cast<bool>(stream) && voxWriteCacheVector(stream, audio.samples);
    for (const auto& variant : audio.worldPitchVariants) ok = ok && voxWriteCacheVector(stream, variant);
    ok = ok && voxWriteCacheVector(stream, audio.worldF0)
        && voxWriteCacheVector(stream, audio.worldFrameEnergy)
        && voxWriteCacheVector(stream, audio.worldParameters.codedSpectralEnvelope)
        && voxWriteCacheVector(stream, audio.worldParameters.codedAperiodicity);
    stream.close();
    if (ok) {
        std::rename(temporary.c_str(), path.c_str());
    } else {
        std::remove(temporary.c_str());
    }
}
#endif

@interface S3GAmbiVoxEncoderView : NSView <NSTextFieldDelegate, NSTextViewDelegate> {
    Plugin* _plugin;
    NSTimer* _timer;
    NSTextField* _phraseField;
    NSScrollView* _lyricsScroll;
    NSTextView* _lyricsEditor;
    BOOL _updatingLyricsEditor;
    clap_id _dragParam;
    int _dragArea;
    int _openMenu;
    int _hoverMenuItem;
    uint32_t _menuItemCount;
    NSPoint _menuOrigin;
    CGFloat _menuWidth;
    int _leftPage;
    uint32_t _selectedVoice;
    uint32_t _selectedScoreNode;
    uint32_t _selectedLyricCue;
    int _scoreDragLane;
    int _viewMode;
    CGFloat _viewAzDeg;
    CGFloat _viewElDeg;
    CGFloat _viewZoom;
    BOOL _dragView;
    NSPoint _lastDragPoint;
    std::array<std::array<s3g::AmbiVotMotionPoint, 48>, s3g::kAmbiVoxMaxVoices> _trails;
    uint32_t _trailHead;
    uint32_t _trailCount;
}
- (instancetype)initWithPlugin:(Plugin*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)phraseTextChanged:(id)sender;
- (void)markPresetCustom;
- (void)applyFactoryPreset:(uint32_t)index;
- (void)loadUserPreset;
- (void)saveUserPreset;
@end

@implementation S3GAmbiVoxEncoderView

- (instancetype)initWithPlugin:(Plugin*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiW, kGuiH)];
    if (self) {
        _plugin = plugin;
        _timer = nil;
        _phraseField = [[NSTextField alloc] initWithFrame:NSMakeRect(912, 736, 214, 22)];
        [_phraseField setStringValue:[NSString stringWithUTF8String:(plugin ? loadVoxPhrase(*plugin).c_str() : "")]];
        [_phraseField setDelegate:self];
        [_phraseField setTarget:self];
        [_phraseField setAction:@selector(phraseTextChanged:)];
        s3g::clap_gui::styleNumberTextField(_phraseField, 11.0, NSTextAlignmentLeft);
        [_phraseField setPlaceholderString:@"type phrase or spaced romaji"];
        [self addSubview:_phraseField];
        _lyricsScroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(44, 104, 544, 420)];
        [_lyricsScroll setHasVerticalScroller:YES];
        [_lyricsScroll setHasHorizontalScroller:NO];
        [_lyricsScroll setBorderType:NSNoBorder];
        [_lyricsScroll setDrawsBackground:YES];
        [_lyricsScroll setBackgroundColor:votColor(0x0b0b0b)];
        _lyricsEditor = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 526, 420)];
        _updatingLyricsEditor = YES;
        [_lyricsEditor setDelegate:self];
        [_lyricsEditor setRichText:NO];
        [_lyricsEditor setImportsGraphics:NO];
        [_lyricsEditor setAllowsUndo:YES];
        [_lyricsEditor setHorizontallyResizable:NO];
        [_lyricsEditor setVerticallyResizable:YES];
        [_lyricsEditor setAutoresizingMask:NSViewWidthSizable];
        [[_lyricsEditor textContainer] setWidthTracksTextView:YES];
        [[_lyricsEditor textContainer] setContainerSize:NSMakeSize(526, CGFLOAT_MAX)];
        [_lyricsEditor setFont:[NSFont systemFontOfSize:11.0 weight:NSFontWeightRegular]];
        [_lyricsEditor setTextColor:votColor(0xb0b0b0)];
        [_lyricsEditor setInsertionPointColor:votColor(0xd0d0d0)];
        [_lyricsEditor setBackgroundColor:votColor(0x0b0b0b)];
        const std::string lyrics = plugin ? loadVoxLyrics(*plugin) : std::string();
        [_lyricsEditor setString:[NSString stringWithUTF8String:lyrics.c_str()]];
        _updatingLyricsEditor = NO;
        [_lyricsScroll setDocumentView:_lyricsEditor];
        [_lyricsScroll setHidden:YES];
        [self addSubview:_lyricsScroll];
        _dragParam = CLAP_INVALID_ID;
        _dragArea = 0;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuItemCount = 0;
        _menuOrigin = NSMakePoint(0, 0);
        _menuWidth = 124.0;
        _leftPage = plugin && plugin->guiPage == 3 ? 3 : 0;
        _selectedVoice = 0;
        _selectedScoreNode = 0;
        _selectedLyricCue = plugin
            ? plugin->guiLyricCue.load(std::memory_order_relaxed) : 0u;
        _scoreDragLane = -1;
        _viewMode = plugin ? plugin->guiViewMode : 2;
        _viewAzDeg = plugin ? plugin->guiViewAzDeg : 38.0;
        _viewElDeg = plugin ? plugin->guiViewElDeg : 32.0;
        _viewZoom = plugin ? plugin->guiViewZoom : 1.0;
        _dragView = NO;
        _lastDragPoint = NSMakePoint(0, 0);
        _trailHead = 0;
        _trailCount = 0;
        [self setWantsLayer:YES];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }

- (void)phraseTextChanged:(id)sender
{
    (void)sender;
    if (!_plugin || !_phraseField) return;
    replaceVoxLyricCueText(*_plugin, _selectedLyricCue,
        [[_phraseField stringValue] UTF8String]);
    if (_lyricsEditor) {
        const std::string lyrics = loadVoxLyrics(*_plugin);
        _updatingLyricsEditor = YES;
        [_lyricsEditor setString:[NSString stringWithUTF8String:lyrics.c_str()]];
        _updatingLyricsEditor = NO;
    }
    [self markPresetCustom];
}

- (void)controlTextDidChange:(NSNotification*)notification
{
    (void)notification;
    [self phraseTextChanged:_phraseField];
}

- (void)textDidChange:(NSNotification*)notification
{
    if (!_plugin || _updatingLyricsEditor || [notification object] != _lyricsEditor) return;
    storeVoxLyricsText(*_plugin, [[_lyricsEditor string] UTF8String]);
    rebuildVoxLyricScore(*_plugin);
    const uint32_t count = std::max<uint32_t>(1u, voxLyricCueCount(*_plugin));
    _selectedLyricCue = std::min<uint32_t>(_selectedLyricCue, count - 1u);
    requestVoxLyricCue(*_plugin, _selectedLyricCue);
    const std::string cue = voxLyricCueText(*_plugin, _selectedLyricCue);
    [_phraseField setStringValue:[NSString stringWithUTF8String:cue.c_str()]];
    [self markPresetCustom];
    [self setNeedsDisplay:YES];
}

- (void)updateTrackingAreas
{
    for (NSTrackingArea* area in [self trackingAreas]) [self removeTrackingArea:area];
    [super updateTrackingAreas];
    const NSTrackingAreaOptions options = NSTrackingMouseMoved | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect;
    NSTrackingArea* area = [[NSTrackingArea alloc] initWithRect:NSZeroRect options:options owner:self userInfo:nil];
    [self addTrackingArea:[area autorelease]];
}

- (void)storeViewState
{
    if (!_plugin) return;
    _plugin->guiPage = _leftPage;
    _plugin->guiViewMode = _viewMode;
    _plugin->guiViewAzDeg = static_cast<float>(_viewAzDeg);
    _plugin->guiViewElDeg = static_cast<float>(_viewElDeg);
    _plugin->guiViewZoom = static_cast<float>(_viewZoom);
}

- (s3g::AmbiVotMotionPoint)snapshotPoint:(uint32_t)index
{
    s3g::AmbiVotMotionPoint point {};
    if (!_plugin || index >= s3g::kAmbiVoxMaxVoices) return point;
    point.azimuthDeg = _plugin->guiAzimuth[index].load(std::memory_order_relaxed);
    point.elevationDeg = _plugin->guiElevation[index].load(std::memory_order_relaxed);
    point.distance = _plugin->guiDistance[index].load(std::memory_order_relaxed);
    point.u = _plugin->guiU[index].load(std::memory_order_relaxed);
    point.v = _plugin->guiV[index].load(std::memory_order_relaxed);
    return point;
}

- (void)captureTrails
{
    if (!_plugin) return;
    for (uint32_t i = 0; i < s3g::kAmbiVoxMaxVoices; ++i) _trails[i][_trailHead] = [self snapshotPoint:i];
    _trailHead = (_trailHead + 1u) % 48u;
    _trailCount = std::min<uint32_t>(48u, _trailCount + 1u);
}

- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [NSTimer timerWithTimeInterval:1.0 / 30.0 target:self selector:@selector(refresh:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}

- (void)stopRefreshTimer
{
    if (!_timer) return;
    [_timer invalidate];
    _timer = nil;
}

- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    if ([self isHidden] || !_plugin || !_plugin->guiVisible.load(std::memory_order_relaxed)) return;
    if (!s3g::clap_support::hostAppIsActive()) return;
    const uint32_t cueCount = voxLyricCueCount(*_plugin);
    if (cueCount > 0u && _plugin->lyric.mode != VoxLyricMode::Loop
        && ![_phraseField currentEditor]
        && [[self window] firstResponder] != _lyricsEditor) {
        const uint32_t activeCue = _plugin->guiLyricCue.load(std::memory_order_relaxed) % cueCount;
        if (activeCue != _selectedLyricCue) {
            _selectedLyricCue = activeCue;
            const std::string cue = voxLyricCueText(*_plugin, activeCue);
            [_phraseField setStringValue:[NSString stringWithUTF8String:cue.c_str()]];
        }
    }
    [self captureTrails];
    [self setNeedsDisplay:YES];
}

- (void)dealloc
{
    [self storeViewState];
    [self stopRefreshTimer];
    [_lyricsScroll release];
    [_lyricsEditor release];
    [_phraseField release];
    [super dealloc];
}

- (NSRect)leftPanelRect { return NSMakeRect(18, 42, 596, 834); }
- (NSRect)leftContentRect { return NSMakeRect(34, 72, 564, 788); }

- (NSRect)pageButtonRect:(int)index
{
    return NSMakeRect(130.0 + static_cast<CGFloat>(index) * 58.0, 46.0, 52.0, 13.0);
}

- (NSRect)viewButtonRect:(int)index
{
    return NSMakeRect(430.0 + static_cast<CGFloat>(index) * 49.0, 46.0, 43.0, 13.0);
}

- (NSRect)zoomButtonRect:(int)index
{
    return NSMakeRect(378.0 + static_cast<CGFloat>(index) * 23.0, 46.0, 18.0, 13.0);
}

- (NSRect)synthLoadButtonRect { return NSMakeRect(824, 46, 48, 13); }
- (NSRect)presetMenuRect { return NSMakeRect(382, 13, 190, 15); }
- (NSRect)presetLoadButtonRect { return NSMakeRect(580, 13, 48, 15); }
- (NSRect)presetSaveButtonRect { return NSMakeRect(636, 13, 48, 15); }
- (NSRect)worldResetButtonRect { return NSMakeRect(1016, 484, 56, 13); }
- (NSRect)encodedLoadButtonRect { return NSMakeRect(1078, 484, 56, 13); }
- (NSRect)scoreRemoveButtonRect { return NSMakeRect(1032, 484, 18, 13); }
- (NSRect)scoreAddButtonRect { return NSMakeRect(1054, 484, 18, 13); }
- (NSRect)scoreResetButtonRect { return NSMakeRect(1080, 484, 54, 13); }

- (NSString*)presetDisplayName
{
    NSString* name = _plugin && _plugin->presetName[0] != '\0'
        ? [NSString stringWithUTF8String:_plugin->presetName]
        : @"CUSTOM";
    if ([name length] <= 22u) return name;
    return [[name substringToIndex:19u] stringByAppendingString:@"..."];
}

- (void)markPresetCustom
{
    if (!_plugin) return;
    _plugin->factoryPresetIndex = -1;
    std::snprintf(_plugin->presetName, sizeof(_plugin->presetName), "%s", "CUSTOM");
}

- (void)setViewPreset:(int)mode
{
    _viewMode = mode;
    if (mode == 0) {
        _viewAzDeg = 0.0;
        _viewElDeg = 0.0;
    } else if (mode == 1) {
        _viewAzDeg = 90.0;
        _viewElDeg = 90.0;
    } else {
        _viewAzDeg = 38.0;
        _viewElDeg = 32.0;
    }
    [self storeViewState];
    [self setNeedsDisplay:YES];
}

- (CGFloat)viewScaleForRect:(NSRect)rect
{
    return std::min(rect.size.width, rect.size.height) * 0.25 * std::clamp(_viewZoom, 0.55, 2.20);
}

- (NSPoint)projectWorldPoint:(s3g::Vec3)point rect:(NSRect)rect depth:(CGFloat*)depth
{
    const CGFloat centerX = NSMidX(rect);
    const CGFloat centerY = NSMidY(rect);
    const CGFloat scale = [self viewScaleForRect:rect];
    if (_viewMode == 0) {
        if (depth) *depth = static_cast<CGFloat>(point.z);
        return NSMakePoint(centerX - static_cast<CGFloat>(point.y) * scale,
                           centerY - static_cast<CGFloat>(point.x) * scale);
    }
    if (_viewMode == 1) {
        if (depth) *depth = static_cast<CGFloat>(point.x);
        return NSMakePoint(centerX - static_cast<CGFloat>(point.y) * scale,
                           centerY - static_cast<CGFloat>(point.z) * scale);
    }
    const float azimuth = static_cast<float>(_viewAzDeg * M_PI / 180.0);
    const float elevation = static_cast<float>(_viewElDeg * M_PI / 180.0);
    const float ca = std::cos(azimuth);
    const float sa = std::sin(azimuth);
    const float ce = std::cos(elevation);
    const float se = std::sin(elevation);
    const float x1 = ca * point.x - sa * point.y;
    const float y1 = sa * point.x + ca * point.y;
    const float y2 = ce * y1 - se * point.z;
    const float z2 = se * y1 + ce * point.z;
    if (depth) *depth = static_cast<CGFloat>(z2);
    return NSMakePoint(centerX + static_cast<CGFloat>(x1) * scale,
                       centerY - static_cast<CGFloat>(y2) * scale);
}

- (NSPoint)projectMotionPoint:(const s3g::AmbiVotMotionPoint&)point rect:(NSRect)rect depth:(CGFloat*)depth
{
    const s3g::Vec3 direction = s3g::directionFromAed(point.azimuthDeg, point.elevationDeg);
    return [self projectWorldPoint:{ direction.x * point.distance, direction.y * point.distance, direction.z * point.distance }
                             rect:rect depth:depth];
}

- (void)drawPageButtons:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    static NSString* labels[] = { @"FIELD", @"LYRICS" };
    static constexpr int pages[] = { 0, 3 };
    const NSRect header = NSMakeRect(18, 42, 596, 21);
    for (int i = 0; i < 2; ++i) {
        s3g::clap_gui::drawHeaderButton([self pageButtonRect:i], header, labels[i], pages[i] == _leftPage, attrs, style);
    }
}

- (void)drawViewButtons:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    static NSString* labels[] = { @"TOP", @"SIDE", @"3/4" };
    const NSRect header = NSMakeRect(18, 42, 596, 21);
    for (int i = 0; i < 3; ++i) {
        s3g::clap_gui::drawHeaderButton([self viewButtonRect:i], header, labels[i], i == _viewMode, attrs, style);
    }
    s3g::clap_gui::drawHeaderButton([self zoomButtonRect:0], header, @"-", false, attrs, style);
    s3g::clap_gui::drawHeaderButton([self zoomButtonRect:1], header, @"+", false, attrs, style);
}

- (void)drawField:(NSRect)rect attrs:(NSDictionary*)attrs
{
    [votColor(0x0b0b0b) setFill];
    NSRectFill(rect);
    [votColor(0x575757) setStroke];
    NSFrameRect(rect);
    [NSGraphicsContext saveGraphicsState];
    [[NSBezierPath bezierPathWithRect:NSInsetRect(rect, 1, 1)] addClip];

    const CGFloat radius = [self viewScaleForRect:rect];
    [votColor(0x343434) setStroke];
    NSBezierPath* sphere = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(NSMidX(rect) - radius, NSMidY(rect) - radius, radius * 2.0, radius * 2.0)];
    [sphere setLineWidth:0.8];
    [sphere stroke];
    NSBezierPath* axes = [NSBezierPath bezierPath];
    [axes moveToPoint:NSMakePoint(NSMidX(rect), NSMinY(rect) + 8)];
    [axes lineToPoint:NSMakePoint(NSMidX(rect), NSMaxY(rect) - 8)];
    [axes moveToPoint:NSMakePoint(NSMinX(rect) + 8, NSMidY(rect))];
    [axes lineToPoint:NSMakePoint(NSMaxX(rect) - 8, NSMidY(rect))];
    [axes setLineWidth:0.6];
    [axes stroke];

    const uint32_t voices = std::clamp<uint32_t>(_plugin->params.voices, 1u, s3g::kAmbiVoxMaxVoices);
    _selectedVoice = std::min<uint32_t>(_selectedVoice, voices - 1u);
    struct ProjectedPoint {
        uint32_t index;
        CGFloat depth;
        NSPoint point;
        s3g::Vec3 world;
        s3g::AmbiVotMotionPoint motion;
    };
    std::array<ProjectedPoint, s3g::kAmbiVoxMaxVoices> projected {};
    std::array<NSPoint, s3g::kAmbiVoxMaxVoices> voicePoints {};
    for (uint32_t voice = 0; voice < voices; ++voice) {
        projected[voice].index = voice;
        projected[voice].motion = [self snapshotPoint:voice];
        const s3g::Vec3 direction = s3g::directionFromAed(
            projected[voice].motion.azimuthDeg, projected[voice].motion.elevationDeg);
        projected[voice].world = {
            direction.x * projected[voice].motion.distance,
            direction.y * projected[voice].motion.distance,
            direction.z * projected[voice].motion.distance,
        };
        projected[voice].point = [self projectWorldPoint:projected[voice].world rect:rect depth:&projected[voice].depth];
        voicePoints[voice] = projected[voice].point;
    }
    const auto orchestration = _plugin->vox.orchestration;
    if (orchestration != s3g::AmbiVoxOrchestration::Individual
        && orchestration != s3g::AmbiVoxOrchestration::Cluster) {
        std::array<int32_t, 4> previous {{ -1, -1, -1, -1 }};
        for (uint32_t voice = 0u; voice < voices; ++voice) {
            const uint32_t cohort = voxEnsembleCohort(orchestration, voice, voices) % 4u;
            if (previous[cohort] >= 0) {
                NSBezierPath* cohortPath = [NSBezierPath bezierPath];
                [cohortPath moveToPoint:voicePoints[static_cast<uint32_t>(previous[cohort])]];
                [cohortPath lineToPoint:voicePoints[voice]];
                [[voxEnsembleRoleColor(orchestration, voice, voices) colorWithAlphaComponent:0.18] setStroke];
                [cohortPath setLineWidth:0.65];
                [cohortPath stroke];
            }
            previous[cohort] = static_cast<int32_t>(voice);
        }
    }
    std::sort(projected.begin(), projected.begin() + voices,
        [](const ProjectedPoint& a, const ProjectedPoint& b) { return a.depth < b.depth; });
    NSDictionary* idAttrs = s3g::clap_gui::textAttrs(votColor(0x0b0b0b), voices > 32u ? 5.5 : 7.0);
    for (uint32_t drawIndex = 0; drawIndex < voices; ++drawIndex) {
        const auto& item = projected[drawIndex];
        const bool selected = item.index == _selectedVoice;
        const CGFloat size = selected ? 15.0 : (voices > 32u ? 9.0 : 12.0);
        const NSRect pointRect = NSMakeRect(item.point.x - size * 0.5, item.point.y - size * 0.5, size, size);
        const float phrasePhase = std::clamp(
            _plugin->guiVoxPhrasePhase[item.index].load(std::memory_order_relaxed), 0.0f, 1.0f);
        const float phrasePulse = 0.5f - 0.5f * std::cos(2.0f * s3g::kPi * phrasePhase);
        const CGFloat roleSize = size + 4.0 + static_cast<CGFloat>(phrasePulse) * 5.0;
        const NSRect roleRect = NSMakeRect(item.point.x - roleSize * 0.5,
            item.point.y - roleSize * 0.5, roleSize, roleSize);
        NSColor* roleColor = voxEnsembleRoleColor(orchestration, item.index, voices);
        [[roleColor colorWithAlphaComponent:0.20 + phrasePulse * 0.48] setStroke];
        NSBezierPath* roleFrame = [NSBezierPath bezierPathWithRect:roleRect];
        [roleFrame setLineWidth:selected ? 1.35 : 0.85];
        [roleFrame stroke];
        [[votPointColor(item.motion.azimuthDeg, item.motion.elevationDeg, item.motion.distance, selected)
            colorWithAlphaComponent:0.96] setFill];
        NSRectFill(pointRect);
        [votColor(selected ? 0xe0e0e0 : 0x111111) setStroke];
        NSFrameRect(pointRect);
        NSString* label = [NSString stringWithFormat:@"%u", item.index + 1u];
        const NSSize labelSize = [label sizeWithAttributes:idAttrs];
        [label drawAtPoint:NSMakePoint(NSMidX(pointRect) - labelSize.width * 0.5,
                                       NSMidY(pointRect) - labelSize.height * 0.5 - 0.5)
            withAttributes:idAttrs];
        NSString* roleLabel = voxEnsembleRoleLabel(orchestration, item.index, voices);
        if ([roleLabel length] > 0u) {
            NSDictionary* roleAttrs = s3g::clap_gui::textAttrs(
                [roleColor colorWithAlphaComponent:selected ? 1.0 : 0.82], 7.0);
            [roleLabel drawAtPoint:NSMakePoint(NSMaxX(roleRect) + 2.0, NSMinY(roleRect) - 1.0)
                     withAttributes:roleAttrs];
        }
    }
    [NSGraphicsContext restoreGraphicsState];

    const auto selected = [self snapshotPoint:_selectedVoice];
    NSString* roleLabel = voxEnsembleRoleLabel(orchestration, _selectedVoice, voices);
    const float phrasePhase = std::clamp(
        _plugin->guiVoxPhrasePhase[_selectedVoice].load(std::memory_order_relaxed), 0.0f, 1.0f);
    NSString* readout = [roleLabel length] > 0u
        ? [NSString stringWithFormat:@"V%u %@   AZ %+06.1f   EL %+05.1f   D %.2f   P %.0f%%",
            _selectedVoice + 1u, roleLabel, selected.azimuthDeg, selected.elevationDeg,
            selected.distance, phrasePhase * 100.0f]
        : [NSString stringWithFormat:@"V%u   AZ %+06.1f   EL %+05.1f   D %.2f   P %.0f%%",
            _selectedVoice + 1u, selected.azimuthDeg, selected.elevationDeg,
            selected.distance, phrasePhase * 100.0f];
    [readout drawAtPoint:NSMakePoint(rect.origin.x + 10, rect.origin.y + 9) withAttributes:attrs];
}

- (void)drawMiniWave:(const std::array<float, s3g::kAmbiVotTableSize>&)table rect:(NSRect)rect color:(NSColor*)color
{
    NSBezierPath* path = [NSBezierPath bezierPath];
    for (uint32_t i = 0; i < s3g::kAmbiVotTableSize; ++i) {
        const CGFloat x = rect.origin.x + static_cast<CGFloat>(i) / static_cast<CGFloat>(s3g::kAmbiVotTableSize - 1u) * rect.size.width;
        const CGFloat y = NSMidY(rect) - static_cast<CGFloat>(table[i]) * rect.size.height * 0.40;
        if (i == 0u) [path moveToPoint:NSMakePoint(x, y)];
        else [path lineToPoint:NSMakePoint(x, y)];
    }
    [color setStroke];
    [path setLineWidth:0.75];
    [path stroke];
}

- (void)drawVectorPage:(NSRect)rect attrs:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    auto bank = activeBank(*_plugin);
    if (!bank) return;
    const NSRect bankRect = NSMakeRect(rect.origin.x + 8, rect.origin.y + 8, 252, 252);
    const NSRect pathRect = NSMakeRect(rect.origin.x + 278, rect.origin.y + 8, 270, 252);
    [votColor(0x0b0b0b) setFill];
    NSRectFill(bankRect);
    NSRectFill(pathRect);
    [votColor(0x575757) setStroke];
    NSFrameRect(bankRect);
    NSFrameRect(pathRect);
    const uint32_t voices = std::clamp<uint32_t>(_plugin->params.voices, 1u, s3g::kAmbiVoxMaxVoices);
    _selectedVoice = std::min<uint32_t>(_selectedVoice, voices - 1u);
    const auto selected = [self snapshotPoint:_selectedVoice];
    const auto vectorWeights = s3g::ambiVotVectorWeights(
        selected.u, selected.v, _plugin->params.morph);
    const float maxWeight = *std::max_element(vectorWeights.values.begin(), vectorWeights.values.end());
    const CGFloat cellW = bankRect.size.width / 4.0;
    const CGFloat cellH = bankRect.size.height / 4.0;
    for (uint32_t displayRow = 0; displayRow < 4u; ++displayRow) {
        const uint32_t row = 3u - displayRow;
        for (uint32_t column = 0; column < 4u; ++column) {
            const uint32_t table = row * 4u + column;
            const NSRect cell = NSMakeRect(bankRect.origin.x + column * cellW,
                                           bankRect.origin.y + displayRow * cellH, cellW, cellH);
            const CGFloat influence = std::sqrt(vectorWeights.values[table]
                / std::max(0.000001f, maxWeight));
            [[NSColor colorWithWhite:0.055 + influence * 0.15 alpha:1.0] setFill];
            NSRectFill(NSInsetRect(cell, 1, 1));
            [votColor(0x303030) setStroke];
            NSFrameRect(cell);
            [self drawMiniWave:bank->tables[table]
                          rect:NSInsetRect(cell, 5, 8)
                         color:[NSColor colorWithWhite:0.38 + influence * 0.35 alpha:1.0]];
        }
    }
    [@"TABLE BANK" drawAtPoint:NSMakePoint(bankRect.origin.x + 8, bankRect.origin.y + 7) withAttributes:attrs];
    [@"VECTOR PATH" drawAtPoint:NSMakePoint(pathRect.origin.x + 8, pathRect.origin.y + 7) withAttributes:attrs];

    const NSRect pathInner = NSInsetRect(pathRect, 14, 28);
    for (int i = 1; i < 4; ++i) {
        [votColor(0x303030) setStroke];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(pathInner.origin.x + pathInner.size.width * i / 4.0, pathInner.origin.y)
                                  toPoint:NSMakePoint(pathInner.origin.x + pathInner.size.width * i / 4.0, NSMaxY(pathInner))];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(pathInner.origin.x, pathInner.origin.y + pathInner.size.height * i / 4.0)
                                  toPoint:NSMakePoint(NSMaxX(pathInner), pathInner.origin.y + pathInner.size.height * i / 4.0)];
    }
    const CGFloat hillRadiusX = vectorWeights.sigmaCells * pathInner.size.width / 4.0;
    const CGFloat hillRadiusY = vectorWeights.sigmaCells * pathInner.size.height / 4.0;
    for (uint32_t row = 0; row < 4u; ++row) {
        for (uint32_t column = 0; column < 4u; ++column) {
            const uint32_t table = row * 4u + column;
            const CGFloat centerU = (static_cast<CGFloat>(column) + 0.5) / 4.0;
            const CGFloat centerV = (static_cast<CGFloat>(row) + 0.5) / 4.0;
            const NSPoint center = NSMakePoint(pathInner.origin.x + centerU * pathInner.size.width,
                                               pathInner.origin.y + (1.0 - centerV) * pathInner.size.height);
            [votColor(0x3b3b3b) setStroke];
            NSBezierPath* hill = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
                center.x - hillRadiusX, center.y - hillRadiusY, hillRadiusX * 2.0, hillRadiusY * 2.0)];
            [hill setLineWidth:0.65];
            [hill stroke];
            const CGFloat influence = std::sqrt(vectorWeights.values[table]
                / std::max(0.000001f, maxWeight));
            [[NSColor colorWithWhite:0.26 + influence * 0.50 alpha:1.0] setFill];
            NSRectFill(NSMakeRect(center.x - 2.0, center.y - 2.0, 4.0, 4.0));
        }
    }
    const bool vectorMoving = _plugin->params.motionScene != s3g::AmbiVotMotionScene::Manual;
    const float vectorCoverage = vectorMoving
        ? _plugin->params.scan
        : 0.0f;
    if (vectorCoverage > 0.001f) {
        const float linkedU = 0.5f + 0.5f * std::sin(selected.azimuthDeg * s3g::kPi / 180.0f);
        const float linkedV = std::clamp((selected.elevationDeg + 90.0f) / 180.0f, 0.0f, 1.0f);
        const float centerU = s3g::lerp(_plugin->params.vectorX, linkedU, _plugin->params.motionLink);
        const float centerV = s3g::lerp(_plugin->params.vectorY, linkedV, _plugin->params.motionLink);
        const float minimumU = centerU * (1.0f - vectorCoverage);
        const float maximumV = centerV * (1.0f - vectorCoverage) + vectorCoverage;
        const NSRect coverageRect = NSMakeRect(
            pathInner.origin.x + minimumU * pathInner.size.width,
            pathInner.origin.y + (1.0f - maximumV) * pathInner.size.height,
            vectorCoverage * pathInner.size.width,
            vectorCoverage * pathInner.size.height);
        [votColor(0x666666) setStroke];
        NSFrameRect(coverageRect);
    }
    if (_trailCount > 1u) {
        NSBezierPath* path = [NSBezierPath bezierPath];
        for (uint32_t step = 0; step < _trailCount; ++step) {
            const uint32_t index = (_trailHead + 48u - _trailCount + step) % 48u;
            const auto& point = _trails[_selectedVoice][index];
            const NSPoint p = NSMakePoint(pathInner.origin.x + point.u * pathInner.size.width,
                                          pathInner.origin.y + (1.0f - point.v) * pathInner.size.height);
            if (step == 0u) [path moveToPoint:p];
            else [path lineToPoint:p];
        }
        const auto point = [self snapshotPoint:_selectedVoice];
        [[votPointColor(point.azimuthDeg, point.elevationDeg, point.distance, true)
            colorWithAlphaComponent:0.64] setStroke];
        [path setLineWidth:1.4];
        [path stroke];
    }
    NSDictionary* idAttrs = s3g::clap_gui::textAttrs(votColor(0x0b0b0b), voices > 32u ? 5.5 : 7.0);
    for (uint32_t voice = 0; voice < voices; ++voice) {
        const auto point = [self snapshotPoint:voice];
        const bool selected = voice == _selectedVoice;
        const CGFloat size = selected ? 14.0 : (voices > 32u ? 8.0 : 11.0);
        const NSPoint p = NSMakePoint(pathInner.origin.x + point.u * pathInner.size.width,
                                      pathInner.origin.y + (1.0f - point.v) * pathInner.size.height);
        const NSRect marker = NSMakeRect(p.x - size * 0.5, p.y - size * 0.5, size, size);
        [votPointColor(point.azimuthDeg, point.elevationDeg, point.distance, selected) setFill];
        NSRectFill(marker);
        [votColor(selected ? 0xe0e0e0 : 0x111111) setStroke];
        NSFrameRect(marker);
        NSString* label = [NSString stringWithFormat:@"%u", voice + 1u];
        const NSSize sizeText = [label sizeWithAttributes:idAttrs];
        [label drawAtPoint:NSMakePoint(NSMidX(marker) - sizeText.width * 0.5, NSMidY(marker) - sizeText.height * 0.5 - 0.5)
            withAttributes:idAttrs];
    }

    const NSRect waveRect = NSMakeRect(rect.origin.x + 8, rect.origin.y + 278, 540, 300);
    [votColor(0x0b0b0b) setFill];
    NSRectFill(waveRect);
    [votColor(0x575757) setStroke];
    NSFrameRect(waveRect);
    [@"INTERPOLATED WAVEFORM" drawAtPoint:NSMakePoint(waveRect.origin.x + 8, waveRect.origin.y + 7) withAttributes:attrs];
    [votColor(0x2d2d2d) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(waveRect.origin.x + 8, NSMidY(waveRect))
                              toPoint:NSMakePoint(NSMaxX(waveRect) - 8, NSMidY(waveRect))];
    NSBezierPath* waveform = [NSBezierPath bezierPath];
    constexpr uint32_t samples = 512;
    for (uint32_t i = 0; i < samples; ++i) {
        const float phase = static_cast<float>(i) / static_cast<float>(samples - 1u);
        const float value = s3g::ambiVotVectorSample(*bank, vectorWeights, phase);
        const CGFloat x = waveRect.origin.x + 10.0 + phase * (waveRect.size.width - 20.0);
        const CGFloat y = NSMidY(waveRect) - static_cast<CGFloat>(value) * (waveRect.size.height * 0.39);
        if (i == 0u) [waveform moveToPoint:NSMakePoint(x, y)];
        else [waveform lineToPoint:NSMakePoint(x, y)];
    }
    [votPointColor(selected.azimuthDeg, selected.elevationDeg, selected.distance, true) setStroke];
    [waveform setLineWidth:1.25];
    [waveform stroke];
    NSString* waveformReadout = [NSString stringWithFormat:@"V%u   U %.3f   V %.3f   LINK %.0f%%",
        _selectedVoice + 1u, selected.u, selected.v, _plugin->params.motionLink * 100.0f];
    const NSSize waveformReadoutSize = [waveformReadout sizeWithAttributes:valueAttrs];
    [waveformReadout drawAtPoint:NSMakePoint(NSMaxX(waveRect) - waveformReadoutSize.width - 8, waveRect.origin.y + 7)
        withAttributes:valueAttrs];

    s3g::clap_gui::drawSlider(@"X", [NSString stringWithFormat:@"%.2f", _plugin->params.vectorX],
        _plugin->params.vectorX, rect.origin.y + 606, attrs, valueAttrs, style, rect.origin.x + 8, rect.origin.x + 86, rect.origin.x + 270, 170);
    s3g::clap_gui::drawSlider(@"Y", [NSString stringWithFormat:@"%.2f", _plugin->params.vectorY],
        _plugin->params.vectorY, rect.origin.y + 632, attrs, valueAttrs, style, rect.origin.x + 8, rect.origin.x + 86, rect.origin.x + 270, 170);
    s3g::clap_gui::drawSlider(@"SCAN", [NSString stringWithFormat:@"%.0f%%", _plugin->params.scan * 100.0f],
        _plugin->params.scan, rect.origin.y + 658, attrs, valueAttrs, style, rect.origin.x + 8, rect.origin.x + 86, rect.origin.x + 270, 170);
    s3g::clap_gui::drawSlider(@"MORPH", [NSString stringWithFormat:@"%.0f%%", _plugin->params.morph * 100.0f],
        _plugin->params.morph, rect.origin.y + 684, attrs, valueAttrs, style, rect.origin.x + 8, rect.origin.x + 86, rect.origin.x + 270, 170);
}

- (void)drawScorePage:(NSRect)rect attrs:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    const auto score = loadScore(*_plugin);
    const uint32_t count = score.nodeCount;
    _selectedScoreNode = std::min<uint32_t>(_selectedScoreNode, count - 1u);
    const NSRect timelineRect = NSMakeRect(rect.origin.x + 8, rect.origin.y + 8, 540, 306);
    const NSRect routeRect = NSMakeRect(rect.origin.x + 8, rect.origin.y + 328, 540, 292);
    [votColor(0x0b0b0b) setFill];
    NSRectFill(timelineRect);
    NSRectFill(routeRect);
    [votColor(0x575757) setStroke];
    NSFrameRect(timelineRect);
    NSFrameRect(routeRect);
    [@"VECTOR SCORE" drawAtPoint:NSMakePoint(timelineRect.origin.x + 8, timelineRect.origin.y + 7) withAttributes:attrs];
    NSString* scoreStatus = [NSString stringWithFormat:@"%u NODES   %@   %.2f S",
        count, [NSString stringWithUTF8String:s3g::ambiVotScoreModeName(_plugin->params.scoreMode)],
        _plugin->params.scoreDurationSec];
    const NSSize statusSize = [scoreStatus sizeWithAttributes:valueAttrs];
    [scoreStatus drawAtPoint:NSMakePoint(NSMaxX(timelineRect) - statusSize.width - 8, timelineRect.origin.y + 7)
              withAttributes:valueAttrs];

    const NSRect uLane = NSMakeRect(timelineRect.origin.x + 14, timelineRect.origin.y + 34,
                                    timelineRect.size.width - 28, 112);
    const NSRect vLane = NSMakeRect(timelineRect.origin.x + 14, timelineRect.origin.y + 174,
                                    timelineRect.size.width - 28, 112);
    const float sustainStart = score.nodes[score.sustainStart].time;
    const float sustainEnd = score.nodes[score.sustainEnd].time;
    for (const auto lane : { uLane, vLane }) {
        [[NSColor colorWithWhite:0.12 alpha:1.0] setFill];
        NSRectFill(NSMakeRect(lane.origin.x + sustainStart * lane.size.width, lane.origin.y,
                              (sustainEnd - sustainStart) * lane.size.width, lane.size.height));
        [votColor(0x343434) setStroke];
        NSFrameRect(lane);
        for (int division = 1; division < 4; ++division) {
            [NSBezierPath strokeLineFromPoint:NSMakePoint(lane.origin.x + lane.size.width * division / 4.0, lane.origin.y)
                                      toPoint:NSMakePoint(lane.origin.x + lane.size.width * division / 4.0, NSMaxY(lane))];
        }
        [NSBezierPath strokeLineFromPoint:NSMakePoint(lane.origin.x, NSMidY(lane))
                                  toPoint:NSMakePoint(NSMaxX(lane), NSMidY(lane))];
    }
    [@"U" drawAtPoint:NSMakePoint(uLane.origin.x + 6, uLane.origin.y + 5) withAttributes:attrs];
    [@"V" drawAtPoint:NSMakePoint(vLane.origin.x + 6, vLane.origin.y + 5) withAttributes:attrs];

    auto drawLane = [&](NSRect lane, bool drawU, NSColor* color) {
        NSBezierPath* path = [NSBezierPath bezierPath];
        bool first = true;
        for (uint32_t segment = 0; segment + 1u < count; ++segment) {
            const auto& from = score.nodes[segment];
            const auto& to = score.nodes[segment + 1u];
            for (uint32_t step = 0; step <= 20u; ++step) {
                const float local = static_cast<float>(step) / 20.0f;
                const float interpolation = s3g::ambiVotScoreCurveValue(from.curve, local);
                const float time = s3g::lerp(from.time, to.time, local);
                const float value = s3g::lerp(drawU ? from.u : from.v, drawU ? to.u : to.v, interpolation);
                const NSPoint point = NSMakePoint(lane.origin.x + time * lane.size.width,
                                                   lane.origin.y + (1.0f - value) * lane.size.height);
                if (first) { [path moveToPoint:point]; first = false; }
                else [path lineToPoint:point];
            }
        }
        [color setStroke];
        [path setLineWidth:1.2];
        [path stroke];
        for (uint32_t node = 0; node < count; ++node) {
            const float value = drawU ? score.nodes[node].u : score.nodes[node].v;
            const NSPoint point = NSMakePoint(lane.origin.x + score.nodes[node].time * lane.size.width,
                                               lane.origin.y + (1.0f - value) * lane.size.height);
            const CGFloat size = node == _selectedScoreNode ? 9.0 : 7.0;
            [[NSColor colorWithWhite:node == _selectedScoreNode ? 0.88 : 0.58 alpha:1.0] setFill];
            NSRectFill(NSMakeRect(point.x - size * 0.5, point.y - size * 0.5, size, size));
        }
    };
    drawLane(uLane, true, votColor(0xd0d0d0));
    drawLane(vLane, false, votColor(0x858585));

    [@"FIELD ROUTE" drawAtPoint:NSMakePoint(routeRect.origin.x + 8, routeRect.origin.y + 7) withAttributes:attrs];
    const NSRect routeInner = NSMakeRect(routeRect.origin.x + 14, routeRect.origin.y + 30,
                                         routeRect.size.width - 28, routeRect.size.height - 44);
    [votColor(0x303030) setStroke];
    for (int division = 1; division < 4; ++division) {
        [NSBezierPath strokeLineFromPoint:NSMakePoint(routeInner.origin.x + routeInner.size.width * division / 4.0, routeInner.origin.y)
                                  toPoint:NSMakePoint(routeInner.origin.x + routeInner.size.width * division / 4.0, NSMaxY(routeInner))];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(routeInner.origin.x, routeInner.origin.y + routeInner.size.height * division / 4.0)
                                  toPoint:NSMakePoint(NSMaxX(routeInner), routeInner.origin.y + routeInner.size.height * division / 4.0)];
    }
    NSBezierPath* route = [NSBezierPath bezierPath];
    for (uint32_t node = 0; node < count; ++node) {
        const NSPoint point = NSMakePoint(routeInner.origin.x + score.nodes[node].u * routeInner.size.width,
                                          routeInner.origin.y + (1.0f - score.nodes[node].v) * routeInner.size.height);
        if (node == 0u) [route moveToPoint:point];
        else [route lineToPoint:point];
    }
    [votColor(0x949494) setStroke];
    [route setLineWidth:1.15];
    [route stroke];
    NSDictionary* nodeAttrs = s3g::clap_gui::textAttrs(votColor(0x0b0b0b), 7.0);
    for (uint32_t node = 0; node < count; ++node) {
        const NSPoint point = NSMakePoint(routeInner.origin.x + score.nodes[node].u * routeInner.size.width,
                                          routeInner.origin.y + (1.0f - score.nodes[node].v) * routeInner.size.height);
        const CGFloat size = node == _selectedScoreNode ? 14.0 : 11.0;
        [[NSColor colorWithWhite:node == _selectedScoreNode ? 0.90 : 0.62 alpha:1.0] setFill];
        const NSRect marker = NSMakeRect(point.x - size * 0.5, point.y - size * 0.5, size, size);
        NSRectFill(marker);
        [votColor(0x151515) setStroke];
        NSFrameRect(marker);
        NSString* label = [NSString stringWithFormat:@"%u", node + 1u];
        const NSSize labelSize = [label sizeWithAttributes:nodeAttrs];
        [label drawAtPoint:NSMakePoint(NSMidX(marker) - labelSize.width * 0.5,
                                       NSMidY(marker) - labelSize.height * 0.5 - 0.5)
            withAttributes:nodeAttrs];
    }

    const auto& selected = score.nodes[_selectedScoreNode];
    s3g::clap_gui::drawSlider(@"TIME", [NSString stringWithFormat:@"%.3f", selected.time],
        selected.time, rect.origin.y + 648, attrs, valueAttrs, style,
        rect.origin.x + 8, rect.origin.x + 86, rect.origin.x + 270, 170);
    s3g::clap_gui::drawSlider(@"U", [NSString stringWithFormat:@"%.3f", selected.u],
        selected.u, rect.origin.y + 674, attrs, valueAttrs, style,
        rect.origin.x + 8, rect.origin.x + 86, rect.origin.x + 270, 170);
    s3g::clap_gui::drawSlider(@"V", [NSString stringWithFormat:@"%.3f", selected.v],
        selected.v, rect.origin.y + 700, attrs, valueAttrs, style,
        rect.origin.x + 8, rect.origin.x + 86, rect.origin.x + 270, 170);
    s3g::clap_gui::drawMenu(@"CURVE", [NSString stringWithUTF8String:s3g::ambiVotScoreCurveName(selected.curve)],
        rect.origin.y + 726, attrs, valueAttrs, style, rect.origin.x + 8, rect.origin.x + 86, 184);
}

- (NSRect)lyricCueRect:(uint32_t)index
{
    const NSRect content = [self leftContentRect];
    const uint32_t column = index % 4u;
    const uint32_t row = index / 4u;
    return NSMakeRect(content.origin.x + 8.0 + static_cast<CGFloat>(column) * 135.0,
        content.origin.y + 446.0 + static_cast<CGFloat>(row) * 20.0, 131.0, 17.0);
}

- (void)drawLyricsPage:(NSRect)rect attrs:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    [votColor(0x0b0b0b) setFill];
    NSRectFill(rect);
    [votColor(0x575757) setStroke];
    NSFrameRect(rect);
    [@"LYRIC CUES" drawAtPoint:NSMakePoint(rect.origin.x + 8, rect.origin.y + 7)
        withAttributes:attrs];

    const auto cues = splitVoxLyricCues(loadVoxLyrics(*_plugin));
    const uint32_t cueCount = std::min<uint32_t>(
        static_cast<uint32_t>(cues.size()), kVoxLyricsMaxCues);
    _selectedLyricCue = cueCount > 0u
        ? std::min<uint32_t>(_selectedLyricCue, cueCount - 1u) : 0u;
    const uint32_t activeCue = cueCount > 0u
        ? _plugin->guiLyricCue.load(std::memory_order_relaxed) % cueCount : 0u;
    [@"CUE MAP" drawAtPoint:NSMakePoint(rect.origin.x + 8, rect.origin.y + 422)
        withAttributes:attrs];
    NSDictionary* cueAttrs = s3g::clap_gui::textAttrs(votColor(0xa8a8a8), 8.5);
    for (uint32_t cue = 0u; cue < cueCount; ++cue) {
        const NSRect cell = [self lyricCueRect:cue];
        [votColor(cue == activeCue ? 0x484848 : 0x202020) setFill];
        NSRectFill(cell);
        [votColor(cue == _selectedLyricCue ? 0xc4c4c4 : 0x505050) setStroke];
        NSFrameRect(cell);
        NSString* text = [NSString stringWithFormat:@"%02u  %@", cue + 1u,
            [NSString stringWithUTF8String:cues[cue].c_str()]];
        [text drawInRect:NSInsetRect(cell, 4, 2) withAttributes:cueAttrs];
    }

    const CGFloat controlsY = rect.origin.y + 618.0;
    s3g::clap_gui::drawMenu(@"MODE",
        [NSString stringWithUTF8String:voxLyricModeName(_plugin->lyric.mode)],
        controlsY, attrs, valueAttrs, style,
        rect.origin.x + 8, rect.origin.x + 110, 150);
    char display[64] {};
    paramsValueToText(nullptr, kLyricCueBeatsParamId, _plugin->lyric.cueBeats,
        display, sizeof(display));
    const double beatNorm = (_plugin->lyric.cueBeats - 0.25) / 63.75;
    s3g::clap_gui::drawSlider(@"CUE BEATS", [NSString stringWithUTF8String:display],
        beatNorm, controlsY + 28, attrs, valueAttrs, style,
        rect.origin.x + 8, rect.origin.x + 110, rect.origin.x + 302, 170);
    paramsValueToText(nullptr, kLyricCueNoteParamId, _plugin->lyric.cueBaseNote,
        display, sizeof(display));
    s3g::clap_gui::drawSlider(@"BASE NOTE", [NSString stringWithUTF8String:display],
        static_cast<double>(_plugin->lyric.cueBaseNote) / 127.0,
        controlsY + 56, attrs, valueAttrs, style,
        rect.origin.x + 8, rect.origin.x + 110, rect.origin.x + 302, 170);
    paramsValueToText(nullptr, kLyricCueChannelParamId, _plugin->lyric.cueChannel,
        display, sizeof(display));
    s3g::clap_gui::drawSlider(@"MIDI CH", [NSString stringWithUTF8String:display],
        static_cast<double>(_plugin->lyric.cueChannel - 1u) / 15.0,
        controlsY + 84, attrs, valueAttrs, style,
        rect.origin.x + 8, rect.origin.x + 110, rect.origin.x + 302, 170);
}

- (void)drawMenu:(NSString*)name value:(NSString*)value y:(CGFloat)y attrs:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    [self drawMenuAtX:630 name:name value:value y:y attrs:attrs valueAttrs:valueAttrs style:style];
}

- (void)drawMenuAtX:(CGFloat)panelX name:(NSString*)name value:(NSString*)value y:(CGFloat)y attrs:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawMenu(name, value, y, attrs, valueAttrs, style, panelX + 16, panelX + 108, 124);
}

- (void)drawSlider:(NSString*)name param:(clap_id)param value:(double)value min:(double)min max:(double)max y:(CGFloat)y attrs:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    [self drawSliderAtX:630 name:name param:param value:value min:min max:max y:y attrs:attrs valueAttrs:valueAttrs style:style];
}

- (void)drawSliderAtX:(CGFloat)panelX name:(NSString*)name param:(clap_id)param value:(double)value min:(double)min max:(double)max y:(CGFloat)y attrs:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    double norm = (value - min) / std::max(0.000001, max - min);
    if (param == kMotionRateParamId || param == kScoreDurationParamId || param == kVibratoRateParamId) norm = std::log(value / min) / std::log(max / min);
    else if (param == kAttackParamId || param == kDecayParamId || param == kReleaseParamId) norm = std::log(value / min) / std::log(max / min);
    else if (param == kPvocStretchParamId) norm = std::log(value / min) / std::log(max / min);
    char text[64] {};
    paramsValueToText(nullptr, param, value, text, sizeof(text));
    s3g::clap_gui::drawSlider(name, [NSString stringWithUTF8String:text], norm, y, attrs, valueAttrs, style,
        panelX + 16, panelX + 108, panelX + 196, 82);
}

- (void)drawPanels:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    const auto p = _plugin->params;
    const auto v = _plugin->vox;
    s3g::clap_gui::drawPanelFrame(630, 42, 250, 150, style);
    s3g::clap_gui::drawPanelHeader(@"ENCODER", true, 630, 42, 250, 21, attrs, style);
    [self drawMenu:@"TRIGGER" value:[NSString stringWithUTF8String:s3g::ambiVotModeName(p.mode)] y:78 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"ORDER" value:[NSString stringWithFormat:@"%uOA", p.order] y:104 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"VOICES" param:kVoicesParamId value:p.voices min:1 max:s3g::kAmbiVoxMaxVoices y:130 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"VOICE" value:[NSString stringWithUTF8String:voxSpeechModeName(v.speechMode)] y:156 attrs:attrs valueAttrs:valueAttrs style:style];

    const bool textureMode = v.speechMode == VoxSpeechMode::Texture;
    s3g::clap_gui::drawPanelFrame(630, 204, 250, 294, style);
    s3g::clap_gui::drawPanelHeader(textureMode ? @"TEXTURE PLAYBACK" : @"PHRASE PLAYBACK",
        true, 630, 204, 250, 21, attrs, style);
    if (textureMode) {
        [self drawSlider:@"SPEED" param:kWorldRateParamId value:v.worldRate min:0 max:1 y:240 attrs:attrs valueAttrs:valueAttrs style:style];
        [self drawSlider:@"LOOP START" param:kWorldLoopStartParamId value:v.worldLoopStart min:0 max:1 y:266 attrs:attrs valueAttrs:valueAttrs style:style];
        [self drawSlider:@"LOOP END" param:kWorldLoopEndParamId value:v.worldLoopEnd min:0 max:1 y:292 attrs:attrs valueAttrs:valueAttrs style:style];
        [self drawSlider:@"FREEZE" param:kWorldFreezeParamId value:v.worldFreeze min:0 max:1 y:318 attrs:attrs valueAttrs:valueAttrs style:style];
        [self drawSlider:@"POSITION" param:kWorldScrubParamId value:v.worldScrub min:0 max:1 y:344 attrs:attrs valueAttrs:valueAttrs style:style];
        [self drawSlider:@"PHR SPREAD" param:kPhraseSpreadParamId value:v.phraseSpread min:0 max:1 y:370 attrs:attrs valueAttrs:valueAttrs style:style];
        [self drawSlider:@"STRETCH" param:kPvocStretchParamId value:v.pvocStretch min:0.25 max:4 y:396 attrs:attrs valueAttrs:valueAttrs style:style];
        [self drawSlider:@"TRANSIENT" param:kPvocTransientParamId value:v.pvocTransient min:0 max:1 y:422 attrs:attrs valueAttrs:valueAttrs style:style];
        [self drawSlider:@"VOICE STEP" param:kWorldVoiceSpreadParamId value:v.worldVoiceSpread min:0 max:1 y:448 attrs:attrs valueAttrs:valueAttrs style:style];
        [self drawSlider:@"VOICE DEV" param:kWorldVoiceDeviationParamId value:v.worldVoiceDeviation min:0 max:1 y:474 attrs:attrs valueAttrs:valueAttrs style:style];
    } else {
        [self drawSlider:@"PACE" param:kPhraseRateParamId value:v.phraseRate min:0 max:1 y:240 attrs:attrs valueAttrs:valueAttrs style:style];
        [self drawSlider:@"STRETCH" param:kPvocStretchParamId value:v.pvocStretch min:0.25 max:4 y:266 attrs:attrs valueAttrs:valueAttrs style:style];
        [self drawSlider:@"TRANSIENT" param:kPvocTransientParamId value:v.pvocTransient min:0 max:1 y:292 attrs:attrs valueAttrs:valueAttrs style:style];
        [self drawSlider:@"BLEND" param:kTransitionParamId value:v.transition min:0 max:1 y:318 attrs:attrs valueAttrs:valueAttrs style:style];
        [self drawSlider:@"PHR SPREAD" param:kPhraseSpreadParamId value:v.phraseSpread min:0 max:1 y:344 attrs:attrs valueAttrs:valueAttrs style:style];
        [self drawSlider:@"VOICE STEP" param:kWorldVoiceSpreadParamId value:v.worldVoiceSpread min:0 max:1 y:370 attrs:attrs valueAttrs:valueAttrs style:style];
        [self drawSlider:@"VOICE DEV" param:kWorldVoiceDeviationParamId value:v.worldVoiceDeviation min:0 max:1 y:396 attrs:attrs valueAttrs:valueAttrs style:style];
    }

    s3g::clap_gui::drawPanelFrame(630, 510, 250, 64, style);
    s3g::clap_gui::drawPanelHeader(@"OUTPUT", true, 630, 510, 250, 21, attrs, style);
    [self drawSlider:@"LEVEL" param:kOutputParamId value:p.outputGainDb min:-60 max:12 y:546 attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(630, 586, 250, 176, style);
    s3g::clap_gui::drawPanelHeader(@"WORLD / ENSEMBLE", true, 630, 586, 250, 21, attrs, style);
    [self drawMenu:@"ENSEMBLE" value:[NSString stringWithUTF8String:s3g::ambiVoxOrchestrationName(v.orchestration)] y:622 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"CONTOUR" value:[NSString stringWithUTF8String:s3g::ambiVoxContourModeName(v.contourMode)] y:648 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"FORMANT" param:kFormantMacroParamId value:v.formantMacro min:-1 max:1 y:674 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"PERIODIC" param:kWorldVoicingParamId value:v.worldVoicing min:0 max:1 y:700 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"AIR COLOR" param:kWorldAirColorParamId value:v.worldAirColor min:-1 max:1 y:726 attrs:attrs valueAttrs:valueAttrs style:style];

    constexpr CGFloat motionX = 896;
    s3g::clap_gui::drawPanelFrame(motionX, 42, 246, 426, style);
    s3g::clap_gui::drawPanelHeader(@"MOTION", true, motionX, 42, 246, 21, attrs, style);
    [self drawMenuAtX:motionX name:@"SCENE" value:[NSString stringWithUTF8String:s3g::ambiVotMotionSceneName(p.motionScene)] y:78 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenuAtX:motionX name:@"CLOCK" value:[NSString stringWithUTF8String:s3g::ambiVotMotionClockName(p.motionClock)] y:104 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSliderAtX:motionX name:@"SPEED" param:kMotionRateParamId value:p.motionRateHz min:0.001 max:2 y:130 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSliderAtX:motionX name:@"DIV" param:kSyncDivisionParamId value:p.syncDivisionBeats min:0.25 max:64 y:156 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSliderAtX:motionX name:@"AMOUNT" param:kMotionAmountParamId value:p.motionAmount min:0 max:1 y:182 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSliderAtX:motionX name:@"SPREAD" param:kSpreadParamId value:p.motionSpread min:0 max:1 y:208 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSliderAtX:motionX name:@"COHERE" param:kCoherenceParamId value:p.motionCoherence min:0 max:1 y:234 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSliderAtX:motionX name:@"CHAOS" param:kChaosParamId value:p.motionChaos min:0 max:1 y:260 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSliderAtX:motionX name:@"LINK" param:kLinkParamId value:p.motionLink min:0 max:1 y:286 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSliderAtX:motionX name:@"SMOOTH" param:kSmoothParamId value:p.motionSmooth min:0 max:1 y:312 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSliderAtX:motionX name:@"AZIMUTH" param:kCenterAzimuthParamId value:p.centerAzimuthDeg min:-180 max:180 y:338 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSliderAtX:motionX name:@"ELEV" param:kCenterElevationParamId value:p.centerElevationDeg min:-90 max:90 y:364 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSliderAtX:motionX name:@"DISTANCE" param:kCenterDistanceParamId value:p.centerDistance min:0.15 max:2 y:390 attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(motionX, 480, 246, 152, style);
    s3g::clap_gui::drawPanelHeader(@"PHRASE", true, motionX, 480, 246, 21, attrs, style);
    const NSRect phraseHeader = NSMakeRect(motionX, 480, 246, 21);
    s3g::clap_gui::drawHeaderActionButton([self worldResetButtonRect], phraseHeader, @"RESET", attrs, style);
    s3g::clap_gui::drawHeaderActionButton([self encodedLoadButtonRect], phraseHeader, @"LOAD", attrs, style);
    const bool hasBank = _plugin && static_cast<bool>(std::atomic_load_explicit(&_plugin->voicebank, std::memory_order_acquire));
    const bool hasWorld = _plugin && static_cast<bool>(std::atomic_load_explicit(&_plugin->worldSample, std::memory_order_acquire));
    if (_phraseField) {
        [_phraseField setHidden:NO];
        [_phraseField setFrame:NSMakeRect(motionX + 16, 516, 214, 22)];
    }
    const std::string loadedName = _plugin ? loadVoxLoadedName(*_plugin) : std::string();
    std::string resolvedText;
    NSString* sourceText = @"LOAD WAV OR VOICEBANK FOLDER";
    if (hasBank) {
        const auto bank = std::atomic_load_explicit(&_plugin->voicebank, std::memory_order_acquire);
        sourceText = [NSString stringWithFormat:@"WORLD BANK  %@  %lu entries",
            [NSString stringWithUTF8String:loadedName.empty() ? "loaded" : loadedName.c_str()],
            bank ? static_cast<unsigned long>(bank->entries.size()) : 0ul];
        if (bank) resolvedText = resolvedVoicebankPhrase(*_plugin, *bank);
    } else if (hasWorld) {
        const auto world = std::atomic_load_explicit(&_plugin->worldSample, std::memory_order_acquire);
        const double duration = world && world->sampleRate > 0
            ? static_cast<double>(world->samples.size()) / static_cast<double>(world->sampleRate)
            : 0.0;
        sourceText = [NSString stringWithFormat:@"WORLD WAV  %@  %.2fs",
            [NSString stringWithUTF8String:loadedName.empty() ? "loaded" : loadedName.c_str()], duration];
    }
    [sourceText drawAtPoint:NSMakePoint(motionX + 16, 548) withAttributes:valueAttrs];
    if (!resolvedText.empty()) {
        NSString* aliases = [NSString stringWithFormat:@"ALIASES  %@",
            [NSString stringWithUTF8String:resolvedText.c_str()]];
        [aliases drawAtPoint:NSMakePoint(motionX + 16, 562) withAttributes:valueAttrs];
    }
    const uint32_t phraseEventCount = _plugin
        ? std::min<uint32_t>(_plugin->voxBankCompiledCount.load(std::memory_order_acquire), kVoxCompiledMaxFrames)
        : 0u;
    if (hasBank && phraseEventCount > 0u) {
        const uint32_t phraseEvent = std::min<uint32_t>(
            _plugin->guiVoxPhraseEvent.load(std::memory_order_relaxed), phraseEventCount - 1u);
        const float phraseProgress = std::clamp(
            _plugin->guiVoxPhraseProgress.load(std::memory_order_relaxed), 0.0f, 1.0f);
        NSString* timelineText = [NSString stringWithFormat:@"VOICE 1  %u/%u",
            phraseEvent + 1u, phraseEventCount];
        [timelineText drawAtPoint:NSMakePoint(motionX + 16, 610) withAttributes:valueAttrs];
        const NSRect timeline = NSMakeRect(motionX + 16, 624, 214, 3);
        [style.strip setFill];
        NSRectFill(timeline);
        if (phraseEventCount <= 32u) {
            const CGFloat step = timeline.size.width / static_cast<CGFloat>(phraseEventCount);
            [style.grid setStroke];
            for (uint32_t event = 1u; event < phraseEventCount; ++event) {
                NSRect divider = NSMakeRect(timeline.origin.x + step * event, timeline.origin.y, 1, timeline.size.height);
                NSFrameRect(divider);
            }
            [style.fill setFill];
            NSRectFill(NSMakeRect(timeline.origin.x + step * phraseEvent,
                timeline.origin.y, std::max<CGFloat>(1.0, step * phraseProgress), timeline.size.height));
        } else {
            [style.fill setFill];
            const CGFloat absoluteProgress = (static_cast<CGFloat>(phraseEvent) + phraseProgress)
                / static_cast<CGFloat>(phraseEventCount);
            NSRectFill(NSMakeRect(timeline.origin.x, timeline.origin.y,
                timeline.size.width * absoluteProgress, timeline.size.height));
        }
        [style.grid setStroke];
        NSFrameRect(timeline);
    }

    s3g::clap_gui::drawPanelFrame(motionX, 644, 246, 246, style);
    s3g::clap_gui::drawPanelHeader(@"PITCH", true, motionX, 644, 246, 21, attrs, style);
    [self drawSliderAtX:motionX name:@"ROOT" param:kBaseNoteParamId value:p.baseNote min:12 max:96 y:680 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenuAtX:motionX name:@"SCALE" value:[NSString stringWithUTF8String:s3g::ambiVotScaleName(p.scale)] y:706 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSliderAtX:motionX name:@"SPREAD" param:kPitchSpreadParamId value:p.pitchSpread min:0 max:2 y:732 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSliderAtX:motionX name:@"TRANSPOSE" param:kWorldPitchParamId value:v.worldPitchCents min:-2400 max:2400 y:758 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSliderAtX:motionX name:@"DEVIATION" param:kDetuneParamId value:p.detune min:0 max:1 y:784 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSliderAtX:motionX name:@"GLIDE" param:kPortamentoParamId value:v.portamento min:0 max:1 y:810 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSliderAtX:motionX name:@"VIBRATO" param:kVibratoDepthParamId value:v.vibratoDepth min:0 max:1 y:836 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSliderAtX:motionX name:@"VIB SPEED" param:kVibratoRateParamId value:v.vibratoRateHz min:0.1 max:12 y:862 attrs:attrs valueAttrs:valueAttrs style:style];
}

- (void)drawOpenMenu:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    if (_openMenu <= 0 || _menuItemCount == 0u) return;
    static NSString* modeItems[] = { @"FREE", @"MIDI", @"BOTH" };
    static NSString* waveItems[] = { @"SINE", @"CLASSIC", @"DIGITAL", @"FORMANT", @"USER" };
    static NSString* orderItems[] = { @"1OA", @"2OA", @"3OA", @"4OA", @"5OA", @"6OA", @"7OA" };
    static NSString* sceneItems[] = { @"MANUAL", @"ORBIT", @"FLOW", @"PATH", @"PULSE" };
    static NSString* clockItems[] = { @"FREE", @"SYNC" };
    static NSString* scaleItems[] = { @"CHROM", @"MAJOR", @"MINOR", @"PENTA", @"WHOLE", @"HARM MIN" };
    static NSString* scoreModeItems[] = { @"OFF", @"ONE", @"LOOP", @"PING" };
    static NSString* curveItems[] = { @"LINEAR", @"SMOOTH", @"EXP", @"HOLD" };
    static NSString* speechItems[] = { @"TEXTURE", @"SPEAK", @"SING" };
    static NSString* ensembleItems[] = { @"INDIVIDUAL", @"UNISON", @"CHORALE", @"CHORUS", @"ROUND", @"CLUSTER" };
    static NSString* contourItems[] = { @"ORIGINAL", @"REDUCED", @"FLAT", @"QUANTIZED" };
    static NSString* lyricModeItems[] = { @"LOOP", @"MIDI STEP", @"MIDI CUE", @"TRANSPORT", @"AUTO" };
    static NSString* factoryItems[] = {
        @"VOX DEFAULT", @"SOLO SPEAK", @"CLOSE UNISON", @"SATB CHORALE",
        @"DOUBLE CHOIR", @"WIDE CHORUS", @"FOUR PART ROUND", @"SEMITONE CLUSTER",
        @"AIR CHOIR", @"DARK VOWELS", @"FROZEN TEXTURE", @"ORBITING CHORUS",
        @"FULL 3OA CHOIR"
    };
    NSString** items = modeItems;
    int selected = 0;
    if (_openMenu == 1) selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.mode));
    else if (_openMenu == 2) {
        items = waveItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.preset));
    } else if (_openMenu == 3) {
        items = orderItems;
        selected = static_cast<int>(_plugin->params.order - 1u);
    } else if (_openMenu == 4) {
        items = sceneItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.motionScene));
    } else if (_openMenu == 5) {
        items = clockItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.motionClock));
    } else if (_openMenu == 6) {
        items = scaleItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.scale));
    } else if (_openMenu == 7) {
        items = scoreModeItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.scoreMode));
    } else if (_openMenu == 9) {
        items = speechItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->vox.speechMode));
    } else if (_openMenu == 10) {
        items = ensembleItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->vox.orchestration));
    } else if (_openMenu == 11) {
        items = contourItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->vox.contourMode));
    } else if (_openMenu == 12) {
        items = factoryItems;
        selected = _plugin->factoryPresetIndex;
    } else if (_openMenu == 13) {
        items = lyricModeItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->lyric.mode));
    } else {
        items = curveItems;
        const auto score = loadScore(*_plugin);
        selected = static_cast<int>(static_cast<uint32_t>(score.nodes[std::min<uint32_t>(_selectedScoreNode, score.nodeCount - 1u)].curve));
    }
    const CGFloat itemHeight = 18.0;
    const NSRect menuRect = NSMakeRect(_menuOrigin.x, _menuOrigin.y, _menuWidth, itemHeight * _menuItemCount);
    s3g::clap_gui::drawDropdownMenu(menuRect, itemHeight, items, _menuItemCount, selected, _hoverMenuItem, attrs, style);
}

- (void)applyFactoryPreset:(uint32_t)index
{
    if (!_plugin || index >= kVoxFactoryPresetCount) return;
    const auto preset = voxFactoryPreset(index);
    if (!queueVoxPreset(*_plugin, preset.snapshot)) {
        NSBeep();
        return;
    }
    storeVoxPhrase(*_plugin, preset.phrase);
    storeVoxLyricsText(*_plugin, preset.phrase);
    rebuildVoxLyricScore(*_plugin);
    _selectedLyricCue = 0u;
    requestVoxLyricCue(*_plugin, 0u);
    if (_phraseField) {
        [_phraseField setStringValue:[NSString stringWithUTF8String:preset.phrase]];
    }
    if (_lyricsEditor) {
        _updatingLyricsEditor = YES;
        [_lyricsEditor setString:[NSString stringWithUTF8String:preset.phrase]];
        _updatingLyricsEditor = NO;
    }
    _plugin->factoryPresetIndex = static_cast<int32_t>(index);
    std::snprintf(_plugin->presetName, sizeof(_plugin->presetName), "%s", preset.name);
    _selectedVoice = std::min<uint32_t>(_selectedVoice,
        std::max<uint32_t>(1u, preset.snapshot.params.voices) - 1u);
    [self setNeedsDisplay:YES];
}

- (NSDictionary*)presetDictionaryWithName:(NSString*)name
{
    NSMutableArray* parameters = [NSMutableArray arrayWithCapacity:publicPresetParameterCount()];
    for (const auto& definition : kParams) {
        if (isLegacyHiddenParam(definition.id)) continue;
        double value = 0.0;
        if (!paramsGetValue(&_plugin->plugin, definition.id, &value)) continue;
        NSString* parameterName = [NSString stringWithUTF8String:definition.name];
        [parameters addObject:@{
            @"id": @(definition.id),
            @"name": parameterName ?: @"PARAMETER",
            @"value": @(value)
        }];
    }

    const auto score = loadScore(*_plugin);
    NSMutableArray* nodes = [NSMutableArray arrayWithCapacity:score.nodeCount];
    for (uint32_t node = 0u; node < score.nodeCount; ++node) {
        [nodes addObject:@{
            @"time": @(score.nodes[node].time),
            @"u": @(score.nodes[node].u),
            @"v": @(score.nodes[node].v),
            @"curve": @(static_cast<uint32_t>(score.nodes[node].curve))
        }];
    }
    NSDictionary* scoreDictionary = @{
        @"sustain_start": @(score.sustainStart),
        @"sustain_end": @(score.sustainEnd),
        @"nodes": nodes
    };
    NSDictionary* viewDictionary = @{
        @"mode": @(_viewMode),
        @"azimuth_degrees": @(_viewAzDeg),
        @"elevation_degrees": @(_viewElDeg),
        @"zoom": @(_viewZoom)
    };
    const std::string phraseText = loadVoxPhrase(*_plugin);
    const std::string lyricsText = loadVoxLyrics(*_plugin);
    NSString* phrase = [NSString stringWithUTF8String:phraseText.c_str()];
    NSString* lyrics = [NSString stringWithUTF8String:lyricsText.c_str()];
    return @{
        @"format": @"s3g-ambi-vox-preset",
        @"version": @1,
        @"plugin": @"org.s3g.s3g-dsp.ambi-vox-encoder-64",
        @"name": name ?: @"USER PRESET",
        @"audio_source": @"external-not-embedded",
        @"phrase": phrase ?: @"",
        @"lyrics": lyrics ?: @"",
        @"parameters": parameters,
        @"score": scoreDictionary,
        @"view": viewDictionary
    };
}

- (void)loadUserPreset
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setAllowsMultipleSelection:NO];
    [panel setCanChooseDirectories:NO];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [panel setAllowedFileTypes:@[ @"s3gvox", @"json" ]];
#pragma clang diagnostic pop
    if ([panel runModal] != NSModalResponseOK) return;

    NSData* data = [NSData dataWithContentsOfURL:[panel URL]];
    if (!data) {
        NSBeep();
        return;
    }
    NSError* error = nil;
    id root = [NSJSONSerialization JSONObjectWithData:data options:0 error:&error];
    if (error || ![root isKindOfClass:[NSDictionary class]]) {
        NSBeep();
        return;
    }
    NSDictionary* dictionary = static_cast<NSDictionary*>(root);
    NSString* format = [dictionary objectForKey:@"format"];
    NSNumber* version = [dictionary objectForKey:@"version"];
    NSArray* parameters = [dictionary objectForKey:@"parameters"];
    if (![format isKindOfClass:[NSString class]]
        || ![format isEqualToString:@"s3g-ambi-vox-preset"]
        || ![version respondsToSelector:@selector(unsignedIntValue)]
        || [version unsignedIntValue] != 1u
        || ![parameters isKindOfClass:[NSArray class]]) {
        NSBeep();
        return;
    }

    VoxPresetSnapshot candidate = makeVoxDefaultSnapshot();
    std::array<bool, kLyricCueChannelParamId + 1u> loadedIds {};
    uint32_t loaded = 0u;
    for (id item in parameters) {
        if (![item isKindOfClass:[NSDictionary class]]) continue;
        NSNumber* identifier = [item objectForKey:@"id"];
        NSNumber* number = [item objectForKey:@"value"];
        if (![identifier respondsToSelector:@selector(unsignedIntValue)]
            || ![number respondsToSelector:@selector(doubleValue)]) continue;
        const clap_id parameterId = [identifier unsignedIntValue];
        if (parameterId >= loadedIds.size() || loadedIds[parameterId]) continue;
        if (!assignVoxPresetParameter(candidate, parameterId, [number doubleValue])) continue;
        loadedIds[parameterId] = true;
        ++loaded;
    }
    const uint32_t minimumParameters = std::min<uint32_t>(24u, publicPresetParameterCount());
    if (loaded < minimumParameters) {
        NSBeep();
        return;
    }

    NSDictionary* scoreDictionary = [dictionary objectForKey:@"score"];
    if ([scoreDictionary isKindOfClass:[NSDictionary class]]) {
        NSArray* nodes = [scoreDictionary objectForKey:@"nodes"];
        if ([nodes isKindOfClass:[NSArray class]] && [nodes count] >= 2u) {
            candidate.score = s3g::ambiVotDefaultScore();
            candidate.score.nodeCount = std::min<uint32_t>(
                static_cast<uint32_t>([nodes count]), s3g::kAmbiVotMaxScoreNodes);
            for (uint32_t node = 0u; node < candidate.score.nodeCount; ++node) {
                id nodeItem = [nodes objectAtIndex:node];
                if (![nodeItem isKindOfClass:[NSDictionary class]]) continue;
                NSNumber* time = [nodeItem objectForKey:@"time"];
                NSNumber* u = [nodeItem objectForKey:@"u"];
                NSNumber* v = [nodeItem objectForKey:@"v"];
                NSNumber* curve = [nodeItem objectForKey:@"curve"];
                if ([time respondsToSelector:@selector(floatValue)]) {
                    candidate.score.nodes[node].time = [time floatValue];
                }
                if ([u respondsToSelector:@selector(floatValue)]) {
                    candidate.score.nodes[node].u = [u floatValue];
                }
                if ([v respondsToSelector:@selector(floatValue)]) {
                    candidate.score.nodes[node].v = [v floatValue];
                }
                if ([curve respondsToSelector:@selector(unsignedIntValue)]) {
                    candidate.score.nodes[node].curve = static_cast<s3g::AmbiVotScoreCurve>(
                        std::min<uint32_t>([curve unsignedIntValue], 3u));
                }
            }
            NSNumber* sustainStart = [scoreDictionary objectForKey:@"sustain_start"];
            NSNumber* sustainEnd = [scoreDictionary objectForKey:@"sustain_end"];
            if ([sustainStart respondsToSelector:@selector(unsignedIntValue)]) {
                candidate.score.sustainStart = [sustainStart unsignedIntValue];
            }
            if ([sustainEnd respondsToSelector:@selector(unsignedIntValue)]) {
                candidate.score.sustainEnd = [sustainEnd unsignedIntValue];
            }
            s3g::ambiVotNormalizeScore(candidate.score);
        }
    }

    if (!queueVoxPreset(*_plugin, candidate)) {
        NSBeep();
        return;
    }
    NSString* phrase = [dictionary objectForKey:@"phrase"];
    NSString* lyrics = [dictionary objectForKey:@"lyrics"];
    if (![lyrics isKindOfClass:[NSString class]] || [lyrics length] == 0u) lyrics = phrase;
    if ([lyrics isKindOfClass:[NSString class]]) {
        storeVoxLyricsText(*_plugin, [lyrics UTF8String]);
        rebuildVoxLyricScore(*_plugin);
        _selectedLyricCue = 0u;
        requestVoxLyricCue(*_plugin, 0u);
        const std::string firstCue = voxLyricCueText(*_plugin, 0u);
        NSString* first = [NSString stringWithUTF8String:firstCue.c_str()];
        storeVoxPhrase(*_plugin, firstCue.c_str());
        [_phraseField setStringValue:first ?: @""];
        _updatingLyricsEditor = YES;
        [_lyricsEditor setString:lyrics];
        _updatingLyricsEditor = NO;
    }

    NSDictionary* view = [dictionary objectForKey:@"view"];
    if ([view isKindOfClass:[NSDictionary class]]) {
        NSNumber* mode = [view objectForKey:@"mode"];
        NSNumber* azimuth = [view objectForKey:@"azimuth_degrees"];
        NSNumber* elevation = [view objectForKey:@"elevation_degrees"];
        NSNumber* zoom = [view objectForKey:@"zoom"];
        if ([mode respondsToSelector:@selector(intValue)]) {
            _viewMode = std::clamp([mode intValue], -1, 2);
        }
        if ([azimuth respondsToSelector:@selector(doubleValue)]) {
            _viewAzDeg = std::clamp([azimuth doubleValue], -180.0, 180.0);
        }
        if ([elevation respondsToSelector:@selector(doubleValue)]) {
            _viewElDeg = std::clamp([elevation doubleValue], -85.0, 85.0);
        }
        if ([zoom respondsToSelector:@selector(doubleValue)]) {
            _viewZoom = std::clamp([zoom doubleValue], 0.55, 2.4);
        }
        [self storeViewState];
    }

    NSString* name = [dictionary objectForKey:@"name"];
    if (![name isKindOfClass:[NSString class]] || [name length] == 0u) {
        name = [[[panel URL] lastPathComponent] stringByDeletingPathExtension];
    }
    name = [name uppercaseString];
    _plugin->factoryPresetIndex = -1;
    std::snprintf(_plugin->presetName, sizeof(_plugin->presetName), "%s", [name UTF8String]);
    _selectedVoice = std::min<uint32_t>(_selectedVoice,
        std::max<uint32_t>(1u, candidate.params.voices) - 1u);
    [self setNeedsDisplay:YES];
}

- (void)saveUserPreset
{
    [self phraseTextChanged:_phraseField];
    NSSavePanel* panel = [NSSavePanel savePanel];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [panel setAllowedFileTypes:@[ @"s3gvox" ]];
#pragma clang diagnostic pop
    [panel setNameFieldStringValue:@"s3g-vox-preset.s3gvox"];
    if ([panel runModal] != NSModalResponseOK) return;

    NSString* name = [[[panel URL] lastPathComponent] stringByDeletingPathExtension];
    if ([name length] == 0u) name = @"USER PRESET";
    NSDictionary* dictionary = [self presetDictionaryWithName:name];
    NSError* error = nil;
    NSData* data = [NSJSONSerialization dataWithJSONObject:dictionary
        options:(NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys) error:&error];
    if (error || !data || ![data writeToURL:[panel URL] atomically:YES]) {
        NSBeep();
        return;
    }
    name = [name uppercaseString];
    _plugin->factoryPresetIndex = -1;
    std::snprintf(_plugin->presetName, sizeof(_plugin->presetName), "%s", [name UTF8String]);
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    if (!_plugin) return;
    const s3g::clap_gui::Style style = s3g::clap_gui::softTextStyle();
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSDictionary* titleAttrs = s3g::clap_gui::softTitleAttrs();
    NSDictionary* labelAttrs = s3g::clap_gui::softLabelAttrs();
    NSDictionary* valueAttrs = s3g::clap_gui::softValueAttrs();
    [@"s3g AMBI VOX ENCODER 64" drawAtPoint:NSMakePoint(18, 14) withAttributes:titleAttrs];
    s3g::clap_gui::drawMenu(@"PRESET", [self presetDisplayName], 14,
        labelAttrs, valueAttrs, style, 320, 382, 190);
    const NSRect titleHeader = NSMakeRect(0, 8, kGuiW, 21);
    s3g::clap_gui::drawHeaderActionButton([self presetLoadButtonRect], titleHeader,
        @"LOAD", labelAttrs, style);
    s3g::clap_gui::drawHeaderActionButton([self presetSaveButtonRect], titleHeader,
        @"SAVE", labelAttrs, style);
    NSString* status = [NSString stringWithFormat:@"%@   64 CH",
        s3g::clap_gui::peakDbText(_plugin->outputPeak.load(std::memory_order_relaxed))];
    s3g::clap_gui::drawRightStatus(status, kGuiW, 14, valueAttrs);

    const NSRect left = [self leftPanelRect];
    s3g::clap_gui::drawPanelFrame(left.origin.x, left.origin.y, left.size.width, left.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"VOICE SPACE", true, left.origin.x, left.origin.y, left.size.width, 21, labelAttrs, style);
    [self drawPageButtons:labelAttrs style:style];
    [_lyricsScroll setHidden:_leftPage != 3 || _openMenu == 12];
    if (_leftPage == 0) {
        [self drawViewButtons:labelAttrs style:style];
        [self drawField:[self leftContentRect] attrs:valueAttrs];
    } else {
        [self drawLyricsPage:[self leftContentRect] attrs:labelAttrs valueAttrs:valueAttrs style:style];
    }
    [self drawPanels:labelAttrs valueAttrs:valueAttrs style:style];
    [self drawOpenMenu:valueAttrs style:style];
}

- (void)loadWave
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setCanChooseDirectories:NO];
    [panel setAllowsMultipleSelection:NO];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [panel setAllowedFileTypes:@[ @"wav", @"wave" ]];
#pragma clang diagnostic pop
    if ([panel runModal] != NSModalResponseOK) return;
    std::vector<float> wave = readWavMono([panel URL]);
    if (wave.size() < 32u) return;
    std::shared_ptr<const s3g::AmbiVotTableBank> bank =
        std::make_shared<s3g::AmbiVotTableBank>(s3g::ambiVotBankFromWave(wave));
    std::atomic_store_explicit(&_plugin->userBank, std::move(bank), std::memory_order_release);
    applyParam(*_plugin, kPresetParamId, static_cast<double>(s3g::AmbiVotPreset::User));
    [self setNeedsDisplay:YES];
}

- (std::vector<uint8_t>)encodedBytesFromData:(NSData*)data
{
    std::vector<uint8_t> bytes;
    if (!data || [data length] == 0) return bytes;
    const auto* raw = static_cast<const uint8_t*>([data bytes]);
    const NSUInteger length = [data length];
    NSUInteger textLike = 0;
    NSUInteger considered = 0;
    for (NSUInteger i = 0; i < std::min<NSUInteger>(length, 4096); ++i) {
        const uint8_t c = raw[i];
        if (std::isxdigit(c) || std::isspace(c) || c == 'x' || c == 'X' || c == ',' || c == ';') ++textLike;
        ++considered;
    }
    const bool parseHex = considered > 0 && textLike > considered * 8u / 10u;
    if (!parseHex) {
        bytes.assign(raw, raw + length);
        return bytes;
    }
    int high = -1;
    for (NSUInteger i = 0; i < length; ++i) {
        const uint8_t c = raw[i];
        int nibble = -1;
        if (c >= '0' && c <= '9') nibble = c - '0';
        else if (c >= 'a' && c <= 'f') nibble = 10 + c - 'a';
        else if (c >= 'A' && c <= 'F') nibble = 10 + c - 'A';
        else continue;
        if (high < 0) high = nibble;
        else {
            bytes.push_back(static_cast<uint8_t>((high << 4) | nibble));
            high = -1;
        }
    }
    return bytes;
}

- (std::shared_ptr<const VoxVoicebank>)loadVoicebankFolder:(NSURL*)folderURL
{
    if (!folderURL) return nullptr;
    auto bank = std::make_shared<VoxVoicebank>();
    bank->name = [[[folderURL path] lastPathComponent] UTF8String] ?: "voicebank";

    NSString* (^readVoiceText)(NSURL*) = ^NSString*(NSURL* url) {
        NSString* text = [NSString stringWithContentsOfURL:url encoding:NSUTF8StringEncoding error:nil];
        if (!text || [text length] == 0) {
            text = [NSString stringWithContentsOfURL:url encoding:NSShiftJISStringEncoding error:nil];
        }
        if (!text || [text length] == 0) {
            text = [NSString stringWithContentsOfURL:url encoding:NSISOLatin1StringEncoding error:nil];
        }
        return text;
    };

    NSString* characterText = readVoiceText([folderURL URLByAppendingPathComponent:@"character.txt"]);
    if (characterText && [characterText length] > 0) {
        NSArray<NSString*>* lines = [characterText componentsSeparatedByCharactersInSet:[NSCharacterSet newlineCharacterSet]];
        for (NSString* rawLine in lines) {
            NSString* line = [rawLine stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
            if ([line length] == 0) continue;
            NSRange equals = [line rangeOfString:@"="];
            if (equals.location == NSNotFound) continue;
            NSString* key = [[[line substringToIndex:equals.location] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]] lowercaseString];
            if (![key isEqualToString:@"name"]) continue;
            NSString* value = [[line substringFromIndex:equals.location + 1] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
            if ([value length] > 0) bank->name = [value UTF8String] ?: bank->name;
            break;
        }
    }

    NSString* pronunciationText = readVoiceText(
        [folderURL URLByAppendingPathComponent:@"s3g-pronunciations.txt"]);
    if (pronunciationText && [pronunciationText length] > 0) {
        NSArray<NSString*>* lines = [pronunciationText componentsSeparatedByCharactersInSet:
            [NSCharacterSet newlineCharacterSet]];
        for (NSString* rawLine in lines) {
            NSString* line = [rawLine stringByTrimmingCharactersInSet:
                [NSCharacterSet whitespaceAndNewlineCharacterSet]];
            if ([line length] == 0 || [line hasPrefix:@"#"]) continue;
            NSRange equals = [line rangeOfString:@"="];
            if (equals.location == NSNotFound || equals.location == 0) continue;
            const std::string word = voxNormalizeToken(
                [[[line substringToIndex:equals.location] lowercaseString] UTF8String] ?: "");
            NSString* value = [line substringFromIndex:equals.location + 1u];
            NSArray<NSString*>* aliases = [value componentsSeparatedByCharactersInSet:
                [NSCharacterSet characterSetWithCharactersInString:@" ,;|-\t"]];
            std::vector<std::string> tokens;
            for (NSString* alias in aliases) {
                const std::string token = voxNormalizeToken([alias UTF8String] ?: "");
                if (!token.empty()) tokens.push_back(token);
            }
            if (!word.empty() && !tokens.empty()) bank->pronunciations[word] = std::move(tokens);
        }
    }

    std::unordered_map<std::string, std::shared_ptr<const VoxVoicebankAudio>> audioByFile;

    NSMutableArray<NSURL*>* otoURLs = [NSMutableArray array];
    NSURL* rootOtoURL = [folderURL URLByAppendingPathComponent:@"oto.ini"];
    if ([[NSFileManager defaultManager] fileExistsAtPath:[rootOtoURL path]]) [otoURLs addObject:rootOtoURL];
    NSDirectoryEnumerator<NSURL*>* enumerator = [[NSFileManager defaultManager]
        enumeratorAtURL:folderURL
        includingPropertiesForKeys:@[ NSURLIsRegularFileKey ]
        options:NSDirectoryEnumerationSkipsHiddenFiles
        errorHandler:nil];
    for (NSURL* url in enumerator) {
        if ([[[url lastPathComponent] lowercaseString] isEqualToString:@"oto.ini"] &&
            ![[url path] isEqualToString:[rootOtoURL path]]) {
            [otoURLs addObject:url];
        }
    }

    for (NSURL* otoURL in otoURLs) {
        NSString* otoText = readVoiceText(otoURL);
        if (!otoText || [otoText length] == 0) continue;
        NSURL* otoFolderURL = [otoURL URLByDeletingLastPathComponent];
        NSArray<NSString*>* lines = [otoText componentsSeparatedByCharactersInSet:[NSCharacterSet newlineCharacterSet]];
        for (NSString* rawLine in lines) {
            NSString* line = [rawLine stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
            if ([line length] == 0 || [line hasPrefix:@"#"]) continue;
            NSRange equals = [line rangeOfString:@"="];
            if (equals.location == NSNotFound || equals.location == 0) continue;
            NSString* fileName = [[line substringToIndex:equals.location] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
            NSString* rest = [line substringFromIndex:equals.location + 1];
            NSArray<NSString*>* fields = [rest componentsSeparatedByString:@","];
            if ([fields count] < 6) continue;
            NSString* alias = [fields[0] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
            NSURL* wavURL = [otoFolderURL URLByAppendingPathComponent:fileName];
            const char* pathUtf8 = [[wavURL path] UTF8String];
            const std::string audioKey = pathUtf8 ? pathUtf8 : "";
            std::shared_ptr<const VoxVoicebankAudio> audio;
            if (const auto found = audioByFile.find(audioKey); found != audioByFile.end()) {
                audio = found->second;
            }
#if defined(S3G_HAS_WORLD)
            if (!audio) audio = readVoxWorldCache(wavURL);
#endif
            if (!audio) {
                int sampleRate = 0;
                std::vector<float> wave = readWavMono(wavURL, &sampleRate);
                if (wave.empty() || sampleRate <= 0) continue;
                auto prepared = std::make_shared<VoxVoicebankAudio>();
                prepared->sampleRate = sampleRate;
#if defined(S3G_HAS_WORLD)
                std::array<std::vector<float>, kVoxWorldPitchAnchorCount - 1u> variants;
                auto world = makeWorldSampleFromWave(wave, sampleRate,
                    [alias UTF8String] ?: audioKey, &variants);
                if (world && !world->samples.empty()) {
                    prepared->samples = std::move(world->samples);
                    prepared->worldPitchVariants = std::move(variants);
                    prepared->worldF0 = std::move(world->f0);
                    prepared->worldFrameEnergy = std::move(world->frameEnergy);
                    prepared->worldParameters = std::move(world->parameters);
                    prepared->worldFramePeriodMs = world->framePeriodMs;
                    prepared->baseMidi = world->baseMidi;
                    prepared->worldResynthesized = true;
                    writeVoxWorldCache(wavURL, *prepared);
                }
#endif
                if (prepared->samples.empty()) prepared->samples = std::move(wave);
                audio = std::move(prepared);
            }
            if (!audio || audio->samples.empty() || audio->sampleRate <= 0) continue;
            audioByFile[audioKey] = audio;
            const int sampleRate = audio->sampleRate;
            const auto msToSample = [&](NSString* value) -> double {
                return std::max(0.0, [value doubleValue] * static_cast<double>(sampleRate) / 1000.0);
            };
            VoxVoicebankEntry entry {};
            entry.alias = [alias UTF8String] ?: "";
            entry.fileKey = voxNormalizeToken([[[fileName stringByDeletingPathExtension] lowercaseString] UTF8String] ?: "");
            entry.searchKey = !entry.fileKey.empty() ? entry.fileKey : voxNormalizeToken(entry.alias);
            entry.audio = std::move(audio);
            const double sampleLength = static_cast<double>(entry.audio->samples.size());
            const bool hasOtoTiming = std::fabs([fields[1] doubleValue]) > 0.0001
                || std::fabs([fields[2] doubleValue]) > 0.0001
                || std::fabs([fields[3] doubleValue]) > 0.0001
                || std::fabs([fields[4] doubleValue]) > 0.0001
                || std::fabs([fields[5] doubleValue]) > 0.0001;
            const double offset = msToSample(fields[1]);
            const double fixed = msToSample(fields[2]);
            const double cutoffMs = [fields[3] doubleValue];
            const double preutter = msToSample(fields[4]);
            const double overlap = msToSample(fields[5]);
            entry.startSample = std::clamp(offset, 0.0, sampleLength - 1.0);
            entry.fixedSample = std::clamp(entry.startSample + fixed, entry.startSample, sampleLength);
            entry.preutterSample = std::clamp(entry.startSample + preutter, entry.startSample, sampleLength);
            entry.overlapSample = std::clamp(entry.startSample + overlap, entry.startSample, sampleLength);
            const double cutoffSamples = std::fabs(cutoffMs) * static_cast<double>(sampleRate) / 1000.0;
            const double requestedEnd = cutoffMs < 0.0
                ? entry.startSample + cutoffSamples
                : sampleLength - cutoffSamples;
            entry.endSample = std::clamp(requestedEnd, entry.startSample + 1.0, sampleLength);
            if (entry.endSample <= entry.startSample + 8.0 && sampleLength > entry.startSample + 8.0) {
                entry.endSample = sampleLength;
            }
            const double usableSamples = std::max(1.0, entry.endSample - entry.startSample);
            if (!hasOtoTiming && usableSamples > 16.0) {
                const std::string& key = !entry.fileKey.empty() ? entry.fileKey : entry.searchKey;
                const bool vowelOnly = key == "a" || key == "e" || key == "i"
                    || key == "o" || key == "u" || key == "n";
                const double onsetMs = vowelOnly ? 32.0 : 78.0;
                const double onsetSamples = std::clamp(
                    onsetMs * static_cast<double>(sampleRate) * 0.001,
                    usableSamples * 0.10, usableSamples * 0.42);
                entry.fixedSample = entry.startSample + onsetSamples;
                entry.preutterSample = entry.startSample + onsetSamples;
                entry.overlapSample = entry.startSample + onsetSamples * 0.35;
            }
            entry.loopStart = std::clamp(entry.fixedSample, entry.startSample, entry.endSample - 1.0);
            entry.loopEnd = hasOtoTiming
                ? entry.endSample
                : entry.startSample + usableSamples * 0.90;
            if (entry.loopEnd <= entry.loopStart + 8.0 || entry.loopEnd > sampleLength) {
                entry.loopStart = s3g::lerp(entry.startSample, entry.endSample, 0.45);
                entry.loopEnd = s3g::lerp(entry.startSample, entry.endSample, 0.88);
            }
            bank->entries.push_back(std::move(entry));
        }
    }
    if (bank->entries.empty()) return nullptr;
    return bank;
}

- (void)loadEncodedPhrase
{
    if (!_plugin) return;
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setCanChooseDirectories:YES];
    [panel setCanChooseFiles:YES];
    [panel setAllowsMultipleSelection:NO];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [panel setAllowedFileTypes:@[ @"wav", @"wave" ]];
#pragma clang diagnostic pop
    if ([panel runModal] != NSModalResponseOK) return;
    NSNumber* isDirectory = nil;
    [[panel URL] getResourceValue:&isDirectory forKey:NSURLIsDirectoryKey error:nil];
    if ([isDirectory boolValue]) {
        auto bank = [self loadVoicebankFolder:[panel URL]];
        if (!bank) return;
        std::atomic_store_explicit(&_plugin->voicebank, std::move(bank), std::memory_order_release);
        std::atomic_store_explicit(&_plugin->worldSample, std::shared_ptr<const VoxWorldSample> {}, std::memory_order_release);
        rebuildVoxLyricScore(*_plugin);
        NSString* folderName = [[[panel URL] path] lastPathComponent];
        storeVoxLoadedName(*_plugin, folderName ? [folderName UTF8String] : "voicebank");
        syncWorldSmoothing(*_plugin);
        [self setNeedsDisplay:YES];
        return;
    }
    NSString* extension = [[[panel URL] pathExtension] lowercaseString];
    NSString* name = [[panel URL] lastPathComponent];
#if defined(S3G_HAS_WORLD)
    if ([extension isEqualToString:@"wav"] || [extension isEqualToString:@"wave"]) {
        int sampleRate = 0;
        std::vector<float> wave = readWavMono([panel URL], &sampleRate);
        auto analyzed = makeWorldSampleFromWave(wave, sampleRate, name ? [name UTF8String] : "WORLD WAV");
        if (!analyzed) return;
        std::shared_ptr<const VoxWorldSample> world = std::move(analyzed);
        std::atomic_store_explicit(&_plugin->worldSample, std::move(world), std::memory_order_release);
        std::atomic_store_explicit(&_plugin->voicebank, std::shared_ptr<const VoxVoicebank> {}, std::memory_order_release);
        rebuildVoxLyricScore(*_plugin);
        storeVoxLoadedName(*_plugin, name ? [name UTF8String] : "");
        syncWorldSmoothing(*_plugin);
        for (uint32_t voice = 0; voice < s3g::kAmbiVoxMaxVoices; ++voice) {
            _plugin->voxWorldPhase[voice] = 0.0;
        }
        resetWorldPhases(*_plugin);
        [self setNeedsDisplay:YES];
        return;
    }
#endif
}

- (double)defaultValueForParam:(clap_id)param
{
    for (const auto& def : kParams) {
        if (def.id == param) return def.def;
    }
    return 0.0;
}

- (void)setParam:(clap_id)param fromNorm:(double)norm
{
    norm = std::clamp(norm, 0.0, 1.0);
    double value = 0.0;
    switch (param) {
    case kVoicesParamId: value = 1.0 + norm * static_cast<double>(s3g::kAmbiVoxMaxVoices - 1u); break;
    case kBaseNoteParamId: value = 12.0 + norm * 84.0; break;
    case kTuneParamId: value = -1200.0 + norm * 2400.0; break;
    case kWorldPitchParamId: value = -2400.0 + norm * 4800.0; break;
    case kDetuneParamId:
    case kHarmonicsParamId:
    case kSubharmonicsParamId:
    case kRaspParamId:
    case kBreathParamId:
    case kThroatParamId:
    case kDriveParamId:
    case kJitterParamId:
    case kVowelSpreadParamId:
    case kPitchScoopParamId:
    case kAttackShapeParamId:
    case kArticulationParamId:
    case kConsonantParamId:
    case kPlosiveParamId:
    case kSibilanceParamId:
    case kPhraseRateParamId:
    case kBendMacroParamId:
    case kCrushMacroParamId:
    case kScoreDepthParamId:
    case kMotionAmountParamId:
    case kSpreadParamId:
    case kCoherenceParamId:
    case kChaosParamId:
    case kLinkParamId:
    case kSmoothParamId:
    case kVectorXParamId:
    case kVectorYParamId:
    case kScanParamId:
    case kMorphParamId:
    case kSustainParamId:
    case kWorldRateParamId:
    case kWorldLoopStartParamId:
    case kWorldLoopEndParamId:
    case kWorldVoiceSpreadParamId:
    case kWorldVoiceDeviationParamId:
    case kWorldFreezeParamId:
    case kWorldScrubParamId:
    case kWorldVoicingParamId:
    case kPvocTransientParamId:
    case kPortamentoParamId:
    case kVibratoDepthParamId:
    case kTransitionParamId:
    case kPhraseSpreadParamId:
        value = norm;
        break;
    case kFormantMacroParamId:
    case kWorldAirColorParamId:
        value = -1.0 + norm * 2.0;
        break;
    case kPitchSpreadParamId: value = norm * 2.0; break;
    case kPvocStretchParamId: value = 0.25 * std::pow(16.0, norm); break;
    case kVibratoRateParamId: value = 0.1 * std::pow(120.0, norm); break;
    case kLyricCueBeatsParamId: value = 0.25 + norm * 63.75; break;
    case kLyricCueNoteParamId: value = norm * 127.0; break;
    case kLyricCueChannelParamId: value = 1.0 + norm * 15.0; break;
    case kAttackParamId: value = 1.0 * std::pow(2000.0, norm); break;
    case kDecayParamId: value = 5.0 * std::pow(800.0, norm); break;
    case kReleaseParamId: value = 5.0 * std::pow(1600.0, norm); break;
    case kMotionRateParamId: value = 0.001 * std::pow(2000.0, norm); break;
    case kScoreDurationParamId: value = 0.25 * std::pow(240.0, norm); break;
    case kSyncDivisionParamId: value = 0.25 + norm * 63.75; break;
    case kCenterAzimuthParamId: value = -180.0 + norm * 360.0; break;
    case kCenterElevationParamId: value = -90.0 + norm * 180.0; break;
    case kCenterDistanceParamId: value = 0.15 + norm * 1.85; break;
    case kNeighborRadiusParamId: value = 0.05 + norm * 3.95; break;
    case kRequiredNeighborsParamId: value = 1.0 + norm * 62.0; break;
    case kOutputParamId: value = -60.0 + norm * 72.0; break;
    default: return;
    }
    applyParam(*_plugin, param, value);
    [self markPresetCustom];
}

- (void)updateDraggedSlider:(NSPoint)point
{
    if (_dragParam == CLAP_INVALID_ID) return;
    const CGFloat trackX = _dragArea == 2 ? 120.0
        : (_dragArea == 9 ? 336.0 : (_dragArea == 7 ? 1004.0 : 738.0));
    const CGFloat trackWidth = (_dragArea == 2 || _dragArea == 9) ? 170.0 : 82.0;
    [self setParam:_dragParam fromNorm:(point.x - trackX) / trackWidth];
    [self setNeedsDisplay:YES];
}

- (void)openMenu:(int)menu count:(uint32_t)count x:(CGFloat)x y:(CGFloat)y width:(CGFloat)width
{
    _openMenu = menu;
    _menuItemCount = count;
    _hoverMenuItem = -1;
    _menuOrigin = NSMakePoint(x, y + 18.0);
    _menuWidth = width;
    if (menu == 12 && _leftPage == 3) [_lyricsScroll setHidden:YES];
    [self setNeedsDisplay:YES];
}

- (void)closeMenuAtPoint:(NSPoint)point
{
    const CGFloat itemHeight = 18.0;
    const NSRect menuRect = NSMakeRect(_menuOrigin.x, _menuOrigin.y, _menuWidth, itemHeight * _menuItemCount);
    const int hit = s3g::clap_gui::dropdownHitIndex(point, menuRect, itemHeight, _menuItemCount);
    if (hit >= 0) {
        if (_openMenu == 12) {
            [self applyFactoryPreset:static_cast<uint32_t>(hit)];
        } else {
            if (_openMenu == 1) applyParam(*_plugin, kModeParamId, hit);
            else if (_openMenu == 2) applyParam(*_plugin, kPresetParamId, hit);
            else if (_openMenu == 3) applyParam(*_plugin, kOrderParamId, hit + 1);
            else if (_openMenu == 4) applyParam(*_plugin, kMotionSceneParamId, hit);
            else if (_openMenu == 5) applyParam(*_plugin, kMotionClockParamId, hit);
            else if (_openMenu == 6) applyParam(*_plugin, kScaleParamId, hit);
            else if (_openMenu == 7) applyParam(*_plugin, kScoreModeParamId, hit);
            else if (_openMenu == 9) applyParam(*_plugin, kSpeechModeParamId, hit);
            else if (_openMenu == 10) applyParam(*_plugin, kOrchestrationParamId, hit);
            else if (_openMenu == 11) applyParam(*_plugin, kContourModeParamId, hit);
            else if (_openMenu == 13) applyParam(*_plugin, kLyricModeParamId, hit);
            else if (_openMenu == 8) {
                auto score = loadScore(*_plugin);
                const uint32_t node = std::min<uint32_t>(_selectedScoreNode, score.nodeCount - 1u);
                score.nodes[node].curve = static_cast<s3g::AmbiVotScoreCurve>(hit);
                storeScore(*_plugin, score);
            }
            [self markPresetCustom];
        }
    }
    _openMenu = 0;
    _hoverMenuItem = -1;
    _menuItemCount = 0;
    _menuWidth = 124.0;
    [_lyricsScroll setHidden:_leftPage != 3];
    [self setNeedsDisplay:YES];
}

- (int)hitVoiceAtPoint:(NSPoint)point
{
    const NSRect rect = [self leftContentRect];
    const uint32_t voices = std::clamp<uint32_t>(_plugin->params.voices, 1u, s3g::kAmbiVoxMaxVoices);
    int bestVoice = -1;
    CGFloat bestDistance = 144.0;
    for (uint32_t voice = 0; voice < voices; ++voice) {
        CGFloat depth = 0.0;
        const NSPoint projected = [self projectMotionPoint:[self snapshotPoint:voice] rect:rect depth:&depth];
        const CGFloat dx = point.x - projected.x;
        const CGFloat dy = point.y - projected.y;
        const CGFloat distance = dx * dx + dy * dy;
        if (distance < bestDistance) {
            bestDistance = distance;
            bestVoice = static_cast<int>(voice);
        }
    }
    return bestVoice;
}

- (void)selectVectorVoiceAtPoint:(NSPoint)point
{
    const NSRect content = [self leftContentRect];
    const NSRect pathRect = NSMakeRect(content.origin.x + 278, content.origin.y + 8, 270, 252);
    const NSRect inner = NSInsetRect(pathRect, 14, 28);
    const uint32_t voices = std::clamp<uint32_t>(_plugin->params.voices, 1u, s3g::kAmbiVoxMaxVoices);
    int bestVoice = -1;
    CGFloat bestDistance = 196.0;
    for (uint32_t voice = 0; voice < voices; ++voice) {
        const auto motion = [self snapshotPoint:voice];
        const NSPoint projected = NSMakePoint(inner.origin.x + motion.u * inner.size.width,
                                              inner.origin.y + (1.0f - motion.v) * inner.size.height);
        const CGFloat dx = point.x - projected.x;
        const CGFloat dy = point.y - projected.y;
        const CGFloat distance = dx * dx + dy * dy;
        if (distance < bestDistance) {
            bestDistance = distance;
            bestVoice = static_cast<int>(voice);
        }
    }
    if (bestVoice >= 0) _selectedVoice = static_cast<uint32_t>(bestVoice);
}

- (void)updateScoreDragAtPoint:(NSPoint)point
{
    auto score = loadScore(*_plugin);
    const uint32_t node = std::min<uint32_t>(_selectedScoreNode, score.nodeCount - 1u);
    const NSRect content = [self leftContentRect];
    if (_dragArea == 4) {
        const NSRect timelineRect = NSMakeRect(content.origin.x + 8, content.origin.y + 8, 540, 306);
        const NSRect lane = _scoreDragLane == 0
            ? NSMakeRect(timelineRect.origin.x + 14, timelineRect.origin.y + 34, timelineRect.size.width - 28, 112)
            : NSMakeRect(timelineRect.origin.x + 14, timelineRect.origin.y + 174, timelineRect.size.width - 28, 112);
        if (node > 0u && node + 1u < score.nodeCount) {
            const float minimum = score.nodes[node - 1u].time + 0.01f;
            const float maximum = score.nodes[node + 1u].time - 0.01f;
            score.nodes[node].time = std::clamp(static_cast<float>((point.x - lane.origin.x) / lane.size.width), minimum, maximum);
        }
        const float value = std::clamp(static_cast<float>(1.0 - (point.y - lane.origin.y) / lane.size.height), 0.0f, 1.0f);
        if (_scoreDragLane == 0) score.nodes[node].u = value;
        else score.nodes[node].v = value;
    } else if (_dragArea == 5) {
        const NSRect routeRect = NSMakeRect(content.origin.x + 8, content.origin.y + 328, 540, 292);
        const NSRect inner = NSMakeRect(routeRect.origin.x + 14, routeRect.origin.y + 30,
                                        routeRect.size.width - 28, routeRect.size.height - 44);
        score.nodes[node].u = std::clamp(static_cast<float>((point.x - inner.origin.x) / inner.size.width), 0.0f, 1.0f);
        score.nodes[node].v = std::clamp(static_cast<float>(1.0 - (point.y - inner.origin.y) / inner.size.height), 0.0f, 1.0f);
    } else if (_dragArea == 6) {
        const float value = std::clamp(static_cast<float>((point.x - (content.origin.x + 270)) / 170.0), 0.0f, 1.0f);
        if (_scoreDragLane == 2 && node > 0u && node + 1u < score.nodeCount) {
            score.nodes[node].time = std::clamp(value, score.nodes[node - 1u].time + 0.01f,
                                                score.nodes[node + 1u].time - 0.01f);
        } else if (_scoreDragLane == 3) score.nodes[node].u = value;
        else if (_scoreDragLane == 4) score.nodes[node].v = value;
    } else if (_dragArea == 8) {
        const float norm = std::clamp(static_cast<float>((point.x - 1004.0) / 82.0), 0.0f, 1.0f);
        const uint32_t selected = std::min<uint32_t>(
            static_cast<uint32_t>(std::lround(norm * static_cast<float>(score.nodeCount - 1u))),
            score.nodeCount - 1u);
        if (_scoreDragLane == 5) score.sustainStart = std::min<uint32_t>(selected, score.sustainEnd - 1u);
        else score.sustainEnd = std::max<uint32_t>(selected, score.sustainStart + 1u);
    }
    storeScore(*_plugin, score);
    [self markPresetCustom];
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_openMenu > 0) {
        [self closeMenuAtPoint:point];
        return;
    }
    if (NSPointInRect(point, [self presetMenuRect])) {
        const NSRect rect = [self presetMenuRect];
        [self openMenu:12 count:kVoxFactoryPresetCount
            x:rect.origin.x y:rect.origin.y width:rect.size.width];
        return;
    }
    if (NSPointInRect(point, [self presetLoadButtonRect])) {
        [self loadUserPreset];
        return;
    }
    if (NSPointInRect(point, [self presetSaveButtonRect])) {
        [self saveUserPreset];
        return;
    }
    if (NSPointInRect(point, [self encodedLoadButtonRect])) {
        [self loadEncodedPhrase];
        return;
    }
    if (NSPointInRect(point, [self worldResetButtonRect])) {
        resetWorldPhases(*_plugin);
        [self setNeedsDisplay:YES];
        return;
    }
    static constexpr int pages[] = { 0, 3 };
    for (int i = 0; i < 2; ++i) {
        if (!NSPointInRect(point, [self pageButtonRect:i])) continue;
        _leftPage = pages[i];
        [_lyricsScroll setHidden:_leftPage != 3];
        [self storeViewState];
        [self setNeedsDisplay:YES];
        return;
    }
    if (_leftPage == 0) {
        for (int i = 0; i < 3; ++i) {
            if (NSPointInRect(point, [self viewButtonRect:i])) {
                [self setViewPreset:i];
                return;
            }
        }
        for (int i = 0; i < 2; ++i) {
            if (NSPointInRect(point, [self zoomButtonRect:i])) {
                _viewZoom = std::clamp(_viewZoom * (i == 0 ? 0.82 : 1.22), 0.55, 2.20);
                _viewMode = -1;
                [self storeViewState];
                [self setNeedsDisplay:YES];
                return;
            }
        }
        if (NSPointInRect(point, [self leftContentRect])) {
            if (([event modifierFlags] & NSEventModifierFlagShift) != 0) {
                _dragView = YES;
                _lastDragPoint = point;
                return;
            }
            const int hit = [self hitVoiceAtPoint:point];
            if (hit >= 0) {
                _selectedVoice = static_cast<uint32_t>(hit);
                [self setNeedsDisplay:YES];
                return;
            }
        }
    } else if (_leftPage == 3) {
        const auto cues = splitVoxLyricCues(loadVoxLyrics(*_plugin));
        for (uint32_t cue = 0u; cue < cues.size(); ++cue) {
            if (!NSPointInRect(point, [self lyricCueRect:cue])) continue;
            _selectedLyricCue = cue;
            requestVoxLyricCue(*_plugin, cue);
            [_phraseField setStringValue:[NSString stringWithUTF8String:cues[cue].c_str()]];
            [self setNeedsDisplay:YES];
            return;
        }
        const NSRect content = [self leftContentRect];
        const CGFloat controlsY = content.origin.y + 618.0;
        if (NSPointInRect(point, NSMakeRect(content.origin.x + 110,
            controlsY - 1, 150, 17))) {
            [self openMenu:13 count:5 x:content.origin.x + 110
                y:controlsY width:150];
            return;
        }
        struct LyricSliderHit { clap_id param; CGFloat y; };
        const LyricSliderHit lyricSliders[] {
            { kLyricCueBeatsParamId, controlsY + 28 },
            { kLyricCueNoteParamId, controlsY + 56 },
            { kLyricCueChannelParamId, controlsY + 84 },
        };
        for (const auto& slider : lyricSliders) {
            if (!NSPointInRect(point, NSMakeRect(content.origin.x + 102,
                slider.y - 8, 404, 24))) continue;
            if ([event clickCount] >= 2) {
                applyParam(*_plugin, slider.param, [self defaultValueForParam:slider.param]);
                [self markPresetCustom];
            } else {
                _dragParam = slider.param;
                _dragArea = 9;
                [self updateDraggedSlider:point];
            }
            [self setNeedsDisplay:YES];
            return;
        }
    }

    struct MenuHit { int menu; uint32_t count; CGFloat x; CGFloat y; CGFloat width; };
    static constexpr MenuHit menus[] {
        { 1, 3, 738, 78, 124 }, { 3, 7, 738, 104, 124 },
        { 9, 3, 738, 156, 124 },
        { 10, 6, 738, 622, 124 }, { 11, 4, 738, 648, 124 },
        { 4, 5, 1004, 78, 124 }, { 5, 2, 1004, 104, 124 },
        { 6, 6, 1004, 706, 124 },
    };
    for (const auto& menu : menus) {
        if (NSPointInRect(point, NSMakeRect(menu.x, menu.y - 1, menu.width, 17))) {
            [self openMenu:menu.menu count:menu.count x:menu.x y:menu.y width:menu.width];
            return;
        }
    }
    struct SliderHit { clap_id param; CGFloat x; CGFloat y; int area; };
    static constexpr SliderHit commonSliders[] {
        { kVoicesParamId, 638, 130, 1 }, { kOutputParamId, 638, 546, 1 },
        { kFormantMacroParamId, 638, 674, 1 }, { kWorldVoicingParamId, 638, 700, 1 },
        { kWorldAirColorParamId, 638, 726, 1 },
        { kMotionRateParamId, 904, 130, 7 }, { kSyncDivisionParamId, 904, 156, 7 },
        { kMotionAmountParamId, 904, 182, 7 }, { kSpreadParamId, 904, 208, 7 },
        { kCoherenceParamId, 904, 234, 7 }, { kChaosParamId, 904, 260, 7 },
        { kLinkParamId, 904, 286, 7 }, { kSmoothParamId, 904, 312, 7 },
        { kCenterAzimuthParamId, 904, 338, 7 }, { kCenterElevationParamId, 904, 364, 7 },
        { kCenterDistanceParamId, 904, 390, 7 },
        { kBaseNoteParamId, 904, 680, 7 }, { kPitchSpreadParamId, 904, 732, 7 },
        { kWorldPitchParamId, 904, 758, 7 }, { kDetuneParamId, 904, 784, 7 },
        { kPortamentoParamId, 904, 810, 7 }, { kVibratoDepthParamId, 904, 836, 7 },
        { kVibratoRateParamId, 904, 862, 7 },
    };
    static constexpr SliderHit phraseSliders[] {
        { kPhraseRateParamId, 638, 240, 1 }, { kPvocStretchParamId, 638, 266, 1 },
        { kPvocTransientParamId, 638, 292, 1 }, { kTransitionParamId, 638, 318, 1 },
        { kPhraseSpreadParamId, 638, 344, 1 }, { kWorldVoiceSpreadParamId, 638, 370, 1 },
        { kWorldVoiceDeviationParamId, 638, 396, 1 },
    };
    static constexpr SliderHit textureSliders[] {
        { kWorldRateParamId, 638, 240, 1 }, { kWorldLoopStartParamId, 638, 266, 1 },
        { kWorldLoopEndParamId, 638, 292, 1 }, { kWorldFreezeParamId, 638, 318, 1 },
        { kWorldScrubParamId, 638, 344, 1 }, { kPhraseSpreadParamId, 638, 370, 1 },
        { kPvocStretchParamId, 638, 396, 1 }, { kPvocTransientParamId, 638, 422, 1 },
        { kWorldVoiceSpreadParamId, 638, 448, 1 }, { kWorldVoiceDeviationParamId, 638, 474, 1 },
    };
    const auto hitSlider = [&](const SliderHit& slider) {
        if (!NSPointInRect(point, NSMakeRect(slider.x, slider.y - 8, 232, 24))) return false;
            if ([event clickCount] >= 2) {
                applyParam(*_plugin, slider.param, [self defaultValueForParam:slider.param]);
                [self markPresetCustom];
            }
            else {
                _dragParam = slider.param;
                _dragArea = slider.area;
                [self updateDraggedSlider:point];
            }
        return true;
    };
    for (const auto& slider : commonSliders) {
        if (hitSlider(slider)) return;
    }
    if (_plugin->vox.speechMode == VoxSpeechMode::Texture) {
        for (const auto& slider : textureSliders) {
            if (hitSlider(slider)) return;
        }
    } else {
        for (const auto& slider : phraseSliders) {
            if (hitSlider(slider)) return;
        }
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_dragView) {
        const CGFloat dx = point.x - _lastDragPoint.x;
        const CGFloat dy = point.y - _lastDragPoint.y;
        _viewAzDeg += dx * 0.35;
        while (_viewAzDeg > 180.0) _viewAzDeg -= 360.0;
        while (_viewAzDeg <= -180.0) _viewAzDeg += 360.0;
        _viewElDeg = std::clamp(_viewElDeg - dy * 0.30, -85.0, 85.0);
        _viewMode = -1;
        _lastDragPoint = point;
        [self storeViewState];
        [self setNeedsDisplay:YES];
        return;
    }
    [self updateDraggedSlider:point];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragParam = CLAP_INVALID_ID;
    _dragArea = 0;
    _scoreDragLane = -1;
    _dragView = NO;
}

- (void)mouseMoved:(NSEvent*)event
{
    if (_openMenu <= 0 || _menuItemCount == 0u) return;
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    const CGFloat itemHeight = 18.0;
    const NSRect menuRect = NSMakeRect(_menuOrigin.x, _menuOrigin.y, _menuWidth, itemHeight * _menuItemCount);
    const int next = s3g::clap_gui::dropdownHitIndex(point, menuRect, itemHeight, _menuItemCount);
    if (next != _hoverMenuItem) {
        _hoverMenuItem = next;
        [self setNeedsDisplay:YES];
    }
}

@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating)
{
    return !isFloating && api && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
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
    auto* state = self(plugin);
    if (state->guiView) return true;
    state->guiView = [[S3GAmbiVoxEncoderView alloc] initWithPlugin:state];
    return state->guiView != nullptr;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* state = self(plugin);
    if (!state || !state->guiView) return;
    state->guiVisible.store(false, std::memory_order_relaxed);
    auto* view = static_cast<S3GAmbiVoxEncoderView*>(state->guiView);
    [view stopRefreshTimer];
    [view removeFromSuperview];
    [view release];
    state->guiView = nullptr;
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t*, uint32_t* width, uint32_t* height)
{
    if (!width || !height) return false;
    *width = kGuiW;
    *height = kGuiH;
    return true;
}
bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t*, uint32_t*) { return false; }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t width, uint32_t height)
{
    auto* state = self(plugin);
    if (!state->guiView) return false;
    [static_cast<NSView*>(state->guiView) setFrameSize:NSMakeSize(width, height)];
    return true;
}
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    if (!window || !window->api || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0 || !window->cocoa) return false;
    auto* state = self(plugin);
    if (!state->guiView) return false;
    NSView* parent = static_cast<NSView*>(window->cocoa);
    NSView* view = static_cast<NSView*>(state->guiView);
    [parent addSubview:view];
    [view setFrame:NSMakeRect(0, 0, kGuiW, kGuiH)];
    return true;
}
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin)
{
    auto* state = self(plugin);
    if (!state->guiView) return false;
    state->guiVisible.store(true, std::memory_order_relaxed);
    [static_cast<NSView*>(state->guiView) setHidden:NO];
    [static_cast<S3GAmbiVoxEncoderView*>(state->guiView) startRefreshTimer];
    return true;
}
bool guiHide(const clap_plugin_t* plugin)
{
    auto* state = self(plugin);
    if (!state->guiView) return false;
    state->guiVisible.store(false, std::memory_order_relaxed);
    [static_cast<S3GAmbiVoxEncoderView*>(state->guiView) stopRefreshTimer];
    [static_cast<NSView*>(state->guiView) setHidden:YES];
    return true;
}

const clap_plugin_gui_t guiExt {
    guiIsApiSupported,
    guiGetPreferredApi,
    guiCreate,
    guiDestroy,
    guiSetScale,
    guiGetSize,
    guiCanResize,
    guiGetResizeHints,
    guiAdjustSize,
    guiSetSize,
    guiSetParent,
    guiSetTransient,
    guiSuggestTitle,
    guiShow,
    guiHide,
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
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SYNTHESIZER,
    CLAP_PLUGIN_FEATURE_AMBISONIC,
    nullptr,
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.ambi-vox-encoder-64",
    "s3g Ambi Vox Encoder 64",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.4.0-pre",
    "16-voice ambisonic WORLD and voicebank instrument with phrase, ensemble, and spectral controls.",
    features,
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* state = new (std::nothrow) Plugin();
    if (!state) return nullptr;
    state->host = host;
    state->presetBanks[0] = makePresetBank(s3g::AmbiVotPreset::Sine);
    state->presetBanks[1] = makePresetBank(s3g::AmbiVotPreset::Classic);
    state->presetBanks[2] = makePresetBank(s3g::AmbiVotPreset::Digital);
    state->presetBanks[3] = makePresetBank(s3g::AmbiVotPreset::Formant);
    std::shared_ptr<const s3g::AmbiVotTableBank> defaultVoxBank = std::make_shared<s3g::AmbiVotTableBank>(
        s3g::ambiVoxPresetBank(s3g::AmbiVoxPreset::BlackMetal));
    std::atomic_store_explicit(&state->userBank, std::move(defaultVoxBank), std::memory_order_release);
    const auto defaultPreset = voxFactoryPreset(0u);
    state->params = defaultPreset.snapshot.params;
    state->vox = defaultPreset.snapshot.vox;
    state->lyric = defaultPreset.snapshot.lyric;
    state->factoryPresetIndex = 0;
    std::snprintf(state->presetName, sizeof(state->presetName), "%s", defaultPreset.name);
    storeVoxPhrase(*state, defaultPreset.phrase);
    storeVoxLyricsText(*state, defaultPreset.phrase);
    rebuildVoxLyricScore(*state);
    storeScore(*state, defaultPreset.snapshot.score);
    state->voxOutputGainSmooth = s3g::dbToGain(state->params.outputGainDb);
    state->voxLimiterGain = 1.0f;
    syncWorldSmoothing(*state);
    state->pvoc.prepare();
    resetVoxMidiVoices(*state);
    for (uint32_t voice = 0; voice < s3g::kAmbiVoxMaxVoices; ++voice) {
        state->voxWorldVoiceGainSmooth[voice] = voice < state->params.voices ? 1.0f : 0.0f;
        state->voxPitchRatioSmooth[voice] = 1.0f;
        state->voxContourRatioSmooth[voice] = 1.0f;
        state->pvoc.reset(state->voxPvocVoice[voice], nullptr, 0.0);
        state->pvoc.reset(state->voxPvocNextVoice[voice], nullptr, 0.0);
        state->voxTargetNoteSmooth[voice] = voxVoiceTargetNote(*state, voice);
        state->voxVibratoPhase[voice] = s3g::ambiVotFract(static_cast<float>(voice) * 0.3819660113f);
        state->voxNeighborEnv[voice] = 1.0f;
        state->voxLpcPhase[voice] = s3g::ambiVotFract(static_cast<float>(voice) * 0.6180339887f);
        state->voxLpcF0[voice] = 110.0f;
        state->voxFormant1Hz[voice] = 600.0f;
        state->voxFormant2Hz[voice] = 1500.0f;
        state->voxFormant3Hz[voice] = 2800.0f;
    }
    state->engine.prepare(state->sampleRate);
    state->engine.setParams(state->params);
    state->engine.setScore(loadScore(*state));
    state->params = state->engine.params();
#if defined(__APPLE__)
    const auto& points = state->engine.motionPoints();
    for (uint32_t i = 0; i < s3g::kAmbiVoxMaxVoices; ++i) {
        state->guiAzimuth[i].store(points[i].azimuthDeg, std::memory_order_relaxed);
        state->guiElevation[i].store(points[i].elevationDeg, std::memory_order_relaxed);
        state->guiDistance[i].store(points[i].distance, std::memory_order_relaxed);
        state->guiU[i].store(points[i].u, std::memory_order_relaxed);
        state->guiV[i].store(points[i].v, std::memory_order_relaxed);
        state->guiNeighborCount[i].store(0u, std::memory_order_relaxed);
        state->guiNeighborGate[i].store(0u, std::memory_order_relaxed);
    }
#endif
    state->plugin.desc = &descriptor;
    state->plugin.plugin_data = state;
    state->plugin.init = init;
    state->plugin.destroy = destroy;
    state->plugin.activate = activate;
    state->plugin.deactivate = deactivate;
    state->plugin.start_processing = startProcessing;
    state->plugin.stop_processing = stopProcessing;
    state->plugin.reset = reset;
    state->plugin.process = process;
    state->plugin.get_extension = pluginGetExtension;
    state->plugin.on_main_thread = onMainThread;
    return &state->plugin;
}

uint32_t factoryGetPluginCount(const clap_plugin_factory*) { return 1; }
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory*, uint32_t index)
{
    return index == 0u ? &descriptor : nullptr;
}
const clap_plugin_t* factoryCreatePlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    return pluginId && std::strcmp(pluginId, descriptor.id) == 0 ? create(host) : nullptr;
}

const clap_plugin_factory pluginFactory {
    factoryGetPluginCount,
    factoryGetPluginDescriptor,
    factoryCreatePlugin,
};

bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId)
{
    return factoryId && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &pluginFactory : nullptr;
}

} // namespace

extern "C" const CLAP_EXPORT clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory,
};
