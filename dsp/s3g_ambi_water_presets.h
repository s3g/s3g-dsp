#pragma once

#include "s3g_ambi_water_encoder.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace s3g {

struct AmbiWaterPresetInfo {
    const char* name;
    const char* description;
};

inline constexpr uint32_t kAmbiWaterFactoryPresetCount = 16;

inline constexpr std::array<AmbiWaterPresetInfo, kAmbiWaterFactoryPresetCount> kAmbiWaterPresetInfo {{
    { "Shallow Creek", "A broad, lightly aerated current over open ground." },
    { "Rain on Leaves", "Fine falling droplets with soft leafy impacts and diffuse tails." },
    { "Concrete Spillway", "Fast bright water descending over hard concrete." },
    { "Cave Drips", "Sparse heavy impacts with occasional delayed pool cavities in a dark enclosure." },
    { "Harbor Slosh", "Heavy low water moving slowly against hard boundaries." },
    { "Drain Vortex", "Converging spiral motion with rising bubbles and pipe transfer." },
    { "Distant Waterfall", "A wide descending field of body noise and spray." },
    { "Underwater Bubbles", "Deep pool motion with rising, splitting, and softly popping bubbles." },
    { "Pebble Rapids", "Small turbulent current with dense rock contacts." },
    { "Window Rain", "Close bright impact pulses with short glass responses and no liquid cavities." },
    { "Metal Cistern", "Weighty isolated drops exciting a short, damped metal response." },
    { "Slow Shoreline", "Long advancing and retreating surface bands." },
    { "Mud Channel", "Dark damped flow with broad slow movement." },
    { "Pipe Runoff", "Focused current, intermittent bubbles, and pipe tone." },
    { "Storm Sheet", "Dense rain and surface splash across a wide field." },
    { "Still Pool", "Subtle body movement, isolated drops, and low bubbles." },
}};

inline AmbiWaterPresetInfo ambiWaterFactoryPresetInfo(uint32_t index)
{
    return kAmbiWaterPresetInfo[std::min<uint32_t>(index, kAmbiWaterFactoryPresetCount - 1u)];
}

