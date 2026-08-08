#pragma once

#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace s3g {

enum class DrumEchoHeadMode : uint32_t {
    Head1 = 0u,
    Head2,
    Head3,
    Head12,
    Head23,
    Head13,
    Heads123,
};

constexpr uint32_t kDrumEchoHeadModeCount = 7u;

inline const char* drumEchoHeadModeName(DrumEchoHeadMode mode)
{
    constexpr const char* names[kDrumEchoHeadModeCount] {
        "HEAD 1", "HEAD 2", "HEAD 3", "HEAD 1+2",
        "HEAD 2+3", "HEAD 1+3", "ALL HEADS",
    };
    return names[std::min<uint32_t>(
        static_cast<uint32_t>(mode), kDrumEchoHeadModeCount - 1u)];
}

enum class DrumEchoClock : uint32_t {
    Free = 0u,
    ThirtySecond,
    SixteenthTriplet,
    Sixteenth,
    EighthTriplet,
    Eighth,
    QuarterTriplet,
    Quarter,
    Half,
    Bar,
};

constexpr uint32_t kDrumEchoClockCount = 10u;

inline const char* drumEchoClockName(DrumEchoClock clock)
{
    constexpr const char* names[kDrumEchoClockCount] {
        "FREE", "1/32", "1/16T", "1/16", "1/8T",
        "1/8", "1/4T", "1/4", "1/2", "1 BAR",
    };
    return names[std::min<uint32_t>(
        static_cast<uint32_t>(clock), kDrumEchoClockCount - 1u)];
}

struct DrumEchoParams {
    DrumEchoHeadMode headMode = DrumEchoHeadMode::Heads123;
    DrumEchoClock clock = DrumEchoClock::Eighth;
    float timeMs = 180.0f;
    float feedback = 0.38f;
    float wear = 0.20f;
    float flutter = 0.12f;
    float transient = 0.35f;
    float sensitivity = 0.55f;
    float duck = 0.45f;
    float tone = 0.0f;
    float spread = 0.55f;
    float mix = 0.35f;
    float outputGainDb = -3.0f;
    bool bypass = false;
};

