#pragma once

#include "s3g_drum_floor_tom.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

struct DrumFloorTomFactoryPresetInfo {
    const char* name;
    const char* description;
};

inline constexpr uint32_t kDrumFloorTomFactoryPresetCount = 12u;

inline const DrumFloorTomFactoryPresetInfo&
drumFloorTomFactoryPresetInfo(uint32_t index)
{
    static constexpr std::array<DrumFloorTomFactoryPresetInfo,
        kDrumFloorTomFactoryPresetCount> info {{
        { "DEEP FLOOR", "Balanced low floor tom with a rounded head and controlled ring." },
        { "OPEN SHELL", "Longer resonant membrane with audible upper shell modes." },
        { "SHORT SKIN", "Quick damped floor tom with a compact acoustic attack." },
        { "LOW HORIZON", "Rare long-settling low body with a restrained transient." },
        { "HARD MALLET", "Firm head impact and brighter shell over a medium tail." },
        { "SOFT BEATER", "Muted attack and warm body for low-velocity passages." },
        { "WOOD EDGE", "Hollow cross-stick character with modest head coupling." },
        { "DARK RIM", "Short electronic rim knock centered below the bright stick band." },
        { "BRIGHT RIM", "Noisier high rim articulation with a fast sharp stick." },
        { "ELASTIC FALL", "Pronounced synthesized pitch gesture over a rounded membrane." },
        { "COARSE CURRENT", "Reduced-rate conversion color around a tight low tom." },
        { "WIDE UPPER", "Centered fundamental with independent stereo shell and stick detail." },
    }};
    return info[std::min<uint32_t>(
        index, kDrumFloorTomFactoryPresetCount - 1u)];
}

