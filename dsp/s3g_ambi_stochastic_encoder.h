#pragma once

#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAmbiStochasticMaxVoices = 64;
constexpr uint32_t kAmbiStochasticMaxOrder = 7;
constexpr uint32_t kAmbiStochasticMaxChannels = 64;
constexpr uint32_t kAmbiStochasticMaxBreakpoints = 32;
constexpr uint32_t kAmbiStochasticTableSize = 512;
constexpr uint32_t kAmbiStochasticCascadeSlots = 64;

enum class AmbiStochasticMode : uint32_t {
    Free = 0,
    Midi = 1,
    Both = 2,
};

enum class AmbiStochasticSystem : uint32_t {
    Independent = 0,
    Neighbor = 1,
    Field = 2,
    Network = 3,
};

enum class AmbiStochasticModel : uint32_t {
    Direct = 0,
    Delta = 1,
    Curved = 2,
    FreePeriod = 3,
};

enum class AmbiStochasticDistribution : uint32_t {
    Uniform = 0,
    Gaussian = 1,
    Cauchy = 2,
    Logistic = 3,
    Arcsine = 4,
    Exponential = 5,
    Binary = 6,
};

enum class AmbiStochasticMotion : uint32_t {
    Field = 0,
    Orbit = 1,
    Drift = 2,
    Feedback = 3,
};

enum class AmbiStochasticDynamics : uint32_t {
    Off = 0,
    Gas = 1,
    Net = 2,
    Cascade = 3,
};

struct AmbiStochasticParams {
    uint32_t order = 3;
    uint32_t voices = 12;
    AmbiStochasticMode mode = AmbiStochasticMode::Free;
    AmbiStochasticSystem system = AmbiStochasticSystem::Network;
    AmbiStochasticModel model = AmbiStochasticModel::FreePeriod;
    AmbiStochasticDistribution amplitudeDistribution = AmbiStochasticDistribution::Cauchy;
    AmbiStochasticDistribution durationDistribution = AmbiStochasticDistribution::Logistic;
    float baseNote = 40.0f;
    float pitchSpreadSemitones = 19.0f;
    float detuneCents = 9.0f;
    uint32_t breakpoints = 16;
    float amplitudeStep = 0.58f;
    float timeStep = 0.52f;
    float inertia = 0.72f;
    float activity = 0.74f;
    float coupling = 0.58f;
    float memory = 0.74f;
    float reactivity = 0.64f;
    float attackMs = 80.0f;
    float decayMs = 480.0f;
    float sustain = 0.72f;
    float releaseMs = 1800.0f;
    AmbiStochasticMotion motion = AmbiStochasticMotion::Feedback;
    float motionRateHz = 0.028f;
    float motionAmount = 0.72f;
    float motionSpread = 0.82f;
    float centerAzimuthDeg = 0.0f;
    float centerElevationDeg = 0.0f;
    float centerDistance = 1.0f;
    AmbiStochasticDynamics dynamics = AmbiStochasticDynamics::Cascade;
    float dynamicsDrive = 0.72f;
    float dynamicsBounce = 0.88f;
    float dynamicsDrag = 0.16f;
    float dynamicsRadius = 0.42f;
    float synthesisDepth = 0.86f;
    float spatialDepth = 0.68f;
    float outputGainDb = -24.0f;
};

struct AmbiStochasticPoint {
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float distance = 1.0f;
};

enum class AmbiStochasticEnvelopeStage : uint8_t {
    Idle = 0,
    Attack,
    Decay,
    Sustain,
    Release,
};

struct AmbiStochasticEnvelope {
    AmbiStochasticEnvelopeStage stage = AmbiStochasticEnvelopeStage::Idle;
    float value = 0.0f;
    bool gate = false;

    void setGate(bool next)
    {
        if (next && !gate) stage = AmbiStochasticEnvelopeStage::Attack;
        else if (!next && gate && stage != AmbiStochasticEnvelopeStage::Idle) {
            stage = AmbiStochasticEnvelopeStage::Release;
        }
        gate = next;
    }

    void trigger()
    {
        gate = true;
        stage = AmbiStochasticEnvelopeStage::Attack;
    }

    float process(float attackCoef, float decayCoef, float sustain, float releaseCoef)
    {
        switch (stage) {
        case AmbiStochasticEnvelopeStage::Attack:
            value += (1.0f - value) * attackCoef;
            if (value >= 0.999f) {
                value = 1.0f;
                stage = AmbiStochasticEnvelopeStage::Decay;
            }
            break;
        case AmbiStochasticEnvelopeStage::Decay:
            value += (sustain - value) * decayCoef;
            if (std::fabs(value - sustain) < 0.0005f) {
                value = sustain;
                stage = AmbiStochasticEnvelopeStage::Sustain;
            }
            break;
        case AmbiStochasticEnvelopeStage::Sustain:
            value = sustain;
            if (!gate) stage = AmbiStochasticEnvelopeStage::Release;
            break;
        case AmbiStochasticEnvelopeStage::Release:
            value += (0.0f - value) * releaseCoef;
            if (value < 0.00005f) {
                value = 0.0f;
                stage = AmbiStochasticEnvelopeStage::Idle;
            }
            break;
        case AmbiStochasticEnvelopeStage::Idle:
        default:
            value = 0.0f;
            break;
        }
        return value;
    }
};

struct AmbiStochasticVoice {
    std::array<float, kAmbiStochasticMaxBreakpoints> amplitudes {};
    std::array<float, kAmbiStochasticMaxBreakpoints> durations {};
    std::array<float, kAmbiStochasticMaxBreakpoints> amplitudeVelocity {};
    std::array<float, kAmbiStochasticMaxBreakpoints> durationVelocity {};
    std::array<float, kAmbiStochasticTableSize> currentTable {};
    std::array<float, kAmbiStochasticTableSize> nextTable {};
    AmbiStochasticEnvelope envelope {};
    uint32_t seed = 1u;
    uint32_t renderedBreakpoints = 12u;
    float phase = 0.0f;
    float frequency = 55.0f;
    float energy = 0.20f;
    float activityOffset = 0.0f;
    float velocity = 0.72f;
    float age = 0.0f;
    float periodRatio = 1.0f;
    int note = 48;
    bool midiGate = false;
    bool midiRole = false;
};

inline const char* ambiStochasticModeName(AmbiStochasticMode mode)
{
    switch (mode) {
    case AmbiStochasticMode::Midi: return "MIDI";
    case AmbiStochasticMode::Both: return "BOTH";
    case AmbiStochasticMode::Free:
    default: return "FREE";
    }
}

inline const char* ambiStochasticSystemName(AmbiStochasticSystem system)
{
    switch (system) {
    case AmbiStochasticSystem::Neighbor: return "NEIGHBOR";
    case AmbiStochasticSystem::Field: return "FIELD";
    case AmbiStochasticSystem::Network: return "NETWORK";
    case AmbiStochasticSystem::Independent:
    default: return "INDEPENDENT";
    }
}

inline const char* ambiStochasticModelName(AmbiStochasticModel model)
{
    switch (model) {
    case AmbiStochasticModel::Delta: return "DELTA";
    case AmbiStochasticModel::Curved: return "CURVED";
    case AmbiStochasticModel::FreePeriod: return "FREE PERIOD";
    case AmbiStochasticModel::Direct:
    default: return "DIRECT";
    }
}

inline const char* ambiStochasticDistributionName(AmbiStochasticDistribution distribution)
{
    switch (distribution) {
    case AmbiStochasticDistribution::Gaussian: return "GAUSS";
    case AmbiStochasticDistribution::Cauchy: return "CAUCHY";
    case AmbiStochasticDistribution::Logistic: return "LOGISTIC";
    case AmbiStochasticDistribution::Arcsine: return "ARCSINE";
    case AmbiStochasticDistribution::Exponential: return "EXPON";
    case AmbiStochasticDistribution::Binary: return "BINARY";
    case AmbiStochasticDistribution::Uniform:
    default: return "UNIFORM";
    }
}

inline const char* ambiStochasticMotionName(AmbiStochasticMotion motion)
{
    switch (motion) {
    case AmbiStochasticMotion::Orbit: return "ORBIT";
    case AmbiStochasticMotion::Drift: return "DRIFT";
    case AmbiStochasticMotion::Feedback: return "FEEDBACK";
    case AmbiStochasticMotion::Field:
    default: return "FIELD";
    }
}

inline const char* ambiStochasticDynamicsName(AmbiStochasticDynamics dynamics)
{
    switch (dynamics) {
    case AmbiStochasticDynamics::Gas: return "GAS";
    case AmbiStochasticDynamics::Net: return "NET";
    case AmbiStochasticDynamics::Cascade: return "CASCADE";
    case AmbiStochasticDynamics::Off:
    default: return "OFF";
    }
}

