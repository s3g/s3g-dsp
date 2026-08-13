#pragma once

#include "s3g_acapella_source_synth.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAcapellaResonatorBands = 22u;
constexpr uint32_t kAcapellaResonatorWideBands = 16u;
constexpr uint32_t kAcapellaResonatorMaxVoices = 8u;
constexpr float kAcapellaResonatorPitchMinimumHz = 48.0f;
constexpr float kAcapellaResonatorPitchMaximumHz = 2000.0f;
// The maximum Pitch Hold control position is a true latch, not a very long
// timer. Keeping the sentinel in the shared DSP contract lets hosts display
// and automate the same endpoint without duplicating a magic value.
constexpr float kAcapellaResonatorInfinitePitchHoldMs = 2000.0f;

constexpr std::array<float, kAcapellaResonatorBands>
    kAcapellaSpeechBandFrequencies {{
        185.0f, 220.0f, 262.0f, 311.0f, 370.0f, 440.0f,
        523.0f, 622.0f, 740.0f, 880.0f, 1047.0f, 1245.0f,
        1480.0f, 1760.0f, 2093.0f, 2489.0f, 2960.0f, 3520.0f,
        4186.0f, 4978.0f, 5920.0f, 7040.0f,
    }};

enum class AcapellaResonatorBandLayout : uint32_t {
    Speech22 = 0u,
    Wide16,
};

enum class AcapellaResonatorAnalysisSlope : uint32_t {
    FourPole = 0u,
    EightPole,
};

constexpr uint32_t kAcapellaResonatorAnalysisSlopeCount = 2u;

enum class AcapellaResonatorCarrierPitchSource : uint32_t {
    Midi = 0u,
    Voice,
};

constexpr uint32_t kAcapellaResonatorCarrierPitchSourceCount = 2u;

enum class AcapellaResonatorPitchScale : uint32_t {
    Continuous = 0u,
    Chromatic,
    Major,
    NaturalMinor,
    HarmonicMinor,
    Dorian,
    MajorPentatonic,
    MinorPentatonic,
};

constexpr uint32_t kAcapellaResonatorPitchScaleCount = 8u;

constexpr uint32_t kAcapellaResonatorBandLayoutCount = 2u;

enum class AcapellaResonatorMode : uint32_t {
    Vocoder = 0u,
    Hybrid,
    FilterBank,
    Resonator = FilterBank,
};

constexpr uint32_t kAcapellaResonatorModeCount = 3u;

enum class AcapellaResonatorCarrierShape : uint32_t {
    Glottal = 0u,
    Saw,
    Pulse,
    Fold,
    Noise,
};

constexpr uint32_t kAcapellaResonatorCarrierShapeCount = 5u;

enum class AcapellaResonatorVoicingMode : uint32_t {
    Tonal = 0u,
    Noise,
    Blend,
    Detect,
};

constexpr uint32_t kAcapellaResonatorVoicingModeCount = 4u;

enum class AcapellaResonatorStereoMode : uint32_t {
    Mono = 0u,
    Spread,
    OddEven,
};

constexpr uint32_t kAcapellaResonatorStereoModeCount = 3u;

enum class AcapellaResonatorModulatorSource : uint32_t {
    ExternalMic = 0u,
    InternalSpeech,
    Blend,
};

constexpr uint32_t kAcapellaResonatorModulatorSourceCount = 3u;

enum class AcapellaResonatorCarrierLfoShape : uint32_t {
    Triangle = 0u,
    Square,
};

constexpr uint32_t kAcapellaResonatorCarrierLfoShapeCount = 2u;

enum class AcapellaResonatorMatrixMode : uint32_t {
    Identity = 0u,
    Rotate,
    Mirror,
    Chord,
    Sparse,
    Custom,
};

constexpr uint32_t kAcapellaResonatorMatrixModeCount = 6u;

enum class AcapellaResonatorFreezeTrigger : uint32_t {
    Continuous = 0u,
    Note,
    Phoneme,
    Syllable,
    Word,
    Rest,
};

constexpr uint32_t kAcapellaResonatorFreezeTriggerCount = 6u;

// Parameter IDs 65--86 map to these fields in declaration order. The bank is
// intentionally control-domain rather than FFT-domain: it stores filter and
// envelope state, never waveform samples or spectral frames.
struct AcapellaResonatorParams {
    float amount = 0.90f;
    AcapellaResonatorBandLayout bandLayout
        = AcapellaResonatorBandLayout::Speech22;
    AcapellaResonatorAnalysisSlope analysisSlope
        = AcapellaResonatorAnalysisSlope::FourPole;
    AcapellaResonatorMode mode = AcapellaResonatorMode::Hybrid;
    AcapellaResonatorCarrierShape carrierShape
        = AcapellaResonatorCarrierShape::Glottal;
    float carrierHarmonics = 0.72f;
    float carrierColor = 0.0f;
    float carrierNoise = 0.10f;
    AcapellaResonatorCarrierPitchSource carrierPitchSource
        = AcapellaResonatorCarrierPitchSource::Midi;
    uint32_t pitchScaleRoot = 0u;
    AcapellaResonatorPitchScale pitchScale
        = AcapellaResonatorPitchScale::Chromatic;
    float pitchHoldMs = 350.0f;
    AcapellaResonatorVoicingMode voicingMode
        = AcapellaResonatorVoicingMode::Detect;
    float voicingThreshold = 0.52f;
    float voicedTransitionMs = 38.0f;
    float unvoicedTransitionMs = 16.0f;
    float voicedLevel = 1.0f;
    float unvoicedLevel = 0.82f;
    AcapellaResonatorModulatorSource modulatorSource
        = AcapellaResonatorModulatorSource::InternalSpeech;
    float micGainDb = 0.0f;
    // Retained as an internal/standalone compatibility rail. The v5 plug-in
    // no longer exposes an external carrier: its audio input is a modulator.
    float externalCarrierMix = 0.55f;
    float externalCarrierGainDb = 0.0f;
    float pulseWidth = 0.50f;
    AcapellaResonatorCarrierLfoShape carrierLfoShape
        = AcapellaResonatorCarrierLfoShape::Triangle;
    float carrierLfoRateHz = 0.22f;
    float carrierLfoDepthSemitones = 0.0f;
    float carrierLfoPwmDepth = 0.0f;
    bool carrierLfoSync = false;
    float carrierLfoSyncDivisionBeats = 1.0f;
    // Zero is measured analysis; one is the score-derived phoneme envelope.
    float analysisBlend = 0.72f;
    float attackMs = 8.0f;
    float releaseMs = 120.0f;
    float resonance = 0.48f;
    float driveDb = 3.0f;
    float bandShiftSemitones = 0.0f;
    float bandStretch = 0.0f;
    float tilt = 0.0f;
    float sibilance = 0.55f;
    float openLevel = 0.08f;
    float articulationThru = 0.0f;
    int32_t coupling = 0;
    std::array<float, kAcapellaResonatorBands> bandTrims = [] {
        std::array<float, kAcapellaResonatorBands> values {};
        values.fill(1.0f);
        return values;
    }();
    AcapellaResonatorMatrixMode matrixMode
        = AcapellaResonatorMatrixMode::Custom;
    float matrixMorph = 1.0f;
    std::array<float, kAcapellaResonatorBands
        * kAcapellaResonatorBands> customMatrixA = [] {
            std::array<float, kAcapellaResonatorBands
                * kAcapellaResonatorBands> values {};
            for (uint32_t band = 0u;
                 band < kAcapellaResonatorBands; ++band) {
                values[band * kAcapellaResonatorBands + band] = 1.0f;
            }
            return values;
        }();
    std::array<float, kAcapellaResonatorBands
        * kAcapellaResonatorBands> customMatrixB = customMatrixA;
    float customMatrixMorph = 0.0f;
    AcapellaResonatorStereoMode stereoMode
        = AcapellaResonatorStereoMode::Spread;
    float stereoSpread = 0.25f;
    float freeze = 0.0f;
    AcapellaResonatorFreezeTrigger freezeTrigger
        = AcapellaResonatorFreezeTrigger::Phoneme;
    float blurMs = 70.0f;
    float gestureFollow = 0.82f;
};

// Copy the control-rate portion without touching the large stored routing
// scenes. The CLAP wrapper uses this after it has already clamped incoming
// values; matrix cells and trims have dedicated bounded RT setters.
inline void copyAcapellaResonatorControlParams(
    AcapellaResonatorParams& target,
    const AcapellaResonatorParams& source)
{
    target.amount = source.amount;
    target.bandLayout = source.bandLayout;
    target.analysisSlope = source.analysisSlope;
    target.mode = source.mode;
    target.carrierShape = source.carrierShape;
    target.carrierHarmonics = source.carrierHarmonics;
    target.carrierColor = source.carrierColor;
    target.carrierNoise = source.carrierNoise;
    target.carrierPitchSource = source.carrierPitchSource;
    target.pitchScaleRoot = source.pitchScaleRoot;
    target.pitchScale = source.pitchScale;
    target.pitchHoldMs = source.pitchHoldMs;
    target.voicingMode = source.voicingMode;
    target.voicingThreshold = source.voicingThreshold;
    target.voicedTransitionMs = source.voicedTransitionMs;
    target.unvoicedTransitionMs = source.unvoicedTransitionMs;
    target.voicedLevel = source.voicedLevel;
    target.unvoicedLevel = source.unvoicedLevel;
    target.modulatorSource = source.modulatorSource;
    target.micGainDb = source.micGainDb;
    target.externalCarrierMix = source.externalCarrierMix;
    target.externalCarrierGainDb = source.externalCarrierGainDb;
    target.pulseWidth = source.pulseWidth;
    target.carrierLfoShape = source.carrierLfoShape;
    target.carrierLfoRateHz = source.carrierLfoRateHz;
    target.carrierLfoDepthSemitones = source.carrierLfoDepthSemitones;
    target.carrierLfoPwmDepth = source.carrierLfoPwmDepth;
    target.carrierLfoSync = source.carrierLfoSync;
    target.carrierLfoSyncDivisionBeats = source.carrierLfoSyncDivisionBeats;
    target.analysisBlend = source.analysisBlend;
    target.attackMs = source.attackMs;
    target.releaseMs = source.releaseMs;
    target.resonance = source.resonance;
    target.driveDb = source.driveDb;
    target.bandShiftSemitones = source.bandShiftSemitones;
    target.bandStretch = source.bandStretch;
    target.tilt = source.tilt;
    target.sibilance = source.sibilance;
    target.openLevel = source.openLevel;
    target.articulationThru = source.articulationThru;
    target.coupling = source.coupling;
    target.matrixMode = source.matrixMode;
    target.matrixMorph = source.matrixMorph;
    target.customMatrixMorph = source.customMatrixMorph;
    target.stereoMode = source.stereoMode;
    target.stereoSpread = source.stereoSpread;
    target.freeze = source.freeze;
    target.freezeTrigger = source.freezeTrigger;
    target.blurMs = source.blurMs;
    target.gestureFollow = source.gestureFollow;
}

inline AcapellaResonatorParams sanitizeAcapellaResonatorParams(
    AcapellaResonatorParams params)
{
    params.amount = clamp(acapellaFiniteOr(params.amount, 0.90f),
        0.0f, 1.0f);
    params.bandLayout = static_cast<AcapellaResonatorBandLayout>(
        std::min<uint32_t>(static_cast<uint32_t>(params.bandLayout),
            kAcapellaResonatorBandLayoutCount - 1u));
    params.analysisSlope = static_cast<AcapellaResonatorAnalysisSlope>(
        std::min<uint32_t>(static_cast<uint32_t>(params.analysisSlope),
            kAcapellaResonatorAnalysisSlopeCount - 1u));
    params.mode = static_cast<AcapellaResonatorMode>(std::min<uint32_t>(
        static_cast<uint32_t>(params.mode),
        kAcapellaResonatorModeCount - 1u));
    params.carrierShape = static_cast<AcapellaResonatorCarrierShape>(
        std::min<uint32_t>(static_cast<uint32_t>(params.carrierShape),
            kAcapellaResonatorCarrierShapeCount - 1u));
    params.carrierHarmonics = clamp(acapellaFiniteOr(
        params.carrierHarmonics, 0.72f), 0.0f, 1.0f);
    params.carrierColor = clamp(acapellaFiniteOr(
        params.carrierColor, 0.0f), -1.0f, 1.0f);
    params.carrierNoise = clamp(acapellaFiniteOr(
        params.carrierNoise, 0.10f), 0.0f, 1.0f);
    params.carrierPitchSource
        = static_cast<AcapellaResonatorCarrierPitchSource>(
            std::min<uint32_t>(
                static_cast<uint32_t>(params.carrierPitchSource),
                kAcapellaResonatorCarrierPitchSourceCount - 1u));
    params.pitchScaleRoot = std::min<uint32_t>(params.pitchScaleRoot, 11u);
    params.pitchScale = static_cast<AcapellaResonatorPitchScale>(
        std::min<uint32_t>(static_cast<uint32_t>(params.pitchScale),
            kAcapellaResonatorPitchScaleCount - 1u));
    params.pitchHoldMs = clamp(acapellaFiniteOr(
        params.pitchHoldMs, 350.0f), 20.0f,
        kAcapellaResonatorInfinitePitchHoldMs);
    params.voicingMode = static_cast<AcapellaResonatorVoicingMode>(
        std::min<uint32_t>(static_cast<uint32_t>(params.voicingMode),
            kAcapellaResonatorVoicingModeCount - 1u));
    params.voicingThreshold = clamp(acapellaFiniteOr(
        params.voicingThreshold, 0.52f), 0.0f, 1.0f);
    params.voicedTransitionMs = clamp(acapellaFiniteOr(
        params.voicedTransitionMs, 38.0f), 10.0f, 250.0f);
    params.unvoicedTransitionMs = clamp(acapellaFiniteOr(
        params.unvoicedTransitionMs, 16.0f), 10.0f, 250.0f);
    params.voicedLevel = clamp(acapellaFiniteOr(
        params.voicedLevel, 1.0f), 0.0f, 1.0f);
    params.unvoicedLevel = clamp(acapellaFiniteOr(
        params.unvoicedLevel, 0.82f), 0.0f, 1.0f);
    params.modulatorSource = static_cast<AcapellaResonatorModulatorSource>(
        std::min<uint32_t>(static_cast<uint32_t>(params.modulatorSource),
            kAcapellaResonatorModulatorSourceCount - 1u));
    params.micGainDb = clamp(acapellaFiniteOr(
        params.micGainDb, 0.0f), -24.0f, 24.0f);
    params.externalCarrierMix = clamp(acapellaFiniteOr(
        params.externalCarrierMix, 0.55f), 0.0f, 1.0f);
    params.externalCarrierGainDb = clamp(acapellaFiniteOr(
        params.externalCarrierGainDb, 0.0f), -24.0f, 24.0f);
    params.pulseWidth = clamp(acapellaFiniteOr(
        params.pulseWidth, 0.50f), 0.05f, 0.95f);
    params.carrierLfoShape = static_cast<AcapellaResonatorCarrierLfoShape>(
        std::min<uint32_t>(static_cast<uint32_t>(params.carrierLfoShape),
            kAcapellaResonatorCarrierLfoShapeCount - 1u));
    params.carrierLfoRateHz = clamp(acapellaFiniteOr(
        params.carrierLfoRateHz, 0.22f), 0.02f, 13.0f);
    params.carrierLfoDepthSemitones = clamp(acapellaFiniteOr(
        params.carrierLfoDepthSemitones, 0.0f), 0.0f, 24.0f);
    params.carrierLfoPwmDepth = clamp(acapellaFiniteOr(
        params.carrierLfoPwmDepth, 0.0f), 0.0f, 1.0f);
    params.carrierLfoSyncDivisionBeats = clamp(acapellaFiniteOr(
        params.carrierLfoSyncDivisionBeats, 1.0f), 0.0625f, 16.0f);
    params.analysisBlend = clamp(acapellaFiniteOr(
        params.analysisBlend, 0.72f), 0.0f, 1.0f);
    params.attackMs = clamp(acapellaFiniteOr(params.attackMs, 8.0f),
        0.5f, 120.0f);
    params.releaseMs = clamp(acapellaFiniteOr(params.releaseMs, 120.0f),
        5.0f, 1500.0f);
    params.resonance = clamp(acapellaFiniteOr(params.resonance, 0.48f),
        0.0f, 1.0f);
    params.driveDb = clamp(acapellaFiniteOr(params.driveDb, 3.0f),
        0.0f, 24.0f);
    params.bandShiftSemitones = clamp(acapellaFiniteOr(
        params.bandShiftSemitones, 0.0f), -24.0f, 24.0f);
    params.bandStretch = clamp(acapellaFiniteOr(
        params.bandStretch, 0.0f), -1.0f, 1.0f);
    params.tilt = clamp(acapellaFiniteOr(params.tilt, 0.0f), -1.0f, 1.0f);
    params.sibilance = clamp(acapellaFiniteOr(
        params.sibilance, 0.55f), 0.0f, 1.0f);
    params.openLevel = clamp(acapellaFiniteOr(
        params.openLevel, 0.08f), 0.0f, 1.0f);
    params.articulationThru = clamp(acapellaFiniteOr(
        params.articulationThru, 0.0f), 0.0f, 1.0f);
    params.coupling = std::max<int32_t>(-3,
        std::min<int32_t>(3, params.coupling));
    for (float& trim : params.bandTrims) {
        trim = clamp(acapellaFiniteOr(trim, 1.0f), 0.0f, 2.0f);
    }
    params.matrixMode = static_cast<AcapellaResonatorMatrixMode>(
        std::min<uint32_t>(static_cast<uint32_t>(params.matrixMode),
            kAcapellaResonatorMatrixModeCount - 1u));
    params.matrixMorph = clamp(acapellaFiniteOr(
        params.matrixMorph, 0.0f), 0.0f, 1.0f);
    for (float& route : params.customMatrixA) {
        route = clamp(acapellaFiniteOr(route, 0.0f), -1.0f, 1.0f);
    }
    for (float& route : params.customMatrixB) {
        route = clamp(acapellaFiniteOr(route, 0.0f), -1.0f, 1.0f);
    }
    params.customMatrixMorph = clamp(acapellaFiniteOr(
        params.customMatrixMorph, 0.0f), 0.0f, 1.0f);
    params.stereoMode = static_cast<AcapellaResonatorStereoMode>(
        std::min<uint32_t>(static_cast<uint32_t>(params.stereoMode),
            kAcapellaResonatorStereoModeCount - 1u));
    params.stereoSpread = clamp(acapellaFiniteOr(
        params.stereoSpread, 0.25f), 0.0f, 1.0f);
    params.freeze = clamp(acapellaFiniteOr(params.freeze, 0.0f), 0.0f, 1.0f);
    params.freezeTrigger = static_cast<AcapellaResonatorFreezeTrigger>(
        std::min<uint32_t>(static_cast<uint32_t>(params.freezeTrigger),
            kAcapellaResonatorFreezeTriggerCount - 1u));
    params.blurMs = clamp(acapellaFiniteOr(params.blurMs, 70.0f),
        0.0f, 2000.0f);
    params.gestureFollow = clamp(acapellaFiniteOr(
        params.gestureFollow, 0.82f), 0.0f, 1.0f);
    return params;
}

