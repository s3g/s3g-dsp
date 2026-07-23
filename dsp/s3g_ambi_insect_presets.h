#pragma once

#include "s3g_ambi_insect_encoder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

struct AmbiInsectPresetInfo {
    const char* name;
    const char* description;
};

inline constexpr uint32_t kAmbiInsectFactoryPresetCount = 18u;

inline constexpr std::array<AmbiInsectPresetInfo, kAmbiInsectFactoryPresetCount> kAmbiInsectPresetInfo {{
    { "Dusk Cricket Choir", "Layered field-cricket chirps with loose phrase coupling." },
    { "Katydid Waves", "Bright trills propagate through a broad elevated population." },
    { "Cicada Canopy", "Tymbal-like rib sequences radiate through a damped canopy." },
    { "Periodical Cicada Wall", "A dense synchronized cicada field with slow massed surges." },
    { "Mosquito Halo", "Close layered wingbeats circle and pass the listener." },
    { "Bee Passage", "Lower wingbeat bodies travel through a loose moving cluster." },
    { "Marsh Chorus", "Warm coupled chirpers spread across a low open field." },
    { "Porch at Midnight", "A small near-field choir reflected by a wooden porch." },
    { "Beetles in Dry Leaves", "Sparse resonant scrapes and ticks near the ground." },
    { "Interior Wall Activity", "Muted intermittent ticks inside a shallow wall cavity." },
    { "Greenhouse Flyers", "Small flyers roam vertically through a reflective interior." },
    { "High Meadow Trill", "Fast fine stridulation in a wide, airy meadow." },
    { "Sparse Winter Ticks", "Cold isolated body clicks with long rests." },
    { "Inharmonic Swarm", "Coordinated call motion with independently spaced wingbeat fundamentals." },
    { "Forest Layers", "Ground chirpers, canopy cicadas, and passing flyers coexist." },
    { "Mixed Summer Night", "A complete warm-night population across several strata." },
    { "Leaf Tremulation", "Substrate-borne body oscillations travel through a low leaf layer." },
    { "Tremulation Duet", "Alternating response calls move through a coupled substrate pair." },
}};

inline AmbiInsectPresetInfo ambiInsectFactoryPresetInfo(uint32_t index)
{
    return kAmbiInsectPresetInfo[std::min<uint32_t>(index, kAmbiInsectFactoryPresetCount - 1u)];
}

