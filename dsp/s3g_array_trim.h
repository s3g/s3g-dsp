#pragma once

#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace s3g {

constexpr uint32_t kArrayTrimMaxChannels = 64;

struct ArrayTrimParams {
    uint32_t activeChannels = 16;
    float outputGainDb = 0.0f;
    bool bypass = false;
    std::array<float, kArrayTrimMaxChannels> gainDb {};
    std::array<uint8_t, kArrayTrimMaxChannels> mute {};
    std::array<uint8_t, kArrayTrimMaxChannels> invert {};
};

class ArrayTrim {
public:
    void prepare(double) { setParams(params_); }
    void reset() {}

    void setParams(ArrayTrimParams params)
    {
        params.activeChannels = std::clamp<uint32_t>(params.activeChannels, 1u, kArrayTrimMaxChannels);
        params.outputGainDb = clamp(params.outputGainDb, -60.0f, 18.0f);
        for (auto& gain : params.gainDb) gain = clamp(gain, -60.0f, 18.0f);
        for (auto& mute : params.mute) mute = mute ? 1u : 0u;
        for (auto& invert : params.invert) invert = invert ? 1u : 0u;
        params_ = params;
    }

    ArrayTrimParams params() const { return params_; }

    template <typename Sample>
    void processBlock(const Sample* const* in, Sample* const* out, uint32_t inputChannels, uint32_t outputChannels, uint32_t frames)
    {
        if (!out || frames == 0u) return;
        outputChannels = std::min<uint32_t>(outputChannels, kArrayTrimMaxChannels);
        for (uint32_t ch = 0; ch < outputChannels; ++ch) {
            if (out[ch]) std::fill(out[ch], out[ch] + frames, static_cast<Sample>(0));
        }
        if (!in) return;

        const uint32_t channels = std::min<uint32_t>({ inputChannels, outputChannels, params_.activeChannels, kArrayTrimMaxChannels });
        const float outputGain = dbToGain(params_.outputGainDb);
        for (uint32_t ch = 0; ch < channels; ++ch) {
            if (!out[ch]) continue;
            const Sample* src = in[ch];
            const float trim = params_.bypass ? outputGain : channelGain(ch) * outputGain;
            for (uint32_t frame = 0; frame < frames; ++frame) {
                const float x = src ? static_cast<float>(src[frame]) : 0.0f;
                out[ch][frame] = static_cast<Sample>(flushDenormal(x * trim));
            }
        }
    }

private:
    float channelGain(uint32_t channel) const
    {
        if (channel >= kArrayTrimMaxChannels || params_.mute[channel]) return 0.0f;
        const float sign = params_.invert[channel] ? -1.0f : 1.0f;
        return sign * dbToGain(params_.gainDb[channel]);
    }

    ArrayTrimParams params_ {};
};

} // namespace s3g
