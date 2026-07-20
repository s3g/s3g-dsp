#pragma once

#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAmbiWaveTerrainMaxVoices = 64;
constexpr uint32_t kAmbiWaveTerrainMaxOrder = 7;
constexpr uint32_t kAmbiWaveTerrainMaxChannels = 64;
constexpr uint32_t kAmbiWaveTerrainTableSize = 512;
constexpr uint32_t kAmbiWaveTerrainMipLevels = 5;
constexpr uint32_t kAmbiWaveTerrainSpatialTableSize = 64;
constexpr float kAmbiWaveTerrainTwoPi = 6.28318530717958647692f;

enum class AmbiWaveTerrainMode : uint32_t { Free = 0, Midi = 1, Both = 2 };
enum class AmbiWaveTerrainPitchMode : uint32_t { Note = 0, Traversal = 1 };
enum class AmbiWaveTerrainMotionMode : uint32_t { Field = 0, Rotate = 1 };
enum class AmbiWaveTerrainSkin : uint32_t {
    Harmonic = 0, Fbm = 1, Cellular = 2, Vot = 3,
    Ridges = 4, Dunes = 5, Craters = 6, Tectonic = 7,
};
enum class AmbiWaveTerrainTrace : uint32_t { Orbit = 0, Lissajous = 1, Rosette = 2, Fold = 3 };
enum class AmbiWaveTerrainSelection : uint32_t { Random = 0, Series = 1, Weight = 2, Tendency = 3, Markov = 4, Walk = 5 };
enum class AmbiWaveTerrainTransition : uint32_t { Link = 0, Merge = 1, Vary = 2 };
enum class AmbiWaveTerrainInterpretation : uint32_t { Height = 0, Edge = 1, Curvature = 2, Blend = 3 };

struct AmbiWaveTerrainParams {
    uint32_t order = 3;
    uint32_t voices = 12;
    AmbiWaveTerrainMode mode = AmbiWaveTerrainMode::Free;
    float baseNote = 40.0f;
    float pitchSpreadSemitones = 19.0f;
    float tuneCents = 0.0f;
    float detuneCents = 9.0f;

    AmbiWaveTerrainSkin skin = AmbiWaveTerrainSkin::Fbm;
    float terrainDepth = 0.82f;
    float terrainRoughness = 0.58f;
    float terrainFold = 0.24f;
    float terrainRelief = 0.62f;

    AmbiWaveTerrainTrace trace = AmbiWaveTerrainTrace::Lissajous;
    AmbiWaveTerrainInterpretation interpretation = AmbiWaveTerrainInterpretation::Height;
    float interpretationMix = 0.32f;
    float scanRadius = 0.16f;
    float scanAspect = 0.68f;
    float scanRotation = 0.0f;
    float scanWarp = 0.18f;

    AmbiWaveTerrainSelection selection = AmbiWaveTerrainSelection::Walk;
    AmbiWaveTerrainTransition transition = AmbiWaveTerrainTransition::Link;
    float fieldDensity = 0.82f;
    float fieldDurationSeconds = 1.8f;
    float fieldRestSeconds = 0.24f;
    float fieldContrast = 0.72f;
    float selectionMemory = 0.74f;
    float regionDeviation = 0.36f;
    float neighborTransfer = 0.28f;
    float macroDurationSeconds = 24.0f;
    float tableXfadeMs = 180.0f;

    float attackMs = 12.0f;
    float decayMs = 180.0f;
    float sustain = 0.76f;
    float releaseMs = 420.0f;

    float centerAzimuthDeg = 0.0f;
    float centerElevationDeg = 0.0f;
    float centerDistance = 1.0f;
    float spatialSpread = 0.82f;
    float spatialFollow = 0.90f;
    float outputGainDb = -22.0f;

    AmbiWaveTerrainPitchMode pitchMode = AmbiWaveTerrainPitchMode::Note;
    AmbiWaveTerrainMotionMode motionMode = AmbiWaveTerrainMotionMode::Rotate;
    float azimuthRateRpm = 0.70f;
    float elevationRateRpm = 0.43f;
    float rotationRateDeviation = 0.28f;
};

struct AmbiWaveTerrainPoint {
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float distance = 1.0f;
    float terrain = 0.0f;
};

struct AmbiWaveTerrainRegion {
    float u = 0.5f;
    float v = 0.5f;
    float radius = 0.16f;
    float aspect = 0.68f;
    float rotation = 0.0f;
    AmbiWaveTerrainTrace trace = AmbiWaveTerrainTrace::Lissajous;
};

enum class AmbiWaveTerrainEnvelopeStage : uint8_t { Idle = 0, Attack, Decay, Sustain, Release };

struct AmbiWaveTerrainEnvelope {
    AmbiWaveTerrainEnvelopeStage stage = AmbiWaveTerrainEnvelopeStage::Idle;
    float value = 0.0f;
    bool gate = false;

    void setGate(bool next)
    {
        if (next && !gate) stage = AmbiWaveTerrainEnvelopeStage::Attack;
        else if (!next && gate && stage != AmbiWaveTerrainEnvelopeStage::Idle) stage = AmbiWaveTerrainEnvelopeStage::Release;
        gate = next;
    }

    float process(float attack, float decay, float sustain, float release)
    {
        switch (stage) {
        case AmbiWaveTerrainEnvelopeStage::Attack:
            value += (1.0f - value) * attack;
            if (value >= 0.999f) { value = 1.0f; stage = AmbiWaveTerrainEnvelopeStage::Decay; }
            break;
        case AmbiWaveTerrainEnvelopeStage::Decay:
            value += (sustain - value) * decay;
            if (std::fabs(value - sustain) < 0.0005f) { value = sustain; stage = AmbiWaveTerrainEnvelopeStage::Sustain; }
            break;
        case AmbiWaveTerrainEnvelopeStage::Sustain:
            value = sustain;
            if (!gate) stage = AmbiWaveTerrainEnvelopeStage::Release;
            break;
        case AmbiWaveTerrainEnvelopeStage::Release:
            value += (0.0f - value) * release;
            if (value < 0.00005f) { value = 0.0f; stage = AmbiWaveTerrainEnvelopeStage::Idle; }
            break;
        case AmbiWaveTerrainEnvelopeStage::Idle:
        default: value = 0.0f; break;
        }
        return value;
    }
};

struct AmbiWaveTerrainTable {
    std::array<std::array<float, kAmbiWaveTerrainTableSize>, kAmbiWaveTerrainMipLevels> levels {};
    std::array<float, kAmbiWaveTerrainTableSize> terrainProfile {};
    std::array<std::array<float, kAmbiWaveTerrainMaxChannels>, kAmbiWaveTerrainSpatialTableSize> spatialBasis {};
    float traversalLength = 4.0f;
    AmbiWaveTerrainRegion region {};
};

