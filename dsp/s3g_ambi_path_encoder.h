#pragma once

#include "s3g_ambi_encoder_depth.h"
#include "s3g_ambisonic_speaker_decoder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAmbiPathEncoderMaxInputs = 64;
constexpr uint32_t kAmbiPathEncoderMaxOrder = 7;
constexpr uint32_t kAmbiPathEncoderMaxChannels = 64;
constexpr uint32_t kAmbiPathEncoderMaxPaths = 16;
constexpr uint32_t kAmbiPathEncoderMaxPoints = 256;

enum class AmbiPathAssignMode : uint32_t {
    One = 0,
    RoundRobin = 1,
    PerSource = 2,
};

enum class AmbiPathPlaybackMode : uint32_t {
    Off = 0,
    Run = 1,
    Scrub = 2,
};

enum class AmbiPathSyncMode : uint32_t {
    Free = 0,
    Sync = 1,
};

enum class AmbiPathLoopMode : uint32_t {
    One = 0,
    Loop = 1,
    Palindrome = 2,
};

enum class AmbiPathInterpolation : uint32_t {
    Linear = 0,
    Catmull = 1,
    Hold = 2,
};

struct AmbiPathPoint {
    float x = 0.0f;
    float y = 1.0f;
    float z = 0.0f;
    float time = 0.0f;
};

struct AmbiPath {
    uint32_t pointCount = 0;
    std::array<AmbiPathPoint, kAmbiPathEncoderMaxPoints> points {};
};

struct AmbiPathEncoderParams {
    uint32_t activeInputs = 64;
    uint32_t order = 3;
    uint32_t activePaths = 1;
    uint32_t selectedPath = 0;
    uint32_t selectedSource = 0;
    AmbiPathAssignMode assignMode = AmbiPathAssignMode::RoundRobin;
    AmbiPathPlaybackMode playback = AmbiPathPlaybackMode::Run;
    AmbiPathSyncMode syncMode = AmbiPathSyncMode::Free;
    AmbiPathLoopMode loopMode = AmbiPathLoopMode::Loop;
    AmbiPathInterpolation interpolation = AmbiPathInterpolation::Catmull;
    float rateHz = 0.08f;
    float syncDivisionBeats = 4.0f;
    float phase = 0.0f;
    float phaseSpread = 0.0f;
    float smooth = 0.12f;
    float ease = 0.0f;
    float distanceScale = 1.0f;
    float doppler = 0.0f;
    float air = 0.0f;
    float outputGainDb = -12.0f;
};

