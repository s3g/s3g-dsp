#pragma once

#include "s3g_spectral_mesh.h"
#include "s3g_topology.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace s3g {

#ifndef S3G_SPECTRAL_TOPOLOGY_CHANNEL_COUNT
#define S3G_SPECTRAL_TOPOLOGY_CHANNEL_COUNT 8
#endif

constexpr uint32_t kSpectralTopologyChannels = S3G_SPECTRAL_TOPOLOGY_CHANNEL_COUNT;

struct SpectralTopologySettings {
    SpectralSprayParams base {};
    TopologyState topology {};
};

inline SpectralSprayParams spectralTopologyLaneParams(const SpectralTopologySettings& settings,
                                                      uint32_t lane,
                                                      uint32_t laneCount)
{
    laneCount = std::max<uint32_t>(1u, std::min<uint32_t>(laneCount, kSpectralTopologyChannels));
    const auto controls = topologyControlsFromState(settings.topology);
    const auto point = topologyPointForLane(lane, laneCount, controls);
    // AMOUNT is the master depth. Other geometry controls can reshape the
    // field, but do not silently turn local parameter variation back on.
    const double amount = topologyAmount(settings.topology.amount);
    const double radius = std::clamp(point.radius, 0.0, 2.5);
    const double outward = std::clamp(radius - 0.72, 0.0, 1.4);
    const double seed = std::clamp(settings.topology.jitter, 0.0, 1.0);
    const double noise = laneNoise(lane + 193u) * seed;

    SpectralSprayParams p = settings.base;
    p.sprayBins = static_cast<float>(std::clamp(
        static_cast<double>(p.sprayBins) +
            amount * (point.x * 42.0 + point.z * 22.0 + outward * 58.0 + noise * 32.0),
        0.0,
        192.0));
    p.drift = static_cast<float>(std::clamp(
        static_cast<double>(p.drift) +
            amount * (std::fabs(point.x - point.z) * 0.18 + seed * 0.28),
        0.0,
        1.0));
    p.hold = static_cast<float>(std::clamp(
        static_cast<double>(p.hold) +
            amount * (point.y * 0.18 - outward * 0.08),
        0.0,
        1.0));
    // Capture is an explicit performance state; geometry does not engage it
    // behind the user's back.
    p.freeze = static_cast<float>(std::clamp(
        static_cast<double>(p.freeze), 0.0, 1.0));
    p.feedback = static_cast<float>(std::clamp(
        static_cast<double>(p.feedback) +
            amount * (point.z * 0.18 + outward * 0.12),
        0.0,
        0.72));
    p.smear = static_cast<float>(std::clamp(
        static_cast<double>(p.smear) +
            amount * (outward * 0.34 + std::max(0.0, point.y) * 0.25),
        0.0,
        1.0));
    p.holes = static_cast<float>(std::clamp(
        static_cast<double>(p.holes) +
            amount * (std::max(0.0, -point.y) * 0.24 + std::fabs(point.x) * 0.07),
        0.0,
        0.60));
    p.phaseBlur = static_cast<float>(std::clamp(
        static_cast<double>(p.phaseBlur) +
            amount * (std::fabs(point.z) * 0.42 + std::fabs(point.lane) * 0.22),
        0.0,
        1.0));
    p.damage = static_cast<float>(std::clamp(
        static_cast<double>(p.damage) +
            amount * (outward * 0.34 + seed * 0.22 + std::fabs(point.x - point.y) * 0.16),
        0.0,
        0.78));
    p.repeat = static_cast<float>(std::clamp(
        static_cast<double>(p.repeat) +
            amount * (outward * 0.22 + std::max(0.0, point.z) * 0.22),
        0.0,
        0.82));
    p.tilt = static_cast<float>(std::clamp(
        static_cast<double>(p.tilt) + amount * point.x * 0.82,
        -1.0,
        1.0));
    p.safety = static_cast<float>(std::clamp(static_cast<double>(p.safety), 0.12, 0.92));
    return p;
}

class SpectralTopologyProcessor {
public:
    bool prepare(double sampleRate,
                 uint32_t channels = kSpectralTopologyChannels,
                 uint32_t fftSize = 2048u,
                 uint32_t overlap = 4u,
                 uint32_t maxBlockFrames = 4096u)
    {
        channels_ = std::max<uint32_t>(
            1u, std::min<uint32_t>(channels, kSpectralTopologyChannels));
        if (!mesh_.prepare(sampleRate, channels_, fftSize, overlap, maxBlockFrames)) {
            ready_ = false;
            return false;
        }
        for (uint32_t ch = 0; ch < channels_; ++ch) {
            mesh_.setLaneParams(ch, laneParams_[ch]);
        }
        mesh_.setTopologyState(topology_);
        mesh_.setTransientProtect(transientProtect_);
        mesh_.setPropagation(
            propagationVelocity_, propagationDispersion_, propagationDamping_);
        ready_ = true;
        return true;
    }

    void reset()
    {
        mesh_.reset();
    }

    bool ready() const { return ready_; }
    uint32_t channels() const { return channels_; }
    uint32_t latencyFrames() const { return mesh_.latencyFrames(); }

    void setLaneParams(uint32_t lane, const SpectralSprayParams& params)
    {
        if (lane >= kSpectralTopologyChannels) return;
        laneParams_[lane] = params;
        if (lane < channels_) mesh_.setLaneParams(lane, laneParams_[lane]);
    }

    void setTopologyState(const TopologyState& topology)
    {
        topology_ = topology;
        mesh_.setTopologyState(topology_);
    }

    void setTransientProtect(float amount)
    {
        transientProtect_ = clamp(amount, 0.0f, 1.0f);
        mesh_.setTransientProtect(transientProtect_);
    }

    float transientProtect() const { return transientProtect_; }
    void setPropagation(float velocity, float dispersion, float damping)
    {
        propagationVelocity_ = clamp(velocity, 0.0f, 1.0f);
        propagationDispersion_ = clamp(dispersion, -1.0f, 1.0f);
        propagationDamping_ = clamp(damping, 0.0f, 1.0f);
        mesh_.setPropagation(
            propagationVelocity_, propagationDispersion_, propagationDamping_);
    }
    float propagationVelocity() const { return propagationVelocity_; }
    float propagationDispersion() const { return propagationDispersion_; }
    float propagationDamping() const { return propagationDamping_; }
    void requestCapture() { mesh_.requestCapture(); }
    void requestClearCapture() { mesh_.requestClearCapture(); }
    bool hasCapture() const { return mesh_.hasCapture(); }
    float edgeActivity(uint32_t source, uint32_t destination) const
    {
        return mesh_.edgeActivity(source, destination);
    }
    float edgePulsePosition(uint32_t source, uint32_t destination) const
    {
        return mesh_.edgePulsePosition(source, destination);
    }
    void setTelemetryEnabled(bool enabled)
    {
        mesh_.setTelemetryEnabled(enabled);
    }

    void process(const float* const* input,
                 uint32_t inputChannels,
                 float* const* output,
                 uint32_t outputChannels,
                 uint32_t frames)
    {
        mesh_.process(input, inputChannels, output, outputChannels, frames);
    }

private:
    uint32_t channels_ = 0;
    bool ready_ = false;
    float transientProtect_ = 0.35f;
    float propagationVelocity_ = 1.0f;
    float propagationDispersion_ = 0.0f;
    float propagationDamping_ = 0.0f;
    TopologyState topology_ {};
    SpectralMeshProcessor mesh_;
    std::array<SpectralSprayParams, kSpectralTopologyChannels> laneParams_ {};
};

} // namespace s3g
