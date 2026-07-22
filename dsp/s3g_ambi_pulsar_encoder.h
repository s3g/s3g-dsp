#pragma once

#include "s3g_ambi_encoder_depth.h"
#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_math.h"
#include "s3g_neural_synthesis.h"
#include "s3g_realtime.h"
#include "s3g_topology.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAmbiPulsarLanes = 3u;
constexpr uint32_t kAmbiPulsarMinPoints = 4u;
constexpr uint32_t kAmbiPulsarMaxPoints = 32u;
constexpr uint32_t kAmbiPulsarMaxGrainsPerLane = 16u;
constexpr uint32_t kAmbiPulsarPendingPerLane = 8u;
constexpr uint32_t kAmbiPulsarMaxOrder = 7u;
constexpr uint32_t kAmbiPulsarMaxChannels = 64u;
constexpr float kAmbiPulsarTwoPi = 6.28318530717958647692f;

enum class AmbiPulsarWaveform : uint32_t {
    Sine = 0u,
    Triangle = 1u,
    Saw = 2u,
    Square = 3u,
    Overtone = 4u,
    Fold = 5u,
    Impulse = 6u,
    Noise = 7u,
};

enum class AmbiPulsarEnvelope : uint32_t {
    Hann = 0u,
    Tukey = 1u,
    Welch = 2u,
    Percussive = 3u,
    Reverse = 4u,
};

enum class AmbiPulsarQuality : uint32_t {
    Eco = 0u,
    High = 1u,
    Ultra = 2u,
};

enum class AmbiPulsarMotionMode : uint32_t {
    Free = 0u,
    Orbit = 1u,
    Sway = 2u,
    FigureEight = 3u,
    Forsy = 4u,
};

enum class AmbiPulsarTuneMode : uint32_t {
    Hertz = 0u,
    Ratio = 1u,
    Subharmonic = 2u,
};

enum class AmbiPulsarRetriggerMode : uint32_t {
    Retrigger = 0u,
    Free = 1u,
    IdleOnly = 2u,
};

struct AmbiPulsarLaneParams {
    float formantHz = 240.0f;
    float overlap = 0.72f;
    float level = 0.78f;
    float triggerOffset = 0.0f;
    AmbiPulsarWaveform waveform = AmbiPulsarWaveform::Sine;
};

struct AmbiPulsarAdvancedLaneParams {
    float carrierRatio = 4.0f;
    float fmRatio = 2.0f;
    float fmIndex = 0.0f;
    float windowSkew = 0.5f;
    AmbiPulsarTuneMode tuneMode = AmbiPulsarTuneMode::Hertz;
    AmbiPulsarRetriggerMode retriggerMode = AmbiPulsarRetriggerMode::Retrigger;
};

struct AmbiPulsarParams {
    uint32_t order = 3u;
    float emissionHz = 18.0f;
    float emissionModRateHz = 0.13f;
    float emissionModDepth = 0.12f;
    float formantModRateHz = 1.7f;
    float formantModDepthSemitones = 0.45f;
    float formantScatterSemitones = 0.22f;
    float phaseScatter = 0.04f;

    float probability = 0.92f;
    uint32_t burstOn = 8u;
    uint32_t burstOff = 0u;
    uint32_t sieveModulo = 1u;
    uint32_t sieveResidue = 0u;
    float pointRandomness = 0.08f;

    std::array<AmbiPulsarLaneParams, kAmbiPulsarLanes> lanes {{
        { 240.0f, 0.72f, 0.82f, 0.00f, AmbiPulsarWaveform::Sine },
        { 720.0f, 0.48f, 0.58f, 0.19f, AmbiPulsarWaveform::Overtone },
        { 1680.0f, 0.32f, 0.42f, 0.41f, AmbiPulsarWaveform::Triangle },
    }};

    AmbiPulsarEnvelope envelope = AmbiPulsarEnvelope::Hann;
    float envelopeEdge = 0.38f;
    AmbiPulsarQuality quality = AmbiPulsarQuality::High;

    float centerAzimuthDeg = 0.0f;
    float centerElevationDeg = 0.0f;
    float centerDistance = 1.0f;
    float spatialWidth = 0.54f;
    float spatialScatter = 0.08f;
    float orbitRateHz = 0.025f;
    float orbitDepth = 0.36f;
    float spatialFollow = 0.72f;
    float air = 0.10f;
    float doppler = 0.0f;
    float outputGainDb = -12.0f;
    uint32_t seed = 1u;

    float neuralLevel = 0.0f;
    NeuralSynthesisParams neural {};
    float neuralPulsaretMix = 0.0f;
    float neuralEnvelopeMix = 0.0f;
    float neuralFmDepthSemitones = 0.0f;
    uint32_t neuralCapture = 0u;
    uint32_t points = 6u;
    // Appended in state version 4 so version 1-3 raw parameter prefixes remain valid.
    std::array<AmbiPulsarAdvancedLaneParams, kAmbiPulsarLanes> advancedLanes {{
        { 4.0f, 2.0f, 0.0f, 0.5f, AmbiPulsarTuneMode::Hertz, AmbiPulsarRetriggerMode::Retrigger },
        { 7.0f, 3.0f, 0.0f, 0.5f, AmbiPulsarTuneMode::Hertz, AmbiPulsarRetriggerMode::Retrigger },
        { 11.0f, 5.0f, 0.0f, 0.5f, AmbiPulsarTuneMode::Hertz, AmbiPulsarRetriggerMode::Retrigger },
    }};
    // Appended in state version 5 so version 1-4 raw parameter prefixes remain valid.
    AmbiPulsarMotionMode motionMode = AmbiPulsarMotionMode::Free;
};

inline float ambiPulsarWrapSignedDeg(float value)
{
    while (value > 180.0f) value -= 360.0f;
    while (value < -180.0f) value += 360.0f;
    return value;
}

inline float ambiPulsarPalindromePhase(double phase)
{
    const double cycle = phase - std::floor(phase * 0.5) * 2.0;
    return static_cast<float>(cycle <= 1.0 ? cycle : 2.0 - cycle);
}

inline AmbiPulsarParams sanitizeAmbiPulsarParams(AmbiPulsarParams params)
{
    params.order = std::clamp<uint32_t>(params.order, 1u, kAmbiPulsarMaxOrder);
    params.emissionHz = clamp(params.emissionHz, 0.05f, 2000.0f);
    params.emissionModRateHz = clamp(params.emissionModRateHz, 0.001f, 80.0f);
    params.emissionModDepth = clamp(params.emissionModDepth, 0.0f, 0.95f);
    params.formantModRateHz = clamp(params.formantModRateHz, 0.001f, 200.0f);
    params.formantModDepthSemitones = clamp(params.formantModDepthSemitones, 0.0f, 24.0f);
    params.formantScatterSemitones = clamp(params.formantScatterSemitones, 0.0f, 24.0f);
    params.phaseScatter = clamp(params.phaseScatter, 0.0f, 1.0f);
    params.probability = clamp(params.probability, 0.0f, 1.0f);
    params.burstOn = std::clamp<uint32_t>(params.burstOn, 1u, 64u);
    params.burstOff = std::min<uint32_t>(params.burstOff, 64u);
    params.sieveModulo = std::clamp<uint32_t>(params.sieveModulo, 1u, 32u);
    params.sieveResidue = std::min<uint32_t>(params.sieveResidue, params.sieveModulo - 1u);
    params.pointRandomness = clamp(params.pointRandomness, 0.0f, 1.0f);
    for (auto& lane : params.lanes) {
        lane.formantHz = clamp(lane.formantHz, 12.0f, 22000.0f);
        lane.overlap = clamp(lane.overlap, 0.025f, 8.0f);
        lane.level = clamp(lane.level, 0.0f, 1.5f);
        lane.triggerOffset = clamp(lane.triggerOffset, 0.0f, 0.95f);
        lane.waveform = static_cast<AmbiPulsarWaveform>(std::min<uint32_t>(static_cast<uint32_t>(lane.waveform), 7u));
    }
    params.envelope = static_cast<AmbiPulsarEnvelope>(std::min<uint32_t>(static_cast<uint32_t>(params.envelope), 4u));
    params.envelopeEdge = clamp(params.envelopeEdge, 0.01f, 1.0f);
    params.quality = static_cast<AmbiPulsarQuality>(std::min<uint32_t>(static_cast<uint32_t>(params.quality), 2u));
    params.centerAzimuthDeg = ambiPulsarWrapSignedDeg(params.centerAzimuthDeg);
    params.centerElevationDeg = clamp(params.centerElevationDeg, -89.0f, 89.0f);
    params.centerDistance = clamp(params.centerDistance, 0.10f, 8.0f);
    params.spatialWidth = clamp(params.spatialWidth, 0.0f, 1.0f);
    params.spatialScatter = clamp(params.spatialScatter, 0.0f, 1.0f);
    params.orbitRateHz = clamp(params.orbitRateHz, -4.0f, 4.0f);
    params.orbitDepth = clamp(params.orbitDepth, 0.0f, 1.0f);
    params.spatialFollow = clamp(params.spatialFollow, 0.0f, 1.0f);
    params.air = clamp(params.air, 0.0f, 1.0f);
    params.doppler = clamp(params.doppler, 0.0f, 1.0f);
    params.outputGainDb = clamp(params.outputGainDb, -60.0f, 6.0f);
    params.neuralLevel = clamp(params.neuralLevel, 0.0f, 1.5f);
    params.neural = sanitizeNeuralSynthesisParams(params.neural);
    params.neuralPulsaretMix = clamp(params.neuralPulsaretMix, 0.0f, 1.0f);
    params.neuralEnvelopeMix = clamp(params.neuralEnvelopeMix, 0.0f, 1.0f);
    params.neuralFmDepthSemitones = clamp(params.neuralFmDepthSemitones, 0.0f, 24.0f);
    params.neuralCapture = std::min<uint32_t>(params.neuralCapture, 65535u);
    params.points = std::clamp<uint32_t>(params.points, kAmbiPulsarMinPoints, kAmbiPulsarMaxPoints);
    for (auto& lane : params.advancedLanes) {
        lane.carrierRatio = clamp(lane.carrierRatio, 0.125f, 128.0f);
        lane.fmRatio = clamp(lane.fmRatio, 0.125f, 64.0f);
        lane.fmIndex = clamp(lane.fmIndex, 0.0f, 20.0f);
        lane.windowSkew = clamp(lane.windowSkew, 0.02f, 0.98f);
        lane.tuneMode = static_cast<AmbiPulsarTuneMode>(
            std::min<uint32_t>(static_cast<uint32_t>(lane.tuneMode), 2u));
        lane.retriggerMode = static_cast<AmbiPulsarRetriggerMode>(
            std::min<uint32_t>(static_cast<uint32_t>(lane.retriggerMode), 2u));
    }
    params.motionMode = static_cast<AmbiPulsarMotionMode>(
        std::min<uint32_t>(static_cast<uint32_t>(params.motionMode), 4u));
    return params;
}

