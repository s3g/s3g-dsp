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
    uint32_t rateModeA;
    uint32_t rateModeB;
    uint32_t rungLoop;
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
    float snap;
    float snapDecay;
    uint32_t inputA;
    uint32_t inputB;
};

struct AmbiWranglerPreset {
    AmbiWranglerPresetInfo info;
    AmbiWranglerPresetValues values;
};

inline constexpr uint32_t kAmbiWranglerBasePresetCount = 18;
inline constexpr uint32_t kAmbiWranglerListenerPresetCount = 3;
inline constexpr uint32_t kAmbiWranglerListenerPresetStart =
    kAmbiWranglerBasePresetCount;
inline constexpr uint32_t kAmbiWranglerCalmPresetCount = 5;
inline constexpr uint32_t kAmbiWranglerCalmPresetStart =
    kAmbiWranglerListenerPresetStart + kAmbiWranglerListenerPresetCount;
inline constexpr uint32_t kAmbiWranglerFactoryPresetCount =
    kAmbiWranglerCalmPresetStart + kAmbiWranglerCalmPresetCount;

inline constexpr std::array<AmbiWranglerPreset, kAmbiWranglerBasePresetCount> kAmbiWranglerPresets {{
    { { "Classic Slow Rungler", "Eight-step Benjolin drift with clear filter sweep and choir mask." }, { 3, 24, 0.180f, 0.115f, 0.18f, 0.14f, 0.38f, 0.32f, 0.18f, 0.08f, 8, 1, 1, 0, 0.48f, 0.46f, 0.42f, 0.52f, 0.36f, 0.44f, 0.34f, 0.66f, 2, 0.58f, 0.045f, 11, 1, 0.028f, 0.54f, 0.52f, 1.02f, 0.02f, 0.0f, 0.0f, 1.00f, 0.94f, -6.0f, 0.18f, 0.28f, 0, 0 } },
    { { "Subharmonic Loop", "LOW-range oscillator B recirculating the register into a large pad." }, { 3, 28, 0.320f, 0.220f, 0.10f, 0.22f, 0.62f, 0.40f, 0.22f, 0.10f, 8, 1, 0, 1, 0.42f, 0.52f, 0.36f, 0.46f, 0.48f, 0.30f, 0.42f, 0.74f, 3, 0.70f, 0.030f, 10, 4, 0.034f, 0.60f, 0.60f, 1.08f, 0.04f, -10.0f, 0.0f, 1.12f, 0.96f, -6.0f, 0.14f, 0.42f, 0, 0 } },
    { { "XOR Teeth", "XOR loop edges with snappy register ticks and sharper motion." }, { 3, 20, 0.460f, 0.380f, 0.24f, 0.52f, 0.48f, 0.66f, 0.16f, 0.12f, 8, 1, 1, 2, 0.72f, 0.30f, 0.34f, 0.64f, 0.32f, 0.24f, 0.50f, 0.82f, 4, 0.74f, 0.135f, 6, 3, 0.064f, 0.56f, 0.48f, 1.00f, 0.08f, 22.0f, 0.0f, 1.12f, 0.90f, -6.0f, 0.42f, 0.18f, 0, 0 } },
    { { "Deep Choir Mask", "Very slow LOW/SINGLE masked choir with visible curve structure." }, { 3, 36, 0.240f, 0.145f, 0.12f, 0.08f, 0.30f, 0.18f, 0.30f, 0.12f, 6, 0, 1, 0, 0.18f, 0.60f, 0.54f, 0.38f, 0.26f, 0.24f, 0.40f, 0.82f, 2, 0.76f, 0.022f, 9, 1, 0.020f, 0.50f, 0.50f, 1.14f, 0.00f, 0.0f, -8.0f, 1.18f, 0.97f, -6.0f, 0.08f, 0.48f, 0, 0 } },
    { { "Spark Cells", "Sparse high-threshold pings with short register snap." }, { 3, 24, 0.610f, 0.082f, 0.10f, 0.62f, 0.24f, 0.42f, 0.28f, 0.16f, 5, 2, 1, 0, 0.88f, 0.18f, 0.30f, 0.70f, 0.22f, 0.26f, 0.30f, 0.92f, 4, 0.84f, 0.180f, 1, 3, 0.052f, 0.52f, 0.48f, 0.92f, 0.00f, 32.0f, 0.0f, 1.20f, 0.88f, -6.0f, 0.50f, 0.12f, 0, 0 } },
    { { "Filter Sweep Organ", "Triangle-B sweep into a resonant filter-run body." }, { 3, 30, 0.280f, 0.360f, 0.22f, 0.18f, 0.18f, 0.36f, 0.14f, 0.07f, 8, 1, 1, 0, 0.44f, 0.64f, 0.58f, 0.74f, 0.30f, 0.78f, 0.20f, 0.58f, 2, 0.46f, 0.040f, 3, 9, 0.020f, 0.42f, 0.36f, 0.86f, 0.00f, 0.0f, 0.0f, 0.92f, 0.96f, -6.0f, 0.12f, 0.34f, 0, 1 } },
    { { "Looped Register Choir", "Recirculating register tones with audible mask offsets." }, { 3, 26, 0.335f, 0.310f, 0.16f, 0.30f, 0.44f, 0.40f, 0.20f, 0.10f, 8, 1, 1, 1, 0.52f, 0.50f, 0.44f, 0.48f, 0.34f, 0.24f, 0.36f, 0.78f, 3, 0.66f, 0.070f, 5, 2, 0.046f, 0.58f, 0.54f, 0.96f, 0.02f, 12.0f, 0.0f, 1.04f, 0.92f, -6.0f, 0.22f, 0.36f, 0, 0 } },
    { { "Tiny Clockwork", "Fast double-range clock percussion through short registers." }, { 3, 40, 0.760f, 0.700f, 0.28f, 0.36f, 0.10f, 0.12f, 0.20f, 0.14f, 4, 2, 2, 0, 0.78f, 0.34f, 0.50f, 0.38f, 0.44f, 0.50f, 0.18f, 0.70f, 1, 0.56f, 0.150f, 6, 17, 0.082f, 0.50f, 0.60f, 0.94f, 0.00f, 26.0f, 0.0f, 1.12f, 0.88f, -6.0f, 0.36f, 0.10f, 0, 0 } },
    { { "Bass Rung Cloud", "LOW A/B sub-oscillator cloud with strong Rungler A." }, { 3, 32, 0.180f, 0.110f, 0.08f, 0.06f, 0.78f, 0.24f, 0.20f, 0.12f, 8, 0, 0, 0, 0.36f, 0.36f, 0.32f, 0.44f, 0.48f, 0.18f, 0.56f, 0.74f, 2, 0.66f, 0.040f, 2, 6, 0.078f, 0.52f, 0.44f, 1.02f, 0.12f, -6.0f, 0.0f, 1.18f, 0.98f, -6.0f, 0.10f, 0.52f, 0, 0 } },
    { { "PWM Glass", "Comparator glass with triangle-B color and a double B clock." }, { 3, 22, 0.520f, 0.430f, 0.16f, 0.22f, 0.12f, 0.10f, 0.12f, 0.10f, 6, 1, 2, 0, 0.12f, 0.22f, 0.62f, 0.78f, 0.36f, 0.42f, 0.22f, 0.88f, 4, 0.78f, 0.110f, 0, 5, 0.076f, 0.48f, 0.42f, 0.94f, 0.00f, 18.0f, 0.0f, 1.04f, 0.90f, -6.0f, 0.20f, 0.22f, 0, 1 } },
    { { "Breathing Crosswire", "Slow FM cross-coupled movement with low-range A." }, { 3, 28, 0.120f, 0.580f, 0.08f, 0.46f, 0.48f, 0.28f, 0.22f, 0.12f, 7, 0, 1, 0, 0.45f, 0.10f, 0.62f, 0.62f, 0.10f, 0.28f, 0.32f, 0.76f, 1, 0.62f, 0.036f, 8, 1, 0.024f, 0.54f, 0.54f, 0.94f, 0.00f, -18.0f, 0.0f, 1.08f, 0.96f, -6.0f, 0.12f, 0.44f, 0, 0 } },
    { { "XOR Dust Window", "Sparse LOW clocks with XOR patterns and wide mask holes." }, { 3, 18, 0.090f, 0.065f, 0.20f, 0.05f, 0.72f, 0.26f, 0.16f, 0.16f, 8, 0, 0, 2, 0.40f, 0.38f, 0.40f, 0.48f, 0.42f, 0.58f, 0.50f, 0.80f, 4, 0.88f, 0.095f, 11, 1, 0.088f, 0.56f, 0.48f, 1.08f, 0.00f, -24.0f, 0.0f, 1.12f, 0.98f, -6.0f, 0.28f, 0.20f, 0, 0 } },
    { { "Resonant Swarm", "Audio-rate oscillators into a rungler-driven resonant filter." }, { 3, 34, 0.640f, 0.330f, 0.22f, 0.22f, 0.12f, 0.18f, 0.24f, 0.12f, 8, 1, 1, 0, 0.22f, 0.22f, 0.66f, 0.88f, 0.48f, 0.42f, 0.20f, 0.70f, 2, 0.60f, 0.055f, 3, 9, 0.034f, 0.58f, 0.58f, 0.98f, 0.00f, 14.0f, 0.0f, 0.94f, 0.93f, -6.0f, 0.16f, 0.26f, 0, 0 } },
    { { "Snappy Register Choir", "Punchy rotating loop cells with an eight-step register." }, { 3, 24, 0.155f, 0.780f, 0.40f, 0.76f, 0.18f, 0.16f, 0.26f, 0.12f, 8, 1, 2, 1, 0.74f, 0.42f, 0.52f, 0.34f, 0.20f, 0.18f, 0.50f, 0.82f, 3, 0.74f, 0.150f, 4, 8, 0.064f, 0.58f, 0.54f, 1.00f, 0.00f, 22.0f, 0.0f, 1.18f, 0.91f, -6.0f, 0.46f, 0.18f, 0, 0 } },
    { { "Classic Narrow", "Compact classic register behavior with little spatial spread." }, { 3, 16, 0.290f, 0.300f, 0.24f, 0.28f, 0.22f, 0.20f, 0.08f, 0.04f, 8, 1, 1, 0, 0.50f, 0.62f, 0.44f, 0.36f, 0.18f, 0.16f, 0.20f, 0.36f, 2, 0.38f, 0.018f, 0, 9, 0.016f, 0.24f, 0.20f, 0.58f, 0.00f, 0.0f, 0.0f, 0.72f, 0.99f, -6.0f, 0.08f, 0.28f, 0, 0 } },
    { { "Fault Mask XOR", "Hot rungler XOR cells clipped into a wide unstable shell." }, { 3, 44, 0.470f, 0.230f, 0.14f, 0.70f, 0.58f, 0.86f, 0.44f, 0.20f, 8, 2, 1, 2, 0.90f, 0.16f, 0.34f, 0.72f, 0.30f, 0.18f, 0.32f, 0.96f, 4, 0.82f, 0.170f, 11, 4, 0.058f, 0.82f, 0.70f, 1.26f, 0.18f, 0.0f, 4.0f, 1.24f, 0.90f, -6.0f, 0.55f, 0.16f, 0, 0 } },
    { { "Run Filter Kites", "Filter run and sweep flying around a looped register." }, { 3, 30, 0.340f, 0.090f, 0.22f, 0.38f, 0.10f, 0.92f, 0.18f, 0.16f, 7, 1, 0, 1, 0.82f, 0.36f, 0.58f, 0.42f, 0.34f, 0.62f, 0.16f, 0.74f, 5, 0.70f, 0.085f, 10, 10, 0.072f, 0.62f, 0.58f, 0.90f, 0.00f, 30.0f, 0.0f, 1.05f, 0.95f, -6.0f, 0.24f, 0.36f, 0, 1 } },
    { { "Wide Backwash Organ", "Large slow choir, broad field, gentle sweep and mask breathing." }, { 3, 48, 0.360f, 0.500f, 0.12f, 0.52f, 0.08f, 0.08f, 0.36f, 0.08f, 8, 1, 1, 0, 0.12f, 0.24f, 0.54f, 0.56f, 0.22f, 0.24f, 0.46f, 0.64f, 1, 0.52f, 0.024f, 9, 9, 0.018f, 0.54f, 0.46f, 1.18f, 0.00f, 0.0f, 0.0f, 1.24f, 0.98f, -6.0f, 0.06f, 0.50f, 0, 0 } },
}};

