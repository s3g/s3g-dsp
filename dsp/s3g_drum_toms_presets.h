#pragma once

#include "s3g_drum_toms.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

struct DrumTomsFactoryPresetInfo {
    const char* name;
    const char* description;
};

inline constexpr uint32_t kDrumTomsFactoryPresetCount = 12u;

inline const DrumTomsFactoryPresetInfo&
drumTomsFactoryPresetInfo(uint32_t index)
{
    static constexpr std::array<DrumTomsFactoryPresetInfo,
        kDrumTomsFactoryPresetCount> info {{
        { "BALANCED RACK", "Ordered low, mid and high toms with a versatile shared head." },
        { "DEEP THREE", "Low-set correlated rack with warm bodies and controlled attacks." },
        { "OPEN THREE", "Longer shell modes and a wider decay contour across the rack." },
        { "SHORT THREE", "Very compact damped tom set with firm early transients." },
        { "ASCENDING WOOD", "Irregular acoustic-like intervals and hollow rim character." },
        { "HIGH TENSION", "Raised rack tuning with brighter attacks and short high decay." },
        { "SOFT MALLETS", "Low-punch rounded heads with restrained rim and stick energy." },
        { "RIM KNOCKS", "Dark electronic rim articulation across three tuned shells." },
        { "WOOD STICKS", "Hollow cross-stick family with medium tonal rim modes." },
        { "BRIGHT EDGES", "Short noisy rimshots and bright stick articulation." },
        { "ELECTRIC FALL", "Stronger pitch dives and compact synthesized tom bodies." },
        { "WIDE RACK", "Centered lows with modest low-to-high stereo placement above them." },
    }};
    return info[std::min<uint32_t>(
        index, kDrumTomsFactoryPresetCount - 1u)];
}

