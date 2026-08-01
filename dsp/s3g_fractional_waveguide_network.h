#pragma once

#include "s3g_ambisonic_geometry.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace s3g {

constexpr uint32_t kFractionalWaveguideMaxNodes = 8u;
constexpr uint32_t kFractionalWaveguideMaxEdges =
    kFractionalWaveguideMaxNodes * (kFractionalWaveguideMaxNodes - 1u) / 2u;
constexpr uint32_t kFractionalWaveguideMaxChannels = k3OaChannels;

struct FractionalWaveguideParams {
    float propagationSpeed = 343.0f;
    float decaySeconds = 2.5f;
    float absorption = 0.22f;
    float junctionNonlinearity = 0.0f;
    float radiation = 0.20f;
    float outputGainDb = -12.0f;
    uint32_t order = 3u;
};

inline FractionalWaveguideParams sanitizeFractionalWaveguideParams(
    FractionalWaveguideParams params)
{
    params.propagationSpeed = clamp(
        std::isfinite(params.propagationSpeed) ? params.propagationSpeed : 343.0f,
        20.0f, 2000.0f);
    params.decaySeconds = clamp(
        std::isfinite(params.decaySeconds) ? params.decaySeconds : 2.5f,
        0.05f, 60.0f);
    params.absorption = clamp(
        std::isfinite(params.absorption) ? params.absorption : 0.22f,
        0.0f, 1.0f);
    params.junctionNonlinearity = clamp(
        std::isfinite(params.junctionNonlinearity)
            ? params.junctionNonlinearity : 0.0f,
        0.0f, 1.0f);
    params.radiation = clamp(
        std::isfinite(params.radiation) ? params.radiation : 0.20f,
        0.0f, 1.0f);
    params.outputGainDb = clamp(
        std::isfinite(params.outputGainDb) ? params.outputGainDb : -12.0f,
        -60.0f, 12.0f);
    params.order = std::clamp<uint32_t>(params.order, 1u, 3u);
    return params;
}

// A static fractional-delay path intended for use inside a feedback network.
// The integer delay is held in a circular buffer and the fractional remainder
// is supplied by a first-order Thiran allpass. Unlike polynomial
// interpolation, the allpass has unity magnitude and cannot amplify a
// stationary waveguide loop merely because its length is non-integral.
class WaveguideFractionalDelay {
public:
    void prepare(double sampleRate, float maximumSeconds)
    {
        sampleRate = std::max(1.0, sampleRate);
        maximumSeconds = std::max(0.001f,
            std::isfinite(maximumSeconds) ? maximumSeconds : 1.0f);
        const uint32_t frames = std::max<uint32_t>(8u,
            static_cast<uint32_t>(std::ceil(sampleRate * maximumSeconds)) + 4u);
        data_.assign(frames, 0.0f);
        setDelaySamples(2.0f);
        reset();
    }

    void reset()
    {
        std::fill(data_.begin(), data_.end(), 0.0f);
        writeIndex_ = 0u;
        previousInput_ = 0.0f;
        previousOutput_ = 0.0f;
    }

    void setDelaySamples(float delaySamples)
    {
        if (data_.size() < 8u) return;
        const float maximum = static_cast<float>(data_.size() - 3u);
        delaySamples_ = clamp(
            std::isfinite(delaySamples) ? delaySamples : 2.0f,
            2.0f, maximum);
        integerDelay_ = static_cast<uint32_t>(std::floor(delaySamples_));
        fractionalDelay_ = delaySamples_ - static_cast<float>(integerDelay_);
        if (fractionalDelay_ < 0.000001f) {
            fractionalDelay_ = 0.0f;
            allpassCoefficient_ = 0.0f;
        } else {
            allpassCoefficient_ =
                (1.0f - fractionalDelay_) / (1.0f + fractionalDelay_);
        }
    }

    float read()
    {
        if (data_.empty()) return 0.0f;
        const uint32_t size = static_cast<uint32_t>(data_.size());
        const uint32_t readIndex =
            (writeIndex_ + size - integerDelay_) % size;
        const float delayed = data_[readIndex];
        if (fractionalDelay_ == 0.0f) {
            previousInput_ = delayed;
            previousOutput_ = delayed;
            return delayed;
        }
        const float output = allpassCoefficient_ * delayed
            + previousInput_
            - allpassCoefficient_ * previousOutput_;
        previousInput_ = delayed;
        previousOutput_ = flushDenormal(
            std::isfinite(output) ? output : 0.0f);
        return previousOutput_;
    }

