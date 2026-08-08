#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kBreakBusMaximumChannels = 16u;

enum class BreakBusLinkMode : uint8_t {
    All = 0u,
    Pair,
    Free,
};

struct BreakBusParams {
    float press = 0.42f;
    float snap = 0.18f;
    float recovery = 0.34f;
    float saturation = 0.20f;
    float bite = 0.08f;
    float clip = 0.0f;
    float tilt = 0.0f;
    BreakBusLinkMode linkMode = BreakBusLinkMode::All;
    bool fieldSafe = false;

    bool valid() const noexcept
    {
        const auto unit = [](float value) {
            return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
        };
        return unit(press) && std::isfinite(snap) && snap >= -1.0f
            && snap <= 1.0f && unit(recovery) && unit(saturation)
            && unit(bite) && unit(clip) && std::isfinite(tilt)
            && tilt >= -1.0f && tilt <= 1.0f
            && static_cast<uint8_t>(linkMode)
                <= static_cast<uint8_t>(BreakBusLinkMode::Free);
    }
};

// A wet-only parallel processor for breakbeat material. Dynamics and SNAP use
// linked scalar gain, while every nonlinear/filter state remains discrete per
// channel. No mode exchanges, sums, decodes, or reorders output lanes.
class BreakBus {
public:
    bool prepare(double sampleRate) noexcept
    {
        if (!(sampleRate > 0.0) || !std::isfinite(sampleRate)) return false;
        sampleRate_ = static_cast<float>(sampleRate);
        updateCoefficients();
        reset();
        prepared_ = true;
        return true;
    }

    void unprepare() noexcept
    {
        reset();
        prepared_ = false;
    }

    void reset() noexcept
    {
        detectorLow_.fill(0.0f);
        fastEnvelope_.fill(0.0f);
        slowEnvelope_.fill(0.0f);
        compressorGain_.fill(1.0f);
        previousSatInput_.fill(0.0f);
        previousBiteInput_.fill(0.0f);
        biteDcInput_.fill(0.0f);
        biteDcOutput_.fill(0.0f);
        previousClipInput_.fill(0.0f);
        tiltLow_.fill(0.0f);
        activity_ = 0.0f;
        gainReductionDb_ = 0.0f;
    }

    bool setParams(const BreakBusParams& params) noexcept
    {
        if (!params.valid()) return false;
        params_ = params;
        updateCoefficients();
        return true;
    }

    const BreakBusParams& params() const noexcept { return params_; }
    float activity() const noexcept { return activity_; }
    float gainReductionDb() const noexcept { return gainReductionDb_; }

    void beginBlock() noexcept
    {
        activity_ = 0.0f;
        gainReductionDb_ = 0.0f;
    }

    void processFrame(float* channels, uint32_t channelCount) noexcept
    {
        if (!prepared_ || !channels || channelCount == 0u) return;
        channelCount = std::min(channelCount, kBreakBusMaximumChannels);
        std::array<float, kBreakBusMaximumChannels> detector {};
        std::array<float, kBreakBusMaximumChannels> groupLevel {};

        for (uint32_t channel = 0u; channel < channelCount; ++channel) {
            const float input = finite(channels[channel]);
            detectorLow_[channel] += detectorHighPassCoefficient_
                * (input - detectorLow_[channel]);
            detector[channel] = std::abs(input - detectorLow_[channel]);
            const uint32_t group = groupFor(channel);
            groupLevel[group] = std::max(groupLevel[group], detector[channel]);
            activity_ = std::max(activity_, std::abs(input));
        }

        const uint32_t groups = groupCount(channelCount);
        std::array<float, kBreakBusMaximumChannels> dynamicGain {};
        for (uint32_t group = 0u; group < groups; ++group) {
            const float level = groupLevel[group];
            fastEnvelope_[group] = follow(fastEnvelope_[group], level,
                fastAttackCoefficient_, fastReleaseCoefficient_);
            slowEnvelope_[group] = follow(slowEnvelope_[group], level,
                slowAttackCoefficient_, slowReleaseCoefficient_);
            const float transient = std::clamp(
                (fastEnvelope_[group] - slowEnvelope_[group])
                    / (slowEnvelope_[group] + 0.001f), 0.0f, 1.0f);
            const float targetDb = compressorGainDb(fastEnvelope_[group]);
            const float target = dbGain(targetDb);
            const float adaptiveRelease = releaseCoefficient_
                + (fastReleaseCoefficient_ - releaseCoefficient_)
                    * transient * 0.45f;
            const float gainCoefficient = target < compressorGain_[group]
                ? compressorAttackCoefficient_ : adaptiveRelease;
            compressorGain_[group] = finite(compressorGain_[group]
                + (target - compressorGain_[group]) * gainCoefficient);
            const float snapGain = dbGain(params_.snap * transient * 12.0f);
            dynamicGain[group] = compressorGain_[group] * snapGain;
            gainReductionDb_ = std::min(gainReductionDb_,
                gainToDb(compressorGain_[group]));
        }

        const bool nonlinear = !params_.fieldSafe;
        for (uint32_t channel = 0u; channel < channelCount; ++channel) {
            float value = finite(channels[channel]
                * dynamicGain[groupFor(channel)]);
            if (nonlinear) {
                value = processSaturation(channel, value);
                value = processBite(channel, value);
                value = processClip(channel, value);
            } else {
                previousSatInput_[channel] = value;
                previousBiteInput_[channel] = value;
                previousClipInput_[channel] = value;
            }
            channels[channel] = processTilt(channel, value);
        }
    }

private:
    static float finite(float value) noexcept
    {
        if (!std::isfinite(value)) return 0.0f;
        return std::abs(value) < 1.0e-20f ? 0.0f : value;
    }

