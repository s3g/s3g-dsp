#pragma once

#include "s3g_articulatory_waveguide.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace s3g {

// A real-time, sample-free vocal source. Reference recordings may be used to
// choose these parameters, but no waveform, spectral frame, impulse response,
// or learned reconstruction data is retained by the engine.
enum class AcapellaDelivery : uint8_t {
    Sung = 0u,
    Rap = 1u,
};

enum class AcapellaVowel : uint8_t {
    A = 0u,
    E,
    I,
    O,
    U,
    Schwa,
};

enum class AcapellaOnset : uint8_t {
    None = 0u,
    B,
    Ch,
    D,
    Dh,
    F,
    G,
    H,
    J,
    K,
    L,
    M,
    N,
    P,
    R,
    S,
    Sh,
    T,
    Th,
    V,
    W,
    Y,
    Z,
    Ng,
    Zh,
};

// Compact ARPAbet-like inventory used by the sample-free text front end.
// Diphthongs are compiled into two timed vowel targets, which lets the tract
// move continuously instead of treating them as static vowel labels.
enum class AcapellaPhoneme : uint8_t {
    Silence = 0u,
    IY, IH, EH, AE, AA, AO, UH, UW, AH, AX, ER,
    P, B, T, D, K, G,
    F, V, TH, DH, S, Z, SH, ZH, HH,
    CH, JH,
    M, N, NG,
    L, R, W, Y,
};

constexpr uint32_t kAcapellaPhonemeCount = 36u;

inline bool acapellaPhonemeIsVowel(AcapellaPhoneme phoneme)
{
    return phoneme >= AcapellaPhoneme::IY
        && phoneme <= AcapellaPhoneme::ER;
}

enum class AcapellaSourcePreset : uint8_t {
    NeutralSung = 0u,
    RhythmicRap,
    AirySung,
    PressedLead,
    HarshScream,
    DeathGrowl,
};

enum class AcapellaGestureSequence : uint8_t {
    Off = 0u,
    VowelOrbit,
    DeathChant,
    ScreamArc,
    RapGrid,
    Text,
};

constexpr uint32_t kAcapellaGestureSequenceCount = 6u;

enum class AcapellaGestureSync : uint8_t {
    Free = 0u,
    Note,
    Transport,
};

constexpr uint32_t kAcapellaGestureSyncCount = 3u;

enum class AcapellaGestureDivision : uint8_t {
    ThirtySecond = 0u,
    SixteenthTriplet,
    Sixteenth,
    SixteenthDotted,
    EighthTriplet,
    Eighth,
    EighthDotted,
    QuarterTriplet,
    Quarter,
    QuarterDotted,
    Half,
    Whole,
};

constexpr uint32_t kAcapellaGestureDivisionCount = 12u;
constexpr uint32_t kAcapellaTextGestureCapacity = 96u;

inline float acapellaGestureDivisionBeats(AcapellaGestureDivision division)
{
    switch (division) {
    case AcapellaGestureDivision::ThirtySecond: return 0.125f;
    case AcapellaGestureDivision::SixteenthTriplet: return 1.0f / 6.0f;
    case AcapellaGestureDivision::Sixteenth: return 0.25f;
    case AcapellaGestureDivision::SixteenthDotted: return 0.375f;
    case AcapellaGestureDivision::EighthTriplet: return 1.0f / 3.0f;
    case AcapellaGestureDivision::Eighth: return 0.5f;
    case AcapellaGestureDivision::EighthDotted: return 0.75f;
    case AcapellaGestureDivision::QuarterTriplet: return 2.0f / 3.0f;
    case AcapellaGestureDivision::Quarter: return 1.0f;
    case AcapellaGestureDivision::QuarterDotted: return 1.5f;
    case AcapellaGestureDivision::Half: return 2.0f;
    case AcapellaGestureDivision::Whole: return 4.0f;
    }
    return 0.5f;
}

struct AcapellaGestureStep {
    float durationScale = 1.0f;
    float amplitude = 1.0f;
    AcapellaPhoneme phoneme = AcapellaPhoneme::AX;
    uint8_t stress = 0u;
    uint8_t flags = 0u;
    uint8_t reserved = 0u;

    constexpr AcapellaGestureStep() = default;
    constexpr AcapellaGestureStep(AcapellaPhoneme value,
        float duration, float gain = 1.0f, uint8_t lexicalStress = 0u,
        uint8_t boundaryFlags = 0u)
        : durationScale(duration), amplitude(gain), phoneme(value),
          stress(lexicalStress), flags(boundaryFlags) {}
};

constexpr uint8_t kAcapellaSyllableStart = 1u << 0u;
constexpr uint8_t kAcapellaWordStart = 1u << 1u;
constexpr uint8_t kAcapellaWordEnd = 1u << 2u;
// Explicit score rest entered with one or more vertical bars. Its duration
// scale is measured in complete phrase divisions rather than the very short
// automatic boundary gap.
constexpr uint8_t kAcapellaForcedRest = 1u << 3u;
// The word's pronunciation was selected by the bounded text-context pass.
// This is score metadata only; the renderer consumes the resulting phonemes.
constexpr uint8_t kAcapellaContextualPronunciation = 1u << 4u;

struct AcapellaGestureProgram {
    std::array<AcapellaGestureStep, kAcapellaTextGestureCapacity> steps {};
    uint32_t count = 0u;
    uint32_t revision = 0u;
    uint32_t wordCount = 0u;
    bool truncated = false;
};

struct AcapellaVoiceProfile {
    // Values above 1.0 model a longer tract and therefore lower formants.
    float tractScale = 1.0f;
    float breath = 0.18f;
    float roughness = 0.08f;
    float brightness = 0.48f;
    float chest = 0.14f;
    float nasal = 0.08f;
    float openQuotient = 0.56f;
    // Nonlinear vocal-fold collision and supraglottal turbulence.
    float harshness = 0.0f;
    // Irregular f/2 and f/3 ventricular/false-fold components.
    float falseFold = 0.0f;
    // Narrow epilaryngeal reinforcement used by screams and growls.
    float throat = 0.0f;
};

struct AcapellaSourceParams {
    AcapellaDelivery delivery = AcapellaDelivery::Sung;
    AcapellaVoiceProfile voice;
    float articulation = 0.72f;
    float consonantStrength = 0.78f;
    float intensity = 0.78f;
    float vibratoRateHz = 5.2f;
    float vibratoDepthCents = 24.0f;
    float pitchDriftCents = 5.0f;
    float glideMs = 32.0f;
    float attackMs = 12.0f;
    float releaseMs = 85.0f;
    // Blend from the tract-filtered glottal model toward an independent
    // additive harmonic/formant model. The latter also supports fresh onsets.
    float hybridBlend = 0.16f;
    // Raised-cosine guard applied only when a voice starts from silence.
    float onsetGuardMs = 14.0f;
    // Blend the legacy parallel formants toward the bidirectional oral/nasal
    // tube model, and control the continuity of its articulator trajectories.
    float waveguideBlend = 0.48f;
    float coarticulation = 0.68f;
    // Preserves phoneme contrast while extreme excitation and post effects
    // remain active. This is an acoustic control, not a text-parser mode.
    float intelligibility = 0.78f;
    AcapellaGestureSequence gestureSequence = AcapellaGestureSequence::Off;
    float gestureRateHz = 5.0f;
    float gestureDepth = 1.0f;
    bool gestureLoop = true;
    AcapellaGestureSync gestureSync = AcapellaGestureSync::Free;
    AcapellaGestureDivision gestureDivision =
        AcapellaGestureDivision::Eighth;
    // Output-continuity ramp used when a monophonic note is stolen.
    float retriggerMs = 6.0f;
    float onsetScoopSemitones = 0.45f;
    float rapDeclinationSemitones = 1.15f;
    uint32_t randomSeed = 0x564f5820u;
};

struct AcapellaSyllable {
    AcapellaVowel vowel = AcapellaVowel::Schwa;
    AcapellaOnset onset = AcapellaOnset::None;
    float frequencyHz = 146.83f;
    float velocity = 0.80f;
    // A prosody horizon, not a sample length. The note remains held until
    // release() so a MIDI host may sustain it for an arbitrary duration.
    float durationMs = 280.0f;
};

inline float acapellaFiniteOr(float value, float fallback)
{
    return std::isfinite(value) ? value : fallback;
}

inline AcapellaSourceParams sanitizeAcapellaSourceParams(
    AcapellaSourceParams params)
{
    params.voice.tractScale = clamp(
        acapellaFiniteOr(params.voice.tractScale, 1.0f), 0.70f, 1.35f);
    params.voice.breath = clamp(
        acapellaFiniteOr(params.voice.breath, 0.18f), 0.0f, 1.0f);
    params.voice.roughness = clamp(
        acapellaFiniteOr(params.voice.roughness, 0.08f), 0.0f, 1.0f);
    params.voice.brightness = clamp(
        acapellaFiniteOr(params.voice.brightness, 0.48f), 0.0f, 1.0f);
    params.voice.chest = clamp(
        acapellaFiniteOr(params.voice.chest, 0.14f), 0.0f, 1.0f);
    params.voice.nasal = clamp(
        acapellaFiniteOr(params.voice.nasal, 0.08f), 0.0f, 1.0f);
    params.voice.openQuotient = clamp(
        acapellaFiniteOr(params.voice.openQuotient, 0.56f), 0.38f, 0.78f);
    params.voice.harshness = clamp(
        acapellaFiniteOr(params.voice.harshness, 0.0f), 0.0f, 1.0f);
    params.voice.falseFold = clamp(
        acapellaFiniteOr(params.voice.falseFold, 0.0f), 0.0f, 1.0f);
    params.voice.throat = clamp(
        acapellaFiniteOr(params.voice.throat, 0.0f), 0.0f, 1.0f);
    params.articulation = clamp(
        acapellaFiniteOr(params.articulation, 0.72f), 0.0f, 1.0f);
    params.consonantStrength = clamp(
        acapellaFiniteOr(params.consonantStrength, 0.78f), 0.0f, 1.0f);
    params.intensity = clamp(
        acapellaFiniteOr(params.intensity, 0.78f), 0.0f, 1.0f);
    params.vibratoRateHz = clamp(
        acapellaFiniteOr(params.vibratoRateHz, 5.2f), 0.05f, 10.0f);
    params.vibratoDepthCents = clamp(
        acapellaFiniteOr(params.vibratoDepthCents, 24.0f), 0.0f, 180.0f);
    params.pitchDriftCents = clamp(
        acapellaFiniteOr(params.pitchDriftCents, 5.0f), 0.0f, 80.0f);
    params.glideMs = clamp(
        acapellaFiniteOr(params.glideMs, 32.0f), 0.0f, 500.0f);
    params.attackMs = clamp(
        acapellaFiniteOr(params.attackMs, 12.0f), 0.25f, 500.0f);
    params.releaseMs = clamp(
        acapellaFiniteOr(params.releaseMs, 85.0f), 2.0f, 3000.0f);
    params.hybridBlend = clamp(
        acapellaFiniteOr(params.hybridBlend, 0.16f), 0.0f, 1.0f);
    params.onsetGuardMs = clamp(
        acapellaFiniteOr(params.onsetGuardMs, 14.0f), 2.0f, 80.0f);
    params.waveguideBlend = clamp(
        acapellaFiniteOr(params.waveguideBlend, 0.48f), 0.0f, 1.0f);
    params.coarticulation = clamp(
        acapellaFiniteOr(params.coarticulation, 0.68f), 0.0f, 1.0f);
    params.intelligibility = clamp(
        acapellaFiniteOr(params.intelligibility, 0.78f), 0.0f, 1.0f);
    if (static_cast<uint32_t>(params.gestureSequence)
        >= kAcapellaGestureSequenceCount) {
        params.gestureSequence = AcapellaGestureSequence::Off;
    }
    params.gestureRateHz = clamp(
        acapellaFiniteOr(params.gestureRateHz, 5.0f), 0.5f, 20.0f);
    params.gestureDepth = clamp(
        acapellaFiniteOr(params.gestureDepth, 1.0f), 0.0f, 1.0f);
    if (static_cast<uint32_t>(params.gestureSync)
        >= kAcapellaGestureSyncCount) {
        params.gestureSync = AcapellaGestureSync::Free;
    }
    if (static_cast<uint32_t>(params.gestureDivision)
        >= kAcapellaGestureDivisionCount) {
        params.gestureDivision = AcapellaGestureDivision::Eighth;
    }
    params.retriggerMs = clamp(
        acapellaFiniteOr(params.retriggerMs, 6.0f), 0.5f, 30.0f);
    params.onsetScoopSemitones = clamp(
        acapellaFiniteOr(params.onsetScoopSemitones, 0.45f), -4.0f, 4.0f);
    params.rapDeclinationSemitones = clamp(
        acapellaFiniteOr(params.rapDeclinationSemitones, 1.15f), -4.0f, 6.0f);
    if (params.randomSeed == 0u) params.randomSeed = 1u;
    return params;
}

