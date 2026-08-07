#pragma once

#include "s3g_drum_clap.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

struct DrumClapFactoryPresetInfo {
    const char* name;
    const char* description;
};

inline constexpr uint32_t kDrumClapFactoryPresetCount = 14u;

inline const DrumClapFactoryPresetInfo&
drumClapFactoryPresetInfo(uint32_t index)
{
    static constexpr std::array<DrumClapFactoryPresetInfo,
        kDrumClapFactoryPresetCount> info {{
        { "BALANCED HANDS", "Four broad contacts with a controlled body and short natural tail." },
        { "TIGHT STACK", "Closely grouped dry contacts with a firm bright center." },
        { "DOUBLE FLAM", "Separated hand groups designed for the dedicated Flam articulation." },
        { "WIDE CROWD", "Seven irregular hands distributed across a broad stereo field." },
        { "DRY SNAP", "Minimal tail and body with one compact high-impact cluster." },
        { "DARK ROOM", "Lower color, stronger body and a diffuse shaded decay." },
        { "BRIGHT PAPER", "Thin bright hands with airy upper-band movement." },
        { "ANALOG BURST", "Rounded filtered-noise contacts and modest nonlinear color." },
        { "SAMPLED DUST", "Dense transient texture with restrained vintage conversion." },
        { "CASSETTE HALL", "Softened bandwidth, compression and a longer worn tail." },
        { "DIGITAL CRACK", "Hard narrow contacts with crystalline reduced-bit edges." },
        { "LOW BODY", "Pronounced low hand cavity beneath a darker contact cluster." },
        { "AIR CHAMBER", "Wideband contacts suspended in a long high-frequency decay." },
        { "SOFT PALMS", "Gradual contacts and rounded velocity response for gentle playing." },
    }};
    return info[std::min<uint32_t>(
        index, kDrumClapFactoryPresetCount - 1u)];
}

