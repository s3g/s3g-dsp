#pragma once

#include "s3g_ambi_wind_encoder.h"

#include <algorithm>
#include <array>
#include <cmath>
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
    uint32_t rateMode;
    uint32_t materialMode;
    float center;
    float sweep;
    float q;
    float shrill;
    float body;
    float breath;
    float grit;
    float field;
    uint32_t maskMode;
    float maskDepth;
    float maskRateHz;
    uint32_t topologyShape;
    uint32_t topologyMotion;
    float topologyRateHz;
    float topologyAmount;
    float topologyDepth;
    float topologyScale;
    float topologyCollapse;
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

inline constexpr uint32_t kAmbiWindFactoryPresetCount = 16;

inline constexpr std::array<AmbiWindPreset, kAmbiWindFactoryPresetCount> kAmbiWindPresets {{
    { { "PAIA Bench Wind", "Circuit-block default: noise, random voltage, band-pass wind." }, { 3, 24, 0.52f, 0.22f, 0.46f, 0.30f, 0.22f, 0.28f, 0.36f, 0.30f, 0.34f, 0.12f, 2, 1, 0, 0.38f, 0.44f, 0.42f, 0.20f, 0.54f, 0.42f, 0.12f, 0.70f, 2, 0.42f, 0.045f, 11, 1, 0.022f, 0.74f, 0.64f, 1.12f, 0.00f, 0.0f, 0.0f, 1.00f, 0.92f, -6.0f } },
    { { "Low Pressure Front", "Slow heavy gusts with broad low filter movement." }, { 3, 36, 0.72f, 0.13f, 0.78f, 0.42f, 0.18f, 0.34f, 0.24f, 0.22f, 0.48f, 0.18f, 1, 0, 0, 0.24f, 0.66f, 0.48f, 0.18f, 0.72f, 0.70f, 0.16f, 0.84f, 5, 0.52f, 0.026f, 9, 1, 0.015f, 0.72f, 0.70f, 1.22f, 0.02f, -10.0f, 0.0f, 1.12f, 0.96f, -6.0f } },
    { { "Pine Needle Hiss", "Fine high air through a flickering material layer." }, { 3, 32, 0.45f, 0.36f, 0.34f, 0.58f, 0.66f, 0.72f, 0.78f, 0.72f, 0.32f, 0.22f, 5, 1, 1, 0.62f, 0.38f, 0.62f, 0.64f, 0.30f, 0.28f, 0.20f, 0.78f, 3, 0.50f, 0.090f, 6, 3, 0.058f, 0.66f, 0.54f, 1.06f, 0.04f, 18.0f, 6.0f, 1.02f, 0.90f, -6.0f } },
    { { "Window Crack Whistle", "Narrow bright Q with small gust envelopes." }, { 3, 18, 0.38f, 0.28f, 0.30f, 0.22f, 0.20f, 0.86f, 0.52f, 0.60f, 0.12f, 0.08f, 0, 1, 3, 0.72f, 0.28f, 0.86f, 0.72f, 0.20f, 0.24f, 0.22f, 0.42f, 1, 0.28f, 0.035f, 0, 9, 0.018f, 0.36f, 0.22f, 0.72f, 0.00f, 0.0f, 4.0f, 0.92f, 0.98f, -6.0f } },
    { { "Canyon Rotor", "Large rotating gust cells with body-heavy noise." }, { 4, 40, 0.68f, 0.20f, 0.62f, 0.48f, 0.30f, 0.42f, 0.28f, 0.34f, 0.58f, 0.20f, 3, 1, 0, 0.34f, 0.62f, 0.56f, 0.26f, 0.74f, 0.52f, 0.20f, 0.88f, 2, 0.46f, 0.040f, 10, 4, 0.032f, 0.82f, 0.72f, 1.28f, 0.08f, 0.0f, -6.0f, 1.16f, 0.90f, -6.0f } },
    { { "Tin Vent Flutter", "Metallic duct flutter with faster random-voltage motion." }, { 3, 28, 0.48f, 0.48f, 0.48f, 0.62f, 0.78f, 0.80f, 0.48f, 0.54f, 0.24f, 0.18f, 4, 2, 4, 0.56f, 0.52f, 0.74f, 0.54f, 0.28f, 0.22f, 0.30f, 0.72f, 4, 0.58f, 0.120f, 3, 8, 0.070f, 0.62f, 0.56f, 1.02f, 0.04f, 24.0f, 0.0f, 1.06f, 0.84f, -6.0f } },
    { { "Distant Tree Line", "Soft massed voices with slow masked choir breathing." }, { 3, 48, 0.42f, 0.10f, 0.42f, 0.20f, 0.14f, 0.30f, 0.30f, 0.24f, 0.52f, 0.12f, 1, 0, 1, 0.30f, 0.34f, 0.34f, 0.12f, 0.68f, 0.78f, 0.08f, 0.82f, 2, 0.66f, 0.018f, 9, 9, 0.012f, 0.54f, 0.44f, 1.26f, 0.00f, 0.0f, 0.0f, 1.24f, 0.98f, -6.0f } },
    { { "Sand Across Metal", "Dry granular wind with sharper high-band texture." }, { 3, 30, 0.54f, 0.42f, 0.30f, 0.72f, 0.58f, 0.74f, 0.62f, 0.82f, 0.38f, 0.26f, 5, 1, 4, 0.58f, 0.22f, 0.54f, 0.60f, 0.26f, 0.18f, 0.42f, 0.86f, 4, 0.72f, 0.160f, 11, 3, 0.082f, 0.74f, 0.62f, 1.10f, 0.10f, -22.0f, 0.0f, 1.10f, 0.86f, -6.0f } },
    { { "Harbor Rigging", "Whistling wires and restless air in a wide shell." }, { 4, 34, 0.46f, 0.24f, 0.40f, 0.52f, 0.38f, 0.92f, 0.64f, 0.50f, 0.46f, 0.22f, 0, 1, 3, 0.74f, 0.46f, 0.90f, 0.58f, 0.22f, 0.34f, 0.24f, 0.76f, 3, 0.58f, 0.065f, 5, 2, 0.046f, 0.68f, 0.70f, 1.18f, 0.02f, 0.0f, 10.0f, 1.08f, 0.92f, -6.0f } },
    { { "Room Tone Draft", "Quiet interior draft, low output density, gentle movement." }, { 3, 16, 0.24f, 0.16f, 0.22f, 0.16f, 0.10f, 0.18f, 0.18f, 0.16f, 0.18f, 0.08f, 2, 0, 0, 0.32f, 0.26f, 0.24f, 0.08f, 0.50f, 0.60f, 0.04f, 0.48f, 1, 0.22f, 0.020f, 2, 1, 0.010f, 0.28f, 0.22f, 0.64f, 0.00f, 0.0f, 0.0f, 0.80f, 0.99f, -9.0f } },
    { { "Storm Gust Wall", "Dense loud gust sheets with strong sweep and turbulence." }, { 4, 56, 0.82f, 0.34f, 0.88f, 0.70f, 0.42f, 0.50f, 0.56f, 0.48f, 0.42f, 0.24f, 2, 1, 2, 0.46f, 0.82f, 0.62f, 0.38f, 0.76f, 0.48f, 0.28f, 0.94f, 5, 0.62f, 0.050f, 4, 4, 0.044f, 0.88f, 0.82f, 1.30f, 0.16f, 0.0f, 0.0f, 1.22f, 0.86f, -6.0f } },
    { { "Grass Ripple", "Low-level rippling gusts with many small masked entrances." }, { 3, 42, 0.36f, 0.30f, 0.36f, 0.36f, 0.54f, 0.40f, 0.44f, 0.48f, 0.50f, 0.20f, 3, 1, 1, 0.42f, 0.40f, 0.38f, 0.22f, 0.42f, 0.42f, 0.10f, 0.78f, 3, 0.74f, 0.110f, 6, 17, 0.060f, 0.58f, 0.50f, 1.16f, 0.02f, 12.0f, -4.0f, 1.10f, 0.92f, -6.0f } },
    { { "Pipe Mouth", "Focused breath through a resonant hollow opening." }, { 3, 20, 0.50f, 0.18f, 0.44f, 0.26f, 0.16f, 0.68f, 0.36f, 0.34f, 0.16f, 0.12f, 1, 1, 2, 0.48f, 0.60f, 0.82f, 0.32f, 0.84f, 0.52f, 0.14f, 0.56f, 2, 0.38f, 0.030f, 0, 9, 0.024f, 0.42f, 0.36f, 0.86f, 0.00f, 0.0f, 0.0f, 0.92f, 0.96f, -6.0f } },
    { { "Broken Fan Intake", "Fast lumpy turbulence with hard-edged gust pulses." }, { 3, 26, 0.60f, 0.58f, 0.54f, 0.76f, 0.84f, 0.58f, 0.46f, 0.58f, 0.24f, 0.20f, 4, 2, 2, 0.44f, 0.34f, 0.58f, 0.42f, 0.48f, 0.20f, 0.46f, 0.72f, 4, 0.56f, 0.190f, 1, 3, 0.105f, 0.50f, 0.44f, 0.94f, 0.08f, 28.0f, 0.0f, 1.02f, 0.82f, -6.0f } },
    { { "Frozen Overpass", "Cold bright sheet with restrained body and wide elevation." }, { 4, 44, 0.58f, 0.20f, 0.50f, 0.36f, 0.30f, 0.56f, 0.84f, 0.68f, 0.54f, 0.14f, 2, 1, 1, 0.66f, 0.52f, 0.50f, 0.72f, 0.24f, 0.38f, 0.16f, 0.86f, 2, 0.54f, 0.036f, 10, 10, 0.036f, 0.72f, 0.74f, 1.34f, 0.06f, 0.0f, 18.0f, 1.20f, 0.90f, -6.0f } },
    { { "Backwash Weather", "Large saturated weather bed for slow multichannel rooms." }, { 4, 64, 0.64f, 0.12f, 0.70f, 0.38f, 0.22f, 0.44f, 0.40f, 0.34f, 0.62f, 0.12f, 1, 0, 0, 0.36f, 0.72f, 0.54f, 0.26f, 0.78f, 0.74f, 0.18f, 0.92f, 5, 0.68f, 0.022f, 9, 1, 0.014f, 0.78f, 0.82f, 1.40f, 0.04f, 0.0f, -8.0f, 1.32f, 0.96f, -6.0f } },
}};