inline AmbiWaterParams ambiWaterFactoryPreset(uint32_t index)
{
    AmbiWaterParams p {};
    p.order = 3u;
    p.outputGainDb = -6.0f;
    switch (std::min<uint32_t>(index, kAmbiWaterFactoryPresetCount - 1u)) {
    case 0u: // Shallow Creek
        p.voices = 32u; p.regime = 0u; p.environment = 0u;
        p.water = 0.62f; p.flow = 0.58f; p.scale = 0.34f; p.turbulence = 0.42f; p.aeration = 0.32f;
        p.drops = 0.12f; p.splash = 0.24f; p.bubbles = 0.10f; p.density = 0.40f;
        p.depth = 0.34f; p.brightness = 0.46f; p.resonance = 0.22f; p.damping = 0.54f; p.contact = 0.22f;
        p.motionRateHz = 0.16f; p.current = 0.62f; p.slope = -0.10f; p.eddy = 0.36f; p.width = 0.72f;
        break;
    case 1u: // Rain on Leaves
        p.voices = 38u; p.regime = 1u; p.environment = 2u;
        p.water = 0.30f; p.flow = 0.16f; p.scale = 0.22f; p.turbulence = 0.38f; p.aeration = 0.48f;
        p.drops = 0.84f; p.splash = 0.34f; p.bubbles = 0.00f; p.density = 0.72f; p.eventSize = 0.14f; p.eventDecay = 0.14f;
        p.depth = 0.18f; p.brightness = 0.68f; p.resonance = 0.18f; p.damping = 0.72f; p.contact = 0.48f;
        p.motionRateHz = 0.34f; p.current = 0.56f; p.slope = -0.68f; p.eddy = 0.22f; p.width = 0.82f;
        break;
    case 2u: // Concrete Spillway
        p.voices = 40u; p.regime = 2u; p.environment = 4u;
        p.water = 0.76f; p.flow = 0.82f; p.scale = 0.66f; p.turbulence = 0.68f; p.aeration = 0.72f;
        p.drops = 0.16f; p.splash = 0.78f; p.bubbles = 0.28f; p.density = 0.66f; p.eventSize = 0.52f;
        p.depth = 0.52f; p.brightness = 0.62f; p.resonance = 0.28f; p.damping = 0.38f; p.contact = 0.60f;
        p.motionRateHz = 0.28f; p.current = 0.82f; p.slope = -0.82f; p.eddy = 0.40f; p.width = 0.78f;
        break;
    case 3u: // Cave Drips
        p.voices = 18u; p.regime = 6u; p.environment = 8u;
        p.water = 0.12f; p.flow = 0.06f; p.scale = 0.58f; p.turbulence = 0.08f; p.aeration = 0.08f;
        p.drops = 0.86f; p.splash = 0.22f; p.bubbles = 0.10f; p.density = 0.16f; p.eventSize = 0.66f; p.eventDecay = 0.76f;
        p.depth = 0.74f; p.brightness = 0.30f; p.resonance = 0.44f; p.damping = 0.42f; p.contact = 0.48f;
        p.motionRateHz = 0.045f; p.current = 0.24f; p.slope = -0.74f; p.eddy = 0.08f; p.width = 0.66f;
        break;
    case 4u: // Harbor Slosh
        p.voices = 36u; p.regime = 5u; p.environment = 4u;
        p.water = 0.76f; p.flow = 0.42f; p.scale = 0.88f; p.turbulence = 0.24f; p.aeration = 0.16f;
        p.drops = 0.04f; p.splash = 0.42f; p.bubbles = 0.16f; p.density = 0.24f; p.eventSize = 0.82f; p.eventDecay = 0.68f;
        p.depth = 0.82f; p.brightness = 0.22f; p.resonance = 0.34f; p.damping = 0.58f; p.contact = 0.48f;
        p.motionRateHz = 0.055f; p.current = 0.38f; p.slope = 0.0f; p.eddy = 0.22f; p.width = 0.84f;
        break;
    case 5u: // Drain Vortex
        p.voices = 24u; p.regime = 4u; p.environment = 7u;
        p.water = 0.58f; p.flow = 0.58f; p.scale = 0.42f; p.turbulence = 0.42f; p.aeration = 0.34f;
        p.drops = 0.08f; p.splash = 0.28f; p.bubbles = 0.72f; p.density = 0.36f; p.eventSize = 0.42f; p.eventDecay = 0.72f;
        p.depth = 0.62f; p.brightness = 0.42f; p.resonance = 0.48f; p.damping = 0.42f; p.contact = 0.58f;
        p.motionRateHz = 0.32f; p.current = 0.72f; p.slope = -0.52f; p.eddy = 0.52f; p.convergence = 0.88f; p.width = 0.58f;
        break;
    case 6u: // Distant Waterfall
        p.voices = 54u; p.regime = 2u; p.environment = 1u;
        p.water = 0.72f; p.flow = 0.82f; p.scale = 0.90f; p.turbulence = 0.62f; p.aeration = 0.84f;
        p.drops = 0.08f; p.splash = 0.62f; p.bubbles = 0.30f; p.density = 0.68f; p.eventSize = 0.66f;
        p.depth = 0.62f; p.brightness = 0.48f; p.resonance = 0.20f; p.damping = 0.62f; p.contact = 0.18f;
        p.motionRateHz = 0.12f; p.current = 0.72f; p.slope = -0.92f; p.eddy = 0.42f; p.width = 0.92f;
        p.centerDistance = 1.42f; p.spatialFollow = 0.90f;
        break;
    case 7u: // Underwater Bubbles
        p.voices = 22u; p.regime = 7u; p.environment = 0u;
        p.water = 0.24f; p.flow = 0.08f; p.scale = 0.68f; p.turbulence = 0.10f; p.aeration = 0.10f;
        p.drops = 0.00f; p.splash = 0.02f; p.bubbles = 0.92f; p.density = 0.44f; p.eventSize = 0.66f; p.eventDecay = 0.86f;
        p.depth = 0.88f; p.brightness = 0.24f; p.resonance = 0.68f; p.damping = 0.48f; p.contact = 0.04f;
        p.motionRateHz = 0.11f; p.current = 0.34f; p.slope = 0.48f; p.eddy = 0.46f; p.width = 0.62f;
        break;
    case 8u: // Pebble Rapids
        p.voices = 44u; p.regime = 0u; p.environment = 1u;
        p.water = 0.70f; p.flow = 0.78f; p.scale = 0.32f; p.turbulence = 0.82f; p.aeration = 0.66f;
        p.drops = 0.20f; p.splash = 0.64f; p.bubbles = 0.22f; p.density = 0.74f; p.eventSize = 0.24f; p.eventDecay = 0.24f;
        p.depth = 0.28f; p.brightness = 0.66f; p.resonance = 0.42f; p.damping = 0.40f; p.contact = 0.72f;
        p.motionRateHz = 0.42f; p.current = 0.86f; p.slope = -0.24f; p.eddy = 0.68f; p.width = 0.78f;
        break;
    case 9u: // Window Rain
        p.voices = 34u; p.regime = 1u; p.environment = 6u;
        p.water = 0.20f; p.flow = 0.10f; p.scale = 0.18f; p.turbulence = 0.26f; p.aeration = 0.24f;
        p.drops = 0.94f; p.splash = 0.18f; p.bubbles = 0.00f; p.density = 0.76f; p.eventSize = 0.24f; p.eventDecay = 0.18f;
        p.depth = 0.12f; p.brightness = 0.76f; p.resonance = 0.28f; p.damping = 0.48f; p.contact = 0.86f;
        p.motionRateHz = 0.26f; p.current = 0.48f; p.slope = -0.82f; p.eddy = 0.16f; p.width = 0.72f;
        break;
    case 10u: // Metal Cistern
        p.voices = 22u; p.regime = 6u; p.environment = 5u;
        p.water = 0.38f; p.flow = 0.18f; p.scale = 0.66f; p.turbulence = 0.20f; p.aeration = 0.16f;
        p.drops = 0.78f; p.splash = 0.24f; p.bubbles = 0.12f; p.density = 0.40f; p.eventSize = 0.64f; p.eventDecay = 0.60f;
        p.depth = 0.70f; p.brightness = 0.48f; p.resonance = 0.38f; p.damping = 0.44f; p.contact = 0.88f;
        p.motionRateHz = 0.065f; p.current = 0.30f; p.eddy = 0.24f; p.width = 0.52f;
        break;
    case 11u: // Slow Shoreline
        p.voices = 42u; p.regime = 3u; p.environment = 0u;
        p.water = 0.68f; p.flow = 0.40f; p.scale = 0.82f; p.turbulence = 0.32f; p.aeration = 0.36f;
        p.drops = 0.04f; p.splash = 0.52f; p.bubbles = 0.14f; p.density = 0.34f; p.eventSize = 0.72f; p.eventDecay = 0.68f;
        p.depth = 0.68f; p.brightness = 0.38f; p.resonance = 0.24f; p.damping = 0.62f; p.contact = 0.12f;
        p.motionRateHz = 0.042f; p.current = 0.48f; p.slope = -0.08f; p.eddy = 0.20f; p.width = 0.96f;
        break;
    case 12u: // Mud Channel
        p.voices = 30u; p.regime = 0u; p.environment = 3u;
        p.water = 0.64f; p.flow = 0.46f; p.scale = 0.74f; p.turbulence = 0.28f; p.aeration = 0.10f;
        p.drops = 0.04f; p.splash = 0.18f; p.bubbles = 0.22f; p.density = 0.30f; p.eventSize = 0.68f; p.eventDecay = 0.48f;
        p.depth = 0.82f; p.brightness = 0.14f; p.resonance = 0.12f; p.damping = 0.88f; p.contact = 0.42f;
        p.motionRateHz = 0.085f; p.current = 0.48f; p.slope = -0.16f; p.eddy = 0.20f; p.width = 0.62f;
        break;
    case 13u: // Pipe Runoff
        p.voices = 24u; p.regime = 0u; p.environment = 7u;
        p.water = 0.58f; p.flow = 0.72f; p.scale = 0.38f; p.turbulence = 0.46f; p.aeration = 0.34f;
        p.drops = 0.12f; p.splash = 0.28f; p.bubbles = 0.46f; p.density = 0.44f; p.eventSize = 0.34f; p.eventDecay = 0.66f;
        p.depth = 0.52f; p.brightness = 0.42f; p.resonance = 0.50f; p.damping = 0.40f; p.contact = 0.72f;
        p.motionRateHz = 0.22f; p.current = 0.78f; p.slope = -0.28f; p.eddy = 0.28f; p.convergence = 0.42f; p.width = 0.34f;
        break;
    case 14u: // Storm Sheet
        p.voices = 56u; p.regime = 1u; p.environment = 4u;
        p.water = 0.54f; p.flow = 0.34f; p.scale = 0.58f; p.turbulence = 0.76f; p.aeration = 0.76f;
        p.drops = 0.94f; p.splash = 0.82f; p.bubbles = 0.06f; p.density = 0.92f; p.eventSize = 0.20f; p.eventDecay = 0.16f;
        p.depth = 0.42f; p.brightness = 0.72f; p.resonance = 0.32f; p.damping = 0.48f; p.contact = 0.54f;
        p.motionRateHz = 0.52f; p.current = 0.82f; p.slope = -0.88f; p.eddy = 0.52f; p.width = 1.0f;
        break;
    default: // Still Pool
        p.voices = 20u; p.regime = 5u; p.environment = 0u;
        p.water = 0.28f; p.flow = 0.08f; p.scale = 0.72f; p.turbulence = 0.06f; p.aeration = 0.08f;
        p.drops = 0.24f; p.splash = 0.04f; p.bubbles = 0.26f; p.density = 0.14f; p.eventSize = 0.56f; p.eventDecay = 0.78f;
        p.depth = 0.76f; p.brightness = 0.22f; p.resonance = 0.42f; p.damping = 0.72f; p.contact = 0.08f;
        p.motionRateHz = 0.018f; p.current = 0.14f; p.slope = 0.0f; p.eddy = 0.18f; p.width = 0.58f;
        break;
    }
    return p;
}

} // namespace s3g
