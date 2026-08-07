#pragma once

#include "s3g_drum_snare.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

struct DrumSnareFactoryPresetInfo {
    const char* name;
    const char* description;
};

inline constexpr uint32_t kDrumSnareFactoryPresetCount = 14u;

inline const DrumSnareFactoryPresetInfo&
drumSnareFactoryPresetInfo(uint32_t index)
{
    static constexpr std::array<DrumSnareFactoryPresetInfo,
        kDrumSnareFactoryPresetCount> info {{
        { "BALANCED FRAME", "Even shell and wire response with a compact natural tail." },
        { "TIGHT CIRCUIT", "Short high-tension snap with a focused electronic shell." },
        { "DEEP PLATE", "Low, broad shell modes under a restrained dark wire layer." },
        { "WIRE CURRENT", "Long bright wires with a controlled central body." },
        { "DRY CARD", "Very short papery impact with almost no residual ring." },
        { "TAPE BURST", "Rounded transient and softened wire noise through reconstruction color." },
        { "HARD SPRING", "Driven impact with taut metallic wire resonance." },
        { "LOOSE RATTLE", "Slack intermittent wires trailing a woody shell." },
        { "BRIGHT ALLOY", "High-tuned inharmonic shell with a crisp top edge." },
        { "DARK CASSETTE", "Reduced-rate grit around a muted low snare voice." },
        { "WIDE BRUSH", "Diffuse stereo wires surrounding a centered soft shell." },
        { "SOFT PRESS", "Compressed low-velocity body with a gentle wire bloom." },
        { "RIM KNOCK", "High spread shell knock with a dry rim-like edge and almost no wires." },
        { "SIDE STICK", "Short high-modal stick articulation with the wire layer removed." },
    }};
    return info[std::min<uint32_t>(
        index, kDrumSnareFactoryPresetCount - 1u)];
}

