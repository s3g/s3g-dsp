#pragma once

#include "s3g_ambi_field_listener.h"
#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_environmental_score.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace s3g {

inline constexpr uint32_t kAmbiHorizonMaxOrder = 7u;
inline constexpr uint32_t kAmbiHorizonMaxChannels = 64u;
inline constexpr uint32_t kAmbiHorizonMaxEntities = 32u;

enum class AmbiHorizonEcology : uint32_t {
    Mixed = 0u,
    Rural,
    Traffic,
    City,
    Industrial,
    Water,
    Weather,
    Airport,
    Coast,
};

enum class AmbiHorizonGround : uint32_t {
    Water = 0u,
    Hard,
    Mixed,
    Grass,
    Forest,
};

enum class AmbiHorizonLayer : uint8_t {
    LocalFloor = 0u,
    HorizonBed,
    HorizonSignal,
};

struct AmbiHorizonEncoderParams {
    uint32_t order = 3u;
    uint32_t entities = 24u;
    AmbiHorizonEcology ecology = AmbiHorizonEcology::Mixed;
    float activity = 0.48f;
    float occupancy = 0.36f;
    float pace = 0.42f;
    float memory = 0.68f;
    float cascade = 0.48f;
    float signals = 0.48f;
    float horizonBed = 0.35f;
    float localFloor = 0.22f;
    float rangeKm = 2.8f;
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float arcDeg = 240.0f;
    float detail = 0.54f;
    float air = 0.58f;
    AmbiHorizonGround ground = AmbiHorizonGround::Mixed;
    float terrain = 0.42f;
    float carry = 0.0f;
    float turbulence = 0.24f;
    float edgeDb = 0.0f;
    float outputGainDb = -6.0f;
    uint32_t seed = 1979u;
    float airNoise = 0.35f;
    float machines = 0.45f;
    float bells = 0.25f;
    float traffic = 0.45f;
    float aircraft = 0.0f;
    float foghorns = 0.0f;
    float surf = 0.15f;
    float trafficSpeed = 0.50f;
    float engineLoad = 0.55f;
    float aircraftFlight = 0.80f;
    float aircraftSpeed = 0.52f;
    float aircraftPower = 0.62f;
    float aircraftTone = 0.35f;
    float foghornPitch = 0.42f;
    float foghornPressure = 0.75f;
    float foghornLength = 0.55f;
    float waveRate = 0.45f;
    float waveBreak = 0.58f;
    float machineTone = 0.50f;
    float bellPitch = 0.52f;
    float bellDecay = 0.68f;
    AmbiFieldListenMode fieldListenMode = AmbiFieldListenMode::Off;
    float fieldListenAmount = 0.65f;
    AmbiFieldListenerResponse fieldListenResponse =
        AmbiFieldListenerResponse::Legacy;
};

struct AmbiHorizonEntityTelemetry {
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float rangeNorm = 0.0f;
    float energy = 0.0f;
    AmbiHorizonLayer layer = AmbiHorizonLayer::HorizonBed;
};

