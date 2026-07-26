#pragma once

#include "s3g_math.h"
#include "s3g_realtime.h"
#include "s3g_topology.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace s3g {

constexpr uint32_t kWaveGeometryChannels = 8;

struct WaveGeometryParams {
    float fold = 0.22f;
    float drive = 0.18f;
    float hold = 0.0f;
    float clip = 0.18f;
    float rectify = 0.0f;
    float edge = 0.0f;
    float zero = 0.0f;
    float polar = 0.0f;
    float bits = 0.0f;
    float step = 0.0f;
    float trans = 0.0f;
    float tape = 0.0f;
    float speed = 0.25f;
    float mix = 0.72f;
    float gainDb = -3.0f;
    float safety = 0.82f;
};

struct WaveGeometryMeshParams {
    float coupling = 0.0f;
    float tension = 0.62f;
    float decay = 0.35f;
    float damping = 0.45f;
};

struct WaveGeometrySettings {
    WaveGeometryParams base {};
    TopologyState topology {};
    WaveGeometryMeshParams mesh {};
};

inline float waveGeometryFold(float x, float fold)
{
    fold = std::clamp(fold, 0.0f, 1.0f);
    if (fold <= 0.0001f) return x;

    const float pre = x * lerp(1.0f, 3.4f, fold);
    const float softStage = std::sin(pre);
    const float softBlend = std::clamp(fold * 1.35f, 0.0f, 1.0f);
    float y = lerp(x, softStage, softBlend);

    const float hardAmount = std::clamp((fold - 0.42f) / 0.58f, 0.0f, 1.0f);
    if (hardAmount > 0.0f) {
        const float limit = lerp(1.20f, 0.32f, hardAmount);
        const float span = std::max(0.0001f, limit * 4.0f);
        float h = std::fmod(pre + limit, span);
        if (h < 0.0f) h += span;
        h = std::fabs(h - span * 0.5f) - limit;
        h *= lerp(0.85f, 1.45f, hardAmount);
        y = lerp(y, h, hardAmount);
    }

    return clamp(y, -1.6f, 1.6f);
}

inline float waveGeometryQuantize(float x, float bits)
{
    bits = std::clamp(bits, 0.0f, 1.0f);
    if (bits <= 0.0001f) return x;
    const float levels = std::floor(lerp(65536.0f, 12.0f, bits));
    return std::round(x * levels) / std::max(1.0f, levels);
}

inline WaveGeometryParams sanitizeWaveGeometryParams(WaveGeometryParams p)
{
    p.fold = clamp(p.fold, 0.0f, 1.0f);
    p.drive = clamp(p.drive, 0.0f, 1.0f);
    p.hold = clamp(p.hold, 0.0f, 1.0f);
    p.clip = clamp(p.clip, 0.0f, 1.0f);
    p.rectify = clamp(p.rectify, 0.0f, 1.0f);
    p.edge = clamp(p.edge, 0.0f, 1.0f);
    p.zero = clamp(p.zero, 0.0f, 1.0f);
    p.polar = clamp(p.polar, 0.0f, 1.0f);
    p.bits = clamp(p.bits, 0.0f, 1.0f);
    p.step = clamp(p.step, 0.0f, 1.0f);
    p.trans = clamp(p.trans, -1.0f, 1.0f);
    p.tape = clamp(p.tape, 0.0f, 1.0f);
    p.speed = clamp(p.speed, 0.0f, 1.0f);
    p.mix = clamp(p.mix, 0.0f, 1.0f);
    p.gainDb = clamp(p.gainDb, -60.0f, 12.0f);
    p.safety = clamp(p.safety, 0.12f, 0.92f);
    return p;
}

inline WaveGeometryMeshParams sanitizeWaveGeometryMeshParams(WaveGeometryMeshParams p)
{
    p.coupling = clamp(p.coupling, 0.0f, 1.0f);
    p.tension = clamp(p.tension, 0.0f, 1.0f);
    p.decay = clamp(p.decay, 0.0f, 1.0f);
    p.damping = clamp(p.damping, 0.0f, 1.0f);
    return p;
}

