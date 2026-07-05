#pragma once

#include "s3g_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t k3OaChannels = 16;
constexpr uint32_t k3OafxVirtualSpeakers = 24;
constexpr uint32_t k3OafxBusChannels = 72;

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline constexpr std::array<Vec3, k3OafxVirtualSpeakers> k3OafxPoints {{
    { 0.285652275f, 0.000000000f, 0.958333333f },
    { -0.356977173f, 0.327020333f, 0.875000000f },
    { 0.053413032f, -0.608613947f, 0.791666667f },
    { 0.429483666f, 0.560185389f, 0.708333333f },
    { -0.768691718f, -0.135970741f, 0.625000000f },
    { 0.709255111f, -0.451170045f, 0.541666667f },
    { -0.230731212f, 0.858308606f, 0.458333333f },
    { -0.427272247f, -0.822686712f, 0.375000000f },
    { 0.898479629f, 0.328123319f, 0.291666667f },
    { -0.904063458f, 0.373184253f, 0.208333333f },
    { 0.420521661f, -0.898630365f, 0.125000000f },
    { 0.299023957f, 0.953335493f, 0.041666667f },
    { -0.864459832f, -0.500972143f, -0.041666667f },
    { 0.969015453f, -0.213035329f, -0.125000000f },
    { -0.562509872f, 0.800112409f, -0.208333333f },
    { -0.122923048f, -0.948588678f, -0.291666667f },
    { 0.708848590f, 0.597418342f, -0.375000000f },
    { -0.888021405f, 0.036722473f, -0.458333333f },
    { 0.595837310f, -0.592937705f, -0.541666667f },
    { -0.036058186f, 0.779791515f, -0.625000000f },
    { -0.452262552f, -0.541961689f, -0.708333333f },
    { 0.605497091f, 0.081468777f, -0.791666667f },
    { -0.397396334f, 0.276498018f, -0.875000000f },
    { 0.062695352f, -0.278687127f, -0.958333333f },
}};

struct AedMaskParams {
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float smoothing = 0.20f;
    float width = 0.75f;
    float focus = 0.05f;
    float level = 1.0f;
    float floor = 0.02f;
    float rearReject = 1.0f;
    float energyComp = 0.65f;
    float gamma = 1.25f;
};

struct MixerParams {
    float wetTrim = 1.0f;
    float dryTrim = 0.65f;
    float outputTrim = 0.90f;
    float maskContrast = 0.10f;
    float maskCeiling = 0.92f;
    float duckCurve = 0.0f;
    float wetLimiter = 0.15f;
    float attackLag = 0.15f;
    float releaseLag = 0.35f;
    bool insertDuck = true;
    bool useIncomingMask = true;
};

struct AedMaskState {
    Vec3 target { 1.0f, 0.0f, 0.0f };
};

struct MixerState {
    std::array<float, k3OafxVirtualSpeakers> maskSmooth {};
};

inline float clamp01f(float value)
{
    return clamp(value, 0.0f, 1.0f);
}

inline float safePow(float value, float exponent)
{
    return std::pow(std::max(0.0f, value), std::max(0.0001f, exponent));
}

inline Vec3 directionFromAed(float azimuthDeg, float elevationDeg)
{
    const float az = azimuthDeg * kPi / 180.0f;
    const float el = elevationDeg * kPi / 180.0f;
    const float ce = std::cos(el);
    return { ce * std::cos(az), ce * std::sin(az), std::sin(el) };
}

inline Vec3 normalize(Vec3 v)
{
    const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len < 0.000001f) {
        return { 1.0f, 0.0f, 0.0f };
    }
    return { v.x / len, v.y / len, v.z / len };
}

inline Vec3 smoothDirection(AedMaskState& state, const AedMaskParams& params)
{
    const Vec3 target = directionFromAed(params.azimuthDeg, params.elevationDeg);
    const float sm = 0.25f * std::pow(1.0f - clamp01f(params.smoothing), 2.0f) + 0.0005f;
    state.target.x += sm * (target.x - state.target.x);
    state.target.y += sm * (target.y - state.target.y);
    state.target.z += sm * (target.z - state.target.z);
    state.target = normalize(state.target);
    return state.target;
}

inline std::array<float, k3OaChannels> acnSn3dBasis(Vec3 p)
{
    p = normalize(p);
    const float x = p.x;
    const float y = p.y;
    const float z = p.z;
    return {
        1.0f,
        y,
        z,
        x,
        1.224744871f * x * y,
        1.224744871f * y * z,
        1.5f * z * z - 0.5f,
        1.224744871f * x * z,
        0.612372435f * (x * x - y * y),
        0.790569415f * y * (3.0f * x * x - y * y),
        2.371708245f * x * y * z,
        0.790569415f * y * (5.0f * z * z - 1.0f),
        0.5f * z * (5.0f * z * z - 3.0f),
        0.790569415f * x * (5.0f * z * z - 1.0f),
        1.185854122f * z * (x * x - y * y),
        0.790569415f * x * (x * x - 3.0f * y * y),
    };
}

inline float decodeNormForPoint(Vec3 point)
{
    const auto basis = acnSn3dBasis(point);
    float sum = 0.0f;
    for (float value : basis) {
        sum += value * value;
    }
    return 1.0f / std::sqrt(std::max(0.000001f, sum));
}

