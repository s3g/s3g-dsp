#pragma once

#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAmbiWindMaxVoices = 64;
constexpr uint32_t kAmbiWindMaxOrder = 7;
constexpr uint32_t kAmbiWindMaxChannels = 64;

struct AmbiWindParams {
    uint32_t order = 3;
    uint32_t voices = 24;
    float wind = 0.55f;
    float gustRate = 0.20f;
    float gustDepth = 0.48f;
    float turbulence = 0.36f;
    float flutter = 0.28f;
    float material = 0.34f;
    float air = 0.42f;
    float hiss = 0.34f;
    float spread = 0.40f;
    float deviation = 0.16f;
    uint32_t gustShape = 2;
    float vectorRateHz = 0.024f;
    uint32_t materialMode = 0;
    uint32_t gustEdge = 0;
    float center = 0.38f;
    float sweep = 0.48f;
    float q = 0.42f;
    float shrill = 0.24f;
    float body = 0.52f;
    float breath = 0.36f;
    float grit = 0.18f;
    float field = 0.70f;
    float motionRateHz = 0.024f;
    float motionFlow = 0.74f;
    float motionShear = 0.64f;
    float motionCurl = 1.18f;
    float motionUpdraft = 0.0f;
    float centerAzimuthDeg = 0.0f;
    float centerElevationDeg = 0.0f;
    float centerDistance = 1.0f;
    float spatialFollow = 0.90f;
    float outputGainDb = -6.0f;
};

struct AmbiWindPoint {
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float distance = 1.0f;
};

struct AmbiWindSvf {
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
        const float hz = clamp(cutoffHz, 6.0f, sr * 0.45f);
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

struct AmbiWindVoice {
    uint32_t seed = 1u;
    float gustPhase = 0.0f;
    float flutterPhase = 0.0f;
    float eddyPhase = 0.0f;
    float objectPhase = 0.0f;
    float objectEnv = 0.0f;
    float objectHz = 440.0f;
    float gustTimer = 0.0f;
    float gustTarget = 0.0f;
    float gust = 0.0f;
    float gustSlow = 0.0f;
    float lastGust = 0.0f;
    float whiteZ = 0.0f;
    float pinkA = 0.0f;
    float pinkB = 0.0f;
    float pinkC = 0.0f;
    float lowWind = 0.0f;
    float airZ = 0.0f;
    float crackle = 0.0f;
    float motionEnergy = 0.0f;
    float energy = 0.0f;
    float gustViz = 0.0f;
    AmbiWindSvf toneFilter {};
    AmbiWindSvf airFilter {};
    AmbiWindSvf bodyFilter {};
    AmbiWindSvf objectFilter {};
};

class AmbiWindEncoder {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        reset();
        setParams(params_);
    }

    void reset()
    {
        motionPhase_ = 0.0f;
        vectorPhase_ = 0.0f;
        transitionFade_ = 1.0f;
        transitionRequested_.store(false, std::memory_order_relaxed);
        lastOutput_.fill(0.0f);
        transitionTail_.fill(0.0f);
        smoothedOutputGain_ = normalizedOutputGain(params_);
        smoothParams_ = params_;
        setMaterialWeights(params_.materialMode);
        smoothReady_ = true;
        for (uint32_t i = 0u; i < kAmbiWindMaxVoices; ++i) {
            voices_[i] = {};
            voices_[i].seed = 0x51f15eedu + i * 0x9e3779b9u;
            initializeVoice(i);
            points_[i] = basePoint(i, std::max<uint32_t>(1u, params_.voices));
            targetPoints_[i] = points_[i];
        }
    }