inline AcapellaSourceParams acapellaSourcePreset(AcapellaSourcePreset preset)
{
    AcapellaSourceParams params;
    switch (preset) {
    case AcapellaSourcePreset::RhythmicRap:
        params.delivery = AcapellaDelivery::Rap;
        params.voice.breath = 0.27f;
        params.voice.roughness = 0.17f;
        params.voice.brightness = 0.52f;
        params.voice.chest = 0.22f;
        params.articulation = 0.90f;
        params.consonantStrength = 0.92f;
        params.vibratoDepthCents = 5.0f;
        params.pitchDriftCents = 11.0f;
        params.glideMs = 15.0f;
        params.attackMs = 4.0f;
        params.releaseMs = 48.0f;
        params.hybridBlend = 0.10f;
        params.onsetGuardMs = 7.0f;
        params.waveguideBlend = 0.50f;
        params.coarticulation = 0.82f;
        params.intelligibility = 0.92f;
        params.onsetScoopSemitones = 0.10f;
        params.rapDeclinationSemitones = 1.45f;
        break;
    case AcapellaSourcePreset::AirySung:
        params.voice.tractScale = 0.94f;
        params.voice.breath = 0.40f;
        params.voice.roughness = 0.07f;
        params.voice.brightness = 0.43f;
        params.voice.chest = 0.07f;
        params.voice.openQuotient = 0.67f;
        params.vibratoDepthCents = 31.0f;
        params.attackMs = 20.0f;
        params.releaseMs = 135.0f;
        params.hybridBlend = 0.32f;
        params.onsetGuardMs = 22.0f;
        params.waveguideBlend = 0.42f;
        params.coarticulation = 0.62f;
        params.intelligibility = 0.84f;
        break;
    case AcapellaSourcePreset::PressedLead:
        params.voice.tractScale = 1.04f;
        params.voice.breath = 0.08f;
        params.voice.roughness = 0.20f;
        params.voice.brightness = 0.62f;
        params.voice.chest = 0.28f;
        params.voice.nasal = 0.13f;
        params.voice.openQuotient = 0.46f;
        params.intensity = 0.86f;
        params.vibratoDepthCents = 18.0f;
        params.attackMs = 7.0f;
        params.releaseMs = 70.0f;
        params.hybridBlend = 0.16f;
        params.onsetGuardMs = 11.0f;
        params.waveguideBlend = 0.54f;
        params.coarticulation = 0.76f;
        params.intelligibility = 0.82f;
        break;
    case AcapellaSourcePreset::HarshScream:
        params.voice.tractScale = 0.94f;
        params.voice.breath = 0.24f;
        params.voice.roughness = 0.44f;
        params.voice.brightness = 0.80f;
        params.voice.chest = 0.30f;
        params.voice.nasal = 0.10f;
        params.voice.openQuotient = 0.43f;
        params.voice.harshness = 0.84f;
        params.voice.falseFold = 0.36f;
        params.voice.throat = 0.78f;
        params.articulation = 0.82f;
        params.consonantStrength = 0.76f;
        params.intensity = 0.91f;
        params.vibratoDepthCents = 9.0f;
        params.pitchDriftCents = 18.0f;
        params.glideMs = 18.0f;
        params.attackMs = 8.0f;
        params.releaseMs = 95.0f;
        params.hybridBlend = 0.24f;
        params.onsetGuardMs = 16.0f;
        params.waveguideBlend = 0.58f;
        params.coarticulation = 0.78f;
        params.intelligibility = 0.80f;
        params.onsetScoopSemitones = 0.18f;
        break;
    case AcapellaSourcePreset::DeathGrowl:
        params.voice.tractScale = 1.24f;
        params.voice.breath = 0.18f;
        params.voice.roughness = 0.67f;
        params.voice.brightness = 0.36f;
        params.voice.chest = 0.82f;
        params.voice.nasal = 0.16f;
        params.voice.openQuotient = 0.44f;
        params.voice.harshness = 0.94f;
        params.voice.falseFold = 0.90f;
        params.voice.throat = 0.91f;
        params.articulation = 0.70f;
        params.consonantStrength = 0.70f;
        params.intensity = 0.94f;
        params.vibratoDepthCents = 3.0f;
        params.pitchDriftCents = 27.0f;
        params.glideMs = 24.0f;
        params.attackMs = 10.0f;
        params.releaseMs = 125.0f;
        params.hybridBlend = 0.22f;
        params.onsetGuardMs = 18.0f;
        params.waveguideBlend = 0.66f;
        params.coarticulation = 0.66f;
        params.intelligibility = 0.84f;
        params.onsetScoopSemitones = -0.12f;
        break;
    case AcapellaSourcePreset::NeutralSung:
    default:
        break;
    }
    return sanitizeAcapellaSourceParams(params);
}

