#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kMcToStereoMaxInputChannels = 128;

enum class McStereoLayout : uint32_t {
    RingProjection = 0,
    LinearLeftRight = 1,
    OddEvenStereo = 2,
    CenterOut = 3,
    PairPreserving = 4,
    SphereProjection = 5,
    HemisphereProjection = 6,
    CubeProjection = 7,
};

enum class McStereoAutogain : uint32_t {
    Off = 0,
    PowerSqrtN = 1,
    EnergySum = 2,
};

struct McStereoParams {
    uint32_t inputChannels = 8;
    float widthPercent = 100.0f;
    float rotationDegrees = 0.0f;
    McStereoAutogain autogain = McStereoAutogain::PowerSqrtN;
    float outputGainDb = 0.0f;
    McStereoLayout layout = McStereoLayout::RingProjection;
    float layoutWeightPercent = 100.0f;
    float attenuation3dPercent = 45.0f;
    float distance3dPercent = 100.0f;
};

struct McStereoPosition {
    float azimuthDegrees = 0.0f;
    float elevationDegrees = 0.0f;
};

struct McStereoPanGain {
    float pan = 0.0f;
    float gain = 1.0f;
};

struct McStereoChannelGains {
    float left = 0.0f;
    float right = 0.0f;
};

inline float clampf(float value, float lo, float hi)
{
    return std::max(lo, std::min(hi, value));
}

inline uint32_t clampInputChannels(uint32_t channels)
{
    return std::max<uint32_t>(2u, std::min<uint32_t>(kMcToStereoMaxInputChannels, channels));
}