inline DrumClapParams drumClapFactoryPreset(uint32_t index)
{
    DrumClapParams p;
    switch (std::min<uint32_t>(index,
        kDrumClapFactoryPresetCount - 1u)) {
    case 1u: // TIGHT STACK
        p.toneHz = 4300.0f;
        p.hands = 4.0f;
        p.spreadMs = 12.0f;
        p.scatter = 0.18f;
        p.attack = 0.82f;
        p.bandwidth = 0.58f;
        p.air = 0.34f;
        p.burstDecaySeconds = 0.020f;
        p.tailDecaySeconds = 0.085f;
        p.tail = 0.25f;
        p.body = 0.18f;
        p.bodyTuneHz = 1120.0f;
        p.bodyDecaySeconds = 0.048f;
        p.flamTimeMs = 24.0f;
        p.texture = 0.62f;
        p.outputGainDb = -7.0f;
        break;
    case 2u: // DOUBLE FLAM
        p.toneHz = 3800.0f;
        p.hands = 5.0f;
        p.spreadMs = 22.0f;
        p.scatter = 0.36f;
        p.attack = 0.70f;
        p.bandwidth = 0.70f;
        p.air = 0.40f;
        p.burstDecaySeconds = 0.026f;
        p.tailDecaySeconds = 0.20f;
        p.tail = 0.44f;
        p.body = 0.28f;
        p.bodyTuneHz = 880.0f;
        p.bodyDecaySeconds = 0.080f;
        p.flamTimeMs = 48.0f;
        p.texture = 0.54f;
        p.stereoWidth = 0.44f;
        break;
    case 3u: // WIDE CROWD
        p.toneHz = 3500.0f;
        p.hands = 7.0f;
        p.spreadMs = 52.0f;
        p.scatter = 0.92f;
        p.attack = 0.50f;
        p.bandwidth = 0.82f;
        p.air = 0.58f;
        p.burstDecaySeconds = 0.038f;
        p.tailDecaySeconds = 0.34f;
        p.tail = 0.62f;
        p.body = 0.26f;
        p.bodyTuneHz = 760.0f;
        p.bodyDecaySeconds = 0.11f;
        p.flamTimeMs = 56.0f;
        p.texture = 0.70f;
        p.stereoWidth = 0.94f;
        p.outputGainDb = -9.5f;
        break;
    case 4u: // DRY SNAP
        p.toneHz = 5200.0f;
        p.hands = 2.0f;
        p.spreadMs = 5.0f;
        p.scatter = 0.08f;
        p.attack = 0.90f;
        p.bandwidth = 0.46f;
        p.air = 0.26f;
        p.burstDecaySeconds = 0.010f;
        p.tailDecaySeconds = 0.045f;
        p.tail = 0.05f;
        p.body = 0.10f;
        p.bodyTuneHz = 1480.0f;
        p.bodyDecaySeconds = 0.026f;
        p.flamTimeMs = 18.0f;
        p.texture = 0.76f;
        p.stereoWidth = 0.12f;
        p.outputGainDb = -6.0f;
        break;
    case 5u: // DARK ROOM
        p.toneHz = 2250.0f;
        p.hands = 5.0f;
        p.spreadMs = 36.0f;
        p.scatter = 0.60f;
        p.attack = 0.38f;
        p.bandwidth = 0.62f;
        p.air = 0.10f;
        p.burstDecaySeconds = 0.042f;
        p.tailDecaySeconds = 0.58f;
        p.tail = 0.78f;
        p.body = 0.66f;
        p.bodyTuneHz = 520.0f;
        p.bodyDecaySeconds = 0.18f;
        p.flamTimeMs = 44.0f;
        p.texture = 0.34f;
        p.character.compression = 0.24f;
        p.character.tone = -0.34f;
        p.outputGainDb = -8.5f;
        break;
    case 6u: // BRIGHT PAPER
        p.toneHz = 6900.0f;
        p.hands = 4.0f;
        p.spreadMs = 25.0f;
        p.scatter = 0.48f;
        p.attack = 0.78f;
        p.bandwidth = 0.86f;
        p.air = 0.92f;
        p.burstDecaySeconds = 0.018f;
        p.tailDecaySeconds = 0.14f;
        p.tail = 0.36f;
        p.body = 0.07f;
        p.bodyTuneHz = 1860.0f;
        p.bodyDecaySeconds = 0.038f;
        p.flamTimeMs = 30.0f;
        p.texture = 0.82f;
        p.outputGainDb = -9.0f;
        break;
    case 7u: // ANALOG BURST
        p.toneHz = 3150.0f;
        p.hands = 4.0f;
        p.spreadMs = 31.0f;
        p.scatter = 0.32f;
        p.attack = 0.58f;
        p.bandwidth = 0.54f;
        p.air = 0.24f;
        p.burstDecaySeconds = 0.034f;
        p.tailDecaySeconds = 0.21f;
        p.tail = 0.50f;
        p.body = 0.42f;
        p.bodyTuneHz = 820.0f;
        p.bodyDecaySeconds = 0.095f;
        p.flamTimeMs = 36.0f;
        p.texture = 0.30f;
        p.character.drive = 0.16f;
        p.character.compression = 0.10f;
        p.outputGainDb = -7.5f;
        break;
    case 8u: // SAMPLED DUST
        p.toneHz = 4100.0f;
        p.hands = 6.0f;
        p.spreadMs = 38.0f;
        p.scatter = 0.68f;
        p.attack = 0.68f;
        p.bandwidth = 0.76f;
        p.air = 0.46f;
        p.burstDecaySeconds = 0.032f;
        p.tailDecaySeconds = 0.25f;
        p.tail = 0.56f;
        p.body = 0.24f;
        p.bodyTuneHz = 1020.0f;
        p.bodyDecaySeconds = 0.072f;
        p.flamTimeMs = 40.0f;
        p.texture = 0.88f;
        p.character.sampleRateReduction = 0.12f;
        p.character.bitDepthReduction = 0.10f;
        p.character.reconstruction = 0.12f;
        p.outputGainDb = -8.5f;
        break;
    case 9u: // CASSETTE HALL
        p.toneHz = 2800.0f;
        p.hands = 5.0f;
        p.spreadMs = 44.0f;
        p.scatter = 0.56f;
        p.attack = 0.32f;
        p.bandwidth = 0.66f;
        p.air = 0.18f;
        p.burstDecaySeconds = 0.044f;
        p.tailDecaySeconds = 0.92f;
        p.tail = 0.84f;
        p.body = 0.38f;
        p.bodyTuneHz = 690.0f;
        p.bodyDecaySeconds = 0.14f;
        p.flamTimeMs = 52.0f;
        p.texture = 0.46f;
        p.character.drive = 0.20f;
        p.character.bias = 0.06f;
        p.character.compression = 0.36f;
        p.character.tone = -0.30f;
        p.outputGainDb = -8.5f;
        break;
    case 10u: // DIGITAL CRACK
        p.toneHz = 6100.0f;
        p.hands = 3.0f;
        p.spreadMs = 18.0f;
        p.scatter = 0.24f;
        p.attack = 0.94f;
        p.bandwidth = 0.40f;
        p.air = 0.68f;
        p.burstDecaySeconds = 0.014f;
        p.tailDecaySeconds = 0.095f;
        p.tail = 0.24f;
        p.body = 0.08f;
        p.bodyTuneHz = 2200.0f;
        p.bodyDecaySeconds = 0.025f;
        p.flamTimeMs = 24.0f;
        p.texture = 0.96f;
        p.character.bitDepthReduction = 0.30f;
        p.character.reconstruction = 0.16f;
        p.outputGainDb = -8.5f;
        break;
    case 11u: // LOW BODY
        p.toneHz = 2600.0f;
        p.hands = 4.0f;
        p.spreadMs = 29.0f;
        p.scatter = 0.40f;
        p.attack = 0.48f;
        p.bandwidth = 0.56f;
        p.air = 0.12f;
        p.burstDecaySeconds = 0.036f;
        p.tailDecaySeconds = 0.22f;
        p.tail = 0.42f;
        p.body = 0.94f;
        p.bodyTuneHz = 390.0f;
        p.bodyDecaySeconds = 0.24f;
        p.flamTimeMs = 38.0f;
        p.texture = 0.38f;
        p.outputGainDb = -8.0f;
        break;
    case 12u: // AIR CHAMBER
        p.toneHz = 4700.0f;
        p.hands = 6.0f;
        p.spreadMs = 46.0f;
        p.scatter = 0.76f;
        p.attack = 0.52f;
        p.bandwidth = 0.94f;
        p.air = 0.96f;
        p.burstDecaySeconds = 0.040f;
        p.tailDecaySeconds = 0.72f;
        p.tail = 0.88f;
        p.body = 0.18f;
        p.bodyTuneHz = 1240.0f;
        p.bodyDecaySeconds = 0.085f;
        p.flamTimeMs = 58.0f;
        p.texture = 0.66f;
        p.stereoWidth = 0.86f;
        p.outputGainDb = -10.0f;
        break;
    case 13u: // SOFT PALMS
        p.toneHz = 3200.0f;
        p.hands = 5.0f;
        p.spreadMs = 40.0f;
        p.scatter = 0.52f;
        p.attack = 0.10f;
        p.bandwidth = 0.72f;
        p.air = 0.32f;
        p.burstDecaySeconds = 0.052f;
        p.tailDecaySeconds = 0.38f;
        p.tail = 0.70f;
        p.body = 0.46f;
        p.bodyTuneHz = 720.0f;
        p.bodyDecaySeconds = 0.15f;
        p.flamTimeMs = 50.0f;
        p.texture = 0.24f;
        p.velocitySensitivity = 0.70f;
        p.outputGainDb = -7.5f;
        break;
    case 0u: // BALANCED HANDS
    default:
        break;
    }
    return p;
}

