#pragma once

#include "s3g_drum_break.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

struct DrumBreakFactoryPresetInfo {
    const char* name;
    const char* description;
};

inline constexpr uint32_t kDrumBreakFactoryPresetCount = 14u;

inline const DrumBreakFactoryPresetInfo&
drumBreakFactoryPresetInfo(uint32_t index)
{
    static constexpr std::array<DrumBreakFactoryPresetInfo,
        kDrumBreakFactoryPresetCount> info {{
        { "BREAK FRAME", "Balanced low, backbeat and top voices with shared room glue." },
        { "22K ROLLER", "Narrow-band mono kit with coarse conversion character and compact tails." },
        { "DUSTY POCKET", "Dark aged shell, soft transients and audible cross-band bleed." },
        { "HARD CHOP", "Short forward hits shaped for dense edited break patterns." },
        { "SOUL ROOM", "Natural crest, round shells and a broad stereo room bloom." },
        { "TIGHT EDIT", "Dry separated voices with minimal bleed and near-zero room." },
        { "LOW SLUG", "Heavy slow low voice beneath restrained mid and high bands." },
        { "WIRE BACKBEAT", "Bright long mid crack with a controlled low anchor." },
        { "AIR DRUMMER", "Open high band, lighter body and wide diffuse ambience." },
        { "MONO CRATE", "Centred aged kit with muted bandwidth and sampler color." },
        { "WIDE SESSION", "Dry central drums surrounded by a lively stereo room." },
        { "BROKEN PRESS", "Driven, compressed and reduced-rate break-machine response." },
        { "GHOST CUT", "Soft transient vocabulary for low-velocity shuffled programming." },
        { "BRIGHT RUSH", "Fast low decay, sharp mid crack and extended high energy." },
    }};
    return info[std::min<uint32_t>(index,
        kDrumBreakFactoryPresetCount - 1u)];
}

