#pragma once

#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_realtime.h"
#include "s3g_topology.h"

#include <algorithm>
#include <array>
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
    uint32_t rateMode = 1;
    uint32_t materialMode = 0;
    uint32_t inputA = 0;
    uint32_t inputB = 0;
    uint32_t rungLoop = 0;
    float center = 0.38f;
    float sweep = 0.48f;
    float q = 0.42f;
    float shrill = 0.24f;
    float body = 0.52f;
    float breath = 0.36f;
    float grit = 0.18f;
    float field = 0.70f;
    uint32_t maskMode = 2;
    float maskDepth = 0.44f;
    float maskRateHz = 0.050f;
    bool voiceBreakpointsEnabled = false;
    std::array<float, kAmbiWindMaxVoices> bpWind {};
    std::array<float, kAmbiWindMaxVoices> bpGustRate {};
    std::array<float, kAmbiWindMaxVoices> bpGustDepth {};
    std::array<float, kAmbiWindMaxVoices> bpTurbulence {};
    std::array<float, kAmbiWindMaxVoices> bpFlutter {};
    std::array<float, kAmbiWindMaxVoices> bpMaterial {};
    std::array<float, kAmbiWindMaxVoices> bpCenter {};
    std::array<float, kAmbiWindMaxVoices> bpQ {};
    std::array<float, kAmbiWindMaxVoices> bpAir {};
    std::array<float, kAmbiWindMaxVoices> bpHiss {};
    std::array<float, kAmbiWindMaxVoices> bpSweep {};
    std::array<float, kAmbiWindMaxVoices> bpBody {};
    std::array<float, kAmbiWindMaxVoices> bpAmp {};
    uint32_t topologyShape = 11;
    uint32_t topologyMotion = 1;
    float topologyRateHz = 0.024f;
    float topologyAmount = 0.74f;
    float topologyDepth = 0.64f;
    float topologyScale = 1.18f;
    float topologyCollapse = 0.0f;
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
    float low = 0.0f;
    float band = 0.0f;

    float process(float input, float cutoffHz, float resonance, float sampleRate)
    {
        const float hz = clamp(cutoffHz, 8.0f, sampleRate * 0.42f);
        const float f = clamp(2.0f * std::sin(kPi * hz / sampleRate), 0.0001f, 1.65f);
        const float damp = 1.72f - clamp(resonance, 0.0f, 1.0f) * 1.55f;
        low = flushDenormal(low + f * band);
        const float high = input - low - damp * band;
        band = flushDenormal(band + f * high);
        return band;
    }
};