struct AcapellaResonatorGesture {
    AcapellaPhoneme phoneme = AcapellaPhoneme::AX;
    float frequencyHz = 146.83f;
    uint32_t stepIndex = 0u;
    uint8_t stress = 0u;
    uint8_t flags = 0u;
    bool active = false;
    // Carrier-only gestures retain held MIDI oscillators without importing
    // the optional text engine's phoneme targets or rest semantics.
    bool carrierOnly = false;
    float stepProgress = 0.0f;
    uint64_t voiceInstance = 0u;

    // A fixed-capacity MIDI carrier list. Oscillator phases are associated
    // with voiceInstanceIds, not array indices, so voice compaction cannot
    // interrupt a surviving note.
    uint32_t voiceCount = 0u;
    std::array<float, kAcapellaResonatorMaxVoices> voiceFrequencyHz {};
    std::array<float, kAcapellaResonatorMaxVoices> voiceGain {};
    std::array<uint64_t, kAcapellaResonatorMaxVoices> voiceInstanceIds {};
};

struct AcapellaResonatorStereoFrame {
    float left = 0.0f;
    float right = 0.0f;
    float dryLeft = 0.0f;
    float dryRight = 0.0f;
};

struct AcapellaResonatorMeterSnapshot {
    uint32_t activeBands = kAcapellaResonatorBands;
    std::array<float, kAcapellaResonatorBands> analysis {};
    std::array<float, kAcapellaResonatorBands> synthesis {};
    float unvoiced = 0.0f;
    float detectedPitchHz = 0.0f;
    float pitchConfidence = 0.0f;
    bool pitchActive = false;
};

// Meter snapshots intentionally expose linear DSP amplitudes. Convert them
// to a useful display range here so every consumer uses the same calibrated
// -72 dBFS floor instead of drawing small-but-valid band levels as sub-pixel
// bars. This is telemetry-only and is never used in the audio path.
inline float acapellaResonatorMeterDisplayLevel(float linearAmplitude)
{
    if (!std::isfinite(linearAmplitude) || linearAmplitude <= 0.0f) {
        return 0.0f;
    }
    constexpr float floorDb = -72.0f;
    const float levelDb = 20.0f * std::log10(
        std::max(linearAmplitude, 1.0e-12f));
    return clamp((levelDb - floorDb) / -floorDb, 0.0f, 1.0f);
}

namespace acapella_resonator_detail {

inline float onePoleMilliseconds(float milliseconds, float sampleRate)
{
    if (!(milliseconds > 0.0f)) return 1.0f;
    return 1.0f - std::exp(-1.0f /
        std::max(1.0f, milliseconds * 0.001f * sampleRate));
}

inline float envelopeMilliseconds(float milliseconds, float sampleRate)
{
    if (!(milliseconds > 0.0f)) return 1.0f;
    // Reach -60 dB of the remaining distance in the labelled time. This is
    // used for the mic-presence gate, where a conventional one-pole time
    // constant would leave a held carrier faintly audible for many times the
    // displayed bank release.
    return 1.0f - std::exp(-kAcapellaEnvelopeSixtyDb /
        std::max(1.0f, milliseconds * 0.001f * sampleRate));
}

struct TptBandpass {
    float integrator1 = 0.0f;
    float integrator2 = 0.0f;
    float a1 = 1.0f;
    float a2 = 0.0f;
    float a3 = 0.0f;
    float damping = 1.0f;
    float bandNormalization = 1.0f;
    float targetA1 = 1.0f;
    float targetA2 = 0.0f;
    float targetA3 = 0.0f;
    float targetDamping = 1.0f;
    float targetBandNormalization = 1.0f;
    float coefficientSlew = 1.0f;
    bool configured = false;

    void reset()
    {
        integrator1 = 0.0f;
        integrator2 = 0.0f;
    }

    void configure(float frequencyHz, float q, float sampleRate,
        bool immediate = false)
    {
        const float nyquistBound = std::max(24.0f, sampleRate * 0.43f);
        const float frequency = clamp(frequencyHz, 18.0f, nyquistBound);
        const float boundedQ = clamp(q, 0.45f, 10.0f);
        const float g = std::tan(kPi * frequency / sampleRate);
        targetDamping = 1.0f / boundedQ;
        targetA1 = 1.0f / (1.0f + g * (g + targetDamping));
        targetA2 = g * targetA1;
        targetA3 = g * targetA2;
        // This retains an audible resonance increase without exposing the
        // raw Q-fold peak gain of an unnormalised bandpass.
        targetBandNormalization = std::sqrt(targetDamping);
        coefficientSlew = onePoleMilliseconds(1.5f, sampleRate);
        if (!configured || immediate) snapCoefficients();
        configured = true;
    }

    void snapCoefficients()
    {
        a1 = targetA1;
        a2 = targetA2;
        a3 = targetA3;
        damping = targetDamping;
        bandNormalization = targetBandNormalization;
    }

    enum class Response : uint32_t { Lowpass, Bandpass, Highpass };

    float process(float input, Response response = Response::Bandpass)
    {
        // The control target may update every 16 samples, but the actual TPT
        // coefficients move every sample. This retains the integrator state
        // without hard coefficient jumps during Shift/Stretch/Q automation.
        a1 += (targetA1 - a1) * coefficientSlew;
        a2 += (targetA2 - a2) * coefficientSlew;
        a3 += (targetA3 - a3) * coefficientSlew;
        damping += (targetDamping - damping) * coefficientSlew;
        bandNormalization += (targetBandNormalization - bandNormalization)
            * coefficientSlew;
        if (!std::isfinite(input)
            || !std::isfinite(integrator1)
            || !std::isfinite(integrator2)) {
            reset();
            return 0.0f;
        }
        const float v3 = input - integrator2;
        const float v1 = a1 * integrator1 + a2 * v3;
        const float v2 = integrator2 + a2 * integrator1 + a3 * v3;
        integrator1 = flushDenormal(2.0f * v1 - integrator1);
        integrator2 = flushDenormal(2.0f * v2 - integrator2);
        float output = v1 * bandNormalization;
        if (response == Response::Lowpass) {
            output = v2;
        } else if (response == Response::Highpass) {
            output = v3 - damping * v1;
        }
        if (!std::isfinite(output)
            || std::abs(integrator1) > 64.0f
            || std::abs(integrator2) > 64.0f) {
            reset();
            return 0.0f;
        }
        return flushDenormal(output);
    }
};

inline float polyBlep(float phase, float increment)
{
    increment = clamp(increment, 1.0e-6f, 0.49f);
    if (phase < increment) {
        const float x = phase / increment;
        return x + x - x * x - 1.0f;
    }
    if (phase > 1.0f - increment) {
        const float x = (phase - 1.0f) / increment;
        return x * x + x + x + 1.0f;
    }
    return 0.0f;
}

inline float shapedOscillator(AcapellaResonatorCarrierShape shape,
    float phase, float increment, float harmonics, float color,
    float pulseWidth)
{
    const float sine = std::sin(2.0f * kPi * phase);
    float rich = sine;
    switch (shape) {
    case AcapellaResonatorCarrierShape::Saw:
        rich = 2.0f * phase - 1.0f - polyBlep(phase, increment);
        break;
    case AcapellaResonatorCarrierShape::Pulse: {
        const float width = clamp(pulseWidth + color * 0.10f, 0.05f, 0.95f);
        float pulse = phase < width ? 1.0f : -1.0f;
        pulse += polyBlep(phase, increment);
        float shifted = phase - width;
        if (shifted < 0.0f) shifted += 1.0f;
        pulse -= polyBlep(shifted, increment);
        rich = pulse * 0.72f;
        break;
    }
    case AcapellaResonatorCarrierShape::Fold: {
        const float driven = sine * (2.1f + 2.4f * harmonics);
        rich = 2.0f * std::abs(0.5f * driven
            - std::floor(0.5f * driven + 0.5f)) - 0.5f;
        rich *= 1.35f;
        break;
    }
    case AcapellaResonatorCarrierShape::Noise:
        // The caller substitutes deterministic noise after the pitched
        // oscillator path. Keep a small pitched trace for note definition.
        rich = sine * 0.18f;
        break;
    case AcapellaResonatorCarrierShape::Glottal:
    default: {
        const float second = std::sin(4.0f * kPi * phase + 0.32f);
        const float third = std::sin(6.0f * kPi * phase + 0.15f);
        rich = sine * 0.68f + second * 0.25f + third * 0.11f;
        rich = std::tanh(rich * (1.35f + 0.55f * harmonics));
        break;
    }
    }
    return lerp(sine, rich, harmonics);
}

inline float bell(float frequency, float center, float width)
{
    const float distance = (frequency - center) / std::max(20.0f, width);
    const float square = distance * distance;
    return 1.0f / (1.0f + square * square);
}

inline float phonemeSibilance(AcapellaPhoneme phoneme)
{
    switch (phoneme) {
    case AcapellaPhoneme::S:
    case AcapellaPhoneme::Z: return 1.0f;
    case AcapellaPhoneme::SH:
    case AcapellaPhoneme::ZH:
    case AcapellaPhoneme::CH:
    case AcapellaPhoneme::JH: return 0.86f;
    case AcapellaPhoneme::F:
    case AcapellaPhoneme::V:
    case AcapellaPhoneme::TH:
    case AcapellaPhoneme::DH:
    case AcapellaPhoneme::HH: return 0.64f;
    case AcapellaPhoneme::T:
    case AcapellaPhoneme::D:
    case AcapellaPhoneme::K:
    case AcapellaPhoneme::G:
    case AcapellaPhoneme::P:
    case AcapellaPhoneme::B: return 0.46f;
    default: return 0.0f;
    }
}

} // namespace acapella_resonator_detail

