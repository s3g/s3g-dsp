#pragma once

#include "s3g_ambi_encoder_depth.h"
#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAmbiTerrainMaxOrder = 7;
constexpr uint32_t kAmbiTerrainMaxChannels = 64;
constexpr uint32_t kAmbiTerrainMaxPoints = 64;
constexpr float kAmbiTerrainTwoPi = 6.28318530717958647692f;

enum class AmbiTerrainOrbit : uint32_t {
    Drift = 0,
    Lissajous = 1,
    Spiral = 2,
    Fold = 3,
};

enum class AmbiTerrainPalette : uint32_t {
    Harmonic = 0,
    Fbm = 1,
    Cellular = 2,
    Vot = 3,
    Ridges = 4,
    Dunes = 5,
    Craters = 6,
    Tectonic = 7,
};

enum class AmbiTerrainPlaybackMode : uint32_t {
    Off = 0,
    Run = 1,
    Scrub = 2,
};

enum class AmbiTerrainSyncMode : uint32_t {
    Free = 0,
    Sync = 1,
};

struct AmbiTerrainPoint {
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float distance = 1.0f;
    float terrain = 0.0f;
    uint32_t shell = 0u;
};

struct AmbiTerrainNavigatorParams {
    uint32_t order = 3;
    uint32_t points = 16;
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float distance = 1.0f;
    float rateHz = 0.035f;
    float traversal = 0.70f;
    float terrainDepth = 0.55f;
    float layerSpread = 0.62f;
    float innerRadius = 0.42f;
    float outerRadius = 1.35f;
    float azimuthWarpDeg = 72.0f;
    float elevationWarpDeg = 34.0f;
    float distanceWarp = 0.34f;
    float fold = 0.20f;
    float smoothing = 0.72f;
    float inputGainDb = 0.0f;
    float outputGainDb = -9.0f;
    AmbiTerrainOrbit orbit = AmbiTerrainOrbit::Lissajous;
    AmbiTerrainPalette palette = AmbiTerrainPalette::Fbm;
    AmbiTerrainPlaybackMode playback = AmbiTerrainPlaybackMode::Run;
    AmbiTerrainSyncMode syncMode = AmbiTerrainSyncMode::Free;
    float syncDivisionBeats = 4.0f;
    float phase = 0.0f;
    float phaseSpread = 0.0f;
    float ease = 0.0f;
    float distanceScale = 1.0f;
    float doppler = 0.0f;
    float air = 0.0f;
    uint32_t selectedSource = 0;
    float rateSpread = 0.35f;
    float rateDeviation = 0.18f;
};

