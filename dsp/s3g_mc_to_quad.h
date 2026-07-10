#pragma once

#include "s3g_mc_to_stereo.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kMcToQuadOutputChannels = 4;

struct McQuadChannelGains {
    float left = 0.0f;
    float right = 0.0f;
    float rightBack = 0.0f;
    float leftBack = 0.0f;
};

using McQuadParams = McStereoParams;

inline float angularDistanceRadians(float a, float b)
{
    float d = a - b;
    while (d > static_cast<float>(M_PI)) d -= 2.0f * static_cast<float>(M_PI);
    while (d < -static_cast<float>(M_PI)) d += 2.0f * static_cast<float>(M_PI);
    return std::abs(d);
}

inline McStereoPosition mcQuadPositionForChannel(uint32_t index, uint32_t count, const McQuadParams& params)
{
    const uint32_t chanCount = clampInputChannels(count);
    switch (params.layout) {
    case McStereoLayout::SphereProjection:
        return spherePosition(index, chanCount);
    case McStereoLayout::HemisphereProjection:
        return hemispherePosition(index, chanCount);
    case McStereoLayout::CubeProjection:
        return cubePosition(index);
    case McStereoLayout::OddEvenStereo:
        return { index % 2u == 0u ? -45.0f : 45.0f, 0.0f };
    case McStereoLayout::LinearLeftRight: {
        const float rotatedIndex = std::fmod(static_cast<float>(index) + (params.rotationDegrees / 360.0f) * static_cast<float>(chanCount) + static_cast<float>(chanCount), static_cast<float>(chanCount));
        const float t = chanCount <= 1u ? 0.5f : rotatedIndex / static_cast<float>(chanCount - 1u);
        return { -45.0f + 90.0f * t, 0.0f };
    }
    case McStereoLayout::CenterOut: {
        const float center = static_cast<float>(chanCount - 1u) * 0.5f;
        const float offset = static_cast<float>(index) - center;
        const float side = offset < 0.0f ? -1.0f : 1.0f;
        const float rank = center <= 0.0f ? 0.0f : std::abs(offset) / center;
        return { side * 135.0f * rank, 0.0f };
    }
    case McStereoLayout::PairPreserving: {
        const uint32_t pair = index / 2u;
        const uint32_t pairCount = std::max<uint32_t>(1u, (chanCount + 1u) / 2u);
        const float angle = 2.0f * static_cast<float>(M_PI) * static_cast<float>(pair) / static_cast<float>(pairCount);
        const float offset = index % 2u == 0u ? -12.0f : 12.0f;
        return { wrapDegrees(angle * 180.0f / static_cast<float>(M_PI) + offset), 0.0f };
    }
    case McStereoLayout::RingProjection:
    default:
        return ringPosition(index, chanCount, -45.0f, 0.0f);
    }
}

inline McQuadChannelGains quadGainsForChannel(uint32_t index, uint32_t count, const McQuadParams& params)
{
    const uint32_t chanCount = clampInputChannels(count);
    const float rotation = params.rotationDegrees * static_cast<float>(M_PI) / 180.0f;
    const float layoutWeight = clampf(params.layoutWeightPercent / 100.0f, 0.0f, 1.0f);
    const float width = std::max(0.0f, params.widthPercent / 100.0f);
    const float attenuation3d = clampf(params.attenuation3dPercent / 100.0f, 0.0f, 1.0f);
    const float distance3d = clampf(params.distance3dPercent / 100.0f, 0.0f, 2.0f);
    const auto pos = mcQuadPositionForChannel(index, chanCount, params);
    const float az = pos.azimuthDegrees * static_cast<float>(M_PI) / 180.0f + rotation;
    const float height = std::abs(pos.elevationDegrees) / 90.0f;
    const float frontness = (std::cos(az) + 1.0f) * 0.5f;
    const float rear = 1.0f - frontness;
    const float layoutAtten = 1.0f - attenuation3d * layoutWeight * (height * 0.40f + rear * 0.10f);
    const float spread = std::max(0.25f, width * (1.0f + height * (distance3d - 1.0f) * 0.35f));

    static constexpr std::array<float, kMcToQuadOutputChannels> kSpeakerAngles {
        -45.0f * static_cast<float>(M_PI) / 180.0f,  // L
         45.0f * static_cast<float>(M_PI) / 180.0f,  // R
        135.0f * static_cast<float>(M_PI) / 180.0f,  // RB
       -135.0f * static_cast<float>(M_PI) / 180.0f   // LB
    };

    std::array<float, kMcToQuadOutputChannels> g {};
    float power = 0.000001f;
    for (uint32_t i = 0; i < kMcToQuadOutputChannels; ++i) {
        const float d = angularDistanceRadians(az, kSpeakerAngles[i]);
        const float x = clampf(1.0f - d / (static_cast<float>(M_PI) * 0.55f * spread), 0.0f, 1.0f);
        g[i] = x * x * (3.0f - 2.0f * x);
        power += g[i] * g[i];
    }
    const float norm = layoutAtten / std::sqrt(power);
    return { g[0] * norm, g[1] * norm, g[2] * norm, g[3] * norm };
}

inline void makeMcToQuadGains(McQuadChannelGains* gains, uint32_t availableInputChannels, const McQuadParams& params)
{
    if (!gains) return;
    for (uint32_t ch = 0; ch < kMcToStereoMaxInputChannels; ++ch) gains[ch] = {};
    const uint32_t count = std::min(clampInputChannels(params.inputChannels), std::min<uint32_t>(availableInputChannels, kMcToStereoMaxInputChannels));
    const float outputGain = autogainForMode(params.autogain, count) * mcStereoDbToGain(params.outputGainDb);
    for (uint32_t ch = 0; ch < count; ++ch) {
        auto g = quadGainsForChannel(ch, count, params);
        gains[ch] = { g.left * outputGain, g.right * outputGain, g.rightBack * outputGain, g.leftBack * outputGain };
    }
}

inline void processMcToQuadFrame(const float* input, uint32_t availableInputChannels, float* outputQuad, const McQuadParams& params)
{
    const uint32_t count = std::min(clampInputChannels(params.inputChannels), std::min<uint32_t>(availableInputChannels, kMcToStereoMaxInputChannels));
    const float outputGain = autogainForMode(params.autogain, count) * mcStereoDbToGain(params.outputGainDb);
    float l = 0.0f;
    float r = 0.0f;
    float rb = 0.0f;
    float lb = 0.0f;
    for (uint32_t ch = 0; ch < count; ++ch) {
        const auto g = quadGainsForChannel(ch, count, params);
        const float x = input[ch] * outputGain;
        l += x * g.left;
        r += x * g.right;
        rb += x * g.rightBack;
        lb += x * g.leftBack;
    }
    outputQuad[0] = l;
    outputQuad[1] = r;
    outputQuad[2] = rb;
    outputQuad[3] = lb;
}

} // namespace s3g
