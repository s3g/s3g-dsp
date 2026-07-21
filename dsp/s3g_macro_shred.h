#pragma once

#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace s3g {

constexpr uint32_t kMacroShredChannels = 24;

struct MacroShredCoreParams {
    float inputGainDb = 0.0f;
    float pressure = 0.28f;
    float shred = 0.18f;
    float feedback = 0.12f;
    float color = 0.55f;
    float react = 0.25f;
    float tune = 0.65f;
    float body = 0.65f;
    float mix = 0.65f;
    float outputGainDb = -3.0f;
    float colorShiftOctaves = 0.0f;
    float intensityTrim = 0.0f;
};

class MacroShredCore {
public:
    void prepare(double sampleRate, double maxFeedbackSeconds = 0.05)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        const auto size = static_cast<size_t>(std::ceil(sampleRate_ * std::max(0.01, maxFeedbackSeconds))) + 4u;
        feedbackBuffer_.assign(std::max<size_t>(size, 8u), 0.0f);
        updateCoefficients();
        reset();
    }

    void reset()
    {
        std::fill(feedbackBuffer_.begin(), feedbackBuffer_.end(), 0.0f);
        writeIndex_ = 0u;
        envelope_ = 0.0f;
        slowEnvelope_ = 0.0f;
        loopLowpass_ = 0.0f;
        loopDcInput_ = 0.0f;
        loopDcOutput_ = 0.0f;
        bodyLowpass_ = 0.0f;
        outputDcInput_ = 0.0f;
        outputDcOutput_ = 0.0f;
        previousExcitation_ = 0.0f;
        safetyEnvelope_ = 0.0f;
        feedbackActivity_ = 0.0f;
        centroidLowpass_.fill(0.0f);
        centroidEnergy_.fill(0.0f);
        centroidLog2Hz_ = 9.813781f;
        centroidHz_ = 900.0f;
        centroidConfidence_ = 0.0f;
        centroidCounter_ = 0u;
        smoothed_ = target_;
        if (feedbackBuffer_.empty()) {
            return;
        }
        refreshDerivedTargets(true);
        inputGainSmoothed_ = inputGainTarget_;
        outputGainSmoothed_ = outputGainTarget_;
        loopLowpassCoeff_ = loopLowpassCoeffTarget_;
        updateFeedbackFrequencyTarget();
        delaySamples_ = delaySamplesTarget_;
    }

    void setParams(const MacroShredCoreParams& params)
    {
        target_ = sanitize(params);
        if (!feedbackBuffer_.empty()) {
            refreshDerivedTargets(false);
        }
    }

    MacroShredCoreParams params() const { return target_; }

    float processSample(float input)
    {
        if (feedbackBuffer_.empty()) {
            return 0.0f;
        }

        smoothParams();
        input = std::isfinite(input) ? input : 0.0f;
        inputGainSmoothed_ += (inputGainTarget_ - inputGainSmoothed_) * parameterSmoothingCoeff_;
        outputGainSmoothed_ += (outputGainTarget_ - outputGainSmoothed_) * parameterSmoothingCoeff_;
        loopLowpassCoeff_ += (loopLowpassCoeffTarget_ - loopLowpassCoeff_) * parameterSmoothingCoeff_;
        const float drivenInput = clamp(input * inputGainSmoothed_, -8.0f, 8.0f);
        updateCentroid(drivenInput);

        const float magnitude = std::abs(drivenInput);
        const float envelopeCoeff = magnitude > envelope_ ? attackCoeff_ : releaseCoeff_;
        envelope_ += (magnitude - envelope_) * envelopeCoeff;
        slowEnvelope_ += (magnitude - slowEnvelope_) * slowEnvelopeCoeff_;
        const float transient = clamp((envelope_ - slowEnvelope_) * 8.0f, 0.0f, 1.0f);
        const float activity = clamp(envelope_ * 3.0f + transient, 0.0f, 1.0f);
        const float reactiveDrive = smoothed_.react * activity;

        const float pressure = clamp(smoothed_.pressure + smoothed_.intensityTrim, 0.0f, 1.0f);
        const float preDrive = 1.0f + pressure * 12.0f + reactiveDrive * 8.0f;
        const float pressured = std::tanh(drivenInput * preDrive);

        delaySamples_ += (delaySamplesTarget_ - delaySamples_) * delaySmoothingCoeff_;
        const float delayed = readDelay(delaySamples_);

        const float loopDc = delayed - loopDcInput_ + dcPole_ * loopDcOutput_;
        loopDcInput_ = delayed;
        loopDcOutput_ = flushDenormal(loopDc);

        loopLowpass_ += (loopDcOutput_ - loopLowpass_) * loopLowpassCoeff_;
        loopLowpass_ = flushDenormal(loopLowpass_);

        safetyEnvelope_ += (std::abs(loopLowpass_) - safetyEnvelope_) * safetyCoeff_;
        const float excess = std::max(0.0f, safetyEnvelope_ - 0.55f);
        const float governor = 1.0f / (1.0f + excess * 10.0f);
        const float feedbackGain = std::min(0.965f,
            smoothed_.feedback * 0.94f + smoothed_.react * activity * 0.025f) * governor;

        const float excitation = pressured + loopLowpass_ * feedbackGain;
        const float midpoint = 0.5f * (previousExcitation_ + excitation);
        const float shaped = 0.5f * (shape(midpoint, smoothed_.shred) + shape(excitation, smoothed_.shred));
        previousExcitation_ = excitation;

        feedbackBuffer_[writeIndex_] = flushDenormal(std::tanh(shaped * 0.92f));
        writeIndex_ = (writeIndex_ + 1u) % feedbackBuffer_.size();

        bodyLowpass_ += (pressured - bodyLowpass_) * bodyLowpassCoeff_;
        bodyLowpass_ = flushDenormal(bodyLowpass_);
        const float wet = shaped * (1.0f - smoothed_.body * 0.20f) + bodyLowpass_ * smoothed_.body * 0.45f;

        const float wetDc = wet - outputDcInput_ + dcPole_ * outputDcOutput_;
        outputDcInput_ = wet;
        outputDcOutput_ = flushDenormal(wetDc);

        const float mixed = drivenInput + (outputDcOutput_ - drivenInput) * smoothed_.mix;
        const float output = mixed * outputGainSmoothed_;
        feedbackActivity_ += (std::abs(loopLowpass_) - feedbackActivity_) * meterCoeff_;
        return softLimit(flushDenormal(output));
    }

    float feedbackActivity() const { return clamp(feedbackActivity_, 0.0f, 1.0f); }
    float centroidHz() const { return centroidHz_; }
    float feedbackFrequencyHz() const { return feedbackFrequencyHz_; }