inline void decode3OaToVirtual24(const float* input3Oa, float* output24)
{
    for (uint32_t spk = 0; spk < k3OafxVirtualSpeakers; ++spk) {
        const auto basis = acnSn3dBasis(k3OafxPoints[spk]);
        float value = 0.0f;
        for (uint32_t ch = 0; ch < k3OaChannels; ++ch) {
            value += input3Oa[ch] * basis[ch];
        }
        output24[spk] = value * decodeNormForPoint(k3OafxPoints[spk]);
    }
}

inline void encodeVirtual24To3Oa(const float* input24, float* output3Oa)
{
    std::fill(output3Oa, output3Oa + k3OaChannels, 0.0f);
    constexpr float scale = 1.0f / static_cast<float>(k3OafxVirtualSpeakers);
    for (uint32_t spk = 0; spk < k3OafxVirtualSpeakers; ++spk) {
        const auto basis = acnSn3dBasis(k3OafxPoints[spk]);
        const float value = input24[spk] * scale;
        for (uint32_t ch = 0; ch < k3OaChannels; ++ch) {
            output3Oa[ch] += value * basis[ch];
        }
    }
}

inline float softSat(float value)
{
    return value / std::sqrt(1.0f + value * value);
}

inline float computeMaskValue(Vec3 speaker, Vec3 target, const AedMaskParams& params)
{
    const float dot = clamp(speaker.x * target.x + speaker.y * target.y + speaker.z * target.z, -1.0f, 1.0f);
    const float width = clamp01f(params.width);
    const float focus = clamp01f(params.focus);
    const float rearReject = clamp01f(params.rearReject);
    const float rejectShape = std::pow(rearReject, 0.75f);
    const float coneEdge = rejectShape * (0.35f + std::pow(1.0f - width, 1.15f) * 0.55f);
    const float beamPow = 1.0f + std::pow(1.0f - width, 1.5f) * 20.0f + focus * 20.0f;
    const float steep = 1.0f + focus * 4.0f;
    const float f = clamp01f((dot - coneEdge) / std::max(0.000001f, 1.0f - coneEdge));
    return clamp01f(std::pow(clamp01f(params.floor) * f + (1.0f - clamp01f(params.floor)) * std::pow(f, beamPow), steep));
}

inline void computeMask(Vec3 target, const AedMaskParams& params, float* maskOut)
{
    float powSum = 0.0f;
    for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) {
        const float m = computeMaskValue(k3OafxPoints[i], target, params);
        maskOut[i] = m;
        powSum += m * m;
    }
    const float comp = (1.0f - clamp01f(params.energyComp)) + clamp01f(params.energyComp) / std::max(1.0f, std::sqrt(powSum));
    for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) {
        maskOut[i] = clamp01f(safePow(maskOut[i], params.gamma) * comp * clamp01f(params.level));
    }
}

inline void process3OafxSendFrame(const float* input3Oa, float* output72, AedMaskState& state, const AedMaskParams& params, bool dryCopy, bool writeMask)
{
    float decoded[k3OafxVirtualSpeakers] {};
    float mask[k3OafxVirtualSpeakers] {};
    decode3OaToVirtual24(input3Oa, decoded);
    computeMask(smoothDirection(state, params), params, mask);
    for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) {
        output72[i] = decoded[i] * mask[i];
        output72[i + 24] = dryCopy ? decoded[i] : 0.0f;
        output72[i + 48] = writeMask ? mask[i] : mask[i];
    }
}

inline void process3OafxReturnFrame(const float* input72, float* output3Oa, AedMaskState& maskState, MixerState& mixerState, const AedMaskParams& independentMask, const MixerParams& mixer)
{
    float mixed24[k3OafxVirtualSpeakers] {};
    float localMask[k3OafxVirtualSpeakers] {};
    computeMask(smoothDirection(maskState, independentMask), independentMask, localMask);

    const float attack = 0.35f * std::pow(1.0f - clamp01f(mixer.attackLag), 2.0f) + 0.0005f;
    const float release = 0.20f * std::pow(1.0f - clamp01f(mixer.releaseLag), 2.0f) + 0.0002f;
    const float contrast = 1.0f + clamp01f(mixer.maskContrast) * 2.5f;
    const float ceiling = clamp(mixer.maskCeiling, 0.5f, 1.0f);
    const float duckPow = 1.0f - clamp01f(mixer.duckCurve) * 0.4f;

    for (uint32_t i = 0; i < k3OafxVirtualSpeakers; ++i) {
        const float wet = input72[i];
        const float dry = input72[i + 24];
        const float incomingMask = clamp01f(input72[i + 48]);
        const float mask = mixer.useIncomingMask ? incomingMask : localMask[i];
        const float raw = std::min(ceiling, safePow(mask, contrast));
        const float lag = raw > mixerState.maskSmooth[i] ? attack : release;
        mixerState.maskSmooth[i] += lag * (raw - mixerState.maskSmooth[i]);
        const float smoothed = safePow(clamp(mixerState.maskSmooth[i], 0.0f, ceiling), duckPow);
        const float activeDuck = smoothed * clamp01f(mixer.wetTrim);
        const float dryGain = mixer.insertDuck ? std::cos(activeDuck * kPi * 0.5f) : 1.0f;
        const float wetSum = wet * clamp01f(mixer.wetTrim);
        const float wetSafe = (1.0f - clamp01f(mixer.wetLimiter)) * wetSum + clamp01f(mixer.wetLimiter) * softSat(wetSum);
        mixed24[i] = (dry * clamp01f(mixer.dryTrim) * dryGain + wetSafe) * clamp01f(mixer.outputTrim);
    }

    encodeVirtual24To3Oa(mixed24, output3Oa);
}

} // namespace s3g
