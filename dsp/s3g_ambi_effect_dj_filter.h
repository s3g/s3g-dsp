#pragma once

#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAmbiEffectDjFilterMaxOrder = 7u;
constexpr uint32_t kAmbiEffectDjFilterMaxChannels = 64u;
constexpr uint32_t kAmbiEffectDjFilterMaxPickups = 12u;

enum class AmbiEffectBody : uint32_t {
    Auto = 0u,
    Tetra4 = 1u,
    Cube8 = 2u,
    Icosa12 = 3u,
};

enum class AmbiEffectTopology : uint32_t {
    Local = 0u,
    Cross = 1u,
    Diffuse = 2u,
    Roaming = 3u,
};

struct AmbiEffectDjFilterParams {
    uint32_t order = 7u;
    AmbiEffectBody body = AmbiEffectBody::Auto;
    AmbiEffectTopology topology = AmbiEffectTopology::Local;
    float filter = 0.5f;
    float resonance = 0.12f;
    float topologyAmount = 0.65f;
    float roamingRateHz = 0.08f;
    float mix = 1.0f;
    float outputGainDb = 0.0f;
    std::array<float, kAmbiEffectDjFilterMaxPickups> pickupFilterTrim {};
    float maskAmount = 0.0f;
    float maskAzimuthDeg = 0.0f;
    float maskElevationDeg = 0.0f;
    float maskWidth = 0.35f;
};

inline AmbiEffectDjFilterParams sanitizeAmbiEffectDjFilterParams(
    AmbiEffectDjFilterParams params)
{
    params.order = std::clamp<uint32_t>(
        params.order, 1u, kAmbiEffectDjFilterMaxOrder);
    params.body = static_cast<AmbiEffectBody>(
        std::min<uint32_t>(static_cast<uint32_t>(params.body), 3u));
    params.topology = static_cast<AmbiEffectTopology>(
        std::min<uint32_t>(static_cast<uint32_t>(params.topology), 3u));
    params.filter = clamp(params.filter, 0.0f, 1.0f);
    params.resonance = clamp(params.resonance, 0.0f, 1.0f);
    params.topologyAmount = clamp(params.topologyAmount, 0.0f, 1.0f);
    params.roamingRateHz = clamp(params.roamingRateHz, 0.005f, 2.0f);
    params.mix = clamp(params.mix, 0.0f, 1.0f);
    params.outputGainDb = clamp(params.outputGainDb, -60.0f, 12.0f);
    for (float& trim : params.pickupFilterTrim) {
        trim = clamp(trim, -1.0f, 1.0f);
    }
    params.maskAmount = clamp(params.maskAmount, 0.0f, 1.0f);
    params.maskAzimuthDeg = clamp(params.maskAzimuthDeg, -180.0f, 180.0f);
    params.maskElevationDeg = clamp(params.maskElevationDeg, -90.0f, 90.0f);
    params.maskWidth = clamp(params.maskWidth, 0.0f, 1.0f);
    return params;
}

inline float ambiEffectPickupFilterPosition(float globalPosition, float trim)
{
    return clamp(globalPosition + trim * 0.5f, 0.0f, 1.0f);
}

inline uint32_t ambiEffectChannelsForOrder(uint32_t order)
{
    order = std::clamp<uint32_t>(order, 1u, kAmbiEffectDjFilterMaxOrder);
    return (order + 1u) * (order + 1u);
}

inline AmbiEffectBody ambiEffectDefaultBodyForOrder(uint32_t order)
{
    order = std::clamp<uint32_t>(order, 1u, kAmbiEffectDjFilterMaxOrder);
    if (order == 1u) return AmbiEffectBody::Tetra4;
    if (order == 2u) return AmbiEffectBody::Cube8;
    return AmbiEffectBody::Icosa12;
}

inline AmbiEffectBody resolveAmbiEffectBody(
    AmbiEffectBody requested, uint32_t order)
{
    requested = static_cast<AmbiEffectBody>(
        std::min<uint32_t>(static_cast<uint32_t>(requested), 3u));
    return requested == AmbiEffectBody::Auto
        ? ambiEffectDefaultBodyForOrder(order) : requested;
}

inline uint32_t ambiEffectBodyPickupCount(AmbiEffectBody body)
{
    switch (body) {
    case AmbiEffectBody::Tetra4: return 4u;
    case AmbiEffectBody::Cube8: return 8u;
    case AmbiEffectBody::Icosa12: return 12u;
    case AmbiEffectBody::Auto:
    default: return 4u;
    }
}

