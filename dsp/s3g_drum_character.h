#pragma once

#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

// Reusable output coloration for the conventional (mono/stereo) drum family.
// Amount controls are normalized. Bias and tone are bipolar, with zero neutral.
// Keeping every member at its default produces a transparent signal path.
struct DrumCharacterParams {
    float drive = 0.0f;
    float bias = 0.0f;
    float compression = 0.0f;
    float sampleRateReduction = 0.0f;
    float bitDepthReduction = 0.0f;
    float reconstruction = 0.0f;
    float tone = 0.0f;
};

class DrumCharacter {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::isfinite(sampleRate)
            ? std::max(8000.0, std::min(768000.0, sampleRate))
            : 48000.0;

        const float sr = static_cast<float>(sampleRate_);
        parameterSmoothingCoefficient_ = onePoleCoefficient(5.0f, sr);
        envelopeAttackCoefficient_ = onePoleCoefficient(0.65f, sr);
        envelopeReleaseCoefficient_ = onePoleCoefficient(85.0f, sr);
        toneCoefficient_ = frequencyCoefficient(950.0f, sr);
        dcBlockerCoefficient_ = std::exp(-2.0f * kPi * 8.0f / sr);
        targetReconstructionCoefficient_ =
            reconstructionCoefficientForAmount(target_.reconstruction);
        maxHoldSamples_ = static_cast<uint32_t>(std::max(1.0f,
            std::min(512.0f, std::round(sr / 375.0f))));
        reset();
    }

    void reset()
    {
        channels_.fill({});
        holdCountdown_ = 0u;
        transparentBypassActive_ = false;
        smoothed_ = target_;
        reconstructionCoefficient_ = targetReconstructionCoefficient_;
    }

    void setParams(DrumCharacterParams params)
    {
        const DrumCharacterParams sanitized = sanitize(params);
        if (sanitized.reconstruction != target_.reconstruction) {
            targetReconstructionCoefficient_ =
                reconstructionCoefficientForAmount(sanitized.reconstruction);
        }
        target_ = sanitized;
    }

    DrumCharacterParams params() const { return target_; }

    // Stateful reconstruction and tone filters can ring briefly after their
    // input stops. Instruments use this to keep a host awake until that small
    // character tail is actually silent.
    bool active() const
    {
        for (const ChannelState& state : channels_) {
            if (std::abs(state.heldSample) * smoothed_.sampleRateReduction
                    > 1.0e-7f
                || (std::abs(state.reconstructionOne)
                        + std::abs(state.reconstructionTwo))
                        * smoothed_.reconstruction > 1.0e-7f
                || std::abs(state.toneLowpass) * std::abs(smoothed_.tone)
                    > 1.0e-7f
                || std::abs(state.dcOutput) * smoothed_.drive > 1.0e-7f) {
                return true;
            }
        }
        return false;
    }

    void processFrame(float& left, float& right)
    {
        smoothParameters();

        std::array<float, 2u> frame {
            sanitizeInput(left),
            sanitizeInput(right),
        };

        if (effectivelyTransparent()) {
            left = frame[0u];
            right = frame[1u];
            return;
        }

        for (uint32_t channel = 0u; channel < channels_.size(); ++channel) {
            frame[channel] = processDrive(frame[channel]);
            frame[channel] = processDcBlocker(
                channels_[channel], frame[channel]);
            frame[channel] = processCompression(
                channels_[channel], frame[channel]);
        }

        processSampleHold(frame);

        for (uint32_t channel = 0u; channel < channels_.size(); ++channel) {
            frame[channel] = processQuantizer(frame[channel]);
            frame[channel] = processReconstruction(
                channels_[channel], frame[channel]);
            frame[channel] = processTone(channels_[channel], frame[channel]);
            frame[channel] = clamp(flushDenormal(frame[channel]), -8.0f, 8.0f);
        }

        left = frame[0u];
        right = frame[1u];
    }