inline DrumSnareParams drumSnareFactoryPreset(uint32_t index)
{
    DrumSnareParams params;
    switch (std::min<uint32_t>(
        index, kDrumSnareFactoryPresetCount - 1u)) {
    case 1u: // TIGHT CIRCUIT
        params.tuneHz = 225.0f;
        params.noteTracking = 0.90f;
        params.pitchDropSemitones = 4.0f;
        params.pitchSweepMs = 10.0f;
        params.shellSpread = 0.28f;
        params.body = 0.38f;
        params.ring = 0.10f;
        params.bodyDecaySeconds = 0.16f;
        params.punch = 0.94f;
        params.wires = 0.76f;
        params.wireTone = 0.67f;
        params.wireTension = 0.88f;
        params.wireDecaySeconds = 0.13f;
        params.rattle = 0.03f;
        params.click = 0.26f;
        params.clickTone = 0.76f;
        params.outputGainDb = -6.5f;
        break;
    case 2u: // DEEP PLATE
        params.tuneHz = 104.0f;
        params.noteTracking = 0.45f;
        params.pitchDropSemitones = 13.0f;
        params.pitchSweepMs = 42.0f;
        params.shellSpread = 0.72f;
        params.body = 0.92f;
        params.ring = 0.52f;
        params.bodyDecaySeconds = 0.92f;
        params.punch = 0.61f;
        params.wires = 0.34f;
        params.wireTone = 0.24f;
        params.wireTension = 0.30f;
        params.wireDecaySeconds = 0.46f;
        params.rattle = 0.22f;
        params.click = 0.025f;
        params.clickTone = 0.31f;
        params.outputGainDb = -7.0f;
        break;
    case 3u: // WIRE CURRENT
        params.tuneHz = 174.0f;
        params.pitchDropSemitones = 5.0f;
        params.pitchSweepMs = 18.0f;
        params.shellSpread = 0.44f;
        params.body = 0.40f;
        params.ring = 0.25f;
        params.bodyDecaySeconds = 0.34f;
        params.punch = 0.72f;
        params.wires = 1.0f;
        params.wireTone = 0.82f;
        params.wireTension = 0.63f;
        params.wireDecaySeconds = 1.28f;
        params.rattle = 0.38f;
        params.click = 0.11f;
        params.clickTone = 0.66f;
        params.stereoWidth = 0.44f;
        params.outputGainDb = -8.0f;
        break;
    case 4u: // DRY CARD
        params.tuneHz = 198.0f;
        params.pitchDropSemitones = 2.0f;
        params.pitchSweepMs = 6.0f;
        params.shellSpread = 0.19f;
        params.body = 0.27f;
        params.ring = 0.015f;
        params.bodyDecaySeconds = 0.07f;
        params.punch = 0.98f;
        params.wires = 0.42f;
        params.wireTone = 0.48f;
        params.wireTension = 0.78f;
        params.wireDecaySeconds = 0.055f;
        params.rattle = 0.0f;
        params.click = 0.34f;
        params.clickTone = 0.54f;
        params.outputGainDb = -5.5f;
        break;
    case 5u: // TAPE BURST
        params.tuneHz = 158.0f;
        params.noteTracking = 0.60f;
        params.pitchDropSemitones = 9.0f;
        params.pitchSweepMs = 31.0f;
        params.shellSpread = 0.58f;
        params.body = 0.63f;
        params.ring = 0.28f;
        params.bodyDecaySeconds = 0.48f;
        params.punch = 0.68f;
        params.wires = 0.73f;
        params.wireTone = 0.42f;
        params.wireTension = 0.43f;
        params.wireDecaySeconds = 0.52f;
        params.rattle = 0.19f;
        params.click = 0.08f;
        params.clickTone = 0.41f;
        params.character.drive = 0.18f;
        params.character.compression = 0.24f;
        params.character.reconstruction = 0.36f;
        params.character.tone = -0.16f;
        params.outputGainDb = -7.0f;
        break;
    case 6u: // HARD SPRING
        params.tuneHz = 212.0f;
        params.pitchDropSemitones = 11.0f;
        params.pitchSweepMs = 16.0f;
        params.shellSpread = 0.66f;
        params.body = 0.52f;
        params.ring = 0.60f;
        params.bodyDecaySeconds = 0.36f;
        params.punch = 1.0f;
        params.wires = 0.88f;
        params.wireTone = 0.74f;
        params.wireTension = 0.96f;
        params.wireDecaySeconds = 0.39f;
        params.rattle = 0.08f;
        params.click = 0.31f;
        params.clickTone = 0.83f;
        params.character.drive = 0.56f;
        params.character.bias = 0.18f;
        params.character.compression = 0.31f;
        params.character.tone = 0.24f;
        params.outputGainDb = -9.0f;
        break;
    case 7u: // LOOSE RATTLE
        params.tuneHz = 136.0f;
        params.noteTracking = 0.35f;
        params.pitchDropSemitones = 8.0f;
        params.pitchSweepMs = 35.0f;
        params.shellSpread = 0.82f;
        params.body = 0.76f;
        params.ring = 0.38f;
        params.bodyDecaySeconds = 0.68f;
        params.punch = 0.55f;
        params.wires = 0.82f;
        params.wireTone = 0.34f;
        params.wireTension = 0.08f;
        params.wireDecaySeconds = 0.86f;
        params.rattle = 0.94f;
        params.click = 0.035f;
        params.clickTone = 0.28f;
        params.outputGainDb = -8.0f;
        break;
    case 8u: // BRIGHT ALLOY
        params.tuneHz = 284.0f;
        params.noteTracking = 1.0f;
        params.pitchDropSemitones = 3.0f;
        params.pitchSweepMs = 8.0f;
        params.shellSpread = 0.94f;
        params.body = 0.33f;
        params.ring = 0.82f;
        params.bodyDecaySeconds = 0.58f;
        params.punch = 0.84f;
        params.wires = 0.64f;
        params.wireTone = 0.96f;
        params.wireTension = 0.84f;
        params.wireDecaySeconds = 0.31f;
        params.rattle = 0.06f;
        params.click = 0.22f;
        params.clickTone = 0.94f;
        params.character.drive = 0.20f;
        params.character.tone = 0.32f;
        params.outputGainDb = -9.0f;
        break;
    case 9u: // DARK CASSETTE
        params.tuneHz = 118.0f;
        params.noteTracking = 0.20f;
        params.pitchDropSemitones = 15.0f;
        params.pitchSweepMs = 58.0f;
        params.shellSpread = 0.37f;
        params.body = 0.86f;
        params.ring = 0.16f;
        params.bodyDecaySeconds = 0.74f;
        params.punch = 0.70f;
        params.wires = 0.68f;
        params.wireTone = 0.18f;
        params.wireTension = 0.26f;
        params.wireDecaySeconds = 0.66f;
        params.rattle = 0.42f;
        params.click = 0.045f;
        params.clickTone = 0.22f;
        params.character.drive = 0.34f;
        params.character.bias = -0.18f;
        params.character.sampleRateReduction = 0.58f;
        params.character.bitDepthReduction = 0.36f;
        params.character.reconstruction = 0.52f;
        params.character.tone = -0.38f;
        params.outputGainDb = -8.5f;
        break;
    case 10u: // WIDE BRUSH
        params.tuneHz = 166.0f;
        params.noteTracking = 0.55f;
        params.pitchDropSemitones = 1.0f;
        params.pitchSweepMs = 25.0f;
        params.shellSpread = 0.51f;
        params.body = 0.32f;
        params.ring = 0.20f;
        params.bodyDecaySeconds = 0.40f;
        params.punch = 0.31f;
        params.wires = 0.94f;
        params.wireTone = 0.72f;
        params.wireTension = 0.36f;
        params.wireDecaySeconds = 0.98f;
        params.rattle = 0.56f;
        params.click = 0.015f;
        params.clickTone = 0.48f;
        params.stereoWidth = 0.96f;
        params.outputGainDb = -8.0f;
        break;
    case 11u: // SOFT PRESS
        params.tuneHz = 146.0f;
        params.noteTracking = 0.50f;
        params.pitchDropSemitones = 6.0f;
        params.pitchSweepMs = 38.0f;
        params.shellSpread = 0.46f;
        params.body = 0.72f;
        params.ring = 0.30f;
        params.bodyDecaySeconds = 0.72f;
        params.punch = 0.28f;
        params.wires = 0.58f;
        params.wireTone = 0.38f;
        params.wireTension = 0.40f;
        params.wireDecaySeconds = 0.70f;
        params.rattle = 0.24f;
        params.click = 0.008f;
        params.clickTone = 0.32f;
        params.character.drive = 0.08f;
        params.character.compression = 0.54f;
        params.character.tone = -0.12f;
        params.velocitySensitivity = 0.62f;
        params.outputGainDb = -6.0f;
        break;
    case 12u: // RIM KNOCK
        params.tuneHz = 420.0f;
        params.noteTracking = 1.0f;
        params.pitchDropSemitones = 1.0f;
        params.pitchSweepMs = 5.0f;
        params.shellSpread = 1.0f;
        params.body = 0.08f;
        params.ring = 0.12f;
        params.bodyDecaySeconds = 0.22f;
        params.punch = 0.95f;
        params.wires = 0.025f;
        params.wireTone = 0.90f;
        params.wireTension = 0.94f;
        params.wireDecaySeconds = 0.08f;
        params.rattle = 0.0f;
        params.click = 0.72f;
        params.clickTone = 1.0f;
        params.character.drive = 0.08f;
        params.character.tone = 0.14f;
        params.outputGainDb = -8.0f;
        break;
    case 13u: // SIDE STICK
        params.tuneHz = 330.0f;
        params.noteTracking = 1.0f;
        params.pitchDropSemitones = 0.0f;
        params.pitchSweepMs = 3.0f;
        params.shellSpread = 1.0f;
        params.body = 0.02f;
        params.ring = 1.0f;
        params.bodyDecaySeconds = 0.20f;
        params.punch = 1.0f;
        params.wires = 0.0f;
        params.wireTone = 0.84f;
        params.wireTension = 1.0f;
        params.wireDecaySeconds = 0.04f;
        params.rattle = 0.0f;
        params.click = 1.0f;
        params.clickTone = 0.88f;
        params.character.drive = 0.08f;
        params.character.compression = 0.08f;
        params.character.tone = 0.18f;
        params.outputGainDb = -9.0f;
        break;
    case 0u: // BALANCED FRAME
    default:
        break;
    }
    return params;
}