struct AmbiPulsarPoint {
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float distance = 1.0f;
};

namespace ambi_pulsar_detail {

inline float polyBlep(float phase, float increment)
{
    increment = clamp(increment, 1.0e-7f, 0.5f);
    if (phase < increment) {
        const float x = phase / increment;
        return x + x - x * x - 1.0f;
    }
    if (phase > 1.0f - increment) {
        const float x = (phase - 1.0f) / increment;
        return x * x + x + x + 1.0f;
    }
    return 0.0f;
}

inline float bandlimitedImpulse(float phase, float increment)
{
    const float available = clamp(0.48f / std::max(1.0e-6f, increment), 1.0f, 256.0f);
    const uint32_t lower = static_cast<uint32_t>(std::floor(available));
    const uint32_t upper = std::min<uint32_t>(lower + 1u, 256u);
    const auto kernel = [&](uint32_t harmonics) {
        const float odd = static_cast<float>(harmonics * 2u + 1u);
        const float denominator = odd * std::sin(kPi * phase);
        return std::fabs(denominator) < 1.0e-5f ? 1.0f
            : std::sin(odd * kPi * phase) / denominator;
    };
    return lerp(kernel(lower), kernel(upper), available - static_cast<float>(lower));
}

inline float envelope(AmbiPulsarEnvelope shape, float phase, float edge)
{
    phase = clamp(phase, 0.0f, 1.0f);
    switch (shape) {
    case AmbiPulsarEnvelope::Tukey: {
        const float halfEdge = std::max(0.005f, edge * 0.5f);
        if (phase < halfEdge) return 0.5f - 0.5f * std::cos(kPi * phase / halfEdge);
        if (phase > 1.0f - halfEdge) return 0.5f - 0.5f * std::cos(kPi * (1.0f - phase) / halfEdge);
        return 1.0f;
    }
    case AmbiPulsarEnvelope::Welch: {
        const float x = phase * 2.0f - 1.0f;
        return std::max(0.0f, 1.0f - x * x);
    }
    case AmbiPulsarEnvelope::Percussive: {
        const float attack = 1.0f - std::exp(-phase * 160.0f);
        return attack * std::exp(-phase * lerp(2.0f, 14.0f, edge));
    }
    case AmbiPulsarEnvelope::Reverse: {
        const float release = 1.0f - std::exp(-(1.0f - phase) * 160.0f);
        return release * std::exp(-(1.0f - phase) * lerp(2.0f, 14.0f, edge));
    }
    case AmbiPulsarEnvelope::Hann:
    default:
        return 0.5f - 0.5f * std::cos(kAmbiPulsarTwoPi * phase);
    }
}

} // namespace ambi_pulsar_detail

