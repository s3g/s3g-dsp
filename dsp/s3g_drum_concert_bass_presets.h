#pragma once

#include "s3g_drum_concert_bass.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

struct DrumConcertBassFactoryPresetInfo {
    const char* name;
    const char* description;
};

inline constexpr uint32_t kDrumConcertBassFactoryPresetCount = 12u;

inline const DrumConcertBassFactoryPresetInfo&
drumConcertBassFactoryPresetInfo(uint32_t index)
{
    static constexpr std::array<DrumConcertBassFactoryPresetInfo,
        kDrumConcertBassFactoryPresetCount> info {{
        { "CONCERT FOUNDATION", "A dense large-head pressure response with broad low bloom." },
        { "SOFT HALL", "Soft beater contact, long low body, and open air pressure." },
        { "TIMPANI MALLET", "A focused kettle-weighted head with clear quasi-harmonic modes." },
        { "DAMPED CONCERT", "Short practical decay with a close, cloth-damped double head." },
        { "GRAND BLOOM", "A slowly clearing low-pressure bloom behind a dark centered impact." },
        { "KETTLE HEAD", "High tension, off-center excitation, and a defined orchestral pitch." },
        { "CONCERT EDGE", "Off-center excitation emphasizing upper membrane motion." },
        { "TAIKO OPEN", "Wide taiko-like body bands with a firm but finite stick onset." },
        { "MUTED THUD", "A dark articulation-ready setup with an especially short muted strike." },
        { "TAIKO RIM", "A resonant rim strike coupled lightly into a large paired head." },
        { "CINEMA O-DAIKO", "Very low extended pressure bands with broad, clean weight." },
        { "COARSE HALL", "An original reduced-rate color around a medium concert-drum body." },
    }};
    return info[std::min<uint32_t>(index,
        kDrumConcertBassFactoryPresetCount - 1u)];
}

