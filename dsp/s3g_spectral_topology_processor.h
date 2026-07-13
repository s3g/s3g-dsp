#pragma once

#include "s3g_spectral_spray.h"
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
    const double noise = laneNoise(lane + 193u) * seed;

    SpectralSprayParams p = settings.base;
    p.sprayBins = static_cast<float>(std::clamp(
        static_cast<double>(p.sprayBins) +
            amount * (point.x * 42.0 + point.z * 22.0 + outward * 58.0 + noise * 32.0),
        0.0,
        192.0));
    p.drift = static_cast<float>(std::clamp(
        static_cast<double>(p.drift) +
            amount * (std::fabs(point.x - point.z) * 0.18 + seed * 0.28 + neighbor * 0.08),
        0.0,
        1.0));
    p.hold = static_cast<float>(std::clamp(
        static_cast<double>(p.hold) +
            amount * (point.y * 0.18 + centroid * 0.18 - outward * 0.08),
        0.0,
        1.0));
    p.freeze = static_cast<float>(std::clamp(
        static_cast<double>(p.freeze) +
            amount * (std::max(0.0, point.y) * 0.46 + centroid * 0.28),
        0.0,
        0.92));
    p.feedback = static_cast<float>(std::clamp(
        static_cast<double>(p.feedback) +
            amount * (point.z * 0.18 + outward * 0.12 + centroid * 0.18),
        0.0,
        0.72));
    p.smear = static_cast<float>(std::clamp(
        static_cast<double>(p.smear) +
            amount * (outward * 0.34 + std::max(0.0, point.y) * 0.25 + neighbor * 0.18),
        0.0,
        1.0));
    p.holes = static_cast<float>(std::clamp(
        static_cast<double>(p.holes) +
            amount * (std::max(0.0, -point.y) * 0.24 + std::fabs(point.x) * 0.07),
        0.0,
        0.60));
    p.phaseBlur = static_cast<float>(std::clamp(
        static_cast<double>(p.phaseBlur) +
            amount * (std::fabs(point.z) * 0.42 + std::fabs(point.lane) * 0.22 + centroid * 0.10),
        0.0,
        1.0));
    p.damage = static_cast<float>(std::clamp(
        static_cast<double>(p.damage) +
            amount * (outward * 0.34 + seed * 0.22 + std::fabs(point.x - point.y) * 0.16),
        0.0,
        0.78));
    p.repeat = static_cast<float>(std::clamp(
        static_cast<double>(p.repeat) +
            amount * (centroid * 0.32 + neighbor * 0.18 + std::max(0.0, point.z) * 0.22),
        0.0,
        0.82));
    p.tilt = static_cast<float>(std::clamp(
        static_cast<double>(p.tilt) + amount * point.x * 0.82,
        -1.0,
        1.0));
    p.gainDb = static_cast<float>(std::clamp(
        static_cast<double>(p.gainDb) - amount * (1.4 + p.feedback * 1.6 + p.freeze * 0.8),
        -60.0,
        12.0));
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
        channels_ = std::max<uint32_t>(1u, std::min<uint32_t>(channels, kSpectralTopologyChannels));
        maxBlockFrames_ = std::max<uint32_t>(1u, maxBlockFrames);
        monoIn_.assign(channels_, std::vector<float>(maxBlockFrames_, 0.0f));
        monoOut_.assign(channels_, std::vector<float>(maxBlockFrames_, 0.0f));
        for (uint32_t ch = 0; ch < channels_; ++ch) {
            if (!sprays_[ch].prepare(sampleRate, 1u, fftSize, overlap, maxBlockFrames_)) {
                ready_ = false;
                return false;
            }
            sprays_[ch].setParams(laneParams_[ch]);
        }
        ready_ = true;
        return true;
    }

    void reset()
    {
        for (uint32_t ch = 0; ch < channels_; ++ch) {
            sprays_[ch].reset();
        }
    }

    bool ready() const { return ready_; }
    uint32_t channels() const { return channels_; }
    uint32_t latencyFrames() const { return channels_ > 0u ? sprays_[0].latencyFrames() : 0u; }

    void setLaneParams(uint32_t lane, const SpectralSprayParams& params)
    {
        if (lane >= kSpectralTopologyChannels) return;
        laneParams_[lane] = params;
        if (lane < channels_) {
            sprays_[lane].setParams(laneParams_[lane]);
        }
    }

    void process(const float* const* input,
                 uint32_t inputChannels,
                 float* const* output,
                 uint32_t outputChannels,
                 uint32_t frames)
    {
        if (!ready_ || !output || frames == 0u) return;
        frames = std::min(frames, maxBlockFrames_);
        const uint32_t active = std::min(channels_, outputChannels);
        for (uint32_t ch = 0; ch < active; ++ch) {
            for (uint32_t i = 0; i < frames; ++i) {
                monoIn_[ch][i] = (input && ch < inputChannels && input[ch]) ? input[ch][i] : 0.0f;
                monoOut_[ch][i] = 0.0f;
            }
            const float* monoInput[1] = { monoIn_[ch].data() };
            float* monoOutput[1] = { monoOut_[ch].data() };
            sprays_[ch].process(monoInput, monoOutput, frames);
            if (output[ch]) {
                std::copy(monoOut_[ch].begin(), monoOut_[ch].begin() + frames, output[ch]);
            }
        }
        for (uint32_t ch = active; ch < outputChannels; ++ch) {
            if (output[ch]) {
                std::fill(output[ch], output[ch] + frames, 0.0f);
            }
        }
    }

private:
    uint32_t channels_ = 0;
    uint32_t maxBlockFrames_ = 0;
    bool ready_ = false;
    std::array<SpectralSpray, kSpectralTopologyChannels> sprays_ {};
    std::array<SpectralSprayParams, kSpectralTopologyChannels> laneParams_ {};
    std::vector<std::vector<float>> monoIn_;
    std::vector<std::vector<float>> monoOut_;
};

} // namespace s3g
