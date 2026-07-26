#pragma once

#include "s3g_geological_field.h"
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
inline constexpr uint32_t kAmbiCryosphereRegimeCount = 13u;
inline constexpr uint32_t kAmbiCryosphereEnvironmentCount = 10u;
inline constexpr uint32_t kAmbiCryospherePlaceCount = 6u;

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
        // HAIL, SLEET, FREEZING RAIN, MELTWATER UNDER ICE.
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
    float cavitationEnvelope = 0.0f;
    float tailEnvelope = 0.0f;
    float forcePulse = 0.0f;
    float eventLevel = 0.0f;
    float energy = 0.0f;
    StructuralFailureModel structure {};
};

class AmbiCryosphereEncoder {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1000.0, sampleRate);
        field_.prepare(sampleRate_);
        reset();
        setParams(params_);
    }

    void reset()
    {
        field_.reset();
        for (uint32_t voice = 0u; voice < voices_.size(); ++voice) {
            initializeVoice(voice);
        }
        lastOutput_.fill(0.0f);
        transitionTail_.fill(0.0f);
        transitionFade_ = 1.0f;
        transitionRequested_.store(false, std::memory_order_relaxed);
        smoothedOutputGain_ = dbToGain(params_.outputGainDb);
        iceLayerEnergy_ = 0.0f;
        fractureEventCount_ = 0u;
        slipEventCount_ = 0u;
        calvingEventCount_ = 0u;
        impactEventCount_ = 0u;
        structuralSnapEventCount_ = 0u;
        plateFailureEventCount_ = 0u;
    }

    void setParams(AmbiCryosphereParams params)
    {
        sanitize(params);
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
    }