class AmbiTerrainNavigator {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        depth_.prepare(sampleRate_);
        reset();
    }

    void reset()
    {
        externalPhase_ = 0.0f;
        for (uint32_t i = 0; i < kAmbiTerrainMaxPoints; ++i) {
            motionPhases_[i] = 0.0f;
            smoothPoints_[i] = {};
            smoothPrimed_[i] = false;
        }
        depth_.reset();
    }

    void setParams(AmbiTerrainNavigatorParams params)
    {
        params_ = sanitize(params);
        depth_.setParams({ params_.doppler, params_.air });
    }

    AmbiTerrainNavigatorParams params() const { return params_; }
    AmbiTerrainPoint point(uint32_t index = 0) const { return smoothPoints_[std::min<uint32_t>(index, kAmbiTerrainMaxPoints - 1u)]; }
    const std::array<AmbiTerrainPoint, kAmbiTerrainMaxPoints>& points() const { return smoothPoints_; }

    AmbiTerrainPoint pathPointForDisplay(uint32_t pointIndex, float phase) const
    {
        const float sourcePhase = params_.phaseSpread * static_cast<float>(pointIndex)
            / static_cast<float>(std::max<uint32_t>(1u, params_.points));
        return rawPoint(pointIndex, easePhase(fract(phase + sourcePhase), params_.ease));
    }

    AmbiTerrainPoint surfacePointForDisplay(float azimuthUnit, float elevationUnit) const
    {
        return surfacePoint(fract(azimuthUnit), clamp(elevationUnit, 0.0f, 1.0f));
    }

    float rateForPoint(uint32_t pointIndex) const
    {
        return params_.rateHz * rateMultiplier(pointIndex);
    }

    void setExternalPhase(float phase)
    {
        externalPhaseActive_ = true;
        externalPhase_ = fract(phase);
    }

    void useFreePhase() { externalPhaseActive_ = false; }

    void processBlock(const float* const* inputs, float* const* outputs, uint32_t inputChannels, uint32_t outputChannels, uint32_t frames)
    {
        if (!outputs || frames == 0u) return;
        outputChannels = std::min<uint32_t>(outputChannels, kAmbiTerrainMaxChannels);
        for (uint32_t ch = 0; ch < outputChannels; ++ch) {
            if (outputs[ch]) std::fill(outputs[ch], outputs[ch] + frames, 0.0f);
        }
        inputChannels = std::min<uint32_t>(inputChannels, kAmbiTerrainMaxPoints);
        if (!inputs || inputChannels == 0u || outputChannels == 0u) return;

        const uint32_t ambiChannels = std::min<uint32_t>(ambiChannelsForOrder(params_.order), outputChannels);
        const uint32_t activePoints = std::min<uint32_t>(params_.points, inputChannels);
        constexpr uint32_t kMotionChunkFrames = 16;
        const float inputGain = dbToGain(params_.inputGainDb);
        const float outputGain = dbToGain(params_.outputGainDb);
        const float normGain = 1.0f / std::sqrt(static_cast<float>(std::max<uint32_t>(1u, activePoints)));

        for (uint32_t chunkStart = 0; chunkStart < frames; chunkStart += kMotionChunkFrames) {
            const uint32_t chunkFrames = std::min<uint32_t>(kMotionChunkFrames, frames - chunkStart);
            const float dt = static_cast<float>(chunkFrames) / static_cast<float>(sampleRate_);
            std::array<AmbiTerrainPoint, kAmbiTerrainMaxPoints> from {};
            std::array<AmbiTerrainPoint, kAmbiTerrainMaxPoints> to {};
            for (uint32_t pointIndex = 0; pointIndex < activePoints; ++pointIndex) {
                from[pointIndex] = currentPoint(pointIndex);
            }
            advance(dt);
            for (uint32_t pointIndex = 0; pointIndex < activePoints; ++pointIndex) {
                to[pointIndex] = currentPoint(pointIndex);
            }

            for (uint32_t frame = chunkStart; frame < chunkStart + chunkFrames; ++frame) {
                const float t = static_cast<float>(frame - chunkStart + 1u) / static_cast<float>(chunkFrames);
                for (uint32_t pointIndex = 0; pointIndex < activePoints; ++pointIndex) {
                    const float* input = inputs[pointIndex];
                    if (!input) continue;
                    AmbiTerrainPoint p {};
                    p.azimuthDeg = lerpAngleDeg(from[pointIndex].azimuthDeg, to[pointIndex].azimuthDeg, t);
                    p.elevationDeg = lerp(from[pointIndex].elevationDeg, to[pointIndex].elevationDeg, t);
                    p.distance = lerp(from[pointIndex].distance, to[pointIndex].distance, t);
                    p.terrain = lerp(from[pointIndex].terrain, to[pointIndex].terrain, t);
                    const auto basis = acnSn3dBasis7(directionFromAed(p.azimuthDeg, p.elevationDeg));
                    const float distanceGain = 1.0f / std::max(0.25f, p.distance);
                    const float shaped = softSat(input[frame] * inputGain * (1.0f + 0.12f * p.terrain));
                    const float sample = depth_.process(pointIndex, shaped * outputGain * distanceGain * normGain, p.distance);
                    for (uint32_t ch = 0; ch < ambiChannels; ++ch) {
                        if (outputs[ch]) outputs[ch][frame] = flushDenormal(outputs[ch][frame] + sample * basis[ch]);
                    }
                }
                depth_.advance();
            }
        }
    }

