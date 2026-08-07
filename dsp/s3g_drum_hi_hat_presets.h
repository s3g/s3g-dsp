#pragma once

#include "s3g_drum_hi_hat.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

struct DrumHiHatFactoryPresetInfo {
    const char* name;
    const char* description;
};

inline constexpr uint32_t kDrumHiHatFactoryPresetCount = 14u;

inline const DrumHiHatFactoryPresetInfo&
drumHiHatFactoryPresetInfo(uint32_t index)
{
    static constexpr std::array<DrumHiHatFactoryPresetInfo,
        kDrumHiHatFactoryPresetCount> info {{
        { "BALANCED ALLOY", "A versatile metallic closed, open and pedal hat set." },
        { "TIGHT CIRCUIT", "Compact closed hats with a firm electronic chick." },
        { "OPEN CURRENT", "Long flowing open wash with a controlled choke." },
        { "PEDAL CHICK", "Pronounced foot closure and dry short metal." },
        { "ANALOG CLUSTER", "Inharmonic oscillator metal with restrained noise wash." },
        { "SAMPLED STEEL", "Dense even-spectrum hats shaped like early drum-machine playback." },
        { "DARK TAPE", "Muted bandwidth, compression and softly worn conversion." },
        { "BRIGHT FOIL", "Airy high metal with short sparkling closed articulation." },
        { "LOOSE WASH", "Broad noisy cymbal wash and a relaxed long opening." },
        { "DRY TICK", "Minimal wash, compact modes and an extremely short close." },
        { "DIGITAL GLASS", "Scattered high modes with clear low-bit crystalline edges." },
        { "BROKEN ROM", "Reduced-rate percussion metal with a filtered reconstruction tail." },
        { "WIDE SIZZLE", "Wide upper partials and sustained animated side energy." },
        { "SOFT BRUSH", "Rounded low-velocity response with a gentle gradual contact." },
    }};
    return info[std::min<uint32_t>(
        index, kDrumHiHatFactoryPresetCount - 1u)];
}