inline AmbiInsectParams ambiInsectFactoryPreset(uint32_t index)
{
    AmbiInsectParams params {};
    params.order = 3u;
    params.outputGainDb = -6.0f;
    index = std::min<uint32_t>(index, kAmbiInsectFactoryPresetCount - 1u);
    switch (index) {
    case 0u: // Dusk Cricket Choir
        params.voices = 30u; params.regime = 0u; params.activity = 0.64f; params.temperature = 0.58f;
        params.variation = 0.24f; params.coupling = 0.18f; params.phraseRateHz = 0.08f; params.chirpRateHz = 1.43f;
        params.pulseRateHz = 58.0f; params.callLength = 0.12f; params.rest = 0.30f; params.bodyPitchHz = 4300.0f;
        params.bodySize = 0.42f; params.rasp = 0.42f; params.wing = 0.10f; params.brightness = 0.64f;
        params.resonance = 0.42f; params.air = 0.24f; params.fieldRateHz = 0.028f; params.roam = 0.20f;
        params.cohesion = 0.36f; params.scatter = 0.62f; params.orbit = 0.06f; params.lift = 0.04f;
        params.nearPass = 0.04f; params.spatialFollow = 0.88f; params.place = 0u; params.space = 0.12f;
        break;
    case 1u: // Katydid Waves
        params.voices = 38u; params.regime = 1u; params.activity = 0.76f; params.temperature = 0.72f;
        params.variation = 0.28f; params.coupling = 0.46f; params.phraseRateHz = 0.24f; params.chirpRateHz = 16.0f;
        params.pulseRateHz = 185.0f; params.callLength = 0.72f; params.rest = 0.34f; params.bodyPitchHz = 7200.0f;
        params.bodySize = 0.24f; params.rasp = 0.56f; params.wing = 0.18f; params.brightness = 0.78f;
        params.resonance = 0.34f; params.air = 0.42f; params.fieldRateHz = 0.045f; params.roam = 0.30f;
        params.cohesion = 0.54f; params.scatter = 0.72f; params.orbit = 0.12f; params.lift = 0.30f;
        params.nearPass = 0.04f; params.spatialFollow = 0.82f; params.place = 2u; params.space = 0.20f;
        break;
    case 2u: // Cicada Canopy
        params.voices = 34u; params.regime = 2u; params.activity = 0.72f; params.temperature = 0.78f;
        params.variation = 0.22f; params.coupling = 0.30f; params.phraseRateHz = 0.18f; params.chirpRateHz = 24.0f;
        params.pulseRateHz = 820.0f; params.callLength = 0.78f; params.rest = 0.28f; params.bodyPitchHz = 4300.0f;
        params.bodySize = 0.62f; params.rasp = 0.48f; params.wing = 0.38f; params.brightness = 0.56f;
        params.resonance = 0.38f; params.air = 0.20f; params.fieldRateHz = 0.018f; params.roam = 0.16f;
        params.cohesion = 0.58f; params.scatter = 0.50f; params.orbit = 0.04f; params.lift = 0.56f;
        params.nearPass = 0.00f; params.spatialFollow = 0.94f; params.centerDistance = 1.18f;
        params.place = 2u; params.space = 0.24f;
        break;
    case 3u: // Periodical Cicada Wall
        params.voices = 56u; params.regime = 2u; params.activity = 0.90f; params.temperature = 0.84f;
        params.variation = 0.14f; params.coupling = 0.78f; params.phraseRateHz = 0.095f; params.chirpRateHz = 31.0f;
        params.pulseRateHz = 1100.0f; params.callLength = 0.88f; params.rest = 0.16f; params.bodyPitchHz = 3600.0f;
        params.bodySize = 0.70f; params.rasp = 0.56f; params.wing = 0.52f; params.brightness = 0.52f;
        params.resonance = 0.34f; params.air = 0.16f; params.fieldRateHz = 0.010f; params.roam = 0.08f;
        params.cohesion = 0.82f; params.scatter = 0.38f; params.orbit = 0.02f; params.lift = 0.62f;
        params.nearPass = 0.00f; params.spatialFollow = 0.97f; params.centerDistance = 1.28f;
        params.place = 2u; params.space = 0.30f;
        break;
    case 4u: // Mosquito Halo
        params.voices = 14u; params.regime = 3u; params.activity = 0.72f; params.temperature = 0.66f;
        params.variation = 0.16f; params.coupling = 0.52f; params.phraseRateHz = 0.32f; params.chirpRateHz = 2.8f;
        params.pulseRateHz = 720.0f; params.callLength = 0.82f; params.rest = 0.22f; params.bodyPitchHz = 510.0f;
        params.bodySize = 0.18f; params.rasp = 0.08f; params.wing = 0.82f; params.brightness = 0.68f;
        params.resonance = 0.20f; params.air = 0.30f; params.fieldRateHz = 0.22f; params.roam = 0.82f;
        params.cohesion = 0.34f; params.scatter = 0.44f; params.orbit = 0.86f; params.lift = 0.52f;
        params.nearPass = 0.92f; params.spatialFollow = 0.28f; params.centerDistance = 0.82f;
        params.place = 3u; params.space = 0.08f;
        break;
    case 5u: // Bee Passage
        params.voices = 18u; params.regime = 3u; params.activity = 0.62f; params.temperature = 0.62f;
        params.variation = 0.28f; params.coupling = 0.12f; params.phraseRateHz = 0.18f; params.chirpRateHz = 1.4f;
        params.pulseRateHz = 320.0f; params.callLength = 0.70f; params.rest = 0.42f; params.bodyPitchHz = 230.0f;
        params.bodySize = 0.72f; params.rasp = 0.18f; params.wing = 0.74f; params.brightness = 0.34f;
        params.resonance = 0.26f; params.air = 0.16f; params.fieldRateHz = 0.13f; params.roam = 0.72f;
        params.cohesion = 0.62f; params.scatter = 0.36f; params.orbit = 0.48f; params.lift = 0.34f;
        params.nearPass = 0.72f; params.spatialFollow = 0.36f; params.centerDistance = 1.02f;
        params.place = 0u; params.space = 0.10f;
        break;
    case 6u: // Marsh Chorus
        params.voices = 48u; params.regime = 0u; params.activity = 0.84f; params.temperature = 0.80f;
        params.variation = 0.32f; params.coupling = 0.54f; params.phraseRateHz = 0.12f; params.chirpRateHz = 2.4f;
        params.pulseRateHz = 72.0f; params.callLength = 0.18f; params.rest = 0.22f; params.bodyPitchHz = 3600.0f;
        params.bodySize = 0.50f; params.rasp = 0.46f; params.wing = 0.14f; params.brightness = 0.58f;
        params.resonance = 0.38f; params.air = 0.26f; params.fieldRateHz = 0.022f; params.roam = 0.18f;
        params.cohesion = 0.48f; params.scatter = 0.82f; params.orbit = 0.06f; params.lift = 0.08f;
        params.nearPass = 0.04f; params.spatialFollow = 0.92f; params.place = 3u; params.space = 0.16f;
        break;
    case 7u: // Porch at Midnight
        params.voices = 18u; params.regime = 0u; params.activity = 0.58f; params.temperature = 0.54f;
        params.variation = 0.20f; params.coupling = 0.16f; params.phraseRateHz = 0.06f; params.chirpRateHz = 1.6f;
        params.pulseRateHz = 58.0f; params.callLength = 0.13f; params.rest = 0.34f; params.bodyPitchHz = 4600.0f;
        params.bodySize = 0.40f; params.rasp = 0.34f; params.wing = 0.08f; params.brightness = 0.62f;
        params.resonance = 0.44f; params.air = 0.18f; params.fieldRateHz = 0.014f; params.roam = 0.10f;
        params.cohesion = 0.44f; params.scatter = 0.46f; params.orbit = 0.02f; params.lift = 0.02f;
        params.nearPass = 0.02f; params.spatialFollow = 0.96f; params.centerDistance = 0.94f;
        params.place = 4u; params.space = 0.38f; params.environmentDecay = 0.58f;
        break;
    case 8u: // Beetles in Dry Leaves
        params.voices = 28u; params.regime = 4u; params.activity = 0.58f; params.temperature = 0.46f;
        params.variation = 0.46f; params.coupling = 0.04f; params.phraseRateHz = 0.24f; params.chirpRateHz = 4.4f;
        params.pulseRateHz = 180.0f; params.callLength = 0.18f; params.rest = 0.72f; params.bodyPitchHz = 1450.0f;
        params.bodySize = 0.68f; params.rasp = 0.76f; params.wing = 0.12f; params.brightness = 0.46f;
        params.resonance = 0.36f; params.air = 0.22f; params.fieldRateHz = 0.028f; params.roam = 0.32f;
        params.cohesion = 0.20f; params.scatter = 0.86f; params.orbit = 0.04f; params.lift = 0.00f;
        params.nearPass = 0.06f; params.spatialFollow = 0.86f; params.place = 1u; params.space = 0.18f;
        break;
    case 9u: // Interior Wall Activity
        params.voices = 20u; params.regime = 4u; params.activity = 0.50f; params.temperature = 0.52f;
        params.variation = 0.52f; params.coupling = 0.02f; params.phraseRateHz = 0.18f; params.chirpRateHz = 3.0f;
        params.pulseRateHz = 120.0f; params.callLength = 0.16f; params.rest = 0.72f; params.bodyPitchHz = 920.0f;
        params.bodySize = 0.78f; params.rasp = 0.52f; params.wing = 0.08f; params.brightness = 0.22f;
        params.resonance = 0.42f; params.air = 0.06f; params.fieldRateHz = 0.006f; params.roam = 0.08f;
        params.cohesion = 0.76f; params.scatter = 0.30f; params.orbit = 0.00f; params.lift = 0.00f;
        params.nearPass = 0.02f; params.spatialFollow = 0.98f; params.centerDistance = 0.72f;
        params.place = 6u; params.space = 0.48f; params.environmentSize = 0.26f; params.environmentDecay = 0.36f;
        break;
    case 10u: // Greenhouse Flyers
        params.voices = 22u; params.regime = 3u; params.activity = 0.68f; params.temperature = 0.74f;
        params.variation = 0.34f; params.coupling = 0.20f; params.phraseRateHz = 0.26f; params.chirpRateHz = 2.2f;
        params.pulseRateHz = 460.0f; params.callLength = 0.74f; params.rest = 0.30f; params.bodyPitchHz = 360.0f;
        params.bodySize = 0.44f; params.rasp = 0.12f; params.wing = 0.76f; params.brightness = 0.52f;
        params.resonance = 0.26f; params.air = 0.24f; params.fieldRateHz = 0.11f; params.roam = 0.76f;
        params.cohesion = 0.52f; params.scatter = 0.52f; params.orbit = 0.62f; params.lift = 0.72f;
        params.nearPass = 0.58f; params.spatialFollow = 0.42f; params.place = 5u; params.space = 0.34f;
        break;
    case 11u: // High Meadow Trill
        params.voices = 42u; params.regime = 1u; params.activity = 0.78f; params.temperature = 0.68f;
        params.variation = 0.36f; params.coupling = 0.34f; params.phraseRateHz = 0.34f; params.chirpRateHz = 22.0f;
        params.pulseRateHz = 240.0f; params.callLength = 0.66f; params.rest = 0.38f; params.bodyPitchHz = 8600.0f;
        params.bodySize = 0.14f; params.rasp = 0.48f; params.wing = 0.18f; params.brightness = 0.88f;
        params.resonance = 0.30f; params.air = 0.48f; params.fieldRateHz = 0.036f; params.roam = 0.30f;
        params.cohesion = 0.34f; params.scatter = 0.88f; params.orbit = 0.08f; params.lift = 0.20f;
        params.nearPass = 0.04f; params.spatialFollow = 0.86f; params.place = 0u; params.space = 0.10f;
        break;
    case 12u: // Sparse Winter Ticks
        params.voices = 16u; params.regime = 4u; params.activity = 0.40f; params.temperature = 0.18f;
        params.variation = 0.58f; params.coupling = 0.00f; params.phraseRateHz = 0.10f; params.chirpRateHz = 1.25f;
        params.pulseRateHz = 80.0f; params.callLength = 0.12f; params.rest = 0.84f; params.bodyPitchHz = 2100.0f;
        params.bodySize = 0.52f; params.rasp = 0.32f; params.wing = 0.04f; params.brightness = 0.58f;
        params.resonance = 0.46f; params.air = 0.12f; params.fieldRateHz = 0.003f; params.roam = 0.04f;
        params.cohesion = 0.18f; params.scatter = 0.72f; params.orbit = 0.00f; params.lift = 0.00f;
        params.nearPass = 0.00f; params.spatialFollow = 0.99f; params.place = 1u; params.space = 0.14f;
        break;
    case 13u: // Inharmonic Swarm
        params.voices = 26u; params.regime = 3u; params.activity = 0.82f; params.temperature = 0.70f;
        params.variation = 0.12f; params.coupling = 0.88f; params.phraseRateHz = 0.22f; params.chirpRateHz = 2.0f;
        params.pulseRateHz = 610.0f; params.callLength = 0.86f; params.rest = 0.18f; params.bodyPitchHz = 430.0f;
        params.bodySize = 0.34f; params.rasp = 0.06f; params.wing = 0.92f; params.brightness = 0.64f;
        params.resonance = 0.28f; params.air = 0.26f; params.fieldRateHz = 0.075f; params.roam = 0.48f;
        params.cohesion = 0.84f; params.scatter = 0.24f; params.orbit = 0.58f; params.lift = 0.48f;
        params.nearPass = 0.36f; params.spatialFollow = 0.58f; params.place = 0u; params.space = 0.10f;
        break;
    case 14u: // Forest Layers
        params.voices = 52u; params.regime = 5u; params.activity = 0.74f; params.temperature = 0.64f;
        params.variation = 0.38f; params.coupling = 0.24f; params.phraseRateHz = 0.28f; params.chirpRateHz = 7.2f;
        params.pulseRateHz = 120.0f; params.callLength = 0.52f; params.rest = 0.42f; params.bodyPitchHz = 3800.0f;
        params.bodySize = 0.48f; params.rasp = 0.42f; params.wing = 0.34f; params.brightness = 0.56f;
        params.resonance = 0.36f; params.air = 0.24f; params.fieldRateHz = 0.024f; params.roam = 0.28f;
        params.cohesion = 0.48f; params.scatter = 0.72f; params.orbit = 0.18f; params.lift = 0.46f;
        params.nearPass = 0.18f; params.spatialFollow = 0.82f; params.place = 1u; params.space = 0.24f;
        break;
    case 15u: // Mixed Summer Night
        params.voices = 64u; params.regime = 5u; params.activity = 0.82f; params.temperature = 0.76f;
        params.variation = 0.42f; params.coupling = 0.34f; params.phraseRateHz = 0.36f; params.chirpRateHz = 8.8f;
        params.pulseRateHz = 150.0f; params.callLength = 0.58f; params.rest = 0.34f; params.bodyPitchHz = 4200.0f;
        params.bodySize = 0.46f; params.rasp = 0.46f; params.wing = 0.38f; params.brightness = 0.62f;
        params.resonance = 0.38f; params.air = 0.30f; params.fieldRateHz = 0.032f; params.roam = 0.36f;
        params.cohesion = 0.52f; params.scatter = 0.82f; params.orbit = 0.24f; params.lift = 0.48f;
        params.nearPass = 0.24f; params.spatialFollow = 0.78f; params.place = 0u; params.space = 0.16f;
        break;
    case 16u: // Leaf Tremulation
        params.voices = 18u; params.regime = 6u; params.activity = 0.62f; params.temperature = 0.55f;
        params.variation = 0.24f; params.coupling = 0.36f; params.phraseRateHz = 0.18f; params.chirpRateHz = 9.0f;
        params.pulseRateHz = 42.0f; params.callLength = 0.68f; params.rest = 0.38f; params.bodyPitchHz = 320.0f;
        params.bodySize = 0.66f; params.rasp = 0.26f; params.wing = 0.38f; params.brightness = 0.28f;
        params.resonance = 0.46f; params.air = 0.06f; params.fieldRateHz = 0.012f; params.roam = 0.14f;
        params.cohesion = 0.58f; params.scatter = 0.64f; params.orbit = 0.02f; params.lift = 0.00f;
        params.nearPass = 0.00f; params.spatialFollow = 0.96f; params.centerDistance = 0.92f;
        params.place = 1u; params.space = 0.14f; params.environmentDamping = 0.68f;
        break;
    default: // Tremulation Duet
        params.voices = 12u; params.regime = 6u; params.activity = 0.70f; params.temperature = 0.60f;
        params.variation = 0.14f; params.coupling = 0.62f; params.phraseRateHz = 0.28f; params.chirpRateHz = 6.2f;
        params.pulseRateHz = 34.0f; params.callLength = 0.56f; params.rest = 0.42f; params.bodyPitchHz = 240.0f;
        params.bodySize = 0.74f; params.rasp = 0.18f; params.wing = 0.44f; params.brightness = 0.22f;
        params.resonance = 0.52f; params.air = 0.04f; params.fieldRateHz = 0.008f; params.roam = 0.08f;
        params.cohesion = 0.72f; params.scatter = 0.48f; params.orbit = 0.00f; params.lift = 0.00f;
        params.nearPass = 0.00f; params.spatialFollow = 0.98f; params.centerDistance = 0.86f;
        params.place = 1u; params.space = 0.12f; params.environmentDamping = 0.74f;
        break;
    }
    constexpr std::array<uint32_t, kAmbiInsectFactoryPresetCount> callTypes {
        1u, 1u, 0u, 1u, 10u, 10u, 1u, 0u, 0u,
        2u, 10u, 0u, 0u, 10u, 1u, 1u, 4u, 2u
    };
    constexpr std::array<uint32_t, kAmbiInsectFactoryPresetCount> sceneSeeds {
        0xa511e9b3u, 0x63d83595u, 0x9e3779b9u, 0x7f4a7c15u,
        0x94d049bbu, 0x369dea0fu, 0xdb4f0b91u, 0xbb67ae85u,
        0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x1f83d9abu,
        0x5be0cd19u, 0xc2b2ae35u, 0x27d4eb2du, 0x165667b1u,
        0x85ebca6bu, 0xd3a2646cu,
    };
    params.callType = callTypes[index];
    params.sceneSeed = sceneSeeds[index];
    return params;
}