namespace acapella_source_detail {

inline float smoothstep(float edge0, float edge1, float value)
{
    if (edge1 <= edge0) return value >= edge1 ? 1.0f : 0.0f;
    const float x = clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

inline float timeCoefficient(float milliseconds, float sampleRate)
{
    const float samples = std::max(1.0f, milliseconds * 0.001f * sampleRate);
    return 1.0f - std::exp(-1.0f / samples);
}

inline float envelopeCoefficient(float milliseconds, float sampleRate)
{
    const float samples = std::max(1.0f, milliseconds * 0.001f * sampleRate);
    // Reach -60 dB of the remaining distance in the labelled envelope time.
    return 1.0f - std::exp(-6.90775527898f / samples);
}

struct Random {
    uint32_t state = 1u;

    void reset(uint32_t seed) { state = seed == 0u ? 1u : seed; }

    uint32_t next()
    {
        state ^= state << 13u;
        state ^= state >> 17u;
        state ^= state << 5u;
        return state;
    }

    float bipolar()
    {
        return static_cast<float>(next() & 0x00ffffffu) / 8388607.5f - 1.0f;
    }
};

struct Biquad {
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
    float z1 = 0.0f;
    float z2 = 0.0f;

    void reset()
    {
        z1 = 0.0f;
        z2 = 0.0f;
    }

    void setBandpass(float frequencyHz, float bandwidthHz, float sampleRate)
    {
        frequencyHz = clamp(frequencyHz, 30.0f, sampleRate * 0.45f);
        bandwidthHz = clamp(bandwidthHz, 25.0f, sampleRate * 0.35f);
        const float q = clamp(frequencyHz / bandwidthHz, 0.25f, 30.0f);
        const float omega = 2.0f * kPi * frequencyHz / sampleRate;
        const float alpha = std::sin(omega) / (2.0f * q);
        const float inverseA0 = 1.0f / (1.0f + alpha);
        b0 = alpha * inverseA0;
        b1 = 0.0f;
        b2 = -b0;
        a1 = -2.0f * std::cos(omega) * inverseA0;
        a2 = (1.0f - alpha) * inverseA0;
    }

    float process(float input)
    {
        const float output = b0 * input + z1;
        z1 = b1 * input - a1 * output + z2;
        z2 = b2 * input - a2 * output;
        z1 = flushDenormal(z1);
        z2 = flushDenormal(z2);
        return output;
    }
};

struct FormantShape {
    std::array<float, 5u> frequency;
    std::array<float, 5u> bandwidth;
};

struct GestureSequenceView {
    const AcapellaGestureStep* steps = nullptr;
    uint32_t count = 0u;
};

inline GestureSequenceView gestureSequenceView(
    AcapellaGestureSequence sequence)
{
    static constexpr std::array<AcapellaGestureStep, 12u> vowelOrbit {{
        { AcapellaPhoneme::M, 0.32f }, { AcapellaPhoneme::AA, 1.20f },
        { AcapellaPhoneme::Y, 0.28f }, { AcapellaPhoneme::EH, 0.85f },
        { AcapellaPhoneme::Y, 0.28f }, { AcapellaPhoneme::IY, 0.85f },
        { AcapellaPhoneme::W, 0.30f }, { AcapellaPhoneme::AO, 1.10f },
        { AcapellaPhoneme::W, 0.30f }, { AcapellaPhoneme::UW, 1.10f },
        { AcapellaPhoneme::R, 0.32f }, { AcapellaPhoneme::AX, 0.90f },
    }};
    static constexpr std::array<AcapellaGestureStep, 10u> deathChant {{
        { AcapellaPhoneme::HH, 0.38f }, { AcapellaPhoneme::AO, 1.35f },
        { AcapellaPhoneme::R, 0.34f }, { AcapellaPhoneme::UW, 0.95f },
        { AcapellaPhoneme::K, 0.25f }, { AcapellaPhoneme::AA, 0.82f },
        { AcapellaPhoneme::N, 0.38f }, { AcapellaPhoneme::AX, 1.10f },
        { AcapellaPhoneme::G, 0.26f }, { AcapellaPhoneme::AO, 0.90f },
    }};
    static constexpr std::array<AcapellaGestureStep, 10u> screamArc {{
        { AcapellaPhoneme::HH, 0.38f }, { AcapellaPhoneme::AA, 1.45f },
        { AcapellaPhoneme::Y, 0.28f }, { AcapellaPhoneme::EH, 0.78f },
        { AcapellaPhoneme::R, 0.34f }, { AcapellaPhoneme::IY, 0.82f },
        { AcapellaPhoneme::SH, 0.42f }, { AcapellaPhoneme::AA, 1.05f },
        { AcapellaPhoneme::V, 0.38f }, { AcapellaPhoneme::AO, 0.90f },
    }};
    static constexpr std::array<AcapellaGestureStep, 16u> rapGrid {{
        { AcapellaPhoneme::T, 0.22f }, { AcapellaPhoneme::AX, 0.55f },
        { AcapellaPhoneme::K, 0.24f }, { AcapellaPhoneme::AE, 0.72f },
        { AcapellaPhoneme::S, 0.36f }, { AcapellaPhoneme::IH, 0.52f },
        { AcapellaPhoneme::D, 0.24f }, { AcapellaPhoneme::AO, 0.68f },
        { AcapellaPhoneme::N, 0.32f }, { AcapellaPhoneme::UW, 0.58f },
        { AcapellaPhoneme::R, 0.32f }, { AcapellaPhoneme::EH, 0.82f },
        { AcapellaPhoneme::P, 0.22f }, { AcapellaPhoneme::AA, 0.50f },
        { AcapellaPhoneme::Z, 0.34f }, { AcapellaPhoneme::AX, 0.63f },
    }};
    switch (sequence) {
    case AcapellaGestureSequence::VowelOrbit:
        return { vowelOrbit.data(), static_cast<uint32_t>(vowelOrbit.size()) };
    case AcapellaGestureSequence::DeathChant:
        return { deathChant.data(), static_cast<uint32_t>(deathChant.size()) };
    case AcapellaGestureSequence::ScreamArc:
        return { screamArc.data(), static_cast<uint32_t>(screamArc.size()) };
    case AcapellaGestureSequence::RapGrid:
        return { rapGrid.data(), static_cast<uint32_t>(rapGrid.size()) };
    case AcapellaGestureSequence::Text:
    case AcapellaGestureSequence::Off:
    default:
        return {};
    }
}

inline FormantShape vowelShape(AcapellaVowel vowel)
{
    switch (vowel) {
    case AcapellaVowel::A:
        return { { 800.0f, 1200.0f, 2850.0f, 3800.0f, 4950.0f },
                 { 90.0f, 110.0f, 160.0f, 240.0f, 320.0f } };
    case AcapellaVowel::E:
        return { { 470.0f, 1900.0f, 2650.0f, 3500.0f, 4850.0f },
                 { 80.0f, 115.0f, 155.0f, 235.0f, 320.0f } };
    case AcapellaVowel::I:
        return { { 300.0f, 2350.0f, 3000.0f, 3700.0f, 4900.0f },
                 { 65.0f, 110.0f, 155.0f, 240.0f, 330.0f } };
    case AcapellaVowel::O:
        return { { 520.0f, 900.0f, 2600.0f, 3400.0f, 4700.0f },
                 { 80.0f, 100.0f, 155.0f, 235.0f, 320.0f } };
    case AcapellaVowel::U:
        return { { 350.0f, 780.0f, 2250.0f, 3300.0f, 4500.0f },
                 { 70.0f, 95.0f, 150.0f, 230.0f, 310.0f } };
    case AcapellaVowel::Schwa:
    default:
        return { { 520.0f, 1480.0f, 2500.0f, 3500.0f, 4700.0f },
                 { 95.0f, 130.0f, 175.0f, 250.0f, 340.0f } };
    }
}

inline FormantShape phonemeVowelShape(AcapellaPhoneme phoneme)
{
    switch (phoneme) {
    case AcapellaPhoneme::IY:
        return { { 270.0f, 2290.0f, 3010.0f, 3700.0f, 4900.0f },
                 { 60.0f, 105.0f, 150.0f, 235.0f, 325.0f } };
    case AcapellaPhoneme::IH:
        return { { 390.0f, 1990.0f, 2550.0f, 3500.0f, 4800.0f },
                 { 72.0f, 112.0f, 160.0f, 240.0f, 330.0f } };
    case AcapellaPhoneme::EH:
        return { { 530.0f, 1840.0f, 2480.0f, 3450.0f, 4780.0f },
                 { 78.0f, 118.0f, 165.0f, 245.0f, 335.0f } };
    case AcapellaPhoneme::AE:
        return { { 660.0f, 1720.0f, 2410.0f, 3400.0f, 4720.0f },
                 { 86.0f, 122.0f, 170.0f, 250.0f, 340.0f } };
    case AcapellaPhoneme::AA:
        return { { 730.0f, 1090.0f, 2440.0f, 3400.0f, 4680.0f },
                 { 92.0f, 112.0f, 165.0f, 245.0f, 330.0f } };
    case AcapellaPhoneme::AO:
        return { { 570.0f, 840.0f, 2410.0f, 3340.0f, 4620.0f },
                 { 84.0f, 105.0f, 160.0f, 240.0f, 325.0f } };
    case AcapellaPhoneme::UH:
        return { { 440.0f, 1020.0f, 2240.0f, 3280.0f, 4520.0f },
                 { 78.0f, 108.0f, 158.0f, 238.0f, 320.0f } };
    case AcapellaPhoneme::UW:
        return { { 300.0f, 870.0f, 2240.0f, 3260.0f, 4480.0f },
                 { 68.0f, 96.0f, 150.0f, 230.0f, 312.0f } };
    case AcapellaPhoneme::AH:
        return { { 640.0f, 1190.0f, 2390.0f, 3400.0f, 4680.0f },
                 { 92.0f, 120.0f, 170.0f, 248.0f, 335.0f } };
    case AcapellaPhoneme::ER:
        return { { 490.0f, 1350.0f, 1690.0f, 3300.0f, 4550.0f },
                 { 88.0f, 125.0f, 135.0f, 245.0f, 330.0f } };
    case AcapellaPhoneme::AX:
    default:
        return { { 500.0f, 1500.0f, 2480.0f, 3500.0f, 4700.0f },
                 { 96.0f, 132.0f, 178.0f, 252.0f, 342.0f } };
    }
}

inline AcapellaOnset phonemeOnset(AcapellaPhoneme phoneme)
{
    switch (phoneme) {
    case AcapellaPhoneme::P: return AcapellaOnset::P;
    case AcapellaPhoneme::B: return AcapellaOnset::B;
    case AcapellaPhoneme::T: return AcapellaOnset::T;
    case AcapellaPhoneme::D: return AcapellaOnset::D;
    case AcapellaPhoneme::K: return AcapellaOnset::K;
    case AcapellaPhoneme::G: return AcapellaOnset::G;
    case AcapellaPhoneme::F: return AcapellaOnset::F;
    case AcapellaPhoneme::V: return AcapellaOnset::V;
    case AcapellaPhoneme::TH: return AcapellaOnset::Th;
    case AcapellaPhoneme::DH: return AcapellaOnset::Dh;
    case AcapellaPhoneme::S: return AcapellaOnset::S;
    case AcapellaPhoneme::Z: return AcapellaOnset::Z;
    case AcapellaPhoneme::SH: return AcapellaOnset::Sh;
    case AcapellaPhoneme::ZH: return AcapellaOnset::Zh;
    case AcapellaPhoneme::HH: return AcapellaOnset::H;
    case AcapellaPhoneme::CH: return AcapellaOnset::Ch;
    case AcapellaPhoneme::JH: return AcapellaOnset::J;
    case AcapellaPhoneme::M: return AcapellaOnset::M;
    case AcapellaPhoneme::N: return AcapellaOnset::N;
    case AcapellaPhoneme::NG: return AcapellaOnset::Ng;
    case AcapellaPhoneme::L: return AcapellaOnset::L;
    case AcapellaPhoneme::R: return AcapellaOnset::R;
    case AcapellaPhoneme::W: return AcapellaOnset::W;
    case AcapellaPhoneme::Y: return AcapellaOnset::Y;
    default: return AcapellaOnset::None;
    }
}

inline float phonemeVoicing(AcapellaPhoneme phoneme)
{
    if (acapellaPhonemeIsVowel(phoneme)) return 1.0f;
    switch (phoneme) {
    case AcapellaPhoneme::B:
    case AcapellaPhoneme::D:
    case AcapellaPhoneme::G: return 0.42f;
    case AcapellaPhoneme::V:
    case AcapellaPhoneme::DH:
    case AcapellaPhoneme::Z:
    case AcapellaPhoneme::ZH:
    case AcapellaPhoneme::JH: return 0.58f;
    case AcapellaPhoneme::M:
    case AcapellaPhoneme::N:
    case AcapellaPhoneme::NG:
    case AcapellaPhoneme::L:
    case AcapellaPhoneme::R:
    case AcapellaPhoneme::W:
    case AcapellaPhoneme::Y: return 0.92f;
    case AcapellaPhoneme::Silence: return 0.0f;
    default: return 0.06f;
    }
}

enum class OnsetKind : uint8_t {
    None,
    Stop,
    VoicedStop,
    Fricative,
    VoicedFricative,
    Nasal,
    Liquid,
};

// Context within a continuous phoneme stream. Only an OnsetRelease opens
// fully into its following vowel; the other roles preserve the direction of
// articulation across clusters and word endings.
enum class PhonemeRole : uint8_t {
    Vowel,
    OnsetLead,
    OnsetRelease,
    Bridge,
    CodaStart,
    CodaContinue,
    Silence,
};

inline OnsetKind onsetKind(AcapellaOnset onset)
{
    switch (onset) {
    case AcapellaOnset::P:
    case AcapellaOnset::T:
    case AcapellaOnset::K:
    case AcapellaOnset::Ch:
        return OnsetKind::Stop;
    case AcapellaOnset::B:
    case AcapellaOnset::D:
    case AcapellaOnset::G:
    case AcapellaOnset::J:
        return OnsetKind::VoicedStop;
    case AcapellaOnset::F:
    case AcapellaOnset::H:
    case AcapellaOnset::S:
    case AcapellaOnset::Sh:
    case AcapellaOnset::Th:
        return OnsetKind::Fricative;
    case AcapellaOnset::Dh:
    case AcapellaOnset::V:
    case AcapellaOnset::Z:
    case AcapellaOnset::Zh:
        return OnsetKind::VoicedFricative;
    case AcapellaOnset::M:
    case AcapellaOnset::N:
    case AcapellaOnset::Ng:
        return OnsetKind::Nasal;
    case AcapellaOnset::L:
    case AcapellaOnset::R:
    case AcapellaOnset::W:
    case AcapellaOnset::Y:
        return OnsetKind::Liquid;
    case AcapellaOnset::None:
    default:
        return OnsetKind::None;
    }
}

inline std::array<float, 5u> onsetLocus(AcapellaOnset onset,
    const FormantShape& vowel)
{
    std::array<float, 5u> result = vowel.frequency;
    switch (onset) {
    case AcapellaOnset::B:
    case AcapellaOnset::P:
    case AcapellaOnset::M:
    case AcapellaOnset::W:
        result[0] = 300.0f; result[1] = 700.0f; result[2] = 2450.0f;
        break;
    case AcapellaOnset::D:
    case AcapellaOnset::T:
    case AcapellaOnset::N:
    case AcapellaOnset::S:
    case AcapellaOnset::Z:
    case AcapellaOnset::Ch:
    case AcapellaOnset::J:
        result[0] = 320.0f; result[1] = 1800.0f; result[2] = 2750.0f;
        break;
    case AcapellaOnset::G:
    case AcapellaOnset::K:
    case AcapellaOnset::Ng: {
        const float pinch = 0.55f * vowel.frequency[1]
            + 0.45f * vowel.frequency[2];
        result[0] = 340.0f; result[1] = pinch; result[2] = pinch + 180.0f;
        break;
    }
    case AcapellaOnset::R:
        result[0] = 380.0f; result[1] = 1250.0f; result[2] = 1650.0f;
        break;
    case AcapellaOnset::L:
        result[0] = 360.0f; result[1] = 1050.0f; result[2] = 2600.0f;
        break;
    case AcapellaOnset::Y:
        result[0] = 270.0f; result[1] = 2250.0f; result[2] = 3000.0f;
        break;
    case AcapellaOnset::F:
    case AcapellaOnset::H:
    case AcapellaOnset::Sh:
    case AcapellaOnset::Zh:
    case AcapellaOnset::Th:
    case AcapellaOnset::V:
    case AcapellaOnset::Dh:
        result[0] *= 0.70f; result[1] *= 0.90f;
        break;
    default:
        break;
    }
    return result;
}

inline float fricativeCenter(AcapellaOnset onset)
{
    switch (onset) {
    case AcapellaOnset::P:
    case AcapellaOnset::B: return 1150.0f;
    case AcapellaOnset::T:
    case AcapellaOnset::D: return 3600.0f;
    case AcapellaOnset::K:
    case AcapellaOnset::G: return 2150.0f;
    case AcapellaOnset::S:
    case AcapellaOnset::Z: return 6800.0f;
    case AcapellaOnset::Sh:
    case AcapellaOnset::Zh:
    case AcapellaOnset::Ch:
    case AcapellaOnset::J: return 3900.0f;
    case AcapellaOnset::F:
    case AcapellaOnset::V:
    case AcapellaOnset::Th:
    case AcapellaOnset::Dh: return 2300.0f;
    case AcapellaOnset::H: return 3200.0f;
    default: return 3000.0f;
    }
}

inline ArticulatoryGesture vowelGesture(AcapellaVowel vowel)
{
    ArticulatoryGesture gesture;
    switch (vowel) {
    case AcapellaVowel::A:
        gesture.tonguePosition = 0.30f;
        gesture.tongueConstriction = 0.18f;
        gesture.jawOpen = 0.94f;
        gesture.lipRound = 0.02f;
        break;
    case AcapellaVowel::E:
        gesture.tonguePosition = 0.70f;
        gesture.tongueConstriction = 0.48f;
        gesture.jawOpen = 0.54f;
        gesture.lipRound = 0.02f;
        break;
    case AcapellaVowel::I:
        gesture.tonguePosition = 0.82f;
        gesture.tongueConstriction = 0.74f;
        gesture.jawOpen = 0.24f;
        gesture.lipRound = 0.0f;
        break;
    case AcapellaVowel::O:
        gesture.tonguePosition = 0.31f;
        gesture.tongueConstriction = 0.42f;
        gesture.jawOpen = 0.58f;
        gesture.lipRound = 0.72f;
        break;
    case AcapellaVowel::U:
        gesture.tonguePosition = 0.40f;
        gesture.tongueConstriction = 0.64f;
        gesture.jawOpen = 0.25f;
        gesture.lipRound = 0.94f;
        break;
    case AcapellaVowel::Schwa:
    default:
        break;
    }
    return gesture;
}

inline ArticulatoryGesture phonemeVowelGesture(AcapellaPhoneme phoneme)
{
    ArticulatoryGesture gesture;
    switch (phoneme) {
    case AcapellaPhoneme::IY:
        gesture.tonguePosition = 0.84f;
        gesture.tongueConstriction = 0.78f;
        gesture.jawOpen = 0.20f;
        break;
    case AcapellaPhoneme::IH:
        gesture.tonguePosition = 0.76f;
        gesture.tongueConstriction = 0.62f;
        gesture.jawOpen = 0.34f;
        break;
    case AcapellaPhoneme::EH:
        gesture.tonguePosition = 0.68f;
        gesture.tongueConstriction = 0.48f;
        gesture.jawOpen = 0.52f;
        break;
    case AcapellaPhoneme::AE:
        gesture.tonguePosition = 0.57f;
        gesture.tongueConstriction = 0.31f;
        gesture.jawOpen = 0.75f;
        break;
    case AcapellaPhoneme::AA:
        gesture.tonguePosition = 0.25f;
        gesture.tongueConstriction = 0.14f;
        gesture.jawOpen = 0.96f;
        break;
    case AcapellaPhoneme::AO:
        gesture.tonguePosition = 0.30f;
        gesture.tongueConstriction = 0.38f;
        gesture.jawOpen = 0.64f;
        gesture.lipRound = 0.52f;
        break;
    case AcapellaPhoneme::UH:
        gesture.tonguePosition = 0.43f;
        gesture.tongueConstriction = 0.52f;
        gesture.jawOpen = 0.37f;
        gesture.lipRound = 0.48f;
        break;
    case AcapellaPhoneme::UW:
        gesture.tonguePosition = 0.41f;
        gesture.tongueConstriction = 0.68f;
        gesture.jawOpen = 0.23f;
        gesture.lipRound = 0.96f;
        break;
    case AcapellaPhoneme::AH:
        gesture.tonguePosition = 0.37f;
        gesture.tongueConstriction = 0.24f;
        gesture.jawOpen = 0.72f;
        break;
    case AcapellaPhoneme::ER:
        gesture.tonguePosition = 0.58f;
        gesture.tongueConstriction = 0.56f;
        gesture.jawOpen = 0.42f;
        gesture.lipRound = 0.16f;
        break;
    case AcapellaPhoneme::AX:
    default:
        break;
    }
    return gesture;
}

inline float onsetClosurePosition(AcapellaOnset onset)
{
    switch (onset) {
    case AcapellaOnset::B:
    case AcapellaOnset::P:
    case AcapellaOnset::M:
    case AcapellaOnset::W:
        return 0.965f;
    case AcapellaOnset::G:
    case AcapellaOnset::K:
    case AcapellaOnset::Ng:
        return 0.56f;
    case AcapellaOnset::R:
        return 0.61f;
    case AcapellaOnset::F:
    case AcapellaOnset::V:
    case AcapellaOnset::Th:
    case AcapellaOnset::Dh:
        return 0.91f;
    case AcapellaOnset::Sh:
    case AcapellaOnset::Zh:
        return 0.71f;
    default:
        return 0.77f;
    }
}

inline ArticulatoryGesture blendVowelGesture(AcapellaVowel base,
    AcapellaVowel target, float amount)
{
    const auto a = vowelGesture(base);
    const auto b = vowelGesture(target);
    amount = clamp(amount, 0.0f, 1.0f);
    ArticulatoryGesture result;
    result.tonguePosition = lerp(a.tonguePosition, b.tonguePosition, amount);
    result.tongueConstriction = lerp(a.tongueConstriction,
        b.tongueConstriction, amount);
    result.jawOpen = lerp(a.jawOpen, b.jawOpen, amount);
    result.lipRound = lerp(a.lipRound, b.lipRound, amount);
    return result;
}

inline ArticulatoryGesture blendArticulatoryGestures(
    const ArticulatoryGesture& a, const ArticulatoryGesture& b, float amount)
{
    amount = clamp(amount, 0.0f, 1.0f);
    ArticulatoryGesture result;
    result.tonguePosition = lerp(a.tonguePosition, b.tonguePosition, amount);
    result.tongueConstriction = lerp(a.tongueConstriction,
        b.tongueConstriction, amount);
    result.jawOpen = lerp(a.jawOpen, b.jawOpen, amount);
    result.lipRound = lerp(a.lipRound, b.lipRound, amount);
    result.velumOpen = lerp(a.velumOpen, b.velumOpen, amount);
    result.oralClosure = lerp(a.oralClosure, b.oralClosure, amount);
    result.closurePosition = lerp(a.closurePosition,
        b.closurePosition, amount);
    result.tractScale = lerp(a.tractScale, b.tractScale, amount);
    result.coarticulation = lerp(a.coarticulation, b.coarticulation, amount);
    return sanitizeArticulatoryGesture(result);
}

inline ArticulatoryGesture applyOnsetGesture(ArticulatoryGesture gesture,
    AcapellaOnset onset, OnsetKind kind, float onsetProgress,
    float consonantStrength, const AcapellaVoiceProfile& voice,
    float coarticulation)
{
    const float consonantEnvelope = 1.0f
        - smoothstep(0.24f, 1.0f, onsetProgress);
    gesture.closurePosition = onsetClosurePosition(onset);
    gesture.tractScale = voice.tractScale;
    gesture.coarticulation = coarticulation;
    gesture.velumOpen = voice.nasal * 0.24f;
    switch (kind) {
    case OnsetKind::Stop:
    case OnsetKind::VoicedStop:
        gesture.oralClosure = consonantStrength
            * (1.0f - smoothstep(0.28f, 0.62f, onsetProgress));
        gesture.jawOpen *= 0.48f + 0.52f
            * smoothstep(0.30f, 0.78f, onsetProgress);
        break;
    case OnsetKind::Fricative:
    case OnsetKind::VoicedFricative:
        gesture.oralClosure = consonantStrength * consonantEnvelope
            * (onset == AcapellaOnset::H ? 0.10f : 0.76f);
        break;
    case OnsetKind::Nasal:
        gesture.oralClosure = consonantStrength * consonantEnvelope * 0.84f;
        gesture.velumOpen = std::max(gesture.velumOpen,
            consonantEnvelope * (0.72f + consonantStrength * 0.24f));
        break;
    case OnsetKind::Liquid:
        gesture.oralClosure = consonantStrength * consonantEnvelope * 0.28f;
        if (onset == AcapellaOnset::W) {
            gesture.lipRound = std::max(gesture.lipRound,
                consonantEnvelope * 0.82f);
        } else if (onset == AcapellaOnset::Y) {
            gesture.tonguePosition = lerp(gesture.tonguePosition, 0.84f,
                consonantEnvelope);
            gesture.tongueConstriction = std::max(
                gesture.tongueConstriction, consonantEnvelope * 0.64f);
        }
        break;
    case OnsetKind::None:
    default:
        break;
    }
    return sanitizeArticulatoryGesture(gesture);
}

inline ArticulatoryGesture vocalGesture(AcapellaVowel vowel,
    AcapellaOnset onset, OnsetKind kind, float onsetProgress,
    float consonantStrength, const AcapellaVoiceProfile& voice,
    float coarticulation)
{
    return applyOnsetGesture(vowelGesture(vowel), onset, kind,
        onsetProgress, consonantStrength, voice, coarticulation);
}

} // namespace acapella_source_detail

// Monophonic by design: this is a source voice, and fixed-capacity polyphony
// can be built by owning several instances. processFrame() performs no heap
// allocation, locking, file access, FFT, or sample-table lookup.
class AcapellaSourceSynth {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::isfinite(sampleRate)
            ? clamp(static_cast<float>(sampleRate), 8000.0f, 192000.0f)
            : 48000.0f;
        params_ = sanitizeAcapellaSourceParams(params_);
        waveguide_.prepare(sampleRate_);
        reset();
    }

