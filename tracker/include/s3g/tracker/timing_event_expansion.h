#pragma once

#include "s3g/tracker/sequencer.h"
#include "s3g/tracker/timing_event_types.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace s3g::tracker {

namespace detail {

inline uint64_t saturatingSampleAdd(uint64_t left, uint64_t right) noexcept
{
    return right > std::numeric_limits<uint64_t>::max() - left
        ? std::numeric_limits<uint64_t>::max() : left + right;
}

inline uint32_t saturatingFrameOffsetAdd(uint32_t left,
    uint64_t right) noexcept
{
    const uint64_t sum = static_cast<uint64_t>(left) + right;
    return static_cast<uint32_t>(std::min<uint64_t>(sum,
        std::numeric_limits<uint32_t>::max()));
}

inline float scaledTimingVelocity(float velocity, float scale) noexcept
{
    if (!std::isfinite(velocity) || !std::isfinite(scale)) return 0.0f;
    return std::clamp(velocity * scale, 0.0f, 1.0f);
}

} // namespace detail

// Emit is called once per expanded onset. AllocateNoteId is called only for
// secondary onsets; the first onset retains the sequencer's canonical ID.
template <typename AllocateNoteId, typename Emit>
void expandTimingEvent(const ScheduledEvent& primary,
    const TimingEventExpansion& requested,
    AllocateNoteId&& allocateNoteId, Emit&& emit) noexcept
{
    const uint64_t tick = std::max<uint64_t>(
        requested.tickDurationSamples, 1u);
    const uint8_t ratchets = std::clamp<uint8_t>(
        requested.ratchetCount, 1u, 8u);
    const uint8_t stutters = std::clamp<uint8_t>(
        requested.stutterCount, 1u, 8u);
    const uint64_t baseTime = detail::saturatingSampleAdd(
        primary.absoluteSampleTime, requested.baseDelaySamples);
    const float baseVelocity = detail::scaledTimingVelocity(
        primary.normalizedVelocity, requested.velocityScale);

    const auto emitAt = [&](uint64_t absoluteTime, float velocity,
                            bool retainPrimaryId) {
        ScheduledEvent event = primary;
        event.absoluteSampleTime = absoluteTime;
        event.frameOffset = detail::saturatingFrameOffsetAdd(
            primary.frameOffset,
            absoluteTime >= primary.absoluteSampleTime
                ? absoluteTime - primary.absoluteSampleTime : 0u);
        event.noteId = retainPrimaryId ? primary.noteId : allocateNoteId();
        event.normalizedVelocity = velocity;
        emit(event);
    };

    for (uint8_t index = 0u; index < ratchets; ++index) {
        const uint64_t offset = tick * static_cast<uint64_t>(index)
            / static_cast<uint64_t>(ratchets);
        emitAt(detail::saturatingSampleAdd(baseTime, offset), baseVelocity,
            index == 0u);
    }
    if (requested.flamEnabled) {
        emitAt(detail::saturatingSampleAdd(baseTime,
                   requested.flamDelaySamples),
            detail::scaledTimingVelocity(baseVelocity, 0.72f), false);
    }
    for (uint8_t index = 1u; index < stutters; ++index) {
        const uint64_t offset = tick * static_cast<uint64_t>(index);
        emitAt(detail::saturatingSampleAdd(baseTime, offset), baseVelocity,
            false);
    }
    if (requested.ghostEnabled) {
        emitAt(detail::saturatingSampleAdd(baseTime, tick / 2u),
            detail::scaledTimingVelocity(primary.normalizedVelocity,
                requested.ghostVelocityScale),
            false);
    }
}

} // namespace s3g::tracker
