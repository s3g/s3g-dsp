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
template <uint32_t Capacity = 512u>
class ParamEventQueue {
    static_assert(Capacity > 1u);

public:
    bool push(const ParamEvent& event) noexcept
    {
        const uint32_t write = writeIndex_.load(std::memory_order_relaxed);
        const uint32_t next = (write + 1u) % Capacity;
        if (next == readIndex_.load(std::memory_order_acquire)) return false;
        events_[write] = event;
        writeIndex_.store(next, std::memory_order_release);
        return true;
    }

    bool peek(ParamEvent& event) const noexcept
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
    std::array<ParamEvent, Capacity> events_ {};
    std::atomic<uint32_t> readIndex_ { 0u };
    std::atomic<uint32_t> writeIndex_ { 0u };
};

} // namespace s3g::clap_gui
