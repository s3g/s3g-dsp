#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace s3g::sample {

enum class SampleNeonMangleCharacter : uint8_t {
    Filter, Echo, Comb, Ring, Flutter, Pulse, Drive, Crush,
};
inline constexpr bool sampleNeonFxAmbisonicSafe(SampleNeonMangleCharacter fx) noexcept
{
    return static_cast<unsigned>(fx) < 6u;
}
inline constexpr std::array<std::array<float, 3u>, 8u> kSampleNeonFxDefaults {{
    {{ 0.0f, 0.72f, 0.2f }}, {{ 0.3f, 0.35f, 0.4f }},
    {{ 0.4f, 0.7f, 0.35f }}, {{ 0.35f, 0.0f, 1.0f }},
    {{ 0.35f, 0.25f, 0.2f }}, {{ 0.5f, 0.5f, 0.15f }},
    {{ 0.4f, 0.5f, 0.65f }}, {{ 0.6f, 0.6f, 0.0f }},
}};
inline constexpr const char* kSampleNeonFxNames[] {
    "Filter", "Echo", "Comb", "Ring", "Flutter", "Pulse", "Drive", "Crush"
};
inline constexpr const char* kSampleNeonFxLabels[8][3] {
    {"TYPE", "CUTOFF", "RESONANCE"}, {"TIME", "FEEDBACK", "DAMPING"},
    {"TUNING", "FEEDBACK", "DAMPING"}, {"FREQUENCY", "WAVE", "DEPTH"},
    {"RATE", "DEPTH", "IRREGULAR"}, {"RATE", "DUTY", "EDGE"},
    {"DRIVE", "BIAS", "TONE"}, {"BITS", "RATE", "JITTER"}
};