    void setParams(AmbiWindParams params)
    {
        params.order = std::clamp<uint32_t>(params.order, 1u, kAmbiWindMaxOrder);
        params.voices = std::clamp<uint32_t>(params.voices, 1u, kAmbiWindMaxVoices);
        params.wind = clampFinite(params.wind, params_.wind, 0.0f, 1.0f);
        params.gustRate = clampFinite(params.gustRate, params_.gustRate, 0.0f, 1.0f);
        params.gustDepth = clampFinite(params.gustDepth, params_.gustDepth, 0.0f, 1.0f);
        params.turbulence = clampFinite(params.turbulence, params_.turbulence, 0.0f, 1.0f);
        params.flutter = clampFinite(params.flutter, params_.flutter, 0.0f, 1.0f);
        params.material = clampFinite(params.material, params_.material, 0.0f, 1.0f);
        params.air = clampFinite(params.air, params_.air, 0.0f, 1.0f);
        params.hiss = clampFinite(params.hiss, params_.hiss, 0.0f, 1.0f);
        params.spread = clampFinite(params.spread, params_.spread, 0.0f, 1.0f);
        params.deviation = clampFinite(params.deviation, params_.deviation, 0.0f, 1.0f);
        params.gustShape = std::clamp<uint32_t>(params.gustShape, 0u, 5u);
        params.vectorRateHz = clampFinite(params.vectorRateHz, params_.vectorRateHz, 0.0f, 0.5f);
        params.materialMode = std::clamp<uint32_t>(params.materialMode, 0u, 9u);
        params.gustEdge = std::clamp<uint32_t>(params.gustEdge, 0u, 2u);
        params.center = clampFinite(params.center, params_.center, 0.0f, 1.0f);
        params.sweep = clampFinite(params.sweep, params_.sweep, 0.0f, 1.0f);
        params.q = clampFinite(params.q, params_.q, 0.0f, 1.0f);
        params.shrill = clampFinite(params.shrill, params_.shrill, 0.0f, 1.0f);
        params.body = clampFinite(params.body, params_.body, 0.0f, 1.0f);
        params.breath = clampFinite(params.breath, params_.breath, 0.0f, 1.0f);
        params.grit = clampFinite(params.grit, params_.grit, 0.0f, 1.0f);
        params.field = clampFinite(params.field, params_.field, 0.0f, 1.0f);
        params.motionRateHz = clampFinite(params.motionRateHz, params_.motionRateHz, 0.001f, 2.0f);
        params.motionFlow = clampFinite(params.motionFlow, params_.motionFlow, 0.0f, 1.0f);
        params.motionShear = clampFinite(params.motionShear, params_.motionShear, 0.0f, 1.0f);
        params.motionCurl = clampFinite(params.motionCurl, params_.motionCurl, 0.0f, 1.0f);
        params.motionUpdraft = clampFinite(params.motionUpdraft, params_.motionUpdraft, 0.0f, 1.0f);
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

    AmbiWindParams params() const { return params_; }
    void beginTransition() { transitionRequested_.store(true, std::memory_order_release); }
    float voiceEnergy(uint32_t voice) const { return voices_[std::min<uint32_t>(voice, kAmbiWindMaxVoices - 1u)].energy; }
    AmbiWindPoint voicePoint(uint32_t voice) const { return points_[std::min<uint32_t>(voice, kAmbiWindMaxVoices - 1u)]; }
    float voiceGustLevel(uint32_t voice) const { return voices_[std::min<uint32_t>(voice, kAmbiWindMaxVoices - 1u)].gustViz; }

    void process(float* const* outputs, uint32_t outputChannels, uint32_t frames)
    {
        if (!outputs || frames == 0u) return;
        if (transitionRequested_.exchange(false, std::memory_order_acq_rel)) startTransition();
        outputChannels = std::min<uint32_t>(outputChannels, kAmbiWindMaxChannels);
        for (uint32_t ch = 0u; ch < outputChannels; ++ch) {
            if (outputs[ch]) std::fill(outputs[ch], outputs[ch] + frames, 0.0f);
        }

        const uint32_t voices = params_.voices;
        const uint32_t ambiChannels = std::min<uint32_t>(ambiChannelsForOrder(params_.order), outputChannels);
        const float voiceNorm = std::pow(static_cast<float>(std::max<uint32_t>(1u, voices)), 0.43f);
        constexpr uint32_t kControlFrames = 16u;

        for (uint32_t chunkStart = 0u; chunkStart < frames; chunkStart += kControlFrames) {
            const uint32_t chunkFrames = std::min<uint32_t>(kControlFrames, frames - chunkStart);
            const float chunkDt = static_cast<float>(chunkFrames) / static_cast<float>(sampleRate_);
            updateSmoothedParams(chunkDt);
            updateMotion(chunkDt);
            const float targetGain = dbToGain(smoothParams_.outputGainDb) * 1.30f / voiceNorm;

            std::array<std::array<float, kAmbiWindMaxChannels>, kAmbiWindMaxVoices> basis {};
            std::array<float, kAmbiWindMaxVoices> distGain {};
            for (uint32_t v = 0u; v < voices; ++v) {
                basis[v] = acnSn3dBasis7(directionFromAed(points_[v].azimuthDeg, points_[v].elevationDeg));
                distGain[v] = 1.0f / std::max(0.44f, points_[v].distance);
            }

            for (uint32_t frame = chunkStart; frame < chunkStart + chunkFrames; ++frame) {
                smoothedOutputGain_ += (targetGain - smoothedOutputGain_) * 0.0014f;
                for (uint32_t v = 0u; v < voices; ++v) {
                    float sample = processVoice(v) * smoothedOutputGain_ * distGain[v];
                    if (!std::isfinite(sample)) {
                        initializeVoice(v);
                        sample = 0.0f;
                    }
                    voices_[v].energy += (sample * sample - voices_[v].energy) * 0.0008f;
                    if (std::fabs(sample) < 0.0000001f) continue;
                    for (uint32_t ch = 0u; ch < ambiChannels; ++ch) {
                        if (outputs[ch]) outputs[ch][frame] = flushDenormal(outputs[ch][frame] + sample * basis[v][ch]);
                    }
                }
            }
        }

        const float transitionStep = 1.0f / std::max(1.0f, static_cast<float>(sampleRate_) * 0.025f);
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            const float mix = transitionFade_;
            for (uint32_t ch = 0u; ch < outputChannels; ++ch) {
                const float summed = (ch < ambiChannels && outputs[ch]) ? outputs[ch][frame] : 0.0f;
                float fresh = std::tanh(clamp(summed * 1.08f, -4.0f, 4.0f));
                if (!std::isfinite(fresh)) fresh = 0.0f;
                const float value = transitionTail_[ch] * (1.0f - mix) + fresh * mix;
                if (outputs[ch]) outputs[ch][frame] = value;
                lastOutput_[ch] = value;
            }
            transitionFade_ = std::min(1.0f, transitionFade_ + transitionStep);
        }
    }

private:
    static float clampFinite(float value, float fallback, float low, float high)
    {
        return clamp(std::isfinite(value) ? value : fallback, low, high);
    }