// Build a complete, musically bounded voice instead of selecting or
// perturbing a factory preset.  The correlations keep long/low shells,
// tight/high shells, and their wire layers in useful combinations.  The
// three performance/session controls remain user-owned so the same helper can
// be used by a tracker without changing note mapping, velocity response, or
// mix gain.
inline DrumSnareParams drumSnareSafeRandomParams(
    const DrumSnareParams& current, DrumRandom& random)
{
    const auto unit = [&]() { return random.unipolar(); };
    const auto range = [&](float minimum, float maximum) {
        return minimum + (maximum - minimum) * unit();
    };
    const auto logRange = [&](float minimum, float maximum) {
        return std::exp(range(std::log(minimum), std::log(maximum)));
    };

    DrumSnareParams params {};
    params.noteTracking = current.noteTracking;
    params.velocitySensitivity = current.velocitySensitivity;
    params.outputGainDb = current.outputGainDb;

    switch (random.nextU32() % 4u) {
    case 0u: { // deep shell / plate
        params.tuneHz = logRange(95.0f, 158.0f);
        params.pitchDropSemitones = range(7.0f, 18.0f);
        params.pitchSweepMs = range(12.0f, 24.0f)
            + params.pitchDropSemitones * range(1.25f, 2.15f);
        params.shellSpread = range(0.34f, 0.82f);
        params.body = range(0.64f, 0.94f);
        params.ring = range(0.20f, 0.62f);
        params.bodyDecaySeconds = logRange(0.42f, 1.10f);
        params.punch = range(0.45f, 0.82f);
        params.wires = range(0.30f, 0.76f);
        params.wireTone = range(0.18f, 0.62f);
        params.wireTension = range(0.15f, 0.56f);
        params.wireDecaySeconds = logRange(0.28f, 0.92f);
        params.rattle = range(0.08f, 0.58f)
            * (1.10f - params.wireTension);
        params.click = range(0.02f, 0.17f);
        params.clickTone = range(0.25f, 0.66f);
        break;
    }
    case 1u: { // general frame snare
        params.tuneHz = logRange(132.0f, 235.0f);
        params.pitchDropSemitones = range(3.0f, 13.0f);
        params.pitchSweepMs = range(7.0f, 16.0f)
            + params.pitchDropSemitones * range(0.85f, 1.75f);
        params.shellSpread = range(0.24f, 0.76f);
        params.body = range(0.42f, 0.82f);
        params.ring = range(0.12f, 0.52f);
        params.bodyDecaySeconds = logRange(0.18f, 0.68f);
        params.punch = range(0.60f, 0.95f);
        params.wires = range(0.46f, 0.94f);
        params.wireTone = range(0.34f, 0.86f);
        params.wireTension = range(0.30f, 0.82f);
        params.wireDecaySeconds = logRange(0.14f, 0.68f);
        params.rattle = range(0.03f, 0.42f)
            * (1.12f - params.wireTension);
        params.click = range(0.04f, 0.29f);
        params.clickTone = range(0.34f, 0.84f);
        break;
    }
    case 2u: { // tight electronic snare
        params.tuneHz = logRange(178.0f, 305.0f);
        params.pitchDropSemitones = range(1.0f, 10.0f);
        params.pitchSweepMs = range(3.0f, 8.0f)
            + params.pitchDropSemitones * range(0.55f, 1.55f);
        params.shellSpread = range(0.15f, 0.66f);
        params.body = range(0.25f, 0.66f);
        params.ring = range(0.04f, 0.36f);
        params.bodyDecaySeconds = logRange(0.07f, 0.34f);
        params.punch = range(0.75f, 1.0f);
        params.wires = range(0.50f, 0.95f);
        params.wireTone = range(0.50f, 0.98f);
        params.wireTension = range(0.62f, 0.98f);
        params.wireDecaySeconds = logRange(0.055f, 0.32f);
        params.rattle = range(0.0f, 0.18f)
            * (1.08f - params.wireTension);
        params.click = range(0.12f, 0.46f);
        params.clickTone = range(0.55f, 0.98f);
        break;
    }
    case 3u:
    default: { // woody rim / side-stick continuum
        params.tuneHz = logRange(230.0f, 395.0f);
        params.pitchDropSemitones = range(0.0f, 5.0f);
        params.pitchSweepMs = range(2.5f, 7.0f)
            + params.pitchDropSemitones * range(0.35f, 1.35f);
        params.shellSpread = range(0.65f, 1.0f);
        params.body = range(0.05f, 0.42f);
        params.ring = range(0.15f, 0.85f);
        params.bodyDecaySeconds = logRange(0.10f, 0.38f);
        params.punch = range(0.76f, 1.0f);
        params.wires = range(0.01f, 0.35f);
        params.wireTone = range(0.45f, 0.95f);
        params.wireTension = range(0.65f, 0.98f);
        params.wireDecaySeconds = logRange(0.04f, 0.25f);
        params.rattle = range(0.0f, 0.12f)
            * (1.06f - params.wireTension);
        params.click = range(0.35f, 0.90f);
        params.clickTone = range(0.65f, 1.0f);
        break;
    }
    }

    // Character color shares a conservative degradation budget so rate and
    // depth reduction cannot both land at their harshest settings.  A little
    // reconstruction follows the amount of degradation instead of being an
    // unrelated roll of the dice.
    params.character.drive = range(0.01f,
        params.tuneHz > 225.0f ? 0.30f : 0.45f);
    params.character.bias = range(-0.22f, 0.22f);
    params.character.compression = range(0.01f, 0.55f);
    params.character.sampleRateReduction =
        std::pow(unit(), 1.65f) * 0.45f;
    params.character.bitDepthReduction =
        std::pow(unit(), 1.65f) * 0.36f;
    const float degradation = params.character.sampleRateReduction
        + params.character.bitDepthReduction;
    if (degradation > 0.58f) {
        const float scale = 0.58f / degradation;
        params.character.sampleRateReduction *= scale;
        params.character.bitDepthReduction *= scale;
    }
    params.character.reconstruction = std::min(0.62f,
        range(0.01f, 0.08f)
            + std::max(params.character.sampleRateReduction,
                params.character.bitDepthReduction)
                * range(0.45f, 1.15f));
    params.character.tone = range(-0.40f, 0.40f);
    params.stereoWidth = range(0.05f, 0.95f);
    return params;
}