class AmbiPulsarEncoder {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        depth_.prepare(sampleRate_);
        neuralNetwork_.prepare(sampleRate_);
        neuralCapture_.prepare(sampleRate_);
        reset();
    }

    void reset()
    {
        for (auto& lane : grains_) for (auto& grain : lane) grain = {};
        for (auto& lane : pending_) lane.fill(false);
        for (auto& lane : pendingSamples_) lane.fill(0.0);
        for (auto& lane : pendingPeriodSamples_) lane.fill(1.0f);
        for (auto& lane : pendingPoint_) lane.fill(0u);
        basisCurrent_ = {};
        basisTarget_ = {};
        pointEnergy_.fill(0.0f);
        pointPulse_.fill(0.0f);
        pointSamples_.fill(0.0f);
        pointPosition_ = {};
        pointTarget_ = {};
        pointAzimuthScatter_.fill(0.0f);
        pointElevationScatter_.fill(0.0f);
        dcInput_.fill(0.0f);
        dcOutput_.fill(0.0f);
        freeCarrierPhase_.fill(0.0f);
        freeCarrierIncrement_.fill(0.0f);
        freeModPhase_.fill(0.0f);
        freeModIncrement_.fill(0.0f);
        lastCarrierHz_.fill(0.0f);
        laneNormalization_.fill(1.0f);
        pointActivation_.fill(0.0f);
        channelActivation_.fill(0.0f);
        eventPhase_ = 0.0;
        emissionModPhase_ = 0.0;
        formantModPhase_ = 0.0;
        orbitPhase_ = 0.0;
        forsyPhase_ = 0.0;
        eventIndex_ = 0u;
        emittedEvents_ = 0u;
        randomState_ = params_.seed ? params_.seed : 1u;
        motionRandomState_ = (params_.seed ^ 0x7f4a7c15u);
        if (motionRandomState_ == 0u) motionRandomState_ = 1u;
        smoothedEmissionHz_ = params_.emissionHz;
        smoothedGain_ = dbToGain(params_.outputGainDb);
        smoothedNeuralLevel_ = params_.neuralLevel;
        smoothedCenterAzimuth_ = params_.centerAzimuthDeg;
        smoothedCenterElevation_ = params_.centerElevationDeg;
        smoothedCenterDistance_ = params_.centerDistance;
        smoothedSpatialWidth_ = params_.spatialWidth;
        smoothedOrbitRate_ = params_.orbitRateHz;
        smoothedOrbitDepth_ = params_.orbitDepth;
        smoothedSpatialFollow_ = params_.spatialFollow;
        smoothedAir_ = params_.air;
        smoothedDoppler_ = params_.doppler;
        smoothedNeuralParams_ = params_.neural;
        startupGain_ = 0.0f;
        pulsarFeedback_ = 0.0f;
        captureRequested_ = false;
        autoCapturePending_ = true;
        observedCapture_ = params_.neuralCapture;
        for (uint32_t lane = 0u; lane < kAmbiPulsarLanes; ++lane) {
            smoothedFormant_[lane] = params_.lanes[lane].formantHz;
            freeCarrierIncrement_[lane] = clamp(params_.lanes[lane].formantHz
                / static_cast<float>(sampleRate_), 1.0e-7f, 0.48f);
            freeModIncrement_[lane] = clamp(params_.emissionHz * params_.advancedLanes[lane].fmRatio
                / static_cast<float>(sampleRate_), 1.0e-7f, 0.48f);
        }
        for (uint32_t point = 0u; point < kAmbiPulsarMaxPoints; ++point) {
            initializeFreeMotionPoint(point, params_.points);
        }
        depth_.reset();
        neuralNetwork_.setParams(params_.neural);
        neuralNetwork_.reset();
        neuralCapture_.reset();
        updateSpatialTargets(0.0f);
        pointPosition_ = pointTarget_;
        basisCurrent_ = basisTarget_;
        for (uint32_t point = 0u; point < params_.points; ++point) pointActivation_[point] = 1.0f;
        const uint32_t activeChannels = ambiChannelsForOrder(params_.order);
        for (uint32_t channel = 0u; channel < activeChannels; ++channel) channelActivation_[channel] = 1.0f;
    }

    void setParams(AmbiPulsarParams params)
    {
        const uint32_t previousSeed = params_.seed;
        const uint32_t previousCapture = params_.neuralCapture;
        const uint32_t previousPoints = params_.points;
        params_ = sanitizeAmbiPulsarParams(params);
        if (params_.seed != previousSeed) {
            randomState_ = params_.seed ? params_.seed : 1u;
            motionRandomState_ = params_.seed ^ 0x7f4a7c15u;
            if (motionRandomState_ == 0u) motionRandomState_ = 1u;
        }
        if (params_.points > previousPoints) {
            for (uint32_t point = previousPoints; point < params_.points; ++point) {
                initializeFreeMotionPoint(point, params_.points);
            }
        }
        if (params_.neuralCapture != previousCapture || params_.neuralCapture != observedCapture_) {
            observedCapture_ = params_.neuralCapture;
            captureRequested_ = true;
        }
    }

    const AmbiPulsarParams& params() const { return params_; }
    uint64_t emittedEventCount() const { return emittedEvents_; }
    uint32_t pointCount() const { return params_.points; }
    AmbiPulsarPoint point(uint32_t index) const
    {
        return pointPosition_[std::min<uint32_t>(index, kAmbiPulsarMaxPoints - 1u)];
    }
    float pointEnergy(uint32_t index) const
    {
        return pointEnergy_[std::min<uint32_t>(index, kAmbiPulsarMaxPoints - 1u)];
    }
    float pointPulse(uint32_t index) const
    {
        return pointPulse_[std::min<uint32_t>(index, kAmbiPulsarMaxPoints - 1u)];
    }
    float neuralNode(uint32_t node) const
    {
        return neuralNetwork_.frame().nodes[std::min<uint32_t>(node, kNeuralSynthesisNodes - 1u)];
    }
    float neuralCluster(uint32_t cluster) const
    {
        return neuralNetwork_.frame().clusters[std::min<uint32_t>(cluster, kNeuralSynthesisClusters - 1u)];
    }
    uint32_t neuralCaptureGeneration() const { return neuralCapture_.generation(); }
    float lastTriggeredCarrierHz(uint32_t lane) const
    {
        return lastCarrierHz_[std::min<uint32_t>(lane, kAmbiPulsarLanes - 1u)];
    }
    float neuralCaptureProgress() const
    {
        return static_cast<float>(neuralCapture_.filled()) / static_cast<float>(kNeuralCaptureSize);
    }

    uint32_t activeGrainCount(uint32_t lane) const
    {
        lane = std::min<uint32_t>(lane, kAmbiPulsarLanes - 1u);
        uint32_t count = 0u;
        for (const auto& grain : grains_[lane]) if (grain.active) ++count;
        return count;
    }

    uint32_t activeGrainCountAtPoint(uint32_t pointIndex, uint32_t lane) const
    {
        pointIndex = std::min<uint32_t>(pointIndex, kAmbiPulsarMaxPoints - 1u);
        lane = std::min<uint32_t>(lane, kAmbiPulsarLanes - 1u);
        uint32_t count = 0u;
        for (const auto& grain : grains_[lane]) {
            if (grain.active && grain.point == pointIndex) ++count;
        }
        return count;
    }

    void process(float* const* outputs, uint32_t outputChannels, uint32_t frames)
    {
        if (!outputs || frames == 0u) return;
        outputChannels = std::min<uint32_t>(outputChannels, kAmbiPulsarMaxChannels);
        for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
            if (outputs[channel]) std::fill(outputs[channel], outputs[channel] + frames, 0.0f);
        }
        if (outputChannels == 0u) return;

        const uint32_t desiredAmbiChannels = std::min<uint32_t>(ambiChannelsForOrder(params_.order), outputChannels);
        constexpr uint32_t kSpatialFrames = 16u;
        const float parameterCoefficient = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.010));
        const float deClickCoefficient = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.003));
        const float dcCoefficient = std::exp(-2.0f * kPi * 8.0f / static_cast<float>(sampleRate_));
        const float pointPulseDecay = std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.085));
        const float startupIncrement = 1.0f / std::max(1.0f, static_cast<float>(sampleRate_ * 0.010));

        for (uint32_t frame = 0u; frame < frames; ++frame) {
            smoothedNeuralParams_.drive += (params_.neural.drive - smoothedNeuralParams_.drive) * parameterCoefficient;
            smoothedNeuralParams_.feedback += (params_.neural.feedback - smoothedNeuralParams_.feedback) * parameterCoefficient;
            smoothedNeuralParams_.coupling += (params_.neural.coupling - smoothedNeuralParams_.coupling) * parameterCoefficient;
            smoothedNeuralParams_.hierarchy += (params_.neural.hierarchy - smoothedNeuralParams_.hierarchy) * parameterCoefficient;
            smoothedNeuralParams_.phaseShift += (params_.neural.phaseShift - smoothedNeuralParams_.phaseShift) * parameterCoefficient;
            smoothedNeuralParams_.brownian += (params_.neural.brownian - smoothedNeuralParams_.brownian) * parameterCoefficient;
            smoothedNeuralParams_.drift += (params_.neural.drift - smoothedNeuralParams_.drift) * parameterCoefficient;
            smoothedNeuralParams_.selfModulation += (params_.neural.selfModulation - smoothedNeuralParams_.selfModulation) * parameterCoefficient;
            smoothedNeuralParams_.audioFeedback += (params_.neural.audioFeedback - smoothedNeuralParams_.audioFeedback) * parameterCoefficient;
            smoothedNeuralParams_.seed = params_.neural.seed;
            neuralNetwork_.setParams(smoothedNeuralParams_);
            const NeuralSynthesisFrame neuralFrame = neuralNetwork_.process(pulsarFeedback_);
            neuralCapture_.push(neuralFrame);
            if ((captureRequested_ && neuralCapture_.ready())
                || (autoCapturePending_ && neuralCapture_.ready())) {
                if (neuralCapture_.capture()) {
                    captureRequested_ = false;
                    autoCapturePending_ = false;
                }
            }

            smoothedEmissionHz_ += (params_.emissionHz - smoothedEmissionHz_) * parameterCoefficient;
            smoothedGain_ += (dbToGain(params_.outputGainDb) - smoothedGain_) * parameterCoefficient;
            smoothedNeuralLevel_ += (params_.neuralLevel - smoothedNeuralLevel_) * parameterCoefficient;
            smoothedCenterAzimuth_ = ambiPulsarWrapSignedDeg(smoothedCenterAzimuth_
                + ambiPulsarWrapSignedDeg(params_.centerAzimuthDeg - smoothedCenterAzimuth_) * parameterCoefficient);
            smoothedCenterElevation_ += (params_.centerElevationDeg - smoothedCenterElevation_) * parameterCoefficient;
            smoothedCenterDistance_ += (params_.centerDistance - smoothedCenterDistance_) * parameterCoefficient;
            smoothedSpatialWidth_ += (params_.spatialWidth - smoothedSpatialWidth_) * parameterCoefficient;
            smoothedOrbitRate_ += (params_.orbitRateHz - smoothedOrbitRate_) * parameterCoefficient;
            smoothedOrbitDepth_ += (params_.orbitDepth - smoothedOrbitDepth_) * parameterCoefficient;
            smoothedSpatialFollow_ += (params_.spatialFollow - smoothedSpatialFollow_) * parameterCoefficient;
            smoothedAir_ += (params_.air - smoothedAir_) * parameterCoefficient;
            smoothedDoppler_ += (params_.doppler - smoothedDoppler_) * parameterCoefficient;
            depth_.setParams({ smoothedDoppler_, smoothedAir_ });
            startupGain_ = std::min(1.0f, startupGain_ + startupIncrement);
            for (uint32_t lane = 0u; lane < kAmbiPulsarLanes; ++lane) {
                smoothedFormant_[lane] += (params_.lanes[lane].formantHz - smoothedFormant_[lane]) * parameterCoefficient;
            }

            emissionModPhase_ = wrapPhase(emissionModPhase_ + params_.emissionModRateHz / sampleRate_);
            formantModPhase_ = wrapPhase(formantModPhase_ + params_.formantModRateHz / sampleRate_);
            orbitPhase_ = wrapPhase(orbitPhase_ + smoothedOrbitRate_ / sampleRate_);
            forsyPhase_ += smoothedOrbitRate_ / sampleRate_;
            if (forsyPhase_ >= 2.0) forsyPhase_ -= 2.0;
            else if (forsyPhase_ < 0.0) forsyPhase_ += 2.0;

            for (uint32_t lane = 0u; lane < kAmbiPulsarLanes; ++lane) {
                for (uint32_t slot = 0u; slot < kAmbiPulsarPendingPerLane; ++slot) {
                    if (!pending_[lane][slot]) continue;
                    pendingSamples_[lane][slot] -= 1.0;
                    if (pendingSamples_[lane][slot] <= 0.0) {
                        triggerLane(lane, pendingPoint_[lane][slot], static_cast<float>(-pendingSamples_[lane][slot]),
                            pendingPeriodSamples_[lane][slot]);
                        pending_[lane][slot] = false;
                    }
                }
            }

            const float emissionLfo = std::sin(static_cast<float>(emissionModPhase_) * kAmbiPulsarTwoPi);
            const float effectiveEmission = clamp(smoothedEmissionHz_ * (1.0f + params_.emissionModDepth * emissionLfo), 0.05f, 2000.0f);
            const double eventIncrement = static_cast<double>(effectiveEmission) / sampleRate_;
            eventPhase_ += eventIncrement;
            while (eventPhase_ >= 1.0) {
                eventPhase_ -= 1.0;
                const float ageSamples = static_cast<float>(eventPhase_ / std::max(1.0e-12, eventIncrement));
                scheduleEvent(effectiveEmission, ageSamples);
            }

            if ((frame % kSpatialFrames) == 0u) {
                updateSpatialTargets(static_cast<float>(kSpatialFrames / sampleRate_));
            }
            std::array<uint32_t, kAmbiPulsarLanes> activePerLane {};
            for (uint32_t lane = 0u; lane < kAmbiPulsarLanes; ++lane) {
                for (const auto& grain : grains_[lane]) if (grain.active) ++activePerLane[lane];
                const float targetNormalization = 1.0f
                    / std::sqrt(static_cast<float>(std::max<uint32_t>(1u, activePerLane[lane])));
                laneNormalization_[lane] += (targetNormalization - laneNormalization_[lane]) * deClickCoefficient;
            }
            float activePointWeight = 0.0f;
            for (uint32_t pointIndex = 0u; pointIndex < kAmbiPulsarMaxPoints; ++pointIndex) {
                const float target = pointIndex < params_.points ? 1.0f : 0.0f;
                pointActivation_[pointIndex] += (target - pointActivation_[pointIndex]) * deClickCoefficient;
                activePointWeight += pointActivation_[pointIndex];
            }
            for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
                const float target = channel < desiredAmbiChannels ? 1.0f : 0.0f;
                channelActivation_[channel] += (target - channelActivation_[channel]) * deClickCoefficient;
            }
            const float spatialCoefficient = lerp(0.22f, 0.0025f, smoothedSpatialFollow_);
            float feedbackSum = 0.0f;
            for (uint32_t pointIndex = 0u; pointIndex < kAmbiPulsarMaxPoints; ++pointIndex) {
                pointPosition_[pointIndex].azimuthDeg = ambiPulsarWrapSignedDeg(
                    pointPosition_[pointIndex].azimuthDeg
                    + ambiPulsarWrapSignedDeg(pointTarget_[pointIndex].azimuthDeg
                        - pointPosition_[pointIndex].azimuthDeg) * spatialCoefficient);
                pointPosition_[pointIndex].elevationDeg += (pointTarget_[pointIndex].elevationDeg
                    - pointPosition_[pointIndex].elevationDeg) * spatialCoefficient;
                pointPosition_[pointIndex].distance += (pointTarget_[pointIndex].distance
                    - pointPosition_[pointIndex].distance) * spatialCoefficient;
                for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
                    basisCurrent_[pointIndex][channel] += (basisTarget_[pointIndex][channel]
                        - basisCurrent_[pointIndex][channel]) * spatialCoefficient;
                }

                float mono = renderPoint(pointIndex);
                const uint32_t cluster = pointIndex % kNeuralSynthesisClusters;
                const uint32_t node = cluster * kNeuralNodesPerCluster + ((pointIndex * 3u + 1u) % kNeuralNodesPerCluster);
                const float neuralVoice = std::tanh(neuralFrame.clusters[cluster] * 0.72f
                    + neuralFrame.nodes[node] * 0.46f);
                mono += neuralVoice * smoothedNeuralLevel_ * 0.62f
                    / std::sqrt(std::max(1.0f, activePointWeight));
                mono *= pointActivation_[pointIndex];
                const float hp = mono - dcInput_[pointIndex] + dcCoefficient * dcOutput_[pointIndex];
                dcInput_[pointIndex] = mono;
                dcOutput_[pointIndex] = flushDenormal(hp);
                feedbackSum += dcOutput_[pointIndex];
                mono = depth_.process(pointIndex, dcOutput_[pointIndex], pointPosition_[pointIndex].distance);
                mono *= 1.0f / std::max(0.35f, pointPosition_[pointIndex].distance);
                if (!std::isfinite(mono)) mono = 0.0f;
                pointSamples_[pointIndex] = mono;
                pointEnergy_[pointIndex] += (std::fabs(mono) - pointEnergy_[pointIndex]) * 0.025f;

                for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
                    if (outputs[channel]) {
                        outputs[channel][frame] = flushDenormal(outputs[channel][frame]
                            + mono * smoothedGain_ * startupGain_ * channelActivation_[channel]
                                * basisCurrent_[pointIndex][channel]);
                    }
                }
            }
            for (float& pulse : pointPulse_) pulse *= pointPulseDecay;
            for (uint32_t lane = 0u; lane < kAmbiPulsarLanes; ++lane) {
                freeCarrierPhase_[lane] += freeCarrierIncrement_[lane];
                freeCarrierPhase_[lane] -= std::floor(freeCarrierPhase_[lane]);
                freeModPhase_[lane] += freeModIncrement_[lane];
                freeModPhase_[lane] -= std::floor(freeModPhase_[lane]);
            }
            pulsarFeedback_ = std::tanh(feedbackSum * 0.42f
                / std::sqrt(static_cast<float>(params_.points)));
            depth_.advance();
        }
    }

