#pragma once

#include <cstddef>
#include <cstdint>

namespace s3g::tracker {

// Fully resolved note timing for one lane tick. This is carried beside, not
// inside, ScheduledEvent so the established audio/MIDI event ABI is unchanged.
struct TimingEventExpansion {
    uint64_t tickDurationSamples = 1u;
    uint64_t baseDelaySamples = 0u;
    uint64_t flamDelaySamples = 0u;
    uint8_t ratchetCount = 1u;
    uint8_t stutterCount = 1u;
    float velocityScale = 1.0f;
    float ghostVelocityScale = 0.0f;
    bool flamEnabled = false;
    bool ghostEnabled = false;
};

constexpr std::size_t timingSecondaryOnsetCount(
    const TimingEventExpansion& timing) noexcept
{
    return static_cast<std::size_t>(timing.ratchetCount > 0u
            ? timing.ratchetCount - 1u : 0u)
        + static_cast<std::size_t>(timing.stutterCount > 0u
                ? timing.stutterCount - 1u : 0u)
        + (timing.flamEnabled ? 1u : 0u)
        + (timing.ghostEnabled ? 1u : 0u);
}

} // namespace s3g::tracker
