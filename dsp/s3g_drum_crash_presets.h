#pragma once

#include "s3g_drum_crash.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

struct DrumCrashFactoryPresetInfo {
    const char* name;
    const char* description;
};

inline constexpr uint32_t kDrumCrashFactoryPresetCount = 12u;

inline const DrumCrashFactoryPresetInfo&
drumCrashFactoryPresetInfo(uint32_t index)
{
    static constexpr std::array<DrumCrashFactoryPresetInfo,
        kDrumCrashFactoryPresetCount> info {{
        { "BALANCED CRASH", "A broad modal crash with a clear attack and natural wash." },
        { "SHORT ELECTRIC", "A compact electronic crash for rapid rhythmic placement." },
        { "LONG SHEET", "Large slow-decaying metal with dense upper movement." },
        { "DARK BRONZE", "Low weighted modes and a shaded, restrained wash." },
        { "BRIGHT GLASS", "Hard luminous upper modes with a clean high-frequency tail." },
        { "TRASH METAL", "Irregular mode spacing and aggressive noisy texture." },
        { "WIDE WASH", "Diffuse stereo noise wrapped around a softer metal center." },
        { "DRY MODAL", "Distinct inharmonic partials with very little noise wash." },
        { "BELL EDGE", "Strong cup-like resonance for the dedicated Bell articulation." },
        { "CASSETTE CRASH", "Compressed darkened cymbal with gently worn conversion." },
        { "DIGITAL SPRAY", "Fine bright modes with reduced-bit and reconstruction color." },
        { "SOFT SWELL", "A slower contact and long smooth decay for restrained strikes." },
    }};
    return info[std::min<uint32_t>(
        index, kDrumCrashFactoryPresetCount - 1u)];
}