struct AmbiInsectCinematicBounds {
    float pitchLow;
    float pitchHigh;
    float phraseLow;
    float phraseHigh;
    float chirpLow;
    float chirpHigh;
    float pulseLow;
    float pulseHigh;
    float lengthLow;
    float lengthHigh;
    float restLow;
    float restHigh;
};

inline uint32_t ambiInsectRandomBits(uint32_t& seed)
{
    seed += 0x9e3779b9u;
    uint32_t value = seed;
    value = (value ^ (value >> 16u)) * 0x21f0aaadu;
    value = (value ^ (value >> 15u)) * 0x735a2d97u;
    return value ^ (value >> 15u);
}

inline float ambiInsectRandomUnit(uint32_t& seed)
{
    return static_cast<float>(ambiInsectRandomBits(seed) & 0x00ffffffu)
        / static_cast<float>(0x01000000u);
}

inline uint32_t ambiInsectRandomChoice(uint32_t& seed, uint32_t count)
{
    if (count == 0u) return 0u;
    return std::min<uint32_t>(count - 1u,
        static_cast<uint32_t>(
            ambiInsectRandomUnit(seed) * static_cast<float>(count)));
}

inline float ambiInsectExponentialRange(
    float low, float high, float amount)
{
    return low * std::pow(
        std::max(1.0f, high / std::max(0.0001f, low)),
        clamp(amount, 0.0f, 1.0f));
}