struct AmbiWaveTerrainVoice {
    AmbiWaveTerrainTable current {};
    AmbiWaveTerrainTable next {};
    AmbiWaveTerrainEnvelope freeEnvelope {};
    AmbiWaveTerrainEnvelope midiEnvelope {};
    float freePhase = 0.0f;
    float midiPhase = 0.0f;
    float transition = 1.0f;
    float transitionStep = 1.0f;
    float eventSamples = 1.0f;
    float energy = 0.0f;
    float age = 0.0f;
    float velocity = 0.8f;
    float azimuthPhase = 0.0f;
    float elevationPhase = 0.0f;
    std::array<float, kAmbiWaveTerrainMaxChannels> freeSpatial {};
    std::array<float, kAmbiWaveTerrainMaxChannels> midiSpatial {};
    uint32_t randomState = 1u;
    uint32_t seriesCursor = 0u;
    int midiNote = 60;
    bool midiGate = false;
    bool fieldActive = true;
};

class AmbiWaveTerrainEncoder {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        reset();
    }

    void reset()
    {
        macroU_ = 0.5f;
        macroV_ = 0.5f;
        macroStartU_ = macroU_;
        macroStartV_ = macroV_;
        macroTargetU_ = 0.67f;
        macroTargetV_ = 0.38f;
        macroPhase_ = 0.0f;
        dirtyVoice_ = kAmbiWaveTerrainMaxVoices;
        for (uint32_t voice = 0; voice < kAmbiWaveTerrainMaxVoices; ++voice) {
            auto& v = voices_[voice];
            v = {};
            v.randomState = hash(voice + 1u);
            v.freePhase = fract(static_cast<float>(voice) * 0.61803398875f);
            v.midiPhase = fract(static_cast<float>(voice) * 0.38196601125f);
            v.azimuthPhase = fract(static_cast<float>(voice) * 0.754877666f);
            v.elevationPhase = fract(static_cast<float>(voice) * 0.569840296f + 0.25f);
            v.midiNote = static_cast<int>(params_.baseNote);
            const AmbiWaveTerrainRegion region = initialRegion(voice);
            renderTable(v.current, region);
            v.next = v.current;
            v.eventSamples = params_.motionMode == AmbiWaveTerrainMotionMode::Rotate
                ? 1.0f + static_cast<float>(voice) * 0.003f * static_cast<float>(sampleRate_)
                : eventDurationSamples(v, true) + static_cast<float>(voice) * 0.017f * static_cast<float>(sampleRate_);
            points_[voice] = pointForRegion(region);
            previousPoints_[voice] = points_[voice];
            fieldActive_[voice] = 1u;
            neighbor_[voice] = (voice + 1u) % kAmbiWaveTerrainMaxVoices;
        }
    }

    void setParams(AmbiWaveTerrainParams params)
    {
        params.order = std::clamp<uint32_t>(params.order, 1u, kAmbiWaveTerrainMaxOrder);
        params.voices = std::clamp<uint32_t>(params.voices, 1u, kAmbiWaveTerrainMaxVoices);
        params.mode = static_cast<AmbiWaveTerrainMode>(std::clamp<uint32_t>(static_cast<uint32_t>(params.mode), 0u, 2u));
        params.baseNote = clamp(params.baseNote, 12.0f, 96.0f);
        params.pitchSpreadSemitones = clamp(params.pitchSpreadSemitones, 0.0f, 48.0f);
        params.tuneCents = clamp(params.tuneCents, -1200.0f, 1200.0f);
        params.detuneCents = clamp(params.detuneCents, 0.0f, 100.0f);
        params.skin = static_cast<AmbiWaveTerrainSkin>(std::clamp<uint32_t>(static_cast<uint32_t>(params.skin), 0u, 7u));
        params.terrainDepth = clamp(params.terrainDepth, 0.0f, 1.0f);
        params.terrainRoughness = clamp(params.terrainRoughness, 0.0f, 1.0f);
        params.terrainFold = clamp(params.terrainFold, 0.0f, 1.0f);
        params.terrainRelief = clamp(params.terrainRelief, 0.0f, 1.0f);
        params.trace = static_cast<AmbiWaveTerrainTrace>(std::clamp<uint32_t>(static_cast<uint32_t>(params.trace), 0u, 3u));
        params.interpretation = static_cast<AmbiWaveTerrainInterpretation>(std::clamp<uint32_t>(static_cast<uint32_t>(params.interpretation), 0u, 3u));
        params.interpretationMix = clamp(params.interpretationMix, 0.0f, 1.0f);
        params.scanRadius = clamp(params.scanRadius, 0.005f, 0.48f);
        params.scanAspect = clamp(params.scanAspect, 0.05f, 1.0f);
        params.scanRotation = clamp(params.scanRotation, -1.0f, 1.0f);
        params.scanWarp = clamp(params.scanWarp, 0.0f, 1.0f);
        params.selection = static_cast<AmbiWaveTerrainSelection>(std::clamp<uint32_t>(static_cast<uint32_t>(params.selection), 0u, 5u));
        params.transition = static_cast<AmbiWaveTerrainTransition>(std::clamp<uint32_t>(static_cast<uint32_t>(params.transition), 0u, 2u));
        params.fieldDensity = clamp(params.fieldDensity, 0.0f, 1.0f);
        params.fieldDurationSeconds = clamp(params.fieldDurationSeconds, 0.05f, 30.0f);
        params.fieldRestSeconds = clamp(params.fieldRestSeconds, 0.02f, 8.0f);
        params.fieldContrast = clamp(params.fieldContrast, 0.0f, 1.0f);
        params.selectionMemory = clamp(params.selectionMemory, 0.0f, 1.0f);
        params.regionDeviation = clamp(params.regionDeviation, 0.0f, 1.0f);
        params.neighborTransfer = clamp(params.neighborTransfer, 0.0f, 1.0f);
        params.macroDurationSeconds = clamp(params.macroDurationSeconds, 2.0f, 300.0f);
        params.tableXfadeMs = clamp(params.tableXfadeMs, 5.0f, 5000.0f);
        params.attackMs = clamp(params.attackMs, 1.0f, 4000.0f);
        params.decayMs = clamp(params.decayMs, 5.0f, 8000.0f);
        params.sustain = clamp(params.sustain, 0.0f, 1.0f);
        params.releaseMs = clamp(params.releaseMs, 5.0f, 12000.0f);
        params.centerAzimuthDeg = wrapSignedDeg(params.centerAzimuthDeg);
        params.centerElevationDeg = clamp(params.centerElevationDeg, -90.0f, 90.0f);
        params.centerDistance = clamp(params.centerDistance, 0.15f, 3.0f);
        params.spatialSpread = clamp(params.spatialSpread, 0.0f, 1.0f);
        params.spatialFollow = clamp(params.spatialFollow, 0.0f, 0.995f);
        params.outputGainDb = clamp(params.outputGainDb, -60.0f, 12.0f);
        params.pitchMode = static_cast<AmbiWaveTerrainPitchMode>(
            std::clamp<uint32_t>(static_cast<uint32_t>(params.pitchMode), 0u, 1u));
        params.motionMode = static_cast<AmbiWaveTerrainMotionMode>(
            std::clamp<uint32_t>(static_cast<uint32_t>(params.motionMode), 0u, 1u));
        params.azimuthRateRpm = clamp(params.azimuthRateRpm, -12.0f, 12.0f);
        params.elevationRateRpm = clamp(params.elevationRateRpm, -12.0f, 12.0f);
        params.rotationRateDeviation = clamp(params.rotationRateDeviation, 0.0f, 1.0f);

        const bool motionModeChanged = params.motionMode != params_.motionMode;
        const bool terrainChanged = params.skin != params_.skin
            || params.terrainDepth != params_.terrainDepth
            || params.terrainRoughness != params_.terrainRoughness
            || params.terrainFold != params_.terrainFold
            || params.terrainRelief != params_.terrainRelief
            || params.trace != params_.trace
            || params.interpretation != params_.interpretation
            || params.interpretationMix != params_.interpretationMix
            || params.scanRadius != params_.scanRadius
            || params.scanAspect != params_.scanAspect
            || params.scanRotation != params_.scanRotation
            || params.scanWarp != params_.scanWarp
            || params.centerAzimuthDeg != params_.centerAzimuthDeg
            || params.centerElevationDeg != params_.centerElevationDeg
            || params.centerDistance != params_.centerDistance
            || params.spatialSpread != params_.spatialSpread;
        params_ = params;
        if (terrainChanged) dirtyVoice_ = 0u;
        if (motionModeChanged) {
            for (uint32_t voice = 0; voice < params_.voices; ++voice) voices_[voice].eventSamples = 1.0f + static_cast<float>(voice) * 64.0f;
        }
    }

    AmbiWaveTerrainParams params() const { return params_; }
    const std::array<AmbiWaveTerrainPoint, kAmbiWaveTerrainMaxVoices>& points() const { return points_; }
    const std::array<uint8_t, kAmbiWaveTerrainMaxVoices>& fieldActive() const { return fieldActive_; }
    const std::array<uint32_t, kAmbiWaveTerrainMaxVoices>& neighbors() const { return neighbor_; }
    float voiceEnergy(uint32_t voice) const { return voices_[std::min<uint32_t>(voice, kAmbiWaveTerrainMaxVoices - 1u)].energy; }
    float voiceTransition(uint32_t voice) const { return voices_[std::min<uint32_t>(voice, kAmbiWaveTerrainMaxVoices - 1u)].transition; }
    float voiceScanPhase(uint32_t voice) const
    {
        const auto& v = voices_[std::min<uint32_t>(voice, kAmbiWaveTerrainMaxVoices - 1u)];
        const bool midiVisible = params_.mode == AmbiWaveTerrainMode::Midi
            || (params_.mode == AmbiWaveTerrainMode::Both && v.midiEnvelope.stage != AmbiWaveTerrainEnvelopeStage::Idle);
        return midiVisible ? v.midiPhase : v.freePhase;
    }
    AmbiWaveTerrainRegion voiceRegion(uint32_t voice, bool next = false) const
    {
        const auto& v = voices_[std::min<uint32_t>(voice, kAmbiWaveTerrainMaxVoices - 1u)];
        return next ? v.next.region : v.current.region;
    }
    float tableValue(uint32_t voice, bool next, uint32_t index) const
    {
        const auto& v = voices_[std::min<uint32_t>(voice, kAmbiWaveTerrainMaxVoices - 1u)];
        return (next ? v.next : v.current).levels[0][index % kAmbiWaveTerrainTableSize];
    }
    float terrainProfileValue(uint32_t voice, bool next, uint32_t index) const
    {
        const auto& v = voices_[std::min<uint32_t>(voice, kAmbiWaveTerrainMaxVoices - 1u)];
        return (next ? v.next : v.current).terrainProfile[index % kAmbiWaveTerrainTableSize];
    }

    float terrainHeight(float u, float v) const { return terrain(fract(u), clamp(v, 0.0f, 1.0f)); }

    AmbiWaveTerrainPoint surfacePoint(float u, float v) const
    {
        const float h = terrainHeight(u, v);
        AmbiWaveTerrainPoint p {};
        p.azimuthDeg = wrapSignedDeg(params_.centerAzimuthDeg + (fract(u) - 0.5f) * 360.0f * params_.spatialSpread);
        p.elevationDeg = clamp(params_.centerElevationDeg + (clamp(v, 0.0f, 1.0f) - 0.5f) * 180.0f * params_.spatialSpread, -90.0f, 90.0f);
        p.distance = clamp(params_.centerDistance + h * params_.terrainDepth * params_.terrainRelief * 0.42f, 0.15f, 3.0f);
        p.terrain = h;
        return p;
    }

    void noteOn(int note, float velocity)
    {
        uint32_t best = 0u;
        float oldest = -1.0f;
        for (uint32_t voice = 0; voice < params_.voices; ++voice) {
            const auto& v = voices_[voice];
            if (!v.midiGate && v.midiEnvelope.value < 0.0001f) { best = voice; break; }
            if (v.age > oldest) { oldest = v.age; best = voice; }
        }
        auto& v = voices_[best];
        v.midiNote = note;
        v.velocity = clamp(velocity, 0.0f, 1.0f);
        v.midiGate = true;
        v.age = 0.0f;
        v.midiEnvelope.setGate(true);
    }

    void noteOff(int note)
    {
        for (auto& voice : voices_) {
            if (voice.midiNote == note) {
                voice.midiGate = false;
                voice.midiEnvelope.setGate(false);
            }
        }
    }

    void process(float* const* outputs, uint32_t outputChannels, uint32_t frames)
    {
        if (!outputs || frames == 0u) return;
        outputChannels = std::min<uint32_t>(outputChannels, kAmbiWaveTerrainMaxChannels);
        for (uint32_t channel = 0; channel < outputChannels; ++channel) {
            if (outputs[channel]) std::fill(outputs[channel], outputs[channel] + frames, 0.0f);
        }
        if (outputChannels == 0u) return;

        refreshDirtyVoice();
        const uint32_t ambiChannels = std::min<uint32_t>(ambiChannelsForOrder(params_.order), outputChannels);
        const bool freeMode = params_.mode == AmbiWaveTerrainMode::Free || params_.mode == AmbiWaveTerrainMode::Both;
        const bool midiMode = params_.mode == AmbiWaveTerrainMode::Midi || params_.mode == AmbiWaveTerrainMode::Both;
        const float layerGain = params_.mode == AmbiWaveTerrainMode::Both ? 0.70710678f : 1.0f;
        const float gain = dbToGain(params_.outputGainDb) * layerGain
            / std::sqrt(static_cast<float>(std::max<uint32_t>(1u, params_.voices)));
        const float attack = envelopeCoefficient(params_.attackMs);
        const float decay = envelopeCoefficient(params_.decayMs);
        const float release = envelopeCoefficient(params_.releaseMs);
        const float spatialCoefficient = clamp(1.0f - params_.spatialFollow, 0.001f, 1.0f);

        constexpr uint32_t kChunkFrames = 16u;
        for (uint32_t chunkStart = 0; chunkStart < frames; chunkStart += kChunkFrames) {
            const uint32_t chunkFrames = std::min<uint32_t>(kChunkFrames, frames - chunkStart);
            updateMacro(chunkFrames);
            for (uint32_t voice = 0; voice < params_.voices; ++voice) {
                updateField(voice, chunkFrames);
                updatePoint(voice);
            }

            for (uint32_t frame = chunkStart; frame < chunkStart + chunkFrames; ++frame) {
                for (uint32_t voice = 0; voice < params_.voices; ++voice) {
                    auto& v = voices_[voice];
                    const float activity = v.fieldActive ? 1.0f : 1.0f - params_.fieldContrast;
                    float voiceSample = 0.0f;
                    const auto renderLayer = [&](float& phase, float frequency, float amplitude,
                                                 std::array<float, kAmbiWaveTerrainMaxChannels>& smoothedSpatial) {
                        const auto scan = oscillator(v, phase, frequency);
                        const float transition = v.transition;
                        const float sample = lerp(scan.current, scan.next, transition);
                        voiceSample += sample * amplitude;
                        for (uint32_t channel = 0; channel < ambiChannels; ++channel) {
                            const float currentBasis = spatialSample(v.current, scan, channel);
                            const float nextBasis = spatialSample(v.next, scan, channel);
                            const float targetBasis = lerp(currentBasis, nextBasis, transition);
                            smoothedSpatial[channel] += (targetBasis - smoothedSpatial[channel]) * spatialCoefficient;
                            if (outputs[channel]) {
                                outputs[channel][frame] = flushDenormal(outputs[channel][frame]
                                    + sample * amplitude * gain * activity * smoothedSpatial[channel]);
                            }
                        }
                    };
                    if (freeMode) {
                        v.freeEnvelope.setGate(v.fieldActive || params_.fieldContrast < 0.999f);
                        const float env = v.freeEnvelope.process(attack, decay, params_.sustain, release);
                        renderLayer(v.freePhase, frequencyForVoice(params_.baseNote, voice, false), env * 0.78f, v.freeSpatial);
                    } else {
                        v.freeEnvelope.setGate(false);
                        v.freeEnvelope.process(attack, decay, params_.sustain, release);
                    }
                    if (midiMode) {
                        v.midiEnvelope.setGate(v.midiGate);
                        const float env = v.midiEnvelope.process(attack, decay, params_.sustain, release);
                        renderLayer(v.midiPhase, frequencyForVoice(static_cast<float>(v.midiNote), voice, true), env * v.velocity, v.midiSpatial);
                        if (v.midiEnvelope.stage != AmbiWaveTerrainEnvelopeStage::Idle) v.age += 1.0f / static_cast<float>(sampleRate_);
                    }
                    if (v.transition < 1.0f) v.transition = std::min(1.0f, v.transition + v.transitionStep);
                    v.energy += (std::fabs(voiceSample * gain * activity) - v.energy) * 0.025f;
                }
            }
        }
        for (uint32_t channel = 0; channel < ambiChannels; ++channel) {
            if (!outputs[channel]) continue;
            for (uint32_t frame = 0; frame < frames; ++frame) outputs[channel][frame] = softSat(outputs[channel][frame]);
        }
    }

