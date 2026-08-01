#pragma once

#include "s3g_fracture_processors.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kMacroFractureChannels = 24u;

struct MacroFractureCoreParams {
    float inputGainDb = 0.0f;
    FractureProcessor processor = FractureProcessor::Relay;
    float amount = 0.55f;
    float color = 0.50f;
    float bias = 0.0f;
    float react = 0.25f;
    float memory = 0.0f;
    float mix = 0.72f;
    float outputGainDb = -3.0f;
};

class MacroFractureCore {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        const float sr = static_cast<float>(sampleRate_);
        parameterSmoothingCoeff_ =
            1.0f - std::exp(-1.0f / (sr * 0.012f));
        envelopeAttackCoeff_ =
            1.0f - std::exp(-1.0f / (sr * 0.004f));
        envelopeReleaseCoeff_ =
            1.0f - std::exp(-1.0f / (sr * 0.095f));
        slowEnvelopeCoeff_ =
            1.0f - std::exp(-1.0f / (sr * 0.280f));
        meterCoeff_ =
            1.0f - std::exp(-1.0f / (sr * 0.060f));
        dcPole_ = std::exp(-2.0f * kPi * 18.0f / sr);
        processorFadeCoeff_ = 1.0f / std::max(1.0f, sr * 0.020f);
        reset();
    }

    void reset()
    {
        runtime_.timeBuffer.fill(0.0f);
        runtime_.timeWrite = 0u;
        processorStates_.fill({});
        recurrence_.fill(0.0f);
        envelope_ = 0.0f;
        slowEnvelope_ = 0.0f;
        activity_ = 0.0f;
        previousInput_ = 0.0f;
        previousModulation_ = 0.0f;
        dcInput_ = 0.0f;
        dcOutput_ = 0.0f;
        activeProcessor_ = target_.processor;
        previousProcessor_ = activeProcessor_;
        processorFade_ = 1.0f;
        smoothed_ = target_;
        inputGainTarget_ = dbToGain(target_.inputGainDb);
        outputGainTarget_ = dbToGain(target_.outputGainDb);
        inputGainSmoothed_ = inputGainTarget_;
        outputGainSmoothed_ = outputGainTarget_;
    }

    void setParams(const MacroFractureCoreParams& params)
    {
        target_ = sanitize(params);
        inputGainTarget_ = dbToGain(target_.inputGainDb);
        outputGainTarget_ = dbToGain(target_.outputGainDb);
    }

    MacroFractureCoreParams params() const { return target_; }

    float processSample(float input, float modulationSource)
    {
        smoothParams();
        inputGainSmoothed_ +=
            (inputGainTarget_ - inputGainSmoothed_)
            * parameterSmoothingCoeff_;
        outputGainSmoothed_ +=
            (outputGainTarget_ - outputGainSmoothed_)
            * parameterSmoothingCoeff_;

        input = std::isfinite(input) ? input : 0.0f;
        modulationSource =
            std::isfinite(modulationSource) ? modulationSource : 0.0f;
        const float drivenInput =
            clamp(input * inputGainSmoothed_, -8.0f, 8.0f);
        const float drivenModulation =
            clamp(modulationSource * inputGainSmoothed_, -8.0f, 8.0f);

        const float magnitude = std::abs(drivenInput);
        envelope_ += (magnitude - envelope_)
            * (magnitude > envelope_
                ? envelopeAttackCoeff_ : envelopeReleaseCoeff_);
        slowEnvelope_ +=
            (magnitude - slowEnvelope_) * slowEnvelopeCoeff_;
        const float transient =
            clamp((envelope_ - slowEnvelope_) * 7.0f, 0.0f, 1.0f);
        const float signalActivity =
            clamp(envelope_ * 2.4f + transient, 0.0f, 1.0f);
        const float amount = clamp(smoothed_.amount
            + smoothed_.react * (signalActivity * 0.24f
                + transient * 0.16f), 0.0f, 1.0f);
        const float color = clamp(smoothed_.color
            + smoothed_.react * (transient - 0.25f) * 0.10f,
            0.0f, 1.0f);

        if (target_.processor != activeProcessor_) {
            previousProcessor_ = activeProcessor_;
            activeProcessor_ = target_.processor;
            processorFade_ = 0.0f;
        }

        const float midpoint =
            0.5f * (previousInput_ + drivenInput);
        const float modulationMidpoint =
            0.5f * (previousModulation_ + drivenModulation);
        const auto processPair = [&](FractureProcessor processor) {
            const uint32_t index = std::min<uint32_t>(
                static_cast<uint32_t>(processor),
                kFractureProcessorCount - 1u);
            auto processHalf = [&](float sample, float modulator) {
                const float memoryInput = clamp(sample
                    + recurrence_[index] * smoothed_.memory * 0.46f,
                    -8.0f, 8.0f);
                const float wet = processFractureProcessor(processor,
                    runtime_, processorStates_[index],
                    memoryInput, modulator, amount, color,
                    smoothed_.bias,
                    static_cast<float>(sampleRate_ * 2.0));
                recurrence_[index] = flushDenormal(
                    std::tanh(clamp(wet, -8.0f, 8.0f)) * 0.985f);
                return wet;
            };
            return 0.5f * (
                processHalf(midpoint, modulationMidpoint)
                + processHalf(drivenInput, drivenModulation));
        };

        const float activeWet = processPair(activeProcessor_);
        float wet = activeWet;
        if (processorFade_ < 1.0f) {
            const float previousWet = processPair(previousProcessor_);
            wet = lerp(previousWet, activeWet, processorFade_);
            processorFade_ =
                std::min(1.0f, processorFade_ + processorFadeCoeff_);
        }
        previousInput_ = drivenInput;
        previousModulation_ = drivenModulation;

        wet = std::isfinite(wet) ? clamp(wet, -8.0f, 8.0f) : 0.0f;
        const float dc = wet - dcInput_ + dcPole_ * dcOutput_;
        dcInput_ = wet;
        dcOutput_ = flushDenormal(dc);
        const float mixed =
            lerp(drivenInput, dcOutput_, smoothed_.mix);
        const float output =
            std::tanh(clamp(mixed * outputGainSmoothed_, -8.0f, 8.0f));
        activity_ += (std::abs(output - drivenInput) - activity_)
            * meterCoeff_;
        return flushDenormal(output);
    }

    float activity() const { return clamp(activity_ * 1.8f, 0.0f, 1.0f); }