// A real-time, sample-free channel vocoder and resonant filter bank. The
// A selectable mic/speech source is the modulator; a stable-ID oscillator set
// excites up to twenty-two synthesis filters. The separate external-carrier
// overload remains available only for standalone compatibility clients.
// Phoneme targets are mixed in the envelope domain, where freeze and matrix
// gestures remain bounded by construction.
class AcapellaResonatorBank {
public:
    bool prepare(double sampleRate)
    {
        sampleRate_ = std::isfinite(sampleRate)
            ? clamp(static_cast<float>(sampleRate), 8000.0f, 192000.0f)
            : 48000.0f;
        params_ = sanitizeAcapellaResonatorParams(params_);
        configureLayout(true);
        parameterCoefficient_ =
            acapella_resonator_detail::onePoleMilliseconds(14.0f, sampleRate_);
        carrierAttackCoefficient_ =
            acapella_resonator_detail::onePoleMilliseconds(4.0f, sampleRate_);
        // Host activity follows audible output, not the longest internal
        // oscillator coefficient. A short release keeps the plug-in awake
        // through the last resonator samples while remaining inside the
        // conservative tailSamples() bound.
        activityReleaseCoefficient_ =
            acapella_resonator_detail::onePoleMilliseconds(32.0f, sampleRate_);
        // Keep the final pole above the highest useful consonant bands.  It
        // is only a dezipper/output guard, not part of the bank voicing, so
        // tying it to 0.11 * sample rate would erase ordinary-rate sibilants
        // (5.28 kHz at 48 kHz) and make the instrument change character with
        // sample rate.
        const float wetCutoff = std::min(9500.0f, sampleRate_ * 0.43f);
        wetOutputCoefficient_ = 1.0f - std::exp(
            -2.0f * kPi * wetCutoff / sampleRate_);
        analysisLevelAttackCoefficient_ =
            acapella_resonator_detail::onePoleMilliseconds(2.5f, sampleRate_);
        modulatorDetectorAttackCoefficient_ =
            acapella_resonator_detail::onePoleMilliseconds(2.0f, sampleRate_);
        modulatorDetectorReleaseCoefficient_ =
            acapella_resonator_detail::onePoleMilliseconds(32.0f, sampleRate_);
        modulatorGateAttackCoefficient_ =
            acapella_resonator_detail::envelopeMilliseconds(5.0f, sampleRate_);
        carrierFrequencyCoefficient_ =
            acapella_resonator_detail::onePoleMilliseconds(7.0f, sampleRate_);
        noiseLowCoefficient_ =
            acapella_resonator_detail::onePoleMilliseconds(0.09f, sampleRate_);
        sibilanceAttackCoefficient_ =
            acapella_resonator_detail::onePoleMilliseconds(5.0f, sampleRate_);
        sibilanceReleaseCoefficient_ =
            acapella_resonator_detail::onePoleMilliseconds(36.0f, sampleRate_);
        carrierLfoEdgeCoefficient_ =
            acapella_resonator_detail::onePoleMilliseconds(2.0f, sampleRate_);
        routeSmoothingCoefficient_ =
            acapella_resonator_detail::onePoleMilliseconds(4.0f, sampleRate_);
        layoutSmoothingCoefficient_ =
            acapella_resonator_detail::onePoleMilliseconds(8.0f, sampleRate_);
        articulationHighpassCoefficient_ = 1.0f - std::exp(
            -2.0f * kPi * std::min(2200.0f, sampleRate_ * 0.38f)
                / sampleRate_);
        pitchTrackerDecimation_ = std::max<uint32_t>(1u,
            static_cast<uint32_t>(std::lround(sampleRate_ / 12000.0f)));
        pitchTrackerRate_ = sampleRate_
            / static_cast<float>(pitchTrackerDecimation_);
        pitchTrackerHop_ = std::max<uint32_t>(32u,
            static_cast<uint32_t>(pitchTrackerRate_ * 0.010f));
        pitchTrackerDcCoefficient_ = 1.0f - std::exp(
            -2.0f * kPi * 32.0f / sampleRate_);
        pitchTrackerLowCoefficient_ = 1.0f - std::exp(
            -2.0f * kPi * std::min(2600.0f, sampleRate_ * 0.20f)
                / sampleRate_);
        pitchTrackerChannelCoefficient_ =
            acapella_resonator_detail::onePoleMilliseconds(18.0f, sampleRate_);
        prepared_ = true;
        reset();
        return true;
    }

    void reset()
    {
        smoothed_ = params_;
        for (auto& filter : analysisLeft_) filter.reset();
        for (auto& filter : analysisRight_) filter.reset();
        for (auto& filter : analysisLeftSecond_) filter.reset();
        for (auto& filter : analysisRightSecond_) filter.reset();
        for (auto& filter : analysisLeftThird_) filter.reset();
        for (auto& filter : analysisRightThird_) filter.reset();
        for (auto& filter : analysisLeftFourth_) filter.reset();
        for (auto& filter : analysisRightFourth_) filter.reset();
        for (auto& filter : synthesisLeft_) filter.reset();
        for (auto& filter : synthesisRight_) filter.reset();
        analysisEnvelope_.fill(0.0f);
        liveEnvelope_.fill(0.0f);
        blurredEnvelope_.fill(0.0f);
        frozenEnvelope_.fill(0.0f);
        captureFromEnvelope_.fill(0.0f);
        mappedEnvelope_.fill(0.0f);
        mappedTarget_.fill(0.0f);
        coupledEnvelope_.fill(0.0f);
        analysisMeters_.fill(0.0f);
        synthesisMeters_.fill(0.0f);
        mappedTarget_.fill(0.0f);
        analysisMeters_.fill(0.0f);
        synthesisMeters_.fill(0.0f);
        for (uint32_t band = 0u; band < kAcapellaResonatorBands; ++band) {
            activeBandGain_[band] = band < activeBandCount() ? 1.0f : 0.0f;
        }
        phonemeTarget_.fill(0.0f);
        smoothedPhonemeTarget_.fill(0.0f);
        carrierVoices_ = {};
        gesture_ = {};
        lastGestureValid_ = false;
        lastNoteIdentity_ = 0u;
        frozenValid_ = false;
        frozenBlend_ = 0.0f;
        continuousCaptureArmed_ = params_.freeze > 1.0e-4f
            && params_.freezeTrigger
                == AcapellaResonatorFreezeTrigger::Continuous;
        pendingCaptureSamples_ = 0u;
        controlCounter_ = 0u;
        noiseStateLeft_ = 0x6d2b79f5u;
        noiseStateRight_ = 0x91e10da5u;
        carrierLowLeft_ = 0.0f;
        carrierLowRight_ = 0.0f;
        noiseLowLeft_ = 0.0f;
        noiseLowRight_ = 0.0f;
        sibilanceEnvelope_ = 0.0f;
        analysisLevel_ = 0.0f;
        modulatorDetector_ = 0.0f;
        modulatorGateTarget_ = 0.0f;
        modulatorGateGain_ = 0.0f;
        modulatorGateOpen_ = false;
        analysisSlopeMix_ = params_.analysisSlope
                == AcapellaResonatorAnalysisSlope::EightPole
            ? 1.0f : 0.0f;
        pitchTrackerBuffer_.fill(0.0f);
        pitchDifference_.fill(1.0f);
        pitchTrackerWrite_ = 0u;
        pitchTrackerCount_ = 0u;
        pitchTrackerDecimationCounter_ = 0u;
        pitchTrackerHopCounter_ = 0u;
        pitchWorkLag_ = 0u;
        pitchWorkMinimumLag_ = 0u;
        pitchWorkMaximumLag_ = 0u;
        pitchTrackerDc_ = 0.0f;
        pitchTrackerLow_ = 0.0f;
        pitchTrackerLeftLevel_ = 0.0f;
        pitchTrackerRightLevel_ = 0.0f;
        pitchTrackerUseRight_ = false;
        detectedPitchHz_ = 146.832f;
        detectedPitchConfidence_ = 0.0f;
        pitchHoldSamplesRemaining_ = 0u;
        pitchHoldLatched_ = false;
        pitchCarrierActive_ = false;
        externalMicModeGain_ = params_.modulatorSource
                == AcapellaResonatorModulatorSource::ExternalMic
            ? 1.0f : 0.0f;
        wetOutputLeft_ = 0.0f;
        wetOutputRight_ = 0.0f;
        previousWetLeft_ = 0.0f;
        previousWetRight_ = 0.0f;
        previousExternalOutputLeft_ = 0.0f;
        previousExternalOutputRight_ = 0.0f;
        articulationLowLeft_ = 0.0f;
        articulationLowRight_ = 0.0f;
        articulationLowLeft_ = 0.0f;
        articulationLowRight_ = 0.0f;
        voicingNoiseMix_ = 0.0f;
        carrierLfoPhase_ = 0.0f;
        carrierLfoValue_ = 0.0f;
        internalCarrierActivity_ = 0.0f;
        activityEnvelope_ = 0.0f;
        updatePhonemeTarget(AcapellaPhoneme::AX);
        updateSynthesisCoefficients();
        for (auto& filter : synthesisLeft_) filter.snapCoefficients();
        for (auto& filter : synthesisRight_) filter.snapCoefficients();
        updateRuntimeCoefficients();
    }

    void setParams(AcapellaResonatorParams params)
    {
        const auto next = sanitizeAcapellaResonatorParams(params);
        if (next.bandLayout != params_.bandLayout) {
            pendingLayout_ = next.bandLayout;
            layoutChangePending_ = true;
        }
        const bool wasFrozen = params_.freeze > 1.0e-4f;
        const bool willFreeze = next.freeze > 1.0e-4f;
        const bool triggerChanged =
            next.freezeTrigger != params_.freezeTrigger;
        if (!willFreeze) {
            pendingCaptureSamples_ = 0u;
            continuousCaptureArmed_ = false;
        } else if (!wasFrozen || triggerChanged) {
            // Never reveal an envelope left over from an earlier freeze
            // gesture. Boundary modes remain live until their next event;
            // Continuous captures once after its control is engaged.
            pendingCaptureSamples_ = 0u;
            continuousCaptureArmed_ = next.freezeTrigger
                == AcapellaResonatorFreezeTrigger::Continuous;
        }
        params_ = next;
        if (!active()) {
            smoothed_.amount = params_.amount;
            // With no sounding carrier/tail there is nothing to crossfade.
            // Snap the direct articulation path so a restored value of zero
            // cannot leak a short burst of microphone audio at activation.
            smoothed_.articulationThru = params_.articulationThru;
        }
    }

    void setRealtimeControlParams(const AcapellaResonatorParams& next)
    {
        if (next.bandLayout != params_.bandLayout) {
            pendingLayout_ = next.bandLayout;
            layoutChangePending_ = true;
        }
        const bool wasFrozen = params_.freeze > 1.0e-4f;
        const bool willFreeze = next.freeze > 1.0e-4f;
        const bool triggerChanged =
            next.freezeTrigger != params_.freezeTrigger;
        if (!willFreeze) {
            pendingCaptureSamples_ = 0u;
            continuousCaptureArmed_ = false;
        } else if (!wasFrozen || triggerChanged) {
            pendingCaptureSamples_ = 0u;
            continuousCaptureArmed_ = next.freezeTrigger
                == AcapellaResonatorFreezeTrigger::Continuous;
        }
        copyAcapellaResonatorControlParams(params_, next);
        if (!active()) {
            smoothed_.amount = params_.amount;
            smoothed_.articulationThru = params_.articulationThru;
        }
    }

    const AcapellaResonatorParams& params() const { return params_; }

    void setTempo(double beatsPerMinute, bool valid = true)
    {
        tempoValid_ = valid && std::isfinite(beatsPerMinute)
            && beatsPerMinute >= 20.0 && beatsPerMinute <= 400.0;
        if (tempoValid_) tempoBpm_ = static_cast<float>(beatsPerMinute);
    }

    void setBandTrim(uint32_t band, float value)
    {
        if (band >= kAcapellaResonatorBands) return;
        params_.bandTrims[band] = clamp(acapellaFiniteOr(value, 1.0f),
            0.0f, 2.0f);
    }

    void setCustomMatrixCell(bool sceneB, uint32_t destination,
        uint32_t source, float value)
    {
        if (destination >= kAcapellaResonatorBands
            || source >= kAcapellaResonatorBands) return;
        auto& scene = sceneB ? params_.customMatrixB : params_.customMatrixA;
        scene[destination * kAcapellaResonatorBands + source] =
            clamp(acapellaFiniteOr(value, 0.0f), -1.0f, 1.0f);
    }

    uint32_t activeBandCount() const
    {
        return params_.bandLayout == AcapellaResonatorBandLayout::Wide16
            ? kAcapellaResonatorWideBands : kAcapellaResonatorBands;
    }

    float bandFrequencyHz(uint32_t band) const
    {
        return band < kAcapellaResonatorBands ? centerFrequencies_[band]
                                              : 0.0f;
    }

    AcapellaResonatorMeterSnapshot meterSnapshot() const
    {
        AcapellaResonatorMeterSnapshot snapshot;
        snapshot.activeBands = activeBandCount();
        snapshot.analysis = analysisMeters_;
        snapshot.synthesis = synthesisMeters_;
        snapshot.unvoiced = voicingNoiseMix_;
        snapshot.detectedPitchHz = detectedPitchHz_;
        snapshot.pitchConfidence = detectedPitchConfidence_;
        snapshot.pitchActive = pitchCarrierActive_;
        return snapshot;
    }

    void setGesture(AcapellaResonatorGesture gesture)
    {
        if (static_cast<uint32_t>(gesture.phoneme)
            >= kAcapellaPhonemeCount) {
            gesture.phoneme = AcapellaPhoneme::AX;
        }
        gesture.frequencyHz = clamp(acapellaFiniteOr(
            gesture.frequencyHz, 146.83f), 16.0f, 20000.0f);
        gesture.stepProgress = clamp(acapellaFiniteOr(
            gesture.stepProgress, 0.0f), 0.0f, 1.0f);
        gesture.voiceCount = std::min<uint32_t>(
            gesture.voiceCount, kAcapellaResonatorMaxVoices);
        for (uint32_t voice = 0u;
             voice < kAcapellaResonatorMaxVoices; ++voice) {
            gesture.voiceFrequencyHz[voice] = clamp(acapellaFiniteOr(
                gesture.voiceFrequencyHz[voice], gesture.frequencyHz),
                16.0f, 20000.0f);
            gesture.voiceGain[voice] = clamp(acapellaFiniteOr(
                gesture.voiceGain[voice], 0.0f), 0.0f, 2.0f);
        }

        const uint64_t incomingNoteIdentity = gesture.voiceInstance != 0u
            ? gesture.voiceInstance
            : (gesture.voiceCount > 0u ? gesture.voiceInstanceIds[0u] : 0u);
        const bool noteEvent = incomingNoteIdentity != 0u
            && incomingNoteIdentity != lastNoteIdentity_;
        if (incomingNoteIdentity != 0u) {
            lastNoteIdentity_ = incomingNoteIdentity;
        }
        const bool phonemeEvent = noteEvent
            || gesture.stepIndex != gesture_.stepIndex
            || gesture.phoneme != gesture_.phoneme;
        const bool syllableEvent = phonemeEvent
            && (gesture.flags & kAcapellaSyllableStart) != 0u;
        const bool wordEvent = (!lastGestureValid_ || phonemeEvent)
            && (gesture.flags & kAcapellaWordStart) != 0u;
        const bool previousArticulation = lastGestureValid_
            && gesture_.active
            && (gesture_.carrierOnly
                || (gesture_.phoneme != AcapellaPhoneme::Silence
                    && (gesture_.flags & kAcapellaForcedRest) == 0u));
        const bool currentRest = !gesture.active
            || (!gesture.carrierOnly
                && (gesture.phoneme == AcapellaPhoneme::Silence
                    || (gesture.flags & kAcapellaForcedRest) != 0u));
        const bool restEvent = previousArticulation && currentRest;

        bool capture = false;
        switch (params_.freezeTrigger) {
        case AcapellaResonatorFreezeTrigger::Continuous:
            capture = continuousCaptureArmed_ || !frozenValid_;
            break;
        case AcapellaResonatorFreezeTrigger::Note:
            capture = noteEvent;
            break;
        case AcapellaResonatorFreezeTrigger::Phoneme:
            capture = phonemeEvent;
            break;
        case AcapellaResonatorFreezeTrigger::Syllable:
            capture = syllableEvent;
            break;
        case AcapellaResonatorFreezeTrigger::Word:
            capture = wordEvent;
            break;
        case AcapellaResonatorFreezeTrigger::Rest:
            capture = restEvent;
            break;
        }
        const bool freezeEngaged = params_.freeze > 1.0e-4f;
        if (capture && freezeEngaged) {
            if (params_.freezeTrigger
                    == AcapellaResonatorFreezeTrigger::Rest
                && restEvent) {
                // A rest edge must preserve the last articulated vector,
                // not an already-decayed silent frame.
                pendingCaptureSamples_ = 0u;
                captureEnvelope();
            } else if (gesture.active && pendingCaptureSamples_ == 0u) {
                // Give onsets enough energy to analyze, but do not restart
                // this countdown when setGesture() is called every sample.
                pendingCaptureSamples_ = std::max<uint32_t>(16u,
                    static_cast<uint32_t>(sampleRate_ * 0.006f));
            }
        }
        if (gesture.phoneme != gesture_.phoneme || !lastGestureValid_) {
            updatePhonemeTarget(gesture.phoneme);
        }
        gesture_ = gesture;
        lastGestureValid_ = true;
    }