class DrumEcho {
public:
    bool prepare(double sampleRate, double maxDelaySeconds = 12.0)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        const double boundedMaximum = std::clamp(maxDelaySeconds, 1.0, 24.0);
        bufferSize_ = static_cast<uint32_t>(
            std::ceil(sampleRate_ * boundedMaximum)) + 4u;
        for (auto& buffer : buffers_) {
            buffer.assign(bufferSize_, 0.0f);
        }
        updateDetectorCoefficients();
        ready_ = true;
        reset();
        return true;
    }

    void reset()
    {
        for (auto& buffer : buffers_) {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
        }
        writePosition_ = 0u;
        fastEnvelope_ = 0.0f;
        slowEnvelope_ = 0.0f;
        duckEnvelope_ = 0.0f;
        transientActivity_ = 0.0f;
        duckGain_ = 1.0f;
        flutterPhaseA_ = 0.0f;
        flutterPhaseB_ = 0.37f;
        feedbackLowpass_.fill(0.0f);
        feedbackHighpass_.fill(0.0f);
        wetTone_.fill(0.0f);
        smoothedDelayMs_ = targetBaseDelayMs();
        smoothedMix_ = params_.mix;
        smoothedOutputGain_ = dbToGain(params_.outputGainDb);
    }

    void setParams(const DrumEchoParams& params)
    {
        params_ = sanitize(params);
    }

    DrumEchoParams params() const { return params_; }

    void setTempo(double beatsPerMinute, bool valid = true)
    {
        if (valid && std::isfinite(beatsPerMinute)
            && beatsPerMinute > 0.0) {
            tempoBpm_ = std::clamp(beatsPerMinute, 30.0, 400.0);
            tempoValid_ = true;
        } else {
            tempoBpm_ = 120.0;
            tempoValid_ = false;
        }
    }

    bool ready() const { return ready_; }
    float transientActivity() const { return transientActivity_; }
    float duckGain() const { return duckGain_; }

    uint32_t tailSamples() const
    {
        const double longestDelaySeconds = std::min(
            static_cast<double>(bufferSize_ - 4u) / sampleRate_,
            static_cast<double>(targetBaseDelayMs()) * 0.003);
        const double feedback = std::clamp(
            static_cast<double>(params_.feedback), 0.0, 0.92);
        const double repeats = feedback > 0.001
            ? std::ceil(std::log(0.001) / std::log(feedback)) : 1.0;
        return static_cast<uint32_t>(std::ceil(std::clamp(
            longestDelaySeconds * repeats + 0.25, 0.25, 120.0)
            * sampleRate_));
    }

    void processFrame(float& left, float& right)
    {
        if (!ready_ || bufferSize_ < 4u) return;
        const float dryLeft = flushDenormal(left);
        const float dryRight = flushDenormal(right);
        if (params_.bypass) {
            left = dryLeft;
            right = dryRight;
            return;
        }

        updateSmoothing();
        const float detectorInput = std::max(
            std::abs(dryLeft), std::abs(dryRight));
        updateTransientDetector(detectorInput);

        const float threshold = 0.002f
            * std::pow(32.0f, 1.0f - params_.sensitivity);
        const float transientDelta = std::max(
            0.0f, fastEnvelope_ - slowEnvelope_);
        const float transientGate = clamp(
            transientDelta / std::max(0.0001f, threshold * 3.0f),
            0.0f, 1.0f);
        transientActivity_ += (transientGate - transientActivity_) * 0.2f;

        const float duckTarget = clamp(
            (detectorInput - threshold) / std::max(0.02f, 0.35f - threshold),
            0.0f, 1.0f);
        const float duckCoefficient = duckTarget > duckEnvelope_
            ? duckAttackCoefficient_ : duckReleaseCoefficient_;
        duckEnvelope_ += (duckTarget - duckEnvelope_) * duckCoefficient;
        duckGain_ = 1.0f - params_.duck * 0.92f
            * std::sqrt(clamp(duckEnvelope_, 0.0f, 1.0f));

        const float flutterDepthSamples = params_.flutter
            * params_.flutter * static_cast<float>(sampleRate_) * 0.0025f;
        const float flutter = flutterDepthSamples
            * (0.68f * std::sin(6.28318530718f * flutterPhaseA_)
                + 0.32f * std::sin(6.28318530718f * flutterPhaseB_));
        advanceFlutter();

        std::array<float, 3u> tapLeft {};
        std::array<float, 3u> tapRight {};
        const auto active = activeHeads(params_.headMode);
        uint32_t activeCount = 0u;
        for (uint32_t head = 0u; head < 3u; ++head) {
            if (!active[head]) continue;
            ++activeCount;
            const float ratio = static_cast<float>(head + 1u);
            const float ageFlutter = flutter * (0.72f + 0.18f * ratio);
            const float delaySamples = clamp(
                smoothedDelayMs_ * ratio
                    * static_cast<float>(sampleRate_) * 0.001f
                    + ageFlutter,
                1.0f, static_cast<float>(bufferSize_ - 3u));
            tapLeft[head] = readDelay(0u, delaySamples);
            tapRight[head] = readDelay(1u, delaySamples);
        }

        const float tapNormalization = 1.0f
            / std::sqrt(static_cast<float>(std::max(1u, activeCount)));
        float wetLeft = 0.0f;
        float wetRight = 0.0f;
        float feedbackLeft = 0.0f;
        float feedbackRight = 0.0f;
        for (uint32_t head = 0u; head < 3u; ++head) {
            if (!active[head]) continue;
            const float headPan = (static_cast<float>(head) - 1.0f)
                * params_.spread;
            const float mid = 0.5f * (tapLeft[head] + tapRight[head]);
            const float side = 0.5f * (tapLeft[head] - tapRight[head])
                * (1.0f - params_.spread * 0.35f);
            const float panLeft = std::sqrt(0.5f * (1.0f - headPan));
            const float panRight = std::sqrt(0.5f * (1.0f + headPan));
            wetLeft += (mid * panLeft + side) * tapNormalization;
            wetRight += (mid * panRight - side) * tapNormalization;
            feedbackLeft += tapLeft[head];
            feedbackRight += tapRight[head];
        }
        feedbackLeft /= static_cast<float>(std::max(1u, activeCount));
        feedbackRight /= static_cast<float>(std::max(1u, activeCount));

        const float feedbackCross = params_.spread * 0.28f;
        const float crossedLeft = lerp(feedbackLeft, feedbackRight, feedbackCross);
        const float crossedRight = lerp(feedbackRight, feedbackLeft, feedbackCross);
        feedbackLeft = filterFeedback(0u, crossedLeft);
        feedbackRight = filterFeedback(1u, crossedRight);

        const float injectionGain = clamp(
            1.0f + params_.transient * transientGate, 0.0f, 2.0f);
        const float drive = 1.0f + params_.wear * 5.0f;
        const float normalization = 1.0f / std::tanh(drive);
        const float writeLeft = std::tanh(clamp(
            dryLeft * injectionGain
                + feedbackLeft * params_.feedback, -4.0f, 4.0f) * drive)
            * normalization;
        const float writeRight = std::tanh(clamp(
            dryRight * injectionGain
                + feedbackRight * params_.feedback, -4.0f, 4.0f) * drive)
            * normalization;
        buffers_[0][writePosition_] = flushDenormal(writeLeft);
        buffers_[1][writePosition_] = flushDenormal(writeRight);
        writePosition_ = (writePosition_ + 1u) % bufferSize_;

        wetLeft = filterWet(0u, wetLeft) * duckGain_;
        wetRight = filterWet(1u, wetRight) * duckGain_;
        const float outputGain = smoothedOutputGain_;
        left = softLimit(lerp(dryLeft, wetLeft, smoothedMix_) * outputGain);
        right = softLimit(lerp(dryRight, wetRight, smoothedMix_) * outputGain);
    }

