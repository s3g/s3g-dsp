#pragma once

#include "s3g_ambi_encoder_depth.h"
#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace s3g {

constexpr uint32_t kAmbiNeuralEcologyLobes = 4u;
constexpr uint32_t kAmbiNeuralEcologyClustersPerLobe = 4u;
constexpr uint32_t kAmbiNeuralEcologyNodesPerCluster = 4u;
constexpr uint32_t kAmbiNeuralEcologyMaxNodes = 64u;
constexpr uint32_t kAmbiNeuralEcologyPickups = 8u;
constexpr uint32_t kAmbiNeuralEcologyMaxOrder = 7u;
constexpr uint32_t kAmbiNeuralEcologyMaxChannels = 64u;
constexpr uint32_t kAmbiNeuralEcologyGenomeValues = 133u;

enum class AmbiNeuralNodeSet : uint32_t {
    Ring4 = 0u,
    Dual8 = 1u,
    Cell16 = 2u,
    Pair32 = 3u,
    Field64 = 4u,
};

enum class AmbiNeuralPlasticityMode : uint32_t {
    Reinforce = 0u,
    Inhibit = 1u,
    Balance = 2u,
    Prune = 3u,
};

enum class AmbiNeuralListeningMode : uint32_t {
    Local = 0u,
    Cross = 1u,
    Diffuse = 2u,
    Roaming = 3u,
};

enum class AmbiNeuralPickupSet : uint32_t {
    Tetra4 = 0u,
    Cube8 = 1u,
};

enum class AmbiNeuralScoreMode : uint32_t {
    Off = 0u,
    Field = 1u,
    Midi = 2u,
    Coupled = 3u,
};

enum class AmbiNeuralScorePlanes : uint32_t {
    One = 0u,
    Two = 1u,
    Four = 2u,
    Eight = 3u,
};

inline uint32_t ambiNeuralScorePlaneCount(AmbiNeuralScorePlanes planes)
{
    return 1u << std::min<uint32_t>(static_cast<uint32_t>(planes), 3u);
}

struct AmbiNeuralEcologyParams {
    uint32_t order = 3u;
    AmbiNeuralNodeSet nodeSet = AmbiNeuralNodeSet::Cell16;

    float activity = 0.52f;
    float drive = 1.95f;
    float ringFeedback = 0.80f;
    float matrixCoupling = 0.42f;
    float hierarchy = 0.54f;
    float phaseShift = 0.30f;
    float registerSemitones = 0.0f;
    float timeSpread = 1.0f;
    float diversity = 0.20f;
    float brownian = 0.10f;
    float drift = 0.12f;
    float selfModulation = 0.34f;

    float fieldReturn = 0.34f;
    float propagationMs = 24.0f;
    float pickupFocus = 0.72f;
    float pickupAdapt = 0.24f;
    float pickupAnchor = 0.35f;
    AmbiNeuralPickupSet pickupSet = AmbiNeuralPickupSet::Tetra4;
    AmbiNeuralListeningMode listeningMode = AmbiNeuralListeningMode::Local;
    float auditoryPlasticity = 0.10f;
    float metabolism = 0.32f;
    float adaptation = 0.18f;
    float genomeMorph = 0.0f;
    float heredity = 0.0f;
    float mutationDepth = 0.35f;
    float plasticity = 0.0f;
    AmbiNeuralPlasticityMode plasticityMode = AmbiNeuralPlasticityMode::Balance;
    uint32_t freeze = 0u;
    uint32_t mutate = 0u;
    AmbiNeuralScoreMode scoreMode = AmbiNeuralScoreMode::Off;
    AmbiNeuralScorePlanes scorePlanes = AmbiNeuralScorePlanes::One;
    float scoreAmount = 0.72f;
    float scoreDwellSeconds = 8.0f;
    float scoreTransitionSeconds = 3.0f;
    float scoreVariation = 0.38f;
    float scoreRecombine = 0.62f;
    float scoreMemory = 0.45f;

    float centerAzimuthDeg = 0.0f;
    float centerElevationDeg = 0.0f;
    float centerDistance = 1.0f;
    float fieldWidth = 0.82f;
    float cellWidth = 0.48f;
    float mobility = 0.32f;
    float spatialInertia = 0.78f;
    float rotationRateHz = 0.012f;
    float air = 0.10f;
    float doppler = 0.0f;
    float outputGainDb = -18.0f;
    uint32_t seed = 0x45434f4cu;
};

inline float ambiNeuralWrapSignedDeg(float value)
{
    while (value > 180.0f) value -= 360.0f;
    while (value < -180.0f) value += 360.0f;
    return value;
}

inline AmbiNeuralEcologyParams sanitizeAmbiNeuralEcologyParams(AmbiNeuralEcologyParams params)
{
    params.order = std::clamp<uint32_t>(params.order, 1u, kAmbiNeuralEcologyMaxOrder);
    params.nodeSet = static_cast<AmbiNeuralNodeSet>(
        std::min<uint32_t>(static_cast<uint32_t>(params.nodeSet), 4u));
    params.activity = clamp(params.activity, 0.0f, 1.0f);
    params.drive = clamp(params.drive, 0.25f, 5.0f);
    params.ringFeedback = clamp(params.ringFeedback, 0.0f, 1.25f);
    params.matrixCoupling = clamp(params.matrixCoupling, 0.0f, 1.25f);
    params.hierarchy = clamp(params.hierarchy, 0.0f, 1.0f);
    params.phaseShift = clamp(params.phaseShift, 0.0f, 1.0f);
    params.registerSemitones = clamp(params.registerSemitones, -48.0f, 48.0f);
    params.timeSpread = clamp(params.timeSpread, 0.0f, 1.6f);
    params.diversity = clamp(params.diversity, 0.0f, 1.0f);
    params.brownian = clamp(params.brownian, 0.0f, 1.0f);
    params.drift = clamp(params.drift, 0.0f, 1.0f);
    params.selfModulation = clamp(params.selfModulation, 0.0f, 1.0f);
    params.fieldReturn = clamp(params.fieldReturn, 0.0f, 1.0f);
    params.propagationMs = clamp(params.propagationMs, 0.0f, 180.0f);
    params.pickupFocus = clamp(params.pickupFocus, 0.0f, 1.0f);
    params.pickupAdapt = clamp(params.pickupAdapt, 0.0f, 1.0f);
    params.pickupAnchor = clamp(params.pickupAnchor, 0.0f, 1.0f);
    params.pickupSet = static_cast<AmbiNeuralPickupSet>(
        std::min<uint32_t>(static_cast<uint32_t>(params.pickupSet), 1u));
    params.listeningMode = static_cast<AmbiNeuralListeningMode>(
        std::min<uint32_t>(static_cast<uint32_t>(params.listeningMode), 3u));
    params.auditoryPlasticity = clamp(params.auditoryPlasticity, 0.0f, 1.0f);
    params.metabolism = clamp(params.metabolism, 0.0f, 1.0f);
    params.adaptation = clamp(params.adaptation, 0.0f, 1.0f);
    params.genomeMorph = clamp(params.genomeMorph, 0.0f, 1.0f);
    params.heredity = clamp(params.heredity, 0.0f, 1.0f);
    params.mutationDepth = clamp(params.mutationDepth, 0.0f, 1.0f);
    params.plasticity = clamp(params.plasticity, 0.0f, 1.0f);
    params.plasticityMode = static_cast<AmbiNeuralPlasticityMode>(
        std::min<uint32_t>(static_cast<uint32_t>(params.plasticityMode), 3u));
    params.freeze = std::min<uint32_t>(params.freeze, 1u);
    params.mutate = std::min<uint32_t>(params.mutate, 65535u);
    params.scoreMode = static_cast<AmbiNeuralScoreMode>(
        std::min<uint32_t>(static_cast<uint32_t>(params.scoreMode), 3u));
    params.scorePlanes = static_cast<AmbiNeuralScorePlanes>(
        std::min<uint32_t>(static_cast<uint32_t>(params.scorePlanes), 3u));
    params.scoreAmount = clamp(params.scoreAmount, 0.0f, 1.0f);
    params.scoreDwellSeconds = clamp(params.scoreDwellSeconds, 0.25f, 60.0f);
    params.scoreTransitionSeconds = clamp(params.scoreTransitionSeconds, 0.05f, 30.0f);
    params.scoreVariation = clamp(params.scoreVariation, 0.0f, 1.0f);
    params.scoreRecombine = clamp(params.scoreRecombine, 0.0f, 1.0f);
    params.scoreMemory = clamp(params.scoreMemory, 0.0f, 1.0f);
    params.centerAzimuthDeg = ambiNeuralWrapSignedDeg(params.centerAzimuthDeg);
    params.centerElevationDeg = clamp(params.centerElevationDeg, -89.0f, 89.0f);
    params.centerDistance = clamp(params.centerDistance, 0.10f, 8.0f);
    params.fieldWidth = clamp(params.fieldWidth, 0.0f, 1.0f);
    params.cellWidth = clamp(params.cellWidth, 0.0f, 1.0f);
    params.mobility = clamp(params.mobility, 0.0f, 1.0f);
    params.spatialInertia = clamp(params.spatialInertia, 0.0f, 1.0f);
    params.rotationRateHz = clamp(params.rotationRateHz, -2.0f, 2.0f);
    params.air = clamp(params.air, 0.0f, 1.0f);
    params.doppler = clamp(params.doppler, 0.0f, 1.0f);
    params.outputGainDb = clamp(params.outputGainDb, -60.0f, 6.0f);
    if (params.seed == 0u) params.seed = 1u;
    return params;
}