    void writeAndAdvance(float value)
    {
        if (data_.empty()) return;
        data_[writeIndex_] = flushDenormal(
            std::isfinite(value) ? value : 0.0f);
        writeIndex_ = (writeIndex_ + 1u)
            % static_cast<uint32_t>(data_.size());
    }

    float delaySamples() const { return delaySamples_; }
    float fractionalDelay() const { return fractionalDelay_; }
    float allpassCoefficient() const { return allpassCoefficient_; }

private:
    std::vector<float> data_;
    uint32_t writeIndex_ = 0u;
    uint32_t integerDelay_ = 2u;
    float delaySamples_ = 2.0f;
    float fractionalDelay_ = 0.0f;
    float allpassCoefficient_ = 0.0f;
    float previousInput_ = 0.0f;
    float previousOutput_ = 0.0f;
};

class FractionalWaveguideNetwork {
public:
    FractionalWaveguideNetwork()
    {
        configureCube(0.5f);
    }

    void prepare(double sampleRate, float maximumDelaySeconds = 1.0f)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        maximumDelaySeconds_ = std::max(0.01f,
            std::isfinite(maximumDelaySeconds) ? maximumDelaySeconds : 1.0f);
        for (auto& edge : edges_) {
            edge.aToB.prepare(sampleRate_, maximumDelaySeconds_);
            edge.bToA.prepare(sampleRate_, maximumDelaySeconds_);
        }
        radiationCoefficient_ = 1.0f - std::exp(
            -2.0f * kPi * 1200.0f / static_cast<float>(sampleRate_));
        meterAttack_ = 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate_ * 0.006));
        meterRelease_ = 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate_ * 0.280));
        guardRelease_ = 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate_ * 0.300));
        prepared_ = true;
        commitGeometry();
    }

    void reset()
    {
        for (auto& edge : edges_) {
            edge.aToB.reset();
            edge.bToA.reset();
            edge.energy = 0.0f;
        }
        pendingExcitation_.fill(0.0f);
        pressure_.fill(0.0f);
        nodeEnergy_.fill(0.0f);
        radiationLowpass_.fill(0.0f);
        incomingAtA_.fill(0.0f);
        incomingAtB_.fill(0.0f);
        guardGain_ = 1.0f;
        outputPeak_ = 0.0f;
        travelingEnergy_ = 0.0f;
    }

    void setParams(FractionalWaveguideParams params)
    {
        const FractionalWaveguideParams sanitized =
            sanitizeFractionalWaveguideParams(params);
        const bool structuralRetune = std::abs(
            sanitized.propagationSpeed - params_.propagationSpeed) > 0.0001f;
        params_ = sanitized;
        if (!prepared_) return;
        if (structuralRetune) {
            commitGeometry();
        } else {
            updatePathMaterial();
        }
    }

    const FractionalWaveguideParams& params() const { return params_; }

    void configureCube(float halfExtentMetres)
    {
        halfExtentMetres = clamp(
            std::isfinite(halfExtentMetres) ? halfExtentMetres : 0.5f,
            0.02f, 4.0f);
        nodeCount_ = 8u;
        for (uint32_t node = 0u; node < nodeCount_; ++node) {
            nodes_[node].position = {
                (node & 1u) != 0u ? halfExtentMetres : -halfExtentMetres,
                (node & 2u) != 0u ? halfExtentMetres : -halfExtentMetres,
                (node & 4u) != 0u ? halfExtentMetres : -halfExtentMetres,
            };
        }
        edgeCount_ = 0u;
        for (uint32_t first = 0u; first < nodeCount_; ++first) {
            for (uint32_t second = first + 1u; second < nodeCount_; ++second) {
                const uint32_t difference = first ^ second;
                if (difference == 1u || difference == 2u || difference == 4u) {
                    addEdge(first, second, 1.0f);
                }
            }
        }
        geometryDirty_ = true;
        if (prepared_) commitGeometry();
    }

    void setNodeCount(uint32_t count)
    {
        nodeCount_ = std::clamp<uint32_t>(
            count, 1u, kFractionalWaveguideMaxNodes);
        uint32_t retained = 0u;
        for (uint32_t edge = 0u; edge < edgeCount_; ++edge) {
            if (edges_[edge].first >= nodeCount_
                || edges_[edge].second >= nodeCount_) {
                continue;
            }
            if (retained != edge) {
                std::swap(edges_[retained], edges_[edge]);
            }
            ++retained;
        }
        edgeCount_ = retained;
        geometryDirty_ = true;
    }

    bool setNodePosition(uint32_t node, Vec3 position)
    {
        if (node >= nodeCount_) return false;
        if (!std::isfinite(position.x)
            || !std::isfinite(position.y)
            || !std::isfinite(position.z)) {
            return false;
        }
        nodes_[node].position = {
            clamp(position.x, -4.0f, 4.0f),
            clamp(position.y, -4.0f, 4.0f),
            clamp(position.z, -4.0f, 4.0f),
        };
        geometryDirty_ = true;
        return true;
    }

    Vec3 nodePosition(uint32_t node) const
    {
        return node < nodeCount_ ? nodes_[node].position : Vec3 {};
    }

    void clearEdges()
    {
        edgeCount_ = 0u;
        geometryDirty_ = true;
    }

    bool addEdge(uint32_t first, uint32_t second, float admittance = 1.0f)
    {
        if (first >= nodeCount_ || second >= nodeCount_ || first == second
            || edgeCount_ >= kFractionalWaveguideMaxEdges) {
            return false;
        }
        for (uint32_t edge = 0u; edge < edgeCount_; ++edge) {
            if ((edges_[edge].first == first && edges_[edge].second == second)
                || (edges_[edge].first == second
                    && edges_[edge].second == first)) {
                return false;
            }
        }
        Edge& edge = edges_[edgeCount_++];
        edge.first = static_cast<uint8_t>(first);
        edge.second = static_cast<uint8_t>(second);
        edge.admittance = clamp(
            std::isfinite(admittance) ? admittance : 1.0f,
            0.05f, 20.0f);
        edge.energy = 0.0f;
        geometryDirty_ = true;
        return true;
    }

    // Geometry changes are deliberately committed outside process(). The
    // first milestone treats node movement as a structural edit and clears
    // travelling waves; a later dual-head mode can morph active lengths.
    void commitGeometry()
    {
        rebuildSpatialState();
        if (prepared_) {
            updatePathMaterial();
            reset();
        }
        geometryDirty_ = false;
    }

    void strike(uint32_t node, float amplitude)
    {
        if (node >= nodeCount_ || !std::isfinite(amplitude)) return;
        pendingExcitation_[node] = clamp(
            pendingExcitation_[node] + amplitude, -4.0f, 4.0f);
    }

    void process(const float* actuator,
                 float** output,
                 uint32_t outputChannels,
                 uint32_t frames,
                 uint32_t actuatorNode = 0u)
    {
        if (!output || frames == 0u) return;
        for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
            if (output[channel]) {
                std::fill(output[channel], output[channel] + frames, 0.0f);
            }
        }
        if (!prepared_ || geometryDirty_ || nodeCount_ == 0u) return;
        actuatorNode = std::min<uint32_t>(
            actuatorNode, nodeCount_ - 1u);
        const uint32_t activeChannels = std::min<uint32_t>(
            outputChannels, (params_.order + 1u) * (params_.order + 1u));
        const float outputGain = dbToGain(params_.outputGainDb);

        for (uint32_t frame = 0u; frame < frames; ++frame) {
            const float actuatorSample = actuator
                ? (std::isfinite(actuator[frame]) ? actuator[frame] : 0.0f)
                : 0.0f;
            renderNetworkSample(actuatorSample, actuatorNode);

            std::array<float, kFractionalWaveguideMaxChannels> encoded {};
            const float normalization =
                1.0f / std::sqrt(static_cast<float>(nodeCount_));
            for (uint32_t node = 0u; node < nodeCount_; ++node) {
                radiationLowpass_[node] +=
                    (pressure_[node] - radiationLowpass_[node])
                    * radiationCoefficient_;
                const float velocityRadiation =
                    pressure_[node] - radiationLowpass_[node];
                const float radiated = lerp(
                    pressure_[node], velocityRadiation, params_.radiation)
                    * nodes_[node].distanceGain * normalization * outputGain;
                for (uint32_t channel = 0u;
                     channel < activeChannels; ++channel) {
                    encoded[channel] +=
                        radiated * nodes_[node].basis[channel];
                }
            }

            float peak = 0.0f;
            for (uint32_t channel = 0u; channel < activeChannels; ++channel) {
                if (!std::isfinite(encoded[channel])) encoded[channel] = 0.0f;
                peak = std::max(peak, std::abs(encoded[channel]));
            }
            constexpr float kGuardCeiling = 0.891250938f; // -1 dBFS
            const float requested = peak > kGuardCeiling
                ? kGuardCeiling / peak : 1.0f;
            if (requested < guardGain_) {
                guardGain_ = requested;
            } else {
                guardGain_ += (requested - guardGain_) * guardRelease_;
            }
            const float guardedPeak = peak * guardGain_;
            outputPeak_ = std::max(
                outputPeak_ * 0.9995f, guardedPeak);
            for (uint32_t channel = 0u; channel < activeChannels; ++channel) {
                const float sample = encoded[channel] * guardGain_;
                if (output[channel]) output[channel][frame] = sample;
            }
        }
    }

    uint32_t nodeCount() const { return nodeCount_; }
    uint32_t edgeCount() const { return edgeCount_; }
    float nodePressure(uint32_t node) const
    {
        return node < nodeCount_ ? pressure_[node] : 0.0f;
    }
    float nodeEnergy(uint32_t node) const
    {
        return node < nodeCount_ ? nodeEnergy_[node] : 0.0f;
    }
    float edgeEnergy(uint32_t edge) const
    {
        return edge < edgeCount_ ? edges_[edge].energy : 0.0f;
    }
    float edgeDelaySamples(uint32_t edge) const
    {
        return edge < edgeCount_
            ? edges_[edge].aToB.delay.delaySamples() : 0.0f;
    }
    float outputPeak() const { return outputPeak_; }
    float guardGain() const { return guardGain_; }
    float travelingEnergy() const { return travelingEnergy_; }

