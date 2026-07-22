#pragma once

#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAmbiWaterMaxVoices = 64;
constexpr uint32_t kAmbiWaterMaxOrder = 7;
constexpr uint32_t kAmbiWaterMaxChannels = 64;
constexpr uint32_t kAmbiWaterRegimeCount = 8;
constexpr uint32_t kAmbiWaterEnvironmentCount = 9;
constexpr uint32_t kAmbiWaterEventSlots = 2;
constexpr uint32_t kAmbiWaterMicroSlots = 3;

struct AmbiWaterParams {
    uint32_t order = 3;
    uint32_t voices = 28;
    float water = 0.58f;
    float flow = 0.48f;
    float scale = 0.46f;
    float turbulence = 0.38f;
    float aeration = 0.30f;
    float spread = 0.58f;
    float deviation = 0.14f;
    uint32_t regime = 0;
    uint32_t environment = 0;
    float drops = 0.26f;
    float splash = 0.34f;
    float bubbles = 0.18f;
    float density = 0.38f;
    float eventSize = 0.42f;
    float eventDecay = 0.38f;
    float depth = 0.48f;
    float brightness = 0.44f;
    float resonance = 0.30f;
    float damping = 0.52f;
    float contact = 0.30f;
    float motionRateHz = 0.12f;
    float current = 0.54f;
    float slope = 0.18f;
    float eddy = 0.32f;
    float convergence = 0.18f;
    float width = 0.68f;
    float centerAzimuthDeg = 0.0f;
    float centerElevationDeg = 0.0f;
    float centerDistance = 1.0f;
    float spatialFollow = 0.72f;
    float outputGainDb = -6.0f;
};

struct AmbiWaterPoint {
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float distance = 1.0f;
};

struct AmbiWaterSvf {
    float ic1eq = 0.0f;
    float ic2eq = 0.0f;

    void reset()
    {
        ic1eq = 0.0f;
        ic2eq = 0.0f;
    }

    bool healthy() const
    {
        return std::isfinite(ic1eq) && std::isfinite(ic2eq)
            && std::fabs(ic1eq) < 64.0f && std::fabs(ic2eq) < 64.0f;
    }

    float process(float input, float cutoffHz, float resonance, float sampleRate)
    {
        if (!std::isfinite(input) || !std::isfinite(cutoffHz)
            || !std::isfinite(resonance) || !healthy()) {
            reset();
            return 0.0f;
        }
        const float sr = std::max(1.0f, sampleRate);
        const float hz = clamp(cutoffHz, 8.0f, sr * 0.45f);
        const float g = std::tan(kPi * hz / sr);
        const float k = 2.0f - clamp(resonance, 0.0f, 1.0f) * 1.88f;
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;
        const float v3 = input - ic2eq;
        const float band = a1 * ic1eq + a2 * v3;
        const float low = ic2eq + a2 * ic1eq + a3 * v3;
        ic1eq = flushDenormal(2.0f * band - ic1eq);
        ic2eq = flushDenormal(2.0f * low - ic2eq);
        if (!healthy() || !std::isfinite(band)) {
            reset();
            return 0.0f;
        }
        return band;
    }
};

struct AmbiWaterDropEvent {
    float age = -1.0f;
    float duration = 0.12f;
    float impactDuration = 0.006f;
    float forceDuration = 0.0005f;
    float radiusMeters = 0.0015f;
    float cavityRadiusMeters = 0.0008f;
    float terminalVelocity = 4.0f;
    float sourceHorizontalMeters = 1.0f;
    float listenerHeightMeters = 1.2f;
    float surfaceDepthMeters = 0.0040f;
    float rigidDistanceMeters = 0.020f;
    float rigidBlend = 0.0f;
    float liquidSurface = 0.0f;
    float surfaceHardness = 0.3f;
    float cavityDelay = 0.003f;
    float cavityGain = 0.0f;
    float amplitude = 0.5f;
    float impactPolarity = 1.0f;
    float diffusion = 0.15f;
    float displacement = 0.0f;
    float velocity = 0.0f;
    float radiationState1 = 0.0f;
    float radiationState2 = 0.0f;
    float tailDecay = 0.025f;
    float chirpDepthOctaves = 0.75f;
    float chirpDuration = 0.080f;
    float chirpDirection = -1.0f;
    float lastFrequency = 900.0f;

    bool healthy() const
    {
        return std::isfinite(age) && std::isfinite(duration)
            && std::isfinite(impactDuration) && std::isfinite(forceDuration)
            && std::isfinite(radiusMeters) && std::isfinite(cavityRadiusMeters)
            && std::isfinite(terminalVelocity) && std::isfinite(sourceHorizontalMeters)
            && std::isfinite(listenerHeightMeters) && std::isfinite(surfaceDepthMeters)
            && std::isfinite(rigidDistanceMeters) && std::isfinite(rigidBlend)
            && std::isfinite(liquidSurface) && std::isfinite(surfaceHardness)
            && std::isfinite(cavityDelay) && std::isfinite(cavityGain)
            && std::isfinite(amplitude) && std::isfinite(impactPolarity)
            && std::isfinite(diffusion)
            && std::isfinite(displacement) && std::isfinite(velocity)
            && std::isfinite(radiationState1) && std::isfinite(radiationState2)
            && std::isfinite(tailDecay) && std::isfinite(chirpDepthOctaves)
            && std::isfinite(chirpDuration) && std::isfinite(chirpDirection)
            && std::isfinite(lastFrequency);
    }
};

struct AmbiWaterBubbleEvent {
    float age = -1.0f;
    float lifetime = 0.35f;
    float forceDuration = 0.0005f;
    float secondaryForceAge = -1.0f;
    float secondaryForceSign = 0.0f;
    float radiusMeters = 0.0020f;
    float surfaceDepthMeters = 0.012f;
    float rigidDistanceMeters = 0.025f;
    float rigidBlend = 0.0f;
    float riseVelocity = 0.12f;
    float amplitude = 0.5f;
    float displacement = 0.0f;
    float velocity = 0.0f;
    float topologyTime = -1.0f;
    float topologyScale = 1.0f;
    float popAge = -1.0f;
    float popDuration = 0.010f;
    float popPhase = 0.0f;
    float popGain = 0.01f;
    float lastFrequency = 320.0f;

    bool healthy() const
    {
        return std::isfinite(age) && std::isfinite(lifetime)
            && std::isfinite(forceDuration) && std::isfinite(secondaryForceAge)
            && std::isfinite(secondaryForceSign) && std::isfinite(radiusMeters)
            && std::isfinite(surfaceDepthMeters) && std::isfinite(rigidDistanceMeters)
            && std::isfinite(rigidBlend) && std::isfinite(riseVelocity)
            && std::isfinite(amplitude) && std::isfinite(displacement)
            && std::isfinite(velocity) && std::isfinite(topologyTime)
            && std::isfinite(topologyScale) && std::isfinite(popAge)
            && std::isfinite(popDuration) && std::isfinite(popPhase)
            && std::isfinite(popGain) && std::isfinite(lastFrequency);
    }
};

struct AmbiWaterMicroBubble {
    float age = -1.0f;
    float delay = 0.0f;
    float duration = 0.020f;
    float forceDuration = 0.0002f;
    float radiusMeters = 0.00035f;
    float amplitude = 0.05f;
    float displacement = 0.0f;
    float velocity = 0.0f;

    bool healthy() const
    {
        return std::isfinite(age) && std::isfinite(delay)
            && std::isfinite(duration) && std::isfinite(forceDuration)
            && std::isfinite(radiusMeters) && std::isfinite(amplitude)
            && std::isfinite(displacement) && std::isfinite(velocity);
    }
};

struct AmbiWaterVoice {
    uint32_t identity = 1u;
    uint32_t rng = 1u;
    float travel = 0.0f;
    float driftPhase = 0.0f;
    float motionEnergy = 0.0f;
    float impactPending = 0.0f;
    float dropTimer = 0.0f;
    float bubbleTimer = 0.0f;
    float splashAge = -1.0f;
    float splashStrength = 0.0f;
    float slowNoise = 0.0f;
    float midNoise = 0.0f;
    float fastNoise = 0.0f;
    float airNoise = 0.0f;
    float energy = 0.0f;
    float eventViz = 0.0f;
    std::array<AmbiWaterDropEvent, kAmbiWaterEventSlots> dropEvents {};
    std::array<AmbiWaterBubbleEvent, kAmbiWaterEventSlots> bubbleEvents {};
    std::array<AmbiWaterMicroBubble, kAmbiWaterMicroSlots> microBubbles {};
    AmbiWaterSvf bodyFilter {};
    AmbiWaterSvf surfaceFilter {};
    AmbiWaterSvf contactFilter {};
    AmbiWaterSvf transferFilter {};
};