inline DrumCrashParams drumCrashFactoryPreset(uint32_t index)
{
    DrumCrashParams p;
    switch (std::min<uint32_t>(index,
        kDrumCrashFactoryPresetCount - 1u)) {
    case 1u: // SHORT ELECTRIC
        p.tuneHz = 960.0f;
        p.size = 0.22f;
        p.alloy = 0.72f;
        p.spread = 0.54f;
        p.density = 9.0f;
        p.brightness = 0.76f;
        p.attack = 0.88f;
        p.decaySeconds = 0.22f;
        p.damping = 0.86f;
        p.wash = 0.48f;
        p.washDecaySeconds = 0.20f;
        p.bell = 0.12f;
        p.texture = 0.70f;
        p.stereoWidth = 0.42f;
        p.outputGainDb = -9.5f;
        break;
    case 2u: // LONG SHEET
        p.tuneHz = 480.0f;
        p.size = 0.96f;
        p.alloy = 0.64f;
        p.spread = 0.78f;
        p.density = 16.0f;
        p.brightness = 0.62f;
        p.attack = 0.48f;
        p.decaySeconds = 4.2f;
        p.damping = 0.10f;
        p.wash = 0.88f;
        p.washDecaySeconds = 4.8f;
        p.bell = 0.18f;
        p.bellTuneHz = 1220.0f;
        p.texture = 0.62f;
        p.stereoWidth = 0.88f;
        p.outputGainDb = -13.0f;
        break;
    case 3u: // DARK BRONZE
        p.tuneHz = 390.0f;
        p.size = 0.78f;
        p.alloy = 0.38f;
        p.spread = 0.46f;
        p.density = 11.0f;
        p.brightness = 0.18f;
        p.attack = 0.52f;
        p.decaySeconds = 2.1f;
        p.damping = 0.34f;
        p.wash = 0.60f;
        p.washDecaySeconds = 2.4f;
        p.bell = 0.34f;
        p.bellTuneHz = 980.0f;
        p.texture = 0.28f;
        p.character.tone = -0.36f;
        p.outputGainDb = -10.0f;
        break;
    case 4u: // BRIGHT GLASS
        p.tuneHz = 1120.0f;
        p.size = 0.42f;
        p.alloy = 0.34f;
        p.spread = 0.60f;
        p.density = 14.0f;
        p.brightness = 0.98f;
        p.attack = 0.96f;
        p.decaySeconds = 1.15f;
        p.damping = 0.52f;
        p.wash = 0.64f;
        p.washDecaySeconds = 1.3f;
        p.bell = 0.42f;
        p.bellTuneHz = 2900.0f;
        p.texture = 0.76f;
        p.outputGainDb = -13.0f;
        break;
    case 5u: // TRASH METAL
        p.tuneHz = 610.0f;
        p.size = 0.58f;
        p.alloy = 0.98f;
        p.spread = 1.0f;
        p.density = 15.0f;
        p.brightness = 0.74f;
        p.attack = 0.82f;
        p.decaySeconds = 1.05f;
        p.damping = 0.48f;
        p.wash = 0.94f;
        p.washDecaySeconds = 1.15f;
        p.bell = 0.08f;
        p.texture = 1.0f;
        p.character.drive = 0.24f;
        p.outputGainDb = -13.5f;
        break;
    case 6u: // WIDE WASH
        p.tuneHz = 560.0f;
        p.size = 0.82f;
        p.alloy = 0.52f;
        p.spread = 0.82f;
        p.density = 13.0f;
        p.brightness = 0.70f;
        p.attack = 0.44f;
        p.decaySeconds = 1.8f;
        p.damping = 0.32f;
        p.wash = 1.0f;
        p.washDecaySeconds = 2.8f;
        p.bell = 0.10f;
        p.texture = 0.82f;
        p.stereoWidth = 1.0f;
        p.outputGainDb = -13.0f;
        break;
    case 7u: // DRY MODAL
        p.tuneHz = 780.0f;
        p.size = 0.48f;
        p.alloy = 0.46f;
        p.spread = 0.52f;
        p.density = 10.0f;
        p.brightness = 0.56f;
        p.attack = 0.78f;
        p.decaySeconds = 0.82f;
        p.damping = 0.58f;
        p.wash = 0.08f;
        p.washDecaySeconds = 0.50f;
        p.bell = 0.44f;
        p.bellTuneHz = 1840.0f;
        p.texture = 0.20f;
        p.stereoWidth = 0.28f;
        p.outputGainDb = -9.5f;
        break;
    case 8u: // BELL EDGE
        p.tuneHz = 640.0f;
        p.size = 0.56f;
        p.alloy = 0.42f;
        p.spread = 0.40f;
        p.density = 9.0f;
        p.brightness = 0.72f;
        p.attack = 0.94f;
        p.decaySeconds = 1.65f;
        p.damping = 0.36f;
        p.wash = 0.38f;
        p.washDecaySeconds = 1.2f;
        p.bell = 0.96f;
        p.bellTuneHz = 2140.0f;
        p.texture = 0.38f;
        p.stereoWidth = 0.38f;
        p.outputGainDb = -12.0f;
        break;
    case 9u: // CASSETTE CRASH
        p.tuneHz = 520.0f;
        p.size = 0.70f;
        p.alloy = 0.58f;
        p.spread = 0.68f;
        p.density = 12.0f;
        p.brightness = 0.42f;
        p.attack = 0.56f;
        p.decaySeconds = 1.7f;
        p.damping = 0.40f;
        p.wash = 0.78f;
        p.washDecaySeconds = 1.9f;
        p.texture = 0.48f;
        p.character.drive = 0.18f;
        p.character.compression = 0.36f;
        p.character.sampleRateReduction = 0.10f;
        p.character.reconstruction = 0.14f;
        p.character.tone = -0.30f;
        p.outputGainDb = -11.5f;
        break;
    case 10u: // DIGITAL SPRAY
        p.tuneHz = 1040.0f;
        p.size = 0.34f;
        p.alloy = 0.82f;
        p.spread = 0.90f;
        p.density = 16.0f;
        p.brightness = 0.92f;
        p.attack = 0.90f;
        p.decaySeconds = 0.72f;
        p.damping = 0.68f;
        p.wash = 0.82f;
        p.washDecaySeconds = 0.86f;
        p.bell = 0.12f;
        p.texture = 0.94f;
        p.character.bitDepthReduction = 0.24f;
        p.character.reconstruction = 0.18f;
        p.outputGainDb = -14.0f;
        break;
    case 11u: // SOFT SWELL
        p.tuneHz = 590.0f;
        p.size = 0.86f;
        p.alloy = 0.42f;
        p.spread = 0.72f;
        p.density = 14.0f;
        p.brightness = 0.48f;
        p.attack = 0.04f;
        p.decaySeconds = 3.1f;
        p.damping = 0.20f;
        p.wash = 0.90f;
        p.washDecaySeconds = 3.6f;
        p.bell = 0.16f;
        p.texture = 0.46f;
        p.velocitySensitivity = 0.72f;
        p.stereoWidth = 0.84f;
        p.outputGainDb = -11.5f;
        break;
    case 0u: // BALANCED CRASH
    default:
        break;
    }
    return p;
}

