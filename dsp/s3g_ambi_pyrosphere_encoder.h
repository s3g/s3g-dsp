#pragma once

#include "s3g_environmental_score.h"
#include "s3g_geological_field.h"
#include "s3g_planetary_modal_body.h"
#include "s3g_planetary_thermal.h"
#include "s3g_structural_failure.h"
#include "s3g_turbulent_flame_jet.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace s3g {

inline constexpr uint32_t kAmbiPyrosphereMaxOrder = 7u;
inline constexpr uint32_t kAmbiPyrosphereMaxChannels = 64u;
inline constexpr uint32_t kAmbiPyrosphereMaxVoices = 64u;
inline constexpr uint32_t kAmbiPyrospherePlaceCount = 7u;
inline constexpr uint32_t kAmbiPyrosphereLegacyMaterialCount = 14u;
inline constexpr uint32_t kAmbiPyrosphereMaterialSilicateCells = 14u;
inline constexpr uint32_t kAmbiPyrosphereMaterialSupercriticalFront = 15u;
inline constexpr uint32_t kAmbiPyrosphereMaterialSulfurSlugVents = 16u;
inline constexpr uint32_t kAmbiPyrosphereMaterialThermoelasticLattice = 17u;
inline constexpr uint32_t kAmbiPyrosphereMaterialCount = 18u;
inline constexpr float kAmbiPyrosphereMinIgnitionRateHz = 0.002f;
inline constexpr float kAmbiPyrosphereMaxIgnitionRateHz = 12.0f;
inline constexpr float kAmbiPyrosphereMinPlumeWanderHz = 0.001f;
inline constexpr float kAmbiPyrosphereMaxPlumeWanderHz = 0.5f;

inline float ambiPyrosphereTemporalScale(float ignitionRateHz)
{
    return std::sqrt(std::clamp(ignitionRateHz
        / kAmbiPyrosphereMaxIgnitionRateHz, 0.0f, 1.0f));
}

inline bool ambiPyrosphereIsPlanetaryMaterial(uint32_t materialMode)
{
    return materialMode >= kAmbiPyrosphereLegacyMaterialCount
        && materialMode < kAmbiPyrosphereMaterialCount;
}

inline PlanetaryThermalMode ambiPyrospherePlanetaryThermalMode(
    uint32_t materialMode)
{
    const uint32_t index = std::clamp<uint32_t>(materialMode,
        kAmbiPyrosphereMaterialSilicateCells,
        kAmbiPyrosphereMaterialThermoelasticLattice)
        - kAmbiPyrosphereMaterialSilicateCells;
    return static_cast<PlanetaryThermalMode>(index);
}

inline PlanetaryModalProfile ambiPyrospherePlanetaryModalProfile(
    uint32_t materialMode)
{
    switch (materialMode) {
    case kAmbiPyrosphereMaterialSupercriticalFront:
        return PlanetaryModalProfile::PyroSupercritical;
    case kAmbiPyrosphereMaterialSulfurSlugVents:
        return PlanetaryModalProfile::PyroVent;
    case kAmbiPyrosphereMaterialThermoelasticLattice:
        return PlanetaryModalProfile::PyroLattice;
    case kAmbiPyrosphereMaterialSilicateCells:
    default:
        return PlanetaryModalProfile::PyroSilicate;
    }
}

struct AmbiPyrosphereParams {
    uint32_t order = 3u;
    uint32_t voices = 24u;
    float wind = 0.55f;          // heat flux
    float gustRate = 0.08f;      // ignition / heat-release events per second
    float gustDepth = 0.48f;     // flare depth
    float turbulence = 0.36f;    // combustion turbulence
    float flutter = 0.28f;       // unstable heat release
    float material = 0.34f;      // available material / fuel load
    float air = 0.42f;
    float hiss = 0.34f;
    float spread = 0.40f;
    float deviation = 0.16f;
    uint32_t gustShape = 2u;
    float vectorRateHz = 0.024f;
    uint32_t materialMode = 0u;
    uint32_t gustEdge = 0u;
    float center = 0.38f;        // combustion core
    float sweep = 0.48f;         // spectral heat color
    float q = 0.42f;             // brittleness
    float shrill = 0.24f;        // high combustion detail
    float body = 0.52f;          // pyrolysis / low body
    float breath = 0.36f;        // oxygen
    float grit = 0.18f;          // char / fragment production
    float field = 0.70f;
    float motionRateHz = 0.024f;
    float motionFlow = 0.74f;
    float motionShear = 0.64f;
    float motionCurl = 1.0f;
    float motionUpdraft = 0.0f;
    float centerAzimuthDeg = 0.0f;
    float centerElevationDeg = 0.0f;
    float centerDistance = 1.0f;
    float spatialFollow = 0.90f;
    float outputGainDb = -6.0f;
    uint32_t place = 0u;
    float space = 0.14f;
    float environmentSize = 0.5f;
    float environmentDecay = 0.5f;
    float environmentDamping = 0.5f;
    AmbiFieldListenMode fieldListenMode = AmbiFieldListenMode::Off;
    float fieldListenAmount = 1.0f;
    AmbiFieldListenerResponse fieldListenResponse =
        AmbiFieldListenerResponse::Legacy;
    float particles = 0.0f;      // lofted fragments / embers
    float vortex = 0.0f;         // rotating plume field
    float pressure = 0.0f;       // pressure release / backdraft
    float structuralLoad = 0.18f; // wind and heat load on standing material
    float snap = 0.34f;          // high-frequency branch rupture detail
    float fall = 0.08f;          // hinge, descent, and ground impact
    float surfaceX = 0.5f;
    float surfaceY = 0.5f;
    float scorePace = 0.48f;       // slow-to-fast causal arc pacing
    float scoreOccupancy = 0.32f;  // simultaneous active entities
    float scoreCascade = 0.52f;    // spatial ignition / failure transfer
    float scoreMemory = 0.66f;     // persistence of macro-scene behaviour
    float scoreRest = 0.58f;       // duration and depth of quiet aftermaths
};

using AmbiPyrospherePoint = GeologicalFieldPoint;

inline AmbiEnvironmentProfileId ambiPyrosphereEnvironmentProfile(
    uint32_t place)
{
    constexpr std::array<AmbiEnvironmentProfileId,
        kAmbiPyrospherePlaceCount> profiles {
        AmbiEnvironmentProfileId::Open,
        AmbiEnvironmentProfileId::Canopy,
        AmbiEnvironmentProfileId::Porch,
        AmbiEnvironmentProfileId::Room,
        AmbiEnvironmentProfileId::Hangar,
        AmbiEnvironmentProfileId::Canyon,
        AmbiEnvironmentProfileId::Tunnel,
    };
    return profiles[std::min<uint32_t>(
        place, kAmbiPyrospherePlaceCount - 1u)];
}

struct AmbiPyrosphereMaterialProfile {
    float combustibility;
    float thermalMismatch;
    float fracture;
    float spall;
    float collapse;
    float moisture;
    float highColor;
    float damping;
};