private:
    static float finiteClamp(float value, float fallback, float low, float high)
    {
        return clamp(std::isfinite(value) ? value : fallback, low, high);
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
        voice.structure.prepare(sampleRate_,
            0x1ceba11u + index * 0x85ebca6bu);
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
        voice.forcePulse += randomSigned(voice.rng) * strength;
        voice.tailEnvelope = std::max(voice.tailEnvelope,
            strength * params_.resonance * 0.46f);
        ++fractureEventCount_;
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

    float processVoice(uint32_t index)
    {
        auto& voice = voices_[index];
        const auto& regime = kAmbiCryosphereRegimeProfiles[
            std::min<uint32_t>(params_.regime,
                kAmbiCryosphereRegimeCount - 1u)];
        const auto& substrate = kAmbiCryosphereSubstrateProfiles[
            std::min<uint32_t>(params_.environment,
                kAmbiCryosphereEnvironmentCount - 1u)];
        const float sr = static_cast<float>(sampleRate_);
        const float dt = 1.0f / sr;
        const float white = randomSigned(voice.rng);
        const float slowHz = 16.0f + params_.depth * 150.0f
            + substrate.lowMass * 72.0f;
        const float contactHz = 340.0f + params_.brightness * 5200.0f
            + substrate.hardness * 1400.0f;
        const float airHz = 2600.0f + params_.brightness * 10500.0f;
        voice.slowNoise += (white - voice.slowNoise)
            * (1.0f - std::exp(-kPi * 2.0f * slowHz / sr));
        voice.contactNoise += (white - voice.contactNoise)
            * (1.0f - std::exp(-kPi * 2.0f * contactHz / sr));
        voice.airNoise += (white - voice.airNoise)
            * (1.0f - std::exp(-kPi * 2.0f * airHz / sr));
        const float contactBand = voice.contactNoise - voice.slowNoise;
        const float highBand = white - voice.airNoise;
        const float derivative = white - voice.previousWhite;
        voice.previousWhite = white;

        const float porePressure = params_.bubbles
            * (0.28f + substrate.poreWater * 0.92f);
        const float eventRate = params_.flow;
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
            * (0.54f
                + params_.convergence * 0.30f
                + regime.fracture * 0.28f)
            * eventRate, 0.0f, 1.0f);
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
            + regime.fracture * 0.20f, 0.0f, 1.0f);
        structureParams.highDetail = params_.snap;
        structureParams.consequence = params_.plateFailure;
        structureParams.mass = clamp(params_.scale * 0.58f
            + substrate.lowMass * 0.28f + params_.depth * 0.14f,
            0.0f, 1.0f);
        structureParams.mode = StructuralConsequence::PlateRupture;
        const auto structural = voice.structure.process(structureParams);
        if (structural.snapTriggered) ++structuralSnapEventCount_;
        if (structural.consequenceTriggered) ++plateFailureEventCount_;
        voice.strain += dt * (freezeDrive * 2.8f + mechanicalDrive * 1.7f)
            * (0.72f + std::fabs(voice.slowNoise) * 0.48f);
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
                voice.cavitationEnvelope = std::max(
                    voice.cavitationEnvelope,
                    impact * (0.18f + regime.waterImpact * 0.58f));
                voice.forcePulse += randomSigned(voice.rng) * impact;
                ++impactEventCount_;
            }
        }

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

        const float creep = voice.slowNoise * voice.strain
            * (0.12f + params_.depth * 0.46f);
        const float fracture = voice.fractureEnvelope
            * (derivative * (0.42f + substrate.hardness * 0.58f)
                + highBand * 0.34f + voice.forcePulse * 0.46f);
        const float macro = voice.macroEnvelope
            * (voice.slowNoise * (0.82f + substrate.lowMass * 0.72f)
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
            * (voice.slowNoise * 1.28f + contactBand * 0.36f
                + voice.forcePulse * 0.42f);
        const float cavitation = voice.cavitationEnvelope
            * (highBand * 0.62f + contactBand * 0.28f);
        const float diffuseTail = voice.tailEnvelope
            * (voice.slowNoise * 0.44f + contactBand * 0.26f)
            * (1.0f - params_.damping * 0.58f);
        const float listenerGain = 1.0f
            + field_.listenerDrive(index) * 0.20f;
        const float iceSample = (creep + fracture * 0.42f + macro * 0.48f
            + slip * 0.38f + grain * 0.32f + grinding * 0.46f
            + snow * 0.42f + impact * 0.58f + cavitation * 0.34f
            + diffuseTail * 0.30f)
            * (0.20f + params_.water * 0.46f
                + params_.density * 0.18f + params_.drops * 0.14f);
        const float structuralSample = (structural.flex * 0.14f
            + structural.crack * 0.62f + structural.snap * 0.76f
            + structural.rupture * 0.54f + structural.fall * 0.48f
            + structural.impact * 0.72f)
            * (0.24f + params_.surfaceLoad * 0.62f);
        const float sample = (iceSample + structuralSample) * listenerGain;

        const float event = std::max({ voice.fractureEnvelope,
            voice.macroEnvelope, voice.slipEnvelope, voice.grainEnvelope,
            voice.impactEnvelope, voice.cavitationEnvelope,
            structural.activity });
        voice.eventLevel += (event - voice.eventLevel) * 0.018f;
        voice.energy += (sample * sample - voice.energy) * 0.0012f;
        return std::isfinite(sample) ? sample : 0.0f;
    }

    AmbiCryosphereParams params_ {};
    GeologicalField field_ {};
    std::array<AmbiCryosphereVoice, kAmbiCryosphereMaxVoices> voices_ {};
    std::array<float, kAmbiCryosphereMaxChannels> lastOutput_ {};
    std::array<float, kAmbiCryosphereMaxChannels> transitionTail_ {};
    double sampleRate_ = 48000.0;
    float smoothedOutputGain_ = dbToGain(-6.0f);
    float transitionFade_ = 1.0f;
    float iceLayerEnergy_ = 0.0f;
    uint64_t fractureEventCount_ = 0u;
    uint64_t slipEventCount_ = 0u;
    uint64_t calvingEventCount_ = 0u;
    uint64_t impactEventCount_ = 0u;
    uint64_t structuralSnapEventCount_ = 0u;
    uint64_t plateFailureEventCount_ = 0u;
    std::atomic<bool> transitionRequested_ { false };
};

} // namespace s3g
