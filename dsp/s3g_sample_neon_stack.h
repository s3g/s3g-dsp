#pragma once
#include "s3g_sample_asset.h"
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
// Main-thread publication. Audio never allocates or modifies a stack.
struct NeonStack {
    std::array<NeonStackLayer, kSampleNeonLayers> layers {};
    uint8_t count = 0u;
    uint8_t primarySliceCount = 1u;
    std::array<double, 33u> primarySlices {{0.0, 1.0}};
};
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
