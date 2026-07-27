#pragma once

#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace s3g {

constexpr uint32_t kAmbiEffectDjFilterMaxOrder = 7u;
constexpr uint32_t kAmbiEffectDjFilterMaxChannels = 64u;
constexpr uint32_t kAmbiEffectDjFilterMaxPickups = 24u;

enum class AmbiEffectBody : uint32_t {
    Auto = 0u,
    Tetra4 = 1u,
    Cube8 = 2u,
    Icosa12 = 3u,
    Dodeca20 = 4u,
    Sphere24 = 5u,
};

enum class AmbiEffectTopology : uint32_t {
    Local = 0u,
    Cross = 1u,
    Diffuse = 2u,
    Roaming = 3u,
};

enum class AmbiEffectEngine : uint32_t {
    DjFilter = 0u,
    Delay = 1u,
    Pitch = 2u,
    Gain = 3u,
};

struct AmbiEffectDjFilterParams {
    AmbiEffectEngine engine = AmbiEffectEngine::DjFilter;
    uint32_t order = 7u;
    AmbiEffectBody body = AmbiEffectBody::Auto;
    AmbiEffectTopology topology = AmbiEffectTopology::Local;
    float filter = 0.5f;
    float resonance = 0.12f;
    float spread = 0.0f;
    float deviation = 0.0f;
    float topologyAmount = 0.65f;
    float roamingRateHz = 0.08f;
    float mix = 1.0f;
    float outputGainDb = 0.0f;
    std::array<float, kAmbiEffectDjFilterMaxPickups> pickupFilterTrim {};
    std::array<float, kAmbiEffectDjFilterMaxPickups> pickupResonanceTrim {};
    float maskAmount = 0.0f;
    float maskAzimuthDeg = 0.0f;
    float maskElevationDeg = 0.0f;
    float maskWidth = 0.35f;
    float maskCurve = 0.5f;
    float maskDry = 1.0f;
    float delayTimeMs = 320.0f;
    float delayFeedback = 0.32f;
    float delayTone = 0.62f;
    std::array<float, kAmbiEffectDjFilterMaxPickups> pickupDelayTimeTrim {};
    std::array<float, kAmbiEffectDjFilterMaxPickups> pickupDelayFeedbackTrim {};
    float pitchSemitones = 0.0f;
    float pitchWindowMs = 80.0f;
    float pitchGlideMs = 250.0f;
    std::array<float, kAmbiEffectDjFilterMaxPickups> pickupPitchTrim {};
    std::array<float, kAmbiEffectDjFilterMaxPickups> pickupPitchWindowTrim {};
    float gainDb = 0.0f;
    std::array<float, kAmbiEffectDjFilterMaxPickups> pickupGainTrim {};
};

inline AmbiEffectDjFilterParams sanitizeAmbiEffectDjFilterParams(
    AmbiEffectDjFilterParams params)
{
    params.engine = static_cast<AmbiEffectEngine>(
        std::min<uint32_t>(static_cast<uint32_t>(params.engine), 3u));
    params.order = std::clamp<uint32_t>(
        params.order, 1u, kAmbiEffectDjFilterMaxOrder);
    params.body = static_cast<AmbiEffectBody>(
        std::min<uint32_t>(static_cast<uint32_t>(params.body), 5u));
    if (params.body == AmbiEffectBody::Tetra4
        || params.body == AmbiEffectBody::Cube8) {
        params.body = AmbiEffectBody::Icosa12;
    }
    params.topology = static_cast<AmbiEffectTopology>(
        std::min<uint32_t>(static_cast<uint32_t>(params.topology), 3u));
    params.filter = clamp(params.filter, 0.0f, 1.0f);
    params.resonance = clamp(params.resonance, 0.0f, 1.0f);
    params.spread = clamp(params.spread, 0.0f, 1.0f);
    params.deviation = clamp(params.deviation, 0.0f, 1.0f);
    params.topologyAmount = clamp(params.topologyAmount, 0.0f, 1.0f);
    params.roamingRateHz = clamp(params.roamingRateHz, 0.005f, 2.0f);
    params.mix = clamp(params.mix, 0.0f, 1.0f);
    params.outputGainDb = clamp(params.outputGainDb, -60.0f, 12.0f);
    for (float& trim : params.pickupFilterTrim) {
        trim = clamp(trim, -1.0f, 1.0f);
    }
    for (float& trim : params.pickupResonanceTrim) {
        trim = clamp(trim, -1.0f, 1.0f);
    }
    params.maskAmount = clamp(params.maskAmount, 0.0f, 1.0f);
    params.maskAzimuthDeg = clamp(params.maskAzimuthDeg, -180.0f, 180.0f);
    params.maskElevationDeg = clamp(params.maskElevationDeg, -90.0f, 90.0f);
    params.maskWidth = clamp(params.maskWidth, 0.0f, 1.0f);
    params.maskCurve = clamp(params.maskCurve, 0.0f, 1.0f);
    params.maskDry = clamp(params.maskDry, 0.0f, 1.0f);
    params.delayTimeMs = clamp(params.delayTimeMs, 5.0f, 2000.0f);
    params.delayFeedback = clamp(params.delayFeedback, 0.0f, 0.88f);
    params.delayTone = clamp(params.delayTone, 0.0f, 1.0f);
    for (float& trim : params.pickupDelayTimeTrim) {
        trim = clamp(trim, -1.0f, 1.0f);
    }
    for (float& trim : params.pickupDelayFeedbackTrim) {
        trim = clamp(trim, -1.0f, 1.0f);
    }
    params.pitchSemitones = clamp(params.pitchSemitones, -24.0f, 24.0f);
    params.pitchWindowMs = clamp(params.pitchWindowMs, 20.0f, 180.0f);
    params.pitchGlideMs = clamp(params.pitchGlideMs, 10.0f, 2000.0f);
    for (float& trim : params.pickupPitchTrim) {
        trim = clamp(trim, -1.0f, 1.0f);
    }
    for (float& trim : params.pickupPitchWindowTrim) {
        trim = clamp(trim, -1.0f, 1.0f);
    }
    params.gainDb = clamp(params.gainDb, -60.0f, 18.0f);
    for (float& trim : params.pickupGainTrim) {
        trim = clamp(trim, -1.0f, 1.0f);
    }
    return params;
}

inline float ambiEffectPickupFilterPosition(float globalPosition, float trim)
{
    return clamp(globalPosition + trim * 0.5f, 0.0f, 1.0f);
}

inline float ambiEffectPickupHash(uint32_t pickup, uint32_t salt)
{
    uint32_t x = pickup * 747796405u + salt * 2891336453u;
    x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
    x = (x >> 22u) ^ x;
    return static_cast<float>(x & 0xffffu) / 32767.5f - 1.0f;
}

inline float ambiEffectPickupOrdinal(uint32_t pickup, uint32_t count)
{
    return count > 1u
        ? static_cast<float>(pickup) * 2.0f
            / static_cast<float>(count - 1u) - 1.0f
        : 0.0f;
}

inline float ambiEffectPickupFilterPosition(float globalPosition, float trim,
    float spread, float deviation, uint32_t pickup, uint32_t count)
{
    const float relationship = ambiEffectPickupOrdinal(pickup, count)
        * clamp(spread, 0.0f, 1.0f) * 0.38f
        + ambiEffectPickupHash(pickup, 1u)
            * clamp(deviation, 0.0f, 1.0f) * 0.22f;
    return clamp(globalPosition + trim * 0.5f + relationship, 0.0f, 1.0f);
}

inline float ambiEffectPickupResonance(float globalResonance, float trim,
    float spread, float deviation, uint32_t pickup, uint32_t count)
{
    const float relationship = -ambiEffectPickupOrdinal(pickup, count)
        * clamp(spread, 0.0f, 1.0f) * 0.22f
        + ambiEffectPickupHash(pickup, 7u)
            * clamp(deviation, 0.0f, 1.0f) * 0.18f;
    return clamp(globalResonance + trim * 0.5f + relationship, 0.0f, 1.0f);
}

inline float ambiEffectPickupDelayMs(float globalMs, float trim,
    float spread, float deviation, uint32_t pickup, uint32_t count)
{
    const float exponent = trim * 1.5f
        + ambiEffectPickupOrdinal(pickup, count)
            * clamp(spread, 0.0f, 1.0f)
        + ambiEffectPickupHash(pickup, 11u)
            * clamp(deviation, 0.0f, 1.0f) * 0.5f;
    return clamp(globalMs * std::pow(2.0f, exponent), 5.0f, 2000.0f);
}