class AmbiStochasticEncoder {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        reset();
    }

    void reset()
    {
        motionPhase_ = 0.0f;
        activityPhase_ = 0.0f;
        dynamicsAccumulator_ = 0.0f;
        dynamicsTick_ = 0u;
        cascadeCursor_ = 0u;
        dynamicsSeed_ = 0x7f4a7c15u;
        globalEnergy_ = 0.20f;
        globalKinetic_ = 0.0f;
        for (auto& lane : cascadeDelay_) lane.fill(0.0f);
        for (uint32_t i = 0; i < kAmbiStochasticMaxVoices; ++i) {
            initializeVoice(i);
            points_[i] = basePoint(i);
            neighborIndex_[i] = (i + 1u) % kAmbiStochasticMaxVoices;
            secondaryNeighborIndex_[i] = (i + 2u) % kAmbiStochasticMaxVoices;
        }
        updateTopologyAnchors();
        for (uint32_t i = 0; i < kAmbiStochasticMaxVoices; ++i) {
            initializeDynamicsVoice(i);
        }
        updateNeighborGraph();
    }

    void setParams(AmbiStochasticParams params)
    {
        params.order = std::clamp<uint32_t>(params.order, 1u, kAmbiStochasticMaxOrder);
        params.voices = std::clamp<uint32_t>(params.voices, 1u, kAmbiStochasticMaxVoices);
        params.mode = static_cast<AmbiStochasticMode>(std::clamp<uint32_t>(static_cast<uint32_t>(params.mode), 0u, 2u));
        params.system = static_cast<AmbiStochasticSystem>(std::clamp<uint32_t>(static_cast<uint32_t>(params.system), 0u, 3u));
        params.model = static_cast<AmbiStochasticModel>(std::clamp<uint32_t>(static_cast<uint32_t>(params.model), 0u, 3u));
        params.amplitudeDistribution = static_cast<AmbiStochasticDistribution>(
            std::clamp<uint32_t>(static_cast<uint32_t>(params.amplitudeDistribution), 0u, 6u));
        params.durationDistribution = static_cast<AmbiStochasticDistribution>(
            std::clamp<uint32_t>(static_cast<uint32_t>(params.durationDistribution), 0u, 6u));
        params.baseNote = clamp(params.baseNote, 12.0f, 96.0f);
        params.pitchSpreadSemitones = clamp(params.pitchSpreadSemitones, 0.0f, 48.0f);
        params.detuneCents = clamp(params.detuneCents, 0.0f, 100.0f);
        params.breakpoints = std::clamp<uint32_t>(params.breakpoints, 4u, kAmbiStochasticMaxBreakpoints);
        params.amplitudeStep = clamp(params.amplitudeStep, 0.0f, 1.0f);
        params.timeStep = clamp(params.timeStep, 0.0f, 1.0f);
        params.inertia = clamp(params.inertia, 0.0f, 1.0f);
        params.activity = clamp(params.activity, 0.0f, 1.0f);
        params.coupling = clamp(params.coupling, 0.0f, 1.0f);
        params.memory = clamp(params.memory, 0.0f, 1.0f);
        params.reactivity = clamp(params.reactivity, 0.0f, 1.0f);
        params.attackMs = clamp(params.attackMs, 1.0f, 4000.0f);
        params.decayMs = clamp(params.decayMs, 5.0f, 8000.0f);
        params.sustain = clamp(params.sustain, 0.0f, 1.0f);
        params.releaseMs = clamp(params.releaseMs, 5.0f, 12000.0f);
        params.motion = static_cast<AmbiStochasticMotion>(std::clamp<uint32_t>(static_cast<uint32_t>(params.motion), 0u, 3u));
        params.motionRateHz = clamp(params.motionRateHz, 0.001f, 1.0f);
        params.motionAmount = clamp(params.motionAmount, 0.0f, 1.0f);
        params.motionSpread = clamp(params.motionSpread, 0.0f, 1.0f);
        params.centerAzimuthDeg = wrapSignedDeg(params.centerAzimuthDeg);
        params.centerElevationDeg = clamp(params.centerElevationDeg, -90.0f, 90.0f);
        params.centerDistance = clamp(params.centerDistance, 0.15f, 2.0f);
        params.dynamics = static_cast<AmbiStochasticDynamics>(
            std::clamp<uint32_t>(static_cast<uint32_t>(params.dynamics), 0u, 3u));
        params.dynamicsDrive = clamp(params.dynamicsDrive, 0.0f, 1.0f);
        params.dynamicsBounce = clamp(params.dynamicsBounce, 0.0f, 1.0f);
        params.dynamicsDrag = clamp(params.dynamicsDrag, 0.0f, 1.0f);
        params.dynamicsRadius = clamp(params.dynamicsRadius, 0.0f, 1.0f);
        params.synthesisDepth = clamp(params.synthesisDepth, 0.0f, 1.0f);
        params.spatialDepth = clamp(params.spatialDepth, 0.0f, 1.0f);
        params.outputGainDb = clamp(params.outputGainDb, -60.0f, 6.0f);
        const uint32_t previousVoices = params_.voices;
        params_ = params;
        if (params_.voices != previousVoices) updateTopologyAnchors();
        if (params_.voices > previousVoices) {
            for (uint32_t i = previousVoices; i < params_.voices; ++i) {
                points_[i] = basePoint(i);
                initializeDynamicsVoice(i);
            }
        }
        if (params_.voices != previousVoices) updateNeighborGraph();
    }

    AmbiStochasticParams params() const { return params_; }
    const std::array<AmbiStochasticPoint, kAmbiStochasticMaxVoices>& points() const { return points_; }
    Vec3 topologyPosition(uint32_t voice) const
    {
        const uint32_t index = std::min<uint32_t>(voice, kAmbiStochasticMaxVoices - 1u);
        const float boundary = topologyBoundary();
        return {
            clamp(dynamicsPosition_[index].x / boundary, -1.0f, 1.0f),
            clamp(dynamicsPosition_[index].y / boundary, -1.0f, 1.0f),
            clamp(dynamicsPosition_[index].z / boundary, -1.0f, 1.0f)
        };
    }
    const std::array<uint32_t, kAmbiStochasticMaxVoices>& neighborIndices() const { return neighborIndex_; }
    const std::array<uint32_t, kAmbiStochasticMaxVoices>& secondaryNeighborIndices() const { return secondaryNeighborIndex_; }
    float globalEnergy() const { return globalEnergy_; }
    float globalKinetic() const { return globalKinetic_; }
    float voiceEnergy(uint32_t voice) const
    {
        return voices_[std::min<uint32_t>(voice, kAmbiStochasticMaxVoices - 1u)].energy;
    }
    float voicePeriodRatio(uint32_t voice) const
    {
        return voices_[std::min<uint32_t>(voice, kAmbiStochasticMaxVoices - 1u)].periodRatio;
    }
    float voiceKinetic(uint32_t voice) const
    {
        return kinetic_[std::min<uint32_t>(voice, kAmbiStochasticMaxVoices - 1u)];
    }
    float voiceContact(uint32_t voice) const
    {
        return contact_[std::min<uint32_t>(voice, kAmbiStochasticMaxVoices - 1u)];
    }
    float voiceCrowding(uint32_t voice) const
    {
        return crowding_[std::min<uint32_t>(voice, kAmbiStochasticMaxVoices - 1u)];
    }
    float voiceTension(uint32_t voice) const
    {
        return tension_[std::min<uint32_t>(voice, kAmbiStochasticMaxVoices - 1u)];
    }
    float voiceNetworkPulse(uint32_t voice) const
    {
        return networkPulse_[std::min<uint32_t>(voice, kAmbiStochasticMaxVoices - 1u)];
    }
    float voiceBondStrength(uint32_t voice) const
    {
        return bondBreak_[std::min<uint32_t>(voice, kAmbiStochasticMaxVoices - 1u)] > 0.0f ? 0.0f : 1.0f;
    }
    const std::array<float, kAmbiStochasticTableSize>& waveform(uint32_t voice) const
    {
        return voices_[std::min<uint32_t>(voice, kAmbiStochasticMaxVoices - 1u)].nextTable;
    }
    uint32_t breakpointCount(uint32_t voice) const
    {
        return voices_[std::min<uint32_t>(voice, kAmbiStochasticMaxVoices - 1u)].renderedBreakpoints;
    }
    float breakpointAmplitude(uint32_t voice, uint32_t point) const
    {
        const auto& state = voices_[std::min<uint32_t>(voice, kAmbiStochasticMaxVoices - 1u)];
        return state.amplitudes[std::min<uint32_t>(point, kAmbiStochasticMaxBreakpoints - 1u)];
    }
    float breakpointDuration(uint32_t voice, uint32_t point) const
    {
        const auto& state = voices_[std::min<uint32_t>(voice, kAmbiStochasticMaxVoices - 1u)];
        return state.durations[std::min<uint32_t>(point, kAmbiStochasticMaxBreakpoints - 1u)];
    }

    void noteOn(int note, float velocity)
    {
        uint32_t selected = 0u;
        float oldest = -1.0f;
        for (uint32_t i = 0; i < params_.voices; ++i) {
            if (!voices_[i].midiRole || voices_[i].envelope.stage == AmbiStochasticEnvelopeStage::Idle) {
                selected = i;
                break;
            }
            if (voices_[i].age > oldest) {
                oldest = voices_[i].age;
                selected = i;
            }
        }
        auto& voice = voices_[selected];
        voice.note = std::clamp(note, 0, 127);
        voice.velocity = clamp(velocity, 0.0f, 1.0f);
        voice.midiGate = true;
        voice.midiRole = true;
        voice.age = 0.0f;
        voice.phase = 0.0f;
        voice.frequency = midiToHz(static_cast<float>(voice.note));
        voice.envelope.trigger();
    }

    void noteOff(int note)
    {
        for (auto& voice : voices_) {
            if (voice.midiRole && voice.note == note) voice.midiGate = false;
        }
    }

    void process(float* const* outputs, uint32_t outputChannels, uint32_t frames)
    {
        if (!outputs || frames == 0u) return;
        outputChannels = std::min<uint32_t>(outputChannels, kAmbiStochasticMaxChannels);
        for (uint32_t ch = 0; ch < outputChannels; ++ch) {
            if (outputs[ch]) std::fill(outputs[ch], outputs[ch] + frames, 0.0f);
        }

        const uint32_t voices = params_.voices;
        const uint32_t ambiChannels = std::min<uint32_t>(ambiChannelsForOrder(params_.order), outputChannels);
        const float outputGain = dbToGain(params_.outputGainDb)
            / std::sqrt(static_cast<float>(std::max<uint32_t>(1u, voices)));
        const float attackCoef = envelopeCoefficient(params_.attackMs);
        const float decayCoef = envelopeCoefficient(params_.decayMs);
        const float releaseCoef = envelopeCoefficient(params_.releaseMs);
        constexpr uint32_t kControlFrames = 16u;

        for (uint32_t chunkStart = 0; chunkStart < frames; chunkStart += kControlFrames) {
            const uint32_t chunkFrames = std::min<uint32_t>(kControlFrames, frames - chunkStart);
            const float dt = static_cast<float>(chunkFrames) / static_cast<float>(sampleRate_);
            updateActivity(dt);
            updateMotion(dt);

            std::array<std::array<float, kAmbiStochasticMaxChannels>, kAmbiStochasticMaxVoices> basis {};
            std::array<float, kAmbiStochasticMaxVoices> distanceGain {};
            std::array<float, kAmbiStochasticMaxVoices> energySum {};
            for (uint32_t i = 0; i < voices; ++i) {
                basis[i] = acnSn3dBasis7(directionFromAed(points_[i].azimuthDeg, points_[i].elevationDeg));
                distanceGain[i] = 1.0f / std::max(0.5f, points_[i].distance);
            }

            for (uint32_t frame = chunkStart; frame < chunkStart + chunkFrames; ++frame) {
                for (uint32_t i = 0; i < voices; ++i) {
                    auto& voice = voices_[i];
                    const bool midiOnly = params_.mode == AmbiStochasticMode::Midi;
                    const bool freeOnly = params_.mode == AmbiStochasticMode::Free;
                    const bool useMidi = !freeOnly && voice.midiRole;
                    const bool freeGate = !midiOnly && activityGate(i);
                    const bool gate = useMidi ? voice.midiGate : freeGate;
                    voice.envelope.setGate(gate);
                    const float envelope = voice.envelope.process(attackCoef, decayCoef, params_.sustain, releaseCoef);
                    if (useMidi && !voice.midiGate && voice.envelope.stage == AmbiStochasticEnvelopeStage::Idle) {
                        voice.midiRole = false;
                    }
                    if (envelope <= 0.000001f) continue;

                    const float freeNote = freeVoiceNote(i);
                    const float note = useMidi ? static_cast<float>(voice.note) : freeNote;
                    const float detune = deterministicSigned(i * 0x45d9f3bu + 19u) * params_.detuneCents * 0.01f;
                    const float periodRatio = params_.model == AmbiStochasticModel::FreePeriod
                        ? voice.periodRatio : 1.0f;
                    const float targetFrequency = std::min(
                        midiToHz(note + detune) * periodRatio, static_cast<float>(sampleRate_) * 0.42f);
                    const float pitchCoef = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.008));
                    voice.frequency += (targetFrequency - voice.frequency) * pitchCoef;

                    const float nextPhase = voice.phase + voice.frequency / static_cast<float>(sampleRate_);
                    const bool wrapped = nextPhase >= 1.0f;
                    voice.phase = nextPhase - std::floor(nextPhase);
                    if (wrapped) advanceWave(i);
                    float sample = tableSample(voice.currentTable, voice.phase);
                    const float nextSample = tableSample(voice.nextTable, voice.phase);
                    sample = lerp(sample, nextSample, voice.phase);
                    const float systemGain = voiceSystemGain(i);
                    const float velocity = useMidi ? std::max(0.04f, voice.velocity) : 0.74f;
                    const float amplitude = std::tanh(sample * 1.12f) * envelope * velocity
                        * systemGain * outputGain * distanceGain[i];
                    energySum[i] += amplitude * amplitude;
                    voice.age += 1.0f / static_cast<float>(sampleRate_);
                    for (uint32_t ch = 0; ch < ambiChannels; ++ch) {
                        if (outputs[ch]) outputs[ch][frame] = flushDenormal(outputs[ch][frame] + amplitude * basis[i][ch]);
                    }
                }
            }
            updateEnergy(energySum, chunkFrames);
        }

        for (uint32_t ch = 0; ch < ambiChannels; ++ch) {
            if (!outputs[ch]) continue;
            for (uint32_t frame = 0; frame < frames; ++frame) {
                outputs[ch][frame] = std::tanh(clamp(outputs[ch][frame], -6.0f, 6.0f));
            }
        }
    }