    uint32_t latencySamples() const { return 0u; }

    uint32_t tailSamples() const
    {
        // These followers use conventional one-pole time constants, while
        // active() and the carrier cleanup threshold are near -100 dB. The
        // host tail must therefore cover multiple constants, including the
        // serial Blur stage and the worst frozen-envelope release. The fixed
        // allowance covers low shifted/high-Q resonator ringing and the
        // output activity detector. This is deliberately conservative at
        // extreme settings; process() may still return SLEEP earlier.
        const float liveReleaseMs = params_.releaseMs * 14.8f
            + params_.blurMs * 11.6f;
        const float frozenReleaseMs = params_.releaseMs
            * (1.0f + 0.45f * params_.freeze) * 11.6f;
        const float milliseconds = std::max(
            liveReleaseMs, frozenReleaseMs)
            + 2100.0f + params_.resonance * 900.0f;
        return static_cast<uint32_t>(std::min<double>(0xfffffffeu,
            std::ceil(milliseconds * 0.001 * sampleRate_)));
    }

    bool active() const
    {
        // Bank Mix zero still exposes the selected carrier. Activity therefore
        // follows the audible output rail rather than treating Amount as a
        // bypass switch.
        return activityEnvelope_ > 1.0e-5f;
    }

    AcapellaResonatorStereoFrame processFrameStereo(
        float analysisLeft, float analysisRight,
        float externalCarrierLeft, float externalCarrierRight,
        bool externalCarrierAvailable)
    {
        analysisLeft = finiteInput(analysisLeft);
        analysisRight = finiteInput(analysisRight);
        externalCarrierLeft = finiteInput(externalCarrierLeft);
        externalCarrierRight = finiteInput(externalCarrierRight);
        if (!prepared_) {
            const float dryLeft = externalCarrierAvailable
                ? externalCarrierLeft : analysisLeft;
            const float dryRight = externalCarrierAvailable
                ? externalCarrierRight : analysisRight;
            return { dryLeft, dryRight, dryLeft, dryRight };
        }

        smoothParams();
        if ((controlCounter_++ & 15u) == 0u) {
            if (layoutChangePending_) {
                configureLayout(false);
                layoutChangePending_ = false;
            }
            updateSynthesisCoefficients();
            updateRuntimeCoefficients();
        }
        const float detectorInput = std::max(
            std::abs(analysisLeft), std::abs(analysisRight));
        const float modulatorDetectorCoefficient
            = detectorInput > modulatorDetector_
            ? modulatorDetectorAttackCoefficient_
            : modulatorDetectorReleaseCoefficient_;
        modulatorDetector_ += (detectorInput - modulatorDetector_)
            * modulatorDetectorCoefficient;
        // Treat the external input as an articulation source, never as a
        // free-running carrier enable. Hysteresis rejects ordinary interface
        // and room noise around -48 dBFS, while Mic Gain lets the user place
        // quiet speech above the -38 dBFS opening threshold. Once open, the
        // detector stays open through consonants down to about -48 dBFS.
        constexpr float openThreshold = 0.0120f;
        constexpr float closeThreshold = 0.0040f;
        // Once the periodicity tracker has confidently acquired a sung note,
        // let a genuinely periodic but quiet vowel keep the articulation gate
        // alive below the ordinary room-noise threshold. Confidence is
        // refreshed only by new tracker results and decays quickly on silence,
        // so this is not another infinite audio gate: Infinite Pitch Hold
        // remembers frequency, while the detector still owns audibility.
        const bool quietTrackedVoice = smoothed_.carrierPitchSource
                == AcapellaResonatorCarrierPitchSource::Voice
            && detectedPitchConfidence_ >= 0.60f;
        constexpr float trackedVoiceCloseThreshold = 0.00050f;
        const float effectiveCloseThreshold = quietTrackedVoice
            ? trackedVoiceCloseThreshold : closeThreshold;
        if (modulatorGateOpen_) {
            if (modulatorDetector_ < effectiveCloseThreshold) {
                modulatorGateOpen_ = false;
            }
        } else if (modulatorDetector_ > openThreshold) {
            modulatorGateOpen_ = true;
        }
        modulatorGateTarget_ = modulatorGateOpen_ ? 1.0f : 0.0f;
        const float modulatorGateCoefficient
            = modulatorGateTarget_ > modulatorGateGain_
            ? modulatorGateAttackCoefficient_
            : modulatorGateReleaseCoefficient_;
        modulatorGateGain_ += (modulatorGateTarget_ - modulatorGateGain_)
            * modulatorGateCoefficient;
        if (modulatorGateTarget_ <= 0.0f
            && modulatorGateGain_ < 1.0e-7f) {
            modulatorGateGain_ = 0.0f;
        }
        const float externalMicModeTarget = smoothed_.modulatorSource
                == AcapellaResonatorModulatorSource::ExternalMic
            ? 1.0f : 0.0f;
        externalMicModeGain_ += (externalMicModeTarget
            - externalMicModeGain_) * parameterCoefficient_;
        const float levelCoefficient = detectorInput > analysisLevel_
            ? analysisLevelAttackCoefficient_
            : analysisLevelReleaseCoefficient_;
        analysisLevel_ += (detectorInput - analysisLevel_) * levelCoefficient;
        const float analysisSlopeTarget = smoothed_.analysisSlope
                == AcapellaResonatorAnalysisSlope::EightPole
            ? 1.0f : 0.0f;
        analysisSlopeMix_ += (analysisSlopeTarget - analysisSlopeMix_)
            * parameterCoefficient_;
        pitchTrackerLeftLevel_ += (std::abs(analysisLeft)
            - pitchTrackerLeftLevel_) * pitchTrackerChannelCoefficient_;
        pitchTrackerRightLevel_ += (std::abs(analysisRight)
            - pitchTrackerRightLevel_) * pitchTrackerChannelCoefficient_;
        if (pitchTrackerUseRight_) {
            if (pitchTrackerLeftLevel_ > pitchTrackerRightLevel_ * 1.20f) {
                pitchTrackerUseRight_ = false;
            }
        } else if (pitchTrackerRightLevel_
            > pitchTrackerLeftLevel_ * 1.20f) {
            pitchTrackerUseRight_ = true;
        }
        // Analyze one stable, strongest channel rather than L+R. A stereo
        // vocal with opposite polarity or short inter-channel delay can have
        // frequency-specific nulls in its mid sum even though neither mic
        // channel nor the vocoder analysis bank has lost that note.
        updatePitchTracker(pitchTrackerUseRight_
            ? analysisRight : analysisLeft);
        syncCarrierVoices();

        const uint32_t activeBands = activeBandCount();
        for (uint32_t band = 0u; band < kAcapellaResonatorBands; ++band) {
            const float activeTarget = band < activeBands ? 1.0f : 0.0f;
            activeBandGain_[band] += (activeTarget - activeBandGain_[band])
                * layoutSmoothingCoefficient_;
            const auto response = responseForBand(band);
            // A classic channel vocoder relies on steep analysis bands so
            // neighbouring vowel formants do not collapse into the same
            // broad loudness contour. Two matched TPT stages produce a
            // click-safe four-pole analysis response while retaining the
            // continuously slewed coefficients used by the original bank.
            const float leftFirst = analysisLeft_[band].process(
                analysisLeft, response);
            const float rightFirst = analysisRight_[band].process(
                analysisRight, response);
            const float leftSecond = analysisLeftSecond_[band].process(
                leftFirst, response);
            const float rightSecond = analysisRightSecond_[band].process(
                rightFirst, response);
            const float leftThird = analysisLeftThird_[band].process(
                leftSecond, response);
            const float rightThird = analysisRightThird_[band].process(
                rightSecond, response);
            const float leftFourth = analysisLeftFourth_[band].process(
                leftThird, response);
            const float rightFourth = analysisRightFourth_[band].process(
                rightThird, response);
            const float left = lerp(
                leftSecond, leftFourth, analysisSlopeMix_);
            const float right = lerp(
                rightSecond, rightFourth, analysisSlopeMix_);
            const float position = static_cast<float>(band)
                / static_cast<float>(kAcapellaResonatorBands - 1u);
            // Speech loses energy rapidly above the first few formants.
            // Analog vocoders compensate that tilt before envelope
            // detection; this band-domain pre-emphasis makes fricatives and
            // upper formants drive the carrier without passing dry mic audio.
            const float preEmphasis = lerp(
                1.0f, 2.35f, position * position);
            // The additional resonator pair contributes its own centre gain.
            // Compensate while morphing so 8 Pole is narrower, not merely a
            // louder envelope detector.
            const float slopeGain = lerp(
                1.0f, 1.0f / 2.70f, analysisSlopeMix_);
            const float detector = preEmphasis * slopeGain
                * std::sqrt(std::max(0.0f,
                    0.5f * (left * left + right * right)));
            const float coefficient = detector > analysisEnvelope_[band]
                ? bandAttackCoefficients_[band]
                : bandReleaseCoefficients_[band];
            analysisEnvelope_[band] +=
                (detector - analysisEnvelope_[band]) * coefficient;
            analysisEnvelope_[band] *= activeBandGain_[band];
            analysisMeters_[band] += (analysisEnvelope_[band]
                - analysisMeters_[band]) * meterCoefficient_;
        }

        const float phonemeTargetCoefficient = gesture_.active
            ? phonemeTargetAttackCoefficient_
            : phonemeTargetReleaseCoefficient_;
        for (uint32_t band = 0u; band < kAcapellaResonatorBands; ++band) {
            smoothedPhonemeTarget_[band] +=
                (phonemeTarget_[band] - smoothedPhonemeTarget_[band])
                    * phonemeTargetCoefficient;
        }
        buildLiveEnvelope();
        for (uint32_t band = 0u; band < kAcapellaResonatorBands; ++band) {
            blurredEnvelope_[band] +=
                (liveEnvelope_[band] - blurredEnvelope_[band])
                    * blurCoefficient_;
        }

        if (pendingCaptureSamples_ > 0u) {
            --pendingCaptureSamples_;
            if (pendingCaptureSamples_ == 0u) captureEnvelope();
        }
        frozenBlend_ += ((frozenValid_ ? 1.0f : 0.0f) - frozenBlend_)
            * freezeCaptureCoefficient_;
        const bool externalMicRest = smoothed_.modulatorSource
                == AcapellaResonatorModulatorSource::ExternalMic
            && modulatorGateTarget_ <= 0.0f;
        const bool articulationRest = externalMicRest
            || (!gesture_.active && !pitchCarrierActive_)
            || (!gesture_.carrierOnly
                && (gesture_.phoneme == AcapellaPhoneme::Silence
                    || (gesture_.flags & kAcapellaForcedRest) != 0u));
        if (articulationRest && frozenValid_) {
            for (float& value : frozenEnvelope_) {
                value += (0.0f - value) * freezeReleaseCoefficient_;
            }
        }
        mapEnvelope();

        float internalLeft = 0.0f;
        float internalRight = 0.0f;
        float dryCarrierLeft = 0.0f;
        float dryCarrierRight = 0.0f;
        renderCarrier(internalLeft, internalRight,
            externalCarrierLeft, externalCarrierRight,
            externalCarrierAvailable, dryCarrierLeft, dryCarrierRight);
        const float carrierLeft = internalLeft;
        const float carrierRight = internalRight;
        const float externalMicGate = lerp(
            1.0f, modulatorGateGain_, externalMicModeGain_);

        float wetLeft = 0.0f;
        float wetRight = 0.0f;
        for (uint32_t band = 0u; band < kAcapellaResonatorBands; ++band) {
            const float position = static_cast<float>(band)
                / static_cast<float>(kAcapellaResonatorBands - 1u);
            float pan = 0.0f;
            if (smoothed_.stereoMode == AcapellaResonatorStereoMode::Spread) {
                pan = (position * 2.0f - 1.0f) * smoothed_.stereoSpread;
            } else if (smoothed_.stereoMode
                    == AcapellaResonatorStereoMode::OddEven) {
                pan = (band & 1u ? 1.0f : -1.0f)
                    * smoothed_.stereoSpread;
            }
            const float leftGain = 1.0f - pan * 0.42f;
            const float rightGain = 1.0f + pan * 0.42f;
            const float envelope = mappedEnvelope_[band];
            const auto response = responseForBand(band);
            const float bandLeft = synthesisLeft_[band].process(
                carrierLeft, response) * activeBandGain_[band];
            const float bandRight = synthesisRight_[band].process(
                carrierRight, response) * activeBandGain_[band];
            wetLeft += bandLeft
                * envelope * leftGain;
            wetRight += bandRight
                * envelope * rightGain;
            const float meterTarget = externalMicGate * 0.5f
                * (std::abs(bandLeft * envelope)
                    + std::abs(bandRight * envelope));
            synthesisMeters_[band] += (meterTarget
                - synthesisMeters_[band]) * meterCoefficient_;
        }

        const float wetGain = 2.15f / std::sqrt(
            static_cast<float>(std::max<uint32_t>(1u, activeBands)));
        const float drive = std::exp2(smoothed_.driveDb / 6.020599913f);
        wetLeft = std::tanh(wetLeft * wetGain * drive)
            * (0.92f - smoothed_.driveDb * 0.006f);
        wetRight = std::tanh(wetRight * wetGain * drive)
            * (0.92f - smoothed_.driveDb * 0.006f);
        articulationLowLeft_ += (analysisLeft - articulationLowLeft_)
            * articulationHighpassCoefficient_;
        articulationLowRight_ += (analysisRight - articulationLowRight_)
            * articulationHighpassCoefficient_;
        const float articulationGain = smoothed_.articulationThru
            * (0.16f + 0.84f * voicingNoiseMix_);
        wetLeft += (analysisLeft - articulationLowLeft_) * articulationGain;
        wetRight += (analysisRight - articulationLowRight_) * articulationGain;
        // A causal output pole suppresses waveform-switch and low-rate fold
        // discontinuities without delaying the exact Amount-zero dry rail.
        wetOutputLeft_ += (wetLeft - wetOutputLeft_) * wetOutputCoefficient_;
        wetOutputRight_ += (wetRight - wetOutputRight_) * wetOutputCoefficient_;
        const float maximumOutputStep = 0.040f;
        wetLeft = previousWetLeft_ + clamp(
            wetOutputLeft_ - previousWetLeft_,
            -maximumOutputStep, maximumOutputStep);
        wetRight = previousWetRight_ + clamp(
            wetOutputRight_ - previousWetRight_,
            -maximumOutputStep, maximumOutputStep);
        previousWetLeft_ = wetLeft;
        previousWetRight_ = wetRight;

        // Bank Mix is a true dry/wet parameter. Scaling it by the carrier's
        // attack envelope briefly exposed the dry oscillator at a nominally
        // fully-wet note onset (most obvious as a click under a silent mic),
        // and scaling it down on release could truncate a resonator tail.
        const float amount = smoothed_.amount;
        // Bank Mix crossfades against the selected carrier, not the raw host
        // input. External/Internal blend and External Gain therefore retain
        // their meaning across the full wet/dry range.
        const float dryLeft = dryCarrierLeft;
        const float dryRight = dryCarrierRight;
        float outputLeft = lerp(dryLeft, wetLeft, amount);
        float outputRight = lerp(dryRight, wetRight, amount);
        if (!std::isfinite(outputLeft) || !std::isfinite(outputRight)) {
            resetSignalState();
            outputLeft = dryLeft;
            outputRight = dryRight;
        }
        // Gate the complete pre-echo bank result. This closes every
        // unmodulated carrier escape path (dry Bank Mix, Open Level, and a
        // held Freeze vector) while downstream echo remains free to decay.
        // Crossfading the source-mode weight prevents Modulator Source
        // automation from hard-switching a full-scale carrier in one sample.
        outputLeft *= externalMicGate;
        outputRight *= externalMicGate;
        // Noise substitution and a partially dry Bank Mix are legitimate
        // broadband signals, but neither may turn a mic-gate edge into a
        // discontinuity. Limit the complete External Mic result, leaving the
        // Internal Speech and compatibility dry-carrier paths unchanged.
        constexpr float maximumExternalOutputStep = 0.040f;
        const float limitedExternalLeft = previousExternalOutputLeft_ + clamp(
            outputLeft - previousExternalOutputLeft_,
            -maximumExternalOutputStep, maximumExternalOutputStep);
        const float limitedExternalRight = previousExternalOutputRight_ + clamp(
            outputRight - previousExternalOutputRight_,
            -maximumExternalOutputStep, maximumExternalOutputStep);
        previousExternalOutputLeft_ = limitedExternalLeft;
        previousExternalOutputRight_ = limitedExternalRight;
        outputLeft = lerp(outputLeft, limitedExternalLeft,
            externalMicModeGain_);
        outputRight = lerp(outputRight, limitedExternalRight,
            externalMicModeGain_);
        outputLeft = clamp(outputLeft, -1.8f, 1.8f);
        outputRight = clamp(outputRight, -1.8f, 1.8f);
        const float outputActivity = std::max(
            std::abs(outputLeft), std::abs(outputRight));
        if (outputActivity > activityEnvelope_) {
            activityEnvelope_ = outputActivity;
        } else {
            activityEnvelope_ += (outputActivity - activityEnvelope_)
                * activityReleaseCoefficient_;
        }
        return { outputLeft, outputRight,
            dryLeft, dryRight };
    }

