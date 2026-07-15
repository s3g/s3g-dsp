#pragma once

#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace s3g {

constexpr uint32_t kArrayDelayMaxChannels = 64;
constexpr float kArrayDelayDefaultMaxMs = 4000.0f;

struct ArrayDelayParams {
    uint32_t activeChannels = 16;
    float maxDelayMs = kArrayDelayDefaultMaxMs;
    float outputGainDb = 0.0f;
    bool bypass = false;
    std::array<float, kArrayDelayMaxChannels> delayMs {};
};

class ArrayDelay {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        allocateBuffers(params_.maxDelayMs);
        reset();
        setParams(params_);
    }

    void reset()
    {
        for (auto& buffer : buffers_) std::fill(buffer.begin(), buffer.end(), 0.0f);
        writeIndex_ = 0u;
    }

    void setParams(ArrayDelayParams params)
    {
        params.activeChannels = std::clamp<uint32_t>(params.activeChannels, 1u, kArrayDelayMaxChannels);
        params.maxDelayMs = clamp(params.maxDelayMs, 1.0f, kArrayDelayDefaultMaxMs);
        params.outputGainDb = clamp(params.outputGainDb, -60.0f, 18.0f);
        for (auto& ms : params.delayMs) ms = clamp(ms, 0.0f, params.maxDelayMs);
        if (std::fabs(params.maxDelayMs - params_.maxDelayMs) > 0.001f || bufferSize_ == 0u) {
            allocateBuffers(params.maxDelayMs);
            reset();
        }
        params_ = params;
    }

    ArrayDelayParams params() const { return params_; }

    void setChannelDelay(uint32_t index, float ms)
    {
        if (index >= kArrayDelayMaxChannels) return;
        params_.delayMs[index] = clamp(ms, 0.0f, params_.maxDelayMs);
    }

    template <typename Sample>
    void processBlock(const Sample* const* in, Sample* const* out, uint32_t inputChannels, uint32_t outputChannels, uint32_t frames)
    {
        if (!out || frames == 0u) return;
        outputChannels = std::min<uint32_t>(outputChannels, kArrayDelayMaxChannels);
        for (uint32_t ch = 0; ch < outputChannels; ++ch) {
            if (out[ch]) std::fill(out[ch], out[ch] + frames, static_cast<Sample>(0));
        }
        if (!in || bufferSize_ == 0u) return;

        const uint32_t channels = std::min<uint32_t>({ inputChannels, outputChannels, params_.activeChannels, kArrayDelayMaxChannels });
        const float gain = dbToGain(params_.outputGainDb);
        for (uint32_t frame = 0; frame < frames; ++frame) {
            for (uint32_t ch = 0; ch < channels; ++ch) {
                const float x = in[ch] ? static_cast<float>(in[ch][frame]) : 0.0f;
                buffers_[ch][writeIndex_] = x;
                const float y = params_.bypass ? x : readDelayed(ch, params_.delayMs[ch]);
                if (out[ch]) out[ch][frame] = static_cast<Sample>(flushDenormal(y * gain));
            }
            writeIndex_ = (writeIndex_ + 1u) % bufferSize_;
        }
    }

private:
    void allocateBuffers(float maxDelayMs)
    {
        const uint32_t samples = static_cast<uint32_t>(std::ceil(std::max(1.0f, maxDelayMs) * static_cast<float>(sampleRate_) * 0.001f)) + 4u;
        bufferSize_ = std::max<uint32_t>(samples, 8u);
        for (auto& buffer : buffers_) buffer.assign(bufferSize_, 0.0f);
        writeIndex_ = 0u;
    }

    float readDelayed(uint32_t channel, float delayMs) const
    {
        const float delaySamples = clamp(delayMs, 0.0f, params_.maxDelayMs) * static_cast<float>(sampleRate_) * 0.001f;
        const float read = static_cast<float>(writeIndex_) - delaySamples;
        float wrapped = read;
        while (wrapped < 0.0f) wrapped += static_cast<float>(bufferSize_);
        while (wrapped >= static_cast<float>(bufferSize_)) wrapped -= static_cast<float>(bufferSize_);
        const uint32_t i0 = static_cast<uint32_t>(std::floor(wrapped)) % bufferSize_;
        const uint32_t i1 = (i0 + 1u) % bufferSize_;
        const float frac = wrapped - static_cast<float>(i0);
        return lerp(buffers_[channel][i0], buffers_[channel][i1], frac);
    }

    double sampleRate_ = 48000.0;
    ArrayDelayParams params_ {};
    uint32_t bufferSize_ = 0u;
    uint32_t writeIndex_ = 0u;
    std::array<std::vector<float>, kArrayDelayMaxChannels> buffers_ {};
};

} // namespace s3g