private:
    static float fract(float value) { return value - std::floor(value); }
    static float lerpWrappedUnit(float from, float to, float amount)
    {
        float delta = to - from;
        if (delta > 0.5f) delta -= 1.0f;
        else if (delta < -0.5f) delta += 1.0f;
        return fract(from + delta * amount);
    }
    static float wrapSignedDeg(float value)
    {
        while (value > 180.0f) value -= 360.0f;
        while (value <= -180.0f) value += 360.0f;
        return value;
    }
    static uint32_t hash(uint32_t value)
    {
        value ^= value >> 16u; value *= 0x7feb352du;
        value ^= value >> 15u; value *= 0x846ca68bu;
        value ^= value >> 16u;
        return value ? value : 1u;
    }
    static float deterministicSigned(uint32_t value)
    {
        return static_cast<float>(hash(value) & 0x00ffffffu) / 8388607.5f - 1.0f;
    }
    static float cell(float x, float y, float z)
    {
        const float dx = x - std::round(x);
        const float dy = y - std::round(y);
        const float dz = z - std::round(z);
        return 1.0f - clamp(std::sqrt(dx * dx + dy * dy + dz * dz) * 1.35f, 0.0f, 1.0f);
    }

    float randomUnit(AmbiWaveTerrainVoice& voice) const
    {
        voice.randomState = voice.randomState * 1664525u + 1013904223u;
        return static_cast<float>((voice.randomState >> 8u) & 0x00ffffffu) / 16777215.0f;
    }
    float randomSigned(AmbiWaveTerrainVoice& voice) const { return randomUnit(voice) * 2.0f - 1.0f; }

    float envelopeCoefficient(float milliseconds) const
    {
        const float samples = std::max(1.0f, milliseconds * 0.001f * static_cast<float>(sampleRate_));
        return 1.0f - std::exp(-6.90775528f / samples);
    }

    float terrainLayer(float u, float v, uint32_t layer) const
    {
        const Vec3 direction = directionFromAed((u - 0.5f) * 360.0f, (v - 0.5f) * 180.0f);
        const float frequency = 1.15f + static_cast<float>(layer) * (0.68f + params_.terrainRoughness * 0.48f);
        const float x = direction.x * frequency + static_cast<float>(layer) * 0.31f;
        const float y = direction.y * (frequency + 0.37f) - static_cast<float>(layer) * 0.17f;
        const float z = direction.z * (frequency + 0.71f) + static_cast<float>(layer) * 0.23f;
        const float harmonic = std::sin(kPi * (x + 0.31f * z)) * std::cos(kPi * (y - 0.23f * z));
        const float diagonal = std::sin(kPi * (x * 0.73f + y * 0.91f + z * 1.17f));
        const float cellularRaw = cell(x * 1.55f + layer * 0.37f, y * 1.55f, z * 1.55f);
        const float cellular = cellularRaw * 2.0f - 1.0f;
        const float fbm = 0.55f * harmonic + 0.32f * diagonal
            + 0.13f * std::sin(kPi * (x * 2.3f - y * 1.7f + z * 0.41f));
        const float ridges = 1.0f - 2.0f * std::fabs(fbm);
        const float dunes = std::sin(kAmbiWaveTerrainTwoPi * (0.19f * x + 0.08f * std::sin(kPi * (y + z))))
            * (0.72f + 0.28f * std::cos(kPi * z));
        const float craterRim = std::exp(-std::pow((cellularRaw - 0.52f) * 7.0f, 2.0f)) * 2.0f - 1.0f;
        const float craters = clamp(0.70f * craterRim - 0.72f * cellularRaw + 0.18f * harmonic, -1.0f, 1.0f);
        const float tectonic = softSat(1.55f * diagonal + 0.75f * (std::fabs(harmonic) * 2.0f - 1.0f));
        switch (params_.skin) {
        case AmbiWaveTerrainSkin::Harmonic: return 0.70f * harmonic + 0.30f * diagonal;
        case AmbiWaveTerrainSkin::Cellular: return 0.75f * cellular + 0.25f * harmonic;
        case AmbiWaveTerrainSkin::Vot: return softSat(1.30f * fbm + 0.45f * cellular);
        case AmbiWaveTerrainSkin::Ridges: return ridges;
        case AmbiWaveTerrainSkin::Dunes: return dunes;
        case AmbiWaveTerrainSkin::Craters: return craters;
        case AmbiWaveTerrainSkin::Tectonic: return tectonic;
        case AmbiWaveTerrainSkin::Fbm:
        default: return fbm;
        }
    }

    float terrain(float u, float v) const
    {
        float value = 0.0f;
        float weight = 0.0f;
        for (uint32_t layer = 0; layer < 4u; ++layer) {
            const float w = std::pow(0.62f + params_.terrainRoughness * 0.13f, static_cast<float>(layer));
            value += terrainLayer(u, v, layer) * w;
            weight += w;
        }
        value = weight > 0.0f ? value / weight : 0.0f;
        const float folded = std::sin(value * (1.0f + 6.0f * params_.terrainFold));
        return softSat(lerp(value, folded, params_.terrainFold) * 1.45f);
    }

    struct ContourSample {
        float u = 0.5f;
        float v = 0.5f;
        float carrier = 0.0f;
    };

    ContourSample contourSample(const AmbiWaveTerrainRegion& region, float phase) const
    {
        const float angle = kAmbiWaveTerrainTwoPi * phase;
        float x = 0.0f;
        float y = 0.0f;
        switch (region.trace) {
        case AmbiWaveTerrainTrace::Orbit:
            x = std::cos(angle); y = std::sin(angle); break;
        case AmbiWaveTerrainTrace::Rosette:
            x = std::cos(angle) * (0.58f + 0.42f * std::cos(angle * 5.0f));
            y = std::sin(angle) * (0.58f + 0.42f * std::cos(angle * 5.0f));
            break;
        case AmbiWaveTerrainTrace::Fold:
            x = std::sin(angle);
            y = std::asin(clamp(std::sin(angle * 2.0f + x * 1.4f), -1.0f, 1.0f)) / (kPi * 0.5f);
            break;
        case AmbiWaveTerrainTrace::Lissajous:
        default:
            x = std::sin(angle * 2.0f + kPi * 0.5f);
            y = std::sin(angle * 3.0f);
            break;
        }
        x += std::sin(angle * 7.0f) * params_.scanWarp * 0.16f;
        y += std::cos(angle * 5.0f) * params_.scanWarp * 0.16f;
        const float ca = std::cos(region.rotation * kPi);
        const float sa = std::sin(region.rotation * kPi);
        const float ex = x * region.radius;
        const float ey = y * region.radius * region.aspect;
        const float du = ex * ca - ey * sa;
        const float dv = ex * sa + ey * ca;
        return { fract(region.u + du), clamp(region.v + dv, 0.0f, 1.0f), clamp(dv / std::max(0.005f, region.radius), -1.0f, 1.0f) };
    }

    std::array<float, 2> contour(const AmbiWaveTerrainRegion& region, float phase) const
    {
        const auto sample = contourSample(region, phase);
        return { sample.u, sample.v };
    }

    void renderTable(AmbiWaveTerrainTable& table, const AmbiWaveTerrainRegion& region) const
    {
        table.region = region;
        const float terrainAmount = params_.terrainDepth * params_.terrainRelief;
        for (uint32_t index = 0; index < kAmbiWaveTerrainTableSize; ++index) {
            const auto sample = contourSample(region, static_cast<float>(index) / static_cast<float>(kAmbiWaveTerrainTableSize));
            const float terrainValue = terrain(sample.u, sample.v);
            table.terrainProfile[index] = lerp(sample.carrier, terrainValue, terrainAmount);
        }

        constexpr float kDerivativeScale = static_cast<float>(kAmbiWaveTerrainTableSize) / kAmbiWaveTerrainTwoPi;
        float mean = 0.0f;
        for (uint32_t index = 0; index < kAmbiWaveTerrainTableSize; ++index) {
            const uint32_t previous = (index + kAmbiWaveTerrainTableSize - 1u) % kAmbiWaveTerrainTableSize;
            const uint32_t next = (index + 1u) % kAmbiWaveTerrainTableSize;
            const float height = table.terrainProfile[index];
            const float edge = softSat((table.terrainProfile[next] - table.terrainProfile[previous]) * 0.5f * kDerivativeScale);
            const float curvature = softSat((2.0f * height - table.terrainProfile[next] - table.terrainProfile[previous])
                * kDerivativeScale * kDerivativeScale);
            float value = height;
            if (params_.interpretation == AmbiWaveTerrainInterpretation::Edge) value = edge;
            else if (params_.interpretation == AmbiWaveTerrainInterpretation::Curvature) value = curvature;
            else if (params_.interpretation == AmbiWaveTerrainInterpretation::Blend) {
                value = lerp(height, lerp(edge, curvature, 0.35f), params_.interpretationMix);
            }
            table.levels[0][index] = value;
            mean += value;
        }
        mean /= static_cast<float>(kAmbiWaveTerrainTableSize);
        float peak = 0.0001f;
        for (auto& value : table.levels[0]) { value -= mean; peak = std::max(peak, std::fabs(value)); }
        const float normalize = 0.92f / peak;
        for (auto& value : table.levels[0]) value *= normalize;
        Vec3 firstWorld {};
        Vec3 previousWorld {};
        float traversalLength = 0.0f;
        for (uint32_t index = 0; index < kAmbiWaveTerrainSpatialTableSize; ++index) {
            const auto uv = contour(region, static_cast<float>(index) / static_cast<float>(kAmbiWaveTerrainSpatialTableSize));
            const auto point = surfacePoint(uv[0], uv[1]);
            const auto direction = directionFromAed(point.azimuthDeg, point.elevationDeg);
            const Vec3 world { direction.x * point.distance, direction.y * point.distance, direction.z * point.distance };
            if (index == 0u) firstWorld = world;
            else {
                const float dx = world.x - previousWorld.x, dy = world.y - previousWorld.y, dz = world.z - previousWorld.z;
                traversalLength += std::sqrt(dx * dx + dy * dy + dz * dz);
            }
            previousWorld = world;
            const auto basis = acnSn3dBasis7(direction);
            const float distanceGain = 1.0f / std::max(0.25f, point.distance);
            for (uint32_t channel = 0; channel < kAmbiWaveTerrainMaxChannels; ++channel) {
                table.spatialBasis[index][channel] = basis[channel] * distanceGain;
            }
        }
        const float closeX = firstWorld.x - previousWorld.x, closeY = firstWorld.y - previousWorld.y, closeZ = firstWorld.z - previousWorld.z;
        table.traversalLength = std::max(0.05f, traversalLength + std::sqrt(closeX * closeX + closeY * closeY + closeZ * closeZ));
        for (uint32_t level = 1; level < kAmbiWaveTerrainMipLevels; ++level) {
            const auto& source = table.levels[level - 1u];
            auto& target = table.levels[level];
            for (uint32_t index = 0; index < kAmbiWaveTerrainTableSize; ++index) {
                const uint32_t im2 = (index + kAmbiWaveTerrainTableSize - 2u) % kAmbiWaveTerrainTableSize;
                const uint32_t im1 = (index + kAmbiWaveTerrainTableSize - 1u) % kAmbiWaveTerrainTableSize;
                const uint32_t ip1 = (index + 1u) % kAmbiWaveTerrainTableSize;
                const uint32_t ip2 = (index + 2u) % kAmbiWaveTerrainTableSize;
                target[index] = (source[im2] + 4.0f * source[im1] + 6.0f * source[index] + 4.0f * source[ip1] + source[ip2]) * 0.0625f;
            }
        }
    }

    static float tableSample(const AmbiWaveTerrainTable& table, float phase, float frequency)
    {
        const int level = std::clamp(static_cast<int>(std::floor(std::log2(std::max(1.0f, frequency / 90.0f)))), 0, static_cast<int>(kAmbiWaveTerrainMipLevels - 1u));
        const auto& data = table.levels[static_cast<uint32_t>(level)];
        const float position = fract(phase) * static_cast<float>(kAmbiWaveTerrainTableSize);
        const int i1 = static_cast<int>(std::floor(position));
        const float t = position - std::floor(position);
        const int i0 = (i1 + static_cast<int>(kAmbiWaveTerrainTableSize) - 1) % static_cast<int>(kAmbiWaveTerrainTableSize);
        const int i2 = (i1 + 1) % static_cast<int>(kAmbiWaveTerrainTableSize);
        const int i3 = (i1 + 2) % static_cast<int>(kAmbiWaveTerrainTableSize);
        const float p0 = data[static_cast<uint32_t>(i0)];
        const float p1 = data[static_cast<uint32_t>(i1)];
        const float p2 = data[static_cast<uint32_t>(i2)];
        const float p3 = data[static_cast<uint32_t>(i3)];
        const float t2 = t * t;
        const float t3 = t2 * t;
        return 0.5f * ((2.0f * p1) + (-p0 + p2) * t
            + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
            + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
    }

    struct ScanSample {
        float current = 0.0f;
        float next = 0.0f;
        uint32_t spatialIndex = 0u;
        uint32_t spatialNext = 0u;
        float spatialMix = 0.0f;
    };

    static float spatialSample(const AmbiWaveTerrainTable& table, const ScanSample& scan, uint32_t channel)
    {
        return lerp(table.spatialBasis[scan.spatialIndex][channel],
                    table.spatialBasis[scan.spatialNext][channel], scan.spatialMix);
    }

    ScanSample oscillator(AmbiWaveTerrainVoice& voice, float& phase, float frequency) const
    {
        phase = fract(phase + frequency / static_cast<float>(sampleRate_));
        ScanSample sample {};
        sample.current = tableSample(voice.current, phase, frequency);
        sample.next = tableSample(voice.next, phase, frequency);
        const float spatialPosition = phase * static_cast<float>(kAmbiWaveTerrainSpatialTableSize);
        sample.spatialIndex = static_cast<uint32_t>(std::floor(spatialPosition)) % kAmbiWaveTerrainSpatialTableSize;
        sample.spatialNext = (sample.spatialIndex + 1u) % kAmbiWaveTerrainSpatialTableSize;
        sample.spatialMix = spatialPosition - std::floor(spatialPosition);
        return sample;
    }

    float frequencyForVoice(float note, uint32_t voice, bool midi) const
    {
        const float center = (static_cast<float>(std::max<uint32_t>(1u, params_.voices)) - 1.0f) * 0.5f;
        const float position = center > 0.0f ? (static_cast<float>(voice) - center) / center : 0.0f;
        const float spread = midi ? 0.0f : position * params_.pitchSpreadSemitones * 0.5f;
        const float detune = deterministicSigned(voice * 29u + 7u) * params_.detuneCents;
        const float tuned = note + spread + (params_.tuneCents + detune) / 100.0f;
        return std::min(440.0f * std::pow(2.0f, (tuned - 69.0f) / 12.0f), static_cast<float>(sampleRate_) * 0.45f);
    }

    AmbiWaveTerrainRegion initialRegion(uint32_t voice) const
    {
        const float count = static_cast<float>(std::max<uint32_t>(1u, params_.voices));
        AmbiWaveTerrainRegion region {};
        region.u = fract(static_cast<float>(voice) * 0.61803398875f + 0.17f);
        region.v = clamp((static_cast<float>(voice) + 0.5f) / count, 0.05f, 0.95f);
        region.radius = params_.scanRadius;
        region.aspect = params_.scanAspect;
        region.rotation = params_.scanRotation + deterministicSigned(voice * 11u + 3u) * 0.12f;
        region.trace = params_.trace;
        return region;
    }

    float eventDurationSamples(AmbiWaveTerrainVoice& voice, bool active) const
    {
        const float base = active ? params_.fieldDurationSeconds : params_.fieldRestSeconds;
        const float variation = std::exp2(randomSigned(voice) * params_.regionDeviation * 1.5f);
        return std::max(1.0f, base * variation * static_cast<float>(sampleRate_));
    }

    AmbiWaveTerrainRegion chooseRegion(uint32_t voiceIndex)
    {
        auto& voice = voices_[voiceIndex];
        AmbiWaveTerrainRegion region = voice.next.region;
        const float step = 0.015f + params_.regionDeviation * 0.32f;
        float targetU = region.u;
        float targetV = region.v;
        switch (params_.selection) {
        case AmbiWaveTerrainSelection::Random:
            targetU = randomUnit(voice); targetV = 0.04f + randomUnit(voice) * 0.92f;
            break;
        case AmbiWaveTerrainSelection::Series:
            ++voice.seriesCursor;
            targetU = fract(static_cast<float>(voice.seriesCursor) * 0.61803398875f + static_cast<float>(voiceIndex) * 0.037f);
            targetV = 0.08f + fract(static_cast<float>(voice.seriesCursor) * 0.38196601125f + static_cast<float>(voiceIndex) * 0.053f) * 0.84f;
            break;
        case AmbiWaveTerrainSelection::Weight: {
            float best = -1.0f;
            for (uint32_t candidate = 0; candidate < 5u; ++candidate) {
                const float u = fract(region.u + randomSigned(voice) * step * 2.0f);
                const float v = clamp(region.v + randomSigned(voice) * step * 2.0f, 0.03f, 0.97f);
                const float score = std::fabs(terrainHeight(u, v));
                if (score > best) { best = score; targetU = u; targetV = v; }
            }
            break;
        }
        case AmbiWaveTerrainSelection::Tendency:
            targetU = fract(lerp(region.u, macroU_, 0.18f + params_.regionDeviation * 0.42f) + randomSigned(voice) * step * 0.35f);
            targetV = clamp(lerp(region.v, macroV_, 0.18f + params_.regionDeviation * 0.42f) + randomSigned(voice) * step * 0.35f, 0.03f, 0.97f);
            break;
        case AmbiWaveTerrainSelection::Markov: {
            const uint32_t direction = std::min<uint32_t>(7u, static_cast<uint32_t>(randomUnit(voice) * 8.0f));
            const float angle = static_cast<float>(direction) * kAmbiWaveTerrainTwoPi / 8.0f;
            targetU = fract(region.u + std::cos(angle) * step);
            targetV = clamp(region.v + std::sin(angle) * step, 0.03f, 0.97f);
            break;
        }
        case AmbiWaveTerrainSelection::Walk:
        default:
            targetU = fract(region.u + randomSigned(voice) * step);
            targetV = clamp(region.v + randomSigned(voice) * step, 0.03f, 0.97f);
            break;
        }

        if (params_.transition == AmbiWaveTerrainTransition::Merge) {
            const uint32_t other = (voiceIndex + 1u) % std::max<uint32_t>(1u, params_.voices);
            const auto neighborRegion = voices_[other].next.region;
            targetU = fract(lerp(targetU, neighborRegion.u, params_.neighborTransfer));
            targetV = clamp(lerp(targetV, neighborRegion.v, params_.neighborTransfer), 0.03f, 0.97f);
            neighbor_[voiceIndex] = other;
        } else if (params_.transition == AmbiWaveTerrainTransition::Vary) {
            targetU = fract(lerp(region.u, targetU, 0.12f + params_.regionDeviation * 0.18f));
            targetV = clamp(lerp(region.v, targetV, 0.12f + params_.regionDeviation * 0.18f), 0.03f, 0.97f);
        } else {
            neighbor_[voiceIndex] = (voiceIndex + 1u) % std::max<uint32_t>(1u, params_.voices);
        }

        const float memory = params_.selectionMemory;
        region.u = fract(lerp(targetU, region.u, memory * 0.82f));
        region.v = clamp(lerp(targetV, region.v, memory * 0.82f), 0.03f, 0.97f);
        region.radius = clamp(params_.scanRadius * std::exp2(randomSigned(voice) * params_.regionDeviation), 0.005f, 0.48f);
        region.aspect = clamp(params_.scanAspect + randomSigned(voice) * params_.regionDeviation * 0.28f, 0.05f, 1.0f);
        region.rotation = clamp(params_.scanRotation + randomSigned(voice) * params_.regionDeviation, -1.0f, 1.0f);
        region.trace = params_.trace;
        return region;
    }

    void beginTransition(uint32_t voiceIndex, const AmbiWaveTerrainRegion& region)
    {
        auto& voice = voices_[voiceIndex];
        if (voice.transition >= 1.0f) voice.current = voice.next;
        else {
            const float t = voice.transition;
            for (uint32_t level = 0; level < kAmbiWaveTerrainMipLevels; ++level) {
                for (uint32_t index = 0; index < kAmbiWaveTerrainTableSize; ++index) {
                    voice.current.levels[level][index] = lerp(voice.current.levels[level][index], voice.next.levels[level][index], t);
                }
            }
            for (uint32_t index = 0; index < kAmbiWaveTerrainTableSize; ++index) {
                voice.current.terrainProfile[index] = lerp(
                    voice.current.terrainProfile[index], voice.next.terrainProfile[index], t);
            }
            for (uint32_t index = 0; index < kAmbiWaveTerrainSpatialTableSize; ++index) {
                for (uint32_t channel = 0; channel < kAmbiWaveTerrainMaxChannels; ++channel) {
                    voice.current.spatialBasis[index][channel] = lerp(
                        voice.current.spatialBasis[index][channel], voice.next.spatialBasis[index][channel], t);
                }
            }
            voice.current.traversalLength = lerp(voice.current.traversalLength, voice.next.traversalLength, t);
            voice.current.region.u = lerpWrappedUnit(voice.current.region.u, voice.next.region.u, t);
            voice.current.region.v = lerp(voice.current.region.v, voice.next.region.v, t);
            voice.current.region.radius = lerp(voice.current.region.radius, voice.next.region.radius, t);
            voice.current.region.aspect = lerp(voice.current.region.aspect, voice.next.region.aspect, t);
            voice.current.region.rotation = lerp(voice.current.region.rotation, voice.next.region.rotation, t);
            if (t >= 0.5f) voice.current.region.trace = voice.next.region.trace;
        }
        renderTable(voice.next, region);
        voice.transition = 0.0f;
        voice.transitionStep = 1.0f / std::max(1.0f, params_.tableXfadeMs * 0.001f * static_cast<float>(sampleRate_));
    }

    void updateField(uint32_t voiceIndex, uint32_t frames)
    {
        auto& voice = voices_[voiceIndex];
        voice.eventSamples -= static_cast<float>(frames);
        if (voice.eventSamples > 0.0f) return;
        if (voice.fieldActive) {
            beginTransition(voiceIndex, chooseRegion(voiceIndex));
            voice.fieldActive = randomUnit(voice) <= params_.fieldDensity;
        } else {
            voice.fieldActive = true;
        }
        fieldActive_[voiceIndex] = voice.fieldActive ? 1u : 0u;
        voice.eventSamples = eventDurationSamples(voice, voice.fieldActive);
    }

    AmbiWaveTerrainPoint pointForRegion(const AmbiWaveTerrainRegion& region) const { return surfacePoint(region.u, region.v); }
    AmbiWaveTerrainPoint scanPointForRegion(const AmbiWaveTerrainRegion& region, float phase) const
    {
        const auto uv = contour(region, phase);
        return surfacePoint(uv[0], uv[1]);
    }

    void updatePoint(uint32_t voiceIndex)
    {
        auto& voice = voices_[voiceIndex];
        const float phase = voiceScanPhase(voiceIndex);
        const auto from = scanPointForRegion(voice.current.region, phase);
        const auto to = scanPointForRegion(voice.next.region, phase);
        AmbiWaveTerrainPoint target {};
        const float t = voice.transition;
        target.azimuthDeg = wrapSignedDeg(from.azimuthDeg + wrapSignedDeg(to.azimuthDeg - from.azimuthDeg) * t);
        target.elevationDeg = lerp(from.elevationDeg, to.elevationDeg, t);
        target.distance = lerp(from.distance, to.distance, t);
        target.terrain = lerp(from.terrain, to.terrain, t);
        const float follow = 1.0f - params_.spatialFollow;
        auto& point = points_[voiceIndex];
        point.azimuthDeg = wrapSignedDeg(point.azimuthDeg + wrapSignedDeg(target.azimuthDeg - point.azimuthDeg) * follow);
        point.elevationDeg += (target.elevationDeg - point.elevationDeg) * follow;
        point.distance += (target.distance - point.distance) * follow;
        point.terrain += (target.terrain - point.terrain) * follow;
        previousPoints_[voiceIndex] = point;
    }

    void updateMacro(uint32_t frames)
    {
        const float duration = std::max(1.0f, params_.macroDurationSeconds * static_cast<float>(sampleRate_));
        macroPhase_ += static_cast<float>(frames) / duration;
        if (macroPhase_ >= 1.0f) {
            macroPhase_ = fract(macroPhase_);
            macroStartU_ = macroTargetU_;
            macroStartV_ = macroTargetV_;
            macroSeed_ = hash(macroSeed_ + 0x9e3779b9u);
            macroTargetU_ = static_cast<float>(macroSeed_ & 0xffffu) / 65535.0f;
            macroTargetV_ = 0.08f + static_cast<float>((macroSeed_ >> 16u) & 0xffffu) / 65535.0f * 0.84f;
        }
        const float smooth = macroPhase_ * macroPhase_ * (3.0f - 2.0f * macroPhase_);
        macroU_ = fract(lerp(macroStartU_, macroTargetU_, smooth));
        macroV_ = lerp(macroStartV_, macroTargetV_, smooth);
    }

    void refreshDirtyVoice()
    {
        if (dirtyVoice_ >= params_.voices) return;
        auto& voice = voices_[dirtyVoice_];
        AmbiWaveTerrainRegion region = voice.next.region;
        region.radius = params_.scanRadius;
        region.aspect = params_.scanAspect;
        region.rotation = params_.scanRotation;
        region.trace = params_.trace;
        beginTransition(dirtyVoice_, region);
        ++dirtyVoice_;
    }

    double sampleRate_ = 48000.0;
    AmbiWaveTerrainParams params_ {};
    std::array<AmbiWaveTerrainVoice, kAmbiWaveTerrainMaxVoices> voices_ {};
    std::array<AmbiWaveTerrainPoint, kAmbiWaveTerrainMaxVoices> points_ {};
    std::array<AmbiWaveTerrainPoint, kAmbiWaveTerrainMaxVoices> previousPoints_ {};
    std::array<uint8_t, kAmbiWaveTerrainMaxVoices> fieldActive_ {};
    std::array<uint32_t, kAmbiWaveTerrainMaxVoices> neighbor_ {};
    uint32_t dirtyVoice_ = kAmbiWaveTerrainMaxVoices;
    uint32_t macroSeed_ = 0x51ed270bu;
    float macroU_ = 0.5f;
    float macroV_ = 0.5f;
    float macroStartU_ = 0.5f;
    float macroStartV_ = 0.5f;
    float macroTargetU_ = 0.67f;
    float macroTargetV_ = 0.38f;
    float macroPhase_ = 0.0f;
};

