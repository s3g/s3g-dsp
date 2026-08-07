#pragma once

#include "s3g_drum_kick.h"
#include "s3g_drum_primitives.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

struct DrumKickFactoryPresetInfo {
    const char* name;
    const char* description;
};

inline constexpr uint32_t kDrumKickFactoryPresetCount = 12u;

inline const DrumKickFactoryPresetInfo&
drumKickFactoryPresetInfo(uint32_t index)
{
    static constexpr std::array<DrumKickFactoryPresetInfo,
        kDrumKickFactoryPresetCount> info {{
        { "DEEP ANCHOR", "Balanced low kick with a clean body and restrained attack." },
        { "TIGHT CIRCUIT", "Short tuned impact with a quick sweep and dry top." },
        { "LONG CURRENT", "Slow-settling fundamental with a sustained clean tail." },
        { "HARD ARC", "Bright, compressed transient over a driven compact body." },
        { "DUST PUNCH", "Textured mid-forward hit with reduced-rate grit." },
        { "STATIC WEIGHT", "Dark noise layer around a heavy low fundamental." },
        { "RUBBER DROP", "Elastic pitch gesture and rounded harmonic body." },
        { "METAL PULSE", "Tonal high attack with a firmer asymmetric edge." },
        { "LOW VOLTAGE", "Soft, low-tuned kick with muted reconstruction color." },
        { "BROKEN SAMPLER", "Coarse conversion character and a clipped-short envelope." },
        { "WIDE TOP", "Centred bass with independent stereo energy above it." },
        { "SOFT PRESSURE", "Low-click sustained weight with gentle compression." },
    }};
    return info[std::min<uint32_t>(
        index, kDrumKickFactoryPresetCount - 1u)];
}

