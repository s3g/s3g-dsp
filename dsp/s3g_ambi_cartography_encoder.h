#pragma once

#include "s3g_ambi_field_listener.h"
#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_cartography_site_processes.h"
#include "s3g_macro_delay.h"
#include "s3g_macro_fracture.h"
#include "s3g_macro_pitch.h"
#include "s3g_macro_shred.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace s3g {

constexpr uint32_t kAmbiCartographyMaxSites = 24u;
constexpr uint32_t kAmbiCartographyMaxOrder = 7u;
constexpr uint32_t kAmbiCartographyMaxChannels = 64u;

enum class AmbiCartographyLayout : uint32_t {
    Radial = 0u,
    Ridge = 1u,
    Corridor = 2u,
    Grid = 3u,
    Waterfront = 4u,
};

enum class AmbiCartographyStereoMap : uint32_t {
    MidSide = 0u,
    Mono = 1u,
    Alternate = 2u,
};

enum class AmbiCartographyTimeReference : uint32_t {
    Relative = 0u,
    Absolute = 1u,
};

enum class AmbiCartographyMacroEngine : uint32_t {
    Clean = 0u,
    Delay = 1u,
    Pitch = 2u,
    Shred = 3u,
    Fracture = 4u,
    SpeakerBody = 5u,
    SpectralRelay = 6u,
    RelayBuffer = 7u,
};

enum class AmbiCartographyMacroMetric : uint32_t {
    Network = 0u,
    Arrival = 1u,
    Bearing = 2u,
    Range = 3u,
};

struct AmbiCartographySite {
    // Coordinates are normalized map coordinates. mapScaleMeters converts one
    // unit to physical distance without changing the authored cartography.
    float x = 0.0f;
    float y = 1.0f;
    float z = 0.0f;
    float gain = 1.0f;
    float networkPosition = 0.0f;
    float networkTrimMs = 0.0f;
    bool enabled = true;
};

struct AmbiCartographyEncoderParams {
    uint32_t activeSites = 12u;
    uint32_t selectedSite = 0u;
    uint32_t order = 3u;
    AmbiCartographyLayout layout = AmbiCartographyLayout::Radial;
    AmbiCartographyStereoMap stereoMap = AmbiCartographyStereoMap::MidSide;
    AmbiCartographyTimeReference timeReference =
        AmbiCartographyTimeReference::Relative;
    AmbiCartographyMacroEngine macroEngine =
        AmbiCartographyMacroEngine::Delay;
    AmbiCartographyMacroMetric macroMetric =
        AmbiCartographyMacroMetric::Network;
    AmbiFieldListenMode listenMode = AmbiFieldListenMode::Off;

    float mapScaleMeters = 240.0f;
    float listenerX = 0.0f;
    float listenerY = 0.0f;
    float listenerZ = 0.0f;
    float networkSpreadMs = 420.0f;
    float propagationScale = 0.35f;
    float air = 0.28f;
    float distanceLoss = 0.55f;
    float carry = 0.20f;
    float turbulence = 0.08f;

    // MACRO is intentionally generic: delay time, pitch interval, drive, or
    // fracture depth according to the selected engine.
    float macro = 0.50f;
    float color = 0.55f;
    float memory = 0.28f;
    float spread = 0.35f;
    float deviation = 0.12f;
    float skew = 0.0f;
    float center = 0.5f;
    float processMix = 0.42f;
    float listenerAmount = 0.0f;
    float outputGainDb = -6.0f;

    float selectedX = 0.0f;
    float selectedY = 1.0f;
    float selectedZ = 0.0f;
    float selectedGain = 1.0f;
    float selectedNetworkTrimMs = 0.0f;
    bool selectedEnabled = true;
};

inline AmbiCartographyEncoderParams sanitizeAmbiCartographyEncoderParams(
    AmbiCartographyEncoderParams params)
{
    params.activeSites = std::clamp<uint32_t>(
        params.activeSites, 1u, kAmbiCartographyMaxSites);
    params.selectedSite = std::min<uint32_t>(
        params.selectedSite, params.activeSites - 1u);
    params.order = std::clamp<uint32_t>(
        params.order, 1u, kAmbiCartographyMaxOrder);
    params.layout = static_cast<AmbiCartographyLayout>(
        std::min<uint32_t>(static_cast<uint32_t>(params.layout), 4u));
    params.stereoMap = static_cast<AmbiCartographyStereoMap>(
        std::min<uint32_t>(static_cast<uint32_t>(params.stereoMap), 2u));
    params.timeReference = static_cast<AmbiCartographyTimeReference>(
        std::min<uint32_t>(static_cast<uint32_t>(params.timeReference), 1u));
    params.macroEngine = static_cast<AmbiCartographyMacroEngine>(
        std::min<uint32_t>(static_cast<uint32_t>(params.macroEngine), 7u));
    params.macroMetric = static_cast<AmbiCartographyMacroMetric>(
        std::min<uint32_t>(static_cast<uint32_t>(params.macroMetric), 3u));
    params.listenMode = sanitizeAmbiFieldListenMode(params.listenMode);

    params.mapScaleMeters = clamp(params.mapScaleMeters, 10.0f, 2000.0f);
    params.listenerX = clamp(params.listenerX, -1.5f, 1.5f);
    params.listenerY = clamp(params.listenerY, -1.5f, 1.5f);
    params.listenerZ = clamp(params.listenerZ, -1.0f, 1.0f);
    params.networkSpreadMs = clamp(params.networkSpreadMs, 0.0f, 2000.0f);
    params.propagationScale = clamp(params.propagationScale, 0.0f, 1.0f);
    params.air = clamp(params.air, 0.0f, 1.0f);
    params.distanceLoss = clamp(params.distanceLoss, 0.0f, 1.0f);
    params.carry = clamp(params.carry, 0.0f, 1.0f);
    params.turbulence = clamp(params.turbulence, 0.0f, 1.0f);
    params.macro = clamp(params.macro, 0.0f, 1.0f);
    params.color = clamp(params.color, 0.0f, 1.0f);
    params.memory = clamp(params.memory, 0.0f, 1.0f);
    params.spread = clamp(params.spread, 0.0f, 1.0f);
    params.deviation = clamp(params.deviation, 0.0f, 1.0f);
    params.skew = clamp(params.skew, -1.0f, 1.0f);
    params.center = clamp(params.center, 0.0f, 1.0f);
    params.processMix = clamp(params.processMix, 0.0f, 1.0f);
    params.listenerAmount = clamp(params.listenerAmount, 0.0f, 1.0f);
    params.outputGainDb = clamp(params.outputGainDb, -60.0f, 12.0f);
    params.selectedX = clamp(params.selectedX, -1.5f, 1.5f);
    params.selectedY = clamp(params.selectedY, -1.5f, 1.5f);
    params.selectedZ = clamp(params.selectedZ, -1.0f, 1.0f);
    params.selectedGain = clamp(params.selectedGain, 0.0f, 2.0f);
    params.selectedNetworkTrimMs = clamp(
        params.selectedNetworkTrimMs, -500.0f, 500.0f);
    params.selectedEnabled = params.selectedEnabled
        && params.selectedSite < params.activeSites;
    return params;
}

struct AmbiCartographyLandscapeParams {
    // Every amount is an exact bypass at zero. The established cartography
    // controls voice secondary behavior: COLOR shapes reflected/occluded
    // spectra, MEMORY shapes network drift and feedback time, SPREAD changes
    // reflection/feedback topology, and AIR/CARRY/TURBULENCE voice weather.
    float multipath = 0.0f;
    float occlusion = 0.0f;
    float networkWeather = 0.0f;
    float refraction = 0.0f;
    float motion = 0.0f;
    float ecology = 0.0f;
    float horizon = 0.0f;
};

struct AmbiCartographySiteProcessOptions {
    MacroShredCircuit shredCircuit = MacroShredCircuit::Shred;
    FractureProcessor fractureProcessor = FractureProcessor::Relay;
};

inline AmbiCartographySiteProcessOptions sanitizeAmbiCartographySiteProcessOptions(
    AmbiCartographySiteProcessOptions options)
{
    options.shredCircuit = static_cast<MacroShredCircuit>(
        std::min<uint32_t>(static_cast<uint32_t>(options.shredCircuit),
            kMacroShredCircuitCount - 1u));
    options.fractureProcessor = static_cast<FractureProcessor>(
        std::min<uint32_t>(static_cast<uint32_t>(options.fractureProcessor),
            kFractureProcessorCount - 1u));
    return options;
}

inline AmbiCartographyLandscapeParams sanitizeAmbiCartographyLandscapeParams(
    AmbiCartographyLandscapeParams params)
{
    params.multipath = clamp(params.multipath, 0.0f, 1.0f);
    params.occlusion = clamp(params.occlusion, 0.0f, 1.0f);
    params.networkWeather = clamp(params.networkWeather, 0.0f, 1.0f);
    params.refraction = clamp(params.refraction, -1.0f, 1.0f);
    params.motion = clamp(params.motion, 0.0f, 1.0f);
    params.ecology = clamp(params.ecology, 0.0f, 1.0f);
    params.horizon = clamp(params.horizon, 0.0f, 1.0f);
    return params;
}

class AmbiCartographyEncoder {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::clamp(
            std::isfinite(sampleRate) ? sampleRate : 48000.0,
            1000.0, 192000.0);

        networkCapacity_ = std::clamp<uint32_t>(
            static_cast<uint32_t>(std::ceil(sampleRate_ * 2.5)) + 8u,
            128u, 480000u);
        networkLeft_.assign(networkCapacity_, 0.0f);
        networkRight_.assign(networkCapacity_, 0.0f);

