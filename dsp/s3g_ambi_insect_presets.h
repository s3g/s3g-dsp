#pragma once

#include "s3g_ambi_insect_encoder.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace s3g {

struct AmbiInsectPresetInfo {
    const char* name;
    const char* description;
};

inline constexpr uint32_t kAmbiInsectFactoryPresetCount = 16u;

inline constexpr std::array<AmbiInsectPresetInfo, kAmbiInsectFactoryPresetCount> kAmbiInsectPresetInfo {{
    { "Dusk Cricket Choir", "Layered field-cricket chirps with loose phrase coupling." },
    { "Katydid Waves", "Bright trills propagate through a broad elevated population." },
    { "Cicada Canopy", "Tymbal-like rib sequences radiate through a damped canopy." },
    { "Periodical Cicada Wall", "A dense synchronized cicada field with slow massed surges." },
    { "Mosquito Halo", "Close harmonic wingbeats circle and pass the listener." },
    { "Bee Passage", "Lower wingbeat bodies travel through a loose moving cluster." },
    { "Marsh Chorus", "Warm coupled chirpers spread across a low open field." },
    { "Porch at Midnight", "A small near-field choir reflected by a wooden porch." },
    { "Beetles in Dry Leaves", "Sparse resonant scrapes and ticks near the ground." },
    { "Interior Wall Activity", "Muted intermittent ticks inside a shallow wall cavity." },
    { "Greenhouse Flyers", "Small flyers roam vertically through a reflective interior." },
    { "High Meadow Trill", "Fast fine stridulation in a wide, airy meadow." },
    { "Sparse Winter Ticks", "Cold isolated body clicks with long rests." },
    { "Harmonic Swarm", "Wingbeat fundamentals pull toward shared harmonic motion." },
    { "Forest Layers", "Ground chirpers, canopy cicadas, and passing flyers coexist." },
    { "Mixed Summer Night", "A complete warm-night population across several strata." },
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
    case 13u: // Harmonic Swarm
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
    default: // Mixed Summer Night
        params.voices = 64u; params.regime = 5u; params.activity = 0.82f; params.temperature = 0.76f;
        params.variation = 0.42f; params.coupling = 0.34f; params.phraseRateHz = 0.36f; params.chirpRateHz = 8.8f;
        params.pulseRateHz = 150.0f; params.callLength = 0.58f; params.rest = 0.34f; params.bodyPitchHz = 4200.0f;
        params.bodySize = 0.46f; params.rasp = 0.46f; params.wing = 0.38f; params.brightness = 0.62f;
        params.resonance = 0.38f; params.air = 0.30f; params.fieldRateHz = 0.032f; params.roam = 0.36f;
        params.cohesion = 0.52f; params.scatter = 0.82f; params.orbit = 0.24f; params.lift = 0.48f;
        params.nearPass = 0.24f; params.spatialFollow = 0.78f; params.place = 0u; params.space = 0.16f;
        break;
    }
    return params;
}

} // namespace s3g