inline AmbiWranglerPresetInfo ambiWranglerFactoryPresetInfo(uint32_t index)
{
    static constexpr std::array<AmbiWranglerPresetInfo, kAmbiWranglerListenerPresetCount> listenerPresets {{
        { "Register A: Open Circuit", "Reference state: the register runs without hearing the ambisonic field." },
        { "Register B: Field Written", "Matched state: eight HOA ears write bits and steer register-addressed motion." },
        { "Register C: Audio Clocked", "Matched state: field-written bits plus delayed directional audio at the comparator." },
    }};
    static constexpr std::array<AmbiWranglerPresetInfo, kAmbiWranglerCalmPresetCount> calmPresets {{
        { "Deep Current: Open", "One bounded circuit distributed over eight field nodes; a deep PWM line with slow register change." },
        { "Twin Low Orbit", "Two bounded circuits and sixteen nodes circling a quiet sub-audio clock." },
        { "Four Engine Dusk", "Four coherent circuits form a restrained low-register ambisonic pad." },
        { "Eight Engine Constellation", "Eight gently detuned circuits inhabit a sixty-four-node field without becoming a noise wall." },
        { "Deep Current: Settled", "The matched Deep Current patch with ambisonic homeostasis holding activity near a calm target." },
    }};
    index = std::min<uint32_t>(index, kAmbiWranglerFactoryPresetCount - 1u);
    if (index < kAmbiWranglerBasePresetCount) return kAmbiWranglerPresets[index].info;
    if (index < kAmbiWranglerCalmPresetStart) {
        return listenerPresets[index - kAmbiWranglerListenerPresetStart];
    }
    return calmPresets[index - kAmbiWranglerCalmPresetStart];
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

inline AmbiWranglerParams ambiWranglerCalmFactoryPreset(uint32_t index)
{
    index = std::min<uint32_t>(index, kAmbiWranglerCalmPresetCount - 1u);
    const bool settledDeepCurrent = index == 4u;
    const uint32_t voiceVariant = settledDeepCurrent ? 0u : index;

    AmbiWranglerParams p {};
    p.order = 3u;
    p.circuitLaw = AmbiWranglerCircuitLaw::Bounded;
    p.rungSize = 8u;
    p.rungLoop = 0u;
    p.threshold = 0.50f;
    p.pwmA = 0.50f;
    p.pwmB = 0.50f;
    p.rampA = 0.50f;
    p.rampB = 0.50f;
    p.inputA = 1u;
    p.inputB = 1u;
    p.voiceBreakpointsEnabled = false;
    p.topologyShape = 0u;
    p.topologyMotion = 9u;
    p.topologyRateHz = 0.008f;
    p.topologyAmount = 0.28f;
    p.topologyDepth = 0.16f;
    p.topologyScale = 0.92f;
    p.topologyCollapse = 0.0f;
    p.centerAzimuthDeg = 0.0f;
    p.centerElevationDeg = -4.0f;
    p.centerDistance = 1.02f;
    p.spatialFollow = 0.995f;
    p.outputGainDb = -6.0f;
    p.maskMode = 0u;
    p.maskDepth = 0.0f;
    p.maskRateHz = 0.012f;
    p.snap = 0.0f;
    p.snapDecay = 0.50f;
    p.listeningEnabled = 0u;
    p.pickupSet = AmbiWranglerPickupSet::Cube8;
    p.listenMode = AmbiWranglerListenMode::Trace;
    p.fieldWrite = 0.0f;
    p.registerMotion = 0.08f;
    p.fieldReturn = 0.0f;
    p.propagation = 0.18f;
    p.returnBypass = 1u;
    p.listenerResponse = AmbiWranglerListenerResponse::Settle;
    p.settleAmount = 0.78f;
    p.settleTarget = 0.22f;
    p.settleRecoverySeconds = 4.5f;

    switch (voiceVariant) {
    case 0u:
        p.voices = 8u;
        p.engines = 1u;
        p.rateA = 0.405f;
        p.rateB = 0.500f;
        p.rateModeA = 1u;
        p.rateModeB = 0u;
        p.fmAtoB = 0.040f;
        p.fmBtoA = 0.055f;
        p.runglerA = 0.120f;
        p.runglerB = 0.080f;
        p.spread = 0.008f;
        p.deviation = 0.005f;
        p.change = 0.10f;
        p.color = 0.96f;
        p.filter = 0.28f;
        p.resonance = 0.36f;
        p.filterRun = 0.10f;
        p.filterSweep = 0.04f;
        p.saturation = 0.055f;
        p.field = 0.34f;
        break;
    case 1u:
        p.voices = 16u;
        p.engines = 2u;
        p.rateA = 0.395f;
        p.rateB = 0.515f;
        p.rateModeA = 1u;
        p.rateModeB = 0u;
        p.fmAtoB = 0.060f;
        p.fmBtoA = 0.080f;
        p.runglerA = 0.150f;
        p.runglerB = 0.100f;
        p.spread = 0.025f;
        p.deviation = 0.012f;
        p.change = 0.14f;
        p.color = 0.94f;
        p.filter = 0.30f;
        p.resonance = 0.40f;
        p.filterRun = 0.12f;
        p.filterSweep = 0.06f;
        p.saturation = 0.070f;
        p.field = 0.46f;
        break;
    case 2u:
        p.voices = 32u;
        p.engines = 4u;
        p.rateA = 0.420f;
        p.rateB = 0.370f;
        p.rateModeA = 1u;
        p.rateModeB = 1u;
        p.fmAtoB = 0.070f;
        p.fmBtoA = 0.090f;
        p.runglerA = 0.120f;
        p.runglerB = 0.120f;
        p.spread = 0.045f;
        p.deviation = 0.018f;
        p.change = 0.18f;
        p.color = 0.92f;
        p.filter = 0.31f;
        p.resonance = 0.38f;
        p.filterRun = 0.14f;
        p.filterSweep = 0.08f;
        p.saturation = 0.085f;
        p.field = 0.58f;
        p.maskMode = 1u;
        p.maskDepth = 0.16f;
        p.maskRateHz = 0.018f;
        break;
    default:
        p.voices = 64u;
        p.engines = 8u;
        p.rateA = 0.430f;
        p.rateB = 0.345f;
        p.rateModeA = 1u;
        p.rateModeB = 1u;
        p.fmAtoB = 0.080f;
        p.fmBtoA = 0.100f;
        p.runglerA = 0.160f;
        p.runglerB = 0.120f;
        p.spread = 0.065f;
        p.deviation = 0.025f;
        p.change = 0.23f;
        p.color = 0.90f;
        p.filter = 0.32f;
        p.resonance = 0.36f;
        p.filterRun = 0.16f;
        p.filterSweep = 0.09f;
        p.saturation = 0.10f;
        p.field = 0.70f;
        p.maskMode = 2u;
        p.maskDepth = 0.22f;
        p.maskRateHz = 0.014f;
        break;
    }

    for (uint32_t voice = 0u; voice < kAmbiWranglerMaxVoices; ++voice) {
        p.bpRateA[voice] = 0.5f;
        p.bpRateB[voice] = 0.5f;
        p.bpFmAtoB[voice] = 0.5f;
        p.bpFmBtoA[voice] = 0.5f;
        p.bpRunglerA[voice] = 0.5f;
        p.bpRunglerB[voice] = 0.5f;
        p.bpFilter[voice] = 0.5f;
        p.bpThreshold[voice] = 0.5f;
        p.bpPwmA[voice] = 0.5f;
        p.bpPwmB[voice] = 0.5f;
        p.bpRampA[voice] = 0.5f;
        p.bpRampB[voice] = 0.5f;
        p.bpAmp[voice] = 1.0f;
    }

    p.listeningEnabled = settledDeepCurrent ? 1u : 0u;
    return p;
}

inline AmbiWranglerParams ambiWranglerFactoryPreset(uint32_t index)
{
    index = std::min<uint32_t>(index, kAmbiWranglerFactoryPresetCount - 1u);
    if (index >= kAmbiWranglerCalmPresetStart) {
        return ambiWranglerCalmFactoryPreset(index - kAmbiWranglerCalmPresetStart);
    }
    const bool listenerPreset = index >= kAmbiWranglerListenerPresetStart;
    const uint32_t sourceIndex = listenerPreset ? 0u : index;
    const auto& v = kAmbiWranglerPresets[sourceIndex].values;
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
    p.rateModeA = std::min<uint32_t>(2u, v.rateModeA);
    p.rateModeB = std::min<uint32_t>(2u, v.rateModeB);
    p.rungLoop = std::min<uint32_t>(2u, v.rungLoop);
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
    p.outputGainDb = v.outputGainDb;
    p.pwmA = std::clamp(0.46f + ambiWranglerPresetSigned(sourceIndex * 331u + 17u) * 0.08f, 0.0f, 1.0f);
    p.pwmB = std::clamp(0.54f + ambiWranglerPresetSigned(sourceIndex * 337u + 19u) * 0.08f, 0.0f, 1.0f);
    p.rampA = std::clamp(0.50f + ambiWranglerPresetSigned(sourceIndex * 347u + 23u) * 0.16f, 0.0f, 1.0f);
    p.rampB = std::clamp(0.50f + ambiWranglerPresetSigned(sourceIndex * 349u + 29u) * 0.16f, 0.0f, 1.0f);
    p.inputA = std::min<uint32_t>(1u, v.inputA);
    p.inputB = std::min<uint32_t>(1u, v.inputB);
    p.snap = v.snap;
    p.snapDecay = v.snapDecay;
    p.circuitLaw = AmbiWranglerCircuitLaw::Legacy;
    p.engines = p.voices;
    p.change = 1.0f;
    p.listenerResponse = AmbiWranglerListenerResponse::Write;
    ambiWranglerFillPresetBreakpoints(p, sourceIndex);
    if (listenerPreset) {
        const uint32_t variant = index - kAmbiWranglerListenerPresetStart;
        p.listeningEnabled = variant == 0u ? 0u : 1u;
        p.pickupSet = AmbiWranglerPickupSet::Cube8;
        p.listenMode = AmbiWranglerListenMode::Ring;
        p.fieldWrite = 0.32f;
        p.registerMotion = 0.20f;
        p.fieldReturn = 0.18f;
        p.propagation = 0.24f;
        p.returnBypass = variant == 1u ? 1u : 0u;
    }
    return p;
}

} // namespace s3g