inline DrumBreakParams drumBreakFactoryPreset(uint32_t index)
{
    DrumBreakParams p;
    switch (std::min<uint32_t>(index, kDrumBreakFactoryPresetCount - 1u)) {
    case 1u: // 22K ROLLER
        p.lowTuneHz = 49.0f; p.lowDropSemitones = 11.0f;
        p.lowDecaySeconds = 0.34f; p.lowWeight = 0.76f;
        p.midTuneHz = 188.0f; p.midBody = 0.44f; p.midCrack = 0.69f;
        p.midDecaySeconds = 0.24f; p.highTone = 0.39f;
        p.highTexture = 0.72f; p.highDecaySeconds = 0.085f;
        p.transient = 0.72f; p.bleed = 0.18f; p.room = 0.12f;
        p.age = 0.70f; p.character.drive = 0.22f;
        p.character.compression = 0.40f;
        p.character.sampleRateReduction = 0.48f;
        p.character.bitDepthReduction = 0.52f;
        p.character.reconstruction = 0.54f;
        p.character.tone = -0.24f; p.stereoWidth = 0.0f;
        p.outputGainDb = -8.5f;
        break;
    case 2u: // DUSTY POCKET
        p.lowTuneHz = 46.0f; p.lowDropSemitones = 9.0f;
        p.lowDecaySeconds = 0.58f; p.lowWeight = 0.82f;
        p.midTuneHz = 148.0f; p.midBody = 0.72f; p.midCrack = 0.48f;
        p.midDecaySeconds = 0.48f; p.highTone = 0.25f;
        p.highTexture = 0.45f; p.highDecaySeconds = 0.16f;
        p.transient = 0.38f; p.bleed = 0.46f; p.room = 0.38f;
        p.age = 0.82f; p.character.drive = 0.16f;
        p.character.compression = 0.18f;
        p.character.reconstruction = 0.32f;
        p.character.tone = -0.38f; p.stereoWidth = 0.24f;
        p.outputGainDb = -7.5f;
        break;
    case 3u: // HARD CHOP
        p.lowTuneHz = 58.0f; p.lowDropSemitones = 24.0f;
        p.lowDecaySeconds = 0.19f; p.lowWeight = 0.72f;
        p.midTuneHz = 226.0f; p.midBody = 0.32f; p.midCrack = 0.92f;
        p.midDecaySeconds = 0.12f; p.highTone = 0.72f;
        p.highTexture = 0.76f; p.highDecaySeconds = 0.045f;
        p.transient = 0.94f; p.bleed = 0.08f; p.room = 0.04f;
        p.age = 0.12f; p.character.drive = 0.48f;
        p.character.bias = 0.14f; p.character.compression = 0.52f;
        p.character.tone = 0.18f; p.stereoWidth = 0.12f;
        p.outputGainDb = -9.5f;
        break;
    case 4u: // SOUL ROOM
        p.lowTuneHz = 43.0f; p.lowDropSemitones = 7.0f;
        p.lowDecaySeconds = 0.66f; p.lowWeight = 0.70f;
        p.midTuneHz = 132.0f; p.midBody = 0.82f; p.midCrack = 0.54f;
        p.midDecaySeconds = 0.62f; p.highTone = 0.42f;
        p.highTexture = 0.50f; p.highDecaySeconds = 0.22f;
        p.transient = 0.44f; p.bleed = 0.52f; p.room = 0.76f;
        p.age = 0.44f; p.character.compression = 0.12f;
        p.character.tone = -0.12f; p.stereoWidth = 0.72f;
        p.outputGainDb = -9.0f;
        break;
    case 5u: // TIGHT EDIT
        p.lowTuneHz = 55.0f; p.lowDropSemitones = 18.0f;
        p.lowDecaySeconds = 0.16f; p.lowWeight = 0.68f;
        p.midTuneHz = 205.0f; p.midBody = 0.36f; p.midCrack = 0.76f;
        p.midDecaySeconds = 0.10f; p.highTone = 0.60f;
        p.highTexture = 0.62f; p.highDecaySeconds = 0.035f;
        p.transient = 0.78f; p.bleed = 0.015f; p.room = 0.0f;
        p.age = 0.08f; p.stereoWidth = 0.0f;
        p.outputGainDb = -6.5f;
        break;
    case 6u: // LOW SLUG
        p.lowTuneHz = 35.0f; p.lowDropSemitones = 20.0f;
        p.lowDecaySeconds = 1.24f; p.lowWeight = 1.0f;
        p.midTuneHz = 126.0f; p.midBody = 0.58f; p.midCrack = 0.32f;
        p.midDecaySeconds = 0.38f; p.highTone = 0.22f;
        p.highTexture = 0.28f; p.highDecaySeconds = 0.09f;
        p.transient = 0.30f; p.bleed = 0.34f; p.room = 0.22f;
        p.age = 0.54f; p.character.compression = 0.34f;
        p.character.tone = -0.30f; p.stereoWidth = 0.16f;
        p.outputGainDb = -10.0f;
        break;
    case 7u: // WIRE BACKBEAT
        p.lowTuneHz = 50.0f; p.lowDecaySeconds = 0.32f;
        p.lowWeight = 0.58f; p.midTuneHz = 194.0f;
        p.midBody = 0.42f; p.midCrack = 1.0f;
        p.midDecaySeconds = 0.82f; p.highTone = 0.70f;
        p.highTexture = 0.76f; p.highDecaySeconds = 0.18f;
        p.transient = 0.68f; p.bleed = 0.28f; p.room = 0.24f;
        p.age = 0.18f; p.character.drive = 0.12f;
        p.stereoWidth = 0.48f; p.outputGainDb = -9.0f;
        break;
    case 8u: // AIR DRUMMER
        p.lowTuneHz = 48.0f; p.lowDropSemitones = 8.0f;
        p.lowDecaySeconds = 0.38f; p.lowWeight = 0.52f;
        p.midTuneHz = 164.0f; p.midBody = 0.46f; p.midCrack = 0.62f;
        p.midDecaySeconds = 0.30f; p.highTone = 0.88f;
        p.highTexture = 0.86f; p.highDecaySeconds = 0.48f;
        p.transient = 0.54f; p.bleed = 0.24f; p.room = 0.62f;
        p.age = 0.04f; p.character.tone = 0.26f;
        p.stereoWidth = 0.88f; p.outputGainDb = -10.0f;
        break;
    case 9u: // MONO CRATE
        p.lowTuneHz = 45.0f; p.lowDropSemitones = 10.0f;
        p.lowDecaySeconds = 0.52f; p.lowWeight = 0.80f;
        p.midTuneHz = 156.0f; p.midBody = 0.68f; p.midCrack = 0.58f;
        p.midDecaySeconds = 0.42f; p.highTone = 0.30f;
        p.highTexture = 0.48f; p.highDecaySeconds = 0.14f;
        p.transient = 0.48f; p.bleed = 0.48f; p.room = 0.32f;
        p.age = 0.76f; p.character.drive = 0.28f;
        p.character.sampleRateReduction = 0.24f;
        p.character.bitDepthReduction = 0.20f;
        p.character.reconstruction = 0.42f;
        p.character.tone = -0.32f; p.stereoWidth = 0.0f;
        p.outputGainDb = -8.0f;
        break;
    case 10u: // WIDE SESSION
        p.lowTuneHz = 51.0f; p.lowDropSemitones = 12.0f;
        p.lowDecaySeconds = 0.46f; p.lowWeight = 0.72f;
        p.midTuneHz = 171.0f; p.midBody = 0.62f; p.midCrack = 0.66f;
        p.midDecaySeconds = 0.38f; p.highTone = 0.62f;
        p.highTexture = 0.64f; p.highDecaySeconds = 0.17f;
        p.transient = 0.55f; p.bleed = 0.30f; p.room = 0.72f;
        p.age = 0.22f; p.character.compression = 0.16f;
        p.stereoWidth = 1.0f; p.outputGainDb = -10.0f;
        break;
    case 11u: // BROKEN PRESS
        p.lowTuneHz = 60.0f; p.lowDropSemitones = 22.0f;
        p.lowDecaySeconds = 0.28f; p.lowWeight = 0.84f;
        p.midTuneHz = 238.0f; p.midBody = 0.44f; p.midCrack = 0.88f;
        p.midDecaySeconds = 0.20f; p.highTone = 0.54f;
        p.highTexture = 0.82f; p.highDecaySeconds = 0.075f;
        p.transient = 0.90f; p.bleed = 0.14f; p.room = 0.08f;
        p.age = 0.48f; p.character.drive = 0.66f;
        p.character.bias = -0.22f; p.character.compression = 0.68f;
        p.character.sampleRateReduction = 0.58f;
        p.character.bitDepthReduction = 0.44f;
        p.character.reconstruction = 0.38f;
        p.stereoWidth = 0.14f; p.outputGainDb = -12.0f;
        break;
    case 12u: // GHOST CUT
        p.lowTuneHz = 50.0f; p.lowDropSemitones = 6.0f;
        p.lowDecaySeconds = 0.26f; p.lowWeight = 0.48f;
        p.midTuneHz = 182.0f; p.midBody = 0.38f; p.midCrack = 0.44f;
        p.midDecaySeconds = 0.18f; p.highTone = 0.46f;
        p.highTexture = 0.38f; p.highDecaySeconds = 0.055f;
        p.transient = 0.26f; p.bleed = 0.34f; p.room = 0.18f;
        p.age = 0.38f; p.character.compression = 0.42f;
        p.stereoWidth = 0.22f; p.velocitySensitivity = 1.0f;
        p.outputGainDb = -7.0f;
        break;
    case 13u: // BRIGHT RUSH
        p.lowTuneHz = 64.0f; p.lowDropSemitones = 28.0f;
        p.lowDecaySeconds = 0.22f; p.lowWeight = 0.64f;
        p.midTuneHz = 262.0f; p.midBody = 0.28f; p.midCrack = 0.96f;
        p.midDecaySeconds = 0.24f; p.highTone = 1.0f;
        p.highTexture = 1.0f; p.highDecaySeconds = 0.32f;
        p.transient = 0.88f; p.bleed = 0.12f; p.room = 0.28f;
        p.age = 0.0f; p.character.drive = 0.24f;
        p.character.tone = 0.42f; p.stereoWidth = 0.62f;
        p.outputGainDb = -11.0f;
        break;
    case 0u:
    default:
        break;
    }
    if (std::min<uint32_t>(index,
            kDrumBreakFactoryPresetCount - 1u) != 0u) {
        // Derive each voice's post-synthesis band from the preset's established
        // body and brightness. Preset zero deliberately retains the parameter
        // defaults so a new instance identifies as BREAK FRAME.
        p.tomTuneHz = clamp(p.midTuneHz * 0.68f, 68.0f, 208.0f);
        p.tomDecaySeconds = clamp(
            (p.lowDecaySeconds + p.midDecaySeconds) * 0.70f,
            0.10f, 1.65f);
        p.kickBandHz = clamp(95.0f + p.lowWeight * 75.0f
                + p.transient * 45.0f,
            70.0f, 280.0f);
        p.snareBandHz = clamp(750.0f + p.midCrack * 3000.0f,
            500.0f, 4500.0f);
        p.tomBandHz = clamp(p.tomTuneHz * 2.7f, 140.0f, 850.0f);
        p.hiHatBandHz = clamp(3800.0f + p.highTone * 8500.0f,
            2800.0f, 13000.0f);
    }
    return p;
}

