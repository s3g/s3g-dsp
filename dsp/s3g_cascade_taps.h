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
    float dry = 0.25f;
    float wet = 0.85f;
    float gainDb = -2.0f;
    float stereo = 0.0f;
    float soft = 0.62f;
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
        prevInput_ = 0.0f;
        transientEnv_ = 0.0f;
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
        prevInput_ = 0.0f;
        transientEnv_ = 0.0f;
        lp_.fill(0.0f);
        out_.fill(0.0f);
    }

    void setParams(const CascadeTapsParams& params) { params_ = sanitize(params); }
    CascadeTapsParams params() const { return params_; }
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
            const float src = (inL + inR * smooth_.stereo) / (1.0f + smooth_.stereo);
            updateTransientSuppressor(src);
            writeSmooth_ += (src - writeSmooth_) * inputSmoothingCoef();
            buffer_[writePos_] = softLimit(writeSmooth_ * transientInputTrim());
            float start = smooth_.pos - 1.0f + phase_ * static_cast<float>(kCascadeTapsChannels);
            start = wrapRing(start);
            const bool forward = smooth_.direction >= 0.0f;
            for (uint32_t ch = 0; ch < kCascadeTapsChannels; ++ch) {
                float f = static_cast<float>(ch) - start;
                if (f < 0.0f) f += static_cast<float>(kCascadeTapsChannels);
                float r = start - static_cast<float>(ch);
                if (r < 0.0f) r += static_cast<float>(kCascadeTapsChannels);
                const float order = forward ? f : r;
                const float delay = clamp((effectiveBaseMs() + effectiveStepMs() * order) * static_cast<float>(sampleRate_) * 0.001f, 1.0f, static_cast<float>(bufferSize_ - 2u));
                const float tap = readDelaySoft(delay) * std::pow(effectiveDecay() + 0.000001f, order) * tapWindow(order) * transientWetTrim() * relationWetTrim();
                const float coef = lerp(0.16f, 0.022f, smooth_.damp);
                lp_[ch] += (tap - lp_[ch]) * coef;
                const float drySig = src * smooth_.dry;
                const float target = (drySig + lp_[ch] * effectiveWet()) * dbToGain(smooth_.gainDb);
                const float next = out_[ch] + (target - out_[ch]) * outputSmoothCoef();
                out_[ch] = slewLimit(out_[ch], next);
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
        return pos;
    }

    float inputSmoothingCoef() const
    {
        const float samples = std::max(1.0f, static_cast<float>(sampleRate_ * 0.0032));
        return 1.0f - std::exp(-1.0f / samples);
    }

    void updateTransientSuppressor(float src)
    {
        const float diff = std::abs(src - prevInput_);
        prevInput_ = src;
        const float drive = clamp((diff - 0.015f) * 9.0f, 0.0f, 1.0f);
        const float attack = 1.0f - std::exp(-1.0f / std::max(1.0f, static_cast<float>(sampleRate_ * 0.0012)));
        const float release = 1.0f - std::exp(-1.0f / std::max(1.0f, static_cast<float>(sampleRate_ * 0.055)));
        const float coef = drive > transientEnv_ ? attack : release;
        transientEnv_ += (drive - transientEnv_) * coef;
    }

    float transientInputTrim() const
    {
        return 1.0f - 0.18f * transientEnv_;
    }

    float transientWetTrim() const
    {
        return 1.0f - (0.38f + 0.18f * smooth_.soft) * transientEnv_;
    }

    float effectiveBaseMs() const
    {
        return std::max(smooth_.baseMs, lerp(1.0f, 8.0f, smooth_.soft));
    }

    float effectiveStepMs() const
    {
        return std::max(smooth_.stepMs, lerp(1.0f, 22.0f, smooth_.soft));
    }

    float relationRisk() const
    {
        const float spacing = effectiveBaseMs() + effectiveStepMs();
        const float tightSpacing = 1.0f - clamp((spacing - 10.0f) / 70.0f, 0.0f, 1.0f);
        const float hotTail = clamp((smooth_.wet - 0.55f) / 0.45f, 0.0f, 1.0f) * clamp((smooth_.decay - 0.68f) / 0.30f, 0.0f, 1.0f);
        const float sharpDamp = 1.0f - smooth_.damp * 0.75f;
        return clamp(tightSpacing * hotTail * sharpDamp, 0.0f, 1.0f);
    }

    float effectiveDecay() const
    {
        return std::min(smooth_.decay, lerp(0.98f, 0.90f, smooth_.soft * relationRisk()));
    }

    float effectiveWet() const
    {
        return smooth_.wet * (1.0f - 0.24f * smooth_.soft * relationRisk());
    }

    float relationWetTrim() const
    {
        return 1.0f - 0.18f * smooth_.soft * relationRisk();
    }

    float outputSmoothCoef() const
    {
        return lerp(0.072f, 0.044f, smooth_.soft);
    }

    float slewLimit(float current, float next) const
    {
        const float base = (0.010f + 0.020f * (1.0f - transientEnv_)) * lerp(1.0f, 0.68f, smooth_.soft);
        const float limit = base * dbToGain(std::min(0.0f, smooth_.gainDb) * 0.35f);
        return current + clamp(next - current, -limit, limit);
    }

    float wrapRing(float v) const
    {
        const float n = static_cast<float>(kCascadeTapsChannels);
        v -= std::floor(v / n) * n;
        return v;
    }

    float tapWindow(float order) const
    {
        const float edgeWidth = lerp(1.15f, 3.25f, smooth_.soft);
        const float end = static_cast<float>(kCascadeTapsChannels);
        const float edge = std::min(order, end - order);
        const float x = clamp(edge / edgeWidth, 0.0f, 1.0f);
        return 0.5f - 0.5f * std::cos(3.14159265359f * x);
    }

    static float softLimit(float value) { return std::tanh(clamp(value, -8.0f, 8.0f)); }

    double sampleRate_ = 48000.0;
    uint32_t maxBlockFrames_ = 0u;
    uint32_t bufferSize_ = 0u;
    uint32_t writePos_ = 0u;
    float phase_ = 0.0f;
    float writeSmooth_ = 0.0f;
    float prevInput_ = 0.0f;
    float transientEnv_ = 0.0f;
    bool ready_ = false;
    CascadeTapsParams params_ {};
    CascadeTapsParams smooth_ {};
    std::vector<float> buffer_;
    std::array<float, kCascadeTapsChannels> lp_ {};
    std::array<float, kCascadeTapsChannels> out_ {};
};

} // namespace s3g
