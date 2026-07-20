#pragma once

#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_realtime.h"
#include "s3g_topology.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace s3g {

constexpr uint32_t kAmbiStochasticMaxVoices = 64;
constexpr uint32_t kAmbiStochasticMaxOrder = 7;
constexpr uint32_t kAmbiStochasticMaxChannels = 64;
constexpr uint32_t kAmbiStochasticMaxBreakpoints = 32;
constexpr uint32_t kAmbiStochasticTableSize = 512;
constexpr uint32_t kAmbiStochasticGeneratorCount = 4;
constexpr uint32_t kAmbiStochasticHistorySize = 32;

enum class AmbiStochasticMode : uint32_t {
    Free = 0,
    Midi = 1,
    Both = 2,
};

enum class AmbiStochasticSelection : uint32_t {
    Random = 0,
    Series = 1,
    Weight = 2,
    Tendency = 3,
    Markov = 4,
    Walk = 5,
};

enum class AmbiStochasticTransition : uint32_t {
    Link = 0,
    Merge = 1,
    Vary = 2,
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

struct AmbiStochasticParams {
    uint32_t order = 3;
    uint32_t voices = 12;
    AmbiStochasticMode mode = AmbiStochasticMode::Free;
    AmbiStochasticSelection selection = AmbiStochasticSelection::Walk;
    AmbiStochasticTransition transition = AmbiStochasticTransition::Link;
    AmbiStochasticDistribution amplitudeDistribution = AmbiStochasticDistribution::Cauchy;
    AmbiStochasticDistribution durationDistribution = AmbiStochasticDistribution::Logistic;
    float baseNote = 40.0f;
    float seedSpreadSemitones = 19.0f;
    float detuneCents = 9.0f;
    float frequencyFloorHz = 18.0f;
    uint32_t breakpoints = 16;
    float amplitudeStep = 0.58f;
    float durationStep = 0.52f;
    float amplitudeRange = 0.65f;
    float durationRange = 0.78f;
    float fieldDensity = 0.84f;
    float neighborTransfer = 0.32f;
    float selectionMemory = 0.82f;
    float fieldDurationSeconds = 0.32f;
    float fieldContrast = 0.58f;
    float attackMs = 12.0f;
    float decayMs = 140.0f;
    float sustain = 0.76f;
    float releaseMs = 280.0f;
    uint32_t topologyShape = 11;
    uint32_t topologyMotion = 1;
    float topologyRateHz = 0.035f;
    float topologyAmount = 0.82f;
    float topologyDepth = 0.78f;
    float topologyScale = 1.20f;
    float topologyCollapse = 0.0f;
    float topologyTwist = 0.0f;
    float centerAzimuthDeg = 0.0f;
    float centerElevationDeg = 0.0f;
    float centerDistance = 1.0f;
    float spatialFollow = 0.92f;
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

struct AmbiStochasticGenerator {
    std::array<float, kAmbiStochasticMaxBreakpoints> amplitudes {};
    std::array<float, kAmbiStochasticMaxBreakpoints> durations {};
    std::array<float, kAmbiStochasticMaxBreakpoints> amplitudePrimary {};
    std::array<float, kAmbiStochasticMaxBreakpoints> durationPrimary {};
    std::array<float, kAmbiStochasticTableSize> table {};
    uint32_t seed = 1u;
    uint32_t renderedBreakpoints = 16u;
    float frequency = 55.0f;
};

struct AmbiStochasticVoice {
    std::array<AmbiStochasticGenerator, kAmbiStochasticGeneratorCount> generators {};
    std::array<float, kAmbiStochasticTableSize> currentTable {};
    std::array<float, kAmbiStochasticTableSize> nextTable {};
    std::array<uint8_t, kAmbiStochasticGeneratorCount> seriesOrder { 0u, 1u, 2u, 3u };
    std::array<uint8_t, kAmbiStochasticHistorySize> history {};
    AmbiStochasticEnvelope envelope {};
    uint32_t seed = 1u;
    uint32_t seriesCursor = 0u;
    uint32_t historyCursor = 0u;
    uint32_t currentGenerator = 0u;
    uint32_t nextGenerator = 0u;
    uint32_t selectionHold = 0u;
    float selectorPrimary = 0.0f;
    float selectorSecondary = 1.5f;
    float tendencyCenter = 1.5f;
    float phase = 0.0f;
    float currentFrequency = 55.0f;
    float nextFrequency = 55.0f;
    float energy = 0.0f;
    float velocity = 0.72f;
    float age = 0.0f;
    float fieldRemainingSamples = 1.0f;
    bool fieldActive = true;
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

inline const char* ambiStochasticSelectionName(AmbiStochasticSelection selection)
{
    switch (selection) {
    case AmbiStochasticSelection::Series: return "SERIES";
    case AmbiStochasticSelection::Weight: return "WEIGHT";
    case AmbiStochasticSelection::Tendency: return "TENDENCY";
    case AmbiStochasticSelection::Markov: return "MARKOV";
    case AmbiStochasticSelection::Walk: return "WALK";
    case AmbiStochasticSelection::Random:
    default: return "RANDOM";
    }
}

inline const char* ambiStochasticTransitionName(AmbiStochasticTransition transition)
{
    switch (transition) {
    case AmbiStochasticTransition::Merge: return "MERGE";
    case AmbiStochasticTransition::Vary: return "VARY";
    case AmbiStochasticTransition::Link:
    default: return "LINK";
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

class AmbiStochasticEncoder {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        reset();
    }

    void reset()
    {
        topologyPhase_ = 0.0f;
        topologySeed_ = 0x7f4a7c15u;
        globalEnergy_ = 0.0f;
        globalKinetic_ = 0.0f;
        smoothedOutputGain_ = dbToGain(params_.outputGainDb)
            / std::sqrt(static_cast<float>(std::max<uint32_t>(1u, params_.voices)));
        for (uint32_t voice = 0u; voice < kAmbiStochasticMaxVoices; ++voice) {
            initializeVoice(voice);
            const auto base = baseTopologyPoint(voice, std::max<uint32_t>(1u, params_.voices));
            topologySecondary_[voice] = {
                static_cast<float>(base[0]), static_cast<float>(base[1]), static_cast<float>(base[2])
            };
            topologyPrimary_[voice] = {
                deterministicSigned(voice * 17u + 1u) * 0.035f,
                deterministicSigned(voice * 31u + 3u) * 0.035f,
                deterministicSigned(voice * 47u + 5u) * 0.035f
            };
            topologyPosition_[voice] = topologySecondary_[voice];
            topologyPrevious_[voice] = topologySecondary_[voice];
            neighborIndex_[voice] = (voice + 1u) % kAmbiStochasticMaxVoices;
            secondaryNeighborIndex_[voice] = (voice + 2u) % kAmbiStochasticMaxVoices;
        }
        bool anyFieldActive = false;
        for (uint32_t voice = 0u; voice < params_.voices; ++voice) {
            anyFieldActive = anyFieldActive || voices_[voice].fieldActive;
        }
        if (!anyFieldActive && params_.fieldDensity > 0.0f) voices_[0].fieldActive = true;
        updateTopology(0.0f);
    }

    void setParams(AmbiStochasticParams params)
    {
        params.order = std::clamp<uint32_t>(params.order, 1u, kAmbiStochasticMaxOrder);
        params.voices = std::clamp<uint32_t>(params.voices, 1u, kAmbiStochasticMaxVoices);
        params.mode = static_cast<AmbiStochasticMode>(std::clamp<uint32_t>(static_cast<uint32_t>(params.mode), 0u, 2u));
        params.selection = static_cast<AmbiStochasticSelection>(std::clamp<uint32_t>(static_cast<uint32_t>(params.selection), 0u, 5u));
        params.transition = static_cast<AmbiStochasticTransition>(std::clamp<uint32_t>(static_cast<uint32_t>(params.transition), 0u, 2u));
        params.amplitudeDistribution = static_cast<AmbiStochasticDistribution>(
            std::clamp<uint32_t>(static_cast<uint32_t>(params.amplitudeDistribution), 0u, 6u));
        params.durationDistribution = static_cast<AmbiStochasticDistribution>(
            std::clamp<uint32_t>(static_cast<uint32_t>(params.durationDistribution), 0u, 6u));
        params.baseNote = clamp(params.baseNote, 12.0f, 96.0f);
        params.seedSpreadSemitones = clamp(params.seedSpreadSemitones, 0.0f, 48.0f);
        params.detuneCents = clamp(params.detuneCents, 0.0f, 100.0f);
        params.frequencyFloorHz = clamp(params.frequencyFloorHz, 2.0f, 240.0f);
        params.breakpoints = std::clamp<uint32_t>(params.breakpoints, 4u, kAmbiStochasticMaxBreakpoints);
        params.amplitudeStep = clamp(params.amplitudeStep, 0.0f, 1.0f);
        params.durationStep = clamp(params.durationStep, 0.0f, 1.0f);
        params.amplitudeRange = clamp(params.amplitudeRange, 0.0f, 1.0f);
        params.durationRange = clamp(params.durationRange, 0.0f, 1.0f);
        params.fieldDensity = clamp(params.fieldDensity, 0.0f, 1.0f);
        params.neighborTransfer = clamp(params.neighborTransfer, 0.0f, 1.0f);
        params.selectionMemory = clamp(params.selectionMemory, 0.0f, 1.0f);
        params.fieldDurationSeconds = clamp(params.fieldDurationSeconds, 0.05f, 30.0f);
        params.fieldContrast = clamp(params.fieldContrast, 0.0f, 1.0f);
        params.attackMs = clamp(params.attackMs, 1.0f, 4000.0f);
        params.decayMs = clamp(params.decayMs, 5.0f, 8000.0f);
        params.sustain = clamp(params.sustain, 0.0f, 1.0f);
        params.releaseMs = clamp(params.releaseMs, 5.0f, 12000.0f);
        params.topologyShape = std::min<uint32_t>(params.topologyShape, kTopologyShapeCount - 1u);
        params.topologyMotion = std::min<uint32_t>(params.topologyMotion, kTopologyMotionModeCount - 1u);
        params.topologyRateHz = clamp(params.topologyRateHz, 0.001f, 1.0f);
        params.topologyAmount = clamp(params.topologyAmount, 0.0f, 1.0f);
        params.topologyDepth = clamp(params.topologyDepth, 0.0f, 1.0f);
        params.topologyScale = clamp(params.topologyScale, 0.25f, 2.0f);
        params.topologyCollapse = clamp(params.topologyCollapse, 0.0f, 1.0f);
        params.topologyTwist = clamp(params.topologyTwist, -1.0f, 1.0f);
        params.centerAzimuthDeg = wrapSignedDeg(params.centerAzimuthDeg);
        params.centerElevationDeg = clamp(params.centerElevationDeg, -90.0f, 90.0f);
        params.centerDistance = clamp(params.centerDistance, 0.15f, 2.0f);
        params.spatialFollow = clamp(params.spatialFollow, 0.0f, 1.0f);
        params.outputGainDb = clamp(params.outputGainDb, -60.0f, 6.0f);

        const uint32_t oldVoices = params_.voices;
        const uint32_t oldBreakpoints = params_.breakpoints;
        params_ = params;
        if (params_.voices > oldVoices) {
            for (uint32_t voice = oldVoices; voice < params_.voices; ++voice) {
                initializeVoice(voice);
                const auto base = baseTopologyPoint(voice, params_.voices);
                topologySecondary_[voice] = {
                    static_cast<float>(base[0]), static_cast<float>(base[1]), static_cast<float>(base[2])
                };
                topologyPrimary_[voice] = {
                    deterministicSigned(voice * 17u + 1u) * 0.035f,
                    deterministicSigned(voice * 31u + 3u) * 0.035f,
                    deterministicSigned(voice * 47u + 5u) * 0.035f
                };
                topologyPosition_[voice] = topologySecondary_[voice];
                topologyPrevious_[voice] = topologySecondary_[voice];
            }
        }
        if (params_.breakpoints != oldBreakpoints) {
            for (uint32_t voice = 0u; voice < params_.voices; ++voice) {
                initializeVoiceGenerators(voice, voices_[voice].midiRole
                    ? midiToHz(static_cast<float>(voices_[voice].note)) : seedFrequencyForVoice(voice, 0u));
            }
        }
        updateTopology(0.0f);
    }

    AmbiStochasticParams params() const { return params_; }
    const std::array<AmbiStochasticPoint, kAmbiStochasticMaxVoices>& points() const { return points_; }
    Vec3 topologyPosition(uint32_t voice) const
    {
        return topologyPosition_[std::min<uint32_t>(voice, kAmbiStochasticMaxVoices - 1u)];
    }
    const std::array<uint32_t, kAmbiStochasticMaxVoices>& neighborIndices() const { return neighborIndex_; }
    const std::array<uint32_t, kAmbiStochasticMaxVoices>& secondaryNeighborIndices() const { return secondaryNeighborIndex_; }
    float globalEnergy() const { return globalEnergy_; }
    float globalKinetic() const { return globalKinetic_; }
    float voiceEnergy(uint32_t voice) const { return voiceState(voice).energy; }
    float voiceFrequency(uint32_t voice) const { return voiceState(voice).currentFrequency; }
    float voicePeriodRatio(uint32_t voice) const
    {
        const float seed = seedFrequencyForVoice(std::min<uint32_t>(voice, params_.voices - 1u), 0u);
        return voiceFrequency(voice) / std::max(0.001f, seed);
    }
    float voiceKinetic(uint32_t voice) const { return kinetic_[safeVoice(voice)]; }
    float voiceContact(uint32_t voice) const { return neighborInfluence_[safeVoice(voice)]; }
    float voiceCrowding(uint32_t voice) const { return crowding_[safeVoice(voice)]; }
    float voiceTension(uint32_t voice) const { return topologyRadius_[safeVoice(voice)]; }
    float voiceNetworkPulse(uint32_t voice) const { return selectionPulse_[safeVoice(voice)]; }
    float voiceBondStrength(uint32_t) const { return 1.0f; }
    bool voiceFieldActive(uint32_t voice) const { return voiceState(voice).fieldActive; }
    uint32_t currentGenerator(uint32_t voice) const { return voiceState(voice).currentGenerator; }
    uint32_t nextGenerator(uint32_t voice) const { return voiceState(voice).nextGenerator; }
    const std::array<uint8_t, kAmbiStochasticHistorySize>& selectionHistory(uint32_t voice) const
    {
        return voiceState(voice).history;
    }
    uint32_t selectionHistoryCursor(uint32_t voice) const { return voiceState(voice).historyCursor; }
    const std::array<float, kAmbiStochasticTableSize>& waveform(uint32_t voice) const
    {
        return voiceState(voice).currentTable;
    }
    const std::array<float, kAmbiStochasticTableSize>& nextWaveform(uint32_t voice) const
    {
        return voiceState(voice).nextTable;
    }
    uint32_t breakpointCount(uint32_t voice) const
    {
        const auto& state = voiceState(voice);
        return state.generators[state.nextGenerator].renderedBreakpoints;
    }
    float breakpointAmplitude(uint32_t voice, uint32_t point) const
    {
        const auto& state = voiceState(voice);
        const auto& generator = state.generators[state.nextGenerator];
        return generator.amplitudes[std::min<uint32_t>(point, kAmbiStochasticMaxBreakpoints - 1u)];
    }
    float breakpointDuration(uint32_t voice, uint32_t point) const
    {
        const auto& state = voiceState(voice);
        const auto& generator = state.generators[state.nextGenerator];
        return generator.durations[std::min<uint32_t>(point, kAmbiStochasticMaxBreakpoints - 1u)];
    }
    float generatorAmplitudePrimary(uint32_t voice, uint32_t generator, uint32_t point) const
    {
        const auto& state = voiceState(voice);
        const auto& source = state.generators[std::min<uint32_t>(generator, kAmbiStochasticGeneratorCount - 1u)];
        return source.amplitudePrimary[std::min<uint32_t>(point, kAmbiStochasticMaxBreakpoints - 1u)];
    }
    float generatorDurationPrimary(uint32_t voice, uint32_t generator, uint32_t point) const
    {
        const auto& state = voiceState(voice);
        const auto& source = state.generators[std::min<uint32_t>(generator, kAmbiStochasticGeneratorCount - 1u)];
        return source.durationPrimary[std::min<uint32_t>(point, kAmbiStochasticMaxBreakpoints - 1u)];
    }
    float amplitudePrimaryBarrier(uint32_t voice) const
    {
        const Vec3 p = topologyPosition(voice);
        return amplitudePrimaryLimit(p);
    }
    float durationPrimaryBarrier(uint32_t voice) const
    {
        const Vec3 p = topologyPosition(voice);
        const auto& state = voiceState(voice);
        return durationPrimaryLimit(p, state.generators[state.nextGenerator]);
    }

    void noteOn(int note, float velocity)
    {
        uint32_t selected = 0u;
        float oldest = -1.0f;
        for (uint32_t voice = 0u; voice < params_.voices; ++voice) {
            if (!voices_[voice].midiRole || voices_[voice].envelope.stage == AmbiStochasticEnvelopeStage::Idle) {
                selected = voice;
                break;
            }
            if (voices_[voice].age > oldest) {
                oldest = voices_[voice].age;
                selected = voice;
            }
        }
        auto& voice = voices_[selected];
        voice.note = std::clamp(note, 0, 127);
        voice.velocity = clamp(velocity, 0.0f, 1.0f);
        voice.midiGate = true;
        voice.midiRole = true;
        voice.age = 0.0f;
        voice.phase = 0.0f;
        initializeVoiceGenerators(selected, midiToHz(static_cast<float>(voice.note)));
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
        for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
            if (outputs[channel]) std::fill(outputs[channel], outputs[channel] + frames, 0.0f);
        }

        const uint32_t voices = params_.voices;
        const uint32_t ambiChannels = std::min<uint32_t>(ambiChannelsForOrder(params_.order), outputChannels);
        const float targetOutputGain = dbToGain(params_.outputGainDb)
            / std::sqrt(static_cast<float>(std::max<uint32_t>(1u, voices)));
        const float attackCoef = envelopeCoefficient(params_.attackMs);
        const float decayCoef = envelopeCoefficient(params_.decayMs);
        const float releaseCoef = envelopeCoefficient(params_.releaseMs);
        constexpr uint32_t kControlFrames = 16u;

        for (uint32_t chunkStart = 0u; chunkStart < frames; chunkStart += kControlFrames) {
            const uint32_t chunkFrames = std::min<uint32_t>(kControlFrames, frames - chunkStart);
            const float dt = static_cast<float>(chunkFrames / sampleRate_);
            updateTopology(dt);
            updateTimeFields(static_cast<float>(chunkFrames));

            std::array<std::array<float, kAmbiStochasticMaxChannels>, kAmbiStochasticMaxVoices> basis {};
            std::array<float, kAmbiStochasticMaxVoices> distanceGain {};
            std::array<float, kAmbiStochasticMaxVoices> energySum {};
            for (uint32_t voice = 0u; voice < voices; ++voice) {
                basis[voice] = acnSn3dBasis7(directionFromAed(points_[voice].azimuthDeg, points_[voice].elevationDeg));
                distanceGain[voice] = 1.0f / std::max(0.5f, points_[voice].distance);
            }

            for (uint32_t frame = chunkStart; frame < chunkStart + chunkFrames; ++frame) {
                smoothedOutputGain_ += (targetOutputGain - smoothedOutputGain_) * 0.0015f;
                for (uint32_t voiceIndex = 0u; voiceIndex < voices; ++voiceIndex) {
                    auto& voice = voices_[voiceIndex];
                    const bool midiGate = voice.midiRole && voice.midiGate;
                    bool gate = false;
                    if (params_.mode == AmbiStochasticMode::Midi) gate = midiGate;
                    else if (params_.mode == AmbiStochasticMode::Both) gate = midiGate || voice.fieldActive;
                    else gate = voice.fieldActive;
                    voice.envelope.setGate(gate);
                    const float envelope = voice.envelope.process(attackCoef, decayCoef, params_.sustain, releaseCoef);
                    if (voice.midiRole && !voice.midiGate
                        && voice.envelope.stage == AmbiStochasticEnvelopeStage::Idle) {
                        voice.midiRole = false;
                    }
                    if (envelope <= 0.000001f) continue;

                    const float increment = std::min(voice.currentFrequency,
                        static_cast<float>(sampleRate_) * 0.42f) / static_cast<float>(sampleRate_);
                    const float nextPhase = voice.phase + increment;
                    const bool wrapped = nextPhase >= 1.0f;
                    voice.phase = nextPhase - std::floor(nextPhase);
                    if (wrapped) advanceWave(voiceIndex);
                    const float transitionPhase = clamp(voice.currentFrequency * 0.0015f, 0.03f, 0.22f);
                    const float transition = clamp(voice.phase / transitionPhase, 0.0f, 1.0f);
                    const float morph = transition * transition * (3.0f - 2.0f * transition);
                    const float sample = lerp(tableSample(voice.currentTable, voice.phase),
                        tableSample(voice.nextTable, voice.phase), morph);
                    const float velocity = voice.midiRole ? std::max(0.04f, voice.velocity) : 0.78f;
                    const float fieldGain = 0.72f + topologyRadius_[voiceIndex] * 0.28f;
                    const float amplitude = std::tanh(sample * 1.18f) * envelope * velocity * fieldGain
                        * smoothedOutputGain_ * distanceGain[voiceIndex];
                    energySum[voiceIndex] += amplitude * amplitude;
                    voice.age += 1.0f / static_cast<float>(sampleRate_);
                    for (uint32_t channel = 0u; channel < ambiChannels; ++channel) {
                        if (outputs[channel]) {
                            outputs[channel][frame] = flushDenormal(outputs[channel][frame]
                                + amplitude * basis[voiceIndex][channel]);
                        }
                    }
                }
            }
            updateEnergy(energySum, chunkFrames);
        }

        for (uint32_t channel = 0u; channel < ambiChannels; ++channel) {
            if (!outputs[channel]) continue;
            for (uint32_t frame = 0u; frame < frames; ++frame) {
                outputs[channel][frame] = std::tanh(clamp(outputs[channel][frame], -6.0f, 6.0f));
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

    static float reflect(float value, float minimum, float maximum)
    {
        if (maximum <= minimum) return minimum;
        if (!std::isfinite(value)) return (minimum + maximum) * 0.5f;
        const float span = maximum - minimum;
        float phase = std::fmod(value - minimum, span * 2.0f);
        if (phase < 0.0f) phase += span * 2.0f;
        if (phase > span) phase = span * 2.0f - phase;
        return minimum + phase;
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

    static float midiToHz(float note)
    {
        return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
    }

    float envelopeCoefficient(float milliseconds) const
    {
        return 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate_ * milliseconds * 0.001));
    }

    uint32_t safeVoice(uint32_t voice) const
    {
        return std::min<uint32_t>(voice, kAmbiStochasticMaxVoices - 1u);
    }

    const AmbiStochasticVoice& voiceState(uint32_t voice) const { return voices_[safeVoice(voice)]; }

    float randomSigned(uint32_t& seed, AmbiStochasticDistribution distribution)
    {
        switch (distribution) {
        case AmbiStochasticDistribution::Gaussian: {
            float sum = 0.0f;
            for (uint32_t i = 0u; i < 6u; ++i) sum += randomUnit(seed);
            return clamp((sum - 3.0f) * 0.78f, -1.0f, 1.0f);
        }
        case AmbiStochasticDistribution::Cauchy: {
            const float u = clamp(randomUnit(seed), 0.001f, 0.999f);
            return clamp(std::tan(kPi * (u - 0.5f)) * 0.18f, -1.0f, 1.0f);
        }
        case AmbiStochasticDistribution::Logistic: {
            const float u = clamp(randomUnit(seed), 0.001f, 0.999f);
            return clamp(std::log(u / (1.0f - u)) * 0.19f, -1.0f, 1.0f);
        }
        case AmbiStochasticDistribution::Arcsine:
            return std::sin(kPi * (randomUnit(seed) - 0.5f));
        case AmbiStochasticDistribution::Exponential: {
            const float u = clamp(randomUnit(seed), 0.0f, 0.9995f);
            const float magnitude = clamp(-std::log(1.0f - u) * 0.26f, 0.0f, 1.0f);
            return randomUnit(seed) < 0.5f ? -magnitude : magnitude;
        }
        case AmbiStochasticDistribution::Binary:
            return randomUnit(seed) < 0.5f ? -1.0f : 1.0f;
        case AmbiStochasticDistribution::Uniform:
        default:
            return randomUnit(seed) * 2.0f - 1.0f;
        }
    }

    float seedFrequencyForVoice(uint32_t voice, uint32_t generator) const
    {
        const float lane = params_.voices <= 1u ? 0.0f
            : static_cast<float>(voice) / static_cast<float>(params_.voices - 1u) * 2.0f - 1.0f;
        static constexpr float generatorOffsets[kAmbiStochasticGeneratorCount] { -0.72f, -0.18f, 0.26f, 0.82f };
        const float spread = lane * params_.seedSpreadSemitones * 0.50f
            + generatorOffsets[std::min<uint32_t>(generator, kAmbiStochasticGeneratorCount - 1u)]
                * params_.seedSpreadSemitones * 0.34f;
        const float detune = deterministicSigned(voice * 0x45d9f3bu + generator * 97u + 19u)
            * params_.detuneCents * 0.01f;
        return clamp(midiToHz(params_.baseNote + spread + detune), 4.0f,
            static_cast<float>(sampleRate_) * 0.35f);
    }

    void initializeVoice(uint32_t voiceIndex)
    {
        auto& voice = voices_[voiceIndex];
        voice = {};
        voice.seed = 0x9e3779b9u ^ ((voiceIndex + 1u) * 0x85ebca6bu);
        if (voice.seed == 0u) voice.seed = 1u;
        voice.phase = randomUnit(voice.seed);
        voice.velocity = 0.72f;
        voice.note = static_cast<int>(std::lround(params_.baseNote));
        voice.fieldActive = randomUnit(voice.seed) < scaledFieldDensity();
        voice.fieldRemainingSamples = static_cast<float>(sampleRate_)
            * params_.fieldDurationSeconds * (0.35f + 0.65f * randomUnit(voice.seed));
        voice.selectorSecondary = static_cast<float>(voiceIndex % kAmbiStochasticGeneratorCount);
        voice.tendencyCenter = voice.selectorSecondary;
        initializeVoiceGenerators(voiceIndex, seedFrequencyForVoice(voiceIndex, 0u));
        shuffleSeries(voice);
    }

    void initializeVoiceGenerators(uint32_t voiceIndex, float seedFrequency)
    {
        auto& voice = voices_[voiceIndex];
        for (uint32_t generatorIndex = 0u; generatorIndex < kAmbiStochasticGeneratorCount; ++generatorIndex) {
            initializeGenerator(voiceIndex, generatorIndex, seedFrequency);
        }
        voice.currentGenerator = 0u;
        voice.nextGenerator = 1u;
        voice.currentTable = voice.generators[0u].table;
        voice.nextTable = voice.generators[1u].table;
        voice.currentFrequency = voice.generators[0u].frequency;
        voice.nextFrequency = voice.generators[1u].frequency;
        voice.selectionHold = 0u;
        voice.history.fill(0u);
        voice.historyCursor = 0u;
        pushHistory(voice, 0u);
        pushHistory(voice, 1u);
    }

    void initializeGenerator(uint32_t voiceIndex, uint32_t generatorIndex, float requestedFrequency)
    {
        auto& voice = voices_[voiceIndex];
        auto& generator = voice.generators[generatorIndex];
        generator = {};
        generator.seed = voice.seed ^ ((generatorIndex + 1u) * 0x27d4eb2du);
        if (generator.seed == 0u) generator.seed = generatorIndex + 1u;
        generator.renderedBreakpoints = params_.breakpoints;
        const float naturalSeed = seedFrequencyForVoice(voiceIndex, generatorIndex);
        const float ratio = requestedFrequency / std::max(0.001f, seedFrequencyForVoice(voiceIndex, 0u));
        const float frequency = clamp(naturalSeed * ratio, 4.0f, static_cast<float>(sampleRate_) * 0.35f);
        const float periodSamples = static_cast<float>(sampleRate_) / frequency;
        float weightSum = 0.0f;
        std::array<float, kAmbiStochasticMaxBreakpoints> weights {};
        for (uint32_t point = 0u; point < generator.renderedBreakpoints; ++point) {
            const float phase = static_cast<float>(point) / static_cast<float>(generator.renderedBreakpoints);
            generator.amplitudes[point] = clamp(std::sin((phase + generatorIndex * 0.071f) * kPi * 2.0f) * 0.46f
                    + randomSigned(generator.seed, params_.amplitudeDistribution) * 0.36f,
                -1.0f, 1.0f);
            generator.amplitudePrimary[point] = randomSigned(generator.seed, params_.amplitudeDistribution) * 0.018f;
            generator.durationPrimary[point] = randomSigned(generator.seed, params_.durationDistribution)
                * periodSamples / static_cast<float>(generator.renderedBreakpoints) * 0.05f;
            weights[point] = 0.35f + randomUnit(generator.seed) * 1.30f;
            weightSum += weights[point];
        }
        for (uint32_t point = 0u; point < generator.renderedBreakpoints; ++point) {
            generator.durations[point] = std::max(1.0f, periodSamples * weights[point] / std::max(0.001f, weightSum));
        }
        renderGenerator(generator);
    }

    void shuffleSeries(AmbiStochasticVoice& voice)
    {
        voice.seriesOrder = { 0u, 1u, 2u, 3u };
        for (uint32_t i = kAmbiStochasticGeneratorCount - 1u; i > 0u; --i) {
            const uint32_t j = randomU32(voice.seed) % (i + 1u);
            std::swap(voice.seriesOrder[i], voice.seriesOrder[j]);
        }
        voice.seriesCursor = 0u;
    }

    void pushHistory(AmbiStochasticVoice& voice, uint32_t generator)
    {
        voice.history[voice.historyCursor % kAmbiStochasticHistorySize] = static_cast<uint8_t>(generator);
        voice.historyCursor = (voice.historyCursor + 1u) % kAmbiStochasticHistorySize;
    }

    float generatorDurationTotal(const AmbiStochasticGenerator& generator) const
    {
        float total = 0.0f;
        for (uint32_t point = 0u; point < generator.renderedBreakpoints; ++point) {
            total += std::max(1.0f, generator.durations[point]);
        }
        return std::max(1.0f, total);
    }

    float minimumFrequencyForGenerator(const Vec3& topology, uint32_t generatorIndex) const
    {
        static constexpr float kGeneratorRegister[kAmbiStochasticGeneratorCount] {
            1.0f, 1.45f, 2.15f, 3.20f
        };
        generatorIndex = std::min<uint32_t>(generatorIndex, kAmbiStochasticGeneratorCount - 1u);
        const float topologyRegister = std::pow(2.0f, (clamp(topology.z, -1.0f, 1.0f) + 1.0f) * 0.75f);
        const float minimumFrequency = clamp(params_.frequencyFloorHz
                * kGeneratorRegister[generatorIndex] * topologyRegister,
            1.0f, static_cast<float>(sampleRate_) * 0.20f);
        return minimumFrequency;
    }

    void constrainGeneratorPeriod(AmbiStochasticGenerator& generator, const Vec3& topology,
        uint32_t generatorIndex)
    {
        const uint32_t points = std::max<uint32_t>(4u, generator.renderedBreakpoints);
        const float total = generatorDurationTotal(generator);
        const float minimumFrequency = minimumFrequencyForGenerator(topology, generatorIndex);
        const float maximumFrequency = std::min(static_cast<float>(sampleRate_) * 0.20f,
            std::max(1200.0f, minimumFrequency * 16.0f));
        const float minimumPeriod = std::max(static_cast<float>(points),
            static_cast<float>(sampleRate_) / maximumFrequency);
        const float maximumPeriod = std::max(minimumPeriod + 1.0f,
            static_cast<float>(sampleRate_) / minimumFrequency);
        const float boundedTotal = reflect(total, minimumPeriod, maximumPeriod);
        if (std::fabs(boundedTotal - total) < 0.0001f) return;

        const float sourceExcess = std::max(0.0001f, total - static_cast<float>(points));
        const float targetExcess = std::max(0.0f, boundedTotal - static_cast<float>(points));
        const float scale = targetExcess / sourceExcess;
        for (uint32_t point = 0u; point < generator.renderedBreakpoints; ++point) {
            generator.durations[point] = 1.0f + std::max(0.0f, generator.durations[point] - 1.0f) * scale;
            generator.durationPrimary[point] *= -0.82f;
        }
    }

    float amplitudePrimaryLimit(const Vec3& topology) const
    {
        const float xBias = 0.72f + 0.36f * (topology.x * 0.5f + 0.5f);
        return 0.002f + params_.amplitudeRange * params_.amplitudeRange * 0.19f * xBias;
    }

    float durationPrimaryLimit(const Vec3& topology, const AmbiStochasticGenerator& generator) const
    {
        const float average = generatorDurationTotal(generator)
            / static_cast<float>(std::max<uint32_t>(4u, generator.renderedBreakpoints));
        const float zBias = 0.62f + 0.76f * (topology.z * 0.5f + 0.5f);
        return std::max(0.02f, average * (0.01f + params_.durationRange * params_.durationRange * 0.52f) * zBias);
    }

    void evolveGenerator(uint32_t voiceIndex, uint32_t generatorIndex)
    {
        auto& voice = voices_[voiceIndex];
        auto& generator = voice.generators[generatorIndex];
        const Vec3 topology = topologyPosition_[voiceIndex];
        const float radius = topologyRadius_[voiceIndex];
        const float amplitudeLimit = amplitudePrimaryLimit(topology);
        const float durationLimit = durationPrimaryLimit(topology, generator);
        const float amplitudeDrive = params_.amplitudeStep * (0.28f + radius * 0.92f);
        const float durationDrive = params_.durationStep * (0.24f + radius * 0.96f);
        const uint32_t neighbor = neighborIndex_[voiceIndex];
        const auto& neighborGenerator = voices_[neighbor].generators[voices_[neighbor].currentGenerator];
        const float transfer = params_.neighborTransfer * neighborInfluence_[voiceIndex] * 0.34f;
        for (uint32_t point = 0u; point < generator.renderedBreakpoints; ++point) {
            const float amplitudeAcceleration = randomSigned(generator.seed, params_.amplitudeDistribution)
                    * amplitudeLimit * amplitudeDrive
                + topology.x * amplitudeLimit * 0.055f * params_.amplitudeStep;
            generator.amplitudePrimary[point] = reflect(generator.amplitudePrimary[point] + amplitudeAcceleration,
                -amplitudeLimit, amplitudeLimit);
            generator.amplitudePrimary[point] = lerp(generator.amplitudePrimary[point],
                neighborGenerator.amplitudePrimary[point], transfer);
            generator.amplitudes[point] = reflect(generator.amplitudes[point] + generator.amplitudePrimary[point],
                -1.0f, 1.0f);

            const float durationAcceleration = randomSigned(generator.seed, params_.durationDistribution)
                    * durationLimit * durationDrive
                + topology.z * durationLimit * 0.045f * params_.durationStep;
            generator.durationPrimary[point] = reflect(generator.durationPrimary[point] + durationAcceleration,
                -durationLimit, durationLimit);
            generator.durationPrimary[point] = lerp(generator.durationPrimary[point],
                neighborGenerator.durationPrimary[point], transfer * 0.72f);
            generator.durations[point] = std::max(1.0f,
                generator.durations[point] + generator.durationPrimary[point]);
        }
        constrainGeneratorPeriod(generator, topology, generatorIndex);
        renderGenerator(generator);
    }

    void renderGenerator(AmbiStochasticGenerator& generator)
    {
        const uint32_t points = std::clamp<uint32_t>(generator.renderedBreakpoints, 4u, kAmbiStochasticMaxBreakpoints);
        const float total = generatorDurationTotal(generator);
        std::array<float, kAmbiStochasticMaxBreakpoints + 1u> cumulative {};
        for (uint32_t point = 0u; point < points; ++point) {
            cumulative[point + 1u] = cumulative[point] + generator.durations[point] / total;
        }
        cumulative[points] = 1.0f;
        uint32_t segment = 0u;
        float mean = 0.0f;
        for (uint32_t sample = 0u; sample < kAmbiStochasticTableSize; ++sample) {
            const float phase = static_cast<float>(sample) / static_cast<float>(kAmbiStochasticTableSize);
            while (segment + 1u < points && phase >= cumulative[segment + 1u]) ++segment;
            const float start = cumulative[segment];
            const float end = cumulative[segment + 1u];
            const float local = clamp((phase - start) / std::max(0.000001f, end - start), 0.0f, 1.0f);
            const float value = lerp(generator.amplitudes[segment], generator.amplitudes[(segment + 1u) % points], local);
            generator.table[sample] = value;
            mean += value;
        }
        mean /= static_cast<float>(kAmbiStochasticTableSize);
        float peak = 0.0f;
        for (float& value : generator.table) {
            value -= mean;
            peak = std::max(peak, std::fabs(value));
        }
        const float normalizer = peak > 0.00001f ? std::min(1.0f, 0.94f / peak) : 1.0f;
        for (float& value : generator.table) value *= normalizer;
        generator.frequency = clamp(static_cast<float>(sampleRate_) / total, 1.0f,
            static_cast<float>(sampleRate_) * 0.42f);
    }

    uint32_t weightedChoice(AmbiStochasticVoice& voice, const std::array<float, kAmbiStochasticGeneratorCount>& weights)
    {
        float total = 0.0f;
        for (float value : weights) total += std::max(0.0f, value);
        float target = randomUnit(voice.seed) * std::max(0.0001f, total);
        for (uint32_t generator = 0u; generator < kAmbiStochasticGeneratorCount; ++generator) {
            target -= std::max(0.0f, weights[generator]);
            if (target <= 0.0f) return generator;
        }
        return kAmbiStochasticGeneratorCount - 1u;
    }

    uint32_t selectGenerator(uint32_t voiceIndex)
    {
        auto& voice = voices_[voiceIndex];
        if (voice.selectionHold > 0u) {
            --voice.selectionHold;
            return voice.nextGenerator;
        }
        const Vec3 topology = topologyPosition_[voiceIndex];
        uint32_t selected = voice.nextGenerator;
        switch (params_.selection) {
        case AmbiStochasticSelection::Series:
            if (voice.seriesCursor >= kAmbiStochasticGeneratorCount) shuffleSeries(voice);
            selected = voice.seriesOrder[voice.seriesCursor++];
            break;
        case AmbiStochasticSelection::Weight: {
            std::array<float, kAmbiStochasticGeneratorCount> weights {};
            const float target = (topology.x * 0.5f + 0.5f) * 3.0f;
            for (uint32_t generator = 0u; generator < kAmbiStochasticGeneratorCount; ++generator) {
                const float distance = std::fabs(static_cast<float>(generator) - target);
                weights[generator] = 0.08f + std::exp(-distance * (1.4f + params_.selectionMemory * 3.8f));
            }
            selected = weightedChoice(voice, weights);
            break;
        }
        case AmbiStochasticSelection::Tendency: {
            const float target = (topology.y * 0.5f + 0.5f) * 3.0f;
            const float follow = 0.04f + (1.0f - params_.selectionMemory) * 0.32f;
            voice.tendencyCenter += (target - voice.tendencyCenter) * follow;
            std::array<float, kAmbiStochasticGeneratorCount> weights {};
            for (uint32_t generator = 0u; generator < kAmbiStochasticGeneratorCount; ++generator) {
                const float distance = std::fabs(static_cast<float>(generator) - voice.tendencyCenter);
                weights[generator] = 0.05f + std::exp(-distance * (1.1f + params_.selectionMemory * 5.0f));
            }
            selected = weightedChoice(voice, weights);
            break;
        }
        case AmbiStochasticSelection::Markov: {
            const uint32_t current = voice.nextGenerator;
            std::array<float, kAmbiStochasticGeneratorCount> weights {};
            weights[current] = 0.15f + params_.selectionMemory * 2.8f;
            weights[(current + 1u) & 3u] = 0.45f + std::max(0.0f, topology.x) * 1.8f;
            weights[(current + 3u) & 3u] = 0.45f + std::max(0.0f, -topology.x) * 1.8f;
            weights[(current + 2u) & 3u] = 0.20f + std::fabs(topology.y) * 1.4f;
            selected = weightedChoice(voice, weights);
            break;
        }
        case AmbiStochasticSelection::Walk: {
            const float acceleration = randomSigned(voice.seed, params_.durationDistribution)
                    * (0.08f + (1.0f - params_.selectionMemory) * 0.72f)
                + topology.x * 0.06f;
            voice.selectorPrimary = reflect(voice.selectorPrimary + acceleration, -0.85f, 0.85f);
            voice.selectorSecondary = reflect(voice.selectorSecondary + voice.selectorPrimary, 0.0f, 3.0f);
            selected = static_cast<uint32_t>(std::lround(voice.selectorSecondary));
            break;
        }
        case AmbiStochasticSelection::Random:
        default:
            selected = randomU32(voice.seed) % kAmbiStochasticGeneratorCount;
            break;
        }

        const uint32_t neighbor = neighborIndex_[voiceIndex];
        const float transferChance = params_.neighborTransfer * neighborInfluence_[voiceIndex] * 0.55f;
        if (neighbor != voiceIndex && randomUnit(voice.seed) < transferChance) {
            selected = voices_[neighbor].nextGenerator;
        }
        const float holdShape = params_.selectionMemory * params_.selectionMemory;
        const uint32_t baseHold = static_cast<uint32_t>(std::lround(holdShape * 24.0f));
        const uint32_t variation = static_cast<uint32_t>(randomUnit(voice.seed)
            * (2.0f + holdShape * 8.0f));
        voice.selectionHold = baseHold + variation;
        return std::min<uint32_t>(selected, kAmbiStochasticGeneratorCount - 1u);
    }

    void advanceWave(uint32_t voiceIndex)
    {
        auto& voice = voices_[voiceIndex];
        voice.currentTable = voice.nextTable;
        voice.currentFrequency = voice.nextFrequency;
        voice.currentGenerator = voice.nextGenerator;
        const uint32_t selected = selectGenerator(voiceIndex);
        evolveGenerator(voiceIndex, selected);
        const auto& selectedGenerator = voice.generators[selected];
        const auto& previousGenerator = voice.generators[voice.currentGenerator];
        const float topologyMix = clamp(0.35f + topologyRadius_[voiceIndex] * 0.50f, 0.0f, 1.0f);

        if (params_.transition == AmbiStochasticTransition::Merge) {
            for (uint32_t sample = 0u; sample < kAmbiStochasticTableSize; ++sample) {
                const float phase = static_cast<float>(sample) / static_cast<float>(kAmbiStochasticTableSize);
                const float weave = 0.5f + 0.5f * std::sin((phase * (2.0f + selected) + topologyPosition_[voiceIndex].y) * kPi * 2.0f);
                voice.nextTable[sample] = lerp(previousGenerator.table[sample], selectedGenerator.table[sample], weave);
            }
            voice.nextFrequency = std::sqrt(std::max(1.0f, previousGenerator.frequency * selectedGenerator.frequency));
        } else if (params_.transition == AmbiStochasticTransition::Vary) {
            for (uint32_t sample = 0u; sample < kAmbiStochasticTableSize; ++sample) {
                voice.nextTable[sample] = lerp(previousGenerator.table[sample], selectedGenerator.table[sample], topologyMix);
            }
            voice.nextFrequency = lerp(previousGenerator.frequency, selectedGenerator.frequency, topologyMix);
        } else {
            voice.nextTable = selectedGenerator.table;
            voice.nextFrequency = selectedGenerator.frequency;
        }
        voice.nextGenerator = selected;
        selectionPulse_[voiceIndex] = selected == voice.currentGenerator ? 0.0f : 1.0f;
        pushHistory(voice, selected);
    }

    static float tableSample(const std::array<float, kAmbiStochasticTableSize>& table, float phase)
    {
        const float position = phase * static_cast<float>(kAmbiStochasticTableSize);
        const uint32_t index = static_cast<uint32_t>(position) % kAmbiStochasticTableSize;
        const uint32_t next = (index + 1u) % kAmbiStochasticTableSize;
        return lerp(table[index], table[next], position - std::floor(position));
    }

    float scaledFieldDensity() const
    {
        const float densityExponent = 1.0f + std::max(0.0f,
            std::log2(static_cast<float>(params_.voices) / 8.0f)) * 1.5f;
        return std::pow(params_.fieldDensity, densityExponent);
    }

    void updateTimeFields(float frames)
    {
        for (uint32_t voiceIndex = 0u; voiceIndex < params_.voices; ++voiceIndex) {
            auto& voice = voices_[voiceIndex];
            voice.fieldRemainingSamples -= frames;
            if (voice.fieldRemainingSamples > 0.0f) continue;
            const Vec3 topology = topologyPosition_[voiceIndex];
            const float neighborGate = voices_[neighborIndex_[voiceIndex]].fieldActive ? 1.0f : 0.0f;
            const float probability = clamp(scaledFieldDensity()
                    + topology.y * params_.fieldContrast * 0.22f
                    + (neighborGate - 0.5f) * params_.neighborTransfer * 0.16f,
                0.01f, 0.99f);
            voice.fieldActive = randomUnit(voice.seed) < probability;
            const float u = clamp(randomUnit(voice.seed), 0.001f, 0.999f);
            const float exponential = -std::log(1.0f - u);
            const float topologyDuration = std::exp(-topology.y * params_.fieldContrast * 0.85f);
            const float seconds = clamp(params_.fieldDurationSeconds
                    * (0.18f + exponential * (0.45f + params_.fieldContrast * 0.70f)) * topologyDuration,
                0.03f, 30.0f);
            voice.fieldRemainingSamples += seconds * static_cast<float>(sampleRate_);
        }
    }

    void updateTopology(float dt)
    {
        if (dt > 0.0f) topologyPhase_ = std::fmod(topologyPhase_ + params_.topologyRateHz * dt, 1.0f);
        TopologyState state {};
        state.amount = params_.topologyAmount;
        state.jitter = params_.topologyDepth * 0.24f;
        state.collapse = params_.topologyCollapse;
        state.dirX = std::sin(topologyPhase_ * kPi * 2.0f);
        state.dirY = std::sin(topologyPhase_ * kPi * 1.37f + 1.1f);
        state.dirZ = std::cos(topologyPhase_ * kPi * 1.73f + 0.4f);
        state.twist = params_.topologyTwist;
        state.flare = (params_.topologyScale - 1.0f) * 0.42f;
        state.shape = params_.topologyShape;
        state.motionMode = params_.topologyMotion;
        state.motionVariant = 0u;
        state.motionRateHz = params_.topologyRateHz;
        state.motionDepth = params_.topologyDepth;
        state.motionPhase = topologyPhase_;
        const TopologyControls controls = topologyControlsFromState(state);

        const bool moving = params_.topologyMotion != 0u && params_.topologyDepth > 0.0001f && dt > 0.0f;
        const float velocityLimit = 0.10f + params_.topologyDepth * 0.72f;
        const float walkDrive = (0.35f + params_.topologyDepth * 1.85f)
            * (0.28f + std::sqrt(params_.topologyRateHz));
        const float smoothing = dt > 0.0f ? 1.0f - std::exp(-dt / 0.070f) : 1.0f;
        float kineticTotal = 0.0f;
        for (uint32_t voice = 0u; voice < params_.voices; ++voice) {
            if (moving) {
                Vec3& primary = topologyPrimary_[voice];
                Vec3& secondary = topologySecondary_[voice];
                primary.x = reflect(primary.x + randomSigned(topologySeed_, AmbiStochasticDistribution::Cauchy) * walkDrive * dt,
                    -velocityLimit, velocityLimit);
                primary.y = reflect(primary.y + randomSigned(topologySeed_, AmbiStochasticDistribution::Logistic) * walkDrive * dt,
                    -velocityLimit, velocityLimit);
                primary.z = reflect(primary.z + randomSigned(topologySeed_, AmbiStochasticDistribution::Gaussian) * walkDrive * dt,
                    -velocityLimit, velocityLimit);
                const float travel = 0.80f + params_.topologyRateHz * 8.0f;
                secondary.x = reflect(secondary.x + primary.x * dt * travel, -1.0f, 1.0f);
                secondary.y = reflect(secondary.y + primary.y * dt * travel, -1.0f, 1.0f);
                secondary.z = reflect(secondary.z + primary.z * dt * travel, -1.0f, 1.0f);
            }

            const TopologyPoint anchor = topologyPointForLane(voice, params_.voices, controls);
            const float depth = params_.topologyDepth;
            Vec3 target {
                static_cast<float>(anchor.x) * (1.0f - depth * 0.32f) + topologySecondary_[voice].x * depth * 0.88f,
                static_cast<float>(anchor.y) * (1.0f - depth * 0.32f) + topologySecondary_[voice].y * depth * 0.88f,
                static_cast<float>(anchor.z) * (1.0f - depth * 0.32f) + topologySecondary_[voice].z * depth * 0.88f
            };
            target.x = clamp(target.x * params_.topologyScale, -1.0f, 1.0f);
            target.y = clamp(target.y * params_.topologyScale, -1.0f, 1.0f);
            target.z = clamp(target.z * params_.topologyScale, -1.0f, 1.0f);
            topologyPosition_[voice].x += (target.x - topologyPosition_[voice].x) * smoothing;
            topologyPosition_[voice].y += (target.y - topologyPosition_[voice].y) * smoothing;
            topologyPosition_[voice].z += (target.z - topologyPosition_[voice].z) * smoothing;
            const float dx = topologyPosition_[voice].x - topologyPrevious_[voice].x;
            const float dy = topologyPosition_[voice].y - topologyPrevious_[voice].y;
            const float dz = topologyPosition_[voice].z - topologyPrevious_[voice].z;
            kinetic_[voice] = dt > 0.0f ? clamp(std::sqrt(dx * dx + dy * dy + dz * dz) / std::max(0.0001f, dt) * 0.08f, 0.0f, 1.0f) : 0.0f;
            topologyPrevious_[voice] = topologyPosition_[voice];
            kineticTotal += kinetic_[voice];
            topologyRadius_[voice] = clamp(std::sqrt(topologyPosition_[voice].x * topologyPosition_[voice].x
                + topologyPosition_[voice].y * topologyPosition_[voice].y
                + topologyPosition_[voice].z * topologyPosition_[voice].z) / 1.7320508f, 0.0f, 1.0f);
        }
        globalKinetic_ += (kineticTotal / static_cast<float>(params_.voices) - globalKinetic_) * 0.08f;
        updateNeighborGraph();
        updateAedPoints();
    }

    void updateNeighborGraph()
    {
        for (uint32_t voice = 0u; voice < params_.voices; ++voice) {
            float firstDistance = std::numeric_limits<float>::max();
            float secondDistance = std::numeric_limits<float>::max();
            uint32_t first = voice;
            uint32_t second = voice;
            for (uint32_t other = 0u; other < params_.voices; ++other) {
                if (other == voice) continue;
                const float dx = topologyPosition_[voice].x - topologyPosition_[other].x;
                const float dy = topologyPosition_[voice].y - topologyPosition_[other].y;
                const float dz = topologyPosition_[voice].z - topologyPosition_[other].z;
                const float distance = dx * dx + dy * dy + dz * dz;
                if (distance < firstDistance) {
                    secondDistance = firstDistance;
                    second = first;
                    firstDistance = distance;
                    first = other;
                } else if (distance < secondDistance) {
                    secondDistance = distance;
                    second = other;
                }
            }
            neighborIndex_[voice] = first;
            secondaryNeighborIndex_[voice] = second;
            const float nearest = std::sqrt(std::max(0.0f, firstDistance));
            neighborInfluence_[voice] = clamp(1.0f - nearest / 1.45f, 0.0f, 1.0f);
            uint32_t close = 0u;
            for (uint32_t other = 0u; other < params_.voices; ++other) {
                if (other == voice) continue;
                const float dx = topologyPosition_[voice].x - topologyPosition_[other].x;
                const float dy = topologyPosition_[voice].y - topologyPosition_[other].y;
                const float dz = topologyPosition_[voice].z - topologyPosition_[other].z;
                if (dx * dx + dy * dy + dz * dz < 0.75f) ++close;
            }
            crowding_[voice] = params_.voices > 1u
                ? static_cast<float>(close) / static_cast<float>(params_.voices - 1u) : 0.0f;
            selectionPulse_[voice] *= 0.92f;
        }
    }

    void updateAedPoints()
    {
        for (uint32_t voice = 0u; voice < params_.voices; ++voice) {
            const Vec3 topology = topologyPosition_[voice];
            const float length = std::sqrt(topology.x * topology.x + topology.y * topology.y + topology.z * topology.z);
            const float localAzimuth = length > 0.0001f
                ? std::atan2(topology.x, topology.z) * 180.0f / kPi : 0.0f;
            const float localElevation = length > 0.0001f
                ? std::asin(clamp(topology.y / length, -1.0f, 1.0f)) * 180.0f / kPi : 0.0f;
            points_[voice].azimuthDeg = wrapSignedDeg(params_.centerAzimuthDeg + localAzimuth * params_.spatialFollow);
            points_[voice].elevationDeg = clamp(params_.centerElevationDeg + localElevation * params_.spatialFollow,
                -90.0f, 90.0f);
            const float localDistance = clamp(length, 0.15f, 2.0f);
            points_[voice].distance = clamp(params_.centerDistance
                    * lerp(1.0f, localDistance, params_.spatialFollow),
                0.15f, 2.0f);
        }
    }

    void updateEnergy(const std::array<float, kAmbiStochasticMaxVoices>& sum, uint32_t frames)
    {
        float total = 0.0f;
        for (uint32_t voice = 0u; voice < params_.voices; ++voice) {
            const float rms = std::sqrt(sum[voice] / static_cast<float>(std::max<uint32_t>(1u, frames)));
            voices_[voice].energy += (rms - voices_[voice].energy) * (rms > voices_[voice].energy ? 0.24f : 0.045f);
            total += voices_[voice].energy;
        }
        const float average = total / static_cast<float>(params_.voices);
        globalEnergy_ += (average - globalEnergy_) * 0.08f;
    }

    double sampleRate_ = 48000.0;
    AmbiStochasticParams params_ {};
    std::array<AmbiStochasticVoice, kAmbiStochasticMaxVoices> voices_ {};
    std::array<AmbiStochasticPoint, kAmbiStochasticMaxVoices> points_ {};
    std::array<Vec3, kAmbiStochasticMaxVoices> topologyPrimary_ {};
    std::array<Vec3, kAmbiStochasticMaxVoices> topologySecondary_ {};
    std::array<Vec3, kAmbiStochasticMaxVoices> topologyPosition_ {};
    std::array<Vec3, kAmbiStochasticMaxVoices> topologyPrevious_ {};
    std::array<uint32_t, kAmbiStochasticMaxVoices> neighborIndex_ {};
    std::array<uint32_t, kAmbiStochasticMaxVoices> secondaryNeighborIndex_ {};
    std::array<float, kAmbiStochasticMaxVoices> kinetic_ {};
    std::array<float, kAmbiStochasticMaxVoices> neighborInfluence_ {};
    std::array<float, kAmbiStochasticMaxVoices> crowding_ {};
    std::array<float, kAmbiStochasticMaxVoices> topologyRadius_ {};
    std::array<float, kAmbiStochasticMaxVoices> selectionPulse_ {};
    float topologyPhase_ = 0.0f;
    uint32_t topologySeed_ = 0x7f4a7c15u;
    float globalEnergy_ = 0.0f;
    float globalKinetic_ = 0.0f;
    float smoothedOutputGain_ = 0.0f;
};

} // namespace s3g
