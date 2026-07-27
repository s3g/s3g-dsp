#pragma once

#include "s3g_ambi_effect_dj_filter.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAmbiEffectGainMaxOrder = kAmbiEffectDjFilterMaxOrder;
constexpr uint32_t kAmbiEffectGainMaxChannels = kAmbiEffectDjFilterMaxChannels;
constexpr uint32_t kAmbiEffectGainMaxPickups = kAmbiEffectDjFilterMaxPickups;

struct AmbiEffectGainParams {
    uint32_t order = 7u;
    AmbiEffectBody body = AmbiEffectBody::Auto;
    AmbiEffectTopology topology = AmbiEffectTopology::Local;
    float gainDb = 0.0f;
    float spread = 0.0f;
    float deviation = 0.0f;
    float topologyAmount = 0.65f;
    float roamingRateHz = 0.08f;
    float mix = 1.0f;
    float outputGainDb = 0.0f;
    std::array<float, kAmbiEffectGainMaxPickups> pickupGainTrim {};
    float maskAmount = 0.0f;
    float maskAzimuthDeg = 0.0f;
    float maskElevationDeg = 0.0f;
    float maskWidth = 0.35f;
    float maskCurve = 0.5f;
    float maskDry = 1.0f;
};

inline AmbiEffectGainParams sanitizeAmbiEffectGainParams(
    AmbiEffectGainParams params)
{
    params.order = std::clamp<uint32_t>(params.order, 1u,
        kAmbiEffectGainMaxOrder);
    params.body = static_cast<AmbiEffectBody>(
        std::min<uint32_t>(static_cast<uint32_t>(params.body), 5u));
    if (params.body == AmbiEffectBody::Tetra4
        || params.body == AmbiEffectBody::Cube8) {
        params.body = AmbiEffectBody::Icosa12;
    }
    params.topology = static_cast<AmbiEffectTopology>(
        std::min<uint32_t>(static_cast<uint32_t>(params.topology), 3u));
    params.gainDb = clamp(params.gainDb, -60.0f, 18.0f);
    params.spread = clamp(params.spread, 0.0f, 1.0f);
    params.deviation = clamp(params.deviation, 0.0f, 1.0f);
    params.topologyAmount = clamp(params.topologyAmount, 0.0f, 1.0f);
    params.roamingRateHz = clamp(params.roamingRateHz, 0.005f, 2.0f);
    params.mix = clamp(params.mix, 0.0f, 1.0f);
    params.outputGainDb = clamp(params.outputGainDb, -60.0f, 12.0f);
    for (float& trim : params.pickupGainTrim) trim = clamp(trim, -1.0f, 1.0f);
    params.maskAmount = clamp(params.maskAmount, 0.0f, 1.0f);
    params.maskAzimuthDeg = clamp(params.maskAzimuthDeg, -180.0f, 180.0f);
    params.maskElevationDeg = clamp(params.maskElevationDeg, -90.0f, 90.0f);
    params.maskWidth = clamp(params.maskWidth, 0.0f, 1.0f);
    params.maskCurve = clamp(params.maskCurve, 0.0f, 1.0f);
    params.maskDry = clamp(params.maskDry, 0.0f, 1.0f);
    return params;
}

class AmbiEffectGain {
public:
    void prepare(double sampleRate)
    {
        core_.setParams(coreParams(params_));
        core_.prepare(sampleRate);
        core_.setParams(coreParams(params_));
    }
    void reset() { core_.reset(); }
    void setParams(AmbiEffectGainParams params)
    {
        params_ = sanitizeAmbiEffectGainParams(params);
        core_.setParams(coreParams(params_));
    }
    const AmbiEffectGainParams& params() const { return params_; }
    AmbiEffectBody resolvedBody() const { return core_.resolvedBody(); }
    uint32_t activePickupCount() const { return core_.activePickupCount(); }
    float nodeLevel(uint32_t node) const { return core_.nodeLevel(node); }
    float nodeWetMask(uint32_t node) const { return core_.nodeWetMask(node); }
    float roamingPhase() const { return core_.roamingPhase(); }

    template <typename Sample>
    void process(Sample** input, Sample** output, uint32_t inputChannels,
        uint32_t outputChannels, uint32_t frames)
    {
        core_.process(input, output, inputChannels, outputChannels, frames);
    }

private:
    static AmbiEffectDjFilterParams coreParams(const AmbiEffectGainParams& p)
    {
        AmbiEffectDjFilterParams result {};
        result.engine = AmbiEffectEngine::Gain;
        result.order = p.order;
        result.body = p.body;
        result.topology = p.topology;
        result.spread = p.spread;
        result.deviation = p.deviation;
        result.topologyAmount = p.topologyAmount;
        result.roamingRateHz = p.roamingRateHz;
        result.mix = p.mix;
        result.outputGainDb = p.outputGainDb;
        result.gainDb = p.gainDb;
        result.pickupGainTrim = p.pickupGainTrim;
        result.maskAmount = p.maskAmount;
        result.maskAzimuthDeg = p.maskAzimuthDeg;
        result.maskElevationDeg = p.maskElevationDeg;
        result.maskWidth = p.maskWidth;
        result.maskCurve = p.maskCurve;
        result.maskDry = p.maskDry;
        return result;
    }

    AmbiEffectGainParams params_ {};
    AmbiEffectDjFilter core_ {};
};

} // namespace s3g