// Autonomous acoustic-horizon scene generator. Source synthesis happens in
// mono per entity; outdoor propagation is applied before the final ACN/SN3D
// encoding stage. The kilometer control is perceptual rather than a calibrated
// sound-pressure model: it preserves the defining condition that very distant
// events remain faintly audible in a low-noise environment.
class AmbiHorizonEncoder {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1000.0, sampleRate);
        smoothingCoefficient_ = 1.0f - std::exp(
            -1.0f / (0.020f * static_cast<float>(sampleRate_)));
        listenerEnergyCoefficient_ = 1.0f - std::exp(
            -1.0f / (0.75f * static_cast<float>(sampleRate_)));
        landscapeDelayBuffer_.assign(static_cast<size_t>(
            std::ceil(sampleRate_ * 2.25)) + 2u, 0.0f);
        fieldListener_.prepare(sampleRate_);
        fieldListener_.setMemorySeconds(0.92f);
        fieldListener_.setExtendedAnalysisEnabled(true);
        const auto& listenerDirections = ambiFieldListenerCubeDirections();
        fieldListener_.setDirections(listenerDirections.data(),
            static_cast<uint32_t>(listenerDirections.size()));
        score_.prepare(sampleRate_, params_.seed);
        score_.setRangeExpansion(1.0f);
        rebuildVoices();
        reset();
    }

    void reset()
    {
        frameCounter_ = 0u;
        score_.prepare(sampleRate_, params_.seed);
        score_.setRangeExpansion(1.0f);
        score_.reset(params_.entities);
        rebuildVoices();
        renderEntityCount_ = params_.entities;
        for (uint32_t index = 0u; index < kAmbiHorizonMaxEntities; ++index) {
            auto& voice = voices_[index];
            voice.phase = randomUnit(voice.rng);
            voice.phase2 = randomUnit(voice.rng);
            voice.eventPhase = voice.kind == SourceKind::Foghorn
                ? voice.identity * 0.08f : randomUnit(voice.rng);
            voice.slowPhase = randomUnit(voice.rng);
            voice.fastPhase = randomUnit(voice.rng);
            voice.envelope = 0.0f;
            voice.eventEnvelope = 0.0f;
            voice.eventTargetEnvelope = 0.0f;
            voice.low = 0.0f;
            voice.low2 = 0.0f;
            voice.localBody = 0.0f;
            voice.localBody2 = 0.0f;
            voice.bedBody = 0.0f;
            voice.bedBody2 = 0.0f;
            voice.textureLow = 0.0f;
            voice.textureBand = 0.0f;
            voice.hornBody = 0.0f;
            voice.hornBell = 0.0f;
            voice.transientBody = 0.0f;
            voice.band = 0.0f;
            voice.modal1 = 0.0f;
            voice.modal2 = 0.0f;
            voice.bellEnvelope = 0.0f;
            voice.bellTargetEnvelope = 0.0f;
            voice.pulseGain = 0.0f;
            voice.energy = 0.0f;
            voice.listenerGain = 1.0f;
            voice.targetListenerGain = 1.0f;
            voice.listenerReturnGain = 1.0f;
            voice.targetListenerReturnGain = 1.0f;
            voice.listenerDistanceBias = 0.0f;
            voice.targetListenerDistanceBias = 0.0f;
        }
        smoothedAirNoise_ = params_.airNoise;
        smoothedMachines_ = params_.machines;
        smoothedBells_ = params_.bells;
        smoothedTraffic_ = params_.traffic;
        smoothedAircraft_ = params_.aircraft;
        smoothedFoghorns_ = params_.foghorns;
        smoothedSurf_ = params_.surf;
        smoothedTrafficSpeed_ = params_.trafficSpeed;
        smoothedEngineLoad_ = params_.engineLoad;
        smoothedAircraftFlight_ = params_.aircraftFlight;
        smoothedAircraftSpeed_ = params_.aircraftSpeed;
        smoothedAircraftPower_ = params_.aircraftPower;
        smoothedAircraftTone_ = params_.aircraftTone;
        smoothedFoghornPitch_ = params_.foghornPitch;
        smoothedFoghornPressure_ = params_.foghornPressure;
        smoothedFoghornLength_ = params_.foghornLength;
        smoothedWaveRate_ = params_.waveRate;
        smoothedWaveBreak_ = params_.waveBreak;
        smoothedMachineTone_ = params_.machineTone;
        smoothedBellPitch_ = params_.bellPitch;
        smoothedBellDecay_ = params_.bellDecay;
        updateControl(64u);
        std::fill(landscapeDelayBuffer_.begin(), landscapeDelayBuffer_.end(), 0.0f);
        landscapeWriteIndex_ = 0u;
        landscapeInputLow_ = 0.0f;
        landscapeFeedbackLow_ = 0.0f;
        landscapeReturnEnergy_ = 0.0f;
        listenerDirectEnergy_ = 0.0f;
        listenerReturnEnergy_ = 0.0f;
        listenerReturnShare_ = 0.0f;
        fieldListener_.reset();
        smoothedLandscapeAmount_ = params_.horizonBed;
        smoothedLandscapeFeedback_ = targetLandscapeFeedback_;
        smoothedLandscapeInputAlpha_ = targetLandscapeInputAlpha_;
        smoothedLandscapeDampingAlpha_ = targetLandscapeDampingAlpha_;
        smoothedLandscapeReturnGain_ = targetLandscapeReturnGain_;
        landscapeDelaySamples_ = targetLandscapeDelaySamples_;
        landscapeEncodingWeights_ = landscapeEncodingTargets_;
    }

    void setParams(AmbiHorizonEncoderParams params)
    {
        params.order = std::clamp<uint32_t>(params.order, 1u, kAmbiHorizonMaxOrder);
        params.entities = std::clamp<uint32_t>(params.entities, 4u, kAmbiHorizonMaxEntities);
        params.ecology = static_cast<AmbiHorizonEcology>(std::min<uint32_t>(
            static_cast<uint32_t>(params.ecology), 8u));
        params.ground = static_cast<AmbiHorizonGround>(std::min<uint32_t>(
            static_cast<uint32_t>(params.ground), 4u));
        params.activity = finiteUnit(params.activity, params_.activity);
        params.occupancy = finiteUnit(params.occupancy, params_.occupancy);
        params.pace = finiteUnit(params.pace, params_.pace);
        params.memory = finiteUnit(params.memory, params_.memory);
        params.cascade = finiteUnit(params.cascade, params_.cascade);
        params.signals = finiteUnit(params.signals, params_.signals);
        params.horizonBed = finiteUnit(params.horizonBed, params_.horizonBed);
        params.localFloor = finiteUnit(params.localFloor, params_.localFloor);
        params.rangeKm = std::clamp(finite(params.rangeKm, params_.rangeKm), 0.03f, 20.0f);
        params.azimuthDeg = std::clamp(finite(params.azimuthDeg, params_.azimuthDeg), -180.0f, 180.0f);
        params.elevationDeg = std::clamp(finite(params.elevationDeg, params_.elevationDeg), -20.0f, 20.0f);
        params.arcDeg = std::clamp(finite(params.arcDeg, params_.arcDeg), 0.0f, 360.0f);
        params.detail = finiteUnit(params.detail, params_.detail);
        params.air = finiteUnit(params.air, params_.air);
        params.terrain = finiteUnit(params.terrain, params_.terrain);
        params.carry = std::clamp(finite(params.carry, params_.carry), -1.0f, 1.0f);
        params.turbulence = finiteUnit(params.turbulence, params_.turbulence);
        params.edgeDb = std::clamp(finite(params.edgeDb, params_.edgeDb), -18.0f, 9.0f);
        params.outputGainDb = std::clamp(finite(params.outputGainDb, params_.outputGainDb), -60.0f, 12.0f);
        params.seed = std::clamp<uint32_t>(params.seed, 1u, 65535u);
        params.airNoise = finiteUnit(params.airNoise, params_.airNoise);
        params.machines = finiteUnit(params.machines, params_.machines);
        params.bells = finiteUnit(params.bells, params_.bells);
        params.traffic = finiteUnit(params.traffic, params_.traffic);
        params.aircraft = finiteUnit(params.aircraft, params_.aircraft);
        params.foghorns = finiteUnit(params.foghorns, params_.foghorns);
        params.surf = finiteUnit(params.surf, params_.surf);
        params.trafficSpeed = finiteUnit(params.trafficSpeed, params_.trafficSpeed);
        params.engineLoad = finiteUnit(params.engineLoad, params_.engineLoad);
        params.aircraftFlight = finiteUnit(params.aircraftFlight, params_.aircraftFlight);
        params.aircraftSpeed = finiteUnit(params.aircraftSpeed, params_.aircraftSpeed);
        params.aircraftPower = finiteUnit(params.aircraftPower, params_.aircraftPower);
        params.aircraftTone = finiteUnit(params.aircraftTone, params_.aircraftTone);
        params.foghornPitch = finiteUnit(params.foghornPitch, params_.foghornPitch);
        params.foghornPressure = finiteUnit(params.foghornPressure, params_.foghornPressure);
        params.foghornLength = finiteUnit(params.foghornLength, params_.foghornLength);
        params.waveRate = finiteUnit(params.waveRate, params_.waveRate);
        params.waveBreak = finiteUnit(params.waveBreak, params_.waveBreak);
        params.machineTone = finiteUnit(params.machineTone, params_.machineTone);
        params.bellPitch = finiteUnit(params.bellPitch, params_.bellPitch);
        params.bellDecay = finiteUnit(params.bellDecay, params_.bellDecay);
        params.fieldListenMode = sanitizeAmbiFieldListenMode(
            params.fieldListenMode);
        params.fieldListenAmount = finiteUnit(
            params.fieldListenAmount, params_.fieldListenAmount);
        params.fieldListenResponse = sanitizeAmbiFieldListenerResponse(
            params.fieldListenResponse);

        const uint32_t previousEntities = params_.entities;
        const bool identityChanged = params.seed != params_.seed
            || params.ecology != params_.ecology;
        params_ = params;
        if (identityChanged) {
            score_.prepare(sampleRate_, params_.seed);
            score_.setRangeExpansion(1.0f);
            rebuildVoices();
        } else if (params_.entities != previousEntities) {
            renderEntityCount_ = std::max(renderEntityCount_,
                std::max(previousEntities, params_.entities));
        }
        updateScoreParams();
    }

    const AmbiHorizonEncoderParams& params() const { return params_; }

    uint32_t activeChannels() const
    {
        return (params_.order + 1u) * (params_.order + 1u);
    }

    uint32_t activeEntities() const { return params_.entities; }

    float fieldListenEnvelope(uint32_t lobe) const
    {
        return fieldListener_.envelope(lobe);
    }

    float fieldListenActivity() const { return fieldListener_.activity(); }

    float fieldListenReturnShare() const { return listenerReturnShare_; }

    AmbiHorizonEntityTelemetry entityTelemetry(uint32_t index) const
    {
        if (index >= params_.entities) return {};
        const auto& voice = voices_[index];
        return { voice.azimuthDeg, voice.elevationDeg, voice.rangeNorm,
            voice.energy, voice.layer };
    }

    void processBlock(float* const* outputs, uint32_t outputChannels,
                      uint32_t frames)
    {
        if (!outputs || frames == 0u) return;
        const uint32_t channels = std::min<uint32_t>(
            outputChannels, activeChannels());
        for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
            clearChannel(outputs[channel], frames);
        }
        if (channels == 0u) return;

        updateControl(frames);
        const float master = dbToGain(params_.outputGainDb);
        const float inverseMaster = 1.0f / std::max(master, 1.0e-6f);
        // The horizon is deliberately low-level, but the original preview
        // reference placed its quietest startup near -73 dBFS RMS and was
        // effectively silent at normal monitor gain. Preserve density
        // normalization while lifting the scene into an audible distant-field
        // range; OUT still supplies 18 dB of downward trim and 12 dB upward.
        const float densityGain = 1.0f
            / std::sqrt(std::max(1.0f, static_cast<float>(params_.entities) / 8.0f));
        const float edgeGain = dbToGain(params_.edgeDb);
        for (uint32_t index = 0u; index < renderEntityCount_; ++index) {
            auto& voice = voices_[index];
            if (index < params_.entities) {
                const float layerGain = voice.layer == AmbiHorizonLayer::LocalFloor
                    ? params_.localFloor
                    : edgeGain * (voice.layer == AmbiHorizonLayer::HorizonBed
                        ? params_.horizonBed * 0.58f : params_.signals);
                voice.targetBlockGain = layerGain * master * densityGain;
            } else {
                voice.targetBlockGain = 0.0f;
            }
        }

        for (uint32_t frame = 0u; frame < frames; ++frame) {
            float landscapeSend = 0.0f;
            float directEnergy = 0.0f;
            std::array<float, kAmbiHorizonMaxChannels> listenerFrame {};
            smoothedAirNoise_ += (params_.airNoise - smoothedAirNoise_)
                * smoothingCoefficient_;
            smoothedMachines_ += (params_.machines - smoothedMachines_)
                * smoothingCoefficient_;
            smoothedBells_ += (params_.bells - smoothedBells_)
                * smoothingCoefficient_;
            smoothedTraffic_ += (params_.traffic - smoothedTraffic_)
                * smoothingCoefficient_;
            smoothedAircraft_ += (params_.aircraft - smoothedAircraft_)
                * smoothingCoefficient_;
            smoothedFoghorns_ += (params_.foghorns - smoothedFoghorns_)
                * smoothingCoefficient_;
            smoothedSurf_ += (params_.surf - smoothedSurf_)
                * smoothingCoefficient_;
            smoothedTrafficSpeed_ += (params_.trafficSpeed - smoothedTrafficSpeed_)
                * smoothingCoefficient_;
            smoothedEngineLoad_ += (params_.engineLoad - smoothedEngineLoad_)
                * smoothingCoefficient_;
            smoothedAircraftFlight_ += (params_.aircraftFlight - smoothedAircraftFlight_)
                * smoothingCoefficient_;
            smoothedAircraftSpeed_ += (params_.aircraftSpeed - smoothedAircraftSpeed_)
                * smoothingCoefficient_;
            smoothedAircraftPower_ += (params_.aircraftPower - smoothedAircraftPower_)
                * smoothingCoefficient_;
            smoothedAircraftTone_ += (params_.aircraftTone - smoothedAircraftTone_)
                * smoothingCoefficient_;
            smoothedFoghornPitch_ += (params_.foghornPitch - smoothedFoghornPitch_)
                * smoothingCoefficient_;
            smoothedFoghornPressure_ +=
                (params_.foghornPressure - smoothedFoghornPressure_)
                * smoothingCoefficient_;
            smoothedFoghornLength_ += (params_.foghornLength - smoothedFoghornLength_)
                * smoothingCoefficient_;
            smoothedWaveRate_ += (params_.waveRate - smoothedWaveRate_)
                * smoothingCoefficient_;
            smoothedWaveBreak_ += (params_.waveBreak - smoothedWaveBreak_)
                * smoothingCoefficient_;
            smoothedMachineTone_ += (params_.machineTone - smoothedMachineTone_)
                * smoothingCoefficient_;
            smoothedBellPitch_ += (params_.bellPitch - smoothedBellPitch_)
                * smoothingCoefficient_;
            smoothedBellDecay_ += (params_.bellDecay - smoothedBellDecay_)
                * smoothingCoefficient_;
            smoothedLandscapeAmount_ +=
                (params_.horizonBed - smoothedLandscapeAmount_)
                * smoothingCoefficient_;
            for (uint32_t index = 0u; index < renderEntityCount_; ++index) {
                auto& voice = voices_[index];
                voice.blockGain += (voice.targetBlockGain - voice.blockGain)
                    * smoothingCoefficient_;
                voice.propagationGain +=
                    (voice.targetPropagationGain - voice.propagationGain)
                    * smoothingCoefficient_;
                voice.modalCoefficient +=
                    (voice.targetModalCoefficient - voice.modalCoefficient)
                    * smoothingCoefficient_;
                voice.modalRadiusSquared +=
                    (voice.targetModalRadiusSquared - voice.modalRadiusSquared)
                    * smoothingCoefficient_;
                voice.listenerGain +=
                    (voice.targetListenerGain - voice.listenerGain)
                    * smoothingCoefficient_;
                voice.listenerReturnGain +=
                    (voice.targetListenerReturnGain
                        - voice.listenerReturnGain)
                    * smoothingCoefficient_;
                voice.listenerDistanceBias +=
                    (voice.targetListenerDistanceBias
                        - voice.listenerDistanceBias)
                    * smoothingCoefficient_;
                const float raw = renderVoice(voice);
                const float propagated = propagate(voice, raw);
                const float sourceGain = sourceLevel(voice.kind);
                const float pathSample = propagated
                    * voice.blockGain * sourceGain * voice.listenerGain;
                if (voice.layer != AmbiHorizonLayer::LocalFloor) {
                    float sendGain = voice.layer
                            == AmbiHorizonLayer::HorizonSignal
                        ? 0.15f : 0.10f;
                    if (voice.kind == SourceKind::Foghorn) {
                        sendGain = 0.78f + 0.62f * voice.rangeNorm;
                    } else if (voice.kind == SourceKind::Modal) {
                        sendGain = 0.28f + 0.18f * voice.rangeNorm;
                    } else if (voice.kind == SourceKind::Motor) {
                        sendGain = 0.20f + 0.16f * voice.rangeNorm;
                    } else if (voice.kind == SourceKind::Aircraft) {
                        sendGain = 0.18f + 0.20f * voice.rangeNorm;
                    }
                    landscapeSend += pathSample * sendGain
                        * voice.listenerReturnGain;
                }
                const float foghornPerspective =
                    voice.kind == SourceKind::Foghorn
                    ? 0.20f + 0.28f * (1.0f - voice.rangeNorm)
                    : 1.0f;
                const float sample = pathSample * foghornPerspective;
                const float listenerDirectSample = sample * inverseMaster;
                directEnergy += listenerDirectSample * listenerDirectSample;
                voice.energy += (std::abs(sample) - voice.energy) * 0.0025f;
                for (uint32_t channel = 0u; channel < channels; ++channel) {
                    voice.encodingWeights[channel] +=
                        (voice.encodingTargets[channel]
                            - voice.encodingWeights[channel])
                        * smoothingCoefficient_;
                    listenerFrame[channel] += sample * inverseMaster
                        * voice.encodingWeights[channel];
                    if (outputs[channel]) {
                        outputs[channel][frame] += sample
                            * voice.encodingWeights[channel];
                    }
                }
            }
            const auto landscapeReturns = processLandscapeReturn(
                landscapeSend * std::sqrt(std::max(
                    0.0f, smoothedLandscapeAmount_)));
            float returnMagnitude = 0.0f;
            float returnEnergy = 0.0f;
            for (uint32_t tap = 0u; tap < landscapeReturns.size(); ++tap) {
                const float returnSample = landscapeReturns[tap];
                returnMagnitude += std::abs(returnSample);
                const float listenerReturnSample = returnSample
                    * inverseMaster;
                returnEnergy += listenerReturnSample
                    * listenerReturnSample;
                for (uint32_t channel = 0u; channel < channels; ++channel) {
                    landscapeEncodingWeights_[tap][channel] +=
                        (landscapeEncodingTargets_[tap][channel]
                            - landscapeEncodingWeights_[tap][channel])
                        * smoothingCoefficient_;
                    listenerFrame[channel] += returnSample * inverseMaster
                        * landscapeEncodingWeights_[tap][channel];
                    if (outputs[channel]) {
                        outputs[channel][frame] += returnSample
                            * landscapeEncodingWeights_[tap][channel];
                    }
                }
            }
            landscapeReturnEnergy_ +=
                (returnMagnitude - landscapeReturnEnergy_) * 0.0015f;
            fieldListener_.processFrame(listenerFrame.data(), channels);
            listenerDirectEnergy_ +=
                (directEnergy - listenerDirectEnergy_)
                * listenerEnergyCoefficient_;
            listenerReturnEnergy_ +=
                (returnEnergy - listenerReturnEnergy_)
                * listenerEnergyCoefficient_;
            const float returnShareTarget = listenerReturnEnergy_
                / (listenerDirectEnergy_ + listenerReturnEnergy_ + 1.0e-6f);
            listenerReturnShare_ +=
                (returnShareTarget - listenerReturnShare_)
                * listenerEnergyCoefficient_;
        }
        while (renderEntityCount_ > params_.entities
            && std::abs(voices_[renderEntityCount_ - 1u].blockGain) < 1.0e-5f) {
            --renderEntityCount_;
        }
        frameCounter_ += frames;
    }

