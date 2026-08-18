#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace s3g::sample {

constexpr std::size_t kMaximumAudioChannels = 16u;

// Shared decoded-audio document used by the Sample-family instruments. All
// active lanes have the same frame count so one playback clock remains sample
// locked for mono, stereo, and spatial material.
struct SampleAsset {
    double sampleRate = 48000.0;
    std::array<std::vector<float>, kMaximumAudioChannels> channels {};
    uint8_t channelCount = 0u;

    bool valid() const noexcept
    {
        if (!(sampleRate > 0.0) || !std::isfinite(sampleRate)
            || channelCount == 0u || channelCount > channels.size()
            || channels[0u].empty()
            || channels[0u].size() > std::numeric_limits<uint32_t>::max())
            return false;
        const auto finite = [](const std::vector<float>& channel) {
            return std::all_of(channel.begin(), channel.end(),
                [](float value) { return std::isfinite(value); });
        };
        const std::size_t frames = channels[0u].size();
        for (std::size_t index = 0u; index < channels.size(); ++index) {
            if (index < channelCount) {
                if (channels[index].size() != frames
                    || !finite(channels[index])) return false;
            } else if (!channels[index].empty()) return false;
        }
        return true;
    }

    uint32_t frameCount() const noexcept
    {
        return channelCount != 0u
                && channels[0u].size()
                    <= std::numeric_limits<uint32_t>::max()
            ? static_cast<uint32_t>(channels[0u].size()) : 0u;
    }
};

struct SafeSampleBounds {
    uint32_t startFrame = 0u;
    uint32_t endFrame = 0u;

    bool validFor(const SampleAsset& asset) const noexcept
    {
        return startFrame < endFrame && endFrame <= asset.frameCount();
    }
};

inline float sampleBoundaryMagnitude(const SampleAsset& asset,
    uint32_t boundary) noexcept
{
    const uint32_t frames = asset.frameCount();
    if (frames == 0u || boundary > frames) return 0.0f;
    float magnitude = 0.0f;
    for (uint8_t channel = 0u; channel < asset.channelCount; ++channel) {
        const auto& samples = asset.channels[channel];
        if (boundary > 0u)
            magnitude = std::max(magnitude,
                std::abs(samples[boundary - 1u]));
        if (boundary < frames)
            magnitude = std::max(magnitude, std::abs(samples[boundary]));
    }
    return magnitude;
}

inline bool sampleBoundaryCrossesZero(const SampleAsset& asset,
    uint32_t boundary) noexcept
{
    constexpr float kQuietBoundary = 1.0e-5f;
    const uint32_t frames = asset.frameCount();
    if (frames == 0u || boundary > frames) return false;
    if (boundary == 0u || boundary == frames)
        return sampleBoundaryMagnitude(asset, boundary) <= kQuietBoundary;

    // A shared boundary must not be derived from an inter-channel average,
    // which could invent a crossing through cancellation. Use the loudest
    // lane around this boundary as the timing reference, then retain the same
    // frame for every source channel.
    uint8_t referenceChannel = 0u;
    float referenceMagnitude = -1.0f;
    for (uint8_t channel = 0u; channel < asset.channelCount; ++channel) {
        const auto& samples = asset.channels[channel];
        const float magnitude = std::max(std::abs(samples[boundary - 1u]),
            std::abs(samples[boundary]));
        if (magnitude > referenceMagnitude) {
            referenceMagnitude = magnitude;
            referenceChannel = channel;
        }
    }
    const auto& reference = asset.channels[referenceChannel];
    const float left = reference[boundary - 1u];
    const float right = reference[boundary];
    return left == 0.0f || right == 0.0f
        || (left < 0.0f && right > 0.0f)
        || (left > 0.0f && right < 0.0f);
}

inline uint32_t nearestSafeSampleBoundary(const SampleAsset& asset,
    uint32_t target, uint32_t minimum, uint32_t maximum) noexcept
{
    const uint32_t frames = asset.frameCount();
    if (frames == 0u) return 0u;
    minimum = std::min(minimum, frames);
    maximum = std::clamp(maximum, minimum, frames);
    target = std::clamp(target, minimum, maximum);

    uint32_t quietest = target;
    float quietestMagnitude = sampleBoundaryMagnitude(asset, target);
    const uint32_t radius = std::max(target - minimum, maximum - target);
    for (uint32_t distance = 0u; distance <= radius; ++distance) {
        std::array<uint32_t, 2u> candidates {{
            target >= distance ? target - distance : minimum,
            distance <= maximum - target
                ? target + distance : maximum,
        }};
        bool found = false;
        uint32_t best = target;
        float bestMagnitude = std::numeric_limits<float>::infinity();
        for (std::size_t index = 0u; index < candidates.size(); ++index) {
            const uint32_t candidate = candidates[index];
            if (candidate < minimum || candidate > maximum
                || (index == 1u && candidate == candidates[0u])) continue;
            const float magnitude = sampleBoundaryMagnitude(asset, candidate);
            if (magnitude < quietestMagnitude) {
                quietest = candidate;
                quietestMagnitude = magnitude;
            }
            if (sampleBoundaryCrossesZero(asset, candidate)
                && magnitude < bestMagnitude) {
                found = true;
                best = candidate;
                bestMagnitude = magnitude;
            }
        }
        if (found) return best;
    }
    return quietest;
}

inline SafeSampleBounds defaultSafeSampleBounds(
    const SampleAsset& asset) noexcept
{
    SafeSampleBounds bounds { 0u, asset.frameCount() };
    if (!asset.valid() || asset.frameCount() < 2u) return bounds;
    const uint32_t frames = asset.frameCount();
    const uint32_t searchFrames = std::max<uint32_t>(1u,
        static_cast<uint32_t>(std::clamp<double>(std::round(
            asset.sampleRate * 0.010), 1.0,
            static_cast<double>(frames - 1u))));
    bounds.startFrame = nearestSafeSampleBoundary(asset, 0u, 0u,
        std::min(searchFrames, frames - 1u));
    bounds.endFrame = nearestSafeSampleBoundary(asset, frames,
        frames > searchFrames ? frames - searchFrames : 1u, frames);
    if (!bounds.validFor(asset)) bounds = { 0u, frames };
    return bounds;
}

} // namespace s3g::sample