    static float normalizedOutputGain(const AmbiWindParams& params)
    {
        const float voiceNorm = std::pow(static_cast<float>(std::max<uint32_t>(1u, params.voices)), 0.43f);
        return dbToGain(params.outputGainDb) * 1.30f / voiceNorm;
    }

    static uint32_t hash(uint32_t x)
    {
        x ^= x >> 16u;
        x *= 0x7feb352du;
        x ^= x >> 15u;
        x *= 0x846ca68bu;
        x ^= x >> 16u;
        return x;
    }

    static float hash01(uint32_t x)
    {
        return static_cast<float>(hash(x) & 0xffffu) / 65535.0f;
    }

    static float hashSigned(uint32_t x) { return hash01(x) * 2.0f - 1.0f; }

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

    static float smoothAngleDeg(float current, float target, float coeff)
    {
        return wrapSignedDeg(current + wrapSignedDeg(target - current) * coeff);
    }

    static float freqFromNorm(float value, float lowHz, float highHz)
    {
        value = clamp(value, 0.0f, 1.0f);
        return lowHz * std::pow(highHz / lowHz, value);
    }

    static AmbiWindPoint basePoint(uint32_t voice, uint32_t voices)
    {
        const float v = static_cast<float>(voice);
        const float n = static_cast<float>(std::max<uint32_t>(1u, voices));
        const float golden = 0.61803398875f;
        const float z = 1.0f - 2.0f * ((v + 0.5f) / n);
        const float radius = std::sqrt(std::max(0.0f, 1.0f - z * z));
        const float az = (std::fmod(v * golden, 1.0f) * 360.0f) - 180.0f;
        const float el = std::asin(clamp(z, -1.0f, 1.0f)) * 180.0f / kPi;
        return { az, el, 0.82f + radius * 0.30f };
    }

    static AmbiWindPoint pointFromVec(Vec3 v)
    {
        const float dist = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (dist <= 0.000001f) return {};
        const float az = std::atan2(v.y, v.x) * 180.0f / kPi;
        const float el = std::asin(clamp(v.z / dist, -1.0f, 1.0f)) * 180.0f / kPi;
        return { wrapSignedDeg(az), el, dist };
    }

    static float shapeCurve(float value, uint32_t shape)
    {
        const float s = clamp(value, 0.0f, 1.0f);
        switch (shape) {
        case 0u: return s;
        case 1u: return s * s * (3.0f - 2.0f * s);
        case 2u: return 1.0f - std::pow(1.0f - s, 2.8f);
        case 3u: return std::pow(s, 2.3f);
        case 4u: return s > 0.62f ? 1.0f : s * 0.30f;
        default: return s > 0.50f ? 0.95f : s * 0.40f;
        }
    }

    void setMaterialWeights(uint32_t mode)
    {
        materialWeights_.fill(0.0f);
        materialWeights_[std::min<uint32_t>(mode, 9u)] = 1.0f;
    }

    float noise(AmbiWindVoice& voice)
    {
        voice.seed = hash(voice.seed + 0x6d2b79f5u);
        return static_cast<float>(static_cast<int32_t>(voice.seed)) / 2147483648.0f;
    }

    void initializeVoice(uint32_t index)
    {
        auto& voice = voices_[index];
        const uint32_t seed = voice.seed == 0u ? 0x51f15eedu + index * 0x9e3779b9u : voice.seed;
        voice = {};
        voice.seed = seed;
        const float h = hash01(voice.seed);
        voice.gustPhase = h;
        voice.flutterPhase = hash01(voice.seed + 17u);
        voice.eddyPhase = hash01(voice.seed + 31u);
        voice.objectPhase = hash01(voice.seed + 43u);
        voice.objectHz = 180.0f + hash01(voice.seed + 59u) * 1200.0f;
        voice.gustTimer = 0.1f + hash01(voice.seed + 71u) * 0.9f;
        voice.gustTarget = h;
        voice.gust = h;
        voice.gustSlow = h;
        voice.lastGust = h;
        voice.objectEnv = 0.0f;
        voice.motionEnergy = 0.0f;
        voice.gustViz = h;
    }

