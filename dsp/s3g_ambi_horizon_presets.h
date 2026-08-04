#pragma once

#include "s3g_ambi_horizon_encoder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

struct AmbiHorizonPresetInfo {
    const char* name = "";
    const char* description = "";
};

inline constexpr std::array<AmbiHorizonPresetInfo, 16u> kAmbiHorizonPresetInfo {{
    { "BELL ACROSS VALLEY", "Sparse modal signals carried over a quiet rural basin." },
    { "CLEAR NIGHT INVERSION", "Long atmospheric reach with a low local noise floor." },
    { "HIGHWAY BEYOND FIELDS", "A continuous traffic band behind open agricultural ground." },
    { "CITY BEYOND RIDGE", "Diffuse urban activity softened by intervening terrain." },
    { "INDUSTRIAL NIGHT REACH", "Continuous tonal machinery held at the distant horizon." },
    { "ACROSS STILL WATER", "Distant signals with strong low-frequency surface carry." },
    { "DAWN LONG HORIZON", "Air, modal calls, and a wide gradually waking field." },
    { "STORM BEYOND HILLS", "Broad turbulent weather energy obscured by terrain." },
    { "DISTANT RAIL CORRIDOR", "Intermittent motor and traffic bodies crossing a narrow arc." },
    { "QUIET AGRICULTURAL BASIN", "Sparse rural events inside an exceptionally quiet floor." },
    { "SETTLEMENT THROUGH FOREST", "A small city bed strongly filtered by forest ground." },
    { "VANISHING ACOUSTIC HORIZON", "Extreme distance with only residual form and audibility." },
    { "HIGH FIDELITY HORIZON", "Recorder-free environmental depth with synthesized air noise removed." },
    { "AIRPORT APPROACH CORRIDOR", "Slow overhead turbine passes from an airport several kilometers away." },
    { "FOGHORNS BEYOND HEADLAND", "Long low marine calls emerging across a quiet coastal horizon." },
    { "OPEN OCEAN BEYOND DUNES", "Overlapping distant surf cycles with almost no anthropogenic sources." },
}};

inline constexpr uint32_t kAmbiHorizonFactoryPresetCount =
    static_cast<uint32_t>(kAmbiHorizonPresetInfo.size());

inline uint32_t ambiHorizonRandomWord(uint32_t& state)
{
    if (state == 0u) state = 0x6d2b79f5u;
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return state;
}

inline float ambiHorizonRandomUnit(uint32_t& state)
{
    return static_cast<float>(ambiHorizonRandomWord(state) & 0x00ffffffu)
        / static_cast<float>(0x01000000u);
}

inline float ambiHorizonRandomRange(
    uint32_t& state, float minimum, float maximum)
{
    return minimum + (maximum - minimum) * ambiHorizonRandomUnit(state);
}

inline uint32_t ambiHorizonRandomChoice(uint32_t& state, uint32_t count)
{
    return std::min<uint32_t>(count - 1u,
        static_cast<uint32_t>(ambiHorizonRandomUnit(state)
            * static_cast<float>(count)));
}

inline float ambiHorizonRandomLogRange(
    uint32_t& state, float minimum, float maximum)
{
    return std::exp(ambiHorizonRandomRange(
        state, std::log(minimum), std::log(maximum)));
}