inline WaveGeometryParams waveGeometryLaneParams(const WaveGeometrySettings& settings,
                                                 uint32_t lane,
                                                 uint32_t laneCount)
{
    laneCount = std::max<uint32_t>(1u, std::min<uint32_t>(laneCount, kWaveGeometryChannels));
    const auto controls = topologyControlsFromState(settings.topology);
    const auto point = topologyPointForLane(lane, laneCount, controls);
    const double motion = topologyMotionActive(settings.topology) ? settings.topology.motionDepth * 0.75 : 0.0;
    const double rawAmount = std::max({
        settings.topology.amount,
        settings.topology.collapse,
        std::fabs(settings.topology.twist) * 0.65,
        std::fabs(settings.topology.flare) * 0.55,
        settings.topology.jitter * 0.50,
        motion
    });
    const double amount = topologyAmount(rawAmount);
    const double radius = std::clamp(point.radius, 0.0, 2.5);
    const double outward = std::clamp(radius - 0.72, 0.0, 1.4);
    const double neighbor = std::clamp(settings.topology.neighborRadius, 0.0, 1.0);
    const double centroid = std::clamp(settings.topology.centroidAmount, 0.0, 1.0);
    const double seed = std::clamp(settings.topology.jitter, 0.0, 1.0);
    const double noise = laneNoise(lane + 431u) * seed;

    WaveGeometryParams p = settings.base;
    p.fold = static_cast<float>(std::clamp(
        static_cast<double>(p.fold) + amount * (std::max(0.0, point.x) * 0.34 + outward * 0.26 + noise * 0.10),
        0.0, 1.0));
    p.drive = static_cast<float>(std::clamp(
        static_cast<double>(p.drive) + amount * (std::fabs(point.x) * 0.28 + outward * 0.22),
        0.0, 1.0));
    p.hold = static_cast<float>(std::clamp(
        static_cast<double>(p.hold) + amount * (std::max(0.0, point.y) * 0.36 + neighbor * 0.14),
        0.0, 1.0));
    p.clip = static_cast<float>(std::clamp(
        static_cast<double>(p.clip) + amount * (std::max(0.0, -point.x) * 0.35 + centroid * 0.16),
        0.0, 1.0));
    p.rectify = static_cast<float>(std::clamp(
        static_cast<double>(p.rectify) + amount * (std::max(0.0, -point.y) * 0.40 + std::fabs(point.z) * 0.12),
        0.0, 1.0));
    p.edge = static_cast<float>(std::clamp(
        static_cast<double>(p.edge) + amount * (outward * 0.34 + std::fabs(point.lane) * 0.20),
        0.0, 1.0));
    p.zero = static_cast<float>(std::clamp(
        static_cast<double>(p.zero) + amount * (centroid * 0.24 + seed * 0.18 + std::max(0.0, -point.z) * 0.18),
        0.0, 0.78));
    p.polar = static_cast<float>(std::clamp(
        static_cast<double>(p.polar) + amount * (std::max(0.0, point.z) * 0.42 + neighbor * 0.12),
        0.0, 1.0));
    p.bits = static_cast<float>(std::clamp(
        static_cast<double>(p.bits) + amount * (std::max(0.0, -point.y) * 0.26 + outward * 0.20 + seed * 0.10),
        0.0, 0.92));
    p.step = static_cast<float>(std::clamp(
        static_cast<double>(p.step) + amount * (std::max(0.0, point.y) * 0.30 + centroid * 0.18),
        0.0, 1.0));
    p.trans = static_cast<float>(std::clamp(
        static_cast<double>(p.trans) + amount * (point.x * 0.62 + point.z * 0.20),
        -1.0, 1.0));
    p.tape = static_cast<float>(std::clamp(
        static_cast<double>(p.tape) + amount * (outward * 0.20 + neighbor * 0.12 + centroid * 0.08),
        0.0, 1.0));
    p.speed = static_cast<float>(std::clamp(
        static_cast<double>(p.speed) + amount * (std::fabs(point.x) * 0.24 + std::fabs(point.z) * 0.18 + seed * 0.10),
        0.0, 1.0));
    p.gainDb = static_cast<float>(std::clamp(
        static_cast<double>(p.gainDb) - amount * (0.9 + p.drive * 1.8 + p.edge * 0.8),
        -60.0, 12.0));
    return sanitizeWaveGeometryParams(p);
}

