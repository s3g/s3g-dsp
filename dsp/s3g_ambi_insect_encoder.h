#pragma once

#include "s3g_ambi_environment_field.h"
#include "s3g_ambi_field_listener.h"
#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAmbiInsectMaxVoices = 64u;
constexpr uint32_t kAmbiInsectMaxOrder = 7u;
constexpr uint32_t kAmbiInsectMaxChannels = 64u;
constexpr uint32_t kAmbiInsectRegimeCount = 7u;
constexpr uint32_t kAmbiInsectCallTypeCount = 11u;
constexpr uint32_t kAmbiInsectPlaceCount = 7u;
constexpr uint32_t kAmbiInsectSineTableSize = 4096u;
constexpr float kAmbiInsectInvTwoPi = 0.15915494309f;
constexpr uint32_t kAmbiInsectMixedRegime = 5u;
constexpr uint32_t kAmbiInsectTremulationRegime = 6u;
constexpr uint32_t kAmbiInsectProductionMethodCount = 6u;
constexpr uint32_t kAmbiInsectMaxColonies = 4u;
constexpr uint32_t kAmbiInsectDefaultSceneSeed = 0x7f4a7c15u;

struct AmbiInsectTemperatureResponse {
    float phrase = 0.35f;
    float chirp = 0.80f;
    float pulse = 1.30f;
    float pitch = 0.25f;
};

inline AmbiInsectTemperatureResponse ambiInsectTemperatureResponse(
    uint32_t productionMethod)
{
    switch (std::min<uint32_t>(
        productionMethod, kAmbiInsectProductionMethodCount - 1u)) {
    case 0u: return { 0.45f, 1.00f, 1.90f, 0.42f }; // Chirpers
    case 1u: return { 0.38f, 1.15f, 1.80f, 0.38f }; // Trillers
    case 2u: return { 0.30f, 0.85f, 1.20f, 0.18f }; // Cicadas
    case 3u: return { 0.24f, 0.42f, 0.55f, 0.62f }; // Flyers
    case 4u: return { 0.20f, 0.55f, 0.65f, 0.20f }; // Tickers
    default: return { 0.24f, 0.65f, 0.80f, 0.18f }; // Tremulators
    }
}

inline float ambiInsectTemperatureScale(
    float temperature, float response)
{
    return std::exp2((clamp(temperature, 0.0f, 1.0f) - 0.5f) * response);
}

struct AmbiInsectCallProfile {
    float phraseRateScale = 1.0f;
    float chirpRateScale = 1.0f;
    float pulseRateScale = 1.0f;
    float phraseDutyBias = 0.0f;
    float chirpDutyBias = 0.0f;
    float callProbabilityBias = 0.0f;
    float synchronyBias = 0.0f;
    float level = 1.0f;
    float alternatePhase = 0.0f;
    float sustain = 0.0f;
};

inline AmbiInsectCallProfile ambiInsectCallProfile(uint32_t callType)
{
    switch (std::min<uint32_t>(callType, kAmbiInsectCallTypeCount - 1u)) {
    case 1u: // Congregational song
        return { 0.82f, 1.06f, 1.02f, 0.12f, 0.10f, 0.12f, 0.22f, 1.00f, 0.0f, 0.08f };
    case 2u: // Response call
        return { 1.18f, 0.86f, 0.96f, -0.10f, -0.05f, -0.04f, 0.10f, 0.94f, 0.50f, 0.0f };
    case 3u: // Premating song
        return { 0.76f, 0.92f, 0.96f, 0.08f, 0.10f, 0.05f, 0.06f, 0.92f, 0.25f, 0.04f };
    case 4u: // Courtship song
        return { 0.68f, 1.24f, 1.08f, 0.12f, 0.07f, 0.08f, 0.12f, 0.88f, 0.125f, 0.06f };
    case 5u: // Agreement song
        return { 0.90f, 1.00f, 1.00f, 0.05f, 0.08f, 0.12f, 0.34f, 1.00f, 0.0f, 0.08f };
    case 6u: // Jumping song
        return { 0.58f, 0.54f, 0.76f, -0.42f, -0.24f, -0.18f, -0.08f, 1.08f, 0.0f, 0.0f };
    case 7u: // Rivalry call
        return { 1.28f, 0.80f, 1.16f, -0.15f, -0.10f, 0.05f, -0.16f, 1.12f, 0.50f, 0.0f };
    case 8u: // Postcopulatory call
        return { 0.46f, 0.74f, 0.78f, -0.34f, -0.18f, -0.22f, -0.10f, 0.80f, 0.0f, 0.0f };
    case 9u: // Defensive call
        return { 1.75f, 1.40f, 1.44f, -0.24f, 0.05f, 0.20f, -0.12f, 1.18f, 0.0f, 0.02f };
    case 10u: // Flight noise
        return { 0.38f, 0.58f, 1.00f, 0.40f, 0.30f, 0.24f, 0.08f, 0.94f, 0.0f, 0.78f };
    default: // Calling song
        return {};
    }
}

inline uint32_t ambiInsectProductionMethod(uint32_t selectedRegime, uint32_t voice)
{
    if (selectedRegime == kAmbiInsectMixedRegime) {
        return voice % kAmbiInsectProductionMethodCount;
    }
    if (selectedRegime == kAmbiInsectTremulationRegime) return 5u;
    return std::min<uint32_t>(selectedRegime, 4u);
}

struct AmbiInsectSineTable {
    std::array<float, kAmbiInsectSineTableSize + 1u> value {};

    AmbiInsectSineTable()
    {
        for (uint32_t index = 0u; index <= kAmbiInsectSineTableSize; ++index) {
            value[index] = std::sin(
                static_cast<float>(index) * kPi * 2.0f
                / static_cast<float>(kAmbiInsectSineTableSize));
        }
    }

    float at(float phase) const
    {
        phase -= std::floor(phase);
        const float position = phase * static_cast<float>(kAmbiInsectSineTableSize);
        const uint32_t index = static_cast<uint32_t>(position);
        const float fraction = position - static_cast<float>(index);
        return value[index] + (value[index + 1u] - value[index]) * fraction;
    }
};

inline const AmbiInsectSineTable kAmbiInsectSineTable {};

inline float ambiInsectSin(float phase)
{
    return kAmbiInsectSineTable.at(phase);
}

inline float ambiInsectCos(float phase)
{
    return kAmbiInsectSineTable.at(phase + 0.25f);
}

struct AmbiInsectParams {
    uint32_t order = 3u;
    uint32_t voices = 28u;
    uint32_t regime = 0u;
    float activity = 0.62f;
    float temperature = 0.56f;
    float variation = 0.22f;
    float coupling = 0.24f;
    float phraseRateHz = 0.08f;
    float chirpRateHz = 1.43f;
    float pulseRateHz = 58.0f;
    float callLength = 0.12f;
    float rest = 0.30f;
    float bodyPitchHz = 4200.0f;
    float bodySize = 0.42f;
    float rasp = 0.38f;
    float wing = 0.18f;
    float brightness = 0.62f;
    float resonance = 0.58f;
    float air = 0.24f;
    float fieldRateHz = 0.035f;
    float roam = 0.34f;
    float cohesion = 0.42f;
    float scatter = 0.54f;
    float orbit = 0.16f;
    float lift = 0.24f;
    float nearPass = 0.12f;
    float spatialFollow = 0.72f;
    float centerAzimuthDeg = 0.0f;
    float centerElevationDeg = 0.0f;
    float centerDistance = 1.0f;
    float outputGainDb = -6.0f;
    uint32_t place = 0u;
    float space = 0.16f;
    float environmentSize = 0.5f;
    float environmentDecay = 0.5f;
    float environmentDamping = 0.5f;
    uint32_t callType = 0u;
    uint32_t sceneSeed = kAmbiInsectDefaultSceneSeed;
    AmbiFieldListenMode fieldListenMode = AmbiFieldListenMode::Off;
};

inline AmbiEnvironmentProfileId ambiInsectEnvironmentProfile(uint32_t place)
{
    constexpr std::array<AmbiEnvironmentProfileId, kAmbiInsectPlaceCount> profiles {
        AmbiEnvironmentProfileId::Open,
        AmbiEnvironmentProfileId::Canopy,
        AmbiEnvironmentProfileId::Canopy,
        AmbiEnvironmentProfileId::Open,
        AmbiEnvironmentProfileId::Porch,
        AmbiEnvironmentProfileId::Room,
        AmbiEnvironmentProfileId::Tunnel,
    };
    return profiles[std::min<uint32_t>(place, kAmbiInsectPlaceCount - 1u)];
}

struct AmbiInsectPoint {
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float distance = 1.0f;
};

struct AmbiInsectColony {
    uint32_t productionMethod = 0u;
    uint32_t voiceCount = 0u;
    float phrasePhase = 0.0f;
    float phraseRateScale = 1.0f;
    float chirpRateScale = 1.0f;
    float pulseRateScale = 1.0f;
    float pitchScale = 1.0f;
    float pitchOffsetOctaves = 0.0f;
    float activityBias = 0.0f;
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float distanceScale = 1.0f;
    float motionPhase = 0.0f;
};

struct AmbiInsectVoiceOutput {
    float direct = 0.0f;
    float fieldSend = 0.0f;
};

struct AmbiInsectSvf {
    float ic1eq = 0.0f;
    float ic2eq = 0.0f;
    float a1 = 1.0f;
    float a2 = 0.0f;
    float a3 = 0.0f;
    float lastCutoffHz = 0.0f;
    float lastResonance = 0.0f;
    uint32_t coefficientPhase = 0u;

    void reset()
    {
        ic1eq = 0.0f;
        ic2eq = 0.0f;
        a1 = 1.0f;
        a2 = 0.0f;
        a3 = 0.0f;
        lastCutoffHz = 0.0f;
        lastResonance = 0.0f;
        coefficientPhase = 0u;
    }

    bool healthy() const
    {
        return std::isfinite(ic1eq) && std::isfinite(ic2eq)
            && std::isfinite(a1) && std::isfinite(a2) && std::isfinite(a3)
            && std::isfinite(lastCutoffHz) && std::isfinite(lastResonance)
            && std::fabs(ic1eq) < 64.0f && std::fabs(ic2eq) < 64.0f;
    }

    float process(float input, float cutoffHz, float resonance, float sampleRate)
    {
        if (!std::isfinite(input) || !std::isfinite(cutoffHz)
            || !std::isfinite(resonance)) {
            reset();
            return 0.0f;
        }
        const float sr = std::max(1000.0f, sampleRate);
        const float hz = clamp(cutoffHz, 20.0f, sr * 0.44f);
        const float boundedResonance = clamp(resonance, 0.0f, 1.0f);
        const float cutoffThreshold = std::max(12.0f, lastCutoffHz * 0.02f);
        const bool refresh = coefficientPhase == 0u || lastCutoffHz <= 0.0f
            || std::fabs(hz - lastCutoffHz) > cutoffThreshold
            || std::fabs(boundedResonance - lastResonance) > 0.02f;
        coefficientPhase = (coefficientPhase + 1u) & 15u;
        if (refresh) {
            const float g = std::tan(kPi * hz / sr);
            const float k = 2.0f - boundedResonance * 1.88f;
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
            lastCutoffHz = hz;
            lastResonance = boundedResonance;
        }
        const float v3 = input - ic2eq;
        const float band = a1 * ic1eq + a2 * v3;
        const float low = ic2eq + a2 * ic1eq + a3 * v3;
        ic1eq = flushDenormal(2.0f * band - ic1eq);
        ic2eq = flushDenormal(2.0f * low - ic2eq);
        if (!std::isfinite(band) || !std::isfinite(ic1eq)
            || !std::isfinite(ic2eq)
            || std::fabs(ic1eq) >= 64.0f || std::fabs(ic2eq) >= 64.0f) {
            reset();
            return 0.0f;
        }
        return band;
    }
};

