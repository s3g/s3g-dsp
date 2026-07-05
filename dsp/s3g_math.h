#pragma once

#include <algorithm>
#include <cmath>

namespace s3g {

constexpr float kPi = 3.14159265358979323846f;

inline float clamp(float v, float lo, float hi)
{
    return std::max(lo, std::min(hi, v));
}

inline float dbToGain(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

inline float gainToDb(float gain)
{
    return gain > 0.0f ? 20.0f * std::log10(gain) : -120.0f;
}

inline float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

} // namespace s3g