// Generate a complete scene inside musically conservative limits. Unlike a
// perturbed factory preset, every ecology gets a compatible source population,
// ground, propagation range, and model controls. Monitoring and listener
// choices remain user-owned.
inline AmbiHorizonEncoderParams ambiHorizonSafeRandomParams(
    const AmbiHorizonEncoderParams& current, uint32_t& state)
{
    AmbiHorizonEncoderParams p {};
    p.order = current.order;
    p.outputGainDb = current.outputGainDb;
    p.fieldListenMode = current.fieldListenMode;
    p.fieldListenAmount = current.fieldListenAmount;
    p.fieldListenResponse = current.fieldListenResponse;

    p.ecology = static_cast<AmbiHorizonEcology>(
        ambiHorizonRandomChoice(state, 9u));
    p.entities = 16u + ambiHorizonRandomChoice(state, 17u);
    p.activity = ambiHorizonRandomRange(state, 0.34f, 0.68f);
    p.occupancy = ambiHorizonRandomRange(state, 0.32f, 0.70f);
    p.pace = ambiHorizonRandomRange(state, 0.10f, 0.58f);
    p.memory = ambiHorizonRandomRange(state, 0.64f, 0.96f);
    p.cascade = ambiHorizonRandomRange(state, 0.16f, 0.72f);
    p.signals = ambiHorizonRandomRange(state, 0.42f, 0.82f);
    p.horizonBed = ambiHorizonRandomRange(state, 0.38f, 0.62f);
    p.localFloor = ambiHorizonRandomRange(state, 0.11f, 0.21f);
    p.azimuthDeg = ambiHorizonRandomRange(state, -180.0f, 180.0f);
    p.arcDeg = ambiHorizonRandomRange(state, 90.0f, 330.0f);
    p.detail = ambiHorizonRandomRange(state, 0.30f, 0.72f);
    p.air = ambiHorizonRandomRange(state, 0.28f, 0.72f);
    p.terrain = ambiHorizonRandomRange(state, 0.14f, 0.68f);
    p.carry = ambiHorizonRandomRange(state, 0.02f, 0.56f);
    p.turbulence = ambiHorizonRandomRange(state, 0.04f, 0.28f);
    p.edgeDb = ambiHorizonRandomRange(state, 2.5f, 5.5f);
    p.airNoise = ambiHorizonRandomUnit(state) < 0.22f
        ? 0.0f : ambiHorizonRandomRange(state, 0.01f, 0.14f);

    p.machines = 0.0f;
    p.bells = 0.0f;
    p.traffic = 0.0f;
    p.aircraft = 0.0f;
    p.foghorns = 0.0f;
    p.surf = 0.0f;

    p.trafficSpeed = ambiHorizonRandomRange(state, 0.20f, 0.72f);
    p.engineLoad = ambiHorizonRandomRange(state, 0.28f, 0.78f);
    p.aircraftFlight = ambiHorizonRandomRange(state, 0.52f, 0.96f);
    p.aircraftSpeed = ambiHorizonRandomRange(state, 0.28f, 0.74f);
    p.aircraftPower = ambiHorizonRandomRange(state, 0.42f, 0.84f);
    p.aircraftTone = ambiHorizonRandomRange(state, 0.20f, 0.58f);
    p.foghornPitch = ambiHorizonRandomRange(state, 0.24f, 0.56f);
    p.foghornPressure = ambiHorizonRandomRange(state, 0.54f, 0.84f);
    p.foghornLength = ambiHorizonRandomRange(state, 0.38f, 0.72f);
    p.waveRate = ambiHorizonRandomRange(state, 0.24f, 0.68f);
    p.waveBreak = ambiHorizonRandomRange(state, 0.30f, 0.76f);
    p.machineTone = ambiHorizonRandomRange(state, 0.24f, 0.68f);
    p.bellPitch = ambiHorizonRandomRange(state, 0.32f, 0.72f);
    p.bellDecay = ambiHorizonRandomRange(state, 0.56f, 0.92f);

    switch (p.ecology) {
    case AmbiHorizonEcology::Rural:
        p.ground = ambiHorizonRandomUnit(state) < 0.78f
            ? AmbiHorizonGround::Grass : AmbiHorizonGround::Forest;
        p.rangeKm = ambiHorizonRandomLogRange(state, 1.2f, 11.0f);
        p.elevationDeg = ambiHorizonRandomRange(state, -4.0f, 7.0f);
        p.machines = ambiHorizonRandomRange(state, 0.0f, 0.16f);
        p.bells = ambiHorizonRandomRange(state, 0.38f, 0.86f);
        p.aircraft = ambiHorizonRandomRange(state, 0.0f, 0.16f);
        break;
    case AmbiHorizonEcology::Traffic:
        p.ground = ambiHorizonRandomUnit(state) < 0.62f
            ? AmbiHorizonGround::Grass : AmbiHorizonGround::Hard;
        p.rangeKm = ambiHorizonRandomLogRange(state, 0.8f, 8.0f);
        p.elevationDeg = ambiHorizonRandomRange(state, -5.0f, 5.0f);
        p.machines = ambiHorizonRandomRange(state, 0.04f, 0.28f);
        p.traffic = ambiHorizonRandomRange(state, 0.58f, 0.96f);
        p.aircraft = ambiHorizonRandomRange(state, 0.0f, 0.14f);
        break;
    case AmbiHorizonEcology::City:
        p.ground = ambiHorizonRandomUnit(state) < 0.72f
            ? AmbiHorizonGround::Hard : AmbiHorizonGround::Forest;
        p.rangeKm = ambiHorizonRandomLogRange(state, 1.0f, 9.0f);
        p.elevationDeg = ambiHorizonRandomRange(state, -4.0f, 7.0f);
        p.machines = ambiHorizonRandomRange(state, 0.12f, 0.42f);
        p.traffic = ambiHorizonRandomRange(state, 0.46f, 0.88f);
        p.aircraft = ambiHorizonRandomRange(state, 0.02f, 0.18f);
        break;
    case AmbiHorizonEcology::Industrial:
        p.ground = AmbiHorizonGround::Hard;
        p.rangeKm = ambiHorizonRandomLogRange(state, 1.4f, 9.0f);
        p.elevationDeg = ambiHorizonRandomRange(state, -5.0f, 5.0f);
        p.machines = ambiHorizonRandomRange(state, 0.46f, 0.88f);
        p.bells = ambiHorizonRandomRange(state, 0.0f, 0.12f);
        p.traffic = ambiHorizonRandomRange(state, 0.18f, 0.52f);
        p.airNoise = std::min(p.airNoise, 0.08f);
        break;
    case AmbiHorizonEcology::Water:
        p.ground = AmbiHorizonGround::Water;
        p.rangeKm = ambiHorizonRandomLogRange(state, 2.0f, 13.0f);
        p.elevationDeg = ambiHorizonRandomRange(state, -4.0f, 1.0f);
        p.carry = ambiHorizonRandomRange(state, 0.34f, 0.76f);
        p.foghorns = ambiHorizonRandomRange(state, 0.32f, 0.78f);
        p.surf = ambiHorizonRandomRange(state, 0.32f, 0.82f);
        break;
    case AmbiHorizonEcology::Weather:
        p.ground = static_cast<AmbiHorizonGround>(
            2u + ambiHorizonRandomChoice(state, 3u));
        p.rangeKm = ambiHorizonRandomLogRange(state, 1.0f, 11.0f);
        p.elevationDeg = ambiHorizonRandomRange(state, -2.0f, 10.0f);
        p.carry = ambiHorizonRandomRange(state, -0.18f, 0.34f);
        p.turbulence = ambiHorizonRandomRange(state, 0.22f, 0.62f);
        p.horizonBed = std::max(p.horizonBed, 0.48f);
        p.localFloor = ambiHorizonRandomRange(state, 0.14f, 0.24f);
        p.edgeDb = std::max(p.edgeDb, 3.5f);
        p.airNoise = ambiHorizonRandomRange(state, 0.10f, 0.22f);
        p.machines = ambiHorizonRandomRange(state, 0.0f, 0.12f);
        p.aircraft = ambiHorizonRandomRange(state, 0.04f, 0.24f);
        p.surf = ambiHorizonRandomRange(state, 0.58f, 0.92f);
        break;
    case AmbiHorizonEcology::Airport:
        p.ground = ambiHorizonRandomUnit(state) < 0.56f
            ? AmbiHorizonGround::Hard : AmbiHorizonGround::Grass;
        p.rangeKm = ambiHorizonRandomLogRange(state, 1.4f, 10.0f);
        p.elevationDeg = ambiHorizonRandomRange(state, 3.0f, 13.0f);
        p.traffic = ambiHorizonRandomRange(state, 0.16f, 0.54f);
        p.aircraft = ambiHorizonRandomRange(state, 0.68f, 1.0f);
        break;
    case AmbiHorizonEcology::Coast:
        p.ground = AmbiHorizonGround::Water;
        p.rangeKm = ambiHorizonRandomLogRange(state, 2.5f, 14.0f);
        p.elevationDeg = ambiHorizonRandomRange(state, -5.0f, 0.0f);
        p.carry = ambiHorizonRandomRange(state, 0.34f, 0.74f);
        p.foghorns = ambiHorizonRandomRange(state, 0.46f, 0.94f);
        p.surf = ambiHorizonRandomRange(state, 0.38f, 0.86f);
        break;
    default:
        p.ground = static_cast<AmbiHorizonGround>(
            ambiHorizonRandomChoice(state, 5u));
        p.rangeKm = ambiHorizonRandomLogRange(state, 0.9f, 11.0f);
        p.elevationDeg = ambiHorizonRandomRange(state, -5.0f, 8.0f);
        p.machines = ambiHorizonRandomRange(state, 0.12f, 0.44f);
        p.bells = ambiHorizonRandomRange(state, 0.12f, 0.48f);
        p.traffic = ambiHorizonRandomRange(state, 0.26f, 0.68f);
        p.aircraft = ambiHorizonRandomRange(state, 0.04f, 0.22f);
        break;
    }

    p.seed = 1u + ambiHorizonRandomChoice(state, 65535u);
    if (p.seed == current.seed) p.seed = p.seed == 65535u ? 1u : p.seed + 1u;
    return p;
}