inline DrumConcertBassParams drumConcertBassFactoryPreset(uint32_t index)
{
    DrumConcertBassParams p;
    switch (std::min<uint32_t>(index,
        kDrumConcertBassFactoryPresetCount - 1u)) {
    case 1u: // SOFT HALL
        p.tuneHz = 41.0f; p.size = 0.48f; p.headTension = 0.36f;
        p.strikePosition = 0.17f; p.beaterHardness = 0.14f;
        p.impact = 0.36f; p.body = 0.90f; p.bodyDecaySeconds = 3.8f;
        p.damping = 0.10f; p.bloom = 0.82f; p.air = 0.76f;
        p.shell = 0.18f; p.stereoWidth = 0.22f; p.outputGainDb = -8.0f;
        break;
    case 2u: // TIMPANI MALLET
        p.tuneHz = 68.0f; p.size = 0.14f; p.headTension = 0.72f;
        p.strikePosition = 0.70f; p.beaterHardness = 0.52f;
        p.impact = 0.66f; p.body = 0.82f; p.bodyDecaySeconds = 3.0f;
        p.damping = 0.16f; p.bloom = 0.42f; p.air = 0.72f;
        p.shell = 0.16f; p.shellTone = 0.52f;
        p.stereoWidth = 0.12f; p.outputGainDb = -8.5f;
        break;
    case 3u: // DAMPED CONCERT
        p.tuneHz = 46.0f; p.size = 0.54f; p.headTension = 0.52f;
        p.beaterHardness = 0.32f; p.impact = 0.60f; p.body = 0.76f;
        p.bodyDecaySeconds = 0.82f; p.damping = 0.72f;
        p.bloom = 0.24f; p.air = 0.28f; p.shell = 0.20f;
        p.mutedDecaySeconds = 0.18f; p.outputGainDb = -4.0f;
        break;
    case 4u: // GRAND BLOOM
        p.tuneHz = 34.0f; p.noteTracking = 0.32f; p.size = 0.68f;
        p.headTension = 0.25f; p.strikePosition = 0.10f;
        p.beaterHardness = 0.22f; p.impact = 0.42f; p.body = 0.96f;
        p.bodyDecaySeconds = 5.2f; p.damping = 0.08f;
        p.bloom = 0.94f; p.air = 0.90f; p.shell = 0.14f;
        p.stereoWidth = 0.12f; p.outputGainDb = -10.0f;
        break;
    case 5u: // KETTLE HEAD
        p.tuneHz = 62.0f; p.size = 0.08f; p.headTension = 0.85f;
        p.strikePosition = 0.73f; p.beaterHardness = 0.64f;
        p.impact = 0.70f; p.body = 0.78f; p.bodyDecaySeconds = 2.4f;
        p.damping = 0.25f; p.bloom = 0.35f; p.air = 0.78f;
        p.shell = 0.20f; p.shellTone = 0.58f; p.outputGainDb = -8.5f;
        break;
    case 6u: // CONCERT EDGE
        p.tuneHz = 45.0f; p.size = 0.56f; p.headTension = 0.56f;
        p.strikePosition = 0.88f; p.beaterHardness = 0.62f;
        p.impact = 0.70f; p.body = 0.66f; p.bodyDecaySeconds = 2.0f;
        p.damping = 0.24f; p.bloom = 0.44f; p.air = 0.42f;
        p.shell = 0.56f; p.shellTone = 0.66f;
        p.stereoWidth = 0.62f; p.outputGainDb = -5.0f;
        break;
    case 7u: // TAIKO OPEN
        p.tuneHz = 48.0f; p.size = 0.86f; p.headTension = 0.50f;
        p.strikePosition = 0.30f; p.beaterHardness = 0.55f;
        p.impact = 0.72f; p.body = 0.86f; p.bodyDecaySeconds = 3.8f;
        p.damping = 0.14f; p.bloom = 0.72f; p.air = 0.82f;
        p.shell = 0.42f; p.shellTone = 0.28f;
        p.rimLevel = 0.70f; p.stereoWidth = 0.44f;
        p.outputGainDb = -10.0f;
        break;
    case 8u: // MUTED THUD
        p.tuneHz = 39.0f; p.size = 0.82f; p.headTension = 0.34f;
        p.strikePosition = 0.18f; p.beaterHardness = 0.18f;
        p.impact = 0.52f; p.body = 0.84f; p.bodyDecaySeconds = 1.8f;
        p.damping = 0.58f; p.bloom = 0.32f; p.air = 0.34f;
        p.shell = 0.16f; p.mutedDecaySeconds = 0.085f;
        p.outputGainDb = -7.0f;
        break;
    case 9u: // TAIKO RIM
        p.tuneHz = 52.0f; p.size = 0.90f; p.headTension = 0.62f;
        p.strikePosition = 0.72f; p.beaterHardness = 0.72f;
        p.impact = 0.58f; p.body = 0.52f; p.bodyDecaySeconds = 1.4f;
        p.damping = 0.44f; p.bloom = 0.22f; p.air = 0.24f;
        p.shell = 0.46f; p.shellTone = 0.60f; p.rimLevel = 0.94f;
        p.rimTone = 0.52f; p.rimDecaySeconds = 0.28f;
        p.stereoWidth = 0.50f; p.outputGainDb = -10.0f;
        break;
    case 10u: // CINEMA O-DAIKO
        p.tuneHz = 30.0f; p.noteTracking = 0.25f; p.size = 1.0f;
        p.headTension = 0.18f; p.strikePosition = 0.14f;
        p.beaterHardness = 0.30f; p.impact = 0.68f; p.body = 1.0f;
        p.bodyDecaySeconds = 6.4f; p.damping = 0.06f;
        p.bloom = 1.0f; p.air = 0.92f; p.shell = 0.44f;
        p.shellTone = 0.25f; p.character.compression = 0.0f;
        p.stereoWidth = 0.72f; p.outputGainDb = -12.0f;
        break;
    case 11u: // COARSE HALL
        p.tuneHz = 49.0f; p.size = 0.54f; p.headTension = 0.54f;
        p.strikePosition = 0.38f; p.beaterHardness = 0.52f;
        p.impact = 0.68f; p.body = 0.74f; p.bodyDecaySeconds = 2.2f;
        p.damping = 0.25f; p.bloom = 0.56f; p.air = 0.54f;
        p.shell = 0.46f; p.character.drive = 0.22f;
        p.character.bias = -0.08f; p.character.compression = 0.20f;
        p.character.sampleRateReduction = 0.30f;
        p.character.bitDepthReduction = 0.18f;
        p.character.reconstruction = 0.24f;
        p.character.tone = -0.16f; p.stereoWidth = 0.36f;
        p.outputGainDb = -9.0f;
        break;
    case 0u: // CONCERT FOUNDATION
    default:
        break;
    }
    return p;
}