class WaveGeometryProcessor {
public:
    bool prepare(double sampleRate,
                 uint32_t channels = kWaveGeometryChannels,
                 uint32_t = 0u,
                 uint32_t = 0u,
                 uint32_t maxBlockFrames = 4096u)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        channels_ = std::max<uint32_t>(1u, std::min<uint32_t>(channels, kWaveGeometryChannels));
        maxBlockFrames_ = std::max<uint32_t>(1u, maxBlockFrames);
        lanes_.assign(channels_, LaneState {});
        for (uint32_t ch = 0; ch < channels_; ++ch) {
            laneParams_[ch] = sanitizeWaveGeometryParams(laneParams_[ch]);
            lanes_[ch].current = laneParams_[ch];
            lanes_[ch].held = 0.0f;
            lanes_[ch].gate = 1.0f;
            lanes_[ch].tapeWrite = 0u;
            const uint32_t tapeFrames = std::max<uint32_t>(2048u, static_cast<uint32_t>(sampleRate_ * 1.5));
            lanes_[ch].tape.assign(tapeFrames, 0.0f);
            lanes_[ch].tapeHeadA = static_cast<double>((lanes_[ch].tape.size() + lanes_[ch].tapeWrite - std::min<size_t>(lanes_[ch].tape.size() - 1u, static_cast<size_t>(sampleRate_ * 0.085))) % lanes_[ch].tape.size());
            lanes_[ch].tapeHeadB = static_cast<double>((lanes_[ch].tape.size() + lanes_[ch].tapeWrite - std::min<size_t>(lanes_[ch].tape.size() - 1u, static_cast<size_t>(sampleRate_ * 0.155))) % lanes_[ch].tape.size());
        }
        meshParams_ = sanitizeWaveGeometryMeshParams(meshParams_);
        meshCurrent_ = meshParams_;
        meshDelayFrames_ = std::max<uint32_t>(
            8u, static_cast<uint32_t>(std::ceil(sampleRate_ * kMeshMaximumTravelSeconds)) + 4u);
        edgeDelayBuffer_.assign(
            static_cast<size_t>(kMeshEdgeCapacity) * meshDelayFrames_, 0.0f);
        const float sr = static_cast<float>(sampleRate_);
        meshParamSmoothing_ = 1.0f - std::exp(-1.0f / (sr * 0.018f));
        meshWeightSmoothing_ = 1.0f - std::exp(-1.0f / (sr * 0.028f));
        meshDelaySmoothing_ = 1.0f - std::exp(-1.0f / (sr * 0.040f));
        meshEnergyAttack_ = 1.0f - std::exp(-1.0f / (sr * 0.006f));
        meshEnergyRelease_ = 1.0f - std::exp(-1.0f / (sr * 0.120f));
        updateMeshGraphTargets();
        resetMeshState();
        ready_ = true;
        return true;
    }

    void reset()
    {
        sampleCounter_ = 0u;
        for (auto& lane : lanes_) {
            lane.lastIn = 0.0f;
            lane.lastOut = 0.0f;
            lane.held = 0.0f;
            lane.gate = 1.0f;
            lane.holdCounter = 0u;
            lane.tapeWrite = 0u;
            lane.tapeHeadA = 0.0;
            lane.tapeHeadB = 0.0;
            std::fill(lane.tape.begin(), lane.tape.end(), 0.0f);
            if (!lane.tape.empty()) {
                lane.tapeHeadA = static_cast<double>((lane.tape.size() + lane.tapeWrite - std::min<size_t>(lane.tape.size() - 1u, static_cast<size_t>(sampleRate_ * 0.085))) % lane.tape.size());
                lane.tapeHeadB = static_cast<double>((lane.tape.size() + lane.tapeWrite - std::min<size_t>(lane.tape.size() - 1u, static_cast<size_t>(sampleRate_ * 0.155))) % lane.tape.size());
            }
        }
        resetMeshState();
    }

    bool ready() const { return ready_; }
    uint32_t channels() const { return channels_; }
    uint32_t latencyFrames() const { return 0u; }

    void setLaneParams(uint32_t lane, const WaveGeometryParams& params)
    {
        if (lane >= kWaveGeometryChannels) return;
        laneParams_[lane] = sanitizeWaveGeometryParams(params);
        if (lane < lanes_.size() && !ready_) lanes_[lane].current = laneParams_[lane];
    }

    void setMeshParams(const WaveGeometryMeshParams& params)
    {
        meshParams_ = sanitizeWaveGeometryMeshParams(params);
        updateMeshDelayTargets();
        if (sampleCounter_ == 0u
            || (meshParams_.coupling <= 0.000001f
                && meshCurrent_.coupling <= 0.000001f
                && meshTailRemaining_ == 0u)) {
            meshCurrent_ = meshParams_;
            edgeDelayCurrent_ = edgeDelayTarget_;
        }
        if (meshParams_.coupling > 0.000001f) {
            meshTailRemaining_ = meshTailFrames();
        }
    }

    void setTopology(const TopologyState& topology)
    {
        topology_ = topology;
        updateMeshGraphTargets();
        if (sampleCounter_ == 0u
            || (meshParams_.coupling <= 0.000001f
                && meshCurrent_.coupling <= 0.000001f
                && meshTailRemaining_ == 0u)) {
            resetMeshState();
        }
    }

    float edgeEnergy(uint32_t source, uint32_t destination) const
    {
        if (source >= channels_ || destination >= channels_ || source == destination) return 0.0f;
        return edgeEnergyPublished_[meshEdgeIndex(source, destination)].load(std::memory_order_relaxed);
    }

    float edgePhase(uint32_t source, uint32_t destination) const
    {
        if (source >= channels_ || destination >= channels_ || source == destination) return 0.0f;
        return edgePhasePublished_[meshEdgeIndex(source, destination)].load(std::memory_order_relaxed);
    }

    uint32_t meshTailFrames() const
    {
        if (meshParams_.coupling <= 0.000001f || meshMaximumDelayTarget_ < 2.0f) return 0u;
        const float reflection = meshReflection(meshParams_) * meshRouteLoss(meshParams_);
        if (reflection <= 0.0001f) return static_cast<uint32_t>(std::ceil(meshMaximumDelayTarget_));
        const double traversals = std::ceil(std::log(0.0001) / std::log(std::min(0.999, static_cast<double>(reflection))));
        const double estimated = static_cast<double>(meshMaximumDelayTarget_)
            * std::max(2.0, traversals + 1.0);
        const double maximum = sampleRate_ * 30.0;
        return static_cast<uint32_t>(std::clamp(
            std::ceil(estimated), 0.0,
            std::min(maximum, static_cast<double>(std::numeric_limits<uint32_t>::max()))));
    }

    // Audio-thread snapshot used by wrappers when a coupling change has
    // stopped refreshing the nominal tail but delayed mesh energy is still
    // draining. Callers publish this value atomically for other threads.
    uint32_t meshTailRemainingFrames() const { return meshTailRemaining_; }

    void process(const float* const* input,
                 uint32_t inputChannels,
                 float* const* output,
                 uint32_t outputChannels,
                 uint32_t frames)
    {
        if (!ready_ || !output || frames == 0u) return;
        frames = std::min(frames, maxBlockFrames_);
        const uint32_t active = std::min(channels_, outputChannels);
        const bool meshActive = meshParams_.coupling > 0.000001f
            || meshCurrent_.coupling > 0.000001f || meshTailRemaining_ > 0u;
        if (!meshActive) {
            // This is the legacy lane-major path. The only intentional change is
            // that POL and ZERO now see the absolute frame clock rather than a
            // block-constant clock, making their result independent of host block size.
            for (uint32_t ch = 0; ch < active; ++ch) {
                if (!output[ch]) continue;
                for (uint32_t i = 0; i < frames; ++i) {
                    const float x = (input && ch < inputChannels && input[ch]) ? input[ch][i] : 0.0f;
                    output[ch][i] = processSample(ch, x, sampleCounter_ + i);
                }
            }
        } else {
            processMesh(input, inputChannels, output, outputChannels, frames);
        }
        for (uint32_t ch = active; ch < outputChannels; ++ch) {
            if (output[ch]) std::fill(output[ch], output[ch] + frames, 0.0f);
        }
        for (uint32_t edge = 0; edge < kMeshEdgeCapacity; ++edge) {
            edgeEnergyPublished_[edge].store(edgeEnergyState_[edge], std::memory_order_relaxed);
            edgePhasePublished_[edge].store(edgePhaseState_[edge], std::memory_order_relaxed);
        }
        sampleCounter_ += frames;
    }

