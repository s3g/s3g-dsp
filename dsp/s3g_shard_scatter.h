#pragma once

#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace s3g {

constexpr uint32_t kShardScatterChannels = 16;

struct ShardScatterParams {
    float density = 5.0f;
    float grainMs = 180.0f;
    float guardMs = 260.0f;
    float scatterMs = 700.0f;
    float pitch = 1.0f;
    float pitchSpread = 0.15f;
    float rotate = 0.18f;
    float width = 0.18f;
    float feedback = 0.12f;
    float freeze = 0.0f;
    float dry = 0.08f;
    float wet = 0.9f;
    float gainDb = -2.5f;
    float stereo = 0.0f;
};

class ShardScatter {
public:
    bool prepare(double sampleRate, uint32_t maxBlockFrames = 4096u, double maxBufferSeconds = 4.0)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        maxBlockFrames_ = std::max<uint32_t>(1u, maxBlockFrames);
        bufferSize_ = static_cast<uint32_t>(std::clamp(sampleRate_ * maxBufferSeconds, 48000.0, 384000.0));
        buffer_.assign(bufferSize_, 0.0f);
        writePos_ = 0u;
        fill_ = 0.0f;
        phase_ = 0.0f;
        boot_ = 0u;
        silenceSamples_ = 0u;
        stopFade_ = 1.0f;
        writeSmooth_ = 0.0f;
        smooth_ = params_;
        for (uint32_t ch = 0; ch < kShardScatterChannels; ++ch) {
            shard_[ch].phase = static_cast<float>(ch) / static_cast<float>(kShardScatterChannels);
            shard_[ch].anchor = std::min<float>(static_cast<float>(bufferSize_ - 1u), 12000.0f + static_cast<float>(ch) * 3000.0f);
            shard_[ch].seed = kInitialSeeds[ch];
            shard_[ch].out = 0.0f;
        }
        ready_ = true;
        return true;
    }

    void reset()
    {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        writePos_ = 0u;
        fill_ = 0.0f;
        phase_ = 0.0f;
        boot_ = 0u;
        silenceSamples_ = 0u;
        stopFade_ = 1.0f;
        writeSmooth_ = 0.0f;
        smooth_ = params_;
        for (uint32_t ch = 0; ch < kShardScatterChannels; ++ch) {
            shard_[ch].phase = static_cast<float>(ch) / static_cast<float>(kShardScatterChannels);
            shard_[ch].anchor = std::min<float>(static_cast<float>(bufferSize_ - 1u), 12000.0f + static_cast<float>(ch) * 3000.0f);
            shard_[ch].seed = kInitialSeeds[ch];
            shard_[ch].out = 0.0f;
        }
    }

    void setParams(const ShardScatterParams& params)
    {
        params_ = sanitize(params);
    }

    ShardScatterParams params() const { return params_; }
    uint32_t outputChannels() const { return kShardScatterChannels; }
    bool ready() const { return ready_; }

    void process(const float* left, const float* right, float* const* output, uint32_t frames)
    {
        if (!ready_ || !output || frames == 0u) return;
        frames = std::min(frames, maxBlockFrames_);
        for (uint32_t i = 0; i < frames; ++i) {
            updateSmoothing();
            const float inL = left ? left[i] : 0.0f;
            const float inR = right ? right[i] : inL;
            const float src = (inL + inR * smooth_.stereo) / (1.0f + smooth_.stereo);
            updateStopFade(src);
            const float writeSrc = smoothedInputForBuffer(src);
            writeInput(writeSrc);
            phase_ += smooth_.rotate / static_cast<float>(sampleRate_);
            phase_ -= std::floor(phase_);

            const float grainSamps = clamp(smooth_.grainMs * static_cast<float>(sampleRate_) * 0.001f, 16.0f, static_cast<float>(bufferSize_) * 0.25f);
            const float guardSamps = clamp(smooth_.guardMs * static_cast<float>(sampleRate_) * 0.001f, grainSamps + 8.0f, static_cast<float>(bufferSize_) * 0.8f);
            const float scatterSamps = clamp(smooth_.scatterMs * static_cast<float>(sampleRate_) * 0.001f, 0.0f, static_cast<float>(bufferSize_) * 0.8f);
            const float incBase = clamp(smooth_.density, 0.2f, 24.0f) / static_cast<float>(sampleRate_);
            const float fillFade = fillEnvelope(guardSamps);
            const float bootFade = bootEnvelope();
            const float scatterGuard = lowDensityScatterGuard();

            std::array<float, kShardScatterChannels> grains {};
            for (uint32_t ch = 0; ch < kShardScatterChannels; ++ch) {
                stepShard(ch, incBase, grainSamps, guardSamps, scatterSamps);
                grains[ch] = readShard(ch, grainSamps) * fillFade;
            }

            const float bleed = clamp(smooth_.width, 0.0f, 1.0f) * 0.5f;
            const float norm = dbToGain(smooth_.gainDb) / (1.0f + bleed * 2.0f + 0.000001f);
            const float drySig = src * smooth_.dry;
            for (uint32_t ch = 0; ch < kShardScatterChannels; ++ch) {
                const uint32_t prev = ch == 0u ? kShardScatterChannels - 1u : ch - 1u;
                const uint32_t next = (ch + 1u) % kShardScatterChannels;
                const float wetSig = (grains[ch] + (grains[prev] + grains[next]) * bleed) * smooth_.wet * norm * bootFade * stopFade_;
                const float target = drySig + wetSig;
                const float outCoef = lerp(0.2f, 0.065f, scatterGuard);
                shard_[ch].out += (target - shard_[ch].out) * outCoef;
                output[ch][i] = softLimit(shard_[ch].out);
            }
        }
    }

