#pragma once

#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAmbiFieldListenerMaxLobes = 8u;
constexpr uint32_t kAmbiFieldListenerMaxChannels = 64u;

enum class AmbiFieldListenMode : uint32_t {
    Off = 0u,
    Follow = 1u,
    Counter = 2u,
    Balance = 3u,
};

inline AmbiFieldListenMode sanitizeAmbiFieldListenMode(AmbiFieldListenMode mode)
{
    return static_cast<AmbiFieldListenMode>(
        std::min<uint32_t>(static_cast<uint32_t>(mode), 3u));
}

inline const std::array<Vec3, kAmbiFieldListenerMaxLobes>& ambiFieldListenerCubeDirections()
{
    constexpr float k = 0.57735026919f;
    static const std::array<Vec3, kAmbiFieldListenerMaxLobes> directions {{
        { -k, -k, -k }, { k, -k, -k }, { -k, k, -k }, { k, k, -k },
        { -k, -k, k }, { k, -k, k }, { -k, k, k }, { k, k, k },
    }};
    return directions;
}

// A bank of virtual directional microphones for an ACN/SN3D field. It analyzes
// the HOA signal only; callers decide how its envelopes affect their own DSP.
class AmbiFieldListener {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1000.0, sampleRate);
        updateCoefficients();
        reset();
    }

    void reset()
    {
        envelope_.fill(0.0f);
        signal_.fill(0.0f);
    }

    void setMemorySeconds(float seconds)
    {
        memorySeconds_ = clamp(
            std::isfinite(seconds) ? seconds : 0.42f, 0.02f, 10.0f);
        updateCoefficients();
    }

    void setDirections(const Vec3* directions, uint32_t count)
    {
        count_ = std::min<uint32_t>(count, kAmbiFieldListenerMaxLobes);
        for (uint32_t lobe = 0u; lobe < count_; ++lobe) {
            const Vec3 direction = normalize(directions[lobe]);
            directions_[lobe] = direction;
            basis_[lobe] = acnSn3dBasis7(direction);
        }
        for (uint32_t lobe = count_; lobe < kAmbiFieldListenerMaxLobes; ++lobe) {
            directions_[lobe] = {};
            basis_[lobe].fill(0.0f);
            envelope_[lobe] = 0.0f;
            signal_[lobe] = 0.0f;
        }
    }

    void processFrame(const float* hoa, uint32_t channels)
    {
        channels = std::min<uint32_t>(channels, kAmbiFieldListenerMaxChannels);
        for (uint32_t lobe = 0u; lobe < count_; ++lobe) {
            float decoded = 0.0f;
            float norm = 0.0f;
            for (uint32_t channel = 0u; channel < channels; ++channel) {
                const float coefficient = basis_[lobe][channel];
                const float value = hoa && std::isfinite(hoa[channel]) ? hoa[channel] : 0.0f;
                decoded += value * coefficient;
                norm += coefficient * coefficient;
            }
            decoded /= std::max(1.0f, norm);
            signal_[lobe] = flushDenormal(decoded);
            const float magnitude = std::fabs(decoded);
            const float coefficient =
                magnitude > envelope_[lobe] ? attackCoefficient_ : releaseCoefficient_;
            envelope_[lobe] += (magnitude - envelope_[lobe]) * coefficient;
            envelope_[lobe] = flushDenormal(envelope_[lobe]);
        }
    }

    uint32_t count() const { return count_; }
    float signal(uint32_t lobe) const
    {
        return lobe < count_ ? signal_[lobe] : 0.0f;
    }
    float envelope(uint32_t lobe) const
    {
        return lobe < count_ ? envelope_[lobe] : 0.0f;
    }
    Vec3 direction(uint32_t lobe) const
    {
        return lobe < count_ ? directions_[lobe] : Vec3 {};
    }

    float meanEnvelope() const
    {
        if (count_ == 0u) return 0.0f;
        float total = 0.0f;
        for (uint32_t lobe = 0u; lobe < count_; ++lobe) total += envelope_[lobe];
        return total / static_cast<float>(count_);
    }

    float peakEnvelope() const
    {
        float peak = 0.0f;
        for (uint32_t lobe = 0u; lobe < count_; ++lobe) {
            peak = std::max(peak, envelope_[lobe]);
        }
        return peak;
    }

    // A normalized measure of whether the field contains enough recent sound
    // to serve as a score. Silence stays neutral instead of becoming a target.
    float activity() const
    {
        const float peak = peakEnvelope();
        return peak / (peak + 0.015f);
    }

    Vec3 preferredDirection(AmbiFieldListenMode mode) const
    {
        mode = sanitizeAmbiFieldListenMode(mode);
        if (mode == AmbiFieldListenMode::Off || count_ == 0u || peakEnvelope() < 1.0e-7f) {
            return {};
        }
        uint32_t selected = 0u;
        for (uint32_t lobe = 1u; lobe < count_; ++lobe) {
            const bool better = mode == AmbiFieldListenMode::Balance
                ? envelope_[lobe] < envelope_[selected]
                : envelope_[lobe] > envelope_[selected];
            if (better) selected = lobe;
        }
        Vec3 result = directions_[selected];
        if (mode == AmbiFieldListenMode::Counter) {
            result.x = -result.x;
            result.y = -result.y;
            result.z = -result.z;
        }
        return result;
    }

    // Scores an arbitrary direction against the recent directional field:
    // Follow seeks heard regions, Counter seeks their antipodes, and Balance
    // seeks regions receiving less energy.
    float preference(Vec3 direction, AmbiFieldListenMode mode) const
    {
        mode = sanitizeAmbiFieldListenMode(mode);
        const float peak = peakEnvelope();
        if (mode == AmbiFieldListenMode::Off || count_ == 0u || peak < 1.0e-7f) {
            return 0.5f;
        }
        direction = normalize(direction);
        if (mode == AmbiFieldListenMode::Counter) {
            direction.x = -direction.x;
            direction.y = -direction.y;
            direction.z = -direction.z;
        }
        float weighted = 0.0f;
        float norm = 0.0f;
        for (uint32_t lobe = 0u; lobe < count_; ++lobe) {
            const Vec3 ear = directions_[lobe];
            const float dot = std::max(0.0f,
                direction.x * ear.x + direction.y * ear.y + direction.z * ear.z);
            const float kernel = dot * dot * dot * dot;
            weighted += envelope_[lobe] * kernel;
            norm += kernel;
        }
        const float observed = norm > 1.0e-7f ? weighted / norm : meanEnvelope();
        const float normalized = clamp(observed / peak, 0.0f, 1.0f);
        return mode == AmbiFieldListenMode::Balance ? 1.0f - normalized : normalized;
    }

private:
    void updateCoefficients()
    {
        const float attackSeconds = std::max(0.008f, memorySeconds_ * 0.16f);
        attackCoefficient_ = 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate_ * attackSeconds));
        releaseCoefficient_ = 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate_ * memorySeconds_));
    }

    double sampleRate_ = 48000.0;
    float memorySeconds_ = 0.42f;
    float attackCoefficient_ = 0.001f;
    float releaseCoefficient_ = 0.0001f;
    uint32_t count_ = 0u;
    std::array<Vec3, kAmbiFieldListenerMaxLobes> directions_ {};
    std::array<std::array<float, kAmbiFieldListenerMaxChannels>,
        kAmbiFieldListenerMaxLobes> basis_ {};
    std::array<float, kAmbiFieldListenerMaxLobes> signal_ {};
    std::array<float, kAmbiFieldListenerMaxLobes> envelope_ {};
};

} // namespace s3g