// A true post-playback processor. All spatial channels share clocks, delay
// taps, filter coefficients and modulation. Nonlinear component processing is
// explicitly prohibited for Ambisonics, even when called without the GUI.
class SampleNeonFx {
public:
    bool prepare(double rate) {
        if (!std::isfinite(rate) || rate <= 0.0 || rate > 768000.0) return false;
        rate_ = rate;
        capacity_ = static_cast<uint32_t>(std::ceil(rate)) + 4u;
        try { delay_.resize(static_cast<std::size_t>(capacity_) * 16u); }
        catch (...) { return false; }
        reset(); return true;
    }
    void unprepare() { std::vector<float>().swap(delay_); capacity_ = 0u; }
    void reset() noexcept {
        // Lazy clearing: old samples cannot be read until overwritten. No
        // large buffer clears or allocation in a render/reset callback.
        written_ = 0u; write_ = 0u; phase_ = 0.0; heldClock_ = 0.0;
        low_ = {}; band_ = {}; held_ = {}; wet_ = 0.0f; last_ = 255u;
    }
    void process(float* const* audio, unsigned channels, uint32_t frames,
        SampleNeonMangleCharacter character, const std::array<float, 3u>& values,
        float amount, bool ambisonic) noexcept
    {
        if (!capacity_) return;
        const auto type = static_cast<unsigned>(character);
        if (type >= 8u || (ambisonic && !sampleNeonFxAmbisonicSafe(character))) { reset(); return; }
        if (last_ != type) { reset(); last_ = type; }
        const double a = std::clamp<double>(values[0], 0.0, 1.0);
        const double b = std::clamp<double>(values[1], 0.0, 1.0);
        const double c = std::clamp<double>(values[2], 0.0, 1.0);
        constexpr double pi = 3.14159265358979323846;
        const double cutoff = std::min(rate_ * 0.45, 20.0 * std::pow(1000.0, b));
        const double g = std::tan(pi * cutoff / rate_);
        const double damping = 1.0 / (0.5 + c * c * 15.0);
        const double a1 = 1.0 / (1.0 + g * (g + damping));
        const double a2 = g * a1, a3 = g * a2;
        const double tone = 1.0 - std::exp(-2.0 * pi * std::min(rate_ * 0.45,
            100.0 * std::pow(150.0, type == 6u ? c : 1.0 - c)) / rate_);
        const double frequency = type == 3u ? 1.0 * std::pow(2000.0, a)
            : type == 5u ? 0.1 * std::pow(320.0, a) : 0.1 * std::pow(120.0, a);
        const double smooth = 1.0 - std::exp(-1.0 / (rate_ * 0.005));
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            wet_ += static_cast<float>((std::clamp(amount, 0.0f, 1.0f) - wet_) * smooth);
            const double sine = std::sin(phase_ * 2.0 * pi);
            double tap = type == 1u ? (0.001 + a * 0.999) * rate_
                : type == 2u ? rate_ / (20.0 * std::pow(100.0, a))
                : rate_ * (0.015 + b * 0.01 * (sine * (1.0 - c) + c * std::sin(phase_ * 2.0 * pi * 2.71)));
            tap = std::clamp(tap, 1.0, static_cast<double>(capacity_ - 2u));
            const auto integer = static_cast<uint32_t>(tap);
            const float fraction = static_cast<float>(tap - integer);
            const auto pos = (write_ + capacity_ - integer) % capacity_;
            const auto prev = (pos + capacity_ - 1u) % capacity_;
            const bool sampleHold = heldClock_ <= 0.0;
            if (sampleHold) heldClock_ += std::max(1.0, std::pow(128.0, 1.0 - b)
                * (1.0 + c * 0.4 * std::sin(phase_ * 17.0)));
            for (unsigned channel = 0u; channel < std::min(channels, 16u); ++channel) {
                const float dry = audio[channel][frame];
                float wet = dry;
                auto* memory = delay_.data() + static_cast<std::size_t>(channel) * capacity_;
                const float delayed = written_ > integer + 1u
                    ? memory[pos] + (memory[prev] - memory[pos]) * fraction : 0.0f;
                float stored = dry;
                switch (character) {
                case SampleNeonMangleCharacter::Filter: {
                    const double v3 = dry - low_[channel];
                    const double bp = a1 * band_[channel] + a2 * v3;
                    const double lp = low_[channel] + a2 * band_[channel] + a3 * v3;
                    band_[channel] = 2.0 * bp - band_[channel]; low_[channel] = 2.0 * lp - low_[channel];
                    wet = static_cast<float>(a < 0.25 ? lp : a < 0.75 ? dry - damping * bp - lp : bp);
                    break;
                }
                case SampleNeonMangleCharacter::Echo:
                case SampleNeonMangleCharacter::Comb:
                    low_[channel] += tone * (delayed - low_[channel]);
                    stored = dry + static_cast<float>(low_[channel] * b * 0.95);
                    wet = type == 1u ? delayed : static_cast<float>((dry + delayed * b) / (1.0 + b));
                    break;
                case SampleNeonMangleCharacter::Ring: {
                    const double wave = b < 0.25 ? sine : b < 0.75 ? 1.0 - 4.0 * std::abs(phase_ - 0.5) : (phase_ < 0.5 ? 1.0 : -1.0);
                    wet = static_cast<float>(dry * (1.0 - c + c * wave)); break;
                }
                case SampleNeonMangleCharacter::Flutter: wet = delayed; break;
                case SampleNeonMangleCharacter::Pulse: {
                    const double duty = 0.05 + b * 0.9;
                    const double edge = std::min(std::min(duty, 1.0 - duty) * 0.5,
                        frequency * (0.001 + c * 0.039));
                    const double ramp = std::min(phase_ / edge, (duty - phase_) / edge);
                    wet = static_cast<float>(dry * (phase_ < duty ? 0.5 - 0.5 * std::cos(pi * std::clamp(ramp, 0.0, 1.0)) : 0.0)); break;
                }
                case SampleNeonMangleCharacter::Drive: {
                    const double bias = (b - 0.5) * 0.8;
                    const double distorted = (std::tanh((dry + bias) * (1.0 + a * 24.0))
                        - std::tanh(bias * (1.0 + a * 24.0))) / std::sqrt(1.0 + a * 8.0);
                    low_[channel] += tone * (distorted - low_[channel]); wet = static_cast<float>(low_[channel]); break;
                }
                case SampleNeonMangleCharacter::Crush:
                    if (sampleHold) {
                        const float steps = std::pow(2.0f, 2.0f + std::round(static_cast<float>(a) * 13.0f));
                        held_[channel] = std::round(dry * steps) / steps;
                    }
                    wet = held_[channel]; break;
                }
                memory[write_] = stored;
                audio[channel][frame] = dry + (wet - dry) * wet_;
            }
            write_ = (write_ + 1u) % capacity_;
            written_ = std::min(written_ + 1u, capacity_);
            heldClock_ -= 1.0;
            phase_ += frequency / rate_; phase_ -= std::floor(phase_);
        }
    }
private:
    std::vector<float> delay_;
    std::array<double, 16u> low_ {}, band_ {};
    std::array<float, 16u> held_ {};
    double rate_ = 48000.0, phase_ = 0.0, heldClock_ = 0.0;
    uint32_t capacity_ = 0u, written_ = 0u, write_ = 0u;
    unsigned last_ = 255u;
    float wet_ = 0.0f;
};
} // namespace s3g::sample