    void reset()
    {
        phase_ = 0.0f;
        subPhase_ = 0.0f;
        thirdPhase_ = 0.0f;
        growlPhase_ = 0.0f;
        vibratoPhase_ = 0.0f;
        driftPhase_ = 0.0f;
        previousFlow_ = 0.0f;
        glottalLow1_ = 0.0f;
        glottalLow2_ = 0.0f;
        noiseLow_ = 0.0f;
        fricationSmooth_ = 0.0f;
        jitter_ = 0.0f;
        jitterTarget_ = 0.0f;
        amplitudeEnvelope_ = 0.0f;
        vibratoEnvelope_ = 0.0f;
        dcInput_ = 0.0f;
        dcOutput_ = 0.0f;
        lastOutput_ = 0.0f;
        retriggerCorrection_ = 0.0f;
        retriggerPending_ = false;
        retriggerSamplesRemaining_ = 0u;
        retriggerTotalSamples_ = 1u;
        onsetGuardSamples_ = 1u;
        hybridIntroSamples_ = 1u;
        onsetTransitionActive_ = false;
        gate_ = false;
        active_ = false;
        syllableAgeSamples_ = 0u;
        gestureAgeSamples_ = 0u;
        sequenceSamplesRemaining_ = 0u;
        sequenceStepTotalSamples_ = 0u;
        sequenceStepIndex_ = 0u;
        activeSequence_ = AcapellaGestureSequence::Off;
        activeGestureSync_ = AcapellaGestureSync::Free;
        activeGestureDivision_ = AcapellaGestureDivision::Eighth;
        activeTextProgramRevision_ = 0u;
        sequenceActive_ = false;
        sequenceFinished_ = false;
        transportResyncPending_ = false;
        transportTempoValid_ = false;
        transportBeatValid_ = false;
        transportPlaying_ = false;
        transportTempoBpm_ = 120.0;
        transportBeat_ = 0.0;
        coefficientCounter_ = 0u;
        targetFrequencyHz_ = 146.83f;
        currentFrequencyHz_ = targetFrequencyHz_;
        velocityGain_ = 0.0f;
        nominalDurationSamples_ = static_cast<uint32_t>(sampleRate_ * 0.28f);
        onset_ = AcapellaOnset::None;
        onsetKind_ = acapella_source_detail::OnsetKind::None;
        onsetDurationSamples_ = 1u;
        random_.reset(params_.randomSeed);
        for (auto& formant : formants_) formant.reset();
        consonantFilter_.reset();
        nasalFilter_.reset();
        throatFilter_.reset();
        auto neutralGesture = acapella_source_detail::vowelGesture(
            AcapellaVowel::Schwa);
        neutralGesture.tractScale = params_.voice.tractScale;
        neutralGesture.velumOpen = params_.voice.nasal * 0.24f;
        neutralGesture.coarticulation = params_.coarticulation;
        targetArticulatoryGesture_ = neutralGesture;
        waveguide_.setGesture(neutralGesture);
        waveguide_.reset();
        const auto neutral = acapella_source_detail::vowelShape(AcapellaVowel::Schwa);
        currentFormants_ = neutral.frequency;
        sourceFormants_ = neutral.frequency;
        targetFormants_ = neutral.frequency;
        formantBandwidths_ = neutral.bandwidth;
        onsetLocus_ = neutral.frequency;
        phonemeRole_ = acapella_source_detail::PhonemeRole::Vowel;
        vowel_ = AcapellaVowel::Schwa;
        baseVowel_ = AcapellaVowel::Schwa;
        baseOnset_ = AcapellaOnset::None;
        gestureBlend_ = 1.0f;
        gestureConsonantStrength_ = params_.consonantStrength;
        gestureGain_ = 1.0f;
        targetGestureGain_ = 1.0f;
        segmentVoicing_ = 1.0f;
        segmentVoicingTarget_ = 1.0f;
        activePhoneme_ = AcapellaPhoneme::AX;
        activePhonemeStress_ = 0u;
        activePhonemeFlags_ = 0u;
        smoothedVoice_ = params_.voice;
        smoothedHybridBlend_ = params_.hybridBlend;
        smoothedWaveguideBlend_ = params_.waveguideBlend;
        additiveGains_.fill(0.0f);
    }

    void setParams(AcapellaSourceParams params)
    {
        params_ = sanitizeAcapellaSourceParams(params);
    }

    const AcapellaSourceParams& params() const { return params_; }

    void setTextGestureProgram(AcapellaGestureProgram program)
    {
        program.count = std::min<uint32_t>(program.count,
            kAcapellaTextGestureCapacity);
        for (uint32_t index = 0u; index < program.count; ++index) {
            auto& step = program.steps[index];
            if (static_cast<uint32_t>(step.phoneme)
                >= kAcapellaPhonemeCount) {
                step.phoneme = AcapellaPhoneme::AX;
            }
            step.durationScale = clamp(acapellaFiniteOr(
                step.durationScale, 1.0f), 0.10f, 8.0f);
            step.amplitude = clamp(acapellaFiniteOr(
                step.amplitude, 1.0f), 0.0f, 1.25f);
            step.stress = std::min<uint8_t>(step.stress, 2u);
        }
        if (program.revision == textProgram_.revision
            && program.count == textProgram_.count) return;
        textProgram_ = program;
    }

    void setGestureTransport(double tempoBpm, double songBeat,
        bool tempoValid, bool beatValid, bool playing = true)
    {
        transportTempoValid_ = tempoValid && std::isfinite(tempoBpm)
            && tempoBpm > 0.0;
        transportTempoBpm_ = transportTempoValid_
            ? std::clamp(tempoBpm, 20.0, 400.0) : 120.0;
        transportBeatValid_ = beatValid && std::isfinite(songBeat);
        if (transportBeatValid_) transportBeat_ = songBeat;
        transportPlaying_ = playing;
        transportResyncPending_ = params_.gestureSync
                == AcapellaGestureSync::Transport
            && transportBeatValid_ && transportPlaying_;
    }