inline const char* ambiWaveTerrainModeName(AmbiWaveTerrainMode mode)
{
    switch (mode) { case AmbiWaveTerrainMode::Midi: return "MIDI"; case AmbiWaveTerrainMode::Both: return "BOTH"; default: return "FREE"; }
}
inline const char* ambiWaveTerrainSkinName(AmbiWaveTerrainSkin skin)
{
    static constexpr const char* names[] { "HARMONIC", "FBM", "CELL", "VOT", "RIDGES", "DUNES", "CRATERS", "TECTONIC" };
    return names[std::min<uint32_t>(static_cast<uint32_t>(skin), 7u)];
}
inline const char* ambiWaveTerrainTraceName(AmbiWaveTerrainTrace trace)
{
    static constexpr const char* names[] { "ORBIT", "LISSAJOUS", "ROSETTE", "FOLD" };
    return names[std::min<uint32_t>(static_cast<uint32_t>(trace), 3u)];
}
inline const char* ambiWaveTerrainSelectionName(AmbiWaveTerrainSelection selection)
{
    static constexpr const char* names[] { "RANDOM", "SERIES", "WEIGHT", "TENDENCY", "MARKOV", "WALK" };
    return names[std::min<uint32_t>(static_cast<uint32_t>(selection), 5u)];
}
inline const char* ambiWaveTerrainTransitionName(AmbiWaveTerrainTransition transition)
{
    static constexpr const char* names[] { "LINK", "MERGE", "VARY" };
    return names[std::min<uint32_t>(static_cast<uint32_t>(transition), 2u)];
}
inline const char* ambiWaveTerrainInterpretationName(AmbiWaveTerrainInterpretation interpretation)
{
    static constexpr const char* names[] { "HEIGHT", "EDGE", "CURVE", "BLEND" };
    return names[std::min<uint32_t>(static_cast<uint32_t>(interpretation), 3u)];
}

} // namespace s3g