class AmbiWaterEncoder {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        reset();
        setParams(params_);
    }

    void reset()
    {
        transitionRequested_.store(false, std::memory_order_relaxed);
        transitionFade_ = 1.0f;
        lastOutput_.fill(0.0f);
        transitionTail_.fill(0.0f);
        smoothParams_ = params_;
        smoothReady_ = true;
        setOneHot(regimeWeights_, params_.regime);
        setOneHot(environmentWeights_, params_.environment);
        smoothedSceneGain_ = predictiveSceneGain(params_);
        smoothedOutputGain_ = normalizedOutputGain(params_) * smoothedSceneGain_;
        for (uint32_t i = 0u; i < kAmbiWaterMaxVoices; ++i) {
            voices_[i] = {};
            voices_[i].identity = 0x72e4a91du + i * 0x9e3779b9u;
            initializeVoice(i);
            points_[i] = basePoint(i, std::max<uint32_t>(1u, params_.voices));
            targetPoints_[i] = points_[i];
        }
    }

    void setParams(AmbiWaterParams params)
    {
        params.order = std::clamp<uint32_t>(params.order, 1u, kAmbiWaterMaxOrder);
        params.voices = std::clamp<uint32_t>(params.voices, 1u, kAmbiWaterMaxVoices);
        params.water = clampFinite(params.water, params_.water, 0.0f, 1.0f);
        params.flow = clampFinite(params.flow, params_.flow, 0.0f, 1.0f);
        params.scale = clampFinite(params.scale, params_.scale, 0.0f, 1.0f);
        params.turbulence = clampFinite(params.turbulence, params_.turbulence, 0.0f, 1.0f);
        params.aeration = clampFinite(params.aeration, params_.aeration, 0.0f, 1.0f);
        params.spread = clampFinite(params.spread, params_.spread, 0.0f, 1.0f);
        params.deviation = clampFinite(params.deviation, params_.deviation, 0.0f, 1.0f);
        params.regime = std::clamp<uint32_t>(params.regime, 0u, kAmbiWaterRegimeCount - 1u);
        params.environment = std::clamp<uint32_t>(params.environment, 0u, kAmbiWaterEnvironmentCount - 1u);
        params.drops = clampFinite(params.drops, params_.drops, 0.0f, 1.0f);
        params.splash = clampFinite(params.splash, params_.splash, 0.0f, 1.0f);
        params.bubbles = clampFinite(params.bubbles, params_.bubbles, 0.0f, 1.0f);
        params.density = clampFinite(params.density, params_.density, 0.0f, 1.0f);
        params.eventSize = clampFinite(params.eventSize, params_.eventSize, 0.0f, 1.0f);
        params.eventDecay = clampFinite(params.eventDecay, params_.eventDecay, 0.0f, 1.0f);
        params.depth = clampFinite(params.depth, params_.depth, 0.0f, 1.0f);
        params.brightness = clampFinite(params.brightness, params_.brightness, 0.0f, 1.0f);
        params.resonance = clampFinite(params.resonance, params_.resonance, 0.0f, 1.0f);
        params.damping = clampFinite(params.damping, params_.damping, 0.0f, 1.0f);
        params.contact = clampFinite(params.contact, params_.contact, 0.0f, 1.0f);
        params.motionRateHz = clampFinite(params.motionRateHz, params_.motionRateHz, 0.002f, 3.0f);
        params.current = clampFinite(params.current, params_.current, 0.0f, 1.0f);
        params.slope = clampFinite(params.slope, params_.slope, -1.0f, 1.0f);
        params.eddy = clampFinite(params.eddy, params_.eddy, 0.0f, 1.0f);
        params.convergence = clampFinite(params.convergence, params_.convergence, 0.0f, 1.0f);
        params.width = clampFinite(params.width, params_.width, 0.0f, 1.0f);
        params.centerAzimuthDeg = wrapSignedDeg(params.centerAzimuthDeg);
        params.centerElevationDeg = clampFinite(params.centerElevationDeg, params_.centerElevationDeg, -90.0f, 90.0f);
        params.centerDistance = clampFinite(params.centerDistance, params_.centerDistance, 0.15f, 2.0f);
        params.spatialFollow = clampFinite(params.spatialFollow, params_.spatialFollow, 0.0f, 1.0f);
        params.outputGainDb = clampFinite(params.outputGainDb, params_.outputGainDb, -60.0f, 12.0f);

        const uint32_t oldVoices = params_.voices;
        params_ = params;
        if (params.voices > oldVoices) {
            for (uint32_t i = oldVoices; i < params.voices; ++i) {
                initializeVoice(i);
                points_[i] = basePoint(i, params.voices);
                targetPoints_[i] = points_[i];
            }
        }
    }

    AmbiWaterParams params() const { return params_; }
    void beginTransition() { transitionRequested_.store(true, std::memory_order_release); }
    float voiceEnergy(uint32_t voice) const { return voices_[std::min<uint32_t>(voice, kAmbiWaterMaxVoices - 1u)].energy; }
    float voiceEventLevel(uint32_t voice) const { return voices_[std::min<uint32_t>(voice, kAmbiWaterMaxVoices - 1u)].eventViz; }
    AmbiWaterPoint voicePoint(uint32_t voice) const { return points_[std::min<uint32_t>(voice, kAmbiWaterMaxVoices - 1u)]; }
    float sceneCompensationDb() const
    {
        return 20.0f * std::log10(std::max(0.000001f, smoothedSceneGain_));
    }

    void process(float* const* outputs, uint32_t outputChannels, uint32_t frames)
    {
        if (!outputs || frames == 0u) return;
        if (transitionRequested_.exchange(false, std::memory_order_acq_rel)) startTransition();
        outputChannels = std::min<uint32_t>(outputChannels, kAmbiWaterMaxChannels);
        for (uint32_t ch = 0u; ch < outputChannels; ++ch) {
            if (outputs[ch]) std::fill(outputs[ch], outputs[ch] + frames, 0.0f);
        }

        const uint32_t voices = params_.voices;
        const uint32_t ambiChannels = std::min<uint32_t>(ambiChannelsForOrder(params_.order), outputChannels);
        const float voiceNorm = std::pow(static_cast<float>(std::max<uint32_t>(1u, voices)), 0.46f);
        constexpr uint32_t kControlFrames = 16u;

        for (uint32_t chunkStart = 0u; chunkStart < frames; chunkStart += kControlFrames) {
            const uint32_t chunkFrames = std::min<uint32_t>(kControlFrames, frames - chunkStart);
            const float dt = static_cast<float>(chunkFrames) / static_cast<float>(sampleRate_);
            updateSmoothedParams(dt);
            updateMotion(dt);
            const float targetSceneGain = predictiveSceneGain(smoothParams_);
            const float sceneTau = targetSceneGain < smoothedSceneGain_ ? 0.28f : 0.72f;
            const float sceneCoeff = 1.0f - std::exp(-dt / sceneTau);
            smoothedSceneGain_ += (targetSceneGain - smoothedSceneGain_) * sceneCoeff;
            const float targetGain = dbToGain(smoothParams_.outputGainDb) * 1.12f
                * smoothedSceneGain_ / voiceNorm;

            std::array<std::array<float, kAmbiWaterMaxChannels>, kAmbiWaterMaxVoices> basis {};
            std::array<float, kAmbiWaterMaxVoices> distanceGain {};
            for (uint32_t voice = 0u; voice < voices; ++voice) {
                basis[voice] = acnSn3dBasis7(directionFromAed(points_[voice].azimuthDeg, points_[voice].elevationDeg));
                distanceGain[voice] = 1.0f / std::max(0.48f, points_[voice].distance);
            }

            for (uint32_t frame = chunkStart; frame < chunkStart + chunkFrames; ++frame) {
                smoothedOutputGain_ += (targetGain - smoothedOutputGain_) * 0.0015f;
                for (uint32_t voice = 0u; voice < voices; ++voice) {
                    float sample = processVoice(voice) * smoothedOutputGain_ * distanceGain[voice];
                    if (!std::isfinite(sample)) {
                        initializeVoice(voice);
                        sample = 0.0f;
                    }
                    auto& state = voices_[voice];
                    state.energy += (sample * sample - state.energy) * 0.0012f;
                    if (std::fabs(sample) < 0.0000001f) continue;
                    for (uint32_t ch = 0u; ch < ambiChannels; ++ch) {
                        if (outputs[ch]) outputs[ch][frame] = flushDenormal(outputs[ch][frame] + sample * basis[voice][ch]);
                    }
                }
            }
        }

        const float transitionStep = 1.0f / std::max(1.0f, static_cast<float>(sampleRate_) * 0.030f);
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            const float mix = transitionFade_;
            for (uint32_t ch = 0u; ch < outputChannels; ++ch) {
                const float sum = (ch < ambiChannels && outputs[ch]) ? outputs[ch][frame] : 0.0f;
                float fresh = std::tanh(clamp(sum * 1.08f, -4.0f, 4.0f));
                if (!std::isfinite(fresh)) fresh = 0.0f;
                const float value = transitionTail_[ch] * (1.0f - mix) + fresh * mix;
                if (outputs[ch]) outputs[ch][frame] = value;
                lastOutput_[ch] = value;
            }
            transitionFade_ = std::min(1.0f, transitionFade_ + transitionStep);
        }
    }