private:
    enum class SourceKind : uint8_t {
        Flow, Roll, Motor, Modal, Air,
        Traffic, Aircraft, Foghorn, Surf
    };

    struct Voice {
        uint32_t rng = 1u;
        SourceKind kind = SourceKind::Flow;
        AmbiHorizonLayer layer = AmbiHorizonLayer::HorizonBed;
        float phase = 0.0f;
        float phase2 = 0.0f;
        float eventPhase = 0.0f;
        float slowPhase = 0.0f;
        float fastPhase = 0.0f;
        float baseFrequency = 80.0f;
        float brightness = 0.5f;
        float identity = 0.5f;
        float positionJitter = 0.0f;
        float rangeJitter = 0.0f;
        float envelope = 0.0f;
        float targetEnvelope = 0.0f;
        float eventEnvelope = 0.0f;
        float eventTargetEnvelope = 0.0f;
        float eventDecay = 0.99998f;
        float bellEnvelope = 0.0f;
        float bellTargetEnvelope = 0.0f;
        float bellDecay = 0.9999f;
        float pulseGain = 0.0f;
        float low = 0.0f;
        float low2 = 0.0f;
        float localBody = 0.0f;
        float localBody2 = 0.0f;
        float bedBody = 0.0f;
        float bedBody2 = 0.0f;
        float bedFilterAlpha = 0.08f;
        float bedDiffusionAlpha = 0.02f;
        float textureLow = 0.0f;
        float textureBand = 0.0f;
        float hornBody = 0.0f;
        float hornBell = 0.0f;
        float transientBody = 0.0f;
        float transientFilterAlpha = 0.25f;
        float band = 0.0f;
        float modal1 = 0.0f;
        float modal2 = 0.0f;
        float filterAlpha = 0.1f;
        float propagationGain = 1.0f;
        float targetPropagationGain = 1.0f;
        float modalCoefficient = 0.0f;
        float targetModalCoefficient = 0.0f;
        float modalRadiusSquared = 0.0f;
        float targetModalRadiusSquared = 0.0f;
        float modalFrequency = 440.0f;
        float modalRatio = 2.41f;
        float modalExcitationGain = 0.028f;
        float motionOffset = 0.0f;
        float motionPosition = 0.0f;
        float slowAttackCoefficient = 0.0002f;
        float slowReleaseCoefficient = 0.00008f;
        float eventAttackCoefficient = 0.0004f;
        float eventReleaseCoefficient = 0.00008f;
        float bellAttackCoefficient = 0.0004f;
        float bellReleaseCoefficient = 0.00012f;
        float blockGain = 0.0f;
        float targetBlockGain = 0.0f;
        float listenerGain = 1.0f;
        float targetListenerGain = 1.0f;
        float listenerReturnGain = 1.0f;
        float targetListenerReturnGain = 1.0f;
        float listenerDistanceBias = 0.0f;
        float targetListenerDistanceBias = 0.0f;
        uint64_t nextModalStrikeFrame = 0u;
        float azimuthDeg = 0.0f;
        float elevationDeg = 0.0f;
        float rangeNorm = 0.5f;
        float energy = 0.0f;
        std::array<float, kAmbiHorizonMaxChannels> basis {};
        std::array<float, kAmbiHorizonMaxChannels> encodingWeights {};
        std::array<float, kAmbiHorizonMaxChannels> encodingTargets {};
    };

    static float finite(float value, float fallback)
    {
        return std::isfinite(value) ? value : fallback;
    }

    static float finiteUnit(float value, float fallback)
    {
        return std::clamp(finite(value, fallback), 0.0f, 1.0f);
    }

    static uint32_t nextRandom(uint32_t& state)
    {
        state ^= state << 13u;
        state ^= state >> 17u;
        state ^= state << 5u;
        return state;
    }

    static float randomUnit(uint32_t& state)
    {
        return static_cast<float>(nextRandom(state) & 0x00ffffffu)
            / static_cast<float>(0x01000000u);
    }

    static float wrapPhase(float phase)
    {
        phase -= std::floor(phase);
        return phase;
    }

    static float wrapDegrees(float degrees)
    {
        while (degrees > 180.0f) degrees -= 360.0f;
        while (degrees < -180.0f) degrees += 360.0f;
        return degrees;
    }

    void updateScoreParams()
    {
        score_.setParams({
            params_.pace,
            params_.occupancy * params_.activity,
            params_.cascade,
            params_.memory,
            0.35f + 0.55f * params_.memory,
        });
    }

    SourceKind selectSource(uint32_t index) const
    {
        static constexpr SourceKind mixed[] {
            SourceKind::Flow, SourceKind::Roll, SourceKind::Air,
            SourceKind::Flow, SourceKind::Traffic, SourceKind::Modal,
            SourceKind::Motor, SourceKind::Aircraft };
        static constexpr SourceKind rural[] {
            SourceKind::Air, SourceKind::Flow, SourceKind::Roll,
            SourceKind::Air, SourceKind::Modal, SourceKind::Motor,
            SourceKind::Modal, SourceKind::Aircraft };
        static constexpr SourceKind traffic[] {
            SourceKind::Roll, SourceKind::Air, SourceKind::Flow,
            SourceKind::Roll, SourceKind::Traffic, SourceKind::Traffic,
            SourceKind::Motor, SourceKind::Aircraft };
        static constexpr SourceKind city[] {
            SourceKind::Air, SourceKind::Flow, SourceKind::Roll,
            SourceKind::Air, SourceKind::Traffic, SourceKind::Motor,
            SourceKind::Aircraft, SourceKind::Flow };
        static constexpr SourceKind industrial[] {
            SourceKind::Roll, SourceKind::Air, SourceKind::Flow,
            SourceKind::Roll, SourceKind::Motor, SourceKind::Flow,
            SourceKind::Modal, SourceKind::Traffic };
        static constexpr SourceKind water[] {
            SourceKind::Flow, SourceKind::Roll, SourceKind::Air,
            SourceKind::Flow, SourceKind::Surf, SourceKind::Foghorn,
            SourceKind::Surf, SourceKind::Foghorn };
        static constexpr SourceKind weather[] {
            SourceKind::Flow, SourceKind::Air, SourceKind::Roll,
            SourceKind::Flow, SourceKind::Motor, SourceKind::Air,
            SourceKind::Surf, SourceKind::Aircraft };
        static constexpr SourceKind airport[] {
            SourceKind::Flow, SourceKind::Air, SourceKind::Roll,
            SourceKind::Flow, SourceKind::Aircraft, SourceKind::Aircraft,
            SourceKind::Traffic, SourceKind::Aircraft };
        static constexpr SourceKind coast[] {
            SourceKind::Flow, SourceKind::Air, SourceKind::Roll,
            SourceKind::Flow, SourceKind::Surf, SourceKind::Foghorn,
            SourceKind::Surf, SourceKind::Foghorn };
        const SourceKind* set = mixed;
        switch (params_.ecology) {
        case AmbiHorizonEcology::Rural: set = rural; break;
        case AmbiHorizonEcology::Traffic: set = traffic; break;
        case AmbiHorizonEcology::City: set = city; break;
        case AmbiHorizonEcology::Industrial: set = industrial; break;
        case AmbiHorizonEcology::Water: set = water; break;
        case AmbiHorizonEcology::Weather: set = weather; break;
        case AmbiHorizonEcology::Airport: set = airport; break;
        case AmbiHorizonEcology::Coast: set = coast; break;
        default: break;
        }
        return set[index % 8u];
    }

    void rebuildVoices()
    {
        uint32_t sceneRng = params_.seed ? params_.seed : 1u;
        const bool bellScene = params_.ecology == AmbiHorizonEcology::Rural
            && params_.seed == 311u;
        for (uint32_t index = 0u; index < kAmbiHorizonMaxEntities; ++index) {
            auto& voice = voices_[index];
            voice = {};
            voice.rng = nextRandom(sceneRng) ^ ((index + 1u) * 0x9e3779b9u);
            if (voice.rng == 0u) voice.rng = index + 1u;
            voice.kind = selectSource(index);
            voice.identity = randomUnit(voice.rng);
            voice.brightness = randomUnit(voice.rng);
            voice.positionJitter = randomUnit(voice.rng) * 2.0f - 1.0f;
            voice.rangeJitter = randomUnit(voice.rng) * 2.0f - 1.0f;
            const float pitchBias = static_cast<float>(params_.ecology == AmbiHorizonEcology::Rural
                || params_.ecology == AmbiHorizonEcology::Water);
            voice.baseFrequency = 32.0f * std::pow(2.0f,
                voice.identity * (2.4f + pitchBias * 1.8f));
            voice.phase = randomUnit(voice.rng);
            voice.phase2 = randomUnit(voice.rng);
            voice.eventPhase = randomUnit(voice.rng);
            voice.slowPhase = randomUnit(voice.rng);
            voice.fastPhase = randomUnit(voice.rng);
            voice.motionOffset = randomUnit(voice.rng);
            const double firstStrikeSeconds = bellScene
                ? 0.10 + 0.12 * static_cast<double>(index % 4u)
                : 1.5 + 5.0 * static_cast<double>(voice.identity);
            voice.nextModalStrikeFrame = frameCounter_
                + static_cast<uint64_t>(firstStrikeSeconds * sampleRate_);
        }
        renderEntityCount_ = params_.entities;
        updateScoreParams();
        updateControl(64u);
    }

    void updateControl(uint32_t frames)
    {
        const float dt = static_cast<float>(frames / sampleRate_);
        updateScoreParams();
        score_.update(dt, params_.entities);

        const uint32_t localCount = std::min<uint32_t>(4u, params_.entities);
        const uint32_t remaining = params_.entities - localCount;
        const uint32_t signalCount = std::min<uint32_t>(remaining,
            std::max<uint32_t>(1u, static_cast<uint32_t>(
                std::lround((0.08f + 0.34f * params_.signals) * remaining))));
        const float logRange = std::log(params_.rangeKm / 0.03f)
            / std::log(20.0f / 0.03f);
        const float elapsed = static_cast<float>(frameCounter_ / sampleRate_);
        const uint32_t channels = activeChannels();
        const float detailBase = 0.28f + 0.72f * params_.detail;
        const float reflectedToDirectDb = 10.0f * std::log10(
            (listenerReturnEnergy_ + 1.0e-10f)
                / (listenerDirectEnergy_ + 1.0e-10f));

        for (uint32_t index = 0u; index < params_.entities; ++index) {
            auto& voice = voices_[index];
            const bool local = index < localCount;
            const uint32_t horizonIndex = index - std::min(index, localCount);
            const bool signal = !local && horizonIndex < signalCount;
            voice.layer = local ? AmbiHorizonLayer::LocalFloor
                : (signal ? AmbiHorizonLayer::HorizonSignal
                          : AmbiHorizonLayer::HorizonBed);
            if (voice.kind == SourceKind::Modal
                && voice.layer == AmbiHorizonLayer::HorizonSignal
                && frameCounter_ + frames >= voice.nextModalStrikeFrame) {
                const bool bellScene = params_.ecology == AmbiHorizonEcology::Rural
                    && params_.seed == 311u;
                voice.bellTargetEnvelope = std::max(
                    voice.bellTargetEnvelope,
                    bellScene ? 1.0f : 0.38f + 0.32f * params_.activity);
                voice.eventTargetEnvelope = std::max(
                    voice.eventTargetEnvelope,
                    bellScene ? 1.0f : 0.55f);
                const double intervalSeconds = bellScene
                    ? 3.2 + 3.8 * static_cast<double>(voice.identity)
                    : 5.0 + 13.0 * static_cast<double>(voice.identity)
                        - 2.0 * static_cast<double>(params_.pace);
                voice.nextModalStrikeFrame = frameCounter_
                    + static_cast<uint64_t>(std::max(1.5, intervalSeconds)
                        * sampleRate_);
            }

            const float spreadIndex = remaining > 1u
                ? static_cast<float>(horizonIndex) / static_cast<float>(remaining - 1u) - 0.5f
                : 0.0f;
            const float localAngle = 360.0f * static_cast<float>(index)
                / static_cast<float>(std::max<uint32_t>(1u, localCount));
            const float slowDrift = std::sin(
                2.0f * kPi * (0.0015f + voice.identity * 0.0025f) * elapsed
                + voice.identity * 6.0f) * params_.turbulence;
            voice.motionPosition = 0.0f;
            voice.azimuthDeg = wrapDegrees(local
                ? localAngle + params_.azimuthDeg * 0.2f
                : params_.azimuthDeg + spreadIndex * params_.arcDeg
                    + voice.positionJitter * (4.0f + 16.0f * params_.terrain)
                    + slowDrift * 7.0f);
            voice.elevationDeg = std::clamp(local
                ? -12.0f + 18.0f * voice.identity
                : params_.elevationDeg
                    + voice.positionJitter * (2.0f + 5.0f * params_.terrain),
                -35.0f, 35.0f);
            voice.rangeNorm = local
                ? 0.02f + 0.10f * voice.identity
                : std::clamp(logRange + voice.rangeJitter * 0.08f, 0.0f, 1.0f);
            if (!local && voice.kind == SourceKind::Aircraft) {
                // A flight path is not a static point on the horizon. Each
                // aircraft traverses the scene over roughly one to three
                // minutes, rises in elevation near closest approach, and
                // recedes again. The shared range remains the distance scale.
                const float flight = smoothedAircraftFlight_;
                const float airborne = flight * flight * (3.0f - 2.0f * flight);
                const float flightSeconds = (235.0f - 155.0f
                    * smoothedAircraftSpeed_) * (0.82f + 0.52f * voice.identity)
                    * (1.45f - 0.45f * airborne);
                const float flightPhase = wrapPhase(voice.motionOffset
                    + elapsed / flightSeconds);
                voice.motionPosition = flightPhase * 2.0f - 1.0f;
                const float closest = std::max(0.0f,
                    1.0f - voice.motionPosition * voice.motionPosition);
                const float airborneArc = std::clamp(
                    std::max(72.0f, params_.arcDeg * 0.52f), 72.0f, 168.0f);
                const float flightArc = 18.0f
                    + airborne * (airborneArc - 18.0f);
                voice.azimuthDeg = wrapDegrees(params_.azimuthDeg
                    + voice.motionPosition * flightArc * 0.5f
                    + voice.positionJitter * 5.0f);
                const float groundElevation = -2.0f + 3.0f * voice.identity;
                const float airborneElevation = params_.elevationDeg
                    + 7.0f + closest * (38.0f + 20.0f * voice.brightness);
                voice.elevationDeg = std::clamp(groundElevation
                    + airborne * (airborneElevation - groundElevation),
                    -10.0f, 72.0f);
                voice.rangeNorm = std::clamp(logRange
                    + std::abs(voice.motionPosition)
                        * (0.035f + 0.065f * airborne)
                    - closest * 0.12f * airborne
                    + voice.rangeJitter * 0.04f,
                    0.0f, 1.0f);
            }

            // Listener Mode reads the completed pre-output HOA field through
            // eight virtual directional pickups. Its Horizon response is
            // deliberately bounded and cue-oriented: direction preference
            // changes audibility, novelty can reveal a temporal glimpse,
            // habituation can settle an exposed region, and Imprint maps the
            // heard spectral/direct-return relationship onto apparent range.
            // This follows the perceptual roles of direct/reverberant energy,
            // high-frequency loss, and spatio-temporal soundscape patterning
            // without pretending to solve an outdoor propagation model.
            voice.targetListenerGain = 1.0f;
            voice.targetListenerReturnGain = 1.0f;
            voice.targetListenerDistanceBias = 0.0f;
            if (params_.fieldListenMode != AmbiFieldListenMode::Off) {
                const Vec3 direction = directionFromAed(
                    voice.azimuthDeg, voice.elevationDeg);
                const float activity = fieldListener_.activity();
                const float amount = params_.fieldListenAmount * activity;
                const float preference = fieldListener_.preference(
                    direction, params_.fieldListenMode);
                const auto heard = fieldListener_.score(
                    direction, params_.fieldListenMode);
                const float directional = (preference - 0.5f) * 2.0f;
                switch (params_.fieldListenResponse) {
                case AmbiFieldListenerResponse::Excite: {
                    const float glimpse = amount
                        * (0.55f + 0.45f * preference)
                        * (heard.novelty * 0.58f
                            + heard.charge * 0.30f
                            + heard.roughness * 0.12f);
                    voice.targetListenerGain = 1.0f + glimpse * 0.16f;
                    voice.targetListenerReturnGain = 1.0f + glimpse * 0.05f;
                    voice.targetListenerDistanceBias = -glimpse * 0.045f;
                    break;
                }
                case AmbiFieldListenerResponse::Settle: {
                    const float settling = amount
                        * (0.45f + 0.55f * preference)
                        * heard.habituation;
                    voice.targetListenerGain = 1.0f - settling * 0.18f;
                    voice.targetListenerReturnGain = 1.0f + settling * 0.14f;
                    voice.targetListenerDistanceBias = settling * 0.055f;
                    break;
                }
                case AmbiFieldListenerResponse::Imprint: {
                    const float distanceEvidence = std::clamp(
                        (reflectedToDirectDb + 27.0f) / 18.0f
                            - heard.spectralTilt * 0.25f
                            + (heard.habituation - 0.5f) * 0.12f,
                        -1.0f, 1.0f) * amount
                        * (0.55f + 0.45f * preference);
                    voice.targetListenerGain = 1.0f
                        - distanceEvidence * 0.08f;
                    voice.targetListenerReturnGain = 1.0f
                        + distanceEvidence * 0.16f;
                    voice.targetListenerDistanceBias =
                        distanceEvidence * 0.10f;
                    break;
                }
                default:
                    voice.targetListenerGain = 1.0f
                        + directional * amount * 0.12f;
                    voice.targetListenerReturnGain = 1.0f
                        - directional * amount * 0.05f;
                    voice.targetListenerDistanceBias =
                        -directional * amount * 0.035f;
                    break;
                }
            }
            voice.targetListenerGain = std::clamp(
                voice.targetListenerGain, 0.78f, 1.18f);
            voice.targetListenerReturnGain = std::clamp(
                voice.targetListenerReturnGain, 0.84f, 1.18f);
            voice.targetListenerDistanceBias = std::clamp(
                voice.targetListenerDistanceBias, -0.10f, 0.10f);
            const float perceptualRange = std::clamp(
                voice.rangeNorm + voice.listenerDistanceBias, 0.0f, 1.0f);
            voice.basis = acnSn3dBasis7(directionFromAed(
                voice.azimuthDeg, voice.elevationDeg));
            const float distanceBase = std::max(
                0.12f, 1.0f - 0.67f * perceptualRange);
            float distanceWeight = 1.0f;
            float detailWeight = 1.0f;
            for (uint32_t degree = 0u; degree <= params_.order; ++degree) {
                if (degree > 0u) {
                    distanceWeight *= distanceBase;
                    detailWeight *= detailBase;
                }
                const float degreeWeight = distanceWeight * detailWeight;
                const uint32_t first = degree * degree;
                const uint32_t end = std::min<uint32_t>(
                    (degree + 1u) * (degree + 1u), channels);
                for (uint32_t channel = first; channel < end; ++channel) {
                    voice.encodingTargets[channel] = voice.basis[channel]
                        * degreeWeight;
                }
            }
            score_.setEntityPosition(index,
                std::cos(voice.azimuthDeg * kPi / 180.0f) * voice.rangeNorm,
                std::sin(voice.azimuthDeg * kPi / 180.0f) * voice.rangeNorm,
                voice.elevationDeg / 35.0f);

            const auto directive = score_.directive(index);
            if (directive.onset || directive.cascadeArrival) {
                const float eventLevel = 0.35f + 0.65f * std::max(
                    directive.drive, directive.propagation);
                voice.eventTargetEnvelope = std::max(
                    voice.eventTargetEnvelope, eventLevel);
            }
            if (local) {
                voice.targetEnvelope = 0.24f + 0.26f * params_.activity;
            } else if (signal) {
                voice.targetEnvelope = 0.03f + directive.activity
                    * (0.25f + 0.75f * params_.activity);
            } else {
                voice.targetEnvelope = 0.16f + 0.34f * params_.occupancy
                    + directive.aftermath * 0.16f;
            }

            const float terrainLoss = params_.terrain * (local ? 0.1f : perceptualRange);
            const float airLoss = params_.air * (local ? 0.04f : perceptualRange);
            float groundLoss = 0.0f;
            switch (params_.ground) {
            case AmbiHorizonGround::Water: groundLoss = -0.10f; break;
            case AmbiHorizonGround::Hard: groundLoss = -0.03f; break;
            case AmbiHorizonGround::Grass: groundLoss = 0.16f; break;
            case AmbiHorizonGround::Forest: groundLoss = 0.28f; break;
            default: groundLoss = 0.08f; break;
            }
            float cutoff = std::clamp(15500.0f * std::pow(0.045f,
                std::max(0.0f, airLoss + terrainLoss * 0.72f + groundLoss * perceptualRange)),
                180.0f, 18000.0f);
            if (voice.kind == SourceKind::Foghorn) {
                cutoff = std::min(cutoff,
                    950.0f + 1100.0f * (1.0f - perceptualRange)
                        + 500.0f * params_.detail);
            }
            voice.filterAlpha = 1.0f - std::exp(
                -2.0f * kPi * cutoff / static_cast<float>(sampleRate_));
            const bool ecologicalBody = voice.kind == SourceKind::Traffic
                || voice.kind == SourceKind::Aircraft
                || voice.kind == SourceKind::Foghorn
                || voice.kind == SourceKind::Surf;
            const float bedCutoff = 280.0f + 920.0f * params_.detail
                + (ecologicalBody ? 480.0f : 0.0f)
                + 260.0f * (1.0f - perceptualRange);
            voice.bedFilterAlpha = 1.0f - std::exp(
                -2.0f * kPi * bedCutoff / static_cast<float>(sampleRate_));
            const float bedDiffusionCutoff = 90.0f
                + 240.0f * params_.detail
                + 100.0f * (1.0f - perceptualRange);
            voice.bedDiffusionAlpha = 1.0f - std::exp(
                -2.0f * kPi * bedDiffusionCutoff
                    / static_cast<float>(sampleRate_));
            const float d = perceptualRange;
            const float geometric = 1.0f / (1.0f + 4.5f * d * d);
            const float carryGain = std::pow(
                2.0f, params_.carry * (0.35f + 1.4f * d));
            float groundCarry = 1.0f;
            if (params_.ground == AmbiHorizonGround::Water) {
                groundCarry += 0.28f * d;
            } else if (params_.ground == AmbiHorizonGround::Hard) {
                groundCarry += 0.12f * d;
            } else if (params_.ground == AmbiHorizonGround::Forest) {
                groundCarry -= 0.24f * d;
            }
            voice.targetPropagationGain = geometric * carryGain * groundCarry;
            const bool bellScene = params_.ecology == AmbiHorizonEcology::Rural
                && params_.seed == 311u;
            voice.modalFrequency = bellScene
                ? 420.0f + 760.0f * voice.identity
                : std::min(4800.0f,
                    voice.baseFrequency * (3.0f + voice.brightness * 13.0f));
            voice.modalFrequency *= std::pow(2.0f,
                (smoothedBellPitch_ - 0.5f) * 2.0f);
            voice.modalRatio = bellScene
                ? 2.36f + 0.24f * voice.brightness
                : 2.08f + 0.42f * voice.brightness;
            const float modalRadius = 0.996f + params_.memory * 0.0032f;
            voice.targetModalCoefficient = 2.0f * modalRadius * std::cos(
                2.0f * kPi * voice.modalFrequency
                    / static_cast<float>(sampleRate_));
            voice.targetModalRadiusSquared = modalRadius * modalRadius;
            const float decaySeconds = bellScene
                ? 2.2f + params_.memory * 4.8f
                : 0.8f + params_.memory * 2.0f;
            voice.bellDecay = std::exp(-1.0f
                / (decaySeconds * (0.35f + 1.30f * smoothedBellDecay_)
                    * static_cast<float>(sampleRate_)));
            voice.modalExcitationGain = bellScene ? 0.012f : 0.028f;
            // A horizon event must arrive as a swell, not as a control-block
            // gain step. Greater range lengthens the attack and removes more
            // upper partial energy before the propagation stage.
            const float distanceSoftness = local ? 0.0f : perceptualRange;
            const float eventAttackSeconds = 0.030f
                + 0.105f * distanceSoftness
                + 0.030f * (1.0f - params_.detail);
            const float eventReleaseSeconds = 0.24f
                + 0.65f * distanceSoftness + 0.45f * params_.memory;
            voice.eventAttackCoefficient = 1.0f - std::exp(-1.0f
                / (eventAttackSeconds * static_cast<float>(sampleRate_)));
            voice.eventReleaseCoefficient = 1.0f - std::exp(-1.0f
                / (eventReleaseSeconds * static_cast<float>(sampleRate_)));
            const float eventDecaySeconds = 0.32f + 0.65f * params_.memory
                + 0.55f * distanceSoftness;
            voice.eventDecay = std::exp(-1.0f
                / (eventDecaySeconds * static_cast<float>(sampleRate_)));
            const float bellAttackSeconds = 0.020f
                + 0.090f * distanceSoftness
                + 0.025f * (1.0f - params_.detail);
            const float bellReleaseSeconds = 0.16f
                + 0.44f * distanceSoftness;
            voice.bellAttackCoefficient = 1.0f - std::exp(-1.0f
                / (bellAttackSeconds * static_cast<float>(sampleRate_)));
            voice.bellReleaseCoefficient = 1.0f - std::exp(-1.0f
                / (bellReleaseSeconds * static_cast<float>(sampleRate_)));
            const float transientCutoff = voice.kind == SourceKind::Modal
                ? 950.0f + 2600.0f * (1.0f - distanceSoftness)
                    + 600.0f * params_.detail
                : 1800.0f + 4200.0f * (1.0f - distanceSoftness)
                    + 800.0f * params_.detail;
            voice.transientFilterAlpha = 1.0f - std::exp(
                -2.0f * kPi * transientCutoff
                    / static_cast<float>(sampleRate_));
            const float slowAttackSeconds = voice.kind == SourceKind::Foghorn
                ? 0.08f + 0.30f * (1.0f - smoothedFoghornPressure_)
                : 0.45f + 0.75f * voice.identity;
            const float slowReleaseSeconds = voice.kind == SourceKind::Foghorn
                ? 0.38f + 0.90f * (1.0f - smoothedFoghornPressure_)
                : 1.6f + 1.8f * params_.memory;
            voice.slowAttackCoefficient = 1.0f - std::exp(-1.0f
                / (slowAttackSeconds * static_cast<float>(sampleRate_)));
            voice.slowReleaseCoefficient = 1.0f - std::exp(-1.0f
                / (slowReleaseSeconds * static_cast<float>(sampleRate_)));
        }
        updateLandscapeTargets(logRange, channels);
    }

    float renderVoice(Voice& voice)
    {
        const float noise = randomUnit(voice.rng) * 2.0f - 1.0f;
        const float attack = voice.layer == AmbiHorizonLayer::HorizonSignal
            ? 0.0022f : 0.00035f;
        const float release = voice.layer == AmbiHorizonLayer::HorizonSignal
            ? 0.00018f : 0.00008f;
        voice.envelope += (voice.targetEnvelope - voice.envelope)
            * (voice.targetEnvelope > voice.envelope ? attack : release);
        voice.eventTargetEnvelope *= voice.eventDecay;
        voice.eventEnvelope +=
            (voice.eventTargetEnvelope - voice.eventEnvelope)
            * (voice.eventTargetEnvelope > voice.eventEnvelope
                ? voice.eventAttackCoefficient
                : voice.eventReleaseCoefficient);
        voice.bellTargetEnvelope *= voice.bellDecay;
        voice.bellEnvelope +=
            (voice.bellTargetEnvelope - voice.bellEnvelope)
            * (voice.bellTargetEnvelope > voice.bellEnvelope
                ? voice.bellAttackCoefficient
                : voice.bellReleaseCoefficient);

        const float slowRate = 0.012f + voice.identity * 0.045f;
        const float fastRate = 0.23f + voice.brightness * 1.8f;
        voice.slowPhase = wrapPhase(voice.slowPhase
            + slowRate / static_cast<float>(sampleRate_));
        voice.fastPhase = wrapPhase(voice.fastPhase
            + fastRate / static_cast<float>(sampleRate_));
        const float slow = 0.5f + 0.5f * std::sin(2.0f * kPi * voice.slowPhase);
        const float fast = 0.5f + 0.5f * std::sin(2.0f * kPi * voice.fastPhase);
        const float turbulenceGain = 1.0f - params_.turbulence
            * (0.22f * slow + 0.09f * fast);

        voice.low += (noise - voice.low) * (0.002f + voice.brightness * 0.008f);
        voice.low2 += (voice.low - voice.low2) * 0.0015f;
        voice.textureLow += (noise - voice.textureLow)
            * (0.018f + voice.brightness * 0.040f);
        voice.textureBand += (voice.textureLow - voice.textureBand)
            * (0.0015f + voice.identity * 0.0040f);
        const float high = noise - voice.low;
        float sample = 0.0f;
        switch (voice.kind) {
        case SourceKind::Flow:
            sample = voice.low * 0.72f + high * (0.08f + voice.brightness * 0.15f);
            break;
        case SourceKind::Roll:
            sample = voice.low2 * 1.4f + (voice.low - voice.low2) * 0.38f;
            break;
        case SourceKind::Motor: {
            const float toneScale = std::pow(2.0f,
                (smoothedMachineTone_ - 0.5f) * 1.25f);
            const float rate = voice.baseFrequency * toneScale
                * (1.0f + 0.018f * std::sin(2.0f * kPi * voice.slowPhase));
            voice.phase = wrapPhase(voice.phase + rate / static_cast<float>(sampleRate_));
            voice.phase2 = wrapPhase(voice.phase2 + rate * (1.98f + 0.04f * voice.identity)
                / static_cast<float>(sampleRate_));
            sample = std::sin(2.0f * kPi * voice.phase)
                    * (0.60f - 0.16f * smoothedMachineTone_)
                + std::sin(2.0f * kPi * voice.phase2)
                    * (0.10f + 0.24f * smoothedMachineTone_)
                + voice.low * 0.18f;
            break;
        }
        case SourceKind::Modal: {
            voice.phase = wrapPhase(voice.phase + voice.modalFrequency
                / static_cast<float>(sampleRate_));
            voice.phase2 = wrapPhase(voice.phase2
                + voice.modalFrequency * voice.modalRatio
                    / static_cast<float>(sampleRate_));
            const float bell = (0.72f * std::sin(2.0f * kPi * voice.phase)
                + 0.28f * std::sin(2.0f * kPi * voice.phase2))
                * voice.bellEnvelope;
            const float excitation = noise * voice.eventEnvelope
                * voice.modalExcitationGain;
            const float modal = excitation
                + voice.modalCoefficient * voice.modal1
                - voice.modalRadiusSquared * voice.modal2;
            voice.modal2 = voice.modal1;
            voice.modal1 = flushDenormal(modal);
            sample = modal * 0.28f + bell * 0.74f + voice.low * 0.08f;
            break;
        }
        case SourceKind::Air:
            voice.band += ((high * 0.6f) - voice.band)
                * (0.01f + voice.brightness * 0.025f);
            sample = voice.band * 0.72f + voice.low * 0.20f;
            break;
        case SourceKind::Traffic: {
            // An aggregate road voice combines a slowly passing vehicle
            // window, low combustion orders, and correlated tyre/road body.
            // Multiple detuned entities create the highway band; no single
            // sawtooth or static oscillator is asked to imply an engine.
            const float passRate = (0.010f + 0.024f * voice.identity
                + 0.014f * params_.pace)
                * (0.45f + 1.35f * smoothedTrafficSpeed_);
            voice.eventPhase = wrapPhase(voice.eventPhase + passRate
                / static_cast<float>(sampleRate_));
            const float passage = 0.30f + 0.70f * std::pow(
                0.5f + 0.5f * std::sin(2.0f * kPi * voice.eventPhase), 2.0f);
            const float firingRate = (20.0f + 50.0f * voice.identity)
                * (0.68f + 0.78f * smoothedEngineLoad_)
                * (0.92f + 0.16f * slow);
            voice.phase = wrapPhase(voice.phase + firingRate
                / static_cast<float>(sampleRate_));
            voice.phase2 = wrapPhase(voice.phase2 + firingRate
                * (1.43f + 0.31f * voice.brightness)
                / static_cast<float>(sampleRate_));
            const float crank = 2.0f * kPi * voice.phase;
            const float combustion = 0.54f * std::sin(crank)
                + 0.22f * std::sin(crank * 2.0f)
                + 0.09f * std::sin(crank * 3.0f)
                + 0.16f * std::sin(2.0f * kPi * voice.phase2);
            const float roadBody = voice.textureLow * 0.58f
                + (voice.textureLow - voice.textureBand) * 0.72f
                + voice.low2 * 0.24f;
            const float combustionMix = 0.16f + 0.52f * smoothedEngineLoad_;
            const float roadMix = 0.92f - 0.38f * smoothedEngineLoad_;
            sample = (combustion * combustionMix + roadBody * roadMix)
                * passage;
            break;
        }
        case SourceKind::Aircraft: {
            // Low turbine roar plus weak spool/blade orders. The matching
            // flight-path position supplies a restrained Doppler shift and
            // closest-approach gain without turning the pass into a siren.
            const float closest = std::max(0.0f,
                1.0f - voice.motionPosition * voice.motionPosition);
            const float flight = smoothedAircraftFlight_;
            const float doppler = 1.0f
                - (0.006f + 0.082f * flight) * voice.motionPosition;
            const float spoolRate = (30.0f + 58.0f * voice.identity)
                * (0.62f + 0.88f * smoothedAircraftPower_) * doppler;
            voice.phase = wrapPhase(voice.phase + spoolRate
                / static_cast<float>(sampleRate_));
            voice.phase2 = wrapPhase(voice.phase2 + spoolRate
                * (4.8f + 3.4f * voice.brightness)
                / static_cast<float>(sampleRate_));
            const float turbineOrders = (0.14f + 0.34f * smoothedAircraftTone_)
                    * std::sin(2.0f * kPi * voice.phase)
                + (0.025f + 0.11f * smoothedAircraftTone_)
                    * std::sin(2.0f * kPi * voice.phase2);
            const float roar = voice.textureBand * 0.56f
                + (voice.textureLow - voice.textureBand) * 1.08f
                + voice.low2 * 0.32f;
            const float passGain = (0.76f - 0.08f * smoothedAircraftPower_)
                + flight * ((0.34f + 0.66f * closest)
                    - (0.76f - 0.08f * smoothedAircraftPower_));
            sample = (roar * (0.58f + 0.50f * smoothedAircraftPower_)
                    + turbineOrders)
                * passGain;
            break;
        }
        case SourceKind::Foghorn: {
            const float cycleRate = 0.016f + 0.018f * voice.identity
                + 0.004f * params_.pace;
            voice.eventPhase = wrapPhase(voice.eventPhase + cycleRate
                / static_cast<float>(sampleRate_));
            const float duty = 0.055f + 0.18f * smoothedFoghornLength_;
            const float target = voice.eventPhase < duty ? 1.0f : 0.0f;
            voice.pulseGain += (target - voice.pulseGain)
                * (target > voice.pulseGain
                    ? voice.slowAttackCoefficient
                    : voice.slowReleaseCoefficient);
            const float hornRate = 55.0f * std::pow(2.0f,
                smoothedFoghornPitch_ * 1.80f)
                * (0.90f + 0.20f * voice.identity);
            const float pressurePitch = 0.74f + 0.26f
                * std::sqrt(std::max(0.0f, voice.pulseGain));
            voice.phase = wrapPhase(voice.phase + hornRate * pressurePitch
                * (0.996f + 0.008f * slow)
                / static_cast<float>(sampleRate_));
            voice.phase2 = wrapPhase(voice.phase2 + hornRate
                * pressurePitch * (1.006f + 0.010f * voice.brightness)
                / static_cast<float>(sampleRate_));
            const float hornPhase = 2.0f * kPi * voice.phase;
            const float pistonDrive = std::sin(hornPhase)
                + 0.31f * std::sin(hornPhase * 2.0f)
                + 0.12f * std::sin(hornPhase * 3.0f)
                + 0.10f * std::sin(2.0f * kPi * voice.phase2);
            const float piston = std::tanh(pistonDrive
                * (1.35f + 4.65f * smoothedFoghornPressure_));
            const float bodyAlpha = 0.010f
                + 0.040f * smoothedFoghornPressure_;
            const float bellAlpha = 0.004f
                + 0.012f * smoothedFoghornPitch_;
            voice.hornBody += (piston - voice.hornBody) * bodyAlpha;
            voice.hornBell += (voice.hornBody - voice.hornBell) * bellAlpha;
            const float airColumn = voice.hornBody * 0.72f
                + voice.hornBell * 0.54f;
            const float speakingAir = voice.textureBand
                * (0.025f + 0.065f * smoothedFoghornPressure_);
            sample = (airColumn + speakingAir) * voice.pulseGain * 1.32f;
            break;
        }
        case SourceKind::Surf: {
            const float waveRate = (0.035f + 0.060f * voice.identity
                + 0.014f * params_.pace)
                * (0.55f + 1.20f * smoothedWaveRate_);
            voice.eventPhase = wrapPhase(voice.eventPhase + waveRate
                / static_cast<float>(sampleRate_));
            const float wave = 0.5f + 0.5f
                * std::sin(2.0f * kPi * voice.eventPhase);
            const float crestTarget = wave * wave * (0.62f + 0.38f * wave);
            voice.pulseGain += (crestTarget - voice.pulseGain)
                * (crestTarget > voice.pulseGain
                    ? voice.slowAttackCoefficient
                    : voice.slowReleaseCoefficient);
            const float wash = voice.textureBand
                    * (0.30f + 0.42f * smoothedWaveBreak_)
                + (voice.textureLow - voice.textureBand)
                    * (0.48f + 0.62f * smoothedWaveBreak_);
            const float undertow = voice.low2
                * (0.62f - 0.26f * smoothedWaveBreak_ + 0.16f * slow);
            sample = undertow + wash
                * (0.10f + (0.52f + 0.38f * smoothedWaveBreak_)
                    * voice.pulseGain);
            break;
        }
        }
        if (voice.kind == SourceKind::Motor
            || voice.kind == SourceKind::Modal) {
            voice.transientBody += (sample - voice.transientBody)
                * voice.transientFilterAlpha;
            sample = voice.transientBody;
        }
        if (voice.layer == AmbiHorizonLayer::HorizonBed) {
            // BED is a distant ecology body, not a broadband noise floor.
            // Generic air/flow voices surrender their raw high component;
            // named generators retain their identity but are diffused into a
            // correlated low/mid field before outdoor propagation.
            float bedInput = sample;
            if (voice.kind == SourceKind::Flow) {
                bedInput = voice.low2 * 1.18f
                    + (voice.low - voice.low2) * 0.16f;
            } else if (voice.kind == SourceKind::Roll) {
                bedInput = voice.low2 * 1.26f
                    + (voice.low - voice.low2) * 0.20f;
            } else if (voice.kind == SourceKind::Air) {
                bedInput = voice.low2 * 0.90f
                    + voice.textureBand * 0.12f;
            }
            voice.bedBody += (bedInput - voice.bedBody)
                * voice.bedFilterAlpha;
            voice.bedBody2 += (voice.bedBody - voice.bedBody2)
                * voice.bedDiffusionAlpha;
            sample = voice.bedBody * 0.68f + voice.bedBody2 * 0.42f;
        }
        if (voice.layer == AmbiHorizonLayer::LocalFloor) {
            // The local layer is environmental presence, not a simulated
            // recorder noise floor. Remove the raw broadband component and
            // retain only slowly moving, correlated body.
            const float bodyAlpha = 0.014f + 0.034f * voice.brightness;
            const float floorAlpha = 0.003f + 0.008f * voice.identity;
            voice.localBody += (sample - voice.localBody) * bodyAlpha;
            voice.localBody2 += (voice.localBody - voice.localBody2)
                * floorAlpha;
            sample = voice.localBody * 0.58f + voice.localBody2 * 0.42f;
        }
        const bool sustained = voice.kind == SourceKind::Motor
            || voice.kind == SourceKind::Traffic
            || voice.kind == SourceKind::Aircraft
            || voice.kind == SourceKind::Surf;
        const float eventLift = voice.layer == AmbiHorizonLayer::HorizonSignal
            ? (voice.kind == SourceKind::Foghorn ? 1.0f
                : (sustained ? 0.78f : 0.38f + voice.eventEnvelope))
            : 0.75f;
        return softSat(sample * voice.envelope * eventLift * turbulenceGain * 1.8f);
    }

    void updateLandscapeTargets(float rangeNorm, uint32_t channels)
    {
        static constexpr std::array<float, 4u> kLandDelaySeconds {
            0.11f, 0.23f, 0.43f, 0.71f
        };
        static constexpr std::array<float, 4u> kWaterDelaySeconds {
            0.16f, 0.32f, 0.58f, 0.94f
        };
        static constexpr std::array<float, 4u> kAzimuthFractions {
            -0.46f, -0.17f, 0.19f, 0.47f
        };
        const bool water = params_.ground == AmbiHorizonGround::Water
            || params_.ecology == AmbiHorizonEcology::Water
            || params_.ecology == AmbiHorizonEcology::Coast;
        const auto& delaySeconds = water
            ? kWaterDelaySeconds : kLandDelaySeconds;
        const float topologyScale = 0.76f + 0.76f * rangeNorm
            + 0.30f * params_.terrain;
        const float maximumDelay = landscapeDelayBuffer_.size() > 2u
            ? static_cast<float>(landscapeDelayBuffer_.size() - 2u)
            : 1.0f;
        for (uint32_t tap = 0u; tap < delaySeconds.size(); ++tap) {
            targetLandscapeDelaySamples_[tap] = std::clamp(
                delaySeconds[tap] * topologyScale
                    * static_cast<float>(sampleRate_),
                1.0f, maximumDelay);
        }

        targetLandscapeFeedback_ = std::clamp(
            0.24f + 0.25f * params_.memory
                + (water ? 0.08f : 0.0f) + 0.06f * params_.terrain,
            0.22f, 0.66f);
        const float inputCutoff = (water ? 1250.0f : 1550.0f)
            + 900.0f * params_.detail
            - 420.0f * params_.air;
        const float dampingCutoff = (water ? 780.0f : 980.0f)
            + 700.0f * params_.detail
            - 320.0f * params_.terrain;
        targetLandscapeInputAlpha_ = 1.0f - std::exp(
            -2.0f * kPi * std::max(320.0f, inputCutoff)
                / static_cast<float>(sampleRate_));
        targetLandscapeDampingAlpha_ = 1.0f - std::exp(
            -2.0f * kPi * std::max(240.0f, dampingCutoff)
                / static_cast<float>(sampleRate_));
        targetLandscapeReturnGain_ = (water ? 0.34f : 0.29f)
            * (0.82f + 0.24f * params_.terrain + 0.12f * params_.carry);

        const float reflectionArc = std::clamp(params_.arcDeg, 90.0f, 300.0f);
        const float diffuseDegree = water ? 0.34f : 0.40f;
        for (uint32_t tap = 0u; tap < kAzimuthFractions.size(); ++tap) {
            const float azimuth = wrapDegrees(params_.azimuthDeg
                + kAzimuthFractions[tap] * reflectionArc);
            const float elevation = water
                ? (-3.0f + static_cast<float>(tap) * 1.7f)
                : (1.0f + params_.terrain
                    * (3.0f + static_cast<float>((tap * 5u) % 4u) * 2.0f));
            const auto basis = acnSn3dBasis7(directionFromAed(
                azimuth, elevation));
            landscapeEncodingTargets_[tap].fill(0.0f);
            float degreeWeight = 1.0f;
            for (uint32_t degree = 0u; degree <= params_.order; ++degree) {
                if (degree > 0u) degreeWeight *= diffuseDegree;
                const uint32_t first = degree * degree;
                const uint32_t end = std::min<uint32_t>(
                    (degree + 1u) * (degree + 1u), channels);
                for (uint32_t channel = first; channel < end; ++channel) {
                    landscapeEncodingTargets_[tap][channel] =
                        basis[channel] * degreeWeight;
                }
            }
        }
    }

    std::array<float, 4u> processLandscapeReturn(float input)
    {
        std::array<float, 4u> taps {};
        if (landscapeDelayBuffer_.size() < 3u) return taps;

        smoothedLandscapeFeedback_ +=
            (targetLandscapeFeedback_ - smoothedLandscapeFeedback_)
            * smoothingCoefficient_;
        smoothedLandscapeInputAlpha_ +=
            (targetLandscapeInputAlpha_ - smoothedLandscapeInputAlpha_)
            * smoothingCoefficient_;
        smoothedLandscapeDampingAlpha_ +=
            (targetLandscapeDampingAlpha_ - smoothedLandscapeDampingAlpha_)
            * smoothingCoefficient_;
        smoothedLandscapeReturnGain_ +=
            (targetLandscapeReturnGain_ - smoothedLandscapeReturnGain_)
            * smoothingCoefficient_;

        const float delaySmoothing = smoothingCoefficient_ * 0.12f;
        const float bufferSize = static_cast<float>(
            landscapeDelayBuffer_.size());
        for (uint32_t tap = 0u; tap < taps.size(); ++tap) {
            landscapeDelaySamples_[tap] +=
                (targetLandscapeDelaySamples_[tap]
                    - landscapeDelaySamples_[tap]) * delaySmoothing;
            float readPosition = static_cast<float>(landscapeWriteIndex_)
                - landscapeDelaySamples_[tap];
            while (readPosition < 0.0f) readPosition += bufferSize;
            while (readPosition >= bufferSize) readPosition -= bufferSize;
            const size_t first = static_cast<size_t>(readPosition);
            const size_t second = (first + 1u)
                % landscapeDelayBuffer_.size();
            const float fraction = readPosition
                - static_cast<float>(first);
            taps[tap] = landscapeDelayBuffer_[first]
                + (landscapeDelayBuffer_[second]
                    - landscapeDelayBuffer_[first]) * fraction;
        }

        landscapeInputLow_ += (input - landscapeInputLow_)
            * smoothedLandscapeInputAlpha_;
        const float feedback = (taps[0] - taps[1] + taps[2] - taps[3])
            * 0.25f;
        landscapeFeedbackLow_ += (feedback - landscapeFeedbackLow_)
            * smoothedLandscapeDampingAlpha_;
        landscapeDelayBuffer_[landscapeWriteIndex_] = softSat(
            landscapeInputLow_
                + landscapeFeedbackLow_ * smoothedLandscapeFeedback_);
        landscapeWriteIndex_ = (landscapeWriteIndex_ + 1u)
            % landscapeDelayBuffer_.size();

        static constexpr std::array<float, 4u> kTapGain {
            0.46f, 0.38f, 0.32f, 0.27f
        };
        for (uint32_t tap = 0u; tap < taps.size(); ++tap) {
            taps[tap] *= kTapGain[tap] * smoothedLandscapeReturnGain_;
        }
        return taps;
    }

    float sourceLevel(SourceKind kind) const
    {
        switch (kind) {
        case SourceKind::Motor: return smoothedMachines_;
        case SourceKind::Modal: return smoothedBells_;
        case SourceKind::Air: return smoothedAirNoise_;
        case SourceKind::Traffic: return smoothedTraffic_;
        case SourceKind::Aircraft: return smoothedAircraft_;
        case SourceKind::Foghorn: return smoothedFoghorns_;
        case SourceKind::Surf: return smoothedSurf_;
        default: return 1.0f;
        }
    }

    float propagate(Voice& voice, float sample)
    {
        voice.band += (sample - voice.band) * voice.filterAlpha;
        return voice.band * voice.propagationGain;
    }

    double sampleRate_ = 48000.0;
    float smoothingCoefficient_ = 0.001f;
    float listenerEnergyCoefficient_ = 0.00003f;
    float smoothedAirNoise_ = 0.35f;
    float smoothedMachines_ = 0.45f;
    float smoothedBells_ = 0.25f;
    float smoothedTraffic_ = 0.45f;
    float smoothedAircraft_ = 0.0f;
    float smoothedFoghorns_ = 0.0f;
    float smoothedSurf_ = 0.15f;
    float smoothedTrafficSpeed_ = 0.50f;
    float smoothedEngineLoad_ = 0.55f;
    float smoothedAircraftFlight_ = 0.80f;
    float smoothedAircraftSpeed_ = 0.52f;
    float smoothedAircraftPower_ = 0.62f;
    float smoothedAircraftTone_ = 0.35f;
    float smoothedFoghornPitch_ = 0.42f;
    float smoothedFoghornPressure_ = 0.75f;
    float smoothedFoghornLength_ = 0.55f;
    float smoothedWaveRate_ = 0.45f;
    float smoothedWaveBreak_ = 0.58f;
    float smoothedMachineTone_ = 0.50f;
    float smoothedBellPitch_ = 0.52f;
    float smoothedBellDecay_ = 0.68f;
    float smoothedLandscapeAmount_ = 0.62f;
    float targetLandscapeFeedback_ = 0.42f;
    float smoothedLandscapeFeedback_ = 0.42f;
    float targetLandscapeInputAlpha_ = 0.15f;
    float smoothedLandscapeInputAlpha_ = 0.15f;
    float targetLandscapeDampingAlpha_ = 0.10f;
    float smoothedLandscapeDampingAlpha_ = 0.10f;
    float targetLandscapeReturnGain_ = 0.30f;
    float smoothedLandscapeReturnGain_ = 0.30f;
    float landscapeInputLow_ = 0.0f;
    float landscapeFeedbackLow_ = 0.0f;
    float landscapeReturnEnergy_ = 0.0f;
    float listenerDirectEnergy_ = 0.0f;
    float listenerReturnEnergy_ = 0.0f;
    float listenerReturnShare_ = 0.0f;
    size_t landscapeWriteIndex_ = 0u;
    std::vector<float> landscapeDelayBuffer_ {};
    std::array<float, 4u> landscapeDelaySamples_ {};
    std::array<float, 4u> targetLandscapeDelaySamples_ {};
    std::array<std::array<float, kAmbiHorizonMaxChannels>, 4u>
        landscapeEncodingWeights_ {};
    std::array<std::array<float, kAmbiHorizonMaxChannels>, 4u>
        landscapeEncodingTargets_ {};
    uint64_t frameCounter_ = 0u;
    uint32_t renderEntityCount_ = 24u;
    AmbiHorizonEncoderParams params_ {};
    EnvironmentalScore score_ {};
    AmbiFieldListener fieldListener_ {};
    std::array<Voice, kAmbiHorizonMaxEntities> voices_ {};
};

} // namespace s3g
