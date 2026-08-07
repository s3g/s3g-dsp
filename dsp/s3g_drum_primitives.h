#pragma once

#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>

namespace s3g {

// Small, allocation-free building blocks shared by the conventional drum
// family.  They deliberately stop below the voice/preset layer: kicks, snares,
// toms and hats have different lifetime and articulation rules.

inline float drumFiniteOr(float value, float fallback)
{
    return std::isfinite(value) ? value : fallback;
}

inline double drumSafeSampleRate(double sampleRate)
{
    return std::isfinite(sampleRate)
        ? std::max(8000.0, std::min(768000.0, sampleRate))
        : 48000.0;
}

inline float drumDecayMultiplier(float timeSeconds, float sampleRate,
    float terminal = 0.001f)
{
    timeSeconds = std::max(1.0e-6f,
        drumFiniteOr(timeSeconds, 0.1f));
    sampleRate = std::max(1.0f, drumFiniteOr(sampleRate, 48000.0f));
    terminal = clamp(drumFiniteOr(terminal, 0.001f), 1.0e-12f, 0.999999f);
    return std::exp(std::log(terminal)
        / std::max(1.0f, timeSeconds * sampleRate));
}

inline float drumOnePoleFrequencyCoefficient(float frequencyHz,
    float sampleRate)
{
    sampleRate = std::max(1.0f, drumFiniteOr(sampleRate, 48000.0f));
    frequencyHz = clamp(drumFiniteOr(frequencyHz, 1000.0f),
        0.001f, sampleRate * 0.45f);
    return 1.0f - std::exp(-2.0f * kPi * frequencyHz / sampleRate);
}

inline float drumOnePoleTimeCoefficient(float timeSeconds, float sampleRate)
{
    sampleRate = std::max(1.0f, drumFiniteOr(sampleRate, 48000.0f));
    timeSeconds = std::max(1.0e-6f,
        drumFiniteOr(timeSeconds, 0.01f));
    return 1.0f - std::exp(
        -1.0f / std::max(1.0f, timeSeconds * sampleRate));
}

inline uint32_t drumLongestTailSamples(double sampleRate,
    std::initializer_list<double> tailSeconds, double minimumSeconds = 0.05,
    double maximumSeconds = 40.0)
{
    sampleRate = drumSafeSampleRate(sampleRate);
    minimumSeconds = std::isfinite(minimumSeconds)
        ? std::max(0.0, minimumSeconds) : 0.05;
    maximumSeconds = std::isfinite(maximumSeconds)
        ? std::max(minimumSeconds, maximumSeconds) : 40.0;
    double longest = minimumSeconds;
    for (double seconds : tailSeconds) {
        if (std::isfinite(seconds)) longest = std::max(longest, seconds);
    }
    longest = std::min(longest, maximumSeconds);
    const double samples = std::ceil(longest * sampleRate);
    return static_cast<uint32_t>(std::min(samples,
        static_cast<double>(std::numeric_limits<uint32_t>::max())));
}

inline uint32_t drumMixedSeed(uint32_t voiceIndex, uint64_t triggerIndex,
    uint32_t salt = 0u)
{
    uint32_t seed = 0x9e3779b9u ^ salt
        ^ static_cast<uint32_t>(triggerIndex)
        ^ static_cast<uint32_t>(triggerIndex >> 32u)
        ^ ((voiceIndex + 1u) * 0x85ebca6bu);
    seed ^= seed >> 16u;
    seed *= 0x7feb352du;
    seed ^= seed >> 15u;
    seed *= 0x846ca68bu;
    seed ^= seed >> 16u;
    return seed != 0u ? seed : 1u;
}

class DrumRandom {
public:
    explicit DrumRandom(uint32_t seed = 1u) { reset(seed); }

    void reset(uint32_t seed = 1u) { state_ = seed != 0u ? seed : 1u; }

    uint32_t nextU32()
    {
        uint32_t state = state_;
        state ^= state << 13u;
        state ^= state >> 17u;
        state ^= state << 5u;
        state_ = state != 0u ? state : 1u;
        return state_;
    }