    AcapellaResonatorStereoFrame processFrameStereo(float left, float right)
    {
        return processFrameStereo(left, right, left, right, true);
    }

    AcapellaResonatorStereoFrame processFrameStereo(
        float analysisLeft, float analysisRight,
        float externalCarrierLeft, float externalCarrierRight)
    {
        return processFrameStereo(analysisLeft, analysisRight,
            externalCarrierLeft, externalCarrierRight, true);
    }

private:
    struct CarrierVoice {
        uint64_t id = 0u;
        float phase = 0.0f;
        float frequencyHz = 146.83f;
        float targetFrequencyHz = 146.83f;
        float level = 0.0f;
        float targetGain = 0.0f;
        float pan = 0.0f;
        bool active = false;
    };

    static float finiteInput(float value)
    {
        return std::isfinite(value) ? clamp(value, -4.0f, 4.0f) : 0.0f;
    }

    using FilterResponse = acapella_resonator_detail::TptBandpass::Response;

    FilterResponse responseForBand(uint32_t band) const
    {
        if (params_.bandLayout != AcapellaResonatorBandLayout::Speech22) {
            return FilterResponse::Bandpass;
        }
        if (band == 0u) return FilterResponse::Lowpass;
        if (band + 1u == kAcapellaResonatorBands) {
            return FilterResponse::Highpass;
        }
        return FilterResponse::Bandpass;
    }

    void configureLayout(bool immediate)
    {
        if (params_.bandLayout == AcapellaResonatorBandLayout::Speech22) {
            centerFrequencies_ = kAcapellaSpeechBandFrequencies;
        } else {
            const float top = std::min(9000.0f, sampleRate_ * 0.43f);
            const float bottom = std::min(90.0f, top * 0.25f);
            for (uint32_t band = 0u;
                 band < kAcapellaResonatorWideBands; ++band) {
                const float position = static_cast<float>(band)
                    / static_cast<float>(kAcapellaResonatorWideBands - 1u);
                centerFrequencies_[band] = bottom
                    * std::pow(top / bottom, position);
            }
            for (uint32_t band = kAcapellaResonatorWideBands;
                 band < kAcapellaResonatorBands; ++band) {
                centerFrequencies_[band] = top;
            }
        }
        for (uint32_t band = 0u; band < kAcapellaResonatorBands; ++band) {
            analysisLeft_[band].configure(
                centerFrequencies_[band], 2.70f, sampleRate_, immediate);
            analysisRight_[band].configure(
                centerFrequencies_[band], 2.70f, sampleRate_, immediate);
            analysisLeftSecond_[band].configure(
                centerFrequencies_[band], 2.70f, sampleRate_, immediate);
            analysisRightSecond_[band].configure(
                centerFrequencies_[band], 2.70f, sampleRate_, immediate);
            analysisLeftThird_[band].configure(
                centerFrequencies_[band], 2.70f, sampleRate_, immediate);
            analysisRightThird_[band].configure(
                centerFrequencies_[band], 2.70f, sampleRate_, immediate);
            analysisLeftFourth_[band].configure(
                centerFrequencies_[band], 2.70f, sampleRate_, immediate);
            analysisRightFourth_[band].configure(
                centerFrequencies_[band], 2.70f, sampleRate_, immediate);
        }
    }

    void smoothParams()
    {
        const auto smooth = [this](float& current, float target) {
            current += (target - current) * parameterCoefficient_;
        };
        smooth(smoothed_.amount, params_.amount);
        smooth(smoothed_.carrierHarmonics, params_.carrierHarmonics);
        smooth(smoothed_.carrierColor, params_.carrierColor);
        smooth(smoothed_.carrierNoise, params_.carrierNoise);
        smooth(smoothed_.pitchHoldMs, params_.pitchHoldMs);
        smooth(smoothed_.voicingThreshold, params_.voicingThreshold);
        smooth(smoothed_.voicedTransitionMs, params_.voicedTransitionMs);
        smooth(smoothed_.unvoicedTransitionMs, params_.unvoicedTransitionMs);
        smooth(smoothed_.voicedLevel, params_.voicedLevel);
        smooth(smoothed_.unvoicedLevel, params_.unvoicedLevel);
        smooth(smoothed_.externalCarrierMix, params_.externalCarrierMix);
        smooth(smoothed_.externalCarrierGainDb,
            params_.externalCarrierGainDb);
        smooth(smoothed_.pulseWidth, params_.pulseWidth);
        smooth(smoothed_.carrierLfoRateHz, params_.carrierLfoRateHz);
        smooth(smoothed_.carrierLfoDepthSemitones,
            params_.carrierLfoDepthSemitones);
        smooth(smoothed_.carrierLfoPwmDepth, params_.carrierLfoPwmDepth);
        smooth(smoothed_.carrierLfoSyncDivisionBeats,
            params_.carrierLfoSyncDivisionBeats);
        smooth(smoothed_.analysisBlend, params_.analysisBlend);
        smooth(smoothed_.attackMs, params_.attackMs);
        smooth(smoothed_.releaseMs, params_.releaseMs);
        smooth(smoothed_.resonance, params_.resonance);
        smooth(smoothed_.driveDb, params_.driveDb);
        smooth(smoothed_.bandShiftSemitones,
            params_.bandShiftSemitones);
        smooth(smoothed_.bandStretch, params_.bandStretch);
        smooth(smoothed_.tilt, params_.tilt);
        smooth(smoothed_.sibilance, params_.sibilance);
        smooth(smoothed_.openLevel, params_.openLevel);
        smooth(smoothed_.articulationThru, params_.articulationThru);
        smooth(smoothed_.matrixMorph, params_.matrixMorph);
        smooth(smoothed_.customMatrixMorph, params_.customMatrixMorph);
        smooth(smoothed_.stereoSpread, params_.stereoSpread);
        smooth(smoothed_.freeze, params_.freeze);
        smooth(smoothed_.blurMs, params_.blurMs);
        smooth(smoothed_.gestureFollow, params_.gestureFollow);
        smoothed_.mode = params_.mode;
        smoothed_.modulatorSource = params_.modulatorSource;
        smoothed_.bandLayout = params_.bandLayout;
        smoothed_.analysisSlope = params_.analysisSlope;
        smoothed_.carrierShape = params_.carrierShape;
        smoothed_.carrierPitchSource = params_.carrierPitchSource;
        smoothed_.pitchScaleRoot = params_.pitchScaleRoot;
        smoothed_.pitchScale = params_.pitchScale;
        smoothed_.voicingMode = params_.voicingMode;
        smoothed_.carrierLfoShape = params_.carrierLfoShape;
        smoothed_.carrierLfoSync = params_.carrierLfoSync;
        smoothed_.coupling = params_.coupling;
        smoothed_.matrixMode = params_.matrixMode;
        smoothed_.stereoMode = params_.stereoMode;
        smoothed_.freezeTrigger = params_.freezeTrigger;
        if (params_.freeze <= 1.0e-5f
            && smoothed_.freeze <= 1.0e-5f) {
            frozenValid_ = false;
        }
    }

    void updateSynthesisCoefficients()
    {
        const float q = lerp(0.72f, 9.0f,
            smoothed_.resonance * smoothed_.resonance);
        const float centerLog = 0.5f * static_cast<float>(
            kAcapellaResonatorBands - 1u);
        for (uint32_t band = 0u; band < kAcapellaResonatorBands; ++band) {
            const float distance = (static_cast<float>(band) - centerLog)
                / centerLog;
            const float stretchSemitones = smoothed_.bandStretch
                * distance * 10.0f;
            const float semitones = smoothed_.bandShiftSemitones
                + stretchSemitones;
            const float frequency = centerFrequencies_[band]
                * std::exp2(semitones / 12.0f);
            synthesisLeft_[band].configure(frequency, q, sampleRate_);
            synthesisRight_[band].configure(frequency, q, sampleRate_);
        }
    }

    void updateRuntimeCoefficients()
    {
        modulatorGateReleaseCoefficient_ =
            acapella_resonator_detail::envelopeMilliseconds(
                clamp(smoothed_.releaseMs, 35.0f, 350.0f), sampleRate_);
        analysisLevelReleaseCoefficient_ =
            acapella_resonator_detail::onePoleMilliseconds(
                smoothed_.releaseMs, sampleRate_);
        for (uint32_t band = 0u; band < kAcapellaResonatorBands; ++band) {
            const float position = static_cast<float>(band)
                / static_cast<float>(kAcapellaResonatorBands - 1u);
            bandAttackCoefficients_[band] =
                acapella_resonator_detail::onePoleMilliseconds(
                    smoothed_.attackMs * lerp(1.32f, 0.68f, position),
                    sampleRate_);
            bandReleaseCoefficients_[band] =
                acapella_resonator_detail::onePoleMilliseconds(
                    smoothed_.releaseMs * lerp(1.28f, 0.72f, position),
                    sampleRate_);
        }
        blurCoefficient_ = smoothed_.blurMs <= 0.01f ? 1.0f
            : acapella_resonator_detail::onePoleMilliseconds(
                smoothed_.blurMs, sampleRate_);
        freezeReleaseCoefficient_ =
            acapella_resonator_detail::onePoleMilliseconds(
                smoothed_.releaseMs * (1.0f + 0.45f * smoothed_.freeze),
                sampleRate_);
        freezeCaptureCoefficient_ =
            acapella_resonator_detail::onePoleMilliseconds(
                std::max(4.0f, std::min(28.0f,
                    smoothed_.attackMs * 1.5f)), sampleRate_);
        phonemeTargetAttackCoefficient_ =
            acapella_resonator_detail::onePoleMilliseconds(
                std::max(1.5f, smoothed_.attackMs * 0.65f), sampleRate_);
        phonemeTargetReleaseCoefficient_ =
            acapella_resonator_detail::onePoleMilliseconds(
                std::max(8.0f, smoothed_.releaseMs * 0.38f), sampleRate_);
        meterCoefficient_ = acapella_resonator_detail::onePoleMilliseconds(
            34.0f, sampleRate_);
        externalCarrierGain_ = std::exp2(
            smoothed_.externalCarrierGainDb / 6.020599913f);
        voicedTransitionCoefficient_ =
            acapella_resonator_detail::onePoleMilliseconds(
                smoothed_.voicedTransitionMs, sampleRate_);
        unvoicedTransitionCoefficient_ =
            acapella_resonator_detail::onePoleMilliseconds(
                smoothed_.unvoicedTransitionMs, sampleRate_);
        const float carrierReleaseMs = smoothed_.freeze > 1.0e-4f
            ? std::max(20.0f, smoothed_.releaseMs
                * (0.35f + 0.65f * smoothed_.freeze))
            : 20.0f;
        carrierReleaseCoefficient_ =
            acapella_resonator_detail::onePoleMilliseconds(
                carrierReleaseMs, sampleRate_);
        const float cutoff = 380.0f * std::exp2(
            (smoothed_.carrierColor + 1.0f) * 2.35f);
        carrierToneCoefficient_ = 1.0f - std::exp(
            -2.0f * kPi * std::min(cutoff, sampleRate_ * 0.38f)
                / sampleRate_);
    }