inline DrumConcertBassParams drumConcertBassSafeRandomParams(
    const DrumConcertBassParams& current, DrumRandom& random)
{
    const auto unit = [&]() { return random.unipolar(); };
    const auto range = [&](float low, float high) {
        return low + (high - low) * unit();
    };
    const auto logRange = [&](float low, float high) {
        return std::exp(range(std::log(low), std::log(high)));
    };
    DrumConcertBassParams p;
    p.noteTracking = current.noteTracking;
    p.velocitySensitivity = current.velocitySensitivity;
    p.outputGainDb = current.outputGainDb;
    const uint32_t family = random.nextU32() % 4u;
    p.tuneHz = logRange(family == 2u ? 24.0f : 30.0f,
        family == 1u ? 66.0f : 56.0f);
    p.size = family == 0u ? range(0.42f, 0.70f)
        : (family == 1u ? range(0.03f, 0.30f)
        : (family == 2u ? range(0.76f, 1.0f) : range(0.28f, 0.94f)));
    p.headTension = range(family == 2u ? 0.08f : 0.22f,
        family == 1u ? 0.94f : 0.72f);
    p.strikePosition = range(0.04f, family == 3u ? 0.92f : 0.62f);
    p.beaterHardness = range(family == 0u ? 0.04f : 0.18f,
        family == 1u ? 0.94f : 0.76f);
    p.impact = range(0.30f, 0.90f);
    p.body = range(0.50f, 0.98f);
    p.bodyDecaySeconds = logRange(family == 1u ? 0.35f : 0.75f,
        family == 2u ? 7.0f : 4.5f);
    p.damping = range(family == 2u ? 0.02f : 0.10f,
        family == 1u ? 0.82f : 0.58f);
    p.bloom = range(family == 1u ? 0.08f : 0.28f,
        family == 2u ? 1.0f : 0.88f);
    p.air = range(0.18f, 0.92f);
    p.shell = range(0.10f, family == 3u ? 0.86f : 0.68f);
    p.shellTone = range(0.08f, 0.82f);
    p.mutedDecaySeconds = logRange(0.055f, 0.72f);
    p.rimLevel = range(family == 3u ? 0.62f : 0.24f, 0.96f);
    p.rimTone = range(0.06f, 0.88f);
    p.rimDecaySeconds = logRange(0.045f, 0.42f);
    p.character.drive = range(0.0f, 0.15f);
    p.character.bias = random.bipolar() * p.character.drive * 0.45f;
    p.character.compression = range(0.0f, 0.20f);
    const float conversion = unit() < 0.80f ? 0.0f : range(0.03f, 0.22f);
    p.character.sampleRateReduction = conversion;
    p.character.bitDepthReduction = conversion * range(0.35f, 0.78f);
    p.character.reconstruction = conversion * range(0.35f, 0.90f);
    p.character.tone = range(-0.36f, 0.34f);
    p.stereoWidth = range(0.0f, 0.78f);
    return p;
}

inline DrumConcertBassParams drumConcertBassSafeRandomParams(
    const DrumConcertBassParams& current, uint32_t& state)
{
    DrumRandom random(state);
    const auto params = drumConcertBassSafeRandomParams(current, random);
    state = random.state();
    return params;
}

inline int32_t drumConcertBassFactoryPresetIndex(
    const DrumConcertBassParams& p)
{
    const auto close = [](float a, float b) {
        return std::fabs(a - b) <= 1.0e-4f;
    };
    for (uint32_t index = 0u; index < kDrumConcertBassFactoryPresetCount;
         ++index) {
        const auto q = drumConcertBassFactoryPreset(index);
        if (close(p.tuneHz, q.tuneHz) && close(p.noteTracking, q.noteTracking)
            && close(p.size, q.size) && close(p.headTension, q.headTension)
            && close(p.strikePosition, q.strikePosition)
            && close(p.beaterHardness, q.beaterHardness)
            && close(p.impact, q.impact) && close(p.body, q.body)
            && close(p.bodyDecaySeconds, q.bodyDecaySeconds)
            && close(p.damping, q.damping) && close(p.bloom, q.bloom)
            && close(p.air, q.air) && close(p.shell, q.shell)
            && close(p.shellTone, q.shellTone)
            && close(p.mutedDecaySeconds, q.mutedDecaySeconds)
            && close(p.rimLevel, q.rimLevel) && close(p.rimTone, q.rimTone)
            && close(p.rimDecaySeconds, q.rimDecaySeconds)
            && close(p.character.drive, q.character.drive)
            && close(p.character.bias, q.character.bias)
            && close(p.character.compression, q.character.compression)
            && close(p.character.sampleRateReduction,
                q.character.sampleRateReduction)
            && close(p.character.bitDepthReduction,
                q.character.bitDepthReduction)
            && close(p.character.reconstruction, q.character.reconstruction)
            && close(p.character.tone, q.character.tone)
            && close(p.stereoWidth, q.stereoWidth)
            && close(p.velocitySensitivity, q.velocitySensitivity)) {
            return static_cast<int32_t>(index);
        }
    }
    return -1;
}

} // namespace s3g