    float bipolar()
    {
        return static_cast<float>(nextU32() >> 8u)
            * (1.0f / 8388607.5f) - 1.0f;
    }

    float unipolar()
    {
        return static_cast<float>(nextU32() >> 8u)
            * (1.0f / 16777215.0f);
    }

    uint32_t state() const { return state_; }

private:
    uint32_t state_ = 1u;
};

class DrumExponentialEnvelope {
public:
    void configure(float timeSeconds, float sampleRate,
        float terminal = 0.001f)
    {
        multiplier_ = drumDecayMultiplier(timeSeconds, sampleRate, terminal);
    }

    void reset(float value = 0.0f)
    {
        value_ = std::max(0.0f, drumFiniteOr(value, 0.0f));
    }

    void trigger(float value = 1.0f) { reset(value); }

    float process()
    {
        const float output = value_;
        value_ = flushDenormal(value_ * multiplier_);
        return output;
    }

    float value() const { return value_; }
    float multiplier() const { return multiplier_; }
    bool active(float threshold = 1.0e-6f) const
    {
        return value_ > std::max(0.0f, threshold);
    }

private:
    float value_ = 0.0f;
    float multiplier_ = 0.999f;
};

// A stable, quadrature-form damped mode.  Excitation is added to the real
// component before each rotation, so a single strike produces a unit-scale
// decaying sinusoid without the frequency-dependent gain of a raw two-pole.
class DrumModalResonator {
public:
    void configure(float frequencyHz, float decaySeconds, float sampleRate,
        float terminal = 0.001f)
    {
        sampleRate = std::max(1.0f, drumFiniteOr(sampleRate, 48000.0f));
        frequencyHz = clamp(drumFiniteOr(frequencyHz, 180.0f),
            1.0f, sampleRate * 0.45f);
        radius_ = drumDecayMultiplier(decaySeconds, sampleRate, terminal);
        const float angle = 2.0f * kPi * frequencyHz / sampleRate;
        rotationReal_ = radius_ * std::cos(angle);
        rotationImaginary_ = radius_ * std::sin(angle);
    }

    void reset()
    {
        real_ = 0.0f;
        imaginary_ = 0.0f;
    }

    void strike(float amplitude, float quadrature = 0.0f)
    {
        real_ = flushDenormal(real_ + drumFiniteOr(amplitude, 0.0f));
        imaginary_ = flushDenormal(imaginary_
            + drumFiniteOr(quadrature, 0.0f));
    }

    float process(float excitation = 0.0f)
    {
        real_ += drumFiniteOr(excitation, 0.0f);
        const float nextReal = real_ * rotationReal_
            - imaginary_ * rotationImaginary_;
        const float nextImaginary = real_ * rotationImaginary_
            + imaginary_ * rotationReal_;
        if (!std::isfinite(nextReal) || !std::isfinite(nextImaginary)
            || std::abs(nextReal) > 64.0f
            || std::abs(nextImaginary) > 64.0f) {
            reset();
            return 0.0f;
        }
        real_ = flushDenormal(nextReal);
        imaginary_ = flushDenormal(nextImaginary);
        return real_;
    }

    float magnitudeSquared() const
    {
        return real_ * real_ + imaginary_ * imaginary_;
    }

    bool active(float threshold = 1.0e-6f) const
    {
        threshold = std::max(0.0f, threshold);
        return magnitudeSquared() > threshold * threshold;
    }

    float radius() const { return radius_; }

private:
    float real_ = 0.0f;
    float imaginary_ = 0.0f;
    float rotationReal_ = 0.999f;
    float rotationImaginary_ = 0.0f;
    float radius_ = 0.999f;
};

inline float drumSafeOutput(float value)
{
    if (!std::isfinite(value)) return 0.0f;
    value = flushDenormal(value);
    const float magnitude = std::abs(value);
    if (magnitude <= 1.5f) return value;
    return std::copysign(1.5f
            + std::tanh((magnitude - 1.5f) * 0.65f) * 1.45f,
        value);
}

} // namespace s3g