struct AmbiInsectVoice {
    uint32_t identity = 1u;
    uint32_t rngState = 1u;
    uint32_t phraseGeneration = 0u;
    uint32_t pulseGeneration = 0u;
    uint32_t wingGeneration = 0u;
    float phrasePhase = 0.0f;
    float chirpPhase = 0.0f;
    float pulsePhase = 0.0f;
    float carrierPhase = 0.0f;
    float harmonicPhase = 0.0f;
    float pairedWingPhase = 0.0f;
    float flutterPhase = 0.0f;
    float motionPhase = 0.0f;
    float phraseAccent = 1.0f;
    float phraseAccentTarget = 1.0f;
    float pulseAccent = 1.0f;
    float pulseRateScale = 1.0f;
    float pitchDrift = 0.0f;
    float pitchDriftTarget = 0.0f;
    float wingCycleGain = 1.0f;
    float wingCycleGainTarget = 1.0f;
    float wingPitchJitter = 0.0f;
    float wingPitchJitterTarget = 0.0f;
    float wingPairOffset = 0.03f;
    float wingPairOffsetTarget = 0.03f;
    float cicadaEntrainmentScale = 1.0f;
    float cicadaEntrainmentTarget = 1.0f;
    uint32_t cicadaEntrainmentMode = 1u;
    float signatureA = 0.0f;
    float signatureB = 0.0f;
    float activityThreshold = 0.5f;
    float flutterRateHz = 3.0f;
    float lane = 0.0f;
    float clockScale = 1.0f;
    float basePitchHz = 440.0f;
    float activeWeight = 1.0f;
    float callEnvelope = 0.0f;
    float noiseLow = 0.0f;
    float noiseHigh = 0.0f;
    float energy = 0.0f;
    float callViz = 0.0f;
    AmbiInsectSvf bodyFilter {};
    AmbiInsectSvf wingFilter {};
    AmbiInsectSvf abdomenFilter {};
    AmbiInsectSvf substrateFilter {};
    AmbiInsectSvf raspFilter {};
    AmbiInsectSvf airFilter {};
    AmbiInsectSvf flightAirFilter {};
};

class AmbiInsectEncoder {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1000.0, sampleRate);
        updateAudioRateCoefficients();
        environmentField_.prepare(sampleRate_);
        fieldListener_.prepare(sampleRate_);
        fieldListener_.setMemorySeconds(0.42f);
        initializeFieldListener();
        reset();
        setParams(params_);
    }

    void reset()
    {
        fieldPhase_ = 0.0f;
        globalPhrasePhase_ = 0.0f;
        transitionFade_ = 1.0f;
        transitionRequested_.store(false, std::memory_order_relaxed);
        lastOutput_.fill(0.0f);
        transitionTail_.fill(0.0f);
        environmentField_.setProfile(ambiInsectEnvironmentProfile(params_.place));
        environmentField_.setAmount(ambiEnvironmentSpaceAmount(params_.space));
        environmentField_.setShape(params_.environmentSize, params_.environmentDecay, params_.environmentDamping);
        environmentField_.reset();
        fieldListener_.reset();
        currentListenWeights_.fill(1.0f);
        targetListenWeights_.fill(1.0f);
        currentListenMix_ =
            params_.fieldListenMode == AmbiFieldListenMode::Off ? 0.0f : 1.0f;
        smoothParams_ = params_;
        callProfile_ = ambiInsectCallProfile(params_.callType);
        smoothReady_ = true;
        smoothedOutputGain_ = normalizedOutputGain(params_);
        rebuildColonies();
        for (uint32_t voice = 0u; voice < kAmbiInsectMaxVoices; ++voice) {
            voices_[voice] = {};
            voices_[voice].identity = voiceIdentity(voice);
            initializeVoice(voice);
            points_[voice] = basePoint(voice);
            targetPoints_[voice] = points_[voice];
        }
    }

    void setParams(AmbiInsectParams params)
    {
        params.order = std::clamp<uint32_t>(params.order, 1u, kAmbiInsectMaxOrder);
        params.voices = std::clamp<uint32_t>(params.voices, 1u, kAmbiInsectMaxVoices);
        params.regime = std::clamp<uint32_t>(params.regime, 0u, kAmbiInsectRegimeCount - 1u);
        params.activity = clampFinite(params.activity, params_.activity, 0.0f, 1.0f);
        params.temperature = clampFinite(params.temperature, params_.temperature, 0.0f, 1.0f);
        params.variation = clampFinite(params.variation, params_.variation, 0.0f, 1.0f);
        params.coupling = clampFinite(params.coupling, params_.coupling, 0.0f, 1.0f);
        params.phraseRateHz = clampFinite(params.phraseRateHz, params_.phraseRateHz, 0.01f, 8.0f);
        params.chirpRateHz = clampFinite(params.chirpRateHz, params_.chirpRateHz, 0.2f, 80.0f);
        params.pulseRateHz = clampFinite(params.pulseRateHz, params_.pulseRateHz, 20.0f, 8000.0f);
        params.callLength = clampFinite(params.callLength, params_.callLength, 0.0f, 1.0f);
        params.rest = clampFinite(params.rest, params_.rest, 0.0f, 1.0f);
        params.bodyPitchHz = clampFinite(params.bodyPitchHz, params_.bodyPitchHz, 90.0f, 14000.0f);
        params.bodySize = clampFinite(params.bodySize, params_.bodySize, 0.0f, 1.0f);
        params.rasp = clampFinite(params.rasp, params_.rasp, 0.0f, 1.0f);
        params.wing = clampFinite(params.wing, params_.wing, 0.0f, 1.0f);
        params.brightness = clampFinite(params.brightness, params_.brightness, 0.0f, 1.0f);
        params.resonance = clampFinite(params.resonance, params_.resonance, 0.0f, 1.0f);
        params.air = clampFinite(params.air, params_.air, 0.0f, 1.0f);
        params.fieldRateHz = clampFinite(params.fieldRateHz, params_.fieldRateHz, 0.001f, 2.0f);
        params.roam = clampFinite(params.roam, params_.roam, 0.0f, 1.0f);
        params.cohesion = clampFinite(params.cohesion, params_.cohesion, 0.0f, 1.0f);
        params.scatter = clampFinite(params.scatter, params_.scatter, 0.0f, 1.0f);
        params.orbit = clampFinite(params.orbit, params_.orbit, 0.0f, 1.0f);
        params.lift = clampFinite(params.lift, params_.lift, 0.0f, 1.0f);
        params.nearPass = clampFinite(params.nearPass, params_.nearPass, 0.0f, 1.0f);
        params.spatialFollow = clampFinite(params.spatialFollow, params_.spatialFollow, 0.0f, 1.0f);
        params.centerAzimuthDeg = wrapSignedDeg(params.centerAzimuthDeg);
        params.centerElevationDeg = clampFinite(params.centerElevationDeg, params_.centerElevationDeg, -90.0f, 90.0f);
        params.centerDistance = clampFinite(params.centerDistance, params_.centerDistance, 0.15f, 2.0f);
        params.outputGainDb = clampFinite(params.outputGainDb, params_.outputGainDb, -60.0f, 12.0f);
        params.place = std::clamp<uint32_t>(params.place, 0u, kAmbiInsectPlaceCount - 1u);
        params.space = clampFinite(params.space, params_.space, 0.0f, 1.0f);
        params.environmentSize = clampFinite(params.environmentSize, params_.environmentSize, 0.0f, 1.0f);
        params.environmentDecay = clampFinite(params.environmentDecay, params_.environmentDecay, 0.0f, 1.0f);
        params.environmentDamping = clampFinite(params.environmentDamping, params_.environmentDamping, 0.0f, 1.0f);
        params.callType = std::clamp<uint32_t>(params.callType, 0u, kAmbiInsectCallTypeCount - 1u);
        params.fieldListenMode =
            sanitizeAmbiFieldListenMode(params.fieldListenMode);
        if (params.sceneSeed == 0u) params.sceneSeed = kAmbiInsectDefaultSceneSeed;

        const uint32_t oldVoices = params_.voices;
        params_ = params;
        if (params.voices > oldVoices) {
            for (uint32_t voice = oldVoices; voice < params.voices; ++voice) {
                voices_[voice].identity = voiceIdentity(voice);
                initializeVoice(voice);
            }
        }
    }

    AmbiInsectParams params() const { return params_; }
    void beginTransition() { transitionRequested_.store(true, std::memory_order_release); }
    float voiceEnergy(uint32_t voice) const { return voices_[std::min<uint32_t>(voice, kAmbiInsectMaxVoices - 1u)].energy; }
    float voiceCallLevel(uint32_t voice) const { return voices_[std::min<uint32_t>(voice, kAmbiInsectMaxVoices - 1u)].callViz; }
    float voicePitchHz(uint32_t voice) const { return voices_[std::min<uint32_t>(voice, kAmbiInsectMaxVoices - 1u)].basePitchHz; }
    float voiceCicadaEntrainmentScale(uint32_t voice) const { return voices_[std::min<uint32_t>(voice, kAmbiInsectMaxVoices - 1u)].cicadaEntrainmentScale; }
    uint32_t voiceCicadaEntrainmentMode(uint32_t voice) const { return voices_[std::min<uint32_t>(voice, kAmbiInsectMaxVoices - 1u)].cicadaEntrainmentMode; }
    uint32_t voiceColony(uint32_t voice) const { return voiceColony_[std::min<uint32_t>(voice, kAmbiInsectMaxVoices - 1u)]; }
    uint32_t voiceProductionMethod(uint32_t voice) const
    {
        const uint32_t colony = voiceColony(voice);
        return colonies_[std::min<uint32_t>(colony, kAmbiInsectMaxColonies - 1u)].productionMethod;
    }
    uint32_t colonyCount() const { return colonyCount_; }
    AmbiInsectPoint voicePoint(uint32_t voice) const { return points_[std::min<uint32_t>(voice, kAmbiInsectMaxVoices - 1u)]; }
    float fieldListenEnvelope(uint32_t lobe) const
    {
        return fieldListener_.envelope(lobe);
    }
    float fieldListenWeight(uint32_t lobe) const
    {
        return lobe < kAmbiFieldListenerMaxLobes
            ? lerp(1.0f, currentListenWeights_[lobe], currentListenMix_)
            : 1.0f;
    }

    void process(float* const* outputs, uint32_t outputChannels, uint32_t frames)
    {
        if (!outputs || frames == 0u) return;
        if (transitionRequested_.exchange(false, std::memory_order_acq_rel)) startTransition();
        outputChannels = std::min<uint32_t>(outputChannels, kAmbiInsectMaxChannels);
        for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
            if (outputs[channel]) std::fill(outputs[channel], outputs[channel] + frames, 0.0f);
        }

        const uint32_t voices = params_.voices;
        const uint32_t ambiChannels = std::min<uint32_t>(ambiChannelsForOrder(params_.order), outputChannels);
        constexpr uint32_t kControlFrames = 16u;
        for (uint32_t chunkStart = 0u; chunkStart < frames; chunkStart += kControlFrames) {
            const uint32_t chunkFrames = std::min<uint32_t>(kControlFrames, frames - chunkStart);
            const float chunkDt = static_cast<float>(chunkFrames) / static_cast<float>(sampleRate_);
            updateSmoothedParams(chunkDt);
            updateMotion(chunkDt);
            updateVoiceAudioControls();
            const float targetGain = normalizedOutputGain(smoothParams_);

            environmentField_.setProfile(ambiInsectEnvironmentProfile(smoothParams_.place));
            environmentField_.setAmount(ambiEnvironmentSpaceAmount(smoothParams_.space));
            environmentField_.setShape(smoothParams_.environmentSize, smoothParams_.environmentDecay,
                smoothParams_.environmentDamping);
            environmentField_.setMotion(smoothParams_.centerAzimuthDeg + std::sin(fieldPhase_ * kPi * 2.0f) * smoothParams_.roam * 18.0f,
                smoothParams_.centerElevationDeg + std::sin(fieldPhase_ * kPi * 1.37f) * smoothParams_.lift * 8.0f);

            std::array<std::array<float, kAmbiInsectMaxChannels>, kAmbiInsectMaxVoices> basis {};
            std::array<Vec3, kAmbiInsectMaxVoices> directions {};
            std::array<float, kAmbiInsectMaxVoices> distanceGain {};
            std::array<float, kAmbiInsectMaxVoices> listenGain {};
            advanceFieldListener(chunkDt);
            const bool listenerActive =
                params_.fieldListenMode != AmbiFieldListenMode::Off
                || currentListenMix_ > 1.0e-4f;
            for (uint32_t voice = 0u; voice < voices; ++voice) {
                directions[voice] = directionFromAed(points_[voice].azimuthDeg, points_[voice].elevationDeg);
                basis[voice] = acnSn3dBasis7(directions[voice]);
                distanceGain[voice] = 1.0f / std::max(0.42f, points_[voice].distance);
                listenGain[voice] =
                    listenerActive ? fieldListenGain(directions[voice]) : 1.0f;
            }

            for (uint32_t frame = chunkStart; frame < chunkStart + chunkFrames; ++frame) {
                smoothedOutputGain_ += (targetGain - smoothedOutputGain_) * 0.0014f;
                for (uint32_t colony = 0u; colony < colonyCount_; ++colony) {
                    auto& profile = colonies_[colony];
                    const auto temperature = ambiInsectTemperatureResponse(
                        profile.productionMethod);
                    profile.phrasePhase = wrapUnit(
                        profile.phrasePhase
                        + smoothParams_.phraseRateHz
                            * callProfile_.phraseRateScale
                            * profile.phraseRateScale
                            * ambiInsectTemperatureScale(
                                smoothParams_.temperature,
                                temperature.phrase)
                            / static_cast<float>(sampleRate_));
                }
                globalPhrasePhase_ = colonies_[0].phrasePhase;
                flightChorusGain_ = 1.0f
                    + ambiInsectSin(globalPhrasePhase_)
                        * smoothParams_.coupling * 0.08f;
                environmentField_.beginFrame();
                for (uint32_t voice = 0u; voice < voices; ++voice) {
                    const auto voiceOutput = processVoice(voice);
                    float sample = voiceOutput.direct * smoothedOutputGain_
                        * distanceGain[voice] * listenGain[voice];
                    if (!std::isfinite(sample)) {
                        initializeVoice(voice);
                        sample = 0.0f;
                    }
                    voices_[voice].energy += (sample * sample - voices_[voice].energy) * 0.0012f;
                    if (std::fabs(sample) < 0.0000001f) continue;
                    environmentField_.addSource(voiceOutput.fieldSend * smoothedOutputGain_
                        * distanceGain[voice] * listenGain[voice] * 2.2f,
                        directions[voice]);
                    for (uint32_t channel = 0u; channel < ambiChannels; ++channel) {
                        if (outputs[channel]) outputs[channel][frame] = flushDenormal(outputs[channel][frame]
                            + sample * basis[voice][channel]);
                    }
                }
                const auto environment = environmentField_.process();
                for (uint32_t channel = 0u; channel < std::min<uint32_t>(ambiChannels, kAmbiEnvironmentChannels); ++channel) {
                    if (outputs[channel]) outputs[channel][frame] = flushDenormal(outputs[channel][frame] + environment[channel]);
                }
                if (listenerActive) {
                    for (uint32_t channel = 0u; channel < ambiChannels; ++channel) {
                        listenerFrame_[channel] = outputs[channel]
                            ? outputs[channel][frame] : 0.0f;
                    }
                    fieldListener_.processFrame(listenerFrame_.data(), ambiChannels);
                }
            }
            updateFieldListenerTargets();
        }

        const float transitionStep = 1.0f / std::max(1.0f, static_cast<float>(sampleRate_) * 0.030f);
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            const float mix = transitionFade_;
            for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
                const float summed = channel < ambiChannels && outputs[channel] ? outputs[channel][frame] : 0.0f;
                float fresh = std::tanh(clamp(summed * 1.06f, -4.0f, 4.0f));
                if (!std::isfinite(fresh)) fresh = 0.0f;
                const float value = transitionTail_[channel] * (1.0f - mix) + fresh * mix;
                if (outputs[channel]) outputs[channel][frame] = value;
                lastOutput_[channel] = value;
            }
            transitionFade_ = std::min(1.0f, transitionFade_ + transitionStep);
        }
    }

