#pragma once

#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace s3g {

constexpr uint32_t kCascadeTapsChannels = 16;

struct CascadeTapsParams {
    float pos = 1.0f;
    float rotate = 0.0f;
    float direction = 1.0f;
    float baseMs = 25.0f;
    float stepMs = 85.0f;
    float decay = 0.78f;
    float damp = 0.25f;
    float dry = 0.12f;
    float wet = 1.0f;
    float gainDb = 0.0f;
    float stereo = 1.0f;
    float soft = 0.35f;
};

class CascadeTaps {
public:
    bool prepare(double sampleRate, uint32_t maxBlockFrames = 4096u, double maxDelaySeconds = 4.0)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        maxBlockFrames_ = std::max<uint32_t>(1u, maxBlockFrames);
        bufferSize_ = static_cast<uint32_t>(std::clamp(sampleRate_ * maxDelaySeconds, 48000.0, 384000.0));
        buffer_.assign(bufferSize_, 0.0f);
        writePos_ = 0u;
        phase_ = 0.0f;
        smooth_ = params_;
        writeSmooth_ = 0.0f;
        lp_.fill(0.0f);
        out_.fill(0.0f);
        ready_ = true;
        return true;
    }

    void reset()
    {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        writePos_ = 0u;
        phase_ = 0.0f;
        smooth_ = params_;
        writeSmooth_ = 0.0f;
        lp_.fill(0.0f);
        out_.fill(0.0f);
    }

    void setParams(const CascadeTapsParams& params) { params_ = sanitize(params); }
    CascadeTapsParams params() const { return params_; }
    void setOutputChannels(uint32_t channels)
    {
        channels = std::clamp<uint32_t>(channels, 2u, kCascadeTapsChannels);
        if (channels == outputChannels_) return;
        outputChannels_ = channels;
        lp_.fill(0.0f);
        out_.fill(0.0f);
    }
    uint32_t outputChannels() const { return outputChannels_; }
    bool ready() const { return ready_; }

    void process(const float* left, const float* right, float* const* output, uint32_t frames)
    {
        if (!ready_ || !output || frames == 0u) return;
        frames = std::min(frames, maxBlockFrames_);
        for (uint32_t i = 0; i < frames; ++i) {
            updateSmoothing();
            phase_ += smooth_.rotate / static_cast<float>(sampleRate_);
            phase_ -= std::floor(phase_);
            const float inL = left ? left[i] : 0.0f;
            const float inR = right ? right[i] : inL;
            const float src = (inL + inR * smooth_.stereo)
                / std::sqrt(1.0f + smooth_.stereo * smooth_.stereo);
            writeSmooth_ += (src - writeSmooth_) * inputSmoothingCoef();
            buffer_[writePos_] = softLimit(writeSmooth_);
            float start = smooth_.pos - 1.0f + phase_ * static_cast<float>(outputChannels_);
            start = wrapRing(start);
            const bool forward = smooth_.direction >= 0.0f;
            for (uint32_t ch = 0; ch < outputChannels_; ++ch) {
                float f = static_cast<float>(ch) - start;
                if (f < 0.0f) f += static_cast<float>(outputChannels_);
                float r = start - static_cast<float>(ch);
                if (r < 0.0f) r += static_cast<float>(outputChannels_);
                const float order = forward ? f : r;
                const float delay = clamp((smooth_.baseMs
                    + smooth_.stepMs * order) * static_cast<float>(sampleRate_)
                    * 0.001f, 1.0f, static_cast<float>(bufferSize_ - 2u));
                const float directTap = readDelay(delay);
                const float softenedTap = readDelaySoft(delay);
                const float tap = lerp(directTap, softenedTap,
                    smooth_.soft * 0.65f)
                    * std::pow(smooth_.decay + 0.000001f, order);
                const float coef = std::max(0.02f, 1.0f - smooth_.damp)
                    * lerp(1.0f, 0.72f, smooth_.soft);
                lp_[ch] += (tap - lp_[ch]) * coef;
                const float leftDryGain = ringPanGain(ch, start);
                const float rightDryGain = ringPanGain(ch, wrapRing(start
                    + 0.5f * smooth_.stereo
                        * static_cast<float>(outputChannels_)));
                const float drySig = (inL * leftDryGain
                    + inR * smooth_.stereo * rightDryGain) * smooth_.dry;
                const float target = (drySig + lp_[ch] * smooth_.wet)
                    * dbToGain(smooth_.gainDb);
                const float next = out_[ch] + (target - out_[ch]) * outputSmoothCoef();
                out_[ch] = next;
                output[ch][i] = softLimit(out_[ch]);
            }
            writePos_ = (writePos_ + 1u) % bufferSize_;
        }
    }