    uint32_t gestureStepIndex() const { return sequenceStepIndex_; }
    float gestureStepProgress() const
    {
        if (!sequenceActive_ || sequenceStepTotalSamples_ == 0u) return 0.0f;
        return clamp(1.0f
            - static_cast<float>(sequenceSamplesRemaining_)
                / static_cast<float>(sequenceStepTotalSamples_),
            0.0f, 1.0f);
    }
    bool gestureSequenceActive() const { return sequenceActive_; }
    AcapellaPhoneme activePhoneme() const { return activePhoneme_; }
    uint8_t activePhonemeStress() const { return activePhonemeStress_; }
    uint8_t activePhonemeFlags() const { return activePhonemeFlags_; }
    float currentFrequencyHz() const { return currentFrequencyHz_; }

    bool trigger(AcapellaSyllable syllable)
    {
        syllable.frequencyHz = clamp(
            acapellaFiniteOr(syllable.frequencyHz, 146.83f), 35.0f,
            sampleRate_ * 0.20f);
        syllable.velocity = clamp(
            acapellaFiniteOr(syllable.velocity, 0.80f), 0.0f, 1.0f);
        syllable.durationMs = clamp(
            acapellaFiniteOr(syllable.durationMs, 280.0f), 35.0f, 10000.0f);
        if (syllable.velocity <= 1.0e-6f) {
            release();
            return false;
        }

        const bool wasActive = active_;
        // The tract, consonant gate, and velocity can all change at a note
        // steal. Preserve oscillator/filter state, then cancel the resulting
        // output discontinuity with a short, phase-preserving correction.
        retriggerPending_ = wasActive;
        onsetTransitionActive_ = !wasActive;
        onsetGuardSamples_ = std::max<uint32_t>(1u,
            static_cast<uint32_t>(params_.onsetGuardMs * 0.001f
                * sampleRate_));
        hybridIntroSamples_ = std::max<uint32_t>(onsetGuardSamples_,
            static_cast<uint32_t>(static_cast<float>(onsetGuardSamples_)
                * 2.5f));
        active_ = true;
        gate_ = true;
        syllableAgeSamples_ = 0u;
        gestureAgeSamples_ = 0u;
        targetFrequencyHz_ = syllable.frequencyHz;
        if (!wasActive) currentFrequencyHz_ = targetFrequencyHz_;
        velocityGain_ = std::sqrt(syllable.velocity);
        nominalDurationSamples_ = std::max<uint32_t>(1u,
            static_cast<uint32_t>(syllable.durationMs * 0.001f * sampleRate_));
        baseVowel_ = syllable.vowel;
        baseOnset_ = syllable.onset;
        startGestureSequence(!wasActive, false);
        jitterTarget_ = random_.bipolar();
        return true;
    }

    void release() { gate_ = false; }

    void setFrequencyHz(float frequencyHz)
    {
        targetFrequencyHz_ = clamp(acapellaFiniteOr(
            frequencyHz, targetFrequencyHz_), 35.0f, sampleRate_ * 0.20f);
    }

    void setVowel(AcapellaVowel vowel, float transitionMs = 55.0f)
    {
        baseVowel_ = vowel;
        if (sequenceActive_) return;
        vowel_ = vowel;
        activePhoneme_ = AcapellaPhoneme::AX;
        activePhonemeStress_ = 0u;
        activePhonemeFlags_ = 0u;
        const auto shape = acapella_source_detail::vowelShape(vowel);
        targetFormants_ = shape.frequency;
        formantBandwidths_ = shape.bandwidth;
        formantCoefficient_ = acapella_source_detail::timeCoefficient(
            clamp(acapellaFiniteOr(transitionMs, 55.0f), 2.0f, 1000.0f),
            sampleRate_);
    }

    bool active() const { return active_; }