inline float ambiEffectPickupDelayFeedback(float globalFeedback, float trim,
    float spread, float deviation, uint32_t pickup, uint32_t count)
{
    const float relationship = trim * 0.35f
        - ambiEffectPickupOrdinal(pickup, count)
            * clamp(spread, 0.0f, 1.0f) * 0.10f
        + ambiEffectPickupHash(pickup, 19u)
            * clamp(deviation, 0.0f, 1.0f) * 0.08f;
    return clamp(globalFeedback + relationship, 0.0f, 0.88f);
}

inline float ambiEffectPickupPitchSemitones(float globalSemitones, float trim,
    float spread, float deviation, uint32_t pickup, uint32_t count)
{
    return clamp(globalSemitones + trim * 12.0f
        + ambiEffectPickupOrdinal(pickup, count)
            * clamp(spread, 0.0f, 1.0f) * 12.0f
        + ambiEffectPickupHash(pickup, 23u)
            * clamp(deviation, 0.0f, 1.0f) * 3.0f,
        -24.0f, 24.0f);
}

inline float ambiEffectPickupPitchWindowMs(float globalMs, float trim,
    float spread, float deviation, uint32_t pickup, uint32_t count)
{
    const float relationship = trim * 60.0f
        - ambiEffectPickupOrdinal(pickup, count)
            * clamp(spread, 0.0f, 1.0f) * 18.0f
        + ambiEffectPickupHash(pickup, 29u)
            * clamp(deviation, 0.0f, 1.0f) * 12.0f;
    return clamp(globalMs + relationship, 20.0f, 180.0f);
}

inline float ambiEffectPickupGainDb(float globalDb, float trim,
    float spread, float deviation, uint32_t pickup, uint32_t count)
{
    return clamp(globalDb + trim * 24.0f
        + ambiEffectPickupOrdinal(pickup, count)
            * clamp(spread, 0.0f, 1.0f) * 18.0f
        + ambiEffectPickupHash(pickup, 31u)
            * clamp(deviation, 0.0f, 1.0f) * 9.0f,
        -60.0f, 18.0f);
}

inline float ambiEffectMaskExponent(float width, float curve)
{
    const float widthExponent = std::pow(
        12.0f, 1.0f - clamp(width, 0.0f, 1.0f)) * 0.5f;
    const float curveScale = std::pow(
        8.0f, clamp(curve, 0.0f, 1.0f) * 2.0f - 1.0f);
    return widthExponent * curveScale;
}

inline uint32_t ambiEffectChannelsForOrder(uint32_t order)
{
    order = std::clamp<uint32_t>(order, 1u, kAmbiEffectDjFilterMaxOrder);
    return (order + 1u) * (order + 1u);
}

inline AmbiEffectBody ambiEffectDefaultBodyForOrder(uint32_t order)
{
    order = std::clamp<uint32_t>(order, 1u, kAmbiEffectDjFilterMaxOrder);
    if (order <= 2u) return AmbiEffectBody::Icosa12;
    if (order == 3u) return AmbiEffectBody::Dodeca20;
    return AmbiEffectBody::Sphere24;
}

inline AmbiEffectBody resolveAmbiEffectBody(
    AmbiEffectBody requested, uint32_t order)
{
    requested = static_cast<AmbiEffectBody>(
        std::min<uint32_t>(static_cast<uint32_t>(requested), 5u));
    if (requested == AmbiEffectBody::Tetra4
        || requested == AmbiEffectBody::Cube8) {
        requested = AmbiEffectBody::Icosa12;
    }
    return requested == AmbiEffectBody::Auto
        ? ambiEffectDefaultBodyForOrder(order) : requested;
}

inline uint32_t ambiEffectBodyPickupCount(AmbiEffectBody body)
{
    switch (body) {
    case AmbiEffectBody::Tetra4:
    case AmbiEffectBody::Cube8:
    case AmbiEffectBody::Icosa12: return 12u;
    case AmbiEffectBody::Dodeca20: return 20u;
    case AmbiEffectBody::Sphere24: return 24u;
    case AmbiEffectBody::Auto:
    default: return 12u;
    }
}

inline const char* ambiEffectBodyName(AmbiEffectBody body)
{
    switch (body) {
    case AmbiEffectBody::Auto: return "AUTO";
    case AmbiEffectBody::Tetra4: return "TETRA 4";
    case AmbiEffectBody::Cube8: return "CUBE 8";
    case AmbiEffectBody::Icosa12: return "ICOSA 12";
    case AmbiEffectBody::Dodeca20: return "DODECA 20";
    case AmbiEffectBody::Sphere24: return "SPHERE 24";
    }
    return "AUTO";
}

inline std::array<Vec3, kAmbiEffectDjFilterMaxPickups>
ambiEffectBodyDirections(AmbiEffectBody body)
{
    std::array<Vec3, kAmbiEffectDjFilterMaxPickups> result {};
    constexpr float phi = 1.6180339887498948f;
    constexpr float invPhi = 1.0f / phi;
    if (body == AmbiEffectBody::Sphere24) {
        for (uint32_t i = 0u; i < kAmbisonicSphere24PointCount; ++i) {
            result[i] = normalize(kAmbisonicSphere24Points[i]);
        }
        return result;
    }
    if (body == AmbiEffectBody::Dodeca20) {
        const std::array<Vec3, 20u> points {{
            { 1, 1, 1 }, { 1, 1, -1 }, { 1, -1, 1 }, { 1, -1, -1 },
            { -1, 1, 1 }, { -1, 1, -1 }, { -1, -1, 1 }, { -1, -1, -1 },
            { 0, invPhi, phi }, { 0, invPhi, -phi },
            { 0, -invPhi, phi }, { 0, -invPhi, -phi },
            { invPhi, phi, 0 }, { invPhi, -phi, 0 },
            { -invPhi, phi, 0 }, { -invPhi, -phi, 0 },
            { phi, 0, invPhi }, { phi, 0, -invPhi },
            { -phi, 0, invPhi }, { -phi, 0, -invPhi },
        }};
        for (uint32_t i = 0u; i < points.size(); ++i) result[i] = normalize(points[i]);
        return result;
    }
    const std::array<Vec3, 12u> points {{
        { 0, 1, phi }, { 0, -1, phi }, { phi, 0, 1 }, { 1, phi, 0 },
        { -1, phi, 0 }, { -phi, 0, 1 }, { -phi, 0, -1 }, { -1, -phi, 0 },
        { 0, -1, -phi }, { 1, -phi, 0 }, { phi, 0, -1 }, { 0, 1, -phi },
    }};
    for (uint32_t i = 0u; i < points.size(); ++i) result[i] = normalize(points[i]);
    return result;
}

inline const char* ambiEffectTopologyName(AmbiEffectTopology topology)
{
    switch (topology) {
    case AmbiEffectTopology::Local: return "LOCAL";
    case AmbiEffectTopology::Cross: return "CROSS";
    case AmbiEffectTopology::Diffuse: return "DIFFUSE";
    case AmbiEffectTopology::Roaming: return "ROAMING";
    }
    return "LOCAL";
}

inline const char* ambiEffectFilterPositionName(float value)
{
    value = clamp(value, 0.0f, 1.0f);
    if (value < 0.495f) return "LOW PASS";
    if (value > 0.505f) return "HIGH PASS";
    return "OPEN";
}

