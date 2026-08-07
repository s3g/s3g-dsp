#include "s3g/tracker/fixed_timed_event_queue.h"

#include <cstdlib>
#include <iostream>

namespace {

struct Event {
    uint64_t absoluteSampleTime = 0u;
    uint32_t identity = 0u;
};

int failures = 0;

void check(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

void testOrderingAndDeadline()
{
    s3g::tracker::FixedTimedEventQueue<Event, 8u> queue;
    check(queue.push({ 20u, 1u }) && queue.push({ 10u, 2u })
            && queue.push({ 10u, 3u }) && queue.push({ 30u, 4u }),
        "bounded queue should accept entries below capacity");
    check(queue.size() == 4u && queue.highWaterMark() == 4u,
        "queue should report current and peak occupancy");

    Event event;
    check(!queue.popBefore(10u, event),
        "exclusive deadline should retain an equal-time event");
    check(queue.popBefore(11u, event) && event.identity == 2u
            && queue.popBefore(11u, event) && event.identity == 3u,
        "equal-time events should retain insertion order");
    check(queue.popBefore(21u, event) && event.identity == 1u,
        "heap should return the earliest absolute sample time");
    check(!queue.popBefore(30u, event)
            && queue.popBefore(31u, event) && event.identity == 4u,
        "deadline crossing should be exact at the sample boundary");
}

void testOverflowAndClear()
{
    s3g::tracker::FixedTimedEventQueue<Event, 2u> queue;
    check(queue.push({ 2u, 1u }) && queue.push({ 1u, 2u })
            && !queue.push({ 0u, 3u }) && queue.size() == 2u,
        "overflow should reject without corrupting retained entries");
    queue.clear();
    check(queue.empty() && queue.highWaterMark() == 2u,
        "clear should cancel entries while retaining peak telemetry");
    queue.resetMetrics();
    check(queue.highWaterMark() == 0u && queue.push({ 4u, 4u }),
        "metric reset should not affect subsequent queue use");
}

void testAllocationFreeCancellationRestoresHeapOrder()
{
    s3g::tracker::FixedTimedEventQueue<Event, 8u> queue;
    check(queue.push({ 30u, 1u }) && queue.push({ 10u, 2u })
            && queue.push({ 20u, 3u }) && queue.push({ 10u, 4u })
            && queue.push({ 40u, 5u }),
        "cancellation fixture should fit in the fixed queue");
    const auto erased = queue.eraseIf([](const Event& event) {
        return event.identity == 2u || event.identity == 3u;
    });
    check(erased == 2u && queue.size() == 3u
            && queue.highWaterMark() == 5u,
        "in-place cancellation should retain occupancy telemetry");
    Event event;
    check(queue.popBefore(50u, event) && event.identity == 4u
            && queue.popBefore(50u, event) && event.identity == 1u
            && queue.popBefore(50u, event) && event.identity == 5u,
        "compaction must restore chronological heap order and stable ties");
}

} // namespace

int main()
{
    testOrderingAndDeadline();
    testOverflowAndClear();
    testAllocationFreeCancellationRestoresHeapOrder();
    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "fixed timed event queue tests passed\n";
    return EXIT_SUCCESS;
}