inline DrumClapParams drumClapSafeRandomParams(
    const DrumClapParams& current, DrumRandom& random)
{
    const auto unit = [&]() { return random.unipolar(); };
    const auto range = [&](float minimum, float maximum) {
        return minimum + (maximum - minimum) * unit();
    };
    const auto logRange = [&](float minimum, float maximum) {
        return std::exp(range(std::log(minimum), std::log(maximum)));
    };

    DrumClapParams p {};
    p.noteTracking = current.noteTracking;
    p.velocitySensitivity = current.velocitySensitivity;
    p.outputGainDb = current.outputGainDb;

    const uint32_t family = random.nextU32() % 5u;
    switch (family) {
    case 0u: // compact electronic
        p.toneHz = logRange(3200.0f, 6200.0f);
        p.hands = std::round(range(2.0f, 5.0f));
        p.spreadMs = logRange(5.0f, 24.0f);
        p.scatter = range(0.04f, 0.42f);
        p.attack = range(0.58f, 0.94f);
        p.bandwidth = range(0.34f, 0.72f);
        p.tailDecaySeconds = logRange(0.045f, 0.20f);
        p.tail = range(0.04f, 0.42f);
        break;
    case 1u: // broad hand ensemble
        p.toneHz = logRange(2800.0f, 5200.0f);
        p.hands = std::round(range(4.0f, 8.0f));
        p.spreadMs = range(26.0f, 58.0f);
        p.scatter = range(0.46f, 0.94f);
        p.attack = range(0.28f, 0.76f);
        p.bandwidth = range(0.68f, 0.96f);
        p.tailDecaySeconds = logRange(0.16f, 0.70f);
        p.tail = range(0.44f, 0.88f);
        break;
    case 2u: // dark body
        p.toneHz = logRange(1700.0f, 3300.0f);
        p.hands = std::round(range(3.0f, 6.0f));
        p.spreadMs = range(18.0f, 46.0f);
        p.scatter = range(0.24f, 0.72f);
        p.attack = range(0.16f, 0.62f);
        p.bandwidth = range(0.38f, 0.74f);
        p.tailDecaySeconds = logRange(0.14f, 0.90f);
        p.tail = range(0.36f, 0.84f);
        break;
    case 3u: // bright and thin
        p.toneHz = logRange(4800.0f, 8200.0f);
        p.hands = std::round(range(2.0f, 6.0f));
        p.spreadMs = range(8.0f, 38.0f);
        p.scatter = range(0.12f, 0.66f);
        p.attack = range(0.62f, 0.96f);
        p.bandwidth = range(0.56f, 0.96f);
        p.tailDecaySeconds = logRange(0.06f, 0.42f);
        p.tail = range(0.16f, 0.66f);
        break;
    case 4u:
    default: // worn machine clap
        p.toneHz = logRange(2300.0f, 4600.0f);
        p.hands = std::round(range(3.0f, 7.0f));
        p.spreadMs = range(20.0f, 52.0f);
        p.scatter = range(0.26f, 0.78f);
        p.attack = range(0.26f, 0.78f);
        p.bandwidth = range(0.42f, 0.84f);
        p.tailDecaySeconds = logRange(0.10f, 0.62f);
        p.tail = range(0.28f, 0.78f);
        break;
    }

    p.air = family == 3u ? range(0.58f, 0.96f)
        : range(0.06f, 0.74f);
    p.burstDecaySeconds = logRange(0.009f, 0.070f);
    p.body = family == 2u ? range(0.54f, 0.94f)
        : range(0.03f, 0.62f);
    p.bodyTuneHz = family == 2u ? logRange(340.0f, 880.0f)
        : logRange(560.0f, 2100.0f);
    p.bodyDecaySeconds = logRange(0.024f, 0.24f);
    p.flamTimeMs = range(14.0f, 78.0f);
    p.texture = range(0.12f, 0.94f);

    p.character.drive = range(0.0f, family == 4u ? 0.34f : 0.22f);
    p.character.bias = random.bipolar()
        * (0.02f + p.character.drive * 0.38f);
    p.character.compression = range(0.0f, 0.44f);
    const float conversionDraw = unit();
    const float conversion = conversionDraw < 0.42f ? 0.0f
        : std::pow((conversionDraw - 0.42f) / 0.58f, 2.0f) * 0.48f;
    p.character.sampleRateReduction = conversion * range(0.44f, 1.0f);
    p.character.bitDepthReduction = conversion * range(0.28f, 0.82f);
    p.character.reconstruction = conversion == 0.0f ? 0.0f
        : std::min(0.52f, conversion * range(0.42f, 0.94f));
    p.character.tone = range(-0.42f, 0.38f);
    p.stereoWidth = 0.94f * std::pow(unit(), 1.12f);
    return drumSanitizeClapParams(p);
}