inline DrumHiHatParams drumHiHatFactoryPreset(uint32_t index)
{
    DrumHiHatParams p;
    switch (std::min<uint32_t>(index,
        kDrumHiHatFactoryPresetCount - 1u)) {
    case 1u: // TIGHT CIRCUIT
        p.tuneHz = 1040.0f;
        p.alloy = 0.76f;
        p.spread = 0.42f;
        p.density = 0.66f;
        p.tone = 0.70f;
        p.air = 0.56f;
        p.attack = 0.82f;
        p.closedDecaySeconds = 0.075f;
        p.openDecaySeconds = 0.58f;
        p.pedalDecaySeconds = 0.16f;
        p.wash = 0.38f;
        p.chick = 0.70f;
        p.chickTone = 0.66f;
        p.sizzle = 0.12f;
        p.chokeTimeMs = 3.5f;
        p.outputGainDb = -7.5f;
        break;
    case 2u: // OPEN CURRENT
        p.tuneHz = 940.0f;
        p.alloy = 0.58f;
        p.spread = 0.70f;
        p.density = 0.86f;
        p.tone = 0.57f;
        p.air = 0.66f;
        p.attack = 0.54f;
        p.closedDecaySeconds = 0.22f;
        p.openDecaySeconds = 2.65f;
        p.pedalDecaySeconds = 0.38f;
        p.wash = 0.82f;
        p.chick = 0.34f;
        p.chickTone = 0.48f;
        p.sizzle = 0.58f;
        p.chokeTimeMs = 13.0f;
        p.stereoWidth = 0.34f;
        p.outputGainDb = -10.0f;
        break;
    case 3u: // PEDAL CHICK
        p.tuneHz = 1280.0f;
        p.alloy = 0.46f;
        p.spread = 0.48f;
        p.density = 0.55f;
        p.tone = 0.58f;
        p.air = 0.42f;
        p.attack = 0.50f;
        p.closedDecaySeconds = 0.11f;
        p.openDecaySeconds = 0.82f;
        p.pedalDecaySeconds = 0.24f;
        p.wash = 0.30f;
        p.chick = 0.94f;
        p.chickTone = 0.42f;
        p.sizzle = 0.08f;
        p.chokeTimeMs = 5.0f;
        p.outputGainDb = -7.0f;
        break;
    case 4u: // ANALOG CLUSTER
        p.tuneHz = 760.0f;
        p.alloy = 0.92f;
        p.spread = 0.22f;
        p.density = 0.82f;
        p.tone = 0.46f;
        p.air = 0.38f;
        p.attack = 0.74f;
        p.closedDecaySeconds = 0.13f;
        p.openDecaySeconds = 0.92f;
        p.pedalDecaySeconds = 0.22f;
        p.wash = 0.22f;
        p.chick = 0.54f;
        p.chickTone = 0.50f;
        p.sizzle = 0.18f;
        p.chokeTimeMs = 4.5f;
        p.character.drive = 0.12f;
        p.outputGainDb = -7.5f;
        break;
    case 5u: // SAMPLED STEEL
        p.tuneHz = 1320.0f;
        p.alloy = 0.52f;
        p.spread = 0.58f;
        p.density = 0.92f;
        p.tone = 0.62f;
        p.air = 0.48f;
        p.attack = 0.72f;
        p.closedDecaySeconds = 0.16f;
        p.openDecaySeconds = 1.45f;
        p.pedalDecaySeconds = 0.30f;
        p.wash = 0.72f;
        p.chick = 0.46f;
        p.chickTone = 0.56f;
        p.sizzle = 0.24f;
        p.character.compression = 0.20f;
        p.character.sampleRateReduction = 0.10f;
        p.character.bitDepthReduction = 0.08f;
        p.character.reconstruction = 0.10f;
        p.outputGainDb = -8.5f;
        break;
    case 6u: // DARK TAPE
        p.tuneHz = 860.0f;
        p.alloy = 0.44f;
        p.spread = 0.50f;
        p.density = 0.78f;
        p.tone = 0.24f;
        p.air = 0.12f;
        p.attack = 0.40f;
        p.closedDecaySeconds = 0.20f;
        p.openDecaySeconds = 1.62f;
        p.pedalDecaySeconds = 0.38f;
        p.wash = 0.64f;
        p.chick = 0.36f;
        p.chickTone = 0.22f;
        p.sizzle = 0.16f;
        p.chokeTimeMs = 10.0f;
        p.character.drive = 0.20f;
        p.character.bias = 0.08f;
        p.character.compression = 0.38f;
        p.character.tone = -0.42f;
        p.outputGainDb = -7.5f;
        break;
    case 7u: // BRIGHT FOIL
        p.tuneHz = 1880.0f;
        p.alloy = 0.68f;
        p.spread = 0.72f;
        p.density = 0.78f;
        p.tone = 0.88f;
        p.air = 0.94f;
        p.attack = 0.88f;
        p.closedDecaySeconds = 0.085f;
        p.openDecaySeconds = 0.72f;
        p.pedalDecaySeconds = 0.16f;
        p.wash = 0.54f;
        p.chick = 0.42f;
        p.chickTone = 0.88f;
        p.sizzle = 0.36f;
        p.chokeTimeMs = 2.5f;
        p.outputGainDb = -9.0f;
        break;
    case 8u: // LOOSE WASH
        p.tuneHz = 720.0f;
        p.alloy = 0.38f;
        p.spread = 0.84f;
        p.density = 0.92f;
        p.tone = 0.52f;
        p.air = 0.74f;
        p.attack = 0.34f;
        p.closedDecaySeconds = 0.30f;
        p.openDecaySeconds = 3.25f;
        p.pedalDecaySeconds = 0.52f;
        p.wash = 0.94f;
        p.chick = 0.22f;
        p.chickTone = 0.40f;
        p.sizzle = 0.72f;
        p.chokeTimeMs = 24.0f;
        p.stereoWidth = 0.48f;
        p.outputGainDb = -10.5f;
        break;
    case 9u: // DRY TICK
        p.tuneHz = 1480.0f;
        p.alloy = 0.74f;
        p.spread = 0.28f;
        p.density = 0.34f;
        p.tone = 0.54f;
        p.air = 0.30f;
        p.attack = 0.70f;
        p.closedDecaySeconds = 0.045f;
        p.openDecaySeconds = 0.34f;
        p.pedalDecaySeconds = 0.085f;
        p.wash = 0.08f;
        p.chick = 0.76f;
        p.chickTone = 0.68f;
        p.sizzle = 0.02f;
        p.chokeTimeMs = 1.4f;
        p.outputGainDb = -6.5f;
        break;
    case 10u: // DIGITAL GLASS
        p.tuneHz = 2140.0f;
        p.alloy = 0.84f;
        p.spread = 0.96f;
        p.density = 0.66f;
        p.tone = 0.78f;
        p.air = 0.86f;
        p.attack = 0.78f;
        p.closedDecaySeconds = 0.12f;
        p.openDecaySeconds = 1.10f;
        p.pedalDecaySeconds = 0.18f;
        p.wash = 0.26f;
        p.chick = 0.36f;
        p.chickTone = 0.78f;
        p.sizzle = 0.46f;
        p.character.bitDepthReduction = 0.22f;
        p.character.reconstruction = 0.12f;
        p.outputGainDb = -9.5f;
        break;
    case 11u: // BROKEN ROM
        p.tuneHz = 1180.0f;
        p.alloy = 0.56f;
        p.spread = 0.62f;
        p.density = 0.72f;
        p.tone = 0.48f;
        p.air = 0.36f;
        p.attack = 0.68f;
        p.closedDecaySeconds = 0.18f;
        p.openDecaySeconds = 1.22f;
        p.pedalDecaySeconds = 0.28f;
        p.wash = 0.62f;
        p.chick = 0.44f;
        p.chickTone = 0.52f;
        p.sizzle = 0.32f;
        p.character.sampleRateReduction = 0.48f;
        p.character.bitDepthReduction = 0.34f;
        p.character.reconstruction = 0.42f;
        p.character.tone = -0.14f;
        p.outputGainDb = -9.0f;
        break;
    case 12u: // WIDE SIZZLE
        p.tuneHz = 1260.0f;
        p.alloy = 0.70f;
        p.spread = 0.88f;
        p.density = 0.96f;
        p.tone = 0.68f;
        p.air = 0.82f;
        p.attack = 0.62f;
        p.closedDecaySeconds = 0.24f;
        p.openDecaySeconds = 2.30f;
        p.pedalDecaySeconds = 0.40f;
        p.wash = 0.78f;
        p.chick = 0.30f;
        p.chickTone = 0.62f;
        p.sizzle = 0.92f;
        p.chokeTimeMs = 16.0f;
        p.stereoWidth = 0.94f;
        p.outputGainDb = -10.0f;
        break;
    case 13u: // SOFT BRUSH
        p.tuneHz = 980.0f;
        p.alloy = 0.32f;
        p.spread = 0.66f;
        p.density = 0.86f;
        p.tone = 0.42f;
        p.air = 0.46f;
        p.attack = 0.12f;
        p.closedDecaySeconds = 0.25f;
        p.openDecaySeconds = 1.80f;
        p.pedalDecaySeconds = 0.46f;
        p.wash = 0.88f;
        p.chick = 0.14f;
        p.chickTone = 0.28f;
        p.sizzle = 0.28f;
        p.chokeTimeMs = 20.0f;
        p.velocitySensitivity = 0.72f;
        p.outputGainDb = -8.0f;
        break;
    case 0u: // BALANCED ALLOY
    default:
        break;
    }
    return p;
}