inline AmbiInsectCinematicBounds ambiInsectCinematicBounds(
    uint32_t regime)
{
    switch (std::min<uint32_t>(regime, kAmbiInsectRegimeCount - 1u)) {
    case 0u:
        return { 1800.0f, 6200.0f, 0.045f, 0.45f, 0.8f, 5.0f,
            42.0f, 96.0f, 0.08f, 0.34f, 0.18f, 0.68f };
    case 1u:
        return { 4200.0f, 9400.0f, 0.08f, 0.70f, 8.0f, 34.0f,
            110.0f, 420.0f, 0.42f, 0.90f, 0.18f, 0.55f };
    case 2u:
        return { 2200.0f, 6500.0f, 0.05f, 0.36f, 12.0f, 34.0f,
            480.0f, 1300.0f, 0.58f, 0.94f, 0.10f, 0.50f };
    case 3u:
        return { 180.0f, 760.0f, 0.06f, 0.50f, 0.8f, 4.5f,
            180.0f, 760.0f, 0.55f, 0.94f, 0.10f, 0.48f };
    case 4u:
        return { 500.0f, 3000.0f, 0.06f, 0.60f, 0.8f, 7.0f,
            60.0f, 300.0f, 0.06f, 0.35f, 0.42f, 0.88f };
    case 5u:
        return { 1800.0f, 5200.0f, 0.08f, 0.60f, 2.0f, 11.0f,
            80.0f, 320.0f, 0.35f, 0.80f, 0.18f, 0.58f };
    default:
        return { 140.0f, 780.0f, 0.06f, 0.45f, 2.0f, 18.0f,
            24.0f, 120.0f, 0.35f, 0.85f, 0.18f, 0.65f };
    }
}