private:
    struct DirectedPath {
        void prepare(double sampleRate, float maximumSeconds)
        {
            sampleRate_ = std::max(1.0, sampleRate);
            delay.prepare(sampleRate_, maximumSeconds);
            reset();
        }

        void reset()
        {
            delay.reset();
            absorptionState = 0.0f;
        }

        void configure(float delaySamples,
                       float lengthMetres,
                       const FractionalWaveguideParams& params)
        {
            delay.setDelaySamples(delaySamples);
            const float travelSeconds =
                delay.delaySamples() / static_cast<float>(sampleRate_);
            traversalGain = std::pow(
                0.001f, travelSeconds / params.decaySeconds);
            const float distanceAmount = clamp(
                lengthMetres * 0.25f, 0.0f, 1.0f);
            const float cutoff = clamp(
                20000.0f * std::pow(
                    0.04f, params.absorption * distanceAmount),
                350.0f,
                static_cast<float>(sampleRate_ * 0.45));
            absorptionCoefficient = 1.0f - std::exp(
                -2.0f * kPi * cutoff / static_cast<float>(sampleRate_));
        }

        float read()
        {
            const float value = delay.read();
            absorptionState +=
                (value - absorptionState) * absorptionCoefficient;
            return flushDenormal(absorptionState * traversalGain);
        }

        void write(float value)
        {
            delay.writeAndAdvance(value);
        }

        WaveguideFractionalDelay delay;
        double sampleRate_ = 48000.0;
        float traversalGain = 0.99f;
        float absorptionCoefficient = 1.0f;
        float absorptionState = 0.0f;
    };

    struct Node {
        Vec3 position {};
        std::array<float, kFractionalWaveguideMaxChannels> basis {};
        float distanceGain = 1.0f;
    };

    struct Edge {
        DirectedPath aToB;
        DirectedPath bToA;
        uint8_t first = 0u;
        uint8_t second = 0u;
        float admittance = 1.0f;
        float lengthMetres = 1.0f;
        float energy = 0.0f;
    };

    static float vectorLength(Vec3 value)
    {
        return std::sqrt(
            value.x * value.x + value.y * value.y + value.z * value.z);
    }

    void rebuildSpatialState()
    {
        degree_.fill(0u);
        admittanceSum_.fill(0.0f);
        for (uint32_t node = 0u; node < nodeCount_; ++node) {
            const float distance = vectorLength(nodes_[node].position);
            nodes_[node].basis = acnSn3dBasis(nodes_[node].position);
            nodes_[node].distanceGain =
                1.0f / std::max(0.5f, distance);
        }
        for (uint32_t edgeIndex = 0u;
             edgeIndex < edgeCount_; ++edgeIndex) {
            Edge& edge = edges_[edgeIndex];
            if (edge.first >= nodeCount_ || edge.second >= nodeCount_) continue;
            const Vec3 first = nodes_[edge.first].position;
            const Vec3 second = nodes_[edge.second].position;
            edge.lengthMetres = std::max(0.001f, vectorLength({
                second.x - first.x,
                second.y - first.y,
                second.z - first.z,
            }));
            ++degree_[edge.first];
            ++degree_[edge.second];
            admittanceSum_[edge.first] += edge.admittance;
            admittanceSum_[edge.second] += edge.admittance;
        }
    }

    void updatePathMaterial()
    {
        for (uint32_t edgeIndex = 0u;
             edgeIndex < edgeCount_; ++edgeIndex) {
            Edge& edge = edges_[edgeIndex];
            const float delaySamples = edge.lengthMetres
                / params_.propagationSpeed
                * static_cast<float>(sampleRate_);
            edge.aToB.configure(
                delaySamples, edge.lengthMetres, params_);
            edge.bToA.configure(
                delaySamples, edge.lengthMetres, params_);
        }
    }

    float shapeJunction(float value) const
    {
        if (params_.junctionNonlinearity <= 0.000001f) return value;
        const float drive =
            1.0f + params_.junctionNonlinearity * 8.0f;
        const float bounded = softSat(value * drive) / drive;
        return lerp(
            value, bounded, params_.junctionNonlinearity);
    }

    void renderNetworkSample(float actuator, uint32_t actuatorNode)
    {
        std::array<float, kFractionalWaveguideMaxNodes> weightedIncoming {};
        travelingEnergy_ = 0.0f;
        for (uint32_t edgeIndex = 0u;
             edgeIndex < edgeCount_; ++edgeIndex) {
            Edge& edge = edges_[edgeIndex];
            incomingAtA_[edgeIndex] = edge.bToA.read();
            incomingAtB_[edgeIndex] = edge.aToB.read();
            const float atFirst = incomingAtA_[edgeIndex];
            const float atSecond = incomingAtB_[edgeIndex];
            weightedIncoming[edge.first] +=
                edge.admittance * atFirst;
            weightedIncoming[edge.second] +=
                edge.admittance * atSecond;
            travelingEnergy_ +=
                atFirst * atFirst + atSecond * atSecond;
            const float target = std::max(
                std::abs(atFirst), std::abs(atSecond));
            const float coefficient =
                target > edge.energy ? meterAttack_ : meterRelease_;
            edge.energy += (target - edge.energy) * coefficient;
        }

        pendingExcitation_[actuatorNode] = clamp(
            pendingExcitation_[actuatorNode] + actuator,
            -4.0f, 4.0f);
        for (uint32_t node = 0u; node < nodeCount_; ++node) {
            float junction = admittanceSum_[node] > 0.000001f
                ? 2.0f * weightedIncoming[node] / admittanceSum_[node]
                : 0.0f;
            const float driveNormalization = degree_[node] > 0u
                ? 1.0f / std::sqrt(static_cast<float>(degree_[node]))
                : 1.0f;
            junction += pendingExcitation_[node] * driveNormalization;
            pendingExcitation_[node] = 0.0f;
            pressure_[node] = flushDenormal(
                std::isfinite(junction)
                    ? shapeJunction(junction) : 0.0f);
            const float target = std::abs(pressure_[node]);
            const float coefficient =
                target > nodeEnergy_[node] ? meterAttack_ : meterRelease_;
            nodeEnergy_[node] +=
                (target - nodeEnergy_[node]) * coefficient;
        }

        for (uint32_t edgeIndex = 0u;
             edgeIndex < edgeCount_; ++edgeIndex) {
            Edge& edge = edges_[edgeIndex];
            edge.aToB.write(
                pressure_[edge.first] - incomingAtA_[edgeIndex]);
            edge.bToA.write(
                pressure_[edge.second] - incomingAtB_[edgeIndex]);
        }
    }

    FractionalWaveguideParams params_ {};
    std::array<Node, kFractionalWaveguideMaxNodes> nodes_ {};
    std::array<Edge, kFractionalWaveguideMaxEdges> edges_ {};
    std::array<uint32_t, kFractionalWaveguideMaxNodes> degree_ {};
    std::array<float, kFractionalWaveguideMaxNodes> admittanceSum_ {};
    std::array<float, kFractionalWaveguideMaxNodes> pendingExcitation_ {};
    std::array<float, kFractionalWaveguideMaxNodes> pressure_ {};
    std::array<float, kFractionalWaveguideMaxNodes> nodeEnergy_ {};
    std::array<float, kFractionalWaveguideMaxNodes> radiationLowpass_ {};
    std::array<float, kFractionalWaveguideMaxEdges> incomingAtA_ {};
    std::array<float, kFractionalWaveguideMaxEdges> incomingAtB_ {};
    uint32_t nodeCount_ = 0u;
    uint32_t edgeCount_ = 0u;
    double sampleRate_ = 48000.0;
    float maximumDelaySeconds_ = 1.0f;
    float radiationCoefficient_ = 0.1f;
    float meterAttack_ = 0.01f;
    float meterRelease_ = 0.001f;
    float guardRelease_ = 0.0001f;
    float guardGain_ = 1.0f;
    float outputPeak_ = 0.0f;
    float travelingEnergy_ = 0.0f;
    bool prepared_ = false;
    bool geometryDirty_ = true;
};

} // namespace s3g
