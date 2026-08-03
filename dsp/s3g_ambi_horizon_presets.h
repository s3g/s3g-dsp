#pragma once

#include "s3g_ambi_horizon_encoder.h"

#include <array>
#include <cstdint>

namespace s3g {

struct AmbiHorizonPresetInfo {
    const char* name = "";
    const char* description = "";
};

inline constexpr std::array<AmbiHorizonPresetInfo, 12u> kAmbiHorizonPresetInfo {{
    { "BELL ACROSS VALLEY", "Sparse modal signals carried over a quiet rural basin." },
    { "CLEAR NIGHT INVERSION", "Long atmospheric reach with a low local noise floor." },
    { "HIGHWAY BEYOND FIELDS", "A continuous traffic band behind open agricultural ground." },
    { "CITY BEYOND RIDGE", "Diffuse urban activity softened by intervening terrain." },
    { "INDUSTRIAL NIGHT REACH", "Tonal machinery and occasional pulses at the horizon." },
    { "ACROSS STILL WATER", "Distant signals with strong low-frequency surface carry." },
    { "DAWN LONG HORIZON", "Air, modal calls, and a wide gradually waking field." },
    { "STORM BEYOND HILLS", "Broad turbulent weather energy obscured by terrain." },
    { "DISTANT RAIL CORRIDOR", "Intermittent motors and pulses crossing a narrow arc." },
    { "QUIET AGRICULTURAL BASIN", "Sparse rural events inside an exceptionally quiet floor." },
    { "SETTLEMENT THROUGH FOREST", "A small city bed strongly filtered by forest ground." },
    { "VANISHING ACOUSTIC HORIZON", "Extreme distance with only residual form and audibility." },
}};

inline constexpr uint32_t kAmbiHorizonFactoryPresetCount =
    static_cast<uint32_t>(kAmbiHorizonPresetInfo.size());

inline AmbiHorizonEncoderParams ambiHorizonFactoryPreset(uint32_t index)
{
    AmbiHorizonEncoderParams p {};
    switch (index % kAmbiHorizonFactoryPresetCount) {
    case 0u:
        p.ecology = AmbiHorizonEcology::Rural;
        p.entities = 18u; p.activity = 0.34f; p.occupancy = 0.18f;
        p.pace = 0.24f; p.memory = 0.86f; p.cascade = 0.28f;
        p.signals = 0.74f; p.horizonBed = 0.30f; p.localFloor = 0.12f;
        p.rangeKm = 4.8f; p.arcDeg = 105.0f; p.detail = 0.64f;
        p.air = 0.48f; p.ground = AmbiHorizonGround::Grass;
        p.terrain = 0.54f; p.carry = 0.34f; p.turbulence = 0.10f;
        p.edgeDb = 1.5f; p.seed = 311u;
        break;
    case 1u:
        p.ecology = AmbiHorizonEcology::Mixed;
        p.entities = 20u; p.activity = 0.26f; p.occupancy = 0.24f;
        p.pace = 0.17f; p.memory = 0.91f; p.cascade = 0.22f;
        p.signals = 0.58f; p.horizonBed = 0.34f; p.localFloor = 0.06f;
        p.rangeKm = 7.5f; p.arcDeg = 300.0f; p.detail = 0.42f;
        p.air = 0.34f; p.ground = AmbiHorizonGround::Grass;
        p.terrain = 0.22f; p.carry = 0.68f; p.turbulence = 0.05f;
        p.edgeDb = 2.0f; p.seed = 907u;
        break;
    case 2u:
        p.ecology = AmbiHorizonEcology::Traffic;
        p.entities = 28u; p.activity = 0.54f; p.occupancy = 0.82f;
        p.pace = 0.46f; p.memory = 0.72f; p.cascade = 0.44f;
        p.signals = 0.38f; p.horizonBed = 0.84f; p.localFloor = 0.13f;
        p.rangeKm = 2.6f; p.azimuthDeg = -18.0f; p.arcDeg = 150.0f;
        p.detail = 0.35f; p.air = 0.62f; p.ground = AmbiHorizonGround::Grass;
        p.terrain = 0.30f; p.carry = 0.08f; p.turbulence = 0.18f;
        p.edgeDb = -1.0f; p.seed = 4051u;
        break;
    case 3u:
        p.ecology = AmbiHorizonEcology::City;
        p.entities = 32u; p.activity = 0.58f; p.occupancy = 0.70f;
        p.pace = 0.52f; p.memory = 0.62f; p.cascade = 0.56f;
        p.signals = 0.44f; p.horizonBed = 0.76f; p.localFloor = 0.16f;
        p.rangeKm = 5.2f; p.azimuthDeg = 25.0f; p.arcDeg = 190.0f;
        p.detail = 0.28f; p.air = 0.72f; p.ground = AmbiHorizonGround::Hard;
        p.terrain = 0.72f; p.carry = 0.18f; p.turbulence = 0.24f;
        p.edgeDb = 0.5f; p.seed = 2129u;
        break;
    case 4u:
        p.ecology = AmbiHorizonEcology::Industrial;
        p.entities = 24u; p.activity = 0.52f; p.occupancy = 0.46f;
        p.pace = 0.36f; p.memory = 0.84f; p.cascade = 0.64f;
        p.signals = 0.76f; p.horizonBed = 0.56f; p.localFloor = 0.10f;
        p.rangeKm = 6.8f; p.azimuthDeg = -32.0f; p.arcDeg = 120.0f;
        p.detail = 0.52f; p.air = 0.54f; p.ground = AmbiHorizonGround::Hard;
        p.terrain = 0.36f; p.carry = 0.42f; p.turbulence = 0.13f;
        p.edgeDb = 1.5f; p.seed = 8011u;
        break;
    case 5u:
        p.ecology = AmbiHorizonEcology::Water;
        p.entities = 22u; p.activity = 0.38f; p.occupancy = 0.36f;
        p.pace = 0.28f; p.memory = 0.88f; p.cascade = 0.34f;
        p.signals = 0.62f; p.horizonBed = 0.42f; p.localFloor = 0.12f;
        p.rangeKm = 9.0f; p.arcDeg = 220.0f; p.elevationDeg = -1.0f;
        p.detail = 0.44f; p.air = 0.42f; p.ground = AmbiHorizonGround::Water;
        p.terrain = 0.10f; p.carry = 0.72f; p.turbulence = 0.06f;
        p.edgeDb = 2.5f; p.seed = 6277u;
        break;
    case 6u:
        p.ecology = AmbiHorizonEcology::Rural;
        p.entities = 30u; p.activity = 0.50f; p.occupancy = 0.44f;
        p.pace = 0.33f; p.memory = 0.76f; p.cascade = 0.52f;
        p.signals = 0.68f; p.horizonBed = 0.38f; p.localFloor = 0.14f;
        p.rangeKm = 3.8f; p.arcDeg = 330.0f; p.elevationDeg = 3.0f;
        p.detail = 0.68f; p.air = 0.46f; p.ground = AmbiHorizonGround::Grass;
        p.terrain = 0.28f; p.carry = 0.20f; p.turbulence = 0.16f;
        p.edgeDb = 0.5f; p.seed = 1483u;
        break;
    case 7u:
        p.ecology = AmbiHorizonEcology::Weather;
        p.entities = 32u; p.activity = 0.72f; p.occupancy = 0.74f;
        p.pace = 0.66f; p.memory = 0.74f; p.cascade = 0.82f;
        p.signals = 0.34f; p.horizonBed = 0.88f; p.localFloor = 0.28f;
        p.rangeKm = 6.0f; p.arcDeg = 280.0f; p.elevationDeg = 5.0f;
        p.detail = 0.22f; p.air = 0.80f; p.ground = AmbiHorizonGround::Forest;
        p.terrain = 0.68f; p.carry = -0.12f; p.turbulence = 0.76f;
        p.edgeDb = 1.0f; p.seed = 9901u;
        break;
    case 8u:
        p.ecology = AmbiHorizonEcology::Traffic;
        p.entities = 20u; p.activity = 0.42f; p.occupancy = 0.26f;
        p.pace = 0.22f; p.memory = 0.90f; p.cascade = 0.70f;
        p.signals = 0.84f; p.horizonBed = 0.38f; p.localFloor = 0.08f;
        p.rangeKm = 8.2f; p.azimuthDeg = 42.0f; p.arcDeg = 72.0f;
        p.detail = 0.50f; p.air = 0.64f; p.ground = AmbiHorizonGround::Hard;
        p.terrain = 0.34f; p.carry = 0.48f; p.turbulence = 0.12f;
        p.edgeDb = 2.5f; p.seed = 5519u;
        break;
    case 9u:
        p.ecology = AmbiHorizonEcology::Rural;
        p.entities = 16u; p.activity = 0.20f; p.occupancy = 0.14f;
        p.pace = 0.12f; p.memory = 0.94f; p.cascade = 0.18f;
        p.signals = 0.60f; p.horizonBed = 0.20f; p.localFloor = 0.04f;
        p.rangeKm = 5.6f; p.arcDeg = 260.0f; p.detail = 0.58f;
        p.air = 0.38f; p.ground = AmbiHorizonGround::Grass;
        p.terrain = 0.20f; p.carry = 0.54f; p.turbulence = 0.04f;
        p.edgeDb = 3.0f; p.seed = 733u;
        break;
    case 10u:
        p.ecology = AmbiHorizonEcology::City;
        p.entities = 26u; p.activity = 0.46f; p.occupancy = 0.56f;
        p.pace = 0.38f; p.memory = 0.80f; p.cascade = 0.46f;
        p.signals = 0.44f; p.horizonBed = 0.66f; p.localFloor = 0.10f;
        p.rangeKm = 3.2f; p.azimuthDeg = -28.0f; p.arcDeg = 170.0f;
        p.detail = 0.24f; p.air = 0.74f; p.ground = AmbiHorizonGround::Forest;
        p.terrain = 0.78f; p.carry = -0.18f; p.turbulence = 0.18f;
        p.edgeDb = 1.0f; p.seed = 6311u;
        break;
    default:
        p.ecology = AmbiHorizonEcology::Mixed;
        p.entities = 18u; p.activity = 0.18f; p.occupancy = 0.12f;
        p.pace = 0.10f; p.memory = 0.96f; p.cascade = 0.16f;
        p.signals = 0.48f; p.horizonBed = 0.18f; p.localFloor = 0.03f;
        p.rangeKm = 16.0f; p.arcDeg = 360.0f; p.detail = 0.12f;
        p.air = 0.86f; p.ground = AmbiHorizonGround::Mixed;
        p.terrain = 0.48f; p.carry = 0.66f; p.turbulence = 0.08f;
        p.edgeDb = 4.5f; p.seed = 12211u;
        break;
    }
    return p;
}

} // namespace s3g