// GAS, WICK/WAX, DUFF/PEAT, TIMBER, COAL, OIL, MASONRY, METAL,
// EMBERS, RESIN, GRASS, FOREST, ROCK/TALUS, PRESSURE JET,
// SILICATE CELLS, SUPERCRITICAL FRONT, SULFUR SLUG VENTS,
// THERMOELASTIC LATTICE.
inline constexpr std::array<AmbiPyrosphereMaterialProfile,
    kAmbiPyrosphereMaterialCount> kAmbiPyrosphereMaterialProfiles {{
        { 1.00f, 0.08f, 0.02f, 0.01f, 0.00f, 0.00f, 0.82f, 0.92f },
        { 0.72f, 0.22f, 0.12f, 0.02f, 0.00f, 0.08f, 0.58f, 0.78f },
        { 0.62f, 0.48f, 0.54f, 0.22f, 0.18f, 0.42f, 0.48f, 0.68f },
        { 0.78f, 0.68f, 0.88f, 0.46f, 0.58f, 0.28f, 0.40f, 0.50f },
        { 0.46f, 0.58f, 0.42f, 0.34f, 0.20f, 0.04f, 0.18f, 0.72f },
        { 0.94f, 0.12f, 0.08f, 0.01f, 0.00f, 0.00f, 0.76f, 0.84f },
        { 0.08f, 0.90f, 0.72f, 0.94f, 0.74f, 0.32f, 0.24f, 0.36f },
        { 0.04f, 0.98f, 0.36f, 0.20f, 0.62f, 0.00f, 0.66f, 0.26f },
        { 0.34f, 0.38f, 0.52f, 0.18f, 0.22f, 0.00f, 0.28f, 0.74f },
        { 0.86f, 0.62f, 1.00f, 0.38f, 0.22f, 0.18f, 0.72f, 0.42f },
        { 0.90f, 0.42f, 0.92f, 0.14f, 0.10f, 0.12f, 0.78f, 0.52f },
        { 0.76f, 0.82f, 0.86f, 0.58f, 0.76f, 0.36f, 0.42f, 0.40f },
        { 0.02f, 1.00f, 0.84f, 1.00f, 0.48f, 0.46f, 0.20f, 0.28f },
        { 1.00f, 0.04f, 0.01f, 0.00f, 0.00f, 0.00f, 0.78f, 0.88f },
        { 0.72f, 0.88f, 0.54f, 0.34f, 0.46f, 0.04f, 0.14f, 0.82f },
        { 1.00f, 0.18f, 0.04f, 0.01f, 0.00f, 0.00f, 0.34f, 0.96f },
        { 0.84f, 0.42f, 0.20f, 0.08f, 0.00f, 0.38f, 0.58f, 0.64f },
        { 0.30f, 1.00f, 0.98f, 0.86f, 0.62f, 0.00f, 0.36f, 0.22f },
    }};

struct AmbiPyrosphereVoice {
    uint32_t rng = 1u;
    float infraNoise = 0.0f;
    float subNoise = 0.0f;
    float slowNoise = 0.0f;
    float midNoise = 0.0f;
    float airNoise = 0.0f;
    float previousWhite = 0.0f;
    float heat = 0.0f;
    float heatTarget = 0.0f;
    float heatTimer = 0.0f;
    float scheduledHeatRateHz = 0.0f;
    bool heatScheduled = false;
    float thermalStress = 0.0f;
    float fractureThreshold = 0.1f;
    float branchTimer = 0.0f;
    uint32_t branchesRemaining = 0u;
    float fragmentTimer = 0.0f;
    float scheduledFragmentRateHz = 0.0f;
    bool fragmentScheduled = false;
    float fractureEnvelope = 0.0f;
    float spallEnvelope = 0.0f;
    float debrisEnvelope = 0.0f;
    float fragmentEnvelope = 0.0f;
    float pressureEnvelope = 0.0f;
    float massEnvelope = 0.0f;
    float massPhase = 0.0f;
    float massFrequencyHz = 38.0f;
    float forcePulse = 0.0f;
    float eventLevel = 0.0f;
    float energy = 0.0f;
    float jetSample = 0.0f;
    float jetActivity = 0.0f;
    float planetarySample = 0.0f;
    float planetaryActivity = 0.0f;
    float planetaryModalSample = 0.0f;
    float planetaryModalActivity = 0.0f;
    float fuelRemaining = 1.0f;
    float structuralIntegrity = 1.0f;
    float emberCharge = 0.0f;
    float scoreActivity = 0.0f;
    float scoreDrive = 0.0f;
    float scorePropagation = 0.0f;
    float scoreConsequence = 0.0f;
    float scoreAftermath = 0.0f;
    StructuralFailureModel structure {};
    TurbulentFlameJetModel flameJet {};
    PlanetaryModalBody planetaryModal {};
    PlanetaryThermalModel planetaryThermal {};
};

