#pragma once

#include "s3g_layout_panner.h"
#include "s3g_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kSubCrossoverMaxChannels = 64;
constexpr uint32_t kSubCrossoverMaxSubs = 8;

enum class SubCrossoverMode : uint32_t {
    Split = 0,
    Send = 1,
};

struct SubCrossoverParams {
    LayoutPannerPreset layout = LayoutPannerPreset::Quad;
    SubCrossoverMode mode = SubCrossoverMode::Split;
    uint32_t highChannels = 4;
    uint32_t subCount = 1;
    uint32_t subOffset = 5;
    float cutoffHz = 90.0f;
    float subFocus = 1.5f;
    float subGainDb = 0.0f;
    float highGainDb = 0.0f;
    bool bypass = false;
    bool foldSubsOnBypass = true;
};

inline const char* subCrossoverModeName(SubCrossoverMode mode)
{
    return mode == SubCrossoverMode::Send ? "SEND" : "SPLIT";
}

class SubCrossover {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        lowState_.fill(0.0f);
        layoutProbe_.prepare(sampleRate_);
        applyParams(params_);
    }

    void setParams(SubCrossoverParams params)
    {
        applyParams(params);
    }

    SubCrossoverParams params() const { return params_; }

    void processFrame(const float* in, float* out, uint32_t channels)
    {
        if (!out) return;
        channels = std::min<uint32_t>(channels, kSubCrossoverMaxChannels);
        for (uint32_t ch = 0; ch < kSubCrossoverMaxChannels; ++ch) out[ch] = 0.0f;

        if (params_.bypass) {
            processBypass(in, out, channels);
            return;
        }

        const float alpha = lowpassAlpha();
        const float highGain = dbToGain(params_.highGainDb);
        const float subGain = dbToGain(params_.subGainDb);
        const uint32_t high = std::min<uint32_t>(params_.highChannels, channels);
        const uint32_t subStart = params_.subOffset - 1u;

        for (uint32_t ch = 0; ch < high; ++ch) {
            const float x = in ? in[ch] : 0.0f;
            lowState_[ch] += alpha * (x - lowState_[ch]);
            const float low = lowState_[ch];
            const float highSample = params_.mode == SubCrossoverMode::Split ? x - low : x;
            out[ch] += highSample * highGain;

            for (uint32_t sub = 0; sub < params_.subCount; ++sub) {
                const uint32_t outCh = subStart + sub;
                if (outCh < kSubCrossoverMaxChannels) {
                    out[outCh] += low * subWeights_[ch][sub] * subGain;
                }
            }
        }
    }

private:
    void applyParams(SubCrossoverParams params)
    {
        params.layout = static_cast<LayoutPannerPreset>(
            std::min<uint32_t>(static_cast<uint32_t>(params.layout), static_cast<uint32_t>(LayoutPannerPreset::Srst25)));
        params.mode = static_cast<SubCrossoverMode>(
            std::min<uint32_t>(static_cast<uint32_t>(params.mode), static_cast<uint32_t>(SubCrossoverMode::Send)));
        params.highChannels = std::clamp<uint32_t>(params.highChannels, 1u, kSubCrossoverMaxChannels);
        params.subCount = std::clamp<uint32_t>(params.subCount, 1u, kSubCrossoverMaxSubs);
        params.subOffset = std::clamp<uint32_t>(params.subOffset, 1u, kSubCrossoverMaxChannels);
        params.cutoffHz = clamp(params.cutoffHz, 20.0f, 240.0f);
        params.subFocus = clamp(params.subFocus, 0.25f, 8.0f);
        params.subGainDb = clamp(params.subGainDb, -60.0f, 18.0f);
        params.highGainDb = clamp(params.highGainDb, -60.0f, 18.0f);
        params_ = params;
        refreshWeights();
    }

    float lowpassAlpha() const
    {
        const float hz = std::min(params_.cutoffHz, static_cast<float>(sampleRate_ * 0.45));
        return clamp(1.0f - std::exp(-2.0f * kPi * hz / static_cast<float>(sampleRate_)), 0.0001f, 1.0f);
    }

    static float subAzimuth(uint32_t sub, uint32_t count)
    {
        return layoutPannerWrapDeg(-45.0f - static_cast<float>(sub) * 360.0f / static_cast<float>(std::max<uint32_t>(1u, count)));
    }

    static float angularDistanceDeg(float a, float b)
    {
        return std::abs(layoutPannerWrapDeg(a - b));
    }

    void refreshWeights()
    {
        for (auto& row : subWeights_) row.fill(0.0f);

        LayoutPannerParams lp {};
        lp.layout = params_.layout;
        lp.activeSpeakers = params_.highChannels;
        lp.customShape = LayoutPannerCustomShape::Ring;
        layoutProbe_.setParams(lp);
        if (layoutProbe_.activeSpeakers() != params_.highChannels) {
            lp.layout = LayoutPannerPreset::Custom;
            lp.activeSpeakers = params_.highChannels;
            lp.customShape = LayoutPannerCustomShape::Ring;
            layoutProbe_.setParams(lp);
        }

        for (uint32_t ch = 0; ch < params_.highChannels; ++ch) {
            const float speakerAz = layoutProbe_.speaker(ch).azimuthDeg;
            float sum = 0.0f;
            for (uint32_t sub = 0; sub < params_.subCount; ++sub) {
                if (params_.subCount == 1u) {
                    subWeights_[ch][sub] = 1.0f;
                } else {
                    const float d = angularDistanceDeg(speakerAz, subAzimuth(sub, params_.subCount));
                    const float c = std::max(0.0f, std::cos(d * kPi / 360.0f));
                    subWeights_[ch][sub] = std::pow(c, params_.subFocus);
                }
                sum += subWeights_[ch][sub];
            }
            if (sum > 0.000001f) {
                for (uint32_t sub = 0; sub < params_.subCount; ++sub) subWeights_[ch][sub] /= sum;
            }
        }
    }

    void processBypass(const float* in, float* out, uint32_t channels)
    {
        for (uint32_t ch = 0; ch < channels; ++ch) out[ch] = in ? in[ch] : 0.0f;
        if (!params_.foldSubsOnBypass) return;

        const uint32_t subStart = params_.subOffset - 1u;
        const uint32_t high = std::min<uint32_t>(params_.highChannels, channels);
        if (high == 0u) return;
        for (uint32_t sub = 0; sub < params_.subCount; ++sub) {
            const uint32_t subCh = subStart + sub;
            if (subCh >= channels || subCh < high) continue;
            const float x = out[subCh];
            out[subCh] = 0.0f;
            for (uint32_t ch = 0; ch < high; ++ch) {
                out[ch] += x * subWeights_[ch][sub] / std::max(1.0f, static_cast<float>(high));
            }
        }
    }

    double sampleRate_ = 48000.0;
    SubCrossoverParams params_ {};
    LayoutPanner layoutProbe_ {};
    std::array<float, kSubCrossoverMaxChannels> lowState_ {};
    std::array<std::array<float, kSubCrossoverMaxSubs>, kSubCrossoverMaxChannels> subWeights_ {};
};

} // namespace s3g
