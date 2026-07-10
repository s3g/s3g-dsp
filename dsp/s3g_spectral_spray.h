#pragma once

#include "s3g_math.h"
#include "s3g_realtime.h"
#include "s3g_spectral_fft.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace s3g {

constexpr uint32_t kSpectralSprayMaxChannels = 24;

struct SpectralSprayParams {
    float sprayBins = 18.0f;
    float drift = 0.18f;
    float hold = 0.72f;
    float freeze = 0.0f;
    float feedback = 0.18f;
    float smear = 0.25f;
    float holes = 0.05f;
    float phaseBlur = 0.28f;
    float loFreq = 0.0f;
    float hiFreq = 20000.0f;
    float gainDb = -2.5f;
    float mix = 1.0f;
    float tilt = 0.0f;
    float safety = 0.82f;
};

class SpectralSpray {
public:
    bool prepare(double sampleRate, uint32_t channels, uint32_t fftSize = 4096u, uint32_t overlap = 8u, uint32_t maxBlockFrames = 4096u)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        channels_ = std::clamp<uint32_t>(channels, 0u, kSpectralSprayMaxChannels);
        maxBlockFrames_ = std::max<uint32_t>(1u, maxBlockFrames);
        frameCounter_ = 0u;
        if (channels_ == 0u || !fft_.prepare(channels_, fftSize, overlap)) {
            ready_ = false;
            return false;
        }

        const size_t bins = fft_.bins();
        memories_.assign(channels_, ChannelMemory {});
        wetBuffers_.assign(channels_, std::vector<float>(maxBlockFrames_, 0.0f));
        wetPtrs_.assign(channels_, nullptr);
        dryDelay_.assign(channels_, std::vector<float>(fft_.fftSize(), 0.0f));
        dryWritePos_ = 0u;
        for (auto& memory : memories_) {
            memory.mag.assign(bins, 0.0f);
            memory.outMag.assign(bins, 0.0f);
            memory.phaseR.assign(bins, 1.0f);
            memory.phaseI.assign(bins, 0.0f);
        }
        reset();
        ready_ = true;
        return true;
    }

    void reset()
    {
        fft_.reset();
        dryWritePos_ = 0u;
        frameCounter_ = 0u;
        for (auto& memory : memories_) {
            std::fill(memory.mag.begin(), memory.mag.end(), 0.0f);
            std::fill(memory.outMag.begin(), memory.outMag.end(), 0.0f);
            std::fill(memory.phaseR.begin(), memory.phaseR.end(), 1.0f);
            std::fill(memory.phaseI.begin(), memory.phaseI.end(), 0.0f);
        }
        for (auto& channel : wetBuffers_) std::fill(channel.begin(), channel.end(), 0.0f);
        for (auto& channel : dryDelay_) std::fill(channel.begin(), channel.end(), 0.0f);
        smoothedParams_ = params_;
        mixSmoothed_ = params_.mix;
        gainSmoothed_ = dbToGain(params_.gainDb);
        safetySmoothed_ = params_.safety;
    }

    void setParams(const SpectralSprayParams& params)
    {
        params_ = sanitize(params);
    }

    SpectralSprayParams params() const { return params_; }
    bool ready() const { return ready_; }
    uint32_t latencyFrames() const { return fft_.fftSize(); }
    uint32_t channels() const { return channels_; }

    void process(const float* const* input, float* const* output, uint32_t frames)
    {
        if (!ready_ || !output || frames == 0u) return;
        frames = std::min(frames, maxBlockFrames_);
        for (uint32_t ch = 0; ch < channels_; ++ch) {
            std::fill(wetBuffers_[ch].begin(), wetBuffers_[ch].begin() + frames, 0.0f);
        }
        for (uint32_t ch = 0; ch < channels_; ++ch) wetPtrs_[ch] = wetBuffers_[ch].data();

        fft_.process(input, wetPtrs_.data(), frames, [&](SpectralFrameView frame) {
            processSpectralFrame(frame);
        });

        const float gainTarget = dbToGain(params_.gainDb);
        const float mixTarget = params_.mix;
        const float safetyTarget = params_.safety;
        for (uint32_t i = 0; i < frames; ++i) {
            mixSmoothed_ += (mixTarget - mixSmoothed_) * 0.0015f;
            gainSmoothed_ += (gainTarget - gainSmoothed_) * 0.0015f;
            safetySmoothed_ += (safetyTarget - safetySmoothed_) * 0.0015f;
            const float safety = std::max(0.05f, safetySmoothed_);
            for (uint32_t ch = 0; ch < channels_; ++ch) {
                const float dryIn = input && input[ch] ? input[ch][i] : 0.0f;
                const float dry = dryDelay_[ch][dryWritePos_];
                dryDelay_[ch][dryWritePos_] = dryIn;
                const float wet = wetBuffers_[ch][i];
                const float mixed = lerp(dry, wet, mixSmoothed_) * gainSmoothed_;
                output[ch][i] = softClip(mixed, safety);
            }
            ++dryWritePos_;
            if (dryWritePos_ >= fft_.fftSize()) dryWritePos_ = 0u;
        }
    }

