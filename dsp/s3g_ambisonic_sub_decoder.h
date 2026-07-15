#pragma once

#include "s3g_3oafx.h"
#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_layout_panner.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAmbiSubDecoderMaxInputChannels = kAmbiSpeakerDecoderMaxChannels;
constexpr uint32_t kAmbiSubDecoderMaxSubs = 8;

struct AmbiSubDecoderParams {
    uint32_t order = 3;
    uint32_t subCount = 1;
    float cutoffHz = 90.0f;
    float directionWidth = 1.0f;
    float outputGainDb = 0.0f;
    bool bypass = false;
};

class AmbiSubDecoder {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        lowState_.fill(0.0f);
        setParams(params_);
    }

    void reset()
    {
        lowState_.fill(0.0f);
    }

    void setParams(AmbiSubDecoderParams params)
    {
        params.order = std::clamp<uint32_t>(params.order, 0u, kAmbiSpeakerDecoderMaxOrder);
        params.subCount = std::clamp<uint32_t>(params.subCount, 1u, kAmbiSubDecoderMaxSubs);
        params.cutoffHz = clamp(params.cutoffHz, 20.0f, 240.0f);
        params.directionWidth = clamp(params.directionWidth, 0.0f, 2.0f);
        params.outputGainDb = clamp(params.outputGainDb, -60.0f, 18.0f);
        params_ = params;
    }

    AmbiSubDecoderParams params() const { return params_; }

    void processFrame(const float* in, float* out, uint32_t inputChannels)
    {
        if (!out) return;
        for (uint32_t sub = 0; sub < kAmbiSubDecoderMaxSubs; ++sub) out[sub] = 0.0f;
        if (!in || inputChannels == 0u) return;

        const float gain = dbToGain(params_.outputGainDb);
        if (params_.bypass) {
            return;
        }

        const float alpha = lowpassAlpha();
        const float w = inputChannels > 0u ? in[0] : 0.0f;
        const float y = inputChannels > 1u ? in[1] : 0.0f;
        const float x = inputChannels > 3u ? in[3] : 0.0f;

        if (params_.subCount == 1u || params_.order == 0u) {
            lowState_[0] += alpha * (w - lowState_[0]);
            out[0] = flushDenormal(lowState_[0] * gain);
            return;
        }

        const float width = params_.directionWidth;
        const float norm = 1.0f / std::max(1.0f, 1.0f + width);
        for (uint32_t sub = 0; sub < params_.subCount; ++sub) {
            const float az = subAzimuth(sub, params_.subCount) * kPi / 180.0f;
            const float px = std::cos(az);
            const float py = std::sin(az);
            const float decoded = (w + width * (x * px + y * py)) * norm;
            lowState_[sub] += alpha * (decoded - lowState_[sub]);
            out[sub] = flushDenormal(lowState_[sub] * gain);
        }
    }

    template <typename Sample>
    void processBlock(const Sample* const* in, Sample* const* out, uint32_t inputChannels, uint32_t outputChannels, uint32_t frames)
    {
        if (!out || frames == 0u) return;
        outputChannels = std::min<uint32_t>(outputChannels, kAmbiSubDecoderMaxSubs);
        for (uint32_t ch = 0; ch < outputChannels; ++ch) {
            if (out[ch]) std::fill(out[ch], out[ch] + frames, static_cast<Sample>(0));
        }
        if (!in || inputChannels == 0u || params_.bypass) return;

        const uint32_t subs = std::min<uint32_t>(params_.subCount, outputChannels);
        const float gain = dbToGain(params_.outputGainDb);
        const float alpha = lowpassAlpha();
        for (uint32_t frame = 0; frame < frames; ++frame) {
            const float w = (inputChannels > 0u && in[0]) ? static_cast<float>(in[0][frame]) : 0.0f;
            const float y = (inputChannels > 1u && in[1]) ? static_cast<float>(in[1][frame]) : 0.0f;
            const float x = (inputChannels > 3u && in[3]) ? static_cast<float>(in[3][frame]) : 0.0f;
            if (subs == 1u || params_.order == 0u) {
                lowState_[0] += alpha * (w - lowState_[0]);
                if (out[0]) out[0][frame] = static_cast<Sample>(flushDenormal(lowState_[0] * gain));
                continue;
            }
            const float width = params_.directionWidth;
            const float norm = 1.0f / std::max(1.0f, 1.0f + width);
            for (uint32_t sub = 0; sub < subs; ++sub) {
                const float az = subAzimuth(sub, params_.subCount) * kPi / 180.0f;
                const float decoded = (w + width * (x * std::cos(az) + y * std::sin(az))) * norm;
                lowState_[sub] += alpha * (decoded - lowState_[sub]);
                if (out[sub]) out[sub][frame] = static_cast<Sample>(flushDenormal(lowState_[sub] * gain));
            }
        }
    }

    static float subAzimuth(uint32_t sub, uint32_t count)
    {
        return layoutPannerWrapDeg(-45.0f - static_cast<float>(sub) * 360.0f / static_cast<float>(std::max<uint32_t>(1u, count)));
    }

private:
    float lowpassAlpha() const
    {
        const float hz = std::min(params_.cutoffHz, static_cast<float>(sampleRate_ * 0.45));
        return clamp(1.0f - std::exp(-2.0f * kPi * hz / static_cast<float>(sampleRate_)), 0.0001f, 1.0f);
    }

    double sampleRate_ = 48000.0;
    AmbiSubDecoderParams params_ {};
    std::array<float, kAmbiSubDecoderMaxSubs> lowState_ {};
};

} // namespace s3g