inline DrumHiHatParams drumHiHatSafeRandomParams(
    const DrumHiHatParams& current, DrumRandom& random)
{
    const auto unit = [&]() { return random.unipolar(); };
    const auto range = [&](float minimum, float maximum) {
        return minimum + (maximum - minimum) * unit();
    };
    const auto logRange = [&](float minimum, float maximum) {
        return std::exp(range(std::log(minimum), std::log(maximum)));
    };

    DrumHiHatParams p {};
    // These three are performance/mix decisions, not timbre randomization.
    p.noteTracking = current.noteTracking;
    p.velocitySensitivity = current.velocitySensitivity;
    p.outputGainDb = current.outputGainDb;

    const uint32_t family = random.nextU32() % 5u;
    switch (family) {
    case 0u: // analog metal cluster
        p.tuneHz = logRange(620.0f, 1180.0f);
        p.alloy = range(0.62f, 0.96f);
        p.spread = range(0.14f, 0.52f);
        p.density = range(0.48f, 0.88f);
        p.tone = range(0.34f, 0.72f);
        p.air = range(0.24f, 0.64f);
        p.wash = range(0.12f, 0.54f);
        break;
    case 1u: // dense machine sample
        p.tuneHz = logRange(920.0f, 1660.0f);
        p.alloy = range(0.36f, 0.74f);
        p.spread = range(0.42f, 0.78f);
        p.density = range(0.74f, 0.98f);
        p.tone = range(0.42f, 0.76f);
        p.air = range(0.34f, 0.74f);
        p.wash = range(0.52f, 0.88f);
        break;
    case 2u: // bright foil
        p.tuneHz = logRange(1480.0f, 2600.0f);
        p.alloy = range(0.48f, 0.90f);
        p.spread = range(0.58f, 0.98f);
        p.density = range(0.54f, 0.92f);
        p.tone = range(0.68f, 0.96f);
        p.air = range(0.72f, 0.98f);
        p.wash = range(0.28f, 0.72f);
        break;
    case 3u: // dark and loose
        p.tuneHz = logRange(560.0f, 1120.0f);
        p.alloy = range(0.22f, 0.62f);
        p.spread = range(0.48f, 0.92f);
        p.density = range(0.70f, 0.98f);
        p.tone = range(0.12f, 0.48f);
        p.air = range(0.08f, 0.48f);
        p.wash = range(0.68f, 0.96f);
        break;
    case 4u:
    default: // dry chick-forward machine
        p.tuneHz = logRange(920.0f, 1900.0f);
        p.alloy = range(0.46f, 0.88f);
        p.spread = range(0.18f, 0.62f);
        p.density = range(0.26f, 0.72f);
        p.tone = range(0.38f, 0.82f);
        p.air = range(0.22f, 0.72f);
        p.wash = range(0.04f, 0.38f);
        break;
    }

    p.attack = range(0.12f, 0.92f);
    p.closedDecaySeconds = logRange(0.045f, 0.34f);
    p.openDecaySeconds = logRange(
        std::max(0.30f, p.closedDecaySeconds * 3.2f), 3.20f);
    p.pedalDecaySeconds = logRange(0.075f, 0.62f);
    p.chick = family == 4u ? range(0.58f, 0.94f)
        : range(0.12f, 0.78f);
    p.chickTone = range(0.12f, 0.92f);
    p.sizzle = family == 3u ? range(0.38f, 0.88f)
        : range(0.02f, 0.72f);
    p.chokeTimeMs = logRange(1.2f, 24.0f);

    p.character.drive = range(0.0f, family == 3u ? 0.34f : 0.24f);
    p.character.bias = random.bipolar()
        * (0.02f + p.character.drive * 0.38f);
    p.character.compression = range(0.0f, 0.42f);
    const float conversionDraw = unit();
    const float conversion = conversionDraw < 0.42f ? 0.0f
        : std::pow((conversionDraw - 0.42f) / 0.58f, 2.0f) * 0.48f;
    p.character.sampleRateReduction = conversion * range(0.46f, 1.0f);
    p.character.bitDepthReduction = conversion * range(0.30f, 0.82f);
    p.character.reconstruction = conversion == 0.0f ? 0.0f
        : std::min(0.52f, conversion * range(0.42f, 0.94f));
    p.character.tone = range(-0.42f, 0.38f);
    p.stereoWidth = 0.94f * std::pow(unit(), 1.16f);
    return drumSanitizeHiHatParams(p);
}

