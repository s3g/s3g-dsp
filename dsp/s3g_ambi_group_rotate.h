#pragma once

#include "s3g_ambisonic_utilities.h"
#include "s3g_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAmbiGroupRotateGroupChannels = 16;
constexpr uint32_t kAmbiGroupRotateMaxGroups = 8;
constexpr uint32_t kAmbiGroupRotateMaxChannels = kAmbiGroupRotateGroupChannels * kAmbiGroupRotateMaxGroups;

struct AmbiGroupRotateParams {
    float yawDeg = 0.0f;
    float pitchDeg = 0.0f;
    float rollDeg = 0.0f;
    float spread = 0.0f;
    float tilt = 0.0f;
    float twist = 0.0f;
    float width = 1.0f;
    float outputGainDb = 0.0f;
};

inline float wrapAmbiGroupRotateDeg(float deg)
{
    while (deg > 180.0f) {
        deg -= 360.0f;
    }
    while (deg < -180.0f) {
        deg += 360.0f;
    }
    return deg;
}

inline AmbiGroupRotateParams sanitizeAmbiGroupRotateParams(AmbiGroupRotateParams params)
{
    params.yawDeg = wrapAmbiGroupRotateDeg(params.yawDeg);
    params.pitchDeg = clamp(params.pitchDeg, -90.0f, 90.0f);
    params.rollDeg = clamp(params.rollDeg, -180.0f, 180.0f);
    params.spread = clamp(params.spread, -1.0f, 1.0f);
    params.tilt = clamp(params.tilt, -1.0f, 1.0f);
    params.twist = clamp(params.twist, -1.0f, 1.0f);
    params.width = clamp(params.width, 0.0f, 1.5f);
    params.outputGainDb = clamp(params.outputGainDb, -60.0f, 12.0f);
    return params;
}

inline AmbiRotateParams ambiGroupRotateParamsForGroup(const AmbiGroupRotateParams& params, uint32_t group, uint32_t groups)
{
    groups = std::max<uint32_t>(1u, groups);
    const float centered = groups <= 1u
        ? 0.0f
        : (static_cast<float>(group) - (static_cast<float>(groups) - 1.0f) * 0.5f)
            / ((static_cast<float>(groups) - 1.0f) * 0.5f);
    const float ring = groups <= 1u ? 0.0f : static_cast<float>(group) / static_cast<float>(groups) * 2.0f * kPi;

    AmbiRotateParams out {};
    out.order = 3u;
    out.yawDeg = wrapAmbiGroupRotateDeg(params.yawDeg + centered * params.spread * 120.0f);
    out.pitchDeg = clamp(params.pitchDeg + std::sin(ring) * params.tilt * 45.0f, -90.0f, 90.0f);
    out.rollDeg = clamp(params.rollDeg + centered * params.twist * 180.0f, -180.0f, 180.0f);
    out.width = params.width;
    out.outputGainDb = params.outputGainDb;
    return out;
}

template <uint32_t Groups>
class AmbiGroupRotateProcessor {
public:
    static_assert(Groups > 0u && Groups <= kAmbiGroupRotateMaxGroups, "unsupported ambisonic group count");
    static constexpr uint32_t kGroups = Groups;
    static constexpr uint32_t kChannels = Groups * kAmbiGroupRotateGroupChannels;

    AmbiGroupRotateProcessor()
    {
        setParams(params_);
        reset();
    }

    void setParams(AmbiGroupRotateParams params)
    {
        params_ = sanitizeAmbiGroupRotateParams(params);
        for (uint32_t group = 0; group < Groups; ++group) {
            processors_[group].setParams(ambiGroupRotateParamsForGroup(params_, group, Groups));
        }
    }

    void reset()
    {
        for (auto& processor : processors_) {
            processor.reset();
        }
    }

    const AmbiGroupRotateParams& params() const { return params_; }
    AmbiRotateParams groupParams(uint32_t group) const
    {
        return ambiGroupRotateParamsForGroup(params_, std::min<uint32_t>(group, Groups - 1u), Groups);
    }

    template <typename Sample>
    void process(Sample* const* inputs, uint32_t inputChannels, Sample* const* outputs, uint32_t outputChannels, uint32_t frames)
    {
        if (!outputs || frames == 0u) {
            return;
        }
        for (uint32_t ch = 0; ch < std::min<uint32_t>(outputChannels, kChannels); ++ch) {
            if (outputs[ch]) {
                std::fill(outputs[ch], outputs[ch] + frames, Sample {});
            }
        }
        if (!inputs) {
            return;
        }

        for (uint32_t group = 0; group < Groups; ++group) {
            Sample* inPtrs[kAmbiGroupRotateGroupChannels] {};
            Sample* outPtrs[kAmbiGroupRotateGroupChannels] {};
            const uint32_t base = group * kAmbiGroupRotateGroupChannels;
            for (uint32_t lane = 0; lane < kAmbiGroupRotateGroupChannels; ++lane) {
                const uint32_t ch = base + lane;
                inPtrs[lane] = ch < inputChannels ? inputs[ch] : nullptr;
                outPtrs[lane] = ch < outputChannels ? outputs[ch] : nullptr;
            }
            processors_[group].process(inPtrs, outPtrs, kAmbiGroupRotateGroupChannels, kAmbiGroupRotateGroupChannels, frames);
        }
    }

private:
    AmbiGroupRotateParams params_ {};
    std::array<AmbiRotateProcessor, Groups> processors_ {};
};

} // namespace s3g