inline DrumCrashParams drumCrashSafeRandomParams(
    const DrumCrashParams& current, DrumRandom& random)
{
    const auto unit = [&]() { return random.unipolar(); };
    const auto range = [&](float minimum, float maximum) {
        return minimum + (maximum - minimum) * unit();
    };
    const auto logRange = [&](float minimum, float maximum) {
        return std::exp(range(std::log(minimum), std::log(maximum)));
    };

    DrumCrashParams p {};
    p.noteTracking = current.noteTracking;
    p.velocitySensitivity = current.velocitySensitivity;
    p.outputGainDb = current.outputGainDb;
    const uint32_t family = random.nextU32() % 4u;
    p.tuneHz = family == 1u ? logRange(760.0f, 1500.0f)
        : logRange(300.0f, 1050.0f);
    p.size = family == 0u ? range(0.62f, 0.98f) : range(0.08f, 0.88f);
    p.alloy = range(0.12f, 0.98f);
    p.spread = range(0.24f, 1.0f);
    p.density = std::round(range(7.0f, 16.0f));
    p.brightness = range(0.10f, 0.98f);
    p.attack = range(0.08f, 0.98f);
    p.decaySeconds = family == 0u ? logRange(1.6f, 5.8f)
        : family == 1u ? logRange(0.12f, 0.72f)
        : logRange(0.45f, 3.2f);
    p.damping = range(0.06f, 0.90f);
    p.wash = family == 2u ? range(0.04f, 0.42f) : range(0.38f, 1.0f);
    p.washDecaySeconds = p.decaySeconds * range(0.72f, 1.42f);
    p.bell = family == 2u ? range(0.52f, 0.98f) : range(0.02f, 0.58f);
    p.bellTuneHz = logRange(700.0f, 3600.0f);
    p.chokeTimeMs = logRange(2.0f, 55.0f);
    p.texture = range(0.08f, 1.0f);
    p.character.drive = range(0.0f, family == 3u ? 0.34f : 0.20f);
    p.character.bias = random.bipolar() * p.character.drive * 0.30f;
    p.character.compression = range(0.0f, 0.42f);
    const float conversion = unit() < 0.54f ? 0.0f : range(0.02f, 0.30f);
    p.character.sampleRateReduction = conversion;
    p.character.bitDepthReduction = conversion * range(0.20f, 0.92f);
    p.character.reconstruction = conversion * range(0.24f, 0.88f);
    p.character.tone = range(-0.40f, 0.38f);
    p.stereoWidth = range(0.16f, 1.0f);
    return drumSanitizeCrashParams(p);
}

inline DrumCrashParams drumCrashSafeRandomParams(
    const DrumCrashParams& current, uint32_t& state)
{
    DrumRandom random(state);
    DrumCrashParams params = drumCrashSafeRandomParams(current, random);
    state = random.state();
    return params;
}

inline int32_t drumCrashFactoryPresetIndex(const DrumCrashParams& params)
{
    const auto close = [](float left, float right) {
        return std::fabs(left - right) <= 1.0e-4f;
    };
    for (uint32_t index = 0u; index < kDrumCrashFactoryPresetCount; ++index) {
        const DrumCrashParams p = drumCrashFactoryPreset(index);
        if (close(params.tuneHz, p.tuneHz)
            && close(params.noteTracking, p.noteTracking)
            && close(params.size, p.size)
            && close(params.alloy, p.alloy)
            && close(params.spread, p.spread)
            && close(params.density, p.density)
            && close(params.brightness, p.brightness)
            && close(params.attack, p.attack)
            && close(params.decaySeconds, p.decaySeconds)
            && close(params.damping, p.damping)
            && close(params.wash, p.wash)
            && close(params.washDecaySeconds, p.washDecaySeconds)
            && close(params.bell, p.bell)
            && close(params.bellTuneHz, p.bellTuneHz)
            && close(params.chokeTimeMs, p.chokeTimeMs)
            && close(params.texture, p.texture)
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