private:
    struct ShardState {
        float phase = 0.0f;
        float anchor = 0.0f;
        float seed = 0.0f;
        float out = 0.0f;
    };

    static constexpr std::array<float, kShardScatterChannels> kPhaseRates {
        1.00f, 0.91f, 1.13f, 0.84f, 1.21f, 0.96f, 1.07f, 0.78f,
        1.31f, 0.88f, 1.17f, 0.73f, 1.26f, 0.82f, 1.09f, 0.94f
    };
    static constexpr std::array<float, kShardScatterChannels> kSeedSteps {
        0.173f, 0.191f, 0.229f, 0.251f, 0.277f, 0.307f, 0.331f, 0.359f,
        0.383f, 0.419f, 0.443f, 0.467f, 0.491f, 0.521f, 0.547f, 0.571f
    };
    static constexpr std::array<float, kShardScatterChannels> kRandFreqs {
        437.1f, 391.7f, 353.3f, 317.9f, 283.1f, 251.9f, 229.3f, 197.7f,
        173.9f, 149.3f, 421.9f, 367.7f, 337.1f, 269.3f, 211.7f, 181.1f
    };
    static constexpr std::array<float, kShardScatterChannels> kInitialSeeds {
        0.11f, 0.23f, 0.37f, 0.41f, 0.53f, 0.67f, 0.71f, 0.83f,
        0.89f, 0.97f, 0.07f, 0.19f, 0.29f, 0.43f, 0.61f, 0.79f
    };
    static constexpr std::array<float, kShardScatterChannels> kPitchSpreadShape {
        -0.45f, 0.19f, -0.31f, 0.37f, -0.12f, 0.28f, -0.23f, 0.49f,
        -0.38f, 0.08f, 0.34f, -0.17f, 0.41f, -0.29f, 0.14f, -0.52f
    };

    static ShardScatterParams sanitize(ShardScatterParams p)
    {
        p.density = clamp(p.density, 0.2f, 24.0f);
        p.grainMs = clamp(p.grainMs, 20.0f, 900.0f);
        p.guardMs = clamp(p.guardMs, 20.0f, 1800.0f);
        p.scatterMs = clamp(p.scatterMs, 0.0f, 2500.0f);
        p.pitch = clamp(p.pitch, -2.0f, 2.0f);
        p.pitchSpread = clamp(p.pitchSpread, 0.0f, 1.5f);
        p.rotate = clamp(p.rotate, -4.0f, 4.0f);
        p.width = clamp(p.width, 0.0f, 1.0f);
        p.feedback = clamp(p.feedback, 0.0f, 0.72f);
        p.freeze = clamp(p.freeze, 0.0f, 1.0f);
        p.dry = clamp(p.dry, 0.0f, 1.0f);
        p.wet = clamp(p.wet, 0.0f, 1.0f);
        p.gainDb = clamp(p.gainDb, -60.0f, 12.0f);
        p.stereo = clamp(p.stereo, 0.0f, 1.0f);
        return p;
    }

    void updateSmoothing()
    {
        constexpr float fast = 0.005f;
        constexpr float medium = 0.0015f;
        constexpr float slow = 0.00045f;
        smooth_.density += (params_.density - smooth_.density) * medium;
        smooth_.grainMs += (params_.grainMs - smooth_.grainMs) * slow;
        smooth_.guardMs += (params_.guardMs - smooth_.guardMs) * slow;
        smooth_.scatterMs += (params_.scatterMs - smooth_.scatterMs) * slow;
        const float pitchTarget = std::abs(params_.pitch) < 0.01f ? 0.0f : params_.pitch;
        smooth_.pitch += (pitchTarget - smooth_.pitch) * slow;
        smooth_.pitchSpread += (params_.pitchSpread - smooth_.pitchSpread) * medium;
        smooth_.rotate += (params_.rotate - smooth_.rotate) * slow;
        smooth_.width += (params_.width - smooth_.width) * medium;
        smooth_.feedback += (params_.feedback - smooth_.feedback) * medium;
        smooth_.freeze += (params_.freeze - smooth_.freeze) * medium;
        smooth_.dry += (params_.dry - smooth_.dry) * fast;
        smooth_.wet += (params_.wet - smooth_.wet) * fast;
        smooth_.gainDb += (params_.gainDb - smooth_.gainDb) * fast;
        smooth_.stereo += (params_.stereo - smooth_.stereo) * medium;
    }

    void writeInput(float src)
    {
        const float old = buffer_[writePos_];
        const float fb = 0.58f * smooth_.feedback * smooth_.feedback;
        buffer_[writePos_] = lerp(src, old * fb + src, fb) * (1.0f - smooth_.freeze) + old * smooth_.freeze;
        writePos_ = (writePos_ + 1u) % bufferSize_;
        fill_ = std::min(fill_ + (1.0f - smooth_.freeze), static_cast<float>(bufferSize_ - 1u));
        boot_ = std::min<uint32_t>(boot_ + 1u, static_cast<uint32_t>(sampleRate_ * 0.05));
    }

    float fillEnvelope(float guardSamps) const
    {
        const float x = clamp(fill_ / std::max(guardSamps, 1.0f), 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    }

    float bootEnvelope() const
    {
        const float denom = std::max(1.0f, static_cast<float>(sampleRate_ * 0.05));
        return clamp(static_cast<float>(boot_) / denom, 0.0f, 1.0f);
    }

    float smoothedInputForBuffer(float src)
    {
        if (silenceSamples_ == 0u) {
            writeSmooth_ = src;
            return src;
        }
        const float samples = std::max(1.0f, static_cast<float>(sampleRate_ * 0.006));
        writeSmooth_ += (0.0f - writeSmooth_) * (1.0f - std::exp(-1.0f / samples));
        return writeSmooth_;
    }

    void updateStopFade(float src)
    {
        const bool active = std::abs(src) > 0.00001f;
        if (active) silenceSamples_ = 0u;
        else silenceSamples_ = std::min<uint32_t>(silenceSamples_ + 1u, static_cast<uint32_t>(sampleRate_));

        const uint32_t holdSamples = static_cast<uint32_t>(sampleRate_ * 0.018);
        const float target = silenceSamples_ > holdSamples ? 0.0f : 1.0f;
        const float ms = target > stopFade_ ? 12.0f : 85.0f;
        const float samples = std::max(1.0f, static_cast<float>(sampleRate_ * ms * 0.001));
        stopFade_ += (target - stopFade_) * (1.0f - std::exp(-1.0f / samples));
    }

    void stepShard(uint32_t ch, float incBase, float grainSamps, float guardSamps, float scatterSamps)
    {
        auto& s = shard_[ch];
        const float next = s.phase + incBase * kPhaseRates[ch];
        const bool trigger = next >= 1.0f;
        s.phase = next - std::floor(next);
        if (!trigger) return;
        s.seed += kSeedSteps[ch];
        const float random = fract(std::sin((s.seed + phase_) * kRandFreqs[ch]) * 43758.5453f);
        s.anchor = wrap(static_cast<float>(writePos_) - guardSamps - scatterSamps * random - grainSamps);
    }

    float readShard(uint32_t ch, float grainSamps) const
    {
        const auto& s = shard_[ch];
        const float pitchRatio = smooth_.pitch * (1.0f + smooth_.pitchSpread * kPitchSpreadShape[ch]);
        const float pos = wrap(s.anchor + s.phase * grainSamps * pitchRatio);
        const float guard = lowDensityScatterGuard();
        const float shaped = lerp(std::pow(std::sin(s.phase * kPi), 2.0f),
                                  std::pow(std::sin(s.phase * kPi), 3.0f),
                                  guard);
        return readBuffer(pos, guard) * shaped;
    }

    float readBuffer(float pos, float smoothAmount) const
    {
        const uint32_t i0 = static_cast<uint32_t>(std::floor(pos));
        const uint32_t i1 = (i0 + 1u) % bufferSize_;
        const float frac = pos - static_cast<float>(i0);
        const float direct = lerp(buffer_[i0], buffer_[i1], frac);
        if (smoothAmount <= 0.0001f) return direct;

        const float wide = readLinear(pos - 2.0f) * 0.0625f
                         + readLinear(pos - 1.0f) * 0.25f
                         + direct * 0.375f
                         + readLinear(pos + 1.0f) * 0.25f
                         + readLinear(pos + 2.0f) * 0.0625f;
        return lerp(direct, wide, smoothAmount);
    }

    float readLinear(float pos) const
    {
        pos = wrap(pos);
        const uint32_t i0 = static_cast<uint32_t>(std::floor(pos));
        const uint32_t i1 = (i0 + 1u) % bufferSize_;
        return lerp(buffer_[i0], buffer_[i1], pos - static_cast<float>(i0));
    }

    float lowDensityScatterGuard() const
    {
        const float densityRisk = smoothstep01((4.2f - smooth_.density) / 3.8f);
        const float scatterRisk = smoothstep01((smooth_.scatterMs - 850.0f) / 1200.0f);
        return densityRisk * scatterRisk;
    }

    static float smoothstep01(float x)
    {
        x = clamp(x, 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    }

    float wrap(float pos) const
    {
        const float size = static_cast<float>(bufferSize_);
        pos -= size * std::floor(pos / size);
        return pos;
    }

    static float fract(float value)
    {
        return value - std::floor(value);
    }

    static float softLimit(float value)
    {
        return std::tanh(clamp(value, -8.0f, 8.0f));
    }

    double sampleRate_ = 48000.0;
    uint32_t maxBlockFrames_ = 0u;
    uint32_t bufferSize_ = 0u;
    uint32_t writePos_ = 0u;
    float fill_ = 0.0f;
    float phase_ = 0.0f;
    uint32_t boot_ = 0u;
    uint32_t silenceSamples_ = 0u;
    float stopFade_ = 1.0f;
    float writeSmooth_ = 0.0f;
    bool ready_ = false;
    ShardScatterParams params_ {};
    ShardScatterParams smooth_ {};
    std::vector<float> buffer_;
    std::array<ShardState, kShardScatterChannels> shard_ {};
};

} // namespace s3g
