#pragma once

#include "s3g_drum_cowbell.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

struct DrumCowbellFactoryPresetInfo {
    const char* name;
    const char* description;
};

inline constexpr uint32_t kDrumCowbellFactoryPresetCount = 12u;

inline const DrumCowbellFactoryPresetInfo&
drumCowbellFactoryPresetInfo(uint32_t index)
{
    static constexpr std::array<DrumCowbellFactoryPresetInfo,
        kDrumCowbellFactoryPresetCount> info {{
        { "BALANCED BELL", "A centered two-tone cowbell with controlled body and strike." },
        { "LOW IRON", "Low inharmonic fundamentals with a long dark shell resonance." },
        { "HIGH BLOCK", "Compact high modes and a short hard beater contact." },
        { "SHORT MUTE", "Very short electronic cowbell shaped for dense patterns." },
        { "HOLLOW ANALOG", "Open pulse-like oscillators with a rounded hollow body." },
        { "WOODEN EDGE", "Softened oscillators and a pronounced dry body knock." },
        { "CASSETTE COW", "Compressed, shaded and gently worn conversion color." },
        { "DIGITAL TINE", "Bright narrow partials with crisp reduced-bit edges." },
        { "BENT METAL", "A falling pitch gesture across an uneven metal interval." },
        { "WIDE FORGE", "Long scattered modes with a broad stereo image." },
        { "DULL BODY", "Restrained strike noise and a low, heavy shell response." },
        { "BRIGHT STRIKE", "Fast bright contact with strong upper attack detail." },
    }};
    return info[std::min<uint32_t>(
        index, kDrumCowbellFactoryPresetCount - 1u)];
}