private:
    struct ProcessorState {
        float memory = 0.0f;
        float low = 0.0f;
        float high = 0.0f;
        float envelope = 0.0f;
        float gate = 0.0f;
        float phase = 0.0f;
    };

    struct ProcessorRuntime {
        std::array<float, kFractureTimeBufferSize> timeBuffer {};
        uint32_t timeWrite = 0u;
    };

    static MacroFractureCoreParams sanitize(
        MacroFractureCoreParams params)
    {
        params.inputGainDb = clamp(params.inputGainDb, -24.0f, 36.0f);
        params.processor = static_cast<FractureProcessor>(
            std::min<uint32_t>(
                static_cast<uint32_t>(params.processor),
                kFractureProcessorCount - 1u));
        params.amount = clamp(params.amount, 0.0f, 1.0f);
        params.color = clamp(params.color, 0.0f, 1.0f);
        params.bias = clamp(params.bias, -1.0f, 1.0f);
        params.react = clamp(params.react, 0.0f, 1.0f);
        params.memory = clamp(params.memory, 0.0f, 1.0f);
        params.mix = clamp(params.mix, 0.0f, 1.0f);
        params.outputGainDb = clamp(params.outputGainDb, -60.0f, 6.0f);
        return params;
    }

    void smoothParams()
    {
        const auto smooth = [this](float& current, float target) {
            current +=
                (target - current) * parameterSmoothingCoeff_;
        };
        smooth(smoothed_.amount, target_.amount);
        smooth(smoothed_.color, target_.color);
        smooth(smoothed_.bias, target_.bias);
        smooth(smoothed_.react, target_.react);
        smooth(smoothed_.memory, target_.memory);
        smooth(smoothed_.mix, target_.mix);
    }

    double sampleRate_ = 48000.0;
    MacroFractureCoreParams target_ {};
    MacroFractureCoreParams smoothed_ {};
    ProcessorRuntime runtime_ {};
    std::array<ProcessorState, kFractureProcessorCount>
        processorStates_ {};
    std::array<float, kFractureProcessorCount> recurrence_ {};
    FractureProcessor activeProcessor_ = FractureProcessor::Relay;
    FractureProcessor previousProcessor_ = FractureProcessor::Relay;
    float processorFade_ = 1.0f;
    float processorFadeCoeff_ = 0.001f;
    float envelope_ = 0.0f;
    float slowEnvelope_ = 0.0f;
    float activity_ = 0.0f;
    float previousInput_ = 0.0f;
    float previousModulation_ = 0.0f;
    float dcInput_ = 0.0f;
    float dcOutput_ = 0.0f;
    float parameterSmoothingCoeff_ = 0.001f;
    float envelopeAttackCoeff_ = 0.01f;
    float envelopeReleaseCoeff_ = 0.001f;
    float slowEnvelopeCoeff_ = 0.0001f;
    float meterCoeff_ = 0.001f;
    float dcPole_ = 0.997f;
    float inputGainTarget_ = 1.0f;
    float outputGainTarget_ = 0.7079458f;
    float inputGainSmoothed_ = 1.0f;
    float outputGainSmoothed_ = 0.7079458f;
};

