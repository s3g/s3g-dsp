#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace s3g::sample {
enum class GrainEnvelope : uint8_t { Parzen, Sine, Hann, Triangle, Gaussian };
inline float grainWindow(GrainEnvelope envelope, float phase, float skew) noexcept
{
    constexpr float pi = 3.14159265358979323846f;
    phase = std::clamp(phase, 0.0f, 1.0f);
    const float peak = 0.5f + 0.4f * std::clamp(skew, -1.0f, 1.0f);
    phase = phase <= peak ? 0.5f * phase / peak
        : 0.5f + 0.5f * (phase - peak) / (1.0f - peak);
    switch (envelope) {
    case GrainEnvelope::Sine: return std::sin(pi * phase);
    case GrainEnvelope::Hann: return 0.5f - 0.5f * std::cos(2.0f * pi * phase);
    case GrainEnvelope::Triangle: return 1.0f - std::abs(2.0f * phase - 1.0f);
    case GrainEnvelope::Gaussian: {
        const float value = (phase - 0.5f) / 0.18f;
        return std::exp(-0.5f * value * value);
    }
    default: {
        const float triangle = 1.0f - std::abs(2.0f * phase - 1.0f);
        return triangle * triangle * (3.0f - 2.0f * triangle);
    }
    }
}
enum class MotorEnvelopeShape : uint8_t { Linear, Rounded, Exponential, Plateau };
inline float motorEnvelopeLevel(float phase, float symmetry, MotorEnvelopeShape shape) noexcept
{
    phase = std::clamp(phase, 0.0f, 1.0f);
    symmetry = std::clamp(symmetry, 0.05f, 0.95f);
    const float linear = std::clamp(phase <= symmetry ? phase / symmetry
        : (1.0f - phase) / (1.0f - symmetry), 0.0f, 1.0f);
    switch (shape) {
    case MotorEnvelopeShape::Rounded: return linear * linear * (3.0f - 2.0f * linear);
    case MotorEnvelopeShape::Exponential: return linear * linear;
    case MotorEnvelopeShape::Plateau: return std::min(1.0f, linear * 3.0f);
    default: return linear;
    }
}
}
