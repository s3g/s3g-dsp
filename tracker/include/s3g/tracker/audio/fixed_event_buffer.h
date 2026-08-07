#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace s3g::tracker::audio {

// Callback-owned storage for a bounded number of trivially copyable events.
// Overflow is explicit and never changes the capacity or allocates memory.
template <typename Event, std::size_t Capacity>
class FixedEventBuffer {
    static_assert(Capacity > 0u, "FixedEventBuffer needs non-zero capacity");
    static_assert(std::is_trivially_copyable<Event>::value,
        "Realtime events must be trivially copyable");

public:
    using value_type = Event;

    constexpr std::size_t capacity() const noexcept { return Capacity; }
    std::size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0u; }
    uint64_t droppedCount() const noexcept
    {
        return droppedCount_.load(std::memory_order_relaxed);
    }

    Event* data() noexcept { return storage_.data(); }
    const Event* data() const noexcept { return storage_.data(); }

    Event& operator[](std::size_t index) noexcept { return storage_[index]; }
    const Event& operator[](std::size_t index) const noexcept
    {
        return storage_[index];
    }

    bool push(const Event& event) noexcept
    {
        if (size_ >= Capacity) {
            droppedCount_.fetch_add(1u, std::memory_order_relaxed);
            return false;
        }
        storage_[size_++] = event;
        return true;
    }

    // Clears block contents while preserving cumulative overflow telemetry.
    void clear() noexcept { size_ = 0u; }
    void resetDroppedCount() noexcept
    {
        droppedCount_.store(0u, std::memory_order_relaxed);
    }

private:
    std::array<Event, Capacity> storage_ {};
    std::size_t size_ = 0u;
    std::atomic<uint64_t> droppedCount_ { 0u };
};

} // namespace s3g::tracker::audio