private:
    static AmbiTerrainNavigatorParams sanitize(AmbiTerrainNavigatorParams p)
    {
        p.order = std::clamp<uint32_t>(p.order, 1u, kAmbiTerrainMaxOrder);
        p.points = std::clamp<uint32_t>(p.points, 1u, kAmbiTerrainMaxPoints);
        p.azimuthDeg = wrapSignedDeg(p.azimuthDeg);
        p.elevationDeg = clamp(p.elevationDeg, -90.0f, 90.0f);
        p.distance = clamp(p.distance, 0.15f, 3.0f);
        p.rateHz = clamp(p.rateHz, 0.000001f, 2.0f);
        p.traversal = clamp(p.traversal, 0.0f, 1.0f);
        p.terrainDepth = clamp(p.terrainDepth, 0.0f, 1.0f);
        p.layerSpread = clamp(p.layerSpread, 0.0f, 1.0f);
        p.innerRadius = clamp(p.innerRadius, 0.05f, 2.0f);
        p.outerRadius = clamp(std::max(p.outerRadius, p.innerRadius + 0.05f), 0.10f, 3.0f);
        p.azimuthWarpDeg = clamp(p.azimuthWarpDeg, 0.0f, 180.0f);
        p.elevationWarpDeg = clamp(p.elevationWarpDeg, 0.0f, 90.0f);
        p.distanceWarp = clamp(p.distanceWarp, 0.0f, 1.0f);
        p.fold = clamp(p.fold, 0.0f, 1.0f);
        p.smoothing = clamp(p.smoothing, 0.0f, 0.995f);
        p.inputGainDb = clamp(p.inputGainDb, -60.0f, 24.0f);
        p.outputGainDb = clamp(p.outputGainDb, -60.0f, 12.0f);
        p.orbit = static_cast<AmbiTerrainOrbit>(std::clamp<uint32_t>(static_cast<uint32_t>(p.orbit), 0u, 3u));
        p.palette = static_cast<AmbiTerrainPalette>(std::clamp<uint32_t>(static_cast<uint32_t>(p.palette), 0u, 7u));
        p.playback = static_cast<AmbiTerrainPlaybackMode>(std::clamp<uint32_t>(static_cast<uint32_t>(p.playback), 0u, 2u));
        p.syncMode = static_cast<AmbiTerrainSyncMode>(std::clamp<uint32_t>(static_cast<uint32_t>(p.syncMode), 0u, 1u));
        p.syncDivisionBeats = clamp(p.syncDivisionBeats, 0.25f, 64.0f);
        p.phase = fract(p.phase);
        p.phaseSpread = clamp(p.phaseSpread, 0.0f, 1.0f);
        p.ease = clamp(p.ease, 0.0f, 1.0f);
        p.distanceScale = clamp(p.distanceScale, 0.05f, 8.0f);
        p.doppler = clamp(p.doppler, 0.0f, 1.0f);
        p.air = clamp(p.air, 0.0f, 1.0f);
        p.selectedSource = std::min<uint32_t>(p.selectedSource, p.points - 1u);
        p.rateSpread = clamp(p.rateSpread, 0.0f, 1.0f);
        p.rateDeviation = clamp(p.rateDeviation, 0.0f, 1.0f);
        return p;
    }

    static float wrapSignedDeg(float value)
    {
        while (value > 180.0f) value -= 360.0f;
        while (value <= -180.0f) value += 360.0f;
        return value;
    }

    static float fract(float v)
    {
        return v - std::floor(v);
    }

    static float smoothstep(float v)
    {
        v = clamp(v, 0.0f, 1.0f);
        return v * v * (3.0f - 2.0f * v);
    }

    static float easePhase(float v, float amount)
    {
        return lerp(v, smoothstep(v), amount);
    }

    static float lerpAngleDeg(float a, float b, float t)
    {
        float delta = b - a;
        while (delta > 180.0f) delta -= 360.0f;
        while (delta < -180.0f) delta += 360.0f;
        return wrapSignedDeg(a + delta * t);
    }

    static float layerCell(float x, float y, float z)
    {
        const float cx = std::round(x);
        const float cy = std::round(y);
        const float cz = std::round(z);
        const float dx = x - cx;
        const float dy = y - cy;
        const float dz = z - cz;
        return 1.0f - clamp(std::sqrt(dx * dx + dy * dy + dz * dz) * 1.35f, 0.0f, 1.0f);
    }

    static float signedHash(uint32_t index)
    {
        uint32_t x = index + 0x9e3779b9u;
        x ^= x >> 16u;
        x *= 0x7feb352du;
        x ^= x >> 15u;
        x *= 0x846ca68bu;
        x ^= x >> 16u;
        return static_cast<float>(x & 0x00ffffffu) / 8388607.5f - 1.0f;
    }

    float rateMultiplier(uint32_t pointIndex) const
    {
        const uint32_t count = std::max<uint32_t>(1u, params_.points);
        const float position = count > 1u
            ? static_cast<float>(std::min<uint32_t>(pointIndex, count - 1u)) / static_cast<float>(count - 1u) * 2.0f - 1.0f
            : 0.0f;
        const float spreadOctaves = position * params_.rateSpread * 3.0f;
        const float deviationOctaves = signedHash(pointIndex) * params_.rateDeviation * 2.0f;
        return std::exp2(clamp(spreadOctaves + deviationOctaves, -6.0f, 6.0f));
    }

    float terrainLayer(float azUnit, float elUnit, float radius, uint32_t layer) const
    {
        const float azimuthDeg = (azUnit - 0.5f) * 360.0f;
        const float elevationDeg = (elUnit - 0.5f) * 180.0f;
        const Vec3 direction = directionFromAed(azimuthDeg, elevationDeg);
        const float frequency = 1.15f + static_cast<float>(layer) * 0.92f;
        const float x = direction.x * frequency + static_cast<float>(layer) * 0.31f;
        const float y = direction.y * (frequency + 0.37f) - static_cast<float>(layer) * 0.17f;
        const float z = direction.z * (frequency + 0.71f) + radius * 0.23f;
        const float harmonic = std::sin(kPi * (x + 0.31f * z)) * std::cos(kPi * (y - 0.23f * z));
        const float diagonal = std::sin(kPi * (x * 0.73f + y * 0.91f + z * 1.17f));
        const float cell = layerCell(x * 1.55f + layer * 0.37f, y * 1.55f, z * 1.55f);
        const float cellular = cell * 2.0f - 1.0f;
        const float fbm = 0.55f * harmonic + 0.32f * diagonal
            + 0.13f * std::sin(kPi * (x * 2.3f - y * 1.7f + z * 0.41f));
        const float ridges = 1.0f - 2.0f * std::fabs(fbm);
        const float dunes = std::sin(kAmbiTerrainTwoPi * (0.19f * x + 0.08f * std::sin(kPi * (y + z))))
            * (0.72f + 0.28f * std::cos(kPi * z));
        const float craterRim = std::exp(-std::pow((cell - 0.52f) * 7.0f, 2.0f)) * 2.0f - 1.0f;
        const float craters = clamp(0.70f * craterRim - 0.72f * cell + 0.18f * harmonic, -1.0f, 1.0f);
        const float tectonic = softSat(1.55f * diagonal + 0.75f * (std::fabs(harmonic) * 2.0f - 1.0f));
        switch (params_.palette) {
        case AmbiTerrainPalette::Harmonic: return 0.70f * harmonic + 0.30f * diagonal;
        case AmbiTerrainPalette::Cellular: return 0.75f * cellular + 0.25f * harmonic;
        case AmbiTerrainPalette::Vot: return softSat(1.30f * fbm + 0.45f * cellular);
        case AmbiTerrainPalette::Ridges: return ridges;
        case AmbiTerrainPalette::Dunes: return dunes;
        case AmbiTerrainPalette::Craters: return craters;
        case AmbiTerrainPalette::Tectonic: return tectonic;
        case AmbiTerrainPalette::Fbm:
        default: return fbm;
        }
    }

    float terrainAt(float azUnit, float elUnit) const
    {
        float terrain = 0.0f;
        float weightSum = 0.0f;
        for (uint32_t layer = 0; layer < 4u; ++layer) {
            const float u = static_cast<float>(layer) / 3.0f;
            const float radius = lerp(params_.innerRadius, params_.outerRadius, u);
            const float weight = std::pow(1.0f - u * 0.45f, 1.0f + params_.layerSpread);
            terrain += terrainLayer(azUnit, elUnit, radius, layer) * weight;
            weightSum += weight;
        }
        terrain = weightSum > 0.0f ? terrain / weightSum : 0.0f;
        const float folded = std::sin(terrain * (1.0f + 4.0f * params_.fold));
        return lerp(terrain, folded, params_.fold);
    }

    AmbiTerrainPoint surfacePoint(float azUnit, float elUnit) const
    {
        const float terrain = terrainAt(azUnit, elUnit);
        const float shellHeight = terrain * params_.terrainDepth * (0.16f + 0.24f * params_.distanceWarp);
        AmbiTerrainPoint out {};
        out.azimuthDeg = wrapSignedDeg((azUnit - 0.5f) * 360.0f + terrain * params_.azimuthWarpDeg * params_.terrainDepth);
        out.elevationDeg = clamp((elUnit - 0.5f) * 180.0f + terrain * params_.elevationWarpDeg * params_.terrainDepth, -90.0f, 90.0f);
        out.distance = clamp((params_.distance + shellHeight) * params_.distanceScale, 0.15f, 8.0f);
        out.terrain = terrain;
        out.shell = 0u;
        return out;
    }

    float phaseForPoint(uint32_t pointIndex) const
    {
        const float sourcePhase = params_.phaseSpread * static_cast<float>(pointIndex)
            / static_cast<float>(std::max<uint32_t>(1u, params_.points));
        float motion = 0.0f;
        if (params_.playback == AmbiTerrainPlaybackMode::Run) {
            motion = params_.syncMode == AmbiTerrainSyncMode::Sync && externalPhaseActive_
                ? externalPhase_ * rateMultiplier(pointIndex)
                : motionPhases_[std::min<uint32_t>(pointIndex, kAmbiTerrainMaxPoints - 1u)];
        }
        return easePhase(fract(motion + params_.phase + sourcePhase), params_.ease);
    }

    AmbiTerrainPoint rawPoint(uint32_t pointIndex, float pathPhase) const
    {
        const float lane = static_cast<float>(pointIndex);
        const float count = static_cast<float>(std::max<uint32_t>(1u, params_.points));
        const float orbitAmount = params_.traversal;
        const float laneAz = lane * 137.507764f;
        const float laneEl = std::asin(clamp(1.0f - 2.0f * ((lane + 0.5f) / count), -1.0f, 1.0f)) * 180.0f / kPi;
        float azUnit = fract((params_.azimuthDeg + laneAz * params_.layerSpread) / 360.0f + 0.5f);
        float elUnit = clamp((params_.elevationDeg + laneEl * params_.layerSpread) / 180.0f + 0.5f, 0.0f, 1.0f);
        const float tracePhase = fract(pathPhase);
        switch (params_.orbit) {
        case AmbiTerrainOrbit::Drift:
            azUnit = fract(azUnit + std::sin(kAmbiTerrainTwoPi * tracePhase) * 0.26f * orbitAmount);
            elUnit = clamp(elUnit + std::sin(kAmbiTerrainTwoPi * (tracePhase + 0.17f)) * 0.20f * orbitAmount, 0.0f, 1.0f);
            break;
        case AmbiTerrainOrbit::Spiral:
            azUnit = fract(azUnit + tracePhase);
            elUnit = clamp(elUnit + std::sin(kAmbiTerrainTwoPi * tracePhase) * 0.46f * orbitAmount, 0.0f, 1.0f);
            break;
        case AmbiTerrainOrbit::Fold:
            azUnit = fract(azUnit + std::sin(kAmbiTerrainTwoPi * tracePhase) * 0.31f * orbitAmount);
            elUnit = 0.5f + std::asin(clamp(std::sin(kAmbiTerrainTwoPi * (tracePhase * 2.0f + azUnit)), -1.0f, 1.0f)) / kPi * orbitAmount;
            break;
        case AmbiTerrainOrbit::Lissajous:
        default:
            azUnit = fract(azUnit + std::sin(kAmbiTerrainTwoPi * tracePhase) * 0.27f * orbitAmount);
            elUnit = clamp(elUnit + std::sin(kAmbiTerrainTwoPi * (tracePhase * 2.0f + 0.25f)) * 0.24f * orbitAmount, 0.0f, 1.0f);
            break;
        }
        return surfacePoint(azUnit, elUnit);
    }

    AmbiTerrainPoint currentPoint(uint32_t pointIndex)
    {
        const uint32_t safe = std::min<uint32_t>(pointIndex, kAmbiTerrainMaxPoints - 1u);
        const AmbiTerrainPoint raw = rawPoint(safe, phaseForPoint(safe));
        if (!smoothPrimed_[safe]) {
            smoothPoints_[safe] = raw;
            smoothPrimed_[safe] = true;
            return smoothPoints_[safe];
        }
        const float follow = 1.0f - params_.smoothing;
        smoothPoints_[safe].azimuthDeg = lerpAngleDeg(smoothPoints_[safe].azimuthDeg, raw.azimuthDeg, follow);
        smoothPoints_[safe].elevationDeg = lerp(smoothPoints_[safe].elevationDeg, raw.elevationDeg, follow);
        smoothPoints_[safe].distance = lerp(smoothPoints_[safe].distance, raw.distance, follow);
        smoothPoints_[safe].terrain = lerp(smoothPoints_[safe].terrain, raw.terrain, follow);
        if (std::fabs(smoothPoints_[safe].distance - raw.distance) < 0.035f) smoothPoints_[safe].shell = raw.shell;
        return smoothPoints_[safe];
    }

    void advance(float dt)
    {
        if (params_.playback != AmbiTerrainPlaybackMode::Run) return;
        if (params_.syncMode == AmbiTerrainSyncMode::Sync && externalPhaseActive_) return;
        for (uint32_t pointIndex = 0; pointIndex < params_.points; ++pointIndex) {
            motionPhases_[pointIndex] = fract(motionPhases_[pointIndex] + rateForPoint(pointIndex) * dt);
        }
    }

    double sampleRate_ = 48000.0;
    AmbiTerrainNavigatorParams params_ {};
    std::array<float, kAmbiTerrainMaxPoints> motionPhases_ {};
    std::array<AmbiTerrainPoint, kAmbiTerrainMaxPoints> smoothPoints_ {};
    std::array<bool, kAmbiTerrainMaxPoints> smoothPrimed_ {};
    AmbiEncoderDepthProcessor<kAmbiTerrainMaxPoints> depth_ {};
    bool externalPhaseActive_ = false;
    float externalPhase_ = 0.0f;
};

} // namespace s3g
