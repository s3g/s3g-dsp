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
    float baseNote = 34.0f;
    float spreadSemitones = 24.0f;
    float detuneCents = 12.0f;
    float chaos = 0.58f;
    float cross = 0.28f;
    float rung = 0.72f;
    uint32_t rungSize = 4;
    float threshold = 0.50f;
    float color = 0.42f;
    float filter = 0.36f;
    float resonance = 0.20f;
    float saturation = 0.36f;
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
    float outputGainDb = -24.0f;
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
        params.baseNote = clamp(params.baseNote, 0.0f, 96.0f);
        params.spreadSemitones = clamp(params.spreadSemitones, 0.0f, 60.0f);
        params.detuneCents = clamp(params.detuneCents, 0.0f, 100.0f);
        params.chaos = clamp(params.chaos, 0.0f, 1.0f);
        params.cross = clamp(params.cross, 0.0f, 1.0f);
        params.rung = clamp(params.rung, 0.0f, 1.0f);
        params.rungSize = std::clamp<uint32_t>(params.rungSize, 2u, 8u);
        params.threshold = clamp(params.threshold, 0.0f, 1.0f);
        params.color = clamp(params.color, 0.0f, 1.0f);
        params.filter = clamp(params.filter, 0.0f, 1.0f);
        params.resonance = clamp(params.resonance, 0.0f, 1.0f);
        params.saturation = clamp(params.saturation, 0.0f, 1.0f);
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
            updateTopology(static_cast<float>(chunkFrames) / static_cast<float>(sampleRate_));

            std::array<std::array<float, kAmbiWranglerMaxChannels>, kAmbiWranglerMaxVoices> basis {};
            std::array<float, kAmbiWranglerMaxVoices> distGain {};
            for (uint32_t v = 0u; v < voices; ++v) {
                basis[v] = acnSn3dBasis7(directionFromAed(points_[v].azimuthDeg, points_[v].elevationDeg));
                distGain[v] = 1.0f / std::max(0.50f, points_[v].distance);
            }

            for (uint32_t frame = chunkStart; frame < chunkStart + chunkFrames; ++frame) {
                smoothedOutputGain_ += (targetGain - smoothedOutputGain_) * 0.0015f;
                for (uint32_t v = 0u; v < voices; ++v) {
                    const float sample = processVoice(v) * smoothedOutputGain_ * distGain[v];
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
        const float lane = static_cast<float>(index) / static_cast<float>(std::max<uint32_t>(1u, params_.voices - 1u));
        const float spread = (lane - 0.5f) * params_.spreadSemitones;
        const float jitter = hashSigned(voice.seed + 19u) * params_.detuneCents * 0.01f;
        const float noteA = params_.baseNote + spread + jitter;
        const float noteB = params_.baseNote + spread * -0.73f + 7.0f + hashSigned(voice.seed + 31u) * 3.0f;
        voice.rateA = midiToHz(noteA);
        voice.rateB = midiToHz(noteB);
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
        state.twist = params_.chaos * 0.7f;
        state.jitter = params_.rung * 0.24f;
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

    float processVoice(uint32_t index)
    {
        auto& voice = voices_[index];
        const float rungPitch = voice.rungSmooth * params_.chaos * 36.0f;
        const float crossA = (voice.phaseB < 0.5f ? 1.0f : -1.0f) * params_.cross * 0.18f;
        const float crossB = (voice.phaseA < 0.5f ? 1.0f : -1.0f) * params_.cross * 0.18f;
        const float hzA = std::min(midiToHz(params_.baseNote + rungPitch) + voice.rateA * (0.35f + params_.chaos), static_cast<float>(sampleRate_) * 0.42f);
        const float hzB = std::min(voice.rateB * std::pow(2.0f, voice.rungSmooth * params_.rung * 1.8f), static_cast<float>(sampleRate_) * 0.42f);
        voice.phaseA += hzA / static_cast<float>(sampleRate_) + crossA / static_cast<float>(sampleRate_);
        voice.phaseB += hzB / static_cast<float>(sampleRate_) + crossB / static_cast<float>(sampleRate_);
        voice.phaseA -= std::floor(voice.phaseA);
        voice.phaseB -= std::floor(voice.phaseB);

        const float triA = phaseTri(voice.phaseA);
        const float triB = phaseTri(voice.phaseB);
        const float squareA = triA > (params_.threshold * 2.0f - 1.0f) ? 1.0f : -1.0f;
        const float squareB = triB > ((1.0f - params_.threshold) * 2.0f - 1.0f) ? 1.0f : -1.0f;
        const bool clock = squareB > 0.0f && voice.lastClock <= 0.0f;
        voice.lastClock = squareB;
        if (clock) {
            const uint32_t bit = squareA > 0.0f ? 1u : 0u;
            const uint32_t mask = (1u << params_.rungSize) - 1u;
            voice.reg = ((voice.reg << 1u) | bit) & mask;
            const float denom = static_cast<float>(std::max<uint32_t>(1u, mask));
            voice.rungValue = static_cast<float>(voice.reg) / denom * 2.0f - 1.0f;
        }
        voice.rungSmooth += (voice.rungValue - voice.rungSmooth) * (0.002f + params_.rung * 0.030f);

        const float filterNorm = 0.0015f + std::pow(params_.filter, 2.2f) * 0.22f + (voice.rungSmooth * 0.5f + 0.5f) * params_.chaos * 0.09f;
        const float filtA = voice.filterA.process(triA + squareB * params_.rung * 0.35f, filterNorm, params_.resonance, params_.saturation);
        const float filtB = voice.filterB.process(triB + squareA * params_.rung * 0.35f, filterNorm * 1.17f, params_.resonance, params_.saturation);
        const float edge = (squareA + squareB) * 0.24f;
        const float body = (triA + triB) * 0.32f;
        const float bloom = (filtA + filtB) * 0.48f;
        const float mixed = lerp(edge + body, bloom + body * 0.24f, params_.color);
        return std::tanh(clamp(mixed * (1.0f + params_.saturation * 3.2f), -7.0f, 7.0f));
    }

    double sampleRate_ = 48000.0;
    AmbiWranglerParams params_ {};
    std::array<AmbiWranglerVoice, kAmbiWranglerMaxVoices> voices_ {};
    std::array<AmbiWranglerPoint, kAmbiWranglerMaxVoices> points_ {};
    std::array<AmbiWranglerPoint, kAmbiWranglerMaxVoices> targetPoints_ {};
    float topologyPhase_ = 0.0f;
    float smoothedOutputGain_ = 0.0f;
};

} // namespace s3g
