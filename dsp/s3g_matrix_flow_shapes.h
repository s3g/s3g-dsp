#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace s3g {

enum class MatrixFlowShape : uint32_t {
    Flow = 0,
    Pulse = 1,
    Chase = 2,
    Swirl = 3,
    Scatter = 4,
    Hold = 5,
};

inline MatrixFlowShape matrixFlowShapeFromIndex(uint32_t index)
{
    return static_cast<MatrixFlowShape>(std::min<uint32_t>(index, static_cast<uint32_t>(MatrixFlowShape::Hold)));
}

inline const char* matrixFlowShapeName(MatrixFlowShape shape)
{
    switch (shape) {
    case MatrixFlowShape::Flow: return "FLOW";
    case MatrixFlowShape::Pulse: return "PULSE";
    case MatrixFlowShape::Chase: return "CHASE";
    case MatrixFlowShape::Swirl: return "SWIRL";
    case MatrixFlowShape::Scatter: return "SCAT";
    case MatrixFlowShape::Hold: return "HOLD";
    default: return "FLOW";
    }
}

inline float matrixFlowHash01(uint32_t src, uint32_t dst, uint32_t seed)
{
    uint32_t h = 2166136261u;
    h = (h ^ (src + 1u)) * 16777619u;
    h = (h ^ ((dst + 1u) * 374761393u)) * 16777619u;
    h = (h ^ (seed * 668265263u)) * 16777619u;
    h ^= h >> 13u;
    h *= 1274126177u;
    h ^= h >> 16u;
    return static_cast<float>(h & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

inline float matrixFlowRingWeight(uint32_t groups, uint32_t dst, float center, float width)
{
    if (groups <= 1u) {
        return 1.0f;
    }
    float d = std::fabs(static_cast<float>(dst) - center);
    d = std::min(d, static_cast<float>(groups) - d);
    return std::exp(-(d * d) / std::max(0.001f, width * width));
}

inline float matrixFlowPulse(float phase)
{
    return 0.15f + 0.85f * (0.5f + 0.5f * std::sin(phase * 6.28318530718f));
}

} // namespace s3g
