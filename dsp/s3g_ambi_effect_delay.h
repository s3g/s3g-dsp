#pragma once

#include "s3g_ambi_effect_dj_filter.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAmbiEffectDelayMaxOrder =
    kAmbiEffectDjFilterMaxOrder;
constexpr uint32_t kAmbiEffectDelayMaxChannels =
    kAmbiEffectDjFilterMaxChannels;
constexpr uint32_t kAmbiEffectDelayMaxPickups =
    kAmbiEffectDjFilterMaxPickups;

struct AmbiEffectDelayParams {
    uint32_t order = 7u;
    AmbiEffectBody body = AmbiEffectBody::Auto;
    AmbiEffectTopology topology = AmbiEffectTopology::Local;
    float timeMs = 320.0f;
    float feedback = 0.32f;
    float tone = 0.62f;
    float spread = 0.0f;
    float deviation = 0.0f;
    float topologyAmount = 0.65f;
    float roamingRateHz = 0.08f;
    float mix = 0.35f;
    float outputGainDb = 0.0f;
    std::array<float, kAmbiEffectDelayMaxPickups> pickupTimeTrim {};
    std::array<float, kAmbiEffectDelayMaxPickups> pickupFeedbackTrim {};
    float maskAmount = 0.0f;
    float maskAzimuthDeg = 0.0f;
    float maskElevationDeg = 0.0f;
    float maskWidth = 0.35f;
    float maskCurve = 0.5f;
    float maskDry = 1.0f;
};

inline AmbiEffectDelayParams sanitizeAmbiEffectDelayParams(
    AmbiEffectDelayParams params)
{
    params.order = std::clamp<uint32_t>(
        params.order, 1u, kAmbiEffectDelayMaxOrder);
    params.body = static_cast<AmbiEffectBody>(
        std::min<uint32_t>(static_cast<uint32_t>(params.body), 5u));
    if (params.body == AmbiEffectBody::Tetra4
        || params.body == AmbiEffectBody::Cube8) {
        params.body = AmbiEffectBody::Icosa12;
    }
    params.topology = static_cast<AmbiEffectTopology>(
        std::min<uint32_t>(static_cast<uint32_t>(params.topology), 3u));
    params.timeMs = clamp(params.timeMs, 5.0f, 2000.0f);
    params.feedback = clamp(params.feedback, 0.0f, 0.88f);
    params.tone = clamp(params.tone, 0.0f, 1.0f);
    params.spread = clamp(params.spread, 0.0f, 1.0f);
    params.deviation = clamp(params.deviation, 0.0f, 1.0f);
    params.topologyAmount = clamp(params.topologyAmount, 0.0f, 1.0f);
    params.roamingRateHz = clamp(params.roamingRateHz, 0.005f, 2.0f);
    params.mix = clamp(params.mix, 0.0f, 1.0f);
    params.outputGainDb = clamp(params.outputGainDb, -60.0f, 12.0f);
    for (float& trim : params.pickupTimeTrim) {
        trim = clamp(trim, -1.0f, 1.0f);
    }
    for (float& trim : params.pickupFeedbackTrim) {
        trim = clamp(trim, -1.0f, 1.0f);
    }
    params.maskAmount = clamp(params.maskAmount, 0.0f, 1.0f);
    params.maskAzimuthDeg = clamp(params.maskAzimuthDeg, -180.0f, 180.0f);
    params.maskElevationDeg = clamp(params.maskElevationDeg, -90.0f, 90.0f);
    params.maskWidth = clamp(params.maskWidth, 0.0f, 1.0f);
    params.maskCurve = clamp(params.maskCurve, 0.0f, 1.0f);
    params.maskDry = clamp(params.maskDry, 0.0f, 1.0f);
    return params;
}

class AmbiEffectDelay {
public:
    void prepare(double sampleRate)
    {
        core_.setParams(coreParams(params_));
        core_.prepare(sampleRate);
        core_.setParams(coreParams(params_));
    }

    void reset() { core_.reset(); }

    void setParams(AmbiEffectDelayParams params)
    {
        params_ = sanitizeAmbiEffectDelayParams(params);
        core_.setParams(coreParams(params_));
    }

    const AmbiEffectDelayParams& params() const { return params_; }
    AmbiEffectBody resolvedBody() const { return core_.resolvedBody(); }
    uint32_t activePickupCount() const { return core_.activePickupCount(); }
    float nodeLevel(uint32_t node) const { return core_.nodeLevel(node); }
    float nodeWetMask(uint32_t node) const { return core_.nodeWetMask(node); }
    float roamingPhase() const { return core_.roamingPhase(); }

    template <typename Sample>
    void process(Sample** input, Sample** output,
        uint32_t inputChannels, uint32_t outputChannels, uint32_t frames)
    {
        core_.process(input, output,
            inputChannels, outputChannels, frames);
    }

private:
    static AmbiEffectDjFilterParams coreParams(
        const AmbiEffectDelayParams& params)
    {
        AmbiEffectDjFilterParams result {};
        result.engine = AmbiEffectEngine::Delay;
        result.order = params.order;
        result.body = params.body;
        result.topology = params.topology;
        result.spread = params.spread;
        result.deviation = params.deviation;
        result.topologyAmount = params.topologyAmount;
        result.roamingRateHz = params.roamingRateHz;
        result.mix = params.mix;
        result.outputGainDb = params.outputGainDb;
        result.maskAmount = params.maskAmount;
        result.maskAzimuthDeg = params.maskAzimuthDeg;
        result.maskElevationDeg = params.maskElevationDeg;
        result.maskWidth = params.maskWidth;
        result.maskCurve = params.maskCurve;
        result.maskDry = params.maskDry;
        result.delayTimeMs = params.timeMs;
        result.delayFeedback = params.feedback;
        result.delayTone = params.tone;
        result.pickupDelayTimeTrim = params.pickupTimeTrim;
        result.pickupDelayFeedbackTrim = params.pickupFeedbackTrim;
        return result;
    }

    AmbiEffectDelayParams params_ {};
    AmbiEffectDjFilter core_ {};
};

} // namespace s3g