    static float dbGain(float db) noexcept
    {
        return std::pow(10.0f, db * 0.05f);
    }

    static float gainToDb(float gain) noexcept
    {
        return 20.0f * std::log10(std::max(gain, 1.0e-9f));
    }

    float coefficient(float milliseconds) const noexcept
    {
        const float seconds = std::max(0.000001f, milliseconds * 0.001f);
        return 1.0f - std::exp(-1.0f / (seconds * sampleRate_));
    }

    static float follow(float current, float target, float attack,
        float release) noexcept
    {
        const float coefficient = target > current ? attack : release;
        return finite(current + (target - current) * coefficient);
    }

    uint32_t groupFor(uint32_t channel) const noexcept
    {
        switch (params_.linkMode) {
        case BreakBusLinkMode::All: return 0u;
        case BreakBusLinkMode::Pair: return channel / 2u;
        case BreakBusLinkMode::Free: return channel;
        }
        return 0u;
    }

    uint32_t groupCount(uint32_t channels) const noexcept
    {
        switch (params_.linkMode) {
        case BreakBusLinkMode::All: return 1u;
        case BreakBusLinkMode::Pair: return (channels + 1u) / 2u;
        case BreakBusLinkMode::Free: return channels;
        }
        return 1u;
    }

    float compressorGainDb(float level) const noexcept
    {
        const float inputDb = gainToDb(level);
        const float thresholdDb = -3.0f - params_.press * 27.0f;
        const float ratio = 1.0f + params_.press * 19.0f;
        constexpr float kneeDb = 6.0f;
        const float over = inputDb - thresholdDb;
        const float slope = 1.0f / ratio - 1.0f;
        if (over <= -kneeDb * 0.5f) return 0.0f;
        if (over >= kneeDb * 0.5f) return slope * over;
        const float kneePosition = over + kneeDb * 0.5f;
        return slope * kneePosition * kneePosition / (2.0f * kneeDb);
    }

    static float logCosh(float value) noexcept
    {
        const float magnitude = std::abs(value);
        return magnitude + std::log1p(std::exp(-2.0f * magnitude))
            - 0.6931471805599453f;
    }

    static float softAntiderivative(float input, float drive) noexcept
    {
        return logCosh(drive * input)
            / (drive * std::tanh(drive));
    }

    static float adaaSoft(float input, float previous, float drive) noexcept
    {
        const float difference = input - previous;
        if (std::abs(difference) < 1.0e-5f)
            return std::tanh(drive * (input + previous) * 0.5f)
                / std::tanh(drive);
        return (softAntiderivative(input, drive)
            - softAntiderivative(previous, drive)) / difference;
    }

    static float asymmetricAntiderivative(float input, float positiveDrive,
        float negativeDrive) noexcept
    {
        const float drive = input >= 0.0f ? positiveDrive : negativeDrive;
        return logCosh(drive * input)
            / (drive * std::tanh(drive));
    }

    static float adaaAsymmetric(float input, float previous,
        float positiveDrive, float negativeDrive) noexcept
    {
        const float difference = input - previous;
        if (std::abs(difference) < 1.0e-5f) {
            const float midpoint = (input + previous) * 0.5f;
            const float drive = midpoint >= 0.0f
                ? positiveDrive : negativeDrive;
            return std::tanh(drive * midpoint) / std::tanh(drive);
        }
        return (asymmetricAntiderivative(input, positiveDrive, negativeDrive)
            - asymmetricAntiderivative(previous, positiveDrive,
                negativeDrive)) / difference;
    }

    static float clipAntiderivative(float input) noexcept
    {
        if (input > 1.0f) return input - 0.5f;
        if (input < -1.0f) return -input - 0.5f;
        return 0.5f * input * input;
    }