inline AmbiHorizonEncoderParams ambiHorizonFactoryPreset(uint32_t index)
{
    AmbiHorizonEncoderParams p {};
    switch (index % kAmbiHorizonFactoryPresetCount) {
    case 0u:
        p.ecology = AmbiHorizonEcology::Rural;
        p.entities = 18u; p.activity = 0.34f; p.occupancy = 0.18f;
        p.pace = 0.24f; p.memory = 0.86f; p.cascade = 0.28f;
        p.signals = 0.68f; p.horizonBed = 0.68f; p.localFloor = 0.12f;
        p.rangeKm = 4.8f; p.arcDeg = 105.0f; p.detail = 0.64f;
        p.air = 0.48f; p.ground = AmbiHorizonGround::Grass;
        p.terrain = 0.54f; p.carry = 0.34f; p.turbulence = 0.10f;
        p.edgeDb = 3.25f; p.seed = 311u; p.airNoise = 0.12f;
        p.machines = 0.0f; p.bells = 0.82f; p.traffic = 0.0f;
        p.aircraft = 0.0f; p.foghorns = 0.0f; p.surf = 0.0f;
        p.bellPitch = 0.58f; p.bellDecay = 0.90f;
        break;
    case 1u:
        p.ecology = AmbiHorizonEcology::Mixed;
        p.entities = 20u; p.activity = 0.32f; p.occupancy = 0.32f;
        p.pace = 0.17f; p.memory = 0.91f; p.cascade = 0.22f;
        p.signals = 0.68f; p.horizonBed = 0.30f; p.localFloor = 0.08f;
        p.rangeKm = 7.5f; p.arcDeg = 300.0f; p.detail = 0.42f;
        p.air = 0.34f; p.ground = AmbiHorizonGround::Grass;
        p.terrain = 0.22f; p.carry = 0.68f; p.turbulence = 0.05f;
        p.edgeDb = 3.5f; p.seed = 907u; p.airNoise = 0.08f;
        p.machines = 0.16f; p.bells = 0.28f; p.traffic = 0.28f;
        p.aircraft = 0.10f; p.foghorns = 0.0f; p.surf = 0.0f;
        p.engineLoad = 0.38f; p.aircraftFlight = 0.76f;
        break;
    case 2u:
        p.ecology = AmbiHorizonEcology::Traffic;
        p.entities = 28u; p.activity = 0.54f; p.occupancy = 0.82f;
        p.pace = 0.46f; p.memory = 0.72f; p.cascade = 0.44f;
        p.signals = 0.38f; p.horizonBed = 0.58f; p.localFloor = 0.13f;
        p.rangeKm = 2.6f; p.azimuthDeg = -18.0f; p.arcDeg = 150.0f;
        p.detail = 0.35f; p.air = 0.62f; p.ground = AmbiHorizonGround::Grass;
        p.terrain = 0.30f; p.carry = 0.08f; p.turbulence = 0.18f;
        p.edgeDb = -1.0f; p.seed = 4051u; p.airNoise = 0.18f;
        p.machines = 0.12f; p.bells = 0.0f; p.traffic = 1.0f;
        p.aircraft = 0.05f; p.foghorns = 0.0f; p.surf = 0.0f;
        p.trafficSpeed = 0.66f; p.engineLoad = 0.52f;
        p.aircraftFlight = 0.70f; p.aircraftSpeed = 0.58f;
        break;
    case 3u:
        p.ecology = AmbiHorizonEcology::City;
        p.entities = 32u; p.activity = 0.58f; p.occupancy = 0.70f;
        p.pace = 0.52f; p.memory = 0.62f; p.cascade = 0.56f;
        p.signals = 0.44f; p.horizonBed = 0.48f; p.localFloor = 0.16f;
        p.rangeKm = 5.2f; p.azimuthDeg = 25.0f; p.arcDeg = 190.0f;
        p.detail = 0.28f; p.air = 0.72f; p.ground = AmbiHorizonGround::Hard;
        p.terrain = 0.72f; p.carry = 0.18f; p.turbulence = 0.24f;
        p.edgeDb = 0.5f; p.seed = 2129u; p.airNoise = 0.16f;
        p.machines = 0.34f; p.bells = 0.0f; p.traffic = 0.82f;
        p.aircraft = 0.12f; p.foghorns = 0.0f; p.surf = 0.0f;
        p.trafficSpeed = 0.48f; p.engineLoad = 0.64f;
        p.aircraftFlight = 0.62f; p.aircraftSpeed = 0.48f;
        break;
    case 4u:
        p.ecology = AmbiHorizonEcology::Industrial;
        p.entities = 24u; p.activity = 0.52f; p.occupancy = 0.46f;
        p.pace = 0.36f; p.memory = 0.84f; p.cascade = 0.64f;
        p.signals = 0.76f; p.horizonBed = 0.34f; p.localFloor = 0.10f;
        p.rangeKm = 6.8f; p.azimuthDeg = -32.0f; p.arcDeg = 120.0f;
        p.detail = 0.52f; p.air = 0.54f; p.ground = AmbiHorizonGround::Hard;
        p.terrain = 0.36f; p.carry = 0.42f; p.turbulence = 0.13f;
        p.edgeDb = 1.5f; p.seed = 8011u; p.airNoise = 0.10f;
        p.machines = 1.0f; p.bells = 0.08f; p.traffic = 0.28f;
        p.aircraft = 0.0f; p.foghorns = 0.0f; p.surf = 0.0f;
        p.machineTone = 0.36f; p.trafficSpeed = 0.24f;
        p.engineLoad = 0.82f; p.bellPitch = 0.34f; p.bellDecay = 0.74f;
        break;
    case 5u:
        p.ecology = AmbiHorizonEcology::Water;
        p.entities = 22u; p.activity = 0.38f; p.occupancy = 0.36f;
        p.pace = 0.28f; p.memory = 0.88f; p.cascade = 0.34f;
        p.signals = 0.62f; p.horizonBed = 0.22f; p.localFloor = 0.12f;
        p.rangeKm = 9.0f; p.arcDeg = 220.0f; p.elevationDeg = -1.0f;
        p.detail = 0.44f; p.air = 0.42f; p.ground = AmbiHorizonGround::Water;
        p.terrain = 0.10f; p.carry = 0.72f; p.turbulence = 0.06f;
        p.edgeDb = 2.5f; p.seed = 6277u; p.airNoise = 0.12f;
        p.machines = 0.0f; p.bells = 0.0f; p.traffic = 0.0f;
        p.aircraft = 0.0f; p.foghorns = 0.72f; p.surf = 0.42f;
        p.foghornPitch = 0.34f; p.foghornPressure = 0.86f;
        p.foghornLength = 0.62f; p.waveRate = 0.38f; p.waveBreak = 0.44f;
        break;
    case 6u:
        p.ecology = AmbiHorizonEcology::Rural;
        p.entities = 30u; p.activity = 0.54f; p.occupancy = 0.48f;
        p.pace = 0.33f; p.memory = 0.76f; p.cascade = 0.52f;
        p.signals = 0.75f; p.horizonBed = 0.34f; p.localFloor = 0.20f;
        p.rangeKm = 3.8f; p.arcDeg = 330.0f; p.elevationDeg = 3.0f;
        p.detail = 0.68f; p.air = 0.46f; p.ground = AmbiHorizonGround::Grass;
        p.terrain = 0.28f; p.carry = 0.20f; p.turbulence = 0.16f;
        p.edgeDb = 2.0f; p.seed = 1483u; p.airNoise = 0.20f;
        p.machines = 0.04f; p.bells = 0.68f; p.traffic = 0.0f;
        p.aircraft = 0.10f; p.foghorns = 0.0f; p.surf = 0.0f;
        p.bellPitch = 0.64f; p.bellDecay = 0.72f;
        p.aircraftFlight = 0.72f;
        break;
    case 7u:
        p.ecology = AmbiHorizonEcology::Weather;
        p.entities = 32u; p.activity = 0.72f; p.occupancy = 0.74f;
        p.pace = 0.66f; p.memory = 0.74f; p.cascade = 0.82f;
        p.signals = 0.34f; p.horizonBed = 0.58f; p.localFloor = 0.28f;
        p.rangeKm = 6.0f; p.arcDeg = 280.0f; p.elevationDeg = 5.0f;
        p.detail = 0.22f; p.air = 0.80f; p.ground = AmbiHorizonGround::Forest;
        p.terrain = 0.68f; p.carry = -0.12f; p.turbulence = 0.76f;
        p.edgeDb = 1.0f; p.seed = 9901u; p.airNoise = 0.55f;
        p.machines = 0.0f; p.bells = 0.0f; p.traffic = 0.0f;
        p.aircraft = 0.0f; p.foghorns = 0.0f; p.surf = 0.28f;
        p.waveRate = 0.72f; p.waveBreak = 0.84f;
        break;
    case 8u:
        p.ecology = AmbiHorizonEcology::Traffic;
        p.entities = 20u; p.activity = 0.42f; p.occupancy = 0.26f;
        p.pace = 0.22f; p.memory = 0.90f; p.cascade = 0.70f;
        p.signals = 0.84f; p.horizonBed = 0.22f; p.localFloor = 0.08f;
        p.rangeKm = 8.2f; p.azimuthDeg = 42.0f; p.arcDeg = 72.0f;
        p.detail = 0.50f; p.air = 0.64f; p.ground = AmbiHorizonGround::Hard;
        p.terrain = 0.34f; p.carry = 0.48f; p.turbulence = 0.12f;
        p.edgeDb = 2.5f; p.seed = 5519u; p.airNoise = 0.10f;
        p.machines = 0.92f; p.bells = 0.0f; p.traffic = 0.22f;
        p.aircraft = 0.0f; p.foghorns = 0.0f; p.surf = 0.0f;
        p.machineTone = 0.28f; p.trafficSpeed = 0.30f;
        p.engineLoad = 0.78f;
        break;
    case 9u:
        p.ecology = AmbiHorizonEcology::Rural;
        p.entities = 16u; p.activity = 0.32f; p.occupancy = 0.28f;
        p.pace = 0.12f; p.memory = 0.94f; p.cascade = 0.18f;
        p.signals = 0.78f; p.horizonBed = 0.42f; p.localFloor = 0.12f;
        p.rangeKm = 5.6f; p.arcDeg = 260.0f; p.detail = 0.58f;
        p.air = 0.38f; p.ground = AmbiHorizonGround::Grass;
        p.terrain = 0.20f; p.carry = 0.54f; p.turbulence = 0.04f;
        p.edgeDb = 8.0f; p.seed = 733u; p.airNoise = 0.02f;
        p.machines = 0.0f; p.bells = 0.42f; p.traffic = 0.0f;
        p.aircraft = 0.06f; p.foghorns = 0.0f; p.surf = 0.0f;
        p.bellPitch = 0.48f; p.bellDecay = 0.86f;
        break;
    case 10u:
        p.ecology = AmbiHorizonEcology::City;
        p.entities = 26u; p.activity = 0.46f; p.occupancy = 0.56f;
        p.pace = 0.38f; p.memory = 0.80f; p.cascade = 0.46f;
        p.signals = 0.44f; p.horizonBed = 0.40f; p.localFloor = 0.10f;
        p.rangeKm = 3.2f; p.azimuthDeg = -28.0f; p.arcDeg = 170.0f;
        p.detail = 0.24f; p.air = 0.74f; p.ground = AmbiHorizonGround::Forest;
        p.terrain = 0.78f; p.carry = -0.18f; p.turbulence = 0.18f;
        p.edgeDb = 5.0f; p.seed = 6311u; p.airNoise = 0.06f;
        p.machines = 0.14f; p.bells = 0.0f; p.traffic = 0.42f;
        p.aircraft = 0.03f; p.foghorns = 0.0f; p.surf = 0.0f;
        p.trafficSpeed = 0.34f; p.engineLoad = 0.46f;
        break;
    case 11u:
        p.ecology = AmbiHorizonEcology::Mixed;
        p.entities = 18u; p.activity = 0.24f; p.occupancy = 0.20f;
        p.pace = 0.10f; p.memory = 0.96f; p.cascade = 0.16f;
        p.signals = 0.62f; p.horizonBed = 0.28f; p.localFloor = 0.07f;
        p.rangeKm = 16.0f; p.arcDeg = 360.0f; p.detail = 0.12f;
        p.air = 0.86f; p.ground = AmbiHorizonGround::Mixed;
        p.terrain = 0.48f; p.carry = 0.66f; p.turbulence = 0.08f;
        p.edgeDb = 6.5f; p.seed = 12211u; p.airNoise = 0.04f;
        p.machines = 0.12f; p.bells = 0.14f; p.traffic = 0.24f;
        p.aircraft = 0.06f; p.foghorns = 0.0f; p.surf = 0.0f;
        break;
    case 12u:
        p.ecology = AmbiHorizonEcology::Mixed;
        p.entities = 24u; p.activity = 0.42f; p.occupancy = 0.40f;
        p.pace = 0.28f; p.memory = 0.84f; p.cascade = 0.38f;
        p.signals = 0.72f; p.horizonBed = 0.24f; p.localFloor = 0.08f;
        p.rangeKm = 6.4f; p.arcDeg = 250.0f; p.detail = 0.76f;
        p.air = 0.30f; p.ground = AmbiHorizonGround::Mixed;
        p.terrain = 0.18f; p.carry = 0.36f; p.turbulence = 0.07f;
        p.edgeDb = 1.5f; p.seed = 2767u; p.airNoise = 0.0f;
        p.machines = 0.22f; p.bells = 0.16f; p.traffic = 0.34f;
        p.aircraft = 0.07f; p.foghorns = 0.0f; p.surf = 0.0f;
        p.trafficSpeed = 0.46f; p.engineLoad = 0.44f;
        p.aircraftFlight = 0.76f; p.foghornPressure = 0.82f;
        break;
    case 13u:
        p.ecology = AmbiHorizonEcology::Airport;
        p.entities = 28u; p.activity = 0.46f; p.occupancy = 0.34f;
        p.pace = 0.24f; p.memory = 0.90f; p.cascade = 0.26f;
        p.signals = 0.82f; p.horizonBed = 0.20f; p.localFloor = 0.06f;
        p.rangeKm = 3.6f; p.azimuthDeg = -12.0f; p.arcDeg = 150.0f;
        p.elevationDeg = 7.0f; p.detail = 0.62f; p.air = 0.48f;
        p.ground = AmbiHorizonGround::Grass; p.terrain = 0.16f;
        p.carry = 0.22f; p.turbulence = 0.12f; p.edgeDb = 1.5f;
        p.seed = 4517u; p.airNoise = 0.06f;
        p.machines = 0.0f; p.bells = 0.0f; p.traffic = 0.10f;
        p.aircraft = 1.0f; p.foghorns = 0.0f; p.surf = 0.0f;
        p.trafficSpeed = 0.20f; p.engineLoad = 0.34f;
        p.aircraftFlight = 0.90f; p.aircraftSpeed = 0.64f;
        p.aircraftPower = 0.78f; p.aircraftTone = 0.38f;
        break;
    case 14u:
        p.ecology = AmbiHorizonEcology::Coast;
        p.entities = 20u; p.activity = 0.28f; p.occupancy = 0.24f;
        p.pace = 0.12f; p.memory = 0.96f; p.cascade = 0.18f;
        p.signals = 0.82f; p.horizonBed = 0.22f; p.localFloor = 0.04f;
        p.rangeKm = 9.8f; p.azimuthDeg = 18.0f; p.arcDeg = 160.0f;
        p.elevationDeg = -2.0f; p.detail = 0.34f; p.air = 0.48f;
        p.ground = AmbiHorizonGround::Water; p.terrain = 0.26f;
        p.carry = 0.72f; p.turbulence = 0.05f; p.edgeDb = 1.5f;
        p.seed = 10103u; p.airNoise = 0.02f;
        p.machines = 0.0f; p.bells = 0.0f; p.traffic = 0.0f;
        p.aircraft = 0.0f; p.foghorns = 1.0f; p.surf = 0.26f;
        p.foghornPitch = 0.36f; p.foghornPressure = 0.84f;
        p.foghornLength = 0.58f; p.waveRate = 0.30f; p.waveBreak = 0.38f;
        break;
    default:
        p.ecology = AmbiHorizonEcology::Coast;
        p.entities = 30u; p.activity = 0.52f; p.occupancy = 0.72f;
        p.pace = 0.34f; p.memory = 0.88f; p.cascade = 0.42f;
        p.signals = 0.18f; p.horizonBed = 0.62f; p.localFloor = 0.08f;
        p.rangeKm = 4.6f; p.azimuthDeg = -8.0f; p.arcDeg = 250.0f;
        p.elevationDeg = -3.0f; p.detail = 0.32f; p.air = 0.52f;
        p.ground = AmbiHorizonGround::Water; p.terrain = 0.10f;
        p.carry = 0.48f; p.turbulence = 0.16f; p.edgeDb = 1.0f;
        p.seed = 8821u; p.airNoise = 0.02f;
        p.machines = 0.0f; p.bells = 0.0f; p.traffic = 0.0f;
        p.aircraft = 0.0f; p.foghorns = 0.10f; p.surf = 1.0f;
        p.foghornPitch = 0.32f; p.foghornPressure = 0.82f;
        p.foghornLength = 0.46f; p.waveRate = 0.56f; p.waveBreak = 0.72f;
        break;
    }
    return p;
}

} // namespace s3g