inline DrumFloorTomParams drumFloorTomFactoryPreset(uint32_t index)
{
    DrumFloorTomParams p;
    switch (std::min<uint32_t>(
        index, kDrumFloorTomFactoryPresetCount - 1u)) {
    case 1u: // OPEN SHELL
        p.tuneHz = 68.0f;
        p.pitchDropSemitones = 4.0f;
        p.pitchSweepMs = 62.0f;
        p.shellSpread = 0.58f;
        p.body = 0.76f;
        p.ring = 0.68f;
        p.bodyDecaySeconds = 0.94f;
        p.punch = 0.58f;
        p.damping = 0.12f;
        p.rimLevel = 0.46f;
        p.rimCharacter = 0.42f;
        p.outputGainDb = -7.5f;
        break;
    case 2u: // SHORT SKIN
        p.tuneHz = 84.0f;
        p.pitchDropSemitones = 3.0f;
        p.pitchSweepMs = 21.0f;
        p.shellSpread = 0.30f;
        p.body = 0.58f;
        p.ring = 0.16f;
        p.bodyDecaySeconds = 0.16f;
        p.punch = 0.86f;
        p.damping = 0.78f;
        p.rimDecaySeconds = 0.045f;
        p.stickDecayMs = 2.4f;
        p.outputGainDb = -5.5f;
        break;
    case 3u: // LOW HORIZON
        p.tuneHz = 58.0f;
        p.noteTracking = 0.45f;
        p.pitchDropSemitones = 5.5f;
        p.pitchSweepMs = 92.0f;
        p.shellSpread = 0.50f;
        p.body = 0.84f;
        p.ring = 0.54f;
        p.bodyDecaySeconds = 1.40f;
        p.punch = 0.46f;
        p.damping = 0.18f;
        p.stickLevel = 0.18f;
        p.outputGainDb = -8.5f;
        break;
    case 4u: // HARD MALLET
        p.tuneHz = 79.0f;
        p.pitchDropSemitones = 5.0f;
        p.pitchSweepMs = 27.0f;
        p.shellSpread = 0.46f;
        p.body = 0.62f;
        p.ring = 0.44f;
        p.bodyDecaySeconds = 0.52f;
        p.punch = 0.96f;
        p.damping = 0.30f;
        p.stickLevel = 0.50f;
        p.stickTone = 0.74f;
        p.character.compression = 0.24f;
        p.outputGainDb = -7.0f;
        break;
    case 5u: // SOFT BEATER
        p.tuneHz = 70.0f;
        p.pitchDropSemitones = 2.0f;
        p.pitchSweepMs = 48.0f;
        p.shellSpread = 0.35f;
        p.body = 0.80f;
        p.ring = 0.24f;
        p.bodyDecaySeconds = 0.68f;
        p.punch = 0.30f;
        p.damping = 0.52f;
        p.rimLevel = 0.34f;
        p.stickLevel = 0.20f;
        p.stickTone = 0.36f;
        p.outputGainDb = -5.0f;
        break;
    case 6u: // WOOD EDGE
        p.tuneHz = 76.0f;
        p.pitchDropSemitones = 1.0f;
        p.pitchSweepMs = 18.0f;
        p.shellSpread = 0.70f;
        p.body = 0.48f;
        p.ring = 0.52f;
        p.bodyDecaySeconds = 0.42f;
        p.punch = 0.74f;
        p.damping = 0.38f;
        p.rimLevel = 0.90f;
        p.rimCharacter = 0.52f;
        p.rimDecaySeconds = 0.086f;
        p.stickLevel = 0.66f;
        p.stickTone = 0.48f;
        p.stickDecayMs = 6.8f;
        p.outputGainDb = -8.0f;
        break;
    case 7u: // DARK RIM
        p.tuneHz = 72.0f;
        p.pitchDropSemitones = 0.0f;
        p.pitchSweepMs = 12.0f;
        p.shellSpread = 0.64f;
        p.body = 0.30f;
        p.ring = 0.40f;
        p.bodyDecaySeconds = 0.28f;
        p.punch = 0.72f;
        p.damping = 0.48f;
        p.rimLevel = 0.94f;
        p.rimCharacter = 0.08f;
        p.rimDecaySeconds = 0.060f;
        p.stickLevel = 0.46f;
        p.stickTone = 0.18f;
        p.stickDecayMs = 5.2f;
        p.outputGainDb = -7.0f;
        break;
    case 8u: // BRIGHT RIM
        p.tuneHz = 86.0f;
        p.pitchDropSemitones = 1.0f;
        p.pitchSweepMs = 9.0f;
        p.shellSpread = 0.82f;
        p.body = 0.24f;
        p.ring = 0.32f;
        p.bodyDecaySeconds = 0.24f;
        p.punch = 0.92f;
        p.damping = 0.30f;
        p.rimLevel = 0.92f;
        p.rimCharacter = 0.92f;
        p.rimDecaySeconds = 0.046f;
        p.stickLevel = 0.82f;
        p.stickTone = 0.92f;
        p.stickDecayMs = 2.1f;
        p.outputGainDb = -9.0f;
        break;
    case 9u: // ELASTIC FALL
        p.tuneHz = 75.0f;
        p.pitchDropSemitones = 18.0f;
        p.pitchSweepMs = 104.0f;
        p.shellSpread = 0.36f;
        p.body = 0.74f;
        p.ring = 0.28f;
        p.bodyDecaySeconds = 0.74f;
        p.punch = 0.66f;
        p.damping = 0.34f;
        p.character.drive = 0.16f;
        p.outputGainDb = -7.5f;
        break;
    case 10u: // COARSE CURRENT
        p.tuneHz = 81.0f;
        p.pitchDropSemitones = 6.0f;
        p.pitchSweepMs = 29.0f;
        p.shellSpread = 0.44f;
        p.body = 0.60f;
        p.ring = 0.30f;
        p.bodyDecaySeconds = 0.43f;
        p.punch = 0.84f;
        p.damping = 0.46f;
        p.rimCharacter = 0.36f;
        p.character.drive = 0.28f;
        p.character.bias = -0.12f;
        p.character.compression = 0.20f;
        p.character.sampleRateReduction = 0.42f;
        p.character.bitDepthReduction = 0.28f;
        p.character.reconstruction = 0.34f;
        p.character.tone = -0.12f;
        p.outputGainDb = -8.0f;
        break;
    case 11u: // WIDE UPPER
        p.tuneHz = 74.0f;
        p.pitchDropSemitones = 5.0f;
        p.pitchSweepMs = 38.0f;
        p.shellSpread = 0.64f;
        p.body = 0.68f;
        p.ring = 0.58f;
        p.bodyDecaySeconds = 0.72f;
        p.punch = 0.76f;
        p.damping = 0.24f;
        p.rimLevel = 0.66f;
        p.rimCharacter = 0.68f;
        p.stickLevel = 0.58f;
        p.stickTone = 0.74f;
        p.stereoWidth = 0.92f;
        p.outputGainDb = -7.5f;
        break;
    case 0u: // DEEP FLOOR
    default:
        break;
    }
    return p;
}