struct AmbiWindVoice {
    uint32_t seed = 1u;
    float gustPhase[3] { 0.0f, 0.0f, 0.0f };
    float gustValue = 0.0f;
    float gustSmooth = 0.0f;
    float pinkA = 0.0f;
    float pinkB = 0.0f;
    float pinkC = 0.0f;
    float lowNoise = 0.0f;
    float materialRing = 0.0f;
    float maskGain = 1.0f;
    float energy = 0.0f;
    AmbiWindSvf mainFilter {};
    AmbiWindSvf airFilter {};
    AmbiWindSvf bodyFilter {};
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
        smoothedOutputGain_ = dbToGain(params_.outputGainDb);
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
        params.wind = clamp(params.wind, 0.0f, 1.0f);
        params.gustRate = clamp(params.gustRate, 0.0f, 1.0f);
        params.gustDepth = clamp(params.gustDepth, 0.0f, 1.0f);
        params.turbulence = clamp(params.turbulence, 0.0f, 1.0f);
        params.flutter = clamp(params.flutter, 0.0f, 1.0f);
        params.material = clamp(params.material, 0.0f, 1.0f);
        params.air = clamp(params.air, 0.0f, 1.0f);
        params.hiss = clamp(params.hiss, 0.0f, 1.0f);
        params.spread = clamp(params.spread, 0.0f, 1.0f);
        params.deviation = clamp(params.deviation, 0.0f, 1.0f);
        params.gustShape = std::clamp<uint32_t>(params.gustShape, 0u, 5u);
        params.rateMode = std::clamp<uint32_t>(params.rateMode, 0u, 2u);
        params.materialMode = std::clamp<uint32_t>(params.materialMode, 0u, 4u);
        params.inputA = std::clamp<uint32_t>(params.inputA, 0u, 1u);
        params.inputB = std::clamp<uint32_t>(params.inputB, 0u, 1u);
        params.rungLoop = std::clamp<uint32_t>(params.rungLoop, 0u, 2u);
        params.center = clamp(params.center, 0.0f, 1.0f);
        params.sweep = clamp(params.sweep, 0.0f, 1.0f);
        params.q = clamp(params.q, 0.0f, 1.0f);
        params.shrill = clamp(params.shrill, 0.0f, 1.0f);
        params.body = clamp(params.body, 0.0f, 1.0f);
        params.breath = clamp(params.breath, 0.0f, 1.0f);
        params.grit = clamp(params.grit, 0.0f, 1.0f);
        params.field = clamp(params.field, 0.0f, 1.0f);
        params.maskMode = std::clamp<uint32_t>(params.maskMode, 0u, 5u);
        params.maskDepth = clamp(params.maskDepth, 0.0f, 1.0f);
        params.maskRateHz = clamp(params.maskRateHz, 0.0f, 4.0f);
        for (uint32_t i = 0u; i < kAmbiWindMaxVoices; ++i) {
            params.bpWind[i] = clamp(params.bpWind[i], 0.0f, 1.0f);
            params.bpGustRate[i] = clamp(params.bpGustRate[i], 0.0f, 1.0f);
            params.bpGustDepth[i] = clamp(params.bpGustDepth[i], 0.0f, 1.0f);
            params.bpTurbulence[i] = clamp(params.bpTurbulence[i], 0.0f, 1.0f);
            params.bpFlutter[i] = clamp(params.bpFlutter[i], 0.0f, 1.0f);
            params.bpMaterial[i] = clamp(params.bpMaterial[i], 0.0f, 1.0f);
            params.bpCenter[i] = clamp(params.bpCenter[i], 0.0f, 1.0f);
            params.bpQ[i] = clamp(params.bpQ[i], 0.0f, 1.0f);
            params.bpAir[i] = clamp(params.bpAir[i], 0.0f, 1.0f);
            params.bpHiss[i] = clamp(params.bpHiss[i], 0.0f, 1.0f);
            params.bpSweep[i] = clamp(params.bpSweep[i], 0.0f, 1.0f);
            params.bpBody[i] = clamp(params.bpBody[i], 0.0f, 1.0f);
            params.bpAmp[i] = clamp(params.bpAmp[i], 0.0f, 1.0f);
        }
        params.topologyRateHz = clamp(params.topologyRateHz, 0.001f, 2.0f);
        params.topologyAmount = clamp(params.topologyAmount, 0.0f, 1.0f);
        params.topologyDepth = clamp(params.topologyDepth, 0.0f, 1.0f);
        params.topologyScale = clamp(params.topologyScale, 0.0f, 1.0f);
        params.topologyCollapse = clamp(params.topologyCollapse, 0.0f, 1.0f);
        params.centerAzimuthDeg = wrapSignedDeg(params.centerAzimuthDeg);
        params.centerElevationDeg = clamp(params.centerElevationDeg, -90.0f, 90.0f);
        params.centerDistance = clamp(params.centerDistance, 0.15f, 2.0f);
        params.spatialFollow = clamp(params.spatialFollow, 0.0f, 1.0f);
        params.outputGainDb = clamp(params.outputGainDb, -60.0f, 12.0f);
        const bool voiceCountChanged = params.voices != params_.voices;
        params_ = params;
        if (voiceCountChanged) {
            for (uint32_t i = 0u; i < kAmbiWindMaxVoices; ++i) initializeVoice(i);
        }
    }

    AmbiWindParams params() const { return params_; }
    float voiceEnergy(uint32_t voice) const { return voices_[std::min<uint32_t>(voice, kAmbiWindMaxVoices - 1u)].energy; }
    AmbiWindPoint voicePoint(uint32_t voice) const { return points_[std::min<uint32_t>(voice, kAmbiWindMaxVoices - 1u)]; }
    float voiceMaskLevel(uint32_t voice) const { return voices_[std::min<uint32_t>(voice, kAmbiWindMaxVoices - 1u)].maskGain; }

    void process(float* const* outputs, uint32_t outputChannels, uint32_t frames)
    {
        if (!outputs || frames == 0u) return;
        outputChannels = std::min<uint32_t>(outputChannels, kAmbiWindMaxChannels);
        for (uint32_t ch = 0u; ch < outputChannels; ++ch) {
            if (outputs[ch]) std::fill(outputs[ch], outputs[ch] + frames, 0.0f);
        }

        const uint32_t voices = params_.voices;
        const uint32_t ambiChannels = std::min<uint32_t>(ambiChannelsForOrder(params_.order), outputChannels);
        const float targetGain = dbToGain(params_.outputGainDb) * 0.42f / std::sqrt(static_cast<float>(std::max<uint32_t>(1u, voices)));
        constexpr uint32_t kControlFrames = 16u;

        for (uint32_t chunkStart = 0u; chunkStart < frames; chunkStart += kControlFrames) {
            const uint32_t chunkFrames = std::min<uint32_t>(kControlFrames, frames - chunkStart);
            const float chunkDt = static_cast<float>(chunkFrames) / static_cast<float>(sampleRate_);
            updateMotion(chunkDt);

            std::array<std::array<float, kAmbiWindMaxChannels>, kAmbiWindMaxVoices> basis {};
            std::array<float, kAmbiWindMaxVoices> distGain {};
            for (uint32_t v = 0u; v < voices; ++v) {
                basis[v] = acnSn3dBasis7(directionFromAed(points_[v].azimuthDeg, points_[v].elevationDeg));
                distGain[v] = 1.0f / std::max(0.50f, points_[v].distance);
            }

            for (uint32_t frame = chunkStart; frame < chunkStart + chunkFrames; ++frame) {
                smoothedOutputGain_ += (targetGain - smoothedOutputGain_) * 0.0010f;
                for (uint32_t v = 0u; v < voices; ++v) {
                    auto& voice = voices_[v];
                    voice.maskGain = params_.voiceBreakpointsEnabled ? clamp(params_.bpAmp[v], 0.04f, 1.0f) : 1.0f;
                    const float sample = processVoice(v) * voice.maskGain * smoothedOutputGain_ * distGain[v];
                    voices_[v].energy += (sample * sample - voices_[v].energy) * 0.0006f;
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
                outputs[ch][frame] = std::tanh(clamp(outputs[ch][frame] * 0.85f, -4.0f, 4.0f));
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

    static float hashSigned(uint32_t x) { return hash01(x) * 2.0f - 1.0f; }

    static float wrapSignedDeg(float value)
    {
        while (value > 180.0f) value -= 360.0f;
        while (value <= -180.0f) value += 360.0f;
        return value;
    }

    static float rateScale(uint32_t mode)
    {
        switch (mode) {
        case 0u: return 0.25f;
        case 2u: return 4.0f;
        default: return 1.0f;
        }
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
        return { az, el, 0.88f + radius * 0.22f };
    }

    static float shapedGust(float phase, uint32_t shape)
    {
        const float s = 0.5f + 0.5f * std::sin(phase * kPi * 2.0f);
        switch (shape) {
        case 0u: return s;
        case 1u: return std::pow(s, 2.4f);
        case 2u: return 1.0f - std::pow(1.0f - s, 3.0f);
        case 3u: return std::fabs(2.0f * phase - 1.0f);
        case 4u: return phase < 0.18f ? 1.0f : std::exp(-(phase - 0.18f) * 5.5f);
        default: return s > 0.58f ? 1.0f : 0.12f + s * 0.28f;
        }
    }

    float noise(AmbiWindVoice& voice)
    {
        voice.seed = hash(voice.seed + 0x6d2b79f5u);
        return static_cast<float>(static_cast<int32_t>(voice.seed)) / 2147483648.0f;
    }

    float laneValue(uint32_t voiceIndex, const std::array<float, kAmbiWindMaxVoices>& values, float macro, float range) const
    {
        if (!params_.voiceBreakpointsEnabled) return macro;
        const float curve = values[voiceIndex];
        return clamp(macro + (curve - 0.5f) * range, 0.0f, 1.0f);
    }

    void initializeVoice(uint32_t index)
    {
        auto& voice = voices_[index];
        const float h = hash01(voice.seed);
        voice.gustPhase[0] = h;
        voice.gustPhase[1] = hash01(voice.seed + 11u);
        voice.gustPhase[2] = hash01(voice.seed + 23u);
        voice.gustValue = h;
        voice.gustSmooth = h;
        voice.maskGain = 1.0f;
    }

    void updateMotion(float dt)
    {
        motionPhase_ += dt * params_.topologyRateHz;
        if (motionPhase_ > 100000.0f) motionPhase_ -= 100000.0f;

        const uint32_t voices = params_.voices;
        for (uint32_t i = 0u; i < voices; ++i) {
            const float lane = static_cast<float>(i) / static_cast<float>(std::max<uint32_t>(1u, voices - 1u));
            const auto base = basePoint(i, voices);
            const float seedA = hash01(voices_[i].seed + 1009u);
            const float seedB = hash01(voices_[i].seed + 2027u);
            const float phase = motionPhase_ + lane * (0.35f + params_.topologyScale * 1.65f);
            const float swirl = std::sin((phase + seedA) * kPi * 2.0f) * params_.topologyAmount;
            const float sway = std::sin((phase * 0.61f + seedB) * kPi * 2.0f) * params_.topologyDepth;
            const float lift = std::sin((phase * 0.37f + lane * 0.5f) * kPi * 2.0f) * params_.topologyCollapse;
            const float orbitAz = base.azimuthDeg + params_.centerAzimuthDeg + swirl * 72.0f;
            const float orbitEl = clamp(base.elevationDeg + params_.centerElevationDeg + sway * 24.0f + lift * 18.0f, -90.0f, 90.0f);
            const float distMod = 1.0f + std::sin((phase * 0.53f + seedA) * kPi * 2.0f) * params_.field * 0.18f;
            const float dist = clamp(params_.centerDistance * base.distance * distMod, 0.35f, 1.80f);
            targetPoints_[i] = { wrapSignedDeg(orbitAz), orbitEl, dist };
            const float follow = 1.0f - std::exp(-dt * (0.20f + params_.spatialFollow * 4.0f));
            points_[i].azimuthDeg = wrapSignedDeg(points_[i].azimuthDeg + wrapSignedDeg(targetPoints_[i].azimuthDeg - points_[i].azimuthDeg) * follow);
            points_[i].elevationDeg += (targetPoints_[i].elevationDeg - points_[i].elevationDeg) * follow;
            points_[i].distance += (targetPoints_[i].distance - points_[i].distance) * follow;
        }
    }

    float processVoice(uint32_t index)
    {
        auto& voice = voices_[index];
        const uint32_t voices = std::max<uint32_t>(1u, params_.voices);
        const float lane = static_cast<float>(index) / static_cast<float>(std::max<uint32_t>(1u, voices - 1u));
        const float rnd = hashSigned(voice.seed + index * 97u);

        const float wind = laneValue(index, params_.bpWind, params_.wind, 0.52f);
        const float gustDepth = laneValue(index, params_.bpGustDepth, params_.gustDepth, 0.70f);
        const float turbulence = laneValue(index, params_.bpTurbulence, params_.turbulence, 0.62f);
        const float flutter = laneValue(index, params_.bpFlutter, params_.flutter, 0.60f);
        const float material = laneValue(index, params_.bpMaterial, params_.material, 0.70f);
        const float center = laneValue(index, params_.bpCenter, params_.center, 0.65f);
        const float q = laneValue(index, params_.bpQ, params_.q, 0.55f);
        const float air = laneValue(index, params_.bpAir, params_.air, 0.60f);
        const float hiss = laneValue(index, params_.bpHiss, params_.hiss, 0.60f);
        const float sweep = laneValue(index, params_.bpSweep, params_.sweep, 0.70f);
        const float body = laneValue(index, params_.bpBody, params_.body, 0.60f);
        const float gustRateNorm = laneValue(index, params_.bpGustRate, params_.gustRate, 0.55f);

        const float baseRate = freqFromNorm(gustRateNorm, 0.006f, 1.8f) * rateScale(params_.rateMode);
        const float rates[3] {
            baseRate * (0.37f + lane * 0.20f + std::fabs(rnd) * 0.18f),
            baseRate * (0.91f + hash01(voice.seed + 3u) * 0.36f),
            baseRate * (1.73f + hash01(voice.seed + 7u) * 0.92f)
        };
        const float dt = 1.0f / static_cast<float>(sampleRate_);
        float rawGust = 0.0f;
        for (uint32_t i = 0u; i < 3u; ++i) {
            voice.gustPhase[i] += rates[i] * dt;
            voice.gustPhase[i] -= std::floor(voice.gustPhase[i]);
            rawGust += shapedGust(voice.gustPhase[i], (params_.gustShape + i) % 6u) * (i == 0u ? 0.46f : (i == 1u ? 0.34f : 0.20f));
        }
        if (params_.inputA == 1u) rawGust = 1.0f - rawGust;
        if (params_.rungLoop == 1u) rawGust = clamp(rawGust * 0.72f + std::pow(rawGust, 2.5f) * 0.42f, 0.0f, 1.0f);
        else if (params_.rungLoop == 2u) rawGust = rawGust > 0.54f ? 1.0f : rawGust * 0.38f;
        const float smoothCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(sampleRate_) * (0.018f + params_.breath * 0.650f)));
        voice.gustValue += (rawGust - voice.gustValue) * smoothCoeff;
        voice.gustSmooth += (voice.gustValue - voice.gustSmooth) * (smoothCoeff * 0.36f);
        const float gust = lerp(voice.gustSmooth, voice.gustValue, turbulence);

        const float white = noise(voice);
        voice.pinkA += (white - voice.pinkA) * 0.020f;
        voice.pinkB += (voice.pinkA - voice.pinkB) * 0.0035f;
        voice.pinkC += (voice.pinkB - voice.pinkC) * 0.0007f;
        voice.lowNoise += (white - voice.lowNoise) * (0.0004f + wind * 0.004f);
        const float rough = white - voice.pinkA;
        const float pink = white * 0.35f + voice.pinkA * 0.35f + voice.pinkB * 0.22f + voice.pinkC * 0.08f;

        const float gustAmp = 0.06f + wind * 0.32f + gust * gustDepth * 0.42f;
        const float flutterRate = 3.0f + flutter * 38.0f + turbulence * 12.0f;
        const float flutterMod = 0.78f + 0.22f * std::sin((voice.gustPhase[2] * flutterRate + lane) * kPi * 2.0f);
        const float excitation = (pink * (0.48f + body * 0.32f) + rough * (hiss * 0.38f + turbulence * 0.20f) + voice.lowNoise * body * 0.30f) * gustAmp * flutterMod;

        const float modeLift = params_.materialMode == 1u ? 0.18f : (params_.materialMode == 2u ? -0.20f : (params_.materialMode == 3u ? 0.34f : 0.0f));
        const float centerHz = freqFromNorm(clamp(center + modeLift + (gust - 0.5f) * sweep * 0.52f + (lane - 0.5f) * params_.spread * 0.34f, 0.0f, 1.0f), 70.0f, 7600.0f);
        const float resonance = clamp(q * 0.72f + params_.shrill * 0.18f + material * 0.08f, 0.0f, 0.82f);
        const float band = voice.mainFilter.process(excitation, centerHz, resonance, static_cast<float>(sampleRate_));
        const float airBias = params_.inputB == 1u ? 1.45f : 1.0f;
        const float airBand = voice.airFilter.process(rough * (0.16f + hiss * 0.34f * airBias), centerHz * (2.2f + air * 3.6f * airBias), 0.08f + params_.shrill * 0.18f, static_cast<float>(sampleRate_));
        const float bodyBand = voice.bodyFilter.process(pink + voice.lowNoise, centerHz * (0.20f + body * 0.40f), 0.18f + body * 0.45f, static_cast<float>(sampleRate_));

        const float ringTarget = std::sin((voice.gustPhase[1] * (1.0f + material * 12.0f) + lane * 0.5f) * kPi * 2.0f);
        voice.materialRing += (ringTarget - voice.materialRing) * (0.0005f + material * 0.004f);
        const float materialTone = voice.materialRing * material * (params_.materialMode == 4u ? 0.08f : 0.04f);

        const float mixed = band * (0.44f + material * 0.28f)
            + airBand * (0.08f + air * 0.20f)
            + bodyBand * (0.10f + body * 0.22f)
            + materialTone;
        const float drive = 1.0f + params_.grit * 2.2f + turbulence * 0.8f;
        return std::tanh(clamp(mixed * drive, -3.0f, 3.0f)) * (0.18f + wind * 0.42f);
    }

    AmbiWindParams params_ {};
    std::array<AmbiWindVoice, kAmbiWindMaxVoices> voices_ {};
    std::array<AmbiWindPoint, kAmbiWindMaxVoices> points_ {};
    std::array<AmbiWindPoint, kAmbiWindMaxVoices> targetPoints_ {};
    double sampleRate_ = 48000.0;
    float motionPhase_ = 0.0f;
    float smoothedOutputGain_ = dbToGain(-6.0f);
};

} // namespace s3g
