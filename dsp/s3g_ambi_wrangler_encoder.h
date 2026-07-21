#pragma once

#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_realtime.h"
#include "s3g_topology.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAmbiWranglerMaxVoices = 64;
constexpr uint32_t kAmbiWranglerMaxOrder = 7;
constexpr uint32_t kAmbiWranglerMaxChannels = 64;

struct AmbiWranglerParams {
    uint32_t order = 3;
    uint32_t voices = 16;
    float rateA = 0.28f;
    float rateB = 0.34f;
    float fmAtoB = 0.18f;
    float fmBtoA = 0.14f;
    float runglerA = 0.25f;
    float runglerB = 0.32f;
    float pwmA = 0.50f;
    float pwmB = 0.50f;
    float rampA = 0.50f;
    float rampB = 0.50f;
    uint32_t inputA = 0;
    uint32_t inputB = 0;
    float spread = 0.26f;
    float deviation = 0.12f;
    uint32_t rungSize = 4;
    uint32_t rateModeA = 0;
    uint32_t rateModeB = 0;
    float threshold = 0.50f;
    float color = 0.42f;
    float filter = 0.36f;
    float resonance = 0.20f;
    float filterRun = 0.28f;
    float filterSweep = 0.20f;
    float saturation = 0.36f;
    float field = 0.62f;
    uint32_t maskMode = 2;
    float maskDepth = 0.55f;
    float maskRateHz = 0.070f;
    bool voiceBreakpointsEnabled = false;
    std::array<float, kAmbiWranglerMaxVoices> bpRateA {};
    std::array<float, kAmbiWranglerMaxVoices> bpRateB {};
    std::array<float, kAmbiWranglerMaxVoices> bpFmAtoB {};
    std::array<float, kAmbiWranglerMaxVoices> bpFmBtoA {};
    std::array<float, kAmbiWranglerMaxVoices> bpRunglerA {};
    std::array<float, kAmbiWranglerMaxVoices> bpRunglerB {};
    std::array<float, kAmbiWranglerMaxVoices> bpFilter {};
    std::array<float, kAmbiWranglerMaxVoices> bpThreshold {};
    std::array<float, kAmbiWranglerMaxVoices> bpPwmA {};
    std::array<float, kAmbiWranglerMaxVoices> bpPwmB {};
    std::array<float, kAmbiWranglerMaxVoices> bpRampA {};
    std::array<float, kAmbiWranglerMaxVoices> bpRampB {};
    std::array<float, kAmbiWranglerMaxVoices> bpAmp {};
    uint32_t topologyShape = 11;
    uint32_t topologyMotion = 1;
    float topologyRateHz = 0.032f;
    float topologyAmount = 0.80f;
    float topologyDepth = 0.72f;
    float topologyScale = 1.14f;
    float topologyCollapse = 0.0f;
    float centerAzimuthDeg = 0.0f;
    float centerElevationDeg = 0.0f;
    float centerDistance = 1.0f;
    float spatialFollow = 0.90f;
    float outputGainDb = -6.0f;
};

struct AmbiWranglerPoint {
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float distance = 1.0f;
};

struct AmbiWranglerFilter {
    float low = 0.0f;
    float band = 0.0f;

    float process(float input, float cutoffNorm, float resonance, float drive)
    {
        const float f = clamp(cutoffNorm, 0.0002f, 0.46f);
        const float q = 1.72f - clamp(resonance, 0.0f, 1.0f) * 1.42f;
        const float driven = std::tanh(clamp(input * (1.0f + drive * 5.0f), -8.0f, 8.0f));
        low = flushDenormal(low + f * band);
        const float high = driven - low - q * band;
        band = flushDenormal(band + f * high);
        return std::tanh(clamp(low + band * (0.25f + resonance * 0.55f), -6.0f, 6.0f));
    }
};

struct AmbiWranglerVoice {
    float phaseA = 0.0f;
    float phaseB = 0.0f;
    float rateA = 80.0f;
    float rateB = 91.0f;
    float rungValue = 0.0f;
    float rungSmooth = 0.0f;
    float lastClock = 0.0f;
    float energy = 0.0f;
    uint32_t reg = 1u;
    uint32_t seed = 1u;
    AmbiWranglerFilter filterA {};
    AmbiWranglerFilter filterB {};
};