    float processFrame()
    {
        using namespace acapella_source_detail;
        if (!active_) return 0.0f;
        advanceGestureSequence();

        const float controlCoefficient = timeCoefficient(20.0f, sampleRate_);
        smoothedVoice_.tractScale += (params_.voice.tractScale
            - smoothedVoice_.tractScale) * controlCoefficient;
        smoothedVoice_.breath += (params_.voice.breath
            - smoothedVoice_.breath) * controlCoefficient;
        smoothedVoice_.roughness += (params_.voice.roughness
            - smoothedVoice_.roughness) * controlCoefficient;
        smoothedVoice_.brightness += (params_.voice.brightness
            - smoothedVoice_.brightness) * controlCoefficient;
        smoothedVoice_.chest += (params_.voice.chest
            - smoothedVoice_.chest) * controlCoefficient;
        smoothedVoice_.nasal += (params_.voice.nasal
            - smoothedVoice_.nasal) * controlCoefficient;
        smoothedVoice_.openQuotient += (params_.voice.openQuotient
            - smoothedVoice_.openQuotient) * controlCoefficient;
        smoothedVoice_.harshness += (params_.voice.harshness
            - smoothedVoice_.harshness) * controlCoefficient;
        smoothedVoice_.falseFold += (params_.voice.falseFold
            - smoothedVoice_.falseFold) * controlCoefficient;
        smoothedVoice_.throat += (params_.voice.throat
            - smoothedVoice_.throat) * controlCoefficient;
        smoothedHybridBlend_ += (params_.hybridBlend
            - smoothedHybridBlend_) * controlCoefficient;
        smoothedWaveguideBlend_ += (params_.waveguideBlend
            - smoothedWaveguideBlend_) * controlCoefficient;

        const float glideCoefficient = params_.glideMs <= 0.01f
            ? 1.0f : timeCoefficient(params_.glideMs, sampleRate_);
        currentFrequencyHz_ += (targetFrequencyHz_ - currentFrequencyHz_)
            * glideCoefficient;

        const float ageSeconds = static_cast<float>(syllableAgeSamples_)
            / sampleRate_;
        const float progress = clamp(static_cast<float>(syllableAgeSamples_)
            / static_cast<float>(nominalDurationSamples_), 0.0f, 1.0f);
        vibratoPhase_ += params_.vibratoRateHz / sampleRate_;
        vibratoPhase_ -= std::floor(vibratoPhase_);
        driftPhase_ += 0.37f / sampleRate_;
        driftPhase_ -= std::floor(driftPhase_);
        vibratoEnvelope_ += (1.0f - vibratoEnvelope_)
            * timeCoefficient(params_.delivery == AcapellaDelivery::Sung
                ? 150.0f : 45.0f, sampleRate_);

        const float deliveryVibratoScale = params_.delivery == AcapellaDelivery::Sung
            ? 1.0f : 0.16f;
        float pitchCents = std::sin(2.0f * kPi * vibratoPhase_)
            * params_.vibratoDepthCents * vibratoEnvelope_ * deliveryVibratoScale;
        pitchCents += std::sin(2.0f * kPi * driftPhase_ + 0.73f)
            * params_.pitchDriftCents;
        if (params_.delivery == AcapellaDelivery::Rap) {
            pitchCents -= params_.rapDeclinationSemitones * 100.0f
                * smoothstep(0.08f, 1.0f, progress);
            pitchCents += std::sin(2.0f * kPi * 3.1f * ageSeconds)
                * params_.pitchDriftCents * 0.55f;
        } else {
            pitchCents -= params_.onsetScoopSemitones * 100.0f
                * std::exp(-ageSeconds * 18.0f);
        }

        jitter_ += (jitterTarget_ - jitter_)
            * (0.0005f + smoothedVoice_.roughness * 0.0022f);
        pitchCents += jitter_ * smoothedVoice_.roughness * 24.0f;
        const float frequency = clamp(currentFrequencyHz_
            * std::exp2(pitchCents / 1200.0f), 35.0f, sampleRate_ * 0.20f);
        phase_ += frequency / sampleRate_;
        subPhase_ += frequency * 0.5f / sampleRate_;
        thirdPhase_ += frequency / 3.0f / sampleRate_;
        growlPhase_ += (19.0f + smoothedVoice_.roughness * 17.0f)
            / sampleRate_;
        subPhase_ -= std::floor(subPhase_);
        thirdPhase_ -= std::floor(thirdPhase_);
        growlPhase_ -= std::floor(growlPhase_);
        if (phase_ >= 1.0f) {
            phase_ -= std::floor(phase_);
            jitterTarget_ = random_.bipolar();
        }

        const float openQuotient = smoothedVoice_.openQuotient;
        float flow = 0.0f;
        if (phase_ < openQuotient) {
            const float p = phase_ / openQuotient;
            flow = 0.5f - 0.5f * std::cos(2.0f * kPi * p);
        } else {
            const float p = (phase_ - openQuotient) / (1.0f - openQuotient);
            flow = 0.20f * (1.0f - p) * (1.0f - p);
        }
        const float differentiated = (flow - previousFlow_) * sampleRate_
            / frequency * 0.035f;
        previousFlow_ = flow;

        const float white = random_.bipolar();
        noiseLow_ += 0.035f * (white - noiseLow_);
        const float highNoise = white - noiseLow_;
        const float shimmer = 1.0f + smoothedVoice_.roughness * 0.15f * jitter_;
        float glottal = std::tanh((0.87f * differentiated
            + 0.09f * (flow - 0.35f)) * shimmer * 2.1f);
        glottal += std::sin(2.0f * kPi * subPhase_)
            * smoothedVoice_.chest * 0.075f;
        const float ventricularMotion = 0.68f
                * std::sin(2.0f * kPi * subPhase_)
            + 0.32f * std::sin(2.0f * kPi * thirdPhase_ + 0.41f);
        const float foldFlutter = 0.72f + 0.28f
            * std::sin(2.0f * kPi * growlPhase_ + jitter_ * 0.34f);
        const float falseFoldSource = ventricularMotion * foldFlutter
            * smoothedVoice_.falseFold * 0.34f;
        const float collisionDrive = 1.0f
            + smoothedVoice_.harshness * 6.5f;
        const float collided = std::tanh(
            (glottal + falseFoldSource) * collisionDrive);
        glottal = lerp(glottal, collided * 0.82f,
            smoothedVoice_.harshness * 0.84f);
        const float glottalCutoff = 1250.0f
            + smoothedVoice_.brightness * 3900.0f;
        const float glottalCoefficient = 1.0f - std::exp(
            -2.0f * kPi * glottalCutoff / sampleRate_);
        glottalLow1_ += glottalCoefficient * (glottal - glottalLow1_);
        glottalLow2_ += glottalCoefficient * (glottalLow1_ - glottalLow2_);
        const float aspiration = white * smoothedVoice_.breath
            * (0.040f + 0.025f * smoothedVoice_.brightness);
        const float foldTurbulence = highNoise * smoothedVoice_.harshness
            * (0.010f + 0.032f * std::abs(glottal))
            * (0.55f + 0.45f * foldFlutter);
        const float voicingCoefficient = timeCoefficient(3.0f, sampleRate_);
        segmentVoicing_ += (segmentVoicingTarget_ - segmentVoicing_)
            * voicingCoefficient;
        const float onsetProgress = clamp(static_cast<float>(
            gestureAgeSamples_) / static_cast<float>(
                onsetDurationSamples_), 0.0f, 1.0f);
        float articulationProgress = onsetProgress;
        float vowelBlend = onsetKind_ == OnsetKind::None
            ? 1.0f : smoothstep(0.32f, 1.0f, onsetProgress);
        float consonantEnvelope = 1.0f
            - smoothstep(0.28f, 1.0f, onsetProgress);
        switch (phonemeRole_) {
        case PhonemeRole::OnsetLead:
            // Earlier members of an onset cluster remain constricted. Only
            // the last consonant is allowed to open into the vowel.
            articulationProgress = 0.08f + onsetProgress * 0.16f;
            vowelBlend = 0.06f;
            consonantEnvelope = smoothstep(0.0f, 0.18f, onsetProgress)
                * (1.0f - onsetProgress * 0.12f);
            break;
        case PhonemeRole::Bridge: {
            // V-C-V motion closes in the middle of the consonant and opens
            // again, rather than treating the consonant as a fresh syllable.
            articulationProgress = std::abs(onsetProgress * 2.0f - 1.0f);
            vowelBlend = 0.18f + 0.82f * articulationProgress;
            consonantEnvelope = 1.0f
                - smoothstep(0.22f, 1.0f, articulationProgress);
            break;
        }
        case PhonemeRole::CodaStart:
            articulationProgress = 1.0f - onsetProgress;
            vowelBlend = 1.0f - smoothstep(0.08f, 0.88f,
                onsetProgress);
            consonantEnvelope = smoothstep(0.05f, 0.72f,
                onsetProgress);
            break;
        case PhonemeRole::CodaContinue:
            articulationProgress = 0.12f;
            vowelBlend = 0.04f;
            consonantEnvelope = smoothstep(0.0f, 0.16f, onsetProgress)
                * (1.0f - onsetProgress * 0.10f);
            break;
        case PhonemeRole::Vowel:
        case PhonemeRole::Silence:
            articulationProgress = 1.0f;
            consonantEnvelope = 0.0f;
            break;
        case PhonemeRole::OnsetRelease:
        default:
            break;
        }

        // Bring contextual glottal energy through an actual release or bridge.
        // Cluster leads and codas remain consonantal instead of each growing a
        // private vowel tail.
        float connectedVoicing = segmentVoicing_;
        if (phonemeRole_ == PhonemeRole::OnsetRelease
            && (onsetKind_ == OnsetKind::Stop
                || onsetKind_ == OnsetKind::VoicedStop)) {
            const float releaseConnection = smoothstep(
                0.28f, 0.88f, onsetProgress);
            const float releaseTarget =
                onsetKind_ == OnsetKind::VoicedStop ? 0.82f : 0.56f;
            connectedVoicing = std::max(connectedVoicing,
                releaseConnection * releaseTarget
                    * gestureConsonantStrength_);
        } else if (phonemeRole_ == PhonemeRole::OnsetRelease
            && (onsetKind_ == OnsetKind::Fricative
                || onsetKind_ == OnsetKind::VoicedFricative)) {
            const float tailConnection = smoothstep(
                0.46f, 1.0f, onsetProgress);
            const float tailTarget =
                onsetKind_ == OnsetKind::VoicedFricative ? 0.74f : 0.30f;
            connectedVoicing = std::max(connectedVoicing,
                tailConnection * tailTarget
                    * gestureConsonantStrength_);
        } else if (phonemeRole_ == PhonemeRole::Bridge) {
            connectedVoicing = std::max(connectedVoicing,
                smoothstep(0.55f, 1.0f, onsetProgress)
                    * 0.62f * gestureConsonantStrength_);
        }
        const float excitation = (0.72f * glottalLow2_
            + 0.28f * glottalLow1_) * connectedVoicing
            + aspiration * (0.40f + 0.60f * connectedVoicing)
            + foldTurbulence * (0.35f + 0.65f * connectedVoicing);

        for (uint32_t index = 0u; index < formants_.size(); ++index) {
            float desired = lerp(onsetLocus_[index],
                targetFormants_[index], vowelBlend);
            if (phonemeRole_ == PhonemeRole::Bridge) {
                desired = onsetProgress < 0.5f
                    ? lerp(sourceFormants_[index], onsetLocus_[index],
                        smoothstep(0.0f, 0.5f, onsetProgress))
                    : lerp(onsetLocus_[index], targetFormants_[index],
                        smoothstep(0.5f, 1.0f, onsetProgress));
            }
            currentFormants_[index] += (desired - currentFormants_[index])
                * formantCoefficient_;
        }

        if ((coefficientCounter_++ & 7u) == 0u) {
            const float tractFrequencyScale = 1.0f / smoothedVoice_.tractScale;
            const float bandwidthScale = 0.82f
                + smoothedVoice_.breath * 0.48f;
            for (uint32_t index = 0u; index < formants_.size(); ++index) {
                formants_[index].setBandpass(
                    currentFormants_[index] * tractFrequencyScale,
                    formantBandwidths_[index] * bandwidthScale,
                    sampleRate_);
            }
            const float throatCenter = (1050.0f
                + smoothedVoice_.brightness * 1050.0f)
                * tractFrequencyScale;
            throatFilter_.setBandpass(throatCenter,
                320.0f + smoothedVoice_.roughness * 620.0f, sampleRate_);
            updateAdditiveSpectrum(frequency);
            waveguide_.setGesture(applyOnsetGesture(
                targetArticulatoryGesture_, onset_,
                onsetKind_, articulationProgress,
                gestureConsonantStrength_,
                smoothedVoice_, params_.coarticulation));
        }

        const float shapedNoise = consonantFilter_.process(
            onset_ == AcapellaOnset::H ? white : highNoise);
        const float strength = gestureConsonantStrength_;
        const bool stop = onsetKind_ == OnsetKind::Stop
            || onsetKind_ == OnsetKind::VoicedStop;
        const float burstPosition = 0.34f;
        const float burstAge = onsetProgress - burstPosition;
        // A vocal plosive is a tract release, not a free broadband impact.
        // Use a small, place-colored pressure release and feed it only through
        // the moving tract. The former broad 12-30 ms pulse could build enough
        // mid-band energy to read as a separate snare-like event.
        const bool releasesIntoVowel =
            phonemeRole_ == PhonemeRole::OnsetRelease;
        const float burst = stop && releasesIntoVowel
                && burstAge >= 0.0f && burstAge < 0.20f
            ? std::sin(kPi * burstAge / 0.20f)
                * std::exp(-burstAge * (7.0f + params_.articulation * 5.0f))
            : 0.0f;
        float oralExcitation = excitation;
        if (stop) {
            oralExcitation += shapedNoise * burst * strength
                * (onsetKind_ == OnsetKind::VoicedStop ? 0.024f : 0.042f);
        } else if (onsetKind_ == OnsetKind::Fricative
            || onsetKind_ == OnsetKind::VoicedFricative) {
            // Most frication now enters at the constriction and acquires the
            // same tract/formant trajectory as the voiced source.
            oralExcitation += shapedNoise * consonantEnvelope
                * strength * 0.050f;
        }

        const std::array<float, 5u> baseGains {
            1.0f,
            0.76f,
            0.10f,
            0.040f,
            0.016f,
        };
        float oral = 0.0f;
        for (uint32_t index = 0u; index < formants_.size(); ++index) {
            const float brightnessGain = index < 2u ? 1.0f
                : 0.48f + smoothedVoice_.brightness * 1.04f;
            oral += formants_[index].process(oralExcitation)
                * baseGains[index] * brightnessGain;
        }
        oral += throatFilter_.process(oralExcitation)
            * smoothedVoice_.throat
            * (0.72f + smoothedVoice_.harshness * 0.72f);

        const auto waveguideFrame = waveguide_.processFrame(oralExcitation);
        const float resonatorVoice = oral * (0.34f + 0.66f * vowelBlend);
        const float waveguideVoice = waveguideFrame.oral
            + waveguideFrame.nasal;
        const float tractVoice = lerp(resonatorVoice, waveguideVoice,
            smoothedWaveguideBlend_);
        const float additiveVoice = renderAdditiveVoice()
            * (0.42f + 0.58f * vowelBlend);
        const float introProgress = onsetTransitionActive_
            ? clamp(static_cast<float>(syllableAgeSamples_)
                    / static_cast<float>(hybridIntroSamples_), 0.0f, 1.0f)
            : 1.0f;
        const float onsetAssist = 1.0f - smoothstep(0.0f, 1.0f,
            introProgress);
        const float clarityHarmonics = sequenceActive_
                && activeGestureDepth_ > 1.0e-5f
            ? params_.intelligibility * 0.20f : 0.0f;
        const float hybridAmount = clamp(std::max(clarityHarmonics,
            smoothedHybridBlend_)
            + (1.0f - smoothedHybridBlend_) * onsetAssist * 0.55f,
            0.0f, 1.0f);
        float voice = lerp(tractVoice, additiveVoice, hybridAmount);

        if (onsetKind_ == OnsetKind::Fricative
            || onsetKind_ == OnsetKind::VoicedFricative) {
            const float fricationCutoff = 4200.0f
                + smoothedVoice_.brightness * 2600.0f;
            const float fricationCoefficient = 1.0f - std::exp(
                -2.0f * kPi * fricationCutoff / sampleRate_);
            fricationSmooth_ += (shapedNoise - fricationSmooth_)
                * fricationCoefficient;
            const float fricationAttack = smoothstep(0.0f, 0.22f,
                onsetProgress);
            const float level = onset_ == AcapellaOnset::H ? 0.032f : 0.068f;
            voice += fricationSmooth_ * consonantEnvelope * fricationAttack
                * strength * level;
            if (onsetKind_ == OnsetKind::VoicedFricative) {
                voice += oral * consonantEnvelope * 0.18f;
            }
        } else if (stop) {
            const float closedGain = onsetKind_ == OnsetKind::VoicedStop
                ? 0.14f : 0.018f;
            const float effectiveClosedGain = lerp(1.0f, closedGain,
                clamp(strength, 0.0f, 1.0f));
            if (phonemeRole_ == PhonemeRole::CodaStart) {
                const float closure = smoothstep(0.12f, 0.82f,
                    onsetProgress);
                voice *= lerp(1.0f, effectiveClosedGain, closure);
            } else if (phonemeRole_ == PhonemeRole::CodaContinue
                || phonemeRole_ == PhonemeRole::OnsetLead) {
                voice *= effectiveClosedGain;
            } else if (phonemeRole_ == PhonemeRole::Bridge) {
                voice *= lerp(1.0f, effectiveClosedGain,
                    consonantEnvelope);
            } else {
                // Release the occluded vowel over several milliseconds. A
                // hard switch here survives downstream compression as a click.
                const float tractRelease = smoothstep(burstPosition,
                    burstPosition + 0.24f, onsetProgress);
                voice *= lerp(effectiveClosedGain, 1.0f, tractRelease);
            }
        } else if (onsetKind_ == OnsetKind::Nasal) {
            const float nasal = nasalFilter_.process(excitation);
            const float nasalMix = clamp(0.42f + strength * 0.40f
                + smoothedVoice_.nasal * 0.28f, 0.0f, 0.92f)
                * consonantEnvelope;
            voice = voice * (1.0f - nasalMix * 0.72f)
                + nasal * nasalMix * 1.35f;
        } else if (onsetKind_ == OnsetKind::Liquid) {
            voice *= 0.70f + 0.30f * vowelBlend;
        }

        const float cleanArticulationVoice = voice;
        if (smoothedVoice_.harshness > 1.0e-5f) {
            const float harshVoice = std::tanh(voice
                * (2.0f + smoothedVoice_.harshness * 6.0f)) * 0.54f;
            voice = lerp(voice, harshVoice,
                smoothedVoice_.harshness * 0.76f);
        }
        if (sequenceActive_ && activeGestureDepth_ > 1.0e-5f) {
            const float cleanPreserve = params_.intelligibility
                * (acapellaPhonemeIsVowel(activePhoneme_) ? 0.20f : 0.38f);
            voice = lerp(voice, cleanArticulationVoice, cleanPreserve);
        }

        const float attackCoefficient = envelopeCoefficient(
            params_.attackMs, sampleRate_);
        const float releaseCoefficient = envelopeCoefficient(
            params_.releaseMs, sampleRate_);
        amplitudeEnvelope_ += ((gate_ ? 1.0f : 0.0f) - amplitudeEnvelope_)
            * (gate_ ? attackCoefficient : releaseCoefficient);
        const float intensityGain = 0.42f + params_.intensity * 0.58f;
        const float guardProgress = onsetTransitionActive_
            ? clamp(static_cast<float>(syllableAgeSamples_)
                    / static_cast<float>(onsetGuardSamples_), 0.0f, 1.0f)
            : 1.0f;
        const float onsetGain = 0.5f - 0.5f
            * std::cos(kPi * guardProgress);
        const float gestureGainCoefficient = timeCoefficient(4.0f,
            sampleRate_);
        gestureGain_ += (targetGestureGain_ - gestureGain_)
            * gestureGainCoefficient;
        float sample = std::tanh(voice * amplitudeEnvelope_ * velocityGain_
            * intensityGain * gestureGain_ * 2.0f) * 0.62f * onsetGain;
        const float dcBlocked = sample - dcInput_ + 0.995f * dcOutput_;
        dcInput_ = sample;
        dcOutput_ = flushDenormal(dcBlocked);
        ++syllableAgeSamples_;
        ++gestureAgeSamples_;
        if (transportTempoValid_ && transportPlaying_) {
            transportBeat_ += transportTempoBpm_
                / (60.0 * static_cast<double>(sampleRate_));
        }
        if (onsetTransitionActive_
            && syllableAgeSamples_ >= hybridIntroSamples_) {
            onsetTransitionActive_ = false;
        }

        if (!std::isfinite(dcOutput_)) {
            reset();
            return 0.0f;
        }
        float outputSample = dcOutput_;
        if (retriggerPending_) {
            retriggerTotalSamples_ = std::max<uint32_t>(1u,
                static_cast<uint32_t>(params_.retriggerMs * 0.001f
                    * sampleRate_));
            retriggerSamplesRemaining_ = retriggerTotalSamples_;
            retriggerCorrection_ = lastOutput_ - outputSample;
            retriggerPending_ = false;
        }
        if (retriggerSamplesRemaining_ > 0u) {
            const float remaining = static_cast<float>(
                retriggerSamplesRemaining_)
                / static_cast<float>(retriggerTotalSamples_);
            const float correctionGain = remaining * remaining
                * (3.0f - 2.0f * remaining);
            outputSample += retriggerCorrection_ * correctionGain;
            --retriggerSamplesRemaining_;
        }
        outputSample = clamp(outputSample, -0.98f, 0.98f);
        lastOutput_ = outputSample;
        if (!gate_ && amplitudeEnvelope_ < 1.0e-5f) {
            active_ = false;
            amplitudeEnvelope_ = 0.0f;
            dcInput_ = 0.0f;
            dcOutput_ = 0.0f;
            waveguide_.reset();
        }
        return outputSample;
    }