    bool voiceHealthy(const AmbiWindVoice& voice) const
    {
        const bool finite = std::isfinite(voice.gustPhase) && std::isfinite(voice.flutterPhase)
            && std::isfinite(voice.eddyPhase) && std::isfinite(voice.objectPhase)
            && std::isfinite(voice.objectEnv) && std::isfinite(voice.objectHz)
            && std::isfinite(voice.gustTimer) && std::isfinite(voice.gustTarget)
            && std::isfinite(voice.gust) && std::isfinite(voice.gustSlow)
            && std::isfinite(voice.lastGust) && std::isfinite(voice.whiteZ)
            && std::isfinite(voice.pinkA) && std::isfinite(voice.pinkB)
            && std::isfinite(voice.pinkC) && std::isfinite(voice.lowWind)
            && std::isfinite(voice.airZ) && std::isfinite(voice.crackle)
            && std::isfinite(voice.motionEnergy) && std::isfinite(voice.energy)
            && std::isfinite(voice.gustViz);
        return finite && voice.toneFilter.healthy() && voice.airFilter.healthy()
            && voice.bodyFilter.healthy() && voice.objectFilter.healthy();
    }

    void startTransition()
    {
        transitionTail_ = lastOutput_;
        transitionFade_ = 0.0f;
        smoothParams_ = params_;
        setMaterialWeights(params_.materialMode);
        smoothedOutputGain_ = normalizedOutputGain(params_);
        for (uint32_t i = 0u; i < kAmbiWindMaxVoices; ++i) initializeVoice(i);
    }

    void updateSmoothedParams(float dt)
    {
        if (!smoothReady_) {
            smoothParams_ = params_;
            setMaterialWeights(params_.materialMode);
            smoothReady_ = true;
            return;
        }

        const float coeff = 1.0f - std::exp(-dt * 22.0f);
        smoothParams_.order = params_.order;
        smoothParams_.voices = params_.voices;
        smoothParams_.gustShape = params_.gustShape;
        smoothParams_.gustEdge = params_.gustEdge;
        smoothParams_.materialMode = params_.materialMode;
        smoothParams_.wind = smoothToward(smoothParams_.wind, params_.wind, coeff);
        smoothParams_.gustRate = smoothToward(smoothParams_.gustRate, params_.gustRate, coeff);
        smoothParams_.gustDepth = smoothToward(smoothParams_.gustDepth, params_.gustDepth, coeff);
        smoothParams_.turbulence = smoothToward(smoothParams_.turbulence, params_.turbulence, coeff);
        smoothParams_.flutter = smoothToward(smoothParams_.flutter, params_.flutter, coeff);
        smoothParams_.material = smoothToward(smoothParams_.material, params_.material, coeff);
        smoothParams_.air = smoothToward(smoothParams_.air, params_.air, coeff);
        smoothParams_.hiss = smoothToward(smoothParams_.hiss, params_.hiss, coeff);
        smoothParams_.spread = smoothToward(smoothParams_.spread, params_.spread, coeff);
        smoothParams_.deviation = smoothToward(smoothParams_.deviation, params_.deviation, coeff);
        smoothParams_.vectorRateHz = smoothToward(smoothParams_.vectorRateHz, params_.vectorRateHz, coeff);
        smoothParams_.center = smoothToward(smoothParams_.center, params_.center, coeff);
        smoothParams_.sweep = smoothToward(smoothParams_.sweep, params_.sweep, coeff);
        smoothParams_.q = smoothToward(smoothParams_.q, params_.q, coeff);
        smoothParams_.shrill = smoothToward(smoothParams_.shrill, params_.shrill, coeff);
        smoothParams_.body = smoothToward(smoothParams_.body, params_.body, coeff);
        smoothParams_.breath = smoothToward(smoothParams_.breath, params_.breath, coeff);
        smoothParams_.grit = smoothToward(smoothParams_.grit, params_.grit, coeff);
        smoothParams_.field = smoothToward(smoothParams_.field, params_.field, coeff);
        smoothParams_.motionRateHz = smoothToward(smoothParams_.motionRateHz, params_.motionRateHz, coeff);
        smoothParams_.motionFlow = smoothToward(smoothParams_.motionFlow, params_.motionFlow, coeff);
        smoothParams_.motionShear = smoothToward(smoothParams_.motionShear, params_.motionShear, coeff);
        smoothParams_.motionCurl = smoothToward(smoothParams_.motionCurl, params_.motionCurl, coeff);
        smoothParams_.motionUpdraft = smoothToward(smoothParams_.motionUpdraft, params_.motionUpdraft, coeff);
        smoothParams_.centerAzimuthDeg = smoothAngleDeg(smoothParams_.centerAzimuthDeg, params_.centerAzimuthDeg, coeff);
        smoothParams_.centerElevationDeg = smoothToward(smoothParams_.centerElevationDeg, params_.centerElevationDeg, coeff);
        smoothParams_.centerDistance = smoothToward(smoothParams_.centerDistance, params_.centerDistance, coeff);
        smoothParams_.spatialFollow = smoothToward(smoothParams_.spatialFollow, params_.spatialFollow, coeff);
        smoothParams_.outputGainDb = smoothToward(smoothParams_.outputGainDb, params_.outputGainDb, coeff);
        const float materialCoeff = 1.0f - std::exp(-dt * 12.0f);
        for (uint32_t mode = 0u; mode < materialWeights_.size(); ++mode) {
            const float target = mode == params_.materialMode ? 1.0f : 0.0f;
            materialWeights_[mode] = smoothToward(materialWeights_[mode], target, materialCoeff);
        }
    }