inline DrumBreakParams drumBreakSafeRandomParams(
    const DrumBreakParams& preserved, uint32_t seed)
{
    DrumRandom random(seed != 0u ? seed : 1u);
    const auto unit = [&]() { return random.unipolar(); };
    const auto curved = [&](float exponent) {
        return std::pow(unit(), exponent);
    };
    DrumBreakParams p = preserved;
    p.lowTuneHz = 34.0f + curved(0.72f) * 34.0f;
    p.lowDropSemitones = 5.0f + unit() * 24.0f;
    p.lowDecaySeconds = 0.14f * std::pow(7.8f, unit());
    p.lowWeight = 0.46f + unit() * 0.50f;
    p.midTuneHz = 118.0f + curved(0.82f) * 146.0f;
    p.midBody = 0.25f + unit() * 0.66f;
    p.midCrack = 0.32f + unit() * 0.66f;
    p.midDecaySeconds = 0.09f * std::pow(8.5f, unit());
    p.highTone = 0.16f + unit() * 0.82f;
    p.highTexture = 0.30f + unit() * 0.68f;
    p.highDecaySeconds = 0.035f * std::pow(10.0f, unit());
    p.tomTuneHz = 72.0f + curved(0.82f) * 120.0f;
    p.tomDecaySeconds = 0.16f * std::pow(7.8f, unit());
    p.kickLevelDb = -3.0f + unit() * 5.0f;
    p.kickBandHz = 75.0f * std::pow(4.0f, unit());
    p.snareLevelDb = -3.0f + unit() * 5.0f;
    p.snareBandHz = 650.0f * std::pow(6.5f, unit());
    p.tomLevelDb = -3.0f + unit() * 5.0f;
    p.tomBandHz = 130.0f * std::pow(6.0f, unit());
    p.hiHatLevelDb = -4.0f + unit() * 6.0f;
    p.hiHatBandHz = 3000.0f * std::pow(4.2f, unit());
    p.transient = 0.22f + unit() * 0.74f;
    p.bleed = unit() * 0.58f;
    p.room = curved(1.35f) * 0.82f;
    p.age = unit() * 0.88f;
    const float color = unit();
    p.character.drive = curved(1.8f) * 0.58f;
    p.character.bias = (unit() * 2.0f - 1.0f) * p.character.drive * 0.45f;
    p.character.compression = curved(1.35f) * 0.62f;
    p.character.sampleRateReduction = color > 0.62f
        ? curved(1.5f) * 0.62f : 0.0f;
    p.character.bitDepthReduction = color > 0.72f
        ? curved(1.7f) * 0.55f : 0.0f;
    p.character.reconstruction = color > 0.55f
        ? 0.12f + unit() * 0.50f : 0.0f;
    p.character.tone = unit() * 0.70f - 0.42f;
    p.stereoWidth = unit() * 0.88f;
    // Deliberately preserve note tracking, velocity response and output gain.
    p.noteTracking = preserved.noteTracking;
    p.velocitySensitivity = preserved.velocitySensitivity;
    p.outputGainDb = preserved.outputGainDb;
    return drumSanitizeBreakParams(p);
}