private:
    static CascadeTapsParams sanitize(CascadeTapsParams p)
    {
        p.pos = clamp(p.pos, 1.0f, static_cast<float>(kCascadeTapsChannels));
        p.rotate = clamp(p.rotate, -4.0f, 4.0f);
        p.direction = clamp(p.direction, -1.0f, 1.0f);
        p.baseMs = clamp(p.baseMs, 1.0f, 1000.0f);
        p.stepMs = clamp(p.stepMs, 1.0f, 500.0f);
        p.decay = clamp(p.decay, 0.0f, 0.98f);
        p.damp = clamp(p.damp, 0.0f, 1.0f);
        p.dry = clamp(p.dry, 0.0f, 1.0f);
        p.wet = clamp(p.wet, 0.0f, 1.0f);
        p.gainDb = clamp(p.gainDb, -60.0f, 12.0f);
        p.stereo = clamp(p.stereo, 0.0f, 1.0f);
        p.soft = clamp(p.soft, 0.0f, 1.0f);
        return p;
    }

    void updateSmoothing()
    {
        constexpr float fast = 0.005f;
        constexpr float medium = 0.0015f;
        constexpr float slow = 0.00045f;
        smooth_.pos += (params_.pos - smooth_.pos) * medium;
        smooth_.rotate += (params_.rotate - smooth_.rotate) * slow;
        smooth_.direction += (params_.direction - smooth_.direction) * medium;
        smooth_.baseMs += (params_.baseMs - smooth_.baseMs) * slow;
        smooth_.stepMs += (params_.stepMs - smooth_.stepMs) * slow;
        smooth_.decay += (params_.decay - smooth_.decay) * medium;
        smooth_.damp += (params_.damp - smooth_.damp) * medium;
        smooth_.dry += (params_.dry - smooth_.dry) * fast;
        smooth_.wet += (params_.wet - smooth_.wet) * fast;
        smooth_.gainDb += (params_.gainDb - smooth_.gainDb) * fast;
        smooth_.stereo += (params_.stereo - smooth_.stereo) * medium;
        smooth_.soft += (params_.soft - smooth_.soft) * medium;
    }

    float readDelay(float delay) const
    {
        float pos = static_cast<float>(writePos_) - delay;
        pos += static_cast<float>(bufferSize_) * (pos < 0.0f ? 1.0f : 0.0f);
        return readLinear(pos);
    }

    float readDelaySoft(float delay) const
    {
        float pos = static_cast<float>(writePos_) - delay;
        pos += static_cast<float>(bufferSize_) * (pos < 0.0f ? 1.0f : 0.0f);
        return readLinear(pos - 1.0f) * 0.20f + readLinear(pos) * 0.60f + readLinear(pos + 1.0f) * 0.20f;
    }

    float readLinear(float pos) const
    {
        pos = wrapBuffer(pos);
        const uint32_t i0 = static_cast<uint32_t>(std::floor(pos));
        const uint32_t i1 = (i0 + 1u) % bufferSize_;
        return lerp(buffer_[i0], buffer_[i1], pos - static_cast<float>(i0));
    }

    float wrapBuffer(float pos) const
    {
        const float size = static_cast<float>(bufferSize_);
        pos -= size * std::floor(pos / size);
        // A negative position immediately below zero can round to exactly
        // `size` here. Preserve the strict [0, size) indexing contract at
        // that interpolation boundary.
        return pos >= 0.0f && pos < size ? pos : 0.0f;
    }

    float inputSmoothingCoef() const
    {
        const float seconds = lerp(0.00005f, 0.0015f, smooth_.soft);
        const float samples = std::max(1.0f,
            static_cast<float>(sampleRate_ * seconds));
        return 1.0f - std::exp(-1.0f / samples);
    }

    float outputSmoothCoef() const
    {
        return lerp(0.18f, 0.10f, smooth_.soft);
    }

    float wrapRing(float v) const
    {
        const float n = static_cast<float>(outputChannels_);
        v -= std::floor(v / n) * n;
        return v;
    }

    float ringPanGain(uint32_t channel, float position) const
    {
        position = wrapRing(position);
        const uint32_t first = static_cast<uint32_t>(std::floor(position))
            % outputChannels_;
        const uint32_t second = (first + 1u) % outputChannels_;
        const float amount = position - std::floor(position);
        if (channel == first) return std::cos(amount * 1.57079632679f);
        if (channel == second) return std::sin(amount * 1.57079632679f);
        return 0.0f;
    }

    static float softLimit(float value) { return std::tanh(clamp(value, -8.0f, 8.0f)); }

    double sampleRate_ = 48000.0;
    uint32_t maxBlockFrames_ = 0u;
    uint32_t bufferSize_ = 0u;
    uint32_t writePos_ = 0u;
    float phase_ = 0.0f;
    float writeSmooth_ = 0.0f;
    bool ready_ = false;
    uint32_t outputChannels_ = kCascadeTapsChannels;
    CascadeTapsParams params_ {};
    CascadeTapsParams smooth_ {};
    std::vector<float> buffer_;
    std::array<float, kCascadeTapsChannels> lp_ {};
    std::array<float, kCascadeTapsChannels> out_ {};
};

} // namespace s3g