private:
    void initializeFieldListener()
    {
        constexpr std::array<Vec3, kAmbiFieldListenerMaxLobes> corners {{
            { 1.0f, 1.0f, 1.0f },
            { -1.0f, 1.0f, 1.0f },
            { -1.0f, -1.0f, 1.0f },
            { 1.0f, -1.0f, 1.0f },
            { 1.0f, 1.0f, -1.0f },
            { -1.0f, 1.0f, -1.0f },
            { -1.0f, -1.0f, -1.0f },
            { 1.0f, -1.0f, -1.0f },
        }};
        for (uint32_t lobe = 0u; lobe < corners.size(); ++lobe) {
            listenDirections_[lobe] = normalize(corners[lobe]);
        }
        fieldListener_.setDirections(
            listenDirections_.data(), static_cast<uint32_t>(listenDirections_.size()));
    }

    void advanceFieldListener(float dt)
    {
        const float mixTarget =
            params_.fieldListenMode == AmbiFieldListenMode::Off ? 0.0f : 1.0f;
        const float mixCoefficient =
            1.0f - std::exp(-std::max(0.0f, dt) / 0.045f);
        const float weightCoefficient =
            1.0f - std::exp(-std::max(0.0f, dt) / 0.52f);
        currentListenMix_ +=
            (mixTarget - currentListenMix_) * mixCoefficient;
        for (uint32_t lobe = 0u; lobe < currentListenWeights_.size(); ++lobe) {
            currentListenWeights_[lobe] +=
                (targetListenWeights_[lobe] - currentListenWeights_[lobe])
                * weightCoefficient;
        }
    }

    float fieldListenGain(Vec3 direction) const
    {
        float weight = 0.0f;
        float total = 0.0f;
        for (uint32_t lobe = 0u; lobe < listenDirections_.size(); ++lobe) {
            const Vec3 listener = listenDirections_[lobe];
            const float dot = std::max(0.0f,
                direction.x * listener.x
                    + direction.y * listener.y
                    + direction.z * listener.z);
            const float kernel = dot * dot * dot * dot;
            weight += currentListenWeights_[lobe] * kernel;
            total += kernel;
        }
        const float shaped = total > 1.0e-5f ? weight / total : 1.0f;
        return lerp(1.0f, shaped, currentListenMix_);
    }

    void updateFieldListenerTargets()
    {
        if (params_.fieldListenMode == AmbiFieldListenMode::Off) {
            targetListenWeights_.fill(1.0f);
            return;
        }
        std::array<float, kAmbiFieldListenerMaxLobes> energy {};
        float mean = 0.0f;
        for (uint32_t lobe = 0u; lobe < energy.size(); ++lobe) {
            energy[lobe] = std::max(0.0f, fieldListener_.envelope(lobe));
            mean += energy[lobe];
        }
        mean /= static_cast<float>(energy.size());
        if (mean < 1.0e-7f) {
            targetListenWeights_.fill(1.0f);
            return;
        }

        std::array<float, kAmbiFieldListenerMaxLobes> raw {};
        float total = 0.0f;
        for (uint32_t lobe = 0u; lobe < raw.size(); ++lobe) {
            float observed = energy[lobe];
            float polarity = 1.0f;
            float strength = 0.64f;
            if (params_.fieldListenMode == AmbiFieldListenMode::Counter) {
                const Vec3 direction = listenDirections_[lobe];
                float opposing = 0.0f;
                float opposingWeight = 0.0f;
                for (uint32_t other = 0u; other < raw.size(); ++other) {
                    const Vec3 candidate = listenDirections_[other];
                    const float antipodal = std::max(0.0f,
                        -(direction.x * candidate.x
                            + direction.y * candidate.y
                            + direction.z * candidate.z));
                    const float kernel =
                        antipodal * antipodal * antipodal * antipodal;
                    opposing += energy[other] * kernel;
                    opposingWeight += kernel;
                }
                observed = opposingWeight > 1.0e-5f
                    ? opposing / opposingWeight : mean;
                strength = 0.72f;
            } else if (params_.fieldListenMode == AmbiFieldListenMode::Balance) {
                polarity = -1.0f;
                strength = 0.54f;
            }
            const float contrast = clamp(
                (observed - mean) / std::max(1.0e-6f, mean), -1.0f, 1.0f);
            raw[lobe] = std::exp(clamp(
                polarity * strength * contrast, -0.58f, 0.58f));
            total += raw[lobe];
        }
        const float normalization =
            static_cast<float>(raw.size()) / std::max(1.0e-6f, total);
        for (uint32_t lobe = 0u; lobe < raw.size(); ++lobe) {
            targetListenWeights_[lobe] =
                clamp(raw[lobe] * normalization, 0.40f, 2.50f);
        }
    }

    static float clampFinite(float value, float fallback, float low, float high)
    {
        return clamp(std::isfinite(value) ? value : fallback, low, high);
    }

    static float wrapSignedDeg(float value)
    {
        if (!std::isfinite(value)) return 0.0f;
        value = std::fmod(value + 180.0f, 360.0f);
        if (value < 0.0f) value += 360.0f;
        return value - 180.0f;
    }

    static float wrapUnit(float value)
    {
        value -= std::floor(value);
        return value < 0.0f ? value + 1.0f : value;
    }

    static float phaseDelta(float target, float current)
    {
        float delta = target - current;
        if (delta > 0.5f) delta -= 1.0f;
        if (delta < -0.5f) delta += 1.0f;
        return delta;
    }

    static float smoothToward(float current, float target, float coefficient)
    {
        if (!std::isfinite(target)) return std::isfinite(current) ? current : 0.0f;
        if (!std::isfinite(current)) return target;
        return current + (target - current) * coefficient;
    }

    static float smoothAngleDeg(float current, float target, float coefficient)
    {
        return wrapSignedDeg(current + wrapSignedDeg(target - current) * coefficient);
    }

    static float coefficientForRate(float rateHz, double sampleRate)
    {
        return 1.0f - std::exp(
            -std::max(0.0f, rateHz) / static_cast<float>(sampleRate));
    }

    static uint32_t hash(uint32_t value)
    {
        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
        return value;
    }

    static float hash01(uint32_t value)
    {
        return static_cast<float>(hash(value) & 0x00ffffffu) / static_cast<float>(0x00ffffffu);
    }

    static float hashSigned(uint32_t value) { return hash01(value) * 2.0f - 1.0f; }

    static float flyerPopulationPitchOffset(uint32_t voice, uint32_t voices)
    {
        if (voices <= 1u) return 0.0f;
        constexpr float inverseGoldenRatio = 0.61803398875f;
        const float quasiPosition = wrapUnit(
            static_cast<float>(voice + 1u) * inverseGoldenRatio);
        const float density = std::sqrt(clamp(
            static_cast<float>(voices - 1u) / 31.0f, 0.0f, 1.0f));
        return (quasiPosition * 2.0f - 1.0f) * density * 0.24f;
    }

    uint32_t voiceIdentity(uint32_t voice) const
    {
        const uint32_t identity = hash(
            params_.sceneSeed ^ (0x1b56c4e9u + voice * 0x9e3779b9u));
        return identity == 0u ? 1u + voice : identity;
    }

    void rebuildColonies()
    {
        const uint32_t voices = std::max<uint32_t>(1u, params_.voices);
        const bool mixed = params_.regime == kAmbiInsectMixedRegime;
        colonyCount_ = mixed
            ? (voices < 2u ? 1u : voices < 12u ? 2u
                : voices < 36u ? 3u : 4u)
            : (voices < 10u ? 1u : voices < 32u ? 2u : 3u);
        colonyCount_ = std::min<uint32_t>(
            colonyCount_, std::min<uint32_t>(voices, kAmbiInsectMaxColonies));

        std::array<float, kAmbiInsectMaxColonies> weights {};
        if (mixed && colonyCount_ > 1u) {
            const float dominantShare = 0.50f
                + hash01(params_.sceneSeed + 0x91e10da5u) * 0.17f;
            float satelliteWeight = 0.0f;
            for (uint32_t colony = 1u; colony < colonyCount_; ++colony) {
                weights[colony] = 0.55f
                    + hash01(params_.sceneSeed + colony * 0x632be59bu)
                        * 0.70f;
                satelliteWeight += weights[colony];
            }
            weights[0] = dominantShare;
            for (uint32_t colony = 1u; colony < colonyCount_; ++colony) {
                weights[colony] = (1.0f - dominantShare)
                    * weights[colony] / std::max(0.001f, satelliteWeight);
            }
        } else {
            for (uint32_t colony = 0u; colony < colonyCount_; ++colony) {
                weights[colony] = 0.82f
                    + hash01(params_.sceneSeed + colony * 0x632be59bu)
                        * 0.36f;
            }
        }

        std::array<uint32_t, kAmbiInsectMaxColonies> counts {};
        std::array<uint32_t, kAmbiInsectMaxColonies> extra {};
        for (uint32_t colony = 0u; colony < colonyCount_; ++colony) {
            counts[colony] = 1u;
        }
        for (uint32_t remaining = voices - colonyCount_;
             remaining > 0u; --remaining) {
            uint32_t selected = 0u;
            float selectedScore = static_cast<float>(extra[0])
                / std::max(0.001f, weights[0]);
            for (uint32_t colony = 1u; colony < colonyCount_; ++colony) {
                const float score = static_cast<float>(extra[colony])
                    / std::max(0.001f, weights[colony]);
                if (score < selectedScore) {
                    selected = colony;
                    selectedScore = score;
                }
            }
            ++counts[selected];
            ++extra[selected];
        }

        static constexpr std::array<float, kAmbiInsectProductionMethodCount>
            mixedPitchScale { 1.00f, 1.55f, 1.00f, 0.11f, 0.42f, 0.11f };
        static constexpr std::array<float, kAmbiInsectProductionMethodCount>
            mixedPhraseScale { 0.82f, 1.18f, 0.72f, 0.54f, 0.88f, 0.74f };
        static constexpr std::array<float, kAmbiInsectProductionMethodCount>
            mixedChirpScale { 0.72f, 2.00f, 3.20f, 0.48f, 0.70f, 1.30f };
        static constexpr std::array<float, kAmbiInsectProductionMethodCount>
            mixedPulseScale { 0.62f, 1.80f, 6.00f, 1.00f, 1.20f, 0.45f };

        uint32_t usedMethods = 0u;
        const float sceneRotation = hashSigned(
            params_.sceneSeed + 0x4f1bbcdcu) * 35.0f;
        for (uint32_t colony = 0u; colony < colonyCount_; ++colony) {
            const uint32_t colonySeed = hash(
                params_.sceneSeed ^ (0x85ebca6bu + colony * 0xc2b2ae35u));
            uint32_t method = ambiInsectProductionMethod(params_.regime, 0u);
            if (mixed) {
                method = hash(colonySeed + 0x27d4eb2du)
                    % kAmbiInsectProductionMethodCount;
                while ((usedMethods & (1u << method)) != 0u) {
                    method = (method + 1u) % kAmbiInsectProductionMethodCount;
                }
                usedMethods |= 1u << method;
            }

            auto& profile = colonies_[colony];
            profile = {};
            profile.productionMethod = method;
            profile.voiceCount = counts[colony];
            profile.phrasePhase = hash01(colonySeed + 11u);
            const float sharedRateOffset = hashSigned(colonySeed + 29u) * 0.12f;
            profile.phraseRateScale = std::exp2(
                sharedRateOffset + hashSigned(colonySeed + 31u) * 0.035f);
            profile.chirpRateScale = std::exp2(
                sharedRateOffset + hashSigned(colonySeed + 37u) * 0.055f);
            profile.pulseRateScale = std::exp2(
                sharedRateOffset + hashSigned(colonySeed + 41u) * 0.075f);
            profile.pitchScale = std::exp2(
                hashSigned(colonySeed + 43u) * 0.07f);
            if (mixed) {
                profile.phraseRateScale *= mixedPhraseScale[method];
                profile.chirpRateScale *= mixedChirpScale[method];
                profile.pulseRateScale *= mixedPulseScale[method];
                profile.pitchScale *= mixedPitchScale[method];
            }
            profile.pitchOffsetOctaves = hashSigned(colonySeed + 47u);
            profile.activityBias = hashSigned(colonySeed + 53u) * 0.08f;
            if (colonyCount_ > 1u) {
                const float position = (
                    (static_cast<float>(colony) + 0.5f)
                        / static_cast<float>(colonyCount_) - 0.5f) * 300.0f;
                profile.azimuthDeg = wrapSignedDeg(
                    position + sceneRotation + hashSigned(colonySeed + 59u) * 14.0f);
            }
            profile.elevationDeg = hashSigned(colonySeed + 61u) * 9.0f;
            profile.distanceScale = 0.82f
                + hash01(colonySeed + 67u) * 0.40f;
            profile.motionPhase = hash01(colonySeed + 71u);
        }
        for (uint32_t colony = colonyCount_;
             colony < kAmbiInsectMaxColonies; ++colony) {
            colonies_[colony] = {};
        }

        uint32_t voice = 0u;
        for (uint32_t colony = 0u; colony < colonyCount_; ++colony) {
            for (uint32_t local = 0u; local < counts[colony]; ++local) {
                voiceColony_[voice] = colony;
                voiceIndexInColony_[voice] = local;
                ++voice;
            }
        }
        for (; voice < kAmbiInsectMaxVoices; ++voice) {
            voiceColony_[voice] = 0u;
            voiceIndexInColony_[voice] = voice;
        }
    }

    static float normalizedOutputGain(const AmbiInsectParams& params)
    {
        const float voiceNorm = std::pow(static_cast<float>(std::max<uint32_t>(1u, params.voices)), 0.48f);
        return dbToGain(params.outputGainDb) * 1.55f / voiceNorm;
    }

    static float periodicGate(float phase, float duty, float edge)
    {
        duty = clamp(duty, 0.025f, 0.975f);
        edge = std::min(clamp(edge, 0.002f, 0.20f), duty * 0.45f);
        if (phase >= duty) return 0.0f;
        const float attack = clamp(phase / edge, 0.0f, 1.0f);
        const float release = clamp((duty - phase) / edge, 0.0f, 1.0f);
        const float a = attack * attack * (3.0f - 2.0f * attack);
        const float r = release * release * (3.0f - 2.0f * release);
        return std::min(a, r);
    }

    static float snapPulse(float phase, float width)
    {
        phase = wrapUnit(phase);
        width = clamp(width, 0.012f, 0.42f);
        if (phase >= width) return 0.0f;
        const float x = phase / width;
        const float sine = std::sin(kPi * x);
        return sine * sine * std::exp(-x * 2.4f);
    }

    static float flightHarmonicWave(
        float fundamentalSine,
        float fundamentalCosine,
        float bodySize,
        float brightness,
        float signature)
    {
        float sine = fundamentalSine;
        float cosine = fundamentalCosine;
        float result = 0.0f;
        const float compact = 1.0f - bodySize;
        const std::array<float, 8> amplitude {{
            0.62f,
            0.23f + bodySize * 0.08f,
            0.13f + brightness * 0.06f,
            0.075f + bodySize * 0.025f,
            (0.035f + brightness * 0.030f) * (0.62f + compact * 0.38f),
            (0.024f + brightness * 0.020f) * (0.55f + compact * 0.45f),
            (0.016f + brightness * 0.014f) * (0.48f + compact * 0.52f),
            (0.010f + brightness * 0.010f) * (0.42f + compact * 0.58f),
        }};
        for (uint32_t harmonic = 0u; harmonic < amplitude.size(); ++harmonic) {
            if (harmonic > 0u) {
                const float nextSine = sine * fundamentalCosine
                    + cosine * fundamentalSine;
                cosine = cosine * fundamentalCosine
                    - sine * fundamentalSine;
                sine = nextSine;
            }
            const float quadrature = signature
                * ((harmonic & 1u) == 0u ? 0.026f : -0.038f)
                * static_cast<float>(harmonic + 1u);
            result += amplitude[harmonic] * (sine + cosine * quadrature);
        }
        return result * 0.70f;
    }

    static float flightStrokePulse(float phase, float width)
    {
        phase = wrapUnit(phase);
        width = clamp(width, 0.012f, 0.42f);
        if (phase >= width) return 0.0f;
        const float x = phase / width;
        const float tail = 1.0f - x;
        return 46.875f * x * x * tail * tail * tail * tail;
    }

    static AmbiInsectPoint pointFromVec(Vec3 value)
    {
        const float distance = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
        if (distance <= 0.000001f) return {};
        return {
            wrapSignedDeg(std::atan2(value.y, value.x) * 180.0f / kPi),
            std::asin(clamp(value.z / distance, -1.0f, 1.0f)) * 180.0f / kPi,
            distance,
        };
    }

    uint32_t voiceRegime(uint32_t voice) const
    {
        const uint32_t colony = voiceColony_[
            std::min<uint32_t>(voice, kAmbiInsectMaxVoices - 1u)];
        return colonies_[std::min<uint32_t>(
            colony, kAmbiInsectMaxColonies - 1u)].productionMethod;
    }

    AmbiInsectPoint basePoint(uint32_t voice) const
    {
        const uint32_t colonyIndex = voiceColony_[
            std::min<uint32_t>(voice, kAmbiInsectMaxVoices - 1u)];
        const auto& colony = colonies_[std::min<uint32_t>(
            colonyIndex, kAmbiInsectMaxColonies - 1u)];
        const uint32_t localIndex = voiceIndexInColony_[voice];
        const uint32_t localCount = std::max<uint32_t>(1u, colony.voiceCount);
        const float lane = (
            (static_cast<float>(localIndex) + 0.5f)
                / static_cast<float>(localCount)) - 0.5f;
        const float localSpread = localCount > 1u ? 68.0f : 0.0f;
        const float azimuth = wrapSignedDeg(
            colony.azimuthDeg + lane * localSpread
                + hashSigned(voiceIdentity(voice) + 7u) * 12.0f);
        const uint32_t regime = colony.productionMethod;
        float elevation = -20.0f + hashSigned(voice * 137u + 19u) * 12.0f;
        if (regime == 2u) elevation = 32.0f + hashSigned(voice * 137u + 19u) * 17.0f;
        else if (regime == 3u) elevation = 10.0f + hashSigned(voice * 137u + 19u) * 30.0f;
        else if (regime == 4u) elevation = -28.0f + hashSigned(voice * 137u + 19u) * 8.0f;
        else if (regime == 5u) elevation = -36.0f + hashSigned(voice * 137u + 19u) * 7.0f;
        elevation += colony.elevationDeg;
        const float distance = colony.distanceScale
            * (0.82f + hash01(voiceIdentity(voice) + 31u) * 0.34f);
        return {
            azimuth,
            clamp(elevation, -80.0f, 80.0f),
            clamp(distance, 0.52f, 1.62f),
        };
    }

    float noise(AmbiInsectVoice& voice)
    {
        voice.rngState = hash(voice.rngState + 0x6d2b79f5u);
        return static_cast<float>(static_cast<int32_t>(voice.rngState)) / 2147483648.0f;
    }

    void selectCicadaEntrainment(
        uint32_t index, uint32_t eventSeed, bool immediate)
    {
        auto& voice = voices_[index];
        if (voiceRegime(index) != 2u) {
            voice.cicadaEntrainmentMode = 1u;
            voice.cicadaEntrainmentTarget = 1.0f;
            if (immediate) voice.cicadaEntrainmentScale = 1.0f;
            return;
        }

        const auto& params = smoothReady_ ? smoothParams_ : params_;
        float halfProbability = 0.08f
            + (1.0f - params.activity) * 0.20f
            + std::max(0.0f, 0.5f - params.temperature) * 0.18f;
        float doubleProbability = 0.06f
            + params.activity * 0.16f
            + std::max(0.0f, params.temperature - 0.5f) * 0.20f;
        float irregularProbability = 0.025f + params.variation * 0.16f;
        const float nonLocked = halfProbability + doubleProbability
            + irregularProbability;
        if (nonLocked > 0.72f) {
            const float scale = 0.72f / nonLocked;
            halfProbability *= scale;
            doubleProbability *= scale;
            irregularProbability *= scale;
        }

        const float choice = hash01(eventSeed + 0x51ed270bu);
        if (choice < halfProbability) {
            voice.cicadaEntrainmentMode = 0u;
            voice.cicadaEntrainmentTarget = 0.5f;
        } else if (choice < halfProbability + doubleProbability) {
            voice.cicadaEntrainmentMode = 2u;
            voice.cicadaEntrainmentTarget = 2.0f;
        } else if (choice < halfProbability + doubleProbability
            + irregularProbability) {
            voice.cicadaEntrainmentMode = 3u;
            voice.cicadaEntrainmentTarget = 0.68f
                + hash01(eventSeed + 0x94d049bbu) * 0.82f;
        } else {
            voice.cicadaEntrainmentMode = 1u;
            voice.cicadaEntrainmentTarget = 1.0f;
        }
        if (immediate) {
            voice.cicadaEntrainmentScale =
                voice.cicadaEntrainmentTarget;
        }
    }

    void initializeVoice(uint32_t index)
    {
        auto& voice = voices_[index];
        const uint32_t identity = voice.identity == 0u
            ? 0x1b56c4e9u + index * 0x9e3779b9u
            : voice.identity;
        voice = {};
        voice.identity = identity;
        voice.rngState = hash(identity ^ 0xa511e9b3u);
        voice.phrasePhase = hash01(identity + 11u);
        voice.chirpPhase = hash01(identity + 23u);
        voice.pulsePhase = hash01(identity + 37u);
        voice.carrierPhase = hash01(identity + 53u);
        voice.harmonicPhase = hash01(identity + 71u);
        voice.wingPairOffset = 0.002f + hash01(identity + 79u) * 0.008f;
        voice.wingPairOffsetTarget = voice.wingPairOffset;
        voice.pairedWingPhase = wrapUnit(
            voice.carrierPhase + voice.wingPairOffset);
        voice.flutterPhase = hash01(identity + 83u);
        voice.motionPhase = hash01(identity + 89u);
        voice.phraseAccent = 0.78f + hash01(identity + 101u) * 0.22f;
        voice.phraseAccentTarget = voice.phraseAccent;
        voice.pulseAccent = 0.78f + hash01(identity + 113u) * 0.22f;
        voice.pulseRateScale = 0.96f + hash01(identity + 127u) * 0.08f;
        voice.wingCycleGain = 0.90f + hash01(identity + 139u) * 0.20f;
        voice.wingCycleGainTarget = voice.wingCycleGain;
        voice.wingPitchJitter = hashSigned(identity + 149u) * 0.003f;
        voice.wingPitchJitterTarget = voice.wingPitchJitter;
        voice.signatureA = hashSigned(identity + 307u);
        voice.signatureB = hashSigned(identity + 911u);
        voice.activityThreshold = hash01(identity + 1237u);
        voice.flutterRateHz = 1.8f + hash01(identity + 1597u) * 4.2f;
        if (voiceIndexInColony_[index] == 0u) {
            const uint32_t colony = voiceColony_[index];
            voice.phrasePhase = 0.012f
                + hash01(identity + 1709u) * 0.015f;
            voice.chirpPhase = 0.82f
                + hash01(identity + 1783u) * 0.12f
                + static_cast<float>(colony) * 0.007f;
            voice.chirpPhase = wrapUnit(voice.chirpPhase);
            voice.activityThreshold *= 0.08f;
        }
        selectCicadaEntrainment(
            index, identity ^ params_.sceneSeed, true);
    }

    bool voiceHealthy(const AmbiInsectVoice& voice) const
    {
        const bool finite = std::isfinite(voice.phrasePhase) && std::isfinite(voice.chirpPhase)
            && std::isfinite(voice.pulsePhase) && std::isfinite(voice.carrierPhase)
            && std::isfinite(voice.harmonicPhase) && std::isfinite(voice.pairedWingPhase)
            && std::isfinite(voice.flutterPhase) && std::isfinite(voice.motionPhase)
            && std::isfinite(voice.phraseAccent) && std::isfinite(voice.phraseAccentTarget)
            && std::isfinite(voice.pulseAccent) && std::isfinite(voice.pulseRateScale)
            && std::isfinite(voice.pitchDrift) && std::isfinite(voice.pitchDriftTarget)
            && std::isfinite(voice.wingCycleGain) && std::isfinite(voice.wingCycleGainTarget)
            && std::isfinite(voice.wingPitchJitter) && std::isfinite(voice.wingPitchJitterTarget)
            && std::isfinite(voice.wingPairOffset) && std::isfinite(voice.wingPairOffsetTarget)
            && std::isfinite(voice.cicadaEntrainmentScale)
            && std::isfinite(voice.cicadaEntrainmentTarget)
            && std::isfinite(voice.signatureA) && std::isfinite(voice.signatureB)
            && std::isfinite(voice.activityThreshold) && std::isfinite(voice.flutterRateHz)
            && std::isfinite(voice.lane) && std::isfinite(voice.clockScale)
            && std::isfinite(voice.basePitchHz) && std::isfinite(voice.activeWeight)
            && std::isfinite(voice.callEnvelope) && std::isfinite(voice.noiseLow)
            && std::isfinite(voice.noiseHigh) && std::isfinite(voice.energy)
            && std::isfinite(voice.callViz);
        return finite && voice.bodyFilter.healthy() && voice.wingFilter.healthy()
            && voice.abdomenFilter.healthy() && voice.substrateFilter.healthy()
            && voice.raspFilter.healthy() && voice.airFilter.healthy()
            && voice.flightAirFilter.healthy();
    }

    void startTransition()
    {
        transitionTail_ = lastOutput_;
        transitionFade_ = 0.0f;
        smoothParams_ = params_;
        callProfile_ = ambiInsectCallProfile(params_.callType);
        smoothedOutputGain_ = normalizedOutputGain(params_);
        rebuildColonies();
        for (uint32_t voice = 0u; voice < kAmbiInsectMaxVoices; ++voice) {
            voices_[voice].identity = voiceIdentity(voice);
            initializeVoice(voice);
        }
    }

    void updateAudioRateCoefficients()
    {
        phraseAccentAttackCoefficient_ = coefficientForRate(18.0f, sampleRate_);
        phraseAccentReleaseCoefficient_ = coefficientForRate(4.5f, sampleRate_);
        pitchDriftCoefficient_ = coefficientForRate(0.72f, sampleRate_);
        envelopeAttackCoefficient_ = coefficientForRate(420.0f, sampleRate_);
        callVizCoefficient_ = coefficientForRate(22.0f, sampleRate_);
        wingPairCoefficient_ = coefficientForRate(7.0f, sampleRate_);
        cicadaEntrainmentCoefficient_ = coefficientForRate(5.0f, sampleRate_);
    }

    void updateSmoothedParams(float dt)
    {
        if (!smoothReady_) {
            smoothParams_ = params_;
            smoothReady_ = true;
            return;
        }
        const float coefficient = 1.0f - std::exp(-dt * 20.0f);
        smoothParams_.order = params_.order;
        smoothParams_.voices = params_.voices;
        smoothParams_.regime = params_.regime;
        smoothParams_.callType = params_.callType;
        smoothParams_.place = params_.place;
        smoothParams_.sceneSeed = params_.sceneSeed;
        smoothParams_.fieldListenMode = params_.fieldListenMode;
        callProfile_ = ambiInsectCallProfile(params_.callType);
#define S3G_SMOOTH_INSECT_PARAM(name) smoothParams_.name = smoothToward(smoothParams_.name, params_.name, coefficient)
        S3G_SMOOTH_INSECT_PARAM(activity);
        S3G_SMOOTH_INSECT_PARAM(temperature);
        S3G_SMOOTH_INSECT_PARAM(variation);
        S3G_SMOOTH_INSECT_PARAM(coupling);
        S3G_SMOOTH_INSECT_PARAM(phraseRateHz);
        S3G_SMOOTH_INSECT_PARAM(chirpRateHz);
        S3G_SMOOTH_INSECT_PARAM(pulseRateHz);
        S3G_SMOOTH_INSECT_PARAM(callLength);
        S3G_SMOOTH_INSECT_PARAM(rest);
        S3G_SMOOTH_INSECT_PARAM(bodyPitchHz);
        S3G_SMOOTH_INSECT_PARAM(bodySize);
        S3G_SMOOTH_INSECT_PARAM(rasp);
        S3G_SMOOTH_INSECT_PARAM(wing);
        S3G_SMOOTH_INSECT_PARAM(brightness);
        S3G_SMOOTH_INSECT_PARAM(resonance);
        S3G_SMOOTH_INSECT_PARAM(air);
        S3G_SMOOTH_INSECT_PARAM(fieldRateHz);
        S3G_SMOOTH_INSECT_PARAM(roam);
        S3G_SMOOTH_INSECT_PARAM(cohesion);
        S3G_SMOOTH_INSECT_PARAM(scatter);
        S3G_SMOOTH_INSECT_PARAM(orbit);
        S3G_SMOOTH_INSECT_PARAM(lift);
        S3G_SMOOTH_INSECT_PARAM(nearPass);
        S3G_SMOOTH_INSECT_PARAM(spatialFollow);
        smoothParams_.centerAzimuthDeg = smoothAngleDeg(smoothParams_.centerAzimuthDeg, params_.centerAzimuthDeg, coefficient);
        S3G_SMOOTH_INSECT_PARAM(centerElevationDeg);
        S3G_SMOOTH_INSECT_PARAM(centerDistance);
        S3G_SMOOTH_INSECT_PARAM(outputGainDb);
        S3G_SMOOTH_INSECT_PARAM(space);
        S3G_SMOOTH_INSECT_PARAM(environmentSize);
        S3G_SMOOTH_INSECT_PARAM(environmentDecay);
        S3G_SMOOTH_INSECT_PARAM(environmentDamping);
#undef S3G_SMOOTH_INSECT_PARAM
    }

    void updateMotion(float dt)
    {
        const auto& params = smoothParams_;
        fieldPhase_ = wrapUnit(fieldPhase_ + params.fieldRateHz * dt);
        const float fieldAngle = fieldPhase_ * kPi * 2.0f;
        const uint32_t voices = params.voices;
        for (uint32_t index = 0u; index < voices; ++index) {
            auto& voice = voices_[index];
            const uint32_t colonyIndex = voiceColony_[index];
            const auto& colony = colonies_[colonyIndex];
            const uint32_t regime = colony.productionMethod;
            const auto base = basePoint(index);
            const float lane = (
                static_cast<float>(voiceIndexInColony_[index]) + 0.5f)
                / static_cast<float>(std::max<uint32_t>(
                    1u, colony.voiceCount));
            const float seedA = hash01(voice.identity + 1009u);
            const float seedB = hash01(voice.identity + 2027u);
            const float flight = regime == 3u ? 1.0f : (regime == 2u ? 0.16f : 0.04f);
            const float orbitPhase = fieldAngle * (0.35f + params.orbit * 2.2f)
                + lane * kPi * 2.0f + seedA * kPi * 2.0f;
            const float wanderPhase = fieldAngle * (0.21f + params.roam * 1.8f)
                + seedB * kPi * 2.0f;
            const float colonyPhase = fieldAngle
                * (0.14f + params.roam * 0.72f)
                + colony.motionPhase * kPi * 2.0f;
            const float clusterScale = 1.0f - params.cohesion * 0.62f;
            const float radial = clusterScale * (0.16f + params.scatter * 0.62f)
                + std::sin(wanderPhase * 1.31f) * params.roam * 0.20f;
            const float localAzimuth = wrapSignedDeg(
                base.azimuthDeg - colony.azimuthDeg);
            float azimuth = params.centerAzimuthDeg
                + colony.azimuthDeg * (0.72f + clusterScale * 0.28f)
                + localAzimuth * clusterScale
                + std::sin(colonyPhase) * params.roam * 22.0f
                + std::sin(wanderPhase) * params.scatter * 34.0f
                + std::sin(orbitPhase) * params.orbit * (65.0f + flight * 105.0f);
            float elevation = params.centerElevationDeg + base.elevationDeg
                + std::sin(colonyPhase * 0.81f + 0.4f)
                    * params.lift * (5.0f + flight * 11.0f)
                + std::sin(wanderPhase * 0.73f + 0.8f)
                    * params.roam * clusterScale * (8.0f + flight * 28.0f)
                + params.lift * (regime == 2u ? 28.0f : flight * 42.0f);
            float distance = params.centerDistance * colony.distanceScale
                    * (0.74f + radial)
                + std::sin(colonyPhase * 0.67f) * params.roam * 0.10f
                + std::cos(orbitPhase) * params.orbit * 0.26f;
            if (flight > 0.5f) {
                const float pass = 0.5f + 0.5f * std::sin(wanderPhase * 1.83f + seedA * 4.0f);
                distance -= params.nearPass * pass * 0.68f;
                azimuth += std::sin(wanderPhase * 2.11f) * params.nearPass * 80.0f;
                elevation += std::cos(wanderPhase * 1.67f) * params.nearPass * 32.0f;
            }
            targetPoints_[index] = { wrapSignedDeg(azimuth), clamp(elevation, -88.0f, 88.0f), clamp(distance, 0.20f, 2.60f) };
            const float follow = 1.0f - std::exp(-dt * (0.55f + (1.0f - params.spatialFollow) * 9.0f));
            points_[index].azimuthDeg = wrapSignedDeg(points_[index].azimuthDeg
                + wrapSignedDeg(targetPoints_[index].azimuthDeg - points_[index].azimuthDeg) * follow);
            points_[index].elevationDeg += (targetPoints_[index].elevationDeg - points_[index].elevationDeg) * follow;
            points_[index].distance += (targetPoints_[index].distance - points_[index].distance) * follow;
        }
    }

    void updateVoiceAudioControls()
    {
        const auto& params = smoothParams_;
        const uint32_t voices = std::max<uint32_t>(1u, params.voices);
        const float sizeScale = std::exp2(
            (0.5f - params.bodySize) * 1.4f);
        const float maximumPitch = static_cast<float>(sampleRate_) * 0.40f;
        envelopeReleaseCoefficient_ = coefficientForRate(
            55.0f + params.rest * 80.0f, sampleRate_);
        for (uint32_t index = 0u; index < voices; ++index) {
            auto& voice = voices_[index];
            if (!voiceHealthy(voice)) initializeVoice(index);
            const uint32_t colonyIndex = voiceColony_[index];
            const auto& colony = colonies_[colonyIndex];
            const uint32_t regime = colony.productionMethod;
            const auto temperature = ambiInsectTemperatureResponse(regime);
            voice.lane = static_cast<float>(index)
                / static_cast<float>(std::max<uint32_t>(1u, voices - 1u));
            const float localLane = colony.voiceCount > 1u
                ? static_cast<float>(voiceIndexInColony_[index])
                    / static_cast<float>(colony.voiceCount - 1u)
                : 0.5f;
            const float rateVariation = std::exp2(
                voice.signatureA * params.variation * 1.45f
                + (localLane - 0.5f) * params.variation * 0.55f);
            voice.clockScale = rateVariation;
            voice.activeWeight = clamp(
                (params.activity + colony.activityBias
                    - voice.activityThreshold * 0.72f) * 4.0f + 0.15f,
                0.0f, 1.0f);

            const bool dedicatedFlyerRegime =
                regime == 3u && params.regime != kAmbiInsectMixedRegime;
            float pitchCenterHz = params.bodyPitchHz
                * (dedicatedFlyerRegime ? 1.0f : colony.pitchScale);
            float pitchOffsetOctaves = voice.signatureB
                * params.variation * 1.4f
                + (localLane - 0.5f) * params.scatter * 0.24f
                + colony.pitchOffsetOctaves
                    * (0.035f + params.variation * 0.16f);
            if (regime == 3u) {
                const uint32_t flyerIndex = params.regime
                        == kAmbiInsectMixedRegime
                    ? voiceIndexInColony_[index] : index;
                const uint32_t flyerVoices = params.regime
                        == kAmbiInsectMixedRegime
                    ? colony.voiceCount : voices;
                pitchOffsetOctaves = flyerPopulationPitchOffset(
                    flyerIndex, flyerVoices)
                    + voice.signatureB * params.variation * 0.86f
                    + voice.signatureA * params.scatter * 0.055f;
            }
            float pitch = pitchCenterHz * std::exp2(pitchOffsetOctaves)
                * ambiInsectTemperatureScale(
                    params.temperature, temperature.pitch);
            if (regime == 3u) pitch = clamp(pitch, 90.0f, 1600.0f);
            else if (regime == 5u) pitch = clamp(pitch, 70.0f, 1800.0f);
            else pitch = clamp(pitch, 180.0f, maximumPitch);
            const float driftScale = std::exp2(voice.pitchDrift);
            const float regimeMaximumPitch = regime == 3u
                ? 1800.0f : maximumPitch;
            pitch = clamp(pitch * sizeScale * driftScale,
                70.0f, regimeMaximumPitch);
            voice.basePitchHz = pitch;
        }
    }

    AmbiInsectVoiceOutput processVoice(uint32_t index)
    {
        const auto& params = smoothParams_;
        auto& voice = voices_[index];
        const uint32_t colonyIndex = voiceColony_[index];
        const auto& colony = colonies_[colonyIndex];
        const uint32_t regime = colony.productionMethod;
        const auto temperature = ambiInsectTemperatureResponse(regime);
        const float phraseTemperature = ambiInsectTemperatureScale(
            params.temperature, temperature.phrase);
        const float chirpTemperature = ambiInsectTemperatureScale(
            params.temperature, temperature.chirp);
        const float pulseTemperature = ambiInsectTemperatureScale(
            params.temperature, temperature.pulse);
        const float lane = voice.lane;
        const float randomA = voice.signatureA;
        const float randomB = voice.signatureB;
        const float clockScale = voice.clockScale;
        const auto& call = callProfile_;
        const float dt = 1.0f / static_cast<float>(sampleRate_);

        const float previousPhrasePhase = voice.phrasePhase;
        voice.phrasePhase = wrapUnit(voice.phrasePhase
            + params.phraseRateHz * call.phraseRateScale
                * colony.phraseRateScale * phraseTemperature
                * clockScale * dt);
        const float chorusTarget = wrapUnit(colony.phrasePhase
            + ((voiceIndexInColony_[index] & 1u) != 0u
                    ? call.alternatePhase : 0.0f));
        const float effectiveCoupling = clamp(
            params.coupling + call.synchronyBias, 0.0f, 1.0f);
        voice.phrasePhase = wrapUnit(voice.phrasePhase
            + phaseDelta(chorusTarget, voice.phrasePhase)
                * effectiveCoupling
                * (0.00018f + params.activity * 0.00032f));
        if (voice.phrasePhase < previousPhrasePhase) {
            ++voice.phraseGeneration;
            const uint32_t eventSeed = hash(
                voice.identity ^ (voice.phraseGeneration * 0x9e3779b9u));
            const float callChoice = hash01(eventSeed + 17u);
            const float callProbability = clamp(
                0.34f + params.activity * 0.78f - params.rest * 0.18f
                    + call.callProbabilityBias,
                0.08f, 0.98f);
            voice.phraseAccentTarget = callChoice < callProbability
                ? 0.68f + hash01(eventSeed + 41u) * 0.42f
                : 0.0f;
            voice.pitchDriftTarget = hashSigned(eventSeed + 73u)
                * params.variation * 0.085f;
            if (regime == 2u) {
                selectCicadaEntrainment(
                    index, eventSeed ^ params.sceneSeed, false);
            }
        }
        const float phraseAccentRate = voice.phraseAccentTarget > voice.phraseAccent
            ? phraseAccentAttackCoefficient_ : phraseAccentReleaseCoefficient_;
        voice.phraseAccent += (voice.phraseAccentTarget - voice.phraseAccent)
            * phraseAccentRate;
        voice.pitchDrift += (voice.pitchDriftTarget - voice.pitchDrift)
            * pitchDriftCoefficient_;
        voice.cicadaEntrainmentScale += (
            voice.cicadaEntrainmentTarget
                - voice.cicadaEntrainmentScale)
            * cicadaEntrainmentCoefficient_;
        voice.chirpPhase = wrapUnit(
            voice.chirpPhase + params.chirpRateHz * call.chirpRateScale
                * colony.chirpRateScale * chirpTemperature
                * clockScale * dt);
        float entrainmentScale = regime == 2u
            ? voice.cicadaEntrainmentScale : 1.0f;
        if (regime == 2u && voice.cicadaEntrainmentMode == 3u) {
            entrainmentScale *= 1.0f
                + ambiInsectSin(
                    voice.flutterPhase * 0.37f + randomB * 0.23f)
                    * (0.035f + params.variation * 0.075f);
        }
        const float previousPulsePhase = voice.pulsePhase;
        voice.pulsePhase = wrapUnit(voice.pulsePhase
            + params.pulseRateHz * call.pulseRateScale * clockScale
                * colony.pulseRateScale * pulseTemperature
                * voice.pulseRateScale * entrainmentScale * dt);
        if (voice.pulsePhase < previousPulsePhase) {
            ++voice.pulseGeneration;
            const uint32_t eventSeed = hash(
                voice.identity ^ (voice.pulseGeneration * 0x85ebca6bu));
            const float omission = regime == 2u
                ? 0.08f + params.variation * 0.24f
                : 0.015f + params.variation * 0.07f;
            voice.pulseAccent = hash01(eventSeed + 19u) < omission
                ? 0.0f
                : 0.58f + hash01(eventSeed + 43u) * 0.54f;
            const float pulseJitter = regime == 2u
                    && voice.cicadaEntrainmentMode == 3u
                ? 0.10f + params.variation * 0.18f
                : params.variation * (regime == 3u ? 0.08f : 0.035f);
            voice.pulseRateScale = 1.0f + hashSigned(eventSeed + 67u)
                * pulseJitter;
        }

        const float activeWeight = voice.activeWeight;
        const float phraseDuty = clamp(
            0.94f - params.rest * 0.82f + call.phraseDutyBias,
            0.04f, 0.98f);
        const float phraseGate = periodicGate(voice.phrasePhase, phraseDuty, 0.035f + params.rest * 0.055f);
        float chirpDuty = 0.08f + params.callLength * 0.80f;
        if (regime == 1u || regime == 2u) chirpDuty = 0.42f + params.callLength * 0.52f;
        if (regime == 3u) chirpDuty = 0.70f + params.callLength * 0.26f;
        if (regime == 5u) chirpDuty = 0.26f + params.callLength * 0.62f;
        chirpDuty = clamp(chirpDuty + call.chirpDutyBias, 0.025f, 0.98f);
        const float chirpGate = periodicGate(voice.chirpPhase, chirpDuty, 0.018f + params.callLength * 0.035f);
        const float articulatedEnvelope = activeWeight * voice.phraseAccent
            * phraseGate * chirpGate;
        const float sustainedEnvelope = activeWeight
            * (0.28f + voice.phraseAccent * 0.72f)
            * (0.38f + phraseGate * 0.62f);
        float targetEnvelope = lerp(
            articulatedEnvelope, sustainedEnvelope, call.sustain) * call.level;
        if (regime == 3u) {
            const float flightEnvelope = activeWeight
                * (0.18f + voice.phraseAccent * 0.82f)
                * (0.22f + phraseGate * 0.78f)
                * (0.58f + chirpGate * 0.42f) * call.level;
            targetEnvelope = lerp(targetEnvelope, flightEnvelope, 0.72f);
        }
        const float envelopeCoefficient = targetEnvelope > voice.callEnvelope
            ? envelopeAttackCoefficient_ : envelopeReleaseCoefficient_;
        voice.callEnvelope += (targetEnvelope - voice.callEnvelope)
            * envelopeCoefficient;
        voice.callViz += (voice.callEnvelope - voice.callViz)
            * callVizCoefficient_;

        float pitch = voice.basePitchHz;
        voice.flutterPhase = wrapUnit(
            voice.flutterPhase + voice.flutterRateHz * dt);
        const float flutter = ambiInsectSin(voice.flutterPhase)
            + ambiInsectSin(
                voice.flutterPhase * 2.0f
                    + randomA * 1.7f * kAmbiInsectInvTwoPi) * 0.28f;
        const float flutterDepth = regime == 3u
            ? params.variation * (0.006f + (1.0f - params.bodySize) * 0.010f)
            : params.variation * 0.008f;
        pitch *= 1.0f + flutter * flutterDepth;
        if (regime == 3u) {
            const float wingResponse = clamp(
                dt * (18.0f + pitch * 0.055f), 0.0f, 1.0f);
            voice.wingCycleGain += (
                voice.wingCycleGainTarget - voice.wingCycleGain) * wingResponse;
            voice.wingPitchJitter += (
                voice.wingPitchJitterTarget - voice.wingPitchJitter) * wingResponse;
            voice.wingPairOffset += (
                voice.wingPairOffsetTarget - voice.wingPairOffset)
                * wingPairCoefficient_;
            pitch *= 1.0f + voice.wingPitchJitter;
        }
        const float nearDoppler = regime == 3u
            ? ambiInsectSin(fieldPhase_ * 1.83f + lane)
                * params.nearPass * 0.035f
            : 0.0f;
        pitch *= 1.0f + nearDoppler;

        const float previousCarrierPhase = voice.carrierPhase;
        voice.carrierPhase = wrapUnit(voice.carrierPhase + pitch * dt);
        if (regime == 3u && voice.carrierPhase < previousCarrierPhase) {
            ++voice.wingGeneration;
            const uint32_t cycleSeed = hash(
                voice.identity ^ (voice.wingGeneration * 0x27d4eb2du));
            voice.wingCycleGainTarget = 0.86f + hash01(cycleSeed + 13u) * 0.28f;
            voice.wingPitchJitterTarget = hashSigned(cycleSeed + 29u)
                * (0.0025f + params.variation * 0.010f);
            voice.wingPairOffsetTarget = 0.002f + hash01(cycleSeed + 47u)
                * (0.008f + params.variation * 0.006f);
        }
        if (regime == 3u) {
            const float pairWander = ambiInsectSin(
                voice.flutterPhase + randomB * kAmbiInsectInvTwoPi)
                * params.variation * 0.0015f;
            voice.pairedWingPhase = wrapUnit(
                voice.carrierPhase + voice.wingPairOffset + pairWander);
        } else {
            const float pairDetune = 1.0f + randomB * 0.006f;
            voice.pairedWingPhase = wrapUnit(
                voice.pairedWingPhase + pitch * pairDetune * dt);
        }
        const bool flexibleWingRegime = regime == 0u || regime == 1u || regime == 4u;
        if (flexibleWingRegime) {
            voice.harmonicPhase = wrapUnit(
                voice.harmonicPhase + pitch * (1.997f + randomA * 0.004f) * dt);
        }
        const float phaseRadians = voice.carrierPhase * kPi * 2.0f;
        const float pairedRadians = voice.pairedWingPhase * kPi * 2.0f;
        float fundamental = 0.0f;
        float fundamentalCosine = 1.0f;
        float pairedFundamental = 0.0f;
        float pairedFundamentalCosine = 1.0f;
        float second = 0.0f;
        float third = 0.0f;
        float pairedSecond = 0.0f;
        float pairedThird = 0.0f;
        if (regime != 2u) {
            fundamental = ambiInsectSin(voice.carrierPhase);
            pairedFundamental = ambiInsectSin(voice.pairedWingPhase);
        }
        if (regime == 3u) {
            fundamentalCosine = ambiInsectCos(voice.carrierPhase);
            pairedFundamentalCosine = ambiInsectCos(voice.pairedWingPhase);
        } else if (flexibleWingRegime) {
            const float secondMask = clamp(
                (static_cast<float>(sampleRate_) * 0.46f - pitch * 2.0f)
                    / (static_cast<float>(sampleRate_) * 0.08f),
                0.0f, 1.0f);
            const float thirdMask = clamp(
                (static_cast<float>(sampleRate_) * 0.46f - pitch * 3.0f)
                    / (static_cast<float>(sampleRate_) * 0.08f),
                0.0f, 1.0f);
            second = std::sin(voice.harmonicPhase * kPi * 2.0f)
                * secondMask;
            third = std::sin(phaseRadians * 3.0f + randomB * 0.24f)
                * thirdMask;
            pairedSecond = std::sin(pairedRadians * 2.0f - 0.43f)
                * secondMask;
            pairedThird = std::sin(pairedRadians * 3.0f + 0.19f)
                * thirdMask;
        }
        const float white = noise(voice);
        voice.noiseLow += (white - voice.noiseLow) * (0.008f + params.bodySize * 0.022f);
        voice.noiseHigh += (white - voice.noiseHigh) * (0.08f + params.brightness * 0.34f);
        const float roughNoise = white - voice.noiseLow;
        const float airNoise = white - voice.noiseHigh;

        float toothStrike = 0.0f;
        float syllableGate = 0.0f;
        if (regime == 0u || regime == 1u) {
            const float toothWidth = 0.055f + params.bodySize * 0.075f;
            toothStrike = (snapPulse(voice.pulsePhase, toothWidth)
                - snapPulse(voice.pulsePhase - toothWidth * 0.72f,
                    toothWidth * 0.64f) * 0.28f) * voice.pulseAccent;
            syllableGate = periodicGate(
                voice.pulsePhase,
                clamp(0.46f + params.callLength * 0.32f, 0.42f, 0.82f),
                0.055f + params.bodySize * 0.045f) * voice.pulseAccent;
        }
        float strokeReversal = 0.0f;
        float pairedStrokeReversal = 0.0f;
        if (regime == 3u) {
            strokeReversal = flightStrokePulse(voice.carrierPhase, 0.12f)
                - flightStrokePulse(voice.carrierPhase - 0.50f, 0.17f) * 0.54f;
            pairedStrokeReversal = flightStrokePulse(voice.pairedWingPhase, 0.12f)
                - flightStrokePulse(voice.pairedWingPhase - 0.50f, 0.17f) * 0.54f;
        }
        float pairedFlexibleWing = 0.0f;
        if (flexibleWingRegime) {
            const float flexibleWingA = std::tanh(
                fundamental * 1.04f + second * 0.18f + third * 0.045f);
            const float flexibleWingB = std::tanh(
                pairedFundamental * 1.04f + pairedSecond * 0.18f
                    + pairedThird * 0.045f);
            pairedFlexibleWing = flexibleWingA * 0.58f
                + flexibleWingB * 0.42f;
        }
        const float q = clamp(
            0.08f + params.resonance * 0.54f + params.bodySize * 0.05f,
            0.08f, 0.72f);
        float source = 0.0f;
        float wingLayer = 0.0f;
        float contactExcitation = 0.0f;

        if (regime == 0u) {
            const float fileStrike = toothStrike
                * (0.38f + roughNoise * (0.18f + params.rasp * 0.30f));
            const float wingRadiation = pairedFlexibleWing * syllableGate
                * (0.86f + roughNoise * params.variation * 0.055f);
            const float fileBand = voice.wingFilter.process(
                fileStrike + roughNoise * syllableGate * params.rasp * 0.11f,
                pitch * (1.0f + randomB * 0.012f),
                q, static_cast<float>(sampleRate_)) * 0.72f;
            const float body = voice.bodyFilter.process(
                fileStrike + wingRadiation * 0.07f,
                pitch * (0.975f + params.bodySize * 0.025f),
                clamp(q * 0.72f, 0.06f, 0.58f),
                static_cast<float>(sampleRate_)) * 0.58f;
            wingLayer = wingRadiation + fileBand;
            source = wingLayer * (0.46f + params.wing * 0.48f)
                + body * (0.22f + params.resonance * 0.22f)
                + roughNoise * syllableGate * params.rasp * 0.045f;
            contactExcitation = fileStrike + body * 0.24f;
        } else if (regime == 1u) {
            const float trillMotion = 0.76f
                + 0.24f * std::sin(voice.chirpPhase * kPi * 2.0f);
            const float fileStrike = toothStrike * trillMotion
                * (0.34f + roughNoise * (0.22f + params.rasp * 0.34f));
            const float wingRadiation = pairedFlexibleWing * syllableGate
                * trillMotion
                * (0.80f + roughNoise * params.variation * 0.065f);
            const float fileBand = voice.wingFilter.process(
                fileStrike + roughNoise * syllableGate * params.rasp * 0.14f,
                pitch * (1.0f + randomA * 0.018f),
                q, static_cast<float>(sampleRate_)) * 0.68f;
            const float body = voice.bodyFilter.process(
                fileStrike + wingRadiation * 0.06f,
                pitch * (0.965f + params.bodySize * 0.035f),
                clamp(q * 0.68f, 0.06f, 0.56f),
                static_cast<float>(sampleRate_)) * 0.54f;
            wingLayer = wingRadiation + fileBand;
            source = wingLayer * (0.42f + params.wing * 0.52f)
                + body * (0.20f + params.resonance * 0.20f)
                + roughNoise * syllableGate * params.rasp * 0.055f;
            contactExcitation = fileStrike + body * 0.20f;
        } else if (regime == 2u) {
            float ribs = 0.0f;
            ribs += snapPulse(voice.pulsePhase, 0.18f);
            ribs += snapPulse(voice.pulsePhase - 0.075f, 0.15f) * 0.86f;
            ribs += snapPulse(voice.pulsePhase - 0.145f, 0.13f) * 0.72f;
            const float driveNormalization = clamp(
                1.0f / std::sqrt(std::max(0.45f, entrainmentScale)),
                0.72f, 1.16f);
            ribs *= voice.pulseAccent * driveNormalization;
            const float buckling = ribs
                * (roughNoise * (0.48f + params.rasp * 0.38f)
                    + airNoise * 0.20f);
            const float bandQ = 0.12f + params.resonance * 0.30f;
            const float lowerBand = voice.bodyFilter.process(
                buckling,
                clamp(pitch * 1.18f, 2400.0f,
                    static_cast<float>(sampleRate_) * 0.36f),
                bandQ, static_cast<float>(sampleRate_)) * 2.35f;
            const float upperBand = voice.abdomenFilter.process(
                buckling,
                clamp(pitch * 1.62f, 3600.0f,
                    static_cast<float>(sampleRate_) * 0.40f),
                clamp(bandQ * 0.86f, 0.08f, 0.46f),
                static_cast<float>(sampleRate_)) * 1.85f;
            const float bodyScatter = voice.wingFilter.process(
                buckling + lowerBand * 0.08f,
                clamp(pitch * (0.72f + params.bodySize * 0.18f),
                    900.0f, static_cast<float>(sampleRate_) * 0.28f),
                0.08f + params.bodySize * 0.16f,
                static_cast<float>(sampleRate_)) * 0.72f;
            wingLayer = upperBand;
            source = lowerBand
                + upperBand * (0.46f + params.wing * 0.34f)
                + bodyScatter * (0.26f + params.bodySize * 0.24f)
                + buckling * 0.055f;
            contactExcitation = buckling + bodyScatter * 0.12f;
        } else if (regime == 3u) {
            const float wingA = flightHarmonicWave(
                fundamental, fundamentalCosine,
                params.bodySize, params.brightness, randomA);
            const float wingB = flightHarmonicWave(
                pairedFundamental, pairedFundamentalCosine,
                params.bodySize, params.brightness, -randomB);
            const float bilateralBalance = 0.50f + randomA * 0.045f;
            const float aerodynamicWing = (
                wingA * bilateralBalance + wingB * (1.0f - bilateralBalance))
                * voice.wingCycleGain;
            const float midStroke = 0.5f * (
                std::fabs(fundamentalCosine)
                + std::fabs(pairedFundamentalCosine));
            const float reversal = strokeReversal
                + pairedStrokeReversal * 0.72f;
            const float wakeExcitation = roughNoise
                * (0.20f + params.bodySize * 0.34f)
                * (0.22f + midStroke * 0.78f)
                + reversal * airNoise
                    * (0.08f + params.brightness * 0.14f);
            const float thorax = voice.bodyFilter.process(
                aerodynamicWing * (0.12f + params.bodySize * 0.16f)
                    + reversal * (0.18f + roughNoise * 0.24f),
                clamp(pitch * (0.46f + params.bodySize * 0.24f),
                    70.0f, static_cast<float>(sampleRate_) * 0.36f),
                clamp(q * 0.46f, 0.05f, 0.34f),
                static_cast<float>(sampleRate_)) * 1.24f;
            const float wakeBody = voice.flightAirFilter.process(
                wakeExcitation,
                clamp(pitch * (1.15f + params.bodySize * 0.72f),
                    100.0f, static_cast<float>(sampleRate_) * 0.34f),
                0.08f + params.resonance * 0.10f,
                static_cast<float>(sampleRate_)) * 1.65f;
            const float wingAir = voice.wingFilter.process(
                wakeExcitation + reversal * roughNoise * 0.18f,
                clamp(pitch * (2.4f + params.brightness * 2.8f),
                    180.0f, static_cast<float>(sampleRate_) * 0.40f),
                0.08f + params.resonance * 0.14f,
                static_cast<float>(sampleRate_)) * 1.32f;
            wingLayer = aerodynamicWing * flightChorusGain_
                + wakeBody * (0.22f + params.bodySize * 0.36f)
                + wingAir * (0.22f + params.brightness * 0.24f);
            source = thorax * (0.36f + params.bodySize * 0.34f)
                + wingLayer * (0.34f + params.wing * 0.54f)
                + wakeExcitation * params.rasp * (0.045f + params.bodySize * 0.065f);
        } else if (regime == 4u) {
            const float tick = snapPulse(
                voice.chirpPhase, 0.08f + params.callLength * 0.10f);
            const float scrape = tick
                * (roughNoise * (0.42f + params.rasp * 0.48f)
                    + pairedFlexibleWing * (0.05f + params.wing * 0.08f));
            const float shell = voice.bodyFilter.process(
                scrape, pitch, clamp(q * 0.68f, 0.06f, 0.50f),
                static_cast<float>(sampleRate_));
            wingLayer = voice.wingFilter.process(scrape,
                pitch * (1.18f + randomA * 0.025f),
                clamp(q * 0.42f, 0.05f, 0.34f),
                static_cast<float>(sampleRate_));
            source = shell * 1.32f + scrape * 0.62f
                + wingLayer * params.wing * 0.22f;
            contactExcitation = tick * (roughNoise * 0.62f + 0.38f)
                + shell * 0.18f;
        } else {
            const float bodyMotion = ambiInsectSin(voice.chirpPhase);
            const float bodyVelocity = ambiInsectCos(voice.chirpPhase);
            const float contactGate = periodicGate(
                voice.pulsePhase,
                0.18f + params.callLength * 0.34f,
                0.06f + params.bodySize * 0.05f);
            const float pressure = 0.24f + std::fabs(bodyMotion) * 0.76f;
            const float slip = contactGate
                * (roughNoise * (0.34f + params.rasp * 0.46f)
                    + bodyVelocity * (0.08f + params.wing * 0.16f));
            const float substrateDrive = slip * pressure
                + airNoise * bodyMotion * params.rasp * 0.08f;
            const float substrateBody = voice.bodyFilter.process(
                substrateDrive,
                pitch,
                0.10f + params.resonance * 0.30f,
                static_cast<float>(sampleRate_)) * 1.48f;
            const float contactOvertone = voice.wingFilter.process(
                substrateDrive + substrateBody * 0.10f,
                clamp(pitch * (1.72f + params.brightness * 1.38f),
                    120.0f, static_cast<float>(sampleRate_) * 0.30f),
                0.07f + params.resonance * 0.18f,
                static_cast<float>(sampleRate_)) * 0.62f;
            wingLayer = contactOvertone;
            source = substrateBody * (0.66f + params.bodySize * 0.34f)
                + contactOvertone * (0.14f + params.wing * 0.32f)
                + substrateDrive * (0.10f + params.rasp * 0.16f);
            contactExcitation = substrateDrive
                + substrateBody * (0.24f + params.bodySize * 0.18f);
        }

        const float raspBand = voice.raspFilter.process(roughNoise * params.rasp * voice.callEnvelope,
            clamp(pitch * (1.15f + params.brightness * 2.8f), 120.0f, static_cast<float>(sampleRate_) * 0.42f),
            0.10f + params.resonance * 0.34f, static_cast<float>(sampleRate_));
        const float airBand = voice.airFilter.process(airNoise * params.air * voice.callEnvelope,
            clamp(pitch * (2.4f + params.brightness * 5.0f), 800.0f, static_cast<float>(sampleRate_) * 0.43f),
            0.06f + params.brightness * 0.20f, static_cast<float>(sampleRate_));
        const float substratePitch = clamp(
            pitch * (0.055f + params.bodySize * 0.14f),
            55.0f, std::min(1800.0f, static_cast<float>(sampleRate_) * 0.20f));
        const float substrate = regime == 3u ? 0.0f
            : voice.substrateFilter.process(contactExcitation,
                substratePitch, 0.22f + params.bodySize * 0.18f,
                static_cast<float>(sampleRate_));
        const float bodyLevel = 0.16f + params.bodySize * 0.34f;
        const float tonalLevel = regime == 3u
            ? 0.38f + params.wing * 0.16f
            : 0.28f + params.resonance * 0.24f;
        static constexpr std::array<float, kAmbiInsectProductionMethodCount> regimeCompensation {
            4.5f, 3.0f, 4.5f, 1.0f, 6.0f, 4.2f
        };
        const float regimeGain = regimeCompensation[
            std::min<uint32_t>(regime, kAmbiInsectProductionMethodCount - 1u)];
        const float substrateMix = regime == 5u
            ? 0.24f + params.bodySize * 0.22f
            : 0.035f + params.bodySize * 0.075f;
        const float mixed = (source * tonalLevel + raspBand * (0.08f + params.rasp * 0.24f)
            + airBand * (0.04f + params.air * 0.20f)
            + substrate * substrateMix)
            * voice.callEnvelope * (0.62f + bodyLevel);
        const float direct = std::tanh(clamp(mixed * regimeGain * (1.0f + params.brightness * 0.38f), -3.6f, 3.6f));
        const float fieldSend = std::tanh(clamp(
            (source * 0.32f + raspBand * 0.22f + airBand * 0.34f
                + substrate * (regime == 5u ? 0.92f : 0.58f))
                * voice.callEnvelope * regimeGain,
            -3.0f, 3.0f));
        if (!std::isfinite(direct) || !std::isfinite(fieldSend)) {
            initializeVoice(index);
            return {};
        }
        return { direct, fieldSend };
    }

    AmbiInsectParams params_ {};
    AmbiInsectParams smoothParams_ {};
    std::array<AmbiInsectVoice, kAmbiInsectMaxVoices> voices_ {};
    std::array<AmbiInsectColony, kAmbiInsectMaxColonies> colonies_ {};
    std::array<uint32_t, kAmbiInsectMaxVoices> voiceColony_ {};
    std::array<uint32_t, kAmbiInsectMaxVoices> voiceIndexInColony_ {};
    uint32_t colonyCount_ = 1u;
    std::array<AmbiInsectPoint, kAmbiInsectMaxVoices> points_ {};
    std::array<AmbiInsectPoint, kAmbiInsectMaxVoices> targetPoints_ {};
    double sampleRate_ = 48000.0;
    float fieldPhase_ = 0.0f;
    float globalPhrasePhase_ = 0.0f;
    float transitionFade_ = 1.0f;
    float smoothedOutputGain_ = dbToGain(-6.0f);
    float flightChorusGain_ = 1.0f;
    float phraseAccentAttackCoefficient_ = 0.000375f;
    float phraseAccentReleaseCoefficient_ = 0.000094f;
    float pitchDriftCoefficient_ = 0.000015f;
    float envelopeAttackCoefficient_ = 0.008713f;
    float envelopeReleaseCoefficient_ = 0.001145f;
    float callVizCoefficient_ = 0.000458f;
    float wingPairCoefficient_ = 0.000146f;
    float cicadaEntrainmentCoefficient_ = 0.000104f;
    AmbiInsectCallProfile callProfile_ {};
    bool smoothReady_ = false;
    std::array<float, kAmbiInsectMaxChannels> lastOutput_ {};
    std::array<float, kAmbiInsectMaxChannels> transitionTail_ {};
    std::array<float, kAmbiInsectMaxChannels> listenerFrame_ {};
    std::array<Vec3, kAmbiFieldListenerMaxLobes> listenDirections_ {};
    std::array<float, kAmbiFieldListenerMaxLobes> currentListenWeights_ {};
    std::array<float, kAmbiFieldListenerMaxLobes> targetListenWeights_ {};
    AmbiFieldListener fieldListener_ {};
    float currentListenMix_ = 0.0f;
    AmbiEnvironmentField environmentField_ {};
    std::atomic<bool> transitionRequested_ { false };
};

} // namespace s3g