class AmbiWranglerEncoder {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        reset();
        setParams(params_);
    }

    void reset()
    {
        topologyPhase_ = 0.0f;
        maskPhase_ = 0.0f;
        smoothedOutputGain_ = dbToGain(params_.outputGainDb);
        for (uint32_t i = 0u; i < kAmbiWranglerMaxVoices; ++i) {
            voices_[i] = {};
            voices_[i].seed = 0x9e3779b9u + i * 0x85ebca6bu;
            voices_[i].reg = (hash(voices_[i].seed) & 0xffu) | 1u;
            initializeVoice(i);
            points_[i] = basePoint(i, std::max<uint32_t>(1u, params_.voices));
            targetPoints_[i] = points_[i];
        }
    }

    void setParams(AmbiWranglerParams params)
    {
        params.order = std::clamp<uint32_t>(params.order, 1u, kAmbiWranglerMaxOrder);
        params.voices = std::clamp<uint32_t>(params.voices, 1u, kAmbiWranglerMaxVoices);
        params.rateA = clamp(params.rateA, 0.0f, 1.0f);
        params.rateB = clamp(params.rateB, 0.0f, 1.0f);
        params.fmAtoB = clamp(params.fmAtoB, 0.0f, 1.0f);
        params.fmBtoA = clamp(params.fmBtoA, 0.0f, 1.0f);
        params.runglerA = clamp(params.runglerA, 0.0f, 1.0f);
        params.runglerB = clamp(params.runglerB, 0.0f, 1.0f);
        params.pwmA = clamp(params.pwmA, 0.0f, 1.0f);
        params.pwmB = clamp(params.pwmB, 0.0f, 1.0f);
        params.rampA = clamp(params.rampA, 0.0f, 1.0f);
        params.rampB = clamp(params.rampB, 0.0f, 1.0f);
        params.inputA = std::clamp<uint32_t>(params.inputA, 0u, 1u);
        params.inputB = std::clamp<uint32_t>(params.inputB, 0u, 1u);
        params.spread = clamp(params.spread, 0.0f, 1.0f);
        params.deviation = clamp(params.deviation, 0.0f, 1.0f);
        params.rungSize = std::clamp<uint32_t>(params.rungSize, 2u, 5u);
        params.rateModeA = std::clamp<uint32_t>(params.rateModeA, 0u, 1u);
        params.rateModeB = std::clamp<uint32_t>(params.rateModeB, 0u, 1u);
        params.threshold = clamp(params.threshold, 0.0f, 1.0f);
        params.color = clamp(params.color, 0.0f, 1.0f);
        params.filter = clamp(params.filter, 0.0f, 1.0f);
        params.resonance = clamp(params.resonance, 0.0f, 1.0f);
        params.filterRun = clamp(params.filterRun, 0.0f, 1.0f);
        params.filterSweep = clamp(params.filterSweep, 0.0f, 1.0f);
        params.saturation = clamp(params.saturation, 0.0f, 1.0f);
        params.field = clamp(params.field, 0.0f, 1.0f);
        params.maskMode = std::clamp<uint32_t>(params.maskMode, 0u, 5u);
        params.maskDepth = clamp(params.maskDepth, 0.0f, 1.0f);
        params.maskRateHz = clamp(params.maskRateHz, 0.0f, 4.0f);
        for (uint32_t i = 0u; i < kAmbiWranglerMaxVoices; ++i) {
            params.bpRateA[i] = clamp(params.bpRateA[i], 0.0f, 1.0f);
            params.bpRateB[i] = clamp(params.bpRateB[i], 0.0f, 1.0f);
            params.bpFmAtoB[i] = clamp(params.bpFmAtoB[i], 0.0f, 1.0f);
            params.bpFmBtoA[i] = clamp(params.bpFmBtoA[i], 0.0f, 1.0f);
            params.bpRunglerA[i] = clamp(params.bpRunglerA[i], 0.0f, 1.0f);
            params.bpRunglerB[i] = clamp(params.bpRunglerB[i], 0.0f, 1.0f);
            params.bpFilter[i] = clamp(params.bpFilter[i], 0.0f, 1.0f);
            params.bpThreshold[i] = clamp(params.bpThreshold[i], 0.0f, 1.0f);
            params.bpPwmA[i] = clamp(params.bpPwmA[i], 0.0f, 1.0f);
            params.bpPwmB[i] = clamp(params.bpPwmB[i], 0.0f, 1.0f);
            params.bpRampA[i] = clamp(params.bpRampA[i], 0.0f, 1.0f);
            params.bpRampB[i] = clamp(params.bpRampB[i], 0.0f, 1.0f);
            params.bpAmp[i] = clamp(params.bpAmp[i], 0.0f, 1.0f);
        }
        params.topologyShape = std::clamp<uint32_t>(params.topologyShape, 0u, kTopologyShapeCount - 1u);
        params.topologyMotion = std::clamp<uint32_t>(params.topologyMotion, 0u, kTopologyMotionModeCount - 1u);
        params.topologyRateHz = clamp(params.topologyRateHz, 0.001f, 2.0f);
        params.topologyAmount = clamp(params.topologyAmount, 0.0f, 1.0f);
        params.topologyDepth = clamp(params.topologyDepth, 0.0f, 1.0f);
        params.topologyScale = clamp(params.topologyScale, 0.20f, 2.50f);
        params.topologyCollapse = clamp(params.topologyCollapse, 0.0f, 1.0f);
        params.centerAzimuthDeg = wrapSignedDeg(params.centerAzimuthDeg);
        params.centerElevationDeg = clamp(params.centerElevationDeg, -90.0f, 90.0f);
        params.centerDistance = clamp(params.centerDistance, 0.15f, 2.0f);
        params.spatialFollow = clamp(params.spatialFollow, 0.0f, 1.0f);
        params.outputGainDb = clamp(params.outputGainDb, -60.0f, 12.0f);
        const bool voiceCountChanged = params.voices != params_.voices;
        params_ = params;
        if (voiceCountChanged) {
            for (uint32_t i = 0u; i < kAmbiWranglerMaxVoices; ++i) initializeVoice(i);
        }
    }

    AmbiWranglerParams params() const { return params_; }
    float voiceEnergy(uint32_t voice) const { return voices_[std::min<uint32_t>(voice, kAmbiWranglerMaxVoices - 1u)].energy; }
    AmbiWranglerPoint voicePoint(uint32_t voice) const { return points_[std::min<uint32_t>(voice, kAmbiWranglerMaxVoices - 1u)]; }
    float voiceMaskLevel(uint32_t voice) const { return voiceMask(std::min<uint32_t>(voice, kAmbiWranglerMaxVoices - 1u)); }

    void process(float* const* outputs, uint32_t outputChannels, uint32_t frames)
    {
        if (!outputs || frames == 0u) return;
        outputChannels = std::min<uint32_t>(outputChannels, kAmbiWranglerMaxChannels);
        for (uint32_t ch = 0u; ch < outputChannels; ++ch) {
            if (outputs[ch]) std::fill(outputs[ch], outputs[ch] + frames, 0.0f);
        }

        const uint32_t voices = params_.voices;
        const uint32_t ambiChannels = std::min<uint32_t>(ambiChannelsForOrder(params_.order), outputChannels);
        const float targetGain = dbToGain(params_.outputGainDb) / std::sqrt(static_cast<float>(std::max<uint32_t>(1u, voices)));
        constexpr uint32_t kControlFrames = 16u;

        for (uint32_t chunkStart = 0u; chunkStart < frames; chunkStart += kControlFrames) {
            const uint32_t chunkFrames = std::min<uint32_t>(kControlFrames, frames - chunkStart);
            const float chunkDt = static_cast<float>(chunkFrames) / static_cast<float>(sampleRate_);
            updateTopology(chunkDt);
            updateMask(chunkDt);

            std::array<std::array<float, kAmbiWranglerMaxChannels>, kAmbiWranglerMaxVoices> basis {};
            std::array<float, kAmbiWranglerMaxVoices> distGain {};
            for (uint32_t v = 0u; v < voices; ++v) {
                basis[v] = acnSn3dBasis7(directionFromAed(points_[v].azimuthDeg, points_[v].elevationDeg));
                distGain[v] = 1.0f / std::max(0.50f, points_[v].distance);
            }

            for (uint32_t frame = chunkStart; frame < chunkStart + chunkFrames; ++frame) {
                smoothedOutputGain_ += (targetGain - smoothedOutputGain_) * 0.0015f;
                for (uint32_t v = 0u; v < voices; ++v) {
                    const float sample = processVoice(v) * voiceMask(v) * smoothedOutputGain_ * distGain[v];
                    voices_[v].energy += (sample * sample - voices_[v].energy) * 0.0008f;
                    if (std::fabs(sample) < 0.0000001f) continue;
                    for (uint32_t ch = 0u; ch < ambiChannels; ++ch) {
                        if (outputs[ch]) outputs[ch][frame] = flushDenormal(outputs[ch][frame] + sample * basis[v][ch]);
                    }
                }
            }
        }

        for (uint32_t ch = 0u; ch < ambiChannels; ++ch) {
            if (!outputs[ch]) continue;
            for (uint32_t frame = 0u; frame < frames; ++frame) {
                outputs[ch][frame] = std::tanh(clamp(outputs[ch][frame], -6.0f, 6.0f));
            }
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

    static float hash01(uint32_t x)
    {
        return static_cast<float>(hash(x) & 0xffffu) / 65535.0f;
    }

    static float hashSigned(uint32_t x)
    {
        return hash01(x) * 2.0f - 1.0f;
    }

    static float midiToHz(float note)
    {
        return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
    }

    static float targetNormToHz(float value)
    {
        value = clamp(value, 0.0f, 1.0f);
        return midiToHz(value * 172.0f - 40.0f);
    }

    static float clampOscHz(float hz, float sampleRate)
    {
        const float limit = sampleRate * 0.42f;
        hz = clamp(hz, -limit, limit);
        if (std::fabs(hz) < 0.015f) return hz < 0.0f ? -0.015f : 0.015f;
        return hz;
    }

    static float wrapSignedDeg(float value)
    {
        while (value > 180.0f) value -= 360.0f;
        while (value <= -180.0f) value += 360.0f;
        return value;
    }

    static float phaseTri(float phase)
    {
        return 1.0f - 4.0f * std::fabs(phase - 0.5f);
    }

    static AmbiWranglerPoint basePoint(uint32_t voice, uint32_t voices)
    {
        const float u = (static_cast<float>(voice) + 0.5f) / static_cast<float>(std::max<uint32_t>(1u, voices));
        const float z = 1.0f - 2.0f * u;
        const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        const float a = static_cast<float>(voice) * 2.39996323f;
        const Vec3 p { std::cos(a) * r, std::sin(a) * r, z };
        return {
            std::atan2(p.y, p.x) * 180.0f / kPi,
            std::asin(clamp(p.z, -1.0f, 1.0f)) * 180.0f / kPi,
            1.0f
        };
    }

    void initializeVoice(uint32_t index)
    {
        auto& voice = voices_[index];
        voice.rateA = 1.0f + hashSigned(voice.seed + 19u) * 0.01f;
        voice.rateB = 1.0f + hashSigned(voice.seed + 31u) * 0.01f;
        voice.phaseA = hash01(voice.seed + 41u);
        voice.phaseB = hash01(voice.seed + 53u);
    }

    void updateTopology(float dt)
    {
        topologyPhase_ += dt * params_.topologyRateHz;
        if (topologyPhase_ > 100000.0f) topologyPhase_ -= 100000.0f;
        const uint32_t voices = params_.voices;
        TopologyState state {};
        state.shape = params_.topologyShape;
        state.motionMode = params_.topologyMotion;
        state.motionRateHz = params_.topologyRateHz;
        state.motionPhase = topologyPhase_;
        state.amount = params_.topologyAmount;
        state.motionDepth = params_.topologyDepth;
        state.collapse = params_.topologyCollapse;
        state.twist = 0.0f;
        state.jitter = std::max(params_.runglerA, params_.runglerB) * 0.24f;
        const float follow = 1.0f - std::pow(params_.spatialFollow, 4.0f);
        for (uint32_t i = 0u; i < voices; ++i) {
            const auto tp = topologyPointForLane(i, voices, state);
            const float az = std::atan2(tp.y, tp.x) * 180.0f / kPi;
            const float el = std::asin(clamp(tp.z, -1.0f, 1.0f)) * 180.0f / kPi;
            const float radius = static_cast<float>(std::sqrt(tp.x * tp.x + tp.y * tp.y + tp.z * tp.z));
            const float dist = clamp(params_.centerDistance * (0.85f + 0.30f * radius) * params_.topologyScale, 0.15f, 2.0f);
            targetPoints_[i] = {
                wrapSignedDeg(params_.centerAzimuthDeg + az),
                clamp(params_.centerElevationDeg + el * 0.72f, -90.0f, 90.0f),
                dist
            };
            points_[i].azimuthDeg = wrapSignedDeg(points_[i].azimuthDeg + wrapSignedDeg(targetPoints_[i].azimuthDeg - points_[i].azimuthDeg) * follow);
            points_[i].elevationDeg += (targetPoints_[i].elevationDeg - points_[i].elevationDeg) * follow;
            points_[i].distance += (targetPoints_[i].distance - points_[i].distance) * follow;
        }
    }

    void updateMask(float dt)
    {
        maskPhase_ += dt * params_.maskRateHz;
        if (maskPhase_ > 100000.0f) maskPhase_ -= 100000.0f;
    }

    float voiceMask(uint32_t index) const
    {
        if (params_.maskMode == 0u || params_.maskDepth <= 0.0001f) return 1.0f;
        const uint32_t voices = std::max<uint32_t>(1u, params_.voices);
        const float lane = static_cast<float>(index) / static_cast<float>(std::max<uint32_t>(1u, voices - 1u));
        const float phase = maskPhase_ + hash01(voices_[index].seed + 1009u);
        const float wheel = phase - std::floor(phase);
        float mask = 1.0f;
        switch (params_.maskMode) {
        case 1: {
            const float pulse = 0.5f + 0.5f * std::sin((wheel + lane * 0.17f) * kPi * 2.0f);
            mask = 0.35f + 0.65f * pulse;
            break;
        }
        case 2: {
            const float choir = 0.5f + 0.5f * std::sin((maskPhase_ * 0.43f + lane * 1.7f) * kPi * 2.0f);
            const float laneBias = 0.62f + 0.38f * hash01(voices_[index].seed + 1223u);
            mask = std::pow(choir, 1.7f) * laneBias;
            break;
        }
        case 3: {
            const float cell = std::fabs((lane - wheel) - std::floor((lane - wheel) + 0.5f));
            mask = cell < 0.18f ? 1.0f : (cell < 0.32f ? 0.35f : 0.03f);
            break;
        }
        case 4: {
            const float n = hash01(static_cast<uint32_t>(std::floor(maskPhase_ * 16.0f)) * 7919u + voices_[index].seed);
            mask = n > 0.72f ? 1.0f : (n > 0.54f ? 0.28f : 0.0f);
            break;
        }
        case 5: {
            const float a = 0.5f + 0.5f * std::sin((maskPhase_ * 0.31f + lane * 2.0f) * kPi * 2.0f);
            const float b = 0.5f + 0.5f * std::sin((maskPhase_ * 0.73f - lane * 3.0f) * kPi * 2.0f);
            mask = std::pow(a * b, 1.25f);
            break;
        }
        default:
            break;
        }
        return lerp(1.0f, clamp(mask, 0.0f, 1.0f), params_.maskDepth);
    }

    static float breakpointRange(uint32_t row)
    {
        switch (row) {
        case 0:
        case 1:
            return 0.85f;
        case 6:
        case 7:
        case 8:
        case 9:
        case 10:
        case 11:
            return 0.70f;
        case 12:
            return 1.0f;
        default:
            return 0.95f;
        }
    }

    float laneValue(uint32_t index, uint32_t row, const std::array<float, kAmbiWranglerMaxVoices>& values, float fallback) const
    {
        if (!params_.voiceBreakpointsEnabled) return fallback;
        const float value = values[std::min<uint32_t>(index, kAmbiWranglerMaxVoices - 1u)];
        if (row == 12u) return value;
        return clamp(fallback + (value - 0.5f) * breakpointRange(row), 0.0f, 1.0f);
    }

    static float shapedRamp(float phase, float shape)
    {
        phase -= std::floor(phase);
        const float pivot = clamp(shape, 0.02f, 0.98f);
        const float up = phase < pivot
            ? phase / pivot
            : 1.0f - (phase - pivot) / std::max(0.0001f, 1.0f - pivot);
        return up * 2.0f - 1.0f;
    }

    static float smoothPulse(float ramp, float width, float hz, float sampleRate)
    {
        const float threshold = width * 2.0f - 1.0f;
        const float edge = clamp(hz / std::max(1.0f, sampleRate) * 18.0f, 0.002f, 0.18f);
        return std::tanh((ramp - threshold) / edge);
    }

    float processVoice(uint32_t index)
    {
        auto& voice = voices_[index];
        const float lane = static_cast<float>(index) / static_cast<float>(std::max<uint32_t>(1u, params_.voices - 1u));
        const float field = params_.field;
        const float rateAParam = laneValue(index, 0u, params_.bpRateA, params_.rateA);
        const float rateBParam = laneValue(index, 1u, params_.bpRateB, params_.rateB);
        const float fmAtoBParam = laneValue(index, 2u, params_.bpFmAtoB, params_.fmAtoB);
        const float fmBtoAParam = laneValue(index, 3u, params_.bpFmBtoA, params_.fmBtoA);
        const float runglerAParam = laneValue(index, 4u, params_.bpRunglerA, params_.runglerA);
        const float runglerBParam = laneValue(index, 5u, params_.bpRunglerB, params_.runglerB);
        const float filterParam = laneValue(index, 6u, params_.bpFilter, params_.filter);
        const float thresholdParam = laneValue(index, 7u, params_.bpThreshold, params_.threshold);
        const float pwmAParam = laneValue(index, 8u, params_.bpPwmA, params_.pwmA);
        const float pwmBParam = laneValue(index, 9u, params_.bpPwmB, params_.pwmB);
        const float rampAParam = laneValue(index, 10u, params_.bpRampA, params_.rampA);
        const float rampBParam = laneValue(index, 11u, params_.bpRampB, params_.rampB);
        const float ampParam = laneValue(index, 12u, params_.bpAmp, 1.0f);
        const float spreadA = (lane - 0.5f) * params_.spread * (1.4f + field * 1.6f);
        const float spreadB = (0.5f - lane) * params_.spread * (1.2f + field * 1.4f);
        const float devA = hashSigned(voice.seed + 101u) * params_.deviation * (0.35f + field * 0.85f);
        const float devB = hashSigned(voice.seed + 211u) * params_.deviation * (0.35f + field * 0.85f);
        const float rateScaleA = params_.rateModeA == 0u ? 1.0f : 2.0f;
        const float rateScaleB = params_.rateModeB == 0u ? 1.0f : 2.0f;
        const float baseA = targetNormToHz(clamp(rateAParam + hashSigned(voice.seed + 307u) * field * params_.deviation * 0.42f, 0.0f, 1.0f)) * rateScaleA * std::pow(2.0f, spreadA + devA) * voice.rateA;
        const float baseB = targetNormToHz(clamp(rateBParam + hashSigned(voice.seed + 401u) * field * params_.deviation * 0.42f, 0.0f, 1.0f)) * rateScaleB * std::pow(2.0f, spreadB + devB) * voice.rateB;
        const float laneFmA = clamp(fmBtoAParam + hashSigned(voice.seed + 503u) * field * params_.deviation * 0.55f, 0.0f, 1.0f);
        const float laneFmB = clamp(fmAtoBParam + hashSigned(voice.seed + 601u) * field * params_.deviation * 0.55f, 0.0f, 1.0f);
        const float laneRunA = clamp(runglerAParam + hashSigned(voice.seed + 701u) * field * params_.deviation * 0.65f, 0.0f, 1.0f);
        const float laneRunB = clamp(runglerBParam + hashSigned(voice.seed + 809u) * field * params_.deviation * 0.65f, 0.0f, 1.0f);
        const float fmToA = phaseTri(voice.phaseB) * targetNormToHz(laneFmA);
        const float fmToB = phaseTri(voice.phaseA) * targetNormToHz(laneFmB);
        const float rungToA = voice.rungSmooth * targetNormToHz(laneRunA);
        const float rungToB = voice.rungSmooth * targetNormToHz(laneRunB);
        const float hzA = clampOscHz(baseA + fmToA + rungToA, static_cast<float>(sampleRate_));
        const float hzB = clampOscHz(baseB + fmToB + rungToB, static_cast<float>(sampleRate_));
        voice.phaseA += hzA / static_cast<float>(sampleRate_);
        voice.phaseB += hzB / static_cast<float>(sampleRate_);
        voice.phaseA -= std::floor(voice.phaseA);
        voice.phaseB -= std::floor(voice.phaseB);

        const float triA = shapedRamp(voice.phaseA, rampAParam);
        const float triB = shapedRamp(voice.phaseB, rampBParam);
        const float widthA = clamp(pwmAParam + (thresholdParam - 0.5f) * 0.42f, 0.03f, 0.97f);
        const float widthB = clamp(pwmBParam - (thresholdParam - 0.5f) * 0.42f, 0.03f, 0.97f);
        const float squareA = smoothPulse(triA, widthA, std::fabs(hzA), static_cast<float>(sampleRate_));
        const float squareB = smoothPulse(triB, widthB, std::fabs(hzB), static_cast<float>(sampleRate_));
        const float clockInput = params_.inputB == 0u ? squareB : triB;
        const bool clock = clockInput > 0.0f && voice.lastClock <= 0.0f;
        voice.lastClock = clockInput;
        if (clock) {
            const float input = params_.inputA == 0u ? squareA : triA;
            const uint32_t bit = input > 0.0f ? 1u : 0u;
            const uint32_t mask = (1u << params_.rungSize) - 1u;
            voice.reg = ((voice.reg << 1u) | bit) & mask;
            const float denom = static_cast<float>(std::max<uint32_t>(1u, mask));
            voice.rungValue = static_cast<float>(voice.reg) / denom * 2.0f - 1.0f;
        }
        const float rungAmount = std::max(params_.runglerA, params_.runglerB);
        voice.rungSmooth += (voice.rungValue - voice.rungSmooth) * (0.002f + rungAmount * 0.030f);

        const float sweep = (triB * 0.5f + 0.5f) * params_.filterSweep * 0.11f;
        const float run = (voice.rungSmooth * 0.5f + 0.5f) * params_.filterRun * 0.14f;
        const float filterNorm = 0.0015f + std::pow(filterParam, 2.2f) * 0.20f + sweep + run;
        const float pwm = squareA * 0.55f + squareB * 0.45f + (triA - triB) * 0.25f;
        const float filtA = voice.filterA.process(pwm + squareB * laneRunA * 0.30f, filterNorm, params_.resonance, params_.saturation);
        const float filtB = voice.filterB.process(pwm + squareA * laneRunB * 0.30f, filterNorm * 1.17f, params_.resonance, params_.saturation);
        const float edge = (squareA + squareB) * 0.24f;
        const float body = (triA + triB) * 0.32f;
        const float bloom = (filtA + filtB) * 0.48f;
        const float mixed = lerp(edge + body, bloom + body * 0.24f, params_.color);
        return std::tanh(clamp(mixed * (1.0f + params_.saturation * 3.2f), -7.0f, 7.0f)) * ampParam;
    }

    double sampleRate_ = 48000.0;
    AmbiWranglerParams params_ {};
    std::array<AmbiWranglerVoice, kAmbiWranglerMaxVoices> voices_ {};
    std::array<AmbiWranglerPoint, kAmbiWranglerMaxVoices> points_ {};
    std::array<AmbiWranglerPoint, kAmbiWranglerMaxVoices> targetPoints_ {};
    float topologyPhase_ = 0.0f;
    float maskPhase_ = 0.0f;
    float smoothedOutputGain_ = 0.0f;
};

} // namespace s3g