private:
    struct Grain {
        bool active = false;
        uint32_t point = 0u;
        float ageSamples = 0.0f;
        float durationSamples = 1.0f;
        float phase = 0.0f;
        float phaseIncrement = 0.0f;
        float modPhase = 0.0f;
        float modPhaseIncrement = 0.0f;
        float fmIndex = 0.0f;
        float level = 0.0f;
        AmbiPulsarWaveform waveform = AmbiPulsarWaveform::Sine;
        AmbiPulsarEnvelope envelope = AmbiPulsarEnvelope::Hann;
        AmbiPulsarQuality quality = AmbiPulsarQuality::High;
        float envelopeEdge = 0.38f;
        float windowSkew = 0.5f;
        float neuralPulsaretMix = 0.0f;
        float neuralEnvelopeMix = 0.0f;
        float neuralFmDepthSemitones = 0.0f;
        bool freeRunning = false;
        bool firstSample = true;
        uint32_t randomState = 1u;
        float triangleState = -1.0f;
        float oversampleState1 = 0.0f;
        float oversampleState2 = 0.0f;
        float lastOutput = 0.0f;
        float stealTail = 0.0f;
        uint32_t stealTailPoint = 0u;
        uint32_t stealTailRemaining = 0u;
    };

    static double wrapPhase(double phase)
    {
        phase -= std::floor(phase);
        return phase;
    }

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

    static float reflectMotion(float value, float minimum, float maximum)
    {
        if (maximum <= minimum) return minimum;
        if (!std::isfinite(value)) return (minimum + maximum) * 0.5f;
        const float span = maximum - minimum;
        float phase = std::fmod(value - minimum, span * 2.0f);
        if (phase < 0.0f) phase += span * 2.0f;
        if (phase > span) phase = span * 2.0f - phase;
        return minimum + phase;
    }

    static uint32_t motionRandomU32(uint32_t& state)
    {
        state ^= state << 13u;
        state ^= state >> 17u;
        state ^= state << 5u;
        return state;
    }

    static float motionRandomUnit(uint32_t& state)
    {
        return static_cast<float>(motionRandomU32(state) & 0x00ffffffu)
            / static_cast<float>(0x01000000u);
    }

    static float deterministicMotionSigned(uint32_t seed)
    {
        uint32_t state = seed == 0u ? 1u : seed;
        return motionRandomUnit(state) * 2.0f - 1.0f;
    }

    enum class MotionDistribution : uint32_t { Gaussian, Cauchy, Logistic };

    float motionRandomSigned(MotionDistribution distribution)
    {
        switch (distribution) {
        case MotionDistribution::Gaussian: {
            float sum = 0.0f;
            for (uint32_t index = 0u; index < 6u; ++index) sum += motionRandomUnit(motionRandomState_);
            return clamp((sum - 3.0f) * 0.78f, -1.0f, 1.0f);
        }
        case MotionDistribution::Cauchy: {
            const float u = clamp(motionRandomUnit(motionRandomState_), 0.001f, 0.999f);
            return clamp(std::tan(kPi * (u - 0.5f)) * 0.18f, -1.0f, 1.0f);
        }
        case MotionDistribution::Logistic:
        default: {
            const float u = clamp(motionRandomUnit(motionRandomState_), 0.001f, 0.999f);
            return clamp(std::log(u / (1.0f - u)) * 0.19f, -1.0f, 1.0f);
        }
        }
    }

    void initializeFreeMotionPoint(uint32_t point, uint32_t activePoints)
    {
        point = std::min<uint32_t>(point, kAmbiPulsarMaxPoints - 1u);
        const uint32_t surroundingPoints = std::max<uint32_t>(1u, activePoints - 1u);
        if (point == 0u) {
            freeMotionPosition_[point] = {};
        } else {
            const auto anchor = baseTopologyPoint(
                std::min<uint32_t>(point - 1u, surroundingPoints - 1u), surroundingPoints);
            freeMotionPosition_[point] = {
                static_cast<float>(anchor[0]), static_cast<float>(anchor[1]), static_cast<float>(anchor[2])
            };
        }
        freeMotionVelocity_[point] = {
            deterministicMotionSigned(point * 17u + 1u) * 0.035f,
            deterministicMotionSigned(point * 31u + 3u) * 0.035f,
            deterministicMotionSigned(point * 47u + 5u) * 0.035f
        };
    }

    void updateFreeMotion(float dt)
    {
        if (dt <= 0.0f || smoothedOrbitDepth_ <= 0.0001f
            || std::fabs(smoothedOrbitRate_) <= 0.00001f) return;
        const float rateHz = std::max(0.001f, std::fabs(smoothedOrbitRate_));
        const float velocityLimit = 0.10f + smoothedOrbitDepth_ * 0.72f;
        const float walkDrive = (0.35f + smoothedOrbitDepth_ * 1.85f)
            * (0.28f + std::sqrt(rateHz));
        const float travel = 0.80f + rateHz * 8.0f;
        for (uint32_t point = 0u; point < params_.points; ++point) {
            Vec3& velocity = freeMotionVelocity_[point];
            Vec3& position = freeMotionPosition_[point];
            velocity.x = reflectMotion(velocity.x
                    + motionRandomSigned(MotionDistribution::Cauchy) * walkDrive * dt,
                -velocityLimit, velocityLimit);
            velocity.y = reflectMotion(velocity.y
                    + motionRandomSigned(MotionDistribution::Logistic) * walkDrive * dt,
                -velocityLimit, velocityLimit);
            velocity.z = reflectMotion(velocity.z
                    + motionRandomSigned(MotionDistribution::Gaussian) * walkDrive * dt,
                -velocityLimit, velocityLimit);
            position.x = reflectMotion(position.x + velocity.x * dt * travel, -1.0f, 1.0f);
            position.y = reflectMotion(position.y + velocity.y * dt * travel, -1.0f, 1.0f);
            position.z = reflectMotion(position.z + velocity.z * dt * travel, -1.0f, 1.0f);
        }
    }

    bool eventMaskPasses(uint64_t index)
    {
        const uint32_t cycle = params_.burstOn + params_.burstOff;
        const bool burst = params_.burstOff == 0u || cycle == 0u || (index % cycle) < params_.burstOn;
        const bool sieve = params_.sieveModulo <= 1u
            || (index % params_.sieveModulo) == params_.sieveResidue;
        return burst && sieve && randomUnit() <= params_.probability;
    }

    void scheduleEvent(float effectiveEmission, float ageSamples)
    {
        const uint64_t index = eventIndex_++;
        if (!eventMaskPasses(index)) return;
        const uint64_t acceptedIndex = emittedEvents_;
        ++emittedEvents_;
        const float periodSamples = static_cast<float>(sampleRate_) / std::max(0.05f, effectiveEmission);
        uint32_t pointIndex = static_cast<uint32_t>(acceptedIndex % params_.points);
        if (randomUnit() < params_.pointRandomness) {
            pointIndex = std::min<uint32_t>(params_.points - 1u,
                static_cast<uint32_t>(randomUnit() * static_cast<float>(params_.points)));
        }
        pointPulse_[pointIndex] = 1.0f;
        pointAzimuthScatter_[pointIndex] = randomSigned() * params_.spatialScatter * 48.0f;
        pointElevationScatter_[pointIndex] = randomSigned() * params_.spatialScatter * 24.0f;
        for (uint32_t lane = 0u; lane < kAmbiPulsarLanes; ++lane) {
            const double delay = static_cast<double>(params_.lanes[lane].triggerOffset * periodSamples - ageSamples);
            if (delay <= 0.0) {
                triggerLane(lane, pointIndex, static_cast<float>(-delay), periodSamples);
            } else {
                uint32_t slot = 0u;
                while (slot < kAmbiPulsarPendingPerLane && pending_[lane][slot]) ++slot;
                if (slot < kAmbiPulsarPendingPerLane) {
                    pending_[lane][slot] = true;
                    pendingSamples_[lane][slot] = delay;
                    pendingPeriodSamples_[lane][slot] = periodSamples;
                    pendingPoint_[lane][slot] = pointIndex;
                } else {
                    // A very fast upward clock sweep can temporarily exceed the
                    // offset queue. Preserve the event instead of silently losing it.
                    triggerLane(lane, pointIndex, ageSamples, periodSamples);
                }
            }
        }
    }

    void triggerLane(uint32_t lane, uint32_t pointIndex, float ageSamples, float periodSamples)
    {
        const auto& advanced = params_.advancedLanes[lane];
        if (advanced.retriggerMode == AmbiPulsarRetriggerMode::IdleOnly) {
            for (auto& grain : grains_[lane]) {
                if (grain.active && grain.ageSamples >= grain.durationSamples) grain.active = false;
                if (grain.active) return;
            }
        }

        Grain* selected = nullptr;
        for (auto& grain : grains_[lane]) {
            if (!grain.active && grain.stealTailRemaining == 0u) {
                selected = &grain;
                break;
            }
        }
        if (!selected) {
            for (auto& grain : grains_[lane]) {
                if (!grain.active) continue;
                if (!selected || grain.ageSamples / std::max(1.0f, grain.durationSamples)
                    > selected->ageSamples / std::max(1.0f, selected->durationSamples)) selected = &grain;
            }
        }
        if (!selected) {
            selected = &grains_[lane][0];
            for (auto& grain : grains_[lane]) {
                if (grain.stealTailRemaining < selected->stealTailRemaining) selected = &grain;
            }
        }

        float stolenTail = selected->active ? selected->lastOutput : 0.0f;
        uint32_t stolenPoint = selected->active ? selected->point : selected->stealTailPoint;
        if (!selected->active && selected->stealTailRemaining > 0u) {
            constexpr float kTailSamples = 32.0f;
            const float progress = 1.0f - static_cast<float>(selected->stealTailRemaining) / kTailSamples;
            stolenTail = selected->stealTail * (0.5f + 0.5f * std::cos(kPi * progress));
        }

        const auto& laneParams = params_.lanes[lane];
        const float duration = clamp(laneParams.overlap * periodSamples, 2.0f, static_cast<float>(sampleRate_ * 2.0));
        const float modPhase = static_cast<float>(formantModPhase_) * kAmbiPulsarTwoPi + static_cast<float>(lane) * 2.094395102f;
        const float semitones = std::sin(modPhase) * params_.formantModDepthSemitones
            + randomSigned() * params_.formantScatterSemitones;
        const float schedulerHz = static_cast<float>(sampleRate_) / std::max(1.0f, periodSamples);
        float baseCarrierHz = smoothedFormant_[lane];
        if (advanced.tuneMode == AmbiPulsarTuneMode::Ratio) {
            baseCarrierHz = schedulerHz * advanced.carrierRatio;
        } else if (advanced.tuneMode == AmbiPulsarTuneMode::Subharmonic) {
            baseCarrierHz = schedulerHz / std::max(1.0f, advanced.carrierRatio);
        }
        const float formant = clamp(baseCarrierHz * std::pow(2.0f, semitones / 12.0f), 12.0f,
            static_cast<float>(sampleRate_ * 0.48));
        lastCarrierHz_[lane] = formant;

        selected->active = true;
        selected->point = pointIndex % params_.points;
        selected->ageSamples = std::max(0.0f, ageSamples);
        selected->durationSamples = duration;
        selected->phaseIncrement = formant / static_cast<float>(sampleRate_);
        selected->modPhaseIncrement = clamp(schedulerHz * advanced.fmRatio
            / static_cast<float>(sampleRate_), 1.0e-7f, 0.48f);
        selected->fmIndex = advanced.fmIndex;
        selected->freeRunning = advanced.retriggerMode == AmbiPulsarRetriggerMode::Free;
        const float phaseScatter = randomSigned() * params_.phaseScatter;
        selected->phase = std::fmod((selected->freeRunning ? 0.0f
            : selected->ageSamples * selected->phaseIncrement) + phaseScatter, 1.0f);
        if (selected->phase < 0.0f) selected->phase += 1.0f;
        selected->modPhase = std::fmod(selected->ageSamples * selected->modPhaseIncrement, 1.0f);
        selected->level = laneParams.level;
        selected->waveform = laneParams.waveform;
        selected->envelope = params_.envelope;
        selected->quality = params_.quality;
        selected->envelopeEdge = params_.envelopeEdge;
        selected->windowSkew = advanced.windowSkew;
        selected->neuralPulsaretMix = params_.neuralPulsaretMix;
        selected->neuralEnvelopeMix = params_.neuralEnvelopeMix;
        selected->neuralFmDepthSemitones = params_.neuralFmDepthSemitones;
        selected->firstSample = true;
        selected->randomState = randomState_ ^ (0x632be5abu * (lane + 1u))
            ^ (0x9e3779b9u * (selected->point + 1u)) ^ static_cast<uint32_t>(eventIndex_);
        float initialCarrierPhase = selected->phase;
        if (selected->freeRunning) initialCarrierPhase += freeCarrierPhase_[lane];
        initialCarrierPhase -= std::floor(initialCarrierPhase);
        selected->triangleState = 1.0f - 4.0f * std::fabs(initialCarrierPhase - 0.5f);
        selected->oversampleState1 = 0.0f;
        selected->oversampleState2 = 0.0f;
        selected->lastOutput = 0.0f;
        selected->stealTail = stolenTail;
        selected->stealTailPoint = stolenPoint;
        selected->stealTailRemaining = std::fabs(stolenTail) > 1.0e-7f ? 32u : 0u;
        if (selected->freeRunning) {
            freeCarrierIncrement_[lane] = selected->phaseIncrement;
            freeModIncrement_[lane] = selected->modPhaseIncrement;
        }
    }

    static float grainRandom(Grain& grain)
    {
        grain.randomState = grain.randomState * 1664525u + 1013904223u;
        return static_cast<float>((grain.randomState >> 8u) & 0x00ffffffu) / 8388608.0f - 1.0f;
    }

    float waveformAt(Grain& grain, float phase, float increment)
    {
        phase -= std::floor(phase);
        using namespace ambi_pulsar_detail;
        float value = 0.0f;
        switch (grain.waveform) {
        case AmbiPulsarWaveform::Triangle: {
            float square = phase < 0.5f ? 1.0f : -1.0f;
            square += polyBlep(phase, increment);
            float half = phase + 0.5f;
            if (half >= 1.0f) half -= 1.0f;
            square -= polyBlep(half, increment);
            grain.triangleState = clamp(grain.triangleState + square * increment * 4.0f, -1.02f, 1.02f);
            value = grain.triangleState;
            break;
        }
        case AmbiPulsarWaveform::Saw:
            value = (phase * 2.0f - 1.0f) - polyBlep(phase, increment);
            break;
        case AmbiPulsarWaveform::Square: {
            value = phase < 0.5f ? 1.0f : -1.0f;
            value += polyBlep(phase, increment);
            float half = phase + 0.5f;
            if (half >= 1.0f) half -= 1.0f;
            value -= polyBlep(half, increment);
            break;
        }
        case AmbiPulsarWaveform::Overtone:
        {
            value = std::sin(kAmbiPulsarTwoPi * phase);
            float normalization = 1.0f;
            const float second = clamp((0.48f - increment * 2.0f) / 0.04f, 0.0f, 1.0f);
            const float third = clamp((0.48f - increment * 3.0f) / 0.04f, 0.0f, 1.0f);
            value += second * 0.42f * std::sin(kAmbiPulsarTwoPi * phase * 2.0f);
            value += third * 0.22f * std::sin(kAmbiPulsarTwoPi * phase * 3.0f);
            normalization += second * 0.42f + third * 0.22f;
            value /= normalization;
            break;
        }
        case AmbiPulsarWaveform::Fold: {
            const float foldedInput = std::sin(kAmbiPulsarTwoPi * phase)
                + 0.34f * std::sin(kAmbiPulsarTwoPi * phase * 2.0f);
            value = std::tanh(foldedInput * 2.25f);
            break;
        }
        case AmbiPulsarWaveform::Impulse:
            value = bandlimitedImpulse(phase, increment);
            break;
        case AmbiPulsarWaveform::Noise:
            value = grainRandom(grain);
            break;
        case AmbiPulsarWaveform::Sine:
        default:
            value = std::sin(kAmbiPulsarTwoPi * phase);
            break;
        }
        return lerp(value, neuralCapture_.pulsaret(phase, increment), grain.neuralPulsaretMix);
    }

    float renderGrain(uint32_t lane, Grain& grain)
    {
        if (!grain.active) return 0.0f;
        if (grain.ageSamples >= grain.durationSamples) {
            grain.active = false;
            return 0.0f;
        }
        const float envelopePhase = grain.ageSamples / grain.durationSamples;
        const float skew = grain.windowSkew;
        const float warpedEnvelopePhase = envelopePhase < skew
            ? 0.5f * envelopePhase / skew
            : 0.5f + 0.5f * (envelopePhase - skew) / (1.0f - skew);
        const float analyticEnvelope = ambi_pulsar_detail::envelope(
            grain.envelope, warpedEnvelopePhase, grain.envelopeEdge);
        const float env = lerp(analyticEnvelope, neuralCapture_.envelope(warpedEnvelopePhase),
            grain.neuralEnvelopeMix);
        const float baseIncrement = grain.freeRunning ? freeCarrierIncrement_[lane] : grain.phaseIncrement;
        const float baseModIncrement = grain.freeRunning ? freeModIncrement_[lane] : grain.modPhaseIncrement;
        const float fm = neuralCapture_.fm(envelopePhase) * grain.neuralFmDepthSemitones;
        const float effectiveIncrement = clamp(baseIncrement * std::exp2(fm / 12.0f), 1.0e-7f, 0.48f);
        const float bandlimitIncrement = clamp(effectiveIncrement
            + grain.fmIndex * baseModIncrement, 1.0e-7f, 0.48f);
        uint32_t oversampling = 1u;
        if (grain.waveform == AmbiPulsarWaveform::Fold || grain.fmIndex > 1.0e-5f) {
            if (grain.quality == AmbiPulsarQuality::Ultra) oversampling = 4u;
            else if (grain.quality == AmbiPulsarQuality::High) oversampling = 2u;
        }

        const float carrierPhase = grain.freeRunning ? freeCarrierPhase_[lane] + grain.phase : grain.phase;
        const float modulatorPhase = grain.freeRunning ? freeModPhase_[lane] : grain.modPhase;
        const auto fmPhaseOffset = [&](float phase) {
            return grain.fmIndex * std::sin(kAmbiPulsarTwoPi * phase) / kAmbiPulsarTwoPi;
        };

        float sample = 0.0f;
        if (oversampling == 1u) {
            sample = waveformAt(grain, carrierPhase + effectiveIncrement * 0.5f
                + fmPhaseOffset(modulatorPhase + baseModIncrement * 0.5f), bandlimitIncrement);
        } else {
            // Two cascaded one-poles provide the reconstruction filtering that a
            // simple average of oversampled nonlinear samples would not.
            const float pole = std::exp(-kPi * 0.82f / static_cast<float>(oversampling));
            for (uint32_t sub = 0u; sub < oversampling; ++sub) {
                const float fraction = (static_cast<float>(sub) + 0.5f) / static_cast<float>(oversampling);
                const float subPhase = carrierPhase + effectiveIncrement * fraction
                    + fmPhaseOffset(modulatorPhase + baseModIncrement * fraction);
                const float value = waveformAt(grain, subPhase,
                    bandlimitIncrement / static_cast<float>(oversampling));
                grain.oversampleState1 = value * (1.0f - pole) + grain.oversampleState1 * pole;
                grain.oversampleState2 = grain.oversampleState1 * (1.0f - pole) + grain.oversampleState2 * pole;
            }
            sample = grain.oversampleState2;
        }
        if (!grain.freeRunning) {
            grain.phase += effectiveIncrement;
            grain.phase -= std::floor(grain.phase);
            grain.modPhase += grain.modPhaseIncrement;
            grain.modPhase -= std::floor(grain.modPhase);
        }
        float boundaryCoverage = std::min(1.0f, grain.durationSamples - grain.ageSamples);
        if (grain.firstSample) {
            boundaryCoverage *= clamp(grain.ageSamples, 0.0f, 1.0f);
            grain.firstSample = false;
        }
        constexpr float kSafetySamples = 16.0f;
        const auto smoothEdge = [](float value) {
            value = clamp(value, 0.0f, 1.0f);
            return value * value * (3.0f - 2.0f * value);
        };
        const float safety = smoothEdge(grain.ageSamples / kSafetySamples)
            * smoothEdge((grain.durationSamples - grain.ageSamples) / kSafetySamples);
        grain.ageSamples += 1.0f;
        const float output = sample * env * grain.level * clamp(boundaryCoverage, 0.0f, 1.0f) * safety;
        grain.lastOutput = output;
        return output;
    }

    float renderStealTail(Grain& grain)
    {
        if (grain.stealTailRemaining == 0u) return 0.0f;
        constexpr float kTailSamples = 32.0f;
        const float progress = 1.0f - static_cast<float>(grain.stealTailRemaining) / kTailSamples;
        const float output = grain.stealTail * (0.5f + 0.5f * std::cos(kPi * progress));
        --grain.stealTailRemaining;
        return output;
    }

    float renderPoint(uint32_t pointIndex)
    {
        float sample = 0.0f;
        for (uint32_t lane = 0u; lane < kAmbiPulsarLanes; ++lane) {
            float laneSample = 0.0f;
            for (auto& grain : grains_[lane]) {
                if (grain.active && grain.point == pointIndex) laneSample += renderGrain(lane, grain);
                if (grain.stealTailRemaining > 0u && grain.stealTailPoint == pointIndex) {
                    laneSample += renderStealTail(grain);
                }
            }
            sample += laneSample * laneNormalization_[lane];
        }
        return sample * 0.577350269f;
    }

    void updateSpatialTargets(float dt)
    {
        const bool stochasticTopology = params_.motionMode == AmbiPulsarMotionMode::Free
            || params_.motionMode == AmbiPulsarMotionMode::Forsy;
        if (stochasticTopology) updateFreeMotion(dt);
        const float orbitAngle = static_cast<float>(orbitPhase_) * kAmbiPulsarTwoPi;
        const float forsyTraversal = ambiPulsarPalindromePhase(forsyPhase_);
        const float forsyAngle = forsyTraversal * kAmbiPulsarTwoPi;
        TopologyControls forsyControls {};
        if (params_.motionMode == AmbiPulsarMotionMode::Forsy) {
            // Match the family FORSY configuration used by Stochastic and
            // Wrangler: shape 11 under FREE topology modulation. Unlike the
            // family's cyclic phase, this encoder traverses the path forward
            // and then backward so every endpoint is continuous. The bounded
            // secondary walk is layered below in the same way as Stochastic.
            TopologyState state {};
            state.shape = 11u;
            state.motionMode = 1u;
            state.motionRateHz = std::fabs(smoothedOrbitRate_);
            state.motionDepth = smoothedOrbitDepth_;
            state.motionPhase = forsyTraversal;
            state.amount = 0.82f;
            state.jitter = smoothedOrbitDepth_ * 0.24f;
            state.dirX = std::sin(forsyAngle);
            state.dirY = std::sin(forsyAngle * 1.37f + 1.1f);
            state.dirZ = std::cos(forsyAngle * 1.73f + 0.4f);
            state.flare = (1.20f - 1.0f) * 0.42f;
            forsyControls = topologyControlsFromState(state);
        }
        for (uint32_t pointIndex = 0u; pointIndex < params_.points; ++pointIndex) {
            if (stochasticTopology) {
                const uint32_t surroundingPoints = std::max<uint32_t>(1u, params_.points - 1u);
                std::array<double, 3> anchor {};
                if (pointIndex > 0u) {
                    if (params_.motionMode == AmbiPulsarMotionMode::Forsy) {
                        const TopologyPoint point = topologyPointForLane(
                            pointIndex - 1u, surroundingPoints, forsyControls);
                        anchor = { point.x * 1.20, point.y * 1.20, point.z * 1.20 };
                    } else {
                        anchor = baseTopologyPoint(pointIndex - 1u, surroundingPoints);
                    }
                }
                const Vec3 drift = freeMotionPosition_[pointIndex];
                const float depth = smoothedOrbitDepth_;
                Vec3 target {
                    static_cast<float>(anchor[0]) * smoothedSpatialWidth_ * (1.0f - depth * 0.32f)
                        + drift.x * depth * 0.88f,
                    static_cast<float>(anchor[1]) * smoothedSpatialWidth_ * (1.0f - depth * 0.32f)
                        + drift.y * depth * 0.88f,
                    static_cast<float>(anchor[2]) * smoothedSpatialWidth_ * (1.0f - depth * 0.32f)
                        + drift.z * depth * 0.88f
                };
                target.x = clamp(target.x, -1.0f, 1.0f);
                target.y = clamp(target.y, -1.0f, 1.0f);
                target.z = clamp(target.z, -1.0f, 1.0f);
                const float length = std::sqrt(target.x * target.x + target.y * target.y + target.z * target.z);
                Vec3 direction { 0.0f, 0.0f, 1.0f };
                if (length > 0.0001f) {
                    const float inverseLength = 1.0f / length;
                    direction = { target.x * inverseLength, target.y * inverseLength, target.z * inverseLength };
                }

                // FREE is a sphere-wide topology. Rotate the complete local
                // direction around the selected center instead of scaling AED
                // angles by Spatial Inertia (which confined the cloud to a
                // sector and pinched it at non-zero center elevations).
                const float centerAzimuth = smoothedCenterAzimuth_ * kPi / 180.0f;
                const float centerElevation = smoothedCenterElevation_ * kPi / 180.0f;
                const float pitchCosine = std::cos(centerElevation);
                const float pitchSine = std::sin(centerElevation);
                const float pitchedY = direction.y * pitchCosine + direction.z * pitchSine;
                const float pitchedZ = -direction.y * pitchSine + direction.z * pitchCosine;
                const float yawCosine = std::cos(centerAzimuth);
                const float yawSine = std::sin(centerAzimuth);
                Vec3 world {
                    direction.x * yawCosine + pitchedZ * yawSine,
                    pitchedY,
                    -direction.x * yawSine + pitchedZ * yawCosine
                };
                const float fullAzimuth = std::atan2(world.x, world.z) * 180.0f / kPi;
                const float fullElevation = std::asin(clamp(world.y, -1.0f, 1.0f)) * 180.0f / kPi;
                // Point 1 still grows smoothly out of the origin. All anchored
                // points use the complete sphere from their first frame.
                const float directionStrength = pointIndex == 0u
                    ? clamp(length * 1.25f, 0.0f, 1.0f) : 1.0f;
                const float azimuth = smoothedCenterAzimuth_
                    + ambiPulsarWrapSignedDeg(fullAzimuth - smoothedCenterAzimuth_) * directionStrength
                    + pointAzimuthScatter_[pointIndex];
                const float elevation = smoothedCenterElevation_
                    + (fullElevation - smoothedCenterElevation_) * directionStrength
                    + pointElevationScatter_[pointIndex];
                const float distance = clamp(smoothedCenterDistance_
                        * (1.0f + target.z * depth * 0.35f), 0.10f, 8.0f);
                AmbiPulsarPoint nextTarget {
                    ambiPulsarWrapSignedDeg(azimuth), clamp(elevation, -89.0f, 89.0f), distance
                };
                if (params_.motionMode == AmbiPulsarMotionMode::Forsy && dt > 0.0f) {
                    // topologyPointForLane can change its local basis abruptly
                    // near a pole. Keep that implementation detail from ever
                    // appearing as a point teleport in the encoder field.
                    const float maxAngularStep = (120.0f
                        + std::fabs(smoothedOrbitRate_) * 360.0f) * dt;
                    const float azimuthStep = clamp(ambiPulsarWrapSignedDeg(
                            nextTarget.azimuthDeg - pointTarget_[pointIndex].azimuthDeg),
                        -maxAngularStep, maxAngularStep);
                    nextTarget.azimuthDeg = ambiPulsarWrapSignedDeg(
                        pointTarget_[pointIndex].azimuthDeg + azimuthStep);
                    nextTarget.elevationDeg = clamp(pointTarget_[pointIndex].elevationDeg
                            + clamp(nextTarget.elevationDeg - pointTarget_[pointIndex].elevationDeg,
                                -maxAngularStep, maxAngularStep),
                        -89.0f, 89.0f);
                    const float maxDistanceStep = (0.40f
                        + std::fabs(smoothedOrbitRate_) * 1.60f) * dt;
                    nextTarget.distance = pointTarget_[pointIndex].distance
                        + clamp(nextTarget.distance - pointTarget_[pointIndex].distance,
                            -maxDistanceStep, maxDistanceStep);
                }
                pointTarget_[pointIndex] = nextTarget;
                basisTarget_[pointIndex] = acnSn3dBasis7(directionFromAed(
                    pointTarget_[pointIndex].azimuthDeg, pointTarget_[pointIndex].elevationDeg));
                continue;
            }
            // Start point 1 on the field origin rather than permanently on the
            // positive-azimuth edge. Motion modes then determine how the whole
            // formation leaves and returns to that origin.
            const float pointPhase = -0.5f * kPi + static_cast<float>(pointIndex) * kAmbiPulsarTwoPi
                / static_cast<float>(params_.points);
            const float relativePhase = pointPhase + 0.5f * kPi;
            const float movingPhase = pointPhase + orbitAngle;
            const float baseAzimuth = std::cos(pointPhase) * smoothedSpatialWidth_ * 105.0f;
            const float originElevation = std::sin(-kPi + 0.52f);
            const float baseElevation = (std::sin(pointPhase * 2.0f + 0.52f) - originElevation)
                * smoothedSpatialWidth_ * 42.0f;
            const float baseDistance = (std::sin(pointPhase) + 1.0f) * smoothedSpatialWidth_ * 0.10f;
            float motionAzimuth = 0.0f;
            float motionElevation = 0.0f;
            float motionDistance = 0.0f;
            switch (params_.motionMode) {
            case AmbiPulsarMotionMode::Sway: {
                const float triangle = (2.0f / kPi) * std::asin(std::sin(orbitAngle));
                motionAzimuth = triangle * smoothedOrbitDepth_ * 72.0f;
                motionElevation = std::sin(orbitAngle + relativePhase * 0.37f)
                    * smoothedOrbitDepth_ * 18.0f;
                motionDistance = std::sin(orbitAngle + relativePhase * 0.23f)
                    * smoothedOrbitDepth_ * 0.20f;
                break;
            }
            case AmbiPulsarMotionMode::FigureEight:
                motionAzimuth = std::sin(orbitAngle + relativePhase * 0.31f)
                    * smoothedOrbitDepth_ * 72.0f;
                motionElevation = std::sin(orbitAngle * 2.0f + relativePhase)
                    * smoothedOrbitDepth_ * 28.0f;
                motionDistance = std::sin(orbitAngle * 2.0f + relativePhase * 0.61f)
                    * smoothedOrbitDepth_ * 0.24f;
                break;
            case AmbiPulsarMotionMode::Orbit:
            default:
                motionAzimuth = (std::cos(movingPhase) - std::cos(pointPhase))
                    * smoothedSpatialWidth_ * smoothedOrbitDepth_ * 105.0f;
                motionElevation = (std::sin(movingPhase * 2.0f + 0.52f)
                    - std::sin(pointPhase * 2.0f + 0.52f))
                    * smoothedSpatialWidth_ * smoothedOrbitDepth_ * 42.0f;
                motionDistance = (std::cos(movingPhase) - std::cos(pointPhase))
                    * smoothedOrbitDepth_ * 0.24f;
                break;
            }
            const float azimuth = smoothedCenterAzimuth_
                + baseAzimuth + motionAzimuth
                + pointAzimuthScatter_[pointIndex];
            const float elevation = smoothedCenterElevation_
                + baseElevation + motionElevation
                + pointElevationScatter_[pointIndex];
            const float distance = clamp(smoothedCenterDistance_ + baseDistance + motionDistance, 0.10f, 8.0f);
            pointTarget_[pointIndex] = {
                ambiPulsarWrapSignedDeg(azimuth), clamp(elevation, -89.0f, 89.0f), distance
            };
            basisTarget_[pointIndex] = acnSn3dBasis7(directionFromAed(
                pointTarget_[pointIndex].azimuthDeg, pointTarget_[pointIndex].elevationDeg));
        }
    }

    double sampleRate_ = 48000.0;
    AmbiPulsarParams params_ {};
    AmbiEncoderDepthProcessor<kAmbiPulsarMaxPoints> depth_ {};
    NeuralSynthesisNetwork neuralNetwork_ {};
    NeuralWaveformCapture neuralCapture_ {};
    std::array<std::array<Grain, kAmbiPulsarMaxGrainsPerLane>, kAmbiPulsarLanes> grains_ {};
    std::array<std::array<bool, kAmbiPulsarPendingPerLane>, kAmbiPulsarLanes> pending_ {};
    std::array<std::array<double, kAmbiPulsarPendingPerLane>, kAmbiPulsarLanes> pendingSamples_ {};
    std::array<std::array<float, kAmbiPulsarPendingPerLane>, kAmbiPulsarLanes> pendingPeriodSamples_ {};
    std::array<std::array<uint32_t, kAmbiPulsarPendingPerLane>, kAmbiPulsarLanes> pendingPoint_ {};
    std::array<float, kAmbiPulsarLanes> smoothedFormant_ {};
    std::array<float, kAmbiPulsarLanes> freeCarrierPhase_ {};
    std::array<float, kAmbiPulsarLanes> freeCarrierIncrement_ {};
    std::array<float, kAmbiPulsarLanes> freeModPhase_ {};
    std::array<float, kAmbiPulsarLanes> freeModIncrement_ {};
    std::array<float, kAmbiPulsarLanes> lastCarrierHz_ {};
    std::array<float, kAmbiPulsarLanes> laneNormalization_ {};
    std::array<float, kAmbiPulsarMaxPoints> pointActivation_ {};
    std::array<float, kAmbiPulsarMaxChannels> channelActivation_ {};
    std::array<float, kAmbiPulsarMaxPoints> dcInput_ {};
    std::array<float, kAmbiPulsarMaxPoints> dcOutput_ {};
    std::array<float, kAmbiPulsarMaxPoints> pointSamples_ {};
    std::array<float, kAmbiPulsarMaxPoints> pointEnergy_ {};
    std::array<float, kAmbiPulsarMaxPoints> pointPulse_ {};
    std::array<float, kAmbiPulsarMaxPoints> pointAzimuthScatter_ {};
    std::array<float, kAmbiPulsarMaxPoints> pointElevationScatter_ {};
    std::array<Vec3, kAmbiPulsarMaxPoints> freeMotionVelocity_ {};
    std::array<Vec3, kAmbiPulsarMaxPoints> freeMotionPosition_ {};
    std::array<AmbiPulsarPoint, kAmbiPulsarMaxPoints> pointPosition_ {};
    std::array<AmbiPulsarPoint, kAmbiPulsarMaxPoints> pointTarget_ {};
    std::array<std::array<float, kAmbiPulsarMaxChannels>, kAmbiPulsarMaxPoints> basisCurrent_ {};
    std::array<std::array<float, kAmbiPulsarMaxChannels>, kAmbiPulsarMaxPoints> basisTarget_ {};
    double eventPhase_ = 0.0;
    double emissionModPhase_ = 0.0;
    double formantModPhase_ = 0.0;
    double orbitPhase_ = 0.0;
    double forsyPhase_ = 0.0;
    float smoothedEmissionHz_ = 18.0f;
    float smoothedGain_ = 0.25f;
    float smoothedNeuralLevel_ = 0.0f;
    float smoothedCenterAzimuth_ = 0.0f;
    float smoothedCenterElevation_ = 0.0f;
    float smoothedCenterDistance_ = 1.0f;
    float smoothedSpatialWidth_ = 0.54f;
    float smoothedOrbitRate_ = 0.025f;
    float smoothedOrbitDepth_ = 0.36f;
    float smoothedSpatialFollow_ = 0.72f;
    float smoothedAir_ = 0.10f;
    float smoothedDoppler_ = 0.0f;
    NeuralSynthesisParams smoothedNeuralParams_ {};
    float startupGain_ = 0.0f;
    uint64_t eventIndex_ = 0u;
    uint64_t emittedEvents_ = 0u;
    uint32_t randomState_ = 1u;
    uint32_t motionRandomState_ = 0x7f4a7c15u;
    uint32_t observedCapture_ = 0u;
    float pulsarFeedback_ = 0.0f;
    bool captureRequested_ = false;
    bool autoCapturePending_ = true;
};

} // namespace s3g