class AmbiEffectDjFilter {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::clamp(
            std::isfinite(sampleRate) ? sampleRate : 48000.0,
            1000.0, 768000.0);
        parameterCoefficient_ = onePoleCoefficient(0.018f);
        matrixCoefficient_ = onePoleCoefficient(0.030f);
        topologyCoefficient_ = onePoleCoefficient(0.024f);
        levelCoefficient_ = onePoleCoefficient(0.055f);
        filterCoefficientInterval_ = std::max<uint32_t>(1u,
            static_cast<uint32_t>(std::round(sampleRate_ / 3000.0)));
        buildMatrixCache();
        setParams(params_);
        if (params_.engine == AmbiEffectEngine::Delay) {
            ensureDelayBuffers();
        } else if (params_.engine == AmbiEffectEngine::Pitch) {
            ensurePitchBuffers();
        }
        snapTargets();
        resetFilterStates();
        resetDelayStates();
        resetPitchStates();
    }

    void reset()
    {
        snapTargets();
        resetFilterStates();
        resetDelayStates();
        resetPitchStates();
        roamingPhase_ = 0.0f;
        previousTopology_ = params_.topology;
        targetTopology_ = params_.topology;
        topologyFade_ = 1.0f;
        filterCoefficientCountdown_ = 0u;
        nodeLevel_.fill(0.0f);
    }

    void setParams(AmbiEffectDjFilterParams params)
    {
        const auto next = sanitizeAmbiEffectDjFilterParams(params);
        const bool matrixChanged = !currentMatrix_
            || next.order != params_.order
            || resolveAmbiEffectBody(next.body, next.order)
                != resolveAmbiEffectBody(params_.body, params_.order);
        if (next.topology != targetTopology_) {
            previousTopology_ = targetTopology_;
            targetTopology_ = next.topology;
            topologyFade_ = 0.0f;
        }
        params_ = next;
        targetFilter_ = params_.filter;
        targetResonance_ = params_.resonance;
        targetSpread_ = params_.spread;
        targetDeviation_ = params_.deviation;
        targetTopologyAmount_ = params_.topologyAmount;
        targetRoamingRateHz_ = params_.roamingRateHz;
        targetMix_ = params_.mix;
        targetOutputGain_ = dbToGain(params_.outputGainDb);
        targetPickupFilterTrim_ = params_.pickupFilterTrim;
        targetPickupResonanceTrim_ = params_.pickupResonanceTrim;
        targetMaskDry_ = params_.maskDry;
        targetDelayTimeMs_ = params_.delayTimeMs;
        targetDelayFeedback_ = params_.delayFeedback;
        targetDelayTone_ = params_.delayTone;
        targetPickupDelayTimeTrim_ = params_.pickupDelayTimeTrim;
        targetPickupDelayFeedbackTrim_ = params_.pickupDelayFeedbackTrim;
        targetPitchSemitones_ = params_.pitchSemitones;
        targetPitchWindowMs_ = params_.pitchWindowMs;
        targetPitchGlideMs_ = params_.pitchGlideMs;
        targetPickupPitchTrim_ = params_.pickupPitchTrim;
        targetPickupPitchWindowTrim_ = params_.pickupPitchWindowTrim;
        targetGainDb_ = params_.gainDb;
        targetPickupGainTrim_ = params_.pickupGainTrim;
        if (matrixChanged) updateMatrixTargets();
        updateMaskTargets();
    }

    const AmbiEffectDjFilterParams& params() const { return params_; }

    AmbiEffectBody resolvedBody() const
    {
        return resolveAmbiEffectBody(params_.body, params_.order);
    }

    uint32_t activePickupCount() const
    {
        return ambiEffectBodyPickupCount(resolvedBody());
    }

    float nodeLevel(uint32_t node) const
    {
        return node < kAmbiEffectDjFilterMaxPickups
            ? nodeLevel_[node] : 0.0f;
    }

    float nodeWetMask(uint32_t node) const
    {
        return node < kAmbiEffectDjFilterMaxPickups
            ? currentMaskGain_[node] : 1.0f;
    }

    float roamingPhase() const { return roamingPhase_; }

    template <typename Sample>
    void process(Sample** input, Sample** output,
        uint32_t inputChannels, uint32_t outputChannels,
        uint32_t frames)
    {
        if (!output) return;
        const uint32_t inCount = std::min<uint32_t>(
            inputChannels, kAmbiEffectDjFilterMaxChannels);
        const uint32_t outCount = std::min<uint32_t>(
            outputChannels, kAmbiEffectDjFilterMaxChannels);
        const uint32_t bodyCount = activePickupCount();
        std::array<float, kAmbiEffectDjFilterMaxChannels> frame {};
        std::array<float, kAmbiEffectDjFilterMaxPickups> ears {};
        std::array<float, kAmbiEffectDjFilterMaxPickups> filtered {};
        std::array<float, kAmbiEffectDjFilterMaxPickups> routedPrevious {};
        std::array<float, kAmbiEffectDjFilterMaxPickups> routedTarget {};
        std::array<float, kAmbiEffectDjFilterMaxPickups> delta {};

        for (uint32_t sample = 0u; sample < frames; ++sample) {
            smoothParameters();
            smoothMatrices();
            if (filterCoefficientCountdown_ == 0u) {
                updateFilterCoefficients();
                filterCoefficientCountdown_ = filterCoefficientInterval_;
            }
            --filterCoefficientCountdown_;
            topologyFade_ += (1.0f - topologyFade_) * topologyCoefficient_;
            if (topologyFade_ > 0.99999f) {
                topologyFade_ = 1.0f;
                previousTopology_ = targetTopology_;
            }
            roamingPhase_ += currentRoamingRateHz_
                / static_cast<float>(sampleRate_);
            roamingPhase_ -= std::floor(roamingPhase_);

            for (uint32_t ch = 0u; ch < kAmbiEffectDjFilterMaxChannels; ++ch) {
                frame[ch] = ch < inCount && input && input[ch]
                    ? static_cast<float>(input[ch][sample]) : 0.0f;
            }

            const uint32_t matrixChannels = matrixSmoothingActive_
                ? kAmbiEffectDjFilterMaxChannels
                : ambiEffectChannelsForOrder(params_.order);
            for (uint32_t node = 0u; node < bodyCount; ++node) {
                float value = 0.0f;
                for (uint32_t ch = 0u; ch < matrixChannels; ++ch) {
                    value += currentDecode_[node][ch] * frame[ch];
                }
                ears[node] = flushDenormal(value);
            }
            for (uint32_t node = bodyCount;
                node < kAmbiEffectDjFilterMaxPickups; ++node) {
                ears[node] = 0.0f;
            }

            std::array<float, kAmbiEffectDjFilterMaxPickups> filterDepth {};
            if (params_.engine == AmbiEffectEngine::DjFilter) {
              for (uint32_t node = 0u; node < bodyCount; ++node) {
                const float position = ambiEffectPickupFilterPosition(
                    currentFilter_, currentPickupFilterTrim_[node],
                    currentSpread_, currentDeviation_, node, bodyCount);
                const float side = std::fabs(position * 2.0f - 1.0f);
                const float depth = side * side * (3.0f - side * 2.0f);
                const float lpFirst = lowPass_[node][0].process(
                    ears[node], lowPassCoefficients_[node]).low;
                const float lp = lowPass_[node][1].process(
                    lpFirst, lowPassDamping_[node]).low;
                const float hpFirst = highPass_[node][0].process(
                    ears[node], highPassCoefficients_[node]).high;
                const float hp = highPass_[node][1].process(
                    hpFirst, highPassDamping_[node]).high;
                const float wing = position < 0.5f ? lp : hp;
                filtered[node] = lerp(ears[node], wing, depth);
                filterDepth[node] = depth;
                const float magnitude = std::fabs(ears[node]);
                nodeLevel_[node] += (magnitude - nodeLevel_[node])
                    * levelCoefficient_;
                nodeLevel_[node] = flushDenormal(nodeLevel_[node]);
              }
            } else if (params_.engine == AmbiEffectEngine::Delay) {
              for (uint32_t node = 0u; node < bodyCount; ++node) {
                filtered[node] = processDelayNode(
                    node, bodyCount, ears[node]);
                filterDepth[node] = 1.0f;
                const float magnitude = std::fabs(ears[node]);
                nodeLevel_[node] += (magnitude - nodeLevel_[node])
                    * levelCoefficient_;
                nodeLevel_[node] = flushDenormal(nodeLevel_[node]);
              }
            } else if (params_.engine == AmbiEffectEngine::Pitch) {
              for (uint32_t node = 0u; node < bodyCount; ++node) {
                filtered[node] = processPitchNode(
                    node, bodyCount, ears[node]);
                filterDepth[node] = 1.0f;
                const float magnitude = std::fabs(ears[node]);
                nodeLevel_[node] += (magnitude - nodeLevel_[node])
                    * levelCoefficient_;
                nodeLevel_[node] = flushDenormal(nodeLevel_[node]);
              }
            } else {
              for (uint32_t node = 0u; node < bodyCount; ++node) {
                const float gainDb = ambiEffectPickupGainDb(
                    currentGainDb_, currentPickupGainTrim_[node],
                    currentSpread_, currentDeviation_, node, bodyCount);
                filtered[node] = flushDenormal(
                    ears[node] * dbToGain(gainDb));
                filterDepth[node] = 1.0f;
                const float magnitude = std::fabs(ears[node]);
                nodeLevel_[node] += (magnitude - nodeLevel_[node])
                    * levelCoefficient_;
                nodeLevel_[node] = flushDenormal(nodeLevel_[node]);
              }
            }
            for (uint32_t node = bodyCount;
                node < kAmbiEffectDjFilterMaxPickups; ++node) {
                filtered[node] = 0.0f;
                nodeLevel_[node] += (0.0f - nodeLevel_[node])
                    * levelCoefficient_;
            }

            routeBody(filtered, routedPrevious,
                previousTopology_, bodyCount, roamingPhase_);
            routeBody(filtered, routedTarget,
                targetTopology_, bodyCount, roamingPhase_);
            for (uint32_t node = 0u; node < bodyCount; ++node) {
                const float routed = lerp(
                    routedPrevious[node], routedTarget[node], topologyFade_);
                const float routeDepth = currentTopologyAmount_
                    * filterDepth[node];
                const float bodyOutput = lerp(
                    filtered[node], routed, routeDepth);
                const float maskedTarget = lerp(
                    ears[node] * currentMaskDry_, bodyOutput,
                    currentMaskGain_[node]);
                delta[node] = maskedTarget - ears[node];
            }
            for (uint32_t node = bodyCount;
                node < kAmbiEffectDjFilterMaxPickups; ++node) {
                delta[node] = 0.0f;
            }

            const uint32_t correctionChannels = matrixSmoothingActive_
                ? outCount
                : std::min<uint32_t>(outCount,
                    ambiEffectChannelsForOrder(params_.order));
            for (uint32_t ch = 0u; ch < correctionChannels; ++ch) {
                float correction = 0.0f;
                for (uint32_t node = 0u; node < bodyCount; ++node) {
                    correction += currentEncode_[ch][node] * delta[node];
                }
                const float dry = frame[ch] * currentChannelGain_[ch];
                const float value = (dry + correction * currentMix_)
                    * currentOutputGain_;
                if (output[ch]) {
                    output[ch][sample] = static_cast<Sample>(
                        flushDenormal(std::isfinite(value) ? value : 0.0f));
                }
            }
            for (uint32_t ch = correctionChannels;
                ch < outCount; ++ch) {
                if (output[ch]) output[ch][sample] = Sample(0);
            }
            for (uint32_t ch = outCount; ch < outputChannels; ++ch) {
                if (output[ch]) output[ch][sample] = Sample(0);
            }
        }
    }