inline DrumTomsParams drumTomsFactoryPreset(uint32_t index)
{
    DrumTomsParams p;
    switch (std::min<uint32_t>(index, kDrumTomsFactoryPresetCount - 1u)) {
    case 1u: // DEEP THREE
        p.lowTuneHz = 65.0f;
        p.midTuneHz = 98.0f;
        p.highTuneHz = 127.0f;
        p.pitchDropSemitones = 4.0f;
        p.pitchSweepMs = 48.0f;
        p.shellSpread = 0.34f;
        p.body = 0.76f;
        p.ring = 0.34f;
        p.bodyDecaySeconds = 0.82f;
        p.decaySpread = 0.42f;
        p.punch = 0.62f;
        p.rimCharacter = 0.36f;
        p.outputGainDb = -7.0f;
        break;
    case 2u: // OPEN THREE
        p.lowTuneHz = 78.0f;
        p.midTuneHz = 118.0f;
        p.highTuneHz = 166.0f;
        p.pitchDropSemitones = 3.0f;
        p.pitchSweepMs = 54.0f;
        p.shellSpread = 0.64f;
        p.body = 0.72f;
        p.ring = 0.68f;
        p.bodyDecaySeconds = 0.94f;
        p.decaySpread = 0.34f;
        p.punch = 0.52f;
        p.rimDecaySeconds = 0.084f;
        p.outputGainDb = -8.0f;
        break;
    case 3u: // SHORT THREE
        p.lowTuneHz = 112.0f;
        p.midTuneHz = 164.0f;
        p.highTuneHz = 286.0f;
        p.pitchDropSemitones = 2.0f;
        p.pitchSweepMs = 13.0f;
        p.shellSpread = 0.24f;
        p.body = 0.48f;
        p.ring = 0.10f;
        p.bodyDecaySeconds = 0.095f;
        p.decaySpread = 0.16f;
        p.punch = 0.90f;
        p.rimDecaySeconds = 0.040f;
        p.stickTone = 0.74f;
        p.outputGainDb = -6.0f;
        break;
    case 4u: // ASCENDING WOOD
        p.lowTuneHz = 73.0f;
        p.midTuneHz = 135.0f;
        p.highTuneHz = 182.0f;
        p.pitchDropSemitones = 2.5f;
        p.pitchSweepMs = 34.0f;
        p.shellSpread = 0.70f;
        p.body = 0.66f;
        p.ring = 0.52f;
        p.bodyDecaySeconds = 0.54f;
        p.decaySpread = 0.26f;
        p.punch = 0.70f;
        p.rimLevel = 0.72f;
        p.rimCharacter = 0.50f;
        p.rimDecaySeconds = 0.082f;
        p.stickTone = 0.48f;
        p.outputGainDb = -7.5f;
        break;
    case 5u: // HIGH TENSION
        p.lowTuneHz = 115.0f;
        p.midTuneHz = 164.0f;
        p.highTuneHz = 301.0f;
        p.pitchDropSemitones = 3.0f;
        p.pitchSweepMs = 18.0f;
        p.shellSpread = 0.44f;
        p.body = 0.48f;
        p.ring = 0.30f;
        p.bodyDecaySeconds = 0.22f;
        p.decaySpread = 0.48f;
        p.punch = 0.90f;
        p.rimCharacter = 0.70f;
        p.stickTone = 0.82f;
        p.outputGainDb = -7.0f;
        break;
    case 6u: // SOFT MALLETS
        p.lowTuneHz = 76.0f;
        p.midTuneHz = 112.0f;
        p.highTuneHz = 154.0f;
        p.pitchDropSemitones = 1.5f;
        p.pitchSweepMs = 42.0f;
        p.shellSpread = 0.32f;
        p.body = 0.82f;
        p.ring = 0.22f;
        p.bodyDecaySeconds = 0.62f;
        p.decaySpread = 0.34f;
        p.punch = 0.28f;
        p.rimLevel = 0.28f;
        p.rimCharacter = 0.32f;
        p.stickTone = 0.30f;
        p.outputGainDb = -5.5f;
        break;
    case 7u: // RIM KNOCKS
        p.lowTuneHz = 78.0f;
        p.midTuneHz = 116.0f;
        p.highTuneHz = 158.0f;
        p.pitchDropSemitones = 0.0f;
        p.pitchSweepMs = 10.0f;
        p.shellSpread = 0.58f;
        p.body = 0.34f;
        p.ring = 0.36f;
        p.bodyDecaySeconds = 0.24f;
        p.decaySpread = 0.20f;
        p.punch = 0.72f;
        p.rimLevel = 0.94f;
        p.rimCharacter = 0.06f;
        p.rimDecaySeconds = 0.060f;
        p.stickTone = 0.16f;
        p.outputGainDb = -7.5f;
        break;
    case 8u: // WOOD STICKS
        p.lowTuneHz = 82.0f;
        p.midTuneHz = 126.0f;
        p.highTuneHz = 178.0f;
        p.pitchDropSemitones = 1.0f;
        p.pitchSweepMs = 14.0f;
        p.shellSpread = 0.78f;
        p.body = 0.40f;
        p.ring = 0.48f;
        p.bodyDecaySeconds = 0.32f;
        p.decaySpread = 0.24f;
        p.punch = 0.78f;
        p.rimLevel = 0.92f;
        p.rimCharacter = 0.52f;
        p.rimDecaySeconds = 0.086f;
        p.stickTone = 0.48f;
        p.outputGainDb = -8.5f;
        break;
    case 9u: // BRIGHT EDGES
        p.lowTuneHz = 92.0f;
        p.midTuneHz = 142.0f;
        p.highTuneHz = 206.0f;
        p.pitchDropSemitones = 1.0f;
        p.pitchSweepMs = 8.0f;
        p.shellSpread = 0.86f;
        p.body = 0.28f;
        p.ring = 0.28f;
        p.bodyDecaySeconds = 0.20f;
        p.decaySpread = 0.12f;
        p.punch = 0.94f;
        p.rimLevel = 0.94f;
        p.rimCharacter = 0.94f;
        p.rimDecaySeconds = 0.044f;
        p.stickTone = 0.94f;
        p.outputGainDb = -9.0f;
        break;
    case 10u: // ELECTRIC FALL
        p.lowTuneHz = 86.0f;
        p.midTuneHz = 127.0f;
        p.highTuneHz = 184.0f;
        p.pitchDropSemitones = 12.0f;
        p.pitchSweepMs = 72.0f;
        p.shellSpread = 0.26f;
        p.body = 0.68f;
        p.ring = 0.22f;
        p.bodyDecaySeconds = 0.48f;
        p.decaySpread = 0.30f;
        p.punch = 0.80f;
        p.character.drive = 0.24f;
        p.character.compression = 0.16f;
        p.character.tone = 0.10f;
        p.outputGainDb = -8.0f;
        break;
    case 11u: // WIDE RACK
        p.lowTuneHz = 82.0f;
        p.midTuneHz = 127.0f;
        p.highTuneHz = 182.0f;
        p.pitchDropSemitones = 5.0f;
        p.pitchSweepMs = 34.0f;
        p.shellSpread = 0.58f;
        p.body = 0.62f;
        p.ring = 0.48f;
        p.bodyDecaySeconds = 0.62f;
        p.decaySpread = 0.36f;
        p.punch = 0.76f;
        p.rimLevel = 0.64f;
        p.rimCharacter = 0.68f;
        p.stickTone = 0.76f;
        p.stereoWidth = 0.94f;
        p.outputGainDb = -7.5f;
        break;
    case 0u: // BALANCED RACK
    default:
        break;
    }
    return p;
}