    void processBlock(float* output, uint32_t frames)
    {
        if (!output) return;
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            output[frame] = processFrame();
        }
    }

private:
    static constexpr uint32_t kAdditiveHarmonics = 16u;

    void configureTarget(const acapella_source_detail::FormantShape& target,
        const ArticulatoryGesture& gesture, AcapellaOnset onset,
        float blendAmount, bool initialiseFormants,
        bool continuityCorrection)
    {
        using namespace acapella_source_detail;
        gestureBlend_ = clamp(blendAmount, 0.0f, 1.0f);
        gestureConsonantStrength_ = sequenceActive_
            ? params_.consonantStrength * gestureBlend_
                * (0.82f + params_.intelligibility * 0.34f)
            : params_.consonantStrength;
        gestureConsonantStrength_ = clamp(gestureConsonantStrength_,
            0.0f, 1.0f);
        onset_ = gestureConsonantStrength_ > 1.0e-5f
            ? onset : AcapellaOnset::None;
        onsetKind_ = onsetKind(onset_);
        targetFormants_ = target.frequency;
        formantBandwidths_ = target.bandwidth;
        if (sequenceActive_ && activeGestureDepth_ > 1.0e-5f) {
            const auto neutral = phonemeVowelShape(AcapellaPhoneme::AX);
            const float contrast = 0.86f
                + params_.intelligibility * 0.34f;
            const float bandwidthScale = 1.08f
                - params_.intelligibility * 0.20f;
            for (uint32_t index = 0u; index < targetFormants_.size(); ++index) {
                targetFormants_[index] = clamp(neutral.frequency[index]
                    + (targetFormants_[index] - neutral.frequency[index])
                        * contrast, 180.0f, sampleRate_ * 0.44f);
                formantBandwidths_[index] *= bandwidthScale;
            }
        }
        targetArticulatoryGesture_ = gesture;
        onsetLocus_ = onsetLocus(onset_, target);
        const float claritySpeed = sequenceActive_
                && activeGestureDepth_ > 1.0e-5f
            ? lerp(1.10f, 0.72f, params_.intelligibility) : 1.0f;
        const float onsetMs = lerp(145.0f, 52.0f, params_.articulation)
            * claritySpeed;
        onsetDurationSamples_ = std::max<uint32_t>(1u,
            static_cast<uint32_t>(onsetMs * 0.001f * sampleRate_));
        const float transitionMs = lerp(95.0f, 28.0f,
            params_.articulation) * claritySpeed;
        formantCoefficient_ = timeCoefficient(transitionMs, sampleRate_);
        if (initialiseFormants) {
            currentFormants_ = onsetKind_ == OnsetKind::None
                ? targetFormants_ : onsetLocus_;
        }
        const float center = fricativeCenter(onset_)
            * (0.84f + params_.voice.brightness * 0.32f);
        const bool stopFilter = onsetKind_ == OnsetKind::Stop
            || onsetKind_ == OnsetKind::VoicedStop;
        const float consonantBandwidth = stopFilter
            ? ((onset_ == AcapellaOnset::P
                    || onset_ == AcapellaOnset::B)
                ? 900.0f : 1450.0f)
            : ((onset_ == AcapellaOnset::S
                    || onset_ == AcapellaOnset::Z)
                ? 1800.0f : 2200.0f);
        consonantFilter_.setBandpass(center,
            consonantBandwidth,
            sampleRate_);
        nasalFilter_.setBandpass(
            onset_ == AcapellaOnset::M ? 240.0f
                : (onset_ == AcapellaOnset::Ng ? 250.0f : 300.0f),
            85.0f, sampleRate_);
        gestureAgeSamples_ = 0u;
        if (continuityCorrection) retriggerPending_ = true;
    }

    void configureGesture(AcapellaVowel vowel, AcapellaOnset onset,
        float blendAmount, bool initialiseFormants,
        bool continuityCorrection)
    {
        using namespace acapella_source_detail;
        blendAmount = clamp(blendAmount, 0.0f, 1.0f);
        const auto base = vowelShape(baseVowel_);
        const auto target = vowelShape(vowel);
        FormantShape blended = base;
        for (uint32_t index = 0u; index < blended.frequency.size(); ++index) {
            blended.frequency[index] = lerp(base.frequency[index],
                target.frequency[index], blendAmount);
            blended.bandwidth[index] = lerp(base.bandwidth[index],
                target.bandwidth[index], blendAmount);
        }
        vowel_ = vowel;
        phonemeRole_ = onset == AcapellaOnset::None
            ? PhonemeRole::Vowel : PhonemeRole::OnsetRelease;
        segmentVoicingTarget_ = 1.0f;
        configureTarget(blended, blendVowelGesture(baseVowel_, vowel,
            blendAmount), onset, blendAmount, initialiseFormants,
            continuityCorrection);
    }

    AcapellaPhoneme contextualVowel(
        const acapella_source_detail::GestureSequenceView& view,
        uint32_t index) const
    {
        if (!view.steps || view.count == 0u) return AcapellaPhoneme::AX;
        for (uint32_t next = index; next < view.count; ++next) {
            const auto phoneme = view.steps[next].phoneme;
            if (acapellaPhonemeIsVowel(phoneme)) return phoneme;
            if (phoneme == AcapellaPhoneme::Silence && next > index) break;
        }
        for (uint32_t previous = index; previous > 0u; --previous) {
            const auto phoneme = view.steps[previous - 1u].phoneme;
            if (acapellaPhonemeIsVowel(phoneme)) return phoneme;
            if (phoneme == AcapellaPhoneme::Silence) break;
        }
        return AcapellaPhoneme::AX;
    }

    acapella_source_detail::PhonemeRole sequencePhonemeRole(
        const acapella_source_detail::GestureSequenceView& view,
        uint32_t index) const
    {
        using namespace acapella_source_detail;
        if (!view.steps || index >= view.count) return PhonemeRole::Silence;
        const auto phoneme = view.steps[index].phoneme;
        if (phoneme == AcapellaPhoneme::Silence) {
            return PhonemeRole::Silence;
        }
        if (acapellaPhonemeIsVowel(phoneme)) return PhonemeRole::Vowel;

        bool vowelBefore = false;
        bool vowelAfter = false;
        for (uint32_t previous = index; previous > 0u; --previous) {
            const auto candidate = view.steps[previous - 1u].phoneme;
            if (candidate == AcapellaPhoneme::Silence) break;
            if (acapellaPhonemeIsVowel(candidate)) {
                vowelBefore = true;
                break;
            }
        }
        for (uint32_t next = index + 1u; next < view.count; ++next) {
            const auto candidate = view.steps[next].phoneme;
            if (candidate == AcapellaPhoneme::Silence) break;
            if (acapellaPhonemeIsVowel(candidate)) {
                vowelAfter = true;
                break;
            }
        }
        const bool immediatelyAfterVowel = index > 0u
            && acapellaPhonemeIsVowel(view.steps[index - 1u].phoneme);
        const bool immediatelyBeforeVowel = index + 1u < view.count
            && acapellaPhonemeIsVowel(view.steps[index + 1u].phoneme);
        if (vowelBefore && vowelAfter) {
            if (immediatelyAfterVowel && immediatelyBeforeVowel) {
                return PhonemeRole::Bridge;
            }
            if (immediatelyAfterVowel) return PhonemeRole::CodaStart;
            if (immediatelyBeforeVowel) return PhonemeRole::OnsetRelease;
            return PhonemeRole::CodaContinue;
        }
        if (vowelAfter) {
            return immediatelyBeforeVowel
                ? PhonemeRole::OnsetRelease : PhonemeRole::OnsetLead;
        }
        if (vowelBefore) {
            return immediatelyAfterVowel
                ? PhonemeRole::CodaStart : PhonemeRole::CodaContinue;
        }
        return PhonemeRole::OnsetLead;
    }

    void configurePhoneme(AcapellaPhoneme phoneme,
        AcapellaPhoneme contextVowel,
        acapella_source_detail::PhonemeRole role, float blendAmount,
        bool initialiseFormants, bool continuityCorrection)
    {
        using namespace acapella_source_detail;
        blendAmount = clamp(blendAmount, 0.0f, 1.0f);
        if (acapellaPhonemeIsVowel(phoneme)) contextVowel = phoneme;
        if (!acapellaPhonemeIsVowel(contextVowel)) {
            contextVowel = AcapellaPhoneme::AX;
        }
        const auto base = vowelShape(baseVowel_);
        const auto target = phonemeVowelShape(contextVowel);
        FormantShape blended = base;
        for (uint32_t index = 0u; index < blended.frequency.size(); ++index) {
            blended.frequency[index] = lerp(base.frequency[index],
                target.frequency[index], blendAmount);
            blended.bandwidth[index] = lerp(base.bandwidth[index],
                target.bandwidth[index], blendAmount);
        }
        const auto gesture = blendArticulatoryGestures(
            vowelGesture(baseVowel_), phonemeVowelGesture(contextVowel),
            blendAmount);
        segmentVoicingTarget_ = lerp(1.0f, phonemeVoicing(phoneme),
            blendAmount);
        activePhoneme_ = phoneme;
        sourceFormants_ = currentFormants_;
        phonemeRole_ = role;
        configureTarget(blended, gesture,
            phoneme == AcapellaPhoneme::Silence
                ? AcapellaOnset::None : phonemeOnset(phoneme),
            blendAmount, initialiseFormants, continuityCorrection);
    }

    acapella_source_detail::GestureSequenceView activeSequenceView() const
    {
        if (activeSequence_ == AcapellaGestureSequence::Text) {
            return { textProgram_.steps.data(), textProgram_.count };
        }
        return acapella_source_detail::gestureSequenceView(activeSequence_);
    }

    uint32_t sequenceSamplesForScale(float durationScale) const
    {
        float seconds = 1.0f / std::max(0.5f, params_.gestureRateHz);
        if (params_.gestureSync != AcapellaGestureSync::Free) {
            const float bpm = static_cast<float>(transportTempoValid_
                ? transportTempoBpm_ : 120.0);
            seconds = 60.0f / bpm
                * acapellaGestureDivisionBeats(params_.gestureDivision);
        }
        const float samples = sampleRate_ * seconds
            * clamp(durationScale, 0.10f, 8.0f);
        return std::max<uint32_t>(1u,
            static_cast<uint32_t>(samples));
    }

    uint32_t sequenceStepSamples(const AcapellaGestureStep& step) const
    {
        return sequenceSamplesForScale(step.durationScale);
    }

    void applySequenceStep(uint32_t index, bool initialiseFormants,
        bool continuityCorrection)
    {
        const auto view = activeSequenceView();
        if (!view.steps || view.count == 0u) {
            sequenceActive_ = false;
            configureGesture(baseVowel_, baseOnset_, 1.0f,
                initialiseFormants, continuityCorrection);
            targetGestureGain_ = 1.0f;
            return;
        }
        sequenceStepIndex_ = std::min(index, view.count - 1u);
        const auto& step = view.steps[sequenceStepIndex_];
        activePhonemeStress_ = step.stress;
        activePhonemeFlags_ = step.flags;
        const bool previousSequenceWasAudible = activeGestureDepth_ > 1.0e-5f;
        activeGestureDepth_ = params_.gestureDepth;
        if (activeGestureDepth_ <= 1.0e-5f) {
            configureGesture(baseVowel_, initialiseFormants
                    ? baseOnset_ : AcapellaOnset::None,
                1.0f, initialiseFormants,
                continuityCorrection && previousSequenceWasAudible);
        } else {
            configurePhoneme(step.phoneme,
                contextualVowel(view, sequenceStepIndex_),
                sequencePhonemeRole(view, sequenceStepIndex_),
                activeGestureDepth_, initialiseFormants,
                continuityCorrection);
        }
        const float amplitude = step.phoneme == AcapellaPhoneme::Silence
            ? 0.0f : step.amplitude;
        targetGestureGain_ = lerp(1.0f, amplitude, activeGestureDepth_);
        sequenceStepTotalSamples_ = sequenceStepSamples(step);
        sequenceSamplesRemaining_ = sequenceStepTotalSamples_;
        sequenceFinished_ = false;
    }

    void startGestureSequence(bool initialiseFormants,
        bool continuityCorrection)
    {
        activeSequence_ = params_.gestureSequence;
        activeGestureSync_ = params_.gestureSync;
        activeGestureDivision_ = params_.gestureDivision;
        activeTextProgramRevision_ = textProgram_.revision;
        const auto view = activeSequenceView();
        sequenceActive_ = view.steps && view.count > 0u;
        sequenceStepIndex_ = 0u;
        sequenceSamplesRemaining_ = 0u;
        sequenceStepTotalSamples_ = 0u;
        sequenceFinished_ = false;
        activeGestureDepth_ = params_.gestureDepth;
        if (sequenceActive_) {
            applySequenceStep(0u, initialiseFormants, continuityCorrection);
        } else {
            configureGesture(baseVowel_, baseOnset_, 1.0f,
                initialiseFormants, continuityCorrection);
            targetGestureGain_ = 1.0f;
        }
    }

    void synchronizeSequenceToTransport()
    {
        transportResyncPending_ = false;
        if (!sequenceActive_ || !params_.gestureLoop
            || params_.gestureSync != AcapellaGestureSync::Transport
            || !transportBeatValid_ || !transportPlaying_) return;
        const auto view = activeSequenceView();
        if (!view.steps || view.count == 0u) return;
        double cycle = 0.0;
        for (uint32_t index = 0u; index < view.count; ++index) {
            cycle += std::clamp(static_cast<double>(
                view.steps[index].durationScale), 0.10, 8.0);
        }
        if (cycle <= 0.0) return;
        const double divisionBeats = static_cast<double>(
            acapellaGestureDivisionBeats(params_.gestureDivision));
        double phase = std::fmod(transportBeat_ / divisionBeats, cycle);
        if (phase < 0.0) phase += cycle;
        uint32_t index = 0u;
        double stepStart = 0.0;
        for (; index + 1u < view.count; ++index) {
            const double duration = std::clamp(static_cast<double>(
                view.steps[index].durationScale), 0.10, 8.0);
            if (phase < stepStart + duration) break;
            stepStart += duration;
        }
        const double duration = std::clamp(static_cast<double>(
            view.steps[index].durationScale), 0.10, 8.0);
        sequenceStepTotalSamples_ = sequenceStepSamples(view.steps[index]);
        if (index != sequenceStepIndex_) {
            applySequenceStep(index, false, true);
        }
        const float remainingScale = static_cast<float>(std::max(
            0.0001, stepStart + duration - phase));
        sequenceSamplesRemaining_ = sequenceSamplesForScale(remainingScale);
    }

    void finishOneShotSequence()
    {
        sequenceSamplesRemaining_ = 0u;
        sequenceStepTotalSamples_ = 0u;
        sequenceFinished_ = true;
        activePhonemeStress_ = 0u;
        activePhonemeFlags_ = 0u;
        activeGestureDepth_ = params_.gestureDepth;
        if (activeGestureDepth_ <= 1.0e-5f) {
            configureGesture(baseVowel_, AcapellaOnset::None,
                1.0f, false, true);
            targetGestureGain_ = 1.0f;
        } else {
            configurePhoneme(AcapellaPhoneme::Silence,
                AcapellaPhoneme::AX,
                acapella_source_detail::PhonemeRole::Silence,
                activeGestureDepth_, false, true);
            targetGestureGain_ = 0.0f;
        }
    }

    void advanceGestureSequence()
    {
        if (activeSequence_ != params_.gestureSequence
            || activeGestureSync_ != params_.gestureSync
            || activeGestureDivision_ != params_.gestureDivision
            || (activeSequence_ == AcapellaGestureSequence::Text
                && activeTextProgramRevision_ != textProgram_.revision)) {
            startGestureSequence(false, true);
            if (params_.gestureSync == AcapellaGestureSync::Transport) {
                synchronizeSequenceToTransport();
            }
            return;
        }
        if (!sequenceActive_) return;
        const auto view = activeSequenceView();
        if (!view.steps || view.count == 0u) {
            startGestureSequence(false, true);
            return;
        }
        if (transportResyncPending_) synchronizeSequenceToTransport();
        if (sequenceFinished_) {
            if (params_.gestureLoop) {
                applySequenceStep(0u, false, true);
            } else if (activeGestureDepth_ != params_.gestureDepth) {
                finishOneShotSequence();
            }
            return;
        }
        if (activeGestureDepth_ != params_.gestureDepth) {
            applySequenceStep(sequenceStepIndex_, false, true);
            return;
        }
        if (sequenceSamplesRemaining_ > 1u) {
            --sequenceSamplesRemaining_;
            return;
        }
        const uint32_t next = sequenceStepIndex_ + 1u;
        if (next < view.count) {
            applySequenceStep(next, false, true);
        } else if (params_.gestureLoop) {
            applySequenceStep(0u, false, true);
        } else {
            finishOneShotSequence();
        }
    }

    void updateAdditiveSpectrum(float fundamentalHz)
    {
        constexpr std::array<float, 5u> formantGains {
            1.0f, 2.40f, 0.16f, 0.070f, 0.035f,
        };
        float energy = 0.0f;
        const float tractScale = std::max(0.70f,
            smoothedVoice_.tractScale);
        for (uint32_t index = 0u; index < additiveGains_.size(); ++index) {
            const float harmonic = static_cast<float>(index + 1u);
            const float harmonicHz = fundamentalHz * harmonic;
            float gain = 0.0f;
            if (harmonicHz < sampleRate_ * 0.45f) {
                float spectralEnvelope = 0.025f;
                for (uint32_t formant = 0u;
                     formant < currentFormants_.size(); ++formant) {
                    const float center = currentFormants_[formant]
                        / tractScale;
                    const float width = formantBandwidths_[formant]
                        * (1.35f + smoothedVoice_.breath * 0.55f);
                    const float offset = (harmonicHz - center)
                        / std::max(45.0f, width);
                    spectralEnvelope += formantGains[formant]
                        / (1.0f + offset * offset);
                }
                const float harmonicTilt = 1.0f / (1.0f
                    + (harmonic - 1.0f)
                        * (0.20f + (1.0f - smoothedVoice_.brightness)
                            * 0.20f));
                gain = spectralEnvelope * harmonicTilt;
            }
            additiveGains_[index] = gain;
            energy += gain * gain;
        }
        const float normalizer = 0.34f
            / std::sqrt(std::max(energy, 1.0e-8f));
        for (float& gain : additiveGains_) gain *= normalizer;
    }

    float renderAdditiveVoice() const
    {
        const float angle = 2.0f * kPi * phase_;
        const float fundamentalSin = std::sin(angle);
        const float fundamentalCos = std::cos(angle);
        float harmonicSin = fundamentalSin;
        float harmonicCos = fundamentalCos;
        float output = 0.0f;
        for (uint32_t index = 0u; index < additiveGains_.size(); ++index) {
            output += harmonicSin * additiveGains_[index];
            const float nextSin = harmonicSin * fundamentalCos
                + harmonicCos * fundamentalSin;
            harmonicCos = harmonicCos * fundamentalCos
                - harmonicSin * fundamentalSin;
            harmonicSin = nextSin;
        }
        return std::tanh(output * 1.35f) * 0.70f;
    }

    float sampleRate_ = 48000.0f;
    AcapellaSourceParams params_ {};
    AcapellaGestureProgram textProgram_ {};
    AcapellaVoiceProfile smoothedVoice_ {};
    acapella_source_detail::Random random_ {};
    std::array<acapella_source_detail::Biquad, 5u> formants_ {};
    acapella_source_detail::Biquad consonantFilter_ {};
    acapella_source_detail::Biquad nasalFilter_ {};
    acapella_source_detail::Biquad throatFilter_ {};
    ArticulatoryWaveguide waveguide_ {};
    std::array<float, 5u> currentFormants_ {};
    std::array<float, 5u> sourceFormants_ {};
    std::array<float, 5u> targetFormants_ {};
    std::array<float, 5u> formantBandwidths_ {};
    std::array<float, 5u> onsetLocus_ {};
    std::array<float, kAdditiveHarmonics> additiveGains_ {};
    AcapellaOnset onset_ = AcapellaOnset::None;
    AcapellaVowel vowel_ = AcapellaVowel::Schwa;
    AcapellaPhoneme activePhoneme_ = AcapellaPhoneme::AX;
    uint8_t activePhonemeStress_ = 0u;
    uint8_t activePhonemeFlags_ = 0u;
    AcapellaOnset baseOnset_ = AcapellaOnset::None;
    AcapellaVowel baseVowel_ = AcapellaVowel::Schwa;
    AcapellaGestureSequence activeSequence_ = AcapellaGestureSequence::Off;
    AcapellaGestureSync activeGestureSync_ = AcapellaGestureSync::Free;
    AcapellaGestureDivision activeGestureDivision_
        = AcapellaGestureDivision::Eighth;
    acapella_source_detail::OnsetKind onsetKind_
        = acapella_source_detail::OnsetKind::None;
    acapella_source_detail::PhonemeRole phonemeRole_
        = acapella_source_detail::PhonemeRole::Vowel;
    bool gate_ = false;
    bool active_ = false;
    bool onsetTransitionActive_ = false;
    bool sequenceActive_ = false;
    bool sequenceFinished_ = false;
    float phase_ = 0.0f;
    float subPhase_ = 0.0f;
    float thirdPhase_ = 0.0f;
    float growlPhase_ = 0.0f;
    float vibratoPhase_ = 0.0f;
    float driftPhase_ = 0.0f;
    float previousFlow_ = 0.0f;
    float glottalLow1_ = 0.0f;
    float glottalLow2_ = 0.0f;
    float noiseLow_ = 0.0f;
    float fricationSmooth_ = 0.0f;
    float jitter_ = 0.0f;
    float jitterTarget_ = 0.0f;
    float amplitudeEnvelope_ = 0.0f;
    float vibratoEnvelope_ = 0.0f;
    float currentFrequencyHz_ = 146.83f;
    float targetFrequencyHz_ = 146.83f;
    float velocityGain_ = 0.0f;
    float formantCoefficient_ = 0.001f;
    float dcInput_ = 0.0f;
    float dcOutput_ = 0.0f;
    float lastOutput_ = 0.0f;
    float retriggerCorrection_ = 0.0f;
    float smoothedHybridBlend_ = 0.16f;
    float smoothedWaveguideBlend_ = 0.48f;
    ArticulatoryGesture targetArticulatoryGesture_ {};
    float gestureBlend_ = 1.0f;
    float gestureConsonantStrength_ = 0.78f;
    float activeGestureDepth_ = 1.0f;
    float gestureGain_ = 1.0f;
    float targetGestureGain_ = 1.0f;
    float segmentVoicing_ = 1.0f;
    float segmentVoicingTarget_ = 1.0f;
    double transportTempoBpm_ = 120.0;
    double transportBeat_ = 0.0;
    bool retriggerPending_ = false;
    bool transportTempoValid_ = false;
    bool transportBeatValid_ = false;
    bool transportPlaying_ = false;
    bool transportResyncPending_ = false;
    uint32_t retriggerSamplesRemaining_ = 0u;
    uint32_t retriggerTotalSamples_ = 1u;
    uint32_t onsetGuardSamples_ = 1u;
    uint32_t hybridIntroSamples_ = 1u;
    uint32_t nominalDurationSamples_ = 1u;
    uint32_t onsetDurationSamples_ = 1u;
    uint32_t syllableAgeSamples_ = 0u;
    uint32_t gestureAgeSamples_ = 0u;
    uint32_t sequenceSamplesRemaining_ = 0u;
    uint32_t sequenceStepTotalSamples_ = 0u;
    uint32_t sequenceStepIndex_ = 0u;
    uint32_t activeTextProgramRevision_ = 0u;
    uint32_t coefficientCounter_ = 0u;
};

} // namespace s3g
