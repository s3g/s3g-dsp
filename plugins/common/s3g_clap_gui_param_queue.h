#pragma once

#include <clap/clap.h>
#include <clap/ext/params.h>

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

// Single producer (the platform GUI thread), single consumer (process/flush).
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

inline void requestParamEventService(const clap_host_t* host,
    const clap_host_params_t* hostParams)
{
    if (host && hostParams && hostParams->request_flush)
        hostParams->request_flush(host);
    else if (host && host->request_process)
        host->request_process(host);
}

template <typename Queue>
bool enqueueParamEvent(Queue& queue, const clap_host_t* host,
    const clap_host_params_t* hostParams, ParamEventKind kind,
    clap_id parameterId, double value = 0.0)
{
    if (!queue.push({ kind, parameterId, value })) return false;
    requestParamEventService(host, hostParams);
    return true;
}

inline bool pushParamEvent(const clap_output_events_t* output,
    const ParamEvent& pending)
{
    if (!output || !output->try_push) return false;
    if (pending.kind == ParamEventKind::Value) {
        clap_event_param_value_t event {};
        event.header.size = sizeof(event);
        event.header.time = 0u;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.param_id = pending.paramId;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = pending.value;
        return output->try_push(output, &event.header);
    }
    clap_event_param_gesture_t event {};
    event.header.size = sizeof(event);
    event.header.time = 0u;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = pending.kind == ParamEventKind::GestureBegin
        ? CLAP_EVENT_PARAM_GESTURE_BEGIN : CLAP_EVENT_PARAM_GESTURE_END;
    event.header.flags = CLAP_EVENT_IS_LIVE;
    event.param_id = pending.paramId;
    return output->try_push(output, &event.header);
}

template <typename Queue, typename ApplyValue>
void serviceParamEvents(Queue& queue, const clap_output_events_t* output,
    ApplyValue&& applyValue)
{
    ParamEvent pending {};
    while (queue.peek(pending)) {
        if (!pushParamEvent(output, pending)) break;
        if (pending.kind == ParamEventKind::Value)
            applyValue(pending.paramId, pending.value);
        queue.pop();
    }
}

} // namespace s3g::clap_gui