private:
    static float wrapSignedDeg(float value)
    {
        while (value > 180.0f) value -= 360.0f;
        while (value <= -180.0f) value += 360.0f;
        return value;
    }

    static float signedAngleDelta(float target, float current)
    {
        return wrapSignedDeg(target - current);
    }

    static float fract(float value) { return value - std::floor(value); }

    static float reflect(float value, float minimum, float maximum)
    {
        for (uint32_t i = 0; i < 4u; ++i) {
            if (value > maximum) value = maximum - (value - maximum);
            else if (value < minimum) value = minimum + (minimum - value);
            else break;
        }
        return clamp(value, minimum, maximum);
    }

    static uint32_t randomU32(uint32_t& state)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    static float randomUnit(uint32_t& state)
    {
        return static_cast<float>(randomU32(state) & 0x00ffffffu) / static_cast<float>(0x01000000u);
    }

    static float deterministicSigned(uint32_t seed)
    {
        uint32_t state = seed == 0u ? 1u : seed;
        return randomUnit(state) * 2.0f - 1.0f;
    }

    float randomSigned(AmbiStochasticVoice& voice, AmbiStochasticDistribution distribution)
    {
        switch (distribution) {
        case AmbiStochasticDistribution::Gaussian: {
            float sum = 0.0f;
            for (uint32_t i = 0; i < 6u; ++i) sum += randomUnit(voice.seed);
            return clamp((sum - 3.0f) * 0.78f, -1.0f, 1.0f);
        }
        case AmbiStochasticDistribution::Cauchy: {
            const float u = clamp(randomUnit(voice.seed), 0.001f, 0.999f);
            return clamp(std::tan(kPi * (u - 0.5f)) * 0.18f, -1.0f, 1.0f);
        }
        case AmbiStochasticDistribution::Logistic: {
            const float u = clamp(randomUnit(voice.seed), 0.001f, 0.999f);
            return clamp(std::log(u / (1.0f - u)) * 0.19f, -1.0f, 1.0f);
        }
        case AmbiStochasticDistribution::Arcsine:
            return std::sin(kPi * (randomUnit(voice.seed) - 0.5f));
        case AmbiStochasticDistribution::Exponential: {
            const float u = clamp(randomUnit(voice.seed), 0.0f, 0.9995f);
            const float magnitude = clamp(-std::log(1.0f - u) * 0.26f, 0.0f, 1.0f);
            return randomUnit(voice.seed) < 0.5f ? -magnitude : magnitude;
        }
        case AmbiStochasticDistribution::Binary:
            return randomUnit(voice.seed) < 0.5f ? -1.0f : 1.0f;
        case AmbiStochasticDistribution::Uniform:
        default:
            return randomUnit(voice.seed) * 2.0f - 1.0f;
        }
    }

    void initializeVoice(uint32_t index)
    {
        auto& voice = voices_[index];
        voice = {};
        voice.seed = 0x9e3779b9u ^ ((index + 1u) * 0x85ebca6bu);
        voice.phase = fract(static_cast<float>(index) * 0.61803398875f);
        voice.activityOffset = fract(static_cast<float>(index) * 0.754877666f);
        voice.note = static_cast<int>(std::lround(params_.baseNote));
        voice.frequency = midiToHz(params_.baseNote);
        voice.energy = 0.20f;
        voice.velocity = 0.72f;
        voice.renderedBreakpoints = params_.breakpoints;
        const float curvePhase = fract(static_cast<float>(index) * 0.173205081f);
        const float harmonic = 2.0f + static_cast<float>(index % 5u);
        const float harmonicGain = deterministicSigned(voice.seed ^ 0x68bc21ebu) * 0.18f;
        for (uint32_t point = 0; point < kAmbiStochasticMaxBreakpoints; ++point) {
            const float phase = fract(static_cast<float>(point) / static_cast<float>(params_.breakpoints)
                + curvePhase);
            voice.amplitudes[point] = clamp(
                std::sin(2.0f * kPi * phase) * 0.78f
                    + std::sin(2.0f * kPi * phase * harmonic) * harmonicGain
                    + deterministicSigned(voice.seed + point * 41u) * 0.08f,
                -1.0f, 1.0f);
            voice.durations[point] = clamp(
                1.0f + deterministicSigned(voice.seed ^ (point * 0x9e3779b9u)) * 0.16f,
                0.72f, 1.28f);
        }
        renderTable(voice, voice.currentTable);
        voice.nextTable = voice.currentTable;
    }

    static Vec3 pointVector(AmbiStochasticPoint point)
    {
        Vec3 value = directionFromAed(point.azimuthDeg, point.elevationDeg);
        value.x *= point.distance;
        value.y *= point.distance;
        value.z *= point.distance;
        return value;
    }

    static AmbiStochasticPoint pointFromVector(Vec3 value)
    {
        const float radius = std::sqrt(std::max(0.000001f,
            value.x * value.x + value.y * value.y + value.z * value.z));
        AmbiStochasticPoint point {};
        point.azimuthDeg = wrapSignedDeg(std::atan2(value.y, value.x) * 180.0f / kPi);
        point.elevationDeg = clamp(std::asin(clamp(value.z / radius, -1.0f, 1.0f)) * 180.0f / kPi,
            -90.0f, 90.0f);
        point.distance = clamp(radius, 0.15f, 2.0f);
        return point;
    }

    static float vectorLength(Vec3 value)
    {
        return std::sqrt(std::max(0.0f, value.x * value.x + value.y * value.y + value.z * value.z));
    }

    static float vectorDistance(Vec3 a, Vec3 b)
    {
        return vectorLength({ b.x - a.x, b.y - a.y, b.z - a.z });
    }

    static Vec3 normalizedOr(Vec3 value, Vec3 fallback)
    {
        const float length = vectorLength(value);
        if (length <= 0.000001f) return fallback;
        return { value.x / length, value.y / length, value.z / length };
    }

    static constexpr float topologyBoundary() { return 1.12f; }

    static Vec3 topologyAnchorFor(uint32_t index, uint32_t count)
    {
        count = std::clamp<uint32_t>(count, 1u, kAmbiStochasticMaxVoices);
        if (count == 1u) return { 0.0f, 0.0f, 0.0f };
        index = std::min<uint32_t>(index, count - 1u);
        constexpr float goldenAngle = 2.39996322972865332f;
        const float lane = (static_cast<float>(index) + 0.5f) / static_cast<float>(count);
        const float z = 1.0f - 2.0f * lane;
        const float ring = std::sqrt(std::max(0.0f, 1.0f - z * z));
        const float theta = goldenAngle * static_cast<float>(index);
        const float radius = 0.72f + 0.18f
            * (0.5f + 0.5f * deterministicSigned(0x7f4a7c15u + index * 97u));
        return {
            std::cos(theta) * ring * radius,
            std::sin(theta) * ring * radius,
            z * radius
        };
    }

    void updateTopologyAnchors()
    {
        for (uint32_t i = 0; i < kAmbiStochasticMaxVoices; ++i) {
            topologyAnchor_[i] = topologyAnchorFor(i,
                i < params_.voices ? params_.voices : kAmbiStochasticMaxVoices);
        }
    }

    void initializeDynamicsVoice(uint32_t index)
    {
        dynamicsPosition_[index] = topologyAnchor_[index];
        const Vec3 randomDirection = normalizedOr({
            deterministicSigned(0x45d9f3bu + index * 17u),
            deterministicSigned(0x27d4eb2du + index * 31u),
            deterministicSigned(0x165667b1u + index * 47u)
        }, { 0.0f, 1.0f, 0.0f });
        const float speed = 0.045f + 0.055f * static_cast<float>((index * 37u) % 11u) / 10.0f;
        dynamicsVelocity_[index] = {
            randomDirection.x * speed,
            randomDirection.y * speed,
            randomDirection.z * speed
        };
        dynamicsNoise_[index] = randomDirection;
        kinetic_[index] = speed;
        contact_[index] = 0.0f;
        crowding_[index] = 0.0f;
        tension_[index] = 0.0f;
        networkPulse_[index] = 0.0f;
        eventHold_[index] = (index % 3u) == 0u ? 0.18f + static_cast<float>(index % 5u) * 0.055f : 0.0f;
        eventRefractory_[index] = 0.0f;
        bondBreak_[index] = 0.0f;
        neighborRest_[index][0] = 0.45f;
        neighborRest_[index][1] = 0.62f;
    }

    float topologyAmount(uint32_t index) const
    {
        const Vec3 position = topologyPosition(index);
        const float mutation = std::fabs(position.x);
        const float event = position.y * 0.5f + 0.5f;
        const float radius = clamp(vectorLength(position), 0.0f, 1.0f);
        const float targetSpeed = 0.035f + params_.dynamicsDrive * params_.dynamicsDrive * 0.36f;
        const float kinetic = clamp(kinetic_[index] / std::max(0.02f, targetSpeed * 1.6f), 0.0f, 1.0f);
        const float transient = contact_[index] * 0.30f
            + std::fabs(networkPulse_[index]) * 0.28f
            + tension_[index] * 0.18f
            + crowding_[index] * 0.12f
            + kinetic * 0.12f;
        return clamp(mutation * 0.28f + event * 0.18f + radius * 0.30f + transient * 0.24f,
            0.0f, 1.0f);
    }

    float topologySigned(uint32_t index) const
    {
        const Vec3 position = topologyPosition(index);
        const float polarity = deterministicSigned(0x9e3779b9u + index * 101u);
        return clamp(position.x * 0.54f + position.z * 0.24f
                + networkPulse_[index] * 0.30f
                + (crowding_[index] - 0.5f) * 0.48f
                + (tension_[index] - 0.35f) * 0.24f
                + contact_[index] * polarity * 0.24f,
            -1.0f, 1.0f);
    }

    float topologyEvent(uint32_t index) const
    {
        return topologyPosition(index).y * 0.5f + 0.5f;
    }

    float topologyPeriod(uint32_t index) const
    {
        return topologyPosition(index).z;
    }

    float topologyRadius(uint32_t index) const
    {
        return clamp(vectorLength(topologyPosition(index)), 0.0f, 1.0f);
    }

    void scheduleCascade(uint32_t target, float pulse, uint32_t delayTicks)
    {
        if (target >= params_.voices || std::fabs(pulse) < 0.015f) return;
        const uint32_t slot = (cascadeCursor_ + std::clamp<uint32_t>(delayTicks, 1u,
            kAmbiStochasticCascadeSlots - 1u)) % kAmbiStochasticCascadeSlots;
        cascadeDelay_[target][slot] = clamp(cascadeDelay_[target][slot] + pulse, -1.0f, 1.0f);
    }

    void triggerTopologyEvent(uint32_t index, float strength, uint32_t source, bool propagate = true)
    {
        if (index >= params_.voices) return;
        strength = clamp(strength, 0.0f, 1.0f);
        const float sign = deterministicSigned(dynamicsTick_ * 0x45d9f3bu + index * 131u + source * 17u);
        contact_[index] = std::max(contact_[index], strength);
        networkPulse_[index] = clamp(networkPulse_[index] + sign * strength * 0.78f, -1.0f, 1.0f);
        const float holdBase = lerp(0.055f, 1.35f, params_.memory * params_.memory);
        const float holdVariation = 0.55f + randomUnit(dynamicsSeed_) * 0.90f;
        const float eventShape = lerp(0.62f, 1.38f, topologyEvent(index));
        eventHold_[index] = std::max(eventHold_[index],
            holdBase * holdVariation * eventShape
                * lerp(0.50f, 1.0f, params_.activity) * (0.55f + strength * 0.75f));
        eventRefractory_[index] = std::max(eventRefractory_[index], 0.035f + params_.memory * 0.18f);
        if (params_.dynamics == AmbiStochasticDynamics::Net && strength > 0.64f) {
            bondBreak_[index] = std::max(bondBreak_[index], 0.35f + strength * (0.8f + params_.memory * 1.8f));
        }
        if (!propagate || params_.dynamics != AmbiStochasticDynamics::Cascade || params_.voices < 2u) return;
        const uint32_t baseDelay = 4u + static_cast<uint32_t>(params_.memory * 30.0f);
        const uint32_t spread = 2u + static_cast<uint32_t>(randomUnit(dynamicsSeed_) * 12.0f);
        const float transfer = strength * params_.coupling * 0.76f;
        scheduleCascade(neighborIndex_[index], sign * transfer, baseDelay + spread);
        scheduleCascade(secondaryNeighborIndex_[index], -sign * transfer * 0.72f, baseDelay + spread * 2u);
    }

    float systemPressure(uint32_t index) const
    {
        const auto& voice = voices_[index];
        const float neighborEnergy = (voices_[neighborIndex_[index]].energy
            + voices_[secondaryNeighborIndex_[index]].energy) * 0.5f;
        const float neighborError = (neighborEnergy - voice.energy) * 2.0f;
        const float targetEnergy = lerp(0.07f, 0.34f, params_.activity);
        const float globalError = (targetEnergy - globalEnergy_) * 2.5f;
        const float topology = topologySigned(index) * params_.synthesisDepth;
        const float kineticTarget = 0.035f + params_.dynamicsDrive * params_.dynamicsDrive * 0.36f;
        const float kineticError = clamp((kineticTarget - globalKinetic_)
            / std::max(0.03f, kineticTarget), -1.0f, 1.0f);
        switch (params_.system) {
        case AmbiStochasticSystem::Neighbor:
            return clamp((neighborError + topology * 0.85f) * params_.coupling, -1.0f, 1.0f);
        case AmbiStochasticSystem::Field:
            return clamp((globalError + kineticError * 0.42f + topology * 0.36f)
                    * params_.coupling,
                -1.0f, 1.0f);
        case AmbiStochasticSystem::Network:
            return clamp((neighborError * 0.46f + globalError * 0.52f
                    + kineticError * 0.30f + topology * 0.95f)
                    * params_.coupling,
                -1.0f, 1.0f);
        case AmbiStochasticSystem::Independent:
        default:
            return topology * 0.30f;
        }
    }

    void advanceWave(uint32_t index)
    {
        auto& voice = voices_[index];
        voice.currentTable = voice.nextTable;
        if (voice.renderedBreakpoints != params_.breakpoints) {
            for (uint32_t point = 0; point < params_.breakpoints; ++point) {
                const float phase = static_cast<float>(point) / static_cast<float>(params_.breakpoints);
                voice.amplitudes[point] = tableSample(voice.currentTable, phase);
                voice.durations[point] = 1.0f;
                voice.amplitudeVelocity[point] = 0.0f;
                voice.durationVelocity[point] = 0.0f;
            }
            voice.renderedBreakpoints = params_.breakpoints;
        }

        const Vec3 topology = topologyPosition(index);
        const float mappedTopology = topologyAmount(index) * params_.synthesisDepth;
        const float mutation = std::fabs(topology.x) * params_.synthesisDepth;
        const float eventShape = topologyEvent(index) * params_.synthesisDepth;
        const float radius = topologyRadius(index) * params_.synthesisDepth;
        const float periodShape = topologyPeriod(index) * params_.synthesisDepth;
        const float pressure = systemPressure(index) * params_.reactivity;
        const float inertia = lerp(0.38f, 0.985f, params_.inertia)
            * lerp(1.0f, 0.72f, mappedTopology * params_.reactivity);
        const float ampKick = (0.0015f + params_.amplitudeStep * params_.amplitudeStep * 0.115f)
            * clamp(1.0f + pressure * 0.85f + mappedTopology * 0.85f
                    + mutation * 1.55f + radius * 0.72f,
                0.24f, 4.2f);
        const float timeKick = (0.0010f + params_.timeStep * params_.timeStep * 0.085f)
            * clamp(1.0f + pressure * 0.70f + mappedTopology * 0.75f
                    + eventShape * 1.85f + radius * 0.62f,
                0.25f, 4.6f);
        const float ampVelocityLimit = (0.015f + params_.amplitudeStep * 0.18f)
            * (1.0f + mutation * 0.82f + radius * 0.54f);
        const float timeVelocityLimit = (0.010f + params_.timeStep * 0.13f)
            * (1.0f + eventShape * 0.92f + radius * 0.58f);

        for (uint32_t point = 0; point < params_.breakpoints; ++point) {
            const float curvePolarity = (point & 1u) == 0u ? 1.0f : -0.62f;
            const float ampNoise = clamp(randomSigned(voice, params_.amplitudeDistribution)
                    + topology.x * curvePolarity * 0.18f * params_.synthesisDepth,
                -1.0f, 1.0f);
            const float timeNoise = randomSigned(voice, params_.durationDistribution);
            if (params_.model == AmbiStochasticModel::Direct) {
                const float stepMemory = params_.inertia * 0.42f;
                voice.amplitudeVelocity[point] = clamp(
                    lerp(ampNoise * ampKick, voice.amplitudeVelocity[point], stepMemory),
                    -ampVelocityLimit, ampVelocityLimit);
                voice.durationVelocity[point] = clamp(
                    lerp(timeNoise * timeKick, voice.durationVelocity[point], stepMemory),
                    -timeVelocityLimit, timeVelocityLimit);
            } else {
                voice.amplitudeVelocity[point] = clamp(
                    voice.amplitudeVelocity[point] * inertia + ampNoise * ampKick * (1.12f - params_.inertia * 0.62f),
                    -ampVelocityLimit, ampVelocityLimit);
                voice.durationVelocity[point] = clamp(
                    voice.durationVelocity[point] * inertia + timeNoise * timeKick * (1.12f - params_.inertia * 0.62f),
                    -timeVelocityLimit, timeVelocityLimit);
            }
            voice.amplitudes[point] = reflect(
                voice.amplitudes[point] + voice.amplitudeVelocity[point], -1.0f, 1.0f);
            voice.durations[point] = reflect(
                voice.durations[point] + voice.durationVelocity[point], 0.22f, 3.8f);
        }
        float durationTotal = 0.0f;
        for (uint32_t point = 0; point < params_.breakpoints; ++point) {
            durationTotal += voice.durations[point];
        }
        const float meanDuration = durationTotal / static_cast<float>(params_.breakpoints);
        voice.periodRatio = params_.model == AmbiStochasticModel::FreePeriod
            ? clamp(std::pow(1.0f / std::max(0.05f, meanDuration), 0.72f)
                    * std::pow(2.0f, periodShape * 1.45f * params_.reactivity)
                    * std::pow(2.0f, networkPulse_[index] * 0.32f * params_.reactivity),
                0.25f, 4.0f)
            : 1.0f;
        renderTable(voice, voice.nextTable);
    }

    void renderTable(const AmbiStochasticVoice& voice, std::array<float, kAmbiStochasticTableSize>& table) const
    {
        const uint32_t points = std::clamp<uint32_t>(voice.renderedBreakpoints, 4u, kAmbiStochasticMaxBreakpoints);
        float durationTotal = 0.0f;
        for (uint32_t point = 0; point < points; ++point) durationTotal += std::max(0.01f, voice.durations[point]);
        uint32_t segment = 0u;
        float segmentStart = 0.0f;
        float segmentEnd = voice.durations[0] / durationTotal;
        for (uint32_t sample = 0; sample < kAmbiStochasticTableSize; ++sample) {
            const float phase = static_cast<float>(sample) / static_cast<float>(kAmbiStochasticTableSize);
            while (phase >= segmentEnd && segment + 1u < points) {
                segmentStart = segmentEnd;
                ++segment;
                segmentEnd += voice.durations[segment] / durationTotal;
            }
            const float local = clamp((phase - segmentStart) / std::max(0.000001f, segmentEnd - segmentStart), 0.0f, 1.0f);
            const uint32_t next = (segment + 1u) % points;
            float value = lerp(voice.amplitudes[segment], voice.amplitudes[next], local);
            if (params_.model == AmbiStochasticModel::Curved) {
                const uint32_t previous = (segment + points - 1u) % points;
                const uint32_t following = (next + 1u) % points;
                const float p0 = voice.amplitudes[previous];
                const float p1 = voice.amplitudes[segment];
                const float p2 = voice.amplitudes[next];
                const float p3 = voice.amplitudes[following];
                const float local2 = local * local;
                const float local3 = local2 * local;
                value = 0.5f * ((2.0f * p1)
                    + (-p0 + p2) * local
                    + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * local2
                    + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * local3);
            }
            table[sample] = value + std::sin(2.0f * kPi * phase) * 0.055f;
        }
        float mean = 0.0f;
        for (float sample : table) mean += sample;
        mean /= static_cast<float>(kAmbiStochasticTableSize);
        float peak = 0.0f;
        for (float& sample : table) {
            sample -= mean;
            peak = std::max(peak, std::fabs(sample));
        }
        const float scale = peak > 0.92f ? 0.92f / peak : 1.0f;
        for (float& sample : table) sample *= scale;
    }

    static float tableSample(const std::array<float, kAmbiStochasticTableSize>& table, float phase)
    {
        const float position = fract(phase) * static_cast<float>(kAmbiStochasticTableSize);
        const uint32_t first = static_cast<uint32_t>(position) % kAmbiStochasticTableSize;
        const uint32_t second = (first + 1u) % kAmbiStochasticTableSize;
        return lerp(table[first], table[second], position - std::floor(position));
    }

    float freeVoiceNote(uint32_t index) const
    {
        const float normalized = params_.voices <= 1u ? 0.0f
            : static_cast<float>(index) / static_cast<float>(params_.voices - 1u) - 0.5f;
        const Vec3 topology = topologyPosition(index);
        const float topologyPitch = topology.z * params_.synthesisDepth
            * std::min(14.0f, 3.0f + params_.pitchSpreadSemitones * 0.34f);
        const float mutationPitch = topology.x * params_.synthesisDepth * 2.4f;
        const float crowdingRegister = (crowding_[index] - 0.5f) * params_.synthesisDepth * 7.0f;
        return params_.baseNote + normalized * params_.pitchSpreadSemitones
            + topologyPitch + mutationPitch + crowdingRegister;
    }

    static float midiToHz(float note)
    {
        return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
    }

    float envelopeCoefficient(float milliseconds) const
    {
        const float samples = std::max(1.0f, milliseconds * 0.001f * static_cast<float>(sampleRate_));
        return 1.0f - std::exp(-4.605170186f / samples);
    }

    bool activityGate(uint32_t index) const
    {
        if (params_.dynamics != AmbiStochasticDynamics::Off) {
            return eventHold_[index] > 0.0f || std::fabs(networkPulse_[index]) > 0.18f;
        }
        if (params_.activity >= 0.999f) return true;
        const float offset = voices_[index].activityOffset;
        const float primary = 0.5f + 0.5f * std::sin(2.0f * kPi * (activityPhase_ + offset));
        const float secondary = 0.5f + 0.5f * std::sin(2.0f * kPi * (activityPhase_ * 0.381966f + offset * 1.71f));
        const float signal = primary * 0.68f + secondary * 0.32f + systemPressure(index) * 0.15f;
        return signal > (1.0f - params_.activity);
    }

    void updateActivity(float dt)
    {
        const float activityRate = lerp(0.36f, 0.026f, params_.memory * params_.memory);
        activityPhase_ = fract(activityPhase_ + dt * activityRate);
    }

    AmbiStochasticPoint basePoint(uint32_t index) const
    {
        const float count = static_cast<float>(std::max<uint32_t>(1u, params_.voices));
        const float lane = static_cast<float>(index) / count;
        AmbiStochasticPoint point {};
        point.azimuthDeg = wrapSignedDeg(params_.centerAzimuthDeg - lane * 360.0f * params_.motionSpread);
        point.elevationDeg = clamp(params_.centerElevationDeg
            + std::sin(static_cast<float>(index) * 2.39996323f) * 52.0f * params_.motionSpread,
            -90.0f, 90.0f);
        point.distance = params_.centerDistance;
        return point;
    }

    AmbiStochasticPoint motionTarget(uint32_t index) const
    {
        AmbiStochasticPoint point = basePoint(index);
        const float lane = static_cast<float>(index) / static_cast<float>(std::max<uint32_t>(1u, params_.voices));
        const float phase = fract(motionPhase_ + lane * (1.0f - params_.coupling) * 0.42f);
        const float theta = 2.0f * kPi * phase;
        const float seed = static_cast<float>(index) * 1.61803398875f;
        const float amount = params_.motionAmount;
        switch (params_.motion) {
        case AmbiStochasticMotion::Orbit:
            point.azimuthDeg = wrapSignedDeg(point.azimuthDeg - phase * 360.0f * amount);
            point.elevationDeg = clamp(point.elevationDeg + std::sin(theta + seed) * 44.0f * amount, -90.0f, 90.0f);
            point.distance = clamp(params_.centerDistance * (1.0f + std::cos(theta * 2.0f + seed) * 0.18f * amount), 0.15f, 2.0f);
            break;
        case AmbiStochasticMotion::Drift:
            point.azimuthDeg = wrapSignedDeg(point.azimuthDeg
                + (std::sin(theta + seed) * 0.68f + std::sin(theta * 0.47f + seed * 1.7f) * 0.32f) * 155.0f * amount);
            point.elevationDeg = clamp(point.elevationDeg
                + (std::cos(theta * 0.61f + seed) * 0.62f + std::sin(theta * 1.31f + seed * 0.3f) * 0.38f) * 58.0f * amount,
                -90.0f, 90.0f);
            point.distance = clamp(params_.centerDistance * (1.0f + std::sin(theta * 0.37f + seed) * 0.30f * amount), 0.15f, 2.0f);
            break;
        case AmbiStochasticMotion::Feedback: {
            const float local = voices_[index].energy;
            const float neighbor = voices_[neighborIndex_[index]].energy;
            const float pressure = systemPressure(index);
            point.azimuthDeg = wrapSignedDeg(point.azimuthDeg - phase * 240.0f * amount
                + (neighbor - local) * 210.0f * params_.reactivity);
            point.elevationDeg = clamp(point.elevationDeg
                + pressure * 54.0f * amount
                + std::sin(theta + seed) * 18.0f * amount, -90.0f, 90.0f);
            point.distance = clamp(params_.centerDistance
                * (1.0f + (local - globalEnergy_) * 1.6f * amount), 0.15f, 2.0f);
            break;
        }
        case AmbiStochasticMotion::Field:
        default:
            break;
        }
        return point;
    }

    void advanceDynamics(float dt)
    {
        const uint32_t voices = params_.voices;
        if (voices == 0u) return;
        ++dynamicsTick_;
        cascadeCursor_ = (cascadeCursor_ + 1u) % kAmbiStochasticCascadeSlots;
        const float contactDecay = std::exp(-dt / 0.16f);
        const float pulseDecay = std::exp(-dt / lerp(0.12f, 0.85f, params_.memory));
        for (uint32_t i = 0; i < voices; ++i) {
            contact_[i] *= contactDecay;
            networkPulse_[i] *= pulseDecay;
            tension_[i] *= 0.82f;
            eventHold_[i] = std::max(0.0f, eventHold_[i] - dt);
            eventRefractory_[i] = std::max(0.0f, eventRefractory_[i] - dt);
            bondBreak_[i] = std::max(0.0f, bondBreak_[i] - dt);

            const float incoming = cascadeDelay_[i][cascadeCursor_];
            cascadeDelay_[i][cascadeCursor_] = 0.0f;
            if (std::fabs(incoming) > 0.012f) {
                networkPulse_[i] = clamp(networkPulse_[i] + incoming, -1.0f, 1.0f);
                if (eventRefractory_[i] <= 0.0f) {
                    const float strength = clamp(std::fabs(incoming), 0.0f, 1.0f);
                    const float hold = lerp(0.06f, 1.15f, params_.memory * params_.memory)
                        * (0.65f + strength * 0.70f);
                    eventHold_[i] = std::max(eventHold_[i], hold);
                    eventRefractory_[i] = 0.04f + params_.memory * 0.16f;
                    const Vec3 kick = normalizedOr({
                        deterministicSigned(dynamicsTick_ + i * 79u),
                        deterministicSigned(dynamicsTick_ * 3u + i * 43u),
                        deterministicSigned(dynamicsTick_ * 7u + i * 29u)
                    }, { 1.0f, 0.0f, 0.0f });
                    const float kickAmount = 0.035f + strength * 0.11f;
                    dynamicsVelocity_[i].x += kick.x * kickAmount;
                    dynamicsVelocity_[i].y += kick.y * kickAmount;
                    dynamicsVelocity_[i].z += kick.z * kickAmount;
                    if (params_.dynamics == AmbiStochasticDynamics::Cascade && strength > 0.08f) {
                        const uint32_t delay = 5u + static_cast<uint32_t>(params_.memory * 26.0f)
                            + static_cast<uint32_t>(randomUnit(dynamicsSeed_) * 9.0f);
                        scheduleCascade(neighborIndex_[i], incoming * params_.coupling * 0.72f, delay);
                        scheduleCascade(secondaryNeighborIndex_[i], -incoming * params_.coupling * 0.52f, delay + 7u);
                    }
                }
            }
        }

        if ((dynamicsTick_ % 6u) == 1u) updateNeighborGraph();
        if (params_.dynamics == AmbiStochasticDynamics::Off) {
            const float follow = 1.0f - std::exp(-dt / 0.28f);
            float kineticSum = 0.0f;
            for (uint32_t i = 0; i < voices; ++i) {
                const Vec3 target = topologyAnchor_[i];
                dynamicsPosition_[i].x += (target.x - dynamicsPosition_[i].x) * follow;
                dynamicsPosition_[i].y += (target.y - dynamicsPosition_[i].y) * follow;
                dynamicsPosition_[i].z += (target.z - dynamicsPosition_[i].z) * follow;
                dynamicsVelocity_[i].x *= 0.90f;
                dynamicsVelocity_[i].y *= 0.90f;
                dynamicsVelocity_[i].z *= 0.90f;
                kinetic_[i] = vectorLength(dynamicsVelocity_[i]);
                kineticSum += kinetic_[i];
            }
            globalKinetic_ += (kineticSum / static_cast<float>(voices) - globalKinetic_) * 0.18f;
            return;
        }

        std::array<Vec3, kAmbiStochasticMaxVoices> forces {};
        const float targetSpeed = 0.035f + params_.dynamicsDrive * params_.dynamicsDrive * 0.36f;
        const float kineticError = clamp((targetSpeed - globalKinetic_) / std::max(0.025f, targetSpeed), -1.0f, 1.0f);
        const float underDrive = std::max(0.0f, kineticError);
        const float overDrive = std::max(0.0f, -kineticError);
        const float noiseSlew = 1.0f - std::exp(-dt / lerp(0.18f, 0.72f, params_.memory));
        for (uint32_t i = 0; i < voices; ++i) {
            const Vec3 noiseTarget = normalizedOr({
                randomUnit(dynamicsSeed_) * 2.0f - 1.0f,
                randomUnit(dynamicsSeed_) * 2.0f - 1.0f,
                randomUnit(dynamicsSeed_) * 2.0f - 1.0f
            }, { 0.0f, 1.0f, 0.0f });
            dynamicsNoise_[i].x += (noiseTarget.x - dynamicsNoise_[i].x) * noiseSlew;
            dynamicsNoise_[i].y += (noiseTarget.y - dynamicsNoise_[i].y) * noiseSlew;
            dynamicsNoise_[i].z += (noiseTarget.z - dynamicsNoise_[i].z) * noiseSlew;
            const Vec3 target = topologyAnchor_[i];
            float tether = params_.dynamics == AmbiStochasticDynamics::Gas ? 0.010f : 0.026f;
            float injection = 0.025f + params_.dynamicsDrive * 0.12f + underDrive * 0.38f;
            if (params_.motion == AmbiStochasticMotion::Field) {
                tether *= 1.85f;
                injection *= 0.36f;
            } else if (params_.motion == AmbiStochasticMotion::Drift) {
                tether *= 0.62f;
                injection *= 1.18f;
            }
            forces[i] = {
                (target.x - dynamicsPosition_[i].x) * tether + dynamicsNoise_[i].x * injection,
                (target.y - dynamicsPosition_[i].y) * tether + dynamicsNoise_[i].y * injection,
                (target.z - dynamicsPosition_[i].z) * tether + dynamicsNoise_[i].z * injection
            };
            const Vec3 radial = normalizedOr(dynamicsPosition_[i], { 1.0f, 0.0f, 0.0f });
            const float energyForce = (voices_[i].energy - globalEnergy_) * params_.coupling
                * params_.reactivity * 0.42f;
            forces[i].x += radial.x * energyForce;
            forces[i].y += radial.y * energyForce;
            forces[i].z += radial.z * energyForce;

            if (params_.motion == AmbiStochasticMotion::Orbit) {
                const Vec3 tangent = normalizedOr({
                    -dynamicsPosition_[i].y,
                    dynamicsPosition_[i].x,
                    std::sin(2.0f * kPi * motionPhase_ + static_cast<float>(i) * 1.618f) * 0.22f
                }, { 0.0f, 1.0f, 0.0f });
                const float orbitForce = params_.motionAmount * (0.055f + params_.dynamicsDrive * 0.16f);
                forces[i].x += tangent.x * orbitForce;
                forces[i].y += tangent.y * orbitForce;
                forces[i].z += tangent.z * orbitForce;
            } else if (params_.motion == AmbiStochasticMotion::Feedback) {
                const Vec3 tangent = normalizedOr({
                    -dynamicsPosition_[i].y,
                    dynamicsPosition_[i].x,
                    dynamicsPosition_[i].x - dynamicsPosition_[i].z
                }, { 0.0f, 1.0f, 0.0f });
                const float feedback = clamp(networkPulse_[i]
                        + (voices_[i].energy - globalEnergy_) * 2.4f,
                    -1.0f, 1.0f);
                const float feedbackForce = params_.motionAmount * params_.reactivity
                    * (0.035f + std::fabs(feedback) * 0.20f);
                forces[i].x += tangent.x * feedbackForce * (feedback >= 0.0f ? 1.0f : -1.0f);
                forces[i].y += tangent.y * feedbackForce * (feedback >= 0.0f ? 1.0f : -1.0f);
                forces[i].z += tangent.z * feedbackForce * (feedback >= 0.0f ? 1.0f : -1.0f);
            }
        }

        if (params_.dynamics == AmbiStochasticDynamics::Net
            || params_.dynamics == AmbiStochasticDynamics::Cascade) {
            const float stiffness = params_.coupling
                * (params_.dynamics == AmbiStochasticDynamics::Net ? 0.46f : 0.28f);
            for (uint32_t i = 0; i < voices; ++i) {
                if (bondBreak_[i] > 0.0f) continue;
                const uint32_t neighbors[2] { neighborIndex_[i], secondaryNeighborIndex_[i] };
                for (uint32_t edge = 0; edge < 2u; ++edge) {
                    const uint32_t neighbor = neighbors[edge];
                    if (neighbor == i || neighbor >= voices || bondBreak_[neighbor] > 0.0f) continue;
                    const Vec3 delta {
                        dynamicsPosition_[neighbor].x - dynamicsPosition_[i].x,
                        dynamicsPosition_[neighbor].y - dynamicsPosition_[i].y,
                        dynamicsPosition_[neighbor].z - dynamicsPosition_[i].z
                    };
                    const float distance = std::max(0.0001f, vectorLength(delta));
                    const float rest = std::max(0.08f, neighborRest_[i][edge]);
                    const float extension = (distance - rest) / rest;
                    const float magnitude = clamp(extension * stiffness * (edge == 0u ? 1.0f : 0.72f), -0.64f, 0.64f);
                    const Vec3 direction { delta.x / distance, delta.y / distance, delta.z / distance };
                    forces[i].x += direction.x * magnitude;
                    forces[i].y += direction.y * magnitude;
                    forces[i].z += direction.z * magnitude;
                    forces[neighbor].x -= direction.x * magnitude;
                    forces[neighbor].y -= direction.y * magnitude;
                    forces[neighbor].z -= direction.z * magnitude;
                    const float edgeTension = clamp(std::fabs(extension), 0.0f, 1.0f);
                    tension_[i] = std::max(tension_[i], edgeTension);
                    tension_[neighbor] = std::max(tension_[neighbor], edgeTension);
                    if (edgeTension > 0.82f && eventRefractory_[i] <= 0.0f) {
                        bondBreak_[i] = 0.35f + edgeTension * (0.75f + params_.memory * 1.7f);
                        triggerTopologyEvent(i, edgeTension, neighbor);
                    }
                }
            }
        }

        const float damping = std::exp(-dt * (0.10f + params_.dynamicsDrag * 3.2f + overDrive * 2.1f));
        const float maximumSpeed = 0.18f + params_.dynamicsDrive * 0.68f;
        const float boundary = topologyBoundary();
        for (uint32_t i = 0; i < voices; ++i) {
            dynamicsVelocity_[i].x = (dynamicsVelocity_[i].x + forces[i].x * dt) * damping;
            dynamicsVelocity_[i].y = (dynamicsVelocity_[i].y + forces[i].y * dt) * damping;
            dynamicsVelocity_[i].z = (dynamicsVelocity_[i].z + forces[i].z * dt) * damping;
            const float speed = vectorLength(dynamicsVelocity_[i]);
            if (speed > maximumSpeed) {
                const float scale = maximumSpeed / speed;
                dynamicsVelocity_[i].x *= scale;
                dynamicsVelocity_[i].y *= scale;
                dynamicsVelocity_[i].z *= scale;
            }
            dynamicsPosition_[i].x += dynamicsVelocity_[i].x * dt;
            dynamicsPosition_[i].y += dynamicsVelocity_[i].y * dt;
            dynamicsPosition_[i].z += dynamicsVelocity_[i].z * dt;
            const float radius = vectorLength(dynamicsPosition_[i]);
            if (radius > boundary) {
                const Vec3 normal {
                    dynamicsPosition_[i].x / radius,
                    dynamicsPosition_[i].y / radius,
                    dynamicsPosition_[i].z / radius
                };
                dynamicsPosition_[i] = { normal.x * boundary, normal.y * boundary, normal.z * boundary };
                const float outward = dynamicsVelocity_[i].x * normal.x
                    + dynamicsVelocity_[i].y * normal.y + dynamicsVelocity_[i].z * normal.z;
                if (outward > 0.0f) {
                    const float restitution = 0.42f + params_.dynamicsBounce * 0.54f;
                    dynamicsVelocity_[i].x -= normal.x * outward * (1.0f + restitution);
                    dynamicsVelocity_[i].y -= normal.y * outward * (1.0f + restitution);
                    dynamicsVelocity_[i].z -= normal.z * outward * (1.0f + restitution);
                    if (eventRefractory_[i] <= 0.0f) {
                        triggerTopologyEvent(i, clamp(outward / std::max(0.05f, maximumSpeed), 0.18f, 1.0f), i);
                    }
                }
            }
        }

        const float collisionDistance = 0.10f + params_.dynamicsRadius * 0.35f;
        for (uint32_t a = 0; a < voices; ++a) {
            for (uint32_t b = a + 1u; b < voices; ++b) {
                Vec3 delta {
                    dynamicsPosition_[b].x - dynamicsPosition_[a].x,
                    dynamicsPosition_[b].y - dynamicsPosition_[a].y,
                    dynamicsPosition_[b].z - dynamicsPosition_[a].z
                };
                float distance = vectorLength(delta);
                if (distance >= collisionDistance) continue;
                Vec3 normal = distance > 0.0001f
                    ? Vec3 { delta.x / distance, delta.y / distance, delta.z / distance }
                    : normalizedOr({
                        deterministicSigned(a * 71u + b * 19u),
                        deterministicSigned(a * 31u + b * 53u),
                        deterministicSigned(a * 43u + b * 29u)
                    }, { 1.0f, 0.0f, 0.0f });
                distance = std::max(0.0001f, distance);
                const float overlap = collisionDistance - distance;
                const float correction = overlap * 0.51f;
                dynamicsPosition_[a].x -= normal.x * correction;
                dynamicsPosition_[a].y -= normal.y * correction;
                dynamicsPosition_[a].z -= normal.z * correction;
                dynamicsPosition_[b].x += normal.x * correction;
                dynamicsPosition_[b].y += normal.y * correction;
                dynamicsPosition_[b].z += normal.z * correction;
                const float relative = (dynamicsVelocity_[b].x - dynamicsVelocity_[a].x) * normal.x
                    + (dynamicsVelocity_[b].y - dynamicsVelocity_[a].y) * normal.y
                    + (dynamicsVelocity_[b].z - dynamicsVelocity_[a].z) * normal.z;
                if (relative < 0.0f) {
                    const float restitution = 0.52f + params_.dynamicsBounce * 0.46f;
                    const float impulse = -(1.0f + restitution) * relative * 0.5f;
                    dynamicsVelocity_[a].x -= normal.x * impulse;
                    dynamicsVelocity_[a].y -= normal.y * impulse;
                    dynamicsVelocity_[a].z -= normal.z * impulse;
                    dynamicsVelocity_[b].x += normal.x * impulse;
                    dynamicsVelocity_[b].y += normal.y * impulse;
                    dynamicsVelocity_[b].z += normal.z * impulse;
                }
                const float impact = clamp((-relative) / std::max(0.04f, maximumSpeed)
                        + overlap / collisionDistance * 0.55f,
                    0.0f, 1.0f);
                contact_[a] = std::max(contact_[a], impact);
                contact_[b] = std::max(contact_[b], impact);
                if (impact > 0.14f) {
                    if (eventRefractory_[a] <= 0.0f) triggerTopologyEvent(a, impact, b);
                    if (eventRefractory_[b] <= 0.0f) triggerTopologyEvent(b, impact, a);
                }
            }
        }

        const float autonomousRate = 0.035f + params_.activity * params_.activity
            * (0.20f + params_.dynamicsDrive * 0.34f);
        float kineticSum = 0.0f;
        for (uint32_t i = 0; i < voices; ++i) {
            const float voiceEventRate = autonomousRate * lerp(0.42f, 1.68f, topologyEvent(i));
            if (eventRefractory_[i] <= 0.0f && randomUnit(dynamicsSeed_) < voiceEventRate * dt) {
                const float strength = 0.26f + randomUnit(dynamicsSeed_) * 0.72f;
                const Vec3 kick = normalizedOr({
                    randomUnit(dynamicsSeed_) * 2.0f - 1.0f,
                    randomUnit(dynamicsSeed_) * 2.0f - 1.0f,
                    randomUnit(dynamicsSeed_) * 2.0f - 1.0f
                }, { 0.0f, 1.0f, 0.0f });
                const float kickAmount = (0.025f + params_.dynamicsDrive * 0.10f) * strength;
                dynamicsVelocity_[i].x += kick.x * kickAmount;
                dynamicsVelocity_[i].y += kick.y * kickAmount;
                dynamicsVelocity_[i].z += kick.z * kickAmount;
                triggerTopologyEvent(i, strength, i);
            }
            kinetic_[i] = vectorLength(dynamicsVelocity_[i]);
            kineticSum += kinetic_[i];
        }
        const float measuredKinetic = kineticSum / static_cast<float>(voices);
        globalKinetic_ += (measuredKinetic - globalKinetic_) * 0.16f;
    }

    void updateMotion(float dt)
    {
        motionPhase_ = fract(motionPhase_ + dt * params_.motionRateHz);
        dynamicsAccumulator_ += std::max(0.0f, dt);
        constexpr float dynamicsStep = 1.0f / 120.0f;
        uint32_t steps = 0u;
        while (dynamicsAccumulator_ >= dynamicsStep && steps < 8u) {
            advanceDynamics(dynamicsStep);
            dynamicsAccumulator_ -= dynamicsStep;
            ++steps;
        }
        if (steps == 8u && dynamicsAccumulator_ > dynamicsStep) dynamicsAccumulator_ = dynamicsStep;

        const float smoothTime = lerp(0.018f, 0.11f, params_.memory);
        const float coefficient = 1.0f - std::exp(-dt / smoothTime);
        const float spatial = params_.dynamics == AmbiStochasticDynamics::Off ? 0.0f : params_.spatialDepth;
        for (uint32_t i = 0; i < params_.voices; ++i) {
            const Vec3 base = pointVector(motionTarget(i));
            const float baseDistance = std::max(0.0001f, vectorLength(base));
            const Vec3 baseDirection = normalizedOr(base, { 1.0f, 0.0f, 0.0f });
            const float topologyDistance = vectorLength(dynamicsPosition_[i]);
            const Vec3 topologyDirection = normalizedOr(dynamicsPosition_[i], baseDirection);
            const Vec3 direction = normalizedOr({
                lerp(baseDirection.x, topologyDirection.x, spatial),
                lerp(baseDirection.y, topologyDirection.y, spatial),
                lerp(baseDirection.z, topologyDirection.z, spatial)
            }, baseDirection);
            const float topologyRadius = clamp(topologyDistance / topologyBoundary(), 0.0f, 1.0f);
            const float mappedDistance = clamp(baseDistance * (0.72f + topologyRadius * 0.52f), 0.15f, 2.0f);
            const float distance = lerp(baseDistance, mappedDistance, spatial);
            const Vec3 target { direction.x * distance, direction.y * distance, direction.z * distance };
            const auto mapped = pointFromVector(target);
            points_[i].azimuthDeg = wrapSignedDeg(points_[i].azimuthDeg
                + signedAngleDelta(mapped.azimuthDeg, points_[i].azimuthDeg) * coefficient);
            points_[i].elevationDeg += (mapped.elevationDeg - points_[i].elevationDeg) * coefficient;
            points_[i].distance += (mapped.distance - points_[i].distance) * coefficient;
        }
    }

    void updateNeighborGraph()
    {
        const uint32_t voices = params_.voices;
        if (voices <= 1u) {
            neighborIndex_[0] = 0u;
            secondaryNeighborIndex_[0] = 0u;
            crowding_[0] = 0.0f;
            return;
        }
        if (voices == 2u) {
            const float distance = vectorDistance(dynamicsPosition_[0], dynamicsPosition_[1]);
            for (uint32_t i = 0u; i < 2u; ++i) {
                neighborIndex_[i] = 1u - i;
                secondaryNeighborIndex_[i] = 1u - i;
                neighborRest_[i][0] = clamp(distance, 0.12f, 1.4f);
                neighborRest_[i][1] = neighborRest_[i][0];
                const float neighborhood = 0.38f + params_.dynamicsRadius * 0.86f;
                const float targetCrowding = clamp(1.0f - distance / std::max(0.1f, neighborhood), 0.0f, 1.0f);
                crowding_[i] += (targetCrowding - crowding_[i]) * 0.32f;
            }
            return;
        }
        for (uint32_t i = 0; i < voices; ++i) {
            uint32_t nearest = i == 0u ? 1u : 0u;
            uint32_t second = nearest;
            float nearestDistance = 1.0e9f;
            float secondDistance = 1.0e9f;
            for (uint32_t j = 0; j < voices; ++j) {
                if (i == j) continue;
                const float distance = vectorDistance(dynamicsPosition_[i], dynamicsPosition_[j]);
                if (distance < nearestDistance) {
                    secondDistance = nearestDistance;
                    second = nearest;
                    nearestDistance = distance;
                    nearest = j;
                } else if (distance < secondDistance) {
                    secondDistance = distance;
                    second = j;
                }
            }
            const uint32_t oldPrimary = neighborIndex_[i];
            const uint32_t oldSecondary = secondaryNeighborIndex_[i];
            if (oldPrimary < voices && oldPrimary != i) {
                const float retained = vectorDistance(dynamicsPosition_[i], dynamicsPosition_[oldPrimary]);
                if (retained <= nearestDistance * 1.24f) {
                    nearest = oldPrimary;
                    nearestDistance = retained;
                    secondDistance = 1.0e9f;
                    for (uint32_t j = 0; j < voices; ++j) {
                        if (j == i || j == nearest) continue;
                        const float distance = vectorDistance(dynamicsPosition_[i], dynamicsPosition_[j]);
                        if (distance < secondDistance) {
                            secondDistance = distance;
                            second = j;
                        }
                    }
                }
            }
            neighborIndex_[i] = nearest;
            secondaryNeighborIndex_[i] = second;
            if (dynamicsTick_ == 0u || nearest != oldPrimary) neighborRest_[i][0] = clamp(nearestDistance, 0.12f, 1.4f);
            if (dynamicsTick_ == 0u || second != oldSecondary) neighborRest_[i][1] = clamp(secondDistance, 0.16f, 1.7f);
            const float neighborhood = 0.38f + params_.dynamicsRadius * 0.86f;
            const float targetCrowding = clamp(1.0f - (nearestDistance + secondDistance) * 0.5f
                    / std::max(0.1f, neighborhood),
                0.0f, 1.0f);
            crowding_[i] += (targetCrowding - crowding_[i]) * 0.32f;
        }
    }

    float voiceSystemGain(uint32_t index) const
    {
        const float pressure = systemPressure(index);
        const float topologyLift = topologyAmount(index) * params_.synthesisDepth * params_.reactivity;
        return clamp(1.0f + pressure * params_.reactivity * 0.36f + topologyLift * 0.16f,
            0.34f, 1.42f);
    }

    void updateEnergy(const std::array<float, kAmbiStochasticMaxVoices>& energySum, uint32_t frames)
    {
        const float dt = static_cast<float>(frames) / static_cast<float>(sampleRate_);
        const float memoryTime = lerp(0.045f, 4.5f, params_.memory * params_.memory);
        const float coefficient = 1.0f - std::exp(-dt / memoryTime);
        float sum = 0.0f;
        for (uint32_t i = 0; i < params_.voices; ++i) {
            const float measured = std::sqrt(energySum[i] / static_cast<float>(std::max<uint32_t>(1u, frames)));
            voices_[i].energy += (measured - voices_[i].energy) * coefficient;
            sum += voices_[i].energy;
        }
        const float target = sum / static_cast<float>(std::max<uint32_t>(1u, params_.voices));
        globalEnergy_ += (target - globalEnergy_) * coefficient;
    }

    double sampleRate_ = 48000.0;
    AmbiStochasticParams params_ {};
    std::array<AmbiStochasticVoice, kAmbiStochasticMaxVoices> voices_ {};
    std::array<AmbiStochasticPoint, kAmbiStochasticMaxVoices> points_ {};
    std::array<uint32_t, kAmbiStochasticMaxVoices> neighborIndex_ {};
    std::array<uint32_t, kAmbiStochasticMaxVoices> secondaryNeighborIndex_ {};
    std::array<Vec3, kAmbiStochasticMaxVoices> topologyAnchor_ {};
    std::array<Vec3, kAmbiStochasticMaxVoices> dynamicsPosition_ {};
    std::array<Vec3, kAmbiStochasticMaxVoices> dynamicsVelocity_ {};
    std::array<Vec3, kAmbiStochasticMaxVoices> dynamicsNoise_ {};
    std::array<std::array<float, 2>, kAmbiStochasticMaxVoices> neighborRest_ {};
    std::array<float, kAmbiStochasticMaxVoices> kinetic_ {};
    std::array<float, kAmbiStochasticMaxVoices> contact_ {};
    std::array<float, kAmbiStochasticMaxVoices> crowding_ {};
    std::array<float, kAmbiStochasticMaxVoices> tension_ {};
    std::array<float, kAmbiStochasticMaxVoices> networkPulse_ {};
    std::array<float, kAmbiStochasticMaxVoices> eventHold_ {};
    std::array<float, kAmbiStochasticMaxVoices> eventRefractory_ {};
    std::array<float, kAmbiStochasticMaxVoices> bondBreak_ {};
    std::array<std::array<float, kAmbiStochasticCascadeSlots>, kAmbiStochasticMaxVoices> cascadeDelay_ {};
    float motionPhase_ = 0.0f;
    float activityPhase_ = 0.0f;
    float dynamicsAccumulator_ = 0.0f;
    float globalEnergy_ = 0.20f;
    float globalKinetic_ = 0.0f;
    uint32_t dynamicsTick_ = 0u;
    uint32_t cascadeCursor_ = 0u;
    uint32_t dynamicsSeed_ = 0x7f4a7c15u;
};

} // namespace s3g
