#pragma once

#include "s3g_ambi_wind_encoder.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace s3g {

struct AmbiWindPresetInfo {
    const char* name;
    const char* description;
};

struct AmbiWindPresetValues {
    uint32_t order;
    uint32_t voices;
    float wind;
    float gustRate;
    float gustDepth;
    float turbulence;
    float flutter;
    float material;
    float air;
    float hiss;
    float spread;
    float deviation;
    uint32_t gustShape;
    float vectorRateHz;
    uint32_t materialMode;
    uint32_t gustEdge;
    float center;
    float sweep;
    float q;
    float shrill;
    float body;
    float breath;
    float grit;
    float field;
    float motionRateHz;
    float flow;
    float shear;
    float curl;
    float updraft;
    float centerAzimuthDeg;
    float centerElevationDeg;
    float centerDistance;
    float spatialFollow;
    float outputGainDb;
};

struct AmbiWindPreset {
    AmbiWindPresetInfo info;
    AmbiWindPresetValues values;
};

inline constexpr uint32_t kAmbiWindFactoryPresetCount = 22;

inline constexpr std::array<AmbiWindPreset, kAmbiWindFactoryPresetCount> kAmbiWindPresets {{
    { { "PAIA Bench Wind", "A quiet filtered-noise reference patch." }, { 3, 20, 0.42f, 0.18f, 0.36f, 0.22f, 0.16f, 0.22f, 0.34f, 0.24f, 0.28f, 0.08f, 1, 1, 0, 0, 0.34f, 0.42f, 0.34f, 0.12f, 0.58f, 0.52f, 0.06f, 0.46f, 0.018f, 0.18f, 0.16f, 0.18f, 0.02f, 0.0f, 0.0f, 1.00f, 0.94f, -12.0f } },
    { { "Low Pressure Front", "Slow dense pressure swells with low body." }, { 3, 36, 0.62f, 0.09f, 0.72f, 0.34f, 0.12f, 0.30f, 0.24f, 0.20f, 0.46f, 0.12f, 2, 0, 0, 1, 0.22f, 0.68f, 0.42f, 0.14f, 0.78f, 0.78f, 0.08f, 0.62f, 0.011f, 0.28f, 0.32f, 0.24f, 0.02f, -8.0f, 0.0f, 1.18f, 0.97f, -13.0f } },
    { { "Pine Needle Hiss", "Fine high air and fast small eddies." }, { 3, 32, 0.44f, 0.34f, 0.30f, 0.58f, 0.62f, 0.54f, 0.76f, 0.70f, 0.34f, 0.18f, 5, 1, 1, 0, 0.66f, 0.34f, 0.48f, 0.58f, 0.26f, 0.30f, 0.14f, 0.52f, 0.046f, 0.34f, 0.26f, 0.30f, 0.04f, 16.0f, 4.0f, 1.02f, 0.90f, -13.5f } },
    { { "Window Crack Whistle", "Narrow bright wind through a small aperture." }, { 3, 16, 0.36f, 0.24f, 0.28f, 0.20f, 0.18f, 0.82f, 0.48f, 0.58f, 0.14f, 0.08f, 0, 1, 3, 0, 0.76f, 0.30f, 0.78f, 0.68f, 0.18f, 0.22f, 0.10f, 0.34f, 0.020f, 0.14f, 0.08f, 0.12f, 0.00f, 0.0f, 6.0f, 0.92f, 0.98f, -15.0f } },
    { { "Canyon Rotor", "Large circular pressure cells in a wide field." }, { 4, 40, 0.58f, 0.16f, 0.58f, 0.44f, 0.26f, 0.34f, 0.28f, 0.30f, 0.58f, 0.16f, 3, 1, 0, 1, 0.30f, 0.62f, 0.46f, 0.22f, 0.76f, 0.58f, 0.12f, 0.76f, 0.030f, 0.50f, 0.42f, 0.46f, 0.08f, 0.0f, -6.0f, 1.20f, 0.88f, -13.0f } },
    { { "Tin Vent Flutter", "Metal vent flicker with hard air edges." }, { 3, 26, 0.48f, 0.46f, 0.42f, 0.64f, 0.78f, 0.74f, 0.44f, 0.56f, 0.24f, 0.16f, 4, 2, 4, 2, 0.56f, 0.50f, 0.62f, 0.44f, 0.30f, 0.22f, 0.22f, 0.50f, 0.066f, 0.36f, 0.30f, 0.28f, 0.05f, 22.0f, 0.0f, 1.06f, 0.84f, -14.0f } },
    { { "Distant Tree Line", "Soft massed voices with almost still motion." }, { 3, 48, 0.34f, 0.075f, 0.34f, 0.16f, 0.10f, 0.24f, 0.28f, 0.20f, 0.54f, 0.10f, 1, 0, 1, 0, 0.28f, 0.32f, 0.28f, 0.08f, 0.72f, 0.84f, 0.03f, 0.68f, 0.009f, 0.16f, 0.14f, 0.18f, 0.00f, 0.0f, 0.0f, 1.28f, 0.99f, -16.0f } },
    { { "Sand Across Metal", "Dry granular air over a reflective sheet." }, { 3, 30, 0.48f, 0.40f, 0.30f, 0.72f, 0.48f, 0.70f, 0.60f, 0.78f, 0.36f, 0.24f, 5, 1, 4, 2, 0.60f, 0.22f, 0.46f, 0.54f, 0.24f, 0.18f, 0.34f, 0.70f, 0.070f, 0.46f, 0.28f, 0.36f, 0.06f, -20.0f, 0.0f, 1.10f, 0.86f, -15.0f } },
    { { "Harbor Rigging", "Wire-like whistles riding open air." }, { 4, 34, 0.42f, 0.22f, 0.38f, 0.50f, 0.34f, 0.88f, 0.62f, 0.46f, 0.46f, 0.18f, 0, 1, 3, 0, 0.78f, 0.44f, 0.82f, 0.52f, 0.20f, 0.30f, 0.14f, 0.62f, 0.042f, 0.38f, 0.34f, 0.42f, 0.02f, 0.0f, 10.0f, 1.08f, 0.92f, -15.0f } },
    { { "Room Tone Draft", "A small interior draft for subtle beds." }, { 3, 12, 0.18f, 0.12f, 0.18f, 0.12f, 0.08f, 0.14f, 0.14f, 0.12f, 0.16f, 0.06f, 1, 0, 0, 0, 0.30f, 0.24f, 0.20f, 0.04f, 0.48f, 0.70f, 0.02f, 0.30f, 0.010f, 0.08f, 0.08f, 0.06f, 0.00f, 0.0f, 0.0f, 0.86f, 0.99f, -18.0f } },
    { { "Storm Gust Wall", "Dense broad sheets with controlled headroom." }, { 4, 56, 0.74f, 0.30f, 0.78f, 0.66f, 0.36f, 0.44f, 0.52f, 0.44f, 0.44f, 0.20f, 2, 1, 2, 1, 0.42f, 0.80f, 0.50f, 0.32f, 0.78f, 0.52f, 0.18f, 0.82f, 0.038f, 0.56f, 0.50f, 0.52f, 0.12f, 0.0f, 0.0f, 1.22f, 0.84f, -15.0f } },
    { { "Grass Ripple", "Many small soft entrances across the lower field." }, { 3, 42, 0.34f, 0.28f, 0.32f, 0.34f, 0.50f, 0.34f, 0.42f, 0.44f, 0.50f, 0.18f, 3, 1, 1, 0, 0.40f, 0.38f, 0.32f, 0.18f, 0.42f, 0.44f, 0.06f, 0.62f, 0.050f, 0.30f, 0.22f, 0.34f, 0.02f, 12.0f, -4.0f, 1.10f, 0.92f, -15.5f } },
    { { "Pipe Mouth", "Focused hollow wind with a strong body band." }, { 3, 18, 0.46f, 0.15f, 0.40f, 0.22f, 0.14f, 0.64f, 0.34f, 0.30f, 0.16f, 0.10f, 1, 1, 2, 0, 0.48f, 0.60f, 0.72f, 0.28f, 0.86f, 0.54f, 0.08f, 0.42f, 0.022f, 0.18f, 0.16f, 0.18f, 0.00f, 0.0f, 0.0f, 0.94f, 0.96f, -15.0f } },
    { { "Broken Fan Intake", "Lumpy mechanical turbulence without oscillator lock." }, { 3, 24, 0.56f, 0.54f, 0.48f, 0.72f, 0.82f, 0.52f, 0.42f, 0.52f, 0.22f, 0.18f, 4, 2, 2, 2, 0.42f, 0.34f, 0.50f, 0.36f, 0.50f, 0.22f, 0.32f, 0.54f, 0.090f, 0.30f, 0.24f, 0.26f, 0.08f, 28.0f, 0.0f, 1.02f, 0.82f, -16.0f } },
    { { "Frozen Overpass", "Cold bright air with restrained low body." }, { 4, 44, 0.52f, 0.18f, 0.46f, 0.32f, 0.26f, 0.50f, 0.82f, 0.64f, 0.54f, 0.12f, 2, 1, 1, 0, 0.68f, 0.50f, 0.42f, 0.66f, 0.22f, 0.36f, 0.08f, 0.76f, 0.032f, 0.42f, 0.40f, 0.42f, 0.04f, 0.0f, 18.0f, 1.18f, 0.90f, -15.0f } },
    { { "Backwash Weather", "A large slow room-scale weather bed." }, { 4, 64, 0.58f, 0.10f, 0.64f, 0.34f, 0.18f, 0.36f, 0.36f, 0.30f, 0.62f, 0.10f, 1, 0, 0, 1, 0.34f, 0.70f, 0.44f, 0.20f, 0.80f, 0.78f, 0.10f, 0.84f, 0.012f, 0.46f, 0.50f, 0.44f, 0.04f, 0.0f, -8.0f, 1.34f, 0.96f, -15.0f } },
    { { "Tornado Column", "High-curl funnel motion with strong updraft and pressure surges." }, { 5, 48, 0.78f, 0.42f, 0.84f, 0.72f, 0.48f, 0.42f, 0.58f, 0.52f, 0.72f, 0.22f, 4, 1, 2, 2, 0.42f, 0.82f, 0.50f, 0.36f, 0.74f, 0.38f, 0.24f, 0.92f, 0.180f, 0.76f, 0.78f, 1.00f, 0.92f, 0.0f, 18.0f, 1.08f, 0.62f, -7.0f } },
    { { "Wind Chime Porch", "Sparse struck metal points in a moving exterior draft." }, { 3, 22, 0.34f, 0.20f, 0.52f, 0.30f, 0.18f, 0.58f, 0.46f, 0.38f, 0.42f, 0.18f, 2, 1, 5, 1, 0.70f, 0.38f, 0.54f, 0.36f, 0.22f, 0.34f, 0.08f, 0.58f, 0.026f, 0.26f, 0.22f, 0.30f, 0.04f, 18.0f, 8.0f, 1.12f, 0.88f, -15.5f } },
    { { "Hanging Wood Blocks", "Short woody knocks animated by uneven gusts." }, { 3, 20, 0.42f, 0.30f, 0.58f, 0.44f, 0.26f, 0.62f, 0.30f, 0.28f, 0.34f, 0.18f, 4, 1, 6, 2, 0.42f, 0.30f, 0.42f, 0.18f, 0.74f, 0.24f, 0.20f, 0.52f, 0.048f, 0.32f, 0.28f, 0.32f, 0.02f, -14.0f, -4.0f, 1.02f, 0.82f, -15.0f } },
    { { "Aeolian Harp Fence", "Continuous string-like shimmer drawn out of the wind." }, { 3, 28, 0.30f, 0.12f, 0.40f, 0.20f, 0.16f, 0.72f, 0.62f, 0.46f, 0.46f, 0.14f, 1, 1, 7, 0, 0.58f, 0.62f, 0.74f, 0.48f, 0.26f, 0.62f, 0.05f, 0.64f, 0.018f, 0.34f, 0.18f, 0.24f, 0.06f, 0.0f, 10.0f, 1.16f, 0.94f, -16.0f } },
    { { "Reed Screen Rattle", "Dry reed buzzing and pressure flutter across a narrow band." }, { 3, 34, 0.38f, 0.42f, 0.44f, 0.58f, 0.66f, 0.64f, 0.52f, 0.42f, 0.38f, 0.20f, 5, 1, 8, 1, 0.54f, 0.36f, 0.50f, 0.34f, 0.42f, 0.26f, 0.22f, 0.60f, 0.058f, 0.42f, 0.36f, 0.38f, 0.02f, 10.0f, 2.0f, 1.10f, 0.84f, -15.0f } },
    { { "Loose Tarp Gusts", "Broad fabric flaps with soft low pressure movement." }, { 3, 26, 0.54f, 0.18f, 0.70f, 0.42f, 0.40f, 0.58f, 0.24f, 0.24f, 0.46f, 0.18f, 3, 1, 9, 1, 0.30f, 0.50f, 0.30f, 0.12f, 0.82f, 0.54f, 0.16f, 0.76f, 0.032f, 0.50f, 0.46f, 0.44f, 0.08f, -6.0f, -10.0f, 1.24f, 0.78f, -15.0f } },
}};