inline DrumCowbellParams drumCowbellFactoryPreset(uint32_t index)
{
    DrumCowbellParams p;
    switch (std::min<uint32_t>(index,
        kDrumCowbellFactoryPresetCount - 1u)) {
    case 1u: // LOW IRON
        p.tuneHz = 310.0f;
        p.intervalRatio = 1.64f;
        p.shape = 0.54f;
        p.decaySeconds = 0.48f;
        p.damping = 0.30f;
        p.body = 0.86f;
        p.bodyDecaySeconds = 0.42f;
        p.brightness = 0.22f;
        p.noise = 0.08f;
        p.strikeTone = 0.22f;
        p.outputGainDb = -8.5f;
        break;
    case 2u: // HIGH BLOCK
        p.tuneHz = 980.0f;
        p.intervalRatio = 1.38f;
        p.shape = 0.82f;
        p.attack = 0.88f;
        p.decaySeconds = 0.11f;
        p.damping = 0.72f;
        p.body = 0.28f;
        p.bodyDecaySeconds = 0.075f;
        p.brightness = 0.88f;
        p.noise = 0.20f;
        p.noiseDecaySeconds = 0.018f;
        p.strikeTone = 0.84f;
        p.outputGainDb = -8.5f;
        break;
    case 3u: // SHORT MUTE
        p.tuneHz = 650.0f;
        p.intervalRatio = 1.50f;
        p.detune = 0.03f;
        p.shape = 0.76f;
        p.attack = 0.82f;
        p.decaySeconds = 0.055f;
        p.damping = 0.88f;
        p.body = 0.16f;
        p.bodyDecaySeconds = 0.038f;
        p.brightness = 0.64f;
        p.noise = 0.12f;
        p.noiseDecaySeconds = 0.010f;
        p.stereoWidth = 0.06f;
        p.outputGainDb = -7.0f;
        break;
    case 4u: // HOLLOW ANALOG
        p.tuneHz = 510.0f;
        p.intervalRatio = 1.49f;
        p.detune = 0.16f;
        p.shape = 0.94f;
        p.attack = 0.48f;
        p.decaySeconds = 0.34f;
        p.damping = 0.36f;
        p.body = 0.72f;
        p.bodyDecaySeconds = 0.24f;
        p.brightness = 0.42f;
        p.noise = 0.06f;
        p.character.drive = 0.14f;
        p.outputGainDb = -8.5f;
        break;
    case 5u: // WOODEN EDGE
        p.tuneHz = 430.0f;
        p.intervalRatio = 1.82f;
        p.shape = 0.28f;
        p.attack = 0.36f;
        p.decaySeconds = 0.16f;
        p.damping = 0.66f;
        p.body = 0.94f;
        p.bodyDecaySeconds = 0.13f;
        p.brightness = 0.20f;
        p.noise = 0.18f;
        p.noiseDecaySeconds = 0.024f;
        p.strikeTone = 0.18f;
        p.outputGainDb = -7.5f;
        break;
    case 6u: // CASSETTE COW
        p.tuneHz = 535.0f;
        p.intervalRatio = 1.56f;
        p.detune = 0.24f;
        p.shape = 0.66f;
        p.decaySeconds = 0.31f;
        p.body = 0.68f;
        p.bodyDecaySeconds = 0.21f;
        p.brightness = 0.38f;
        p.noise = 0.22f;
        p.character.drive = 0.18f;
        p.character.compression = 0.34f;
        p.character.sampleRateReduction = 0.10f;
        p.character.reconstruction = 0.12f;
        p.character.tone = -0.28f;
        p.outputGainDb = -8.5f;
        break;
    case 7u: // DIGITAL TINE
        p.tuneHz = 760.0f;
        p.intervalRatio = 2.04f;
        p.detune = 0.02f;
        p.shape = 0.48f;
        p.attack = 0.92f;
        p.decaySeconds = 0.18f;
        p.damping = 0.58f;
        p.body = 0.34f;
        p.bodyDecaySeconds = 0.11f;
        p.brightness = 0.96f;
        p.noise = 0.10f;
        p.strikeTone = 0.92f;
        p.character.bitDepthReduction = 0.26f;
        p.character.reconstruction = 0.10f;
        p.outputGainDb = -9.0f;
        break;
    case 8u: // BENT METAL
        p.tuneHz = 590.0f;
        p.intervalRatio = 1.71f;
        p.detune = 0.52f;
        p.shape = 0.74f;
        p.decaySeconds = 0.30f;
        p.body = 0.56f;
        p.brightness = 0.62f;
        p.noise = 0.14f;
        p.bendSemitones = 10.0f;
        p.bendDecaySeconds = 0.055f;
        p.stereoWidth = 0.30f;
        break;
    case 9u: // WIDE FORGE
        p.tuneHz = 470.0f;
        p.intervalRatio = 1.91f;
        p.detune = 0.86f;
        p.shape = 0.60f;
        p.attack = 0.44f;
        p.decaySeconds = 0.72f;
        p.damping = 0.18f;
        p.body = 0.84f;
        p.bodyDecaySeconds = 0.64f;
        p.brightness = 0.70f;
        p.noise = 0.24f;
        p.noiseDecaySeconds = 0.065f;
        p.stereoWidth = 0.92f;
        p.outputGainDb = -10.0f;
        break;
    case 10u: // DULL BODY
        p.tuneHz = 385.0f;
        p.intervalRatio = 1.44f;
        p.shape = 0.44f;
        p.attack = 0.22f;
        p.decaySeconds = 0.22f;
        p.damping = 0.54f;
        p.body = 0.98f;
        p.bodyDecaySeconds = 0.30f;
        p.brightness = 0.06f;
        p.noise = 0.03f;
        p.strikeTone = 0.08f;
        p.character.tone = -0.38f;
        p.outputGainDb = -7.5f;
        break;
    case 11u: // BRIGHT STRIKE
        p.tuneHz = 820.0f;
        p.intervalRatio = 1.58f;
        p.shape = 0.88f;
        p.attack = 0.98f;
        p.decaySeconds = 0.15f;
        p.damping = 0.62f;
        p.body = 0.38f;
        p.bodyDecaySeconds = 0.085f;
        p.brightness = 1.0f;
        p.noise = 0.48f;
        p.noiseDecaySeconds = 0.014f;
        p.strikeTone = 1.0f;
        p.outputGainDb = -10.0f;
        break;
    case 0u: // BALANCED BELL
    default:
        break;
    }
    return p;
}