    void updatePhonemeTarget(AcapellaPhoneme phoneme)
    {
        phonemeTarget_.fill(0.0f);
        if (phoneme == AcapellaPhoneme::Silence) return;
        if (acapellaPhonemeIsVowel(phoneme)) {
            const auto shape = acapella_source_detail::phonemeVowelShape(
                phoneme);
            constexpr std::array<float, 5u> weights {{
                1.0f, 0.82f, 0.58f, 0.36f, 0.22f,
            }};
            float maximum = 1.0e-6f;
            for (uint32_t band = 0u; band < kAcapellaResonatorBands; ++band) {
                float value = 0.035f;
                for (uint32_t formant = 0u; formant < 5u; ++formant) {
                    value += weights[formant]
                        * acapella_resonator_detail::bell(
                            centerFrequencies_[band],
                            shape.frequency[formant],
                            shape.bandwidth[formant] * 2.2f);
                }
                phonemeTarget_[band] = value;
                maximum = std::max(maximum, value);
            }
            for (float& value : phonemeTarget_) value /= maximum;
            return;
        }

        float center = 3000.0f;
        float width = 1800.0f;
        switch (phoneme) {
        case AcapellaPhoneme::S:
        case AcapellaPhoneme::Z: center = 6500.0f; width = 1900.0f; break;
        case AcapellaPhoneme::SH:
        case AcapellaPhoneme::ZH: center = 3600.0f; width = 1250.0f; break;
        case AcapellaPhoneme::CH:
        case AcapellaPhoneme::JH: center = 4100.0f; width = 1800.0f; break;
        case AcapellaPhoneme::T:
        case AcapellaPhoneme::D: center = 5200.0f; width = 2400.0f; break;
        case AcapellaPhoneme::K:
        case AcapellaPhoneme::G: center = 2600.0f; width = 1500.0f; break;
        case AcapellaPhoneme::P:
        case AcapellaPhoneme::B: center = 1150.0f; width = 1100.0f; break;
        case AcapellaPhoneme::F:
        case AcapellaPhoneme::V:
        case AcapellaPhoneme::TH:
        case AcapellaPhoneme::DH: center = 4300.0f; width = 3200.0f; break;
        case AcapellaPhoneme::HH: center = 2200.0f; width = 3100.0f; break;
        case AcapellaPhoneme::M: center = 280.0f; width = 380.0f; break;
        case AcapellaPhoneme::N: center = 900.0f; width = 720.0f; break;
        case AcapellaPhoneme::NG: center = 1550.0f; width = 950.0f; break;
        case AcapellaPhoneme::L: center = 1250.0f; width = 1050.0f; break;
        case AcapellaPhoneme::R: center = 1550.0f; width = 900.0f; break;
        case AcapellaPhoneme::W: center = 650.0f; width = 700.0f; break;
        case AcapellaPhoneme::Y: center = 2300.0f; width = 1050.0f; break;
        default: break;
        }
        const float voicing = acapella_source_detail::phonemeVoicing(phoneme);
        for (uint32_t band = 0u; band < kAcapellaResonatorBands; ++band) {
            const float lowVoice = acapella_resonator_detail::bell(
                centerFrequencies_[band], 420.0f, 520.0f) * voicing * 0.52f;
            const float articulation = acapella_resonator_detail::bell(
                centerFrequencies_[band], center, width);
            phonemeTarget_[band] = clamp(
                0.018f + lowVoice + articulation, 0.0f, 1.0f);
        }
    }

    void buildLiveEnvelope()
    {
        const bool scoreEnabled = !gesture_.carrierOnly
            && smoothed_.modulatorSource
                != AcapellaResonatorModulatorSource::ExternalMic;
        const float gate = gesture_.active && scoreEnabled ? 1.0f : 0.0f;
        const float analysisScale = clamp(analysisLevel_ * 2.8f,
            0.0f, 0.72f);
        const float scoreScale = gate * (0.075f + analysisScale * 0.92f);
        const float scoreMix = scoreEnabled ? smoothed_.analysisBlend
            * smoothed_.gestureFollow : 0.0f;
        const float consonant = scoreEnabled
            ? acapella_resonator_detail::phonemeSibilance(gesture_.phoneme)
            : 0.0f;
        for (uint32_t band = 0u; band < kAcapellaResonatorBands; ++band) {
            const float position = static_cast<float>(band)
                / static_cast<float>(kAcapellaResonatorBands - 1u);
            // The cascaded analysis filters have greater on-band gain but
            // much stronger rejection. A gentle expansion preserves that
            // contrast in the VCA controls instead of turning the result
            // back into a broadband amplitude follower.
            const float normalized = clamp(
                analysisEnvelope_[band] * 2.55f, 0.0f, 1.25f);
            const float measured = std::pow(normalized, 1.35f);
            const float score = smoothedPhonemeTarget_[band] * scoreScale
                * (0.78f + 0.30f * static_cast<float>(gesture_.stress > 0u));
            float value = 0.0f;
            switch (smoothed_.mode) {
            case AcapellaResonatorMode::Vocoder:
                value = measured;
                break;
            case AcapellaResonatorMode::Hybrid:
                value = std::max(smoothed_.openLevel,
                    lerp(measured, score, scoreMix));
                break;
            case AcapellaResonatorMode::FilterBank:
                value = smoothed_.openLevel
                    + score * 0.22f * smoothed_.gestureFollow
                    + measured * 0.08f;
                break;
            }
            const float highBand = position * position;
            value += consonant * smoothed_.sibilance * highBand
                * (0.070f + analysisScale * 0.42f) * gate;
            liveEnvelope_[band] = clamp(value, 0.0f, 1.25f);
        }
    }

    void captureEnvelope()
    {
        // Capture articulation energy, not the slow audible blur state. With
        // a 200--300 ms Blur, sampling the latter 6 ms into a syllable stored
        // an almost-silent vector. Blur still governs the live path and the
        // crossfade into this held control vector.
        for (uint32_t band = 0u; band < kAcapellaResonatorBands; ++band) {
            // Preserve the held side of the existing Freeze crossfade. The
            // outer blurred/live-to-held mix is applied later by
            // blurredOrFrozen(); storing that already-mixed result here would
            // apply partial Freeze twice and make every recapture step down.
            captureFromEnvelope_[band] = frozenValid_
                ? lerp(captureFromEnvelope_[band], frozenEnvelope_[band],
                      frozenBlend_)
                : blurredEnvelope_[band];
        }
        frozenEnvelope_ = liveEnvelope_;
        frozenValid_ = true;
        frozenBlend_ = 0.0f;
        continuousCaptureArmed_ = false;
    }

    float transformedEnvelope(uint32_t band) const
    {
        switch (smoothed_.matrixMode) {
        case AcapellaResonatorMatrixMode::Rotate:
            return coupledEnvelope_[(band + 3u) % kAcapellaResonatorBands];
        case AcapellaResonatorMatrixMode::Mirror:
            return coupledEnvelope_[kAcapellaResonatorBands - 1u - band];
        case AcapellaResonatorMatrixMode::Chord: {
            const float root = coupledEnvelope_[band];
            const float third = coupledEnvelope_[
                (band + kAcapellaResonatorBands - 3u)
                    % kAcapellaResonatorBands];
            const float fifth = coupledEnvelope_[
                (band + 4u) % kAcapellaResonatorBands];
            return root * 0.46f + third * 0.31f + fifth * 0.34f;
        }
        case AcapellaResonatorMatrixMode::Sparse: {
            constexpr std::array<float, kAcapellaResonatorBands> mask {{
                1.0f, 0.08f, 0.18f, 0.95f,
                0.10f, 0.72f, 0.05f, 1.0f,
                0.12f, 0.55f, 0.04f, 0.88f,
                0.08f, 0.62f, 0.04f, 0.82f,
                0.06f, 0.70f, 0.12f, 0.92f, 0.04f, 0.64f,
            }};
            return coupledEnvelope_[(band * 5u) % kAcapellaResonatorBands]
                * mask[band];
        }
        case AcapellaResonatorMatrixMode::Custom: {
            float routed = 0.0f;
            float absoluteSum = 0.0f;
            const uint32_t row = band * kAcapellaResonatorBands;
            for (uint32_t source = 0u;
                 source < kAcapellaResonatorBands; ++source) {
                const float route = lerp(params_.customMatrixA[row + source],
                    params_.customMatrixB[row + source],
                    smoothed_.customMatrixMorph);
                routed += coupledEnvelope_[source] * route;
                absoluteSum += std::abs(route);
            }
            return routed / std::max(1.0f, absoluteSum);
        }
        case AcapellaResonatorMatrixMode::Identity:
        default:
            return coupledEnvelope_[band];
        }
    }

    float blurredOrFrozen(uint32_t band) const
    {
        if (!frozenValid_) return blurredEnvelope_[band];
        const float capture = lerp(captureFromEnvelope_[band],
            frozenEnvelope_[band], frozenBlend_);
        return lerp(blurredEnvelope_[band], capture, smoothed_.freeze);
    }

    void mapEnvelope()
    {
        for (uint32_t band = 0u; band < kAcapellaResonatorBands; ++band) {
            const int32_t source = static_cast<int32_t>(band)
                - smoothed_.coupling;
            coupledEnvelope_[band] = source >= 0
                    && source < static_cast<int32_t>(kAcapellaResonatorBands)
                ? blurredOrFrozen(static_cast<uint32_t>(source)) : 0.0f;
        }
        for (uint32_t band = 0u; band < kAcapellaResonatorBands; ++band) {
            const float identity = coupledEnvelope_[band];
            const float transformed = transformedEnvelope(band);
            const float position = static_cast<float>(band)
                / static_cast<float>(kAcapellaResonatorBands - 1u);
            const float tilt = std::exp2(smoothed_.tilt
                * (position * 2.0f - 1.0f) * 1.7f);
            mappedTarget_[band] = clamp(lerp(identity, transformed,
                smoothed_.matrixMorph) * tilt * params_.bandTrims[band],
                -2.0f, 2.0f);
            mappedEnvelope_[band] += (mappedTarget_[band]
                - mappedEnvelope_[band]) * routeSmoothingCoefficient_;
        }
    }

    static bool pitchClassInScale(uint32_t pitchClass,
        uint32_t root, AcapellaResonatorPitchScale scale)
    {
        if (scale == AcapellaResonatorPitchScale::Continuous
            || scale == AcapellaResonatorPitchScale::Chromatic) {
            return true;
        }
        const uint32_t interval = (pitchClass + 12u - root) % 12u;
        switch (scale) {
        case AcapellaResonatorPitchScale::Major:
            return interval == 0u || interval == 2u || interval == 4u
                || interval == 5u || interval == 7u || interval == 9u
                || interval == 11u;
        case AcapellaResonatorPitchScale::NaturalMinor:
            return interval == 0u || interval == 2u || interval == 3u
                || interval == 5u || interval == 7u || interval == 8u
                || interval == 10u;
        case AcapellaResonatorPitchScale::HarmonicMinor:
            return interval == 0u || interval == 2u || interval == 3u
                || interval == 5u || interval == 7u || interval == 8u
                || interval == 11u;
        case AcapellaResonatorPitchScale::Dorian:
            return interval == 0u || interval == 2u || interval == 3u
                || interval == 5u || interval == 7u || interval == 9u
                || interval == 10u;
        case AcapellaResonatorPitchScale::MajorPentatonic:
            return interval == 0u || interval == 2u || interval == 4u
                || interval == 7u || interval == 9u;
        case AcapellaResonatorPitchScale::MinorPentatonic:
            return interval == 0u || interval == 3u || interval == 5u
                || interval == 7u || interval == 10u;
        default:
            return true;
        }
    }