inline DrumKickParams drumKickFactoryPreset(uint32_t index)
{
    DrumKickParams params;
    switch (std::min<uint32_t>(
        index, kDrumKickFactoryPresetCount - 1u)) {
    case 1u: // TIGHT CIRCUIT
        params.tuneHz = 55.0f;
        params.pitchDropSemitones = 19.0f;
        params.pitchSweepMs = 22.0f;
        params.pitchSettle = 0.08f;
        params.body = 0.18f;
        params.harmonics = 0.16f;
        params.decaySeconds = 0.24f;
        params.tail = 0.04f;
        params.punch = 0.94f;
        params.click = 0.24f;
        params.clickTone = 0.67f;
        params.clickDecayMs = 3.2f;
        params.texture = 0.015f;
        params.textureDecaySeconds = 0.035f;
        params.outputGainDb = -5.0f;
        break;
    case 2u: // LONG CURRENT
        params.tuneHz = 41.0f;
        params.noteTracking = 0.35f;
        params.pitchDropSemitones = 18.0f;
        params.pitchSweepMs = 105.0f;
        params.pitchSettle = 0.72f;
        params.body = 0.20f;
        params.harmonics = 0.035f;
        params.decaySeconds = 2.6f;
        params.tail = 0.82f;
        params.punch = 0.58f;
        params.click = 0.025f;
        params.clickTone = 0.38f;
        params.clickDecayMs = 2.5f;
        params.texture = 0.012f;
        params.textureDecaySeconds = 0.30f;
        params.outputGainDb = -7.5f;
        break;
    case 3u: // HARD ARC
        params.tuneHz = 51.0f;
        params.pitchDropSemitones = 38.0f;
        params.pitchSweepMs = 27.0f;
        params.pitchSettle = 0.15f;
        params.body = 0.46f;
        params.harmonics = 0.48f;
        params.decaySeconds = 0.48f;
        params.tail = 0.12f;
        params.punch = 1.0f;
        params.click = 0.42f;
        params.clickTone = 0.78f;
        params.clickDecayMs = 4.0f;
        params.texture = 0.045f;
        params.textureTone = 0.68f;
        params.textureDecaySeconds = 0.055f;
        params.character.drive = 0.62f;
        params.character.bias = 0.20f;
        params.character.compression = 0.34f;
        params.character.tone = 0.22f;
        params.outputGainDb = -8.0f;
        break;
    case 4u: // DUST PUNCH
        params.tuneHz = 49.0f;
        params.pitchDropSemitones = 27.0f;
        params.pitchSweepMs = 36.0f;
        params.pitchSettle = 0.22f;
        params.body = 0.32f;
        params.harmonics = 0.26f;
        params.decaySeconds = 0.60f;
        params.tail = 0.18f;
        params.punch = 0.88f;
        params.click = 0.30f;
        params.clickTone = 0.47f;
        params.clickDecayMs = 8.5f;
        params.texture = 0.18f;
        params.textureTone = 0.42f;
        params.textureDecaySeconds = 0.16f;
        params.character.drive = 0.26f;
        params.character.sampleRateReduction = 0.37f;
        params.character.bitDepthReduction = 0.18f;
        params.character.reconstruction = 0.30f;
        params.outputGainDb = -7.0f;
        break;
    case 5u: // STATIC WEIGHT
        params.tuneHz = 38.0f;
        params.noteTracking = 0.2f;
        params.pitchDropSemitones = 14.0f;
        params.pitchSweepMs = 72.0f;
        params.pitchSettle = 0.46f;
        params.body = 0.58f;
        params.harmonics = 0.09f;
        params.decaySeconds = 1.35f;
        params.tail = 0.54f;
        params.punch = 0.73f;
        params.click = 0.055f;
        params.clickTone = 0.31f;
        params.texture = 0.27f;
        params.textureTone = 0.22f;
        params.textureDecaySeconds = 0.72f;
        params.character.compression = 0.25f;
        params.character.tone = -0.28f;
        params.outputGainDb = -8.0f;
        break;
    case 6u: // RUBBER DROP
        params.tuneHz = 46.0f;
        params.pitchDropSemitones = 31.0f;
        params.pitchSweepMs = 88.0f;
        params.pitchSettle = 0.62f;
        params.body = 0.76f;
        params.harmonics = 0.22f;
        params.decaySeconds = 0.92f;
        params.tail = 0.34f;
        params.punch = 0.50f;
        params.click = 0.07f;
        params.clickTone = 0.44f;
        params.clickDecayMs = 7.0f;
        params.texture = 0.025f;
        params.character.drive = 0.18f;
        params.character.bias = -0.14f;
        params.outputGainDb = -6.5f;
        break;
    case 7u: // METAL PULSE
        params.tuneHz = 58.0f;
        params.pitchDropSemitones = 22.0f;
        params.pitchSweepMs = 31.0f;
        params.pitchSettle = 0.12f;
        params.body = 0.42f;
        params.harmonics = 0.64f;
        params.decaySeconds = 0.42f;
        params.tail = 0.08f;
        params.punch = 0.91f;
        params.click = 0.56f;
        params.clickTone = 0.90f;
        params.clickDecayMs = 12.0f;
        params.texture = 0.09f;
        params.textureTone = 0.82f;
        params.textureDecaySeconds = 0.10f;
        params.character.drive = 0.43f;
        params.character.bias = 0.36f;
        params.character.tone = 0.40f;
        params.outputGainDb = -9.0f;
        break;
    case 8u: // LOW VOLTAGE
        params.tuneHz = 34.0f;
        params.noteTracking = 0.0f;
        params.pitchDropSemitones = 10.0f;
        params.pitchSweepMs = 120.0f;
        params.pitchSettle = 0.34f;
        params.body = 0.33f;
        params.harmonics = 0.035f;
        params.decaySeconds = 1.75f;
        params.tail = 0.46f;
        params.punch = 0.42f;
        params.click = 0.012f;
        params.texture = 0.028f;
        params.textureTone = 0.18f;
        params.textureDecaySeconds = 0.36f;
        params.character.reconstruction = 0.42f;
        params.character.tone = -0.38f;
        params.outputGainDb = -6.0f;
        break;
    case 9u: // BROKEN SAMPLER
        params.tuneHz = 53.0f;
        params.pitchDropSemitones = 17.0f;
        params.pitchSweepMs = 19.0f;
        params.pitchSettle = 0.04f;
        params.body = 0.28f;
        params.harmonics = 0.38f;
        params.decaySeconds = 0.31f;
        params.tail = 0.03f;
        params.punch = 0.96f;
        params.click = 0.34f;
        params.clickTone = 0.60f;
        params.clickDecayMs = 5.0f;
        params.texture = 0.20f;
        params.textureTone = 0.55f;
        params.textureDecaySeconds = 0.085f;
        params.character.drive = 0.46f;
        params.character.bias = -0.22f;
        params.character.compression = 0.18f;
        params.character.sampleRateReduction = 0.72f;
        params.character.bitDepthReduction = 0.64f;
        params.character.reconstruction = 0.58f;
        params.outputGainDb = -8.5f;
        break;
    case 10u: // WIDE TOP
        params.tuneHz = 47.0f;
        params.pitchDropSemitones = 25.0f;
        params.pitchSweepMs = 43.0f;
        params.body = 0.24f;
        params.harmonics = 0.14f;
        params.decaySeconds = 0.72f;
        params.tail = 0.20f;
        params.punch = 0.82f;
        params.click = 0.27f;
        params.clickTone = 0.72f;
        params.clickDecayMs = 8.0f;
        params.texture = 0.24f;
        params.textureTone = 0.76f;
        params.textureDecaySeconds = 0.24f;
        params.stereoWidth = 0.92f;
        params.outputGainDb = -7.0f;
        break;
    case 11u: // SOFT PRESSURE
        params.tuneHz = 43.0f;
        params.noteTracking = 0.55f;
        params.pitchDropSemitones = 16.0f;
        params.pitchSweepMs = 68.0f;
        params.pitchSettle = 0.48f;
        params.body = 0.38f;
        params.harmonics = 0.06f;
        params.decaySeconds = 1.55f;
        params.tail = 0.60f;
        params.punch = 0.35f;
        params.click = 0.008f;
        params.texture = 0.018f;
        params.textureDecaySeconds = 0.20f;
        params.character.drive = 0.10f;
        params.character.compression = 0.42f;
        params.character.tone = -0.16f;
        params.outputGainDb = -5.5f;
        break;
    case 0u: // DEEP ANCHOR
    default:
        break;
    }
    return params;
}