    void updateMotion(float dt)
    {
        const auto& p = smoothParams_;
        motionPhase_ += dt * p.motionRateHz;
        vectorPhase_ += dt * p.vectorRateHz;
        if (motionPhase_ > 100000.0f) motionPhase_ -= 100000.0f;
        if (vectorPhase_ > 100000.0f) vectorPhase_ -= 100000.0f;

        const float flowPhase = motionPhase_ * kPi * 2.0f;
        const float vectorPhase = vectorPhase_ * kPi * 2.0f;
        const float vectorAz = std::sin(vectorPhase) * p.motionFlow * 145.0f;
        const float vectorEl = std::sin(vectorPhase * 0.71f + 0.9f) * p.motionUpdraft * 58.0f;
        const float vectorDistance = 1.0f + std::sin(vectorPhase * 0.43f + 0.5f) * p.field * 0.42f;
        const float flowAz = wrapSignedDeg(p.centerAzimuthDeg + vectorAz + std::sin(flowPhase * 0.17f) * p.motionFlow * 70.0f);
        const Vec3 flowDir = directionFromAed(flowAz, p.centerElevationDeg * 0.25f);
        const Vec3 sideDir { -flowDir.y, flowDir.x, 0.0f };
        const uint32_t voices = p.voices;

        for (uint32_t i = 0u; i < voices; ++i) {
            const float lane = static_cast<float>(i) / static_cast<float>(std::max<uint32_t>(1u, voices - 1u));
            const float seedA = hash01(voices_[i].seed + 1009u);
            const float seedB = hash01(voices_[i].seed + 2027u);
            const auto base = basePoint(i, voices);
            const float cell = motionPhase_ + lane * (0.80f + p.field * 3.8f) + seedA * 0.21f;
            const float wander = std::sin((cell * 0.63f + seedA) * kPi * 2.0f);
            const float shear = std::sin((cell * 0.41f + seedB) * kPi * 2.0f);
            const float curl = std::sin((cell * 1.37f + seedA * 0.7f) * kPi * 2.0f);
            const float gust = voices_[i].gustViz;
            const float tornado = std::max(0.0f, (p.motionCurl - 0.52f) / 0.48f);
            const float vortexPhase = flowPhase * (0.95f + p.motionRateHz * 5.2f) + lane * kPi * 9.0f + seedB * kPi * 2.0f;
            const Vec3 baseDir = directionFromAed(base.azimuthDeg + p.centerAzimuthDeg + vectorAz * 0.32f,
                clamp(base.elevationDeg + p.centerElevationDeg + vectorEl, -88.0f, 88.0f));

            const float radius = (0.08f + p.field * 0.88f) * tornado * (0.35f + gust * 0.95f);
            Vec3 pos {
                baseDir.x * base.distance * p.centerDistance * vectorDistance,
                baseDir.y * base.distance * p.centerDistance * vectorDistance,
                baseDir.z * base.distance * p.centerDistance * vectorDistance
            };
            pos.x += flowDir.x * (p.motionFlow * (0.20f + gust * 0.55f)) + sideDir.x * (wander * p.motionShear * 0.52f);
            pos.y += flowDir.y * (p.motionFlow * (0.20f + gust * 0.55f)) + sideDir.y * (wander * p.motionShear * 0.52f);
            pos.z += shear * p.motionShear * 0.35f + curl * p.motionCurl * 0.30f + gust * p.motionUpdraft * 0.60f;
            pos.x += std::cos(vortexPhase) * radius;
            pos.y += std::sin(vortexPhase) * radius;
            pos.z += tornado * p.motionUpdraft * (0.24f + gust * 1.0f) - tornado * std::fabs(lane - 0.5f) * 0.22f;

            targetPoints_[i] = pointFromVec(pos);
            targetPoints_[i].distance = clamp(targetPoints_[i].distance, 0.20f, 2.60f);
            targetPoints_[i].elevationDeg = clamp(targetPoints_[i].elevationDeg, -90.0f, 90.0f);

            const float azDelta = std::fabs(wrapSignedDeg(targetPoints_[i].azimuthDeg - points_[i].azimuthDeg)) / 180.0f;
            const float elDelta = std::fabs(targetPoints_[i].elevationDeg - points_[i].elevationDeg) / 90.0f;
            const float distDelta = std::fabs(targetPoints_[i].distance - points_[i].distance);
            const float motionExcitation = clamp((azDelta * 0.55f + elDelta * 0.36f + distDelta * 0.68f)
                    * (0.55f + p.motionFlow * 0.70f + p.motionShear * 0.50f + p.motionCurl * 0.75f + p.motionUpdraft * 0.45f),
                0.0f, 1.0f);
            voices_[i].motionEnergy += (motionExcitation - voices_[i].motionEnergy) * (1.0f - std::exp(-dt * 20.0f));

            const float follow = 1.0f - std::exp(-dt * (0.45f + (1.0f - p.spatialFollow) * 8.8f));
            points_[i].azimuthDeg = wrapSignedDeg(points_[i].azimuthDeg + wrapSignedDeg(targetPoints_[i].azimuthDeg - points_[i].azimuthDeg) * follow);
            points_[i].elevationDeg += (targetPoints_[i].elevationDeg - points_[i].elevationDeg) * follow;
            points_[i].distance += (targetPoints_[i].distance - points_[i].distance) * follow;
        }
    }