inline DrumClapParams drumClapSafeRandomParams(
    const DrumClapParams& current, uint32_t& state)
{
    DrumRandom random(state);
    DrumClapParams params = drumClapSafeRandomParams(current, random);
    state = random.state();
    return params;
}

inline int32_t drumClapFactoryPresetIndex(const DrumClapParams& params)
{
    const auto close = [](float left, float right) {
        return std::fabs(left - right) <= 1.0e-4f;
    };
    for (uint32_t index = 0u; index < kDrumClapFactoryPresetCount; ++index) {
        const DrumClapParams preset = drumClapFactoryPreset(index);
        if (close(params.toneHz, preset.toneHz)
            && close(params.noteTracking, preset.noteTracking)
            && close(params.hands, preset.hands)
            && close(params.spreadMs, preset.spreadMs)
            && close(params.scatter, preset.scatter)
            && close(params.attack, preset.attack)
            && close(params.bandwidth, preset.bandwidth)
            && close(params.air, preset.air)
            && close(params.burstDecaySeconds,
                preset.burstDecaySeconds)
            && close(params.tailDecaySeconds, preset.tailDecaySeconds)
            && close(params.tail, preset.tail)
            && close(params.body, preset.body)
            && close(params.bodyTuneHz, preset.bodyTuneHz)
            && close(params.bodyDecaySeconds, preset.bodyDecaySeconds)
            && close(params.flamTimeMs, preset.flamTimeMs)
            && close(params.texture, preset.texture)
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
