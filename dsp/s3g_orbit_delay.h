#pragma once

#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace s3g {

constexpr uint32_t kOrbitDelayChannels = 16;

struct OrbitDelayParams {
    float pos = 1.0f;
    float spread = 3.8f;
    float rotate = 0.03f;
    float width = 1.2f;
    float focus = 1.5f;
    float gainDb = -3.5f;
    float stereo = 0.0f;
    float delayMs = 180.0f;
    float feedback = 0.35f;
    float orbit = 1.0f;
    float damp = 0.35f;
    float wet = 0.55f;
};

class OrbitDelay {
public:
    bool prepare(double sampleRate, uint32_t maxBlockFrames = 4096u, double maxDelaySeconds = 3.0)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        maxBlockFrames_ = std::max<uint32_t>(1u, maxBlockFrames);
        bufferSize_ = static_cast<uint32_t>(std::clamp(sampleRate_ * maxDelaySeconds, 48000.0, 384000.0));
        buffers_.assign(kOrbitDelayChannels, std::vector<float>(bufferSize_, 0.0f));
        writePos_ = 0u;
        phase_ = 0.0f;
        silenceSamples_ = 0u;
        stopFade_ = 1.0f;
        writeSmooth_ = 0.0f;
        smooth_ = params_;
        lp_.fill(0.0f);
        out_.fill(0.0f);
        ready_ = true;
        return true;
    }

    void reset()
    {
        for (auto& b : buffers_) std::fill(b.begin(), b.end(), 0.0f);
        writePos_ = 0u;
        phase_ = 0.0f;
        silenceSamples_ = 0u;
        stopFade_ = 1.0f;
        writeSmooth_ = 0.0f;
        smooth_ = params_;
        lp_.fill(0.0f);
        out_.fill(0.0f);
    }

    void setParams(const OrbitDelayParams& params) { params_ = sanitize(params); }
    OrbitDelayParams params() const { return params_; }
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
            updateStopFade(src);
            const float writeSrc = smoothedInputForBuffer(src);
            const float srcL = inL;
            const float srcR = inR * smooth_.stereo;
            const float nchan = static_cast<float>(kOrbitDelayChannels);
            float centre = smooth_.pos - 1.0f + phase_ * nchan;
            centre = wrapRing(centre);
            const float halfWidth = smooth_.width * 0.5f * smooth_.stereo;
            const float cL = wrapRing(centre - halfWidth);
            const float cR = wrapRing(centre + halfWidth);
            const float radius = std::max(0.05f, smooth_.spread);

            std::array<float, kOrbitDelayChannels> dry {};
            float sumL = 0.000001f;
            float sumR = 0.000001f;
            std::array<float, kOrbitDelayChannels> gL {};
            std::array<float, kOrbitDelayChannels> gR {};
            for (uint32_t ch = 0; ch < kOrbitDelayChannels; ++ch) {
                const float dL = ringDistance(static_cast<float>(ch), cL);
                const float dR = ringDistance(static_cast<float>(ch), cR);
                gL[ch] = std::pow(std::max(1.0f - dL / radius, 0.0f), smooth_.focus);
                gR[ch] = std::pow(std::max(1.0f - dR / radius, 0.0f), smooth_.focus);
                sumL += gL[ch] * gL[ch];
                sumR += gR[ch] * gR[ch];
            }
            const float nL = dbToGain(smooth_.gainDb) / std::sqrt(sumL);
            const float nR = dbToGain(smooth_.gainDb) / std::sqrt(sumR);
            for (uint32_t ch = 0; ch < kOrbitDelayChannels; ++ch) dry[ch] = srcL * gL[ch] * nL + srcR * gR[ch] * nR;

            std::array<float, kOrbitDelayChannels> echo {};
            for (uint32_t ch = 0; ch < kOrbitDelayChannels; ++ch) {
                const float ratio = kDelayRatios[ch];
                const float delay = clamp(smooth_.delayMs * ratio * static_cast<float>(sampleRate_) * 0.001f, 1.0f, static_cast<float>(bufferSize_ - 2u));
                echo[ch] = readDelay(ch, delay);
                const float coef = 1.0f - smooth_.damp;
                lp_[ch] += (echo[ch] - lp_[ch]) * coef;
            }
            for (uint32_t ch = 0; ch < kOrbitDelayChannels; ++ch) {
                const uint32_t prev = ch == 0u ? kOrbitDelayChannels - 1u : ch - 1u;
                const uint32_t next = (ch + 1u) % kOrbitDelayChannels;
                const float fbSource = smooth_.orbit >= 0.0f ? lp_[prev] : lp_[next];
                const float fb = fbSource * (0.78f * smooth_.feedback * smooth_.feedback);
                buffers_[ch][writePos_] = softLimit(writeSrc * 0.35f + fb);
                const float wetSig = lp_[ch] * dbToGain(smooth_.gainDb) * stopFade_;
                const float target = lerp(dry[ch], wetSig, smooth_.wet);
                out_[ch] += (target - out_[ch]) * 0.18f;
                output[ch][i] = softLimit(out_[ch]);
            }
            writePos_ = (writePos_ + 1u) % bufferSize_;
        }
    }

