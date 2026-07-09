#pragma once

#include "s3g_delay_processor.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kMacroDelayChannels = 24;

struct MacroDelayParams {
    float timeMs = 260.0f;
    float feedback = 0.32f;
    float tone = 0.62f;
    float character = 0.24f;
    float smear = 0.0f;
    float spread = 0.0f;
    float deviation = 0.0f;
    float skew = 0.0f;
    float center = 0.5f;
    float glideMs = 250.0f;
    float mix = 0.35f;
    float outputGainDb = 0.0f;
};

class MacroDelay {
public:
    void prepare(double sampleRate, uint32_t channels, double maxDelaySeconds = 2.5)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        channels_ = std::clamp<uint32_t>(channels, 0u, kMacroDelayChannels);
        delay_.prepare(sampleRate_, static_cast<int>(channels_), maxDelaySeconds);
        reset();
    }

    void reset()
    {
        delay_.reset();
        smoothedSpread_ = params_.spread;
        smoothedDeviation_ = params_.deviation;
        smoothedSkew_ = params_.skew;
        smoothedCenter_ = params_.center;
        mixSmoothed_ = params_.mix;
        gainSmoothed_ = dbToGain(params_.outputGainDb);
    }

    void setParams(const MacroDelayParams& params)
    {
        params_ = sanitize(params);
    }

    MacroDelayParams params() const { return params_; }

    void processFrame(const float* input, float* output)
    {
        if (!input || !output || channels_ == 0u) {
            return;
        }

        updateRelationshipSmoothing();
        applyLaneParams();

        std::array<float, kMacroDelayChannels> wet {};
        delay_.processFrame(input, wet.data());

        const float mixTarget = params_.mix;
        const float gainTarget = dbToGain(params_.outputGainDb);
        for (uint32_t ch = 0; ch < channels_; ++ch) {
            mixSmoothed_ += (mixTarget - mixSmoothed_) * 0.0015f;
            gainSmoothed_ += (gainTarget - gainSmoothed_) * 0.0015f;
            const float dry = input[ch];
            const float value = dry + (wet[ch] - dry) * mixSmoothed_;
            output[ch] = softLimit(flushDenormal(value * gainSmoothed_));
        }
    }

    void processWetFrame(const float* input, float* output)
    {
        if (!input || !output || channels_ == 0u) {
            return;
        }

        updateRelationshipSmoothing();
        applyLaneParams();

        std::array<float, kMacroDelayChannels> wet {};
        delay_.processFrame(input, wet.data());

        const float mixTarget = params_.mix;
        const float gainTarget = dbToGain(params_.outputGainDb);
        for (uint32_t ch = 0; ch < channels_; ++ch) {
            mixSmoothed_ += (mixTarget - mixSmoothed_) * 0.0015f;
            gainSmoothed_ += (gainTarget - gainSmoothed_) * 0.0015f;
            output[ch] = softLimit(flushDenormal(wet[ch] * mixSmoothed_ * gainSmoothed_));
        }
    }

    uint32_t channels() const { return channels_; }

private:
    static MacroDelayParams sanitize(MacroDelayParams params)
    {
        params.timeMs = clamp(params.timeMs, 5.0f, 2000.0f);
        params.feedback = clamp(params.feedback, 0.0f, 0.78f);
        params.tone = clamp(params.tone, 0.0f, 1.0f);
        params.character = clamp(params.character, 0.0f, 1.0f);
        params.smear = clamp(params.smear, 0.0f, 1.0f);
        params.spread = clamp(params.spread, 0.0f, 1.0f);
        params.deviation = clamp(params.deviation, 0.0f, 1.0f);
        params.skew = clamp(params.skew, -1.0f, 1.0f);
        params.center = clamp(params.center, 0.0f, 1.0f);
        params.glideMs = clamp(params.glideMs, 10.0f, 2000.0f);
        params.mix = clamp(params.mix, 0.0f, 1.0f);
        params.outputGainDb = clamp(params.outputGainDb, -60.0f, 12.0f);
        return params;
    }

    static float laneHash(uint32_t lane)
    {
        uint32_t x = lane * 747796405u + 2891336453u;
        x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
        x = (x >> 22u) ^ x;
        return static_cast<float>(x & 0xffffu) / 32767.5f - 1.0f;
    }

    static float softLimit(float value)
    {
        return std::tanh(clamp(value, -8.0f, 8.0f));
    }

    void updateRelationshipSmoothing()
    {
        const float coeff = 1.0f - std::exp(-1.0f / static_cast<float>(std::max(1.0, sampleRate_ * params_.glideMs * 0.001)));
        smoothedSpread_ += (params_.spread - smoothedSpread_) * coeff;
        smoothedDeviation_ += (params_.deviation - smoothedDeviation_) * coeff;
        smoothedSkew_ += (params_.skew - smoothedSkew_) * coeff;
        smoothedCenter_ += (params_.center - smoothedCenter_) * coeff;
    }

    void applyLaneParams()
    {
        const float denom = static_cast<float>(std::max<uint32_t>(1u, channels_ - 1u));
        for (uint32_t ch = 0; ch < channels_; ++ch) {
            const float u = channels_ > 1u ? static_cast<float>(ch) / denom : 0.5f;
            const float centered = clamp((u - smoothedCenter_) * 2.0f, -1.0f, 1.0f);
            const float spreadRatio = std::pow(2.0f, centered * smoothedSpread_);
            const float devRatio = std::pow(2.0f, laneHash(ch) * smoothedDeviation_ * 0.5f);
            const float skewRatio = std::pow(2.0f, smoothedSkew_ * u);
            delay_.setChannelDelayMs(static_cast<int>(ch), params_.timeMs * spreadRatio * devRatio * skewRatio);
            delay_.setChannelFeedback(static_cast<int>(ch), params_.feedback * (1.0f - smoothedDeviation_ * 0.08f));
            delay_.setChannelTone(static_cast<int>(ch), params_.tone);
            delay_.setChannelCharacter(static_cast<int>(ch), params_.character);
            delay_.setChannelSmearAmount(static_cast<int>(ch), params_.smear);
            delay_.setChannelNetwork(static_cast<int>(ch), 0.0f);
            delay_.setChannelPitchSemitones(static_cast<int>(ch), 0.0f);
        }
    }

    double sampleRate_ = 48000.0;
    uint32_t channels_ = 0;
    MacroDelayParams params_ {};
    DelayProcessor delay_;
    float smoothedSpread_ = 0.0f;
    float smoothedDeviation_ = 0.0f;
    float smoothedSkew_ = 0.0f;
    float smoothedCenter_ = 0.5f;
    float mixSmoothed_ = 0.35f;
    float gainSmoothed_ = 1.0f;
};

} // namespace s3g