inline const char* ambiEffectBodyName(AmbiEffectBody body)
{
    switch (body) {
    case AmbiEffectBody::Auto: return "AUTO";
    case AmbiEffectBody::Tetra4: return "TETRA 4";
    case AmbiEffectBody::Cube8: return "CUBE 8";
    case AmbiEffectBody::Icosa12: return "ICOSA 12";
    }
    return "AUTO";
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
        snapTargets();
        resetFilterStates();
    }

    void reset()
    {
        snapTargets();
        resetFilterStates();
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
        targetTopologyAmount_ = params_.topologyAmount;
        targetRoamingRateHz_ = params_.roamingRateHz;
        targetMix_ = params_.mix;
        targetOutputGain_ = dbToGain(params_.outputGainDb);
        targetPickupFilterTrim_ = params_.pickupFilterTrim;
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
            for (uint32_t node = 0u; node < bodyCount; ++node) {
                const float position = ambiEffectPickupFilterPosition(
                    currentFilter_, currentPickupFilterTrim_[node]);
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
                delta[node] = (bodyOutput - ears[node])
                    * currentMaskGain_[node];
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
            currentMaskGain_[node] += (
                targetMaskGain_[node] - currentMaskGain_[node])
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
                currentFilter_, currentPickupFilterTrim_[node]);
            const float side = std::fabs(position * 2.0f - 1.0f);
            const float cutoffPosition = std::pow(side, 0.70f);
            const float lpCutoff = exponentialMap(
                lpMaximum, 70.0f, cutoffPosition);
            const float hpCutoff = exponentialMap(
                22.0f, hpMaximum, cutoffPosition);
            lowPassCoefficients_[node] = coefficientsFor(
                lpCutoff, currentResonance_);
            highPassCoefficients_[node] = coefficientsFor(
                hpCutoff, currentResonance_);
            lowPassDamping_[node] = coefficientsFor(lpCutoff, 0.22f);
            highPassDamping_[node] = coefficientsFor(hpCutoff, 0.22f);
        }
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
        currentTopologyAmount_ = targetTopologyAmount_;
        currentRoamingRateHz_ = targetRoamingRateHz_;
        currentMix_ = targetMix_;
        currentOutputGain_ = targetOutputGain_;
        currentPickupFilterTrim_ = targetPickupFilterTrim_;
        currentMaskGain_ = targetMaskGain_;
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
        const float exponent = std::pow(
            12.0f, 1.0f - params_.maskWidth) * 0.5f;
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
            for (uint32_t bodyIndex = 0u; bodyIndex < 3u; ++bodyIndex) {
                buildMatrix(matrixCache_[order - 1u][bodyIndex],
                    order, static_cast<AmbiEffectBody>(bodyIndex + 1u));
            }
        }
        matricesBuilt_ = true;
    }

    static void setBodyDirections(MatrixSet& matrix, AmbiEffectBody body)
    {
        constexpr float k = 0.5773502691896258f;
        constexpr float phi = 1.6180339887498948f;
        if (body == AmbiEffectBody::Tetra4) {
            matrix.count = 4u;
            matrix.directions[0] = normalize({ k, k, k });
            matrix.directions[1] = normalize({ -k, -k, k });
            matrix.directions[2] = normalize({ -k, k, -k });
            matrix.directions[3] = normalize({ k, -k, -k });
            return;
        }
        if (body == AmbiEffectBody::Cube8) {
            matrix.count = 8u;
            static constexpr std::array<Vec3, 8u> points {{
                { k, k, k }, { -k, -k, k }, { -k, k, -k }, { k, -k, -k },
                { -k, -k, -k }, { k, k, -k }, { k, -k, k }, { -k, k, k },
            }};
            matrix.directions = {};
            for (uint32_t i = 0u; i < points.size(); ++i) {
                matrix.directions[i] = normalize(points[i]);
            }
            return;
        }
        matrix.count = 12u;
        const std::array<Vec3, 12u> points {{
            { 0, 1, phi }, { 0, -1, phi }, { phi, 0, 1 }, { 1, phi, 0 },
            { -1, phi, 0 }, { -phi, 0, 1 }, { -phi, 0, -1 }, { -1, -phi, 0 },
            { 0, -1, -phi }, { 1, -phi, 0 }, { phi, 0, -1 }, { 0, 1, -phi },
        }};
        for (uint32_t i = 0u; i < points.size(); ++i) {
            matrix.directions[i] = normalize(points[i]);
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
    std::array<std::array<MatrixSet, 3u>,
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
    std::array<float, kAmbiEffectDjFilterMaxPickups> targetMaskGain_ {
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    };
    std::array<float, kAmbiEffectDjFilterMaxPickups> currentMaskGain_ {
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    };
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
    float targetTopologyAmount_ = 0.65f;
    float currentTopologyAmount_ = 0.65f;
    float targetRoamingRateHz_ = 0.08f;
    float currentRoamingRateHz_ = 0.08f;
    float targetMix_ = 1.0f;
    float currentMix_ = 1.0f;
    float targetOutputGain_ = 1.0f;
    float currentOutputGain_ = 1.0f;

    float parameterCoefficient_ = 0.001f;
    float matrixCoefficient_ = 0.0007f;
    float topologyCoefficient_ = 0.0008f;
    float levelCoefficient_ = 0.0004f;
};

} // namespace s3g