struct AmbiNeuralEcologyPoint {
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float distance = 1.0f;
};

namespace ambi_neural_ecology_detail {

inline constexpr std::array<Vec3, 4> kTetra {{
    { 0.577350269f, 0.577350269f, 0.577350269f },
    { 0.577350269f, -0.577350269f, -0.577350269f },
    { -0.577350269f, 0.577350269f, -0.577350269f },
    { -0.577350269f, -0.577350269f, 0.577350269f },
}};

inline constexpr std::array<Vec3, 8> kCube {{
    { 0.577350269f, 0.577350269f, 0.577350269f },
    { 0.577350269f, -0.577350269f, -0.577350269f },
    { -0.577350269f, 0.577350269f, -0.577350269f },
    { -0.577350269f, -0.577350269f, 0.577350269f },
    { -0.577350269f, -0.577350269f, -0.577350269f },
    { -0.577350269f, 0.577350269f, 0.577350269f },
    { 0.577350269f, -0.577350269f, 0.577350269f },
    { 0.577350269f, 0.577350269f, -0.577350269f },
}};

inline Vec3 add(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline Vec3 scale(Vec3 value, float amount) { return { value.x * amount, value.y * amount, value.z * amount }; }
inline float length(Vec3 value) { return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z); }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 subtract(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline Vec3 cross(Vec3 a, Vec3 b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

inline Vec3 rotateZ(Vec3 value, float radians)
{
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return { value.x * cosine - value.y * sine, value.x * sine + value.y * cosine, value.z };
}

inline Vec3 rotateFromForward(Vec3 local, Vec3 forward)
{
    forward = normalize(forward);
    const Vec3 up = std::fabs(forward.z) > 0.92f ? Vec3 { 0.0f, 1.0f, 0.0f } : Vec3 { 0.0f, 0.0f, 1.0f };
    Vec3 right {
        up.y * forward.z - up.z * forward.y,
        up.z * forward.x - up.x * forward.z,
        up.x * forward.y - up.y * forward.x
    };
    right = normalize(right);
    const Vec3 vertical {
        forward.y * right.z - forward.z * right.y,
        forward.z * right.x - forward.x * right.z,
        forward.x * right.y - forward.y * right.x
    };
    return normalize({
        forward.x * local.x + right.x * local.y + vertical.x * local.z,
        forward.y * local.x + right.y * local.y + vertical.y * local.z,
        forward.z * local.x + right.z * local.y + vertical.z * local.z
    });
}

class PickupDelay {
public:
    void prepare(double sampleRate)
    {
        const uint32_t frames = std::max<uint32_t>(16u,
            static_cast<uint32_t>(std::ceil(std::max(1.0, sampleRate) * 0.200)) + 4u);
        data_.assign(frames, 0.0f);
        reset();
    }

    void reset()
    {
        std::fill(data_.begin(), data_.end(), 0.0f);
        write_ = 0u;
    }

    float process(float input, float delaySamples)
    {
        if (data_.size() < 4u) return 0.0f;
        data_[write_] = flushDenormal(input);
        delaySamples = clamp(delaySamples, 1.0f, static_cast<float>(data_.size() - 3u));
        float read = static_cast<float>(write_) - delaySamples;
        while (read < 0.0f) read += static_cast<float>(data_.size());
        const uint32_t first = static_cast<uint32_t>(read) % static_cast<uint32_t>(data_.size());
        const uint32_t second = (first + 1u) % static_cast<uint32_t>(data_.size());
        const float output = lerp(data_[first], data_[second], read - std::floor(read));
        write_ = (write_ + 1u) % static_cast<uint32_t>(data_.size());
        return output;
    }

private:
    std::vector<float> data_ {};
    uint32_t write_ = 0u;
};

} // namespace ambi_neural_ecology_detail

class AmbiNeuralEcology {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        lobeEnergyCoefficient_ = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.180));
        depth_.prepare(sampleRate_);
        for (auto& delay : pickupDelay_) delay.prepare(sampleRate_);
        reset();
    }

    void setParams(AmbiNeuralEcologyParams params)
    {
        params = sanitizeAmbiNeuralEcologyParams(params);
        const uint32_t previousSeed = params_.seed;
        const uint32_t previousMutate = params_.mutate;
        const auto previousSet = params_.nodeSet;
        params_ = params;
        if (params_.seed != previousSeed) resetRandomState();
        if (params_.nodeSet != previousSet) seedNewNodes(previousSet, params_.nodeSet);
        if (params_.mutate != previousMutate) mutateTopology();
    }

    const AmbiNeuralEcologyParams& params() const { return params_; }

    void reset()
    {
        resetRandomState();
        state_.fill(0.0f);
        output_.fill(0.0f);
        previousOutput_.fill(0.0f);
        clusterOutput_.fill(0.0f);
        lobeOutput_.fill(0.0f);
        phaseMemory_.fill(0.0f);
        brownianState_.fill(0.0f);
        brownianVelocity_.fill(0.0f);
        driftPhase_.fill(0.0);
        driftRate_.fill(0.0);
        activation_.fill(0.0f);
        dcInput_.fill(0.0f);
        dcOutput_.fill(0.0f);
        nodeEnergy_.fill(0.0f);
        nodePosition_ = {};
        basisCurrent_ = {};
        basisTarget_ = {};
        pickupBasis_ = {};
        pickupValue_.fill(0.0f);
        pickupFilter_.fill(0.0f);
        pickupFeedback_.fill(0.0f);
        pickupSteering_.fill(0.0f);
        pickupDirection_ = {};
        pickupAnchorDirection_ = {};
        auditoryReturn_.fill(0.0f);
        auditoryPlasticity_.fill(0.0f);
        lobeEnergy_.fill(0.0f);
        homeostaticBias_.fill(0.0f);
        clusterPlasticity_.fill(0.0f);
        lobePlasticity_.fill(0.0f);
        channelActivation_.fill(0.0f);
        rotationPhase_ = 0.0;
        auditoryRoamPhase_ = 0.0;
        controlCounter_ = 0u;
        spatialCounter_ = 0u;
        startupGain_ = 0.0f;
        smoothedActivity_ = params_.activity;
        smoothedDrive_ = params_.drive;
        smoothedRingFeedback_ = params_.ringFeedback;
        smoothedMatrixCoupling_ = params_.matrixCoupling;
        smoothedHierarchy_ = params_.hierarchy;
        smoothedPhaseShift_ = params_.phaseShift;
        smoothedRegister_ = params_.registerSemitones;
        smoothedTimeSpread_ = params_.timeSpread;
        smoothedDiversity_ = params_.diversity;
        smoothedBrownian_ = params_.brownian;
        smoothedDrift_ = params_.drift;
        smoothedSelfModulation_ = params_.selfModulation;
        smoothedFieldReturn_ = params_.fieldReturn;
        smoothedPropagationMs_ = params_.propagationMs;
        smoothedPickupFocus_ = params_.pickupFocus;
        smoothedPickupAdapt_ = params_.pickupAdapt;
        smoothedPickupAnchor_ = params_.pickupAnchor;
        smoothedAuditoryPlasticity_ = params_.auditoryPlasticity;
        smoothedMetabolism_ = params_.metabolism;
        smoothedAdaptation_ = params_.adaptation;
        smoothedGenomeMorph_ = params_.genomeMorph;
        smoothedHeredity_ = params_.heredity;
        smoothedPlasticity_ = params_.plasticity;
        smoothedCenterAzimuth_ = params_.centerAzimuthDeg;
        smoothedCenterElevation_ = params_.centerElevationDeg;
        smoothedCenterDistance_ = params_.centerDistance;
        smoothedFieldWidth_ = params_.fieldWidth;
        smoothedCellWidth_ = params_.cellWidth;
        smoothedMobility_ = params_.mobility;
        smoothedSpatialInertia_ = params_.spatialInertia;
        smoothedRotationRate_ = params_.rotationRateHz;
        smoothedAir_ = params_.air;
        smoothedDoppler_ = params_.doppler;
        smoothedOutputGainDb_ = params_.outputGainDb;
        for (auto& delay : pickupDelay_) delay.reset();

        for (uint32_t node = 0u; node < kAmbiNeuralEcologyMaxNodes; ++node) {
            const float seed = randomSigned() * 0.08f;
            state_[node] = seed;
            output_[node] = std::tanh(seed * params_.drive);
            driftPhase_[node] = static_cast<double>(randomUnit());
            driftRate_[node] = 0.0012 + static_cast<double>(randomUnit()) * 0.020;
            activation_[node] = nodeActive(node, params_.nodeSet) ? 1.0f : 0.0f;
        }
        updateGroups();
        updateSpatialTargets();
        nodePosition_ = targetPosition_;
        basisCurrent_ = basisTarget_;
        const uint32_t channels = ambiChannelsForOrder(params_.order);
        for (uint32_t channel = 0u; channel < channels; ++channel) channelActivation_[channel] = 1.0f;
    }

    uint32_t activeNodeCount() const { return nodeCount(params_.nodeSet); }
    uint32_t activePickupCount() const { return pickupCount(params_.pickupSet); }
    float nodeValue(uint32_t node) const { return output_[std::min<uint32_t>(node, kAmbiNeuralEcologyMaxNodes - 1u)]; }
    float nodeActivation(uint32_t node) const { return activation_[std::min<uint32_t>(node, kAmbiNeuralEcologyMaxNodes - 1u)]; }
    float nodeEnergy(uint32_t node) const { return nodeEnergy_[std::min<uint32_t>(node, kAmbiNeuralEcologyMaxNodes - 1u)]; }
    AmbiNeuralEcologyPoint nodePosition(uint32_t node) const { return nodePosition_[std::min<uint32_t>(node, kAmbiNeuralEcologyMaxNodes - 1u)]; }
    float clusterValue(uint32_t cluster) const { return clusterOutput_[std::min<uint32_t>(cluster, 15u)]; }
    float lobeValue(uint32_t lobe) const { return lobeOutput_[std::min<uint32_t>(lobe, 3u)]; }
    float pickupValue(uint32_t pickup) const
    {
        return pickupValue_[std::min<uint32_t>(pickup, kAmbiNeuralEcologyPickups - 1u)];
    }
    AmbiNeuralEcologyPoint pickupDirection(uint32_t pickup) const
    {
        return pickupDirection_[std::min<uint32_t>(pickup, kAmbiNeuralEcologyPickups - 1u)];
    }
    AmbiNeuralEcologyPoint pickupAnchorDirection(uint32_t pickup) const
    {
        return pickupAnchorDirection_[std::min<uint32_t>(pickup, kAmbiNeuralEcologyPickups - 1u)];
    }
    float pickupSteering(uint32_t pickup, uint32_t axis) const
    {
        pickup = std::min<uint32_t>(pickup, kAmbiNeuralEcologyPickups - 1u);
        axis = std::min<uint32_t>(axis, 1u);
        return pickupSteering_[pickup * 2u + axis];
    }
    float auditoryWeight(uint32_t lobe, uint32_t pickup) const
    {
        lobe = std::min<uint32_t>(lobe, 3u);
        pickup = std::min<uint32_t>(pickup, kAmbiNeuralEcologyPickups - 1u);
        return clamp(baseAuditoryWeight(lobe, pickup)
            + auditoryPlasticity_[lobe * kAmbiNeuralEcologyPickups + pickup], -1.35f, 1.35f);
    }
    float auditoryReturn(uint32_t lobe) const { return auditoryReturn_[std::min<uint32_t>(lobe, 3u)]; }
    float lobeEnergy(uint32_t lobe) const { return lobeEnergy_[std::min<uint32_t>(lobe, 3u)]; }
    float homeostaticBias(uint32_t lobe) const { return homeostaticBias_[std::min<uint32_t>(lobe, 3u)]; }

    std::array<float, kAmbiNeuralEcologyGenomeValues> genomeValues() const
    {
        std::array<float, kAmbiNeuralEcologyGenomeValues> values {};
        std::copy(clusterPlasticity_.begin(), clusterPlasticity_.end(), values.begin());
        std::copy(lobePlasticity_.begin(), lobePlasticity_.end(), values.begin() + 64u);
        std::copy(auditoryPlasticity_.begin(), auditoryPlasticity_.end(), values.begin() + 80u);
        std::copy(homeostaticBias_.begin(), homeostaticBias_.end(), values.begin() + 112u);
        values[116u] = static_cast<float>(auditoryRoamPhase_);
        std::copy(pickupSteering_.begin(), pickupSteering_.end(), values.begin() + 117u);
        return values;
    }

    void restoreGenome(const std::array<float, kAmbiNeuralEcologyGenomeValues>& values)
    {
        auto finite = [](float value) { return std::isfinite(value) ? value : 0.0f; };
        for (uint32_t index = 0u; index < 64u; ++index) {
            clusterPlasticity_[index] = clamp(finite(values[index]), -0.42f, 0.42f);
        }
        for (uint32_t index = 0u; index < 16u; ++index) {
            lobePlasticity_[index] = clamp(finite(values[64u + index]), -0.38f, 0.38f);
        }
        for (uint32_t index = 0u; index < 32u; ++index) {
            auditoryPlasticity_[index] = clamp(finite(values[80u + index]), -0.65f, 0.65f);
        }
        for (uint32_t index = 0u; index < 4u; ++index) {
            homeostaticBias_[index] = clamp(finite(values[112u + index]), -0.24f, 0.24f);
        }
        auditoryRoamPhase_ = static_cast<double>(finite(values[116u]));
        auditoryRoamPhase_ -= std::floor(auditoryRoamPhase_);
        for (uint32_t index = 0u; index < pickupSteering_.size(); ++index) {
            pickupSteering_[index] = clamp(finite(values[117u + index]), -1.0f, 1.0f);
        }
        constrainPickupSteering();
    }

    void setGenomeTarget(
        const std::array<float, kAmbiNeuralEcologyGenomeValues>& values,
        float transitionSeconds, float amount = 1.0f)
    {
        genomeTransitionFrom_ = genomeValues();
        genomeTransitionTarget_ = values;
        sanitizeGenome(genomeTransitionTarget_);
        amount = clamp(amount, 0.0f, 1.0f);
        for (uint32_t index = 0u; index < genomeTransitionTarget_.size(); ++index) {
            if (index == 116u) {
                float delta = genomeTransitionTarget_[index] - genomeTransitionFrom_[index];
                delta -= std::round(delta);
                genomeTransitionTarget_[index] = genomeTransitionFrom_[index] + delta * amount;
                genomeTransitionTarget_[index] -= std::floor(genomeTransitionTarget_[index]);
            } else {
                genomeTransitionTarget_[index] =
                    lerp(genomeTransitionFrom_[index], genomeTransitionTarget_[index], amount);
            }
        }
        genomeTransitionDuration_ = std::max(0.0f, transitionSeconds);
        genomeTransitionProgress_ = 0.0f;
        genomeTransitionActive_ = amount > 0.0f;
        if (genomeTransitionActive_ && genomeTransitionDuration_ <= 1.0e-4f) {
            restoreGenome(genomeTransitionTarget_);
            genomeTransitionProgress_ = 1.0f;
            genomeTransitionActive_ = false;
        }
    }

    void setGenomeSlot(uint32_t slot,
        const std::array<float, kAmbiNeuralEcologyGenomeValues>& values, bool valid = true)
    {
        slot = std::min<uint32_t>(slot, 1u);
        genomeSlot_[slot] = values;
        genomeSlotValid_[slot] = valid;
        if (valid) sanitizeGenome(genomeSlot_[slot]);
    }

    bool genomeSlotValid(uint32_t slot) const
    {
        return genomeSlotValid_[std::min<uint32_t>(slot, 1u)];
    }

    std::array<float, kAmbiNeuralEcologyGenomeValues> genomeSlot(uint32_t slot) const
    {
        return genomeSlot_[std::min<uint32_t>(slot, 1u)];
    }

    std::array<float, kAmbiNeuralEcologyGenomeValues> morphedGenome() const
    {
        return morphedGenomeAt(params_.genomeMorph);
    }

    void recallGenomeSlot(uint32_t slot)
    {
        slot = std::min<uint32_t>(slot, 1u);
        if (genomeSlotValid_[slot]) restoreGenome(genomeSlot_[slot]);
    }

    void process(float* const* outputs, uint32_t outputChannels, uint32_t frames)
    {
        if (!outputs || frames == 0u) return;
        outputChannels = std::min<uint32_t>(outputChannels, kAmbiNeuralEcologyMaxChannels);
        for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
            if (outputs[channel]) std::fill(outputs[channel], outputs[channel] + frames, 0.0f);
        }
        if (outputChannels == 0u) return;

        const uint32_t desiredChannels = std::min<uint32_t>(ambiChannelsForOrder(params_.order), outputChannels);
        const float parameterCoefficient = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.012));
        const float activationCoefficient = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.120));
        const float channelCoefficient = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.004));
        const float dcCoefficient = std::exp(-2.0f * kPi * 9.0f / static_cast<float>(sampleRate_));
        const float energyCoefficient = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.050));
        const float startupIncrement = 1.0f / std::max(1.0f, static_cast<float>(sampleRate_ * 0.020));

        for (uint32_t frame = 0u; frame < frames; ++frame) {
            smoothParams(parameterCoefficient);
            startupGain_ = std::min(1.0f, startupGain_ + startupIncrement);
            rotationPhase_ += static_cast<double>(smoothedRotationRate_) / sampleRate_;
            rotationPhase_ -= std::floor(rotationPhase_);

            for (uint32_t node = 0u; node < kAmbiNeuralEcologyMaxNodes; ++node) {
                const float target = nodeActive(node, params_.nodeSet) ? 1.0f : 0.0f;
                activation_[node] += (target - activation_[node]) * activationCoefficient;
            }
            for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
                const float target = channel < desiredChannels ? 1.0f : 0.0f;
                channelActivation_[channel] += (target - channelActivation_[channel]) * channelCoefficient;
            }

            processNetwork();
            if (spatialCounter_ == 0u) updateSpatialTargets();
            spatialCounter_ = (spatialCounter_ + 1u) % 16u;
            const float spatialCoefficient = lerp(0.24f, 0.0020f, smoothedSpatialInertia_);
            float activeWeight = 0.0f;
            for (float value : activation_) activeWeight += value;
            const float normalization = 0.86f / std::sqrt(std::max(1.0f, activeWeight));
            std::array<float, kAmbiNeuralEcologyMaxChannels> hoaFrame {};
            depth_.setParams({ smoothedDoppler_, smoothedAir_ });

            for (uint32_t node = 0u; node < kAmbiNeuralEcologyMaxNodes; ++node) {
                nodePosition_[node].azimuthDeg = ambiNeuralWrapSignedDeg(nodePosition_[node].azimuthDeg
                    + ambiNeuralWrapSignedDeg(targetPosition_[node].azimuthDeg - nodePosition_[node].azimuthDeg)
                        * spatialCoefficient);
                nodePosition_[node].elevationDeg += (targetPosition_[node].elevationDeg
                    - nodePosition_[node].elevationDeg) * spatialCoefficient;
                nodePosition_[node].distance += (targetPosition_[node].distance
                    - nodePosition_[node].distance) * spatialCoefficient;
                for (uint32_t channel = 0u; channel < desiredChannels; ++channel) {
                    basisCurrent_[node][channel] += (basisTarget_[node][channel]
                        - basisCurrent_[node][channel]) * spatialCoefficient;
                }

                float sample = output_[node] * activation_[node];
                const float hp = sample - dcInput_[node] + dcCoefficient * dcOutput_[node];
                dcInput_[node] = sample;
                dcOutput_[node] = flushDenormal(hp);
                nodeEnergy_[node] += (std::fabs(dcOutput_[node]) - nodeEnergy_[node]) * energyCoefficient;
                sample = depth_.process(node, dcOutput_[node], nodePosition_[node].distance);
                sample *= normalization / std::max(0.35f, nodePosition_[node].distance);
                if (!std::isfinite(sample)) sample = 0.0f;
                for (uint32_t channel = 0u; channel < desiredChannels; ++channel) {
                    hoaFrame[channel] = flushDenormal(hoaFrame[channel] + sample * basisCurrent_[node][channel]);
                }
            }
            depth_.advance();

            listenToField(hoaFrame, desiredChannels);
            const float gain = dbToGain(smoothedOutputGainDb_) * startupGain_;
            for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
                if (outputs[channel]) outputs[channel][frame] = channel < desiredChannels
                    ? flushDenormal(hoaFrame[channel] * gain * channelActivation_[channel]) : 0.0f;
            }
        }
    }