private:
    struct ChannelMemory {
        std::vector<float> mag;
        std::vector<float> outMag;
        std::vector<float> phaseR;
        std::vector<float> phaseI;
    };

    static SpectralSprayParams sanitize(SpectralSprayParams p)
    {
        p.sprayBins = clamp(p.sprayBins, 0.0f, 256.0f);
        p.drift = clamp(p.drift, 0.0f, 1.0f);
        p.hold = clamp(p.hold, 0.0f, 1.0f);
        p.freeze = clamp(p.freeze, 0.0f, 1.0f);
        p.feedback = clamp(p.feedback, 0.0f, 0.85f);
        p.smear = clamp(p.smear, 0.0f, 1.0f);
        p.holes = clamp(p.holes, 0.0f, 0.95f);
        p.phaseBlur = clamp(p.phaseBlur, 0.0f, 1.0f);
        p.loFreq = clamp(p.loFreq, 0.0f, 24000.0f);
        p.hiFreq = clamp(p.hiFreq, 20.0f, 24000.0f);
        if (p.hiFreq < p.loFreq + 20.0f) p.hiFreq = p.loFreq + 20.0f;
        p.gainDb = clamp(p.gainDb, -60.0f, 18.0f);
        p.mix = clamp(p.mix, 0.0f, 1.0f);
        p.tilt = clamp(p.tilt, -1.0f, 1.0f);
        p.safety = clamp(p.safety, 0.05f, 1.0f);
        return p;
    }

    static uint32_t hash(uint32_t x)
    {
        x ^= x >> 16u;
        x *= 0x7feb352du;
        x ^= x >> 15u;
        x *= 0x846ca68bu;
        x ^= x >> 16u;
        return x;
    }

    static float hashSigned(uint32_t x)
    {
        return static_cast<float>(hash(x) & 0xffffu) / 32767.5f - 1.0f;
    }

    static float softClip(float value, float safety)
    {
        const float drive = std::max(0.05f, safety);
        return std::tanh(clamp(value / drive, -8.0f, 8.0f)) * drive;
    }

    static float smoothToward(float current, float target, float coeff)
    {
        return current + (target - current) * coeff;
    }

    void updateSpectralSmoothing()
    {
        const float hopSeconds = static_cast<float>(fft_.hopSize()) / static_cast<float>(std::max(1.0, sampleRate_));
        const float fast = 1.0f - std::exp(-hopSeconds / 0.035f);
        const float medium = 1.0f - std::exp(-hopSeconds / 0.085f);
        const float slow = 1.0f - std::exp(-hopSeconds / 0.160f);
        smoothedParams_.sprayBins = smoothToward(smoothedParams_.sprayBins, params_.sprayBins, slow);
        smoothedParams_.drift = smoothToward(smoothedParams_.drift, params_.drift, slow);
        smoothedParams_.hold = smoothToward(smoothedParams_.hold, params_.hold, medium);
        smoothedParams_.freeze = smoothToward(smoothedParams_.freeze, params_.freeze, fast);
        smoothedParams_.feedback = smoothToward(smoothedParams_.feedback, params_.feedback, medium);
        smoothedParams_.smear = smoothToward(smoothedParams_.smear, params_.smear, medium);
        smoothedParams_.holes = smoothToward(smoothedParams_.holes, params_.holes, medium);
        smoothedParams_.phaseBlur = smoothToward(smoothedParams_.phaseBlur, params_.phaseBlur, medium);
        smoothedParams_.loFreq = smoothToward(smoothedParams_.loFreq, params_.loFreq, slow);
        smoothedParams_.hiFreq = smoothToward(smoothedParams_.hiFreq, params_.hiFreq, slow);
        smoothedParams_.tilt = smoothToward(smoothedParams_.tilt, params_.tilt, medium);
    }

    void processSpectralFrame(SpectralFrameView frame)
    {
        if (frame.channel >= memories_.size()) return;
        if (frame.channel == 0u) updateSpectralSmoothing();
        const auto& prm = smoothedParams_;
        auto& memory = memories_[frame.channel];

        const float eps = 0.0000001f;
        const float nyquist = static_cast<float>(sampleRate_ * 0.5);
        const float binHz = frame.bins > 1u ? nyquist / static_cast<float>(frame.bins - 1u) : nyquist;
        const uint32_t loBin = static_cast<uint32_t>(std::floor(prm.loFreq / std::max(1.0f, binHz)));
        const uint32_t hiBin = std::min<uint32_t>(frame.bins - 1u, static_cast<uint32_t>(std::ceil(prm.hiFreq / std::max(1.0f, binHz))));
        const float spray = prm.sprayBins;
        const float motion = static_cast<float>(frameCounter_) * prm.drift * 0.03125f;
        const float feedback = 0.82f * prm.feedback * prm.feedback;

        for (uint32_t bin = 0; bin < frame.bins; ++bin) {
            const float cr = frame.real[bin];
            const float ci = frame.imag[bin];
            const float mag = std::sqrt(cr * cr + ci * ci);
            const float safeMag = mag + eps;
            const float unitR = cr / safeMag;
            const float unitI = ci / safeMag;

            const float oldMag = memory.mag[bin];
            float writeMag = std::max(mag, oldMag * prm.hold);
            writeMag = lerp(writeMag, oldMag, prm.freeze);
            memory.mag[bin] = writeMag;

            const float patA = std::sin(static_cast<float>(bin) * 0.071f + motion);
            const float patB = std::sin(static_cast<float>(bin) * 0.017f + motion * 2.137f + std::sin(static_cast<float>(bin) * 0.0031f) * kPi);
            const int offset = static_cast<int>(std::floor((patA * 0.62f + patB * 0.38f) * spray + 0.5f));
            const uint32_t src = static_cast<uint32_t>(std::clamp<int>(static_cast<int>(bin) + offset, 0, static_cast<int>(frame.bins - 1u)));
            const uint32_t spread = std::max<uint32_t>(1u, static_cast<uint32_t>(std::floor(spray * 0.12f)));
            const uint32_t lo = src > spread ? src - spread : 0u;
            const uint32_t hi = std::min<uint32_t>(frame.bins - 1u, src + spread);

            float sprayMag = memory.mag[src];
            const float wideMag = (memory.mag[lo] + sprayMag + memory.mag[hi]) * 0.33333334f;
            sprayMag = lerp(sprayMag, wideMag, prm.smear);

            float targetMag = lerp(sprayMag, memory.outMag[bin], feedback);
            const float holePat = 0.5f + 0.5f * std::sin(static_cast<float>(bin) * 1.618f + static_cast<float>(frameCounter_) * std::abs(prm.drift) * 0.017f);
            const float holeThresh = lerp(1.01f, 0.32f, prm.holes);
            if (holePat >= holeThresh) {
                targetMag *= lerp(0.18f, 0.65f, prm.smear);
            }

            if (bin < loBin || bin > hiBin) {
                targetMag = 0.0f;
            }

            const float norm = frame.bins > 1u ? static_cast<float>(bin) / static_cast<float>(frame.bins - 1u) : 0.0f;
            const float lowShelf = std::pow(std::max(1.0f - norm, 0.0f), 0.72f);
            const float highShelf = std::pow(std::max(norm, 0.0f), 0.72f);
            targetMag *= lerp(1.0f, lowShelf, std::max(-prm.tilt, 0.0f) * 0.45f);
            targetMag *= lerp(1.0f, highShelf, std::max(prm.tilt, 0.0f) * 0.45f);
            targetMag = targetMag / (1.0f + 0.10f * std::max(targetMag - 1.0f, 0.0f));
            targetMag = clamp(targetMag, 0.0f, 12.0f);
            memory.outMag[bin] = targetMag;

            float sprayR = lerp(unitR, memory.phaseR[src], prm.phaseBlur);
            float sprayI = lerp(unitI, memory.phaseI[src], prm.phaseBlur);
            const float phaseLen = std::sqrt(sprayR * sprayR + sprayI * sprayI) + eps;
            sprayR /= phaseLen;
            sprayI /= phaseLen;

            memory.phaseR[bin] = unitR;
            memory.phaseI[bin] = unitI;

            const float outMag = lerp(mag, targetMag, prm.mix);
            frame.real[bin] = lerp(unitR, sprayR, prm.phaseBlur) * outMag;
            frame.imag[bin] = lerp(unitI, sprayI, prm.phaseBlur) * outMag;
        }
        if (frame.channel + 1u >= channels_) ++frameCounter_;
    }

    double sampleRate_ = 48000.0;
    uint32_t channels_ = 0u;
    uint32_t maxBlockFrames_ = 0u;
    uint32_t dryWritePos_ = 0u;
    uint32_t frameCounter_ = 0u;
    bool ready_ = false;
    SpectralSprayParams params_ {};
    SpectralSprayParams smoothedParams_ {};
    SpectralFftProcessor fft_;
    std::vector<ChannelMemory> memories_;
    std::vector<std::vector<float>> wetBuffers_;
    std::vector<float*> wetPtrs_;
    std::vector<std::vector<float>> dryDelay_;
    float mixSmoothed_ = 1.0f;
    float gainSmoothed_ = 1.0f;
    float safetySmoothed_ = 0.82f;
};

} // namespace s3g
