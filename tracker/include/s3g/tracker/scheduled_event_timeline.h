#pragma once

#include "s3g/tracker/fixed_timed_event_queue.h"
#include "s3g/tracker/sequencer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace s3g::tracker {

struct CanonicalScheduledEventOrder {
    static bool earlierAtEqualTime(const ScheduledEvent& left,
        const ScheduledEvent& right) noexcept
    {
        if (left.track != right.track) return left.track < right.track;
        const auto priority = [](ScheduledEventKind kind) {
            switch (kind) {
            case ScheduledEventKind::NoteOff: return 0u;
            case ScheduledEventKind::Parameter: return 1u;
            case ScheduledEventKind::NoteOn: return 2u;
            }
            return 3u;
        };
        return priority(left.kind) < priority(right.kind);
    }
};

// Persistent allocation-free boundary between tick generation and block
// delivery. Sequencer timing actions may create events beyond the block that
// generated them; this timeline retains those events and emits a canonical,
// stable absolute-time ordering only when they become due.
class ScheduledEventTimeline {
public:
    bool enqueue(const ScheduledEvent& event) noexcept
    {
        if (event.destination == EventDestination::None) return true;
        if (events_.push(event)) return true;
        ++droppedEventCount_;
        return false;
    }

    bool enqueue(const ScheduledEvent* events, std::size_t count) noexcept
    {
        if (!events && count != 0u) {
            droppedEventCount_ += count;
            return false;
        }
        std::size_t routedCount = 0u;
        for (std::size_t index = 0u; index < count; ++index) {
            if (events[index].destination != EventDestination::None)
                ++routedCount;
        }
        if (routedCount > events_.remainingCapacity()) {
            droppedEventCount_ += routedCount;
            return false;
        }
        bool accepted = true;
        for (std::size_t index = 0u; index < count; ++index)
            accepted = enqueue(events[index]) && accepted;
        return accepted;
    }

    // Cancel a primary onset that has not happened before its logical release.
    // Note-scoped controls belonging to that canceled voice are removed too;
    // global/channel controls and independently identified secondary hits stay.
    bool cancelPendingPrimaryOnset(uint64_t noteId,
        uint64_t releaseTime) noexcept
    {
        if (noteId == 0u) return false;
        const auto removedOnsets = events_.eraseIf(
            [noteId, releaseTime](const ScheduledEvent& event) {
                return event.noteId == noteId
                    && event.kind == ScheduledEventKind::NoteOn
                    && event.absoluteSampleTime >= releaseTime;
            });
        if (removedOnsets == 0u) return false;
        (void)events_.eraseIf(
            [noteId, releaseTime](const ScheduledEvent& event) {
                return event.noteId == noteId
                    && event.kind == ScheduledEventKind::Parameter
                    && event.parameterScope == ParameterScope::Note
                    && event.absoluteSampleTime >= releaseTime;
            });
        return true;
    }

    std::size_t drain(uint64_t blockStart, uint64_t blockEnd,
        ScheduledEvent* output, std::size_t outputCapacity) noexcept
    {
        if (!output) outputCapacity = 0u;
        std::size_t outputCount = 0u;
        ScheduledEvent event;
        while (events_.popBefore(blockEnd, event)) {
            const uint64_t relative = event.absoluteSampleTime > blockStart
                ? event.absoluteSampleTime - blockStart : 0u;
            event.frameOffset = static_cast<uint32_t>(std::min<uint64_t>(
                relative, std::numeric_limits<uint32_t>::max()));
            if (outputCount < outputCapacity) output[outputCount++] = event;
            else ++droppedEventCount_;
        }
        return outputCount;
    }

    void clear() noexcept { events_.clear(); }

    void reset() noexcept
    {
        events_.clear();
        events_.resetMetrics();
        droppedEventCount_ = 0u;
    }

    std::size_t pendingEventCount() const noexcept { return events_.size(); }
    std::size_t highWaterMark() const noexcept
    {
        return events_.highWaterMark();
    }
    uint64_t droppedEventCount() const noexcept
    {
        return droppedEventCount_;
    }

private:
    FixedTimedEventQueue<ScheduledEvent,
        kMaximumPendingScheduledEvents,
        CanonicalScheduledEventOrder> events_;
    uint64_t droppedEventCount_ = 0u;
};

} // namespace s3g::tracker