inline int drumBreakFactoryPresetIndex(const DrumBreakParams& params,
    float tolerance = 1.0e-4f)
{
    const auto close = [tolerance](float a, float b) {
        return std::abs(a - b) <= tolerance
            * std::max(1.0f, std::max(std::abs(a), std::abs(b)));
    };
    for (uint32_t index = 0u; index < kDrumBreakFactoryPresetCount; ++index) {
        const auto p = drumBreakFactoryPreset(index);
        if (close(params.lowTuneHz, p.lowTuneHz)
            && close(params.noteTracking, p.noteTracking)
            && close(params.lowDropSemitones, p.lowDropSemitones)
            && close(params.lowDecaySeconds, p.lowDecaySeconds)
            && close(params.lowWeight, p.lowWeight)
            && close(params.kickLevelDb, p.kickLevelDb)
            && close(params.kickBandHz, p.kickBandHz)
            && close(params.midTuneHz, p.midTuneHz)
            && close(params.midBody, p.midBody)
            && close(params.midCrack, p.midCrack)
            && close(params.midDecaySeconds, p.midDecaySeconds)
            && close(params.snareLevelDb, p.snareLevelDb)
            && close(params.snareBandHz, p.snareBandHz)
            && close(params.highTone, p.highTone)
            && close(params.highTexture, p.highTexture)
            && close(params.highDecaySeconds, p.highDecaySeconds)
            && close(params.hiHatLevelDb, p.hiHatLevelDb)
            && close(params.hiHatBandHz, p.hiHatBandHz)
            && close(params.tomTuneHz, p.tomTuneHz)
            && close(params.tomDecaySeconds, p.tomDecaySeconds)
            && close(params.tomLevelDb, p.tomLevelDb)
            && close(params.tomBandHz, p.tomBandHz)
            && close(params.transient, p.transient)
            && close(params.bleed, p.bleed)
            && close(params.room, p.room)
            && close(params.age, p.age)
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
            && close(params.velocitySensitivity, p.velocitySensitivity)
            && close(params.outputGainDb, p.outputGainDb)) return index;
    }
    return -1;
}

} // namespace s3g