inline DrumHiHatParams drumHiHatSafeRandomParams(
    const DrumHiHatParams& current, uint32_t& state)
{
    DrumRandom random(state);
    DrumHiHatParams params = drumHiHatSafeRandomParams(current, random);
    state = random.state();
    return params;
}

inline int32_t drumHiHatFactoryPresetIndex(const DrumHiHatParams& params)
{
    const auto close = [](float left, float right) {
        return std::fabs(left - right) <= 1.0e-4f;
    };
    for (uint32_t index = 0u;
         index < kDrumHiHatFactoryPresetCount; ++index) {
        const DrumHiHatParams preset = drumHiHatFactoryPreset(index);
        if (close(params.tuneHz, preset.tuneHz)
            && close(params.noteTracking, preset.noteTracking)
            && close(params.alloy, preset.alloy)
            && close(params.spread, preset.spread)
            && close(params.density, preset.density)
            && close(params.tone, preset.tone)
            && close(params.air, preset.air)
            && close(params.attack, preset.attack)
            && close(params.closedDecaySeconds,
                preset.closedDecaySeconds)
            && close(params.openDecaySeconds, preset.openDecaySeconds)
            && close(params.pedalDecaySeconds,
                preset.pedalDecaySeconds)
            && close(params.wash, preset.wash)
            && close(params.chick, preset.chick)
            && close(params.chickTone, preset.chickTone)
            && close(params.sizzle, preset.sizzle)
            && close(params.chokeTimeMs, preset.chokeTimeMs)
            && close(params.character.drive, preset.character.drive)
            && close(params.character.bias, preset.character.bias)
            && close(params.character.compression,
                preset.character.compression)
            && close(params.character.sampleRateReduction,
                preset.character.sampleRateReduction)
            && close(params.character.bitDepthReduction,
                preset.character.bitDepthReduction)
            && close(params.character.reconstruction,
                preset.character.reconstruction)
            && close(params.character.tone, preset.character.tone)
            && close(params.stereoWidth, preset.stereoWidth)
            && close(params.velocitySensitivity,
                preset.velocitySensitivity)) {
            return static_cast<int32_t>(index);
        }
    }
    return -1;
}

} // namespace s3g