private:
    static constexpr uint32_t kControlInterval = 32u;

    static uint32_t nodeCount(AmbiNeuralNodeSet set)
    {
        static constexpr std::array<uint32_t, 5> counts {{ 4u, 8u, 16u, 32u, 64u }};
        return counts[std::min<uint32_t>(static_cast<uint32_t>(set), 4u)];
    }

    static uint32_t pickupCount(AmbiNeuralPickupSet set)
    {
        return set == AmbiNeuralPickupSet::Cube8 ? 8u : 4u;
    }

    static uint32_t nodeIndex(uint32_t lobe, uint32_t cluster, uint32_t local)
    {
        return lobe * 16u + cluster * 4u + local;
    }

    static bool clusterActive(uint32_t lobe, uint32_t cluster, AmbiNeuralNodeSet set)
    {
        switch (set) {
        case AmbiNeuralNodeSet::Ring4: return lobe == 0u && cluster == 0u;
        case AmbiNeuralNodeSet::Dual8: return lobe == 0u && cluster < 2u;
        case AmbiNeuralNodeSet::Cell16: return lobe == 0u;
        case AmbiNeuralNodeSet::Pair32: return lobe < 2u;
        case AmbiNeuralNodeSet::Field64: return true;
        }
        return false;
    }

    static bool nodeActive(uint32_t node, AmbiNeuralNodeSet set)
    {
        const uint32_t lobe = node / 16u;
        const uint32_t cluster = (node % 16u) / 4u;
        return clusterActive(lobe, cluster, set);
    }

    static uint32_t activeLobes(AmbiNeuralNodeSet set)
    {
        if (set == AmbiNeuralNodeSet::Pair32) return 2u;
        if (set == AmbiNeuralNodeSet::Field64) return 4u;
        return 1u;
    }

    void resetRandomState() { randomState_ = params_.seed ? params_.seed : 1u; }

    float randomUnit()
    {
        randomState_ += 0x9e3779b9u;
        uint32_t value = randomState_;
        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
        return static_cast<float>(value & 0x00ffffffu) / static_cast<float>(0x01000000u);
    }

    float randomSigned() { return randomUnit() * 2.0f - 1.0f; }

    void seedNewNodes(AmbiNeuralNodeSet previous, AmbiNeuralNodeSet next)
    {
        for (uint32_t node = 0u; node < kAmbiNeuralEcologyMaxNodes; ++node) {
            if (nodeActive(node, previous) || !nodeActive(node, next)) continue;
            const uint32_t lobe = node / 16u;
            const uint32_t cluster = (node % 16u) / 4u;
            float inherited = lobeOutput_[lobe] * 0.56f + clusterOutput_[lobe * 4u + cluster] * 0.32f;
            if (std::fabs(inherited) < 1.0e-4f) inherited = randomSigned() * 0.06f;
            state_[node] = inherited + randomSigned() * 0.035f;
            output_[node] = std::tanh(state_[node] * params_.drive);
        }
    }

    void mutateTopology()
    {
        const float depth = params_.mutationDepth;
        for (float& weight : clusterPlasticity_) {
            weight = clamp(weight + randomSigned() * 0.18f * depth, -0.42f, 0.42f);
        }
        for (float& weight : lobePlasticity_) {
            weight = clamp(weight + randomSigned() * 0.16f * depth, -0.38f, 0.38f);
        }
        for (float& weight : auditoryPlasticity_) {
            weight = clamp(weight + randomSigned() * 0.22f * depth, -0.65f, 0.65f);
        }
        for (float& steering : pickupSteering_) {
            steering = clamp(steering + randomSigned() * 0.34f * depth, -1.0f, 1.0f);
        }
        constrainPickupSteering();
        for (uint32_t node = 0u; node < kAmbiNeuralEcologyMaxNodes; ++node) {
            if (nodeActive(node, params_.nodeSet)) state_[node] += randomSigned() * 0.045f * depth;
        }
    }

    void smoothParams(float coefficient)
    {
        auto smooth = [coefficient](float& current, float target) { current += (target - current) * coefficient; };
        smooth(smoothedActivity_, params_.activity);
        smooth(smoothedDrive_, params_.drive);
        smooth(smoothedRingFeedback_, params_.ringFeedback);
        smooth(smoothedMatrixCoupling_, params_.matrixCoupling);
        smooth(smoothedHierarchy_, params_.hierarchy);
        smooth(smoothedPhaseShift_, params_.phaseShift);
        smooth(smoothedRegister_, params_.registerSemitones);
        smooth(smoothedTimeSpread_, params_.timeSpread);
        smooth(smoothedDiversity_, params_.diversity);
        smooth(smoothedBrownian_, params_.brownian);
        smooth(smoothedDrift_, params_.drift);
        smooth(smoothedSelfModulation_, params_.selfModulation);
        smooth(smoothedFieldReturn_, params_.fieldReturn);
        smooth(smoothedPropagationMs_, params_.propagationMs);
        smooth(smoothedPickupFocus_, params_.pickupFocus);
        smooth(smoothedPickupAdapt_, params_.pickupAdapt);
        smooth(smoothedPickupAnchor_, params_.pickupAnchor);
        smooth(smoothedAuditoryPlasticity_, params_.auditoryPlasticity);
        smooth(smoothedMetabolism_, params_.metabolism);
        smooth(smoothedAdaptation_, params_.adaptation);
        smooth(smoothedGenomeMorph_, params_.genomeMorph);
        smooth(smoothedHeredity_, params_.heredity);
        smooth(smoothedPlasticity_, params_.plasticity);
        smoothedCenterAzimuth_ = ambiNeuralWrapSignedDeg(smoothedCenterAzimuth_
            + ambiNeuralWrapSignedDeg(params_.centerAzimuthDeg - smoothedCenterAzimuth_) * coefficient);
        smooth(smoothedCenterElevation_, params_.centerElevationDeg);
        smooth(smoothedCenterDistance_, params_.centerDistance);
        smooth(smoothedFieldWidth_, params_.fieldWidth);
        smooth(smoothedCellWidth_, params_.cellWidth);
        smooth(smoothedMobility_, params_.mobility);
        smooth(smoothedSpatialInertia_, params_.spatialInertia);
        smooth(smoothedRotationRate_, params_.rotationRateHz);
        smooth(smoothedAir_, params_.air);
        smooth(smoothedDoppler_, params_.doppler);
        smooth(smoothedOutputGainDb_, params_.outputGainDb);
    }

    float phaseShift(float input, uint32_t node, uint32_t cluster)
    {
        const float coefficient = 0.16f + 0.12f * static_cast<float>(cluster);
        const float allpass = -coefficient * input + phaseMemory_[node];
        phaseMemory_[node] = flushDenormal(input + coefficient * allpass);
        return lerp(input, allpass, smoothedPhaseShift_);
    }

    float response(float input, uint32_t local) const
    {
        const float sigmoid = std::tanh(input);
        float alternate = sigmoid;
        switch (local) {
        case 0u:
            alternate = std::tanh(input * 1.32f) * 0.88f;
            break;
        case 1u:
            alternate = (std::tanh(input + 0.24f) - std::tanh(0.24f)) * 0.94f;
            break;
        case 2u:
            alternate = std::sin(clamp(input, -1.570796327f, 1.570796327f));
            break;
        case 3u:
            alternate = std::tanh(input * 2.1f) * 0.72f + sigmoid * 0.22f;
            break;
        }
        return clamp(lerp(sigmoid, alternate, smoothedDiversity_), -1.0f, 1.0f);
    }

    void processNetwork()
    {
        previousOutput_ = output_;
        const auto previousClusters = clusterOutput_;
        const auto previousLobes = lobeOutput_;
        if (controlCounter_ == 0u) updateSlowControl(previousClusters, previousLobes);
        controlCounter_ = (controlCounter_ + 1u) % kControlInterval;

        static constexpr std::array<float, 4> ringForward {{ 1.31f, 1.18f, 1.42f, -1.27f }};
        static constexpr std::array<float, 4> ringReverse {{ -0.24f, 0.19f, 0.22f, 0.17f }};
        static constexpr std::array<float, 4> localBias {{ -0.10f, 0.07f, -0.045f, 0.12f }};
        static constexpr std::array<float, 4> tauMs {{ 80.0f, 32.0f, 9.5f, 2.0f }};
        static constexpr std::array<float, 16> clusterMatrix {{
             0.00f,  0.31f, -0.17f,  0.10f,
            -0.28f,  0.00f,  0.23f,  0.13f,
             0.16f, -0.26f,  0.00f,  0.29f,
            -0.12f,  0.19f, -0.25f,  0.00f,
        }};
        static constexpr std::array<float, 16> lobeMatrix {{
             0.00f,  0.27f, -0.15f,  0.11f,
            -0.25f,  0.00f,  0.21f,  0.14f,
             0.18f, -0.24f,  0.00f,  0.26f,
            -0.10f,  0.17f, -0.23f,  0.00f,
        }};
        static constexpr std::array<float, 4> pickupSigns {{ 1.0f, -0.72f, 0.58f, -0.86f }};

        const float logMeanTau = 0.25f * (std::log(tauMs[0]) + std::log(tauMs[1])
            + std::log(tauMs[2]) + std::log(tauMs[3]));
        const float registerScale = std::exp2(-smoothedRegister_ / 12.0f);
        const float activityBias = (smoothedActivity_ - 0.5f) * 0.34f;

        for (uint32_t lobe = 0u; lobe < 4u; ++lobe) {
            float lobeCross = 0.0f;
            for (uint32_t source = 0u; source < 4u; ++source) {
                const float weight = lobeMatrix[lobe * 4u + source] + lobePlasticity_[lobe * 4u + source];
                lobeCross += previousLobes[source] * weight;
            }
            int previousActiveCluster = -1;
            for (uint32_t cluster = 0u; cluster < 4u; ++cluster) {
                const uint32_t clusterIndex = lobe * 4u + cluster;
                if (!clusterActive(lobe, cluster, params_.nodeSet) && clusterOutput_[clusterIndex] == 0.0f) continue;
                float cross = 0.0f;
                for (uint32_t source = 0u; source < 4u; ++source) {
                    const float weight = clusterMatrix[cluster * 4u + source]
                        + clusterPlasticity_[lobe * 16u + cluster * 4u + source];
                    cross += previousClusters[lobe * 4u + source] * weight;
                }
                const float slowSignal = previousActiveCluster < 0
                    ? previousClusters[clusterIndex]
                    : 0.62f * previousClusters[lobe * 4u + static_cast<uint32_t>(previousActiveCluster)]
                        + 0.38f * previousClusters[clusterIndex];
                const float timeMod = std::exp2(-smoothedSelfModulation_ * slowSignal
                    * (0.34f + 0.26f * static_cast<float>(cluster)));
                const float spreadTau = std::exp(logMeanTau
                    + (std::log(tauMs[cluster]) - logMeanTau) * smoothedTimeSpread_);
                const float tauSeconds = clamp(spreadTau * registerScale * timeMod, 0.18f, 4000.0f) * 0.001f;
                const float alpha = 1.0f - std::exp(-1.0f
                    / std::max(1.0f, static_cast<float>(sampleRate_) * tauSeconds));
                const float parent = previousActiveCluster < 0 ? 0.0f
                    : previousClusters[lobe * 4u + static_cast<uint32_t>(previousActiveCluster)];
                const float parentGate = previousActiveCluster < 0 ? 1.0f
                    : lerp(1.0f, 0.12f + 0.88f * (parent * 0.5f + 0.5f), smoothedHierarchy_);

                for (uint32_t local = 0u; local < 4u; ++local) {
                    const uint32_t node = nodeIndex(lobe, cluster, local);
                    if (activation_[node] < 1.0e-5f && !nodeActive(node, params_.nodeSet)) {
                        state_[node] *= 0.9995f;
                        output_[node] *= 0.9995f;
                        continue;
                    }
                    const uint32_t previous = nodeIndex(lobe, cluster, (local + 3u) % 4u);
                    const uint32_t next = nodeIndex(lobe, cluster, (local + 1u) % 4u);
                    const float ring = phaseShift(previousOutput_[previous], node, cluster) * ringForward[local]
                        + previousOutput_[next] * ringReverse[local];
                    const float drift = std::sin(static_cast<float>(driftPhase_[node]) * 6.283185307f)
                        * smoothedDrift_ * (0.11f + 0.018f * static_cast<float>(cluster));
                    const float wander = brownianState_[node] * smoothedBrownian_;
                    const float feedback = ring * smoothedRingFeedback_ * (1.0f + wander * 0.30f);
                    const float hierarchyDrive = parent * smoothedHierarchy_ * 0.58f;
                    const float fieldDrive = auditoryReturn_[lobe] * smoothedFieldReturn_ * pickupSigns[local] * 0.78f;
                    const float summed = localBias[local] + activityBias + homeostaticBias_[lobe]
                        + drift + wander * 0.20f
                        + feedback * parentGate + cross * smoothedMatrixCoupling_
                        + lobeCross * smoothedMatrixCoupling_ * 0.56f + hierarchyDrive + fieldDrive;
                    const float target = response(summed * smoothedDrive_, local);
                    state_[node] = flushDenormal(state_[node] + (target - state_[node]) * alpha);
                    output_[node] = std::tanh(state_[node] * 1.42f);
                }
                if (clusterActive(lobe, cluster, params_.nodeSet)) previousActiveCluster = static_cast<int>(cluster);
            }
        }
        updateGroups();
    }

    void updateGroups()
    {
        for (uint32_t lobe = 0u; lobe < 4u; ++lobe) {
            float lobeSum = 0.0f;
            float lobeWeight = 0.0f;
            for (uint32_t cluster = 0u; cluster < 4u; ++cluster) {
                float sum = 0.0f;
                float weight = 0.0f;
                for (uint32_t local = 0u; local < 4u; ++local) {
                    const uint32_t node = nodeIndex(lobe, cluster, local);
                    sum += output_[node] * activation_[node];
                    weight += activation_[node];
                }
                const uint32_t index = lobe * 4u + cluster;
                clusterOutput_[index] = weight > 1.0e-5f ? sum / weight : 0.0f;
                if (weight > 1.0e-5f) {
                    lobeSum += clusterOutput_[index];
                    lobeWeight += 1.0f;
                }
            }
            lobeOutput_[lobe] = lobeWeight > 0.0f ? lobeSum / lobeWeight : 0.0f;
            lobeEnergy_[lobe] += (std::fabs(lobeOutput_[lobe]) - lobeEnergy_[lobe]) * lobeEnergyCoefficient_;
        }
    }

    void updateSlowControl(const std::array<float, 16>& clusters, const std::array<float, 4>& lobes)
    {
        const float seconds = static_cast<float>(kControlInterval / sampleRate_);
        advanceGenomeTransition(seconds);
        if (params_.freeze == 0u) {
            for (uint32_t node = 0u; node < kAmbiNeuralEcologyMaxNodes; ++node) {
                const uint32_t cluster = (node % 16u) / 4u;
                const float rate = 0.16f + 0.12f * static_cast<float>(cluster)
                    + 0.018f * static_cast<float>(node % 4u);
                const float step = std::sqrt(std::max(0.0f, seconds * rate));
                brownianVelocity_[node] += randomSigned() * step * 0.065f;
                brownianVelocity_[node] *= std::exp(-seconds * 2.2f);
                brownianState_[node] += brownianVelocity_[node] * step;
                brownianState_[node] -= brownianState_[node] * seconds * 0.11f;
                if (std::fabs(brownianState_[node]) > 1.0f) {
                    brownianState_[node] = clamp(brownianState_[node], -1.0f, 1.0f);
                    brownianVelocity_[node] *= -0.55f;
                }
                driftPhase_[node] += driftRate_[node] * static_cast<double>(kControlInterval) / sampleRate_;
                driftPhase_[node] -= std::floor(driftPhase_[node]);
            }
            updatePlasticity(clusters, lobes, seconds);
            updateAuditoryPlasticity(lobes, seconds);
            updatePickupSteering(seconds);
            updateHomeostasis(seconds);
            updateInheritance(seconds);
            if (params_.listeningMode == AmbiNeuralListeningMode::Roaming) {
                auditoryRoamPhase_ += seconds * (0.006 + 0.044 * smoothedAuditoryPlasticity_);
                auditoryRoamPhase_ -= std::floor(auditoryRoamPhase_);
            }
        }
    }

    void advanceGenomeTransition(float seconds)
    {
        if (!genomeTransitionActive_) return;
        genomeTransitionProgress_ = std::min(
            1.0f, genomeTransitionProgress_ + seconds / std::max(1.0e-4f, genomeTransitionDuration_));
        const float amount = genomeTransitionProgress_ * genomeTransitionProgress_
            * (3.0f - 2.0f * genomeTransitionProgress_);
        std::array<float, kAmbiNeuralEcologyGenomeValues> genome {};
        for (uint32_t index = 0u; index < genome.size(); ++index) {
            if (index == 116u) {
                float delta = genomeTransitionTarget_[index] - genomeTransitionFrom_[index];
                delta -= std::round(delta);
                genome[index] = genomeTransitionFrom_[index] + delta * amount;
                genome[index] -= std::floor(genome[index]);
            } else {
                genome[index] = lerp(
                    genomeTransitionFrom_[index], genomeTransitionTarget_[index], amount);
            }
        }
        restoreGenome(genome);
        if (genomeTransitionProgress_ >= 1.0f) genomeTransitionActive_ = false;
    }

    float plasticDelta(float source, float destination) const
    {
        const float correlation = source * destination;
        switch (params_.plasticityMode) {
        case AmbiNeuralPlasticityMode::Reinforce: return correlation;
        case AmbiNeuralPlasticityMode::Inhibit: return -std::fabs(correlation);
        case AmbiNeuralPlasticityMode::Balance: return -correlation * (0.35f + std::fabs(destination));
        case AmbiNeuralPlasticityMode::Prune: return std::fabs(correlation) > 0.18f ? correlation * 0.28f : -0.12f;
        }
        return 0.0f;
    }

    void updatePlasticity(const std::array<float, 16>& clusters, const std::array<float, 4>& lobes, float seconds)
    {
        if (smoothedPlasticity_ <= 1.0e-5f) return;
        const float rate = smoothedPlasticity_ * seconds * 0.72f;
        for (uint32_t lobe = 0u; lobe < 4u; ++lobe) {
            for (uint32_t destination = 0u; destination < 4u; ++destination) {
                for (uint32_t source = 0u; source < 4u; ++source) {
                    if (source == destination) continue;
                    float& weight = clusterPlasticity_[lobe * 16u + destination * 4u + source];
                    weight += plasticDelta(clusters[lobe * 4u + source], clusters[lobe * 4u + destination]) * rate;
                    weight -= weight * seconds * 0.018f;
                    weight = clamp(weight, -0.42f, 0.42f);
                }
            }
        }
        for (uint32_t destination = 0u; destination < 4u; ++destination) {
            for (uint32_t source = 0u; source < 4u; ++source) {
                if (source == destination) continue;
                float& weight = lobePlasticity_[destination * 4u + source];
                weight += plasticDelta(lobes[source], lobes[destination]) * rate * 0.72f;
                weight -= weight * seconds * 0.014f;
                weight = clamp(weight, -0.38f, 0.38f);
            }
        }
    }

    float baseAuditoryWeight(uint32_t destination, uint32_t source) const
    {
        const uint32_t count = pickupCount(params_.pickupSet);
        switch (params_.listeningMode) {
        case AmbiNeuralListeningMode::Local:
            return source == destination ? 1.0f : 0.0f;
        case AmbiNeuralListeningMode::Cross:
            if (source == (destination + 1u) % count) return 0.88f;
            return source == destination ? 0.18f : 0.0f;
        case AmbiNeuralListeningMode::Diffuse:
            return source == destination ? 0.30f : 0.70f / static_cast<float>(count - 1u);
        case AmbiNeuralListeningMode::Roaming: {
            const float position = std::fmod(static_cast<float>(destination)
                + static_cast<float>(auditoryRoamPhase_) * static_cast<float>(count), static_cast<float>(count));
            float distance = std::fabs(static_cast<float>(source) - position);
            distance = std::min(distance, static_cast<float>(count) - distance);
            const float roaming = clamp(1.0f - distance, 0.0f, 1.0f);
            return roaming * 0.90f + (source == destination ? 0.10f : 0.0f);
        }
        }
        return 0.0f;
    }

    void updateAuditoryPlasticity(const std::array<float, 4>& lobes, float seconds)
    {
        if (smoothedAuditoryPlasticity_ <= 1.0e-5f) return;
        const float rate = smoothedAuditoryPlasticity_ * seconds * 0.92f;
        const uint32_t count = pickupCount(params_.pickupSet);
        for (uint32_t destination = 0u; destination < 4u; ++destination) {
            for (uint32_t source = 0u; source < count; ++source) {
                float& weight = auditoryPlasticity_[destination * kAmbiNeuralEcologyPickups + source];
                weight += plasticDelta(pickupFeedback_[source], lobes[destination]) * rate;
                weight -= weight * seconds * 0.010f;
                weight = clamp(weight, -0.65f, 0.65f);
            }
        }
    }

    void updateHomeostasis(float seconds)
    {
        const uint32_t active = activeLobes(params_.nodeSet);
        const float target = 0.015f + smoothedMetabolism_ * 0.42f;
        const float rate = smoothedAdaptation_ * smoothedAdaptation_ * seconds * 0.75f;
        for (uint32_t lobe = 0u; lobe < 4u; ++lobe) {
            if (lobe < active) {
                homeostaticBias_[lobe] += (target - lobeEnergy_[lobe]) * rate;
            } else {
                homeostaticBias_[lobe] *= std::exp(-seconds * 0.08f);
            }
            homeostaticBias_[lobe] = clamp(homeostaticBias_[lobe], -0.24f, 0.24f);
        }
    }

    void pickupFrame(uint32_t pickup, Vec3& anchor, Vec3& tangentX, Vec3& tangentY) const
    {
        using namespace ambi_neural_ecology_detail;
        const Vec3 center = directionFromAed(smoothedCenterAzimuth_, smoothedCenterElevation_);
        const float rotation = static_cast<float>(rotationPhase_) * 6.283185307f;
        const Vec3 local = rotateZ(kCube[pickup % kAmbiNeuralEcologyPickups], rotation * 0.37f);
        anchor = rotateFromForward(local, center);
        const Vec3 reference = std::fabs(anchor.z) > 0.92f
            ? Vec3 { 0.0f, 1.0f, 0.0f } : Vec3 { 0.0f, 0.0f, 1.0f };
        tangentX = normalize(cross(reference, anchor));
        tangentY = normalize(cross(anchor, tangentX));
    }

    Vec3 pickupLearningTarget(uint32_t pickup) const
    {
        using namespace ambi_neural_ecology_detail;
        const uint32_t active = activeLobes(params_.nodeSet);
        const uint32_t associatedLobe = pickup % kAmbiNeuralEcologyLobes;
        const bool useAssociatedLobe = associatedLobe < active;
        Vec3 centroid {};
        float total = 0.0f;
        for (uint32_t node = 0u; node < kAmbiNeuralEcologyMaxNodes; ++node) {
            if (activation_[node] <= 1.0e-4f) continue;
            if (useAssociatedLobe && node / 16u != associatedLobe) continue;
            const float weight = activation_[node] * (0.015f + nodeEnergy_[node]);
            centroid = add(centroid, scale(directionFromAed(
                nodePosition_[node].azimuthDeg, nodePosition_[node].elevationDeg), weight));
            total += weight;
        }
        if (total <= 1.0e-5f || length(centroid) <= 1.0e-5f) {
            Vec3 anchor {};
            Vec3 tangentX {};
            Vec3 tangentY {};
            pickupFrame(pickup, anchor, tangentX, tangentY);
            return anchor;
        }
        return normalize(centroid);
    }

    void constrainPickupSteering()
    {
        for (uint32_t pickup = 0u; pickup < kAmbiNeuralEcologyPickups; ++pickup) {
            float& x = pickupSteering_[pickup * 2u];
            float& y = pickupSteering_[pickup * 2u + 1u];
            const float magnitude = std::sqrt(x * x + y * y);
            if (magnitude > 1.0f) {
                x /= magnitude;
                y /= magnitude;
            }
        }
    }

    void updatePickupSteering(float seconds)
    {
        using namespace ambi_neural_ecology_detail;
        const uint32_t count = pickupCount(params_.pickupSet);
        const uint32_t active = activeLobes(params_.nodeSet);
        const float adapt = smoothedPickupAdapt_;
        const float anchorDecay = std::exp(-seconds * 0.50f
            * smoothedPickupAnchor_ * smoothedPickupAnchor_);
        for (uint32_t pickup = 0u; pickup < count; ++pickup) {
            Vec3 anchor {};
            Vec3 tangentX {};
            Vec3 tangentY {};
            pickupFrame(pickup, anchor, tangentX, tangentY);
            const Vec3 target = pickupLearningTarget(pickup);
            float targetX = dot(target, tangentX);
            float targetY = dot(target, tangentY);
            float targetMagnitude = std::sqrt(targetX * targetX + targetY * targetY);
            if (targetMagnitude > 1.0f) {
                targetX /= targetMagnitude;
                targetY /= targetMagnitude;
            }

            float signedRelationship = 0.0f;
            float relationshipNorm = 0.0f;
            for (uint32_t lobe = 0u; lobe < active; ++lobe) {
                const float energy = 0.02f + lobeEnergy_[lobe];
                const float weight = auditoryWeight(lobe, pickup);
                signedRelationship += weight * energy;
                relationshipNorm += std::fabs(weight) * energy;
            }
            const float polarity = relationshipNorm > 1.0e-5f && signedRelationship < 0.0f ? -1.0f : 1.0f;
            targetX *= polarity;
            targetY *= polarity;
            const float heard = std::fabs(pickupFeedback_[pickup]);
            const float influence = relationshipNorm / std::max(0.02f, static_cast<float>(active));
            const float rate = adapt * (0.08f + 2.4f * heard * (0.18f + influence));
            const float coefficient = 1.0f - std::exp(-seconds * rate);
            float& x = pickupSteering_[pickup * 2u];
            float& y = pickupSteering_[pickup * 2u + 1u];
            x += (targetX - x) * coefficient;
            y += (targetY - y) * coefficient;
            x *= anchorDecay;
            y *= anchorDecay;
        }

        constexpr float kRepulsionThreshold = 0.848048096f; // 32 degrees
        for (uint32_t first = 0u; first < count; ++first) {
            const Vec3 firstDirection = directionFromAed(
                pickupDirection_[first].azimuthDeg, pickupDirection_[first].elevationDeg);
            for (uint32_t second = first + 1u; second < count; ++second) {
                const Vec3 secondDirection = directionFromAed(
                    pickupDirection_[second].azimuthDeg, pickupDirection_[second].elevationDeg);
                const float similarity = dot(firstDirection, secondDirection);
                if (similarity <= kRepulsionThreshold) continue;
                const Vec3 separation = subtract(firstDirection, secondDirection);
                if (length(separation) <= 1.0e-5f) continue;
                const Vec3 away = normalize(separation);
                const float amount = seconds * 0.90f
                    * (similarity - kRepulsionThreshold) / (1.0f - kRepulsionThreshold);
                Vec3 anchor {};
                Vec3 tangentX {};
                Vec3 tangentY {};
                pickupFrame(first, anchor, tangentX, tangentY);
                pickupSteering_[first * 2u] += dot(away, tangentX) * amount;
                pickupSteering_[first * 2u + 1u] += dot(away, tangentY) * amount;
                pickupFrame(second, anchor, tangentX, tangentY);
                pickupSteering_[second * 2u] -= dot(away, tangentX) * amount;
                pickupSteering_[second * 2u + 1u] -= dot(away, tangentY) * amount;
            }
        }
        constrainPickupSteering();
    }

    static void sanitizeGenome(std::array<float, kAmbiNeuralEcologyGenomeValues>& values)
    {
        auto finite = [](float value) { return std::isfinite(value) ? value : 0.0f; };
        for (uint32_t index = 0u; index < 64u; ++index) {
            values[index] = clamp(finite(values[index]), -0.42f, 0.42f);
        }
        for (uint32_t index = 0u; index < 16u; ++index) {
            values[64u + index] = clamp(finite(values[64u + index]), -0.38f, 0.38f);
        }
        for (uint32_t index = 0u; index < 32u; ++index) {
            values[80u + index] = clamp(finite(values[80u + index]), -0.65f, 0.65f);
        }
        for (uint32_t index = 0u; index < 4u; ++index) {
            values[112u + index] = clamp(finite(values[112u + index]), -0.24f, 0.24f);
        }
        values[116u] = finite(values[116u]);
        values[116u] -= std::floor(values[116u]);
        for (uint32_t index = 117u; index < kAmbiNeuralEcologyGenomeValues; ++index) {
            values[index] = clamp(finite(values[index]), -1.0f, 1.0f);
        }
        for (uint32_t pickup = 0u; pickup < kAmbiNeuralEcologyPickups; ++pickup) {
            float& x = values[117u + pickup * 2u];
            float& y = values[117u + pickup * 2u + 1u];
            const float magnitude = std::sqrt(x * x + y * y);
            if (magnitude > 1.0f) {
                x /= magnitude;
                y /= magnitude;
            }
        }
    }

    std::array<float, kAmbiNeuralEcologyGenomeValues> morphedGenomeAt(float morph) const
    {
        if (!genomeSlotValid_[0u] && !genomeSlotValid_[1u]) return genomeValues();
        if (!genomeSlotValid_[0u]) return genomeSlot_[1u];
        if (!genomeSlotValid_[1u]) return genomeSlot_[0u];
        morph = clamp(morph, 0.0f, 1.0f);
        std::array<float, kAmbiNeuralEcologyGenomeValues> result {};
        for (uint32_t index = 0u; index < 116u; ++index) {
            result[index] = lerp(genomeSlot_[0u][index], genomeSlot_[1u][index], morph);
        }
        float phaseDelta = genomeSlot_[1u][116u] - genomeSlot_[0u][116u];
        phaseDelta -= std::round(phaseDelta);
        result[116u] = genomeSlot_[0u][116u] + phaseDelta * morph;
        result[116u] -= std::floor(result[116u]);
        for (uint32_t index = 117u; index < kAmbiNeuralEcologyGenomeValues; ++index) {
            result[index] = lerp(genomeSlot_[0u][index], genomeSlot_[1u][index], morph);
        }
        return result;
    }

    void updateInheritance(float seconds)
    {
        if (smoothedHeredity_ <= 1.0e-5f || (!genomeSlotValid_[0u] && !genomeSlotValid_[1u])) return;
        const auto target = morphedGenomeAt(smoothedGenomeMorph_);
        const float coefficient = 1.0f - std::exp(-seconds * 1.5f
            * smoothedHeredity_ * smoothedHeredity_);
        for (uint32_t index = 0u; index < 64u; ++index) {
            clusterPlasticity_[index] += (target[index] - clusterPlasticity_[index]) * coefficient;
        }
        for (uint32_t index = 0u; index < 16u; ++index) {
            lobePlasticity_[index] += (target[64u + index] - lobePlasticity_[index]) * coefficient;
        }
        for (uint32_t index = 0u; index < 32u; ++index) {
            auditoryPlasticity_[index] += (target[80u + index] - auditoryPlasticity_[index]) * coefficient;
        }
        for (uint32_t index = 0u; index < 4u; ++index) {
            homeostaticBias_[index] += (target[112u + index] - homeostaticBias_[index]) * coefficient;
        }
        double phaseDelta = static_cast<double>(target[116u]) - auditoryRoamPhase_;
        phaseDelta -= std::round(phaseDelta);
        auditoryRoamPhase_ += phaseDelta * coefficient;
        auditoryRoamPhase_ -= std::floor(auditoryRoamPhase_);
        for (uint32_t index = 0u; index < pickupSteering_.size(); ++index) {
            pickupSteering_[index] += (target[117u + index] - pickupSteering_[index]) * coefficient;
        }
        constrainPickupSteering();
    }

    Vec3 lobeCenter(uint32_t lobe) const
    {
        using namespace ambi_neural_ecology_detail;
        const uint32_t count = activeLobes(params_.nodeSet);
        if (count == 1u) return {};
        if (count == 2u) return { lobe == 0u ? -0.58f : 0.58f, 0.0f, 0.0f };
        return scale(kTetra[lobe % 4u], 0.72f);
    }

    Vec3 clusterCenter(uint32_t cluster) const
    {
        using namespace ambi_neural_ecology_detail;
        if (params_.nodeSet == AmbiNeuralNodeSet::Ring4) return {};
        if (params_.nodeSet == AmbiNeuralNodeSet::Dual8) {
            return { cluster == 0u ? -0.44f : 0.44f, 0.0f, 0.0f };
        }
        return scale(kTetra[cluster % 4u], 0.42f);
    }

    void updateSpatialTargets()
    {
        using namespace ambi_neural_ecology_detail;
        const Vec3 center = directionFromAed(smoothedCenterAzimuth_, smoothedCenterElevation_);
        const float rotation = static_cast<float>(rotationPhase_) * 6.283185307f;
        for (uint32_t node = 0u; node < kAmbiNeuralEcologyMaxNodes; ++node) {
            const uint32_t lobe = node / 16u;
            const uint32_t cluster = (node % 16u) / 4u;
            const uint32_t local = node % 4u;
            Vec3 position = scale(lobeCenter(lobe), smoothedFieldWidth_);
            position = add(position, scale(clusterCenter(cluster), smoothedFieldWidth_ * 0.72f));
            position = add(position, scale(kTetra[local], smoothedCellWidth_ * 0.24f));
            const float stateMotion = smoothedMobility_ * (clusterOutput_[lobe * 4u + cluster] * 0.56f
                + output_[node] * 0.24f + auditoryReturn_[lobe] * 0.20f);
            position = rotateZ(position, rotation + stateMotion * 1.8f);
            position.z += stateMotion * 0.22f;
            const float magnitude = length(position);
            Vec3 localDirection = magnitude > 1.0e-5f ? normalize(position) : Vec3 { 1.0f, 0.0f, 0.0f };
            Vec3 direction = normalize(add(scale(center, 1.0f - smoothedFieldWidth_),
                scale(rotateFromForward(localDirection, center), smoothedFieldWidth_)));
            const float azimuth = std::atan2(direction.y, direction.x) * 180.0f / kPi;
            const float elevation = std::asin(clamp(direction.z, -1.0f, 1.0f)) * 180.0f / kPi;
            const float radial = 1.0f + clamp(magnitude - 0.52f, -0.45f, 0.80f) * 0.30f
                + stateMotion * 0.08f;
            targetPosition_[node] = { ambiNeuralWrapSignedDeg(azimuth), clamp(elevation, -89.0f, 89.0f),
                clamp(smoothedCenterDistance_ * radial, 0.10f, 8.0f) };
            basisTarget_[node] = acnSn3dBasis7(direction);
        }

        const uint32_t count = pickupCount(params_.pickupSet);
        for (uint32_t pickup = 0u; pickup < count; ++pickup) {
            Vec3 anchor {};
            Vec3 tangentX {};
            Vec3 tangentY {};
            pickupFrame(pickup, anchor, tangentX, tangentY);
            const float steerX = pickupSteering_[pickup * 2u];
            const float steerY = pickupSteering_[pickup * 2u + 1u];
            constexpr float kMaximumTangent = 1.279941633f; // tan(52 degrees)
            const Vec3 direction = normalize(add(anchor, add(
                scale(tangentX, steerX * kMaximumTangent),
                scale(tangentY, steerY * kMaximumTangent))));
            const float anchorAzimuth = std::atan2(anchor.y, anchor.x) * 180.0f / kPi;
            const float anchorElevation = std::asin(clamp(anchor.z, -1.0f, 1.0f)) * 180.0f / kPi;
            const float azimuth = std::atan2(direction.y, direction.x) * 180.0f / kPi;
            const float elevation = std::asin(clamp(direction.z, -1.0f, 1.0f)) * 180.0f / kPi;
            pickupAnchorDirection_[pickup] = {
                ambiNeuralWrapSignedDeg(anchorAzimuth), clamp(anchorElevation, -89.0f, 89.0f), 1.0f
            };
            pickupDirection_[pickup] = {
                ambiNeuralWrapSignedDeg(azimuth), clamp(elevation, -89.0f, 89.0f), 1.0f
            };
            pickupBasis_[pickup] = acnSn3dBasis7(direction);
        }
    }

    void listenToField(const std::array<float, 64>& hoaFrame, uint32_t channels)
    {
        const float cutoff = lerp(15000.0f, 1700.0f,
            clamp(smoothedPropagationMs_ / 180.0f + smoothedAir_ * 0.55f, 0.0f, 1.0f));
        const float pole = std::exp(-2.0f * kPi * cutoff / static_cast<float>(sampleRate_));
        const uint32_t count = pickupCount(params_.pickupSet);
        for (uint32_t pickup = 0u; pickup < count; ++pickup) {
            float directional = 0.0f;
            float norm = 0.0f;
            for (uint32_t channel = 0u; channel < channels; ++channel) {
                directional += hoaFrame[channel] * pickupBasis_[pickup][channel];
                norm += pickupBasis_[pickup][channel] * pickupBasis_[pickup][channel];
            }
            directional /= std::max(1.0f, norm);
            const float heard = lerp(hoaFrame[0], directional, smoothedPickupFocus_);
            pickupFilter_[pickup] = heard * (1.0f - pole) + pickupFilter_[pickup] * pole;
            const float spread = count > 1u
                ? static_cast<float>(pickup) / static_cast<float>(count - 1u) : 0.0f;
            const float delaySamples = std::max(1.0f, smoothedPropagationMs_ * static_cast<float>(sampleRate_) * 0.001f
                * (0.82f + 0.33f * spread));
            const float delayed = pickupDelay_[pickup].process(pickupFilter_[pickup], delaySamples);
            pickupValue_[pickup] += (delayed - pickupValue_[pickup]) * 0.12f;
            pickupFeedback_[pickup] = std::tanh(delayed * 1.8f);
        }
        for (uint32_t lobe = 0u; lobe < 4u; ++lobe) {
            float mixed = 0.0f;
            float norm = 0.0f;
            for (uint32_t pickup = 0u; pickup < count; ++pickup) {
                const float weight = auditoryWeight(lobe, pickup);
                mixed += pickupFeedback_[pickup] * weight;
                norm += std::fabs(weight);
            }
            auditoryReturn_[lobe] = std::tanh(mixed / std::max(1.0f, norm));
        }
    }

    double sampleRate_ = 48000.0;
    AmbiNeuralEcologyParams params_ {};
    std::array<float, 64> state_ {};
    std::array<float, 64> output_ {};
    std::array<float, 64> previousOutput_ {};
    std::array<float, 16> clusterOutput_ {};
    std::array<float, 4> lobeOutput_ {};
    std::array<float, 64> phaseMemory_ {};
    std::array<float, 64> brownianState_ {};
    std::array<float, 64> brownianVelocity_ {};
    std::array<double, 64> driftPhase_ {};
    std::array<double, 64> driftRate_ {};
    std::array<float, 64> activation_ {};
    std::array<float, 64> dcInput_ {};
    std::array<float, 64> dcOutput_ {};
    std::array<float, 64> nodeEnergy_ {};
    std::array<AmbiNeuralEcologyPoint, 64> nodePosition_ {};
    std::array<AmbiNeuralEcologyPoint, 64> targetPosition_ {};
    std::array<std::array<float, 64>, 64> basisCurrent_ {};
    std::array<std::array<float, 64>, 64> basisTarget_ {};
    std::array<std::array<float, 64>, kAmbiNeuralEcologyPickups> pickupBasis_ {};
    std::array<ambi_neural_ecology_detail::PickupDelay, kAmbiNeuralEcologyPickups> pickupDelay_ {};
    std::array<float, kAmbiNeuralEcologyPickups> pickupValue_ {};
    std::array<float, kAmbiNeuralEcologyPickups> pickupFilter_ {};
    std::array<float, kAmbiNeuralEcologyPickups> pickupFeedback_ {};
    std::array<float, kAmbiNeuralEcologyPickups * 2u> pickupSteering_ {};
    std::array<AmbiNeuralEcologyPoint, kAmbiNeuralEcologyPickups> pickupDirection_ {};
    std::array<AmbiNeuralEcologyPoint, kAmbiNeuralEcologyPickups> pickupAnchorDirection_ {};
    std::array<float, 4> auditoryReturn_ {};
    std::array<float, kAmbiNeuralEcologyLobes * kAmbiNeuralEcologyPickups> auditoryPlasticity_ {};
    std::array<float, 4> lobeEnergy_ {};
    std::array<float, 4> homeostaticBias_ {};
    std::array<float, 64> clusterPlasticity_ {};
    std::array<float, 16> lobePlasticity_ {};
    std::array<float, 64> channelActivation_ {};
    std::array<std::array<float, kAmbiNeuralEcologyGenomeValues>, 2u> genomeSlot_ {};
    std::array<bool, 2u> genomeSlotValid_ {{ false, false }};
    std::array<float, kAmbiNeuralEcologyGenomeValues> genomeTransitionFrom_ {};
    std::array<float, kAmbiNeuralEcologyGenomeValues> genomeTransitionTarget_ {};
    float genomeTransitionProgress_ = 1.0f;
    float genomeTransitionDuration_ = 0.0f;
    bool genomeTransitionActive_ = false;
    AmbiEncoderDepthProcessor<64> depth_ {};
    uint32_t randomState_ = 1u;
    uint32_t controlCounter_ = 0u;
    uint32_t spatialCounter_ = 0u;
    double rotationPhase_ = 0.0;
    double auditoryRoamPhase_ = 0.0;
    float startupGain_ = 0.0f;
    float lobeEnergyCoefficient_ = 0.0001f;

    float smoothedActivity_ = params_.activity;
    float smoothedDrive_ = params_.drive;
    float smoothedRingFeedback_ = params_.ringFeedback;
    float smoothedMatrixCoupling_ = params_.matrixCoupling;
    float smoothedHierarchy_ = params_.hierarchy;
    float smoothedPhaseShift_ = params_.phaseShift;
    float smoothedRegister_ = params_.registerSemitones;
    float smoothedTimeSpread_ = params_.timeSpread;
    float smoothedDiversity_ = params_.diversity;
    float smoothedBrownian_ = params_.brownian;
    float smoothedDrift_ = params_.drift;
    float smoothedSelfModulation_ = params_.selfModulation;
    float smoothedFieldReturn_ = params_.fieldReturn;
    float smoothedPropagationMs_ = params_.propagationMs;
    float smoothedPickupFocus_ = params_.pickupFocus;
    float smoothedPickupAdapt_ = params_.pickupAdapt;
    float smoothedPickupAnchor_ = params_.pickupAnchor;
    float smoothedAuditoryPlasticity_ = params_.auditoryPlasticity;
    float smoothedMetabolism_ = params_.metabolism;
    float smoothedAdaptation_ = params_.adaptation;
    float smoothedGenomeMorph_ = params_.genomeMorph;
    float smoothedHeredity_ = params_.heredity;
    float smoothedPlasticity_ = params_.plasticity;
    float smoothedCenterAzimuth_ = params_.centerAzimuthDeg;
    float smoothedCenterElevation_ = params_.centerElevationDeg;
    float smoothedCenterDistance_ = params_.centerDistance;
    float smoothedFieldWidth_ = params_.fieldWidth;
    float smoothedCellWidth_ = params_.cellWidth;
    float smoothedMobility_ = params_.mobility;
    float smoothedSpatialInertia_ = params_.spatialInertia;
    float smoothedRotationRate_ = params_.rotationRateHz;
    float smoothedAir_ = params_.air;
    float smoothedDoppler_ = params_.doppler;
    float smoothedOutputGainDb_ = params_.outputGainDb;
};

} // namespace s3g
