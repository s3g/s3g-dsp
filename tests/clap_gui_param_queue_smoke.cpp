#include <clap/clap.h>
#include <clap/ext/params.h>

#include "../plugins/common/s3g_clap_gui_param_queue.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

struct CapturedEvent {
    uint16_t type = 0u;
    uint32_t flags = 0u;
    clap_id parameterId = CLAP_INVALID_ID;
    double value = 0.0;
};

struct HostContext {
    uint32_t flushRequests = 0u;
    uint32_t processRequests = 0u;
};

struct OutputContext {
    bool accept = true;
    std::vector<CapturedEvent> events;
};

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << message << '\n';
    return condition;
}

void hostRequestRestart(const clap_host_t*) {}

void hostRequestProcess(const clap_host_t* host)
{
    if (host && host->host_data)
        ++static_cast<HostContext*>(host->host_data)->processRequests;
}

void hostRequestCallback(const clap_host_t*) {}

void hostRequestFlush(const clap_host_t* host)
{
    if (host && host->host_data)
        ++static_cast<HostContext*>(host->host_data)->flushRequests;
}

bool captureEvent(const clap_output_events_t* output,
    const clap_event_header_t* header)
{
    auto* context = output
        ? static_cast<OutputContext*>(output->ctx) : nullptr;
    if (!context || !context->accept || !header
        || header->space_id != CLAP_CORE_EVENT_SPACE_ID) return false;

    CapturedEvent captured;
    captured.type = header->type;
    captured.flags = header->flags;
    if (header->type == CLAP_EVENT_PARAM_VALUE
        && header->size >= sizeof(clap_event_param_value_t)) {
        const auto* value =
            reinterpret_cast<const clap_event_param_value_t*>(header);
        captured.parameterId = value->param_id;
        captured.value = value->value;
    } else if ((header->type == CLAP_EVENT_PARAM_GESTURE_BEGIN
            || header->type == CLAP_EVENT_PARAM_GESTURE_END)
        && header->size >= sizeof(clap_event_param_gesture_t)) {
        captured.parameterId =
            reinterpret_cast<const clap_event_param_gesture_t*>(header)
                ->param_id;
    } else {
        return false;
    }
    context->events.push_back(captured);
    return true;
}

} // namespace

int main()
{
    HostContext hostContext;
    clap_host_t host {
        CLAP_VERSION_INIT,
        &hostContext,
        "s3g GUI parameter queue smoke host",
        "s3g",
        "https://github.com/s3g/s3g-dsp",
        "0.1.0",
        nullptr,
        hostRequestRestart,
        hostRequestProcess,
        hostRequestCallback,
    };
    const clap_host_params_t hostParams {
        nullptr,
        nullptr,
        hostRequestFlush,
    };
    OutputContext outputContext;
    const clap_output_events_t output {
        &outputContext,
        captureEvent,
    };
    s3g::clap_gui::ParamEventQueue<8u> queue;

    constexpr clap_id parameterId = 77u;
    bool ok = true;
    ok &= expect(s3g::clap_gui::enqueueParamEvent(queue, &host,
        &hostParams, s3g::clap_gui::ParamEventKind::GestureBegin,
        parameterId), "could not enqueue gesture begin");
    ok &= expect(s3g::clap_gui::enqueueParamEvent(queue, &host,
        &hostParams, s3g::clap_gui::ParamEventKind::Value,
        parameterId, 0.25), "could not enqueue parameter value");
    ok &= expect(s3g::clap_gui::enqueueParamEvent(queue, &host,
        &hostParams, s3g::clap_gui::ParamEventKind::GestureEnd,
        parameterId), "could not enqueue gesture end");
    ok &= expect(hostContext.flushRequests == 3u,
        "GUI events did not request a host flush");

    uint32_t appliedValues = 0u;
    double appliedValue = 0.0;
    s3g::clap_gui::serviceParamEvents(queue, &output,
        [&](clap_id id, double value) {
            if (id == parameterId) {
                ++appliedValues;
                appliedValue = value;
            }
        });
    ok &= expect(outputContext.events.size() == 3u,
        "unexpected host event count");
    if (outputContext.events.size() == 3u) {
        ok &= expect(outputContext.events[0].type
                == CLAP_EVENT_PARAM_GESTURE_BEGIN
                && outputContext.events[1].type
                == CLAP_EVENT_PARAM_VALUE
                && outputContext.events[2].type
                == CLAP_EVENT_PARAM_GESTURE_END,
            "gesture events were emitted out of order");
        for (const auto& event : outputContext.events) {
            ok &= expect(event.parameterId == parameterId,
                "gesture event used the wrong parameter ID");
            ok &= expect((event.flags & CLAP_EVENT_IS_LIVE) != 0u,
                "GUI event is missing CLAP_EVENT_IS_LIVE");
        }
    }
    ok &= expect(appliedValues == 1u
            && std::abs(appliedValue - 0.25) < 1e-12,
        "parameter value was not applied exactly once");

    ok &= expect(s3g::clap_gui::enqueueParamEvent(queue, &host,
        &hostParams, s3g::clap_gui::ParamEventKind::Value,
        parameterId, 0.75), "could not enqueue back-pressure value");
    outputContext.accept = false;
    s3g::clap_gui::serviceParamEvents(queue, &output,
        [&](clap_id, double) { ++appliedValues; });
    s3g::clap_gui::ParamEvent pending;
    ok &= expect(queue.peek(pending) && appliedValues == 1u,
        "rejected host event was not retained");
    outputContext.accept = true;
    s3g::clap_gui::serviceParamEvents(queue, &output,
        [&](clap_id id, double value) {
            if (id == parameterId && std::abs(value - 0.75) < 1e-12)
                ++appliedValues;
        });
    ok &= expect(!queue.peek(pending) && appliedValues == 2u,
        "retained host event was not retried");

    s3g::clap_gui::requestParamEventService(&host, nullptr);
    ok &= expect(hostContext.processRequests == 1u,
        "request_process fallback was not used");

    if (ok) std::cout << "CLAP GUI parameter queue smoke passed\n";
    return ok ? 0 : 1;
}
