#pragma once

#include "s3g_ambi_wrangler_encoder.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace s3g {

struct AmbiWranglerPresetInfo {
    const char* name;
    const char* description;
};

struct AmbiWranglerPresetValues {
    uint32_t order;
    uint32_t voices;
    float rateA;
    float rateB;
    float fmAtoB;
    float fmBtoA;
    float runglerA;
    float runglerB;
    float spread;
    float deviation;
    uint32_t rungSize;
    uint32_t rateMode;
    float threshold;
    float color;
    float filter;
    float resonance;
    float filterRun;
    float filterSweep;
    float saturation;
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

struct AmbiWranglerPreset {
    AmbiWranglerPresetInfo info;
    AmbiWranglerPresetValues values;
};

inline constexpr uint32_t kAmbiWranglerFactoryPresetCount = 18;

inline constexpr std::array<AmbiWranglerPreset, kAmbiWranglerFactoryPresetCount> kAmbiWranglerPresets {{
    { { "Deep Choir Mask", "Slow masked low-register ensemble from the 20a pad zones." }, { 3, 28, 0.055f, 0.180f, 0.135f, 0.108f, 0.125f, 0.098f, 0.24f, 0.10f, 2, 0, 0.071f, 0.59f, 0.54f, 0.40f, 0.23f, 0.21f, 0.47f, 0.78f, 2, 0.72f, 0.030f, 10, 4, 0.036f, 0.60f, 0.60f, 0.98f, 0.0f, 0.0f, 0.0f, 1.0f, 0.94f, -28.0f } },
    { { "Half Lit Register", "Uneven high/low oscillator contrast with voices entering in cells." }, { 3, 24, 0.555f, 0.825f, 0.147f, 0.203f, 0.028f, 0.507f, 0.34f, 0.18f, 2, 1, 0.658f, 0.25f, 0.39f, 0.58f, 0.10f, 0.28f, 0.36f, 0.84f, 3, 0.70f, 0.085f, 11, 1, 0.016f, 0.52f, 0.50f, 0.88f, 0.0f, 0.0f, 10.0f, 1.05f, 0.92f, -34.0f } },
    { { "Bass Rung Cloud", "Low oscillators with large Rungler-B choir movement." }, { 3, 32, 0.035f, 0.016f, 0.154f, 0.049f, 0.759f, 0.185f, 0.16f, 0.13f, 3, 0, 0.436f, 0.25f, 0.38f, 0.42f, 0.40f, 0.14f, 0.55f, 0.72f, 2, 0.64f, 0.055f, 2, 6, 0.084f, 0.52f, 0.44f, 0.94f, 0.12f, -8.0f, 0.0f, 1.18f, 0.96f, -31.5f } },
    { { "Glass Mask", "Bright rate split, strong filtering, and a sparse audible layer." }, { 3, 20, 0.686f, 0.314f, 0.224f, 0.229f, 0.016f, 0.045f, 0.14f, 0.13f, 2, 1, 0.071f, 0.24f, 0.58f, 0.68f, 0.43f, 0.44f, 0.24f, 0.92f, 4, 0.80f, 0.120f, 0, 5, 0.080f, 0.50f, 0.42f, 0.92f, 0.0f, 18.0f, 0.0f, 1.05f, 0.88f, -32.5f } },
    { { "Slow Teeth", "Low reciprocal FM bite with rotating partial audibility." }, { 3, 20, 0.105f, 0.186f, 0.324f, 0.750f, 0.169f, 0.246f, 0.14f, 0.09f, 2, 0, 0.496f, 0.56f, 0.28f, 0.34f, 0.09f, 0.26f, 0.30f, 0.70f, 3, 0.66f, 0.045f, 5, 13, 0.028f, 0.56f, 0.56f, 0.94f, 0.0f, -12.0f, 0.0f, 0.96f, 0.94f, -31.5f } },
    { { "Twenty Tiny Clocks", "Fast small voices with a breathing amplitude mesh." }, { 3, 36, 0.674f, 0.696f, 0.219f, 0.388f, 0.052f, 0.059f, 0.22f, 0.14f, 2, 1, 0.794f, 0.36f, 0.50f, 0.31f, 0.46f, 0.52f, 0.16f, 0.68f, 1, 0.58f, 0.140f, 6, 17, 0.076f, 0.48f, 0.60f, 0.90f, 0.0f, 26.0f, 0.0f, 1.16f, 0.88f, -33.0f } },
    { { "Soft Comparator Pad", "Filtered PWM body with slow choir drift and restrained FM." }, { 3, 30, 0.250f, 0.260f, 0.400f, 0.420f, 0.300f, 0.300f, 0.10f, 0.05f, 4, 0, 0.500f, 0.50f, 0.36f, 0.30f, 0.14f, 0.10f, 0.18f, 0.48f, 2, 0.46f, 0.045f, 3, 1, 0.014f, 0.34f, 0.24f, 0.72f, 0.0f, 0.0f, -10.0f, 0.88f, 0.98f, -25.5f } },
    { { "Sparkle Lattice", "Snappy bright cells and high threshold sparkle bursts." }, { 3, 24, 0.465f, 0.077f, 0.101f, 0.691f, 0.261f, 0.339f, 0.30f, 0.16f, 2, 1, 0.906f, 0.18f, 0.29f, 0.68f, 0.22f, 0.24f, 0.28f, 0.92f, 4, 0.82f, 0.200f, 1, 3, 0.052f, 0.52f, 0.48f, 0.92f, 0.0f, 34.0f, 0.0f, 1.22f, 0.86f, -34.5f } },
    { { "Rungler Altos", "Strong B-side rungler choir with mellow upper voices." }, { 3, 22, 0.077f, 0.491f, 0.621f, 0.458f, 0.040f, 0.925f, 0.42f, 0.09f, 2, 1, 0.906f, 0.51f, 0.40f, 0.48f, 0.35f, 0.29f, 0.32f, 0.86f, 2, 0.70f, 0.070f, 5, 2, 0.068f, 0.60f, 0.56f, 0.86f, 0.0f, 8.0f, 0.0f, 1.02f, 0.90f, -35.0f } },
    { { "Breathing Crosswire", "A slow ensemble that swells through FM cross-coupling." }, { 3, 26, 0.107f, 0.609f, 0.072f, 0.424f, 0.441f, 0.254f, 0.18f, 0.12f, 2, 0, 0.452f, 0.06f, 0.64f, 0.61f, 0.07f, 0.24f, 0.30f, 0.74f, 1, 0.62f, 0.038f, 8, 1, 0.024f, 0.54f, 0.54f, 0.92f, 0.0f, -18.0f, 0.0f, 1.08f, 0.96f, -31.0f } },
    { { "Dust Window", "Sparse quiet dust from low clocks and audible mask holes." }, { 3, 18, 0.026f, 0.055f, 0.218f, 0.035f, 0.748f, 0.218f, 0.16f, 0.14f, 2, 0, 0.414f, 0.40f, 0.42f, 0.42f, 0.39f, 0.59f, 0.53f, 0.78f, 4, 0.88f, 0.110f, 11, 1, 0.092f, 0.56f, 0.48f, 0.98f, 0.0f, -26.0f, 0.0f, 1.12f, 0.98f, -32.0f } },
    { { "Formant Swarm", "Resonant clustered voices with mid-speed filter-rung motion." }, { 3, 34, 0.634f, 0.305f, 0.216f, 0.219f, 0.048f, 0.076f, 0.20f, 0.11f, 4, 0, 0.175f, 0.22f, 0.64f, 0.85f, 0.43f, 0.39f, 0.18f, 0.66f, 2, 0.58f, 0.050f, 3, 9, 0.032f, 0.58f, 0.58f, 0.96f, 0.0f, 14.0f, 0.0f, 0.92f, 0.93f, -31.0f } },
    { { "Snappy Register Choir", "High contrast slots reworked into punchy rotating voicings." }, { 3, 20, 0.059f, 0.810f, 0.408f, 0.778f, 0.147f, 0.081f, 0.24f, 0.10f, 3, 1, 0.742f, 0.41f, 0.52f, 0.26f, 0.15f, 0.12f, 0.48f, 0.80f, 3, 0.74f, 0.160f, 4, 8, 0.064f, 0.58f, 0.54f, 0.98f, 0.0f, 22.0f, 0.0f, 1.18f, 0.90f, -33.0f } },
    { { "Narrow Organism II", "Compact choir center with very legible beating." }, { 3, 16, 0.290f, 0.300f, 0.240f, 0.280f, 0.220f, 0.200f, 0.08f, 0.04f, 4, 0, 0.500f, 0.62f, 0.44f, 0.36f, 0.18f, 0.16f, 0.20f, 0.34f, 2, 0.38f, 0.018f, 0, 9, 0.016f, 0.24f, 0.20f, 0.56f, 0.0f, 0.0f, 0.0f, 0.72f, 0.99f, -24.0f } },
    { { "Fault Mask", "Hot rungler cells clipped into a wide unstable shell." }, { 3, 40, 0.452f, 0.227f, 0.122f, 0.682f, 0.535f, 0.870f, 0.44f, 0.18f, 2, 1, 0.906f, 0.17f, 0.34f, 0.70f, 0.27f, 0.16f, 0.26f, 0.96f, 4, 0.78f, 0.180f, 11, 4, 0.056f, 0.82f, 0.70f, 1.22f, 0.16f, 0.0f, 4.0f, 1.22f, 0.90f, -36.0f } },
    { { "Run Filter Kites II", "Filter-run voices rising and falling in a lifted mask." }, { 3, 30, 0.320f, 0.083f, 0.219f, 0.372f, 0.062f, 0.932f, 0.17f, 0.14f, 2, 1, 0.906f, 0.36f, 0.57f, 0.38f, 0.29f, 0.56f, 0.12f, 0.72f, 5, 0.70f, 0.090f, 10, 10, 0.072f, 0.62f, 0.58f, 0.88f, 0.0f, 30.0f, 0.0f, 1.05f, 0.94f, -33.5f } },
    { { "Quiet Teeth Halo", "Soft sparse mask with low clocks and distant topology." }, { 3, 22, 0.065f, 0.045f, 0.082f, 0.734f, 0.029f, 0.316f, 0.12f, 0.09f, 2, 0, 0.839f, 0.13f, 0.30f, 0.64f, 0.24f, 0.24f, 0.31f, 0.58f, 4, 0.72f, 0.035f, 6, 12, 0.048f, 0.50f, 0.46f, 1.10f, 0.08f, -18.0f, -6.0f, 1.34f, 0.98f, -32.5f } },
    { { "Wide Backwash Organ", "Large slow choir, broad field, and gentle mask breathing." }, { 3, 48, 0.366f, 0.514f, 0.124f, 0.530f, 0.069f, 0.068f, 0.34f, 0.08f, 2, 0, 0.112f, 0.22f, 0.53f, 0.54f, 0.19f, 0.20f, 0.47f, 0.62f, 1, 0.50f, 0.022f, 9, 9, 0.018f, 0.54f, 0.46f, 1.18f, 0.0f, 0.0f, 0.0f, 1.24f, 0.97f, -30.0f } },
}};

inline AmbiWranglerPresetInfo ambiWranglerFactoryPresetInfo(uint32_t index)
{
    return kAmbiWranglerPresets[std::min<uint32_t>(index, kAmbiWranglerFactoryPresetCount - 1u)].info;
}

inline float ambiWranglerPresetUnit(uint32_t seed)
{
    seed ^= seed >> 16u;
    seed *= 0x7feb352du;
    seed ^= seed >> 15u;
    seed *= 0x846ca68bu;
    seed ^= seed >> 16u;
    return static_cast<float>(seed & 0x00ffffffu) / static_cast<float>(0x00ffffffu);
}

inline float ambiWranglerPresetSigned(uint32_t seed)
{
    return ambiWranglerPresetUnit(seed) * 2.0f - 1.0f;
}

inline float ambiWranglerPresetWave(float lane, float cycles, float phase)
{
    return std::sin((lane * cycles + phase) * kPi * 2.0f);
}

inline float ambiWranglerPresetCell(float lane, float center, float width)
{
    const float wrapped = std::fabs((lane - center) - std::floor((lane - center) + 0.5f));
    if (wrapped >= width) return 0.0f;
    return 1.0f - wrapped / std::max(0.0001f, width);
}

inline void ambiWranglerFillPresetBreakpoints(AmbiWranglerParams& p, uint32_t presetIndex)
{
    p.voiceBreakpointsEnabled = true;
    const uint32_t voices = std::clamp<uint32_t>(p.voices, 1u, kAmbiWranglerMaxVoices);
    const float phase = ambiWranglerPresetUnit(1009u + presetIndex * 131u);
    const float rateDepth = 0.10f + p.spread * 0.28f + p.field * 0.08f;
    const float crossDepth = 0.08f + p.deviation * 0.35f + p.field * 0.10f;
    const float rungDepth = 0.10f + std::max(p.runglerA, p.runglerB) * 0.24f;
    const float filterDepth = 0.08f + p.filterRun * 0.22f + p.filterSweep * 0.18f;
    const float thresholdDepth = 0.08f + p.deviation * 0.26f;
    const float sparse = p.maskMode == 4u ? 0.42f : (p.maskMode == 3u ? 0.34f : 0.18f);
    for (uint32_t voice = 0u; voice < kAmbiWranglerMaxVoices; ++voice) {
        const float lane = static_cast<float>(voice) / static_cast<float>(std::max<uint32_t>(1u, voices - 1u));
        const float activeLane = static_cast<float>(voice % voices) / static_cast<float>(std::max<uint32_t>(1u, voices - 1u));
        const float alt = voice % 2u ? -1.0f : 1.0f;
        const float rnd = ambiWranglerPresetSigned(voice * 7919u + presetIndex * 104729u);
        const float slow = ambiWranglerPresetWave(activeLane, 1.0f + static_cast<float>(presetIndex % 4u), phase);
        const float fast = ambiWranglerPresetWave(activeLane, 2.0f + static_cast<float>((presetIndex + 1u) % 5u), phase * 0.37f + 0.21f);
        const float braid = ambiWranglerPresetWave(activeLane, 3.0f + static_cast<float>((presetIndex + 2u) % 3u), phase * 0.71f + 0.09f);
        const float cellA = ambiWranglerPresetCell(activeLane, std::fmod(phase + 0.17f, 1.0f), 0.20f + p.field * 0.12f);
        const float cellB = ambiWranglerPresetCell(activeLane, std::fmod(phase + 0.53f, 1.0f), 0.16f + p.deviation * 0.22f);

        p.bpRateA[voice] = std::clamp(0.5f + slow * rateDepth + (lane - 0.5f) * p.spread * 0.26f + rnd * p.deviation * 0.08f, 0.0f, 1.0f);
        p.bpRateB[voice] = std::clamp(0.5f - fast * rateDepth * 0.82f - (lane - 0.5f) * p.spread * 0.22f + rnd * p.deviation * 0.06f, 0.0f, 1.0f);
        p.bpFmAtoB[voice] = std::clamp(0.5f + (cellA - 0.35f) * crossDepth + braid * crossDepth * 0.55f, 0.0f, 1.0f);
        p.bpFmBtoA[voice] = std::clamp(0.5f + (cellB - 0.35f) * crossDepth - slow * crossDepth * 0.50f, 0.0f, 1.0f);
        p.bpRunglerA[voice] = std::clamp(0.5f + alt * rungDepth * 0.36f + fast * rungDepth * 0.56f + rnd * 0.035f, 0.0f, 1.0f);
        p.bpRunglerB[voice] = std::clamp(0.5f - alt * rungDepth * 0.28f - braid * rungDepth * 0.62f - rnd * 0.030f, 0.0f, 1.0f);
        p.bpFilter[voice] = std::clamp(0.5f + slow * filterDepth * 0.70f + cellA * filterDepth * 0.42f - cellB * filterDepth * 0.25f, 0.0f, 1.0f);
        p.bpThreshold[voice] = std::clamp(0.5f + braid * thresholdDepth + alt * thresholdDepth * 0.22f, 0.0f, 1.0f);
        p.bpPwmA[voice] = std::clamp(0.5f + fast * 0.18f + alt * 0.08f + cellA * 0.12f, 0.0f, 1.0f);
        p.bpPwmB[voice] = std::clamp(0.5f - slow * 0.16f - alt * 0.06f + cellB * 0.14f, 0.0f, 1.0f);
        p.bpRampA[voice] = std::clamp(0.5f + braid * 0.16f + (lane - 0.5f) * 0.14f, 0.0f, 1.0f);
        p.bpRampB[voice] = std::clamp(0.5f - fast * 0.14f - (lane - 0.5f) * 0.12f + rnd * 0.04f, 0.0f, 1.0f);

        float amp = 0.64f + slow * 0.16f + cellA * 0.22f - cellB * sparse + rnd * 0.055f;
        if (p.maskMode == 4u) amp = (ambiWranglerPresetUnit(voice * 157u + presetIndex * 1297u) > 0.42f) ? amp : amp * 0.18f;
        if (p.maskMode == 3u && cellA < 0.08f && cellB < 0.08f) amp *= 0.34f;
        p.bpAmp[voice] = std::clamp(amp, 0.04f, 1.0f);
    }
}

inline AmbiWranglerParams ambiWranglerFactoryPreset(uint32_t index)
{
    index = std::min<uint32_t>(index, kAmbiWranglerFactoryPresetCount - 1u);
    const auto& v = kAmbiWranglerPresets[index].values;
    AmbiWranglerParams p {};
    p.order = v.order;
    p.voices = v.voices;
    p.rateA = v.rateA;
    p.rateB = v.rateB;
    p.fmAtoB = v.fmAtoB;
    p.fmBtoA = v.fmBtoA;
    p.runglerA = v.runglerA;
    p.runglerB = v.runglerB;
    p.spread = v.spread;
    p.deviation = v.deviation;
    p.rungSize = v.rungSize;
    p.rateModeA = v.rateMode;
    p.rateModeB = v.rateMode;
    p.threshold = v.threshold;
    p.color = v.color;
    p.filter = v.filter;
    p.resonance = v.resonance;
    p.filterRun = v.filterRun;
    p.filterSweep = v.filterSweep;
    p.saturation = v.saturation;
    p.field = v.field;
    p.maskMode = v.maskMode;
    p.maskDepth = v.maskDepth;
    p.maskRateHz = v.maskRateHz;
    p.topologyShape = v.topologyShape;
    p.topologyMotion = v.topologyMotion;
    p.topologyRateHz = v.topologyRateHz;
    p.topologyAmount = v.topologyAmount;
    p.topologyDepth = v.topologyDepth;
    p.topologyScale = v.topologyScale;
    p.topologyCollapse = v.topologyCollapse;
    p.centerAzimuthDeg = v.centerAzimuthDeg;
    p.centerElevationDeg = v.centerElevationDeg;
    p.centerDistance = v.centerDistance;
    p.spatialFollow = v.spatialFollow;
    p.outputGainDb = -6.0f;
    p.pwmA = std::clamp(0.46f + ambiWranglerPresetSigned(index * 331u + 17u) * 0.08f, 0.0f, 1.0f);
    p.pwmB = std::clamp(0.54f + ambiWranglerPresetSigned(index * 337u + 19u) * 0.08f, 0.0f, 1.0f);
    p.rampA = std::clamp(0.50f + ambiWranglerPresetSigned(index * 347u + 23u) * 0.16f, 0.0f, 1.0f);
    p.rampB = std::clamp(0.50f + ambiWranglerPresetSigned(index * 349u + 29u) * 0.16f, 0.0f, 1.0f);
    p.inputA = (index % 5u) == 2u ? 1u : 0u;
    p.inputB = (index % 6u) == 3u ? 1u : 0u;
    ambiWranglerFillPresetBreakpoints(p, index);
    return p;
}

} // namespace s3g
