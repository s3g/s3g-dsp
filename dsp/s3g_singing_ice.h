#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

struct SingingIceOutput {
    float sample = 0.0f;
    float activity = 0.0f;
};

// Event-triggered flexural-wave packets for floating lake ice. An impulse
// launches one dominant inverse-dispersive chirp: a compact, dull "pew"
// descending toward the air-coupled flexural frequency. Quiet, short-lived
// inharmonic plate modes follow it. The model is dormant outside
// Cryosphere's SINGING LAKE regime.
class SingingIceModel {
public:
    static constexpr uint32_t kModeCount = 4u;

    void prepare(double sampleRate, uint32_t seed)
    {
        sampleRate_ = std::max(1000.0, sampleRate);
        rng_ = seed ? seed : 1u;
        reset();
    }

    void reset()
    {
        pulse_ = {};
        for (auto& mode : modes_) mode = {};
        active_ = false;
        dominantFrequencyHz_ = 0.0f;
    }

    void excite(float strength, float scale, float brightness,
        float resonance, float damping, float depth)
    {
        strength = std::clamp(strength, 0.0f, 2.0f);
        scale = std::clamp(scale, 0.0f, 1.0f);
        brightness = std::clamp(brightness, 0.0f, 1.0f);
        resonance = std::clamp(resonance, 0.0f, 1.0f);
        damping = std::clamp(damping, 0.0f, 1.0f);
        depth = std::clamp(depth, 0.0f, 1.0f);
        constexpr std::array<float, kModeCount> kInharmonicRatios {
            1.0f, 1.72f, 2.47f, 3.36f,
        };
        const float baseFrequency = 54.0f + (1.0f - scale) * 100.0f
            + (1.0f - depth) * 38.0f;
        const float nyquistGuard = static_cast<float>(sampleRate_) * 0.42f;

        pulse_.endFrequencyHz = baseFrequency
            * (0.96f + randomUnit() * 0.08f);
        pulse_.startFrequencyHz = std::min(nyquistGuard,
            pulse_.endFrequencyHz * (5.0f + brightness * 5.2f));
        pulse_.duration = std::max(0.24f,
            (0.42f + resonance * 1.15f + scale * 0.32f)
                * (1.0f - damping * 0.48f));
        pulse_.sweepTime = 0.16f + resonance * 0.17f + scale * 0.08f;
        pulse_.age = 0.0f;
        pulse_.phase = randomUnit() < 0.5f ? 0.0f
            : 3.14159265358979323846f;
        pulse_.amplitude = strength * (0.92f + randomUnit() * 0.16f);

        const float modeDuration = (0.22f + resonance * 0.90f
                + scale * 0.22f)
            * (1.0f - damping * 0.56f);
        for (uint32_t index = 0u; index < kModeCount; ++index) {
            auto& mode = modes_[index];
            const float detune = 0.94f + randomUnit() * 0.12f;
            mode.endFrequencyHz = std::min(nyquistGuard,
                baseFrequency * kInharmonicRatios[index] * detune);
            mode.startFrequencyHz = std::min(nyquistGuard,
                mode.endFrequencyHz
                    * (1.12f + brightness * 0.62f
                        + static_cast<float>(index) * 0.04f));
            mode.duration = std::max(0.12f, modeDuration
                * (1.0f - static_cast<float>(index) * 0.10f));
            mode.sweepTime = mode.duration * 0.24f;
            mode.delay = static_cast<float>(kModeCount - 1u - index)
                * (0.002f + scale * 0.003f);
            mode.age = 0.0f;
            mode.phase = randomUnit() < 0.5f ? 0.0f
                : 3.14159265358979323846f;
            const float spectralWeight = 1.0f
                / std::sqrt(1.0f + static_cast<float>(index));
            mode.amplitude = strength * spectralWeight
                * (0.18f + randomUnit() * 0.10f);
        }
        active_ = strength > 1.0e-5f;
    }