inline DrumFloorTomParams drumFloorTomSafeRandomParams(
    const DrumFloorTomParams& current, DrumRandom& random)
{
    const auto unit = [&]() { return random.unipolar(); };
    const auto range = [&](float minimum, float maximum) {
        return minimum + (maximum - minimum) * unit();
    };
    const auto logRange = [&](float minimum, float maximum) {
        return std::exp(range(std::log(minimum), std::log(maximum)));
    };

    DrumFloorTomParams p {};
    p.noteTracking = current.noteTracking;
    p.velocitySensitivity = current.velocitySensitivity;
    p.outputGainDb = current.outputGainDb;
    const uint32_t family = random.nextU32() % 5u;
    switch (family) {
    case 0u: // short, damped floor head
        p.tuneHz = logRange(67.0f, 92.0f);
        p.pitchDropSemitones = range(0.5f, 4.0f);
        p.pitchSweepMs = logRange(10.0f, 42.0f);
        p.shellSpread = range(0.18f, 0.58f);
        p.body = range(0.42f, 0.74f);
        p.ring = range(0.06f, 0.34f);
        p.bodyDecaySeconds = logRange(0.12f, 0.30f);
        p.punch = range(0.68f, 0.96f);
        p.damping = range(0.54f, 0.90f);
        break;
    case 1u: // classic rounded floor
        p.tuneHz = logRange(63.0f, 89.0f);
        p.pitchDropSemitones = range(1.0f, 5.0f);
        p.pitchSweepMs = logRange(24.0f, 76.0f);
        p.shellSpread = range(0.28f, 0.68f);
        p.body = range(0.58f, 0.86f);
        p.ring = range(0.18f, 0.58f);
        p.bodyDecaySeconds = logRange(0.38f, 0.82f);
        p.punch = range(0.48f, 0.82f);
        p.damping = range(0.16f, 0.55f);
        break;
    case 2u: // long specialty floor
        p.tuneHz = logRange(55.0f, 78.0f);
        p.pitchDropSemitones = range(1.0f, 6.0f);
        p.pitchSweepMs = logRange(48.0f, 130.0f);
        p.shellSpread = range(0.34f, 0.76f);
        p.body = range(0.68f, 0.92f);
        p.ring = range(0.32f, 0.72f);
        p.bodyDecaySeconds = logRange(0.82f, 1.40f);
        p.punch = range(0.34f, 0.68f);
        p.damping = range(0.06f, 0.34f);
        break;
    case 3u: // electronic pitch gesture
        p.tuneHz = logRange(62.0f, 108.0f);
        p.pitchDropSemitones = range(4.0f, 12.0f);
        p.pitchSweepMs = logRange(18.0f, 105.0f);
        p.shellSpread = range(0.12f, 0.56f);
        p.body = range(0.42f, 0.82f);
        p.ring = range(0.08f, 0.48f);
        p.bodyDecaySeconds = logRange(0.18f, 0.72f);
        p.punch = range(0.62f, 0.96f);
        p.damping = range(0.24f, 0.72f);
        break;
    case 4u:
    default: // rim/stick-forward but still useful as a head
        p.tuneHz = logRange(65.0f, 105.0f);
        p.pitchDropSemitones = range(0.0f, 4.0f);
        p.pitchSweepMs = logRange(8.0f, 45.0f);
        p.shellSpread = range(0.48f, 0.92f);
        p.body = range(0.28f, 0.66f);
        p.ring = range(0.18f, 0.62f);
        p.bodyDecaySeconds = logRange(0.16f, 0.58f);
        p.punch = range(0.62f, 0.96f);
        p.damping = range(0.24f, 0.68f);
        break;
    }

    p.rimLevel = family == 4u ? range(0.68f, 0.96f)
        : range(0.28f, 0.82f);
    p.rimCharacter = range(0.04f, 0.96f);
    p.rimDecaySeconds = logRange(0.035f, 0.105f);
    p.stickLevel = family == 4u ? range(0.48f, 0.88f)
        : range(0.18f, 0.68f);
    p.stickTone = range(0.08f, 0.94f);
    p.stickDecayMs = logRange(1.2f, 10.0f);

    p.character.drive = range(0.0f, family == 3u ? 0.46f : 0.32f);
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
    p.stereoWidth = 0.90f * std::pow(unit(), 1.25f);
    return p;
}

inline DrumFloorTomParams drumFloorTomSafeRandomParams(
    const DrumFloorTomParams& current, uint32_t& state)
{
    DrumRandom random(state);
    DrumFloorTomParams params = drumFloorTomSafeRandomParams(current, random);
    state = random.state();
    return params;
}

inline int32_t drumFloorTomFactoryPresetIndex(
    const DrumFloorTomParams& params)
{
    const auto close = [](float left, float right) {
        return std::fabs(left - right) <= 1.0e-4f;
    };
    for (uint32_t index = 0u; index < kDrumFloorTomFactoryPresetCount;
         ++index) {
        const DrumFloorTomParams preset = drumFloorTomFactoryPreset(index);
        if (close(params.tuneHz, preset.tuneHz)
            && close(params.noteTracking, preset.noteTracking)
            && close(params.pitchDropSemitones, preset.pitchDropSemitones)
            && close(params.pitchSweepMs, preset.pitchSweepMs)
            && close(params.shellSpread, preset.shellSpread)
            && close(params.body, preset.body)
            && close(params.ring, preset.ring)
            && close(params.bodyDecaySeconds, preset.bodyDecaySeconds)
            && close(params.punch, preset.punch)
            && close(params.damping, preset.damping)
            && close(params.rimLevel, preset.rimLevel)
            && close(params.rimCharacter, preset.rimCharacter)
            && close(params.rimDecaySeconds, preset.rimDecaySeconds)
            && close(params.stickLevel, preset.stickLevel)
            && close(params.stickTone, preset.stickTone)
            && close(params.stickDecayMs, preset.stickDecayMs)
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
