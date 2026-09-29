#pragma once
#include "s3g_sample_asset.h"
#include "s3g_sample_cutups.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace s3g::sample {
struct WavesetMap;
constexpr unsigned kSampleNeonLayers = 32u;
enum class NeonSourceMode : uint8_t { Primary, Selected, Velocity, Random, Scan };
struct NeonStackLayer {
    const SampleAsset* asset = nullptr;
    double start = 0.0, end = 1.0;
    const WavesetMap* wavesets = nullptr;
};
struct NeonMosaicFragment {
    float start = 0, end = 1, energy = 0, brightness = 0;
    uint8_t layer = 0;
};
// Main-thread publication. Audio never allocates or modifies a stack.
struct NeonStack {
    std::array<NeonStackLayer, kSampleNeonLayers> layers {};
    std::array<CutupsLaneMetadata, kSampleNeonLayers> cutupsMetadata {};
    std::array<float, kSampleNeonLayers> sourceBpms {};
    uint8_t count = 0u;
    uint8_t primarySliceCount = 1u;
    std::array<double, 33u> primarySlices {{0.0, 1.0}};
    std::array<NeonMosaicFragment, 1024> fragments {};
    unsigned fragmentCount = 0;
};
// Bounded descriptor analysis on the publication thread, never in render().
// Use channel energy, not a sum that can cancel anti-phase/spatial sources.
inline NeonMosaicFragment neonDescribeFragment(const NeonStackLayer& source,
    unsigned layer, double start, double end) noexcept {
    NeonMosaicFragment result {static_cast<float>(start), static_cast<float>(end), 0, 0, static_cast<uint8_t>(layer)};
    if (!source.asset || source.asset->frameCount() < 2 || end <= start) return result;
    const auto& asset = *source.asset;
    const auto lo = static_cast<std::size_t>(start * (asset.frameCount() - 1));
    const auto hi = static_cast<std::size_t>(end * (asset.frameCount() - 1));
    double energy = 0, difference = 0; unsigned count = 0;
    for (unsigned probe = 0; probe < 16; ++probe) {
        const auto begin = lo + (hi - lo) * probe / 16;
        for (auto at = begin; at < std::min(hi, begin + 16); ++at)
            for (unsigned ch = 0; ch < asset.channelCount; ++ch) {
                const double a = asset.channels[ch][at], b = asset.channels[ch][at + 1];
                energy += a * a; difference += (b - a) * (b - a); ++count;
            }
    }
    result.energy = static_cast<float>(std::clamp((20 * std::log10(std::sqrt(energy / std::max(1u, count)) + 1.e-9) + 60) / 60, 0., 1.));
    result.brightness = static_cast<float>(std::clamp(difference / std::max(1.e-12, energy) / 4, 0., 1.));
    return result;
}
inline unsigned neonChooseMosaic(const NeonStack& stack, unsigned previous, unsigned mode,
    bool allLayers, float target) noexcept {
    if (!stack.fragmentCount) return 0;
    previous = std::min(previous, stack.fragmentCount - 1);
    const auto& last = stack.fragments[previous];
    unsigned best = previous; float score = 1.e9f;
    // Walk from the previous fragment for deterministic tie-breaking. Avoid
    // immediately repeating it unless it is the only eligible candidate.
    for (unsigned step = 1; step <= stack.fragmentCount; ++step) {
        const unsigned index = (previous + step) % stack.fragmentCount;
        const auto& f = stack.fragments[index];
        if ((!allLayers && f.layer != 0) || !stack.layers[f.layer].asset) continue;
        const float distance = std::abs(f.energy - last.energy) + std::abs(f.brightness - last.brightness);
        const float candidate = (mode == 1 ? distance : mode == 2 ? -distance
            : mode == 3 ? std::abs(f.energy - target) : std::abs(f.brightness - target))
            + (index == previous ? 4.f : 0.f);
        if (candidate < score) { score = candidate; best = index; }
    }
    return best;
}
inline unsigned neonVelocityLayer(float velocity, unsigned count) noexcept {
    return count ? std::min(count - 1u, static_cast<unsigned>(
        std::clamp(velocity, 0.0f, 1.0f) * count)) : 0u;
}
struct NeonStackBlend { unsigned first = 0u, second = 0u; float mix = 0.0f; };
inline NeonStackBlend neonStackBlend(double position, unsigned count) noexcept {
    if (count < 2u) return {};
    const double p = std::clamp(position, 0.0, 1.0) * (count - 1u);
    const auto a = static_cast<unsigned>(p);
    return {a, std::min(count - 1u, a + 1u), static_cast<float>(p - a)};
}
} // namespace s3g::sample