    SingingIceOutput process()
    {
        if (!active_) return {};
        constexpr float kTwoPi = 6.28318530717958647692f;
        const float dt = 1.0f / static_cast<float>(sampleRate_);
        float sample = 0.0f;
        float activity = 0.0f;
        bool anyActive = false;
        dominantFrequencyHz_ = 0.0f;

        pulse_.age += dt;
        if (pulse_.age < pulse_.duration) {
            anyActive = true;
            const float frequency = pulse_.endFrequencyHz
                + (pulse_.startFrequencyHz - pulse_.endFrequencyHz)
                    * std::exp(-pulse_.age
                        / std::max(0.01f, pulse_.sweepTime));
            pulse_.phase += kTwoPi * frequency * dt;
            if (pulse_.phase > kTwoPi) pulse_.phase -= kTwoPi;
            const float attack = 1.0f - std::exp(-pulse_.age / 0.0018f);
            const float decayTime = std::max(0.08f,
                pulse_.duration * 0.31f);
            const float envelope = attack
                * std::exp(-pulse_.age / decayTime)
                * std::max(0.0f, 1.0f - pulse_.age / pulse_.duration);
            const float pulseTone = std::sin(pulse_.phase)
                + std::sin(pulse_.phase * 2.0f) * 0.08f;
            const float weighted = pulse_.amplitude * envelope;
            sample += pulseTone * weighted * 0.86f;
            activity = std::max(activity, std::fabs(weighted));
            dominantFrequencyHz_ = frequency;
        }

        for (uint32_t index = 0u; index < kModeCount; ++index) {
            auto& mode = modes_[index];
            mode.age += dt;
            const float localAge = mode.age - mode.delay;
            if (localAge < 0.0f || localAge >= mode.duration) continue;
            anyActive = true;
            const float frequency = mode.endFrequencyHz
                + (mode.startFrequencyHz - mode.endFrequencyHz)
                    * std::exp(-localAge
                        / std::max(0.01f, mode.sweepTime));
            mode.phase += kTwoPi * frequency * dt;
            if (mode.phase > kTwoPi) mode.phase -= kTwoPi;
            const float attack = 1.0f - std::exp(-localAge / 0.0035f);
            const float decayTime = std::max(0.08f,
                mode.duration * (0.20f + 0.025f * index));
            const float envelope = attack * std::exp(-localAge / decayTime)
                * std::max(0.0f, 1.0f - localAge / mode.duration);
            const float weighted = mode.amplitude * envelope;
            sample += std::sin(mode.phase) * weighted * 0.18f;
            activity = std::max(activity, std::fabs(weighted));
        }
        active_ = anyActive;
        sample = std::tanh(sample * 1.18f) * 0.30f;
        return { std::isfinite(sample) ? sample : 0.0f,
            std::clamp(activity, 0.0f, 1.0f) };
    }

    bool active() const { return active_; }
    float dominantFrequencyHz() const { return dominantFrequencyHz_; }

private:
    struct Mode {
        float phase = 0.0f;
        float age = 0.0f;
        float delay = 0.0f;
        float duration = 0.0f;
        float sweepTime = 0.1f;
        float startFrequencyHz = 0.0f;
        float endFrequencyHz = 0.0f;
        float amplitude = 0.0f;
    };

    uint32_t nextRandom()
    {
        rng_ ^= rng_ << 13u;
        rng_ ^= rng_ >> 17u;
        rng_ ^= rng_ << 5u;
        return rng_;
    }

    float randomUnit()
    {
        return static_cast<float>(nextRandom() & 0x00ffffffu)
            / static_cast<float>(0x01000000u);
    }

    std::array<Mode, kModeCount> modes_ {};
    Mode pulse_ {};
    double sampleRate_ = 48000.0;
    uint32_t rng_ = 1u;
    bool active_ = false;
    float dominantFrequencyHz_ = 0.0f;
};

} // namespace s3g