private:
    static constexpr uint32_t kMeshEdgeCapacity = kWaveGeometryChannels * kWaveGeometryChannels;
    static constexpr float kMeshMaximumTravelSeconds = 0.040f;
    static constexpr float kMeshMinimumDelayFrames = 2.0f;

    static constexpr uint32_t meshEdgeIndex(uint32_t source, uint32_t destination)
    {
        return source * kWaveGeometryChannels + destination;
    }

    static float meshReflection(const WaveGeometryMeshParams& params)
    {
        const auto p = sanitizeWaveGeometryMeshParams(params);
        // DECY = 0 is a single traversal: excitation still enters an edge, but
        // an arrival is not scattered onward. The upper bound stays passive.
        return 0.96f * std::pow(p.decay, 1.35f);
    }

    static float meshRouteLoss(const WaveGeometryMeshParams& params)
    {
        const auto p = sanitizeWaveGeometryMeshParams(params);
        return lerp(0.998f, 0.70f, p.damping * p.damping);
    }

    static float meshOutputLimit(float value)
    {
        constexpr float knee = 0.92f;
        constexpr float headroom = 1.0f - knee;
        const float magnitude = std::fabs(value);
        if (magnitude <= knee) return value;
        return std::copysign(
            knee + headroom * std::tanh((magnitude - knee) / headroom),
            value);
    }

    void resetMeshState()
    {
        std::fill(edgeDelayBuffer_.begin(), edgeDelayBuffer_.end(), 0.0f);
        edgeIncoming_.fill(0.0f);
        edgeOutgoing_.fill(0.0f);
        edgeLowpassState_.fill(0.0f);
        edgeDcState_.fill(0.0f);
        edgeEnergyState_.fill(0.0f);
        edgePhaseState_.fill(0.0f);
        nodeNetwork_.fill(0.0f);
        meshWrite_ = 0u;
        meshTailRemaining_ = 0u;
        meshCurrent_ = meshParams_;
        for (uint32_t edge = 0; edge < kMeshEdgeCapacity; ++edge) {
            edgeWeightCurrent_[edge] = edgeWeightTarget_[edge];
            edgeDelayCurrent_[edge] = edgeDelayTarget_[edge];
            edgeEnergyPublished_[edge].store(0.0f, std::memory_order_relaxed);
            edgePhasePublished_[edge].store(0.0f, std::memory_order_relaxed);
        }
    }

    void updateMeshGraphTargets()
    {
        edgeWeightTarget_.fill(0.0f);
        edgeDistance_.fill(0.0f);
        if (channels_ < 2u) {
            meshMaximumDelayTarget_ = 0.0f;
            return;
        }
        std::array<TopologyPoint, kWaveGeometryChannels> points {};
        for (uint32_t node = 0; node < channels_; ++node) {
            points[node] = topologyPointForLane(node, channels_, topology_);
        }
        const uint32_t requested = std::clamp<uint32_t>(topology_.neighborCount, 1u, 3u);
        const float reach = static_cast<float>(0.20 + std::clamp(topology_.neighborRadius, 0.0, 1.0) * 2.80);
        for (uint32_t source = 0; source < channels_; ++source) {
            const auto neighbors = nearestTopologyNeighbors(topology_, source, channels_);
            std::array<bool, kWaveGeometryChannels> seen {};
            for (uint32_t slot = 0; slot < requested; ++slot) {
                const int candidate = neighbors[slot];
                if (candidate < 0) continue;
                const uint32_t destination = static_cast<uint32_t>(candidate);
                if (destination >= channels_ || destination == source || seen[destination]) continue;
                seen[destination] = true;
                const double dx = points[source].x - points[destination].x;
                const double dy = points[source].y - points[destination].y;
                const double dz = points[source].z - points[destination].z;
                const float distance = static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));
                const float weight = std::exp(-distance / std::max(0.08f, reach));
                const uint32_t forward = meshEdgeIndex(source, destination);
                const uint32_t reverse = meshEdgeIndex(destination, source);
                edgeWeightTarget_[forward] = std::max(edgeWeightTarget_[forward], weight);
                edgeWeightTarget_[reverse] = std::max(edgeWeightTarget_[reverse], weight);
                edgeDistance_[forward] = distance;
                edgeDistance_[reverse] = distance;
            }
        }

        // CENT adds a virtual hub without creating an instantaneous global
        // shortcut. Non-neighbor pairs receive a weaker composite route whose
        // delay is the trip from the source to the origin and back out to the
        // destination. Direct neighbors retain their shorter path.
        const float centroid = static_cast<float>(std::clamp(
            topology_.centroidAmount, 0.0, 1.0));
        if (centroid > 0.000001f) {
            const float hubAmount = 0.65f * std::pow(centroid, 1.35f);
            for (uint32_t source = 0; source < channels_; ++source) {
                for (uint32_t destination = source + 1u;
                     destination < channels_; ++destination) {
                    const float viaDistance = std::max(
                        0.05f,
                        static_cast<float>(points[source].radius
                            + points[destination].radius));
                    const float hubWeight = hubAmount * std::exp(
                        -viaDistance / std::max(0.08f, reach));
                    const uint32_t forward = meshEdgeIndex(source, destination);
                    const uint32_t reverse = meshEdgeIndex(destination, source);
                    if (edgeWeightTarget_[forward] <= 0.000001f) {
                        edgeWeightTarget_[forward] = hubWeight;
                        edgeWeightTarget_[reverse] = hubWeight;
                        edgeDistance_[forward] = viaDistance;
                        edgeDistance_[reverse] = viaDistance;
                    } else {
                        const float reinforced = std::min(
                            1.0f,
                            edgeWeightTarget_[forward] + hubWeight * 0.28f);
                        edgeWeightTarget_[forward] = reinforced;
                        edgeWeightTarget_[reverse] = reinforced;
                    }
                }
            }
        }
        updateMeshDelayTargets();
    }

    void updateMeshDelayTargets()
    {
        const float tension = sanitizeWaveGeometryMeshParams(meshParams_).tension;
        const float secondsPerUnit = 0.018f * std::pow(0.00035f / 0.018f, tension);
        const float maximum = std::max(kMeshMinimumDelayFrames,
            static_cast<float>(meshDelayFrames_ > 3u ? meshDelayFrames_ - 3u : 2u));
        meshMaximumDelayTarget_ = 0.0f;
        for (uint32_t edge = 0; edge < kMeshEdgeCapacity; ++edge) {
            if (edgeWeightTarget_[edge] <= 0.000001f) {
                edgeDelayTarget_[edge] = kMeshMinimumDelayFrames;
                continue;
            }
            edgeDelayTarget_[edge] = clamp(
                edgeDistance_[edge] * secondsPerUnit * static_cast<float>(sampleRate_),
                kMeshMinimumDelayFrames, maximum);
            meshMaximumDelayTarget_ = std::max(meshMaximumDelayTarget_, edgeDelayTarget_[edge]);
        }
    }

    float readMeshEdge(uint32_t edge, float delayFrames) const
    {
        if (meshDelayFrames_ < 2u || edgeDelayBuffer_.empty()) return 0.0f;
        const float size = static_cast<float>(meshDelayFrames_);
        float position = static_cast<float>(meshWrite_) - delayFrames;
        while (position < 0.0f) position += size;
        while (position >= size) position -= size;
        const uint32_t i0 = static_cast<uint32_t>(position);
        const uint32_t i1 = (i0 + 1u) % meshDelayFrames_;
        const float fraction = position - static_cast<float>(i0);
        const size_t base = static_cast<size_t>(edge) * meshDelayFrames_;
        return lerp(edgeDelayBuffer_[base + i0], edgeDelayBuffer_[base + i1], fraction);
    }

    void processMesh(const float* const* input,
                     uint32_t inputChannels,
                     float* const* output,
                     uint32_t outputChannels,
                     uint32_t frames)
    {
        const uint32_t tailFrames = meshTailFrames();
        const float dcCoefficient = 1.0f - std::exp(
            -2.0f * static_cast<float>(M_PI) * 18.0f / static_cast<float>(sampleRate_));
        std::array<float, kWaveGeometryChannels> local {};
        std::array<float, kWaveGeometryChannels> dry {};
        std::array<float, kWaveGeometryChannels> injection {};
        std::array<float, kWaveGeometryChannels> incomingWeight {};

        for (uint32_t frame = 0; frame < frames; ++frame) {
            meshCurrent_.coupling += (meshParams_.coupling - meshCurrent_.coupling) * meshParamSmoothing_;
            meshCurrent_.tension += (meshParams_.tension - meshCurrent_.tension) * meshParamSmoothing_;
            meshCurrent_.decay += (meshParams_.decay - meshCurrent_.decay) * meshParamSmoothing_;
            meshCurrent_.damping += (meshParams_.damping - meshCurrent_.damping) * meshParamSmoothing_;
            meshCurrent_ = sanitizeWaveGeometryMeshParams(meshCurrent_);

            const float cutoff = std::min(
                static_cast<float>(sampleRate_) * 0.45f,
                18000.0f * std::pow(650.0f / 18000.0f, meshCurrent_.damping));
            const float lowpassCoefficient = 1.0f - std::exp(
                -2.0f * static_cast<float>(M_PI) * cutoff / static_cast<float>(sampleRate_));
            const float routeLoss = meshRouteLoss(meshCurrent_);
            const float reflection = meshReflection(meshCurrent_);
            const float coupling = meshCurrent_.coupling;
            const float couplingRoot = std::sqrt(coupling);

            for (uint32_t edge = 0; edge < kMeshEdgeCapacity; ++edge) {
                edgeWeightCurrent_[edge] += (edgeWeightTarget_[edge] - edgeWeightCurrent_[edge])
                    * meshWeightSmoothing_;
                edgeDelayCurrent_[edge] += (edgeDelayTarget_[edge] - edgeDelayCurrent_[edge])
                    * meshDelaySmoothing_;
                const float raw = readMeshEdge(edge, edgeDelayCurrent_[edge]);
                edgeLowpassState_[edge] += (raw - edgeLowpassState_[edge]) * lowpassCoefficient;
                edgeDcState_[edge] += (edgeLowpassState_[edge] - edgeDcState_[edge]) * dcCoefficient;
                edgeIncoming_[edge] = flushDenormal(
                    (edgeLowpassState_[edge] - edgeDcState_[edge]) * routeLoss);
                edgePhaseState_[edge] += 1.0f / std::max(kMeshMinimumDelayFrames, edgeDelayCurrent_[edge]);
                edgePhaseState_[edge] -= std::floor(edgePhaseState_[edge]);
            }

            nodeNetwork_.fill(0.0f);
            incomingWeight.fill(0.0f);
            for (uint32_t source = 0; source < channels_; ++source) {
                for (uint32_t destination = 0; destination < channels_; ++destination) {
                    if (source == destination) continue;
                    const uint32_t edge = meshEdgeIndex(source, destination);
                    const float weight = edgeWeightCurrent_[edge];
                    if (weight <= 0.000001f) continue;
                    nodeNetwork_[destination] += edgeIncoming_[edge] * weight;
                    incomingWeight[destination] += weight;
                }
            }
            for (uint32_t node = 0; node < channels_; ++node) {
                nodeNetwork_[node] = incomingWeight[node] > 0.000001f
                    ? 2.0f * nodeNetwork_[node] / incomingWeight[node]
                    : 0.0f;
                const float x = (input && node < inputChannels && input[node])
                    ? input[node][frame] : 0.0f;
                dry[node] = x;
                local[node] = processLocalWet(
                    node, x, sampleCounter_ + frame);
                const float injectionGain = lerp(0.32f, 0.13f, meshCurrent_.decay);
                injection[node] = local[node] * couplingRoot * injectionGain;
            }

            for (uint32_t source = 0; source < channels_; ++source) {
                for (uint32_t destination = 0; destination < channels_; ++destination) {
                    const uint32_t edge = meshEdgeIndex(source, destination);
                    float outgoing = 0.0f;
                    const float weight = source == destination ? 0.0f : edgeWeightCurrent_[edge];
                    if (weight > 0.000001f) {
                        const uint32_t reverse = meshEdgeIndex(destination, source);
                        const float scattered = nodeNetwork_[source] - edgeIncoming_[reverse];
                        outgoing = weight * std::tanh(injection[source] + reflection * scattered);
                    }
                    edgeOutgoing_[edge] = flushDenormal(outgoing);
                    const size_t index = static_cast<size_t>(edge) * meshDelayFrames_ + meshWrite_;
                    edgeDelayBuffer_[index] = edgeOutgoing_[edge];
                    const float activity = std::max(
                        std::fabs(edgeIncoming_[edge] * weight), std::fabs(edgeOutgoing_[edge]));
                    const float coefficient = activity > edgeEnergyState_[edge]
                        ? meshEnergyAttack_ : meshEnergyRelease_;
                    edgeEnergyState_[edge] += (activity - edgeEnergyState_[edge]) * coefficient;
                    edgeEnergyState_[edge] = flushDenormal(edgeEnergyState_[edge]);
                }
            }

            for (uint32_t node = 0; node < channels_; ++node) {
                if (node >= outputChannels || !output[node]) continue;
                const float contribution = couplingRoot * 0.72f
                    * std::tanh(nodeNetwork_[node] * 0.75f);
                const float combinedWet = local[node] + contribution;
                // Enter the safety knee continuously with COUP. An arbitrarily
                // small nonzero mesh setting must converge to the exact legacy
                // local path rather than engaging the full limiter at once.
                const float meshedWet = lerp(
                    combinedWet,
                    meshOutputLimit(combinedWet),
                    coupling);
                output[node][frame] = finishSample(
                    node, dry[node], meshedWet);
            }
            meshWrite_ = (meshWrite_ + 1u) % meshDelayFrames_;
            if (meshParams_.coupling > 0.000001f) meshTailRemaining_ = tailFrames;
            else if (meshTailRemaining_ > 0u) --meshTailRemaining_;
        }
    }

    struct LaneState {
        WaveGeometryParams current {};
        float lastIn = 0.0f;
        float lastOut = 0.0f;
        float held = 0.0f;
        float gate = 1.0f;
        uint32_t holdCounter = 0u;
        std::vector<float> tape;
        uint32_t tapeWrite = 0u;
        double tapeHeadA = 0.0;
        double tapeHeadB = 0.0;
    };

    static float hash01(uint32_t x)
    {
        x ^= x >> 16u;
        x *= 0x7feb352du;
        x ^= x >> 15u;
        x *= 0x846ca68bu;
        x ^= x >> 16u;
        return static_cast<float>(x & 0xffffu) / 65535.0f;
    }

    static float smoothParam(float current, float target)
    {
        return current + (target - current) * 0.0018f;
    }


    float readTape(const LaneState& lane, double pos) const
    {
        if (lane.tape.empty()) return 0.0f;
        const double size = static_cast<double>(lane.tape.size());
        pos = std::fmod(pos, size);
        if (pos < 0.0) pos += size;
        const uint32_t i0 = static_cast<uint32_t>(pos);
        const uint32_t i1 = (i0 + 1u) % static_cast<uint32_t>(lane.tape.size());
        const float frac = static_cast<float>(pos - static_cast<double>(i0));
        return lerp(lane.tape[i0], lane.tape[i1], frac);
    }

    float processTapeLayer(uint32_t ch, float input, const WaveGeometryParams& p)
    {
        auto& lane = lanes_[ch];
        if (lane.tape.empty()) return input;

        lane.tape[lane.tapeWrite] = input;
        const float headA = readTape(lane, lane.tapeHeadA);
        const float headB = readTape(lane, lane.tapeHeadB);
        const float tapeWet = std::tanh((headA + headB) * 0.58f);

        const double laneBias = (static_cast<double>(ch) / static_cast<double>(std::max<uint32_t>(1u, channels_ - 1u))) - 0.5;
        const double spread = static_cast<double>(p.speed);
        const double rateA = std::clamp(1.0 - spread * (0.22 + std::fabs(laneBias) * 0.10), 0.42, 1.12);
        const double rateB = std::clamp(1.0 + spread * (0.28 + std::fabs(laneBias) * 0.16), 0.88, 1.58);
        const double size = static_cast<double>(lane.tape.size());
        lane.tapeHeadA += rateA;
        lane.tapeHeadB += rateB;
        if (lane.tapeHeadA >= size) lane.tapeHeadA -= size * std::floor(lane.tapeHeadA / size);
        if (lane.tapeHeadB >= size) lane.tapeHeadB -= size * std::floor(lane.tapeHeadB / size);
        lane.tapeWrite = (lane.tapeWrite + 1u) % static_cast<uint32_t>(lane.tape.size());

        return lerp(input, tapeWet, p.tape);
    }

    float processLocalWet(uint32_t ch, float input, uint64_t absoluteFrame)
    {
        auto& lane = lanes_[ch];
        const auto& target = laneParams_[ch];
        lane.current.fold = smoothParam(lane.current.fold, target.fold);
        lane.current.drive = smoothParam(lane.current.drive, target.drive);
        lane.current.hold = smoothParam(lane.current.hold, target.hold);
        lane.current.clip = smoothParam(lane.current.clip, target.clip);
        lane.current.rectify = smoothParam(lane.current.rectify, target.rectify);
        lane.current.edge = smoothParam(lane.current.edge, target.edge);
        lane.current.zero = smoothParam(lane.current.zero, target.zero);
        lane.current.polar = smoothParam(lane.current.polar, target.polar);
        lane.current.bits = smoothParam(lane.current.bits, target.bits);
        lane.current.step = smoothParam(lane.current.step, target.step);
        lane.current.trans = smoothParam(lane.current.trans, target.trans);
        lane.current.tape = smoothParam(lane.current.tape, target.tape);
        lane.current.speed = smoothParam(lane.current.speed, target.speed);
        lane.current.mix = smoothParam(lane.current.mix, target.mix);
        lane.current.gainDb = smoothParam(lane.current.gainDb, target.gainDb);
        lane.current.safety = smoothParam(lane.current.safety, target.safety);

        const auto p = sanitizeWaveGeometryParams(lane.current);
        float x = flushDenormal(input);
        const float edge = x - lane.lastIn;
        lane.lastIn = x;

        const uint32_t holdPeriod = 1u + static_cast<uint32_t>(std::floor(p.hold * p.hold * 95.0f + p.step * 31.0f));
        if (holdPeriod <= 1u || lane.holdCounter == 0u) {
            lane.held = x;
            lane.holdCounter = holdPeriod;
        } else {
            --lane.holdCounter;
        }
        x = lerp(x, lane.held, std::max(p.hold, p.step * 0.65f));

        x += edge * p.edge * lerp(0.5f, 5.5f, p.edge);
        const float rect = p.trans >= 0.0f ? std::fabs(x) : -std::fabs(x);
        const float half = p.trans >= 0.0f ? std::max(0.0f, x) : std::min(0.0f, x);
        x = lerp(x, lerp(rect, half, std::fabs(p.trans)), p.rectify);

        const uint32_t polarPeriod = 6u + static_cast<uint32_t>(std::floor((1.0f - p.polar) * 34.0f + p.step * 16.0f));
        const float sign = (((absoluteFrame / std::max<uint32_t>(1u, polarPeriod)) + ch) & 1u) ? -1.0f : 1.0f;
        x = lerp(x, std::fabs(x) * sign, p.polar);

        const float driveGain = lerp(1.0f, 18.0f, p.drive);
        x *= driveGain;
        x = waveGeometryFold(x, p.fold);
        const float threshold = lerp(1.05f, 0.12f, p.clip);
        x = clamp(x, -threshold, threshold) / threshold;
        x = lerp(x, std::atan(x * lerp(1.0f, 7.5f, p.drive)) / std::atan(lerp(1.0f, 7.5f, p.drive)), 0.62f);
        x = waveGeometryQuantize(x, p.bits);

        const uint32_t zeroPeriod = 8u + static_cast<uint32_t>(std::floor((1.0f - p.zero) * 160.0f));
        const float zeroChance = p.zero * p.zero * 0.58f;
        const bool zeroCell = hash01(static_cast<uint32_t>(absoluteFrame / std::max<uint32_t>(1u, zeroPeriod)) + ch * 8191u) < zeroChance;
        const float gateTarget = zeroCell ? 0.0f : 1.0f;
        lane.gate += (gateTarget - lane.gate) * lerp(0.010f, 0.060f, p.zero);
        x *= lane.gate;
        x = processTapeLayer(ch, x, p);

        return std::tanh(clamp(x, -8.0f, 8.0f));
    }

    float finishSample(uint32_t ch, float input, float wet)
    {
        auto& lane = lanes_[ch];
        const auto p = sanitizeWaveGeometryParams(lane.current);
        const float gain = dbToGain(p.gainDb);
        const float mixed = input + (wet - input) * p.mix;
        const float limited = std::tanh(clamp(mixed * gain, -8.0f, 8.0f) * lerp(1.0f, 1.8f, 1.0f - p.safety));
        lane.lastOut = flushDenormal(limited);
        return lane.lastOut;
    }

    float processSample(uint32_t ch, float input, uint64_t absoluteFrame)
    {
        return finishSample(
            ch,
            input,
            processLocalWet(ch, input, absoluteFrame));
    }

    double sampleRate_ = 48000.0;
    uint32_t channels_ = 0;
    uint32_t maxBlockFrames_ = 0;
    bool ready_ = false;
    uint64_t sampleCounter_ = 0u;
    std::array<WaveGeometryParams, kWaveGeometryChannels> laneParams_ {};
    std::vector<LaneState> lanes_;

    WaveGeometryMeshParams meshParams_ {};
    WaveGeometryMeshParams meshCurrent_ {};
    TopologyState topology_ {};
    uint32_t meshDelayFrames_ = 0u;
    uint32_t meshWrite_ = 0u;
    uint32_t meshTailRemaining_ = 0u;
    float meshMaximumDelayTarget_ = 0.0f;
    float meshParamSmoothing_ = 1.0f;
    float meshWeightSmoothing_ = 1.0f;
    float meshDelaySmoothing_ = 1.0f;
    float meshEnergyAttack_ = 1.0f;
    float meshEnergyRelease_ = 1.0f;
    std::array<float, kMeshEdgeCapacity> edgeWeightTarget_ {};
    std::array<float, kMeshEdgeCapacity> edgeWeightCurrent_ {};
    std::array<float, kMeshEdgeCapacity> edgeDistance_ {};
    std::array<float, kMeshEdgeCapacity> edgeDelayTarget_ {};
    std::array<float, kMeshEdgeCapacity> edgeDelayCurrent_ {};
    std::array<float, kMeshEdgeCapacity> edgeIncoming_ {};
    std::array<float, kMeshEdgeCapacity> edgeOutgoing_ {};
    std::array<float, kMeshEdgeCapacity> edgeLowpassState_ {};
    std::array<float, kMeshEdgeCapacity> edgeDcState_ {};
    std::array<float, kMeshEdgeCapacity> edgeEnergyState_ {};
    std::array<float, kMeshEdgeCapacity> edgePhaseState_ {};
    std::array<std::atomic<float>, kMeshEdgeCapacity> edgeEnergyPublished_ {};
    std::array<std::atomic<float>, kMeshEdgeCapacity> edgePhasePublished_ {};
    std::array<float, kWaveGeometryChannels> nodeNetwork_ {};
    std::vector<float> edgeDelayBuffer_;
};

} // namespace s3g
