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

enum class WaveguideExciter : uint32_t {
    Off = 0u,
    Bow = 1u,
    Reed = 2u,
    AirJet = 3u,
};

struct FractionalWaveguideParams {
    float propagationSpeed = 343.0f;
    float decaySeconds = 2.5f;
    float absorption = 0.22f;
    float junctionNonlinearity = 0.0f;
    float radiation = 0.20f;
    float dispersion = 0.0f;
    float sustainedExcitation = 0.0f;
    float exciterCharacter = 0.5f;
    float outputGainDb = -12.0f;
    WaveguideExciter exciter = WaveguideExciter::Off;
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
    params.dispersion = clamp(
        std::isfinite(params.dispersion) ? params.dispersion : 0.0f,
        0.0f, 1.0f);
    params.sustainedExcitation = clamp(
        std::isfinite(params.sustainedExcitation)
            ? params.sustainedExcitation : 0.0f,
        0.0f, 1.0f);
    params.exciterCharacter = clamp(
        std::isfinite(params.exciterCharacter)
            ? params.exciterCharacter : 0.5f,
        0.0f, 1.0f);
    params.outputGainDb = clamp(
        std::isfinite(params.outputGainDb) ? params.outputGainDb : -12.0f,
        -60.0f, 12.0f);
    params.order = std::clamp<uint32_t>(params.order, 1u, 3u);
    params.exciter = static_cast<WaveguideExciter>(
        std::min<uint32_t>(static_cast<uint32_t>(params.exciter), 3u));
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
        morphIncrement_ = 1.0f / std::max(
            1.0f, static_cast<float>(sampleRate) * 0.018f);
        setDelaySamples(2.0f, false);
        reset();
    }

    void reset()
    {
        std::fill(data_.begin(), data_.end(), 0.0f);
        writeIndex_ = 0u;
        previousInput_ = 0.0f;
        previousOutput_ = 0.0f;
        morphPreviousInput_ = 0.0f;
        morphPreviousOutput_ = 0.0f;
        morphAmount_ = 0.0f;
        morphing_ = false;
        currentDelaySamples_ = delaySamples_;
        configureCurrentTap(currentDelaySamples_);
    }

    void setDelaySamples(float delaySamples, bool smooth = false)
    {
        if (data_.size() < 8u) return;
        const float maximum = static_cast<float>(data_.size() - 3u);
        delaySamples_ = clamp(
            std::isfinite(delaySamples) ? delaySamples : 2.0f,
            2.0f, maximum);
        if (!smooth) {
            currentDelaySamples_ = delaySamples_;
            configureCurrentTap(currentDelaySamples_);
            previousInput_ = 0.0f;
            previousOutput_ = 0.0f;
            morphing_ = false;
            morphAmount_ = 0.0f;
        } else if (!morphing_
            && std::abs(delaySamples_ - currentDelaySamples_) > 0.0001f) {
            beginMorph(delaySamples_);
        }
    }

    float read()
    {
        if (data_.empty()) return 0.0f;
        const float current = readTap(
            integerDelay_, fractionalDelay_, allpassCoefficient_,
            previousInput_, previousOutput_);
        if (!morphing_) return current;
        const float target = readTap(
            morphIntegerDelay_, morphFractionalDelay_,
            morphAllpassCoefficient_, morphPreviousInput_,
            morphPreviousOutput_);
        const float mix = morphAmount_ * morphAmount_
            * (3.0f - 2.0f * morphAmount_);
        const float output = lerp(current, target, mix);
        morphAmount_ = std::min(1.0f, morphAmount_ + morphIncrement_);
        if (morphAmount_ >= 1.0f) {
            currentDelaySamples_ = morphDelaySamples_;
            integerDelay_ = morphIntegerDelay_;
            fractionalDelay_ = morphFractionalDelay_;
            allpassCoefficient_ = morphAllpassCoefficient_;
            previousInput_ = morphPreviousInput_;
            previousOutput_ = morphPreviousOutput_;
            morphing_ = false;
            morphAmount_ = 0.0f;
            if (std::abs(delaySamples_ - currentDelaySamples_) > 0.0001f) {
                beginMorph(delaySamples_);
            }
        }
        return flushDenormal(std::isfinite(output) ? output : 0.0f);
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
    static void tapParameters(float delaySamples,
        uint32_t& integerDelay, float& fractionalDelay,
        float& allpassCoefficient)
    {
        integerDelay = static_cast<uint32_t>(std::floor(delaySamples));
        fractionalDelay = delaySamples - static_cast<float>(integerDelay);
        if (fractionalDelay < 0.000001f) {
            fractionalDelay = 0.0f;
            allpassCoefficient = 0.0f;
        } else {
            allpassCoefficient =
                (1.0f - fractionalDelay) / (1.0f + fractionalDelay);
        }
    }

    void configureCurrentTap(float delaySamples)
    {
        tapParameters(delaySamples, integerDelay_, fractionalDelay_,
            allpassCoefficient_);
    }

    void beginMorph(float targetDelaySamples)
    {
        morphDelaySamples_ = targetDelaySamples;
        tapParameters(morphDelaySamples_, morphIntegerDelay_,
            morphFractionalDelay_, morphAllpassCoefficient_);
        const uint32_t size = static_cast<uint32_t>(data_.size());
        const uint32_t readIndex =
            (writeIndex_ + size - morphIntegerDelay_) % size;
        const float delayed = data_[readIndex];
        morphPreviousInput_ = delayed;
        morphPreviousOutput_ = delayed;
        morphAmount_ = 0.0f;
        morphing_ = true;
    }

    float readTap(uint32_t integerDelay, float fractionalDelay,
        float allpassCoefficient, float& previousInput,
        float& previousOutput)
    {
        const uint32_t size = static_cast<uint32_t>(data_.size());
        const uint32_t readIndex =
            (writeIndex_ + size - integerDelay) % size;
        const float delayed = data_[readIndex];
        if (fractionalDelay == 0.0f) {
            previousInput = delayed;
            previousOutput = delayed;
            return delayed;
        }
        const float output = allpassCoefficient * delayed
            + previousInput - allpassCoefficient * previousOutput;
        previousInput = delayed;
        previousOutput = flushDenormal(
            std::isfinite(output) ? output : 0.0f);
        return previousOutput;
    }

    std::vector<float> data_;
    uint32_t writeIndex_ = 0u;
    uint32_t integerDelay_ = 2u;
    float delaySamples_ = 2.0f;
    float currentDelaySamples_ = 2.0f;
    float fractionalDelay_ = 0.0f;
    float allpassCoefficient_ = 0.0f;
    float previousInput_ = 0.0f;
    float previousOutput_ = 0.0f;
    uint32_t morphIntegerDelay_ = 2u;
    float morphDelaySamples_ = 2.0f;
    float morphFractionalDelay_ = 0.0f;
    float morphAllpassCoefficient_ = 0.0f;
    float morphPreviousInput_ = 0.0f;
    float morphPreviousOutput_ = 0.0f;
    float morphAmount_ = 0.0f;
    float morphIncrement_ = 1.0f;
    bool morphing_ = false;
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
        distanceGainSmoothing_ = 1.0f - std::exp(
            -1.0f / static_cast<float>(sampleRate_ * 0.018));
        directivityActivityRelease_ = std::exp(
            -1.0f / static_cast<float>(sampleRate_ * 0.220));
        exciterDcCoefficient_ = 1.0f - std::exp(
            -2.0f * kPi * 9.0f / static_cast<float>(sampleRate_));
        updateTriggeredExciterDecay();
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
        pendingDirectivityFocusMask_ = 0u;
        pressure_.fill(0.0f);
        nodeEnergy_.fill(0.0f);
        radiationLowpass_.fill(0.0f);
        exciterDc_.fill(0.0f);
        exciterNoise_.fill(0.0f);
        triggeredExciterLevel_.fill(0.0f);
        for (auto& node : nodes_) {
            node.directivityMaskActivity = 0.0f;
            node.directivityMaskGain = 1.0f;
            node.targetDirectivityMaskGain = 1.0f;
        }
        exciterRandomState_ = 0x7f4a7c15u;
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
        updateTriggeredExciterDecay();
        if (!prepared_) return;
        if (structuralRetune) {
            updatePathMaterial(true);
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
        updateDirectivityMaskTargets(!prepared_);
        geometryDirty_ = true;
        if (prepared_) commitGeometry();
    }

    // Cube size is a performance control. Preserve the travelling waves and
    // crossfade each delay path to its new length instead of clearing the
    // resonator on every slider event.
    void morphCube(float halfExtentMetres)
    {
        halfExtentMetres = clamp(
            std::isfinite(halfExtentMetres) ? halfExtentMetres : 0.5f,
            0.02f, 4.0f);
        if (nodeCount_ != 8u || edgeCount_ != 12u) {
            configureCube(halfExtentMetres);
            return;
        }
        for (uint32_t node = 0u; node < nodeCount_; ++node) {
            nodes_[node].position = {
                (node & 1u) != 0u ? halfExtentMetres : -halfExtentMetres,
                (node & 2u) != 0u ? halfExtentMetres : -halfExtentMetres,
                (node & 4u) != 0u ? halfExtentMetres : -halfExtentMetres,
            };
        }
        rebuildSpatialState(true);
        if (prepared_) updatePathMaterial(true);
        geometryDirty_ = false;
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
        updateDirectivityMaskTargets(!prepared_);
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

    void setNodeDirectivity(
        uint32_t node, float amount, bool immediate = false)
    {
        if (node >= nodeCount_) return;
        amount = clamp(
            std::isfinite(amount) ? amount : 0.0f, 0.0f, 1.0f);
        nodes_[node].targetDirectivity = amount;
        updateDirectivityMaskTargets(immediate || !prepared_);
    }

    float nodeDirectivity(uint32_t node) const
    {
        return node < nodeCount_
            ? nodes_[node].targetDirectivity : 0.0f;
    }

    float nodeDirectivityMaskTarget(uint32_t node) const
    {
        return node < nodeCount_
            ? nodes_[node].targetDirectivityMaskGain : 1.0f;
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

    // Arbitrary topology edits remain structural and clear travelling waves.
    // The fixed cube's performance-size control uses morphCube() instead.
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
        if (std::abs(amplitude) > 0.000001f) {
            pendingDirectivityFocusMask_ |= 1u << node;
        }
    }

    // Start a finite physical-exciter gesture at one node. This is separate
    // from sustainedExcitation so sequencers and node clicks can articulate
    // Bow, Reed, and Air Jet even when the continuous Sustain control is zero.
    void triggerExciter(uint32_t node, float amplitude)
    {
        if (node >= nodeCount_ || !std::isfinite(amplitude)) return;
        const float level = clamp(std::abs(amplitude), 0.0f, 1.0f);
        triggeredExciterLevel_[node] = clamp(
            triggeredExciterLevel_[node]
                + level * (1.0f - triggeredExciterLevel_[node]),
            0.0f, 1.0f);
        if (level > 0.000001f) {
            pendingDirectivityFocusMask_ |= 1u << node;
        }
    }

    void setTuningFrequency(float frequencyHz)
    {
        frequencyHz = std::isfinite(frequencyHz) ? frequencyHz : 0.0f;
        if (frequencyHz > 0.0f) {
            const float highest = std::max(18.0f, std::min(
                4000.0f, static_cast<float>(sampleRate_) / 12.0f));
            while (frequencyHz < 18.0f) frequencyHz *= 2.0f;
            while (frequencyHz > highest) frequencyHz *= 0.5f;
            frequencyHz = clamp(frequencyHz, 18.0f, highest);
        } else {
            frequencyHz = 0.0f;
        }
        if (std::abs(frequencyHz - tuningFrequencyHz_) <= 0.0001f) return;
        tuningFrequencyHz_ = frequencyHz;
        if (prepared_) updatePathMaterial(true);
    }

    void clearTuningFrequency() { setTuningFrequency(0.0f); }
    float tuningFrequency() const { return tuningFrequencyHz_; }

    void process(const float* actuator,
                 float** output,
                 uint32_t outputChannels,
                 uint32_t frames,
                 uint32_t actuatorNode = 0u,
                 const float* sustainedGate = nullptr,
                 const float* const* nodeActuators = nullptr,
                 const float* const* nodeSustainedGates = nullptr)
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
            const float gate = sustainedGate
                ? clamp(std::isfinite(sustainedGate[frame])
                    ? sustainedGate[frame] : 0.0f, 0.0f, 1.0f)
                : 1.0f;
            renderNetworkSample(actuatorSample, actuatorNode, gate,
                nodeActuators, nodeSustainedGates, frame);

            std::array<float, kFractionalWaveguideMaxChannels> encoded {};
            const float normalization =
                1.0f / std::sqrt(static_cast<float>(nodeCount_));
            for (uint32_t node = 0u; node < nodeCount_; ++node) {
                nodes_[node].distanceGain +=
                    (nodes_[node].targetDistanceGain
                        - nodes_[node].distanceGain)
                    * distanceGainSmoothing_;
                radiationLowpass_[node] +=
                    (pressure_[node] - radiationLowpass_[node])
                    * radiationCoefficient_;
                const float velocityRadiation =
                    pressure_[node] - radiationLowpass_[node];
                nodes_[node].directivityMaskGain +=
                    (nodes_[node].targetDirectivityMaskGain
                        - nodes_[node].directivityMaskGain)
                    * distanceGainSmoothing_;
                const float radiated = lerp(
                    pressure_[node], velocityRadiation, params_.radiation)
                    * nodes_[node].distanceGain
                    * nodes_[node].directivityMaskGain
                    * normalization * outputGain;
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
            materialSmoothing_ = 1.0f - std::exp(
                -1.0f / static_cast<float>(sampleRate_ * 0.018));
            reset();
        }

        void reset()
        {
            delay.reset();
            absorptionState = 0.0f;
            dispersionPreviousInput = 0.0f;
            dispersionPreviousOutput = 0.0f;
        }

        void configure(float delaySamples,
                       float lengthMetres,
                       const FractionalWaveguideParams& params,
                       bool smoothDelay = false)
        {
            if (std::abs(delay.delaySamples() - delaySamples) > 0.0001f) {
                delay.setDelaySamples(delaySamples, smoothDelay);
            }
            const float travelSeconds =
                delay.delaySamples() / static_cast<float>(sampleRate_);
            const float requestedTraversalGain = std::pow(
                0.001f, travelSeconds / params.decaySeconds);
            const float distanceAmount = clamp(
                lengthMetres * 0.25f, 0.0f, 1.0f);
            const float cutoff = clamp(
                20000.0f * std::pow(
                    0.04f, params.absorption * distanceAmount),
                350.0f,
                static_cast<float>(sampleRate_ * 0.45));
            const float requestedAbsorptionCoefficient = 1.0f - std::exp(
                -2.0f * kPi * cutoff / static_cast<float>(sampleRate_));
            const float requestedDispersionCoefficient =
                -0.72f * params.dispersion;
            targetTraversalGain = requestedTraversalGain;
            targetAbsorptionCoefficient = requestedAbsorptionCoefficient;
            targetDispersionCoefficient = requestedDispersionCoefficient;
            if (!smoothDelay) {
                traversalGain = targetTraversalGain;
                absorptionCoefficient = targetAbsorptionCoefficient;
                dispersionCoefficient = targetDispersionCoefficient;
            }
        }

        float read()
        {
            traversalGain +=
                (targetTraversalGain - traversalGain) * materialSmoothing_;
            absorptionCoefficient +=
                (targetAbsorptionCoefficient - absorptionCoefficient)
                * materialSmoothing_;
            dispersionCoefficient +=
                (targetDispersionCoefficient - dispersionCoefficient)
                * materialSmoothing_;
            const float value = delay.read();
            absorptionState +=
                (value - absorptionState) * absorptionCoefficient;
            float dispersed = absorptionState;
            if (std::abs(dispersionCoefficient) > 0.000001f) {
                dispersed = dispersionCoefficient * absorptionState
                    + dispersionPreviousInput
                    - dispersionCoefficient * dispersionPreviousOutput;
            }
            dispersionPreviousInput = absorptionState;
            dispersionPreviousOutput = flushDenormal(
                std::isfinite(dispersed) ? dispersed : 0.0f);
            return flushDenormal(
                dispersionPreviousOutput * traversalGain);
        }

        void write(float value)
        {
            delay.writeAndAdvance(value);
        }

        WaveguideFractionalDelay delay;
        double sampleRate_ = 48000.0;
        float traversalGain = 0.99f;
        float targetTraversalGain = 0.99f;
        float absorptionCoefficient = 1.0f;
        float targetAbsorptionCoefficient = 1.0f;
        float absorptionState = 0.0f;
        float dispersionCoefficient = 0.0f;
        float targetDispersionCoefficient = 0.0f;
        float materialSmoothing_ = 0.001f;
        float dispersionPreviousInput = 0.0f;
        float dispersionPreviousOutput = 0.0f;
    };

    struct Node {
        Vec3 position {};
        std::array<float, kFractionalWaveguideMaxChannels> basis {};
        float distanceGain = 1.0f;
        float targetDistanceGain = 1.0f;
        float targetDirectivity = 0.0f;
        float directivityMaskActivity = 0.0f;
        float directivityMaskGain = 1.0f;
        float targetDirectivityMaskGain = 1.0f;
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

    void updateDirectivityMaskTargets(bool immediate = false)
    {
        if (nodeCount_ == 0u) return;

        // Each strike supplies the center of an ambi-style directional mask;
        // the parameter stored on that node is its depth. Multiple nodes struck
        // at the same sample protect multiple centers, while the activity
        // envelope returns the field smoothly to an unmasked state.
        for (uint32_t radiationNode = 0u;
             radiationNode < nodeCount_; ++radiationNode) {
            float protection = 0.0f;
            float attenuation = 0.0f;
            for (uint32_t focusNode = 0u;
                 focusNode < nodeCount_; ++focusNode) {
                const float activity =
                    nodes_[focusNode].directivityMaskActivity;
                const float kernel =
                    directivityMaskKernel_[focusNode][radiationNode];
                protection = std::max(
                    protection, activity * kernel);
                attenuation = std::max(attenuation,
                    activity * nodes_[focusNode].targetDirectivity
                        * (1.0f - kernel));
            }
            nodes_[radiationNode].targetDirectivityMaskGain = clamp(
                std::max(protection, 1.0f - attenuation),
                0.0f, 1.0f);
            if (immediate) {
                nodes_[radiationNode].directivityMaskGain =
                    nodes_[radiationNode].targetDirectivityMaskGain;
            }
        }
    }

    void rebuildSpatialState(bool smoothDistance = false)
    {
        degree_.fill(0u);
        admittanceSum_.fill(0.0f);
        for (uint32_t node = 0u; node < nodeCount_; ++node) {
            const float distance = vectorLength(nodes_[node].position);
            nodes_[node].basis = acnSn3dBasis(nodes_[node].position);
            const float desiredDistanceGain =
                1.0f / std::max(0.5f, distance);
            nodes_[node].targetDistanceGain = desiredDistanceGain;
            if (!smoothDistance || !prepared_) {
                nodes_[node].distanceGain = desiredDistanceGain;
            }
        }
        constexpr float kDirectivityMaskExponent = 8.0f;
        for (uint32_t focusNode = 0u;
             focusNode < nodeCount_; ++focusNode) {
            const Vec3 focus = nodes_[focusNode].position;
            const float focusLength = vectorLength(focus);
            for (uint32_t radiationNode = 0u;
                 radiationNode < nodeCount_; ++radiationNode) {
                const Vec3 radiation = nodes_[radiationNode].position;
                const float denominator = focusLength
                    * vectorLength(radiation);
                const float dot = denominator > 0.000001f
                    ? (focus.x * radiation.x
                        + focus.y * radiation.y
                        + focus.z * radiation.z) / denominator
                    : (focusNode == radiationNode ? 1.0f : -1.0f);
                const float normalized = clamp(
                    (dot + 1.0f) * 0.5f, 0.0f, 1.0f);
                directivityMaskKernel_[focusNode][radiationNode] =
                    std::pow(normalized, kDirectivityMaskExponent);
            }
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

    void updatePathMaterial(bool smoothDelay = false)
    {
        for (uint32_t edgeIndex = 0u;
             edgeIndex < edgeCount_; ++edgeIndex) {
            Edge& edge = edges_[edgeIndex];
            const float physicalDelaySamples = edge.lengthMetres
                / params_.propagationSpeed
                * static_cast<float>(sampleRate_);
            float delaySamples = physicalDelaySamples;
            const float shortestMusicalDelay = 3.0f;
            const float longestMusicalDelay =
                static_cast<float>(sampleRate_) / (4.0f * 18.0f);
            while (delaySamples < shortestMusicalDelay) {
                delaySamples *= 2.0f;
            }
            while (delaySamples > longestMusicalDelay) {
                delaySamples *= 0.5f;
            }
            delaySamples = clamp(
                delaySamples, shortestMusicalDelay, longestMusicalDelay);
            if (tuningFrequencyHz_ > 0.0f) {
                delaySamples = static_cast<float>(sampleRate_)
                    / (4.0f * tuningFrequencyHz_);
                delaySamples = clamp(
                    delaySamples, shortestMusicalDelay, longestMusicalDelay);
            }
            edge.aToB.configure(
                delaySamples, edge.lengthMetres, params_, smoothDelay);
            edge.bToA.configure(
                delaySamples, edge.lengthMetres, params_, smoothDelay);
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

    float nextExciterNoise()
    {
        exciterRandomState_ ^= exciterRandomState_ << 13u;
        exciterRandomState_ ^= exciterRandomState_ >> 17u;
        exciterRandomState_ ^= exciterRandomState_ << 5u;
        return static_cast<float>(
            (exciterRandomState_ >> 8u) & 0x00ffffffu)
            / 8388607.5f - 1.0f;
    }

    void updateTriggeredExciterDecay()
    {
        float durationSeconds = 0.08f;
        if (params_.exciter == WaveguideExciter::Bow) {
            durationSeconds = lerp(
                0.16f, 1.20f, params_.exciterCharacter);
        } else if (params_.exciter == WaveguideExciter::Reed) {
            durationSeconds = lerp(
                0.07f, 0.42f, params_.exciterCharacter);
        } else if (params_.exciter == WaveguideExciter::AirJet) {
            durationSeconds = lerp(
                0.10f, 0.72f, params_.exciterCharacter);
        }
        // durationSeconds is the approximate -60 dB gesture length.
        triggeredExciterDecay_ = std::exp(
            -6.90775527898f
            / std::max(1.0f,
                static_cast<float>(sampleRate_) * durationSeconds));
    }

    float renderSustainedExciter(
        uint32_t node, float localWave, float sustainedGate)
    {
        const float triggeredLevel = triggeredExciterLevel_[node];
        triggeredExciterLevel_[node] = flushDenormal(
            triggeredLevel * triggeredExciterDecay_);
        const float level = clamp(
            params_.sustainedExcitation * sustainedGate + triggeredLevel,
            0.0f, 1.25f);
        if (params_.exciter == WaveguideExciter::Off
            || level <= 0.000001f) {
            exciterDc_[node] *= 0.9995f;
            exciterNoise_[node] *= 0.9995f;
            return 0.0f;
        }

        const float character = params_.exciterCharacter;
        float raw = 0.0f;
        if (params_.exciter == WaveguideExciter::Bow) {
            const float bowVelocity = 0.035f + 0.22f * character;
            const float relativeVelocity = bowVelocity - localWave;
            const float frictionArgument = 0.75f
                + std::abs(relativeVelocity) * (2.0f + 14.0f * character);
            const float inverseSquare =
                1.0f / (frictionArgument * frictionArgument);
            const float friction = std::min(
                1.0f, inverseSquare * inverseSquare);
            raw = relativeVelocity * friction * level * 0.85f;
        } else if (params_.exciter == WaveguideExciter::Reed) {
            const float mouthPressure = 0.18f + level * 0.58f;
            const float pressureDifference = mouthPressure
                - localWave * (0.72f + 0.46f * character);
            const float reedOpening = clamp(
                1.0f - std::max(0.0f, pressureDifference)
                    * (1.15f + 1.70f * character),
                0.0f, 1.0f);
            const float flow = std::copysign(
                std::sqrt(std::abs(pressureDifference)),
                pressureDifference) * reedOpening;
            const float breathNoise = nextExciterNoise();
            exciterNoise_[node] +=
                (breathNoise - exciterNoise_[node])
                * (0.06f + 0.20f * character);
            raw = flow * level * 0.16f
                + exciterNoise_[node] * level
                    * (0.008f + 0.014f * character);
        } else {
            const float noise = nextExciterNoise();
            const float noiseCoefficient = 0.018f + 0.38f * character;
            exciterNoise_[node] +=
                (noise - exciterNoise_[node]) * noiseCoefficient;
            const float jet = 0.16f
                + exciterNoise_[node] * (0.08f + 0.28f * character)
                - localWave * (1.10f + 1.30f * character);
            raw = softSat(jet * (2.5f + 3.0f * character))
                * level * 0.18f;
        }

        exciterDc_[node] +=
            (raw - exciterDc_[node]) * exciterDcCoefficient_;
        return clamp(raw - exciterDc_[node], -0.75f, 0.75f);
    }

    void renderNetworkSample(
        float actuator, uint32_t actuatorNode, float sustainedGate,
        const float* const* nodeActuators,
        const float* const* nodeSustainedGates,
        uint32_t frame)
    {
        if (pendingDirectivityFocusMask_ != 0u) {
            for (uint32_t node = 0u; node < nodeCount_; ++node) {
                nodes_[node].directivityMaskActivity =
                    (pendingDirectivityFocusMask_ & (1u << node)) != 0u
                    ? 1.0f : 0.0f;
            }
            pendingDirectivityFocusMask_ = 0u;
        }
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
        nodes_[actuatorNode].directivityMaskActivity = std::max(
            nodes_[actuatorNode].directivityMaskActivity,
            clamp(std::abs(actuator) * 8.0f, 0.0f, 1.0f));
        if (nodeActuators) {
            for (uint32_t node = 0u; node < nodeCount_; ++node) {
                const float sample = nodeActuators[node]
                    && std::isfinite(nodeActuators[node][frame])
                    ? nodeActuators[node][frame] : 0.0f;
                pendingExcitation_[node] = clamp(
                    pendingExcitation_[node] + sample, -4.0f, 4.0f);
                nodes_[node].directivityMaskActivity = std::max(
                    nodes_[node].directivityMaskActivity,
                    clamp(std::abs(sample) * 8.0f, 0.0f, 1.0f));
            }
        }
        for (uint32_t node = 0u; node < nodeCount_; ++node) {
            float junction = admittanceSum_[node] > 0.000001f
                ? 2.0f * weightedIncoming[node] / admittanceSum_[node]
                : 0.0f;
            const float driveNormalization = degree_[node] > 0u
                ? 1.0f / std::sqrt(static_cast<float>(degree_[node]))
                : 1.0f;
            float nodeGate = 0.0f;
            if (nodeSustainedGates) {
                nodeGate = nodeSustainedGates[node]
                    && std::isfinite(nodeSustainedGates[node][frame])
                    ? clamp(nodeSustainedGates[node][frame], 0.0f, 1.0f)
                    : 0.0f;
            } else if (node == actuatorNode) {
                nodeGate = sustainedGate;
            }
            if (params_.exciter != WaveguideExciter::Off) {
                const float exciterActivity = clamp(
                    params_.sustainedExcitation * nodeGate
                        + triggeredExciterLevel_[node],
                    0.0f, 1.0f);
                nodes_[node].directivityMaskActivity = std::max(
                    nodes_[node].directivityMaskActivity,
                    exciterActivity);
            }
            // Render every node so finite sequenced exciter gestures can live
            // at their own spatial positions without opening a global gate.
            junction += renderSustainedExciter(
                node, junction, nodeGate) * driveNormalization;
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
        updateDirectivityMaskTargets();
        for (uint32_t node = 0u; node < nodeCount_; ++node) {
            nodes_[node].directivityMaskActivity = flushDenormal(
                nodes_[node].directivityMaskActivity
                    * directivityActivityRelease_);
        }
    }

    FractionalWaveguideParams params_ {};
    std::array<Node, kFractionalWaveguideMaxNodes> nodes_ {};
    std::array<Edge, kFractionalWaveguideMaxEdges> edges_ {};
    std::array<uint32_t, kFractionalWaveguideMaxNodes> degree_ {};
    std::array<float, kFractionalWaveguideMaxNodes> admittanceSum_ {};
    std::array<float, kFractionalWaveguideMaxNodes> pendingExcitation_ {};
    std::array<std::array<float, kFractionalWaveguideMaxNodes>,
        kFractionalWaveguideMaxNodes> directivityMaskKernel_ {};
    std::array<float, kFractionalWaveguideMaxNodes> pressure_ {};
    std::array<float, kFractionalWaveguideMaxNodes> nodeEnergy_ {};
    std::array<float, kFractionalWaveguideMaxNodes> radiationLowpass_ {};
    std::array<float, kFractionalWaveguideMaxNodes> exciterDc_ {};
    std::array<float, kFractionalWaveguideMaxNodes> exciterNoise_ {};
    std::array<float, kFractionalWaveguideMaxNodes>
        triggeredExciterLevel_ {};
    std::array<float, kFractionalWaveguideMaxEdges> incomingAtA_ {};
    std::array<float, kFractionalWaveguideMaxEdges> incomingAtB_ {};
    uint32_t nodeCount_ = 0u;
    uint32_t edgeCount_ = 0u;
    double sampleRate_ = 48000.0;
    float maximumDelaySeconds_ = 1.0f;
    float tuningFrequencyHz_ = 0.0f;
    float radiationCoefficient_ = 0.1f;
    float meterAttack_ = 0.01f;
    float meterRelease_ = 0.001f;
    float guardRelease_ = 0.0001f;
    float distanceGainSmoothing_ = 0.001f;
    float directivityActivityRelease_ = 0.9999f;
    float exciterDcCoefficient_ = 0.001f;
    float triggeredExciterDecay_ = 0.999f;
    uint32_t exciterRandomState_ = 0x7f4a7c15u;
    float guardGain_ = 1.0f;
    float outputPeak_ = 0.0f;
    float travelingEnergy_ = 0.0f;
    uint32_t pendingDirectivityFocusMask_ = 0u;
    bool prepared_ = false;
    bool geometryDirty_ = true;
};

} // namespace s3g