class AmbiPathEncoder {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        depth_.prepare(sampleRate_);
        resetPaths();
        reset();
    }

    void reset()
    {
        transportPhase_ = params_.phase;
        for (auto& p : smoothPositions_) p = { 0.0f, 1.0f, 0.0f };
        smoothPrimed_ = false;
        depth_.reset();
    }

    void resetPaths()
    {
        for (uint32_t i = 0; i < kAmbiPathEncoderMaxPaths; ++i) {
            paths_[i].pointCount = 8;
            const float radius = 1.0f;
            for (uint32_t p = 0; p < paths_[i].pointCount; ++p) {
                const float t = static_cast<float>(p) / static_cast<float>(paths_[i].pointCount);
                const float az = (-90.0f + t * 360.0f + static_cast<float>(i) * 17.0f) * kPi / 180.0f;
                paths_[i].points[p].x = std::sin(az) * radius;
                paths_[i].points[p].y = std::cos(az) * radius;
                paths_[i].points[p].z = std::sin(t * 2.0f * kPi + static_cast<float>(i)) * 0.18f;
                paths_[i].points[p].time = t;
            }
        }
    }

    void setParams(AmbiPathEncoderParams params)
    {
        params.activeInputs = std::clamp<uint32_t>(params.activeInputs, 1u, kAmbiPathEncoderMaxInputs);
        params.order = std::clamp<uint32_t>(params.order, 1u, kAmbiPathEncoderMaxOrder);
        params.activePaths = std::clamp<uint32_t>(params.activePaths, 1u, kAmbiPathEncoderMaxPaths);
        params.selectedPath = std::min<uint32_t>(params.selectedPath, params.activePaths - 1u);
        params.selectedSource = std::min<uint32_t>(params.selectedSource, params.activeInputs - 1u);
        params.assignMode = static_cast<AmbiPathAssignMode>(std::clamp<uint32_t>(static_cast<uint32_t>(params.assignMode), 0u, 2u));
        params.playback = static_cast<AmbiPathPlaybackMode>(std::clamp<uint32_t>(static_cast<uint32_t>(params.playback), 0u, 2u));
        params.syncMode = static_cast<AmbiPathSyncMode>(std::clamp<uint32_t>(static_cast<uint32_t>(params.syncMode), 0u, 1u));
        params.loopMode = static_cast<AmbiPathLoopMode>(std::clamp<uint32_t>(static_cast<uint32_t>(params.loopMode), 0u, 2u));
        params.interpolation = static_cast<AmbiPathInterpolation>(std::clamp<uint32_t>(static_cast<uint32_t>(params.interpolation), 0u, 2u));
        params.rateHz = clamp(params.rateHz, 0.001f, 4.0f);
        params.syncDivisionBeats = clamp(params.syncDivisionBeats, 0.25f, 64.0f);
        params.phase = fract(params.phase);
        params.phaseSpread = clamp(params.phaseSpread, 0.0f, 1.0f);
        params.smooth = clamp(params.smooth, 0.0f, 0.995f);
        params.ease = clamp(params.ease, 0.0f, 1.0f);
        params.distanceScale = clamp(params.distanceScale, 0.05f, 8.0f);
        params.doppler = clamp(params.doppler, 0.0f, 1.0f);
        params.air = clamp(params.air, 0.0f, 1.0f);
        params.outputGainDb = clamp(params.outputGainDb, -60.0f, 12.0f);
        params_ = params;
        depth_.setParams({ params_.doppler, params_.air });
    }

    AmbiPathEncoderParams params() const { return params_; }
    const std::array<AmbiPath, kAmbiPathEncoderMaxPaths>& paths() const { return paths_; }

    void setExternalPhase(float phase)
    {
        externalPhaseActive_ = true;
        transportPhase_ = fract(phase);
    }

    void useFreePhase()
    {
        externalPhaseActive_ = false;
    }

    void setPaths(const std::array<AmbiPath, kAmbiPathEncoderMaxPaths>& paths)
    {
        paths_ = paths;
        sanitizePaths();
    }

    void setPath(uint32_t index, const AmbiPath& path)
    {
        if (index >= kAmbiPathEncoderMaxPaths) return;
        paths_[index] = path;
        sanitizePath(paths_[index]);
    }

    Vec3 sourcePositionForDisplay(uint32_t source) const
    {
        return evaluateSource(source, true);
    }

    template <typename Sample>
    void processBlock(const Sample* const* inputs, Sample* const* outputs, uint32_t inputChannels, uint32_t outputChannels, uint32_t frames)
    {
        if (!outputs || frames == 0u) return;
        outputChannels = std::min<uint32_t>(outputChannels, kAmbiPathEncoderMaxChannels);
        for (uint32_t ch = 0; ch < outputChannels; ++ch) {
            if (outputs[ch]) std::fill(outputs[ch], outputs[ch] + frames, static_cast<Sample>(0));
        }
        if (!inputs) return;

        const uint32_t ambiChannels = std::min<uint32_t>(ambiChannelsForOrder(params_.order), outputChannels);
        const uint32_t inputCount = std::min<uint32_t>({ inputChannels, params_.activeInputs, kAmbiPathEncoderMaxInputs });
        const float outGain = dbToGain(params_.outputGainDb);
        const float norm = 1.0f / std::sqrt(static_cast<float>(std::max<uint32_t>(1u, inputCount)));
        const float phaseStart = transportPhase_;
        const float phaseInc = (params_.playback == AmbiPathPlaybackMode::Run
            && !(params_.syncMode == AmbiPathSyncMode::Sync && externalPhaseActive_))
            ? params_.rateHz / static_cast<float>(sampleRate_)
            : 0.0f;
        constexpr uint32_t kMotionChunkFrames = 16;

        for (uint32_t chunkStart = 0; chunkStart < frames; chunkStart += kMotionChunkFrames) {
            const uint32_t chunkFrames = std::min<uint32_t>(kMotionChunkFrames, frames - chunkStart);
            const float chunkPhase = phaseStart + phaseInc * static_cast<float>(chunkStart);
            std::array<std::array<float, kAmbiPathEncoderMaxChannels>, kAmbiPathEncoderMaxInputs> basis {};
            std::array<float, kAmbiPathEncoderMaxInputs> gains {};
            std::array<float, kAmbiPathEncoderMaxInputs> distances {};
            for (uint32_t src = 0; src < inputCount; ++src) {
                Vec3 pos = evaluateSourceAtPhase(src, false, chunkPhase);
                if (!smoothPrimed_) smoothPositions_[src] = pos;
                else {
                    const float alpha = 1.0f - params_.smooth;
                    smoothPositions_[src].x += (pos.x - smoothPositions_[src].x) * alpha;
                    smoothPositions_[src].y += (pos.y - smoothPositions_[src].y) * alpha;
                    smoothPositions_[src].z += (pos.z - smoothPositions_[src].z) * alpha;
                }
                pos = smoothPositions_[src];
                const float d = std::sqrt(pos.x * pos.x + pos.y * pos.y + pos.z * pos.z);
                distances[src] = d;
                basis[src] = acnSn3dBasis7(normalize(pos));
                gains[src] = outGain * norm / std::max(0.15f, d);
            }
            smoothPrimed_ = true;

            for (uint32_t frame = chunkStart; frame < chunkStart + chunkFrames; ++frame) {
                for (uint32_t src = 0; src < inputCount; ++src) {
                    const Sample* in = inputs[src];
                    if (!in) continue;
                    const float sample = depth_.process(src, static_cast<float>(in[frame]) * gains[src], distances[src]);
                    if (sample == 0.0f) continue;
                    for (uint32_t ch = 0; ch < ambiChannels; ++ch) {
                        if (outputs[ch]) outputs[ch][frame] = static_cast<Sample>(flushDenormal(static_cast<float>(outputs[ch][frame]) + sample * basis[src][ch]));
                    }
                }
                depth_.advance();
            }
        }
        advance(frames);
    }