inline uint32_t ambiInsectCinematicCallType(
    uint32_t& seed, uint32_t regime)
{
    constexpr std::array<uint32_t, 5> social {
        0u, 1u, 4u, 7u, 9u
    };
    constexpr std::array<uint32_t, 4> flight {
        10u, 10u, 0u, 4u
    };
    constexpr std::array<uint32_t, 5> percussion {
        0u, 2u, 7u, 9u, 9u
    };
    constexpr std::array<uint32_t, 6> tremulation {
        0u, 2u, 3u, 4u, 5u, 7u
    };
    if (regime == 3u) {
        return flight[ambiInsectRandomChoice(
            seed, static_cast<uint32_t>(flight.size()))];
    }
    if (regime == 4u) {
        return percussion[ambiInsectRandomChoice(
            seed, static_cast<uint32_t>(percussion.size()))];
    }
    if (regime == kAmbiInsectTremulationRegime) {
        return tremulation[ambiInsectRandomChoice(
            seed, static_cast<uint32_t>(tremulation.size()))];
    }
    return social[ambiInsectRandomChoice(
        seed, static_cast<uint32_t>(social.size()))];
}

inline AmbiInsectParams ambiInsectCinematicRandomParamsForRegime(
    uint32_t& seed, uint32_t regime)
{
    regime = std::min<uint32_t>(regime, kAmbiInsectRegimeCount - 1u);
    const auto bounds = ambiInsectCinematicBounds(regime);
    const float metabolism = ambiInsectRandomUnit(seed);
    const float morphology = ambiInsectRandomUnit(seed);
    const float articulation = ambiInsectRandomUnit(seed);
    const float social = ambiInsectRandomUnit(seed);
    const float texture = ambiInsectRandomUnit(seed);
    const float density = ambiInsectRandomUnit(seed);
    const float motion = ambiInsectRandomUnit(seed);
    const float habitat = ambiInsectRandomUnit(seed);
    const float rhythmicDrive = clamp(
        metabolism * 0.62f + articulation * 0.38f, 0.0f, 1.0f);

    AmbiInsectParams params {};
    params.order = 3u;
    params.regime = regime;
    params.callType = ambiInsectCinematicCallType(seed, regime);
    const uint32_t minimumVoices = regime == kAmbiInsectMixedRegime
        ? 24u : regime == 3u ? 8u : 10u;
    const uint32_t maximumVoices = regime == kAmbiInsectMixedRegime
        ? 64u : regime == 3u ? 36u : 56u;
    params.voices = minimumVoices + static_cast<uint32_t>(
        std::lround(density
            * static_cast<float>(maximumVoices - minimumVoices)));
    params.temperature = 0.18f + metabolism * 0.72f;
    params.activity = clamp(
        0.30f + (metabolism * 0.62f + density * 0.38f) * 0.56f,
        0.30f, 0.90f);
    params.variation = clamp(
        0.08f + (texture * 0.62f + (1.0f - social) * 0.38f)
            * 0.48f,
        0.08f, 0.58f);
    params.coupling = clamp(
        0.02f + social * (0.52f + density * 0.26f),
        0.02f, 0.82f);

    params.bodyPitchHz = ambiInsectExponentialRange(
        bounds.pitchLow, bounds.pitchHigh, morphology);
    params.bodySize = clamp(
        0.86f - morphology * 0.68f + (texture - 0.5f) * 0.08f,
        0.12f, 0.88f);
    params.phraseRateHz = ambiInsectExponentialRange(
        bounds.phraseLow, bounds.phraseHigh,
        rhythmicDrive * 0.58f + social * 0.42f);
    params.chirpRateHz = ambiInsectExponentialRange(
        bounds.chirpLow, bounds.chirpHigh,
        rhythmicDrive * 0.72f + social * 0.28f);
    params.pulseRateHz = regime == 3u
        ? params.bodyPitchHz
        : ambiInsectExponentialRange(
            bounds.pulseLow, bounds.pulseHigh,
            metabolism * 0.76f + morphology * 0.24f);
    const float sustained = clamp(
        articulation * 0.55f + social * 0.30f
            + metabolism * 0.15f,
        0.0f, 1.0f);
    params.callLength = lerp(
        bounds.lengthLow, bounds.lengthHigh, sustained);
    params.rest = lerp(
        bounds.restHigh, bounds.restLow,
        params.activity * 0.58f + articulation * 0.42f);

    static constexpr std::array<float, kAmbiInsectRegimeCount> raspBase {
        0.24f, 0.30f, 0.28f, 0.03f, 0.36f, 0.24f, 0.12f
    };
    static constexpr std::array<float, kAmbiInsectRegimeCount> raspRange {
        0.38f, 0.40f, 0.34f, 0.16f, 0.46f, 0.36f, 0.28f
    };
    static constexpr std::array<float, kAmbiInsectRegimeCount> wingBase {
        0.05f, 0.08f, 0.22f, 0.62f, 0.02f, 0.18f, 0.20f
    };
    static constexpr std::array<float, kAmbiInsectRegimeCount> wingRange {
        0.17f, 0.22f, 0.38f, 0.32f, 0.14f, 0.34f, 0.34f
    };
    params.rasp = clamp(
        raspBase[regime] + texture * raspRange[regime], 0.0f, 0.90f);
    params.wing = clamp(
        wingBase[regime] + (0.62f * articulation + 0.38f * motion)
            * wingRange[regime],
        0.0f, 0.96f);
    params.brightness = clamp(
        0.20f + morphology * 0.58f + texture * 0.10f,
        0.18f, 0.90f);
    params.resonance = clamp(
        0.28f + (1.0f - texture) * 0.26f
            + params.bodySize * 0.18f,
        0.24f, 0.78f);
    params.air = clamp(
        0.05f + habitat * 0.20f + params.brightness * 0.22f,
        0.04f, 0.52f);

    params.fieldRateHz = ambiInsectExponentialRange(
        0.004f, regime == 3u ? 0.30f : 0.075f, motion);
    params.roam = clamp(0.06f + motion * 0.72f, 0.06f, 0.82f);
    params.cohesion = clamp(
        0.18f + social * 0.62f, 0.12f, 0.86f);
    params.scatter = clamp(
        0.26f + (1.0f - social) * 0.34f + density * 0.28f,
        0.22f, 0.90f);
    params.orbit = clamp(
        motion * (regime == 3u ? 0.90f : 0.30f), 0.0f, 0.92f);
    params.lift = clamp(
        motion * (regime == 2u || regime == 3u ? 0.78f : 0.28f),
        0.0f, 0.82f);
    params.nearPass = clamp(
        motion * (regime == 3u ? 0.88f : 0.18f), 0.0f, 0.92f);
    params.spatialFollow = clamp(
        0.92f - motion * 0.58f, 0.26f, 0.96f);
    params.centerAzimuthDeg =
        (ambiInsectRandomUnit(seed) * 2.0f - 1.0f) * 35.0f;
    params.centerElevationDeg =
        (ambiInsectRandomUnit(seed) * 2.0f - 1.0f) * 20.0f;
    params.centerDistance = 0.84f
        + ambiInsectRandomUnit(seed) * 0.52f;
    params.place = ambiInsectRandomChoice(seed, kAmbiInsectPlaceCount);
    params.space = clamp(
        0.05f + habitat * (params.place == 0u ? 0.20f : 0.46f),
        0.04f, 0.54f);
    params.environmentSize = 0.28f + habitat * 0.52f;
    params.environmentDecay = 0.30f
        + (habitat * 0.68f + social * 0.32f) * 0.48f;
    params.environmentDamping = 0.24f
        + (1.0f - params.brightness) * 0.34f
        + habitat * 0.18f;
    params.outputGainDb = -6.0f;
    params.sceneSeed = ambiInsectRandomBits(seed);
    if (params.sceneSeed == 0u) {
        params.sceneSeed = kAmbiInsectDefaultSceneSeed;
    }
    return params;
}

inline AmbiInsectParams ambiInsectCinematicRandomParams(uint32_t& seed)
{
    const uint32_t regime = ambiInsectRandomChoice(
        seed, kAmbiInsectRegimeCount);
    return ambiInsectCinematicRandomParamsForRegime(seed, regime);
}

} // namespace s3g
