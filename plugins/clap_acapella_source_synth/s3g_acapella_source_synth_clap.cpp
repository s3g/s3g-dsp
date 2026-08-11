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

constexpr uint32_t kStateVersion = 10u;
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
constexpr uint32_t kParamCount = 64u;
constexpr uint32_t kSavedParamCount = kParamCount - 1u;
constexpr double kCustomPreset = 6.0;

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
    { kPresetParamId, "Source Profile", "Source", 0.0, 6.0, 0.0, true },
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
    { kIntelligibilityParamId, "Intelligibility", "Phoneme Engine", 0.0, 1.0, 0.78, false },
    { kEchoHeadsParamId, "Echo Heads", "Tape Echo", 0.0, 6.0, 6.0, true },
    { kEchoClockParamId, "Echo Clock", "Tape Echo", 0.0, 9.0, 5.0, true },
    { kEchoFeedbackParamId, "Echo Feedback", "Tape Echo", 0.0, 0.92, 0.34, false },
    { kEchoWearParamId, "Tape Wear", "Tape Echo", 0.0, 1.0, 0.18, false },
    { kEchoFlutterParamId, "Tape Flutter", "Tape Echo", 0.0, 1.0, 0.10, false },
    { kEchoToneParamId, "Echo Tone", "Tape Echo", -1.0, 1.0, -0.12, false },
    { kEchoSpreadParamId, "Head Spread", "Tape Echo", 0.0, 1.0, 0.58, false },
}};

struct StateHeader {
    uint32_t version = kStateVersion;
    uint32_t reserved = 0u;
};

constexpr std::array<clap_id, kSavedParamCount> kSavedParamIds {{
    1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u,
    13u, 14u, 15u, 16u, 17u, 18u, 19u, 20u, 21u, 22u, 23u, 24u,
    26u, 27u, 28u, 29u, 30u, 31u, 32u, 33u, 34u, 35u, 36u, 37u,
    38u, 39u, 40u, 41u, 42u, 43u, 44u, 45u,
    46u, 47u, 48u, 49u, 50u, 51u, 52u, 53u, 54u,
    55u, 56u, 57u,
    58u, 59u, 60u, 61u, 62u, 63u, 64u,
}};

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
    s3g::clap_gui::SpscEventQueue<TextProgramMessage, 8u>
        textProgramEvents {};
    s3g::clap_gui::ParamEventQueue<> guiParamEvents {};
    s3g::AcapellaGestureProgram activeTextProgram {};
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

const ParamDef* paramDef(clap_id id)
{
    for (const auto& def : kParamDefs) {
        if (def.id == id) return &def;
    }
    return nullptr;
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
    } else if (plugin.host && plugin.host->request_process) {
        plugin.host->request_process(plugin.host);
    }
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

void selectPreset(Plugin& plugin, uint32_t index)
{
    if (index >= 6u) {
        storeValue(plugin, kPresetParamId, kCustomPreset);
        return;
    }
    const auto params = s3g::acapellaSourcePreset(
        static_cast<s3g::AcapellaSourcePreset>(index));
    storeVoiceParams(plugin, params);
    storeEffectsParams(plugin, s3g::acapellaVocalFxPreset(
        static_cast<s3g::AcapellaSourcePreset>(index)));
    storeEnsembleParams(plugin, s3g::acapellaEnsemblePreset(
        static_cast<s3g::AcapellaSourcePreset>(index)));
    storeValue(plugin, kPresetParamId, static_cast<double>(index));
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
        || (id >= kGestureSequenceParamId && id <= kGestureDivisionParamId);
}

void publishControlParam(Plugin& plugin, clap_id id, double value)
{
    const auto* def = paramDef(id);
    if (!def) return;
    value = clampValue(*def, value);
    if (id == kPresetParamId) {
        selectPreset(plugin, static_cast<uint32_t>(value));
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
    if (customisingParam(id)) storeValue(plugin, kPresetParamId, kCustomPreset);
    if (plugin.host && plugin.host->request_process) {
        plugin.host->request_process(plugin.host);
    }
}

void syncAudioParams(Plugin& plugin)
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
    auto effects = plugin.effectsParams;
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
    plugin.effectsParams = s3g::sanitizeAcapellaVocalFxParams(effects);
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
    plugin.effects.setParams(plugin.effectsParams);
}