// Build a complete, musically bounded kick rather than choosing or perturbing
// a factory preset.  Monitoring/performance choices remain user-owned: note
// tracking, velocity response and output trim are copied bit-for-bit from the
// current voice.  Passing a DrumRandom lets the tracker retain and advance the
// same deterministic random stream used for its other per-step decisions.
inline DrumKickParams drumKickSafeRandomParams(
    const DrumKickParams& current, DrumRandom& random)
{
    struct VoiceRanges {
        float tuneLow;
        float tuneHigh;
        float dropLow;
        float dropHigh;
        float sweepLow;
        float sweepHigh;
        float settleLow;
        float settleHigh;
        float bodyLow;
        float bodyHigh;
        float harmonicLow;
        float harmonicHigh;
        float decayLow;
        float decayHigh;
        float tailLow;
        float tailHigh;
        float punchLow;
        float punchHigh;
        float clickLow;
        float clickHigh;
        float textureLow;
        float textureHigh;
        float characterLow;
        float characterHigh;
    };

    // Five broad synthesis gestures keep independently drawn controls related:
    // deep/long, tight, rounded, hard, and textured.  These are design regions,
    // not hidden presets; every exposed voice control is still newly generated.
    static constexpr std::array<VoiceRanges, 5u> ranges {{
        { 32.0f, 49.0f, 10.0f, 29.0f, 58.0f, 150.0f,
          0.30f, 0.72f, 0.28f, 0.68f, 0.015f, 0.24f,
          0.90f, 2.40f, 0.32f, 0.72f, 0.38f, 0.72f,
          0.015f, 0.16f, 0.005f, 0.13f, 0.00f, 0.24f },
        { 45.0f, 70.0f, 16.0f, 38.0f, 12.0f, 48.0f,
          0.05f, 0.27f, 0.12f, 0.42f, 0.04f, 0.38f,
          0.20f, 0.58f, 0.02f, 0.18f, 0.74f, 0.98f,
          0.15f, 0.48f, 0.005f, 0.10f, 0.00f, 0.30f },
        { 36.0f, 59.0f, 12.0f, 33.0f, 34.0f, 98.0f,
          0.18f, 0.58f, 0.42f, 0.72f, 0.015f, 0.30f,
          0.52f, 1.55f, 0.12f, 0.52f, 0.48f, 0.84f,
          0.025f, 0.22f, 0.005f, 0.12f, 0.00f, 0.26f },
        { 42.0f, 74.0f, 24.0f, 44.0f, 15.0f, 62.0f,
          0.05f, 0.34f, 0.20f, 0.58f, 0.20f, 0.58f,
          0.28f, 0.98f, 0.03f, 0.32f, 0.78f, 0.98f,
          0.20f, 0.48f, 0.02f, 0.15f, 0.18f, 0.52f },
        { 35.0f, 66.0f, 10.0f, 36.0f, 24.0f, 108.0f,
          0.12f, 0.62f, 0.18f, 0.58f, 0.08f, 0.46f,
          0.36f, 1.55f, 0.08f, 0.48f, 0.54f, 0.90f,
          0.08f, 0.36f, 0.12f, 0.26f, 0.10f, 0.44f },
    }};

    const auto unit = [&]() { return random.unipolar(); };
    const auto linear = [&](float minimum, float maximum) {
        return minimum + (maximum - minimum) * unit();
    };
    const auto logarithmic = [&](float minimum, float maximum) {
        return std::exp(linear(std::log(minimum), std::log(maximum)));
    };

    const uint32_t gesture = random.nextU32()
        % static_cast<uint32_t>(ranges.size());
    const VoiceRanges& r = ranges[gesture];
    DrumKickParams p {};
    p.noteTracking = current.noteTracking;
    p.velocitySensitivity = current.velocitySensitivity;
    p.outputGainDb = current.outputGainDb;

    p.tuneHz = logarithmic(r.tuneLow, r.tuneHigh);
    p.pitchDropSemitones = linear(r.dropLow, r.dropHigh);
    p.pitchSweepMs = logarithmic(r.sweepLow, r.sweepHigh);
    p.pitchSettle = linear(r.settleLow, r.settleHigh);
    p.body = linear(r.bodyLow, r.bodyHigh);
    p.harmonics = linear(r.harmonicLow, r.harmonicHigh);
    p.decaySeconds = logarithmic(r.decayLow, r.decayHigh);
    p.tail = linear(r.tailLow, r.tailHigh);
    p.punch = linear(r.punchLow, r.punchHigh);
    p.click = linear(r.clickLow, r.clickHigh);
    p.clickTone = linear(0.22f, 0.90f);
    p.clickDecayMs = logarithmic(1.4f, 15.0f);
    p.texture = linear(r.textureLow, r.textureHigh);
    p.textureTone = linear(0.12f, 0.84f);
    p.textureDecaySeconds = logarithmic(0.025f, 0.65f);

    p.character.drive = linear(r.characterLow, r.characterHigh);
    p.character.bias = random.bipolar()
        * (0.04f + p.character.drive * 0.50f);
    p.character.compression = linear(0.0f,
        gesture == 3u ? 0.50f : 0.42f);
    // Roughly one third of patches retain a clean conversion stage.  The rest
    // favor subtle reduction; the full curated ceiling remains reachable but
    // is not the statistical center of RANDOM.
    const float conversionDraw = unit();
    const float conversion = conversionDraw < 0.34f ? 0.0f
        : std::pow((conversionDraw - 0.34f) / 0.66f,
            gesture == 4u ? 1.25f : 2.8f)
            * (gesture == 4u ? 0.58f : 0.46f);
    p.character.sampleRateReduction = conversion
        * linear(0.55f, 1.0f);
    p.character.bitDepthReduction = conversion
        * linear(0.42f, 0.88f);
    // Reconstruction tracks conversion severity, avoiding a bright filter on
    // otherwise clean voices while retaining useful sampler-like coloration.
    p.character.reconstruction = std::min(0.52f,
        conversion * linear(0.48f, 0.92f));
    p.character.tone = linear(-0.42f, 0.42f);
    p.stereoWidth = 0.85f * std::pow(unit(), 1.35f);
    return p;
}