inline AmbiWindPresetInfo ambiWindFactoryPresetInfo(uint32_t index)
{
    return kAmbiWindPresets[std::min<uint32_t>(index, kAmbiWindFactoryPresetCount - 1u)].info;
}

inline float ambiWindPresetUnit(uint32_t seed)
{
    seed ^= seed >> 16u;
    seed *= 0x7feb352du;
    seed ^= seed >> 15u;
    seed *= 0x846ca68bu;
    seed ^= seed >> 16u;
    return static_cast<float>(seed & 0x00ffffffu) / static_cast<float>(0x00ffffffu);
}

inline float ambiWindPresetSigned(uint32_t seed)
{
    return ambiWindPresetUnit(seed) * 2.0f - 1.0f;
}

inline float ambiWindPresetWave(float lane, float cycles, float phase)
{
    return std::sin((lane * cycles + phase) * kPi * 2.0f);
}

inline void ambiWindFillPresetBreakpoints(AmbiWindParams& p, uint32_t presetIndex)
{
    p.voiceBreakpointsEnabled = true;
    const uint32_t voices = std::clamp<uint32_t>(p.voices, 1u, kAmbiWindMaxVoices);
    const float phase = ambiWindPresetUnit(2027u + presetIndex * 149u);
    for (uint32_t voice = 0u; voice < kAmbiWindMaxVoices; ++voice) {
        const float lane = static_cast<float>(voice % voices) / static_cast<float>(std::max<uint32_t>(1u, voices - 1u));
        const float alt = voice % 2u ? -1.0f : 1.0f;
        const float rnd = ambiWindPresetSigned(voice * 7919u + presetIndex * 104729u);
        const float slow = ambiWindPresetWave(lane, 1.0f + static_cast<float>(presetIndex % 4u), phase);
        const float fast = ambiWindPresetWave(lane, 2.0f + static_cast<float>((presetIndex + 1u) % 5u), phase * 0.37f + 0.21f);
        const float cell = std::max(0.0f, 1.0f - std::fabs(lane - std::fmod(phase + 0.31f, 1.0f)) / (0.14f + p.field * 0.18f));

        p.bpWind[voice] = std::clamp(0.5f + slow * 0.18f + rnd * p.deviation * 0.10f, 0.0f, 1.0f);
        p.bpGustRate[voice] = std::clamp(0.5f - fast * 0.22f + (lane - 0.5f) * p.spread * 0.20f, 0.0f, 1.0f);
        p.bpGustDepth[voice] = std::clamp(0.5f + cell * 0.32f - fast * 0.10f, 0.0f, 1.0f);
        p.bpTurbulence[voice] = std::clamp(0.5f + fast * 0.26f + rnd * 0.08f, 0.0f, 1.0f);
        p.bpFlutter[voice] = std::clamp(0.5f + alt * 0.18f + fast * 0.18f, 0.0f, 1.0f);
        p.bpMaterial[voice] = std::clamp(0.5f - alt * 0.12f + slow * 0.20f + cell * 0.16f, 0.0f, 1.0f);
        p.bpCenter[voice] = std::clamp(0.5f + slow * 0.28f + (lane - 0.5f) * p.spread * 0.22f, 0.0f, 1.0f);
        p.bpQ[voice] = std::clamp(0.5f + fast * 0.20f + cell * 0.20f, 0.0f, 1.0f);
        p.bpAir[voice] = std::clamp(0.5f + fast * 0.22f + rnd * 0.08f, 0.0f, 1.0f);
        p.bpHiss[voice] = std::clamp(0.5f - slow * 0.18f + rnd * 0.09f, 0.0f, 1.0f);
        p.bpSweep[voice] = std::clamp(0.5f + slow * 0.24f - cell * 0.14f, 0.0f, 1.0f);
        p.bpBody[voice] = std::clamp(0.5f - fast * 0.18f + alt * 0.10f, 0.0f, 1.0f);
        p.bpAmp[voice] = std::clamp(0.58f + slow * 0.16f + cell * 0.24f + rnd * 0.06f, 0.04f, 1.0f);
    }
}

