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

enum class AmbiFieldListenerResponse : uint32_t {
    Legacy = 0u,
    Excite = 1u,
    Settle = 2u,
    Imprint = 3u,
};

inline AmbiFieldListenMode sanitizeAmbiFieldListenMode(AmbiFieldListenMode mode)
{
    return static_cast<AmbiFieldListenMode>(
        std::min<uint32_t>(static_cast<uint32_t>(mode), 3u));
}

inline AmbiFieldListenerResponse sanitizeAmbiFieldListenerResponse(
    AmbiFieldListenerResponse response)
{
    return static_cast<AmbiFieldListenerResponse>(
        std::min<uint32_t>(static_cast<uint32_t>(response), 3u));
}

struct AmbiFieldListenerScore {
    float energy = 0.0f;
    float relativeEnergy = 0.0f;
    float novelty = 0.0f;
    float roughness = 0.0f;
    float spectralTilt = 0.0f;
    float habituation = 0.0f;
    float charge = 0.0f;
};

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
        previousSignal_.fill(0.0f);
        fastEnvelope_.fill(0.0f);
        slowEnvelope_.fill(0.0f);
        roughEnvelope_.fill(0.0f);
        lowSignal_.fill(0.0f);
        lowEnvelope_.fill(0.0f);
        highEnvelope_.fill(0.0f);
        relativeEnergy_.fill(0.0f);
        novelty_.fill(0.0f);
        roughness_.fill(0.0f);
        spectralTilt_.fill(0.0f);
        habituation_.fill(0.0f);
        charge_.fill(0.0f);
        energyGradient_ = {};
        noveltyGradient_ = {};
    }

    void setMemorySeconds(float seconds)
    {
        memorySeconds_ = clamp(
            std::isfinite(seconds) ? seconds : 0.42f, 0.02f, 10.0f);
        updateCoefficients();
    }

    void setExtendedAnalysisEnabled(bool enabled)
    {
        extendedAnalysisEnabled_ = enabled;
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
            previousSignal_[lobe] = 0.0f;
            fastEnvelope_[lobe] = 0.0f;
            slowEnvelope_[lobe] = 0.0f;
            roughEnvelope_[lobe] = 0.0f;
            lowSignal_[lobe] = 0.0f;
            lowEnvelope_[lobe] = 0.0f;
            highEnvelope_[lobe] = 0.0f;
            relativeEnergy_[lobe] = 0.0f;
            novelty_[lobe] = 0.0f;
            roughness_[lobe] = 0.0f;
            spectralTilt_[lobe] = 0.0f;
            habituation_[lobe] = 0.0f;
            charge_[lobe] = 0.0f;
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

            if (extendedAnalysisEnabled_) {
                const float fastCoefficient = magnitude > fastEnvelope_[lobe]
                    ? fastAttackCoefficient_ : fastReleaseCoefficient_;
                fastEnvelope_[lobe] +=
                    (magnitude - fastEnvelope_[lobe]) * fastCoefficient;
                slowEnvelope_[lobe] +=
                    (magnitude - slowEnvelope_[lobe]) * slowCoefficient_;

                const float difference =
                    std::fabs(decoded - previousSignal_[lobe]);
                previousSignal_[lobe] = decoded;
                const float roughCoefficient = difference > roughEnvelope_[lobe]
                    ? roughAttackCoefficient_ : roughReleaseCoefficient_;
                roughEnvelope_[lobe] +=
                    (difference - roughEnvelope_[lobe]) * roughCoefficient;

                lowSignal_[lobe] +=
                    (decoded - lowSignal_[lobe]) * spectralSplitCoefficient_;
                const float lowMagnitude = std::fabs(lowSignal_[lobe]);
                const float highMagnitude =
                    std::fabs(decoded - lowSignal_[lobe]);
                lowEnvelope_[lobe] +=
                    (lowMagnitude - lowEnvelope_[lobe])
                        * spectralEnvelopeCoefficient_;
                highEnvelope_[lobe] +=
                    (highMagnitude - highEnvelope_[lobe])
                        * spectralEnvelopeCoefficient_;

                fastEnvelope_[lobe] = flushDenormal(fastEnvelope_[lobe]);
                slowEnvelope_[lobe] = flushDenormal(slowEnvelope_[lobe]);
                roughEnvelope_[lobe] = flushDenormal(roughEnvelope_[lobe]);
                lowSignal_[lobe] = flushDenormal(lowSignal_[lobe]);
                lowEnvelope_[lobe] = flushDenormal(lowEnvelope_[lobe]);
                highEnvelope_[lobe] = flushDenormal(highEnvelope_[lobe]);
            }
        }

        if (!extendedAnalysisEnabled_) return;
        const float peak = peakEnvelope();
        const float activityValue = peak / (peak + 0.015f);
        energyGradient_ = {};
        noveltyGradient_ = {};
        for (uint32_t lobe = 0u; lobe < count_; ++lobe) {
            relativeEnergy_[lobe] = peak > 1.0e-7f
                ? clamp(envelope_[lobe] / peak, 0.0f, 1.0f) : 0.0f;
            novelty_[lobe] = clamp(
                (fastEnvelope_[lobe] - slowEnvelope_[lobe])
                    / (fastEnvelope_[lobe] + 0.005f),
                0.0f, 1.0f);
            roughness_[lobe] = clamp(
                roughEnvelope_[lobe] / (fastEnvelope_[lobe] * 2.0f + 0.006f),
                0.0f, 1.0f);
            spectralTilt_[lobe] = clamp(
                (highEnvelope_[lobe] - lowEnvelope_[lobe])
                    / (highEnvelope_[lobe] + lowEnvelope_[lobe] + 0.005f),
                -1.0f, 1.0f);

            const float habituationTarget =
                relativeEnergy_[lobe] * activityValue;
            const float habituationCoefficient =
                habituationTarget > habituation_[lobe]
                ? habituationAttackCoefficient_ : habituationReleaseCoefficient_;
            habituation_[lobe] +=
                (habituationTarget - habituation_[lobe])
                    * habituationCoefficient;
            const float chargeTarget = novelty_[lobe]
                * (0.70f + roughness_[lobe] * 0.30f) * activityValue;
            const float chargeCoefficient = chargeTarget > charge_[lobe]
                ? chargeAttackCoefficient_ : chargeReleaseCoefficient_;
            charge_[lobe] +=
                (chargeTarget - charge_[lobe]) * chargeCoefficient;

            const Vec3 direction = directions_[lobe];
            energyGradient_.x += direction.x * relativeEnergy_[lobe];
            energyGradient_.y += direction.y * relativeEnergy_[lobe];
            energyGradient_.z += direction.z * relativeEnergy_[lobe];
            noveltyGradient_.x += direction.x * novelty_[lobe];
            noveltyGradient_.y += direction.y * novelty_[lobe];
            noveltyGradient_.z += direction.z * novelty_[lobe];
        }
        if (count_ > 0u) {
            const float scale = 1.0f / static_cast<float>(count_);
            energyGradient_.x *= scale;
            energyGradient_.y *= scale;
            energyGradient_.z *= scale;
            noveltyGradient_.x *= scale;
            noveltyGradient_.y *= scale;
            noveltyGradient_.z *= scale;
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
    float fastEnvelope(uint32_t lobe) const
    {
        return lobe < count_ ? fastEnvelope_[lobe] : 0.0f;
    }
    float slowEnvelope(uint32_t lobe) const
    {
        return lobe < count_ ? slowEnvelope_[lobe] : 0.0f;
    }
    float relativeEnergy(uint32_t lobe) const
    {
        return lobe < count_ ? relativeEnergy_[lobe] : 0.0f;
    }
    float novelty(uint32_t lobe) const
    {
        return lobe < count_ ? novelty_[lobe] : 0.0f;
    }
    float roughness(uint32_t lobe) const
    {
        return lobe < count_ ? roughness_[lobe] : 0.0f;
    }
    float spectralTilt(uint32_t lobe) const
    {
        return lobe < count_ ? spectralTilt_[lobe] : 0.0f;
    }
    float habituation(uint32_t lobe) const
    {
        return lobe < count_ ? habituation_[lobe] : 0.0f;
    }
    float charge(uint32_t lobe) const
    {
        return lobe < count_ ? charge_[lobe] : 0.0f;
    }
    Vec3 energyGradient() const { return energyGradient_; }
    Vec3 noveltyGradient() const { return noveltyGradient_; }
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

    AmbiFieldListenerScore score(
        Vec3 direction, AmbiFieldListenMode mode) const
    {
        AmbiFieldListenerScore result {};
        mode = sanitizeAmbiFieldListenMode(mode);
        if (mode == AmbiFieldListenMode::Off || count_ == 0u) return result;
        direction = normalize(direction);
        if (mode == AmbiFieldListenMode::Counter) {
            direction.x = -direction.x;
            direction.y = -direction.y;
            direction.z = -direction.z;
        }
        float norm = 0.0f;
        for (uint32_t lobe = 0u; lobe < count_; ++lobe) {
            const Vec3 ear = directions_[lobe];
            const float dot = std::max(0.0f,
                direction.x * ear.x + direction.y * ear.y + direction.z * ear.z);
            const float kernel = dot * dot * dot * dot;
            result.energy += envelope_[lobe] * kernel;
            result.relativeEnergy += relativeEnergy_[lobe] * kernel;
            result.novelty += novelty_[lobe] * kernel;
            result.roughness += roughness_[lobe] * kernel;
            result.spectralTilt += spectralTilt_[lobe] * kernel;
            result.habituation += habituation_[lobe] * kernel;
            result.charge += charge_[lobe] * kernel;
            norm += kernel;
        }
        if (norm > 1.0e-7f) {
            result.energy /= norm;
            result.relativeEnergy /= norm;
            result.novelty /= norm;
            result.roughness /= norm;
            result.spectralTilt /= norm;
            result.habituation /= norm;
            result.charge /= norm;
        }
        if (mode == AmbiFieldListenMode::Balance) {
            result.relativeEnergy = 1.0f - result.relativeEnergy;
            result.habituation = 1.0f - result.habituation;
            result.spectralTilt = -result.spectralTilt;
        }
        return result;
    }

private:
    void updateCoefficients()
    {
        const float attackSeconds = std::max(0.008f, memorySeconds_ * 0.16f);
        attackCoefficient_ = 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate_ * attackSeconds));
        releaseCoefficient_ = 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate_ * memorySeconds_));
        fastAttackCoefficient_ = 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate_ * 0.008));
        fastReleaseCoefficient_ = 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate_ * 0.085));
        slowCoefficient_ = 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate_
                * std::max(0.18f, memorySeconds_ * 1.75f)));
        roughAttackCoefficient_ = 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate_ * 0.012));
        roughReleaseCoefficient_ = 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate_ * 0.16));
        spectralSplitCoefficient_ = 1.0f - std::exp(
            -2.0f * kPi * 900.0f / static_cast<float>(sampleRate_));
        spectralEnvelopeCoefficient_ = 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate_ * 0.055));
        habituationAttackCoefficient_ = 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate_ * 2.4));
        habituationReleaseCoefficient_ = 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate_ * 0.72));
        chargeAttackCoefficient_ = 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate_ * 0.11));
        chargeReleaseCoefficient_ = 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate_ * 1.35));
    }

    double sampleRate_ = 48000.0;
    float memorySeconds_ = 0.42f;
    float attackCoefficient_ = 0.001f;
    float releaseCoefficient_ = 0.0001f;
    float fastAttackCoefficient_ = 0.0026f;
    float fastReleaseCoefficient_ = 0.00024f;
    float slowCoefficient_ = 0.00003f;
    float roughAttackCoefficient_ = 0.0017f;
    float roughReleaseCoefficient_ = 0.00013f;
    float spectralSplitCoefficient_ = 0.11f;
    float spectralEnvelopeCoefficient_ = 0.00038f;
    float habituationAttackCoefficient_ = 0.0000087f;
    float habituationReleaseCoefficient_ = 0.000029f;
    float chargeAttackCoefficient_ = 0.00019f;
    float chargeReleaseCoefficient_ = 0.000015f;
    uint32_t count_ = 0u;
    bool extendedAnalysisEnabled_ = false;
    std::array<Vec3, kAmbiFieldListenerMaxLobes> directions_ {};
    std::array<std::array<float, kAmbiFieldListenerMaxChannels>,
        kAmbiFieldListenerMaxLobes> basis_ {};
    std::array<float, kAmbiFieldListenerMaxLobes> signal_ {};
    std::array<float, kAmbiFieldListenerMaxLobes> envelope_ {};
    std::array<float, kAmbiFieldListenerMaxLobes> previousSignal_ {};
    std::array<float, kAmbiFieldListenerMaxLobes> fastEnvelope_ {};
    std::array<float, kAmbiFieldListenerMaxLobes> slowEnvelope_ {};
    std::array<float, kAmbiFieldListenerMaxLobes> roughEnvelope_ {};
    std::array<float, kAmbiFieldListenerMaxLobes> lowSignal_ {};
    std::array<float, kAmbiFieldListenerMaxLobes> lowEnvelope_ {};
    std::array<float, kAmbiFieldListenerMaxLobes> highEnvelope_ {};
    std::array<float, kAmbiFieldListenerMaxLobes> relativeEnergy_ {};
    std::array<float, kAmbiFieldListenerMaxLobes> novelty_ {};
    std::array<float, kAmbiFieldListenerMaxLobes> roughness_ {};
    std::array<float, kAmbiFieldListenerMaxLobes> spectralTilt_ {};
    std::array<float, kAmbiFieldListenerMaxLobes> habituation_ {};
    std::array<float, kAmbiFieldListenerMaxLobes> charge_ {};
    Vec3 energyGradient_ {};
    Vec3 noveltyGradient_ {};
};

} // namespace s3g