    float processVoice(uint32_t index)
    {
        const auto& p = smoothParams_;
        auto& voice = voices_[index];
        if (!voiceHealthy(voice)) initializeVoice(index);
        const uint32_t voices = std::max<uint32_t>(1u, p.voices);
        const float lane = static_cast<float>(index) / static_cast<float>(std::max<uint32_t>(1u, voices - 1u));
        const float rnd = hashSigned(voice.seed + index * 97u);
        const float laneSkew = (lane - 0.5f) * p.spread + rnd * p.deviation;

        const float wind = clamp(p.wind + laneSkew * 0.24f, 0.0f, 1.0f);
        const float gustDepth = clamp(p.gustDepth + laneSkew * 0.22f, 0.0f, 1.0f);
        const float turbulence = clamp(p.turbulence + std::fabs(rnd) * p.deviation * 0.48f, 0.0f, 1.0f);
        const float flutter = clamp(p.flutter + hashSigned(voice.seed + 307u) * p.deviation * 0.38f, 0.0f, 1.0f);
        const float material = clamp(p.material + laneSkew * 0.34f, 0.0f, 1.0f);
        const float air = clamp(p.air + hashSigned(voice.seed + 911u) * p.deviation * 0.32f, 0.0f, 1.0f);
        const float hiss = clamp(p.hiss + std::fabs(rnd) * p.deviation * 0.42f, 0.0f, 1.0f);
        const float center = clamp(p.center + laneSkew * 0.30f, 0.0f, 1.0f);
        const float sweep = clamp(p.sweep + laneSkew * 0.24f, 0.0f, 1.0f);
        const float q = clamp(p.q + std::fabs(laneSkew) * 0.24f, 0.0f, 1.0f);
        const float body = clamp(p.body - laneSkew * 0.22f, 0.0f, 1.0f);
        const float motion = clamp(voice.motionEnergy + p.field * 0.12f, 0.0f, 1.0f);
        const float dt = 1.0f / static_cast<float>(sampleRate_);

        const float gustHz = freqFromNorm(p.gustRate, 0.004f, 5.5f);
        const float laneRate = gustHz * (0.38f + lane * 0.72f + std::fabs(rnd) * 0.52f);
        voice.gustPhase += laneRate * dt;
        voice.gustPhase -= std::floor(voice.gustPhase);
        voice.eddyPhase += laneRate * (1.4f + turbulence * 5.0f + p.field * 1.2f) * dt;
        voice.eddyPhase -= std::floor(voice.eddyPhase);
        voice.flutterPhase += (2.5f + flutter * 70.0f + turbulence * 24.0f) * dt;
        voice.flutterPhase -= std::floor(voice.flutterPhase);

        voice.gustTimer -= laneRate * (0.65f + turbulence * 1.55f) * dt;
        if (voice.gustTimer <= 0.0f) {
            voice.gustTimer += 0.10f + hash01(voice.seed += 0x9e3779b9u) * (0.90f - turbulence * 0.42f);
            voice.gustTarget = shapeCurve(hash01(voice.seed + 17u), p.gustShape);
        }

        const float gustWave = 0.5f + 0.5f * std::sin((voice.gustPhase + hash01(voice.seed + 5u)) * kPi * 2.0f);
        const float eddy = 0.5f + 0.5f * std::sin((voice.eddyPhase + lane * 0.37f) * kPi * 2.0f);
        float rawGust = shapeCurve(voice.gustTarget * 0.34f + gustWave * (0.38f + gustDepth * 0.16f) + eddy * turbulence * 0.26f + motion * 0.22f, p.gustShape);
        if (p.gustEdge == 1u) rawGust = rawGust * rawGust * (3.0f - 2.0f * rawGust);
        else if (p.gustEdge == 2u) rawGust = rawGust > 0.67f ? 1.0f : rawGust * 0.28f;

        const float gustCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(sampleRate_) * (0.005f + p.breath * 0.55f)));
        voice.gust += (rawGust - voice.gust) * gustCoeff;
        voice.gustSlow += (voice.gust - voice.gustSlow) * (gustCoeff * 0.28f);
        const float gust = lerp(voice.gustSlow, voice.gust, turbulence);
        voice.gustViz += (gust - voice.gustViz) * (gustCoeff * 0.22f);

        const float white = noise(voice);
        voice.whiteZ += (white - voice.whiteZ) * (0.06f + turbulence * 0.24f);
        voice.pinkA += (white - voice.pinkA) * (0.014f + hiss * 0.045f);
        voice.pinkB += (voice.pinkA - voice.pinkB) * (0.0022f + air * 0.0075f);
        voice.pinkC += (voice.pinkB - voice.pinkC) * (0.00038f + body * 0.0022f);
        voice.lowWind += (white - voice.lowWind) * (0.00010f + wind * 0.0058f + gust * 0.0038f + motion * 0.0028f);
        voice.airZ += (white - voice.airZ) * (0.035f + air * 0.36f + hiss * 0.14f);
        const float rough = white - voice.pinkA;
        const float airy = white - voice.airZ;
        const float pink = voice.pinkA * (0.30f + body * 0.16f)
            + voice.pinkB * (0.25f + body * 0.22f)
            + voice.pinkC * (0.12f + body * 0.26f)
            + white * (0.08f + hiss * 0.20f);

        const float flutterWave = std::sin((voice.flutterPhase + lane * 0.25f) * kPi * 2.0f);
        const float pressure = 0.030f + wind * (0.22f + gustDepth * 0.32f) + gust * gustDepth * 1.10f + motion * p.field * 0.48f;
        const float flutterAmp = 1.0f + flutterWave * flutter * (0.10f + turbulence * 0.22f + material * 0.12f);
        const float crackChance = turbulence * p.grit * (0.00030f + gust * 0.00075f + motion * 0.00090f);
        if (hash01(voice.seed + 131u) < crackChance) voice.crackle = 1.0f;
        voice.crackle *= clamp(0.997f - p.grit * 0.038f - turbulence * 0.012f, 0.93f, 0.9992f);
        const float grain = voice.crackle * rough * (0.06f + p.grit * 0.88f);

        const float leaf = materialWeights_[1];
        const float hollow = materialWeights_[2];
        const float wire = materialWeights_[3];
        const float metal = materialWeights_[4];
        const float chimes = materialWeights_[5];
        const float blocks = materialWeights_[6];
        const float harp = materialWeights_[7];
        const float reeds = materialWeights_[8];
        const float fabric = materialWeights_[9];
        const float modeLift = leaf * 0.16f + hollow * -0.26f + wire * 0.34f + metal * 0.50f
            + chimes * 0.46f + blocks * -0.10f + harp * 0.31f + reeds * 0.13f + fabric * -0.20f;

        const float filterNorm = clamp(center + modeLift + (gust - 0.5f) * sweep * (0.36f + sweep * 0.52f)
                + flutterWave * flutter * sweep * 0.18f + motion * p.field * 0.26f + (lane - 0.5f) * p.spread * 0.44f,
            0.0f, 1.0f);
        const float centerHz = freqFromNorm(filterNorm, 22.0f, 15500.0f);
        const float resonance = clamp(q * 0.78f + p.shrill * 0.34f + material * 0.16f, 0.0f, 0.92f);
        const float excitation = (pink * (0.30f + body * 0.62f)
            + rough * (hiss * 0.62f + turbulence * 0.30f + air * 0.20f)
            + voice.lowWind * (body * 0.80f + wind * 0.24f)
            + grain) * pressure * flutterAmp;
        const float toneBand = voice.toneFilter.process(excitation, centerHz, resonance, static_cast<float>(sampleRate_));
        const float airBand = voice.airFilter.process(airy * (0.045f + air * 0.30f + hiss * 0.70f),
            centerHz * (1.45f + air * 7.8f + p.shrill * 1.2f), 0.02f + p.shrill * 0.25f, static_cast<float>(sampleRate_));
        const float bodyBand = voice.bodyFilter.process(pink + voice.lowWind * 2.8f,
            centerHz * (0.050f + body * 0.55f), 0.06f + body * 0.66f, static_cast<float>(sampleRate_));

        const float gustRise = std::max(0.0f, gust - voice.lastGust);
        voice.lastGust += (gust - voice.lastGust) * 0.006f;
        const float strikeMode = chimes + blocks;
        const float strikeChance = strikeMode * material * (0.0045f + gustRise * 0.58f + turbulence * 0.0026f + motion * 0.012f);
        if (hash01(voice.seed + 271u) < strikeChance) {
            const float pitchSeed = hash01(voice.seed += 0x7f4a7c15u);
            const float chimeHz = 580.0f + pitchSeed * pitchSeed * 5600.0f + lane * 420.0f;
            const float blockHz = 120.0f + pitchSeed * 980.0f + body * 320.0f;
            voice.objectHz = lerp(blockHz, chimeHz, chimes / std::max(0.0001f, strikeMode));
            voice.objectEnv = std::max(voice.objectEnv, 0.34f + gust * 0.66f);
        }
        voice.objectPhase += voice.objectHz * dt;
        voice.objectPhase -= std::floor(voice.objectPhase);
        voice.objectEnv *= clamp(1.0f - (0.00065f + chimes * 0.0010f + blocks * 0.0078f + p.grit * 0.0024f), 0.955f, 0.9995f);
        const float struckTone = (std::sin(voice.objectPhase * kPi * 2.0f)
            + std::sin((voice.objectPhase * 2.68f + 0.13f) * kPi * 2.0f) * (0.14f + chimes * 0.36f)
            + rough * blocks * 0.42f) * voice.objectEnv;
        const float harpTone = std::sin((voice.eddyPhase * (2.0f + sweep * 7.0f) + lane * 0.19f) * kPi * 2.0f)
            * (0.12f + gust * 0.42f + motion * 0.20f) * harp;
        const float reedTone = std::tanh((rough * 3.0f + flutterWave * 0.85f + voice.lowWind * 0.90f) * (0.9f + q * 2.3f))
            * reeds * (0.08f + gust * 0.28f + turbulence * 0.12f);
        const float fabricTone = (voice.lowWind * 1.45f + rough * 0.34f + grain * 0.60f) * fabric * (0.12f + gust * 0.38f) * (0.50f + body);
        const float ringTone = std::sin((voice.eddyPhase * (0.8f + material * 9.0f) + lane * 0.5f) * kPi * 2.0f)
            * material * (0.026f + wire * 0.052f + metal * 0.072f + leaf * 0.018f) * (gust + motion * 0.50f);
        const float objectTone = voice.objectFilter.process(struckTone * (chimes * 0.135f + blocks * 0.170f)
                + harpTone * 0.140f + reedTone * 0.150f + fabricTone * 0.190f + ringTone,
            centerHz * (0.55f + material * 1.6f), clamp(0.18f + q * 0.58f + material * 0.18f, 0.0f, 0.92f), static_cast<float>(sampleRate_)) * material;

        const float mixed = toneBand * (0.32f + material * 0.30f + q * 0.10f)
            + airBand * (0.030f + air * 0.44f + hiss * 0.20f)
            + bodyBand * (0.050f + body * 0.52f)
            + objectTone;
        const float drive = 0.82f + p.grit * 3.3f + turbulence * 1.0f + p.shrill * 0.38f;
        const float output = std::tanh(clamp(mixed * drive, -3.8f, 3.8f)) * (0.14f + wind * 0.82f + gustDepth * 0.14f);
        if (!std::isfinite(output)) {
            initializeVoice(index);
            return 0.0f;
        }
        return output;
    }

    AmbiWindParams params_ {};
    AmbiWindParams smoothParams_ {};
    std::array<AmbiWindVoice, kAmbiWindMaxVoices> voices_ {};
    std::array<AmbiWindPoint, kAmbiWindMaxVoices> points_ {};
    std::array<AmbiWindPoint, kAmbiWindMaxVoices> targetPoints_ {};
    double sampleRate_ = 48000.0;
    float motionPhase_ = 0.0f;
    float vectorPhase_ = 0.0f;
    float transitionFade_ = 1.0f;
    float smoothedOutputGain_ = dbToGain(-6.0f);
    std::array<float, 10> materialWeights_ { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    bool smoothReady_ = false;
    std::array<float, kAmbiWindMaxChannels> lastOutput_ {};
    std::array<float, kAmbiWindMaxChannels> transitionTail_ {};
    std::atomic<bool> transitionRequested_ { false };
};

} // namespace s3g