class AmbiPyrosphereEncoder {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1000.0, sampleRate);
        field_.prepare(sampleRate_);
        score_.prepare(sampleRate_, 0x5079726fu);
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
        combustionLayerEnergy_ = 0.0f;
        jetLayerEnergy_ = 0.0f;
        planetaryLayerEnergy_ = 0.0f;
        planetaryModalLayerEnergy_ = 0.0f;
        geologicalEventCount_ = 0u;
        spallEventCount_ = 0u;
        collapseEventCount_ = 0u;
        ignitionEventCount_ = 0u;
        structuralSnapEventCount_ = 0u;
        fallEventCount_ = 0u;
        planetaryBlisterEventCount_ = 0u;
        planetaryFrontEventCount_ = 0u;
        planetaryVentEventCount_ = 0u;
        planetaryLatticeEventCount_ = 0u;
        modalExcitationEventCount_ = 0u;
    }

    void setParams(AmbiPyrosphereParams params)
    {
        const uint32_t previousMaterial = params_.materialMode;
        sanitize(params);
        params_ = params;
        GeologicalFieldParams fieldParams {};
        fieldParams.voices = params.voices;
        fieldParams.spread = params.spread;
        fieldParams.deviation = clamp(params.deviation
            * (0.48f + params.field * 0.72f), 0.0f, 1.0f);
        fieldParams.motionRateHz = clamp(params.motionRateHz
            + params.vectorRateHz, 0.001f, 2.0f);
        fieldParams.transport = params.motionFlow
            * (0.42f + params.field * 0.58f);
        fieldParams.shear = params.motionShear;
        fieldParams.curl = clamp(params.motionCurl + params.vortex * 0.62f,
            0.0f, 1.5f);
        fieldParams.vertical = params.motionUpdraft;
        fieldParams.centerAzimuthDeg = params.centerAzimuthDeg;
        fieldParams.centerElevationDeg = params.centerElevationDeg;
        fieldParams.centerDistance = params.centerDistance;
        fieldParams.spatialFollow = params.spatialFollow;
        fieldParams.environmentProfile =
            ambiPyrosphereEnvironmentProfile(params.place);
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
        score_.setRangeExpansion(
            ambiPyrosphereIsPlanetaryMaterial(params.materialMode)
                ? 1.0f : 0.0f);
        const auto modalParams = pyrosphereModalParams(params);
        const bool modalTopologyChanged =
            previousMaterial != params.materialMode
            && (ambiPyrosphereIsPlanetaryMaterial(previousMaterial)
                || ambiPyrosphereIsPlanetaryMaterial(params.materialMode));
        for (auto& voice : voices_) {
            if (modalTopologyChanged) {
                voice.planetaryModal.reset();
                voice.planetaryModalSample = 0.0f;
                voice.planetaryModalActivity = 0.0f;
            }
            voice.planetaryModal.setParams(modalParams);
        }
    }

    AmbiPyrosphereParams params() const { return params_; }

    void setParameterSurfaceGlideMs(float glideMs)
    {
        field_.setParameterSurfaceGlideMs(glideMs);
    }

    void setParameterSurfaceVoiceMembership(
        const std::array<float, kAmbiPyrosphereMaxVoices>& membership)
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

    void beginTransition()
    {
        transitionRequested_.store(true, std::memory_order_release);
    }

    float voiceEnergy(uint32_t voice) const
    {
        return voices_[std::min<uint32_t>(
            voice, kAmbiPyrosphereMaxVoices - 1u)].energy;
    }

    AmbiPyrospherePoint voicePoint(uint32_t voice) const
    {
        return field_.point(voice);
    }

    float voiceGustLevel(uint32_t voice) const
    {
        return voices_[std::min<uint32_t>(
            voice, kAmbiPyrosphereMaxVoices - 1u)].eventLevel;
    }

    float combustionLayerEnergy() const { return combustionLayerEnergy_; }
    float jetLayerEnergy() const { return jetLayerEnergy_; }
    float planetaryLayerEnergy() const { return planetaryLayerEnergy_; }
    float planetaryModalLayerEnergy() const
    {
        return planetaryModalLayerEnergy_;
    }
    uint64_t geologicalEventCount() const { return geologicalEventCount_; }
    uint64_t spallEventCount() const { return spallEventCount_; }
    uint64_t collapseEventCount() const { return collapseEventCount_; }
    uint64_t ignitionEventCount() const { return ignitionEventCount_; }
    uint64_t structuralSnapEventCount() const
    {
        return structuralSnapEventCount_;
    }
    uint64_t fallEventCount() const { return fallEventCount_; }
    uint64_t planetaryBlisterEventCount() const
    {
        return planetaryBlisterEventCount_;
    }
    uint64_t planetaryFrontEventCount() const
    {
        return planetaryFrontEventCount_;
    }
    uint64_t planetaryVentEventCount() const
    {
        return planetaryVentEventCount_;
    }
    uint64_t planetaryLatticeEventCount() const
    {
        return planetaryLatticeEventCount_;
    }
    uint64_t modalExcitationEventCount() const
    {
        return modalExcitationEventCount_;
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
            outputChannels, kAmbiPyrosphereMaxChannels);
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
        const float voiceNorm = std::pow(field_.voiceMass(), 0.46f);
        constexpr uint32_t kControlFrames = 16u;
        double layerEnergy = 0.0;
        double jetEnergy = 0.0;
        double planetaryEnergy = 0.0;
        double planetaryModalEnergy = 0.0;

        for (uint32_t chunkStart = 0u; chunkStart < frames;
            chunkStart += kControlFrames) {
            const uint32_t chunkFrames = std::min<uint32_t>(
                kControlFrames, frames - chunkStart);
            const float dt = static_cast<float>(chunkFrames)
                / static_cast<float>(sampleRate_);
            std::array<float, kAmbiPyrosphereMaxVoices> activity {};
            for (uint32_t voice = 0u; voice < voiceCount; ++voice) {
                activity[voice] = voices_[voice].eventLevel;
            }
            field_.update(dt, activity.data());
            for (uint32_t voice = 0u; voice < voiceCount; ++voice) {
                const auto direction = field_.direction(voice);
                score_.setEntityPosition(voice,
                    direction.x, direction.y, direction.z);
            }
            score_.update(dt, voiceCount);
            applyScoreDirectives(voiceCount);
            const float targetGain = dbToGain(params_.outputGainDb)
                * 1.18f / voiceNorm;

            for (uint32_t frame = chunkStart;
                frame < chunkStart + chunkFrames; ++frame) {
                smoothedOutputGain_ +=
                    (targetGain - smoothedOutputGain_) * 0.0015f;
                field_.beginEnvironmentFrame();
                std::array<float, kAmbiPyrosphereMaxChannels>
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
                    jetEnergy += static_cast<double>(voices_[voice].jetSample)
                        * voices_[voice].jetSample;
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
                        0.62f + params_.body * 0.32f
                            + voices_[voice].eventLevel * 0.58f);
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
            1.0f, static_cast<float>(sampleRate_) * 0.026f);
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
        combustionLayerEnergy_ +=
            (measured - combustionLayerEnergy_) * 0.24f;
        const float measuredJet = static_cast<float>(jetEnergy
            / std::max<uint32_t>(1u,
                frames * std::max<uint32_t>(1u, voiceCount)));
        jetLayerEnergy_ += (measuredJet - jetLayerEnergy_) * 0.24f;
        const float measuredPlanetary = static_cast<float>(planetaryEnergy
            / std::max<uint32_t>(1u,
                frames * std::max<uint32_t>(1u, voiceCount)));
        planetaryLayerEnergy_ +=
            (measuredPlanetary - planetaryLayerEnergy_) * 0.24f;
        const float measuredPlanetaryModal = static_cast<float>(
            planetaryModalEnergy
            / std::max<uint32_t>(1u,
                frames * std::max<uint32_t>(1u, voiceCount)));
        planetaryModalLayerEnergy_ +=
            (measuredPlanetaryModal - planetaryModalLayerEnergy_) * 0.24f;
    }