    static float adaaClip(float input, float previous) noexcept
    {
        const float difference = input - previous;
        if (std::abs(difference) < 1.0e-5f)
            return std::clamp((input + previous) * 0.5f, -1.0f, 1.0f);
        return (clipAntiderivative(input) - clipAntiderivative(previous))
            / difference;
    }

    float processSaturation(uint32_t channel, float input) noexcept
    {
        const float amount = params_.saturation;
        if (!(amount > 0.0f)) {
            previousSatInput_[channel] = input;
            return input;
        }
        const float drive = 1.0f + amount * 8.0f;
        const float saturated = adaaSoft(input, previousSatInput_[channel],
            drive);
        previousSatInput_[channel] = input;
        return finite(input + (saturated - input) * amount);
    }

    float processBite(uint32_t channel, float input) noexcept
    {
        const float amount = params_.bite;
        if (!(amount > 0.0f)) {
            previousBiteInput_[channel] = input;
            biteDcInput_[channel] = input;
            biteDcOutput_[channel] = input;
            return input;
        }
        const float positiveDrive = 1.0f + amount * 12.0f;
        const float negativeDrive = 1.0f + amount * 3.0f;
        const float shaped = adaaAsymmetric(input,
            previousBiteInput_[channel], positiveDrive, negativeDrive);
        previousBiteInput_[channel] = input;
        const float colored = input + (shaped - input) * amount;
        const float dcBlocked = colored - biteDcInput_[channel]
            + 0.995f * biteDcOutput_[channel];
        biteDcInput_[channel] = colored;
        biteDcOutput_[channel] = finite(dcBlocked);
        return biteDcOutput_[channel];
    }

    float processClip(uint32_t channel, float input) noexcept
    {
        const float amount = params_.clip;
        if (!(amount > 0.0f)) {
            previousClipInput_[channel] = input;
            return input;
        }
        const float driven = input * (1.0f + amount * amount * 12.0f);
        const float clipped = adaaClip(driven, previousClipInput_[channel]);
        previousClipInput_[channel] = driven;
        const float ceiling = 4.0f - amount * 3.0f;
        return finite(std::clamp(input + (clipped - input) * amount,
            -ceiling, ceiling));
    }

    float processTilt(uint32_t channel, float input) noexcept
    {
        tiltLow_[channel] += tiltCoefficient_
            * (input - tiltLow_[channel]);
        const float low = tiltLow_[channel];
        const float high = input - low;
        const float lowGain = dbGain(-params_.tilt * 6.0f);
        const float highGain = dbGain(params_.tilt * 6.0f);
        return finite(low * lowGain + high * highGain);
    }

    void updateCoefficients() noexcept
    {
        detectorHighPassCoefficient_ = coefficient(2.25f);
        fastAttackCoefficient_ = coefficient(0.20f);
        fastReleaseCoefficient_ = coefficient(30.0f);
        slowAttackCoefficient_ = coefficient(18.0f);
        slowReleaseCoefficient_ = coefficient(
            80.0f + params_.recovery * 520.0f);
        compressorAttackCoefficient_ = coefficient(
            12.0f - params_.press * 10.0f);
        releaseCoefficient_ = coefficient(
            20.0f + params_.recovery * 580.0f);
        constexpr float kTwoPi = 6.28318530717958647692f;
        tiltCoefficient_ = 1.0f - std::exp(-kTwoPi * 900.0f / sampleRate_);
    }

    BreakBusParams params_ {};
    std::array<float, kBreakBusMaximumChannels> detectorLow_ {};
    std::array<float, kBreakBusMaximumChannels> fastEnvelope_ {};
    std::array<float, kBreakBusMaximumChannels> slowEnvelope_ {};
    std::array<float, kBreakBusMaximumChannels> compressorGain_ {};
    std::array<float, kBreakBusMaximumChannels> previousSatInput_ {};
    std::array<float, kBreakBusMaximumChannels> previousBiteInput_ {};
    std::array<float, kBreakBusMaximumChannels> biteDcInput_ {};
    std::array<float, kBreakBusMaximumChannels> biteDcOutput_ {};
    std::array<float, kBreakBusMaximumChannels> previousClipInput_ {};
    std::array<float, kBreakBusMaximumChannels> tiltLow_ {};
    float sampleRate_ = 48000.0f;
    float detectorHighPassCoefficient_ = 0.05f;
    float fastAttackCoefficient_ = 0.1f;
    float fastReleaseCoefficient_ = 0.001f;
    float slowAttackCoefficient_ = 0.001f;
    float slowReleaseCoefficient_ = 0.0001f;
    float compressorAttackCoefficient_ = 0.01f;
    float releaseCoefficient_ = 0.001f;
    float tiltCoefficient_ = 0.1f;
    float activity_ = 0.0f;
    float gainReductionDb_ = 0.0f;
    bool prepared_ = false;
};

} // namespace s3g