private:
    static uint32_t hash(uint32_t x)
    {
        x ^= x >> 16u;
        x *= 0x7feb352du;
        x ^= x >> 15u;
        x *= 0x846ca68bu;
        x ^= x >> 16u;
        return x;
    }

    static float hash01(uint32_t x) { return static_cast<float>(hash(x) & 0xffffu) / 65535.0f; }
    static float hashSigned(uint32_t x) { return hash01(x) * 2.0f - 1.0f; }

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

    static float smoothToward(float current, float target, float coeff)
    {
        if (!std::isfinite(target)) return std::isfinite(current) ? current : 0.0f;
        if (!std::isfinite(current)) return target;
        return current + (target - current) * coeff;
    }

    static float smoothAngle(float current, float target, float coeff)
    {
        return wrapSignedDeg(current + wrapSignedDeg(target - current) * coeff);
    }

    static float freqFromNorm(float value, float lowHz, float highHz)
    {
        return lowHz * std::pow(highHz / lowHz, clamp(value, 0.0f, 1.0f));
    }

    static float normalizedOutputGain(const AmbiWaterParams& params)
    {
        const float voiceNorm = std::pow(static_cast<float>(std::max<uint32_t>(1u, params.voices)), 0.46f);
        return dbToGain(params.outputGainDb) * 1.12f / voiceNorm;
    }

    float predictiveSceneGain(const AmbiWaterParams& p) const
    {
        const float currentMode = regimeWeights_[0];
        const float rainMode = regimeWeights_[1];
        const float cascadeMode = regimeWeights_[2];
        const float surgeMode = regimeWeights_[3];
        const float vortexMode = regimeWeights_[4];
        const float sloshMode = regimeWeights_[5];
        const float dripMode = regimeWeights_[6];
        const float plumeMode = regimeWeights_[7];

        const float bodyMode = currentMode * 0.96f + rainMode * 0.62f
            + cascadeMode * 1.16f + surgeMode * 1.10f + vortexMode * 0.86f
            + sloshMode * 0.84f + dripMode * 0.46f + plumeMode * 0.58f;
        const float sprayMode = currentMode * 0.28f + rainMode * 1.20f
            + cascadeMode * 1.36f + surgeMode * 0.76f + vortexMode * 0.28f
            + sloshMode * 0.24f + dripMode * 0.20f + plumeMode * 0.08f;
        const float dropMode = currentMode * 0.42f + rainMode * 3.22f
            + cascadeMode * 1.14f + surgeMode * 0.78f + vortexMode * 0.35f
            + sloshMode * 0.52f + dripMode * 1.12f + plumeMode * 0.08f;
        const float bubbleMode = currentMode * 0.45f + rainMode * 0.30f
            + cascadeMode * (0.45f + p.aeration * 1.20f) + surgeMode * 0.72f
            + vortexMode * 2.85f + sloshMode * 1.25f + dripMode * 0.18f
            + plumeMode * 3.20f;
        const float splashMode = currentMode * 0.42f + rainMode * 0.62f
            + cascadeMode * 1.35f + surgeMode * 1.18f + vortexMode * 0.58f
            + sloshMode * 1.08f + dripMode * 0.48f + plumeMode * 0.32f;

        const float size = naturalEventMacro(p.eventSize);
        const float life = naturalEventMacro(p.eventDecay);
        const float dropRate = p.drops * (0.025f + p.density * p.density * 2.8f) * dropMode;
        const float bubbleRate = p.bubbles * (0.018f + p.density * p.density * 1.35f)
            * bubbleMode;
        const float body = p.water * (0.16f + p.flow * 0.68f + p.scale * 0.28f)
            * bodyMode;
        const float spray = p.aeration * (0.04f + p.turbulence * 0.44f) * sprayMode;
        const float dropStrength = 0.02f + 0.98f * std::pow(size, 2.2f);
        const float drops = p.drops * std::sqrt(std::max(0.0f, dropRate)) * dropStrength
            * (0.55f + p.contact * 0.45f);
        const float splash = p.splash * (0.12f + p.density * 0.88f)
            * (0.25f + p.aeration * 0.75f) * splashMode;
        const float bubbleStrength = (0.12f + std::pow(size, 0.80f) * 0.88f)
            * (0.30f + life * 0.70f);
        const float bubbles = p.bubbles * std::sqrt(std::max(0.0f, bubbleRate))
            * bubbleStrength;

        const float familyEnergy = std::sqrt(
            std::pow(body * 0.16f, 2.0f)
            + std::pow(spray * 0.55f, 2.0f)
            + std::pow(drops * 0.22f, 2.0f)
            + std::pow(splash * 0.50f, 2.0f)
            + std::pow(bubbles * 0.04f, 2.0f));
        const float outputScale = 0.18f + p.water * 0.78f + p.density * 0.08f;
        const float predicted = familyEnergy * outputScale;
        const float sourceActivity = p.water + p.aeration + p.drops + p.splash + p.bubbles;
        if (!std::isfinite(predicted) || sourceActivity < 0.002f) return 1.0f;

        constexpr float targetActivity = 0.055f;
        const float theoreticalDb = 20.0f
            * std::log10(targetActivity / std::max(0.0015f, predicted));
        return dbToGain(clamp(theoreticalDb * 0.85f, -10.0f, 15.0f));
    }

    template <size_t N>
    static void setOneHot(std::array<float, N>& weights, uint32_t selected)
    {
        weights.fill(0.0f);
        weights[std::min<uint32_t>(selected, static_cast<uint32_t>(N - 1u))] = 1.0f;
    }

    static AmbiWaterPoint basePoint(uint32_t voice, uint32_t voices)
    {
        const float lane = static_cast<float>(voice) / static_cast<float>(std::max<uint32_t>(1u, voices - 1u));
        return { (lane - 0.5f) * 150.0f, std::sin(lane * kPi * 2.0f) * 12.0f, 0.84f + lane * 0.30f };
    }

    static Vec3 pointVector(float azimuthDeg, float elevationDeg, float distance)
    {
        const Vec3 direction = directionFromAed(wrapSignedDeg(azimuthDeg), clamp(elevationDeg, -89.0f, 89.0f));
        return { direction.x * distance, direction.y * distance, direction.z * distance };
    }

    static AmbiWaterPoint pointFromVector(Vec3 value)
    {
        const float distance = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
        if (!std::isfinite(distance) || distance < 0.000001f) return {};
        return {
            wrapSignedDeg(std::atan2(value.y, value.x) * 180.0f / kPi),
            std::asin(clamp(value.z / distance, -1.0f, 1.0f)) * 180.0f / kPi,
            distance
        };
    }

    float randomSigned(AmbiWaterVoice& voice)
    {
        voice.rng = hash(voice.rng + 0x6d2b79f5u);
        return static_cast<float>(static_cast<int32_t>(voice.rng)) / 2147483648.0f;
    }

    float randomUnit(AmbiWaterVoice& voice)
    {
        return randomSigned(voice) * 0.5f + 0.5f;
    }

    float eventInterval(AmbiWaterVoice& voice, float rate)
    {
        if (rate <= 0.00001f) return 8.0f;
        return -std::log(std::max(0.0001f, 1.0f - randomUnit(voice))) / rate;
    }

    void initializeVoice(uint32_t index)
    {
        auto& voice = voices_[index];
        const uint32_t identity = voice.identity == 0u ? 0x72e4a91du + index * 0x9e3779b9u : voice.identity;
        voice = {};
        voice.identity = identity;
        voice.rng = hash(identity ^ 0xa53c9e17u);
        voice.travel = hash01(identity + 11u);
        voice.driftPhase = hash01(identity + 23u);
        voice.dropTimer = 0.02f + hash01(identity + 31u) * 0.58f;
        voice.bubbleTimer = 0.04f + hash01(identity + 47u) * 0.86f;
        voice.splashAge = -1.0f;
    }

    bool voiceHealthy(const AmbiWaterVoice& voice) const
    {
        const bool finite = std::isfinite(voice.travel) && std::isfinite(voice.driftPhase)
            && std::isfinite(voice.motionEnergy) && std::isfinite(voice.impactPending)
            && std::isfinite(voice.dropTimer) && std::isfinite(voice.bubbleTimer)
            && std::isfinite(voice.splashAge) && std::isfinite(voice.splashStrength)
            && std::isfinite(voice.slowNoise)
            && std::isfinite(voice.midNoise) && std::isfinite(voice.fastNoise)
            && std::isfinite(voice.airNoise) && std::isfinite(voice.energy)
            && std::isfinite(voice.eventViz);
        if (!finite || !voice.bodyFilter.healthy() || !voice.surfaceFilter.healthy()
            || !voice.contactFilter.healthy() || !voice.transferFilter.healthy()) {
            return false;
        }
        for (const auto& event : voice.dropEvents) {
            if (!event.healthy()) return false;
        }
        for (const auto& event : voice.bubbleEvents) {
            if (!event.healthy()) return false;
        }
        for (const auto& event : voice.microBubbles) {
            if (!event.healthy()) return false;
        }
        return true;
    }

    void startTransition()
    {
        transitionTail_ = lastOutput_;
        transitionFade_ = 0.0f;
        smoothParams_ = params_;
        setOneHot(regimeWeights_, params_.regime);
        setOneHot(environmentWeights_, params_.environment);
        smoothedSceneGain_ = predictiveSceneGain(params_);
        smoothedOutputGain_ = normalizedOutputGain(params_) * smoothedSceneGain_;
        for (uint32_t i = 0u; i < kAmbiWaterMaxVoices; ++i) initializeVoice(i);
    }

    void updateSmoothedParams(float dt)
    {
        if (!smoothReady_) {
            smoothParams_ = params_;
            setOneHot(regimeWeights_, params_.regime);
            setOneHot(environmentWeights_, params_.environment);
            smoothReady_ = true;
            return;
        }

        const float coeff = 1.0f - std::exp(-dt * 20.0f);
        smoothParams_.order = params_.order;
        smoothParams_.voices = params_.voices;
        smoothParams_.regime = params_.regime;
        smoothParams_.environment = params_.environment;
        smoothParams_.water = smoothToward(smoothParams_.water, params_.water, coeff);
        smoothParams_.flow = smoothToward(smoothParams_.flow, params_.flow, coeff);
        smoothParams_.scale = smoothToward(smoothParams_.scale, params_.scale, coeff);
        smoothParams_.turbulence = smoothToward(smoothParams_.turbulence, params_.turbulence, coeff);
        smoothParams_.aeration = smoothToward(smoothParams_.aeration, params_.aeration, coeff);
        smoothParams_.spread = smoothToward(smoothParams_.spread, params_.spread, coeff);
        smoothParams_.deviation = smoothToward(smoothParams_.deviation, params_.deviation, coeff);
        smoothParams_.drops = smoothToward(smoothParams_.drops, params_.drops, coeff);
        smoothParams_.splash = smoothToward(smoothParams_.splash, params_.splash, coeff);
        smoothParams_.bubbles = smoothToward(smoothParams_.bubbles, params_.bubbles, coeff);
        smoothParams_.density = smoothToward(smoothParams_.density, params_.density, coeff);
        smoothParams_.eventSize = smoothToward(smoothParams_.eventSize, params_.eventSize, coeff);
        smoothParams_.eventDecay = smoothToward(smoothParams_.eventDecay, params_.eventDecay, coeff);
        smoothParams_.depth = smoothToward(smoothParams_.depth, params_.depth, coeff);
        smoothParams_.brightness = smoothToward(smoothParams_.brightness, params_.brightness, coeff);
        smoothParams_.resonance = smoothToward(smoothParams_.resonance, params_.resonance, coeff);
        smoothParams_.damping = smoothToward(smoothParams_.damping, params_.damping, coeff);
        smoothParams_.contact = smoothToward(smoothParams_.contact, params_.contact, coeff);
        smoothParams_.motionRateHz = smoothToward(smoothParams_.motionRateHz, params_.motionRateHz, coeff);
        smoothParams_.current = smoothToward(smoothParams_.current, params_.current, coeff);
        smoothParams_.slope = smoothToward(smoothParams_.slope, params_.slope, coeff);
        smoothParams_.eddy = smoothToward(smoothParams_.eddy, params_.eddy, coeff);
        smoothParams_.convergence = smoothToward(smoothParams_.convergence, params_.convergence, coeff);
        smoothParams_.width = smoothToward(smoothParams_.width, params_.width, coeff);
        smoothParams_.centerAzimuthDeg = smoothAngle(smoothParams_.centerAzimuthDeg, params_.centerAzimuthDeg, coeff);
        smoothParams_.centerElevationDeg = smoothToward(smoothParams_.centerElevationDeg, params_.centerElevationDeg, coeff);
        smoothParams_.centerDistance = smoothToward(smoothParams_.centerDistance, params_.centerDistance, coeff);
        smoothParams_.spatialFollow = smoothToward(smoothParams_.spatialFollow, params_.spatialFollow, coeff);
        smoothParams_.outputGainDb = smoothToward(smoothParams_.outputGainDb, params_.outputGainDb, coeff);

        const float regimeCoeff = 1.0f - std::exp(-dt * 7.5f);
        for (uint32_t mode = 0u; mode < regimeWeights_.size(); ++mode) {
            regimeWeights_[mode] = smoothToward(regimeWeights_[mode], mode == params_.regime ? 1.0f : 0.0f, regimeCoeff);
        }
        const float environmentCoeff = 1.0f - std::exp(-dt * 11.0f);
        for (uint32_t mode = 0u; mode < environmentWeights_.size(); ++mode) {
            environmentWeights_[mode] = smoothToward(environmentWeights_[mode], mode == params_.environment ? 1.0f : 0.0f, environmentCoeff);
        }
    }

    void updateMotion(float dt)
    {
        const auto& p = smoothParams_;
        const uint32_t voices = p.voices;
        const float currentMode = regimeWeights_[0];
        const float rainMode = regimeWeights_[1];
        const float cascadeMode = regimeWeights_[2];
        const float surgeMode = regimeWeights_[3];
        const float vortexMode = regimeWeights_[4];
        const float sloshMode = regimeWeights_[5];
        const float dripMode = regimeWeights_[6];
        const float plumeMode = regimeWeights_[7];

        for (uint32_t i = 0u; i < voices; ++i) {
            auto& voice = voices_[i];
            const float lane = static_cast<float>(i) / static_cast<float>(std::max<uint32_t>(1u, voices - 1u));
            const float identityA = hash01(voice.identity + 101u);
            const float identityB = hash01(voice.identity + 211u);
            const float speed = p.motionRateHz * (0.20f + p.current * 1.80f)
                * (0.72f + identityA * 0.56f);
            const float previousTravel = voice.travel;
            voice.travel += speed * dt;
            voice.travel -= std::floor(voice.travel);
            const bool wrapped = voice.travel < previousTravel;
            voice.driftPhase += dt * p.motionRateHz * (0.18f + p.eddy * 1.7f) * (0.8f + identityB * 0.5f);
            voice.driftPhase -= std::floor(voice.driftPhase);

            const float phase = voice.travel * kPi * 2.0f;
            const float driftPhase = voice.driftPhase * kPi * 2.0f;
            const float laneSigned = lane * 2.0f - 1.0f;
            const float meander = std::sin(phase * (1.0f + p.turbulence * 1.8f) + identityA * kPi * 2.0f);
            const float eddy = std::sin(driftPhase + lane * kPi * 4.0f);
            const float widthDeg = 12.0f + p.width * 154.0f;
            const float centerAz = p.centerAzimuthDeg;
            const float centerEl = p.centerElevationDeg;
            const float centerDistance = p.centerDistance;

            const Vec3 currentPoint = pointVector(
                centerAz + (voice.travel - 0.5f) * widthDeg + meander * p.eddy * 34.0f,
                centerEl + p.slope * (voice.travel - 0.5f) * 72.0f + eddy * p.eddy * 13.0f,
                centerDistance * (0.72f + voice.travel * 0.56f));
            const Vec3 rainPoint = pointVector(
                centerAz + laneSigned * widthDeg * 0.72f + eddy * p.eddy * 18.0f,
                centerEl + 72.0f - voice.travel * 146.0f,
                centerDistance * (0.88f + identityA * 0.34f));
            const Vec3 cascadePoint = pointVector(
                centerAz + laneSigned * widthDeg * 0.50f + meander * p.eddy * 22.0f,
                centerEl + 78.0f - voice.travel * 158.0f,
                centerDistance * (0.76f + voice.travel * 0.54f + p.aeration * 0.16f));
            const float surgeWave = std::sin(phase);
            const Vec3 surgePoint = pointVector(
                centerAz + laneSigned * widthDeg * 0.84f + eddy * p.eddy * 12.0f,
                centerEl + surgeWave * (4.0f + p.turbulence * 10.0f) - p.slope * 8.0f,
                centerDistance * (1.08f - surgeWave * (0.16f + p.current * 0.28f)));
            const float vortexRadius = 1.0f - voice.travel * (0.42f + p.convergence * 0.50f);
            const Vec3 vortexPoint = pointVector(
                centerAz + voice.travel * (300.0f + p.convergence * 840.0f) + laneSigned * 55.0f,
                centerEl - voice.travel * p.convergence * 68.0f + eddy * p.eddy * 8.0f,
                centerDistance * (0.42f + vortexRadius * 0.74f));
            const float sloshWave = std::sin(phase + identityB * kPi * 0.5f);
            const Vec3 sloshPoint = pointVector(
                centerAz + sloshWave * widthDeg * 0.48f + laneSigned * widthDeg * 0.22f,
                centerEl + std::sin(phase * 0.5f + identityB * kPi * 2.0f)
                    * (3.0f + p.eddy * 15.0f),
                centerDistance * (0.90f - sloshWave * (0.08f + p.current * 0.22f)));
            const Vec3 dripPoint = pointVector(
                centerAz + laneSigned * widthDeg * 0.62f + eddy * p.eddy * 8.0f,
                centerEl + 80.0f - voice.travel * 166.0f,
                centerDistance * (0.82f + identityA * 0.30f));
            const float plumeRadius = 0.18f + p.width * 0.40f + lane * 0.18f;
            const Vec3 plumePoint = pointVector(
                centerAz + voice.travel * (160.0f + p.eddy * 480.0f)
                    + laneSigned * widthDeg * 0.22f,
                centerEl - 68.0f + voice.travel * 136.0f + meander * p.turbulence * 8.0f,
                centerDistance * (0.62f + plumeRadius + std::sin(phase + identityA) * 0.08f));

            Vec3 mixed {
                currentPoint.x * currentMode + rainPoint.x * rainMode + cascadePoint.x * cascadeMode
                    + surgePoint.x * surgeMode + vortexPoint.x * vortexMode + sloshPoint.x * sloshMode
                    + dripPoint.x * dripMode + plumePoint.x * plumeMode,
                currentPoint.y * currentMode + rainPoint.y * rainMode + cascadePoint.y * cascadeMode
                    + surgePoint.y * surgeMode + vortexPoint.y * vortexMode + sloshPoint.y * sloshMode
                    + dripPoint.y * dripMode + plumePoint.y * plumeMode,
                currentPoint.z * currentMode + rainPoint.z * rainMode + cascadePoint.z * cascadeMode
                    + surgePoint.z * surgeMode + vortexPoint.z * vortexMode + sloshPoint.z * sloshMode
                    + dripPoint.z * dripMode + plumePoint.z * plumeMode
            };
            targetPoints_[i] = pointFromVector(mixed);
            targetPoints_[i].distance = clamp(targetPoints_[i].distance, 0.20f, 2.60f);

            const float azDelta = std::fabs(wrapSignedDeg(targetPoints_[i].azimuthDeg - points_[i].azimuthDeg)) / 180.0f;
            const float elDelta = std::fabs(targetPoints_[i].elevationDeg - points_[i].elevationDeg) / 90.0f;
            const float distanceDelta = std::fabs(targetPoints_[i].distance - points_[i].distance);
            const float targetEnergy = clamp((azDelta * 0.38f + elDelta * 0.42f + distanceDelta * 0.62f)
                    * (0.7f + p.current + p.eddy * 0.8f + p.convergence * 0.7f),
                0.0f, 1.0f);
            voice.motionEnergy += (targetEnergy - voice.motionEnergy) * (1.0f - std::exp(-dt * 16.0f));

            if (wrapped) {
                const float impact = rainMode * 0.66f + cascadeMode
                    + surgeMode * (0.28f + p.turbulence * 0.36f)
                    + currentMode * p.turbulence * 0.18f
                    + vortexMode * p.convergence * 0.24f
                    + sloshMode * (0.18f + p.current * 0.26f)
                    + dripMode + plumeMode * p.aeration * 0.12f;
                voice.impactPending = std::max(voice.impactPending, clamp(impact, 0.0f, 1.0f));
            }

            const float follow = 1.0f - std::exp(-dt * (0.55f + (1.0f - p.spatialFollow) * 10.0f));
            points_[i].azimuthDeg = wrapSignedDeg(points_[i].azimuthDeg
                + wrapSignedDeg(targetPoints_[i].azimuthDeg - points_[i].azimuthDeg) * follow);
            points_[i].elevationDeg += (targetPoints_[i].elevationDeg - points_[i].elevationDeg) * follow;
            points_[i].distance += (targetPoints_[i].distance - points_[i].distance) * follow;
        }
    }

    float eventEnvelope(float& age, float attackSeconds, float decaySeconds, float dt)
    {
        if (age < 0.0f) return 0.0f;
        age += dt;
        const float attack = 1.0f - std::exp(-age / std::max(0.0002f, attackSeconds));
        const float decay = std::exp(-age / std::max(0.001f, decaySeconds));
        if (age > decaySeconds * 9.0f + attackSeconds) age = -1.0f;
        return attack * decay;
    }

    static float parabolicPulse(float age, float duration)
    {
        const float u = age / std::max(0.0001f, duration);
        if (u <= 0.0f || u >= 1.0f) return 0.0f;
        const float parabola = 4.0f * u * (1.0f - u);
        return parabola * parabola;
    }

    static float smoothRange(float low, float high, float value)
    {
        const float x = clamp((value - low) / std::max(0.000001f, high - low), 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    }

    static float raindropTerminalVelocity(float radiusMeters)
    {
        const float diameterMm = clamp(radiusMeters * 2000.0f, 0.15f, 6.0f);
        return clamp(9.65f - 10.3f * std::exp(-0.60f * diameterMm), 0.45f, 9.35f);
    }

    // Normalized form of the finite circular impact source in Miklavcic et al., DAFx-04.
    static float diskImpactPressure(const AmbiWaterDropEvent& event)
    {
        const double u = static_cast<double>(event.age)
            / std::max(0.000001, static_cast<double>(event.impactDuration));
        if (u <= 0.0 || u >= 1.0) return 0.0f;

        const double radius = std::max(0.000001, static_cast<double>(event.radiusMeters));
        const double horizontal = std::max(radius * 1.01,
            static_cast<double>(event.sourceHorizontalMeters));
        const double height = std::max(0.01, static_cast<double>(event.listenerHeightMeters));
        const double nearRadius = std::max(0.000001, horizontal - radius);
        const double farRadius = horizontal + radius;
        const double nearPath = std::sqrt(nearRadius * nearRadius + height * height);
        const double farPath = std::sqrt(farRadius * farRadius + height * height);
        const double path = nearPath + (farPath - nearPath) * u;
        const double projected = std::sqrt(std::max(0.000000000001, path * path - height * height));
        const double denominator = std::max(0.000000000001, 2.0 * horizontal * projected);
        const double cosine = std::clamp((projected * projected + horizontal * horizontal
                - radius * radius) / denominator,
            -1.0, 1.0);
        const double peak = std::asin(std::clamp(radius / horizontal, 0.0, 0.999999));
        if (peak < 0.00000001) {
            const double centered = u * 2.0 - 1.0;
            return static_cast<float>(std::sqrt(std::max(0.0, 1.0 - centered * centered)));
        }
        return clamp(static_cast<float>(std::acos(cosine) / peak), 0.0f, 1.0f);
    }

    static float naturalEventMacro(float value)
    {
        const float x = clamp(value, 0.0f, 1.0f);
        return 0.10f + 0.80f * x * x * (3.0f - 2.0f * x);
    }

    // Compact real-time form of the Cornell radius oscillator, boundary proxy, and damping model.
    static constexpr float kWaterSoundSpeed = 1497.0f;
    static constexpr float kAirSoundSpeed = 343.0f;
    static constexpr float kWaterViscosity = 8.9e-4f;
    static constexpr float kWaterDensity = 998.0f;
    static constexpr float kThermalConstant = 1.6e6f;
    static constexpr float kGasGamma = 1.4f;
    static constexpr float kGravity = 9.8f;
    static constexpr float kSurfaceTension = 0.072f;
    static constexpr float kAtmosphericPressure = 101325.0f;

    static float bubbleCapacitance(float radiusMeters, float boundaryDistanceMeters, bool freeSurface)
    {
        const float radius = clamp(radiusMeters, 0.00005f, 0.020f);
        const float distance = std::max(radius * 1.01f, boundaryDistanceMeters);
        const float ratio = clamp(radius / (2.0f * distance), 0.0f, 0.495f);
        const float ratio2 = ratio * ratio;
        const float ratio4 = ratio2 * ratio2;
        const float denominator = freeSurface ? 1.0f - ratio - ratio4 : 1.0f + ratio - ratio4;
        return radius / std::max(0.20f, denominator);
    }

    static float bubbleFrequencyHz(float radiusMeters, float surfaceDepthMeters,
        float rigidDistanceMeters, float rigidBlend)
    {
        const float radius = clamp(radiusMeters, 0.00005f, 0.020f);
        const float freeCapacitance = bubbleCapacitance(radius, surfaceDepthMeters, true);
        const float rigidCapacitance = bubbleCapacitance(radius, rigidDistanceMeters, false);
        const float rigidRatio = rigidCapacitance / radius;
        const float capacitance = freeCapacitance
            * (1.0f + (rigidRatio - 1.0f) * clamp(rigidBlend, 0.0f, 1.0f));
        const float volume = (4.0f / 3.0f) * kPi * radius * radius * radius;
        const float pressure = kAtmosphericPressure + kWaterDensity * kGravity * surfaceDepthMeters
            + 2.0f * kSurfaceTension / radius;
        const float omegaSquared = 4.0f * kPi * kGasGamma * pressure * capacitance
            / std::max(1.0e-14f, kWaterDensity * volume);
        return std::sqrt(std::max(0.0f, omegaSquared)) / (2.0f * kPi);
    }

    static float bubbleDampingBeta(float radiusMeters, float frequencyHz, float dampingTrim)
    {
        const float radius = clamp(radiusMeters, 0.00005f, 0.020f);
        const float omega = std::max(1.0f, frequencyHz * kPi * 2.0f);
        const float radiation = omega * radius / kWaterSoundSpeed;
        const float viscosity = 4.0f * kWaterViscosity
            / (kWaterDensity * omega * radius * radius);
        const float gammaMinusOne = kGasGamma - 1.0f;
        const float phi = 16.0f * kThermalConstant * kGravity
            / (9.0f * gammaMinusOne * gammaMinusOne * omega);
        float thermal = 0.0f;
        if (phi > 4.001f) {
            thermal = 2.0f * (std::sqrt(std::max(0.0f, phi - 3.0f))
                    - (3.0f * kGasGamma - 1.0f) / (3.0f * gammaMinusOne))
                / (phi - 4.0f);
        }
        const float total = std::max(0.0001f, radiation + viscosity + std::max(0.0f, thermal));
        return omega * total / std::sqrt(total * total + 4.0f)
            * clamp(dampingTrim, 0.55f, 1.65f);
    }

    static float bubbleTerminalVelocity(float radiusMeters)
    {
        const float diameter = clamp(radiusMeters * 2.0f, 0.0001f, 0.040f);
        constexpr float densityDifference = 997.0f;
        const float potential = densityDifference * kGravity * diameter * diameter
            / (36.0f * kWaterViscosity);
        const float velocity1 = potential * std::sqrt(1.0f
            + 0.73667f * std::sqrt(kGravity * diameter) / std::max(0.00001f, potential));
        const float velocity2 = std::sqrt(3.0f * kSurfaceTension / (kWaterDensity * diameter)
            + kGravity * diameter * densityDifference / (2.0f * kWaterDensity));
        return 1.0f / std::sqrt(1.0f / std::max(1.0e-8f, velocity1 * velocity1)
            + 1.0f / std::max(1.0e-8f, velocity2 * velocity2));
    }

    static float neckAcceleration(float age, float duration, float amplitude,
        float frequencyHz, float sign)
    {
        if (age < 0.0f || age >= duration) return 0.0f;
        const float u = age / std::max(0.00002f, duration);
        return sign * amplitude * frequencyHz * kPi * 2.0f
            * (3.0f * u * u / std::max(0.00002f, duration));
    }

    static void integrateBubble(float& displacement, float& velocity,
        float frequencyHz, float beta, float acceleration, float dt, float sampleRate)
    {
        const float hz = clamp(frequencyHz, 25.0f, sampleRate * 0.40f);
        const float omega = hz * kPi * 2.0f;
        const float omegaSquared = omega * omega;
        const float damping = std::max(0.0f, beta);

        if (damping < omega * 0.999f) {
            // Exact zero-order-held step for x'' + 2 beta x' + omega^2 x = a.
            const float equilibrium = acceleration / omegaSquared;
            const float relative = displacement - equilibrium;
            const float dampedOmega = std::sqrt(std::max(1.0f,
                omegaSquared - damping * damping));
            const float phase = dampedOmega * dt;
            const float decay = std::exp(-damping * dt);
            const float cosine = std::cos(phase);
            const float sineOverOmega = std::sin(phase) / dampedOmega;
            const float nextDisplacement = equilibrium + decay
                * (relative * cosine + (velocity + damping * relative) * sineOverOmega);
            const float nextVelocity = decay
                * (velocity * cosine
                    - (damping * velocity + omegaSquared * relative) * sineOverOmega);
            displacement = flushDenormal(nextDisplacement);
            velocity = flushDenormal(nextVelocity);
        } else {
            // Trapezoidal integration remains A-stable at critical and overdamping.
            const float halfDt = dt * 0.5f;
            const float positionTerm = displacement + halfDt * velocity;
            const float velocityTerm = (1.0f - damping * dt) * velocity
                + acceleration * dt - halfDt * omegaSquared * displacement;
            const float denominator = 1.0f + damping * dt
                + halfDt * halfDt * omegaSquared;
            const float nextVelocity = (velocityTerm
                - halfDt * omegaSquared * positionTerm) / denominator;
            velocity = flushDenormal(nextVelocity);
            displacement = flushDenormal(positionTerm + halfDt * nextVelocity);
        }
        if (!std::isfinite(displacement) || !std::isfinite(velocity)
            || std::fabs(displacement) > 8.0f || std::fabs(velocity) > sampleRate * 64.0f) {
            displacement = 0.0f;
            velocity = 0.0f;
        }
    }

    static float powerLawRadius(float unit)
    {
        constexpr float minRadius = 0.00010f;
        constexpr float maxRadius = 0.00100f;
        const float lo = 1.0f / std::sqrt(minRadius);
        const float hi = 1.0f / std::sqrt(maxRadius);
        const float inverseRoot = lo + (hi - lo) * clamp(unit, 0.0f, 1.0f);
        return 1.0f / (inverseRoot * inverseRoot);
    }

    AmbiWaterDropEvent* freeDropEvent(AmbiWaterVoice& voice)
    {
        for (auto& event : voice.dropEvents) {
            if (event.age < 0.0f) return &event;
        }
        return nullptr;
    }

    AmbiWaterBubbleEvent* freeBubbleEvent(AmbiWaterVoice& voice)
    {
        for (auto& event : voice.bubbleEvents) {
            if (event.age < 0.0f) return &event;
        }
        return nullptr;
    }

    AmbiWaterMicroBubble* freeMicroBubble(AmbiWaterVoice& voice)
    {
        for (auto& event : voice.microBubbles) {
            if (event.age < 0.0f) return &event;
        }
        return nullptr;
    }

    void seedMicroBubbles(AmbiWaterVoice& voice, float strength, float onsetDelay = 0.0f)
    {
        const uint32_t count = 1u + static_cast<uint32_t>(clamp(strength, 0.0f, 1.0f) * 2.99f);
        for (uint32_t i = 0u; i < count; ++i) {
            auto* event = freeMicroBubble(voice);
            if (!event) return;
            *event = {};
            event->age = 0.0f;
            event->delay = std::max(0.0f, onsetDelay)
                + randomUnit(voice) * (0.012f + strength * 0.070f);
            event->radiusMeters = powerLawRadius(randomUnit(voice));
            const float frequency = clamp(bubbleFrequencyHz(event->radiusMeters,
                event->radiusMeters * 18.0f, event->radiusMeters * 24.0f, 0.0f),
                1200.0f, static_cast<float>(sampleRate_) * 0.38f);
            const float beta = bubbleDampingBeta(event->radiusMeters, frequency, 1.0f);
            event->forceDuration = std::min(0.0006f, 0.5f / frequency);
            event->duration = clamp(4.6052f / std::max(1.0f, beta), 0.002f, 0.045f);
            event->amplitude = (0.014f + strength * 0.040f)
                * std::cbrt(event->radiusMeters / 0.001f) * (0.72f + randomUnit(voice) * 0.56f);
        }
    }

    void triggerDrop(AmbiWaterVoice& voice, const AmbiWaterParams& p,
        float eventSize, float brightness, float turbulence,
        float environmentHardness, float environmentDamp, float liquidSurface,
        float pointElevationDeg, float pointDistance, float entrainment, float dripMode)
    {
        auto* event = freeDropEvent(voice);
        if (!event) return;

        *event = {};
        event->age = 0.0f;
        const float size = naturalEventMacro(eventSize);
        const float sizeScatter = randomUnit(voice);
        const float centeredScatter = sizeScatter * sizeScatter - 0.333333f;
        const float radiusNorm = clamp(0.07f + size * 0.68f + p.depth * 0.03f
                - brightness * 0.025f
                + centeredScatter * (0.34f + p.deviation * 0.12f),
            0.015f, 0.98f);
        event->radiusMeters = 0.00010f * std::pow(30.0f, radiusNorm);
        event->terminalVelocity = raindropTerminalVelocity(event->radiusMeters);
        event->sourceHorizontalMeters = 0.40f + clamp(pointDistance, 0.2f, 2.6f) * 2.4f;
        event->listenerHeightMeters = 0.72f
            + clamp((75.0f - pointElevationDeg) / 165.0f, 0.0f, 1.0f) * 1.05f;
        event->liquidSurface = clamp(liquidSurface, 0.0f, 1.0f);
        event->surfaceHardness = clamp(environmentHardness, 0.0f, 1.0f);
        event->impactPolarity = randomUnit(voice) < 0.5f ? -1.0f : 1.0f;

        const float nearPath = std::sqrt(std::pow(event->sourceHorizontalMeters
                - event->radiusMeters, 2.0f)
            + event->listenerHeightMeters * event->listenerHeightMeters);
        const float farPath = std::sqrt(std::pow(event->sourceHorizontalMeters
                + event->radiusMeters, 2.0f)
            + event->listenerHeightMeters * event->listenerHeightMeters);
        const float geometricDuration = (farPath - nearPath) / kAirSoundSpeed;
        const float compliance = 0.62f + (1.0f - event->surfaceHardness) * 3.2f
            + environmentDamp * 0.80f;
        const float contactDuration = 2.0f * event->radiusMeters
            / std::max(0.45f, event->terminalVelocity) * compliance;
        event->impactDuration = clamp(std::max(3.25f / static_cast<float>(sampleRate_),
                contactDuration + geometricDuration),
            0.00007f, 0.0065f);
        const float dripContactDuration = 0.0012f + size * 0.0028f;
        event->impactDuration = std::max(event->impactDuration,
            dripContactDuration * dripMode);

        const float dropVolume = (4.0f / 3.0f) * kPi * event->radiusMeters
            * event->radiusMeters * event->radiusMeters;
        const float kineticEnergy = 0.5f * kWaterDensity * dropVolume
            * event->terminalVelocity * event->terminalVelocity;
        const float energyScale = std::sqrt(std::max(0.0f, kineticEnergy) / 0.000075f);
        event->amplitude = clamp((0.085f + energyScale * 0.28f)
                * (0.82f + randomUnit(voice) * 0.36f),
            0.045f, 0.78f);

        const float elevationDepth = clamp((50.0f - pointElevationDeg) / 140.0f, 0.0f, 1.0f);
        const float cavityScatter = randomUnit(voice);
        const float regularCavityRadius = event->radiusMeters
            * (0.34f + cavityScatter * 0.48f);
        const float dripCavityRadius = event->radiusMeters
            * (1.10f + cavityScatter * 1.80f);
        event->cavityRadiusMeters = clamp(regularCavityRadius
                + (dripCavityRadius - regularCavityRadius) * dripMode,
            0.00005f, 0.0060f);
        event->surfaceDepthMeters = event->cavityRadiusMeters
            * (2.1f + p.depth * 5.0f + elevationDepth * 2.0f + randomUnit(voice) * 2.0f);
        event->rigidDistanceMeters = event->cavityRadiusMeters
            * (1.3f + (1.0f - environmentHardness) * 14.0f + randomUnit(voice) * 4.0f);
        event->rigidBlend = environmentHardness;

        const float diameterMm = event->radiusMeters * 2000.0f;
        const float typeOne = std::exp(-0.5f * std::pow((diameterMm - 0.95f) / 0.34f, 2.0f));
        const float typeTwo = std::exp(-0.5f * std::pow((diameterMm - 2.75f) / 0.86f, 2.0f));
        const float speedGate = smoothRange(0.8f, 3.8f, event->terminalVelocity);
        const float sizeCondition = clamp(0.06f + typeOne * 0.58f + typeTwo * 0.72f,
            0.0f, 1.0f);
        const float wetGate = smoothRange(0.18f, 0.58f, event->liquidSurface);
        const float regularEntrainmentChance = clamp(wetGate * clamp(entrainment, 0.0f, 1.0f)
                * sizeCondition * speedGate
                * (0.40f + p.splash * 0.28f + p.aeration * 0.20f + turbulence * 0.12f),
            0.0f, 0.72f);
        const float dripEntrainmentChance = clamp((0.88f + size * 0.08f)
                * (0.85f + event->liquidSurface * 0.15f),
            0.72f, 0.96f);
        const float entrainmentChance = regularEntrainmentChance
            + (dripEntrainmentChance - regularEntrainmentChance) * dripMode;
        const bool cavityAccepted = randomUnit(voice) < entrainmentChance;
        const float regularCavityGain = clamp(0.20f + entrainment * 0.28f
                + p.splash * 0.18f + p.aeration * 0.12f,
            0.20f, 0.72f);
        const float dripCavityGain = clamp(0.48f + size * 0.24f
                + (1.0f - p.damping) * 0.12f,
            0.48f, 0.82f);
        event->cavityGain = cavityAccepted
            ? regularCavityGain + (dripCavityGain - regularCavityGain) * dripMode
            : 0.0f;
        event->cavityDelay = event->impactDuration + clamp(0.0007f
                + std::sqrt(event->cavityRadiusMeters / 0.001f) * 0.0018f
                + (1.0f - speedGate) * 0.0012f,
            0.0008f, 0.0060f);
        const float frequency = clamp(bubbleFrequencyHz(event->cavityRadiusMeters,
            event->surfaceDepthMeters, event->rigidDistanceMeters, event->rigidBlend),
            70.0f, std::min(5600.0f, static_cast<float>(sampleRate_) * 0.38f));
        const float beta = bubbleDampingBeta(event->cavityRadiusMeters, frequency,
            0.72f + p.damping * 0.68f);
        const float regularForceDuration = std::min(0.0006f, 0.5f / frequency);
        const float dripForceDuration = std::min(0.0020f, 1.5f / frequency);
        event->forceDuration = regularForceDuration
            + (dripForceDuration - regularForceDuration) * dripMode;
        const float chirpScatter = hash01(voice.rng + 0x43a1b7d5u);
        const float chirpDirection = hash01(voice.rng + 0x91e10da5u);
        event->chirpDepthOctaves = 0.45f + size * 0.75f + chirpScatter * 0.35f;
        event->chirpDuration = 0.035f + naturalEventMacro(p.eventDecay) * 0.110f;
        event->chirpDirection = chirpDirection < 0.68f ? -1.0f : 1.0f;
        const float materialTailScale = 1.0f - event->surfaceHardness * 0.64f;
        event->tailDecay = (0.0030f + (1.0f - event->surfaceHardness) * 0.0060f
                + (1.0f - p.damping) * 0.016f + naturalEventMacro(p.eventDecay) * 0.010f)
            * materialTailScale;
        event->tailDecay += dripMode
            * (0.045f + naturalEventMacro(p.eventDecay) * 0.070f);
        event->diffusion = clamp(0.06f + turbulence * 0.20f + randomUnit(voice) * 0.10f,
            0.0f, 0.48f);
        event->lastFrequency = frequency;
        const float cavityDuration = cavityAccepted
            ? event->cavityDelay + clamp(4.6052f / std::max(1.0f, beta), 0.008f, 0.24f)
            : 0.0f;
        event->duration = std::max(event->impactDuration + event->tailDecay * 6.0f,
            cavityDuration) + 0.025f;
        if (cavityAccepted && event->cavityGain > 0.06f) {
            seedMicroBubbles(voice, clamp((p.aeration * 0.42f + p.splash * 0.38f
                + turbulence * 0.20f) * std::sqrt(event->cavityGain), 0.0f, 1.0f),
                event->cavityDelay);
        }
    }

    void triggerBubble(AmbiWaterVoice& voice, const AmbiWaterParams& p,
        float eventSize, float turbulence, float environmentHardness,
        float pointElevationDeg)
    {
        auto* event = freeBubbleEvent(voice);
        if (!event) return;

        *event = {};
        event->age = 0.0f;
        const float size = naturalEventMacro(eventSize);
        const float life = naturalEventMacro(p.eventDecay);
        const float radiusNorm = clamp(0.14f + size * 0.70f + p.depth * 0.06f
                + randomSigned(voice) * (0.10f + p.deviation * 0.08f),
            0.06f, 0.92f);
        event->radiusMeters = 0.00035f * std::pow(30.0f, radiusNorm);
        const float radiusScale = std::sqrt(event->radiusMeters / 0.0080f);
        const float elevationDepth = clamp((45.0f - pointElevationDeg) / 135.0f, 0.0f, 1.0f);
        event->surfaceDepthMeters = event->radiusMeters * 2.0f
            * (1.10f + p.depth * 10.0f + elevationDepth * 3.0f + randomUnit(voice) * 4.0f);
        event->rigidDistanceMeters = event->radiusMeters
            * (1.25f + (1.0f - environmentHardness) * 16.0f + randomUnit(voice) * 5.0f);
        event->rigidBlend = environmentHardness;
        const float frequency = clamp(bubbleFrequencyHz(event->radiusMeters,
            event->surfaceDepthMeters, event->rigidDistanceMeters, event->rigidBlend),
            70.0f, std::min(6500.0f, static_cast<float>(sampleRate_) * 0.38f));
        event->forceDuration = std::min(0.0006f, 0.5f / frequency);
        event->riseVelocity = bubbleTerminalVelocity(event->radiusMeters)
            * (0.48f + p.resonance * 1.52f);
        event->lifetime = (0.10f + life * 0.90f) * (0.82f + randomUnit(voice) * 0.36f);
        event->amplitude = (0.16f + radiusScale * 0.42f)
            * (0.78f + size * 0.22f) * (0.76f + randomUnit(voice) * 0.48f);
        const float topologyChance = 0.08f + turbulence * 0.34f + p.aeration * 0.28f;
        if (randomUnit(voice) < topologyChance) {
            event->topologyTime = event->lifetime * (0.28f + randomUnit(voice) * 0.44f);
            if (randomUnit(voice) < 0.58f) {
                event->topologyScale = std::cbrt(0.44f + randomUnit(voice) * 0.42f);
            } else {
                event->topologyScale = std::cbrt(1.18f + randomUnit(voice) * 0.62f);
            }
        }
        event->popDuration = 0.004f + randomUnit(voice) * 0.016f;
        event->popGain = 0.001f + randomUnit(voice) * 0.029f;
        event->lastFrequency = frequency;
    }

    float processDropEvent(AmbiWaterDropEvent& event, const AmbiWaterParams& p,
        float rough, float sprayNoise, float surfaceBand, float slowNoise,
        float rainMode, float dripMode, float dt, float& activity, float& impactSignal)
    {
        if (event.age < 0.0f) return 0.0f;
        event.age += dt;
        const float impact = diskImpactPressure(event);
        const float impactProgress = clamp(event.age / std::max(0.00001f, event.impactDuration),
            0.0f, 1.0f);
        const float pressureWave = event.impactPolarity * impact * (1.0f - impactProgress * 2.0f);
        float frequency = event.lastFrequency;
        if (event.cavityGain > 0.0f && event.age >= event.cavityDelay) {
            const float jitter = 1.0f + slowNoise * event.diffusion * 0.025f
                + rough * event.diffusion * 0.002f;
            const float cavityAge = event.age - event.cavityDelay;
            const float chirpProgress = clamp(cavityAge
                    / std::max(0.005f, event.chirpDuration),
                0.0f, 1.0f);
            const float chirpShape = chirpProgress * chirpProgress
                * (3.0f - 2.0f * chirpProgress);
            const float chirpRatio = std::exp2(event.chirpDirection
                * event.chirpDepthOctaves * chirpShape);
            const float dripChirp = 1.0f + (chirpRatio - 1.0f) * dripMode;
            frequency = clamp(bubbleFrequencyHz(event.cavityRadiusMeters,
                    event.surfaceDepthMeters, event.rigidDistanceMeters, event.rigidBlend)
                    * jitter * dripChirp,
                50.0f, std::min(5600.0f, static_cast<float>(sampleRate_) * 0.38f));
            const float beta = bubbleDampingBeta(event.cavityRadiusMeters, frequency,
                0.72f + p.damping * 0.68f);
            const float acceleration = neckAcceleration(cavityAge, event.forceDuration,
                event.amplitude * event.cavityGain, frequency, -1.0f);
            integrateBubble(event.displacement, event.velocity, frequency, beta, acceleration,
                dt, static_cast<float>(sampleRate_));
        }
        event.lastFrequency = frequency;
        const float tailAge = std::max(0.0f, event.age - event.impactDuration * 0.35f);
        const float tailAttack = 1.0f - std::exp(-tailAge / 0.0014f);
        const float tailEnv = tailAttack * std::exp(-tailAge / std::max(0.002f, event.tailDecay));
        const float broadImpactNoise = rough * 0.72f + sprayNoise * 0.28f;
        const float dripImpactNoise = surfaceBand * 0.30f + sprayNoise * 0.08f
            + slowNoise * 0.62f;
        const float impactNoise = (broadImpactNoise
                + (dripImpactNoise - broadImpactNoise) * dripMode) * impact;
        const float collision = pressureWave * (0.18f + event.surfaceHardness * 0.56f)
                * (1.0f - dripMode * 0.84f)
            + impactNoise * (0.66f - event.surfaceHardness * 0.24f)
                * (1.0f - dripMode * 0.82f);
        const float broadLiquidTail = surfaceBand * 0.70f + rough * 0.30f;
        const float dripLiquidTail = surfaceBand * 0.22f + slowNoise * 0.78f;
        const float liquidTail = (broadLiquidTail
                + (dripLiquidTail - broadLiquidTail) * dripMode)
            * tailEnv * event.liquidSurface * (1.0f + dripMode * 1.40f);
        const float broadSolidTail = rough * 0.64f + slowNoise * 0.36f;
        const float dripSolidTail = surfaceBand * 0.15f + slowNoise * 0.85f;
        const float solidTail = (broadSolidTail
                + (dripSolidTail - broadSolidTail) * dripMode)
            * tailEnv * (1.0f - event.liquidSurface)
            * (0.18f + (1.0f - event.surfaceHardness) * 0.34f)
            * (1.0f + dripMode * 0.70f);
        const float toneAmount = (0.28f + p.depth * 0.24f + (1.0f - p.contact) * 0.14f)
            * (1.0f - event.diffusion * 0.34f) * (1.0f + dripMode * 2.5f);
        const float noiseAmount = (0.50f + p.contact * 0.58f + rainMode * 0.18f)
            * (1.0f - dripMode * 0.60f);
        const float dripAttack = 1.0f - std::exp(-event.age / 0.0035f);
        const float eventAttack = 1.0f + (dripAttack - 1.0f) * dripMode;
        const float rawOutput = (event.amplitude * (collision * noiseAmount
            + (liquidTail + solidTail) * (0.30f + p.contact * 0.22f
                + event.diffusion * 0.24f))
            + event.displacement * toneAmount * event.cavityGain) * eventAttack;
        const float broadRadiationHz = 2600.0f + p.brightness * 4500.0f
            + event.surfaceHardness * 1300.0f + rainMode * 7000.0f;
        const float dripRadiationHz = 1800.0f + p.brightness * 1200.0f
            + event.surfaceHardness * 300.0f;
        const float radiationHz = clamp(broadRadiationHz
                + (dripRadiationHz - broadRadiationHz) * dripMode,
            1200.0f, static_cast<float>(sampleRate_) * 0.35f);
        const float radiationCoeff = 1.0f
            - std::exp(-kPi * 2.0f * radiationHz * dt);
        event.radiationState1 = flushDenormal(event.radiationState1
            + (rawOutput - event.radiationState1) * radiationCoeff);
        event.radiationState2 = flushDenormal(event.radiationState2
            + (event.radiationState1 - event.radiationState2) * radiationCoeff);
        const float output = event.radiationState2
            + (rawOutput - event.radiationState2) * rainMode * 0.55f;
        impactSignal += event.amplitude * collision;
        const float resonanceLevel = clamp(std::fabs(event.displacement)
            / std::max(0.01f, event.amplitude), 0.0f, 1.0f) * event.cavityGain;
        activity = std::max(activity, std::max(impact, std::max(resonanceLevel, tailEnv))
            * event.amplitude);
        if (event.age >= event.duration) event.age = -1.0f;
        return output;
    }

    float processBubbleEvent(AmbiWaterBubbleEvent& event, const AmbiWaterParams& p,
        float rough, float sprayNoise, float surfaceBand, float slowNoise,
        float dt, float& activity)
    {
        if (event.age < 0.0f) return 0.0f;
        event.age += dt;
        if (event.popAge >= 0.0f) {
            event.popAge += dt;
            const float progress = clamp(event.popAge / std::max(0.001f, event.popDuration), 0.0f, 1.0f);
            const float volume = (4.0f / 3.0f) * kPi * event.radiusMeters
                * event.radiusMeters * event.radiusMeters;
            const float film = std::max(0.0001f, std::sin(progress * kPi * 0.5f));
            const float popFrequency = clamp(kAirSoundSpeed / (2.0f * kPi)
                    * std::sqrt(3.0f * kPi * kPi * event.radiusMeters * film
                        / std::max(1.0e-14f, 16.0f * volume)),
                700.0f, static_cast<float>(sampleRate_) * 0.42f);
            event.popPhase += popFrequency * dt;
            event.popPhase -= std::floor(event.popPhase);
            const float popEnvelope = std::exp(std::log(0.00001f) * progress * progress)
                * (2.0f / kPi) * std::atan(progress * 8.0f);
            const float collapseBeta = bubbleDampingBeta(event.radiusMeters,
                std::max(70.0f, event.lastFrequency), 1.6f) * 6.0f;
            integrateBubble(event.displacement, event.velocity, event.lastFrequency,
                collapseBeta, 0.0f, dt, static_cast<float>(sampleRate_));
            const float collapse = event.displacement * std::exp(-event.popAge / 0.0012f);
            const float pop = std::sin(event.popPhase * kPi * 2.0f)
                * popEnvelope * event.amplitude * event.popGain;
            activity = std::max(activity, clamp(popEnvelope * event.popGain * 10.0f
                + std::fabs(collapse), 0.0f, 1.0f));
            if (event.popAge >= event.popDuration) event.age = -1.0f;
            return collapse + pop;
        }

        if (event.topologyTime >= 0.0f && event.age >= event.topologyTime) {
            event.radiusMeters = clamp(event.radiusMeters * event.topologyScale,
                0.00025f, 0.010f);
            event.surfaceDepthMeters = std::max(event.radiusMeters * 1.02f,
                event.surfaceDepthMeters);
            event.rigidDistanceMeters = std::max(event.radiusMeters * 1.02f,
                event.rigidDistanceMeters);
            event.riseVelocity = bubbleTerminalVelocity(event.radiusMeters)
                * (0.48f + p.resonance * 1.52f);
            event.secondaryForceAge = 0.0f;
            event.secondaryForceSign = event.topologyScale > 1.0f ? 0.46f : -0.58f;
            event.topologyTime = -1.0f;
        }

        event.surfaceDepthMeters = std::max(event.radiusMeters * 1.01f,
            event.surfaceDepthMeters - event.riseVelocity * dt);
        const float frequency = clamp(bubbleFrequencyHz(event.radiusMeters,
                event.surfaceDepthMeters, event.rigidDistanceMeters, event.rigidBlend)
                * (1.0f + slowNoise * 0.006f),
            50.0f, std::min(6500.0f, static_cast<float>(sampleRate_) * 0.38f));
        const float beta = bubbleDampingBeta(event.radiusMeters, frequency,
            0.72f + p.damping * 0.68f);
        float acceleration = neckAcceleration(event.age, event.forceDuration,
            event.amplitude, frequency, -1.0f);
        if (event.secondaryForceAge >= 0.0f) {
            acceleration += neckAcceleration(event.secondaryForceAge, event.forceDuration,
                event.amplitude, frequency, event.secondaryForceSign);
            event.secondaryForceAge += dt;
            if (event.secondaryForceAge >= event.forceDuration) event.secondaryForceAge = -1.0f;
        }
        integrateBubble(event.displacement, event.velocity, frequency, beta, acceleration,
            dt, static_cast<float>(sampleRate_));
        event.lastFrequency = frequency;
        const float nearSurface = std::exp(-std::max(0.0f,
            event.surfaceDepthMeters - event.radiusMeters) / std::max(0.0001f, event.radiusMeters * 7.0f));
        const float radiation = 0.30f + nearSurface * 0.70f;
        const float wakeLevel = clamp(std::fabs(event.displacement)
            / std::max(0.01f, event.amplitude), 0.0f, 1.0f);
        const float wake = (surfaceBand * 0.72f + sprayNoise * 0.28f)
            * wakeLevel * (0.015f + p.aeration * 0.035f);
        const float output = event.displacement * radiation * (0.76f + p.depth * 0.20f) + wake;
        activity = std::max(activity, clamp(wakeLevel * event.amplitude + nearSurface * 0.08f,
            0.0f, 1.0f));

        if (event.surfaceDepthMeters <= event.radiusMeters * 1.015f) {
            event.popAge = 0.0f;
        } else if (event.age >= event.lifetime
            && std::fabs(event.displacement) < 0.0001f) {
            event.age = -1.0f;
        } else if (event.age >= event.lifetime + 0.30f) {
            event.age = -1.0f;
        }
        return output;
    }

    float processMicroBubble(AmbiWaterMicroBubble& event, float rough,
        float dt, float& activity)
    {
        if (event.age < 0.0f) return 0.0f;
        event.age += dt;
        const float localAge = event.age - event.delay;
        if (localAge < 0.0f) return 0.0f;
        const float frequency = clamp(bubbleFrequencyHz(event.radiusMeters,
            event.radiusMeters * 18.0f, event.radiusMeters * 24.0f, 0.0f),
            1200.0f, static_cast<float>(sampleRate_) * 0.38f);
        const float beta = bubbleDampingBeta(event.radiusMeters, frequency, 1.0f);
        const float acceleration = neckAcceleration(localAge, event.forceDuration,
            event.amplitude, frequency, -1.0f);
        integrateBubble(event.displacement, event.velocity, frequency, beta, acceleration,
            dt, static_cast<float>(sampleRate_));
        const float formation = parabolicPulse(localAge, event.forceDuration * 3.0f);
        const float output = event.displacement + rough * formation * event.amplitude * 0.18f;
        activity = std::max(activity, clamp(std::fabs(event.displacement)
            / std::max(0.002f, event.amplitude), 0.0f, 1.0f) * event.amplitude);
        if (localAge >= event.duration) event.age = -1.0f;
        return output;
    }

    float processVoice(uint32_t index)
    {
        auto& voice = voices_[index];
        if (!voiceHealthy(voice)) initializeVoice(index);
        const auto& p = smoothParams_;
        const float dt = 1.0f / static_cast<float>(sampleRate_);
        const float lane = static_cast<float>(index) / static_cast<float>(std::max<uint32_t>(1u, p.voices - 1u));
        const float identity = hashSigned(voice.identity + 307u);
        const float deviation = identity * p.deviation;
        const float water = clamp(p.water + deviation * 0.20f, 0.0f, 1.0f);
        const float flow = clamp(p.flow + (lane - 0.5f) * p.spread * 0.22f, 0.0f, 1.0f);
        const float turbulence = clamp(p.turbulence + std::fabs(deviation) * 0.42f, 0.0f, 1.0f);
        const float brightness = clamp(p.brightness + deviation * 0.24f, 0.0f, 1.0f);
        const float eventSize = clamp(p.eventSize + deviation * 0.28f, 0.0f, 1.0f);

        const float currentMode = regimeWeights_[0];
        const float rainMode = regimeWeights_[1];
        const float cascadeMode = regimeWeights_[2];
        const float surgeMode = regimeWeights_[3];
        const float vortexMode = regimeWeights_[4];
        const float sloshMode = regimeWeights_[5];
        const float dripMode = regimeWeights_[6];
        const float plumeMode = regimeWeights_[7];

        const float white = randomSigned(voice);
        voice.fastNoise += (white - voice.fastNoise) * (0.09f + brightness * 0.30f + p.aeration * 0.12f);
        voice.midNoise += (white - voice.midNoise) * (0.012f + flow * 0.055f + turbulence * 0.06f);
        voice.slowNoise += (voice.midNoise - voice.slowNoise) * (0.0010f + p.scale * 0.006f + flow * 0.004f);
        voice.airNoise += (white - voice.airNoise) * (0.18f + p.aeration * 0.38f);
        const float rough = white - voice.fastNoise;
        const float sprayNoise = white - voice.airNoise;
        const float bodyNoise = voice.slowNoise * (0.72f + p.depth * 0.78f)
            + voice.midNoise * (0.42f + flow * 0.54f) + rough * turbulence * 0.30f;

        const float rock = environmentWeights_[1];
        const float leaves = environmentWeights_[2];
        const float mud = environmentWeights_[3];
        const float concrete = environmentWeights_[4];
        const float metal = environmentWeights_[5];
        const float glass = environmentWeights_[6];
        const float pipe = environmentWeights_[7];
        const float cave = environmentWeights_[8];
        const float environmentBright = rock * 0.10f + leaves * 0.25f - mud * 0.28f + concrete * 0.18f
            + metal * 0.34f + glass * 0.46f + pipe * 0.08f - cave * 0.18f;
        const float environmentHardness = rock * 0.42f + leaves * 0.10f + mud * 0.02f
            + concrete * 0.58f + metal * 0.78f + glass * 0.88f + pipe * 0.66f + cave * 0.24f;
        const float environmentDamp = mud * 0.72f + leaves * 0.34f + cave * 0.22f;
        const float liquidSurface = clamp(environmentWeights_[0] + rock * 0.68f
                + leaves * 0.04f + mud * 0.16f + concrete * 0.12f
                + metal * 0.16f + glass * 0.02f + pipe * 0.36f + cave * 0.62f,
            0.0f, 1.0f);
        const float entrainment = clamp(liquidSurface * (currentMode * 0.52f
                + rainMode * 0.42f + cascadeMode * 0.82f + surgeMode * 0.72f
                + vortexMode * 0.78f + sloshMode * 0.58f + dripMode * 0.78f
                + plumeMode),
            0.0f, 1.0f);

        const float bodyNorm = clamp(0.18f + brightness * 0.56f - p.depth * 0.24f
                + environmentBright + voice.motionEnergy * 0.10f,
            0.0f, 1.0f);
        const float bodyHz = freqFromNorm(bodyNorm, 28.0f, 9200.0f);
        const float bodyResonance = clamp(p.resonance * 0.42f + environmentHardness * 0.08f
                - p.damping * 0.28f - environmentDamp * 0.24f,
            0.0f, 0.64f);
        const float bodyBand = voice.bodyFilter.process(bodyNoise, bodyHz, bodyResonance, static_cast<float>(sampleRate_));
        const float surfaceBand = voice.surfaceFilter.process(sprayNoise + rough * 0.32f,
            bodyHz * (2.1f + p.aeration * 5.8f + rainMode * 1.8f),
            0.03f + p.resonance * 0.12f, static_cast<float>(sampleRate_));

        const float dropRate = p.drops * (0.025f + p.density * p.density * 2.8f)
            * (currentMode * 0.42f + rainMode * 3.22f + cascadeMode * 1.14f
                + surgeMode * 0.78f + vortexMode * 0.35f + sloshMode * 0.52f
                + dripMode * 1.12f + plumeMode * 0.08f);
        voice.dropTimer -= dt;
        if (voice.dropTimer <= 0.0f) {
            // Timers represent listener arrival times, avoiding a distance-dependent startup ramp.
            voice.dropTimer = eventInterval(voice, dropRate);
            triggerDrop(voice, p, eventSize, brightness, turbulence,
                environmentHardness, environmentDamp, liquidSurface,
                points_[index].elevationDeg, points_[index].distance, entrainment, dripMode);
        }

        const float bubbleRate = p.bubbles * (0.018f + p.density * p.density * 1.35f)
            * (currentMode * 0.45f + rainMode * 0.30f
                + cascadeMode * (0.45f + p.aeration * 1.20f) + surgeMode * 0.72f
                + vortexMode * 2.85f + sloshMode * 1.25f + dripMode * 0.18f
                + plumeMode * 3.20f);
        voice.bubbleTimer -= dt;
        if (voice.bubbleTimer <= 0.0f) {
            voice.bubbleTimer = eventInterval(voice, bubbleRate);
            triggerBubble(voice, p, eventSize, turbulence,
                environmentHardness, points_[index].elevationDeg);
        }

        if (voice.impactPending > 0.001f && p.splash > 0.001f && voice.splashAge < 0.0f) {
            voice.splashAge = 0.0f;
            voice.splashStrength = voice.impactPending;
            if (liquidSurface > 0.08f) {
                seedMicroBubbles(voice, clamp((voice.impactPending * 0.48f
                    + p.aeration * 0.34f + turbulence * 0.18f)
                        * std::sqrt(liquidSurface),
                    0.0f, 1.0f));
            }
        }
        voice.impactPending = 0.0f;

        const float splashDecay = 0.018f + (1.0f - p.damping) * 0.10f
            + eventSize * 0.060f + p.scale * 0.060f;
        const float splashEnv = eventEnvelope(voice.splashAge, 0.0020f + eventSize * 0.0040f, splashDecay, dt);
        float dropTone = 0.0f;
        float dropLevel = 0.0f;
        float dropImpact = 0.0f;
        for (auto& event : voice.dropEvents) {
            dropTone += processDropEvent(event, p, rough, sprayNoise, surfaceBand,
                voice.slowNoise, rainMode, dripMode, dt, dropLevel, dropImpact);
        }
        dropTone *= p.drops;

        float bubbleTone = 0.0f;
        float bubbleLevel = 0.0f;
        for (auto& event : voice.bubbleEvents) {
            bubbleTone += processBubbleEvent(event, p, rough, sprayNoise, surfaceBand,
                voice.slowNoise, dt, bubbleLevel);
        }
        bubbleTone *= p.bubbles;

        float microTone = 0.0f;
        float microLevel = 0.0f;
        for (auto& event : voice.microBubbles) {
            microTone += processMicroBubble(event, rough, dt, microLevel);
        }
        const float splashTone = (surfaceBand * 1.42f + rough * 0.42f)
            * splashEnv * p.splash * (0.34f + voice.splashStrength * 0.76f);
        voice.splashStrength *= std::exp(-dt / std::max(0.004f, splashDecay * 0.72f));

        const float impactEnvelope = clamp(dropLevel * (0.42f + p.drops * 0.58f)
                + splashEnv * (0.34f + voice.splashStrength * 0.66f),
            0.0f, 1.5f);
        const float materialBaseHz = 1200.0f * environmentWeights_[0]
            + 1800.0f * rock + 680.0f * leaves + 220.0f * mud
            + 2400.0f * concrete + 3300.0f * metal + 4800.0f * glass
            + 980.0f * pipe + 460.0f * cave;
        const float materialHz = clamp(materialBaseHz * (0.64f + brightness * 0.92f),
            90.0f, 9200.0f);
        const float materialResonance = clamp(0.025f + rock * 0.055f + leaves * 0.010f
                + concrete * 0.070f + metal * 0.145f + glass * 0.095f
                + pipe * 0.120f + cave * 0.040f + p.resonance * 0.055f
                - p.damping * 0.025f,
            0.0f, 0.28f);
        const float materialNoise = (rough * 0.58f + voice.midNoise * 0.28f
                + sprayNoise * 0.14f) * impactEnvelope
            * (0.10f + leaves * 0.72f + mud * 0.24f + cave * 0.18f);
        const float materialExcitation = dropImpact * (0.62f + environmentHardness * 0.52f)
            + materialNoise;
        const float surfaceTexture = voice.contactFilter.process(materialExcitation, materialHz,
            materialResonance,
            static_cast<float>(sampleRate_));

        const float bubbleFocus = clamp((plumeMode + sloshMode * 0.28f) * p.bubbles, 0.0f, 1.0f);
        const float bodyAmount = water * (0.16f + flow * 0.68f + p.scale * 0.28f)
            * (currentMode * 0.96f + rainMode * 0.62f + cascadeMode * 1.16f
                + surgeMode * 1.10f + vortexMode * 0.86f + sloshMode * 0.84f
                + dripMode * 0.46f + plumeMode * 0.58f) * (1.0f - bubbleFocus * 0.34f);
        const float sprayAmount = p.aeration * (0.04f + turbulence * 0.44f)
            * (currentMode * 0.28f + rainMode * 1.20f + cascadeMode * 1.36f
                + surgeMode * 0.76f + vortexMode * 0.28f + sloshMode * 0.24f
                + dripMode * 0.20f + plumeMode * 0.08f)
            * (1.0f - bubbleFocus * 0.62f);
        const float eventSource = dropTone * (currentMode * 0.048f + rainMode * 0.116f
                + cascadeMode * 0.080f + surgeMode * 0.062f + vortexMode * 0.044f
                + sloshMode * 0.060f + dripMode * 0.850f + plumeMode * 0.018f)
            + splashTone * (currentMode * 0.050f + rainMode * 0.056f
                + cascadeMode * 0.145f + surgeMode * 0.110f + vortexMode * 0.052f
                + sloshMode * 0.092f + dripMode * 0.058f + plumeMode * 0.032f)
            + bubbleTone * (currentMode * 0.070f + rainMode * 0.055f
                + cascadeMode * 0.092f + surgeMode * 0.078f + vortexMode * 0.175f
                + sloshMode * 0.128f + dripMode * 0.082f + plumeMode * 0.360f)
            + microTone * (currentMode * 0.016f + rainMode * 0.028f
                + cascadeMode * 0.036f + surgeMode * 0.026f + vortexMode * 0.030f
                + sloshMode * 0.022f + dripMode * 0.024f + plumeMode * 0.042f);
        const float transferMix = clamp(environmentHardness * 0.52f + cave * 0.28f
                + pipe * 0.20f + glass * 0.12f,
            0.0f, 0.78f);
        const float transferHz = freqFromNorm(clamp(0.20f + brightness * 0.24f
                + environmentBright * 0.42f + p.depth * 0.08f,
            0.0f, 1.0f), 95.0f, 5200.0f);
        const float transferResonance = clamp(0.025f + rock * 0.045f + concrete * 0.050f
                + metal * 0.115f + glass * 0.075f + pipe * 0.125f + cave * 0.070f
                + p.resonance * 0.050f - p.damping * 0.025f,
            0.0f, 0.27f);
        const float transferredEvents = voice.transferFilter.process(eventSource,
            transferHz, transferResonance, static_cast<float>(sampleRate_));
        const float mixed = bodyBand * bodyAmount
            + surfaceBand * sprayAmount
            + eventSource * (1.0f - transferMix * 0.42f)
            + transferredEvents * transferMix * 0.72f
            + surfaceTexture * p.contact
                * (0.052f + environmentHardness * 0.090f + leaves * 0.095f);
        const float drive = 0.90f + turbulence * 0.84f + p.aeration * 0.48f + p.resonance * 0.34f;
        const float output = std::tanh(clamp(mixed * drive, -3.8f, 3.8f))
            * (0.18f + water * 0.78f + p.density * 0.08f);
        const float eventLevel = std::max(dropLevel,
            std::max(splashEnv, std::max(bubbleLevel, microLevel)));
        voice.eventViz += (eventLevel - voice.eventViz) * 0.012f;
        if (!std::isfinite(output)) {
            initializeVoice(index);
            return 0.0f;
        }
        return output;
    }

    AmbiWaterParams params_ {};
    AmbiWaterParams smoothParams_ {};
    std::array<AmbiWaterVoice, kAmbiWaterMaxVoices> voices_ {};
    std::array<AmbiWaterPoint, kAmbiWaterMaxVoices> points_ {};
    std::array<AmbiWaterPoint, kAmbiWaterMaxVoices> targetPoints_ {};
    std::array<float, kAmbiWaterRegimeCount> regimeWeights_ { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    std::array<float, kAmbiWaterEnvironmentCount> environmentWeights_ { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    std::array<float, kAmbiWaterMaxChannels> lastOutput_ {};
    std::array<float, kAmbiWaterMaxChannels> transitionTail_ {};
    double sampleRate_ = 48000.0;
    float smoothedOutputGain_ = dbToGain(-6.0f);
    float smoothedSceneGain_ = 1.0f;
    float transitionFade_ = 1.0f;
    bool smoothReady_ = false;
    std::atomic<bool> transitionRequested_ { false };
};

} // namespace s3g
