#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace s3g::tracker {

// Allocation-free stable min-heap for trivially copied musical events. Event
// must expose uint64-compatible absoluteSampleTime. Equal-time entries leave
// in insertion order, which preserves release -> parameter -> onset ordering.
template <typename Event>
struct StableTimedEventOrder {
    static bool earlierAtEqualTime(const Event&, const Event&) noexcept
    {
        return false;
    }
};

template <typename Event, std::size_t Capacity,
    typename EqualTimeOrder = StableTimedEventOrder<Event>>
class FixedTimedEventQueue {
public:
    static constexpr std::size_t capacity() noexcept { return Capacity; }

    bool push(const Event& event) noexcept
    {
        if (size_ >= Capacity) return false;
        Entry incoming { event, nextOrder_++ };
        std::size_t child = size_++;
        while (child > 0u) {
            const std::size_t parent = (child - 1u) / 2u;
            if (!earlier(incoming, entries_[parent])) break;
            entries_[child] = entries_[parent];
            child = parent;
        }
        entries_[child] = incoming;
        if (size_ > highWaterMark_) highWaterMark_ = size_;
        return true;
    }

    bool popBefore(uint64_t exclusiveDeadline, Event& event) noexcept
    {
        if (size_ == 0u
            || entries_[0u].event.absoluteSampleTime >= exclusiveDeadline)
            return false;
        event = entries_[0u].event;
        --size_;
        if (size_ == 0u) return true;
        entries_[0u] = entries_[size_];
        siftDown(0u);
        return true;
    }

    // Allocation-free cancellation seam used when a delayed musical event is
    // invalidated before delivery. Entry order tokens remain unchanged, then
    // the compacted range is restored to a heap in linear time.
    template <typename Predicate>
    std::size_t eraseIf(Predicate&& shouldErase) noexcept
    {
        std::size_t retained = 0u;
        for (std::size_t index = 0u; index < size_; ++index) {
            if (shouldErase(entries_[index].event)) continue;
            if (retained != index) entries_[retained] = entries_[index];
            ++retained;
        }
        const std::size_t erased = size_ - retained;
        size_ = retained;
        for (std::size_t parent = size_ / 2u; parent > 0u; --parent)
            siftDown(parent - 1u);
        return erased;
    }

    void clear() noexcept
    {
        size_ = 0u;
        nextOrder_ = 0u;
    }

    void resetMetrics() noexcept { highWaterMark_ = size_; }
    std::size_t size() const noexcept { return size_; }
    std::size_t remainingCapacity() const noexcept
    {
        return Capacity - size_;
    }
    std::size_t highWaterMark() const noexcept { return highWaterMark_; }
    bool empty() const noexcept { return size_ == 0u; }

private:
    struct Entry {
        Event event {};
        uint64_t order = 0u;
    };

    static bool earlier(const Entry& left, const Entry& right) noexcept
    {
        return left.event.absoluteSampleTime
                < right.event.absoluteSampleTime
            || (left.event.absoluteSampleTime
                    == right.event.absoluteSampleTime
                && (EqualTimeOrder::earlierAtEqualTime(
                        left.event, right.event)
                    || (!EqualTimeOrder::earlierAtEqualTime(
                            right.event, left.event)
                        && left.order < right.order)));
    }

    void siftDown(std::size_t parent) noexcept
    {
        Entry replacement = entries_[parent];
        while (true) {
            const std::size_t left = parent * 2u + 1u;
            if (left >= size_) break;
            const std::size_t right = left + 1u;
            const std::size_t earlierChild = right < size_
                    && earlier(entries_[right], entries_[left])
                ? right : left;
            if (!earlier(entries_[earlierChild], replacement)) break;
            entries_[parent] = entries_[earlierChild];
            parent = earlierChild;
        }
        entries_[parent] = replacement;
    }

    std::array<Entry, Capacity> entries_ {};
    std::size_t size_ = 0u;
    std::size_t highWaterMark_ = 0u;
    uint64_t nextOrder_ = 0u;
};

} // namespace s3g::tracker
