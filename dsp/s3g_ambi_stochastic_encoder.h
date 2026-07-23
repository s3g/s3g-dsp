#pragma once

#include "s3g_ambi_field_listener.h"
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
    float fieldRestSeconds = 0.12f;
    float macroDurationSeconds = 24.0f;
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
    float outputGainDb = -6.0f;
    AmbiFieldListenMode fieldListenMode = AmbiFieldListenMode::Off;
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
    float cascadeExcitation = 0.0f;
    float cascadeDelaySamples = 0.0f;
    float cascadePendingStrength = 0.0f;
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
        fieldListener_.prepare(sampleRate_);
        fieldListener_.setMemorySeconds(0.48f);
        const auto& directions = ambiFieldListenerCubeDirections();
        fieldListener_.setDirections(directions.data(), static_cast<uint32_t>(directions.size()));
        reset();
    }

    void reset()
    {
        fieldListener_.reset();
        topologyPhase_ = 0.0f;
        topologySeed_ = 0x7f4a7c15u;
        macroSeed_ = 0x51ed270bu;
        macroCurrent_ = {};
        macroTarget_ = {};
        macroRemainingSamples_ = params_.macroDurationSeconds * static_cast<float>(sampleRate_);
        globalActivity_ = 0.0f;
        globalEnergy_ = 0.0f;
        globalKinetic_ = 0.0f;
        cascadePulse_.fill(0.0f);
        smoothedOutputGain_ = dbToGain(params_.outputGainDb)
            / std::sqrt(static_cast<float>(std::max<uint32_t>(1u, params_.voices)));
        for (uint32_t voice = 0u; voice < kAmbiStochasticMaxVoices; ++voice) {
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
            initializeVoice(voice);
            neighborIndex_[voice] = (voice + 1u) % kAmbiStochasticMaxVoices;
            secondaryNeighborIndex_[voice] = (voice + 2u) % kAmbiStochasticMaxVoices;
        }
        bool anyFieldActive = false;
        for (uint32_t voice = 0u; voice < params_.voices; ++voice) {
            anyFieldActive = anyFieldActive || voices_[voice].fieldActive;
        }
        if (!anyFieldActive && params_.fieldDensity > 0.0f) {
            voices_[0].fieldActive = true;
            voices_[0].fieldRemainingSamples = activeDurationSamples(0u);
        }
        uint32_t activeVoices = 0u;
        for (uint32_t voice = 0u; voice < params_.voices; ++voice) {
            if (voices_[voice].fieldActive) ++activeVoices;
        }
        globalActivity_ = static_cast<float>(activeVoices)
            / static_cast<float>(std::max<uint32_t>(1u, params_.voices));
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
        params.fieldRestSeconds = clamp(params.fieldRestSeconds, 0.02f, 8.0f);
        params.macroDurationSeconds = clamp(params.macroDurationSeconds, 2.0f, 300.0f);
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
        params.fieldListenMode = sanitizeAmbiFieldListenMode(params.fieldListenMode);

        const uint32_t oldVoices = params_.voices;
        const uint32_t oldBreakpoints = params_.breakpoints;
        const float oldMacroDuration = params_.macroDurationSeconds;
        params_ = params;
        if (oldMacroDuration > 0.0f && std::fabs(params_.macroDurationSeconds - oldMacroDuration) > 0.0001f) {
            macroRemainingSamples_ = std::max(1.0f, macroRemainingSamples_
                * params_.macroDurationSeconds / oldMacroDuration);
        }
        if (params_.voices > oldVoices) {
            for (uint32_t voice = oldVoices; voice < params_.voices; ++voice) {
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
                initializeVoice(voice);
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
    float globalActivity() const { return globalActivity_; }
    float fieldListenEnvelope(uint32_t lobe) const { return fieldListener_.envelope(lobe); }
    float fieldListenActivity() const { return fieldListener_.activity(); }
    float macroDensityScale() const { return macroCurrent_.density; }
    float macroDurationScale() const { return macroCurrent_.duration; }
    float macroMutationScale() const { return macroCurrent_.mutation; }
    float macroCascadeScale() const { return macroCurrent_.cascade; }
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
    float voiceNetworkPulse(uint32_t voice) const
    {
        const uint32_t safe = safeVoice(voice);
        return std::max(selectionPulse_[safe], cascadePulse_[safe]);
    }
    float voiceCascadePulse(uint32_t voice) const { return cascadePulse_[safeVoice(voice)]; }
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
            updateMacroScene(static_cast<float>(chunkFrames));
            updateCascadeState(static_cast<float>(chunkFrames));
            updateTimeFields(static_cast<float>(chunkFrames));

            std::array<std::array<float, kAmbiStochasticMaxChannels>, kAmbiStochasticMaxVoices> basis {};
            std::array<float, kAmbiStochasticMaxVoices> distanceGain {};
            std::array<float, kAmbiStochasticMaxVoices> energySum {};
            for (uint32_t voice = 0u; voice < voices; ++voice) {
                basis[voice] = acnSn3dBasis7(directionFromAed(points_[voice].azimuthDeg, points_[voice].elevationDeg));
                distanceGain[voice] = 1.0f / std::max(0.5f, points_[voice].distance);
            }

            for (uint32_t frame = chunkStart; frame < chunkStart + chunkFrames; ++frame) {
                std::array<float, kAmbiStochasticMaxChannels> listenerFrame {};
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
                    const float internalAmplitude = std::tanh(sample * 1.18f) * envelope
                        * velocity * fieldGain * distanceGain[voiceIndex];
                    const float amplitude = internalAmplitude * smoothedOutputGain_;
                    energySum[voiceIndex] += amplitude * amplitude;
                    voice.age += 1.0f / static_cast<float>(sampleRate_);
                    for (uint32_t channel = 0u; channel < ambiChannels; ++channel) {
                        listenerFrame[channel] += internalAmplitude * basis[voiceIndex][channel];
                        if (outputs[channel]) {
                            outputs[channel][frame] = flushDenormal(outputs[channel][frame]
                                + amplitude * basis[voiceIndex][channel]);
                        }
                    }
                }
                fieldListener_.processFrame(listenerFrame.data(), ambiChannels);
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
    struct MacroScene {
        float density = 1.0f;
        float duration = 1.0f;
        float mutation = 1.0f;
        float cascade = 1.0f;
    };

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

    float activeDurationSamples(uint32_t voiceIndex)
    {
        auto& voice = voices_[voiceIndex];
        const Vec3 topology = topologyPosition_[voiceIndex];
        const float u = clamp(randomUnit(voice.seed), 0.001f, 0.999f);
        const float exponential = -std::log(1.0f - u);
        const float topologyDuration = std::exp(-topology.y * params_.fieldContrast * 0.85f);
        const float seconds = clamp(params_.fieldDurationSeconds * macroCurrent_.duration
                * (0.18f + exponential * (0.45f + params_.fieldContrast * 0.70f)) * topologyDuration,
            0.03f, 30.0f);
        return seconds * static_cast<float>(sampleRate_);
    }

    float restDurationSamples(AmbiStochasticVoice& voice)
    {
        const float variation = 1.0f + randomUnit(voice.seed) * (0.35f + params_.fieldContrast * 0.65f);
        return params_.fieldRestSeconds * variation * static_cast<float>(sampleRate_);
    }

    float retryDurationSamples(AmbiStochasticVoice& voice)
    {
        const float seconds = std::max(0.02f, params_.fieldRestSeconds
            * (0.22f + randomUnit(voice.seed) * 0.38f));
        return seconds * static_cast<float>(sampleRate_);
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
        voice.fieldRemainingSamples = voice.fieldActive
            ? activeDurationSamples(voiceIndex) : restDurationSamples(voice);
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
        const float mutation = clamp(macroCurrent_.mutation, 0.45f, 1.65f);
        const float amplitudeDrive = params_.amplitudeStep * (0.28f + radius * 0.92f) * mutation;
        const float durationDrive = params_.durationStep * (0.24f + radius * 0.96f) * mutation;
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
        const float listenAmount = fieldListener_.activity()
            * (params_.fieldListenMode == AmbiFieldListenMode::Off ? 0.0f : 1.0f);
        const float listenerGenerator = listenerGeneratorTarget();
        uint32_t selected = voice.nextGenerator;
        switch (params_.selection) {
        case AmbiStochasticSelection::Series:
            if (voice.seriesCursor >= kAmbiStochasticGeneratorCount) shuffleSeries(voice);
            selected = voice.seriesOrder[voice.seriesCursor++];
            break;
        case AmbiStochasticSelection::Weight: {
            std::array<float, kAmbiStochasticGeneratorCount> weights {};
            const float topologyTarget = (topology.x * 0.5f + 0.5f) * 3.0f;
            const float target = lerp(topologyTarget, listenerGenerator, listenAmount * 0.62f);
            for (uint32_t generator = 0u; generator < kAmbiStochasticGeneratorCount; ++generator) {
                const float distance = std::fabs(static_cast<float>(generator) - target);
                weights[generator] = 0.08f + std::exp(-distance * (1.4f + params_.selectionMemory * 3.8f));
            }
            selected = weightedChoice(voice, weights);
            break;
        }
        case AmbiStochasticSelection::Tendency: {
            const float topologyTarget = (topology.y * 0.5f + 0.5f) * 3.0f;
            const float target = lerp(topologyTarget, listenerGenerator, listenAmount * 0.62f);
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
            for (uint32_t generator = 0u; generator < kAmbiStochasticGeneratorCount; ++generator) {
                const float distance = std::fabs(static_cast<float>(generator) - listenerGenerator);
                weights[generator] += listenAmount * (0.10f + std::exp(-distance * 1.5f) * 2.2f);
            }
            selected = weightedChoice(voice, weights);
            break;
        }
        case AmbiStochasticSelection::Walk: {
            const float acceleration = randomSigned(voice.seed, params_.durationDistribution)
                    * (0.08f + (1.0f - params_.selectionMemory) * 0.72f)
                + topology.x * 0.06f
                + (listenerGenerator - voice.selectorSecondary) * listenAmount * 0.045f;
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
        return clamp(std::pow(params_.fieldDensity, densityExponent) * macroCurrent_.density, 0.0f, 1.0f);
    }

    void updateMacroScene(float frames)
    {
        macroRemainingSamples_ -= frames;
        if (macroRemainingSamples_ <= 0.0f) {
            static constexpr std::array<MacroScene, 6> kScenes {{
                { 0.58f, 1.55f, 0.62f, 0.42f },
                { 1.24f, 0.62f, 1.42f, 1.34f },
                { 0.84f, 1.78f, 0.76f, 1.24f },
                { 0.72f, 0.72f, 1.52f, 0.58f },
                { 0.96f, 1.02f, 0.92f, 1.62f },
                { 0.52f, 1.92f, 0.58f, 0.30f },
            }};
            const MacroScene candidate = kScenes[randomU32(macroSeed_) % kScenes.size()];
            const float reach = 0.28f + (1.0f - params_.selectionMemory) * 0.72f;
            const float jitter = 0.94f + randomUnit(macroSeed_) * 0.12f;
            macroTarget_.density = clamp(lerp(macroCurrent_.density, candidate.density, reach) * jitter,
                0.45f, 1.35f);
            macroTarget_.duration = clamp(lerp(macroCurrent_.duration, candidate.duration, reach)
                    * (0.96f + randomUnit(macroSeed_) * 0.08f),
                0.50f, 2.0f);
            macroTarget_.mutation = clamp(lerp(macroCurrent_.mutation, candidate.mutation, reach)
                    * (0.95f + randomUnit(macroSeed_) * 0.10f),
                0.50f, 1.60f);
            macroTarget_.cascade = clamp(lerp(macroCurrent_.cascade, candidate.cascade, reach)
                    * (0.94f + randomUnit(macroSeed_) * 0.12f),
                0.25f, 1.75f);
            const float hold = params_.macroDurationSeconds
                * (0.68f + randomUnit(macroSeed_) * 0.72f)
                * (0.90f + params_.selectionMemory * 0.20f);
            macroRemainingSamples_ += hold * static_cast<float>(sampleRate_);
        }

        const float smoothingSeconds = 0.45f + params_.macroDurationSeconds * 0.025f
            + params_.selectionMemory * 1.35f;
        const float smoothing = 1.0f - std::exp(-frames
            / std::max(1.0f, static_cast<float>(sampleRate_) * smoothingSeconds));
        macroCurrent_.density += (macroTarget_.density - macroCurrent_.density) * smoothing;
        macroCurrent_.duration += (macroTarget_.duration - macroCurrent_.duration) * smoothing;
        macroCurrent_.mutation += (macroTarget_.mutation - macroCurrent_.mutation) * smoothing;
        macroCurrent_.cascade += (macroTarget_.cascade - macroCurrent_.cascade) * smoothing;
    }

    void scheduleCascade(uint32_t sourceIndex)
    {
        if (params_.neighborTransfer <= 0.0001f || params_.voices < 2u) return;
        auto& source = voices_[sourceIndex];
        const float baseStrength = params_.neighborTransfer
            * (0.34f + params_.fieldContrast * 0.66f) * macroCurrent_.cascade;
        const auto scheduleTarget = [&](uint32_t targetIndex, float scale) {
            if (targetIndex == sourceIndex || targetIndex >= params_.voices) return;
            const Vec3 sourcePoint = topologyPosition_[sourceIndex];
            const Vec3 targetPoint = topologyPosition_[targetIndex];
            const float dx = sourcePoint.x - targetPoint.x;
            const float dy = sourcePoint.y - targetPoint.y;
            const float dz = sourcePoint.z - targetPoint.z;
            const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float proximity = clamp(1.0f - distance / 2.4f, 0.12f, 1.0f);
            const float strength = clamp(baseStrength * scale * proximity, 0.0f, 1.0f);
            if (strength <= 0.001f) return;
            const float delaySeconds = 0.015f + std::min(2.4f, distance) * 0.065f
                + randomUnit(source.seed) * 0.018f;
            auto& target = voices_[targetIndex];
            const float delaySamples = delaySeconds * static_cast<float>(sampleRate_);
            if (target.cascadePendingStrength <= 0.0001f
                || delaySamples < target.cascadeDelaySamples) {
                target.cascadeDelaySamples = delaySamples;
            }
            target.cascadePendingStrength = 1.0f
                - (1.0f - clamp(target.cascadePendingStrength, 0.0f, 1.0f)) * (1.0f - strength);
        };

        const uint32_t first = neighborIndex_[sourceIndex];
        const uint32_t second = secondaryNeighborIndex_[sourceIndex];
        scheduleTarget(first, 1.0f);
        if (second != first) scheduleTarget(second, 0.58f);
    }

    void updateCascadeState(float frames)
    {
        const float excitationDecay = std::exp(-frames / std::max(1.0f,
            static_cast<float>(sampleRate_) * (0.22f + params_.fieldContrast * 1.05f)));
        const float pulseDecay = std::exp(-frames / std::max(1.0f,
            static_cast<float>(sampleRate_) * 0.18f));
        for (uint32_t voiceIndex = 0u; voiceIndex < params_.voices; ++voiceIndex) {
            auto& voice = voices_[voiceIndex];
            voice.cascadeExcitation *= excitationDecay;
            cascadePulse_[voiceIndex] *= pulseDecay;
            if (voice.cascadePendingStrength <= 0.0001f) continue;
            voice.cascadeDelaySamples -= frames;
            if (voice.cascadeDelaySamples > 0.0f) continue;
            voice.cascadeExcitation = clamp(voice.cascadeExcitation
                + voice.cascadePendingStrength, 0.0f, 1.5f);
            voice.cascadePendingStrength = 0.0f;
            voice.cascadeDelaySamples = 0.0f;
            cascadePulse_[voiceIndex] = 1.0f;
        }
    }

    void updateTimeFields(float frames)
    {
        uint32_t activeVoices = 0u;
        for (uint32_t voiceIndex = 0u; voiceIndex < params_.voices; ++voiceIndex) {
            if (voices_[voiceIndex].fieldActive) ++activeVoices;
        }
        const float measuredActivity = static_cast<float>(activeVoices)
            / static_cast<float>(std::max<uint32_t>(1u, params_.voices));
        const float activitySmoothing = 1.0f - std::exp(-frames
            / std::max(1.0f, static_cast<float>(sampleRate_) * 0.22f));
        globalActivity_ += (measuredActivity - globalActivity_) * activitySmoothing;
        const float targetActivity = scaledFieldDensity();
        const float homeostasis = clamp((targetActivity - globalActivity_) * 0.82f, -0.32f, 0.32f);

        for (uint32_t voiceIndex = 0u; voiceIndex < params_.voices; ++voiceIndex) {
            auto& voice = voices_[voiceIndex];
            voice.fieldRemainingSamples -= frames;
            if (voice.fieldRemainingSamples > 0.0f) continue;
            if (voice.fieldActive) {
                voice.fieldActive = false;
                voice.fieldRemainingSamples += restDurationSamples(voice);
                continue;
            }

            if (params_.fieldDensity <= 0.0001f) {
                voice.fieldRemainingSamples += retryDurationSamples(voice);
                continue;
            }
            const Vec3 topology = topologyPosition_[voiceIndex];
            const float topologyBias = topology.y * params_.fieldContrast * 0.13f;
            const float cascadeBias = voice.cascadeExcitation * 0.58f;
            const float listenerBias = params_.fieldListenMode == AmbiFieldListenMode::Off
                ? 0.0f
                : (listenerPreferenceForVoice(voiceIndex) - 0.5f)
                    * fieldListener_.activity() * 0.62f;
            const float probability = clamp(targetActivity + topologyBias + cascadeBias
                    + homeostasis + listenerBias,
                0.002f, 0.995f);
            if (randomUnit(voice.seed) < probability) {
                voice.fieldActive = true;
                voice.fieldRemainingSamples += activeDurationSamples(voiceIndex);
                voice.cascadeExcitation *= 0.18f;
                scheduleCascade(voiceIndex);
            } else {
                voice.fieldRemainingSamples += retryDurationSamples(voice);
            }
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
            const float listenAmount = fieldListener_.activity()
                * (params_.fieldListenMode == AmbiFieldListenMode::Off ? 0.0f : 0.34f);
            if (listenAmount > 0.0001f) {
                const Vec3 listenerTarget = listenerTopologyTarget();
                target.x = lerp(target.x, listenerTarget.x, listenAmount);
                target.y = lerp(target.y, listenerTarget.y, listenAmount);
                target.z = lerp(target.z, listenerTarget.z, listenAmount);
            }
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

    Vec3 listenerTopologyTarget() const
    {
        const Vec3 direction = fieldListener_.preferredDirection(params_.fieldListenMode);
        // Topology coordinates map (x,y,z) to (azimuth sine, elevation,
        // azimuth cosine), while the ambisonic direction is Cartesian.
        return { direction.y, direction.z, direction.x };
    }

    float listenerGeneratorTarget() const
    {
        const Vec3 target = listenerTopologyTarget();
        return clamp((target.x * 0.5f + 0.5f) * 3.0f, 0.0f, 3.0f);
    }

    float listenerPreferenceForVoice(uint32_t voice) const
    {
        const AmbiStochasticPoint& point =
            points_[std::min<uint32_t>(voice, params_.voices - 1u)];
        return fieldListener_.preference(
            directionFromAed(point.azimuthDeg, point.elevationDeg),
            params_.fieldListenMode);
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
    std::array<float, kAmbiStochasticMaxVoices> cascadePulse_ {};
    float topologyPhase_ = 0.0f;
    uint32_t topologySeed_ = 0x7f4a7c15u;
    uint32_t macroSeed_ = 0x51ed270bu;
    MacroScene macroCurrent_ {};
    MacroScene macroTarget_ {};
    float macroRemainingSamples_ = 1.0f;
    float globalActivity_ = 0.0f;
    float globalEnergy_ = 0.0f;
    float globalKinetic_ = 0.0f;
    float smoothedOutputGain_ = 0.0f;
    AmbiFieldListener fieldListener_ {};
};

} // namespace s3g
