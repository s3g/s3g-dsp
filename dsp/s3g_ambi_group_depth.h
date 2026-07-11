#pragma once

#include "s3g_ambisonic_utilities.h"
#include "s3g_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAmbiGroupDepthGroupChannels = 16;
constexpr uint32_t kAmbiGroupDepthMaxGroups = 8;
constexpr uint32_t kAmbiGroupDepthMaxChannels = kAmbiGroupDepthGroupChannels * kAmbiGroupDepthMaxGroups;

struct AmbiGroupDepthParams {
    float depth = 0.0f;
    float spread = 0.0f;
    float focus = 0.0f;
    float air = 0.0f;
    float low = 0.0f;
    float width = 1.0f;
    float outputGainDb = 0.0f;
};

struct AmbiGroupDepthGroupState {
    float depth = 0.0f;
    float bipolarDepth = 0.0f;
    float direct = 1.0f;
};

inline AmbiGroupDepthParams sanitizeAmbiGroupDepthParams(AmbiGroupDepthParams params)
{
    params.depth = clamp(params.depth, -1.0f, 1.0f);
    params.spread = clamp(params.spread, -1.0f, 1.0f);
    params.focus = clamp(params.focus, -1.0f, 1.0f);
    params.air = clamp(params.air, -1.0f, 1.0f);
    params.low = clamp(params.low, -1.0f, 1.0f);
    params.width = clamp(params.width, 0.0f, 1.5f);
    params.outputGainDb = clamp(params.outputGainDb, -60.0f, 12.0f);
    return params;
}

inline AmbiGroupDepthGroupState ambiGroupDepthStateForGroup(const AmbiGroupDepthParams& params, uint32_t group, uint32_t groups)
{
    groups = std::max<uint32_t>(1u, groups);
    const float centered = groups <= 1u
        ? 0.0f
        : (static_cast<float>(group) - (static_cast<float>(groups) - 1.0f) * 0.5f)
            / ((static_cast<float>(groups) - 1.0f) * 0.5f);
    const float offset = centered * params.spread * 0.72f;
    const float bipolar = clamp(params.depth + offset, -1.0f, 1.0f);
    const float visualDepth = bipolar * 0.5f + 0.5f;
    return { visualDepth, bipolar, 1.0f - visualDepth };
}

template <uint32_t Groups>
class AmbiGroupDepthProcessor {
public:
    static_assert(Groups > 0u && Groups <= kAmbiGroupDepthMaxGroups, "unsupported ambisonic group count");
    static constexpr uint32_t kGroups = Groups;
    static constexpr uint32_t kChannels = Groups * kAmbiGroupDepthGroupChannels;

    AmbiGroupDepthProcessor()
    {
        setParams(params_);
        reset();
    }

    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1000.0, sampleRate);
        reset();
        rebuildTargets();
    }

    void setParams(AmbiGroupDepthParams params)
    {
        params_ = sanitizeAmbiGroupDepthParams(params);
        rebuildTargets();
    }

    void reset()
    {
        currentGain_ = targetGain_;
        lpState_.fill(0.0f);
    }

    const AmbiGroupDepthParams& params() const { return params_; }
    AmbiGroupDepthGroupState groupState(uint32_t group) const
    {
        return ambiGroupDepthStateForGroup(params_, std::min<uint32_t>(group, Groups - 1u), Groups);
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

        constexpr float kGainSmooth = 0.0012f;
        constexpr float kFilterSmooth = 0.0020f;
        for (uint32_t i = 0; i < frames; ++i) {
            for (uint32_t ch = 0; ch < std::min<uint32_t>({ inputChannels, outputChannels, kChannels }); ++ch) {
                if (!outputs[ch]) {
                    continue;
                }
                const float dry = inputs[ch] ? static_cast<float>(inputs[ch][i]) : 0.0f;
                currentGain_[ch] += (targetGain_[ch] - currentGain_[ch]) * kGainSmooth;
                currentLp_[ch] += (targetLp_[ch] - currentLp_[ch]) * kFilterSmooth;
                lpState_[ch] += (dry - lpState_[ch]) * currentLp_[ch];
                const float dampMix = targetAirMix_[ch];
                const float shaped = lerp(dry, lpState_[ch], dampMix) * currentGain_[ch];
                outputs[ch][i] = static_cast<Sample>(std::clamp(shaped, -8.0f, 8.0f));
            }
        }
    }

private:
    void rebuildTargets()
    {
        params_ = sanitizeAmbiGroupDepthParams(params_);
        const float outGain = dbToGain(params_.outputGainDb);
        for (uint32_t group = 0; group < Groups; ++group) {
            const auto state = ambiGroupDepthStateForGroup(params_, group, Groups);
            const float farAmount = std::max(0.0f, state.bipolarDepth);
            const float nearAmount = std::max(0.0f, -state.bipolarDepth);
            const float farSoft = farAmount * farAmount;
            const float focusNorm = params_.focus * 0.5f + 0.5f;
            const float focusKeep = lerp(0.20f, 0.95f, focusNorm);
            const float width = std::max(0.0f, params_.width);
            const float airDamping = std::max(0.0f, params_.air) * farSoft;
            const float airPresence = std::max(0.0f, -params_.air) * (0.25f + nearAmount * 0.75f);
            const float cutoff = lerp(19000.0f, 2200.0f, airDamping);
            const float lp = onePoleCoeff(cutoff);

            for (uint32_t lane = 0; lane < kAmbiGroupDepthGroupChannels; ++lane) {
                const uint32_t ch = group * kAmbiGroupDepthGroupChannels + lane;
                const uint32_t order = ambiUtilityOrderForChannel(lane);
                const float orderNorm = static_cast<float>(order) / 3.0f;
                const float distanceShape = std::pow(std::max(0.02f, 1.0f - farSoft * (1.0f - focusKeep)), orderNorm);
                const float presenceShape = 1.0f + nearAmount * orderNorm * (0.18f + focusNorm * 0.18f) + airPresence * orderNorm * 0.20f;
                const float widthShape = order == 0u ? 1.0f : std::pow(std::max(0.0f, width), orderNorm);
                const float lowShape = order == 0u
                    ? 1.0f + params_.low * (0.45f + nearAmount * 0.25f)
                    : 1.0f - std::max(0.0f, params_.low) * farAmount * orderNorm * 0.18f;
                targetGain_[ch] = std::max(0.0f, distanceShape * presenceShape * widthShape * lowShape * outGain);
                targetLp_[ch] = lp;
                targetAirMix_[ch] = airDamping * (0.20f + 0.80f * orderNorm);
            }
        }
    }

    float onePoleCoeff(float cutoffHz) const
    {
        const float hz = clamp(cutoffHz, 20.0f, static_cast<float>(sampleRate_ * 0.45));
        return 1.0f - std::exp(-2.0f * kPi * hz / static_cast<float>(sampleRate_));
    }

    double sampleRate_ = 48000.0;
    AmbiGroupDepthParams params_ {};
    std::array<float, kChannels> currentGain_ {};
    std::array<float, kChannels> targetGain_ {};
    std::array<float, kChannels> currentLp_ {};
    std::array<float, kChannels> targetLp_ {};
    std::array<float, kChannels> targetAirMix_ {};
    std::array<float, kChannels> lpState_ {};
};

} // namespace s3g
