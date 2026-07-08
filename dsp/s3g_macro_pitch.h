#pragma once

#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace s3g {

constexpr uint32_t kMacroPitchChannels = 24;

struct MacroPitchParams {
    float pitchSemitones = 0.0f;
    float fineCents = 0.0f;
    float windowMs = 80.0f;
    float spread = 0.0f;
    float deviation = 0.0f;
    float skew = 0.0f;
    float center = 0.5f;
    float glideMs = 250.0f;
    float mix = 0.35f;
    float outputGainDb = 0.0f;
};

class MacroPitch {
public:
    void prepare(double sampleRate, uint32_t channels, double maxWindowSeconds = 0.35)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        channels_ = std::clamp<uint32_t>(channels, 0u, kMacroPitchChannels);
        bufferFrames_ = static_cast<uint32_t>(std::max(256.0, std::ceil(sampleRate_ * maxWindowSeconds) + 8.0));
        for (auto& lane : lanes_) {
            lane.buffer.assign(bufferFrames_, 0.0f);
        }
        reset();
    }

    void reset()
    {
        for (uint32_t ch = 0; ch < kMacroPitchChannels; ++ch) {
            auto& lane = lanes_[ch];
            std::fill(lane.buffer.begin(), lane.buffer.end(), 0.0f);
            lane.write = 0u;
            lane.phaseA = 0.0f;
            lane.phaseB = 0.5f;
            lane.lfo = laneHash(ch) * 0.5f + 0.5f;
            lane.ratio = 1.0f;
            lane.windowSamples = std::max(8.0f, params_.windowMs * 0.001f * static_cast<float>(sampleRate_));
        }
        smoothedPitch_ = params_.pitchSemitones;
        smoothedFine_ = params_.fineCents;
        smoothedSpread_ = params_.spread;
        smoothedDeviation_ = params_.deviation;
        smoothedSkew_ = params_.skew;
        smoothedCenter_ = params_.center;
        smoothedWindowMs_ = params_.windowMs;
        mixSmoothed_ = params_.mix;
        gainSmoothed_ = dbToGain(params_.outputGainDb);
    }

    void setParams(const MacroPitchParams& params)
    {
        params_ = sanitize(params);
    }

    MacroPitchParams params() const { return params_; }

    void processFrame(const float* input, float* output)
    {
        if (!input || !output || channels_ == 0u || bufferFrames_ < 8u) {
            return;
        }

        updateSmoothing();
        const float mixTarget = params_.mix;
        const float gainTarget = dbToGain(params_.outputGainDb);
        mixSmoothed_ += (mixTarget - mixSmoothed_) * 0.0015f;
        gainSmoothed_ += (gainTarget - gainSmoothed_) * 0.0015f;

        for (uint32_t ch = 0; ch < channels_; ++ch) {
            auto& lane = lanes_[ch];
            const float dry = input[ch];
            lane.buffer[lane.write] = dry;

            const float u = channels_ > 1u
                ? static_cast<float>(ch) / static_cast<float>(channels_ - 1u)
                : 0.5f;
            const float centered = clamp((u - smoothedCenter_) * 2.0f, -1.0f, 1.0f);
            lane.lfo += (0.012f + 0.003f * static_cast<float>(ch)) / static_cast<float>(sampleRate_);
            if (lane.lfo >= 1.0f) {
                lane.lfo -= std::floor(lane.lfo);
            }
            const float deviation = laneHash(ch) * smoothedDeviation_ * 3.0f;
            const float skew = smoothedSkew_ * u * 6.0f;
            const float semis = smoothedPitch_ + smoothedFine_ * 0.01f + centered * smoothedSpread_ * 12.0f + deviation + skew;
            const float targetRatio = std::pow(2.0f, clamp(semis, -24.0f, 24.0f) / 12.0f);
            lane.ratio += (targetRatio - lane.ratio) * 0.0012f;

            const float targetWindow = clamp(smoothedWindowMs_, 20.0f, 180.0f) * 0.001f * static_cast<float>(sampleRate_);
            lane.windowSamples += (targetWindow - lane.windowSamples) * 0.0012f;
            lane.windowSamples = clamp(lane.windowSamples, 8.0f, static_cast<float>(bufferFrames_ - 4u));

            const float wet = processLane(lane);
            const float value = dry + (wet - dry) * mixSmoothed_;
            output[ch] = softLimit(flushDenormal(value * gainSmoothed_));

            lane.write = (lane.write + 1u) % bufferFrames_;
        }
    }

    uint32_t channels() const { return channels_; }