inline DrumTomsParams drumTomsSafeRandomParams(
    const DrumTomsParams& current, DrumRandom& random)
{
    const auto unit = [&]() { return random.unipolar(); };
    const auto range = [&](float minimum, float maximum) {
        return minimum + (maximum - minimum) * unit();
    };
    const auto logRange = [&](float minimum, float maximum) {
        return std::exp(range(std::log(minimum), std::log(maximum)));
    };

    DrumTomsParams p {};
    p.noteTracking = current.noteTracking;
    p.velocitySensitivity = current.velocitySensitivity;
    p.outputGainDb = current.outputGainDb;
    const uint32_t family = random.nextU32() % 5u;
    switch (family) {
    case 0u: // low, rounded three
        p.lowTuneHz = logRange(59.0f, 72.0f);
        p.midTuneHz = logRange(91.0f, 106.0f);
        p.highTuneHz = logRange(119.0f, 139.0f);
        p.pitchDropSemitones = range(0.5f, 4.0f);
        p.pitchSweepMs = logRange(24.0f, 65.0f);
        p.bodyDecaySeconds = logRange(0.62f, 1.05f);
        p.shellSpread = range(0.22f, 0.62f);
        p.body = range(0.58f, 0.84f);
        p.ring = range(0.16f, 0.52f);
        p.punch = range(0.48f, 0.82f);
        break;
    case 1u: // middle electronic family
        p.lowTuneHz = logRange(79.0f, 96.0f);
        p.midTuneHz = logRange(117.0f, 139.0f);
        p.highTuneHz = logRange(166.0f, 198.0f);
        p.pitchDropSemitones = range(3.0f, 8.0f);
        p.pitchSweepMs = logRange(18.0f, 74.0f);
        p.bodyDecaySeconds = logRange(0.36f, 0.78f);
        p.shellSpread = range(0.14f, 0.56f);
        p.body = range(0.44f, 0.78f);
        p.ring = range(0.08f, 0.42f);
        p.punch = range(0.66f, 0.94f);
        break;
    case 2u: // high and very short rack
        p.lowTuneHz = logRange(103.0f, 124.0f);
        p.midTuneHz = logRange(150.0f, 178.0f);
        p.highTuneHz = logRange(268.0f, 326.0f);
        p.pitchDropSemitones = range(0.0f, 4.0f);
        p.pitchSweepMs = logRange(5.0f, 24.0f);
        p.bodyDecaySeconds = logRange(0.055f, 0.16f);
        p.shellSpread = range(0.18f, 0.62f);
        p.body = range(0.34f, 0.66f);
        p.ring = range(0.04f, 0.30f);
        p.punch = range(0.74f, 0.98f);
        break;
    case 3u: // irregular woody intervals
        p.lowTuneHz = logRange(67.0f, 81.0f);
        p.midTuneHz = logRange(124.0f, 148.0f);
        p.highTuneHz = logRange(168.0f, 202.0f);
        p.pitchDropSemitones = range(0.0f, 4.0f);
        p.pitchSweepMs = logRange(14.0f, 52.0f);
        p.bodyDecaySeconds = logRange(0.20f, 0.62f);
        p.shellSpread = range(0.52f, 0.88f);
        p.body = range(0.48f, 0.78f);
        p.ring = range(0.28f, 0.68f);
        p.punch = range(0.54f, 0.88f);
        break;
    case 4u:
    default: // rim-forward ordered rack
        p.lowTuneHz = logRange(72.0f, 94.0f);
        p.midTuneHz = logRange(108.0f, 145.0f);
        p.highTuneHz = logRange(154.0f, 214.0f);
        p.pitchDropSemitones = range(0.0f, 4.0f);
        p.pitchSweepMs = logRange(7.0f, 38.0f);
        p.bodyDecaySeconds = logRange(0.12f, 0.48f);
        p.shellSpread = range(0.48f, 0.94f);
        p.body = range(0.28f, 0.62f);
        p.ring = range(0.18f, 0.62f);
        p.punch = range(0.66f, 0.96f);
        break;
    }

    p.decaySpread = range(-0.10f, 0.72f);
    p.rimLevel = family == 4u ? range(0.70f, 0.96f)
        : range(0.26f, 0.82f);
    p.rimCharacter = range(0.04f, 0.96f);
    p.rimDecaySeconds = logRange(0.034f, 0.105f);
    p.stickTone = range(0.08f, 0.95f);

    p.character.drive = range(0.0f, family == 1u ? 0.44f : 0.30f);
    p.character.bias = random.bipolar()
        * (0.03f + p.character.drive * 0.45f);
    p.character.compression = range(0.0f, 0.48f);
    const float conversionDraw = unit();
    const float conversion = conversionDraw < 0.34f ? 0.0f
        : std::pow((conversionDraw - 0.34f) / 0.66f, 2.2f) * 0.48f;
    p.character.sampleRateReduction = conversion * range(0.52f, 1.0f);
    p.character.bitDepthReduction = conversion * range(0.38f, 0.82f);
    p.character.reconstruction = conversion == 0.0f ? 0.0f
        : std::min(0.52f, conversion * range(0.48f, 0.96f));
    p.character.tone = range(-0.40f, 0.40f);
    p.stereoWidth = 0.92f * std::pow(unit(), 1.20f);
    return p;
}

