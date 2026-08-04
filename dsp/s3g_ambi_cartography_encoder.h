#pragma once

#include "s3g_ambi_field_listener.h"
#include "s3g_ambisonic_speaker_decoder.h"
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
        std::min<uint32_t>(static_cast<uint32_t>(params.macroEngine), 4u));
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
        std::fill(networkLeft_.begin(), networkLeft_.end(), 0.0f);
        std::fill(networkRight_.begin(), networkRight_.end(), 0.0f);
        for (auto& line : airDelay_) std::fill(line.begin(), line.end(), 0.0f);
        networkWrite_ = 0u;
        airWrite_ = 0u;
        smoothedNetworkDelay_.fill(0.0f);
        smoothedAirDelay_.fill(0.0f);
        airState_.fill(0.0f);
        siteLevel_.fill(0.0f);
        turbulencePhase_.fill(0.0f);
        for (uint32_t site = 0u; site < kAmbiCartographyMaxSites; ++site) {
            turbulencePhase_[site] = hashSigned(site, 41u) * 0.5f + 0.5f;
        }
        delayPrimed_ = false;
        engineFade_ = 1.0f;
        macroDelay_.reset();
        macroPitch_.reset();
        macroShred_.reset();
        macroFracture_.reset();
        fieldListener_.reset();
    }

    void setParams(AmbiCartographyEncoderParams params)
    {
        const auto previousLayout = params_.layout;
        const uint32_t previousSites = params_.activeSites;
        const uint32_t previousSelected = params_.selectedSite;
        const auto previousEngine = params_.macroEngine;
        params = sanitizeAmbiCartographyEncoderParams(params);

        const bool layoutChanged = params.layout != previousLayout
            || params.activeSites != previousSites;
        params_ = params;
        if (layoutChanged) {
            resetLayout(params_.layout);
            syncSelectedFromSite();
        } else if (params_.selectedSite != previousSelected) {
            syncSelectedFromSite();
        } else {
            auto& site = sites_[params_.selectedSite];
            site.x = params_.selectedX;
            site.y = params_.selectedY;
            site.z = params_.selectedZ;
            site.gain = params_.selectedGain;
            site.networkTrimMs = params_.selectedNetworkTrimMs;
            site.enabled = params_.selectedEnabled;
        }
        if (params_.macroEngine != previousEngine) engineFade_ = 0.0f;
        configureMacroEngines();
    }

    void regenerateLayout()
    {
        resetLayout(params_.layout);
        syncSelectedFromSite();
        delayPrimed_ = false;
    }

    AmbiCartographyEncoderParams params() const { return params_; }

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

    const AmbiFieldListener& fieldListener() const { return fieldListener_; }

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

        updateGeometryAndRouting();
        const uint32_t ambiChannels = std::min<uint32_t>(
            ambiChannelsForOrder(params_.order), outputChannels);
        const uint32_t active = params_.activeSites;
        const float outputGain = dbToGain(params_.outputGainDb);
        const float siteNorm = 1.0f / static_cast<float>(active);
        const float delayCoefficient = 1.0f - std::exp(-1.0f
            / static_cast<float>(sampleRate_ * 0.030));
        const float levelCoefficient = 1.0f - std::exp(-1.0f
            / static_cast<float>(sampleRate_ * 0.060));
        const float engineFadeStep = 1.0f
            / static_cast<float>(sampleRate_ * 0.030);

        std::array<float, kAmbiCartographyMaxSites> siteInput {};
        std::array<float, kAmbiCartographyMaxSites> macroInput {};
        std::array<float, kAmbiCartographyMaxSites> macroOutput {};
        std::array<float, kAmbiCartographyMaxChannels> field {};

        for (uint32_t frameIndex = 0u; frameIndex < frames; ++frameIndex) {
            const float left = inputs && inputChannels > 0u && inputs[0]
                ? finiteSample(inputs[0][frameIndex]) : 0.0f;
            const float right = inputs && inputChannels > 1u && inputs[1]
                ? finiteSample(inputs[1][frameIndex]) : left;
            networkLeft_[networkWrite_] = left;
            networkRight_[networkWrite_] = right;

            if (!delayPrimed_) {
                smoothedNetworkDelay_ = targetNetworkDelay_;
                smoothedAirDelay_ = targetAirDelay_;
                delayPrimed_ = true;
            }

            macroInput.fill(0.0f);
            for (uint32_t siteIndex = 0u; siteIndex < active; ++siteIndex) {
                smoothedNetworkDelay_[siteIndex] +=
                    (targetNetworkDelay_[siteIndex]
                        - smoothedNetworkDelay_[siteIndex])
                    * delayCoefficient;
                const float siteLeft = readDelay(
                    networkLeft_, networkWrite_,
                    smoothedNetworkDelay_[siteIndex]);
                const float siteRight = readDelay(
                    networkRight_, networkWrite_,
                    smoothedNetworkDelay_[siteIndex]);
                siteInput[siteIndex] = stereoSiteInput(
                    siteLeft, siteRight, siteIndex);
                macroInput[siteToLane_[siteIndex]] = siteInput[siteIndex];
            }
            for (uint32_t siteIndex = active;
                siteIndex < kAmbiCartographyMaxSites; ++siteIndex) {
                siteInput[siteIndex] = 0.0f;
            }

            processMacroFrame(macroInput, macroOutput);
            engineFade_ = std::min(1.0f, engineFade_ + engineFadeStep);
            field.fill(0.0f);
            const float listenerActivity = fieldListener_.activity();

            for (uint32_t siteIndex = 0u; siteIndex < active; ++siteIndex) {
                const auto& site = sites_[siteIndex];
                const float wet = macroOutput[siteToLane_[siteIndex]];
                const float preference = fieldListener_.preference(
                    directions_[siteIndex], params_.listenMode);
                const float centeredPreference = (preference - 0.5f) * 2.0f;
                const float listenerDepth = params_.listenerAmount
                    * listenerActivity;
                const float adaptiveWet = clamp(
                    params_.processMix
                        * (1.0f + centeredPreference
                            * listenerDepth * 0.80f),
                    0.0f, 1.0f) * engineFade_;
                const float processed = params_.macroEngine
                        == AmbiCartographyMacroEngine::Clean
                    ? siteInput[siteIndex]
                    : lerp(siteInput[siteIndex], wet, adaptiveWet);

                airDelay_[siteIndex][airWrite_] = site.enabled
                    ? flushDenormal(processed) : 0.0f;
                smoothedAirDelay_[siteIndex] +=
                    (targetAirDelay_[siteIndex]
                        - smoothedAirDelay_[siteIndex])
                    * delayCoefficient;
                float arrived = readDelay(airDelay_[siteIndex], airWrite_,
                    smoothedAirDelay_[siteIndex]);

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

                turbulencePhase_[siteIndex] +=
                    (0.017f + static_cast<float>(siteIndex % 7u) * 0.004f)
                    / static_cast<float>(sampleRate_);
                turbulencePhase_[siteIndex] -=
                    std::floor(turbulencePhase_[siteIndex]);
                const float turbulence = 1.0f + params_.turbulence
                    * (0.075f * std::sin(
                           turbulencePhase_[siteIndex] * 2.0f * kPi)
                        + 0.025f * hashSigned(siteIndex,
                            static_cast<uint32_t>(
                                turbulencePhase_[siteIndex] * 97.0f)));
                const float lossExponent = params_.distanceLoss
                    * (1.0f - params_.carry * 0.78f) * 0.68f;
                const float distanceGain = std::pow(
                    1.0f + distanceMeters_[siteIndex] / 25.0f,
                    -lossExponent);
                const float reach = clamp(
                    1.0f + centeredPreference * listenerDepth * 0.18f,
                    0.72f, 1.28f);
                arrived *= site.gain * distanceGain * turbulence
                    * reach * siteNorm;
                arrived = flushDenormal(arrived);

                siteLevel_[siteIndex] +=
                    (std::fabs(arrived) - siteLevel_[siteIndex])
                    * levelCoefficient;
                for (uint32_t channel = 0u;
                    channel < ambiChannels; ++channel) {
                    field[channel] += arrived * basis_[siteIndex][channel];
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
                    flushDenormal(field[channel] * outputGain));
            }

            networkWrite_ = (networkWrite_ + 1u) % networkCapacity_;
            airWrite_ = (airWrite_ + 1u) % airCapacity_;
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
        uint32_t write, float delaySamples)
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
        return lerp(line[first], line[second], fraction);
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
        macroShred_.setParams(shred);

        MacroFractureParams fracture {};
        fracture.processor = FractureProcessor::Relay;
        fracture.amount = params_.macro;
        fracture.color = params_.color;
        fracture.react = 0.15f + params_.memory * 0.60f;
        fracture.memory = params_.memory;
        fracture.spread = params_.spread;
        fracture.deviation = params_.deviation;
        fracture.skew = params_.skew;
        fracture.center = params_.center;
        fracture.mix = 1.0f;
        fracture.outputGainDb = 0.0f;
        macroFracture_.setParams(fracture);
    }

    void processMacroFrame(
        const std::array<float, kAmbiCartographyMaxSites>& input,
        std::array<float, kAmbiCartographyMaxSites>& output)
    {
        switch (params_.macroEngine) {
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
            macroFracture_.processFrame(input.data(), output.data());
            break;
        default:
            output = input;
            break;
        }
    }

    void updateGeometryAndRouting()
    {
        const uint32_t active = params_.activeSites;
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
            const float dx = sites_[site].x - params_.listenerX;
            const float dy = sites_[site].y - params_.listenerY;
            const float dz = sites_[site].z - params_.listenerZ;
            Vec3 vector { dx, dy, dz };
            const float normalizedDistance = std::sqrt(
                dx * dx + dy * dy + dz * dz);
            distanceMeters_[site] = normalizedDistance
                * params_.mapScaleMeters;
            directions_[site] = normalizedDistance > 1.0e-6f
                ? normalize(vector) : Vec3 { 0.0f, 1.0f, 0.0f };
            basis_[site] = acnSn3dBasis7(directions_[site]);
            const float airSeconds = distanceMeters_[site]
                / 343.0f * params_.propagationScale;
            if (sites_[site].enabled) {
                minAirSeconds = std::min(minAirSeconds, airSeconds);
            }

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
            const float airSeconds = distanceMeters_[site]
                / 343.0f * params_.propagationScale;
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
            targetNetworkDelay_[site] = 0.0f;
            targetAirDelay_[site] = 0.0f;
        }

        std::array<float, kAmbiCartographyMaxSites> metric {};
        for (uint32_t site = 0u; site < active; ++site) {
            if (params_.macroMetric == AmbiCartographyMacroMetric::Arrival) {
                metric[site] = arrivalSeconds_[site];
            } else if (params_.macroMetric
                == AmbiCartographyMacroMetric::Bearing) {
                metric[site] = std::atan2(
                    directions_[site].x, directions_[site].y);
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
        }
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
    bool prepared_ = false;
    bool delayPrimed_ = false;
    float engineFade_ = 1.0f;
    AmbiCartographyEncoderParams params_ {};
    std::array<AmbiCartographySite, kAmbiCartographyMaxSites> sites_ {};
    std::vector<float> networkLeft_ {};
    std::vector<float> networkRight_ {};
    std::array<std::vector<float>, kAmbiCartographyMaxSites> airDelay_ {};
    std::array<float, kAmbiCartographyMaxSites> targetNetworkDelay_ {};
    std::array<float, kAmbiCartographyMaxSites> targetAirDelay_ {};
    std::array<float, kAmbiCartographyMaxSites> smoothedNetworkDelay_ {};
    std::array<float, kAmbiCartographyMaxSites> smoothedAirDelay_ {};
    std::array<float, kAmbiCartographyMaxSites> airState_ {};
    std::array<float, kAmbiCartographyMaxSites> turbulencePhase_ {};
    std::array<float, kAmbiCartographyMaxSites> distanceMeters_ {};
    std::array<float, kAmbiCartographyMaxSites> arrivalSeconds_ {};
    std::array<float, kAmbiCartographyMaxSites> siteLevel_ {};
    std::array<Vec3, kAmbiCartographyMaxSites> directions_ {};
    std::array<std::array<float, kAmbiCartographyMaxChannels>,
        kAmbiCartographyMaxSites> basis_ {};
    std::array<uint32_t, kAmbiCartographyMaxSites> siteToLane_ {};
    std::array<uint32_t, kAmbiCartographyMaxSites> laneToSite_ {};
    MacroDelay macroDelay_ {};
    MacroPitch macroPitch_ {};
    MacroShred macroShred_ {};
    MacroFracture macroFracture_ {};
    AmbiFieldListener fieldListener_ {};
};

} // namespace s3g
