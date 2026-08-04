#pragma once

#include "s3g_environmental_score.h"
#include "s3g_geological_field.h"
#include "s3g_planetary_cryosphere.h"
#include "s3g_planetary_modal_body.h"
#include "s3g_quasicrystal_ice.h"
#include "s3g_structural_failure.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace s3g {

inline constexpr uint32_t kAmbiCryosphereMaxOrder = 7u;
inline constexpr uint32_t kAmbiCryosphereMaxChannels = 64u;
inline constexpr uint32_t kAmbiCryosphereMaxVoices = 64u;
inline constexpr uint32_t kAmbiCryosphereSingingLakeRegime = 13u;
inline constexpr uint32_t kAmbiCryosphereAperiodicLatticeRegime = 14u;
inline constexpr uint32_t kAmbiCryosphereTidalShellRegime = 15u;
inline constexpr uint32_t kAmbiCryosphereHydrocarbonDuneRegime = 16u;
inline constexpr uint32_t kAmbiCryosphereReactiveBrineRegime = 17u;
inline constexpr uint32_t kAmbiCryosphereRegimeCount = 18u;
inline constexpr uint32_t kAmbiCryosphereEnvironmentCount = 10u;
inline constexpr uint32_t kAmbiCryospherePlaceCount = 6u;

inline constexpr bool isAmbiCryospherePlanetaryRegime(uint32_t regime)
{
    return regime >= kAmbiCryosphereTidalShellRegime
        && regime <= kAmbiCryosphereReactiveBrineRegime;
}

inline constexpr bool isAmbiCryosphereAlienRegime(uint32_t regime)
{
    return regime >= kAmbiCryosphereAperiodicLatticeRegime
        && regime <= kAmbiCryosphereReactiveBrineRegime;
}

inline constexpr bool isAmbiCryosphereModalRegime(uint32_t regime)
{
    return regime == kAmbiCryosphereSingingLakeRegime
        || isAmbiCryosphereAlienRegime(regime);
}

inline constexpr PlanetaryCryosphereMode ambiCryospherePlanetaryMode(
    uint32_t regime)
{
    return regime == kAmbiCryosphereHydrocarbonDuneRegime
        ? PlanetaryCryosphereMode::HydrocarbonDune
        : regime == kAmbiCryosphereReactiveBrineRegime
            ? PlanetaryCryosphereMode::ReactiveBrine
            : PlanetaryCryosphereMode::TidalShell;
}

struct AmbiCryosphereParams {
    uint32_t order = 3u;
    uint32_t voices = 28u;
    float water = 0.58f;         // ice growth / cold agent
    float flow = 0.48f;          // master geological event-rate multiplier
    float scale = 0.46f;
    float turbulence = 0.38f;    // fracture branching
    float aeration = 0.30f;      // loose grains
    float spread = 0.58f;
    float deviation = 0.14f;
    uint32_t regime = 0u;
    uint32_t environment = 0u;   // affected substrate
    float drops = 0.26f;         // frozen impacts
    float splash = 0.34f;        // calving probability
    float bubbles = 0.18f;       // brine / pore-water pressure
    float density = 0.38f;       // fracture density
    float eventSize = 0.42f;     // released mass
    float eventDecay = 0.38f;    // aperiodic fracture tail
    float depth = 0.48f;
    float brightness = 0.44f;
    float resonance = 0.30f;     // diffuse body tail, never a free mode
    float damping = 0.52f;
    float contact = 0.30f;       // brittleness
    float motionRateHz = 0.12f;
    float current = 0.54f;       // transport / basal drive
    float slope = 0.18f;
    float eddy = 0.32f;
    float convergence = 0.18f;
    float width = 0.68f;
    float centerAzimuthDeg = 0.0f;
    float centerElevationDeg = 0.0f;
    float centerDistance = 1.0f;
    float spatialFollow = 0.72f;
    float outputGainDb = -6.0f;
    uint32_t place = 0u;
    float space = 0.18f;
    float environmentSize = 0.5f;
    float environmentDecay = 0.5f;
    float environmentDamping = 0.5f;
    AmbiFieldListenMode fieldListenMode = AmbiFieldListenMode::Off;
    float fieldListenAmount = 1.0f;
    AmbiFieldListenerResponse fieldListenResponse =
        AmbiFieldListenerResponse::Legacy;
    float foam = 0.0f;           // snow mass
    float shore = 0.0f;          // basal grinding
    float surfaceLoad = 0.20f;   // pressure steps / weight on ice plates
    float snap = 0.48f;          // high-frequency radial crack detail
    float plateFailure = 0.14f;  // through-fracture and local collapse
    float surfaceX = 0.5f;
    float surfaceY = 0.5f;
    float scorePace = 0.46f;       // slow-to-fast causal arc pacing
    float scoreOccupancy = 0.28f;  // simultaneous plates / loads / cracks
    float scoreCascade = 0.58f;    // crack and load propagation
    float scoreMemory = 0.72f;     // persistence of macro-scene behaviour
    float scoreRest = 0.64f;       // duration and depth of stillness
};

using AmbiCryospherePoint = GeologicalFieldPoint;

inline AmbiEnvironmentProfileId ambiCryosphereEnvironmentProfile(
    uint32_t place)
{
    constexpr std::array<AmbiEnvironmentProfileId,
        kAmbiCryospherePlaceCount> profiles {
        AmbiEnvironmentProfileId::Open,
        AmbiEnvironmentProfileId::Submerged,
        AmbiEnvironmentProfileId::Cave,
        AmbiEnvironmentProfileId::Cistern,
        AmbiEnvironmentProfileId::Channel,
        AmbiEnvironmentProfileId::Pipe,
    };
    return profiles[std::min<uint32_t>(
        place, kAmbiCryospherePlaceCount - 1u)];
}

struct AmbiCryosphereRegimeProfile {
    float freezeGrowth;
    float fracture;
    float basalSlip;
    float granular;
    float calving;
    float waterImpact;
    float settling;
    float hardness;
};

inline constexpr std::array<AmbiCryosphereRegimeProfile,
    kAmbiCryosphereRegimeCount> kAmbiCryosphereRegimeProfiles {{
        // FROST CRACK, ICE SEGREGATION, PERMAFROST HEAVE,
        // BASAL STICK-SLIP, PRESSURE RIDGE, CALVING,
        // ICEBERG IMPACT, AVALANCHE, SNOWPACK CREEP,
        // HAIL, SLEET, FREEZING RAIN, MELTWATER UNDER ICE, SINGING LAKE,
        // APERIODIC LATTICE, TIDAL SHELL, HYDROCARBON DUNE,
        // REACTIVE BRINE.
        { 0.82f, 0.88f, 0.12f, 0.08f, 0.00f, 0.12f, 0.32f, 0.94f },
        { 1.00f, 0.72f, 0.10f, 0.10f, 0.00f, 0.10f, 0.62f, 0.80f },
        { 0.94f, 0.64f, 0.58f, 0.34f, 0.02f, 0.10f, 0.78f, 0.68f },
        { 0.62f, 0.76f, 0.92f, 0.24f, 0.34f, 0.28f, 0.58f, 0.88f },
        { 0.78f, 0.82f, 1.00f, 0.34f, 0.30f, 0.24f, 0.64f, 0.92f },
        { 0.54f, 0.94f, 0.42f, 0.42f, 1.00f, 0.72f, 0.72f, 0.78f },
        { 0.28f, 0.72f, 0.18f, 0.42f, 0.78f, 1.00f, 0.68f, 0.82f },
        { 0.38f, 0.60f, 0.88f, 1.00f, 0.46f, 0.54f, 0.92f, 0.42f },
        { 0.28f, 0.24f, 0.64f, 1.00f, 0.08f, 0.12f, 1.00f, 0.22f },
        { 0.10f, 0.48f, 0.16f, 1.00f, 0.01f, 0.20f, 0.24f, 1.00f },
        { 0.22f, 0.42f, 0.30f, 1.00f, 0.02f, 0.42f, 0.34f, 0.78f },
        { 0.62f, 0.64f, 0.24f, 0.84f, 0.10f, 0.52f, 0.44f, 0.90f },
        { 0.44f, 0.42f, 0.82f, 0.28f, 0.08f, 0.76f, 0.64f, 0.56f },
        { 0.72f, 0.82f, 0.08f, 0.02f, 0.00f, 0.08f, 0.74f, 0.88f },
        { 0.68f, 0.96f, 0.06f, 0.02f, 0.00f, 0.00f, 0.82f, 0.92f },
        { 0.38f, 0.92f, 0.04f, 0.00f, 0.00f, 0.00f, 0.86f, 0.88f },
        { 0.08f, 0.12f, 0.72f, 0.94f, 0.00f, 0.00f, 0.96f, 0.34f },
        { 0.72f, 0.58f, 0.18f, 0.06f, 0.00f, 0.00f, 0.90f, 0.62f },
    }};

struct AmbiCryosphereSubstrateProfile {
    float hardness;
    float damping;
    float roughness;
    float poreWater;
    float lowMass;
};

// OPEN ICE, ROCK, SNOWPACK, MORAINE, CONCRETE, METAL, GLASS,
// ICE TUNNEL, ICE CAVE, GLACIER.
inline constexpr std::array<AmbiCryosphereSubstrateProfile,
    kAmbiCryosphereEnvironmentCount> kAmbiCryosphereSubstrateProfiles {{
        { 0.72f, 0.38f, 0.30f, 0.48f, 0.52f },
        { 0.86f, 0.46f, 0.78f, 0.62f, 0.72f },
        { 0.12f, 0.94f, 0.66f, 0.42f, 0.24f },
        { 0.68f, 0.62f, 0.94f, 0.72f, 0.88f },
        { 0.92f, 0.34f, 0.42f, 0.20f, 0.68f },
        { 1.00f, 0.18f, 0.28f, 0.04f, 0.56f },
        { 0.98f, 0.22f, 0.18f, 0.02f, 0.34f },
        { 0.82f, 0.30f, 0.46f, 0.38f, 0.62f },
        { 0.74f, 0.42f, 0.66f, 0.58f, 0.82f },
        { 0.78f, 0.52f, 0.72f, 0.68f, 1.00f },
    }};