private:
    static MacroShredCoreParams sanitize(MacroShredCoreParams params)
    {
        params.inputGainDb = clamp(params.inputGainDb, -24.0f, 36.0f);
        params.pressure = clamp(params.pressure, 0.0f, 1.0f);
        params.shred = clamp(params.shred, 0.0f, 1.0f);
        params.feedback = clamp(params.feedback, 0.0f, 1.0f);
        params.color = clamp(params.color, 0.0f, 1.0f);
        params.react = clamp(params.react, 0.0f, 1.0f);
        params.tune = clamp(params.tune, 0.0f, 1.0f);
        params.body = clamp(params.body, 0.0f, 1.0f);
        params.mix = clamp(params.mix, 0.0f, 1.0f);
        params.outputGainDb = clamp(params.outputGainDb, -60.0f, 6.0f);
        params.colorShiftOctaves = clamp(params.colorShiftOctaves, -2.0f, 2.0f);
        params.intensityTrim = clamp(params.intensityTrim, -0.35f, 0.35f);
        return params;
    }

    static float folded(float value)
    {
        const float shifted = value + 1.0f;
        const float wrapped = shifted - 4.0f * std::floor(shifted * 0.25f);
        return wrapped <= 2.0f ? wrapped - 1.0f : 3.0f - wrapped;
    }

    static float shape(float value, float amount)
    {
        value = clamp(value, -8.0f, 8.0f);
        const float drive = 1.0f + amount * 10.0f;
        const float saturated = std::tanh(value * drive);
        const float foldedValue = folded(value * (1.0f + amount * 6.0f));
        const float foldMix = amount * amount * 0.72f;
        return saturated + (foldedValue - saturated) * foldMix;
    }

    static float softLimit(float value)
    {
        return std::tanh(clamp(value, -8.0f, 8.0f));
    }

    void smoothParams()
    {
        auto smooth = [this](float& current, float target) {
            current += (target - current) * parameterSmoothingCoeff_;
        };
        smooth(smoothed_.pressure, target_.pressure);
        smooth(smoothed_.shred, target_.shred);
        smooth(smoothed_.feedback, target_.feedback);
        smooth(smoothed_.color, target_.color);
        smooth(smoothed_.react, target_.react);
        smooth(smoothed_.tune, target_.tune);
        smooth(smoothed_.body, target_.body);
        smooth(smoothed_.mix, target_.mix);
        smooth(smoothed_.colorShiftOctaves, target_.colorShiftOctaves);
        smooth(smoothed_.intensityTrim, target_.intensityTrim);
    }

    void refreshDerivedTargets(bool force)
    {
        if (force || target_.inputGainDb != derivedInputGainDb_) {
            derivedInputGainDb_ = target_.inputGainDb;
            inputGainTarget_ = dbToGain(derivedInputGainDb_);
        }
        if (force || target_.outputGainDb != derivedOutputGainDb_) {
            derivedOutputGainDb_ = target_.outputGainDb;
            outputGainTarget_ = dbToGain(derivedOutputGainDb_);
        }
        if (force || target_.color != derivedColor_) {
            derivedColor_ = target_.color;
            const float cutoff = 700.0f * std::pow(20.0f, derivedColor_);
            loopLowpassCoeffTarget_ = 1.0f - std::exp(-2.0f * kPi
                * std::min(cutoff, static_cast<float>(sampleRate_ * 0.45))
                / static_cast<float>(sampleRate_));
        }
    }

    void updateCentroid(float input)
    {
        centroidLowpass_[0] += (input - centroidLowpass_[0]) * centroidFilterCoeff_[0];
        centroidLowpass_[1] += (input - centroidLowpass_[1]) * centroidFilterCoeff_[1];
        centroidLowpass_[2] += (input - centroidLowpass_[2]) * centroidFilterCoeff_[2];
        const std::array<float, 4> bands {
            centroidLowpass_[0],
            centroidLowpass_[1] - centroidLowpass_[0],
            centroidLowpass_[2] - centroidLowpass_[1],
            input - centroidLowpass_[2]
        };
        for (size_t band = 0u; band < bands.size(); ++band) {
            const float energy = bands[band] * bands[band];
            const float coeff = energy > centroidEnergy_[band] ? centroidEnergyAttackCoeff_ : centroidEnergyReleaseCoeff_;
            centroidEnergy_[band] += (energy - centroidEnergy_[band]) * coeff;
            centroidEnergy_[band] = flushDenormal(centroidEnergy_[band]);
        }

        if (++centroidCounter_ < kCentroidUpdateInterval) {
            return;
        }
        centroidCounter_ = 0u;

        constexpr std::array<float, 4> kBandLog2Hz {
            6.965784f, 8.965784f, 10.731319f, 12.550747f
        };
        float totalEnergy = 0.0f;
        float weightedLog2 = 0.0f;
        for (size_t band = 0u; band < centroidEnergy_.size(); ++band) {
            totalEnergy += centroidEnergy_[band];
            weightedLog2 += centroidEnergy_[band] * kBandLog2Hz[band];
        }

        float confidenceTarget = 0.0f;
        if (totalEnergy > 1.0e-9f) {
            const float observedLog2 = weightedLog2 / totalEnergy;
            centroidLog2Hz_ += (observedLog2 - centroidLog2Hz_) * centroidSmoothingCoeff_;
            centroidHz_ = std::pow(2.0f, centroidLog2Hz_);
            confidenceTarget = clamp(std::sqrt(totalEnergy) * 10.0f, 0.0f, 1.0f);
        }
        const float confidenceCoeff = confidenceTarget > centroidConfidence_
            ? centroidConfidenceAttackCoeff_
            : centroidConfidenceReleaseCoeff_;
        centroidConfidence_ += (confidenceTarget - centroidConfidence_) * confidenceCoeff;
        updateFeedbackFrequencyTarget();
    }

    void updateFeedbackFrequencyTarget()
    {
        constexpr float kLog2BaseHz = 6.491853f;
        const float laneShift = smoothed_.colorShiftOctaves;
        const float fixedLog2 = kLog2BaseHz + smoothed_.color * 6.0f + laneShift;
        const float centroidLog2 = centroidLog2Hz_ + (smoothed_.color - 0.5f) * 2.0f + laneShift;
        const float track = clamp(smoothed_.tune * centroidConfidence_, 0.0f, 1.0f);
        const float frequency = std::pow(2.0f, fixedLog2 + (centroidLog2 - fixedLog2) * track);
        feedbackFrequencyHz_ = clamp(frequency, 30.0f, static_cast<float>(sampleRate_ * 0.45));
        delaySamplesTarget_ = clamp(static_cast<float>(sampleRate_) / feedbackFrequencyHz_,
            2.0f, static_cast<float>(std::max<size_t>(3u, feedbackBuffer_.size() - 2u)));
    }

    float readDelay(float delaySamples) const
    {
        const float size = static_cast<float>(feedbackBuffer_.size());
        float read = static_cast<float>(writeIndex_) - delaySamples;
        while (read < 0.0f) {
            read += size;
        }
        while (read >= size) {
            read -= size;
        }
        const auto indexA = static_cast<size_t>(read);
        const auto indexB = (indexA + 1u) % feedbackBuffer_.size();
        const float fraction = read - static_cast<float>(indexA);
        return feedbackBuffer_[indexA] + (feedbackBuffer_[indexB] - feedbackBuffer_[indexA]) * fraction;
    }

    void updateCoefficients()
    {
        const float sr = static_cast<float>(sampleRate_);
        parameterSmoothingCoeff_ = 1.0f - std::exp(-1.0f / (sr * 0.020f));
        delaySmoothingCoeff_ = 1.0f - std::exp(-1.0f / (sr * 0.045f));
        attackCoeff_ = 1.0f - std::exp(-1.0f / (sr * 0.002f));
        releaseCoeff_ = 1.0f - std::exp(-1.0f / (sr * 0.070f));
        slowEnvelopeCoeff_ = 1.0f - std::exp(-1.0f / (sr * 0.180f));
        safetyCoeff_ = 1.0f - std::exp(-1.0f / (sr * 0.012f));
        meterCoeff_ = 1.0f - std::exp(-1.0f / (sr * 0.060f));
        dcPole_ = std::exp(-2.0f * kPi * 18.0f / sr);
        bodyLowpassCoeff_ = 1.0f - std::exp(-2.0f * kPi * 1200.0f / sr);
        centroidFilterCoeff_[0] = 1.0f - std::exp(-2.0f * kPi * 250.0f / sr);
        centroidFilterCoeff_[1] = 1.0f - std::exp(-2.0f * kPi * 900.0f / sr);
        centroidFilterCoeff_[2] = 1.0f - std::exp(-2.0f * kPi * 3000.0f / sr);
        centroidEnergyAttackCoeff_ = 1.0f - std::exp(-1.0f / (sr * 0.006f));
        centroidEnergyReleaseCoeff_ = 1.0f - std::exp(-1.0f / (sr * 0.090f));
        centroidSmoothingCoeff_ = 1.0f - std::exp(-static_cast<float>(kCentroidUpdateInterval) / (sr * 0.100f));
        centroidConfidenceAttackCoeff_ = 1.0f - std::exp(-static_cast<float>(kCentroidUpdateInterval) / (sr * 0.035f));
        centroidConfidenceReleaseCoeff_ = 1.0f - std::exp(-static_cast<float>(kCentroidUpdateInterval) / (sr * 0.400f));
    }

    double sampleRate_ = 48000.0;
    MacroShredCoreParams target_ {};
    MacroShredCoreParams smoothed_ {};
    std::vector<float> feedbackBuffer_;
    size_t writeIndex_ = 0u;
    float delaySamples_ = 8.0f;
    float delaySamplesTarget_ = 8.0f;
    float envelope_ = 0.0f;
    float slowEnvelope_ = 0.0f;
    float loopLowpass_ = 0.0f;
    float loopDcInput_ = 0.0f;
    float loopDcOutput_ = 0.0f;
    float bodyLowpass_ = 0.0f;
    float outputDcInput_ = 0.0f;
    float outputDcOutput_ = 0.0f;
    float previousExcitation_ = 0.0f;
    float safetyEnvelope_ = 0.0f;
    float feedbackActivity_ = 0.0f;
    static constexpr uint32_t kCentroidUpdateInterval = 16u;
    std::array<float, 3> centroidLowpass_ {};
    std::array<float, 3> centroidFilterCoeff_ {};
    std::array<float, 4> centroidEnergy_ {};
    float centroidEnergyAttackCoeff_ = 0.003f;
    float centroidEnergyReleaseCoeff_ = 0.0002f;
    float centroidSmoothingCoeff_ = 0.003f;
    float centroidConfidenceAttackCoeff_ = 0.01f;
    float centroidConfidenceReleaseCoeff_ = 0.001f;
    float centroidLog2Hz_ = 9.813781f;
    float centroidHz_ = 900.0f;
    float centroidConfidence_ = 0.0f;
    float feedbackFrequencyHz_ = 900.0f;
    uint32_t centroidCounter_ = 0u;
    float parameterSmoothingCoeff_ = 0.001f;
    float delaySmoothingCoeff_ = 0.001f;
    float attackCoeff_ = 0.01f;
    float releaseCoeff_ = 0.001f;
    float slowEnvelopeCoeff_ = 0.0001f;
    float safetyCoeff_ = 0.001f;
    float meterCoeff_ = 0.001f;
    float dcPole_ = 0.997f;
    float bodyLowpassCoeff_ = 0.1f;
    float inputGainTarget_ = 1.0f;
    float inputGainSmoothed_ = 1.0f;
    float outputGainTarget_ = 0.7079458f;
    float outputGainSmoothed_ = 0.7079458f;
    float loopLowpassCoeffTarget_ = 0.1f;
    float loopLowpassCoeff_ = 0.1f;
    float derivedInputGainDb_ = 1000.0f;
    float derivedOutputGainDb_ = 1000.0f;
    float derivedColor_ = -1.0f;
};