private:
    static DrumEchoParams sanitize(DrumEchoParams params)
    {
        params.headMode = static_cast<DrumEchoHeadMode>(
            std::min<uint32_t>(static_cast<uint32_t>(params.headMode),
                kDrumEchoHeadModeCount - 1u));
        params.clock = static_cast<DrumEchoClock>(
            std::min<uint32_t>(static_cast<uint32_t>(params.clock),
                kDrumEchoClockCount - 1u));
        params.timeMs = clamp(params.timeMs, 20.0f, 1800.0f);
        params.feedback = clamp(params.feedback, 0.0f, 0.92f);
        params.wear = clamp(params.wear, 0.0f, 1.0f);
        params.flutter = clamp(params.flutter, 0.0f, 1.0f);
        params.transient = clamp(params.transient, -1.0f, 1.0f);
        params.sensitivity = clamp(params.sensitivity, 0.0f, 1.0f);
        params.duck = clamp(params.duck, 0.0f, 1.0f);
        params.tone = clamp(params.tone, -1.0f, 1.0f);
        params.spread = clamp(params.spread, 0.0f, 1.0f);
        params.mix = clamp(params.mix, 0.0f, 1.0f);
        params.outputGainDb = clamp(params.outputGainDb, -36.0f, 12.0f);
        return params;
    }

    static std::array<bool, 3u> activeHeads(DrumEchoHeadMode mode)
    {
        switch (mode) {
        case DrumEchoHeadMode::Head1: return { true, false, false };
        case DrumEchoHeadMode::Head2: return { false, true, false };
        case DrumEchoHeadMode::Head3: return { false, false, true };
        case DrumEchoHeadMode::Head12: return { true, true, false };
        case DrumEchoHeadMode::Head23: return { false, true, true };
        case DrumEchoHeadMode::Head13: return { true, false, true };
        case DrumEchoHeadMode::Heads123: return { true, true, true };
        }
        return { true, true, true };
    }

    static float clockBeats(DrumEchoClock clock)
    {
        switch (clock) {
        case DrumEchoClock::ThirtySecond: return 0.125f;
        case DrumEchoClock::SixteenthTriplet: return 1.0f / 6.0f;
        case DrumEchoClock::Sixteenth: return 0.25f;
        case DrumEchoClock::EighthTriplet: return 1.0f / 3.0f;
        case DrumEchoClock::Eighth: return 0.5f;
        case DrumEchoClock::QuarterTriplet: return 2.0f / 3.0f;
        case DrumEchoClock::Quarter: return 1.0f;
        case DrumEchoClock::Half: return 2.0f;
        case DrumEchoClock::Bar: return 4.0f;
        case DrumEchoClock::Free: return 0.0f;
        }
        return 0.0f;
    }

    float targetBaseDelayMs() const
    {
        if (params_.clock == DrumEchoClock::Free) return params_.timeMs;
        const float tempo = static_cast<float>(
            tempoValid_ ? tempoBpm_ : 120.0);
        return clamp(60000.0f / tempo * clockBeats(params_.clock),
            20.0f, 4000.0f);
    }

    void updateSmoothing()
    {
        const float delayCoefficient = 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate_ * 0.035));
        const float outputCoefficient = 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate_ * 0.008));
        smoothedDelayMs_ += (targetBaseDelayMs() - smoothedDelayMs_)
            * delayCoefficient;
        smoothedMix_ += (params_.mix - smoothedMix_) * outputCoefficient;
        smoothedOutputGain_ += (dbToGain(params_.outputGainDb)
            - smoothedOutputGain_) * outputCoefficient;
    }

    void updateDetectorCoefficients()
    {
        const auto coefficient = [this](double milliseconds) {
            return static_cast<float>(1.0 - std::exp(
                -1.0 / (sampleRate_ * milliseconds * 0.001)));
        };
        fastAttackCoefficient_ = coefficient(0.35);
        fastReleaseCoefficient_ = coefficient(24.0);
        slowAttackCoefficient_ = coefficient(9.0);
        slowReleaseCoefficient_ = coefficient(95.0);
        duckAttackCoefficient_ = coefficient(0.8);
        duckReleaseCoefficient_ = coefficient(150.0);
    }

    void updateTransientDetector(float value)
    {
        const float fastCoefficient = value > fastEnvelope_
            ? fastAttackCoefficient_ : fastReleaseCoefficient_;
        const float slowCoefficient = value > slowEnvelope_
            ? slowAttackCoefficient_ : slowReleaseCoefficient_;
        fastEnvelope_ += (value - fastEnvelope_) * fastCoefficient;
        slowEnvelope_ += (value - slowEnvelope_) * slowCoefficient;
    }

    void advanceFlutter()
    {
        flutterPhaseA_ += 0.37f / static_cast<float>(sampleRate_);
        flutterPhaseB_ += 0.83f / static_cast<float>(sampleRate_);
        flutterPhaseA_ -= std::floor(flutterPhaseA_);
        flutterPhaseB_ -= std::floor(flutterPhaseB_);
    }

    float readDelay(uint32_t channel, float delaySamples) const
    {
        float position = static_cast<float>(writePosition_) - delaySamples;
        const float size = static_cast<float>(bufferSize_);
        while (position < 0.0f) position += size;
        while (position >= size) position -= size;
        const uint32_t index0 = static_cast<uint32_t>(position);
        const uint32_t index1 = (index0 + 1u) % bufferSize_;
        return lerp(buffers_[channel][index0], buffers_[channel][index1],
            position - static_cast<float>(index0));
    }

    float filterFeedback(uint32_t channel, float sample)
    {
        const float normalizedTone = (params_.tone + 1.0f) * 0.5f;
        const float cutoff = 900.0f * std::pow(16.0f, normalizedTone)
            * (1.0f - params_.wear * 0.42f);
        const float lowpassCoefficient = 1.0f - std::exp(
            -6.28318530718f * std::min(
                cutoff, static_cast<float>(sampleRate_ * 0.45))
                / static_cast<float>(sampleRate_));
        feedbackLowpass_[channel] += (sample - feedbackLowpass_[channel])
            * lowpassCoefficient;
        const float highpassCoefficient = 1.0f - std::exp(
            -6.28318530718f * 42.0f / static_cast<float>(sampleRate_));
        feedbackHighpass_[channel] += (feedbackLowpass_[channel]
            - feedbackHighpass_[channel]) * highpassCoefficient;
        return flushDenormal(feedbackLowpass_[channel]
            - feedbackHighpass_[channel]);
    }

    float filterWet(uint32_t channel, float sample)
    {
        const float cutoff = 1500.0f * std::pow(
            8.0f, (params_.tone + 1.0f) * 0.5f);
        const float coefficient = 1.0f - std::exp(
            -6.28318530718f * std::min(
                cutoff, static_cast<float>(sampleRate_ * 0.45))
                / static_cast<float>(sampleRate_));
        wetTone_[channel] += (sample - wetTone_[channel]) * coefficient;
        return flushDenormal(wetTone_[channel]);
    }

    static float softLimit(float value)
    {
        return std::tanh(clamp(value, -8.0f, 8.0f));
    }

    double sampleRate_ = 48000.0;
    double tempoBpm_ = 120.0;
    bool tempoValid_ = false;
    bool ready_ = false;
    uint32_t bufferSize_ = 0u;
    uint32_t writePosition_ = 0u;
    DrumEchoParams params_ {};
    std::array<std::vector<float>, 2u> buffers_ {};
    std::array<float, 2u> feedbackLowpass_ {};
    std::array<float, 2u> feedbackHighpass_ {};
    std::array<float, 2u> wetTone_ {};
    float fastEnvelope_ = 0.0f;
    float slowEnvelope_ = 0.0f;
    float duckEnvelope_ = 0.0f;
    float transientActivity_ = 0.0f;
    float duckGain_ = 1.0f;
    float fastAttackCoefficient_ = 0.1f;
    float fastReleaseCoefficient_ = 0.001f;
    float slowAttackCoefficient_ = 0.01f;
    float slowReleaseCoefficient_ = 0.0001f;
    float duckAttackCoefficient_ = 0.1f;
    float duckReleaseCoefficient_ = 0.0001f;
    float flutterPhaseA_ = 0.0f;
    float flutterPhaseB_ = 0.37f;
    float smoothedDelayMs_ = 180.0f;
    float smoothedMix_ = 0.35f;
    float smoothedOutputGain_ = 1.0f;
};

} // namespace s3g