        airCapacity_ = std::clamp<uint32_t>(
            static_cast<uint32_t>(std::ceil(sampleRate_ * 12.0)) + 8u,
            128u, 1152000u);
        for (auto& line : airDelay_) line.assign(airCapacity_, 0.0f);

        macroDelay_.prepare(sampleRate_, kAmbiCartographyMaxSites, 2.5);
        macroPitch_.prepare(sampleRate_, kAmbiCartographyMaxSites);
        macroShred_.prepare(sampleRate_, kAmbiCartographyMaxSites);
        macroFracture_.prepare(sampleRate_, kAmbiCartographyMaxSites);
        speakerBody_.prepare(sampleRate_, kAmbiCartographyMaxSites);
        spectralRelay_.prepare(sampleRate_, kAmbiCartographyMaxSites);
        relayBuffer_.prepare(sampleRate_, kAmbiCartographyMaxSites);

        fieldListener_.prepare(sampleRate_);
        fieldListener_.setExtendedAnalysisEnabled(true);
        fieldListener_.setDirections(
            ambiFieldListenerCubeDirections().data(),
            kAmbiFieldListenerMaxLobes);
        fieldListener_.setMemorySeconds(0.72f);

        if (!prepared_) {
            resetLayout(params_.layout);
            syncSelectedFromSite();
            prepared_ = true;
        }
        setParams(params_);
        reset();
    }

    void reset()
    {
        networkWrite_ = 0u;
        airWrite_ = 0u;
        networkHistorySamples_ = 0u;
        airHistorySamples_ = 0u;
        smoothedNetworkDelay_.fill(0.0f);
        smoothedAirDelay_.fill(0.0f);
        airState_.fill(0.0f);
        groundState_.fill(0.0f);
        facadeState_.fill(0.0f);
        occlusionState_.fill(0.0f);
        horizonState_.fill(0.0f);
        smoothedOcclusion_.fill(0.0f);
        smoothedHorizonGain_.fill(1.0f);
        smoothedProcessMix_ = params_.processMix;
        smoothedOutputGain_ = dbToGain(params_.outputGainDb);
        for (uint32_t site = 0u;
            site < kAmbiCartographyMaxSites; ++site) {
            smoothedSiteGain_[site] = sites_[site].gain;
            smoothedSiteEnabled_[site] = sites_[site].enabled ? 1.0f : 0.0f;
        }
        lastWetBySite_.fill(0.0f);
        wetRemapCorrection_.fill(0.0f);
        wetRemapPhase_.fill(1.0f);
        wetRemapPending_.fill(false);
        wetRemapValid_.fill(false);
        routingContinuityPrimed_ = false;
        ecologyState_.fill(0.0f);
        siteLevel_.fill(0.0f);
        turbulencePhase_.fill(0.0f);
        weatherPhase_.fill(0.0f);
        weatherDriftPhase_.fill(0.0f);
        weatherPulsePhase_.fill(0.0f);
        for (uint32_t site = 0u; site < kAmbiCartographyMaxSites; ++site) {
            turbulencePhase_[site] = hashSigned(site, 41u) * 0.5f + 0.5f;
            weatherPhase_[site] = hashSigned(site, 173u) * 0.5f + 0.5f;
            weatherDriftPhase_[site] =
                hashSigned(site, 211u) * 0.5f + 0.5f;
            weatherPulsePhase_[site] =
                hashSigned(site, 293u) * 0.5f + 0.5f;
        }
        motionPhase_ = 0.0f;
        motionControlCountdown_ = 32u;
        geometryDirty_ = true;
        delayPrimed_ = false;
        activeEngine_ = params_.macroEngine;
        previousEngine_ = activeEngine_;
        engineCrossfade_ = 1.0f;
        macroDelay_.reset();
        macroPitch_.reset();
        macroShred_.reset();
        macroFracture_.reset();
        speakerBody_.reset();
        spectralRelay_.reset();
        relayBuffer_.reset();
        fieldListener_.reset();
    }

    void setParams(AmbiCartographyEncoderParams params)
    {
        const auto previousParams = params_;
        const auto previousLayout = params_.layout;
        const uint32_t previousSites = params_.activeSites;
        const uint32_t previousSelected = params_.selectedSite;
        params = sanitizeAmbiCartographyEncoderParams(params);

        const bool layoutChanged = params.layout != previousLayout
            || params.activeSites != previousSites;
        const bool routingChanged = layoutChanged
            || params.timeReference != previousParams.timeReference
            || params.mapScaleMeters != previousParams.mapScaleMeters
            || params.listenerX != previousParams.listenerX
            || params.listenerY != previousParams.listenerY
            || params.listenerZ != previousParams.listenerZ
            || params.networkSpreadMs != previousParams.networkSpreadMs
            || params.propagationScale != previousParams.propagationScale
            || params.air != previousParams.air
            || params.carry != previousParams.carry
            || params.skew != previousParams.skew
            || params.macroMetric != previousParams.macroMetric;
        bool authoredSiteChanged = false;
        params_ = params;
        if (layoutChanged) {
            resetLayout(params_.layout);
            syncSelectedFromSite();
        } else if (params_.selectedSite != previousSelected) {
            syncSelectedFromSite();
        } else {
            auto& site = sites_[params_.selectedSite];
            authoredSiteChanged = site.x != params_.selectedX
                || site.y != params_.selectedY
                || site.z != params_.selectedZ
                || site.networkTrimMs != params_.selectedNetworkTrimMs
                || site.enabled != params_.selectedEnabled;
            site.x = params_.selectedX;
            site.y = params_.selectedY;
            site.z = params_.selectedZ;
            site.gain = params_.selectedGain;
            site.networkTrimMs = params_.selectedNetworkTrimMs;
            site.enabled = params_.selectedEnabled;
        }
        if (!prepared_) {
            activeEngine_ = params_.macroEngine;
            previousEngine_ = activeEngine_;
            engineCrossfade_ = 1.0f;
        } else if (previousEngine_ != activeEngine_
            && engineCrossfade_ <= 0.0f
            && params_.macroEngine != activeEngine_) {
            // Hosts may publish several parameter events before rendering the
            // block. No active-endpoint signal is audible at blend zero, so
            // replace that unseen choice with the newest request. Selecting
            // the previous endpoint cancels the untouched transition.
            activeEngine_ = params_.macroEngine;
            if (activeEngine_ == previousEngine_) {
                engineCrossfade_ = 1.0f;
            }
        } else if ((previousEngine_ == activeEngine_
                       || engineCrossfade_ >= 1.0f)
            && params_.macroEngine != activeEngine_) {
            // Keep a live transition's two endpoints stable. Automation may
            // continue to update params_.macroEngine; the newest request is
            // started after this pair reaches its active endpoint.
            previousEngine_ = activeEngine_;
            activeEngine_ = params_.macroEngine;
            engineCrossfade_ = 0.0f;
        }
        configureMacroEngines();
        geometryDirty_ = geometryDirty_
            || routingChanged || authoredSiteChanged;
    }

    void regenerateLayout()
    {
        resetLayout(params_.layout);
        syncSelectedFromSite();
        geometryDirty_ = true;
        delayPrimed_ = false;
    }

    AmbiCartographyEncoderParams params() const { return params_; }

    void setLandscapeParams(AmbiCartographyLandscapeParams params)
    {
        params = sanitizeAmbiCartographyLandscapeParams(params);
        if (landscape_.multipath > 0.0f && params.multipath == 0.0f) {
            groundState_.fill(0.0f);
            facadeState_.fill(0.0f);
        }
        if (landscape_.occlusion > 0.0f && params.occlusion == 0.0f) {
            occlusionState_.fill(0.0f);
            smoothedOcclusion_.fill(0.0f);
        }
        if (landscape_.ecology > 0.0f && params.ecology == 0.0f) {
            ecologyState_.fill(0.0f);
        }
        if (landscape_.horizon > 0.0f && params.horizon == 0.0f) {
            horizonState_.fill(0.0f);
            smoothedHorizonGain_.fill(1.0f);
        }
        const bool geometryChanged = params.multipath != landscape_.multipath
            || params.occlusion != landscape_.occlusion
            || params.refraction != landscape_.refraction
            || params.motion != landscape_.motion
            || params.horizon != landscape_.horizon;
        landscape_ = params;
        geometryDirty_ = geometryDirty_ || geometryChanged;
    }

    AmbiCartographyLandscapeParams landscapeParams() const
    {
        return landscape_;
    }

    void setSiteProcessOptions(AmbiCartographySiteProcessOptions options)
    {
        siteProcessOptions_ =
            sanitizeAmbiCartographySiteProcessOptions(options);
        configureMacroEngines();
    }

    AmbiCartographySiteProcessOptions siteProcessOptions() const
    {
        return siteProcessOptions_;
    }

    const std::array<AmbiCartographySite, kAmbiCartographyMaxSites>& sites() const
    {
        return sites_;
    }

    void setSites(
        std::array<AmbiCartographySite, kAmbiCartographyMaxSites> sites)
    {
        for (uint32_t index = 0u; index < sites.size(); ++index) {
            auto& site = sites[index];
            site.x = clamp(site.x, -1.5f, 1.5f);
            site.y = clamp(site.y, -1.5f, 1.5f);
            site.z = clamp(site.z, -1.0f, 1.0f);
            site.gain = clamp(site.gain, 0.0f, 2.0f);
            site.networkPosition = clamp(site.networkPosition, 0.0f, 1.0f);
            site.networkTrimMs = clamp(site.networkTrimMs, -500.0f, 500.0f);
            site.enabled = site.enabled && index < params_.activeSites;
        }
        sites_ = sites;
        syncSelectedFromSite();
        geometryDirty_ = true;
        delayPrimed_ = false;
    }

    AmbiCartographySite site(uint32_t index) const
    {
        return sites_[std::min<uint32_t>(
            index, kAmbiCartographyMaxSites - 1u)];
    }

    void setSite(uint32_t index, AmbiCartographySite site)
    {
        if (index >= kAmbiCartographyMaxSites) return;
        site.x = clamp(site.x, -1.5f, 1.5f);
        site.y = clamp(site.y, -1.5f, 1.5f);
        site.z = clamp(site.z, -1.0f, 1.0f);
        site.gain = clamp(site.gain, 0.0f, 2.0f);
        site.networkPosition = clamp(site.networkPosition, 0.0f, 1.0f);
        site.networkTrimMs = clamp(site.networkTrimMs, -500.0f, 500.0f);
        site.enabled = site.enabled && index < params_.activeSites;
        sites_[index] = site;
        if (index == params_.selectedSite) syncSelectedFromSite();
        geometryDirty_ = true;
        delayPrimed_ = false;
    }

    float siteLevel(uint32_t site) const
    {
        return site < kAmbiCartographyMaxSites ? siteLevel_[site] : 0.0f;
    }

    float siteDistanceMeters(uint32_t site) const
    {
        return site < kAmbiCartographyMaxSites
            ? distanceMeters_[site] : 0.0f;
    }

    float siteArrivalSeconds(uint32_t site) const
    {
        return site < kAmbiCartographyMaxSites
            ? arrivalSeconds_[site] : 0.0f;
    }

    Vec3 siteDirection(uint32_t site) const
    {
        return site < kAmbiCartographyMaxSites
            ? directions_[site] : Vec3 {};
    }

    float siteOcclusion(uint32_t site) const
    {
        return site < kAmbiCartographyMaxSites
            ? renderedOcclusion_[site] : 0.0f;
    }

    Vec3 renderedSitePosition(uint32_t site) const
    {
        return site < kAmbiCartographyMaxSites
            ? renderedSitePosition_[site] : Vec3 {};
    }

    Vec3 renderedListenerPosition() const { return renderedListenerPosition_; }

    const AmbiFieldListener& fieldListener() const { return fieldListener_; }

    // The CLAP transport bridge must cover a Body engine that is either side
    // of a live engine crossfade. Use the larger of target and smoothed mix so
    // a simultaneous Mix/Process automation event cannot shorten protection
    // while Body is still audible.
    float transportBoundaryBodyWet() const
    {
        const bool bodyAudible =
            activeEngine_ == AmbiCartographyMacroEngine::SpeakerBody
            || (previousEngine_ == AmbiCartographyMacroEngine::SpeakerBody
                && engineCrossfade_ < 1.0f);
        if (!bodyAudible) return 0.0f;
        return clamp(std::max(params_.processMix, smoothedProcessMix_)
            * (1.0f + params_.listenerAmount * 0.80f), 0.0f, 1.0f);
    }

    template <typename Sample>
    void processBlock(const Sample* const* inputs, Sample* const* outputs,
        uint32_t inputChannels, uint32_t outputChannels, uint32_t frames)
    {
        if (!outputs || frames == 0u) return;
        outputChannels = std::min<uint32_t>(
            outputChannels, kAmbiCartographyMaxChannels);
        for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
            if (outputs[channel]) {
                std::fill(outputs[channel], outputs[channel] + frames,
                    static_cast<Sample>(0));
            }
        }
        if (airCapacity_ < 8u || networkCapacity_ < 8u) return;

        if (geometryDirty_) {
            updateGeometryAndRouting();
            geometryDirty_ = false;
        }
        const uint32_t ambiChannels = std::min<uint32_t>(
            ambiChannelsForOrder(params_.order), outputChannels);
        const uint32_t active = params_.activeSites;
        const float targetOutputGain = dbToGain(params_.outputGainDb);
        const float siteNorm = 1.0f / static_cast<float>(active);
        const float delayCoefficient = 1.0f - std::exp(-1.0f
            / static_cast<float>(sampleRate_ * 0.030));
        const float levelCoefficient = 1.0f - std::exp(-1.0f
            / static_cast<float>(sampleRate_ * 0.060));
        const float engineCrossfadeStep = 1.0f
            / static_cast<float>(sampleRate_ * 0.030);
        const float processMixCoefficient = 1.0f - std::exp(-1.0f
            / static_cast<float>(sampleRate_ * 0.015));
        const float siteControlCoefficient = 1.0f - std::exp(-1.0f
            / static_cast<float>(sampleRate_ * 0.012));
        const float wetRemapStep = 1.0f
            / static_cast<float>(sampleRate_ * 0.030);
        const float landscapeCoefficient = 1.0f - std::exp(-1.0f
            / static_cast<float>(sampleRate_ * 0.050));
        const float ecologyCutoff = lerp(12000.0f, 650.0f,
            params_.memory * 0.78f + (1.0f - params_.color) * 0.22f);
        const float ecologyPole = std::exp(-2.0f * kPi * ecologyCutoff
            / static_cast<float>(sampleRate_));
        const float groundPole = std::exp(-2.0f * kPi
            * lerp(900.0f, 14500.0f, params_.color)
            / static_cast<float>(sampleRate_));
        const float facadePole = std::exp(-2.0f * kPi
            * lerp(1700.0f, 18500.0f, params_.color)
            / static_cast<float>(sampleRate_));

        std::array<float, kAmbiCartographyMaxSites> siteInput {};
        std::array<float, kAmbiCartographyMaxSites> macroInput {};
        std::array<float, kAmbiCartographyMaxSites> bodyInput {};
        std::array<float, kAmbiCartographyMaxSites> siteProcessInput {};
        std::array<float, kAmbiCartographyMaxSites> fractureModulation {};
        std::array<float, kAmbiCartographyMaxSites> macroOutput {};
        std::array<float, kAmbiCartographyMaxSites> previousMacroOutput {};
        std::array<float, kAmbiCartographyMaxChannels> field {};

        for (uint32_t frameIndex = 0u; frameIndex < frames; ++frameIndex) {
            smoothedOutputGain_ += (targetOutputGain - smoothedOutputGain_)
                * siteControlCoefficient;
            const float left = inputs && inputChannels > 0u && inputs[0]
                ? finiteSample(inputs[0][frameIndex]) : 0.0f;
            const float right = inputs && inputChannels > 1u && inputs[1]
                ? finiteSample(inputs[1][frameIndex]) : left;
            networkLeft_[networkWrite_] = left;
            networkRight_[networkWrite_] = right;
            networkHistorySamples_ = std::min(
                networkCapacity_, networkHistorySamples_ + 1u);
            const uint32_t airAvailableSamples = std::min(
                airCapacity_, airHistorySamples_ + 1u);

            if (!delayPrimed_) {
                smoothedNetworkDelay_ = targetNetworkDelay_;
                smoothedAirDelay_ = targetAirDelay_;
                delayPrimed_ = true;
            }

            macroInput.fill(0.0f);
            bodyInput.fill(0.0f);
            siteProcessInput.fill(0.0f);
            fractureModulation.fill(0.0f);
            for (uint32_t siteIndex = 0u; siteIndex < active; ++siteIndex) {
                smoothedSiteGain_[siteIndex] +=
                    (sites_[siteIndex].gain
                        - smoothedSiteGain_[siteIndex])
                    * siteControlCoefficient;
                smoothedSiteEnabled_[siteIndex] +=
                    ((sites_[siteIndex].enabled ? 1.0f : 0.0f)
                        - smoothedSiteEnabled_[siteIndex])
                    * siteControlCoefficient;
                smoothedNetworkDelay_[siteIndex] +=
                    (targetNetworkDelay_[siteIndex]
                        - smoothedNetworkDelay_[siteIndex])
                    * delayCoefficient;
                const float weatherRate =
                    0.013f + (1.0f - params_.memory) * 0.041f
                    + static_cast<float>(siteIndex % 5u) * 0.0037f;
                weatherPhase_[siteIndex] += weatherRate
                    / static_cast<float>(sampleRate_);
                weatherPhase_[siteIndex] -=
                    std::floor(weatherPhase_[siteIndex]);
                weatherDriftPhase_[siteIndex] += weatherRate * 3.17f
                    / static_cast<float>(sampleRate_);
                weatherDriftPhase_[siteIndex] -=
                    std::floor(weatherDriftPhase_[siteIndex]);
                weatherPulsePhase_[siteIndex] += weatherRate * 2.31f
                    / static_cast<float>(sampleRate_);
                weatherPulsePhase_[siteIndex] -=
                    std::floor(weatherPulsePhase_[siteIndex]);
                float networkDelay = smoothedNetworkDelay_[siteIndex];
                float relayGain = 1.0f;
                if (landscape_.networkWeather > 0.0f) {
                    const float phase = weatherPhase_[siteIndex]
                        * 2.0f * kPi;
                    const float drift = std::sin(phase)
                        + 0.32f * std::sin(
                            weatherDriftPhase_[siteIndex] * 2.0f * kPi);
                    const float weatherMs = landscape_.networkWeather
                        * (0.6f + params_.memory * 42.0f
                            + params_.turbulence * 38.0f) * drift;
                    networkDelay += weatherMs * 0.001f
                        * static_cast<float>(sampleRate_);
                    const float relayPulse = std::pow(std::max(0.0f,
                        std::sin(weatherPulsePhase_[siteIndex]
                            * 2.0f * kPi)), 18.0f);
                    relayGain = 1.0f - landscape_.networkWeather
                        * (0.12f + params_.turbulence * 0.76f)
                        * relayPulse;
                }
                const float siteLeft = readDelay(
                    networkLeft_, networkWrite_,
                    networkDelay, networkHistorySamples_, true) * relayGain;
                const float siteRight = readDelay(
                    networkRight_, networkWrite_,
                    networkDelay, networkHistorySamples_, true) * relayGain;
                float input = stereoSiteInput(
                    siteLeft, siteRight, siteIndex);
                if (landscape_.ecology > 0.0f && active > 1u) {
                    const uint32_t offset = 1u
                        + static_cast<uint32_t>(std::lround(params_.spread
                            * static_cast<float>(active - 2u)));
                    const uint32_t source = (siteIndex + active - offset)
                        % active;
                    const float feedbackMs = 18.0f
                        + params_.memory * 520.0f
                        + sites_[source].networkPosition
                            * params_.spread * 180.0f;
                    const float returned = readDelay(airDelay_[source],
                        airWrite_, feedbackMs * 0.001f
                            * static_cast<float>(sampleRate_),
                        airHistorySamples_, false);
                    ecologyState_[siteIndex] = flushDenormal(
                        returned * (1.0f - ecologyPole)
                            + ecologyState_[siteIndex] * ecologyPole);
                    input += std::tanh(ecologyState_[siteIndex])
                        * landscape_.ecology * 0.42f;
                }
                siteInput[siteIndex] = flushDenormal(input);
                macroInput[siteToLane_[siteIndex]] = siteInput[siteIndex];
                bodyInput[siteIndex] = siteInput[siteIndex]
                    * smoothedSiteEnabled_[siteIndex];
                if (sites_[siteIndex].enabled && processLaneCount_ > 0u) {
                    siteProcessInput[siteToProcessLane_[siteIndex]] =
                        siteInput[siteIndex];
                }
            }
            for (uint32_t siteIndex = active;
                siteIndex < kAmbiCartographyMaxSites; ++siteIndex) {
                siteInput[siteIndex] = 0.0f;
            }
            for (uint32_t siteIndex = 0u;
                siteIndex < active; ++siteIndex) {
                fractureModulation[siteToLane_[siteIndex]] =
                    siteInput[fractureModulatorSite_[siteIndex]];
            }

            macroOutput.fill(0.0f);
            const auto& activeInput =
                activeEngine_ == AmbiCartographyMacroEngine::SpeakerBody
                ? bodyInput : usesCompactSiteLanes(activeEngine_)
                    ? siteProcessInput : macroInput;
            processMacroFrame(activeEngine_, activeInput, macroOutput,
                &fractureModulation);
            const bool engineTransition = previousEngine_ != activeEngine_
                && engineCrossfade_ < 1.0f;
            if (engineTransition) {
                previousMacroOutput.fill(0.0f);
                const auto& previousInput = previousEngine_
                        == AmbiCartographyMacroEngine::SpeakerBody
                    ? bodyInput : usesCompactSiteLanes(previousEngine_)
                        ? siteProcessInput : macroInput;
                processMacroFrame(previousEngine_, previousInput,
                    previousMacroOutput, &fractureModulation);
            }
            const float engineBlend = engineTransition
                ? smoothStep(0.0f, 1.0f, engineCrossfade_) : 1.0f;
            engineCrossfade_ = std::min(
                1.0f, engineCrossfade_ + engineCrossfadeStep);
            field.fill(0.0f);
            const float listenerActivity = fieldListener_.activity();
            smoothedProcessMix_ += (params_.processMix
                - smoothedProcessMix_) * processMixCoefficient;

            for (uint32_t siteIndex = 0u; siteIndex < active; ++siteIndex) {
                const auto& site = sites_[siteIndex];
                const uint32_t activeLane = activeEngine_
                        == AmbiCartographyMacroEngine::SpeakerBody
                    ? siteIndex : usesCompactSiteLanes(activeEngine_)
                        ? siteToProcessLane_[siteIndex]
                        : siteToLane_[siteIndex];
                float wet = macroOutput[activeLane];
                if (engineTransition) {
                    const uint32_t previousLane = previousEngine_
                            == AmbiCartographyMacroEngine::SpeakerBody
                        ? siteIndex : usesCompactSiteLanes(previousEngine_)
                            ? siteToProcessLane_[siteIndex]
                            : siteToLane_[siteIndex];
                    wet = lerp(previousMacroOutput[previousLane],
                        wet, engineBlend);
                }
                if (wetRemapPending_[siteIndex]) {
                    wetRemapCorrection_[siteIndex] =
                        wetRemapValid_[siteIndex]
                        ? lastWetBySite_[siteIndex] - wet : 0.0f;
                    wetRemapPhase_[siteIndex] =
                        wetRemapValid_[siteIndex] ? 0.0f : 1.0f;
                    wetRemapPending_[siteIndex] = false;
                }
                if (wetRemapPhase_[siteIndex] < 1.0f) {
                    const float window = 1.0f - smoothStep(
                        0.0f, 1.0f, wetRemapPhase_[siteIndex]);
                    wet += wetRemapCorrection_[siteIndex] * window;
                    wetRemapPhase_[siteIndex] = std::min(1.0f,
                        wetRemapPhase_[siteIndex] + wetRemapStep);
                }
                wet = flushDenormal(wet);
                lastWetBySite_[siteIndex] = wet;
                wetRemapValid_[siteIndex] = true;
                const float preference = fieldListener_.preference(
                    directions_[siteIndex], params_.listenMode);
                const float centeredPreference = (preference - 0.5f) * 2.0f;
                const float listenerDepth = params_.listenerAmount
                    * listenerActivity;
                const float adaptiveWet = clamp(
                    smoothedProcessMix_
                        * (1.0f + centeredPreference
                            * listenerDepth * 0.80f),
                    0.0f, 1.0f);
                const float processed = lerp(
                    siteInput[siteIndex], wet, adaptiveWet);

                airDelay_[siteIndex][airWrite_] = flushDenormal(
                    processed * smoothedSiteEnabled_[siteIndex]);
                smoothedAirDelay_[siteIndex] +=
                    (targetAirDelay_[siteIndex]
                        - smoothedAirDelay_[siteIndex])
                    * delayCoefficient;
                float arrived = readDelay(airDelay_[siteIndex], airWrite_,
                    smoothedAirDelay_[siteIndex], airAvailableSamples, true);

                float groundArrival = 0.0f;
                float facadeArrival = 0.0f;
                if (landscape_.multipath > 0.0f) {
                    groundArrival = readDelay(airDelay_[siteIndex], airWrite_,
                        smoothedAirDelay_[siteIndex]
                            + groundExtraDelay_[siteIndex],
                        airAvailableSamples, true);
                    facadeArrival = readDelay(airDelay_[siteIndex], airWrite_,
                        smoothedAirDelay_[siteIndex]
                            + facadeExtraDelay_[siteIndex],
                        airAvailableSamples, true);
                    groundState_[siteIndex] = flushDenormal(
                        groundArrival * (1.0f - groundPole)
                            + groundState_[siteIndex] * groundPole);
                    facadeState_[siteIndex] = flushDenormal(
                        facadeArrival * (1.0f - facadePole)
                            + facadeState_[siteIndex] * facadePole);
                    groundArrival = groundState_[siteIndex];
                    facadeArrival = facadeState_[siteIndex];
                }

                const float range = clamp(distanceMeters_[siteIndex]
                    / std::max(10.0f, params_.mapScaleMeters * 2.0f),
                    0.0f, 1.0f);
                const float cutoff = lerp(19000.0f, 1600.0f,
                    params_.air * std::pow(range, 0.62f));
                const float pole = std::exp(-2.0f * kPi * cutoff
                    / static_cast<float>(sampleRate_));
                airState_[siteIndex] = flushDenormal(
                    arrived * (1.0f - pole)
                        + airState_[siteIndex] * pole);
                arrived = lerp(arrived, airState_[siteIndex], params_.air);

                if (landscape_.occlusion > 0.0f) {
                    smoothedOcclusion_[siteIndex] +=
                        (renderedOcclusion_[siteIndex]
                            - smoothedOcclusion_[siteIndex])
                        * landscapeCoefficient;
                    const float amount = smoothedOcclusion_[siteIndex];
                    const float occlusionCutoff = lerp(15000.0f, 520.0f,
                        amount * (0.72f + (1.0f - params_.color) * 0.28f));
                    const float occlusionPole = std::exp(-2.0f * kPi
                        * occlusionCutoff / static_cast<float>(sampleRate_));
                    occlusionState_[siteIndex] = flushDenormal(
                        arrived * (1.0f - occlusionPole)
                            + occlusionState_[siteIndex] * occlusionPole);
                    arrived = lerp(arrived, occlusionState_[siteIndex], amount)
                        * lerp(1.0f, 0.24f, amount);
                }

                if (landscape_.horizon > 0.0f) {
                    smoothedHorizonGain_[siteIndex] +=
                        (horizonGain_[siteIndex]
                            - smoothedHorizonGain_[siteIndex])
                        * landscapeCoefficient;
                    const float loss = 1.0f
                        - smoothedHorizonGain_[siteIndex];
                    const float horizonCutoff = lerp(
                        17000.0f, 720.0f, loss);
                    const float horizonPole = std::exp(-2.0f * kPi
                        * horizonCutoff / static_cast<float>(sampleRate_));
                    horizonState_[siteIndex] = flushDenormal(
                        arrived * (1.0f - horizonPole)
                            + horizonState_[siteIndex] * horizonPole);
                    arrived = lerp(arrived, horizonState_[siteIndex], loss)
                        * smoothedHorizonGain_[siteIndex];
                    groundArrival *= smoothedHorizonGain_[siteIndex];
                    facadeArrival *= smoothedHorizonGain_[siteIndex];
                }

                turbulencePhase_[siteIndex] +=
                    (0.017f + static_cast<float>(siteIndex % 7u) * 0.004f)
                    / static_cast<float>(sampleRate_);
                turbulencePhase_[siteIndex] -=
                    std::floor(turbulencePhase_[siteIndex]);
                const float noisePosition =
                    turbulencePhase_[siteIndex] * 97.0f;
                const uint32_t noiseBucket = static_cast<uint32_t>(
                    std::floor(noisePosition)) % 97u;
                const uint32_t nextNoiseBucket = (noiseBucket + 1u) % 97u;
                const float noiseFraction = smoothStep(
                    0.0f, 1.0f, noisePosition - std::floor(noisePosition));
                const float turbulenceNoise = lerp(
                    hashSigned(siteIndex, noiseBucket),
                    hashSigned(siteIndex, nextNoiseBucket), noiseFraction);
                const float turbulence = 1.0f + params_.turbulence
                    * (0.075f * std::sin(
                           turbulencePhase_[siteIndex] * 2.0f * kPi)
                        + 0.025f * turbulenceNoise);
                const float lossExponent = params_.distanceLoss
                    * (1.0f - params_.carry * 0.78f) * 0.68f;
                const float distanceGain = std::pow(
                    1.0f + distanceMeters_[siteIndex] / 25.0f,
                    -lossExponent);
                const float reach = clamp(
                    1.0f + centeredPreference * listenerDepth * 0.18f,
                    0.72f, 1.28f);
                const float commonGain = smoothedSiteGain_[siteIndex]
                    * distanceGain
                    * turbulence * reach * siteNorm
                    * refractionGain_[siteIndex];
                arrived *= commonGain;
                const float groundGain = landscape_.multipath
                    * lerp(0.38f, 0.18f, params_.spread);
                const float facadeGain = landscape_.multipath
                    * lerp(0.16f, 0.42f, params_.spread);
                const float pathNorm = 1.0f / std::sqrt(
                    1.0f + groundGain * groundGain
                        + facadeGain * facadeGain);
                arrived *= pathNorm;
                groundArrival *= commonGain * groundGain * pathNorm;
                facadeArrival *= commonGain * facadeGain * pathNorm;
                arrived = flushDenormal(arrived);
                groundArrival = flushDenormal(groundArrival);
                facadeArrival = flushDenormal(facadeArrival);

                siteLevel_[siteIndex] +=
                    (std::fabs(arrived) + std::fabs(groundArrival)
                        + std::fabs(facadeArrival) - siteLevel_[siteIndex])
                    * levelCoefficient;
                for (uint32_t channel = 0u;
                    channel < ambiChannels; ++channel) {
                    field[channel] += arrived * basis_[siteIndex][channel]
                        + groundArrival
                            * groundBasis_[siteIndex][channel]
                        + facadeArrival
                            * facadeBasis_[siteIndex][channel];
                }
            }
            for (uint32_t siteIndex = active;
                siteIndex < kAmbiCartographyMaxSites; ++siteIndex) {
                airDelay_[siteIndex][airWrite_] = 0.0f;
                siteLevel_[siteIndex] +=
                    (0.0f - siteLevel_[siteIndex]) * levelCoefficient;
            }

            fieldListener_.processFrame(field.data(), ambiChannels);
            for (uint32_t channel = 0u;
                channel < ambiChannels; ++channel) {
                if (!outputs[channel]) continue;
                outputs[channel][frameIndex] = static_cast<Sample>(
                    flushDenormal(field[channel] * smoothedOutputGain_));
            }

            networkWrite_ = (networkWrite_ + 1u) % networkCapacity_;
            airWrite_ = (airWrite_ + 1u) % airCapacity_;
            airHistorySamples_ = airAvailableSamples;
            if (landscape_.motion > 0.0f) {
                const float motionRateHz = lerp(
                    0.065f, 0.006f, params_.memory);
                motionPhase_ += motionRateHz
                    / static_cast<float>(sampleRate_);
                motionPhase_ -= std::floor(motionPhase_);
                if (motionControlCountdown_ > 0u) {
                    --motionControlCountdown_;
                }
                if (motionControlCountdown_ == 0u) {
                    updateGeometryAndRouting();
                    geometryDirty_ = false;
                    motionControlCountdown_ = 32u;
                }
            }
            if (engineCrossfade_ >= 1.0f
                && params_.macroEngine != activeEngine_) {
                // A retarget received during the preceding transition begins
                // on the next frame, whose first sample is exactly the current
                // active endpoint.
                previousEngine_ = activeEngine_;
                activeEngine_ = params_.macroEngine;
                engineCrossfade_ = 0.0f;
            }
        }
    }

