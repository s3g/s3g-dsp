#pragma once

#include "s3g_ambi_environment_field.h"
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
constexpr uint32_t kAmbiInsectRegimeCount = 6u;
constexpr uint32_t kAmbiInsectPlaceCount = 7u;
constexpr uint32_t kAmbiInsectSineTableSize = 4096u;
constexpr float kAmbiInsectInvTwoPi = 0.15915494309f;

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
        smoothParams_ = params_;
        smoothReady_ = true;
        smoothedOutputGain_ = normalizedOutputGain(params_);
        for (uint32_t voice = 0u; voice < kAmbiInsectMaxVoices; ++voice) {
            voices_[voice] = {};
            voices_[voice].identity = 0x1b56c4e9u + voice * 0x9e3779b9u;
            initializeVoice(voice);
            points_[voice] = basePoint(voice, std::max<uint32_t>(1u, params_.voices), params_.regime);
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

        const uint32_t oldVoices = params_.voices;
        params_ = params;
        if (params.voices > oldVoices) {
            for (uint32_t voice = oldVoices; voice < params.voices; ++voice) initializeVoice(voice);
        }
    }

    AmbiInsectParams params() const { return params_; }
    void beginTransition() { transitionRequested_.store(true, std::memory_order_release); }
    float voiceEnergy(uint32_t voice) const { return voices_[std::min<uint32_t>(voice, kAmbiInsectMaxVoices - 1u)].energy; }
    float voiceCallLevel(uint32_t voice) const { return voices_[std::min<uint32_t>(voice, kAmbiInsectMaxVoices - 1u)].callViz; }
    AmbiInsectPoint voicePoint(uint32_t voice) const { return points_[std::min<uint32_t>(voice, kAmbiInsectMaxVoices - 1u)]; }

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
            for (uint32_t voice = 0u; voice < voices; ++voice) {
                directions[voice] = directionFromAed(points_[voice].azimuthDeg, points_[voice].elevationDeg);
                basis[voice] = acnSn3dBasis7(directions[voice]);
                distanceGain[voice] = 1.0f / std::max(0.42f, points_[voice].distance);
            }

            for (uint32_t frame = chunkStart; frame < chunkStart + chunkFrames; ++frame) {
                smoothedOutputGain_ += (targetGain - smoothedOutputGain_) * 0.0014f;
                const float temperatureScale = std::exp2((smoothParams_.temperature - 0.5f) * 1.35f);
                globalPhrasePhase_ = wrapUnit(globalPhrasePhase_
                    + smoothParams_.phraseRateHz * temperatureScale / static_cast<float>(sampleRate_));
                flightConvergence_ = 1.0f
                    + ambiInsectSin(globalPhrasePhase_)
                        * smoothParams_.coupling * 0.08f;
                environmentField_.beginFrame();
                for (uint32_t voice = 0u; voice < voices; ++voice) {
                    const auto voiceOutput = processVoice(voice);
                    float sample = voiceOutput.direct * smoothedOutputGain_ * distanceGain[voice];
                    if (!std::isfinite(sample)) {
                        initializeVoice(voice);
                        sample = 0.0f;
                    }
                    voices_[voice].energy += (sample * sample - voices_[voice].energy) * 0.0012f;
                    if (std::fabs(sample) < 0.0000001f) continue;
                    environmentField_.addSource(voiceOutput.fieldSend * smoothedOutputGain_
                        * distanceGain[voice] * 2.2f, directions[voice]);
                    for (uint32_t channel = 0u; channel < ambiChannels; ++channel) {
                        if (outputs[channel]) outputs[channel][frame] = flushDenormal(outputs[channel][frame]
                            + sample * basis[voice][channel]);
                    }
                }
                const auto environment = environmentField_.process();
                for (uint32_t channel = 0u; channel < std::min<uint32_t>(ambiChannels, kAmbiEnvironmentChannels); ++channel) {
                    if (outputs[channel]) outputs[channel][frame] = flushDenormal(outputs[channel][frame] + environment[channel]);
                }
            }
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

    static uint32_t voiceRegime(uint32_t selectedRegime, uint32_t voice)
    {
        return selectedRegime == 5u ? voice % 5u : selectedRegime;
    }

    static AmbiInsectPoint basePoint(uint32_t voice, uint32_t voices, uint32_t selectedRegime)
    {
        const float lane = (static_cast<float>(voice) + 0.5f) / static_cast<float>(std::max<uint32_t>(1u, voices));
        const float azimuth = wrapSignedDeg(lane * 360.0f - 180.0f + hashSigned(voice * 101u + 7u) * 20.0f);
        const uint32_t regime = voiceRegime(selectedRegime, voice);
        float elevation = -20.0f + hashSigned(voice * 137u + 19u) * 12.0f;
        if (regime == 2u) elevation = 32.0f + hashSigned(voice * 137u + 19u) * 17.0f;
        else if (regime == 3u) elevation = 10.0f + hashSigned(voice * 137u + 19u) * 30.0f;
        else if (regime == 4u) elevation = -28.0f + hashSigned(voice * 137u + 19u) * 8.0f;
        return { azimuth, clamp(elevation, -80.0f, 80.0f), 0.78f + hash01(voice * 173u + 31u) * 0.52f };
    }

    float noise(AmbiInsectVoice& voice)
    {
        voice.rngState = hash(voice.rngState + 0x6d2b79f5u);
        return static_cast<float>(static_cast<int32_t>(voice.rngState)) / 2147483648.0f;
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
        smoothedOutputGain_ = normalizedOutputGain(params_);
        for (uint32_t voice = 0u; voice < kAmbiInsectMaxVoices; ++voice) initializeVoice(voice);
    }

    void updateAudioRateCoefficients()
    {
        phraseAccentAttackCoefficient_ = coefficientForRate(18.0f, sampleRate_);
        phraseAccentReleaseCoefficient_ = coefficientForRate(4.5f, sampleRate_);
        pitchDriftCoefficient_ = coefficientForRate(0.72f, sampleRate_);
        envelopeAttackCoefficient_ = coefficientForRate(420.0f, sampleRate_);
        callVizCoefficient_ = coefficientForRate(22.0f, sampleRate_);
        wingPairCoefficient_ = coefficientForRate(7.0f, sampleRate_);
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
        smoothParams_.place = params_.place;
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
            const uint32_t regime = voiceRegime(params.regime, index);
            const auto base = basePoint(index, voices, params.regime);
            const float lane = (static_cast<float>(index) + 0.5f) / static_cast<float>(voices);
            const float seedA = hash01(voice.identity + 1009u);
            const float seedB = hash01(voice.identity + 2027u);
            const float flight = regime == 3u ? 1.0f : (regime == 2u ? 0.16f : 0.04f);
            const float orbitPhase = fieldAngle * (0.35f + params.orbit * 2.2f)
                + lane * kPi * 2.0f + seedA * kPi * 2.0f;
            const float wanderPhase = fieldAngle * (0.21f + params.roam * 1.8f)
                + seedB * kPi * 2.0f;
            const float clusterScale = 1.0f - params.cohesion * 0.62f;
            const float radial = clusterScale * (0.16f + params.scatter * 0.62f)
                + std::sin(wanderPhase * 1.31f) * params.roam * 0.20f;
            float azimuth = params.centerAzimuthDeg + base.azimuthDeg * clusterScale
                + std::sin(wanderPhase) * params.scatter * 54.0f
                + std::sin(orbitPhase) * params.orbit * (65.0f + flight * 105.0f);
            float elevation = params.centerElevationDeg + base.elevationDeg
                + std::sin(wanderPhase * 0.73f + 0.8f) * params.roam * (8.0f + flight * 28.0f)
                + params.lift * (regime == 2u ? 28.0f : flight * 42.0f);
            float distance = params.centerDistance * (0.74f + radial)
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
        const float temperatureScale = std::exp2(
            (params.temperature - 0.5f) * 1.35f);
        const float sizeScale = std::exp2(
            (0.5f - params.bodySize) * 1.4f);
        const float maximumPitch = static_cast<float>(sampleRate_) * 0.40f;
        envelopeReleaseCoefficient_ = coefficientForRate(
            55.0f + params.rest * 80.0f, sampleRate_);
        for (uint32_t index = 0u; index < voices; ++index) {
            auto& voice = voices_[index];
            if (!voiceHealthy(voice)) initializeVoice(index);
            const uint32_t regime = voiceRegime(params.regime, index);
            voice.lane = static_cast<float>(index)
                / static_cast<float>(std::max<uint32_t>(1u, voices - 1u));
            const float rateVariation = std::exp2(
                voice.signatureA * params.variation * 1.8f
                + (voice.lane - 0.5f) * params.variation * 0.7f);
            voice.clockScale = temperatureScale * rateVariation;
            voice.activeWeight = clamp(
                (params.activity - voice.activityThreshold * 0.72f) * 4.0f
                    + 0.15f,
                0.0f, 1.0f);

            const float pitchVariation = std::exp2(
                voice.signatureB * params.variation * 1.4f
                + (voice.lane - 0.5f) * params.scatter * 0.30f);
            float pitch = params.bodyPitchHz * pitchVariation;
            pitch = regime == 3u
                ? clamp(pitch, 90.0f, 1600.0f)
                : clamp(pitch, 180.0f, maximumPitch);
            const float driftScale = std::exp2(voice.pitchDrift);
            pitch = clamp(pitch * sizeScale * driftScale, 70.0f, maximumPitch);
            if (regime == 3u) {
                const float sharedPitch = clamp(
                    params.bodyPitchHz * sizeScale * driftScale,
                    70.0f, 1600.0f);
                const float convergence = params.coupling * 0.38f;
                pitch = std::exp2(lerp(
                    std::log2(std::max(1.0f, pitch)),
                    std::log2(std::max(1.0f, sharedPitch)),
                    convergence));
            }
            voice.basePitchHz = pitch;
        }
    }

    AmbiInsectVoiceOutput processVoice(uint32_t index)
    {
        const auto& params = smoothParams_;
        auto& voice = voices_[index];
        const uint32_t regime = voiceRegime(params.regime, index);
        const float lane = voice.lane;
        const float randomA = voice.signatureA;
        const float randomB = voice.signatureB;
        const float clockScale = voice.clockScale;
        const float dt = 1.0f / static_cast<float>(sampleRate_);

        const float previousPhrasePhase = voice.phrasePhase;
        voice.phrasePhase = wrapUnit(voice.phrasePhase
            + params.phraseRateHz * clockScale * dt);
        voice.phrasePhase = wrapUnit(voice.phrasePhase
            + phaseDelta(globalPhrasePhase_, voice.phrasePhase) * params.coupling * (0.00018f + params.activity * 0.00032f));
        if (voice.phrasePhase < previousPhrasePhase) {
            ++voice.phraseGeneration;
            const uint32_t eventSeed = hash(
                voice.identity ^ (voice.phraseGeneration * 0x9e3779b9u));
            const float callChoice = hash01(eventSeed + 17u);
            const float callProbability = clamp(
                0.34f + params.activity * 0.78f - params.rest * 0.18f,
                0.08f, 0.98f);
            voice.phraseAccentTarget = callChoice < callProbability
                ? 0.68f + hash01(eventSeed + 41u) * 0.42f
                : 0.0f;
            voice.pitchDriftTarget = hashSigned(eventSeed + 73u)
                * params.variation * 0.085f;
        }
        const float phraseAccentRate = voice.phraseAccentTarget > voice.phraseAccent
            ? phraseAccentAttackCoefficient_ : phraseAccentReleaseCoefficient_;
        voice.phraseAccent += (voice.phraseAccentTarget - voice.phraseAccent)
            * phraseAccentRate;
        voice.pitchDrift += (voice.pitchDriftTarget - voice.pitchDrift)
            * pitchDriftCoefficient_;
        voice.chirpPhase = wrapUnit(
            voice.chirpPhase + params.chirpRateHz * clockScale * dt);
        const float previousPulsePhase = voice.pulsePhase;
        voice.pulsePhase = wrapUnit(voice.pulsePhase
            + params.pulseRateHz * clockScale
                * voice.pulseRateScale * dt);
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
            voice.pulseRateScale = 1.0f + hashSigned(eventSeed + 67u)
                * params.variation * (regime == 3u ? 0.08f : 0.035f);
        }

        const float activeWeight = voice.activeWeight;
        const float phraseDuty = clamp(0.94f - params.rest * 0.82f, 0.08f, 0.94f);
        const float phraseGate = periodicGate(voice.phrasePhase, phraseDuty, 0.035f + params.rest * 0.055f);
        float chirpDuty = 0.08f + params.callLength * 0.80f;
        if (regime == 1u || regime == 2u) chirpDuty = 0.42f + params.callLength * 0.52f;
        if (regime == 3u) chirpDuty = 0.70f + params.callLength * 0.26f;
        const float chirpGate = periodicGate(voice.chirpPhase, chirpDuty, 0.018f + params.callLength * 0.035f);
        float targetEnvelope = activeWeight * voice.phraseAccent * phraseGate * chirpGate;
        if (regime == 3u) {
            targetEnvelope = activeWeight * (0.18f + voice.phraseAccent * 0.82f)
                * (0.22f + phraseGate * 0.78f) * (0.58f + chirpGate * 0.42f);
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
            ribs *= voice.pulseAccent;
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
            wingLayer = aerodynamicWing * flightConvergence_
                + wakeBody * (0.22f + params.bodySize * 0.36f)
                + wingAir * (0.22f + params.brightness * 0.24f);
            source = thorax * (0.36f + params.bodySize * 0.34f)
                + wingLayer * (0.34f + params.wing * 0.54f)
                + wakeExcitation * params.rasp * (0.045f + params.bodySize * 0.065f);
        } else {
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
        static constexpr std::array<float, 5> regimeCompensation { 4.5f, 3.0f, 4.5f, 1.0f, 6.0f };
        const float regimeGain = regimeCompensation[std::min<uint32_t>(regime, 4u)];
        const float mixed = (source * tonalLevel + raspBand * (0.08f + params.rasp * 0.24f)
            + airBand * (0.04f + params.air * 0.20f)
            + substrate * (0.035f + params.bodySize * 0.075f))
            * voice.callEnvelope * (0.62f + bodyLevel);
        const float direct = std::tanh(clamp(mixed * regimeGain * (1.0f + params.brightness * 0.38f), -3.6f, 3.6f));
        const float fieldSend = std::tanh(clamp(
            (source * 0.32f + raspBand * 0.22f + airBand * 0.34f
                + substrate * 0.58f)
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
    std::array<AmbiInsectPoint, kAmbiInsectMaxVoices> points_ {};
    std::array<AmbiInsectPoint, kAmbiInsectMaxVoices> targetPoints_ {};
    double sampleRate_ = 48000.0;
    float fieldPhase_ = 0.0f;
    float globalPhrasePhase_ = 0.0f;
    float transitionFade_ = 1.0f;
    float smoothedOutputGain_ = dbToGain(-6.0f);
    float flightConvergence_ = 1.0f;
    float phraseAccentAttackCoefficient_ = 0.000375f;
    float phraseAccentReleaseCoefficient_ = 0.000094f;
    float pitchDriftCoefficient_ = 0.000015f;
    float envelopeAttackCoefficient_ = 0.008713f;
    float envelopeReleaseCoefficient_ = 0.001145f;
    float callVizCoefficient_ = 0.000458f;
    float wingPairCoefficient_ = 0.000146f;
    bool smoothReady_ = false;
    std::array<float, kAmbiInsectMaxChannels> lastOutput_ {};
    std::array<float, kAmbiInsectMaxChannels> transitionTail_ {};
    AmbiEnvironmentField environmentField_ {};
    std::atomic<bool> transitionRequested_ { false };
};

} // namespace s3g