inline DrumSnareParams drumSnareSafeRandomParams(
    const DrumSnareParams& current, uint32_t& state)
{
    DrumRandom random(state);
    DrumSnareParams params = drumSnareSafeRandomParams(current, random);
    state = random.state();
    return params;
}

inline int32_t drumSnareFactoryPresetIndex(const DrumSnareParams& params)
{
    // Output trim is intentionally session-local and does not participate in
    // voice preset recognition.
    const auto close = [](float left, float right) {
        return std::fabs(left - right) <= 1.0e-4f;
    };
    for (uint32_t index = 0u; index < kDrumSnareFactoryPresetCount; ++index) {
        const DrumSnareParams preset = drumSnareFactoryPreset(index);
        if (close(params.tuneHz, preset.tuneHz)
            && close(params.noteTracking, preset.noteTracking)
            && close(params.pitchDropSemitones, preset.pitchDropSemitones)
            && close(params.pitchSweepMs, preset.pitchSweepMs)
            && close(params.shellSpread, preset.shellSpread)
            && close(params.body, preset.body)
            && close(params.ring, preset.ring)
            && close(params.bodyDecaySeconds, preset.bodyDecaySeconds)
            && close(params.punch, preset.punch)
            && close(params.wires, preset.wires)
            && close(params.wireTone, preset.wireTone)
            && close(params.wireTension, preset.wireTension)
            && close(params.wireDecaySeconds, preset.wireDecaySeconds)
            && close(params.rattle, preset.rattle)
            && close(params.click, preset.click)
            && close(params.clickTone, preset.clickTone)
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