private:
    template <typename Sample>
    static float finiteSample(Sample value)
    {
        const float result = static_cast<float>(value);
        return std::isfinite(result) ? result : 0.0f;
    }

    static float hashSigned(uint32_t index, uint32_t salt)
    {
        uint32_t value = index * 747796405u + salt * 2891336453u;
        value = ((value >> ((value >> 28u) + 4u)) ^ value) * 277803737u;
        value = (value >> 22u) ^ value;
        return static_cast<float>(value & 0xffffu) / 32767.5f - 1.0f;
    }

    static float readDelay(const std::vector<float>& line,
        uint32_t write, float delaySamples, uint32_t validSamples,
        bool writeContainsCurrent)
    {
        if (line.size() < 4u) return 0.0f;
        delaySamples = clamp(delaySamples, 0.0f,
            static_cast<float>(line.size() - 3u));
        float read = static_cast<float>(write) - delaySamples;
        while (read < 0.0f) read += static_cast<float>(line.size());
        const uint32_t first = static_cast<uint32_t>(std::floor(read))
            % static_cast<uint32_t>(line.size());
        const uint32_t second = (first + 1u)
            % static_cast<uint32_t>(line.size());
        const float fraction = read - std::floor(read);
        const auto sample = [&](uint32_t position) {
            const uint32_t capacity = static_cast<uint32_t>(line.size());
            uint32_t age = (write + capacity - position) % capacity;
            if (!writeContainsCurrent && age == 0u) age = capacity;
            const bool valid = writeContainsCurrent
                ? age < validSamples : age <= validSamples;
            return valid ? line[position] : 0.0f;
        };
        return lerp(sample(first), sample(second), fraction);
    }

    void resetLayout(AmbiCartographyLayout layout)
    {
        const uint32_t count = std::max<uint32_t>(1u, params_.activeSites);
        const uint32_t columns = 6u;
        for (uint32_t index = 0u;
            index < kAmbiCartographyMaxSites; ++index) {
            const float u = count > 1u
                ? static_cast<float>(std::min(index, count - 1u))
                    / static_cast<float>(count - 1u)
                : 0.5f;
            AmbiCartographySite site {};
            site.networkPosition = u;
            site.enabled = index < count;
            if (layout == AmbiCartographyLayout::Radial) {
                const float angle = (u * 2.0f - 0.25f) * kPi;
                const float radius = 0.72f
                    + 0.22f * static_cast<float>(index % 3u) / 2.0f;
                site.x = std::sin(angle) * radius;
                site.y = std::cos(angle) * radius;
                site.z = (static_cast<float>(index % 5u) - 2.0f) * 0.035f;
            } else if (layout == AmbiCartographyLayout::Ridge) {
                site.x = lerp(-1.0f, 1.0f, u);
                site.y = 0.42f + 0.17f * std::sin(u * 3.0f * kPi);
                site.z = 0.10f + 0.34f * std::sin(u * kPi);
            } else if (layout == AmbiCartographyLayout::Corridor) {
                site.x = (index & 1u) ? 0.24f : -0.24f;
                site.y = lerp(-1.0f, 1.0f, u);
                site.z = static_cast<float>(index % 4u) * 0.025f;
            } else if (layout == AmbiCartographyLayout::Grid) {
                const uint32_t row = index / columns;
                const uint32_t column = index % columns;
                site.x = -1.0f + static_cast<float>(column) * 0.40f;
                site.y = -0.75f + static_cast<float>(row) * 0.50f;
                site.z = ((row + column) & 1u) ? 0.06f : 0.0f;
                site.networkPosition = clamp(
                    (static_cast<float>(row) * 6.0f
                        + ((row & 1u) ? 5.0f - static_cast<float>(column)
                                      : static_cast<float>(column)))
                        / static_cast<float>(kAmbiCartographyMaxSites - 1u),
                    0.0f, 1.0f);
            } else {
                const float angle = lerp(-0.72f * kPi, 0.72f * kPi, u);
                site.x = std::sin(angle) * 1.0f;
                site.y = 0.12f + std::cos(angle) * 0.70f;
                site.z = 0.04f + 0.08f * std::sin(u * 4.0f * kPi);
            }
            sites_[index] = site;
        }
        geometryDirty_ = true;
        delayPrimed_ = false;
    }

    void syncSelectedFromSite()
    {
        params_.selectedSite = std::min<uint32_t>(
            params_.selectedSite, params_.activeSites - 1u);
        const auto& site = sites_[params_.selectedSite];
        params_.selectedX = site.x;
        params_.selectedY = site.y;
        params_.selectedZ = site.z;
        params_.selectedGain = site.gain;
        params_.selectedNetworkTrimMs = site.networkTrimMs;
        params_.selectedEnabled = site.enabled;
    }

    float stereoSiteInput(float left, float right, uint32_t site) const
    {
        if (params_.stereoMap == AmbiCartographyStereoMap::Mono) {
            return (left + right) * 0.5f;
        }
        if (params_.stereoMap == AmbiCartographyStereoMap::Alternate) {
            return (site & 1u) ? right : left;
        }
        const float mid = (left + right) * 0.5f;
        const float side = (left - right) * 0.5f;
        const float sideWeight = clamp(sites_[site].x, -1.0f, 1.0f);
        return mid + side * sideWeight;
    }

    void configureMacroEngines()
    {
        MacroDelayParams delay {};
        delay.timeMs = 10.0f * std::pow(200.0f, params_.macro);
        delay.feedback = params_.memory * 0.72f;
        delay.tone = params_.color;
        delay.character = 0.12f + params_.color * 0.58f;
        delay.smear = params_.memory * 0.35f;
        delay.spread = params_.spread;
        delay.deviation = params_.deviation;
        delay.skew = params_.skew;
        delay.center = params_.center;
        delay.mix = 1.0f;
        delay.outputGainDb = 0.0f;
        macroDelay_.setParams(delay);

        MacroPitchParams pitch {};
        pitch.pitchSemitones = (params_.macro * 2.0f - 1.0f) * 24.0f;
        pitch.windowMs = lerp(28.0f, 150.0f, params_.color);
        pitch.spread = params_.spread;
        pitch.deviation = params_.deviation;
        pitch.skew = params_.skew;
        pitch.center = params_.center;
        pitch.mix = 1.0f;
        pitch.outputGainDb = 0.0f;
        macroPitch_.setParams(pitch);

        MacroShredParams shred {};
        shred.pressure = params_.macro;
        shred.shred = params_.macro;
        shred.feedback = params_.memory * 0.78f;
        shred.color = params_.color;
        shred.react = 0.18f + params_.memory * 0.52f;
        shred.tune = 0.25f + params_.color * 0.70f;
        shred.body = 0.35f + params_.color * 0.45f;
        shred.spread = params_.spread;
        shred.deviation = params_.deviation;
        shred.skew = params_.skew;
        shred.center = params_.center;
        shred.mix = 1.0f;
        shred.outputGainDb = 0.0f;
        shred.circuit = siteProcessOptions_.shredCircuit;
        macroShred_.setParams(shred);

        MacroFractureParams fracture {};
        fracture.processor = siteProcessOptions_.fractureProcessor;
        fracture.amount = params_.macro;
        fracture.color = params_.color;
        fracture.bias = params_.skew;
        fracture.react = 0.15f + params_.memory * 0.60f;
        fracture.memory = params_.memory;
        fracture.spread = params_.spread;
        fracture.deviation = params_.deviation;
        // In Cartography, the shared SKEW control becomes Fracture's
        // processor-specific third control (Balance, Direction, Shift, and
        // so on). Do not also apply it as lane skew; Deviation still gives
        // each relationship lane a bounded variation around that bias.
        fracture.skew = 0.0f;
        fracture.center = params_.center;
        fracture.mix = 1.0f;
        fracture.outputGainDb = 0.0f;
        macroFracture_.setParams(fracture);

        CartographySiteProcessParams siteProcess {};
        siteProcess.macro = params_.macro;
        siteProcess.color = params_.color;
        siteProcess.memory = params_.memory;
        siteProcess.spread = params_.spread;
        siteProcess.deviation = params_.deviation;
        siteProcess.skew = params_.skew;
        siteProcess.center = params_.center;
        speakerBody_.setParams(siteProcess);
        spectralRelay_.setParams(siteProcess);
        relayBuffer_.setParams(siteProcess);
    }

    static bool usesCompactSiteLanes(AmbiCartographyMacroEngine engine)
    {
        return engine == AmbiCartographyMacroEngine::SpectralRelay
            || engine == AmbiCartographyMacroEngine::RelayBuffer;
    }

    static bool needsRemapContinuity(AmbiCartographyMacroEngine engine)
    {
        return engine != AmbiCartographyMacroEngine::Clean
            && engine != AmbiCartographyMacroEngine::SpeakerBody;
    }

    void processMacroFrame(AmbiCartographyMacroEngine engine,
        const std::array<float, kAmbiCartographyMaxSites>& input,
        std::array<float, kAmbiCartographyMaxSites>& output,
        const std::array<float, kAmbiCartographyMaxSites>*
            fractureModulation = nullptr)
    {
        switch (engine) {
        case AmbiCartographyMacroEngine::Delay:
            macroDelay_.processFrame(input.data(), output.data());
            break;
        case AmbiCartographyMacroEngine::Pitch:
            macroPitch_.processFrame(input.data(), output.data());
            break;
        case AmbiCartographyMacroEngine::Shred:
            macroShred_.processFrame(input.data(), output.data());
            break;
        case AmbiCartographyMacroEngine::Fracture:
            macroFracture_.processFrame(input.data(), output.data(),
                fractureModulation ? fractureModulation->data() : nullptr);
            break;
        case AmbiCartographyMacroEngine::SpeakerBody:
            speakerBody_.processFrame(
                input.data(), output.data(), bodyLaneUnit_.data());
            break;
        case AmbiCartographyMacroEngine::SpectralRelay:
            spectralRelay_.processFrame(input.data(), output.data());
            break;
        case AmbiCartographyMacroEngine::RelayBuffer:
            relayBuffer_.processFrame(input.data(), output.data());
            break;
        default:
            output = input;
            break;
        }
    }

    static float smoothStep(float edge0, float edge1, float value)
    {
        const float t = clamp((value - edge0)
            / std::max(1.0e-6f, edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    static float layoutObstruction(AmbiCartographyLayout layout,
        const Vec3& site, const Vec3& listener)
    {
        const float x = (site.x + listener.x) * 0.5f;
        const float y = (site.y + listener.y) * 0.5f;
        const float z = (site.z + listener.z) * 0.5f;
        const float dx = site.x - listener.x;
        const float dy = site.y - listener.y;
        const float path = std::sqrt(dx * dx + dy * dy);
        float obstacle = 0.0f;
        if (layout == AmbiCartographyLayout::Ridge) {
            const float ridge = std::exp(-std::pow((y - 0.34f) / 0.30f, 2.0f));
            obstacle = ridge * (0.52f + 0.14f * std::cos(x * 4.2f));
        } else if (layout == AmbiCartographyLayout::Corridor) {
            obstacle = 0.26f + 0.34f
                * (1.0f - clamp(std::fabs(x) / 0.42f, 0.0f, 1.0f));
        } else if (layout == AmbiCartographyLayout::Grid) {
            obstacle = 0.30f + 0.22f
                * std::fabs(std::sin(x * 5.3f) * std::cos(y * 4.7f));
        } else if (layout == AmbiCartographyLayout::Waterfront) {
            obstacle = 0.12f + 0.20f
                * std::exp(-std::pow((y - 0.18f) / 0.42f, 2.0f));
        } else {
            obstacle = 0.10f + 0.30f
                * std::exp(-(x * x + y * y) / 0.20f);
        }
        return clamp((obstacle - z - 0.06f) * 2.2f
            + path * 0.08f, 0.0f, 1.0f);
    }

    void updateGeometryAndRouting()
    {
        const auto previousSiteToLane = siteToLane_;
        const auto previousSiteToProcessLane = siteToProcessLane_;
        const uint32_t previousProcessLaneCount = processLaneCount_;
        const uint32_t previousActiveSites = routingActiveSites_;
        const uint32_t active = params_.activeSites;
        renderedListenerPosition_ = {
            params_.listenerX, params_.listenerY, params_.listenerZ };
        for (uint32_t site = 0u; site < active; ++site) {
            renderedSitePosition_[site] = {
                sites_[site].x, sites_[site].y, sites_[site].z };
        }
        if (landscape_.motion > 0.0f) {
            const float phase = motionPhase_ * 2.0f * kPi;
            const float amount = landscape_.motion;
            if (params_.layout == AmbiCartographyLayout::Radial) {
                renderedListenerPosition_.x += std::cos(phase) * amount * 0.52f;
                renderedListenerPosition_.y += std::sin(phase) * amount * 0.52f;
            } else if (params_.layout == AmbiCartographyLayout::Ridge) {
                for (uint32_t site = 0u; site < active; ++site) {
                    const float offset = phase
                        + sites_[site].networkPosition * 2.0f * kPi;
                    renderedSitePosition_[site].y +=
                        std::sin(offset) * amount * 0.18f;
                    renderedSitePosition_[site].z +=
                        std::cos(offset) * amount * 0.12f;
                }
            } else if (params_.layout == AmbiCartographyLayout::Corridor) {
                for (uint32_t site = 0u; site < active; ++site) {
                    const float polarity = (site & 1u) ? 1.0f : -1.0f;
                    renderedSitePosition_[site].y += polarity
                        * std::sin(phase) * amount * 0.34f;
                }
            } else if (params_.layout == AmbiCartographyLayout::Grid) {
                for (uint32_t site = 0u; site < active; ++site) {
                    const float offset = phase
                        + sites_[site].networkPosition * 2.0f * kPi;
                    renderedSitePosition_[site].x +=
                        std::sin(offset) * amount * 0.22f;
                    renderedSitePosition_[site].y +=
                        std::cos(offset) * amount * 0.16f;
                }
            } else {
                renderedListenerPosition_.x +=
                    std::sin(phase) * amount * 0.92f;
                renderedListenerPosition_.y +=
                    std::cos(phase) * amount * 0.24f;
            }
        }

        float minAirSeconds = 1.0e9f;
        float minNetworkPosition = 1.0f;
        float maxNetworkPosition = 0.0f;
        for (uint32_t site = 0u; site < active; ++site) {
            minNetworkPosition = std::min(
                minNetworkPosition, sites_[site].networkPosition);
            maxNetworkPosition = std::max(
                maxNetworkPosition, sites_[site].networkPosition);
        }
        const float networkSpan = std::max(
            1.0e-6f, maxNetworkPosition - minNetworkPosition);

        for (uint32_t site = 0u; site < active; ++site) {
            const auto& renderedSite = renderedSitePosition_[site];
            const float dx = renderedSite.x - renderedListenerPosition_.x;
            const float dy = renderedSite.y - renderedListenerPosition_.y;
            const float dz = renderedSite.z - renderedListenerPosition_.z;
            Vec3 vector { dx, dy, dz };
            const float normalizedDistance = std::sqrt(
                dx * dx + dy * dy + dz * dz);
            distanceMeters_[site] = normalizedDistance
                * params_.mapScaleMeters;
            float soundSpeedScale = 1.0f;
            refractionGain_[site] = 1.0f;
            if (std::fabs(landscape_.refraction) > 1.0e-8f) {
                const float horizontal = std::sqrt(dx * dx + dy * dy);
                const float bearing = params_.skew * kPi;
                const float windX = std::sin(bearing);
                const float windY = std::cos(bearing);
                const float alignment = horizontal > 1.0e-6f
                    ? (dx * windX + dy * windY) / horizontal : 0.0f;
                soundSpeedScale = clamp(1.0f
                    + landscape_.refraction * alignment
                        * (0.025f + params_.carry * 0.055f),
                    0.90f, 1.10f);
                const float range = clamp(normalizedDistance / 2.0f,
                    0.0f, 1.0f);
                vector.z += landscape_.refraction
                    * (0.04f + 0.18f * range)
                    * (0.35f + params_.air * 0.65f);
                refractionGain_[site] = clamp(1.0f
                    + landscape_.refraction * alignment
                        * (0.06f + params_.carry * 0.22f),
                    0.70f, 1.30f);
            }
            // Cartography uses conventional map axes (+X screen-right,
            // +Y/front, +Z/up). Convert them to the ambisonic axes
            // (+X/front, +Y/listener-left, +Z/up) before encoding.
            const Vec3 ambisonicVector { vector.y, -vector.x, vector.z };
            directions_[site] = normalizedDistance > 1.0e-6f
                ? normalize(ambisonicVector) : Vec3 { 1.0f, 0.0f, 0.0f };
            basis_[site] = acnSn3dBasis7(directions_[site]);
            const float airSeconds = distanceMeters_[site]
                / (343.0f * soundSpeedScale) * params_.propagationScale;
            if (sites_[site].enabled) {
                minAirSeconds = std::min(minAirSeconds, airSeconds);
            }

            if (landscape_.multipath > 0.0f) {
                const float groundOffset = clamp(
                    2.0f / std::max(10.0f, params_.mapScaleMeters),
                    0.001f, 0.20f);
                const float groundZ = std::min(renderedSite.z,
                    renderedListenerPosition_.z) - groundOffset;
                Vec3 groundVector {
                    dx, dy,
                    2.0f * groundZ - renderedSite.z
                        - renderedListenerPosition_.z };
                const float groundDistance = std::sqrt(
                    groundVector.x * groundVector.x
                        + groundVector.y * groundVector.y
                        + groundVector.z * groundVector.z)
                    * params_.mapScaleMeters;
                const Vec3 groundAmbisonic {
                    groundVector.y, -groundVector.x, groundVector.z };
                groundBasis_[site] = acnSn3dBasis7(
                    normalize(groundAmbisonic));
                groundExtraDelay_[site] = clamp(
                    std::max(0.0f,
                        groundDistance - distanceMeters_[site])
                        / 343.0f * lerp(0.18f, 1.0f,
                            params_.propagationScale)
                        * static_cast<float>(sampleRate_),
                    0.0f, static_cast<float>(airCapacity_ - 3u));

                const float wallX = (renderedSite.x
                        + renderedListenerPosition_.x) >= 0.0f
                    ? 1.45f : -1.45f;
                Vec3 facadeVector {
                    2.0f * wallX - renderedSite.x
                        - renderedListenerPosition_.x,
                    dy, dz };
                const float facadeDistance = std::sqrt(
                    facadeVector.x * facadeVector.x
                        + facadeVector.y * facadeVector.y
                        + facadeVector.z * facadeVector.z)
                    * params_.mapScaleMeters;
                const Vec3 facadeAmbisonic {
                    facadeVector.y, -facadeVector.x, facadeVector.z };
                facadeBasis_[site] = acnSn3dBasis7(
                    normalize(facadeAmbisonic));
                facadeExtraDelay_[site] = clamp(
                    std::max(0.0f,
                        facadeDistance - distanceMeters_[site])
                        / 343.0f * lerp(0.18f, 1.0f,
                            params_.propagationScale)
                        * static_cast<float>(sampleRate_),
                    0.0f, static_cast<float>(airCapacity_ - 3u));
            } else {
                groundBasis_[site].fill(0.0f);
                facadeBasis_[site].fill(0.0f);
                groundExtraDelay_[site] = 0.0f;
                facadeExtraDelay_[site] = 0.0f;
            }

            renderedOcclusion_[site] = landscape_.occlusion
                * layoutObstruction(params_.layout,
                    renderedSite, renderedListenerPosition_);
            const float horizonDistance = params_.mapScaleMeters
                * (0.70f + params_.carry * 2.30f);
            const float beyond = smoothStep(horizonDistance * 0.70f,
                horizonDistance * 1.25f, distanceMeters_[site]);
            horizonGain_[site] = 1.0f
                - landscape_.horizon * beyond;

            const float networkNorm = (sites_[site].networkPosition
                - minNetworkPosition) / networkSpan;
            const float networkMs = clamp(
                networkNorm * params_.networkSpreadMs
                    + sites_[site].networkTrimMs,
                0.0f, 2400.0f);
            targetNetworkDelay_[site] = clamp(
                networkMs * 0.001f * static_cast<float>(sampleRate_),
                0.0f, static_cast<float>(networkCapacity_ - 3u));
            arrivalSeconds_[site] = networkMs * 0.001f + airSeconds;
        }
        if (!std::isfinite(minAirSeconds)) minAirSeconds = 0.0f;

        for (uint32_t site = 0u; site < active; ++site) {
            const float dx = renderedSitePosition_[site].x
                - renderedListenerPosition_.x;
            const float dy = renderedSitePosition_[site].y
                - renderedListenerPosition_.y;
            const float horizontal = std::sqrt(dx * dx + dy * dy);
            float soundSpeedScale = 1.0f;
            if (std::fabs(landscape_.refraction) > 1.0e-8f) {
                const float bearing = params_.skew * kPi;
                const float alignment = horizontal > 1.0e-6f
                    ? (dx * std::sin(bearing) + dy * std::cos(bearing))
                        / horizontal : 0.0f;
                soundSpeedScale = clamp(1.0f
                    + landscape_.refraction * alignment
                        * (0.025f + params_.carry * 0.055f),
                    0.90f, 1.10f);
            }
            const float airSeconds = distanceMeters_[site]
                / (343.0f * soundSpeedScale) * params_.propagationScale;
            const float renderedSeconds = params_.timeReference
                    == AmbiCartographyTimeReference::Relative
                ? std::max(0.0f, airSeconds - minAirSeconds)
                : airSeconds;
            targetAirDelay_[site] = clamp(
                renderedSeconds * static_cast<float>(sampleRate_),
                0.0f, static_cast<float>(airCapacity_ - 3u));
        }
        for (uint32_t site = active;
            site < kAmbiCartographyMaxSites; ++site) {
            distanceMeters_[site] = 0.0f;
            arrivalSeconds_[site] = 0.0f;
            directions_[site] = {};
            basis_[site].fill(0.0f);
            groundBasis_[site].fill(0.0f);
            facadeBasis_[site].fill(0.0f);
            targetNetworkDelay_[site] = 0.0f;
            targetAirDelay_[site] = 0.0f;
            groundExtraDelay_[site] = 0.0f;
            facadeExtraDelay_[site] = 0.0f;
            renderedOcclusion_[site] = 0.0f;
            horizonGain_[site] = 1.0f;
            refractionGain_[site] = 1.0f;
            renderedSitePosition_[site] = {};
        }

        std::array<float, kAmbiCartographyMaxSites> metric {};
        for (uint32_t site = 0u; site < active; ++site) {
            if (params_.macroMetric == AmbiCartographyMacroMetric::Arrival) {
                metric[site] = arrivalSeconds_[site];
            } else if (params_.macroMetric
                == AmbiCartographyMacroMetric::Bearing) {
                metric[site] = std::atan2(
                    directions_[site].y, directions_[site].x);
            } else if (params_.macroMetric
                == AmbiCartographyMacroMetric::Range) {
                metric[site] = distanceMeters_[site];
            } else {
                metric[site] = sites_[site].networkPosition;
            }
            laneToSite_[site] = site;
        }
        // Stable insertion sort avoids allocation in the audio callback.
        for (uint32_t index = 1u; index < active; ++index) {
            const uint32_t candidate = laneToSite_[index];
            uint32_t cursor = index;
            while (cursor > 0u
                && metric[candidate] < metric[laneToSite_[cursor - 1u]]) {
                laneToSite_[cursor] = laneToSite_[cursor - 1u];
                --cursor;
            }
            laneToSite_[cursor] = candidate;
        }
        for (uint32_t rank = 0u; rank < active; ++rank) {
            // The reusable macro processors own 24 persistent lanes. Spread
            // any smaller site population across that complete ordinal range
            // so SPREAD, SKEW, and CENTER retain their authored meaning.
            const uint32_t macroLane = active > 1u
                ? static_cast<uint32_t>(std::lround(
                    static_cast<float>(rank)
                    * static_cast<float>(kAmbiCartographyMaxSites - 1u)
                    / static_cast<float>(active - 1u)))
                : kAmbiCartographyMaxSites / 2u;
            siteToLane_[laneToSite_[rank]] = macroLane;
            bodyLaneUnit_[laneToSite_[rank]] =
                static_cast<float>(macroLane)
                / static_cast<float>(kAmbiCartographyMaxSites - 1u);
        }
        // The cartography-native cross-lane engines instead operate on a
        // compact list of actual enabled sites. Feeding their modulo routing
        // through the sparse 24-lane map would turn the unused ordinal gaps
        // into silent phantom sites. Persistent process memory belongs to the
        // relationship rank: moving or reordering sites traverses that field.
        // Topology changes preserve overlapping lane memory and receive a
        // short output correction below so that the remap is continuous.
        siteToProcessLane_.fill(0u);
        for (uint32_t site = 0u;
            site < kAmbiCartographyMaxSites; ++site) {
            fractureModulatorSite_[site] = site;
        }
        processLaneCount_ = 0u;
        std::array<uint32_t, kAmbiCartographyMaxSites> enabledByRank {};
        for (uint32_t rank = 0u; rank < active; ++rank) {
            const uint32_t site = laneToSite_[rank];
            if (!sites_[site].enabled) continue;
            enabledByRank[processLaneCount_] = site;
            siteToProcessLane_[site] = processLaneCount_++;
        }
        if (processLaneCount_ > 0u) {
            for (uint32_t rank = 0u; rank < processLaneCount_; ++rank) {
                fractureModulatorSite_[enabledByRank[rank]] =
                    enabledByRank[(rank + 1u) % processLaneCount_];
            }
        }
        // Body resonance state belongs to a physical site. Its relationship
        // rank is a smoothed voicing coordinate, never a state-array index.
        speakerBody_.setActiveChannels(active);
        spectralRelay_.setActiveChannels(processLaneCount_);
        relayBuffer_.setActiveChannels(processLaneCount_);
        const bool protectRemap = needsRemapContinuity(activeEngine_)
            || (previousEngine_ != activeEngine_
                && engineCrossfade_ < 1.0f
                && needsRemapContinuity(previousEngine_));
        if (routingContinuityPrimed_ && protectRemap) {
            const bool cardinalityChanged = previousActiveSites != active
                || previousProcessLaneCount != processLaneCount_;
            for (uint32_t site = 0u; site < active; ++site) {
                if (cardinalityChanged
                    || previousSiteToLane[site] != siteToLane_[site]
                    || previousSiteToProcessLane[site]
                        != siteToProcessLane_[site]) {
                    wetRemapPending_[site] = true;
                }
            }
        }
        routingContinuityPrimed_ = true;
        routingActiveSites_ = active;
        for (uint32_t site = active;
            site < kAmbiCartographyMaxSites; ++site) {
            siteToLane_[site] = site;
            laneToSite_[site] = site;
        }
    }

    double sampleRate_ = 48000.0;
    uint32_t networkCapacity_ = 128u;
    uint32_t airCapacity_ = 128u;
    uint32_t networkWrite_ = 0u;
    uint32_t airWrite_ = 0u;
    uint32_t networkHistorySamples_ = 0u;
    uint32_t airHistorySamples_ = 0u;
    bool prepared_ = false;
    bool delayPrimed_ = false;
    bool geometryDirty_ = true;
    // Engine memories remain parked while inactive: clearing multi-megabyte
    // delay and capture stores from a parameter event would violate realtime
    // bounds. Re-entry always runs through the live old/new crossfade above,
    // so a parked tail can never return as a hard switch.
    AmbiCartographyMacroEngine activeEngine_ =
        AmbiCartographyMacroEngine::Delay;
    AmbiCartographyMacroEngine previousEngine_ =
        AmbiCartographyMacroEngine::Delay;
    float engineCrossfade_ = 1.0f;
    float motionPhase_ = 0.0f;
    uint32_t motionControlCountdown_ = 32u;
    AmbiCartographyEncoderParams params_ {};
    AmbiCartographyLandscapeParams landscape_ {};
    AmbiCartographySiteProcessOptions siteProcessOptions_ {};
    std::array<AmbiCartographySite, kAmbiCartographyMaxSites> sites_ {};
    std::vector<float> networkLeft_ {};
    std::vector<float> networkRight_ {};
    std::array<std::vector<float>, kAmbiCartographyMaxSites> airDelay_ {};
    std::array<float, kAmbiCartographyMaxSites> targetNetworkDelay_ {};
    std::array<float, kAmbiCartographyMaxSites> targetAirDelay_ {};
    std::array<float, kAmbiCartographyMaxSites> smoothedNetworkDelay_ {};
    std::array<float, kAmbiCartographyMaxSites> smoothedAirDelay_ {};
    std::array<float, kAmbiCartographyMaxSites> airState_ {};
    std::array<float, kAmbiCartographyMaxSites> groundState_ {};
    std::array<float, kAmbiCartographyMaxSites> facadeState_ {};
    std::array<float, kAmbiCartographyMaxSites> occlusionState_ {};
    std::array<float, kAmbiCartographyMaxSites> horizonState_ {};
    std::array<float, kAmbiCartographyMaxSites> smoothedOcclusion_ {};
    std::array<float, kAmbiCartographyMaxSites> smoothedHorizonGain_ {};
    std::array<float, kAmbiCartographyMaxSites> smoothedSiteGain_ {};
    std::array<float, kAmbiCartographyMaxSites> smoothedSiteEnabled_ {};
    std::array<float, kAmbiCartographyMaxSites> ecologyState_ {};
    std::array<float, kAmbiCartographyMaxSites> turbulencePhase_ {};
    std::array<float, kAmbiCartographyMaxSites> weatherPhase_ {};
    std::array<float, kAmbiCartographyMaxSites> weatherDriftPhase_ {};
    std::array<float, kAmbiCartographyMaxSites> weatherPulsePhase_ {};
    std::array<float, kAmbiCartographyMaxSites> distanceMeters_ {};
    std::array<float, kAmbiCartographyMaxSites> arrivalSeconds_ {};
    std::array<float, kAmbiCartographyMaxSites> siteLevel_ {};
    std::array<float, kAmbiCartographyMaxSites> groundExtraDelay_ {};
    std::array<float, kAmbiCartographyMaxSites> facadeExtraDelay_ {};
    std::array<float, kAmbiCartographyMaxSites> renderedOcclusion_ {};
    std::array<float, kAmbiCartographyMaxSites> horizonGain_ {};
    std::array<float, kAmbiCartographyMaxSites> refractionGain_ {};
    Vec3 renderedListenerPosition_ {};
    std::array<Vec3, kAmbiCartographyMaxSites> renderedSitePosition_ {};
    std::array<Vec3, kAmbiCartographyMaxSites> directions_ {};
    std::array<std::array<float, kAmbiCartographyMaxChannels>,
        kAmbiCartographyMaxSites> basis_ {};
    std::array<std::array<float, kAmbiCartographyMaxChannels>,
        kAmbiCartographyMaxSites> groundBasis_ {};
    std::array<std::array<float, kAmbiCartographyMaxChannels>,
        kAmbiCartographyMaxSites> facadeBasis_ {};
    std::array<uint32_t, kAmbiCartographyMaxSites> siteToLane_ {};
    std::array<uint32_t, kAmbiCartographyMaxSites> siteToProcessLane_ {};
    std::array<uint32_t, kAmbiCartographyMaxSites>
        fractureModulatorSite_ {};
    std::array<float, kAmbiCartographyMaxSites> bodyLaneUnit_ {};
    std::array<uint32_t, kAmbiCartographyMaxSites> laneToSite_ {};
    std::array<float, kAmbiCartographyMaxSites> lastWetBySite_ {};
    std::array<float, kAmbiCartographyMaxSites> wetRemapCorrection_ {};
    std::array<float, kAmbiCartographyMaxSites> wetRemapPhase_ {};
    std::array<bool, kAmbiCartographyMaxSites> wetRemapPending_ {};
    std::array<bool, kAmbiCartographyMaxSites> wetRemapValid_ {};
    uint32_t processLaneCount_ = 0u;
    uint32_t routingActiveSites_ = 0u;
    bool routingContinuityPrimed_ = false;
    float smoothedProcessMix_ = 0.42f;
    float smoothedOutputGain_ = 0.5f;
    MacroDelay macroDelay_ {};
    MacroPitch macroPitch_ {};
    MacroShred macroShred_ {};
    MacroFracture macroFracture_ {};
    CartographySpeakerBody speakerBody_ {};
    CartographySpectralRelay spectralRelay_ {};
    CartographyRelayBuffer relayBuffer_ {};
    AmbiFieldListener fieldListener_ {};
};

} // namespace s3g
