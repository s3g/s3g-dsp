#pragma once

#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kArrayHpfMaxChannels = 64;

struct ArrayHpfParams {
    uint32_t activeChannels = 16;
    uint32_t poles = 2;
    float cutoffHz = 90.0f;
    float outputGainDb = 0.0f;
    bool bypass = false;
};

class ArrayHpf {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        reset();
        setParams(params_);
    }

    void reset()
    {
        for (auto& row : lowState_) row.fill(0.0f);
    }

    void setParams(ArrayHpfParams params)
    {
        params.activeChannels = std::clamp<uint32_t>(params.activeChannels, 1u, kArrayHpfMaxChannels);
        params.poles = std::clamp<uint32_t>(params.poles, 1u, kMaxPoles);
        params.cutoffHz = clamp(params.cutoffHz, 20.0f, 240.0f);
        params.outputGainDb = clamp(params.outputGainDb, -60.0f, 18.0f);
        params_ = params;
    }

    ArrayHpfParams params() const { return params_; }

    void processFrame(const float* in, float* out, uint32_t channels)
    {
        if (!out) return;
        channels = std::min<uint32_t>({ channels, params_.activeChannels, kArrayHpfMaxChannels });
        for (uint32_t ch = 0; ch < kArrayHpfMaxChannels; ++ch) out[ch] = 0.0f;
        if (!in) return;
        if (params_.bypass) {
            const float gain = dbToGain(params_.outputGainDb);
            for (uint32_t ch = 0; ch < channels; ++ch) out[ch] = flushDenormal(in[ch] * gain);
            return;
        }
        const float alpha = lowpassAlpha();
        const float gain = dbToGain(params_.outputGainDb);
        for (uint32_t ch = 0; ch < channels; ++ch) {
            float y = in[ch];
            for (uint32_t pole = 0; pole < params_.poles; ++pole) {
                lowState_[ch][pole] += alpha * (y - lowState_[ch][pole]);
                y -= lowState_[ch][pole];
            }
            out[ch] = flushDenormal(y * gain);
        }
    }

    template <typename Sample>
    void processBlock(const Sample* const* in, Sample* const* out, uint32_t inputChannels, uint32_t outputChannels, uint32_t frames)
    {
        if (!out || frames == 0u) return;
        outputChannels = std::min<uint32_t>(outputChannels, kArrayHpfMaxChannels);
        for (uint32_t ch = 0; ch < outputChannels; ++ch) {
            if (out[ch]) std::fill(out[ch], out[ch] + frames, static_cast<Sample>(0));
        }
        if (!in) return;

        const uint32_t channels = std::min<uint32_t>({ inputChannels, outputChannels, params_.activeChannels, kArrayHpfMaxChannels });
        const float gain = dbToGain(params_.outputGainDb);
        if (params_.bypass) {
            for (uint32_t ch = 0; ch < channels; ++ch) {
                if (!out[ch]) continue;
                const Sample* src = in[ch];
                for (uint32_t frame = 0; frame < frames; ++frame) {
                    out[ch][frame] = static_cast<Sample>((src ? static_cast<float>(src[frame]) : 0.0f) * gain);
                }
            }
            return;
        }

        const float alpha = lowpassAlpha();
        for (uint32_t ch = 0; ch < channels; ++ch) {
            if (!out[ch]) continue;
            const Sample* src = in[ch];
            for (uint32_t frame = 0; frame < frames; ++frame) {
                float y = src ? static_cast<float>(src[frame]) : 0.0f;
                for (uint32_t pole = 0; pole < params_.poles; ++pole) {
                    lowState_[ch][pole] += alpha * (y - lowState_[ch][pole]);
                    y -= lowState_[ch][pole];
                }
                out[ch][frame] = static_cast<Sample>(flushDenormal(y * gain));
            }
        }
    }

private:
    static constexpr uint32_t kMaxPoles = 4;

    float lowpassAlpha() const
    {
        const float hz = std::min(params_.cutoffHz, static_cast<float>(sampleRate_ * 0.45));
        return clamp(1.0f - std::exp(-2.0f * kPi * hz / static_cast<float>(sampleRate_)), 0.0001f, 1.0f);
    }

    double sampleRate_ = 48000.0;
    ArrayHpfParams params_ {};
    std::array<std::array<float, kMaxPoles>, kArrayHpfMaxChannels> lowState_ {};
};

} // namespace s3g