private:
    static float fract(float v)
    {
        return v - std::floor(v);
    }

    static float smoother(float t, float amount)
    {
        const float s = t * t * (3.0f - 2.0f * t);
        return lerp(t, s, amount);
    }

    static Vec3 pointVec(const AmbiPathPoint& p)
    {
        return { p.x, p.y, p.z };
    }

    static Vec3 mixVec(Vec3 a, Vec3 b, float t)
    {
        return { lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.z, b.z, t) };
    }

    static Vec3 catmull(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, float t)
    {
        const float t2 = t * t;
        const float t3 = t2 * t;
        return {
            0.5f * ((2.0f * p1.x) + (-p0.x + p2.x) * t + (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 + (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3),
            0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * t + (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 + (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3),
            0.5f * ((2.0f * p1.z) + (-p0.z + p2.z) * t + (2.0f * p0.z - 5.0f * p1.z + 4.0f * p2.z - p3.z) * t2 + (-p0.z + 3.0f * p1.z - 3.0f * p2.z + p3.z) * t3)
        };
    }

    void advance(uint32_t frames)
    {
        if (params_.playback != AmbiPathPlaybackMode::Run) {
            transportPhase_ = params_.phase;
            return;
        }
        if (params_.syncMode == AmbiPathSyncMode::Sync && externalPhaseActive_) return;
        transportPhase_ += static_cast<float>(frames) / static_cast<float>(sampleRate_) * params_.rateHz;
        if (transportPhase_ > 100000.0f) transportPhase_ -= std::floor(transportPhase_);
    }

    uint32_t pathIndexForSource(uint32_t source) const
    {
        switch (params_.assignMode) {
        case AmbiPathAssignMode::One: return std::min<uint32_t>(params_.selectedPath, params_.activePaths - 1u);
        case AmbiPathAssignMode::PerSource: return source % std::max<uint32_t>(1u, params_.activePaths);
        case AmbiPathAssignMode::RoundRobin:
        default: return source % std::max<uint32_t>(1u, params_.activePaths);
        }
    }

    float phaseForSource(uint32_t source, float phaseBase) const
    {
        const float base = params_.playback == AmbiPathPlaybackMode::Scrub ? params_.phase : phaseBase + params_.phase;
        const float raw = base + params_.phaseSpread * static_cast<float>(source) / static_cast<float>(std::max<uint32_t>(1u, params_.activeInputs));
        float ph = fract(raw);
        if (params_.loopMode == AmbiPathLoopMode::Palindrome) {
            const float p = fract(raw * 0.5f) * 2.0f;
            ph = p <= 1.0f ? p : 2.0f - p;
        } else if (params_.loopMode == AmbiPathLoopMode::One) {
            ph = std::min(1.0f, std::max(0.0f, base));
        }
        return smoother(ph, params_.ease);
    }

    Vec3 evaluateSource(uint32_t source, bool display) const
    {
        return evaluateSourceAtPhase(source, display, transportPhase_);
    }

    Vec3 evaluateSourceAtPhase(uint32_t source, bool display, float phaseBase) const
    {
        const uint32_t pathIndex = pathIndexForSource(source);
        Vec3 p = evaluatePath(paths_[pathIndex], phaseForSource(source, phaseBase));
        const float scale = display ? std::pow(params_.distanceScale, 0.55f) : params_.distanceScale;
        p.x *= scale;
        p.y *= scale;
        p.z *= scale;
        return p;
    }

    Vec3 evaluatePath(const AmbiPath& path, float phase) const
    {
        if (path.pointCount == 0u) return { 0.0f, 1.0f, 0.0f };
        const uint32_t n = std::clamp<uint32_t>(path.pointCount, 1u, kAmbiPathEncoderMaxPoints);
        if (n == 1u) return pointVec(path.points[0]);
        phase = clamp(phase, 0.0f, 1.0f);
        const float segFloat = phase * static_cast<float>(n);
        const uint32_t i1 = std::min<uint32_t>(static_cast<uint32_t>(std::floor(segFloat)), n - 1u);
        const uint32_t i2 = (i1 + 1u) % n;
        float t = segFloat - std::floor(segFloat);
        if (params_.loopMode == AmbiPathLoopMode::One && i1 >= n - 1u) {
            return pointVec(path.points[n - 1u]);
        }
        if (params_.interpolation == AmbiPathInterpolation::Hold) return pointVec(path.points[i1]);
        if (params_.interpolation == AmbiPathInterpolation::Linear || n < 4u) return mixVec(pointVec(path.points[i1]), pointVec(path.points[i2]), t);

        const uint32_t i0 = (i1 + n - 1u) % n;
        const uint32_t i3 = (i2 + 1u) % n;
        return catmull(pointVec(path.points[i0]), pointVec(path.points[i1]), pointVec(path.points[i2]), pointVec(path.points[i3]), t);
    }

    static void sanitizePath(AmbiPath& path)
    {
        path.pointCount = std::min<uint32_t>(path.pointCount, kAmbiPathEncoderMaxPoints);
        for (uint32_t i = 0; i < path.pointCount; ++i) {
            auto& p = path.points[i];
            p.x = clamp(p.x, -8.0f, 8.0f);
            p.y = clamp(p.y, -8.0f, 8.0f);
            p.z = clamp(p.z, -8.0f, 8.0f);
            p.time = clamp(p.time, 0.0f, 1.0f);
            if (std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z) < 0.0001f) {
                p.y = 0.15f;
            }
        }
    }

    void sanitizePaths()
    {
        for (auto& path : paths_) sanitizePath(path);
    }

    double sampleRate_ = 48000.0;
    AmbiPathEncoderParams params_ {};
    std::array<AmbiPath, kAmbiPathEncoderMaxPaths> paths_ {};
    std::array<Vec3, kAmbiPathEncoderMaxInputs> smoothPositions_ {};
    AmbiEncoderDepthProcessor<kAmbiPathEncoderMaxInputs> depth_ {};
    float transportPhase_ = 0.0f;
    bool externalPhaseActive_ = false;
    bool smoothPrimed_ = false;
};

} // namespace s3g
