#pragma once

#include "s3g_ambi_encoder_depth.h"
#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_math.h"
#include "s3g_neural_synthesis.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAmbiPulsarLanes = 3u;
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

struct AmbiPulsarLaneParams {
    float formantHz = 240.0f;
    float overlap = 0.72f;
    float level = 0.78f;
    float triggerOffset = 0.0f;
    AmbiPulsarWaveform waveform = AmbiPulsarWaveform::Sine;
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
    float laneDistribution = 0.08f;

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
};

inline float ambiPulsarWrapSignedDeg(float value)
{
    while (value > 180.0f) value -= 360.0f;
    while (value < -180.0f) value += 360.0f;
    return value;
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
    params.laneDistribution = clamp(params.laneDistribution, 0.0f, 1.0f);
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
    const uint32_t harmonics = std::clamp<uint32_t>(
        static_cast<uint32_t>(std::floor(0.48f / std::max(1.0e-6f, increment))), 1u, 256u);
    const float odd = static_cast<float>(harmonics * 2u + 1u);
    const float denominator = odd * std::sin(kPi * phase);
    if (std::fabs(denominator) < 1.0e-5f) return 1.0f;
    return std::sin(odd * kPi * phase) / denominator;
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
        basisCurrent_ = {};
        basisTarget_ = {};
        laneEnergy_.fill(0.0f);
        laneSamples_.fill(0.0f);
        lanePoint_ = {};
        dcInput_.fill(0.0f);
        dcOutput_.fill(0.0f);
        eventPhase_ = 0.0;
        emissionModPhase_ = 0.0;
        formantModPhase_ = 0.0;
        orbitPhase_ = 0.0;
        eventIndex_ = 0u;
        emittedEvents_ = 0u;
        randomState_ = params_.seed ? params_.seed : 1u;
        smoothedEmissionHz_ = params_.emissionHz;
        smoothedGain_ = dbToGain(params_.outputGainDb);
        pulsarFeedback_ = 0.0f;
        captureRequested_ = false;
        autoCapturePending_ = true;
        observedCapture_ = params_.neuralCapture;
        for (uint32_t lane = 0u; lane < kAmbiPulsarLanes; ++lane) {
            smoothedFormant_[lane] = params_.lanes[lane].formantHz;
        }
        depth_.reset();
        neuralNetwork_.setParams(params_.neural);
        neuralNetwork_.reset();
        neuralCapture_.reset();
        updateSpatialTargets();
        basisCurrent_ = basisTarget_;
    }

    void setParams(AmbiPulsarParams params)
    {
        const uint32_t previousSeed = params_.seed;
        const uint32_t previousCapture = params_.neuralCapture;
        params_ = sanitizeAmbiPulsarParams(params);
        depth_.setParams({ params_.doppler, params_.air });
        neuralNetwork_.setParams(params_.neural);
        if (params_.seed != previousSeed) randomState_ = params_.seed ? params_.seed : 1u;
        if (params_.neuralCapture != previousCapture || params_.neuralCapture != observedCapture_) {
            observedCapture_ = params_.neuralCapture;
            captureRequested_ = true;
        }
    }

    const AmbiPulsarParams& params() const { return params_; }
    uint64_t emittedEventCount() const { return emittedEvents_; }
    AmbiPulsarPoint lanePoint(uint32_t lane) const { return lanePoint_[std::min<uint32_t>(lane, kAmbiPulsarLanes - 1u)]; }
    float laneEnergy(uint32_t lane) const { return laneEnergy_[std::min<uint32_t>(lane, kAmbiPulsarLanes - 1u)]; }
    float neuralNode(uint32_t node) const
    {
        return neuralNetwork_.frame().nodes[std::min<uint32_t>(node, kNeuralSynthesisNodes - 1u)];
    }
    float neuralCluster(uint32_t cluster) const
    {
        return neuralNetwork_.frame().clusters[std::min<uint32_t>(cluster, kNeuralSynthesisClusters - 1u)];
    }
    uint32_t neuralCaptureGeneration() const { return neuralCapture_.generation(); }
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

    void process(float* const* outputs, uint32_t outputChannels, uint32_t frames)
    {
        if (!outputs || frames == 0u) return;
        outputChannels = std::min<uint32_t>(outputChannels, kAmbiPulsarMaxChannels);
        for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
            if (outputs[channel]) std::fill(outputs[channel], outputs[channel] + frames, 0.0f);
        }
        if (outputChannels == 0u) return;

        const uint32_t ambiChannels = std::min<uint32_t>(ambiChannelsForOrder(params_.order), outputChannels);
        constexpr uint32_t kSpatialFrames = 16u;
        const float parameterCoefficient = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.010));
        const float spatialCoefficient = lerp(0.22f, 0.0025f, params_.spatialFollow);
        const float dcCoefficient = std::exp(-2.0f * kPi * 8.0f / static_cast<float>(sampleRate_));

        for (uint32_t frame = 0u; frame < frames; ++frame) {
            const NeuralSynthesisFrame neuralFrame = neuralNetwork_.process(pulsarFeedback_);
            neuralCapture_.push(neuralFrame);
            if ((captureRequested_ && neuralCapture_.filled() >= 64u)
                || (autoCapturePending_ && neuralCapture_.ready())) {
                if (neuralCapture_.capture()) {
                    captureRequested_ = false;
                    autoCapturePending_ = false;
                }
            }

            smoothedEmissionHz_ += (params_.emissionHz - smoothedEmissionHz_) * parameterCoefficient;
            smoothedGain_ += (dbToGain(params_.outputGainDb) - smoothedGain_) * parameterCoefficient;
            float feedbackSum = 0.0f;
            for (uint32_t lane = 0u; lane < kAmbiPulsarLanes; ++lane) {
                smoothedFormant_[lane] += (params_.lanes[lane].formantHz - smoothedFormant_[lane]) * parameterCoefficient;
            }

            emissionModPhase_ = wrapPhase(emissionModPhase_ + params_.emissionModRateHz / sampleRate_);
            formantModPhase_ = wrapPhase(formantModPhase_ + params_.formantModRateHz / sampleRate_);
            orbitPhase_ = wrapPhase(orbitPhase_ + params_.orbitRateHz / sampleRate_);

            for (uint32_t lane = 0u; lane < kAmbiPulsarLanes; ++lane) {
                for (uint32_t slot = 0u; slot < kAmbiPulsarPendingPerLane; ++slot) {
                    if (!pending_[lane][slot]) continue;
                    pendingSamples_[lane][slot] -= 1.0;
                    if (pendingSamples_[lane][slot] <= 0.0) {
                        triggerLane(lane, static_cast<float>(-pendingSamples_[lane][slot]), pendingPeriodSamples_[lane][slot]);
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

            if ((frame % kSpatialFrames) == 0u) updateSpatialTargets();
            for (uint32_t lane = 0u; lane < kAmbiPulsarLanes; ++lane) {
                for (uint32_t channel = 0u; channel < ambiChannels; ++channel) {
                    basisCurrent_[lane][channel] += (basisTarget_[lane][channel] - basisCurrent_[lane][channel]) * spatialCoefficient;
                }

                float mono = renderLane(lane);
                const uint32_t cluster = std::min<uint32_t>(lane + 1u, kNeuralSynthesisClusters - 1u);
                const uint32_t node = cluster * kNeuralNodesPerCluster + ((lane * 2u + 1u) % kNeuralNodesPerCluster);
                const float neuralVoice = std::tanh(neuralFrame.clusters[cluster] * 0.72f
                    + neuralFrame.nodes[node] * 0.46f);
                mono += neuralVoice * params_.neuralLevel * 0.62f;
                const float hp = mono - dcInput_[lane] + dcCoefficient * dcOutput_[lane];
                dcInput_[lane] = mono;
                dcOutput_[lane] = flushDenormal(hp);
                feedbackSum += dcOutput_[lane];
                mono = depth_.process(lane, dcOutput_[lane], lanePoint_[lane].distance);
                mono *= 1.0f / std::max(0.35f, lanePoint_[lane].distance);
                if (!std::isfinite(mono)) mono = 0.0f;
                laneSamples_[lane] = mono;
                laneEnergy_[lane] += (std::fabs(mono) - laneEnergy_[lane]) * 0.025f;

                const float gain = smoothedGain_ * 0.577350269f;
                for (uint32_t channel = 0u; channel < ambiChannels; ++channel) {
                    if (outputs[channel]) {
                        outputs[channel][frame] = flushDenormal(outputs[channel][frame]
                            + mono * gain * basisCurrent_[lane][channel]);
                    }
                }
            }
            pulsarFeedback_ = std::tanh(feedbackSum * 0.42f);
            depth_.advance();
        }
    }

private:
    struct Grain {
        bool active = false;
        float ageSamples = 0.0f;
        float durationSamples = 1.0f;
        float phase = 0.0f;
        float phaseIncrement = 0.0f;
        float level = 0.0f;
        AmbiPulsarWaveform waveform = AmbiPulsarWaveform::Sine;
        uint32_t randomState = 1u;
        float triangleState = -1.0f;
        float oversampleState1 = 0.0f;
        float oversampleState2 = 0.0f;
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
        ++emittedEvents_;
        const float periodSamples = static_cast<float>(sampleRate_) / std::max(0.05f, effectiveEmission);
        const uint32_t distributedLane = static_cast<uint32_t>(index % kAmbiPulsarLanes);
        for (uint32_t lane = 0u; lane < kAmbiPulsarLanes; ++lane) {
            if (lane != distributedLane && randomUnit() < params_.laneDistribution) continue;
            const double delay = static_cast<double>(params_.lanes[lane].triggerOffset * periodSamples - ageSamples);
            if (delay <= 0.0) {
                triggerLane(lane, static_cast<float>(-delay), periodSamples);
            } else {
                uint32_t slot = 0u;
                while (slot < kAmbiPulsarPendingPerLane && pending_[lane][slot]) ++slot;
                if (slot < kAmbiPulsarPendingPerLane) {
                    pending_[lane][slot] = true;
                    pendingSamples_[lane][slot] = delay;
                    pendingPeriodSamples_[lane][slot] = periodSamples;
                } else {
                    // A very fast upward clock sweep can temporarily exceed the
                    // offset queue. Preserve the event instead of silently losing it.
                    triggerLane(lane, ageSamples, periodSamples);
                }
            }
        }
    }

    void triggerLane(uint32_t lane, float ageSamples, float periodSamples)
    {
        Grain* selected = nullptr;
        for (auto& grain : grains_[lane]) {
            if (!grain.active) {
                selected = &grain;
                break;
            }
        }
        if (!selected) {
            selected = &grains_[lane][0];
            for (auto& grain : grains_[lane]) {
                if (grain.ageSamples / std::max(1.0f, grain.durationSamples)
                    > selected->ageSamples / std::max(1.0f, selected->durationSamples)) selected = &grain;
            }
        }

        const auto& laneParams = params_.lanes[lane];
        const float duration = clamp(laneParams.overlap * periodSamples, 2.0f, static_cast<float>(sampleRate_ * 2.0));
        const float modPhase = static_cast<float>(formantModPhase_) * kAmbiPulsarTwoPi + static_cast<float>(lane) * 2.094395102f;
        const float semitones = std::sin(modPhase) * params_.formantModDepthSemitones
            + randomSigned() * params_.formantScatterSemitones;
        const float formant = clamp(smoothedFormant_[lane] * std::pow(2.0f, semitones / 12.0f), 12.0f,
            static_cast<float>(sampleRate_ * 0.48));

        selected->active = true;
        selected->ageSamples = std::max(0.0f, ageSamples);
        selected->durationSamples = duration;
        selected->phaseIncrement = formant / static_cast<float>(sampleRate_);
        selected->phase = std::fmod(selected->ageSamples * selected->phaseIncrement
            + randomSigned() * params_.phaseScatter, 1.0f);
        if (selected->phase < 0.0f) selected->phase += 1.0f;
        selected->level = laneParams.level;
        selected->waveform = laneParams.waveform;
        selected->randomState = randomState_ ^ (0x632be5abu * (lane + 1u)) ^ static_cast<uint32_t>(eventIndex_);
        selected->triangleState = 1.0f - 4.0f * std::fabs(selected->phase - 0.5f);
        selected->oversampleState1 = 0.0f;
        selected->oversampleState2 = 0.0f;

        const float scatter = params_.spatialScatter;
        laneAzimuthScatter_[lane] = randomSigned() * scatter * 48.0f;
        laneElevationScatter_[lane] = randomSigned() * scatter * 24.0f;
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
            if (increment * 2.0f < 0.48f) {
                value += 0.42f * std::sin(kAmbiPulsarTwoPi * phase * 2.0f);
                normalization += 0.42f;
            }
            if (increment * 3.0f < 0.48f) {
                value += 0.22f * std::sin(kAmbiPulsarTwoPi * phase * 3.0f);
                normalization += 0.22f;
            }
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
        return lerp(value, neuralCapture_.pulsaret(phase, increment), params_.neuralPulsaretMix);
    }

    float renderGrain(Grain& grain)
    {
        if (!grain.active) return 0.0f;
        if (grain.ageSamples >= grain.durationSamples) {
            grain.active = false;
            return 0.0f;
        }
        const float envelopePhase = grain.ageSamples / grain.durationSamples;
        const float analyticEnvelope = ambi_pulsar_detail::envelope(params_.envelope, envelopePhase, params_.envelopeEdge);
        const float env = lerp(analyticEnvelope, neuralCapture_.envelope(envelopePhase), params_.neuralEnvelopeMix);
        const float fm = neuralCapture_.fm(envelopePhase) * params_.neuralFmDepthSemitones;
        const float effectiveIncrement = clamp(grain.phaseIncrement * std::exp2(fm / 12.0f), 1.0e-7f, 0.48f);
        uint32_t oversampling = 1u;
        if (grain.waveform == AmbiPulsarWaveform::Fold) {
            if (params_.quality == AmbiPulsarQuality::Ultra) oversampling = 4u;
            else if (params_.quality == AmbiPulsarQuality::High) oversampling = 2u;
        }

        float sample = 0.0f;
        if (oversampling == 1u) {
            sample = waveformAt(grain, grain.phase + effectiveIncrement * 0.5f, effectiveIncrement);
        } else {
            // Two cascaded one-poles provide the reconstruction filtering that a
            // simple average of oversampled nonlinear samples would not.
            const float pole = std::exp(-kPi * 0.82f / static_cast<float>(oversampling));
            for (uint32_t sub = 0u; sub < oversampling; ++sub) {
                const float subPhase = grain.phase
                    + effectiveIncrement * (static_cast<float>(sub) + 0.5f) / static_cast<float>(oversampling);
                const float value = waveformAt(grain, subPhase, effectiveIncrement / static_cast<float>(oversampling));
                grain.oversampleState1 = value * (1.0f - pole) + grain.oversampleState1 * pole;
                grain.oversampleState2 = grain.oversampleState1 * (1.0f - pole) + grain.oversampleState2 * pole;
            }
            sample = grain.oversampleState2;
        }
        grain.phase += effectiveIncrement;
        grain.phase -= std::floor(grain.phase);
        grain.ageSamples += 1.0f;
        return sample * env * grain.level;
    }

    float renderLane(uint32_t lane)
    {
        float sample = 0.0f;
        uint32_t active = 0u;
        for (auto& grain : grains_[lane]) {
            if (!grain.active) continue;
            sample += renderGrain(grain);
            if (grain.active) ++active;
        }
        return sample / std::sqrt(static_cast<float>(std::max<uint32_t>(1u, active)));
    }

    void updateSpatialTargets()
    {
        const float orbitAngle = static_cast<float>(orbitPhase_) * kAmbiPulsarTwoPi;
        static constexpr std::array<float, kAmbiPulsarLanes> kLanePosition {{ -1.0f, 0.0f, 1.0f }};
        static constexpr std::array<float, kAmbiPulsarLanes> kLaneHeight {{ 0.42f, -0.24f, 0.34f }};
        for (uint32_t lane = 0u; lane < kAmbiPulsarLanes; ++lane) {
            const float lanePhase = orbitAngle + static_cast<float>(lane) * 2.094395102f;
            const float azimuth = params_.centerAzimuthDeg
                + kLanePosition[lane] * params_.spatialWidth * 105.0f
                + std::sin(lanePhase) * params_.orbitDepth * 72.0f
                + laneAzimuthScatter_[lane];
            const float elevation = params_.centerElevationDeg
                + kLaneHeight[lane] * params_.spatialWidth * 58.0f
                + std::sin(lanePhase * 0.73f + 0.8f) * params_.orbitDepth * 28.0f
                + laneElevationScatter_[lane];
            const float distance = clamp(params_.centerDistance
                + std::cos(lanePhase * 0.61f) * params_.orbitDepth * 0.24f
                + kLanePosition[lane] * params_.spatialWidth * 0.08f, 0.10f, 8.0f);
            lanePoint_[lane] = { ambiPulsarWrapSignedDeg(azimuth), clamp(elevation, -89.0f, 89.0f), distance };
            basisTarget_[lane] = acnSn3dBasis7(directionFromAed(lanePoint_[lane].azimuthDeg, lanePoint_[lane].elevationDeg));
        }
    }

    double sampleRate_ = 48000.0;
    AmbiPulsarParams params_ {};
    AmbiEncoderDepthProcessor<kAmbiPulsarLanes> depth_ {};
    NeuralSynthesisNetwork neuralNetwork_ {};
    NeuralWaveformCapture neuralCapture_ {};
    std::array<std::array<Grain, kAmbiPulsarMaxGrainsPerLane>, kAmbiPulsarLanes> grains_ {};
    std::array<std::array<bool, kAmbiPulsarPendingPerLane>, kAmbiPulsarLanes> pending_ {};
    std::array<std::array<double, kAmbiPulsarPendingPerLane>, kAmbiPulsarLanes> pendingSamples_ {};
    std::array<std::array<float, kAmbiPulsarPendingPerLane>, kAmbiPulsarLanes> pendingPeriodSamples_ {};
    std::array<float, kAmbiPulsarLanes> smoothedFormant_ {};
    std::array<float, kAmbiPulsarLanes> dcInput_ {};
    std::array<float, kAmbiPulsarLanes> dcOutput_ {};
    std::array<float, kAmbiPulsarLanes> laneSamples_ {};
    std::array<float, kAmbiPulsarLanes> laneEnergy_ {};
    std::array<float, kAmbiPulsarLanes> laneAzimuthScatter_ {};
    std::array<float, kAmbiPulsarLanes> laneElevationScatter_ {};
    std::array<AmbiPulsarPoint, kAmbiPulsarLanes> lanePoint_ {};
    std::array<std::array<float, kAmbiPulsarMaxChannels>, kAmbiPulsarLanes> basisCurrent_ {};
    std::array<std::array<float, kAmbiPulsarMaxChannels>, kAmbiPulsarLanes> basisTarget_ {};
    double eventPhase_ = 0.0;
    double emissionModPhase_ = 0.0;
    double formantModPhase_ = 0.0;
    double orbitPhase_ = 0.0;
    float smoothedEmissionHz_ = 18.0f;
    float smoothedGain_ = 0.25f;
    uint64_t eventIndex_ = 0u;
    uint64_t emittedEvents_ = 0u;
    uint32_t randomState_ = 1u;
    uint32_t observedCapture_ = 0u;
    float pulsarFeedback_ = 0.0f;
    bool captureRequested_ = false;
    bool autoCapturePending_ = true;
};

} // namespace s3g