private:
    static float finiteClamp(float value, float fallback, float low, float high)
    {
        return clamp(std::isfinite(value) ? value : fallback, low, high);
    }

    static PlanetaryModalParams pyrosphereModalParams(
        const AmbiPyrosphereParams& params)
    {
        const auto& material = kAmbiPyrosphereMaterialProfiles[
            std::min<uint32_t>(params.materialMode,
                kAmbiPyrosphereMaterialCount - 1u)];
        const float instability = clamp(params.turbulence * 0.62f
            + params.flutter * 0.38f, 0.0f, 1.0f);
        const float detail = clamp(params.shrill * 0.48f
            + params.air * 0.30f + params.grit * 0.22f, 0.0f, 1.0f);

        PlanetaryModalParams modal {};
        modal.profile = ambiPyrospherePlanetaryModalProfile(
            params.materialMode);
        modal.scale = clamp(params.body * 0.64f
            + params.material * 0.36f, 0.0f, 1.0f);
        modal.damping = clamp(material.damping * 0.62f
            + params.environmentDamping * 0.38f, 0.0f, 1.0f);
        switch (modal.profile) {
        case PlanetaryModalProfile::PyroSilicate:
            modal.amount = 0.22f;
            modal.irregularity = clamp(instability * 0.55f
                    + params.deviation * 0.45f,
                0.0f, 1.0f);
            modal.coupling = clamp(params.scoreCascade * 0.45f
                    + params.vortex * 0.55f,
                0.0f, 1.0f);
            modal.brightness = clamp(detail * 0.70f
                    + params.q * 0.30f,
                0.0f, 1.0f);
            break;
        case PlanetaryModalProfile::PyroSupercritical:
            modal.amount = 0.16f;
            modal.irregularity = clamp(instability * 0.72f
                    + params.deviation * 0.28f,
                0.0f, 1.0f);
            modal.coupling = clamp(params.scoreCascade * 0.65f
                    + params.motionFlow * 0.35f,
                0.0f, 1.0f);
            modal.brightness = clamp(detail * 0.75f
                    + instability * 0.25f,
                0.0f, 1.0f);
            break;
        case PlanetaryModalProfile::PyroVent:
            modal.amount = 0.24f;
            modal.irregularity = clamp(instability * 0.55f
                    + params.deviation * 0.45f,
                0.0f, 1.0f);
            modal.coupling = clamp(params.scoreCascade * 0.65f
                    + params.pressure * 0.35f,
                0.0f, 1.0f);
            modal.brightness = clamp(detail * 0.60f
                    + params.pressure * 0.40f,
                0.0f, 1.0f);
            break;
        case PlanetaryModalProfile::PyroLattice:
            modal.amount = 0.34f;
            modal.irregularity = clamp(instability * 0.45f
                    + params.deviation * 0.55f,
                0.0f, 1.0f);
            modal.coupling = params.scoreCascade;
            modal.brightness = clamp(detail * 0.40f
                    + params.q * 0.60f,
                0.0f, 1.0f);
            break;
        default:
            // Legacy materials never render this body. Zero amount keeps an
            // accidentally processed helper inaudible without changing state.
            modal.amount = 0.0f;
            break;
        }
        if (!ambiPyrosphereIsPlanetaryMaterial(params.materialMode)) {
            modal.amount = 0.0f;
        }
        return modal;
    }

    void sanitize(AmbiPyrosphereParams& params) const
    {
        params.order = std::clamp<uint32_t>(
            params.order, 1u, kAmbiPyrosphereMaxOrder);
        params.voices = std::clamp<uint32_t>(
            params.voices, 1u, kAmbiPyrosphereMaxVoices);
#define S3G_PYRO_CLAMP(member, low, high) \
        params.member = finiteClamp(params.member, params_.member, low, high)
        S3G_PYRO_CLAMP(wind, 0.0f, 1.0f);
        S3G_PYRO_CLAMP(gustRate, kAmbiPyrosphereMinIgnitionRateHz,
            kAmbiPyrosphereMaxIgnitionRateHz);
        S3G_PYRO_CLAMP(gustDepth, 0.0f, 1.0f);
        S3G_PYRO_CLAMP(turbulence, 0.0f, 1.0f);
        S3G_PYRO_CLAMP(flutter, 0.0f, 1.0f);
        S3G_PYRO_CLAMP(material, 0.0f, 1.0f);
        S3G_PYRO_CLAMP(air, 0.0f, 1.0f);
        S3G_PYRO_CLAMP(hiss, 0.0f, 1.0f);
        S3G_PYRO_CLAMP(spread, 0.0f, 1.0f);
        S3G_PYRO_CLAMP(deviation, 0.0f, 1.0f);
        params.gustShape = std::min<uint32_t>(params.gustShape, 5u);
        S3G_PYRO_CLAMP(vectorRateHz, kAmbiPyrosphereMinPlumeWanderHz,
            kAmbiPyrosphereMaxPlumeWanderHz);
        params.materialMode = std::min<uint32_t>(
            params.materialMode, kAmbiPyrosphereMaterialCount - 1u);
        params.gustEdge = std::min<uint32_t>(params.gustEdge, 2u);
        S3G_PYRO_CLAMP(center, 0.0f, 1.0f);
        S3G_PYRO_CLAMP(sweep, 0.0f, 1.0f);
        S3G_PYRO_CLAMP(q, 0.0f, 1.0f);
        S3G_PYRO_CLAMP(shrill, 0.0f, 1.0f);
        S3G_PYRO_CLAMP(body, 0.0f, 1.0f);
        S3G_PYRO_CLAMP(breath, 0.0f, 1.0f);
        S3G_PYRO_CLAMP(grit, 0.0f, 1.0f);
        S3G_PYRO_CLAMP(field, 0.0f, 1.0f);
        S3G_PYRO_CLAMP(motionRateHz, 0.001f, 2.0f);
        S3G_PYRO_CLAMP(motionFlow, 0.0f, 1.5f);
        S3G_PYRO_CLAMP(motionShear, 0.0f, 1.5f);
        S3G_PYRO_CLAMP(motionCurl, 0.0f, 1.5f);
        S3G_PYRO_CLAMP(motionUpdraft, 0.0f, 1.0f);
        S3G_PYRO_CLAMP(centerAzimuthDeg, -180.0f, 180.0f);
        S3G_PYRO_CLAMP(centerElevationDeg, -90.0f, 90.0f);
        S3G_PYRO_CLAMP(centerDistance, 0.15f, 2.0f);
        S3G_PYRO_CLAMP(spatialFollow, 0.0f, 1.0f);
        S3G_PYRO_CLAMP(outputGainDb, -60.0f, 12.0f);
        params.place = std::min<uint32_t>(
            params.place, kAmbiPyrospherePlaceCount - 1u);
        S3G_PYRO_CLAMP(space, 0.0f, 1.0f);
        S3G_PYRO_CLAMP(environmentSize, 0.0f, 1.0f);
        S3G_PYRO_CLAMP(environmentDecay, 0.0f, 1.0f);
        S3G_PYRO_CLAMP(environmentDamping, 0.0f, 1.0f);
        params.fieldListenMode = sanitizeAmbiFieldListenMode(
            params.fieldListenMode);
        S3G_PYRO_CLAMP(fieldListenAmount, 0.0f, 1.0f);
        params.fieldListenResponse = sanitizeAmbiFieldListenerResponse(
            params.fieldListenResponse);
        S3G_PYRO_CLAMP(particles, 0.0f, 1.0f);
        S3G_PYRO_CLAMP(vortex, 0.0f, 1.0f);
        S3G_PYRO_CLAMP(pressure, 0.0f, 1.0f);
        S3G_PYRO_CLAMP(structuralLoad, 0.0f, 1.0f);
        S3G_PYRO_CLAMP(snap, 0.0f, 1.0f);
        S3G_PYRO_CLAMP(fall, 0.0f, 1.0f);
        S3G_PYRO_CLAMP(surfaceX, 0.0f, 1.0f);
        S3G_PYRO_CLAMP(surfaceY, 0.0f, 1.0f);
        S3G_PYRO_CLAMP(scorePace, 0.0f, 1.0f);
        S3G_PYRO_CLAMP(scoreOccupancy, 0.0f, 1.0f);
        S3G_PYRO_CLAMP(scoreCascade, 0.0f, 1.0f);
        S3G_PYRO_CLAMP(scoreMemory, 0.0f, 1.0f);
        S3G_PYRO_CLAMP(scoreRest, 0.0f, 1.0f);
#undef S3G_PYRO_CLAMP
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

    float randomInterval(AmbiPyrosphereVoice& voice, float rate)
    {
        if (rate <= 1.0e-5f) return 3600.0f;
        return -std::log(std::max(0.0001f,
            1.0f - randomUnit(voice.rng))) / rate;
    }

    void initializeVoice(uint32_t index)
    {
        auto& voice = voices_[index];
        voice = {};
        voice.rng = 0xf17e5eedu + index * 0x9e3779b9u;
        voice.heat = randomUnit(voice.rng) * 0.08f;
        voice.heatTarget = voice.heat;
        voice.heatTimer = 0.0f;
        voice.thermalStress = 0.0f;
        voice.fractureThreshold = 0.035f
            + randomUnit(voice.rng) * 0.22f;
        voice.fragmentTimer = 0.0f;
        voice.fuelRemaining = 0.58f + randomUnit(voice.rng) * 0.42f;
        voice.structuralIntegrity = 0.74f
            + randomUnit(voice.rng) * 0.26f;
        voice.emberCharge = 0.0f;
        voice.structure.prepare(sampleRate_,
            0xb12a4c5du + index * 0x85ebca6bu);
        voice.flameJet.prepare(sampleRate_);
        voice.planetaryModal.prepare(sampleRate_,
            0x6d6f6461u + index * 0x165667b1u);
        voice.planetaryModal.setParams(
            pyrosphereModalParams(params_));
        voice.planetaryThermal.prepare(sampleRate_,
            0x706c6e74u + index * 0x27d4eb2du);
    }

    void applyScoreDirectives(uint32_t voiceCount)
    {
        for (uint32_t index = 0u; index < voiceCount; ++index) {
            auto& voice = voices_[index];
            const auto directive = score_.directive(index);
            voice.scoreActivity = directive.activity;
            voice.scoreDrive = directive.drive;
            voice.scorePropagation = directive.propagation;
            voice.scoreConsequence = directive.consequence;
            voice.scoreAftermath = directive.aftermath;
            const bool planetary =
                ambiPyrosphereIsPlanetaryMaterial(params_.materialMode);
            const auto planetaryMode = ambiPyrospherePlanetaryThermalMode(
                params_.materialMode);
            if (directive.arcStarted) {
                voice.fuelRemaining = 0.56f
                    + randomUnit(voice.rng) * 0.44f;
                voice.structuralIntegrity = 0.74f
                    + randomUnit(voice.rng) * 0.26f;
                voice.emberCharge = 0.0f;
                voice.thermalStress *= 0.18f;
                if (planetary) {
                    voice.planetaryThermal.excite(planetaryMode,
                        0.12f + directive.resource * 0.18f,
                        params_.scoreCascade, false);
                }
            }
            if (directive.onset || directive.cascadeArrival) {
                const float transfer = directive.cascadeArrival
                    ? 0.58f : 1.0f;
                const float ignition = transfer
                    * (0.34f + params_.wind * 0.54f
                        + params_.gustDepth * 0.32f);
                voice.heatTarget = std::max(voice.heatTarget, ignition);
                voice.pressureEnvelope = std::max(
                    voice.pressureEnvelope,
                    ignition * params_.pressure * 0.46f);
                voice.fragmentEnvelope = std::max(
                    voice.fragmentEnvelope,
                    ignition * params_.particles * 0.22f);
                voice.emberCharge = std::max(voice.emberCharge,
                    directive.cascadeArrival ? 0.48f : 0.24f);
                if (planetary) {
                    voice.planetaryThermal.excite(planetaryMode,
                        ignition, params_.scoreCascade,
                        false);
                }
                ++ignitionEventCount_;
            }
            if (directive.consequenceStarted) {
                const float force = std::clamp(0.54f
                        + directive.consequence * 0.46f,
                    0.0f, 1.0f);
                const bool standingMaterial = params_.materialMode == 2u
                    || params_.materialMode == 3u
                    || params_.materialMode == 9u
                    || params_.materialMode == 10u
                    || params_.materialMode == 11u;
                if (standingMaterial
                    && std::max({ params_.structuralLoad,
                            params_.snap, params_.fall }) > 0.001f) {
                    voice.structure.excite(force, true);
                }
                voice.thermalStress = std::max(voice.thermalStress,
                    voice.fractureThreshold * (1.02f + force * 0.18f));
                voice.structuralIntegrity *= 0.34f;
                if (planetary) {
                    voice.planetaryThermal.excite(planetaryMode,
                        force, params_.scoreCascade, true);
                }
            }
        }
    }

    void triggerFracture(AmbiPyrosphereVoice& voice,
        const AmbiPyrosphereMaterialProfile& material, bool branch)
    {
        const float listener = field_.listenerDrive(
            static_cast<uint32_t>(&voice - voices_.data()));
        const float strength = (0.14f + randomUnit(voice.rng) * 0.86f)
            * (0.18f + params_.material * 0.46f
                + params_.q * 0.34f + material.fracture * 0.52f)
            * (1.0f + listener * 0.18f)
            * (branch ? 0.54f : 1.0f);
        voice.fractureEnvelope = std::max(
            voice.fractureEnvelope, strength);
        voice.forcePulse += randomSigned(voice.rng) * strength;
        ++geologicalEventCount_;

        const float spallChance = material.spall
            * (0.16f + params_.wind * 0.48f + params_.grit * 0.42f);
        if (!branch && randomUnit(voice.rng) < spallChance) {
            voice.spallEnvelope = std::max(voice.spallEnvelope,
                strength * (0.44f + material.thermalMismatch * 0.72f));
            ++spallEventCount_;
        }
        const float collapseChance = material.collapse
            * (0.08f + params_.material * 0.40f + params_.pressure * 0.32f);
        if (!branch && randomUnit(voice.rng) < collapseChance) {
            voice.debrisEnvelope = std::max(voice.debrisEnvelope,
                strength * (0.42f + params_.body * 0.82f));
            voice.massEnvelope = std::max(voice.massEnvelope,
                strength * (0.38f + params_.body * 0.72f));
            voice.massFrequencyHz = 23.0f
                + (1.0f - params_.body) * 42.0f
                + randomUnit(voice.rng) * 16.0f;
            voice.massPhase = 0.0f;
            ++collapseEventCount_;
        }
        if (!branch) {
            voice.branchesRemaining = static_cast<uint32_t>(
                randomUnit(voice.rng)
                * (1.0f + params_.grit * 5.0f
                    + material.fracture * 4.0f));
            voice.branchTimer = 0.002f + randomUnit(voice.rng)
                * (0.010f + params_.gustDepth * 0.032f);
        }
    }

    float processPlanetaryVoice(uint32_t index, AmbiPyrosphereVoice& voice,
        const AmbiPyrosphereMaterialProfile& material, float unstableHeat)
    {
        PlanetaryThermalParams planetaryParams {};
        planetaryParams.mode = ambiPyrospherePlanetaryThermalMode(
            params_.materialMode);
        planetaryParams.heat = unstableHeat / 1.8f;
        planetaryParams.flux = params_.wind;
        planetaryParams.activity = clamp(voice.scoreActivity
            + voice.scoreAftermath * 0.24f, 0.0f, 1.0f);
        planetaryParams.drive = clamp(voice.scoreDrive, 0.0f, 1.0f);
        planetaryParams.propagation = clamp(
            voice.scorePropagation, 0.0f, 1.0f);
        planetaryParams.consequence = clamp(
            voice.scoreConsequence, 0.0f, 1.0f);
        planetaryParams.instability = clamp(params_.turbulence * 0.62f
            + params_.flutter * 0.38f, 0.0f, 1.0f);
        planetaryParams.pressure = params_.pressure;
        planetaryParams.density = params_.body;
        planetaryParams.brittleness = params_.q;
        planetaryParams.damping = clamp(material.damping * 0.62f
            + params_.environmentDamping * 0.38f, 0.0f, 1.0f);
        planetaryParams.branching = params_.scoreCascade;
        planetaryParams.scale = clamp(params_.body * 0.64f
            + params_.material * 0.36f, 0.0f, 1.0f);
        planetaryParams.detail = clamp(params_.shrill * 0.48f
            + params_.air * 0.30f + params_.grit * 0.22f, 0.0f, 1.0f);
        const auto planetary = voice.planetaryThermal.process(
            planetaryParams);

        // Score gestures enter the thermal reservoir above. Modal energy is
        // admitted only after that reservoir reports a physical release, so
        // the resonances retain the planetary process's causal traversal.
        const float releaseStrength = clamp(0.14f
                + planetary.activity * 0.58f
                + std::min(1.0f, std::fabs(planetary.sample) * 3.2f) * 0.28f,
            0.0f, 1.0f);
        const auto exciteModal = [&](float strength, float cascade,
                                     bool consequence) {
            voice.planetaryModal.excite(
                clamp(strength, 0.0f, 1.0f),
                clamp(cascade, 0.0f, 1.0f), consequence);
            ++modalExcitationEventCount_;
        };

        if (planetary.blisterReleased) {
            ++planetaryBlisterEventCount_;
            score_.exciteCascade(index, 0.30f + params_.pressure * 0.34f);
            exciteModal(releaseStrength
                    * (0.58f + planetaryParams.pressure * 0.42f),
                params_.scoreCascade * (0.40f
                    + planetaryParams.pressure * 0.60f), false);
        }
        if (planetary.frontAdvanced) {
            ++planetaryFrontEventCount_;
            score_.exciteCascade(index,
                0.24f + params_.scoreCascade * 0.46f);
            exciteModal(releaseStrength
                    * (0.50f + planetaryParams.propagation * 0.50f),
                params_.scoreCascade, false);
        }
        if (planetary.slugReleased) {
            ++planetaryVentEventCount_;
            score_.exciteCascade(index, 0.34f + params_.pressure * 0.52f);
            exciteModal(releaseStrength
                    * (0.55f + planetaryParams.pressure * 0.45f),
                params_.scoreCascade * 0.60f
                    + planetaryParams.pressure * 0.40f,
                false);
        }
        if (planetary.latticeFrontAdvanced) {
            ++planetaryLatticeEventCount_;
            score_.exciteCascade(index,
                0.42f + params_.scoreCascade * 0.54f);
            exciteModal(releaseStrength
                    * (0.52f + planetaryParams.brittleness * 0.48f),
                params_.scoreCascade, false);
        }
        if (planetary.avalancheAdvanced) {
            ++planetaryLatticeEventCount_;
            exciteModal(releaseStrength
                    * (0.60f + planetaryParams.branching * 0.40f),
                params_.scoreCascade, true);
        }

        const auto modal = voice.planetaryModal.process(
            planetary.sample, planetary.activity);
        const float modalSample = modal.sample
            / (1.0f + std::fabs(modal.sample) * 0.72f);

        // Planetary materials replace every legacy source. Clearing dormant
        // envelopes also prevents a prior terrestrial scene from reappearing
        // when a SURF cell crosses the discrete material boundary.
        voice.jetSample = 0.0f;
        voice.jetActivity = 0.0f;
        voice.massEnvelope = 0.0f;
        voice.fractureEnvelope = 0.0f;
        voice.spallEnvelope = 0.0f;
        voice.debrisEnvelope = 0.0f;
        voice.fragmentEnvelope = 0.0f;
        voice.pressureEnvelope = 0.0f;
        voice.forcePulse = 0.0f;
        voice.branchesRemaining = 0u;
        voice.planetaryModalSample = modalSample;
        voice.planetaryModalActivity = modal.activity;
        voice.planetarySample = planetary.sample + modalSample;
        voice.planetaryActivity = planetary.activity;

        // Expanded scores own the empty intervals for planetary processes;
        // unlike the legacy combustion bed, a resting entity is truly silent.
        const float entityBed = clamp(voice.scoreActivity
            + voice.scoreAftermath * 0.24f, 0.0f, 1.0f);
        const float listenerGain = 1.0f
            + field_.listenerDrive(index) * 0.22f;
        const float sample = voice.planetarySample
            * (0.28f + params_.wind * 0.72f)
            * entityBed * listenerGain;
        const float combinedActivity = std::max(
            planetary.activity, modal.activity);
        voice.eventLevel +=
            (combinedActivity - voice.eventLevel) * 0.018f;
        voice.energy += (sample * sample - voice.energy) * 0.0012f;
        return std::isfinite(sample) ? sample : 0.0f;
    }

    float processVoice(uint32_t index)
    {
        auto& voice = voices_[index];
        const auto& material = kAmbiPyrosphereMaterialProfiles[
            std::min<uint32_t>(params_.materialMode,
                kAmbiPyrosphereMaterialCount - 1u)];
        const bool planetaryMaterial =
            ambiPyrosphereIsPlanetaryMaterial(params_.materialMode);
        const auto planetaryMode = ambiPyrospherePlanetaryThermalMode(
            params_.materialMode);
        const float sr = static_cast<float>(sampleRate_);
        const float dt = 1.0f / sr;
        const float white = randomSigned(voice.rng);
        const float infraHz = 2.4f + (1.0f - params_.body) * 5.6f
            + (1.0f - material.collapse) * 2.0f;
        const float subHz = 14.0f + (1.0f - params_.body) * 52.0f
            + material.collapse * 12.0f;
        const float slowHz = 48.0f + (1.0f - params_.body) * 140.0f
            + material.collapse * 42.0f;
        const float midHz = 260.0f + params_.sweep * 2100.0f
            + material.highColor * 680.0f;
        const float airHz = 2200.0f + params_.shrill * 10800.0f
            + params_.air * 2400.0f;
        voice.infraNoise += (white - voice.infraNoise)
            * (1.0f - std::exp(-kPi * 2.0f * infraHz / sr));
        voice.subNoise += (white - voice.subNoise)
            * (1.0f - std::exp(-kPi * 2.0f * subHz / sr));
        voice.slowNoise += (white - voice.slowNoise)
            * (1.0f - std::exp(-kPi * 2.0f * slowHz / sr));
        voice.midNoise += (white - voice.midNoise)
            * (1.0f - std::exp(-kPi * 2.0f * midHz / sr));
        voice.airNoise += (white - voice.airNoise)
            * (1.0f - std::exp(-kPi * 2.0f * airHz / sr));
        const float midBand = voice.midNoise - voice.slowNoise;
        const float highBand = white - voice.airNoise;
        const float plumeBand = (voice.subNoise - voice.infraNoise)
            * (5.0f + params_.body * 4.2f
                + material.collapse * 1.4f);
        const float derivative = white - voice.previousWhite;
        voice.previousWhite = white;

        const float scoreClock = (1.0f - params_.scoreRest)
            + params_.scoreRest * std::clamp(0.015f
                + voice.scoreDrive
                + voice.scorePropagation * 0.32f, 0.015f, 1.0f);
        const float ignitionRateHz = params_.gustRate * scoreClock;
        const float temporalScale = ambiPyrosphereTemporalScale(
            ignitionRateHz);
        if (!voice.heatScheduled) {
            voice.heatTimer = randomInterval(voice, ignitionRateHz);
            voice.scheduledHeatRateHz = ignitionRateHz;
            voice.heatScheduled = true;
        } else if (std::fabs(ignitionRateHz
                - voice.scheduledHeatRateHz) > 1.0e-6f) {
            voice.heatTimer *= voice.scheduledHeatRateHz
                / std::max(1.0e-5f, ignitionRateHz);
            voice.scheduledHeatRateHz = ignitionRateHz;
        }

        voice.heatTimer -= dt;
        if (voice.heatTimer <= 0.0f) {
            const float oxygen = 0.18f + params_.breath * 0.82f;
            const float heatAvailability = params_.wind
                * (0.26f + material.combustibility * 0.74f)
                * (0.10f + voice.fuelRemaining * 0.90f)
                * std::clamp(0.06f + voice.scoreDrive
                    + voice.scorePropagation * 0.28f, 0.06f, 1.0f);
            const float shapedRandom = std::pow(randomUnit(voice.rng),
                0.55f + static_cast<float>(params_.gustShape) * 0.24f);
            voice.heatTarget = clamp(heatAvailability * oxygen
                * (0.36f + params_.center * 0.44f
                    + shapedRandom
                        * (0.42f + params_.gustDepth * 0.72f)),
                0.0f, 1.5f);
            voice.heatTimer = randomInterval(voice, ignitionRateHz);
            if (planetaryMaterial) {
                voice.planetaryThermal.excite(planetaryMode,
                    0.18f + voice.heatTarget * 0.54f,
                    params_.scoreCascade, false);
            }
            ++ignitionEventCount_;
            if (voice.heatTarget - voice.heat > 0.46f
                && randomUnit(voice.rng) < params_.pressure * 0.72f) {
                voice.pressureEnvelope = std::max(
                    voice.pressureEnvelope,
                    (voice.heatTarget - voice.heat)
                        * (0.4f + params_.pressure));
            }
        }
        const float edgeScale = params_.gustEdge == 2u ? 0.34f
            : (params_.gustEdge == 1u ? 0.68f : 1.0f);
        const float heatTime = voice.heatTarget > voice.heat
            ? (0.018f + (1.0f - params_.gustDepth) * 0.12f)
                * edgeScale
            : 0.10f + params_.material * 0.52f;
        voice.heat += (voice.heatTarget - voice.heat)
            * (1.0f - std::exp(-dt / heatTime));
        const float unstableHeat = clamp(voice.heat
            * (0.72f + std::fabs(voice.slowNoise)
                * (0.34f + params_.flutter * 0.92f)),
            0.0f, 1.8f);
        voice.fuelRemaining = std::max(0.0f,
            voice.fuelRemaining - dt * voice.scoreDrive
                * (0.018f + params_.wind * 0.092f)
                * (0.46f + material.combustibility * 0.72f));
        voice.emberCharge = std::max(0.0f,
            voice.emberCharge - dt
                * (0.10f + params_.particles * 0.28f));
        if (planetaryMaterial) {
            return processPlanetaryVoice(index, voice, material,
                unstableHeat);
        }
        if (voice.planetaryModal.active()) {
            voice.planetaryModal.reset();
        }
        voice.planetarySample = 0.0f;
        voice.planetaryActivity = 0.0f;
        voice.planetaryModalSample = 0.0f;
        voice.planetaryModalActivity = 0.0f;
        const bool pressureJet = params_.materialMode == 13u;
        const auto jet = pressureJet
            ? voice.flameJet.process(white, params_.pressure, params_.wind,
                params_.turbulence, params_.breath, params_.hiss,
                params_.body, clamp(params_.sweep * 0.62f
                    + params_.shrill * 0.38f, 0.0f, 1.0f))
            : TurbulentFlameJetOutput {};
        voice.jetSample = jet.sample;
        voice.jetActivity = jet.activity;

        float standingStructure = 0.0f;
        float hierarchy = 0.36f;
        float structureMass = 0.44f;
        switch (params_.materialMode) {
        case 3u: standingStructure = 0.92f; hierarchy = 0.68f; structureMass = 0.82f; break; // timber
        case 9u: standingStructure = 0.68f; hierarchy = 0.78f; structureMass = 0.46f; break; // resin
        case 10u: standingStructure = 0.28f; hierarchy = 0.94f; structureMass = 0.16f; break; // grass
        case 11u: standingStructure = 1.00f; hierarchy = 1.00f; structureMass = 1.00f; break; // forest
        case 2u: standingStructure = 0.32f; hierarchy = 0.54f; structureMass = 0.34f; break; // duff/root
        default: break;
        }
        voice.structuralIntegrity = std::max(0.0f,
            voice.structuralIntegrity - dt * unstableHeat
                * standingStructure
                * (0.008f + params_.structuralLoad * 0.068f)
                * (0.18f + voice.scoreDrive * 0.82f));
        StructuralFailureParams structureParams {};
        structureParams.drive = clamp(params_.structuralLoad
            * standingStructure
            * (0.28f + params_.motionFlow * 0.34f
                + unstableHeat * 0.48f)
            * (0.02f + temporalScale * 0.98f)
            * (0.08f + voice.scoreDrive * 0.68f
                + voice.scoreConsequence * 0.44f), 0.0f, 1.0f);
        structureParams.motion = clamp(params_.motionFlow * 0.46f
            + params_.motionShear * 0.24f + params_.vortex * 0.30f,
            0.0f, 1.0f);
        structureParams.hierarchy = hierarchy;
        structureParams.stiffness = clamp(0.24f
            + material.damping * 0.42f + params_.body * 0.20f,
            0.0f, 1.0f);
        structureParams.toughness = clamp(0.76f
            - material.fracture * 0.42f - params_.q * 0.24f,
            0.0f, 1.0f);
        structureParams.damage = clamp(unstableHeat * 0.46f
            + params_.grit * 0.24f
            + material.thermalMismatch * 0.30f
            + (1.0f - voice.structuralIntegrity) * 0.46f
            + voice.scoreConsequence * 0.34f, 0.0f, 1.0f);
        structureParams.highDetail = params_.snap;
        structureParams.consequence = clamp(params_.fall * standingStructure
            + voice.scoreConsequence * standingStructure * 0.52f,
            0.0f, 1.0f);
        structureParams.mass = structureMass;
        structureParams.mode = StructuralConsequence::HingeFall;
        const auto structural = voice.structure.process(structureParams);
        if (structural.snapTriggered) ++structuralSnapEventCount_;
        if (structural.consequenceTriggered) ++fallEventCount_;
        if (structural.consequenceTriggered) {
            voice.massEnvelope = std::max(voice.massEnvelope,
                (0.46f + structureMass * 0.82f)
                    * (0.52f + params_.fall * 0.48f));
            voice.massFrequencyHz = 19.0f
                + (1.0f - structureMass) * 38.0f
                + randomUnit(voice.rng) * 13.0f;
            voice.massPhase = 0.0f;
        }
        if (structural.snapTriggered || structural.consequenceTriggered) {
            score_.exciteCascade(index,
                structural.consequenceTriggered ? 1.0f
                    : 0.38f + params_.particles * 0.42f);
        }

        const float thermalDrive = unstableHeat
            * (0.18f + material.thermalMismatch * 1.32f)
            * (0.28f + params_.material * 0.82f)
            * (0.38f + params_.q * 0.78f)
            * temporalScale
            * (0.08f + voice.scoreDrive * 0.74f
                + voice.scorePropagation * 0.36f);
        voice.thermalStress += dt * thermalDrive;
        voice.thermalStress = std::max(0.0f,
            voice.thermalStress - dt
                * (0.008f + material.damping * 0.026f));
        if (voice.thermalStress >= voice.fractureThreshold) {
            triggerFracture(voice, material, false);
            voice.thermalStress *= 0.14f + randomUnit(voice.rng) * 0.24f;
            voice.fractureThreshold = 0.045f
                + randomUnit(voice.rng)
                    * (0.18f + material.damping * 0.34f);
        }

        if (voice.branchesRemaining > 0u) {
            voice.branchTimer -= dt;
            if (voice.branchTimer <= 0.0f) {
                triggerFracture(voice, material, true);
                --voice.branchesRemaining;
                voice.branchTimer = 0.0015f + randomUnit(voice.rng)
                    * (0.010f + params_.grit * 0.028f);
            }
        }

        const float fragmentRate = (0.1f + params_.particles
                * params_.particles * (6.0f + material.fracture * 26.0f)
                + params_.grit * material.fracture * 4.0f)
            * temporalScale
            * (0.08f + voice.scoreActivity * 0.72f
                + voice.emberCharge * 0.42f);
        if (!voice.fragmentScheduled) {
            voice.fragmentTimer = randomInterval(voice, fragmentRate);
            voice.scheduledFragmentRateHz = fragmentRate;
            voice.fragmentScheduled = true;
        } else if (std::fabs(fragmentRate
                - voice.scheduledFragmentRateHz) > 1.0e-6f) {
            voice.fragmentTimer *= voice.scheduledFragmentRateHz
                / std::max(1.0e-5f, fragmentRate);
            voice.scheduledFragmentRateHz = fragmentRate;
        }
        voice.fragmentTimer -= dt;
        if (voice.fragmentTimer <= 0.0f) {
            voice.fragmentTimer = randomInterval(voice, fragmentRate);
            voice.fragmentEnvelope = std::max(voice.fragmentEnvelope,
                (0.08f + randomUnit(voice.rng) * 0.42f)
                    * (params_.particles + params_.grit * 0.22f));
        }

        const float fractureTime = 0.0012f + params_.q * 0.009f
            + material.damping * 0.004f;
        const float spallTime = 0.014f + params_.body * 0.11f
            + material.spall * 0.08f;
        const float debrisTime = 0.06f + params_.body * 0.56f
            + material.collapse * 0.42f;
        voice.fractureEnvelope *= std::exp(-dt / fractureTime);
        voice.spallEnvelope *= std::exp(-dt / spallTime);
        voice.debrisEnvelope *= std::exp(-dt / debrisTime);
        voice.fragmentEnvelope *= std::exp(-dt
            / (0.0014f + params_.particles * 0.008f));
        voice.pressureEnvelope *= std::exp(-dt
            / (0.055f + params_.pressure * 0.46f));
        voice.forcePulse *= std::exp(-dt
            / (0.0011f + material.damping * 0.006f));
        voice.massPhase += kPi * 2.0f * voice.massFrequencyHz
            * (1.0f + voice.infraNoise * 0.08f) / sr;
        if (voice.massPhase >= kPi * 2.0f) {
            voice.massPhase -= kPi * 2.0f;
        }
        const float massMode = std::sin(voice.massPhase)
            * voice.massEnvelope;
        voice.massEnvelope *= std::exp(-dt
            / (0.10f + params_.body * 0.34f
                + material.collapse * 0.18f));

        const float combustion = material.combustibility * params_.material;
        const float plumeBody = std::tanh(plumeBand
                * (0.34f + params_.body * 1.12f))
            * unstableHeat * combustion
            * (0.14f + params_.wind * 0.34f
                + params_.pressure * 0.24f);
        const float roar = std::tanh((voice.subNoise
                * (1.4f + params_.body * 4.2f)
            + voice.slowNoise * (0.8f + params_.body * 1.8f)
            + midBand * (0.34f + params_.turbulence * 1.42f))
            * (0.46f + params_.turbulence * 1.54f))
            * unstableHeat * combustion;
        const float flameNoise = highBand
            * (params_.hiss * 0.48f + params_.air * 0.24f)
            * (0.10f + material.highColor * 0.56f)
            * unstableHeat * combustion;
        const float fracture = voice.fractureEnvelope
            * (derivative * (0.34f + material.fracture * 0.66f)
                + highBand * 0.36f + voice.forcePulse * 0.48f);
        const float spall = voice.spallEnvelope
            * (midBand * 0.72f + voice.slowNoise * 0.64f
                + voice.forcePulse * 0.38f);
        const float debris = voice.debrisEnvelope
            * (plumeBand * 0.38f + voice.subNoise * 0.82f
                + voice.slowNoise * 0.72f
                + midBand * 0.38f)
            * (0.48f + params_.grit * 0.72f);
        const float fragments = voice.fragmentEnvelope
            * (highBand * 0.74f + derivative * 0.18f);
        const float pressure = voice.pressureEnvelope
            * (plumeBand * 0.54f + voice.subNoise * 0.72f
                + voice.slowNoise * 0.34f)
            * params_.pressure * 1.18f;
        const float listenerGain = 1.0f
            + field_.listenerDrive(index) * 0.22f;
        const float entityBed = pressureJet
            ? (1.0f - params_.scoreRest * 0.30f)
                + params_.scoreRest * 0.30f
                    * std::clamp(voice.scoreActivity, 0.0f, 1.0f)
            : (1.0f - params_.scoreRest)
                + params_.scoreRest * std::clamp(0.025f
                    + voice.scoreActivity
                    + voice.scoreAftermath * 0.18f, 0.025f, 1.0f);
        const float combustionSample = (plumeBody * 0.42f
            + roar * 0.34f + flameNoise * 0.26f
            + fracture * 0.34f + spall * 0.48f + debris * 0.38f
            + fragments * 0.30f + pressure * 0.42f
            + voice.jetSample * 0.92f)
            * (0.18f + params_.wind * 0.68f) * entityBed;
        const float structuralTransient = structural.flex * 0.12f
            + structural.crack * 0.82f + structural.snap * 1.12f
            + structural.rupture * 0.92f;
        const float structuralConsequence = structural.fall * 1.10f
            + structural.impact * 1.52f
            + massMode * (0.24f + structureMass * 0.62f);
        const float structuralSample = (structuralTransient
                + structuralConsequence)
            * standingStructure
            * (0.32f + params_.structuralLoad * 0.98f)
            * (1.0f + params_.fall * 0.28f);
        const float structuralFocus = clamp(structural.activity
            * standingStructure
            * (0.32f + params_.snap * 0.18f + params_.fall * 0.38f),
            0.0f, 0.68f);
        const float geologicalMass = massMode
            * material.collapse * (0.16f + params_.body * 0.34f);
        const float sample = (combustionSample * (1.0f - structuralFocus)
                + structuralSample + geologicalMass)
            * listenerGain;

        const float event = std::max({ voice.fractureEnvelope,
            voice.spallEnvelope, voice.debrisEnvelope,
            voice.fragmentEnvelope, voice.pressureEnvelope,
            structural.activity, voice.jetActivity });
        voice.eventLevel += (event - voice.eventLevel) * 0.018f;
        voice.energy += (sample * sample - voice.energy) * 0.0012f;
        return std::isfinite(sample) ? sample : 0.0f;
    }

    AmbiPyrosphereParams params_ {};
    GeologicalField field_ {};
    EnvironmentalScore score_ {};
    std::array<AmbiPyrosphereVoice, kAmbiPyrosphereMaxVoices> voices_ {};
    std::array<float, kAmbiPyrosphereMaxChannels> lastOutput_ {};
    std::array<float, kAmbiPyrosphereMaxChannels> transitionTail_ {};
    double sampleRate_ = 48000.0;
    float smoothedOutputGain_ = dbToGain(-6.0f);
    float transitionFade_ = 1.0f;
    float combustionLayerEnergy_ = 0.0f;
    float jetLayerEnergy_ = 0.0f;
    float planetaryLayerEnergy_ = 0.0f;
    float planetaryModalLayerEnergy_ = 0.0f;
    uint64_t geologicalEventCount_ = 0u;
    uint64_t spallEventCount_ = 0u;
    uint64_t collapseEventCount_ = 0u;
    uint64_t ignitionEventCount_ = 0u;
    uint64_t structuralSnapEventCount_ = 0u;
    uint64_t fallEventCount_ = 0u;
    uint64_t planetaryBlisterEventCount_ = 0u;
    uint64_t planetaryFrontEventCount_ = 0u;
    uint64_t planetaryVentEventCount_ = 0u;
    uint64_t planetaryLatticeEventCount_ = 0u;
    uint64_t modalExcitationEventCount_ = 0u;
    std::atomic<bool> transitionRequested_ { false };
};

} // namespace s3g
