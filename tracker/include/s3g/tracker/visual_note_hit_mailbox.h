#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace s3g::tracker {

struct VisualNoteHitEvent {
    uint64_t absoluteSampleTime = 0u;
    std::size_t row = 0u;
};

// Single-writer audio-thread publication for one lane's latest visual onset.
// The GUI consumes each completed publication at most once. Payload members
// remain atomic so the seqlock never introduces a data race when the GUI reads
// while the audio thread begins the next publication.
class VisualNoteHitMailbox {
public:
    void publish(std::size_t row, uint64_t absoluteSampleTime) noexcept
    {
        sequence_.fetch_add(1u, std::memory_order_acq_rel);
        row_.store(row, std::memory_order_relaxed);
        absoluteSampleTime_.store(absoluteSampleTime,
            std::memory_order_relaxed);
        sequence_.fetch_add(1u, std::memory_order_release);
    }

    bool readLatest(uint64_t& consumedSequence,
        VisualNoteHitEvent& event) const noexcept
    {
        // A publication spans only three atomic operations. Avoid blocking
        // the GUI indefinitely if an audio callback happens to preempt it.
        for (unsigned attempt = 0u; attempt < 4u; ++attempt) {
            const uint64_t before = sequence_.load(std::memory_order_acquire);
            if ((before & 1u) != 0u) continue;
            const auto row = row_.load(std::memory_order_relaxed);
            const auto sampleTime = absoluteSampleTime_.load(
                std::memory_order_relaxed);
            const uint64_t after = sequence_.load(std::memory_order_acquire);
            if (before != after || (after & 1u) != 0u) continue;
            if (after == 0u || after == consumedSequence) return false;
            consumedSequence = after;
            event.row = row;
            event.absoluteSampleTime = sampleTime;
            return true;
        }
        return false;
    }

private:
    std::atomic<uint64_t> sequence_ { 0u };
    std::atomic<std::size_t> row_ { 0u };
    std::atomic<uint64_t> absoluteSampleTime_ { 0u };
};

} // namespace s3g::tracker