struct MacroFractureParams {
    float inputGainDb = 0.0f;
    FractureProcessor processor = FractureProcessor::Relay;
    float amount = 0.55f;
    float color = 0.50f;
    float bias = 0.0f;
    float react = 0.25f;
    float memory = 0.0f;
    float spread = 0.0f;
    float deviation = 0.0f;
    float skew = 0.0f;
    float center = 0.5f;
    float glideMs = 250.0f;
    float mix = 0.72f;
    float outputGainDb = -3.0f;
};

class MacroFracture {
public:
    void prepare(double sampleRate, uint32_t channels)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        channels_ =
            std::clamp<uint32_t>(channels, 0u, kMacroFractureChannels);
        for (uint32_t ch = 0u; ch < channels_; ++ch) {
            cores_[ch].prepare(sampleRate_);
        }
        setParams(params_);
        reset();
    }

    void reset()
    {
        smoothedSpread_ = params_.spread;
        smoothedDeviation_ = params_.deviation;
        smoothedSkew_ = params_.skew;
        smoothedCenter_ = params_.center;
        logicPhase_ = 0.0f;
        for (uint32_t ch = 0u; ch < channels_; ++ch) {
            cores_[ch].setParams(laneParams(ch));
            cores_[ch].reset();
        }
    }

    void panic() { reset(); }

    void setParams(const MacroFractureParams& params)
    {
        params_ = sanitize(params);
        relationshipSmoothingCoeff_ = 1.0f - std::exp(-1.0f
            / static_cast<float>(std::max(
                1.0, sampleRate_ * params_.glideMs * 0.001)));
    }

    MacroFractureParams params() const { return params_; }

    void processFrame(const float* input, float* output)
    {
        if (!input || !output || channels_ == 0u) return;

        updateRelationshipSmoothing();
        const float logicHz =
            35.0f * std::pow(32.0f, params_.color);
        logicPhase_ = fractureWrapPhase(logicPhase_
            + logicHz / static_cast<float>(sampleRate_));
        const float monoLogic =
            std::sin(logicPhase_ * 2.0f * kPi);

        for (uint32_t ch = 0u; ch < channels_; ++ch) {
            MacroFractureCoreParams lane = laneParams(ch);
            cores_[ch].setParams(lane);
            const float modulator = channels_ > 1u
                ? input[(ch + 1u) % channels_] : monoLogic;
            output[ch] = cores_[ch].processSample(input[ch], modulator);
        }
    }

    uint32_t channels() const { return channels_; }

    float activity() const
    {
        float value = 0.0f;
        for (uint32_t ch = 0u; ch < channels_; ++ch) {
            value = std::max(value, cores_[ch].activity());
        }
        return value;
    }

    MacroFractureCoreParams laneParams(uint32_t ch) const
    {
        const float denominator = static_cast<float>(
            std::max<uint32_t>(1u, channels_ - 1u));
        const float u = channels_ > 1u
            ? static_cast<float>(ch) / denominator : 0.5f;
        const float centered = channels_ > 1u
            ? clamp((u - smoothedCenter_) * 2.0f, -1.0f, 1.0f)
            : 0.0f;
        const float random =
            channels_ > 1u ? laneHash(ch) : 0.0f;
        const float deviation =
            channels_ > 1u ? smoothedDeviation_ : 0.0f;

        MacroFractureCoreParams lane;
        lane.inputGainDb = params_.inputGainDb;
        lane.processor = params_.processor;
        lane.amount = clamp(params_.amount
            + random * deviation * 0.12f, 0.0f, 1.0f);
        lane.color = clamp(params_.color
            + centered * smoothedSpread_ * 0.42f
            + random * deviation * 0.18f, 0.0f, 1.0f);
        lane.bias = clamp(params_.bias
            + smoothedSkew_ * (u - 0.5f) * 0.65f
            + random * deviation * 0.18f, -1.0f, 1.0f);
        lane.react = params_.react;
        lane.memory = clamp(params_.memory
            + std::abs(random) * deviation * 0.08f, 0.0f, 1.0f);
        lane.mix = params_.mix;
        lane.outputGainDb = params_.outputGainDb;
        return lane;
    }