inline float mcStereoDbToGain(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

inline float wrapDegrees(float degrees)
{
    float wrapped = degrees - std::floor(degrees / 360.0f) * 360.0f;
    if (wrapped > 180.0f) {
        wrapped -= 360.0f;
    }
    return wrapped;
}

inline McStereoPosition ringPosition(uint32_t index, uint32_t count, float startAzimuth, float elevation)
{
    const float step = 360.0f / static_cast<float>(std::max<uint32_t>(1u, count));
    return { wrapDegrees(startAzimuth + static_cast<float>(index) * step), elevation };
}

inline McStereoPosition cubePosition(uint32_t index)
{
    const uint32_t slot = index % 8u;
    static constexpr std::array<float, 8> kAzimuth {
        45.0f, 135.0f, -135.0f, -45.0f, 45.0f, 135.0f, -135.0f, -45.0f
    };
    return { kAzimuth[slot], slot < 4u ? -35.2644f : 35.2644f };
}

inline McStereoPosition dodecaPosition(uint32_t index)
{
    static constexpr std::array<McStereoPosition, 12> kPoints {{
        { -31.717474f, 0.0f },
        { 31.717474f, 0.0f },
        { -148.282526f, 0.0f },
        { 148.282526f, 0.0f },
        { 180.0f, 58.282526f },
        { 0.0f, 58.282526f },
        { 180.0f, -58.282526f },
        { 0.0f, -58.282526f },
        { 90.0f, -31.717474f },
        { 90.0f, 31.717474f },
        { -90.0f, -31.717474f },
        { -90.0f, 31.717474f },
    }};
    return kPoints[index % 12u];
}

inline McStereoPosition dome24Position(uint32_t index)
{
    const uint32_t slot = index % 24u;
    if (slot < 12u) {
        return ringPosition(slot, 12u, 30.0f, 0.0f);
    }
    if (slot < 20u) {
        return ringPosition(slot - 12u, 8u, 45.0f, 32.0f);
    }
    return ringPosition(slot - 20u, 4u, 90.0f, 66.6f);
}

inline McStereoPosition spherePosition(uint32_t index, uint32_t count)
{
    if (count == 8u) {
        return cubePosition(index);
    }
    if (count == 12u) {
        return dodecaPosition(index);
    }
    if (count == 16u) {
        return index < 8u ? ringPosition(index, 8u, 30.0f, -32.0f)
                          : ringPosition(index - 8u, 8u, 30.0f, 32.0f);
    }
    if (count == 24u) {
        return dome24Position(index);
    }
    if (count == 25u) {
        return index < 24u ? dome24Position(index) : McStereoPosition { 0.0f, 90.0f };
    }

    const float frac = (static_cast<float>(index) + 0.5f) / static_cast<float>(std::max<uint32_t>(1u, count));
    const float z = 1.0f - 2.0f * frac;
    const float el = std::asin(clampf(z, -1.0f, 1.0f)) * 180.0f / static_cast<float>(M_PI);
    return { wrapDegrees(30.0f + static_cast<float>(index) * 137.507764f), el };
}

inline McStereoPosition hemispherePosition(uint32_t index, uint32_t count)
{
    if (count == 16u) {
        return index < 8u ? ringPosition(index, 8u, 30.0f, 0.0f)
                          : ringPosition(index - 8u, 8u, 30.0f, 45.0f);
    }
    if (count == 24u || count == 25u) {
        return dome24Position(index);
    }

    const float frac = (static_cast<float>(index) + 0.5f) / static_cast<float>(std::max<uint32_t>(1u, count));
    return {
        wrapDegrees(30.0f + static_cast<float>(index) * 360.0f / static_cast<float>(std::max<uint32_t>(1u, count))),
        65.0f * frac
    };
}

inline McStereoPanGain projectionPanGain(McStereoLayout layout, uint32_t index, uint32_t count, float width, float rotationRadians, float layoutWeight, float attenuation3d, float distance3d)
{
    McStereoPosition pos {};
    if (layout == McStereoLayout::SphereProjection) {
        pos = spherePosition(index, count);
    } else if (layout == McStereoLayout::HemisphereProjection) {
        pos = hemispherePosition(index, count);
    } else {
        pos = cubePosition(index);
    }

    const float azr = pos.azimuthDegrees * static_cast<float>(M_PI) / 180.0f + rotationRadians;
    const float frontness = (std::cos(azr) + 1.0f) * 0.5f;
    const float rear = 1.0f - frontness;
    const float heightness = std::abs(pos.elevationDegrees) / 90.0f;
    const float atten = attenuation3d * layoutWeight;

    float gain = 1.0f;
    if (layout == McStereoLayout::HemisphereProjection) {
        gain = 1.0f - atten * (heightness * 0.55f + rear * 0.22f);
    } else if (layout == McStereoLayout::CubeProjection) {
        gain = 1.0f - atten * (heightness * 0.38f + rear * 0.30f);
    } else {
        gain = 1.0f - atten * (heightness * 0.42f + rear * 0.24f);
    }

    const float heightSpread = 1.0f + heightness * (distance3d - 1.0f);
    const float pan = std::sin(azr) * width * heightSpread * (frontness + (1.0f - frontness) * layoutWeight);
    return { clampf(pan, -1.0f, 1.0f), clampf(gain, 0.15f, 1.0f) };
}

inline McStereoPanGain panGainForChannel(uint32_t index, uint32_t count, const McStereoParams& params)
{
    const uint32_t chanCount = clampInputChannels(count);
    const float width = std::max(0.0f, params.widthPercent / 100.0f);
    const float rotation = params.rotationDegrees * static_cast<float>(M_PI) / 180.0f;
    const float layoutWeight = clampf(params.layoutWeightPercent / 100.0f, 0.0f, 1.0f);
    const float attenuation3d = clampf(params.attenuation3dPercent / 100.0f, 0.0f, 1.0f);
    const float distance3d = clampf(params.distance3dPercent / 100.0f, 0.0f, 2.0f);
    const float angle = rotation + (2.0f * static_cast<float>(M_PI) * static_cast<float>(index)) / static_cast<float>(chanCount);

    float pan = 0.0f;
    switch (params.layout) {
    case McStereoLayout::RingProjection: {
        const float frontness = (std::cos(angle) + 1.0f) * 0.5f;
        const float rearFold = frontness + (1.0f - frontness) * layoutWeight;
        pan = std::sin(angle) * width * rearFold;
        return { clampf(pan, -1.0f, 1.0f), 1.0f };
    }
    case McStereoLayout::LinearLeftRight: {
        const float rotatedIndex = std::fmod(static_cast<float>(index) + (params.rotationDegrees / 360.0f) * static_cast<float>(chanCount) + static_cast<float>(chanCount), static_cast<float>(chanCount));
        pan = -1.0f + (2.0f * rotatedIndex) / static_cast<float>(chanCount - 1u);
        pan *= width * layoutWeight;
        return { clampf(pan, -1.0f, 1.0f), 1.0f };
    }
    case McStereoLayout::OddEvenStereo:
        pan = (index % 2u == 0u ? -1.0f : 1.0f) * width * layoutWeight;
        return { clampf(pan, -1.0f, 1.0f), 1.0f };
    case McStereoLayout::CenterOut: {
        const float center = static_cast<float>(chanCount - 1u) * 0.5f;
        const float offset = static_cast<float>(index) - center;
        const float rank = std::floor(std::abs(offset) + 0.5f);
        const float side = offset < 0.0f ? -1.0f : 1.0f;
        pan = rank <= 0.0f ? 0.0f : side * (rank / std::max(1.0f, center)) * width * layoutWeight;
        return { clampf(pan, -1.0f, 1.0f), 1.0f };
    }
    case McStereoLayout::PairPreserving: {
        const uint32_t pair = index / 2u;
        const uint32_t pairCount = std::max<uint32_t>(1u, (chanCount + 1u) / 2u);
        const float pairPos = pairCount <= 1u ? 0.0f : -1.0f + (2.0f * static_cast<float>(pair)) / static_cast<float>(pairCount - 1u);
        const float pairWidth = index % 2u == 0u ? -0.16f : 0.16f;
        pan = (pairPos * 0.82f + pairWidth) * width * layoutWeight;
        return { clampf(pan, -1.0f, 1.0f), 1.0f };
    }
    case McStereoLayout::SphereProjection:
    case McStereoLayout::HemisphereProjection:
    case McStereoLayout::CubeProjection:
        return projectionPanGain(params.layout, index, chanCount, width, rotation, layoutWeight, attenuation3d, distance3d);
    }

    return { 0.0f, 1.0f };
}

inline float autogainForMode(McStereoAutogain mode, uint32_t inputChannels)
{
    const float n = static_cast<float>(std::max<uint32_t>(1u, inputChannels));
    if (mode == McStereoAutogain::PowerSqrtN) {
        return 1.0f / std::sqrt(n);
    }
    if (mode == McStereoAutogain::EnergySum) {
        return 1.0f / n;
    }
    return 1.0f;
}

inline void makeMcToStereoGains(McStereoChannelGains* gains, uint32_t availableInputChannels, const McStereoParams& params)
{
    if (!gains) {
        return;
    }
    for (uint32_t ch = 0; ch < kMcToStereoMaxInputChannels; ++ch) {
        gains[ch] = {};
    }

    const uint32_t count = std::min(clampInputChannels(params.inputChannels), std::min<uint32_t>(availableInputChannels, kMcToStereoMaxInputChannels));
    const float outputGain = autogainForMode(params.autogain, count) * mcStereoDbToGain(params.outputGainDb);
    for (uint32_t ch = 0; ch < count; ++ch) {
        const auto panGain = panGainForChannel(ch, count, params);
        const float pan = clampf(panGain.pan, -1.0f, 1.0f);
        const float theta = (pan + 1.0f) * static_cast<float>(M_PI) / 4.0f;
        const float gain = panGain.gain * outputGain;
        gains[ch].left = std::cos(theta) * gain;
        gains[ch].right = std::sin(theta) * gain;
    }
}

inline void processMcToStereoFrame(const float* input, uint32_t availableInputChannels, float* outputStereo, const McStereoParams& params)
{
    const uint32_t count = std::min(clampInputChannels(params.inputChannels), std::min<uint32_t>(availableInputChannels, kMcToStereoMaxInputChannels));
    float left = 0.0f;
    float right = 0.0f;
    for (uint32_t ch = 0; ch < count; ++ch) {
        const auto panGain = panGainForChannel(ch, count, params);
        const float pan = clampf(panGain.pan, -1.0f, 1.0f);
        const float theta = (pan + 1.0f) * static_cast<float>(M_PI) / 4.0f;
        const float x = input[ch] * panGain.gain;
        left += x * std::cos(theta);
        right += x * std::sin(theta);
    }

    const float gain = autogainForMode(params.autogain, count) * mcStereoDbToGain(params.outputGainDb);
    outputStereo[0] = left * gain;
    outputStereo[1] = right * gain;
}

} // namespace s3g