private:
    struct ChannelState {
        float envelope = 0.0f;
        float heldSample = 0.0f;
        float reconstructionOne = 0.0f;
        float reconstructionTwo = 0.0f;
        float toneLowpass = 0.0f;
        float dcInput = 0.0f;
        float dcOutput = 0.0f;
    };

    static float finiteOrZero(float value)
    {
        return std::isfinite(value) ? value : 0.0f;
    }

    static DrumCharacterParams sanitize(DrumCharacterParams params)
    {
        params.drive = clamp(finiteOrZero(params.drive), 0.0f, 1.0f);
        params.bias = clamp(finiteOrZero(params.bias), -1.0f, 1.0f);
        params.compression = clamp(
            finiteOrZero(params.compression), 0.0f, 1.0f);
        params.sampleRateReduction = clamp(
            finiteOrZero(params.sampleRateReduction), 0.0f, 1.0f);
        params.bitDepthReduction = clamp(
            finiteOrZero(params.bitDepthReduction), 0.0f, 1.0f);
        params.reconstruction = clamp(
            finiteOrZero(params.reconstruction), 0.0f, 1.0f);
        params.tone = clamp(finiteOrZero(params.tone), -1.0f, 1.0f);
        return params;
    }

    static float sanitizeInput(float input)
    {
        return clamp(finiteOrZero(input), -8.0f, 8.0f);
    }

    static float onePoleCoefficient(float timeMs, float sampleRate)
    {
        const float samples = std::max(1.0f, timeMs * 0.001f * sampleRate);
        return 1.0f - std::exp(-1.0f / samples);
    }

    static float frequencyCoefficient(float frequencyHz, float sampleRate)
    {
        const float frequency = clamp(frequencyHz, 1.0f, sampleRate * 0.45f);
        return 1.0f - std::exp(-2.0f * kPi * frequency / sampleRate);
    }

    float reconstructionCoefficientForAmount(float amount) const
    {
        const float cutoff = 20000.0f
            * std::pow(500.0f / 20000.0f, amount);
        return frequencyCoefficient(cutoff, static_cast<float>(sampleRate_));
    }

    bool effectivelyTransparent()
    {
        const bool targetIsTransparent = target_.drive == 0.0f
            && target_.compression == 0.0f
            && target_.sampleRateReduction == 0.0f
            && target_.bitDepthReduction == 0.0f
            && target_.reconstruction == 0.0f
            && target_.tone == 0.0f;
        const bool smoothedIsTransparent = smoothed_.drive < 1.0e-7f
            && smoothed_.compression < 1.0e-7f
            && smoothed_.sampleRateReduction < 1.0e-7f
            && smoothed_.bitDepthReduction < 1.0e-7f
            && smoothed_.reconstruction < 1.0e-7f
            && std::abs(smoothed_.tone) < 1.0e-7f;
        if (!targetIsTransparent || !smoothedIsTransparent) {
            transparentBypassActive_ = false;
            return false;
        }
        smoothed_.drive = 0.0f;
        smoothed_.compression = 0.0f;
        smoothed_.sampleRateReduction = 0.0f;
        smoothed_.bitDepthReduction = 0.0f;
        smoothed_.reconstruction = 0.0f;
        smoothed_.tone = 0.0f;
        reconstructionCoefficient_ = targetReconstructionCoefficient_;
        if (!transparentBypassActive_) {
            channels_.fill({});
            holdCountdown_ = 0u;
            transparentBypassActive_ = true;
        }
        return true;
    }

    void smoothParameters()
    {
        const auto smooth = [this](float& value, float target) {
            value += (target - value) * parameterSmoothingCoefficient_;
            value = flushDenormal(value);
        };
        smooth(smoothed_.drive, target_.drive);
        smooth(smoothed_.bias, target_.bias);
        smooth(smoothed_.compression, target_.compression);
        smooth(smoothed_.sampleRateReduction, target_.sampleRateReduction);
        smooth(smoothed_.bitDepthReduction, target_.bitDepthReduction);
        smooth(smoothed_.reconstruction, target_.reconstruction);
        smooth(smoothed_.tone, target_.tone);
        smooth(reconstructionCoefficient_,
            targetReconstructionCoefficient_);
    }

    float processDrive(float input) const
    {
        const float amount = smoothed_.drive;
        if (amount <= 0.0f) return input;

        // Subtracting the transfer value at zero keeps a biased/asymmetric
        // circuit silent for a silent input.
        const float gain = 1.0f + amount * amount * 22.0f;
        const float offset = smoothed_.bias * amount * 0.48f;
        const float shaped = (std::tanh(input * gain + offset)
            - std::tanh(offset)) / std::sqrt(1.0f + amount * 2.5f);
        return input + (shaped - input) * amount;
    }

    float processDcBlocker(ChannelState& state, float input) const
    {
        const float filtered = flushDenormal(input - state.dcInput
            + dcBlockerCoefficient_ * state.dcOutput);
        state.dcInput = input;
        state.dcOutput = filtered;
        return input + (filtered - input) * smoothed_.drive;
    }

    float processCompression(ChannelState& state, float input) const
    {
        const float magnitude = std::abs(input);
        const float coefficient = magnitude > state.envelope
            ? envelopeAttackCoefficient_
            : envelopeReleaseCoefficient_;
        state.envelope = flushDenormal(
            state.envelope + (magnitude - state.envelope) * coefficient);

        const float amount = smoothed_.compression;
        if (amount <= 0.0f) return input;

        const float threshold = lerp(0.9f, 0.08f, amount * amount);
        const float over = std::max(state.envelope, threshold);
        const float reduction = std::pow(threshold / over, amount * 0.9f);
        const float makeup = 1.0f + amount * 0.24f;
        return input * reduction * makeup;
    }

    void processSampleHold(std::array<float, 2u>& frame)
    {
        const float amount = smoothed_.sampleRateReduction;
        const float curved = amount * amount;
        const uint32_t holdSamples = 1u + static_cast<uint32_t>(
            curved * static_cast<float>(maxHoldSamples_ - 1u) + 0.5f);

        if (holdCountdown_ == 0u || holdCountdown_ >= holdSamples) {
            channels_[0u].heldSample = frame[0u];
            channels_[1u].heldSample = frame[1u];
            holdCountdown_ = holdSamples - 1u;
        } else {
            --holdCountdown_;
        }

        if (amount <= 0.0f) return;
        frame[0u] += (channels_[0u].heldSample - frame[0u]) * amount;
        frame[1u] += (channels_[1u].heldSample - frame[1u]) * amount;
    }

    float processQuantizer(float input) const
    {
        const float amount = smoothed_.bitDepthReduction;
        if (amount <= 0.0f) return input;

        const uint32_t removedBits = static_cast<uint32_t>(
            amount * 22.0f + 0.5f);
        const uint32_t bits = 24u - std::min(removedBits, 22u);
        const float levels = static_cast<float>(1u << (bits - 1u));
        const float quantized = std::round(input * levels) / levels;
        return input + (quantized - input) * amount;
    }

    float processReconstruction(ChannelState& state, float input) const
    {
        const float amount = smoothed_.reconstruction;
        const float coefficient = reconstructionCoefficient_;
        state.reconstructionOne = flushDenormal(state.reconstructionOne
            + (input - state.reconstructionOne) * coefficient);
        state.reconstructionTwo = flushDenormal(state.reconstructionTwo
            + (state.reconstructionOne - state.reconstructionTwo)
                * coefficient);
        if (amount <= 0.0f) return input;
        return input + (state.reconstructionTwo - input) * amount;
    }

    float processTone(ChannelState& state, float input) const
    {
        state.toneLowpass = flushDenormal(state.toneLowpass
            + (input - state.toneLowpass) * toneCoefficient_);

        const float amount = smoothed_.tone;
        if (amount == 0.0f) return input;

        const float low = state.toneLowpass;
        const float high = input - low;
        if (amount > 0.0f) {
            return low * (1.0f - amount * 0.22f)
                + high * (1.0f + amount * 1.45f);
        }

        const float dark = -amount;
        return low * (1.0f + dark * 0.20f)
            + high * (1.0f - dark * 0.92f);
    }

    double sampleRate_ = 48000.0;
    DrumCharacterParams target_ {};
    DrumCharacterParams smoothed_ {};
    std::array<ChannelState, 2u> channels_ {};
    float parameterSmoothingCoefficient_ = 0.004157998f;
    float envelopeAttackCoefficient_ = 0.031548f;
    float envelopeReleaseCoefficient_ = 0.000245f;
    float toneCoefficient_ = 0.11687f;
    float dcBlockerCoefficient_ = 0.998953f;
    float targetReconstructionCoefficient_ = 0.927051f;
    float reconstructionCoefficient_ = 0.927051f;
    uint32_t maxHoldSamples_ = 128u;
    uint32_t holdCountdown_ = 0u;
    bool transparentBypassActive_ = false;
};

} // namespace s3g