private:
    static MacroFractureParams sanitize(MacroFractureParams params)
    {
        params.inputGainDb = clamp(params.inputGainDb, -24.0f, 36.0f);
        params.processor = static_cast<FractureProcessor>(
            std::min<uint32_t>(
                static_cast<uint32_t>(params.processor),
                kFractureProcessorCount - 1u));
        params.amount = clamp(params.amount, 0.0f, 1.0f);
        params.color = clamp(params.color, 0.0f, 1.0f);
        params.bias = clamp(params.bias, -1.0f, 1.0f);
        params.react = clamp(params.react, 0.0f, 1.0f);
        params.memory = clamp(params.memory, 0.0f, 1.0f);
        params.spread = clamp(params.spread, 0.0f, 1.0f);
        params.deviation = clamp(params.deviation, 0.0f, 1.0f);
        params.skew = clamp(params.skew, -1.0f, 1.0f);
        params.center = clamp(params.center, 0.0f, 1.0f);
        params.glideMs = clamp(params.glideMs, 10.0f, 2000.0f);
        params.mix = clamp(params.mix, 0.0f, 1.0f);
        params.outputGainDb = clamp(params.outputGainDb, -60.0f, 6.0f);
        return params;
    }

    static float laneHash(uint32_t lane)
    {
        uint32_t x = lane * 747796405u + 2891336453u;
        x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
        x = (x >> 22u) ^ x;
        return static_cast<float>(x & 0xffffu) / 32767.5f - 1.0f;
    }

    void updateRelationshipSmoothing()
    {
        smoothedSpread_ +=
            (params_.spread - smoothedSpread_)
            * relationshipSmoothingCoeff_;
        smoothedDeviation_ +=
            (params_.deviation - smoothedDeviation_)
            * relationshipSmoothingCoeff_;
        smoothedSkew_ +=
            (params_.skew - smoothedSkew_)
            * relationshipSmoothingCoeff_;
        smoothedCenter_ +=
            (params_.center - smoothedCenter_)
            * relationshipSmoothingCoeff_;
    }

    double sampleRate_ = 48000.0;
    uint32_t channels_ = 0u;
    MacroFractureParams params_ {};
    std::array<MacroFractureCore, kMacroFractureChannels> cores_ {};
    float smoothedSpread_ = 0.0f;
    float smoothedDeviation_ = 0.0f;
    float smoothedSkew_ = 0.0f;
    float smoothedCenter_ = 0.5f;
    float relationshipSmoothingCoeff_ = 0.0001f;
    float logicPhase_ = 0.0f;
};

} // namespace s3g