inline DrumTomsParams drumTomsSafeRandomParams(
    const DrumTomsParams& current, uint32_t& state)
{
    DrumRandom random(state);
    DrumTomsParams params = drumTomsSafeRandomParams(current, random);
    state = random.state();
    return params;
}

inline int32_t drumTomsFactoryPresetIndex(const DrumTomsParams& params)
{
    const auto close = [](float left, float right) {
        return std::fabs(left - right) <= 1.0e-4f;
    };
    for (uint32_t index = 0u; index < kDrumTomsFactoryPresetCount; ++index) {
        const DrumTomsParams preset = drumTomsFactoryPreset(index);
        if (close(params.lowTuneHz, preset.lowTuneHz)
            && close(params.noteTracking, preset.noteTracking)
            && close(params.midTuneHz, preset.midTuneHz)
            && close(params.highTuneHz, preset.highTuneHz)
            && close(params.pitchDropSemitones, preset.pitchDropSemitones)
            && close(params.pitchSweepMs, preset.pitchSweepMs)
            && close(params.shellSpread, preset.shellSpread)
            && close(params.body, preset.body)
            && close(params.ring, preset.ring)
            && close(params.bodyDecaySeconds, preset.bodyDecaySeconds)
            && close(params.decaySpread, preset.decaySpread)
            && close(params.punch, preset.punch)
            && close(params.rimLevel, preset.rimLevel)
            && close(params.rimCharacter, preset.rimCharacter)
            && close(params.rimDecaySeconds, preset.rimDecaySeconds)
            && close(params.stickTone, preset.stickTone)
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