inline DrumCowbellParams drumCowbellSafeRandomParams(
    const DrumCowbellParams& current, DrumRandom& random)
{
    const auto unit = [&]() { return random.unipolar(); };
    const auto range = [&](float minimum, float maximum) {
        return minimum + (maximum - minimum) * unit();
    };
    const auto logRange = [&](float minimum, float maximum) {
        return std::exp(range(std::log(minimum), std::log(maximum)));
    };

    DrumCowbellParams p {};
    p.noteTracking = current.noteTracking;
    p.velocitySensitivity = current.velocitySensitivity;
    p.outputGainDb = current.outputGainDb;
    const uint32_t family = random.nextU32() % 4u;
    p.tuneHz = family == 0u ? logRange(250.0f, 560.0f)
        : family == 1u ? logRange(520.0f, 980.0f)
        : logRange(340.0f, 820.0f);
    p.intervalRatio = range(1.22f, 2.18f);
    p.detune = range(0.0f, family == 2u ? 0.90f : 0.48f);
    p.shape = range(0.18f, 0.96f);
    p.attack = range(0.16f, 0.98f);
    p.decaySeconds = family == 3u ? logRange(0.35f, 1.1f)
        : logRange(0.045f, 0.55f);
    p.damping = range(0.08f, 0.92f);
    p.body = range(0.12f, 0.96f);
    p.bodyDecaySeconds = logRange(0.035f, 0.72f);
    p.brightness = range(0.04f, 0.98f);
    p.noise = range(0.0f, 0.48f);
    p.noiseDecaySeconds = logRange(0.007f, 0.11f);
    p.bendSemitones = random.bipolar()
        * (unit() < 0.55f ? 2.0f : 16.0f);
    p.bendDecaySeconds = logRange(0.008f, 0.14f);
    p.strikeTone = range(0.04f, 0.98f);
    p.character.drive = range(0.0f, family == 2u ? 0.36f : 0.22f);
    p.character.bias = random.bipolar() * p.character.drive * 0.32f;
    p.character.compression = range(0.0f, 0.42f);
    const float conversion = unit() < 0.48f ? 0.0f : range(0.02f, 0.34f);
    p.character.sampleRateReduction = conversion;
    p.character.bitDepthReduction = conversion * range(0.25f, 0.90f);
    p.character.reconstruction = conversion * range(0.30f, 0.82f);
    p.character.tone = range(-0.42f, 0.36f);
    p.stereoWidth = std::pow(unit(), 1.25f) * 0.92f;
    return drumSanitizeCowbellParams(p);
}

inline DrumCowbellParams drumCowbellSafeRandomParams(
    const DrumCowbellParams& current, uint32_t& state)
{
    DrumRandom random(state);
    DrumCowbellParams params = drumCowbellSafeRandomParams(current, random);
    state = random.state();
    return params;
}

inline int32_t drumCowbellFactoryPresetIndex(const DrumCowbellParams& params)
{
    const auto close = [](float left, float right) {
        return std::fabs(left - right) <= 1.0e-4f;
    };
    for (uint32_t index = 0u; index < kDrumCowbellFactoryPresetCount; ++index) {
        const DrumCowbellParams p = drumCowbellFactoryPreset(index);
        if (close(params.tuneHz, p.tuneHz)
            && close(params.noteTracking, p.noteTracking)
            && close(params.intervalRatio, p.intervalRatio)
            && close(params.detune, p.detune)
            && close(params.shape, p.shape)
            && close(params.attack, p.attack)
            && close(params.decaySeconds, p.decaySeconds)
            && close(params.damping, p.damping)
            && close(params.body, p.body)
            && close(params.bodyDecaySeconds, p.bodyDecaySeconds)
            && close(params.brightness, p.brightness)
            && close(params.noise, p.noise)
            && close(params.noiseDecaySeconds, p.noiseDecaySeconds)
            && close(params.bendSemitones, p.bendSemitones)
            && close(params.bendDecaySeconds, p.bendDecaySeconds)
            && close(params.strikeTone, p.strikeTone)
            && close(params.character.drive, p.character.drive)
            && close(params.character.bias, p.character.bias)
            && close(params.character.compression, p.character.compression)
            && close(params.character.sampleRateReduction,
                p.character.sampleRateReduction)
            && close(params.character.bitDepthReduction,
                p.character.bitDepthReduction)
            && close(params.character.reconstruction,
                p.character.reconstruction)
            && close(params.character.tone, p.character.tone)
            && close(params.stereoWidth, p.stereoWidth)
            && close(params.velocitySensitivity, p.velocitySensitivity)) {
            return static_cast<int32_t>(index);
        }
    }
    return -1;
}

} // namespace s3g