struct MacroShredParams {
    float inputGainDb = 0.0f;
    float pressure = 0.28f;
    float shred = 0.18f;
    float feedback = 0.12f;
    float color = 0.55f;
    float react = 0.25f;
    float tune = 0.65f;
    float body = 0.65f;
    float spread = 0.0f;
    float deviation = 0.0f;
    float skew = 0.0f;
    float center = 0.5f;
    float glideMs = 250.0f;
    float mix = 0.65f;
    float outputGainDb = -3.0f;
};

class MacroShred {
public:
    void prepare(double sampleRate, uint32_t channels)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        channels_ = std::clamp<uint32_t>(channels, 0u, kMacroShredChannels);
        for (uint32_t ch = 0; ch < channels_; ++ch) {
            cores_[ch].prepare(sampleRate_);
        }
        reset();
    }

    void reset()
    {
        for (uint32_t ch = 0; ch < channels_; ++ch) {
            cores_[ch].reset();
        }
        smoothedSpread_ = params_.spread;
        smoothedDeviation_ = params_.deviation;
        smoothedSkew_ = params_.skew;
        smoothedCenter_ = params_.center;
    }

    void panic() { reset(); }

    void setParams(const MacroShredParams& params)
    {
        params_ = sanitize(params);
        relationshipSmoothingCoeff_ = 1.0f - std::exp(-1.0f
            / static_cast<float>(std::max(1.0, sampleRate_ * params_.glideMs * 0.001)));
    }

    MacroShredParams params() const { return params_; }

    void processFrame(const float* input, float* output)
    {
        if (!input || !output || channels_ == 0u) {
            return;
        }

        updateRelationshipSmoothing();
        const float denominator = static_cast<float>(std::max<uint32_t>(1u, channels_ - 1u));
        for (uint32_t ch = 0; ch < channels_; ++ch) {
            const float u = channels_ > 1u ? static_cast<float>(ch) / denominator : 0.5f;
            const float centered = channels_ > 1u ? clamp((u - smoothedCenter_) * 2.0f, -1.0f, 1.0f) : 0.0f;
            const float random = channels_ > 1u ? laneHash(ch) : 0.0f;
            const float laneDeviation = channels_ > 1u ? smoothedDeviation_ : 0.0f;

            MacroShredCoreParams lane;
            lane.inputGainDb = params_.inputGainDb;
            lane.pressure = params_.pressure;
            lane.shred = params_.shred;
            lane.feedback = params_.feedback * (1.0f - laneDeviation * 0.06f);
            lane.color = params_.color;
            lane.react = params_.react;
            lane.tune = params_.tune;
            lane.body = params_.body;
            lane.mix = params_.mix;
            lane.outputGainDb = params_.outputGainDb;
            lane.colorShiftOctaves = centered * smoothedSpread_ * 1.5f
                + random * laneDeviation * 0.75f
                + smoothedSkew_ * (u - 0.5f) * 0.75f;
            lane.intensityTrim = random * laneDeviation * 0.16f
                + smoothedSkew_ * (u - 0.5f) * 0.24f;
            cores_[ch].setParams(lane);
            output[ch] = cores_[ch].processSample(input[ch]);
        }
    }

    uint32_t channels() const { return channels_; }

    float feedbackActivity() const
    {
        float activity = 0.0f;
        for (uint32_t ch = 0; ch < channels_; ++ch) {
            activity = std::max(activity, cores_[ch].feedbackActivity());
        }
        return activity;
    }

    float centroidHz() const
    {
        if (channels_ == 0u) return 0.0f;
        float logSum = 0.0f;
        for (uint32_t ch = 0; ch < channels_; ++ch) {
            logSum += std::log2(std::max(1.0f, cores_[ch].centroidHz()));
        }
        return std::pow(2.0f, logSum / static_cast<float>(channels_));
    }

    float feedbackFrequencyHz() const
    {
        if (channels_ == 0u) return 0.0f;
        float logSum = 0.0f;
        for (uint32_t ch = 0; ch < channels_; ++ch) {
            logSum += std::log2(std::max(1.0f, cores_[ch].feedbackFrequencyHz()));
        }
        return std::pow(2.0f, logSum / static_cast<float>(channels_));
    }