inline AmbiWindPresetInfo ambiWindFactoryPresetInfo(uint32_t index)
{
    return kAmbiWindPresets[std::min<uint32_t>(index, kAmbiWindFactoryPresetCount - 1u)].info;
}

inline AmbiWindParams ambiWindFactoryPreset(uint32_t index)
{
    index = std::min<uint32_t>(index, kAmbiWindFactoryPresetCount - 1u);
    const auto& v = kAmbiWindPresets[index].values;
    AmbiWindParams p {};
    (void)v.order;
    p.order = 3u;
    p.voices = v.voices;
    p.wind = v.wind;
    p.gustRate = v.gustRate;
    p.gustDepth = v.gustDepth;
    p.turbulence = v.turbulence;
    p.flutter = v.flutter;
    p.material = v.material;
    p.air = v.air;
    p.hiss = v.hiss;
    p.spread = v.spread;
    p.deviation = v.deviation;
    p.gustShape = v.gustShape;
    p.vectorRateHz = 0.006f + std::min(v.vectorRateHz, 2.0f) * 0.026f;
    p.materialMode = v.materialMode;
    p.gustEdge = v.gustEdge;
    p.center = v.center;
    p.sweep = v.sweep;
    p.q = v.q;
    p.shrill = v.shrill;
    p.body = v.body;
    p.breath = v.breath;
    p.grit = v.grit;
    p.field = v.field;
    p.motionRateHz = v.motionRateHz;
    p.motionFlow = v.flow;
    p.motionShear = v.shear;
    p.motionCurl = v.curl;
    p.motionUpdraft = v.updraft;
    p.centerAzimuthDeg = v.centerAzimuthDeg;
    p.centerElevationDeg = v.centerElevationDeg;
    p.centerDistance = v.centerDistance;
    p.spatialFollow = 1.0f - v.spatialFollow;
    p.outputGainDb = std::min(v.outputGainDb + 6.0f, -4.0f);
    switch (index) {
    case 2u: p.place = 1u; p.space = 0.24f; break; // Pine Needle Hiss / Canopy
    case 3u: p.place = 3u; p.space = 0.28f; break; // Window Crack Whistle / Room
    case 4u: p.place = 5u; p.space = 0.54f; break; // Canyon Rotor / Canyon
    case 5u: p.place = 6u; p.space = 0.34f; break; // Tin Vent Flutter / Tunnel
    case 6u: p.place = 1u; p.space = 0.18f; break; // Distant Tree Line / Canopy
    case 9u: p.place = 3u; p.space = 0.30f; break; // Room Tone Draft / Room
    case 12u: p.place = 6u; p.space = 0.46f; break; // Pipe Mouth / Tunnel
    case 13u: p.place = 6u; p.space = 0.30f; break; // Broken Fan Intake / Tunnel
    case 14u: p.place = 2u; p.space = 0.22f; break; // Frozen Overpass / Porch
    case 15u: p.place = 4u; p.space = 0.38f; break; // Backwash Weather / Hangar
    case 17u: p.place = 2u; p.space = 0.30f; break; // Wind Chime Porch / Porch
    case 18u: p.place = 2u; p.space = 0.26f; break; // Hanging Wood Blocks / Porch
    case 20u: p.place = 1u; p.space = 0.20f; break; // Reed Screen Rattle / Canopy
    case 21u: p.place = 2u; p.space = 0.18f; break; // Loose Tarp Gusts / Porch
    default: p.place = 0u; p.space = index == 16u ? 0.10f : 0.12f; break;
    }
    return p;
}

} // namespace s3g