private:
    static constexpr std::array<float, kOrbitDelayChannels> kDelayRatios {
        1.00f, 1.13f, 1.31f, 1.47f, 1.61f, 1.79f, 1.97f, 2.11f,
        2.29f, 2.43f, 2.61f, 2.83f, 3.07f, 3.31f, 3.59f, 3.89f
    };

    static OrbitDelayParams sanitize(OrbitDelayParams p)
    {
        p.pos = clamp(p.pos, 1.0f, static_cast<float>(kOrbitDelayChannels));
        p.spread = clamp(p.spread, 0.05f, static_cast<float>(kOrbitDelayChannels));
        p.rotate = clamp(p.rotate, -8.0f, 8.0f);
        p.width = clamp(p.width, 0.0f, static_cast<float>(kOrbitDelayChannels));
        p.focus = clamp(p.focus, 0.1f, 8.0f);
        p.gainDb = clamp(p.gainDb, -60.0f, 12.0f);
        p.stereo = clamp(p.stereo, 0.0f, 1.0f);
        p.delayMs = clamp(p.delayMs, 5.0f, 1800.0f);
        p.feedback = clamp(p.feedback, 0.0f, 0.82f);
        p.orbit = clamp(p.orbit, -6.0f, 6.0f);
        p.damp = clamp(p.damp, 0.0f, 1.0f);
        p.wet = clamp(p.wet, 0.0f, 1.0f);
        return p;
    }

    void updateSmoothing()
    {
        constexpr float fast = 0.005f;
        constexpr float medium = 0.0015f;
        constexpr float slow = 0.00045f;
        smooth_.pos += (params_.pos - smooth_.pos) * medium;
        smooth_.spread += (params_.spread - smooth_.spread) * medium;
        smooth_.rotate += (params_.rotate - smooth_.rotate) * slow;
        smooth_.width += (params_.width - smooth_.width) * medium;
        smooth_.focus += (params_.focus - smooth_.focus) * medium;
        smooth_.gainDb += (params_.gainDb - smooth_.gainDb) * fast;
        smooth_.stereo += (params_.stereo - smooth_.stereo) * medium;
        smooth_.delayMs += (params_.delayMs - smooth_.delayMs) * slow;
        smooth_.feedback += (params_.feedback - smooth_.feedback) * medium;
        smooth_.orbit += (params_.orbit - smooth_.orbit) * medium;
        smooth_.damp += (params_.damp - smooth_.damp) * medium;
        smooth_.wet += (params_.wet - smooth_.wet) * fast;
    }

    float readDelay(uint32_t ch, float delay) const
    {
        float pos = static_cast<float>(writePos_) - delay;
        pos += static_cast<float>(bufferSize_) * (pos < 0.0f ? 1.0f : 0.0f);
        const uint32_t i0 = static_cast<uint32_t>(std::floor(pos));
        const uint32_t i1 = (i0 + 1u) % bufferSize_;
        return lerp(buffers_[ch][i0], buffers_[ch][i1], pos - static_cast<float>(i0));
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

        const uint32_t holdSamples = static_cast<uint32_t>(sampleRate_ * 0.012);
        const float target = silenceSamples_ > holdSamples ? 0.0f : 1.0f;
        const float ms = target > stopFade_ ? 10.0f : 95.0f;
        const float samples = std::max(1.0f, static_cast<float>(sampleRate_ * ms * 0.001));
        stopFade_ += (target - stopFade_) * (1.0f - std::exp(-1.0f / samples));
    }

    float wrapRing(float v) const
    {
        const float n = static_cast<float>(kOrbitDelayChannels);
        v -= std::floor(v / n) * n;
        return v;
    }

    float ringDistance(float a, float b) const
    {
        const float n = static_cast<float>(kOrbitDelayChannels);
        const float d = std::abs(a - b);
        return std::min(d, n - d);
    }

    static float softLimit(float value) { return std::tanh(clamp(value, -8.0f, 8.0f)); }

    double sampleRate_ = 48000.0;
    uint32_t maxBlockFrames_ = 0u;
    uint32_t bufferSize_ = 0u;
    uint32_t writePos_ = 0u;
    float phase_ = 0.0f;
    uint32_t silenceSamples_ = 0u;
    float stopFade_ = 1.0f;
    float writeSmooth_ = 0.0f;
    bool ready_ = false;
    OrbitDelayParams params_ {};
    OrbitDelayParams smooth_ {};
    std::vector<std::vector<float>> buffers_;
    std::array<float, kOrbitDelayChannels> lp_ {};
    std::array<float, kOrbitDelayChannels> out_ {};
};

} // namespace s3g