private:
    static MacroShredParams sanitize(MacroShredParams params)
    {
        params.inputGainDb = clamp(params.inputGainDb, -24.0f, 36.0f);
        params.pressure = clamp(params.pressure, 0.0f, 1.0f);
        params.shred = clamp(params.shred, 0.0f, 1.0f);
        params.feedback = clamp(params.feedback, 0.0f, 1.0f);
        params.color = clamp(params.color, 0.0f, 1.0f);
        params.react = clamp(params.react, 0.0f, 1.0f);
        params.tune = clamp(params.tune, 0.0f, 1.0f);
        params.body = clamp(params.body, 0.0f, 1.0f);
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
        smoothedSpread_ += (params_.spread - smoothedSpread_) * relationshipSmoothingCoeff_;
        smoothedDeviation_ += (params_.deviation - smoothedDeviation_) * relationshipSmoothingCoeff_;
        smoothedSkew_ += (params_.skew - smoothedSkew_) * relationshipSmoothingCoeff_;
        smoothedCenter_ += (params_.center - smoothedCenter_) * relationshipSmoothingCoeff_;
    }

    double sampleRate_ = 48000.0;
    uint32_t channels_ = 0u;
    MacroShredParams params_ {};
    std::array<MacroShredCore, kMacroShredChannels> cores_ {};
    float smoothedSpread_ = 0.0f;
    float smoothedDeviation_ = 0.0f;
    float smoothedSkew_ = 0.0f;
    float smoothedCenter_ = 0.5f;
    float relationshipSmoothingCoeff_ = 0.0001f;
};

} // namespace s3g