inline DrumKickParams drumKickSafeRandomParams(
    const DrumKickParams& current, uint32_t seed)
{
    DrumRandom random(seed);
    return drumKickSafeRandomParams(current, random);
}

inline int32_t drumKickFactoryPresetIndex(const DrumKickParams& params)
{
    // Output gain is a host/session trim: loading and editing it must not turn
    // an otherwise exact voice preset into CUSTOM.
    const auto close = [](float left, float right) {
        return std::fabs(left - right) <= 1.0e-4f;
    };
    for (uint32_t index = 0u; index < kDrumKickFactoryPresetCount; ++index) {
        const DrumKickParams preset = drumKickFactoryPreset(index);
        if (close(params.tuneHz, preset.tuneHz)
            && close(params.noteTracking, preset.noteTracking)
            && close(params.pitchDropSemitones, preset.pitchDropSemitones)
            && close(params.pitchSweepMs, preset.pitchSweepMs)
            && close(params.pitchSettle, preset.pitchSettle)
            && close(params.body, preset.body)
            && close(params.harmonics, preset.harmonics)
            && close(params.decaySeconds, preset.decaySeconds)
            && close(params.tail, preset.tail)
            && close(params.punch, preset.punch)
            && close(params.click, preset.click)
            && close(params.clickTone, preset.clickTone)
            && close(params.clickDecayMs, preset.clickDecayMs)
            && close(params.texture, preset.texture)
            && close(params.textureTone, preset.textureTone)
            && close(params.textureDecaySeconds, preset.textureDecaySeconds)
            && close(params.character.drive, preset.character.drive)
            && close(params.character.bias, preset.character.bias)
            && close(params.character.compression, preset.character.compression)
            && close(params.character.sampleRateReduction,
                preset.character.sampleRateReduction)
            && close(params.character.bitDepthReduction,
                preset.character.bitDepthReduction)
            && close(params.character.reconstruction,
                preset.character.reconstruction)
            && close(params.character.tone, preset.character.tone)
            && close(params.stereoWidth, preset.stereoWidth)
            && close(params.velocitySensitivity, preset.velocitySensitivity)) {
            return static_cast<int32_t>(index);
        }
    }
    return -1;
}

} // namespace s3g