    float quantizedPitch(float frequencyHz) const
    {
        frequencyHz = clamp(frequencyHz,
            kAcapellaResonatorPitchMinimumHz,
            kAcapellaResonatorPitchMaximumHz);
        if (smoothed_.pitchScale
            == AcapellaResonatorPitchScale::Continuous) {
            return frequencyHz;
        }
        const float midi = 69.0f + 12.0f
            * std::log2(frequencyHz / 440.0f);
        const int32_t center = static_cast<int32_t>(std::lround(midi));
        int32_t best = center;
        float bestDistance = 1.0e9f;
        for (int32_t candidate = center - 12;
             candidate <= center + 12; ++candidate) {
            const uint32_t pitchClass = static_cast<uint32_t>(
                (candidate % 12 + 12) % 12);
            if (!pitchClassInScale(pitchClass,
                    smoothed_.pitchScaleRoot, smoothed_.pitchScale)) {
                continue;
            }
            const float distance = std::abs(
                static_cast<float>(candidate) - midi);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = candidate;
            }
        }
        return 440.0f * std::exp2(
            (static_cast<float>(best) - 69.0f) / 12.0f);
    }

    float pitchTrackerSampleAgo(uint32_t ago) const
    {
        const uint32_t index = (pitchTrackerWrite_
            + kPitchTrackerBufferSize - 1u - ago)
            % kPitchTrackerBufferSize;
        return pitchTrackerBuffer_[index];
    }

    void finishPitchTrackerWork()
    {
        pitchDifference_[0u] = 1.0f;
        double cumulative = 0.0;
        for (uint32_t lag = 1u; lag <= pitchWorkMaximumLag_; ++lag) {
            const double difference = pitchDifference_[lag];
            cumulative += difference;
            pitchDifference_[lag] = cumulative > 1.0e-15
                ? static_cast<float>(difference * lag / cumulative)
                : 1.0f;
        }
        uint32_t bestLag = pitchWorkMinimumLag_;
        float bestValue = pitchDifference_[bestLag];
        // At the top of the supported register the true period can be the
        // very first allowed lag. Treat that boundary as a valid local
        // minimum; otherwise the search skips it and often selects its first
        // multiple, producing an octave-down jump or eventual gate closure.
        const bool minimumBoundaryAccepted = bestValue < 0.22f
            && bestValue < pitchDifference_[bestLag + 1u];
        if (!minimumBoundaryAccepted) {
            for (uint32_t lag = pitchWorkMinimumLag_ + 1u;
                 lag < pitchWorkMaximumLag_; ++lag) {
                const float value = pitchDifference_[lag];
                if (value < bestValue) {
                    bestValue = value;
                    bestLag = lag;
                }
                if (value < 0.22f
                    && value <= pitchDifference_[lag - 1u]
                    && value < pitchDifference_[lag + 1u]) {
                    bestLag = lag;
                    bestValue = value;
                    break;
                }
            }
        }
        const float confidence = clamp(1.0f - bestValue, 0.0f, 1.0f);
        bool accepted = false;
        float refinedLag = 0.0f;
        if (confidence >= 0.68f) {
            const float before = pitchDifference_[bestLag - 1u];
            const float center = pitchDifference_[bestLag];
            const float after = pitchDifference_[bestLag + 1u];
            const float denominator = before - 2.0f * center + after;
            const float correction = std::abs(denominator) > 1.0e-6f
                ? clamp(0.5f * (before - after) / denominator,
                      -0.5f, 0.5f)
                : 0.0f;
            refinedLag = static_cast<float>(bestLag) + correction;
            accepted = refinedLag > 1.0f;
        }
        if (accepted && modulatorGateOpen_) {
            const float target = quantizedPitch(
                pitchTrackerRate_ / refinedLag);
            if (!pitchHoldLatched_ && pitchHoldSamplesRemaining_ == 0u) {
                detectedPitchHz_ = target;
            } else {
                const float currentLog = std::log2(
                    std::max(20.0f, detectedPitchHz_));
                const float targetLog = std::log2(target);
                detectedPitchHz_ = std::exp2(lerp(
                    currentLog, targetLog, 0.32f));
            }
            detectedPitchConfidence_ = confidence;
            if (params_.pitchHoldMs
                >= kAcapellaResonatorInfinitePitchHoldMs - 0.5f) {
                pitchHoldLatched_ = true;
                pitchHoldSamplesRemaining_ = 0u;
            } else {
                pitchHoldLatched_ = false;
                pitchHoldSamplesRemaining_ = std::max<uint32_t>(1u,
                    static_cast<uint32_t>(sampleRate_
                        * smoothed_.pitchHoldMs * 0.001f));
            }
        } else {
            detectedPitchConfidence_ *= 0.94f;
        }
        pitchWorkLag_ = 0u;
    }

    void processPitchTrackerWork()
    {
        if (pitchWorkLag_ == 0u) return;
        // Spread the YIN difference calculation across decimated samples.
        // This preserves the same analysis window while avoiding a large
        // once-per-hop spike in a 32-frame real-time callback.
        constexpr uint32_t lagsPerTick = 2u;
        for (uint32_t work = 0u;
             work < lagsPerTick && pitchWorkLag_ <= pitchWorkMaximumLag_;
             ++work, ++pitchWorkLag_) {
            double difference = 0.0;
            for (uint32_t sample = 0u; sample < kPitchTrackerWindow; ++sample) {
                const float delta = pitchAnalysisWindow_[sample]
                    - pitchAnalysisWindow_[sample + pitchWorkLag_];
                difference += static_cast<double>(delta) * delta;
            }
            pitchDifference_[pitchWorkLag_]
                = static_cast<float>(difference);
        }
        if (pitchWorkLag_ > pitchWorkMaximumLag_) {
            finishPitchTrackerWork();
        }
    }

    void updatePitchTracker(float input)
    {
        // YIN is intentionally dormant in MIDI mode; this keeps the
        // traditional polyphonic path as cheap as the original bank.
        if (smoothed_.carrierPitchSource
            != AcapellaResonatorCarrierPitchSource::Voice) {
            pitchHoldSamplesRemaining_ = 0u;
            pitchHoldLatched_ = false;
            pitchCarrierActive_ = false;
            detectedPitchConfidence_ = 0.0f;
            return;
        }
        const bool infiniteHold = params_.pitchHoldMs
            >= kAcapellaResonatorInfinitePitchHoldMs - 0.5f;
        if (infiniteHold && !pitchHoldLatched_
            && pitchHoldSamplesRemaining_ > 0u) {
            // Moving the control to Infinite after a pitch has already been
            // acquired latches that pitch immediately.
            pitchHoldLatched_ = true;
            pitchHoldSamplesRemaining_ = 0u;
        } else if (!infiniteHold && pitchHoldLatched_) {
            // Returning to a timed hold resumes with the selected duration.
            pitchHoldLatched_ = false;
            pitchHoldSamplesRemaining_ = std::max<uint32_t>(1u,
                static_cast<uint32_t>(sampleRate_
                    * smoothed_.pitchHoldMs * 0.001f));
        }
        if (!pitchHoldLatched_ && pitchHoldSamplesRemaining_ > 0u) {
            --pitchHoldSamplesRemaining_;
        }
        const auto updateCarrierActive = [&] {
            pitchCarrierActive_ = modulatorGateOpen_
                && (pitchHoldLatched_ || pitchHoldSamplesRemaining_ > 0u);
        };
        pitchTrackerDc_ += (input - pitchTrackerDc_)
            * pitchTrackerDcCoefficient_;
        const float highPassed = input - pitchTrackerDc_;
        pitchTrackerLow_ += (highPassed - pitchTrackerLow_)
            * pitchTrackerLowCoefficient_;
        if (++pitchTrackerDecimationCounter_ < pitchTrackerDecimation_) {
            updateCarrierActive();
            return;
        }
        pitchTrackerDecimationCounter_ = 0u;
        pitchTrackerBuffer_[pitchTrackerWrite_] = pitchTrackerLow_;
        pitchTrackerWrite_ = (pitchTrackerWrite_ + 1u)
            % kPitchTrackerBufferSize;
        pitchTrackerCount_ = std::min<uint32_t>(
            pitchTrackerCount_ + 1u, kPitchTrackerBufferSize);
        processPitchTrackerWork();
        if (++pitchTrackerHopCounter_ < pitchTrackerHop_) {
            updateCarrierActive();
            return;
        }
        pitchTrackerHopCounter_ = 0u;
        const uint32_t minimumLag = std::max<uint32_t>(2u,
            static_cast<uint32_t>(pitchTrackerRate_
                / kAcapellaResonatorPitchMaximumHz));
        const uint32_t maximumLag = std::min<uint32_t>(
            kPitchTrackerDifferenceSize - 2u,
            static_cast<uint32_t>(pitchTrackerRate_
                / kAcapellaResonatorPitchMinimumHz));
        if (pitchWorkLag_ == 0u
            && pitchTrackerCount_ >= kPitchTrackerWindow + maximumLag) {
            pitchWorkMinimumLag_ = minimumLag;
            pitchWorkMaximumLag_ = maximumLag;
            for (uint32_t sample = 0u;
                 sample < kPitchTrackerWindow + maximumLag; ++sample) {
                pitchAnalysisWindow_[sample] = pitchTrackerSampleAgo(sample);
            }
            pitchWorkLag_ = 1u;
        }
        if (!modulatorGateOpen_ && !pitchHoldLatched_) {
            pitchHoldSamplesRemaining_ = 0u;
        }
        updateCarrierActive();
    }

    void syncCarrierVoices()
    {
        for (auto& voice : carrierVoices_) voice.targetGain = 0.0f;
        const bool voiceTrackedCarrier = smoothed_.carrierPitchSource
                == AcapellaResonatorCarrierPitchSource::Voice
            && pitchCarrierActive_;
        uint32_t desiredCount = voiceTrackedCarrier ? 1u
            : (gesture_.active ? gesture_.voiceCount : 0u);
        desiredCount = std::min<uint32_t>(
            desiredCount, kAcapellaResonatorMaxVoices);
        const bool fallback = !voiceTrackedCarrier
            && gesture_.active && desiredCount == 0u;
        if (fallback) desiredCount = 1u;

        const auto desiredId = [&](uint32_t desired) {
            if (voiceTrackedCarrier) return uint64_t { 0x706974636874726bull };
            if (fallback) {
                return gesture_.voiceInstance != 0u
                    ? gesture_.voiceInstance : uint64_t { 1u };
            }
            return gesture_.voiceInstanceIds[desired] != 0u
                ? gesture_.voiceInstanceIds[desired]
                : static_cast<uint64_t>(desired + 1u);
        };

        std::array<int32_t, kAcapellaResonatorMaxVoices> destinations {};
        destinations.fill(-1);
        std::array<bool, kAcapellaResonatorMaxVoices> claimed {};

        // Match every surviving ID before stealing any slot. Without this
        // pass, replacing a full chord could erase a later surviving voice
        // before its desired entry had been inspected.
        for (uint32_t desired = 0u; desired < desiredCount; ++desired) {
            const uint64_t id = desiredId(desired);
            for (uint32_t slot = 0u;
                 slot < kAcapellaResonatorMaxVoices; ++slot) {
                if (!claimed[slot] && carrierVoices_[slot].active
                    && carrierVoices_[slot].id == id) {
                    destinations[desired] = static_cast<int32_t>(slot);
                    claimed[slot] = true;
                    break;
                }
            }
        }

        // Allocate every missing ID from a different unclaimed slot. A newly
        // initialized zero-level voice is therefore never selected again in
        // the same synchronization pass.
        for (uint32_t desired = 0u; desired < desiredCount; ++desired) {
            if (destinations[desired] >= 0) continue;
            int32_t destination = -1;
            for (uint32_t slot = 0u;
                 slot < kAcapellaResonatorMaxVoices; ++slot) {
                if (!claimed[slot]
                    && (!carrierVoices_[slot].active
                        || carrierVoices_[slot].level < 1.0e-5f)) {
                    destination = static_cast<int32_t>(slot);
                    break;
                }
            }
            if (destination < 0) {
                float quietest = 1.0e9f;
                for (uint32_t slot = 0u;
                     slot < kAcapellaResonatorMaxVoices; ++slot) {
                    if (!claimed[slot]
                        && carrierVoices_[slot].level < quietest) {
                        quietest = carrierVoices_[slot].level;
                        destination = static_cast<int32_t>(slot);
                    }
                }
            }
            if (destination >= 0) {
                destinations[desired] = destination;
                claimed[static_cast<uint32_t>(destination)] = true;
            }
        }

        for (uint32_t desired = 0u; desired < desiredCount; ++desired) {
            const uint64_t id = desiredId(desired);
            const float frequency = voiceTrackedCarrier ? detectedPitchHz_
                : (fallback ? gesture_.frequencyHz
                            : gesture_.voiceFrequencyHz[desired]);
            const float gain = voiceTrackedCarrier ? 1.0f
                : (fallback ? 1.0f : gesture_.voiceGain[desired]);
            if (destinations[desired] < 0) continue;
            CarrierVoice* destination = &carrierVoices_[
                static_cast<uint32_t>(destinations[desired])];
            if (!destination->active || destination->id != id) {
                destination->id = id;
                destination->phase = 0.0f;
                destination->frequencyHz = frequency;
                destination->level = 0.0f;
                const uint32_t hash = static_cast<uint32_t>(
                    id ^ (id >> 32u));
                destination->pan = (static_cast<float>(hash & 1023u)
                    / 511.5f) - 1.0f;
                destination->active = true;
            }
            destination->targetFrequencyHz = frequency;
            destination->targetGain = gain;
        }

        for (auto& voice : carrierVoices_) {
            if (!voice.active) continue;
            const float coefficient = voice.targetGain > voice.level
                ? carrierAttackCoefficient_ : carrierReleaseCoefficient_;
            voice.level += (voice.targetGain - voice.level) * coefficient;
            voice.frequencyHz += (voice.targetFrequencyHz - voice.frequencyHz)
                * carrierFrequencyCoefficient_;
            if (voice.targetGain <= 0.0f && voice.level < 1.0e-5f) {
                voice = {};
            }
        }
    }

    static float randomBipolar(uint32_t& state)
    {
        state ^= state << 13u;
        state ^= state >> 17u;
        state ^= state << 5u;
        return static_cast<float>(state & 0x00ffffffu)
            * (2.0f / 16777215.0f) - 1.0f;
    }

    void renderCarrier(float& left, float& right,
        float externalLeft, float externalRight,
        bool externalCarrierAvailable,
        float& dryCarrierLeft, float& dryCarrierRight)
    {
        float lfoRate = smoothed_.carrierLfoRateHz;
        if (smoothed_.carrierLfoSync && tempoValid_) {
            lfoRate = tempoBpm_ / (60.0f
                * std::max(0.0625f, smoothed_.carrierLfoSyncDivisionBeats));
        }
        lfoRate = clamp(lfoRate, 0.02f, 13.0f);
        carrierLfoPhase_ += clamp(lfoRate / sampleRate_, 0.0f, 0.25f);
        carrierLfoPhase_ -= std::floor(carrierLfoPhase_);
        const float triangle = 1.0f - 4.0f
            * std::abs(carrierLfoPhase_ - 0.5f);
        const float square = carrierLfoPhase_ < 0.5f ? 1.0f : -1.0f;
        const float lfoTarget = smoothed_.carrierLfoShape
                == AcapellaResonatorCarrierLfoShape::Square ? square
                                                            : triangle;
        // Square modulation is intentionally edge-smoothed so PWM/FM cannot
        // inject note-on-like discontinuities into the synthesis bank.
        carrierLfoValue_ += (lfoTarget - carrierLfoValue_)
            * (smoothed_.carrierLfoShape
                    == AcapellaResonatorCarrierLfoShape::Square
                ? carrierLfoEdgeCoefficient_ : 1.0f);
        const float pitchModulation = std::exp2(carrierLfoValue_
            * smoothed_.carrierLfoDepthSemitones / 12.0f);
        const float pulseWidth = clamp(smoothed_.pulseWidth
            + carrierLfoValue_ * smoothed_.carrierLfoPwmDepth * 0.42f,
            0.05f, 0.95f);
        left = 0.0f;
        right = 0.0f;
        for (auto& voice : carrierVoices_) {
            if (!voice.active || voice.level <= 1.0e-6f) continue;
            const float increment = clamp(
                voice.frequencyHz * pitchModulation / sampleRate_,
                1.0e-6f, 0.45f);
            const float oscillator =
                acapella_resonator_detail::shapedOscillator(
                    smoothed_.carrierShape, voice.phase, increment,
                    smoothed_.carrierHarmonics, smoothed_.carrierColor,
                    pulseWidth);
            const float carrierSpread = smoothed_.stereoMode
                    == AcapellaResonatorStereoMode::Spread
                ? smoothed_.stereoSpread : 0.0f;
            const float pan = voice.pan * carrierSpread * 0.62f;
            left += oscillator * voice.level * (1.0f - pan);
            right += oscillator * voice.level * (1.0f + pan);
            voice.phase += increment;
            voice.phase -= std::floor(voice.phase);
        }

        const float noiseLeft = randomBipolar(noiseStateLeft_);
        const float noiseRight = randomBipolar(noiseStateRight_);
        noiseLowLeft_ += (noiseLeft - noiseLowLeft_) * noiseLowCoefficient_;
        noiseLowRight_ += (noiseRight - noiseLowRight_) * noiseLowCoefficient_;
        const float highNoiseLeft = noiseLeft - noiseLowLeft_;
        const float highNoiseRight = noiseRight - noiseLowRight_;
        const float commonNoise = (highNoiseLeft + highNoiseRight)
            * 0.70710678f;
        const float carrierSpread = smoothed_.stereoMode
                == AcapellaResonatorStereoMode::Spread
            ? smoothed_.stereoSpread : 0.0f;
        const float spreadNoiseLeft = lerp(
            commonNoise, highNoiseLeft, carrierSpread);
        const float spreadNoiseRight = lerp(
            commonNoise, highNoiseRight, carrierSpread);
        const bool scoreEnabled = !gesture_.carrierOnly
            && smoothed_.modulatorSource
                != AcapellaResonatorModulatorSource::ExternalMic;
        const float consonantTarget = gesture_.active && scoreEnabled
            ? acapella_resonator_detail::phonemeSibilance(gesture_.phoneme)
                * smoothed_.sibilance
            : 0.0f;
        const float sibilanceCoefficient = consonantTarget > sibilanceEnvelope_
            ? sibilanceAttackCoefficient_ : sibilanceReleaseCoefficient_;
        sibilanceEnvelope_ += (consonantTarget - sibilanceEnvelope_)
            * sibilanceCoefficient;
        const float pureNoise = smoothed_.carrierShape
                == AcapellaResonatorCarrierShape::Noise ? 0.72f : 0.0f;
        float noiseTarget = 0.5f;
        switch (smoothed_.voicingMode) {
        case AcapellaResonatorVoicingMode::Tonal: noiseTarget = 0.0f; break;
        case AcapellaResonatorVoicingMode::Noise: noiseTarget = 1.0f; break;
        case AcapellaResonatorVoicingMode::Blend: noiseTarget = 0.5f; break;
        case AcapellaResonatorVoicingMode::Detect:
        default: {
            const float phonemeNoise = scoreEnabled
                ? acapella_resonator_detail::phonemeSibilance(
                    gesture_.phoneme)
                : 0.0f;
            const float highEnergy = analysisEnvelope_[18u]
                + analysisEnvelope_[19u] + analysisEnvelope_[20u]
                + analysisEnvelope_[21u];
            const float lowEnergy = analysisEnvelope_[3u]
                + analysisEnvelope_[5u] + analysisEnvelope_[8u]
                + analysisEnvelope_[11u] + 1.0e-5f;
            const float spectralNoise = highEnergy
                / (highEnergy + lowEnergy);
            // Internal Speech can use its phoneme classification as a strong
            // prior. External Mic has no phoneme label, so its measured
            // high/low ratio must span the full detector range; the previous
            // 0.38 multiplier made the default 0.52 threshold unreachable and
            // effectively disabled classic unvoiced substitution.
            const float detector = clamp(scoreEnabled
                    ? 0.62f * phonemeNoise + 0.38f * spectralNoise
                    : spectralNoise,
                0.0f, 1.0f);
            const float hysteresis = noiseTarget_ > 0.5f ? -0.045f : 0.045f;
            noiseTarget_ = detector > smoothed_.voicingThreshold + hysteresis
                ? 1.0f : 0.0f;
            noiseTarget = noiseTarget_;
            break;
        }
        }
        const float voicingCoefficient = noiseTarget > voicingNoiseMix_
            ? unvoicedTransitionCoefficient_ : voicedTransitionCoefficient_;
        voicingNoiseMix_ += (noiseTarget - voicingNoiseMix_)
            * voicingCoefficient;
        // In External Mic mode there is no score phoneme to request a
        // consonant carrier. Let the measured voiced/unvoiced decision drive
        // the same rail so S/F/SH/T energy becomes an audible filtered noise
        // articulation rather than a faint change in oscillator colour.
        const float measuredSibilance = scoreEnabled
            ? sibilanceEnvelope_
            : voicingNoiseMix_ * smoothed_.sibilance;
        const float noiseAmount = smoothed_.carrierNoise * 0.65f
            + pureNoise + measuredSibilance * 0.58f;
        float carrierActivity = 0.0f;
        for (const auto& voice : carrierVoices_) {
            carrierActivity = std::max(carrierActivity, voice.level);
        }
        // Noise is a carrier texture, not a free-running generator. Keep it
        // attached to the same click-safe carrier envelope as pitched voices
        // so an open filter bank cannot make sound with no note/input.
        carrierActivity = clamp(carrierActivity * 2.0f, 0.0f, 1.0f);
        internalCarrierActivity_ = carrierActivity;
        const float externalMix = externalCarrierAvailable
            ? smoothed_.externalCarrierMix : 0.0f;
        if (smoothed_.stereoMode != AcapellaResonatorStereoMode::Spread) {
            const float externalMid = 0.5f * (externalLeft + externalRight);
            externalLeft = externalMid;
            externalRight = externalMid;
        }
        const float internalLeft = left * 0.38f;
        const float internalRight = right * 0.38f;
        const float tonalLeft = lerp(
            internalLeft, externalLeft * externalCarrierGain_, externalMix);
        const float tonalRight = lerp(
            internalRight, externalRight * externalCarrierGain_, externalMix);
        const float externalActivity = externalCarrierAvailable
            ? clamp(std::max(std::abs(externalLeft), std::abs(externalRight))
                    * 4.0f, 0.0f, 1.0f)
            : 0.0f;
        // The modulator decides the noise/tonal balance, but it must not be
        // able to create a free-running carrier. In the v5 internal-carrier
        // route, both tonal and unvoiced excitation require a MIDI carrier
        // envelope. The compatibility external-carrier route may instead use
        // the actual external carrier's activity.
        const float noiseActivity = externalCarrierAvailable
            ? std::max(carrierActivity, externalActivity)
            : carrierActivity;
        const float tonalGain = smoothed_.voicedLevel
            * std::sqrt(std::max(0.0f, 1.0f - voicingNoiseMix_));
        const float noiseGain = smoothed_.unvoicedLevel
            * std::sqrt(std::max(0.0f, voicingNoiseMix_));
        left = tonalLeft * tonalGain
            + spreadNoiseLeft * noiseAmount * noiseGain * noiseActivity;
        right = tonalRight * tonalGain
            + spreadNoiseRight * noiseAmount * noiseGain * noiseActivity;

        carrierLowLeft_ += (left - carrierLowLeft_) * carrierToneCoefficient_;
        carrierLowRight_ += (right - carrierLowRight_)
            * carrierToneCoefficient_;
        if (smoothed_.carrierColor < 0.0f) {
            left = lerp(left, carrierLowLeft_, -smoothed_.carrierColor);
            right = lerp(right, carrierLowRight_, -smoothed_.carrierColor);
        } else {
            left += (left - carrierLowLeft_) * smoothed_.carrierColor * 0.78f;
            right += (right - carrierLowRight_)
                * smoothed_.carrierColor * 0.78f;
        }
        left = clamp(left, -2.0f, 2.0f);
        right = clamp(right, -2.0f, 2.0f);
        // Bank Mix=0 exposes the carrier that actually excites the synthesis
        // filters, including voiced/unvoiced balance, noise texture, and
        // color. Returning the pre-texture tonal oscillator here made the dry
        // side disagree with every visible carrier control.
        dryCarrierLeft = left;
        dryCarrierRight = right;
    }

    void resetSignalState()
    {
        for (auto& filter : analysisLeft_) filter.reset();
        for (auto& filter : analysisRight_) filter.reset();
        for (auto& filter : analysisLeftSecond_) filter.reset();
        for (auto& filter : analysisRightSecond_) filter.reset();
        for (auto& filter : analysisLeftThird_) filter.reset();
        for (auto& filter : analysisRightThird_) filter.reset();
        for (auto& filter : analysisLeftFourth_) filter.reset();
        for (auto& filter : analysisRightFourth_) filter.reset();
        for (auto& filter : synthesisLeft_) filter.reset();
        for (auto& filter : synthesisRight_) filter.reset();
        analysisEnvelope_.fill(0.0f);
        liveEnvelope_.fill(0.0f);
        blurredEnvelope_.fill(0.0f);
        frozenEnvelope_.fill(0.0f);
        captureFromEnvelope_.fill(0.0f);
        mappedEnvelope_.fill(0.0f);
        smoothedPhonemeTarget_.fill(0.0f);
        carrierLowLeft_ = 0.0f;
        carrierLowRight_ = 0.0f;
        noiseLowLeft_ = 0.0f;
        noiseLowRight_ = 0.0f;
        analysisLevel_ = 0.0f;
        modulatorDetector_ = 0.0f;
        modulatorGateTarget_ = 0.0f;
        modulatorGateGain_ = 0.0f;
        modulatorGateOpen_ = false;
        pitchTrackerBuffer_.fill(0.0f);
        pitchDifference_.fill(1.0f);
        pitchTrackerWrite_ = 0u;
        pitchTrackerCount_ = 0u;
        pitchTrackerDecimationCounter_ = 0u;
        pitchTrackerHopCounter_ = 0u;
        pitchWorkLag_ = 0u;
        pitchWorkMinimumLag_ = 0u;
        pitchWorkMaximumLag_ = 0u;
        pitchTrackerDc_ = 0.0f;
        pitchTrackerLow_ = 0.0f;
        pitchTrackerLeftLevel_ = 0.0f;
        pitchTrackerRightLevel_ = 0.0f;
        pitchTrackerUseRight_ = false;
        detectedPitchHz_ = 146.832f;
        detectedPitchConfidence_ = 0.0f;
        pitchHoldSamplesRemaining_ = 0u;
        pitchHoldLatched_ = false;
        pitchCarrierActive_ = false;
        externalMicModeGain_ = params_.modulatorSource
                == AcapellaResonatorModulatorSource::ExternalMic
            ? 1.0f : 0.0f;
        sibilanceEnvelope_ = 0.0f;
        wetOutputLeft_ = 0.0f;
        wetOutputRight_ = 0.0f;
        previousWetLeft_ = 0.0f;
        previousWetRight_ = 0.0f;
        previousExternalOutputLeft_ = 0.0f;
        previousExternalOutputRight_ = 0.0f;
        frozenBlend_ = 0.0f;
        frozenValid_ = false;
    }

    float sampleRate_ = 48000.0f;
    bool prepared_ = false;
    AcapellaResonatorParams params_ {};
    AcapellaResonatorParams smoothed_ {};
    AcapellaResonatorGesture gesture_ {};
    bool lastGestureValid_ = false;
    std::array<float, kAcapellaResonatorBands> centerFrequencies_ {};
    std::array<acapella_resonator_detail::TptBandpass,
        kAcapellaResonatorBands> analysisLeft_ {};
    std::array<acapella_resonator_detail::TptBandpass,
        kAcapellaResonatorBands> analysisRight_ {};
    std::array<acapella_resonator_detail::TptBandpass,
        kAcapellaResonatorBands> analysisLeftSecond_ {};
    std::array<acapella_resonator_detail::TptBandpass,
        kAcapellaResonatorBands> analysisRightSecond_ {};
    std::array<acapella_resonator_detail::TptBandpass,
        kAcapellaResonatorBands> analysisLeftThird_ {};
    std::array<acapella_resonator_detail::TptBandpass,
        kAcapellaResonatorBands> analysisRightThird_ {};
    std::array<acapella_resonator_detail::TptBandpass,
        kAcapellaResonatorBands> analysisLeftFourth_ {};
    std::array<acapella_resonator_detail::TptBandpass,
        kAcapellaResonatorBands> analysisRightFourth_ {};
    std::array<acapella_resonator_detail::TptBandpass,
        kAcapellaResonatorBands> synthesisLeft_ {};
    std::array<acapella_resonator_detail::TptBandpass,
        kAcapellaResonatorBands> synthesisRight_ {};
    std::array<float, kAcapellaResonatorBands> analysisEnvelope_ {};
    std::array<float, kAcapellaResonatorBands> liveEnvelope_ {};
    std::array<float, kAcapellaResonatorBands> blurredEnvelope_ {};
    std::array<float, kAcapellaResonatorBands> frozenEnvelope_ {};
    std::array<float, kAcapellaResonatorBands> captureFromEnvelope_ {};
    std::array<float, kAcapellaResonatorBands> mappedEnvelope_ {};
    std::array<float, kAcapellaResonatorBands> mappedTarget_ {};
    std::array<float, kAcapellaResonatorBands> coupledEnvelope_ {};
    std::array<float, kAcapellaResonatorBands> activeBandGain_ {};
    std::array<float, kAcapellaResonatorBands> analysisMeters_ {};
    std::array<float, kAcapellaResonatorBands> synthesisMeters_ {};
    std::array<float, kAcapellaResonatorBands> phonemeTarget_ {};
    std::array<float, kAcapellaResonatorBands> smoothedPhonemeTarget_ {};
    std::array<float, kAcapellaResonatorBands> bandAttackCoefficients_ {};
    std::array<float, kAcapellaResonatorBands> bandReleaseCoefficients_ {};
    static constexpr uint32_t kPitchTrackerBufferSize = 1024u;
    static constexpr uint32_t kPitchTrackerWindow = 512u;
    static constexpr uint32_t kPitchTrackerDifferenceSize = 256u;
    std::array<float, kPitchTrackerBufferSize> pitchTrackerBuffer_ {};
    std::array<float, kPitchTrackerBufferSize> pitchAnalysisWindow_ {};
    std::array<float, kPitchTrackerDifferenceSize> pitchDifference_ {};
    std::array<CarrierVoice, kAcapellaResonatorMaxVoices> carrierVoices_ {};
    float parameterCoefficient_ = 0.001f;
    float carrierAttackCoefficient_ = 0.004f;
    float carrierReleaseCoefficient_ = 0.004f;
    float carrierFrequencyCoefficient_ = 0.003f;
    float activityReleaseCoefficient_ = 0.0001f;
    float wetOutputCoefficient_ = 0.5f;
    float analysisLevelAttackCoefficient_ = 0.004f;
    float analysisLevelReleaseCoefficient_ = 0.0001f;
    float modulatorDetectorAttackCoefficient_ = 0.004f;
    float modulatorDetectorReleaseCoefficient_ = 0.0005f;
    float modulatorGateAttackCoefficient_ = 0.02f;
    float modulatorGateReleaseCoefficient_ = 0.001f;
    float blurCoefficient_ = 1.0f;
    float freezeReleaseCoefficient_ = 0.0001f;
    float freezeCaptureCoefficient_ = 0.02f;
    float phonemeTargetAttackCoefficient_ = 0.01f;
    float phonemeTargetReleaseCoefficient_ = 0.001f;
    float noiseLowCoefficient_ = 0.8f;
    float sibilanceAttackCoefficient_ = 0.004f;
    float sibilanceReleaseCoefficient_ = 0.0005f;
    float carrierToneCoefficient_ = 0.25f;
    float carrierLfoEdgeCoefficient_ = 0.01f;
    float routeSmoothingCoefficient_ = 0.01f;
    float layoutSmoothingCoefficient_ = 0.01f;
    float articulationHighpassCoefficient_ = 0.25f;
    float meterCoefficient_ = 0.001f;
    float voicedTransitionCoefficient_ = 0.001f;
    float unvoicedTransitionCoefficient_ = 0.001f;
    float externalCarrierGain_ = 1.0f;
    float carrierLowLeft_ = 0.0f;
    float carrierLowRight_ = 0.0f;
    float noiseLowLeft_ = 0.0f;
    float noiseLowRight_ = 0.0f;
    float sibilanceEnvelope_ = 0.0f;
    float analysisLevel_ = 0.0f;
    float modulatorDetector_ = 0.0f;
    float modulatorGateTarget_ = 0.0f;
    float modulatorGateGain_ = 0.0f;
    float externalMicModeGain_ = 0.0f;
    float analysisSlopeMix_ = 0.0f;
    float pitchTrackerRate_ = 12000.0f;
    float pitchTrackerDcCoefficient_ = 0.01f;
    float pitchTrackerLowCoefficient_ = 0.1f;
    float pitchTrackerChannelCoefficient_ = 0.001f;
    float pitchTrackerDc_ = 0.0f;
    float pitchTrackerLow_ = 0.0f;
    float pitchTrackerLeftLevel_ = 0.0f;
    float pitchTrackerRightLevel_ = 0.0f;
    float detectedPitchHz_ = 146.832f;
    float detectedPitchConfidence_ = 0.0f;
    float activityEnvelope_ = 0.0f;
    float wetOutputLeft_ = 0.0f;
    float wetOutputRight_ = 0.0f;
    float previousWetLeft_ = 0.0f;
    float previousWetRight_ = 0.0f;
    float previousExternalOutputLeft_ = 0.0f;
    float previousExternalOutputRight_ = 0.0f;
    float articulationLowLeft_ = 0.0f;
    float articulationLowRight_ = 0.0f;
    float voicingNoiseMix_ = 0.0f;
    float noiseTarget_ = 0.0f;
    float carrierLfoPhase_ = 0.0f;
    float carrierLfoValue_ = 0.0f;
    float internalCarrierActivity_ = 0.0f;
    float tempoBpm_ = 120.0f;
    float frozenBlend_ = 0.0f;
    uint32_t noiseStateLeft_ = 0x6d2b79f5u;
    uint32_t noiseStateRight_ = 0x91e10da5u;
    uint32_t pendingCaptureSamples_ = 0u;
    uint32_t controlCounter_ = 0u;
    uint32_t pitchTrackerDecimation_ = 4u;
    uint32_t pitchTrackerDecimationCounter_ = 0u;
    uint32_t pitchTrackerHop_ = 120u;
    uint32_t pitchTrackerHopCounter_ = 0u;
    uint32_t pitchWorkLag_ = 0u;
    uint32_t pitchWorkMinimumLag_ = 0u;
    uint32_t pitchWorkMaximumLag_ = 0u;
    uint32_t pitchTrackerWrite_ = 0u;
    uint32_t pitchTrackerCount_ = 0u;
    uint32_t pitchHoldSamplesRemaining_ = 0u;
    bool modulatorGateOpen_ = false;
    bool pitchTrackerUseRight_ = false;
    bool pitchHoldLatched_ = false;
    bool pitchCarrierActive_ = false;
    uint64_t lastNoteIdentity_ = 0u;
    bool frozenValid_ = false;
    bool continuousCaptureArmed_ = false;
    bool tempoValid_ = false;
    bool layoutChangePending_ = false;
    AcapellaResonatorBandLayout pendingLayout_
        = AcapellaResonatorBandLayout::Speech22;
};

} // namespace s3g