float midiFrequency(int16_t key)
{
    return 440.0f * std::exp2((static_cast<float>(key) - 69.0f) / 12.0f);
}

void triggerVoice(Plugin& plugin, int16_t key, float velocity,
    int32_t noteId = -1, int16_t channel = -1, bool audition = false)
{
    syncAudioParams(plugin);
    plugin.ensemble.trigger({
        { plugin.vowel, plugin.onset, midiFrequency(key), velocity,
            plugin.durationMs },
        audition ? -2 : noteId,
        channel,
        key,
    });
    plugin.auditionVoice = audition;
}

bool applyAudioParam(Plugin& plugin, clap_id id, double value)
{
    const auto* def = paramDef(id);
    if (!def) return false;
    value = clampValue(*def, value);
    if (id == kPresetParamId) {
        selectPreset(plugin, static_cast<uint32_t>(value));
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
    if (customisingParam(id)) storeValue(plugin, kPresetParamId, kCustomPreset);
    return true;
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
        const bool noteOn = event->type == CLAP_EVENT_NOTE_ON
            && note->velocity > 0.0;
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

bool serviceGuiParamEvents(Plugin& plugin,
    const clap_output_events_t* output)
{
    s3g::clap_gui::ParamEvent pending;
    while (plugin.guiParamEvents.peek(pending)) {
        if (!pushGuiParamEvent(output, pending)) break;
        plugin.guiParamEvents.pop();
    }
    // queueGuiParamValue() already published the value atomically. This path
    // only mirrors the gesture to the host; DSP changes remain on process().
    return false;
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

clap_process_status process(const clap_plugin_t* plugin,
    const clap_process_t* processData)
{
    auto* instance = self(plugin);
    if (!processData) return CLAP_PROCESS_ERROR;
    serviceTextPrograms(*instance);
    (void)serviceGuiParamEvents(*instance, processData->out_events);
    syncAudioParams(*instance);
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
        if (paramsChanged) syncAudioParams(*instance);
        return instance->ensemble.active() || instance->effects.active()
            ? CLAP_PROCESS_CONTINUE : CLAP_PROCESS_SLEEP;
    }

    const auto& output = processData->audio_outputs[0u];
    const uint32_t channels = std::min<uint32_t>(
        output.channel_count, kOutputChannels);
    if (channels == 0u || (!output.data32 && !output.data64)) {
        return CLAP_PROCESS_ERROR;
    }
    const float gainCoefficient = 1.0f - std::exp(
        -1.0f / (0.010f * static_cast<float>(instance->sampleRate)));
    float blockPeak = 0.0f;
    for (uint32_t frame = 0u; frame < processData->frames_count; ++frame) {
        bool paramsChanged = false;
        while (eventIndex < eventCount) {
            const auto* event = events->get(events, eventIndex);
            if (!event || event->time > frame) break;
            paramsChanged |= applyEvent(*instance, event);
            ++eventIndex;
        }
        if (paramsChanged) syncAudioParams(*instance);
        instance->smoothedOutputGain += (instance->outputGain
            - instance->smoothedOutputGain) * gainCoefficient;
        const auto ensemble = instance->ensemble.processFrame();
        const auto vocal = instance->effects.processFrameStereo(
            ensemble.left, ensemble.right);
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
    bool paramsChanged = false;
    while (eventIndex < eventCount) {
        paramsChanged |= applyEvent(
            *instance, events->get(events, eventIndex++));
    }
    if (paramsChanged) syncAudioParams(*instance);
    instance->outputPeak.store(blockPeak, std::memory_order_relaxed);
    return instance->ensemble.active() || instance->effects.active()
        ? CLAP_PROCESS_CONTINUE : CLAP_PROCESS_SLEEP;
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
    *value = loadValue(*self(plugin), id);
    return true;
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value,
    char* display, uint32_t size)
{
    if (!display || size == 0u || !paramDef(id)) return false;
    if (id == kPresetParamId) {
        constexpr const char* names[] {
            "Neutral", "Rhythmic", "Air", "Pressed",
            "Overdrive", "Subharmonic", "Custom"
        };
        const uint32_t index = std::min<uint32_t>(6u,
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
    } else if (id == kDurationParamId || id == kGlideParamId
        || id == kAttackParamId || id == kReleaseParamId
        || id == kRetriggerParamId || id == kEchoTimeParamId
        || id == kDoubleTimingParamId || id == kOnsetGuardParamId) {
        std::snprintf(display, size, "%.1f ms", value);
    } else if (id == kVibratoRateParamId || id == kGestureRateParamId) {
        std::snprintf(display, size, "%.2f Hz", value);
    } else if (id == kVibratoDepthParamId || id == kPitchDriftParamId
        || id == kDoubleDetuneParamId) {
        std::snprintf(display, size, "%.1f ct", value);
    } else if (id == kScoopParamId || id == kDeclinationParamId) {
        std::snprintf(display, size, "%+.2f st", value);
    } else if (id == kOutputParamId || id == kFuzzDriveParamId) {
        std::snprintf(display, size, "%+.1f dB", value);
    } else if (id == kFuzzToneParamId) {
        std::snprintf(display, size, "%.0f Hz", value);
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
    if (id == kPresetParamId) {
        constexpr const char* names[] {
            "Neutral", "Rhythmic", "Air", "Pressed",
            "Overdrive", "Subharmonic", "Custom"
        };
        for (uint32_t index = 0u; index < 7u; ++index) {
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
    (void)serviceGuiParamEvents(*instance, output);
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
    } else if (header.version == kStateVersion) {
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
    storeValue(*instance, kAuditionParamId, 0.0);
    instance->controlAuditionGate.store(false, std::memory_order_release);
    if (instance->host && instance->host->request_process) {
        instance->host->request_process(instance->host);
    }
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

uint32_t tailGet(const clap_plugin_t* plugin)
{
    const auto* instance = self(plugin);
    const double releaseSeconds = loadValue(*instance, kReleaseParamId) * 0.002;
    const double delaySamples = loadValue(*instance, kEchoMixParamId) > 0.001
        ? static_cast<double>(instance->effects.tailSamples()) : 0.0;
    const double samples = releaseSeconds * instance->sampleRate + delaySamples;
    return static_cast<uint32_t>(std::min<double>(
        std::numeric_limits<uint32_t>::max() - 1u,
        std::max(1.0, std::ceil(samples))));
}

const clap_plugin_tail_t tailExt { tailGet };

#if defined(__APPLE__)
} // namespace
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

    [self drawPanel:@"PHONEME SCORE" panel:kArticulatorPhrasePanel
        style:style];
    [self drawScoreGrid:style];
    [self drawScoreControls:style];
    s3g::clap_gui::drawHeaderActionButton(
        articulatorCompileRect(), articulatorCompileRect(),
        @"COMPILE", s3g::clap_gui::softLabelAttrs(), style);

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
        [_phraseField setHidden:NO];
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
            point, &_plugin->plugin, @"Processor Articulator",
            titleBand, _titlePresetName, sizeof(_titlePresetName),
            kOutputParamId)) {
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, articulatorCompileRect())) {
        [self commitPhrase:self];
        return;
    }

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
        kGestureRateParamId, kGestureDepthParamId, kIntelligibilityParamId,
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
    instance->guiView = [[S3GProcessorArticulatorView alloc]
        initWithPlugin:instance];
    if (!instance->guiView) return false;
    if (!s3g::clap_gui::createResponsiveViewport(instance->guiViewport,
            static_cast<NSView*>(instance->guiView), kGuiWidth, kGuiHeight,
            kGuiWidth, 360u)) {
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
    [static_cast<S3GProcessorArticulatorView*>(instance->guiView)
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
    [static_cast<S3GProcessorArticulatorView*>(instance->guiView)
        startRefreshTimer];
    return true;
}
bool guiHide(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance->guiView) return false;
    instance->guiVisible = false;
    [static_cast<S3GProcessorArticulatorView*>(instance->guiView)
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
    nullptr
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.processor-articulator",
    "s3g Processor Articulator",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "1.4.0",
    "Sample-free polyphonic articulatory synthesizer with text-to-phoneme scoring, continuous tract motion, procedural doubling, shape processing, and synchronized multi-head tape echo.",
    features
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* instance = new (std::nothrow) Plugin();
    if (!instance) return nullptr;
    instance->host = host;
    for (const auto& def : kParamDefs) storeValue(*instance, def.id, def.defaultValue);
    selectPreset(*instance, 0u);
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