private:
    struct Lane {
        std::vector<float> buffer;
        uint32_t write = 0u;
        float phaseA = 0.0f;
        float phaseB = 0.5f;
        float lfo = 0.0f;
        float ratio = 1.0f;
        float windowSamples = 3840.0f;
    };

    static MacroPitchParams sanitize(MacroPitchParams params)
    {
        params.pitchSemitones = clamp(params.pitchSemitones, -24.0f, 24.0f);
        params.fineCents = clamp(params.fineCents, -100.0f, 100.0f);
        params.windowMs = clamp(params.windowMs, 20.0f, 180.0f);
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

    void updateSmoothing()
    {
        const float coeff = 1.0f - std::exp(-1.0f / static_cast<float>(std::max(1.0, sampleRate_ * params_.glideMs * 0.001)));
        smoothedPitch_ += (params_.pitchSemitones - smoothedPitch_) * coeff;
        smoothedFine_ += (params_.fineCents - smoothedFine_) * coeff;
        smoothedSpread_ += (params_.spread - smoothedSpread_) * coeff;
        smoothedDeviation_ += (params_.deviation - smoothedDeviation_) * coeff;
        smoothedSkew_ += (params_.skew - smoothedSkew_) * coeff;
        smoothedCenter_ += (params_.center - smoothedCenter_) * coeff;
        smoothedWindowMs_ += (params_.windowMs - smoothedWindowMs_) * coeff;
    }

    float processLane(Lane& lane)
    {
        const float ratioDelta = std::fabs(lane.ratio - 1.0f);
        if (ratioDelta < 0.00015f) {
            return lane.buffer[lane.write];
        }

        const float phaseStep = ratioDelta / std::max(8.0f, lane.windowSamples);
        lane.phaseA += phaseStep;
        lane.phaseB += phaseStep;
        lane.phaseA -= std::floor(lane.phaseA);
        lane.phaseB -= std::floor(lane.phaseB);

        const float a = readPitchPhase(lane, lane.phaseA);
        const float b = readPitchPhase(lane, lane.phaseB);
        const float wa = grainWindow(lane.phaseA);
        const float wb = grainWindow(lane.phaseB);
        const float denom = std::max(0.0001f, wa + wb);
        return (a * wa + b * wb) / denom;
    }

    float readPitchPhase(const Lane& lane, float phase) const
    {
        const float delay = lane.ratio >= 1.0f
            ? lane.windowSamples * (1.0f - phase)
            : lane.windowSamples * phase;
        return readDelay(lane, clamp(delay, 1.0f, static_cast<float>(bufferFrames_ - 4u)));
    }

    static float grainWindow(float phase)
    {
        return 0.5f - 0.5f * std::cos(6.28318530718f * clamp(phase, 0.0f, 1.0f));
    }

    float readDelay(const Lane& lane, float delaySamples) const
    {
        float read = static_cast<float>(lane.write) - delaySamples;
        while (read < 0.0f) {
            read += static_cast<float>(bufferFrames_);
        }
        while (read >= static_cast<float>(bufferFrames_)) {
            read -= static_cast<float>(bufferFrames_);
        }
        const uint32_t i0 = static_cast<uint32_t>(read) % bufferFrames_;
        const uint32_t i1 = (i0 + 1u) % bufferFrames_;
        const float frac = read - std::floor(read);
        return lane.buffer[i0] + (lane.buffer[i1] - lane.buffer[i0]) * frac;
    }

    double sampleRate_ = 48000.0;
    uint32_t channels_ = 0;
    uint32_t bufferFrames_ = 0;
    MacroPitchParams params_ {};
    float smoothedPitch_ = 0.0f;
    float smoothedFine_ = 0.0f;
    float smoothedSpread_ = 0.0f;
    float smoothedDeviation_ = 0.0f;
    float smoothedSkew_ = 0.0f;
    float smoothedCenter_ = 0.5f;
    float smoothedWindowMs_ = 80.0f;
    float mixSmoothed_ = 0.35f;
    float gainSmoothed_ = 1.0f;
    std::array<Lane, kMacroPitchChannels> lanes_ {};
};

} // namespace s3g