struct AmbiCryosphereVoice {
    uint32_t rng = 1u;
    float infraNoise = 0.0f;
    float massNoise = 0.0f;
    float slowNoise = 0.0f;
    float contactNoise = 0.0f;
    float airNoise = 0.0f;
    float previousWhite = 0.0f;
    float strain = 0.0f;
    float fractureThreshold = 0.08f;
    float slipLoad = 0.0f;
    float slipThreshold = 0.08f;
    float branchTimer = 0.0f;
    uint32_t branchesRemaining = 0u;
    float grainTimer = 0.0f;
    float scheduledGrainRateHz = 0.0f;
    bool grainScheduled = false;
    float calvingTimer = 0.0f;
    float scheduledCalvingRateHz = 0.0f;
    bool calvingScheduled = false;
    float impactCountdown = -1.0f;
    float fractureEnvelope = 0.0f;
    float macroEnvelope = 0.0f;
    float slipEnvelope = 0.0f;
    float grainEnvelope = 0.0f;
    float impactEnvelope = 0.0f;
    float plateBodyEnvelope = 0.0f;
    float plateBodyPhase = 0.0f;
    float plateBodyFrequencyHz = 46.0f;
    float cavitationEnvelope = 0.0f;
    float tailEnvelope = 0.0f;
    float forcePulse = 0.0f;
    float eventLevel = 0.0f;
    float energy = 0.0f;
    float singingSample = 0.0f;
    float singingActivity = 0.0f;
    float quasicrystalSample = 0.0f;
    float quasicrystalActivity = 0.0f;
    float planetarySample = 0.0f;
    float planetaryActivity = 0.0f;
    float planetaryModalSample = 0.0f;
    float planetaryModalActivity = 0.0f;
    bool planetaryRouteActive = false;
    float plateIntegrity = 1.0f;
    float crackExtent = 0.0f;
    float brineCharge = 0.0f;
    float scoreActivity = 0.0f;
    float scoreDrive = 0.0f;
    float scorePropagation = 0.0f;
    float scoreConsequence = 0.0f;
    float scoreAftermath = 0.0f;
    // Normalized to a mean of one across active entities. surfaceLoad blends
    // this from uniform pressure to a localized physical contact while the
    // existing field directions remain the distributed sheet pickups.
    float skinContactWeight = 1.0f;
    StructuralFailureModel structure {};
    QuasicrystalIceModel quasicrystalIce {};
    PlanetaryCryosphereModel planetaryCryosphere {};
    PlanetaryModalBody planetaryModalBody {};
};

class AmbiCryosphereEncoder {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1000.0, sampleRate);
        field_.prepare(sampleRate_);
        score_.prepare(sampleRate_, 0x4372796fu);
        reset();
        setParams(params_);
    }

    void reset()
    {
        field_.reset();
        score_.reset(params_.voices);
        for (uint32_t voice = 0u; voice < voices_.size(); ++voice) {
            initializeVoice(voice);
        }
        lastOutput_.fill(0.0f);
        transitionTail_.fill(0.0f);
        transitionFade_ = 1.0f;
        transitionRequested_.store(false, std::memory_order_relaxed);
        smoothedOutputGain_ = dbToGain(params_.outputGainDb);
        iceLayerEnergy_ = 0.0f;
        singingIceLayerEnergy_ = 0.0f;
        quasicrystalLayerEnergy_ = 0.0f;
        planetaryLayerEnergy_ = 0.0f;
        planetaryModalLayerEnergy_ = 0.0f;
        fractureEventCount_ = 0u;
        slipEventCount_ = 0u;
        calvingEventCount_ = 0u;
        impactEventCount_ = 0u;
        structuralSnapEventCount_ = 0u;
        plateFailureEventCount_ = 0u;
        singingIceEventCount_ = 0u;
        quasicrystalFrontEventCount_ = 0u;
        quasicrystalAvalancheEventCount_ = 0u;
        tidalRiftEventCount_ = 0u;
        hydrocarbonAvalancheEventCount_ = 0u;
        brineBreakthroughEventCount_ = 0u;
        modalExcitationEventCount_ = 0u;
    }

    void setParams(AmbiCryosphereParams params)
    {
        sanitize(params);
        const uint32_t previousRegime = params_.regime;
        params_ = params;
        GeologicalFieldParams fieldParams {};
        fieldParams.voices = params.voices;
        fieldParams.spread = clamp(params.spread
            * (0.52f + params.width * 0.58f), 0.0f, 1.0f);
        fieldParams.deviation = clamp(params.deviation
            * (0.54f + params.width * 0.52f), 0.0f, 1.0f);
        fieldParams.motionRateHz = params.motionRateHz;
        fieldParams.transport = params.current;
        fieldParams.shear = params.convergence;
        fieldParams.curl = params.eddy;
        fieldParams.vertical = params.slope;
        fieldParams.centerAzimuthDeg = params.centerAzimuthDeg;
        fieldParams.centerElevationDeg = params.centerElevationDeg;
        fieldParams.centerDistance = params.centerDistance;
        fieldParams.spatialFollow = params.spatialFollow;
        fieldParams.environmentProfile =
            ambiCryosphereEnvironmentProfile(params.place);
        fieldParams.environmentAmount = params.space;
        fieldParams.environmentSize = params.environmentSize;
        fieldParams.environmentDecay = params.environmentDecay;
        fieldParams.environmentDamping = params.environmentDamping;
        fieldParams.fieldListenMode = params.fieldListenMode;
        fieldParams.fieldListenAmount = params.fieldListenAmount;
        fieldParams.fieldListenResponse = params.fieldListenResponse;
        field_.setParams(fieldParams);
        score_.setParams({ params.scorePace, params.scoreOccupancy,
            params.scoreCascade, params.scoreMemory, params.scoreRest });
        score_.setRangeExpansion(params.regime
                >= kAmbiCryosphereAperiodicLatticeRegime
            ? 1.0f : 0.0f);
        const auto modalParams = cryosphereModalParams(params);
        const bool modalTopologyChanged = previousRegime != params.regime
            && (isAmbiCryosphereModalRegime(previousRegime)
                || isAmbiCryosphereModalRegime(params.regime));
        for (auto& voice : voices_) {
            if (modalTopologyChanged) {
                voice.planetaryModalBody.reset();
                voice.planetaryModalSample = 0.0f;
                voice.planetaryModalActivity = 0.0f;
            }
            voice.planetaryModalBody.setParams(modalParams);
        }
    }

    AmbiCryosphereParams params() const { return params_; }

    void setParameterSurfaceGlideMs(float glideMs)
    {
        field_.setParameterSurfaceGlideMs(glideMs);
    }

    void setParameterSurfaceVoiceMembership(
        const std::array<float, kAmbiCryosphereMaxVoices>& membership)
    {
        field_.setParameterSurfaceVoiceMembership(membership);
    }

    void clearParameterSurfaceVoiceMembership()
    {
        field_.clearParameterSurfaceVoiceMembership();
    }

    uint32_t processingVoiceCount() const
    {
        return field_.processingVoiceCount();
    }

    float voiceRenderGain(uint32_t voice) const
    {
        return field_.voiceRenderGain(voice);
    }

    float voiceSkinContactWeight(uint32_t voice) const
    {
        return voices_[std::min<uint32_t>(
            voice, kAmbiCryosphereMaxVoices - 1u)].skinContactWeight;
    }

    void beginTransition()
    {
        transitionRequested_.store(true, std::memory_order_release);
    }

    float voiceEnergy(uint32_t voice) const
    {
        return voices_[std::min<uint32_t>(
            voice, kAmbiCryosphereMaxVoices - 1u)].energy;
    }

    AmbiCryospherePoint voicePoint(uint32_t voice) const
    {
        return field_.point(voice);
    }

    float voiceEventLevel(uint32_t voice) const
    {
        return voices_[std::min<uint32_t>(
            voice, kAmbiCryosphereMaxVoices - 1u)].eventLevel;
    }

    float iceLayerEnergy() const { return iceLayerEnergy_; }
    float singingIceLayerEnergy() const { return singingIceLayerEnergy_; }
    float singingLakeLayerEnergy() const { return singingIceLayerEnergy_; }
    float quasicrystalLayerEnergy() const { return quasicrystalLayerEnergy_; }
    float planetaryLayerEnergy() const { return planetaryLayerEnergy_; }
    float planetaryModalLayerEnergy() const
    {
        return planetaryModalLayerEnergy_;
    }
    uint64_t modalExcitationEventCount() const
    {
        return modalExcitationEventCount_;
    }
    uint64_t fractureEventCount() const { return fractureEventCount_; }
    uint64_t slipEventCount() const { return slipEventCount_; }
    uint64_t calvingEventCount() const { return calvingEventCount_; }
    uint64_t impactEventCount() const { return impactEventCount_; }
    uint64_t structuralSnapEventCount() const
    {
        return structuralSnapEventCount_;
    }
    uint64_t plateFailureEventCount() const
    {
        return plateFailureEventCount_;
    }
    uint64_t singingIceEventCount() const { return singingIceEventCount_; }
    uint64_t singingLakeEventCount() const { return singingIceEventCount_; }
    uint64_t quasicrystalFrontEventCount() const
    {
        return quasicrystalFrontEventCount_;
    }
    uint64_t quasicrystalAvalancheEventCount() const
    {
        return quasicrystalAvalancheEventCount_;
    }
    uint64_t tidalRiftEventCount() const { return tidalRiftEventCount_; }
    uint64_t hydrocarbonAvalancheEventCount() const
    {
        return hydrocarbonAvalancheEventCount_;
    }
    uint64_t brineBreakthroughEventCount() const
    {
        return brineBreakthroughEventCount_;
    }
    float scoreActivity() const { return score_.globalActivity(); }
    uint32_t scoredEntityCount() const { return score_.activeEntityCount(); }
    uint64_t scoreArcCount() const { return score_.arcCount(); }
    uint64_t scoreCascadeCount() const { return score_.cascadeCount(); }
    uint64_t scoreConsequenceCount() const
    {
        return score_.consequenceCount();
    }

    void process(float* const* outputs, uint32_t outputChannels,
        uint32_t frames)
    {
        if (!outputs || frames == 0u) return;
        outputChannels = std::min<uint32_t>(
            outputChannels, kAmbiCryosphereMaxChannels);
        for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
            if (outputs[channel]) {
                std::fill(outputs[channel], outputs[channel] + frames, 0.0f);
            }
        }
        if (transitionRequested_.exchange(false, std::memory_order_acq_rel)) {
            transitionTail_ = lastOutput_;
            transitionFade_ = 0.0f;
        }

        const uint32_t ambiChannels = std::min<uint32_t>(
            ambiChannelsForOrder(params_.order), outputChannels);
        const uint32_t voiceCount = processingVoiceCount();
        const float voiceNorm = std::pow(field_.voiceMass(), 0.44f);
        constexpr uint32_t kControlFrames = 16u;
        double layerEnergy = 0.0;
        double singingEnergy = 0.0;
        double quasicrystalEnergy = 0.0;
        double planetaryEnergy = 0.0;
        double planetaryModalEnergy = 0.0;

        for (uint32_t chunkStart = 0u; chunkStart < frames;
            chunkStart += kControlFrames) {
            const uint32_t chunkFrames = std::min<uint32_t>(
                kControlFrames, frames - chunkStart);
            const float dt = static_cast<float>(chunkFrames)
                / static_cast<float>(sampleRate_);
            std::array<float, kAmbiCryosphereMaxVoices> activity {};
            for (uint32_t voice = 0u; voice < voiceCount; ++voice) {
                activity[voice] = voices_[voice].eventLevel;
            }
            field_.update(dt, activity.data());
            updateSkinContactWeights(voiceCount, dt);
            for (uint32_t voice = 0u; voice < voiceCount; ++voice) {
                const auto direction = field_.direction(voice);
                score_.setEntityPosition(voice,
                    direction.x, direction.y, direction.z);
            }
            score_.update(dt, voiceCount);
            applyScoreDirectives(voiceCount);
            const float targetGain = dbToGain(params_.outputGainDb)
                * 1.16f / voiceNorm;

            for (uint32_t frame = chunkStart;
                frame < chunkStart + chunkFrames; ++frame) {
                smoothedOutputGain_ +=
                    (targetGain - smoothedOutputGain_) * 0.0015f;
                field_.beginEnvironmentFrame();
                std::array<float, kAmbiCryosphereMaxChannels>
                    listenerFrame {};

                for (uint32_t voice = 0u; voice < voiceCount; ++voice) {
                    const float membership = field_.voiceRenderGain(voice);
                    if (membership <= 1.0e-7f) continue;
                    float sample = processVoice(voice)
                        * smoothedOutputGain_ * membership
                        * field_.distanceGain(voice);
                    if (!std::isfinite(sample)) {
                        initializeVoice(voice);
                        sample = 0.0f;
                    }
                    layerEnergy += static_cast<double>(sample) * sample;
                    singingEnergy += static_cast<double>(
                        voices_[voice].singingSample)
                        * voices_[voice].singingSample;
                    quasicrystalEnergy += static_cast<double>(
                        voices_[voice].quasicrystalSample)
                        * voices_[voice].quasicrystalSample;
                    planetaryEnergy += static_cast<double>(
                        voices_[voice].planetarySample)
                        * voices_[voice].planetarySample;
                    planetaryModalEnergy += static_cast<double>(
                        voices_[voice].planetaryModalSample)
                        * voices_[voice].planetaryModalSample;
                    const auto& basis = field_.basis(voice);
                    for (uint32_t channel = 0u; channel < ambiChannels;
                        ++channel) {
                        const float encoded = sample * basis[channel];
                        listenerFrame[channel] += encoded;
                        if (outputs[channel]) {
                            outputs[channel][frame] += encoded;
                        }
                    }
                    field_.addEnvironmentSource(sample, voice,
                        0.58f + params_.depth * 0.30f
                            + voices_[voice].eventLevel * 0.64f);
                }

                const auto environment = field_.processEnvironment();
                for (uint32_t channel = 0u; channel
                        < std::min<uint32_t>(ambiChannels,
                            kAmbiEnvironmentChannels);
                    ++channel) {
                    listenerFrame[channel] += environment[channel];
                    if (outputs[channel]) {
                        outputs[channel][frame] += environment[channel];
                    }
                }
                field_.processListenerFrame(listenerFrame.data(), ambiChannels);
            }
        }

        const float transitionStep = 1.0f / std::max(
            1.0f, static_cast<float>(sampleRate_) * 0.032f);
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            const float mix = transitionFade_;
            for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
                const float sum = channel < ambiChannels && outputs[channel]
                    ? outputs[channel][frame] : 0.0f;
                float fresh = std::tanh(clamp(sum * 1.08f, -4.0f, 4.0f));
                if (!std::isfinite(fresh)) fresh = 0.0f;
                const float value = transitionTail_[channel] * (1.0f - mix)
                    + fresh * mix;
                if (outputs[channel]) outputs[channel][frame] = value;
                lastOutput_[channel] = value;
            }
            transitionFade_ = std::min(1.0f,
                transitionFade_ + transitionStep);
        }

        const float measured = static_cast<float>(layerEnergy
            / std::max<uint32_t>(1u,
                frames * std::max<uint32_t>(1u, voiceCount)));
        iceLayerEnergy_ += (measured - iceLayerEnergy_) * 0.24f;
        const float measuredSinging = static_cast<float>(singingEnergy
            / std::max<uint32_t>(1u,
                frames * std::max<uint32_t>(1u, voiceCount)));
        singingIceLayerEnergy_ +=
            (measuredSinging - singingIceLayerEnergy_) * 0.24f;
        const float measuredQuasicrystal = static_cast<float>(
            quasicrystalEnergy / std::max<uint32_t>(1u,
                frames * std::max<uint32_t>(1u, voiceCount)));
        quasicrystalLayerEnergy_ +=
            (measuredQuasicrystal - quasicrystalLayerEnergy_) * 0.24f;
        const float measuredPlanetary = static_cast<float>(
            planetaryEnergy / std::max<uint32_t>(1u,
                frames * std::max<uint32_t>(1u, voiceCount)));
        planetaryLayerEnergy_ +=
            (measuredPlanetary - planetaryLayerEnergy_) * 0.24f;
        const float measuredPlanetaryModal = static_cast<float>(
            planetaryModalEnergy / std::max<uint32_t>(1u,
                frames * std::max<uint32_t>(1u, voiceCount)));
        planetaryModalLayerEnergy_ +=
            (measuredPlanetaryModal - planetaryModalLayerEnergy_) * 0.24f;
    }

