#pragma once

#include <clap/clap.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace s3g::clap_gui {

enum class ParamEventKind : uint8_t {
    GestureBegin,
    Value,
    GestureEnd,
};

struct ParamEvent {
    ParamEventKind kind = ParamEventKind::Value;
    clap_id paramId = CLAP_INVALID_ID;
    double value = 0.0;
};

// Single producer (the Cocoa main thread), single consumer (process/flush).
// One slot remains empty so equal indices always mean an empty queue.
// Batch publication advances the write index only after every element has
// been copied, so compound commands cannot become partially visible.
template <typename Event, uint32_t Capacity>
class SpscEventQueue {
    static_assert(Capacity > 1u);

public:
    uint32_t available() const noexcept
    {
        const uint32_t write = writeIndex_.load(std::memory_order_relaxed);
        const uint32_t read = readIndex_.load(std::memory_order_acquire);
        const uint32_t used = write >= read
            ? write - read : Capacity - (read - write);
        return Capacity - 1u - used;
    }

    bool push(const Event& event) noexcept
    {
        return pushBatch(&event, 1u);
    }

    bool pushBatch(const Event* events, uint32_t count) noexcept
    {
        if (count == 0u) return true;
        if (!events || count >= Capacity) return false;
        const uint32_t write = writeIndex_.load(std::memory_order_relaxed);
        if (count > available()) return false;
        uint32_t next = write;
        for (uint32_t index = 0u; index < count; ++index) {
            events_[next] = events[index];
            next = (next + 1u) % Capacity;
        }
        writeIndex_.store(next, std::memory_order_release);
        return true;
    }

    bool peek(Event& event) const noexcept
    {
        const uint32_t read = readIndex_.load(std::memory_order_relaxed);
        if (read == writeIndex_.load(std::memory_order_acquire)) return false;
        event = events_[read];
        return true;
    }

    void pop() noexcept
    {
        const uint32_t read = readIndex_.load(std::memory_order_relaxed);
        if (read != writeIndex_.load(std::memory_order_acquire)) {
            readIndex_.store((read + 1u) % Capacity, std::memory_order_release);
        }
    }

private:
    std::array<Event, Capacity> events_ {};
    std::atomic<uint32_t> readIndex_ { 0u };
    std::atomic<uint32_t> writeIndex_ { 0u };
};

template <uint32_t Capacity = 512u>
class ParamEventQueue final : public SpscEventQueue<ParamEvent, Capacity> {};

} // namespace s3g::clap_gui