inline AmbiWindParams ambiWindFactoryPreset(uint32_t index)
{
    index = std::min<uint32_t>(index, kAmbiWindFactoryPresetCount - 1u);
    const auto& v = kAmbiWindPresets[index].values;
    AmbiWindParams p {};
    p.order = v.order;
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
    p.rateMode = v.rateMode;
    p.materialMode = v.materialMode;
    p.center = v.center;
    p.sweep = v.sweep;
    p.q = v.q;
    p.shrill = v.shrill;
    p.body = v.body;
    p.breath = v.breath;
    p.grit = v.grit;
    p.field = v.field;
    p.maskMode = 0u;
    p.maskDepth = 0.0f;
    p.maskRateHz = 0.0f;
    p.topologyShape = 0u;
    p.topologyMotion = 0u;
    p.topologyRateHz = v.topologyRateHz;
    p.topologyAmount = std::min(v.topologyAmount * 0.55f, 0.52f);
    p.topologyDepth = std::min(v.topologyDepth * 0.55f, 0.50f);
    p.topologyScale = std::clamp((v.topologyScale - 0.70f) * 0.72f, 0.08f, 0.56f);
    p.topologyCollapse = std::min(v.topologyCollapse, 0.20f);
    p.centerAzimuthDeg = v.centerAzimuthDeg;
    p.centerElevationDeg = v.centerElevationDeg;
    p.centerDistance = v.centerDistance;
    p.spatialFollow = v.spatialFollow;
    p.outputGainDb = std::min(v.outputGainDb, -9.0f);
    ambiWindFillPresetBreakpoints(p, index);
    return p;
}

} // namespace s3g