private:
    static float finiteClamp(float value, float fallback, float low, float high)
    {
        return clamp(std::isfinite(value) ? value : fallback, low, high);
    }

    static PlanetaryModalParams cryosphereModalParams(
        const AmbiCryosphereParams& params)
    {
        PlanetaryModalParams modal {};
        modal.profile = PlanetaryModalProfile::CryoTidal;
        modal.amount = isAmbiCryosphereModalRegime(params.regime)
            ? params.resonance : 0.0f;
        modal.scale = std::clamp(
            params.scale * 0.58f + params.depth * 0.42f, 0.0f, 1.0f);
        modal.damping = params.damping;
        modal.irregularity = std::clamp(params.density * 0.42f
                + params.turbulence * 0.36f + params.deviation * 0.22f,
            0.0f, 1.0f);
        modal.coupling = std::clamp(params.scoreCascade * 0.62f
                + params.convergence * 0.38f,
            0.0f, 1.0f);
        modal.brightness = std::clamp(params.brightness * 0.48f
                + params.snap * 0.52f,
            0.0f, 1.0f);

        switch (params.regime) {
        case kAmbiCryosphereSingingLakeRegime:
            modal.profile = PlanetaryModalProfile::CryoSingingLake;
            modal.scale = std::clamp(params.scale * 0.62f
                    + params.depth * 0.38f,
                0.0f, 1.0f);
            modal.irregularity = std::clamp(params.density * 0.46f
                    + params.turbulence * 0.24f
                    + params.deviation * 0.18f
                    + params.contact * 0.12f,
                0.0f, 1.0f);
            modal.coupling = std::clamp(params.scoreCascade * 0.48f
                    + params.surfaceLoad * 0.30f
                    + params.snap * 0.22f,
                0.0f, 1.0f);
            modal.brightness = std::clamp(params.brightness * 0.62f
                    + params.snap * 0.38f,
                0.0f, 1.0f);
            break;
        case kAmbiCryosphereHydrocarbonDuneRegime:
            modal.profile = PlanetaryModalProfile::CryoDune;
            modal.scale = std::clamp(params.scale * 0.54f
                    + params.density * 0.28f + params.foam * 0.18f,
                0.0f, 1.0f);
            modal.irregularity = std::clamp(params.aeration * 0.44f
                    + params.turbulence * 0.30f
                    + params.deviation * 0.26f,
                0.0f, 1.0f);
            modal.coupling = std::clamp(params.scoreCascade * 0.54f
                    + params.eddy * 0.28f + params.current * 0.18f,
                0.0f, 1.0f);
            modal.brightness = std::clamp(params.brightness * 0.62f
                    + params.contact * 0.38f,
                0.0f, 1.0f);
            break;
        case kAmbiCryosphereReactiveBrineRegime:
            modal.profile = PlanetaryModalProfile::CryoBrine;
            modal.scale = std::clamp(params.scale * 0.52f
                    + params.depth * 0.48f,
                0.0f, 1.0f);
            modal.irregularity = std::clamp(params.bubbles * 0.38f
                    + params.eddy * 0.34f + params.deviation * 0.28f,
                0.0f, 1.0f);
            modal.coupling = std::clamp(params.scoreCascade * 0.52f
                    + params.eddy * 0.28f + params.current * 0.20f,
                0.0f, 1.0f);
            modal.brightness = std::clamp(params.brightness * 0.52f
                    + params.snap * 0.48f,
                0.0f, 1.0f);
            break;
        case kAmbiCryosphereAperiodicLatticeRegime:
            modal.profile = PlanetaryModalProfile::CryoQuasicrystal;
            modal.irregularity = std::clamp(params.density * 0.38f
                    + params.turbulence * 0.30f
                    + params.convergence * 0.20f
                    + params.deviation * 0.12f,
                0.0f, 1.0f);
            break;
        case kAmbiCryosphereTidalShellRegime:
        default:
            break;
        }
        return modal;
    }

    void sanitize(AmbiCryosphereParams& params) const
    {
        params.order = std::clamp<uint32_t>(
            params.order, 1u, kAmbiCryosphereMaxOrder);
        params.voices = std::clamp<uint32_t>(
            params.voices, 1u, kAmbiCryosphereMaxVoices);
#define S3G_CRYO_CLAMP(member, low, high) \
        params.member = finiteClamp(params.member, params_.member, low, high)
        S3G_CRYO_CLAMP(water, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(flow, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(scale, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(turbulence, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(aeration, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(spread, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(deviation, 0.0f, 1.0f);
        params.regime = std::min<uint32_t>(
            params.regime, kAmbiCryosphereRegimeCount - 1u);
        params.environment = std::min<uint32_t>(
            params.environment, kAmbiCryosphereEnvironmentCount - 1u);
        S3G_CRYO_CLAMP(drops, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(splash, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(bubbles, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(density, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(eventSize, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(eventDecay, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(depth, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(brightness, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(resonance, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(damping, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(contact, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(motionRateHz, 0.002f, 3.0f);
        S3G_CRYO_CLAMP(current, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(slope, -1.0f, 1.0f);
        S3G_CRYO_CLAMP(eddy, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(convergence, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(width, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(centerAzimuthDeg, -180.0f, 180.0f);
        S3G_CRYO_CLAMP(centerElevationDeg, -90.0f, 90.0f);
        S3G_CRYO_CLAMP(centerDistance, 0.15f, 2.0f);
        S3G_CRYO_CLAMP(spatialFollow, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(outputGainDb, -60.0f, 12.0f);
        params.place = std::min<uint32_t>(
            params.place, kAmbiCryospherePlaceCount - 1u);
        S3G_CRYO_CLAMP(space, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(environmentSize, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(environmentDecay, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(environmentDamping, 0.0f, 1.0f);
        params.fieldListenMode = sanitizeAmbiFieldListenMode(
            params.fieldListenMode);
        S3G_CRYO_CLAMP(fieldListenAmount, 0.0f, 1.0f);
        params.fieldListenResponse = sanitizeAmbiFieldListenerResponse(
            params.fieldListenResponse);
        S3G_CRYO_CLAMP(foam, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(shore, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(surfaceLoad, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(snap, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(plateFailure, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(surfaceX, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(surfaceY, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(scorePace, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(scoreOccupancy, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(scoreCascade, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(scoreMemory, 0.0f, 1.0f);
        S3G_CRYO_CLAMP(scoreRest, 0.0f, 1.0f);
#undef S3G_CRYO_CLAMP
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

    static float randomSigned(uint32_t& state)
    {
        return randomUnit(state) * 2.0f - 1.0f;
    }

    float randomInterval(AmbiCryosphereVoice& voice, float rate)
    {
        if (rate <= 1.0e-5f) return 3600.0f;
        return -std::log(std::max(0.0001f,
            1.0f - randomUnit(voice.rng))) / rate;
    }

    void initializeVoice(uint32_t index)
    {
        auto& voice = voices_[index];
        voice = {};
        voice.rng = 0x1ce5eedu + index * 0x9e3779b9u;
        voice.strain = 0.0f;
        voice.fractureThreshold = 0.025f
            + randomUnit(voice.rng) * 0.16f;
        voice.slipLoad = 0.0f;
        voice.slipThreshold = 0.025f
            + randomUnit(voice.rng) * 0.18f;
        voice.grainTimer = 0.0f;
        voice.calvingTimer = 0.0f;
        voice.impactCountdown = -1.0f;
        voice.plateIntegrity = 0.68f + randomUnit(voice.rng) * 0.32f;
        voice.crackExtent = 0.0f;
        voice.brineCharge = randomUnit(voice.rng) * 0.18f;
        voice.skinContactWeight = 1.0f;
        voice.structure.prepare(sampleRate_,
            0x1ceba11u + index * 0x85ebca6bu);
        voice.quasicrystalIce.prepare(sampleRate_,
            0xa93c5e1du + index * 0x27d4eb2du);
        voice.planetaryCryosphere.prepare(sampleRate_,
            0x71da15ceu + index * 0x165667b1u);
        voice.planetaryModalBody.prepare(sampleRate_,
            0xc8a4f31du + index * 0x94d049bbu);
        voice.planetaryModalBody.setParams(
            cryosphereModalParams(params_));
    }

    void updateSkinContactWeights(uint32_t voiceCount, float dt)
    {
        if (voiceCount == 0u) return;
        std::array<float, kAmbiCryosphereMaxVoices> local {};
        float sum = 0.0f;
        const float radius = 0.07f + params_.width * 0.24f
            + params_.spread * 0.08f;
        const float inverseRadiusSquared = 1.0f
            / std::max(1.0e-6f, radius * radius);
        for (uint32_t index = 0u; index < voiceCount; ++index) {
            const Vec3 direction = normalize(field_.direction(index));
            const float azimuthUnit = std::atan2(direction.y, direction.x)
                    / (2.0f * kPi)
                + 0.5f;
            const float elevationUnit = std::asin(clamp(
                    direction.z, -1.0f, 1.0f))
                    / kPi
                + 0.5f;
            float dx = std::fabs(azimuthUnit - params_.surfaceX);
            dx = std::min(dx, 1.0f - dx);
            const float dy = elevationUnit - params_.surfaceY;
            const float distanceSquared = dx * dx + dy * dy;
            local[index] = 0.025f + std::exp(
                -0.5f * distanceSquared * inverseRadiusSquared);
            sum += local[index];
        }
        const float mean = sum / static_cast<float>(voiceCount);
        const float localization = std::sqrt(params_.surfaceLoad);
        std::array<float, kAmbiCryosphereMaxVoices> target {};
        float targetSum = 0.0f;
        for (uint32_t index = 0u; index < voiceCount; ++index) {
            target[index] = clamp(lerp(1.0f, local[index]
                    / std::max(1.0e-6f, mean), localization),
                0.18f, 4.0f);
            targetSum += target[index];
        }
        const float targetMean = targetSum / static_cast<float>(voiceCount);
        const float smoothing = 1.0f - std::exp(
            -dt / 0.055f);
        for (uint32_t index = 0u; index < voiceCount; ++index) {
            const float normalizedTarget = target[index]
                / std::max(1.0e-6f, targetMean);
            voices_[index].skinContactWeight += smoothing
                * (normalizedTarget - voices_[index].skinContactWeight);
        }
        for (uint32_t index = voiceCount;
            index < kAmbiCryosphereMaxVoices; ++index) {
            voices_[index].skinContactWeight = 1.0f;
        }
    }

    void applyScoreDirectives(uint32_t voiceCount)
    {
        const auto& regime = kAmbiCryosphereRegimeProfiles[
            std::min<uint32_t>(params_.regime,
                kAmbiCryosphereRegimeCount - 1u)];
        for (uint32_t index = 0u; index < voiceCount; ++index) {
            auto& voice = voices_[index];
            const auto directive = score_.directive(index);
            voice.scoreActivity = directive.activity;
            voice.scoreDrive = directive.drive;
            voice.scorePropagation = directive.propagation;
            voice.scoreConsequence = directive.consequence;
            voice.scoreAftermath = directive.aftermath;
            if (params_.flow <= 1.0e-5f) {
                voice.scoreActivity = 0.0f;
                voice.scoreDrive = 0.0f;
                voice.scorePropagation = 0.0f;
                voice.scoreConsequence = 0.0f;
                voice.scoreAftermath = 0.0f;
                continue;
            }
            if (isAmbiCryospherePlanetaryRegime(params_.regime)) {
                const auto mode = ambiCryospherePlanetaryMode(
                    params_.regime);
                if (directive.onset || directive.cascadeArrival) {
                    const float transfer = directive.cascadeArrival
                        ? 0.68f : 1.0f;
                    const float force = transfer * std::clamp(0.24f
                            + params_.eventSize * 0.24f
                            + params_.surfaceLoad
                                * voice.skinContactWeight * 0.20f
                            + params_.density * 0.16f
                            + params_.bubbles * 0.16f,
                        0.0f, 1.0f);
                    voice.planetaryCryosphere.excite(mode, force,
                        params_.scoreCascade, false);
                }
                if (directive.consequenceStarted) {
                    const float force = std::clamp(0.62f
                            + directive.consequence * 0.22f
                            + params_.eventSize * 0.16f,
                        0.0f, 1.0f);
                    voice.planetaryCryosphere.excite(mode, force,
                        params_.scoreCascade, true);
                }
                continue;
            }
            if (directive.arcStarted) {
                voice.plateIntegrity = 0.66f
                    + randomUnit(voice.rng) * 0.34f;
                voice.crackExtent = 0.0f;
                voice.brineCharge = params_.bubbles
                    * (0.24f + randomUnit(voice.rng) * 0.58f);
                voice.strain *= 0.16f;
                voice.slipLoad *= 0.22f;
            }
            if (directive.onset || directive.cascadeArrival) {
                const float transfer = directive.cascadeArrival
                    ? 0.68f : 1.0f;
                const float force = transfer
                    * (0.38f + params_.surfaceLoad
                            * voice.skinContactWeight * 0.38f
                        + params_.contact * 0.26f);
                voice.forcePulse += randomSigned(voice.rng) * force;
                voice.strain = std::max(voice.strain,
                    voice.fractureThreshold * (directive.cascadeArrival
                        ? 1.04f : 1.01f));
                voice.crackExtent = std::min(1.0f,
                    voice.crackExtent + force * 0.18f);
                voice.brineCharge = std::min(1.0f,
                    voice.brineCharge + force * params_.bubbles * 0.22f);
                if (params_.regime
                        == kAmbiCryosphereAperiodicLatticeRegime) {
                    voice.quasicrystalIce.excite(
                        force * (directive.cascadeArrival ? 0.72f : 1.0f),
                        params_.scoreCascade);
                }
            }
            if (directive.consequenceStarted) {
                const float force = std::clamp(0.58f
                        + directive.consequence * 0.42f,
                    0.0f, 1.0f);
                if (std::max({ params_.surfaceLoad, params_.snap,
                        params_.plateFailure }) > 0.001f) {
                    voice.structure.excite(force * std::sqrt(
                        voice.skinContactWeight), true);
                }
                voice.plateIntegrity *= 0.28f;
                voice.crackExtent = 1.0f;
                voice.strain = std::max(voice.strain,
                    voice.fractureThreshold * (1.06f + force * 0.12f));
                if (regime.calving * params_.splash > 0.18f) {
                    triggerCalving(voice, regime);
                } else {
                    voice.macroEnvelope = std::max(
                        voice.macroEnvelope,
                        force * (0.34f + params_.eventSize * 0.66f));
                }
                if (params_.regime
                        == kAmbiCryosphereAperiodicLatticeRegime) {
                    voice.quasicrystalIce.excite(
                        force * (0.82f + params_.eventSize * 0.72f),
                        params_.scoreCascade);
                }
            }
        }
    }

    void triggerFracture(AmbiCryosphereVoice& voice,
        const AmbiCryosphereRegimeProfile& regime,
        const AmbiCryosphereSubstrateProfile& substrate, bool branch)
    {
        const uint32_t index = static_cast<uint32_t>(
            &voice - voices_.data());
        const float listener = field_.listenerDrive(index);
        const float strength = (0.12f + randomUnit(voice.rng) * 0.88f)
            * (0.18f + params_.eventSize * 0.64f
                + params_.contact * 0.48f + regime.fracture * 0.54f)
            * (0.72f + substrate.hardness * 0.46f)
            * (1.0f + listener * 0.16f)
            * (branch ? 0.52f : 1.0f);
        voice.fractureEnvelope = std::max(
            voice.fractureEnvelope, strength);
        if (!branch
            && params_.regime != kAmbiCryosphereAperiodicLatticeRegime
            && params_.regime != kAmbiCryosphereSingingLakeRegime) {
            voice.plateBodyEnvelope = std::max(
                voice.plateBodyEnvelope,
                strength * (0.18f + params_.eventSize * 0.34f
                    + params_.scale * 0.28f));
            voice.plateBodyFrequencyHz = 31.0f
                + (1.0f - params_.scale) * 58.0f
                + randomUnit(voice.rng) * 16.0f;
            voice.plateBodyPhase = 0.0f;
        }
        voice.forcePulse += randomSigned(voice.rng) * strength;
        voice.tailEnvelope = std::max(voice.tailEnvelope,
            strength * params_.resonance * 0.46f);
        ++fractureEventCount_;
        if (params_.regime == kAmbiCryosphereAperiodicLatticeRegime) {
            voice.quasicrystalIce.excite(strength
                    * (branch ? 0.58f : 1.0f),
                std::clamp(params_.scoreCascade
                    + (branch ? 0.10f : 0.0f), 0.0f, 1.0f));
        }
        if (!branch && params_.regime
                == kAmbiCryosphereSingingLakeRegime) {
            voice.planetaryModalBody.excite(strength,
                params_.scoreCascade, false);
            ++singingIceEventCount_;
            ++modalExcitationEventCount_;
        }
        if (!branch) {
            voice.branchesRemaining = static_cast<uint32_t>(
                randomUnit(voice.rng)
                * (1.0f + params_.turbulence * 10.0f
                    + regime.fracture * 4.0f));
            voice.branchTimer = 0.002f + randomUnit(voice.rng)
                * (0.010f + params_.scale * 0.038f);
            if (randomUnit(voice.rng) < regime.calving
                    * params_.splash * 0.48f) {
                voice.macroEnvelope = std::max(voice.macroEnvelope,
                    strength * (0.58f + params_.scale * 0.82f));
            }
        }
    }

    void triggerSlip(AmbiCryosphereVoice& voice,
        const AmbiCryosphereRegimeProfile& regime,
        const AmbiCryosphereSubstrateProfile& substrate)
    {
        const float strength = (0.12f + randomUnit(voice.rng) * 0.72f)
            * (0.18f + params_.current * 0.46f
                + params_.convergence * 0.64f + params_.shore * 0.54f)
            * (0.42f + regime.basalSlip * 0.72f)
            * (0.58f + substrate.roughness * 0.62f);
        voice.slipEnvelope = std::max(voice.slipEnvelope, strength);
        voice.forcePulse += randomSigned(voice.rng) * strength * 0.48f;
        ++slipEventCount_;
    }

    void triggerCalving(AmbiCryosphereVoice& voice,
        const AmbiCryosphereRegimeProfile& regime)
    {
        const float strength = (0.32f + randomUnit(voice.rng) * 0.68f)
            * (0.28f + params_.eventSize * 0.72f)
            * (0.48f + params_.splash * 0.82f);
        voice.macroEnvelope = std::max(
            voice.macroEnvelope, strength);
        if (params_.regime != kAmbiCryosphereSingingLakeRegime) {
            voice.plateBodyEnvelope = std::max(
                voice.plateBodyEnvelope, strength * (0.58f
                    + params_.scale * 0.64f + params_.depth * 0.32f));
            voice.plateBodyFrequencyHz = 17.0f
                + (1.0f - params_.scale) * 34.0f
                + (1.0f - params_.depth) * 15.0f
                + randomUnit(voice.rng) * 9.0f;
            voice.plateBodyPhase = 0.0f;
        }
        voice.fractureEnvelope = std::max(
            voice.fractureEnvelope, strength * 0.58f);
        voice.impactCountdown = 0.055f + params_.scale * 0.42f
            + randomUnit(voice.rng) * (0.08f + params_.scale * 0.32f);
        voice.branchesRemaining += static_cast<uint32_t>(
            2.0f + randomUnit(voice.rng)
                * (3.0f + params_.turbulence * 10.0f));
        voice.branchTimer = 0.002f + randomUnit(voice.rng) * 0.014f;
        ++calvingEventCount_;
    }

    float processPlanetaryVoice(uint32_t index)
    {
        auto& voice = voices_[index];
        if (!voice.planetaryRouteActive) {
            voice.planetaryRouteActive = true;
            voice.strain = 0.0f;
            voice.slipLoad = 0.0f;
            voice.branchesRemaining = 0u;
            voice.grainScheduled = false;
            voice.calvingScheduled = false;
            voice.impactCountdown = -1.0f;
            voice.fractureEnvelope = 0.0f;
            voice.macroEnvelope = 0.0f;
            voice.slipEnvelope = 0.0f;
            voice.grainEnvelope = 0.0f;
            voice.impactEnvelope = 0.0f;
            voice.plateBodyEnvelope = 0.0f;
            voice.cavitationEnvelope = 0.0f;
            voice.tailEnvelope = 0.0f;
            voice.forcePulse = 0.0f;
            voice.structure.prepare(sampleRate_,
                0x1ceba11u + index * 0x85ebca6bu);
            voice.quasicrystalIce.reset();
        }
        const auto mode = ambiCryospherePlanetaryMode(params_.regime);
        PlanetaryCryosphereParams planetaryParams {};
        planetaryParams.mode = mode;
        const float scoredFlux = std::clamp(voice.scoreDrive
                + voice.scorePropagation * 0.64f
                + voice.scoreConsequence * 0.48f
                + voice.scoreAftermath * params_.scoreRest * 0.18f,
            0.0f, 1.0f);
        planetaryParams.drive = params_.flow * scoredFlux;
        planetaryParams.propagation = std::clamp(
            voice.scorePropagation * 0.46f
                + params_.scoreCascade * 0.38f
                + voice.scoreConsequence * 0.16f,
            0.0f, 1.0f);
        planetaryParams.consequence = voice.scoreConsequence;
        planetaryParams.aftermath = voice.scoreAftermath;

        switch (mode) {
        case PlanetaryCryosphereMode::TidalShell:
            planetaryParams.mass = std::clamp(
                params_.scale * 0.54f + params_.depth * 0.46f,
                0.0f, 1.0f);
            planetaryParams.mobility = std::clamp(
                params_.convergence * 0.52f + params_.current * 0.26f
                    + (1.0f - params_.damping) * 0.22f,
                0.0f, 1.0f);
            planetaryParams.branching = std::clamp(
                params_.scoreCascade * 0.64f
                    + params_.turbulence * 0.36f,
                0.0f, 1.0f);
            planetaryParams.pressure = std::clamp(
                params_.bubbles * 0.42f + params_.surfaceLoad * 0.34f
                    + params_.depth * 0.24f,
                0.0f, 1.0f);
            planetaryParams.cohesion = std::clamp(
                params_.water * 0.52f + params_.damping * 0.48f,
                0.0f, 1.0f);
            planetaryParams.porosity = std::clamp(
                params_.bubbles * 0.62f + params_.density * 0.38f,
                0.0f, 1.0f);
            planetaryParams.brightness = std::clamp(
                params_.brightness * 0.46f + params_.snap * 0.54f,
                0.0f, 1.0f);
            planetaryParams.damping = params_.damping;
            break;
        case PlanetaryCryosphereMode::HydrocarbonDune:
            planetaryParams.mass = std::clamp(
                params_.scale * 0.38f + params_.density * 0.34f
                    + params_.foam * 0.28f,
                0.0f, 1.0f);
            planetaryParams.mobility = std::clamp(
                params_.current * 0.44f + std::fabs(params_.slope) * 0.28f
                    + params_.eddy * 0.28f,
                0.0f, 1.0f);
            planetaryParams.branching = std::clamp(
                params_.turbulence * 0.48f
                    + params_.scoreCascade * 0.34f
                    + params_.aeration * 0.18f,
                0.0f, 1.0f);
            planetaryParams.pressure = std::clamp(
                params_.depth * 0.58f + params_.space * 0.42f,
                0.0f, 1.0f);
            planetaryParams.cohesion = std::clamp(
                params_.foam * 0.44f + params_.density * 0.34f
                    + params_.water * 0.22f,
                0.0f, 1.0f);
            planetaryParams.porosity = std::clamp(
                params_.aeration * 0.54f + params_.turbulence * 0.26f
                    + (1.0f - params_.foam) * 0.20f,
                0.0f, 1.0f);
            planetaryParams.brightness = std::clamp(
                params_.brightness * 0.54f + params_.contact * 0.46f,
                0.0f, 1.0f);
            planetaryParams.damping = std::clamp(
                params_.damping * 0.68f + params_.depth * 0.32f,
                0.0f, 1.0f);
            break;
        case PlanetaryCryosphereMode::ReactiveBrine:
            planetaryParams.mass = std::clamp(
                params_.scale * 0.48f + params_.depth * 0.52f,
                0.0f, 1.0f);
            planetaryParams.mobility = std::clamp(
                params_.current * 0.44f + params_.eddy * 0.36f
                    + std::fabs(params_.slope) * 0.20f,
                0.0f, 1.0f);
            planetaryParams.branching = std::clamp(
                params_.scoreCascade * 0.52f + params_.eddy * 0.28f
                    + params_.turbulence * 0.20f,
                0.0f, 1.0f);
            planetaryParams.pressure = std::clamp(
                params_.bubbles * 0.46f + params_.depth * 0.30f
                    + params_.surfaceLoad * 0.24f,
                0.0f, 1.0f);
            planetaryParams.cohesion = std::clamp(
                params_.water * 0.56f + params_.damping * 0.44f,
                0.0f, 1.0f);
            planetaryParams.porosity = std::clamp(
                params_.bubbles * 0.38f + params_.eddy * 0.34f
                    + (1.0f - params_.density) * 0.28f,
                0.0f, 1.0f);
            planetaryParams.brightness = std::clamp(
                params_.snap * 0.54f + params_.brightness * 0.46f,
                0.0f, 1.0f);
            planetaryParams.damping = params_.damping;
            break;
        }

        const auto planetary = voice.planetaryCryosphere.process(
            planetaryParams);
        voice.planetarySample = planetary.sample;
        voice.planetaryActivity = planetary.activity;
        voice.singingSample = 0.0f;
        voice.singingActivity = 0.0f;
        voice.quasicrystalSample = 0.0f;
        voice.quasicrystalActivity = 0.0f;

        if (planetary.primaryEvent) {
            switch (mode) {
            case PlanetaryCryosphereMode::TidalShell:
                ++tidalRiftEventCount_;
                break;
            case PlanetaryCryosphereMode::HydrocarbonDune:
                ++hydrocarbonAvalancheEventCount_;
                break;
            case PlanetaryCryosphereMode::ReactiveBrine:
                ++brineBreakthroughEventCount_;
                break;
            }
            const float strength = std::clamp(0.18f
                    + planetary.activity * 0.58f
                    + params_.eventSize * 0.24f,
                0.0f, 1.0f);
            voice.planetaryModalBody.excite(strength,
                params_.scoreCascade, voice.scoreConsequence > 0.35f);
            ++modalExcitationEventCount_;
        }
        if (planetary.secondaryEvent) {
            const float strength = std::clamp(0.08f
                    + planetary.activity * 0.38f
                    + params_.brightness * 0.14f,
                0.0f, 0.72f);
            voice.planetaryModalBody.excite(strength,
                std::clamp(params_.scoreCascade * 0.72f
                        + params_.turbulence * 0.28f,
                    0.0f, 1.0f));
            ++modalExcitationEventCount_;
        }

        const auto modal = voice.planetaryModalBody.process(
            planetary.sample, planetary.activity);
        const float boundedModal = modal.sample
            / (1.0f + std::fabs(modal.sample) * 0.72f);
        const float modalGain = mode
                == PlanetaryCryosphereMode::HydrocarbonDune
            ? 0.12f + params_.density * 0.08f
            : 0.16f + params_.eventSize * 0.10f;
        voice.planetaryModalSample = boundedModal * modalGain;
        voice.planetaryModalActivity = modal.activity;

        const float listenerGain = 1.0f
            + field_.listenerDrive(index) * 0.20f;
        const float materialGain = mode
                == PlanetaryCryosphereMode::HydrocarbonDune
            ? 0.94f + params_.density * 0.26f
            : 0.82f + params_.eventSize * 0.28f
                + params_.depth * 0.12f;
        const float sample = (planetary.sample * materialGain
                + voice.planetaryModalSample)
            * listenerGain;
        const float eventActivity = std::max(
            planetary.activity, voice.planetaryModalActivity);
        voice.eventLevel +=
            (eventActivity - voice.eventLevel) * 0.018f;
        voice.energy += (sample * sample - voice.energy) * 0.0012f;
        return std::isfinite(sample) ? sample : 0.0f;
    }

    float processVoice(uint32_t index)
    {
        auto& voice = voices_[index];
        if (isAmbiCryospherePlanetaryRegime(params_.regime)) {
            return processPlanetaryVoice(index);
        }
        if (voice.planetaryCryosphere.active()) {
            voice.planetaryCryosphere.reset();
        }
        voice.planetaryRouteActive = false;
        voice.planetarySample = 0.0f;
        voice.planetaryActivity = 0.0f;
        const bool aperiodicLattice = params_.regime
            == kAmbiCryosphereAperiodicLatticeRegime;
        const bool singingLake = params_.regime
            == kAmbiCryosphereSingingLakeRegime;
        if (!aperiodicLattice && !singingLake) {
            if (voice.planetaryModalBody.active()) {
                voice.planetaryModalBody.reset();
            }
        }
        voice.planetaryModalSample = 0.0f;
        voice.planetaryModalActivity = 0.0f;
        const auto& regime = kAmbiCryosphereRegimeProfiles[
            std::min<uint32_t>(params_.regime,
                kAmbiCryosphereRegimeCount - 1u)];
        const auto& substrate = kAmbiCryosphereSubstrateProfiles[
            std::min<uint32_t>(params_.environment,
                kAmbiCryosphereEnvironmentCount - 1u)];
        const float sr = static_cast<float>(sampleRate_);
        const float dt = 1.0f / sr;
        const float white = randomSigned(voice.rng);
        const float infraHz = 2.0f + (1.0f - params_.depth) * 5.0f;
        const float massHz = 24.0f + (1.0f - params_.scale) * 58.0f
            + (1.0f - params_.depth) * 22.0f
            + (1.0f - substrate.lowMass) * 16.0f;
        const float slowHz = 16.0f + params_.depth * 150.0f
            + substrate.lowMass * 72.0f;
        const float contactHz = 340.0f + params_.brightness * 5200.0f
            + substrate.hardness * 1400.0f;
        const float airHz = 2600.0f + params_.brightness * 10500.0f;
        voice.infraNoise += (white - voice.infraNoise)
            * (1.0f - std::exp(-kPi * 2.0f * infraHz / sr));
        voice.massNoise += (white - voice.massNoise)
            * (1.0f - std::exp(-kPi * 2.0f * massHz / sr));
        voice.slowNoise += (white - voice.slowNoise)
            * (1.0f - std::exp(-kPi * 2.0f * slowHz / sr));
        voice.contactNoise += (white - voice.contactNoise)
            * (1.0f - std::exp(-kPi * 2.0f * contactHz / sr));
        voice.airNoise += (white - voice.airNoise)
            * (1.0f - std::exp(-kPi * 2.0f * airHz / sr));
        const float contactBand = voice.contactNoise - voice.slowNoise;
        const float highBand = white - voice.airNoise;
        const float massBand = (voice.massNoise - voice.infraNoise)
            * (1.5f + params_.scale * 1.0f
                + params_.depth * 0.56f + substrate.lowMass * 0.34f);
        const float derivative = white - voice.previousWhite;
        voice.previousWhite = white;

        const float porePressure = params_.bubbles
            * (0.28f + substrate.poreWater * 0.92f)
            + voice.brineCharge * 0.34f;
        const float directedClock = std::clamp(0.002f
            + voice.scoreDrive + voice.scorePropagation * 0.38f
            + voice.scoreAftermath * params_.scoreRest * 0.08f,
            0.002f, 1.0f);
        const float scoreClock = aperiodicLattice
            ? directedClock
            : (1.0f - params_.scoreRest)
                + params_.scoreRest * std::clamp(0.012f
                    + voice.scoreDrive
                    + voice.scorePropagation * 0.38f, 0.012f, 1.0f);
        const float eventRate = params_.flow * scoreClock;
        const float freezeDrive = params_.water * regime.freezeGrowth
            * (1.02f + porePressure * 0.64f)
            * (0.42f + params_.density * 0.88f)
            * eventRate;
        const float mechanicalDrive = (params_.convergence * 0.78f
                + params_.current * regime.basalSlip * 0.42f)
            * (0.24f + params_.contact * 0.76f)
            * eventRate;
        StructuralFailureParams structureParams {};
        structureParams.drive = clamp(params_.surfaceLoad
            * voice.skinContactWeight
            * (0.54f
                + params_.convergence * 0.30f
                + regime.fracture * 0.28f)
            * eventRate
            * (0.08f + voice.scoreDrive * 0.66f
                + voice.scoreConsequence * 0.52f), 0.0f, 1.0f);
        structureParams.motion = clamp(0.12f + params_.current * 0.36f
            + params_.drops * 0.22f + std::fabs(params_.slope) * 0.18f,
            0.0f, 1.0f);
        structureParams.hierarchy = clamp(0.48f
            + params_.turbulence * 0.42f, 0.0f, 1.0f);
        structureParams.stiffness = clamp(0.28f
            + substrate.hardness * 0.54f
                - params_.water * 0.14f, 0.0f, 1.0f);
        structureParams.toughness = clamp(0.78f
            - params_.contact * 0.46f
                + substrate.damping * 0.16f, 0.0f, 1.0f);
        structureParams.damage = clamp(std::sqrt(eventRate) * 0.34f
            + params_.density * 0.24f + porePressure * 0.22f
            + regime.fracture * 0.20f
            + (1.0f - voice.plateIntegrity) * 0.42f
            + voice.scoreConsequence * 0.36f, 0.0f, 1.0f);
        structureParams.highDetail = params_.snap;
        structureParams.consequence = clamp(params_.plateFailure
            + voice.scoreConsequence * 0.56f, 0.0f, 1.0f);
        structureParams.mass = clamp(params_.scale * 0.58f
            + substrate.lowMass * 0.28f + params_.depth * 0.14f,
            0.0f, 1.0f);
        structureParams.mode = StructuralConsequence::PlateRupture;
        const auto structural = voice.structure.process(structureParams);
        if (structural.snapTriggered) ++structuralSnapEventCount_;
        if (structural.consequenceTriggered) ++plateFailureEventCount_;
        if (structural.consequenceTriggered
            && !aperiodicLattice && !singingLake) {
            voice.plateBodyEnvelope = std::max(
                voice.plateBodyEnvelope,
                (0.48f + structureParams.mass * 0.92f)
                    * (0.52f + params_.plateFailure * 0.48f));
            voice.plateBodyFrequencyHz = 20.0f
                + (1.0f - structureParams.mass) * 48.0f
                + randomUnit(voice.rng) * 12.0f;
            voice.plateBodyPhase = 0.0f;
        }
        if (structural.snapTriggered || structural.consequenceTriggered) {
            score_.exciteCascade(index,
                structural.consequenceTriggered ? 1.0f
                    : 0.42f + params_.turbulence * 0.38f);
            if (aperiodicLattice) {
                voice.quasicrystalIce.excite(
                    (structural.consequenceTriggered ? 1.0f : 0.46f)
                        * (0.44f + params_.surfaceLoad * 0.56f),
                    params_.scoreCascade);
            }
        }
        voice.plateIntegrity = std::max(0.0f,
            voice.plateIntegrity - dt
                * (structural.activity * (0.016f + params_.contact * 0.054f)
                    + voice.scoreDrive * params_.surfaceLoad * 0.012f));
        voice.crackExtent = std::min(1.0f,
            voice.crackExtent + dt
                * (structural.activity * 0.28f
                    + voice.scorePropagation * 0.12f));
        if (params_.regime == kAmbiCryosphereSingingLakeRegime
            && (structural.snapTriggered
                || structural.consequenceTriggered)) {
            const float strength = structural.consequenceTriggered
                ? 1.0f : 0.58f;
            voice.planetaryModalBody.excite(strength
                    * (0.36f + params_.surfaceLoad * 0.64f),
                params_.scoreCascade, structural.consequenceTriggered);
            ++singingIceEventCount_;
            ++modalExcitationEventCount_;
        }
        voice.strain += dt * (freezeDrive * 2.8f + mechanicalDrive * 1.7f)
            * (0.72f + std::fabs(voice.slowNoise) * 0.48f)
            * (0.08f + voice.scoreDrive * 0.74f
                + voice.scorePropagation * 0.34f);
        voice.strain = std::max(0.0f, voice.strain - dt
            * (0.006f + params_.damping * substrate.damping * 0.038f));
        if (voice.strain >= voice.fractureThreshold) {
            triggerFracture(voice, regime, substrate, false);
            voice.strain *= 0.12f + randomUnit(voice.rng) * 0.22f;
            voice.fractureThreshold = 0.028f
                + randomUnit(voice.rng)
                    * (0.16f + params_.scale * 0.34f);
        }

        if (voice.branchesRemaining > 0u) {
            voice.branchTimer -= dt;
            if (voice.branchTimer <= 0.0f) {
                triggerFracture(voice, regime, substrate, true);
                --voice.branchesRemaining;
                voice.branchTimer = 0.0015f + randomUnit(voice.rng)
                    * (0.008f + params_.turbulence * 0.034f);
            }
        }

        voice.slipLoad += dt * regime.basalSlip
            * (params_.current * 1.2f + params_.convergence * 1.8f
                + params_.shore * 1.4f)
            * (0.30f + substrate.roughness * 0.86f)
            * eventRate;
        if (voice.slipLoad >= voice.slipThreshold) {
            triggerSlip(voice, regime, substrate);
            voice.slipLoad *= 0.08f + randomUnit(voice.rng) * 0.18f;
            voice.slipThreshold = 0.030f + randomUnit(voice.rng)
                * (0.16f + params_.scale * 0.26f);
        }

        const float grainRate = eventRate * (0.15f + regime.granular
            * (params_.aeration * params_.aeration * 96.0f
                + params_.drops * params_.drops * 110.0f
                + params_.foam * 72.0f));
        if (!voice.grainScheduled) {
            voice.grainTimer = randomInterval(voice, grainRate);
            voice.scheduledGrainRateHz = grainRate;
            voice.grainScheduled = true;
        } else if (std::fabs(grainRate
                - voice.scheduledGrainRateHz) > 1.0e-6f) {
            voice.grainTimer *= voice.scheduledGrainRateHz
                / std::max(1.0e-5f, grainRate);
            voice.scheduledGrainRateHz = grainRate;
        }
        voice.grainTimer -= dt;
        if (voice.grainTimer <= 0.0f) {
            voice.grainTimer = randomInterval(voice, grainRate);
            voice.grainEnvelope = std::max(voice.grainEnvelope,
                (0.06f + randomUnit(voice.rng) * 0.54f)
                    * (0.18f + params_.aeration * 0.42f
                        + params_.drops * 0.64f + params_.foam * 0.38f)
                    * (0.36f + substrate.hardness * 0.72f));
        }

        const float calvingRate = eventRate
            * (0.015f + params_.splash * params_.splash
                * (0.18f + regime.calving * 0.82f));
        if (!voice.calvingScheduled) {
            voice.calvingTimer = randomInterval(voice, calvingRate);
            voice.scheduledCalvingRateHz = calvingRate;
            voice.calvingScheduled = true;
        } else if (std::fabs(calvingRate
                - voice.scheduledCalvingRateHz) > 1.0e-6f) {
            voice.calvingTimer *= voice.scheduledCalvingRateHz
                / std::max(1.0e-5f, calvingRate);
            voice.scheduledCalvingRateHz = calvingRate;
        }
        voice.calvingTimer -= dt;
        if (voice.calvingTimer <= 0.0f) {
            if (randomUnit(voice.rng) < params_.splash * regime.calving) {
                triggerCalving(voice, regime);
            }
            voice.calvingTimer = randomInterval(voice, calvingRate);
        }
        if (voice.impactCountdown >= 0.0f) {
            voice.impactCountdown -= dt;
            if (voice.impactCountdown < 0.0f) {
                const float impact = (0.28f + randomUnit(voice.rng) * 0.72f)
                    * (0.32f + params_.eventSize * 0.88f)
                    * (0.42f + regime.waterImpact * 0.86f);
                voice.impactEnvelope = std::max(
                    voice.impactEnvelope, impact);
                if (!aperiodicLattice && !singingLake) {
                    voice.plateBodyEnvelope = std::max(
                        voice.plateBodyEnvelope,
                        impact * (0.46f + params_.scale * 0.72f));
                    voice.plateBodyFrequencyHz = 18.0f
                        + (1.0f - params_.scale) * 38.0f
                        + randomUnit(voice.rng) * 11.0f;
                    voice.plateBodyPhase = 0.0f;
                }
                voice.cavitationEnvelope = std::max(
                    voice.cavitationEnvelope,
                    impact * (0.18f + regime.waterImpact * 0.58f));
                voice.forcePulse += randomSigned(voice.rng) * impact;
                ++impactEventCount_;
            }
        }
        voice.brineCharge = std::max(0.0f,
            voice.brineCharge - dt
                * (0.018f + params_.damping * 0.044f));

        voice.fractureEnvelope *= std::exp(-dt
            / (0.0014f + params_.eventSize * 0.013f));
        voice.macroEnvelope *= std::exp(-dt
            / (0.045f + params_.eventDecay * 0.72f
                + params_.scale * 0.34f));
        voice.slipEnvelope *= std::exp(-dt
            / (0.018f + params_.shore * 0.18f
                + regime.settling * 0.16f));
        voice.grainEnvelope *= std::exp(-dt
            / (0.0007f + params_.eventDecay * 0.012f
                + params_.foam * 0.010f));
        voice.impactEnvelope *= std::exp(-dt
            / (0.055f + params_.depth * 0.42f));
        voice.cavitationEnvelope *= std::exp(-dt
            / (0.012f + params_.bubbles * 0.18f));
        voice.tailEnvelope *= std::exp(-dt
            / (0.025f + params_.eventDecay * 0.42f
                + params_.resonance * 0.54f));
        voice.forcePulse *= std::exp(-dt
            / (0.0012f + substrate.damping * 0.007f));
        float plateBody = 0.0f;
        if (!aperiodicLattice && !singingLake) {
            voice.plateBodyPhase += kPi * 2.0f
                * voice.plateBodyFrequencyHz
                * (1.0f + voice.infraNoise * 0.06f) / sr;
            if (voice.plateBodyPhase >= kPi * 2.0f) {
                voice.plateBodyPhase -= kPi * 2.0f;
            }
            plateBody = voice.plateBodyEnvelope
                * (std::sin(voice.plateBodyPhase) * 0.72f
                    + massBand * 0.28f);
        } else {
            voice.plateBodyEnvelope = 0.0f;
            voice.plateBodyPhase = 0.0f;
        }
        voice.plateBodyEnvelope *= std::exp(-dt
            / (0.075f + params_.scale * 0.18f
                + params_.depth * 0.12f));

        const float creep = voice.slowNoise * voice.strain
            * (0.12f + params_.depth * 0.46f);
        const float fracture = voice.fractureEnvelope
            * (derivative * (0.42f + substrate.hardness * 0.58f)
                + highBand * 0.34f + voice.forcePulse * 0.46f);
        const float macro = voice.macroEnvelope
            * (massBand * (0.42f + substrate.lowMass * 0.52f)
                + voice.slowNoise * (0.82f + substrate.lowMass * 0.72f)
                + contactBand * 0.42f);
        const float slip = voice.slipEnvelope
            * (contactBand * (0.58f + substrate.roughness * 0.64f)
                + voice.slowNoise * 0.52f);
        const float grain = voice.grainEnvelope
            * (highBand * (0.62f + substrate.hardness * 0.44f)
                + derivative * 0.18f);
        const float grinding = params_.shore * regime.basalSlip
            * substrate.roughness * contactBand
            * (0.10f + std::fabs(voice.slowNoise) * 0.34f);
        const float snow = params_.foam * regime.granular
            * highBand * (0.04f + std::fabs(voice.slowNoise) * 0.18f)
            * (1.0f - substrate.hardness * 0.48f);
        const float impact = voice.impactEnvelope
            * (massBand * 0.72f + voice.slowNoise * 1.28f
                + contactBand * 0.36f
                + voice.forcePulse * 0.42f);
        const float cavitation = voice.cavitationEnvelope
            * (highBand * 0.62f + contactBand * 0.28f);
        const float diffuseTail = voice.tailEnvelope
            * (voice.slowNoise * 0.44f + contactBand * 0.26f)
            * (1.0f - params_.damping * 0.58f);
        if (singingLake) {
            const float lakeFlux = fracture * 0.72f
                + structural.crack * 0.34f
                + structural.snap * 0.48f
                + structural.rupture * 0.22f;
            const float lakeActivity = std::max(
                voice.fractureEnvelope, structural.activity);
            const auto singing = voice.planetaryModalBody.process(
                lakeFlux, lakeActivity);
            const float boundedModal = singing.sample
                / (1.0f + std::fabs(singing.sample) * 0.72f);
            voice.singingSample = boundedModal
                * (0.18f + params_.eventSize * 0.16f
                    + params_.brightness * 0.10f);
            voice.singingActivity = singing.activity;
        } else {
            voice.singingSample = 0.0f;
            voice.singingActivity = 0.0f;
        }
        if (aperiodicLattice) {
            QuasicrystalIceParams latticeParams {};
            const float scoredFlux = std::clamp(
                voice.scoreDrive + voice.scorePropagation * 0.72f
                    + voice.scoreConsequence * 0.42f
                    + voice.scoreAftermath * params_.scoreRest * 0.12f,
                0.0f, 1.0f);
            latticeParams.strainRate = params_.flow * scoredFlux
                * (0.26f + params_.water * 0.52f
                    + params_.convergence * 0.22f);
            latticeParams.phaseMobility = std::clamp(0.08f
                    + params_.current * 0.30f
                    + params_.scorePace * 0.26f
                    + (1.0f - params_.scoreMemory) * 0.30f,
                0.0f, 1.0f);
            latticeParams.frontSpeed = std::clamp(0.06f
                    + params_.scorePace * 0.70f
                    + params_.current * 0.18f,
                0.0f, 1.0f);
            latticeParams.branching = std::clamp(
                params_.scoreCascade * 0.62f
                    + params_.turbulence * 0.38f,
                0.0f, 1.0f);
            latticeParams.anisotropy = std::clamp(0.16f
                    + params_.convergence * 0.48f
                    + params_.width * 0.24f
                    + params_.resonance * 0.12f,
                0.0f, 1.0f);
            latticeParams.heterogeneity = std::clamp(0.10f
                    + params_.density * 0.38f
                    + params_.contact * 0.28f
                    + (1.0f - params_.scoreMemory) * 0.24f,
                0.0f, 1.0f);
            latticeParams.scale = params_.scale;
            latticeParams.brittleness = std::clamp(
                params_.contact * 0.48f + params_.snap * 0.34f
                    + params_.brightness * 0.18f,
                0.0f, 1.0f);
            latticeParams.damping = std::clamp(
                params_.damping * 0.78f + params_.scoreRest * 0.22f,
                0.0f, 1.0f);
            const auto lattice = voice.quasicrystalIce.process(
                latticeParams);
            voice.quasicrystalSample = lattice.sample;
            voice.quasicrystalActivity = lattice.activity;
            if (lattice.frontAdvanced) {
                ++quasicrystalFrontEventCount_;
                const float strength = std::clamp(0.18f
                        + lattice.activity * 0.58f
                        + params_.eventSize * 0.24f,
                    0.0f, 1.0f);
                voice.planetaryModalBody.excite(strength,
                    params_.scoreCascade,
                    voice.scoreConsequence > 0.35f);
                ++modalExcitationEventCount_;
            }
            if (lattice.avalancheAdvanced) {
                ++quasicrystalAvalancheEventCount_;
                const float strength = std::clamp(0.08f
                        + lattice.activity * 0.40f
                        + params_.snap * 0.16f,
                    0.0f, 0.76f);
                voice.planetaryModalBody.excite(strength,
                    std::clamp(params_.scoreCascade * 0.76f
                            + params_.turbulence * 0.24f,
                        0.0f, 1.0f));
                ++modalExcitationEventCount_;
            }
            const auto modal = voice.planetaryModalBody.process(
                lattice.sample, lattice.activity);
            const float boundedModal = modal.sample
                / (1.0f + std::fabs(modal.sample) * 0.72f);
            voice.planetaryModalSample = boundedModal
                * (0.16f + params_.density * 0.10f
                    + params_.eventSize * 0.06f);
            voice.planetaryModalActivity = modal.activity;
        } else {
            if (voice.quasicrystalIce.active()) {
                voice.quasicrystalIce.reset();
            }
            voice.quasicrystalSample = 0.0f;
            voice.quasicrystalActivity = 0.0f;
        }
        const float listenerGain = 1.0f
            + field_.listenerDrive(index) * 0.20f;
        const float entityBed = (1.0f - params_.scoreRest)
            + params_.scoreRest * std::clamp(0.018f
                + voice.scoreActivity + voice.scoreAftermath * 0.24f,
                0.018f, 1.0f);
        const float basalPressure = massBand
            * (params_.bubbles * (0.12f + substrate.poreWater * 0.34f)
                + params_.current * params_.depth * 0.18f)
            * (0.18f + voice.brineCharge * 0.52f);
        const float iceSample = (basalPressure * 0.38f
            + creep + fracture * 0.42f + macro * 0.52f
            + slip * 0.38f + grain * 0.32f + grinding * 0.46f
            + snow * 0.42f + impact * 0.58f + cavitation * 0.34f
            + diffuseTail * 0.30f)
            * (0.20f + params_.water * 0.46f
                + params_.density * 0.18f + params_.drops * 0.14f)
            * entityBed;
        const float structuralSample = (structural.flex * 0.14f
            + structural.crack * 0.62f + structural.snap * 0.76f
            + structural.rupture * 0.54f + structural.fall * 0.48f
            + structural.impact * 0.72f)
            * (0.24f + params_.surfaceLoad
                * clamp(voice.skinContactWeight, 0.25f, 2.25f) * 0.62f);
        const float massConsequence = (singingLake ? 0.0f : plateBody)
            * (0.09f + params_.eventSize * 0.135f
                + params_.scale * 0.09f + params_.depth * 0.06f);
        const float legacySample = iceSample + structuralSample
            + massConsequence
            + voice.singingSample;
        const float latticeSample = voice.quasicrystalSample
            * (0.72f + params_.eventSize * 0.24f
                + params_.density * 0.18f + params_.brightness * 0.10f);
        const float sample = (aperiodicLattice
                ? latticeSample + voice.planetaryModalSample
                : legacySample)
            * listenerGain;

        const float legacyEvent = std::max({ voice.fractureEnvelope,
            voice.macroEnvelope, voice.slipEnvelope, voice.grainEnvelope,
            voice.impactEnvelope, voice.cavitationEnvelope,
            structural.activity, voice.singingActivity });
        const float event = aperiodicLattice
            ? std::max(voice.quasicrystalActivity,
                voice.planetaryModalActivity)
            : legacyEvent;
        voice.eventLevel += (event - voice.eventLevel) * 0.018f;
        voice.energy += (sample * sample - voice.energy) * 0.0012f;
        return std::isfinite(sample) ? sample : 0.0f;
    }

    AmbiCryosphereParams params_ {};
    GeologicalField field_ {};
    EnvironmentalScore score_ {};
    std::array<AmbiCryosphereVoice, kAmbiCryosphereMaxVoices> voices_ {};
    std::array<float, kAmbiCryosphereMaxChannels> lastOutput_ {};
    std::array<float, kAmbiCryosphereMaxChannels> transitionTail_ {};
    double sampleRate_ = 48000.0;
    float smoothedOutputGain_ = dbToGain(-6.0f);
    float transitionFade_ = 1.0f;
    float iceLayerEnergy_ = 0.0f;
    float singingIceLayerEnergy_ = 0.0f;
    float quasicrystalLayerEnergy_ = 0.0f;
    float planetaryLayerEnergy_ = 0.0f;
    float planetaryModalLayerEnergy_ = 0.0f;
    uint64_t fractureEventCount_ = 0u;
    uint64_t slipEventCount_ = 0u;
    uint64_t calvingEventCount_ = 0u;
    uint64_t impactEventCount_ = 0u;
    uint64_t structuralSnapEventCount_ = 0u;
    uint64_t plateFailureEventCount_ = 0u;
    uint64_t singingIceEventCount_ = 0u;
    uint64_t quasicrystalFrontEventCount_ = 0u;
    uint64_t quasicrystalAvalancheEventCount_ = 0u;
    uint64_t tidalRiftEventCount_ = 0u;
    uint64_t hydrocarbonAvalancheEventCount_ = 0u;
    uint64_t brineBreakthroughEventCount_ = 0u;
    uint64_t modalExcitationEventCount_ = 0u;
    std::atomic<bool> transitionRequested_ { false };
};

} // namespace s3g