private:
    struct SvfCoefficients {
        float a1 = 1.0f;
        float a2 = 0.0f;
        float a3 = 0.0f;
        float damping = 1.41421356f;
    };

    struct SvfOutputs {
        float low = 0.0f;
        float high = 0.0f;
    };

    struct SvfState {
        float integrator1 = 0.0f;
        float integrator2 = 0.0f;

        void reset()
        {
            integrator1 = 0.0f;
            integrator2 = 0.0f;
        }

        SvfOutputs process(float input, const SvfCoefficients& coefficients)
        {
            const float v3 = input - integrator2;
            const float v1 = coefficients.a1 * integrator1
                + coefficients.a2 * v3;
            const float v2 = integrator2
                + coefficients.a2 * integrator1
                + coefficients.a3 * v3;
            integrator1 = flushDenormal(2.0f * v1 - integrator1);
            integrator2 = flushDenormal(2.0f * v2 - integrator2);
            const float high = input
                - coefficients.damping * v1 - v2;
            return { flushDenormal(v2), flushDenormal(high) };
        }
    };

    struct MatrixSet {
        uint32_t count = 0u;
        std::array<Vec3, kAmbiEffectDjFilterMaxPickups> directions {};
        std::array<std::array<float, kAmbiEffectDjFilterMaxChannels>,
            kAmbiEffectDjFilterMaxPickups> decode {};
        std::array<std::array<float, kAmbiEffectDjFilterMaxPickups>,
            kAmbiEffectDjFilterMaxChannels> encode {};
        std::array<uint32_t, kAmbiEffectDjFilterMaxPickups> opposite {};
        std::array<std::array<uint32_t, 5u>,
            kAmbiEffectDjFilterMaxPickups> neighbors {};
        std::array<uint32_t, kAmbiEffectDjFilterMaxPickups> neighborCount {};
        std::array<uint32_t, kAmbiEffectDjFilterMaxPickups> roamCycle {};
    };

    static float dot(Vec3 a, Vec3 b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    static float exponentialMap(float start, float end, float position)
    {
        start = std::max(1.0f, start);
        end = std::max(1.0f, end);
        return start * std::pow(end / start, clamp(position, 0.0f, 1.0f));
    }

    float onePoleCoefficient(float seconds) const
    {
        return 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate_ * seconds));
    }

    SvfCoefficients coefficientsFor(float cutoffHz, float resonance) const
    {
        cutoffHz = clamp(cutoffHz, 10.0f,
            static_cast<float>(sampleRate_) * 0.44f);
        const float g = std::tan(
            kPi * cutoffHz / static_cast<float>(sampleRate_));
        const float damping = 2.0f - clamp(resonance, 0.0f, 1.0f) * 1.76f;
        const float a1 = 1.0f / (1.0f + g * (g + damping));
        return { a1, g * a1, g * g * a1, damping };
    }

    void resetFilterStates()
    {
        for (auto& node : lowPass_) {
            for (auto& stage : node) stage.reset();
        }
        for (auto& node : highPass_) {
            for (auto& stage : node) stage.reset();
        }
    }

    void smoothParameters()
    {
        currentFilter_ += (targetFilter_ - currentFilter_)
            * parameterCoefficient_;
        currentResonance_ += (targetResonance_ - currentResonance_)
            * parameterCoefficient_;
        currentSpread_ += (targetSpread_ - currentSpread_)
            * parameterCoefficient_;
        currentDeviation_ += (targetDeviation_ - currentDeviation_)
            * parameterCoefficient_;
        currentTopologyAmount_ += (
            targetTopologyAmount_ - currentTopologyAmount_)
            * parameterCoefficient_;
        currentRoamingRateHz_ += (
            targetRoamingRateHz_ - currentRoamingRateHz_)
            * parameterCoefficient_;
        currentMix_ += (targetMix_ - currentMix_)
            * parameterCoefficient_;
        currentOutputGain_ += (targetOutputGain_ - currentOutputGain_)
            * parameterCoefficient_;
        for (uint32_t node = 0u;
            node < kAmbiEffectDjFilterMaxPickups; ++node) {
            currentPickupFilterTrim_[node] += (
                targetPickupFilterTrim_[node]
                - currentPickupFilterTrim_[node])
                * parameterCoefficient_;
            currentPickupResonanceTrim_[node] += (
                targetPickupResonanceTrim_[node]
                - currentPickupResonanceTrim_[node])
                * parameterCoefficient_;
            currentMaskGain_[node] += (
                targetMaskGain_[node] - currentMaskGain_[node])
                * parameterCoefficient_;
        }
        currentMaskDry_ += (targetMaskDry_ - currentMaskDry_)
            * parameterCoefficient_;
        currentDelayFeedback_ += (
            targetDelayFeedback_ - currentDelayFeedback_)
            * parameterCoefficient_;
        currentDelayTone_ += (targetDelayTone_ - currentDelayTone_)
            * parameterCoefficient_;
        currentPitchSemitones_ += (
            targetPitchSemitones_ - currentPitchSemitones_)
            * parameterCoefficient_;
        currentPitchWindowMs_ += (
            targetPitchWindowMs_ - currentPitchWindowMs_)
            * parameterCoefficient_;
        currentPitchGlideMs_ += (
            targetPitchGlideMs_ - currentPitchGlideMs_)
            * parameterCoefficient_;
        currentGainDb_ += (targetGainDb_ - currentGainDb_)
            * parameterCoefficient_;
        for (uint32_t node = 0u;
            node < kAmbiEffectDjFilterMaxPickups; ++node) {
            currentPickupDelayFeedbackTrim_[node] += (
                targetPickupDelayFeedbackTrim_[node]
                - currentPickupDelayFeedbackTrim_[node])
                * parameterCoefficient_;
            currentPickupPitchTrim_[node] += (
                targetPickupPitchTrim_[node]
                - currentPickupPitchTrim_[node])
                * parameterCoefficient_;
            currentPickupPitchWindowTrim_[node] += (
                targetPickupPitchWindowTrim_[node]
                - currentPickupPitchWindowTrim_[node])
                * parameterCoefficient_;
            currentPickupGainTrim_[node] += (
                targetPickupGainTrim_[node]
                - currentPickupGainTrim_[node])
                * parameterCoefficient_;
        }
    }

    void updateFilterCoefficients()
    {
        const float nyquistLimit = static_cast<float>(sampleRate_) * 0.44f;
        const float lpMaximum = std::min(20000.0f, nyquistLimit);
        const float hpMaximum = std::min(14000.0f, nyquistLimit);
        for (uint32_t node = 0u;
            node < kAmbiEffectDjFilterMaxPickups; ++node) {
            const float position = ambiEffectPickupFilterPosition(
                currentFilter_, currentPickupFilterTrim_[node],
                currentSpread_, currentDeviation_, node,
                std::max<uint32_t>(1u, activePickupCount()));
            const float resonance = ambiEffectPickupResonance(
                currentResonance_, currentPickupResonanceTrim_[node],
                currentSpread_, currentDeviation_, node,
                std::max<uint32_t>(1u, activePickupCount()));
            const float side = std::fabs(position * 2.0f - 1.0f);
            const float cutoffPosition = std::pow(side, 0.70f);
            const float lpCutoff = exponentialMap(
                lpMaximum, 70.0f, cutoffPosition);
            const float hpCutoff = exponentialMap(
                22.0f, hpMaximum, cutoffPosition);
            lowPassCoefficients_[node] = coefficientsFor(
                lpCutoff, resonance);
            highPassCoefficients_[node] = coefficientsFor(
                hpCutoff, resonance);
            lowPassDamping_[node] = coefficientsFor(lpCutoff, 0.22f);
            highPassDamping_[node] = coefficientsFor(hpCutoff, 0.22f);
        }
    }

    void ensureDelayBuffers()
    {
        const uint32_t needed = std::max<uint32_t>(4u,
            static_cast<uint32_t>(std::ceil(sampleRate_ * 2.05)) + 4u);
        if (delayBufferSize_ == needed && !delayBuffers_[0].empty()) return;
        delayBufferSize_ = needed;
        for (auto& buffer : delayBuffers_) buffer.assign(needed, 0.0f);
        delayWriteIndex_.fill(0u);
        delayToneState_.fill(0.0f);
        delayCrossfadeIncrement_ = 1.0f / std::max(
            1.0f, static_cast<float>(sampleRate_) * 0.035f);
        resetDelayTapTargets();
    }

    void resetDelayStates()
    {
        for (auto& buffer : delayBuffers_) {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
        }
        delayWriteIndex_.fill(0u);
        delayToneState_.fill(0.0f);
        resetDelayTapTargets();
    }

    void resetDelayTapTargets()
    {
        const uint32_t count = std::max<uint32_t>(1u, activePickupCount());
        for (uint32_t node = 0u;
            node < kAmbiEffectDjFilterMaxPickups; ++node) {
            const float timeMs = ambiEffectPickupDelayMs(
                targetDelayTimeMs_, targetPickupDelayTimeTrim_[node],
                targetSpread_, targetDeviation_, node, count);
            const float samples = clamp(
                timeMs * static_cast<float>(sampleRate_) * 0.001f,
                1.0f, static_cast<float>(std::max<uint32_t>(4u,
                    delayBufferSize_) - 3u));
            delayTapCurrentSamples_[node] = samples;
            delayTapNextSamples_[node] = samples;
            delayCrossfadePhase_[node] = 1.0f;
        }
    }

    float readDelayTap(uint32_t node, float delaySamples) const
    {
        float position = static_cast<float>(delayWriteIndex_[node])
            - delaySamples;
        while (position < 0.0f) {
            position += static_cast<float>(delayBufferSize_);
        }
        const uint32_t first = static_cast<uint32_t>(position)
            % delayBufferSize_;
        const uint32_t second = (first + 1u) % delayBufferSize_;
        return lerp(delayBuffers_[node][first], delayBuffers_[node][second],
            position - std::floor(position));
    }

    float processDelayNode(uint32_t node, uint32_t count, float input)
    {
        if (node >= kAmbiEffectDjFilterMaxPickups
            || delayBufferSize_ < 4u || delayBuffers_[node].empty()) {
            return 0.0f;
        }
        const float timeMs = ambiEffectPickupDelayMs(
            targetDelayTimeMs_, targetPickupDelayTimeTrim_[node],
            targetSpread_, targetDeviation_, node, count);
        const float desiredSamples = clamp(
            timeMs * static_cast<float>(sampleRate_) * 0.001f,
            1.0f, static_cast<float>(delayBufferSize_ - 3u));
        if (delayCrossfadePhase_[node] >= 1.0f
            && std::fabs(desiredSamples
                - delayTapCurrentSamples_[node]) > 0.25f) {
            delayTapNextSamples_[node] = desiredSamples;
            delayCrossfadePhase_[node] = 0.0f;
        }
        float delayed = readDelayTap(node, delayTapCurrentSamples_[node]);
        if (delayCrossfadePhase_[node] < 1.0f) {
            const float phase = clamp(delayCrossfadePhase_[node], 0.0f, 1.0f);
            const float angle = phase * kPi * 0.5f;
            const float toGain = std::sin(angle);
            const float fromGain = std::cos(angle);
            delayed = delayed * fromGain * fromGain
                + readDelayTap(node, delayTapNextSamples_[node])
                    * toGain * toGain;
            delayCrossfadePhase_[node] += delayCrossfadeIncrement_;
            if (delayCrossfadePhase_[node] >= 1.0f) {
                delayCrossfadePhase_[node] = 1.0f;
                delayTapCurrentSamples_[node] = delayTapNextSamples_[node];
            }
        }
        const float cutoff = 450.0f * std::pow(
            18000.0f / 450.0f, currentDelayTone_);
        const float toneCoefficient = 1.0f - std::exp(
            -2.0f * kPi * cutoff / static_cast<float>(sampleRate_));
        delayToneState_[node] += (delayed - delayToneState_[node])
            * toneCoefficient;
        const float feedback = ambiEffectPickupDelayFeedback(
            currentDelayFeedback_, currentPickupDelayFeedbackTrim_[node],
            currentSpread_, currentDeviation_, node, count);
        delayBuffers_[node][delayWriteIndex_[node]] = flushDenormal(
            input + std::tanh(delayToneState_[node] * feedback));
        delayWriteIndex_[node] = (delayWriteIndex_[node] + 1u)
            % delayBufferSize_;
        return flushDenormal(delayed);
    }

    void ensurePitchBuffers()
    {
        const uint32_t needed = std::max<uint32_t>(256u,
            static_cast<uint32_t>(std::ceil(sampleRate_ * 0.22)) + 8u);
        if (pitchBufferSize_ == needed && !pitchBuffers_[0].empty()) return;
        pitchBufferSize_ = needed;
        for (auto& buffer : pitchBuffers_) buffer.assign(needed, 0.0f);
        resetPitchStates();
    }

    void resetPitchStates()
    {
        for (auto& buffer : pitchBuffers_) {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
        }
        pitchWriteIndex_.fill(0u);
        pitchPhaseA_.fill(0.0f);
        pitchPhaseB_.fill(0.5f);
        pitchRatio_.fill(1.0f);
        pitchWindowSamples_.fill(clamp(
            targetPitchWindowMs_ * 0.001f * static_cast<float>(sampleRate_),
            8.0f, static_cast<float>(std::max<uint32_t>(12u,
                pitchBufferSize_) - 4u)));
    }

    float readPitchTap(uint32_t node, float delaySamples) const
    {
        float position = static_cast<float>(pitchWriteIndex_[node])
            - delaySamples;
        while (position < 0.0f) {
            position += static_cast<float>(pitchBufferSize_);
        }
        while (position >= static_cast<float>(pitchBufferSize_)) {
            position -= static_cast<float>(pitchBufferSize_);
        }
        const uint32_t first = static_cast<uint32_t>(position)
            % pitchBufferSize_;
        const uint32_t second = (first + 1u) % pitchBufferSize_;
        return lerp(pitchBuffers_[node][first], pitchBuffers_[node][second],
            position - std::floor(position));
    }

    float processPitchNode(uint32_t node, uint32_t count, float input)
    {
        if (node >= kAmbiEffectDjFilterMaxPickups
            || pitchBufferSize_ < 12u || pitchBuffers_[node].empty()) {
            return input;
        }
        auto& buffer = pitchBuffers_[node];
        buffer[pitchWriteIndex_[node]] = input;
        const float semitones = ambiEffectPickupPitchSemitones(
            currentPitchSemitones_, currentPickupPitchTrim_[node],
            currentSpread_, currentDeviation_, node, count);
        const float targetRatio = std::pow(2.0f, semitones / 12.0f);
        const float glideCoefficient = 1.0f - std::exp(-1.0f
            / std::max(1.0f, static_cast<float>(sampleRate_)
                * currentPitchGlideMs_ * 0.001f));
        pitchRatio_[node] += (targetRatio - pitchRatio_[node])
            * glideCoefficient;
        const float targetWindow = ambiEffectPickupPitchWindowMs(
            currentPitchWindowMs_, currentPickupPitchWindowTrim_[node],
            currentSpread_, currentDeviation_, node, count)
            * 0.001f * static_cast<float>(sampleRate_);
        pitchWindowSamples_[node] += (
            targetWindow - pitchWindowSamples_[node])
            * parameterCoefficient_;
        pitchWindowSamples_[node] = clamp(pitchWindowSamples_[node], 8.0f,
            static_cast<float>(pitchBufferSize_ - 4u));

        float wet = input;
        const float ratioDelta = std::fabs(pitchRatio_[node] - 1.0f);
        if (ratioDelta >= 0.00015f) {
            const float phaseStep = ratioDelta
                / std::max(8.0f, pitchWindowSamples_[node]);
            pitchPhaseA_[node] += phaseStep;
            pitchPhaseB_[node] += phaseStep;
            pitchPhaseA_[node] -= std::floor(pitchPhaseA_[node]);
            pitchPhaseB_[node] -= std::floor(pitchPhaseB_[node]);
            const auto readPhase = [&](float phase) {
                const float delay = pitchRatio_[node] >= 1.0f
                    ? pitchWindowSamples_[node] * (1.0f - phase)
                    : pitchWindowSamples_[node] * phase;
                return readPitchTap(node, clamp(delay, 1.0f,
                    static_cast<float>(pitchBufferSize_ - 4u)));
            };
            const float a = readPhase(pitchPhaseA_[node]);
            const float b = readPhase(pitchPhaseB_[node]);
            const auto window = [](float phase) {
                return 0.5f - 0.5f * std::cos(
                    2.0f * kPi * clamp(phase, 0.0f, 1.0f));
            };
            const float wa = window(pitchPhaseA_[node]);
            const float wb = window(pitchPhaseB_[node]);
            wet = (a * wa + b * wb) / std::max(0.0001f, wa + wb);
        }
        pitchWriteIndex_[node] = (pitchWriteIndex_[node] + 1u)
            % pitchBufferSize_;
        return flushDenormal(wet);
    }

    void smoothMatrices()
    {
        if (!matrixSmoothingActive_) return;
        float maximumDelta = 0.0f;
        for (uint32_t node = 0u;
            node < kAmbiEffectDjFilterMaxPickups; ++node) {
            for (uint32_t ch = 0u;
                ch < kAmbiEffectDjFilterMaxChannels; ++ch) {
                const float decodeDelta = targetDecode_[node][ch]
                    - currentDecode_[node][ch];
                const float encodeDelta = targetEncode_[ch][node]
                    - currentEncode_[ch][node];
                maximumDelta = std::max(maximumDelta,
                    std::max(std::fabs(decodeDelta),
                        std::fabs(encodeDelta)));
                currentDecode_[node][ch] += decodeDelta
                    * matrixCoefficient_;
                currentEncode_[ch][node] += encodeDelta
                    * matrixCoefficient_;
            }
        }
        for (uint32_t ch = 0u;
            ch < kAmbiEffectDjFilterMaxChannels; ++ch) {
            const float delta = targetChannelGain_[ch]
                - currentChannelGain_[ch];
            maximumDelta = std::max(maximumDelta, std::fabs(delta));
            currentChannelGain_[ch] += delta * matrixCoefficient_;
        }
        if (maximumDelta < 1.0e-5f) {
            snapMatrixTargets();
        }
    }

    void snapMatrixTargets()
    {
        currentDecode_ = targetDecode_;
        currentEncode_ = targetEncode_;
        currentChannelGain_ = targetChannelGain_;
        matrixSmoothingActive_ = false;
    }

    void snapTargets()
    {
        currentFilter_ = targetFilter_;
        currentResonance_ = targetResonance_;
        currentSpread_ = targetSpread_;
        currentDeviation_ = targetDeviation_;
        currentTopologyAmount_ = targetTopologyAmount_;
        currentRoamingRateHz_ = targetRoamingRateHz_;
        currentMix_ = targetMix_;
        currentOutputGain_ = targetOutputGain_;
        currentPickupFilterTrim_ = targetPickupFilterTrim_;
        currentPickupResonanceTrim_ = targetPickupResonanceTrim_;
        currentMaskGain_ = targetMaskGain_;
        currentMaskDry_ = targetMaskDry_;
        currentDelayTimeMs_ = targetDelayTimeMs_;
        currentDelayFeedback_ = targetDelayFeedback_;
        currentDelayTone_ = targetDelayTone_;
        currentPickupDelayTimeTrim_ = targetPickupDelayTimeTrim_;
        currentPickupDelayFeedbackTrim_ = targetPickupDelayFeedbackTrim_;
        currentPitchSemitones_ = targetPitchSemitones_;
        currentPitchWindowMs_ = targetPitchWindowMs_;
        currentPitchGlideMs_ = targetPitchGlideMs_;
        currentPickupPitchTrim_ = targetPickupPitchTrim_;
        currentPickupPitchWindowTrim_ = targetPickupPitchWindowTrim_;
        currentGainDb_ = targetGainDb_;
        currentPickupGainTrim_ = targetPickupGainTrim_;
        filterCoefficientCountdown_ = 0u;
        snapMatrixTargets();
    }

    void updateMatrixTargets()
    {
        if (!matricesBuilt_) return;
        const bool transitioning = currentMatrix_ != nullptr;
        const AmbiEffectBody body = resolveAmbiEffectBody(
            params_.body, params_.order);
        const uint32_t bodyIndex = static_cast<uint32_t>(body) - 1u;
        const MatrixSet& matrix = matrixCache_[params_.order - 1u][bodyIndex];
        targetDecode_ = matrix.decode;
        targetEncode_ = matrix.encode;
        currentMatrix_ = &matrix;
        const uint32_t channels = ambiEffectChannelsForOrder(params_.order);
        for (uint32_t ch = 0u;
            ch < kAmbiEffectDjFilterMaxChannels; ++ch) {
            targetChannelGain_[ch] = ch < channels ? 1.0f : 0.0f;
        }
        matrixSmoothingActive_ = transitioning;
    }

    void updateMaskTargets()
    {
        targetMaskGain_.fill(1.0f);
        if (!currentMatrix_) return;
        const float azimuth = params_.maskAzimuthDeg * kPi / 180.0f;
        const float elevation = params_.maskElevationDeg * kPi / 180.0f;
        const float cosElevation = std::cos(elevation);
        const Vec3 maskDirection {
            cosElevation * std::cos(azimuth),
            cosElevation * std::sin(azimuth),
            std::sin(elevation),
        };
        std::array<float, kAmbiEffectDjFilterMaxPickups> alignment {};
        float maximum = 0.000001f;
        for (uint32_t node = 0u; node < currentMatrix_->count; ++node) {
            alignment[node] = clamp(
                (dot(currentMatrix_->directions[node], maskDirection)
                    + 1.0f) * 0.5f,
                0.0f, 1.0f);
            maximum = std::max(maximum, alignment[node]);
        }
        const float exponent = ambiEffectMaskExponent(
            params_.maskWidth, params_.maskCurve);
        for (uint32_t node = 0u; node < currentMatrix_->count; ++node) {
            const float directional = std::pow(
                clamp(alignment[node] / maximum, 0.0f, 1.0f), exponent);
            targetMaskGain_[node] = lerp(
                1.0f, directional, params_.maskAmount);
        }
    }

    void buildMatrixCache()
    {
        for (uint32_t order = 1u;
            order <= kAmbiEffectDjFilterMaxOrder; ++order) {
            for (uint32_t bodyIndex = 0u; bodyIndex < 5u; ++bodyIndex) {
                buildMatrix(matrixCache_[order - 1u][bodyIndex],
                    order, static_cast<AmbiEffectBody>(bodyIndex + 1u));
            }
        }
        matricesBuilt_ = true;
    }

    static void setBodyDirections(MatrixSet& matrix, AmbiEffectBody body)
    {
        constexpr float phi = 1.6180339887498948f;
        if (body != AmbiEffectBody::Dodeca20
            && body != AmbiEffectBody::Sphere24) {
            matrix.count = 12u;
            const std::array<Vec3, 12u> points {{
                { 0, 1, phi }, { 0, -1, phi }, { phi, 0, 1 }, { 1, phi, 0 },
                { -1, phi, 0 }, { -phi, 0, 1 }, { -phi, 0, -1 }, { -1, -phi, 0 },
                { 0, -1, -phi }, { 1, -phi, 0 }, { phi, 0, -1 }, { 0, 1, -phi },
            }};
            for (uint32_t i = 0u; i < points.size(); ++i) {
                matrix.directions[i] = normalize(points[i]);
            }
            return;
        }
        if (body == AmbiEffectBody::Dodeca20) {
            matrix.count = 20u;
            constexpr float invPhi = 1.0f / phi;
            const std::array<Vec3, 20u> points {{
                { 1, 1, 1 }, { 1, 1, -1 }, { 1, -1, 1 }, { 1, -1, -1 },
                { -1, 1, 1 }, { -1, 1, -1 }, { -1, -1, 1 }, { -1, -1, -1 },
                { 0, invPhi, phi }, { 0, invPhi, -phi },
                { 0, -invPhi, phi }, { 0, -invPhi, -phi },
                { invPhi, phi, 0 }, { invPhi, -phi, 0 },
                { -invPhi, phi, 0 }, { -invPhi, -phi, 0 },
                { phi, 0, invPhi }, { phi, 0, -invPhi },
                { -phi, 0, invPhi }, { -phi, 0, -invPhi },
            }};
            for (uint32_t i = 0u; i < points.size(); ++i) {
                matrix.directions[i] = normalize(points[i]);
            }
            return;
        }
        matrix.count = kAmbisonicSphere24PointCount;
        for (uint32_t i = 0u; i < kAmbisonicSphere24PointCount; ++i) {
            matrix.directions[i] = normalize(kAmbisonicSphere24Points[i]);
        }
    }

    static bool invertMatrix(
        const std::array<std::array<double,
            kAmbiEffectDjFilterMaxPickups>,
            kAmbiEffectDjFilterMaxPickups>& input,
        uint32_t count,
        std::array<std::array<double,
            kAmbiEffectDjFilterMaxPickups>,
            kAmbiEffectDjFilterMaxPickups>& inverse)
    {
        std::array<std::array<double,
            kAmbiEffectDjFilterMaxPickups * 2u>,
            kAmbiEffectDjFilterMaxPickups> augmented {};
        for (uint32_t row = 0u; row < count; ++row) {
            for (uint32_t col = 0u; col < count; ++col) {
                augmented[row][col] = input[row][col];
            }
            augmented[row][count + row] = 1.0;
        }
        for (uint32_t pivot = 0u; pivot < count; ++pivot) {
            uint32_t best = pivot;
            for (uint32_t row = pivot + 1u; row < count; ++row) {
                if (std::fabs(augmented[row][pivot])
                    > std::fabs(augmented[best][pivot])) best = row;
            }
            if (std::fabs(augmented[best][pivot]) < 1.0e-12) return false;
            if (best != pivot) std::swap(augmented[best], augmented[pivot]);
            const double divisor = augmented[pivot][pivot];
            for (uint32_t col = 0u; col < count * 2u; ++col) {
                augmented[pivot][col] /= divisor;
            }
            for (uint32_t row = 0u; row < count; ++row) {
                if (row == pivot) continue;
                const double factor = augmented[row][pivot];
                for (uint32_t col = 0u; col < count * 2u; ++col) {
                    augmented[row][col] -= factor * augmented[pivot][col];
                }
            }
        }
        for (uint32_t row = 0u; row < count; ++row) {
            for (uint32_t col = 0u; col < count; ++col) {
                inverse[row][col] = augmented[row][count + col];
            }
        }
        return true;
    }

    static bool findRoamCycle(const MatrixSet& matrix,
        uint32_t depth, uint32_t current,
        std::array<uint32_t, kAmbiEffectDjFilterMaxPickups>& path,
        std::array<bool, kAmbiEffectDjFilterMaxPickups>& used)
    {
        if (depth == matrix.count) {
            for (uint32_t slot = 0u;
                slot < matrix.neighborCount[current]; ++slot) {
                if (matrix.neighbors[current][slot] == path[0]) return true;
            }
            return false;
        }
        for (uint32_t slot = 0u;
            slot < matrix.neighborCount[current]; ++slot) {
            const uint32_t next = matrix.neighbors[current][slot];
            if (used[next]) continue;
            used[next] = true;
            path[depth] = next;
            if (findRoamCycle(matrix, depth + 1u, next, path, used)) {
                return true;
            }
            used[next] = false;
        }
        return false;
    }

    static void buildRelationships(MatrixSet& matrix)
    {
        for (uint32_t node = 0u; node < matrix.count; ++node) {
            uint32_t opposite = 0u;
            float minimumDot = 2.0f;
            float nearestDot = -2.0f;
            for (uint32_t other = 0u; other < matrix.count; ++other) {
                if (node == other) continue;
                const float value = dot(
                    matrix.directions[node], matrix.directions[other]);
                if (value < minimumDot) {
                    minimumDot = value;
                    opposite = other;
                }
                nearestDot = std::max(nearestDot, value);
            }
            matrix.opposite[node] = opposite;
            uint32_t count = 0u;
            for (uint32_t other = 0u;
                other < matrix.count && count < 5u; ++other) {
                if (node == other) continue;
                const float value = dot(
                    matrix.directions[node], matrix.directions[other]);
                if (value >= nearestDot - 0.0001f) {
                    matrix.neighbors[node][count++] = other;
                }
            }
            matrix.neighborCount[node] = count;
        }

        matrix.roamCycle.fill(0u);
        std::array<bool, kAmbiEffectDjFilterMaxPickups> used {};
        used[0] = true;
        matrix.roamCycle[0] = 0u;
        if (!findRoamCycle(matrix, 1u, 0u, matrix.roamCycle, used)) {
            for (uint32_t node = 0u; node < matrix.count; ++node) {
                matrix.roamCycle[node] = node;
            }
        }
    }

    static void buildMatrix(MatrixSet& matrix,
        uint32_t order, AmbiEffectBody body)
    {
        matrix = {};
        setBodyDirections(matrix, body);
        buildRelationships(matrix);
        const uint32_t channels = ambiEffectChannelsForOrder(order);
        for (uint32_t node = 0u; node < matrix.count; ++node) {
            const auto basis = acnSn3dBasis7(matrix.directions[node]);
            double norm = 0.0;
            for (uint32_t ch = 0u; ch < channels; ++ch) {
                norm += static_cast<double>(basis[ch]) * basis[ch];
            }
            norm = std::max(1.0, norm);
            for (uint32_t ch = 0u; ch < channels; ++ch) {
                matrix.decode[node][ch] = static_cast<float>(
                    static_cast<double>(basis[ch]) / norm);
            }
        }

        std::array<std::array<double,
            kAmbiEffectDjFilterMaxPickups>,
            kAmbiEffectDjFilterMaxPickups> gram {};
        for (uint32_t row = 0u; row < matrix.count; ++row) {
            for (uint32_t col = 0u; col < matrix.count; ++col) {
                for (uint32_t ch = 0u; ch < channels; ++ch) {
                    gram[row][col] += static_cast<double>(
                        matrix.decode[row][ch]) * matrix.decode[col][ch];
                }
                if (row == col) gram[row][col] += 1.0e-5;
            }
        }
        std::array<std::array<double,
            kAmbiEffectDjFilterMaxPickups>,
            kAmbiEffectDjFilterMaxPickups> inverse {};
        if (!invertMatrix(gram, matrix.count, inverse)) return;
        for (uint32_t ch = 0u; ch < channels; ++ch) {
            for (uint32_t node = 0u; node < matrix.count; ++node) {
                double value = 0.0;
                for (uint32_t row = 0u; row < matrix.count; ++row) {
                    value += static_cast<double>(matrix.decode[row][ch])
                        * inverse[row][node];
                }
                matrix.encode[ch][node] = static_cast<float>(value);
            }
        }
    }

    void routeBody(
        const std::array<float, kAmbiEffectDjFilterMaxPickups>& input,
        std::array<float, kAmbiEffectDjFilterMaxPickups>& output,
        AmbiEffectTopology topology, uint32_t count, float phase) const
    {
        output.fill(0.0f);
        if (!currentMatrix_ || count == 0u) return;
        const MatrixSet& matrix = *currentMatrix_;
        if (topology == AmbiEffectTopology::Local) {
            for (uint32_t node = 0u; node < count; ++node) {
                output[node] = input[node];
            }
            return;
        }
        if (topology == AmbiEffectTopology::Cross) {
            for (uint32_t node = 0u; node < count; ++node) {
                output[node] = input[matrix.opposite[node] % count];
            }
            return;
        }
        if (topology == AmbiEffectTopology::Diffuse) {
            for (uint32_t node = 0u; node < count; ++node) {
                float value = 0.0f;
                const uint32_t neighbors = matrix.neighborCount[node];
                for (uint32_t slot = 0u; slot < neighbors; ++slot) {
                    value += input[matrix.neighbors[node][slot] % count];
                }
                output[node] = neighbors > 0u
                    ? value / static_cast<float>(neighbors) : input[node];
            }
            return;
        }

        const float position = phase * static_cast<float>(count);
        const uint32_t firstOffset = static_cast<uint32_t>(
            std::floor(position)) % count;
        const uint32_t secondOffset = (firstOffset + 1u) % count;
        const float fraction = position - std::floor(position);
        for (uint32_t ordinal = 0u; ordinal < count; ++ordinal) {
            const uint32_t destination = matrix.roamCycle[ordinal] % count;
            const uint32_t first = matrix.roamCycle[
                (ordinal + firstOffset) % count] % count;
            const uint32_t second = matrix.roamCycle[
                (ordinal + secondOffset) % count] % count;
            output[destination] = lerp(
                input[first], input[second], fraction);
        }
    }

    double sampleRate_ = 48000.0;
    AmbiEffectDjFilterParams params_ {};
    bool matricesBuilt_ = false;
    std::array<std::array<MatrixSet, 5u>,
        kAmbiEffectDjFilterMaxOrder> matrixCache_ {};
    const MatrixSet* currentMatrix_ = nullptr;
    bool matrixSmoothingActive_ = false;

    std::array<std::array<float, kAmbiEffectDjFilterMaxChannels>,
        kAmbiEffectDjFilterMaxPickups> currentDecode_ {};
    std::array<std::array<float, kAmbiEffectDjFilterMaxChannels>,
        kAmbiEffectDjFilterMaxPickups> targetDecode_ {};
    std::array<std::array<float, kAmbiEffectDjFilterMaxPickups>,
        kAmbiEffectDjFilterMaxChannels> currentEncode_ {};
    std::array<std::array<float, kAmbiEffectDjFilterMaxPickups>,
        kAmbiEffectDjFilterMaxChannels> targetEncode_ {};
    std::array<float, kAmbiEffectDjFilterMaxChannels> currentChannelGain_ {};
    std::array<float, kAmbiEffectDjFilterMaxChannels> targetChannelGain_ {};

    std::array<std::array<SvfState, 2u>,
        kAmbiEffectDjFilterMaxPickups> lowPass_ {};
    std::array<std::array<SvfState, 2u>,
        kAmbiEffectDjFilterMaxPickups> highPass_ {};
    std::array<SvfCoefficients,
        kAmbiEffectDjFilterMaxPickups> lowPassCoefficients_ {};
    std::array<SvfCoefficients,
        kAmbiEffectDjFilterMaxPickups> highPassCoefficients_ {};
    std::array<SvfCoefficients,
        kAmbiEffectDjFilterMaxPickups> lowPassDamping_ {};
    std::array<SvfCoefficients,
        kAmbiEffectDjFilterMaxPickups> highPassDamping_ {};
    std::array<float, kAmbiEffectDjFilterMaxPickups> nodeLevel_ {};
    std::array<float,
        kAmbiEffectDjFilterMaxPickups> targetPickupFilterTrim_ {};
    std::array<float,
        kAmbiEffectDjFilterMaxPickups> currentPickupFilterTrim_ {};
    std::array<float,
        kAmbiEffectDjFilterMaxPickups> targetPickupResonanceTrim_ {};
    std::array<float,
        kAmbiEffectDjFilterMaxPickups> currentPickupResonanceTrim_ {};
    std::array<float, kAmbiEffectDjFilterMaxPickups> targetMaskGain_ = [] {
        std::array<float, kAmbiEffectDjFilterMaxPickups> value {};
        value.fill(1.0f);
        return value;
    }();
    std::array<float, kAmbiEffectDjFilterMaxPickups> currentMaskGain_ = [] {
        std::array<float, kAmbiEffectDjFilterMaxPickups> value {};
        value.fill(1.0f);
        return value;
    }();
    uint32_t filterCoefficientInterval_ = 16u;
    uint32_t filterCoefficientCountdown_ = 0u;

    AmbiEffectTopology previousTopology_ = AmbiEffectTopology::Local;
    AmbiEffectTopology targetTopology_ = AmbiEffectTopology::Local;
    float topologyFade_ = 1.0f;
    float roamingPhase_ = 0.0f;

    float targetFilter_ = 0.5f;
    float currentFilter_ = 0.5f;
    float targetResonance_ = 0.12f;
    float currentResonance_ = 0.12f;
    float targetSpread_ = 0.0f;
    float currentSpread_ = 0.0f;
    float targetDeviation_ = 0.0f;
    float currentDeviation_ = 0.0f;
    float targetTopologyAmount_ = 0.65f;
    float currentTopologyAmount_ = 0.65f;
    float targetRoamingRateHz_ = 0.08f;
    float currentRoamingRateHz_ = 0.08f;
    float targetMix_ = 1.0f;
    float currentMix_ = 1.0f;
    float targetOutputGain_ = 1.0f;
    float currentOutputGain_ = 1.0f;
    float targetMaskDry_ = 1.0f;
    float currentMaskDry_ = 1.0f;
    float targetDelayTimeMs_ = 320.0f;
    float currentDelayTimeMs_ = 320.0f;
    float targetDelayFeedback_ = 0.32f;
    float currentDelayFeedback_ = 0.32f;
    float targetDelayTone_ = 0.62f;
    float currentDelayTone_ = 0.62f;
    std::array<float, kAmbiEffectDjFilterMaxPickups>
        targetPickupDelayTimeTrim_ {};
    std::array<float, kAmbiEffectDjFilterMaxPickups>
        currentPickupDelayTimeTrim_ {};
    std::array<float, kAmbiEffectDjFilterMaxPickups>
        targetPickupDelayFeedbackTrim_ {};
    std::array<float, kAmbiEffectDjFilterMaxPickups>
        currentPickupDelayFeedbackTrim_ {};
    std::array<std::vector<float>, kAmbiEffectDjFilterMaxPickups>
        delayBuffers_ {};
    std::array<uint32_t, kAmbiEffectDjFilterMaxPickups> delayWriteIndex_ {};
    std::array<float, kAmbiEffectDjFilterMaxPickups> delayToneState_ {};
    std::array<float, kAmbiEffectDjFilterMaxPickups>
        delayTapCurrentSamples_ {};
    std::array<float, kAmbiEffectDjFilterMaxPickups>
        delayTapNextSamples_ {};
    std::array<float, kAmbiEffectDjFilterMaxPickups>
        delayCrossfadePhase_ {};
    float delayCrossfadeIncrement_ = 1.0f / 1680.0f;
    uint32_t delayBufferSize_ = 0u;

    float targetPitchSemitones_ = 0.0f;
    float currentPitchSemitones_ = 0.0f;
    float targetPitchWindowMs_ = 80.0f;
    float currentPitchWindowMs_ = 80.0f;
    float targetPitchGlideMs_ = 250.0f;
    float currentPitchGlideMs_ = 250.0f;
    std::array<float, kAmbiEffectDjFilterMaxPickups>
        targetPickupPitchTrim_ {};
    std::array<float, kAmbiEffectDjFilterMaxPickups>
        currentPickupPitchTrim_ {};
    std::array<float, kAmbiEffectDjFilterMaxPickups>
        targetPickupPitchWindowTrim_ {};
    std::array<float, kAmbiEffectDjFilterMaxPickups>
        currentPickupPitchWindowTrim_ {};
    std::array<std::vector<float>, kAmbiEffectDjFilterMaxPickups>
        pitchBuffers_ {};
    std::array<uint32_t, kAmbiEffectDjFilterMaxPickups> pitchWriteIndex_ {};
    std::array<float, kAmbiEffectDjFilterMaxPickups> pitchPhaseA_ {};
    std::array<float, kAmbiEffectDjFilterMaxPickups> pitchPhaseB_ {};
    std::array<float, kAmbiEffectDjFilterMaxPickups> pitchRatio_ {};
    std::array<float, kAmbiEffectDjFilterMaxPickups> pitchWindowSamples_ {};
    uint32_t pitchBufferSize_ = 0u;

    float targetGainDb_ = 0.0f;
    float currentGainDb_ = 0.0f;
    std::array<float, kAmbiEffectDjFilterMaxPickups>
        targetPickupGainTrim_ {};
    std::array<float, kAmbiEffectDjFilterMaxPickups>
        currentPickupGainTrim_ {};

    float parameterCoefficient_ = 0.001f;
    float matrixCoefficient_ = 0.0007f;
    float topologyCoefficient_ = 0.0008f;
    float levelCoefficient_ = 0.0004f;
};

} // namespace s3g
