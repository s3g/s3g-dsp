#include "s3g/tracker/scheduled_event_timeline.h"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {

using s3g::tracker::EventDestination;
using s3g::tracker::ScheduledEvent;
using s3g::tracker::ScheduledEventTimeline;

int failures = 0;

void check(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

ScheduledEvent eventAt(uint64_t frame, uint64_t noteId)
{
    ScheduledEvent event;
    event.absoluteSampleTime = frame;
    event.noteId = noteId;
    event.destination = EventDestination::Internal;
    return event;
}

void testCrossBlockOrderingAndOffsets()
{
    ScheduledEventTimeline timeline;
    const std::array<ScheduledEvent, 4u> generated {
        eventAt(140u, 4u),
        eventAt(99u, 1u),
        eventAt(100u, 2u),
        eventAt(100u, 3u),
    };
    check(timeline.enqueue(generated.data(), generated.size()),
        "a small generated batch should fit");

    std::array<ScheduledEvent, 4u> due {};
    auto count = timeline.drain(96u, 100u, due.data(), due.size());
    check(count == 1u && due[0u].noteId == 1u
            && due[0u].frameOffset == 3u,
        "the first block should contain only events before its exclusive end");

    count = timeline.drain(100u, 128u, due.data(), due.size());
    check(count == 2u && due[0u].noteId == 2u
            && due[1u].noteId == 3u
            && due[0u].frameOffset == 0u
            && due[1u].frameOffset == 0u,
        "equal-time events should retain insertion order in the next block");
    check(timeline.pendingEventCount() == 1u,
        "a future event should remain pending across both blocks");

    count = timeline.drain(128u, 160u, due.data(), due.size());
    check(count == 1u && due[0u].noteId == 4u
            && due[0u].frameOffset == 12u,
        "a retained future event should receive its destination block offset");
}

void testLateEventAndFailClosedOutput()
{
    ScheduledEventTimeline timeline;
    const std::array<ScheduledEvent, 3u> generated {
        eventAt(8u, 1u), eventAt(10u, 2u), eventAt(11u, 3u),
    };
    check(timeline.enqueue(generated.data(), generated.size()),
        "late-event test batch should fit");
    std::array<ScheduledEvent, 1u> due {};
    const auto count = timeline.drain(10u, 12u, due.data(), due.size());
    check(count == 1u && due[0u].noteId == 1u
            && due[0u].frameOffset == 0u,
        "a late event should clamp to the start of the current block");
    check(timeline.droppedEventCount() == 2u
            && timeline.pendingEventCount() == 0u,
        "due events beyond output capacity must be consumed and counted");
}

void testResetAndDestinationNone()
{
    ScheduledEventTimeline timeline;
    auto ignored = eventAt(5u, 1u);
    ignored.destination = EventDestination::None;
    check(timeline.enqueue(ignored)
            && timeline.pendingEventCount() == 0u,
        "destination-none events should not consume pending capacity");
    check(timeline.enqueue(eventAt(6u, 2u))
            && timeline.highWaterMark() == 1u,
        "the timeline should expose pending high-water telemetry");
    timeline.reset();
    check(timeline.pendingEventCount() == 0u
            && timeline.highWaterMark() == 0u
            && timeline.droppedEventCount() == 0u,
        "reset should clear events and telemetry");
}

void testCanonicalSameSampleOrdering()
{
    ScheduledEventTimeline timeline;
    auto onset = eventAt(100u, 1u);
    onset.track = 2u;
    auto release = eventAt(100u, 2u);
    release.track = 2u;
    release.kind = s3g::tracker::ScheduledEventKind::NoteOff;
    auto parameter = eventAt(100u, 0u);
    parameter.track = 2u;
    parameter.kind = s3g::tracker::ScheduledEventKind::Parameter;
    auto control = eventAt(100u, 0u);
    control.track = 2u;
    control.kind = s3g::tracker::ScheduledEventKind::ControlChange;
    auto earlierTrack = eventAt(100u, 3u);
    earlierTrack.track = 1u;
    check(timeline.enqueue(onset) && timeline.enqueue(parameter)
            && timeline.enqueue(control) && timeline.enqueue(release)
            && timeline.enqueue(earlierTrack),
        "same-sample ordering test events should fit");
    std::array<ScheduledEvent, 5u> due {};
    const auto count = timeline.drain(96u, 101u, due.data(), due.size());
    check(count == 5u && due[0u].track == 1u
            && due[1u].kind
                == s3g::tracker::ScheduledEventKind::NoteOff
            && due[2u].kind
                == s3g::tracker::ScheduledEventKind::Parameter
            && due[3u].kind
                == s3g::tracker::ScheduledEventKind::ControlChange
            && due[4u].kind
                == s3g::tracker::ScheduledEventKind::NoteOn,
        "same-sample delivery should be track-stable and release-control-onset ordered");
}

void testControlInterpolationCancellation()
{
    ScheduledEventTimeline timeline;
    auto before = eventAt(90u, 0u);
    before.kind = s3g::tracker::ScheduledEventKind::ControlChange;
    before.channel = 4u;
    before.parameterId = 74u;
    before.generatedInterpolation = true;
    auto stale = before;
    stale.absoluteSampleTime = 110u;
    auto other = stale;
    other.parameterId = 71u;
    check(timeline.enqueue(before) && timeline.enqueue(stale)
            && timeline.enqueue(other),
        "control cancellation fixture should fit");
    check(timeline.cancelPendingControlInterpolation(4u, 74u, 100u) == 1u,
        "a new endpoint should cancel only its future derived CC tail");
    std::array<ScheduledEvent, 3u> due {};
    const auto count = timeline.drain(0u, 200u, due.data(), due.size());
    check(count == 2u && due[0u].parameterId == 74u
            && due[1u].parameterId == 71u,
        "past and unrelated CC interpolation points should remain queued");
}

void testBatchInsertionIsTransactional()
{
    ScheduledEventTimeline timeline;
    for (std::size_t index = 0u;
         index + 1u < s3g::tracker::kMaximumPendingScheduledEvents;
         ++index) {
        check(timeline.enqueue(eventAt(index + 1u, index + 1u)),
            "transaction fixture events should fit");
    }
    const std::array<ScheduledEvent, 2u> group {
        eventAt(9000u, 9000u), eventAt(9001u, 9001u),
    };
    const auto before = timeline.pendingEventCount();
    check(!timeline.enqueue(group.data(), group.size())
            && timeline.pendingEventCount() == before
            && timeline.droppedEventCount() == group.size(),
        "a timing group that cannot fit must be rejected without a partial prefix");
}

void testPendingPrimaryLifecycleCancellation()
{
    ScheduledEventTimeline timeline;
    auto earlier = eventAt(90u, 1u);
    auto equal = eventAt(100u, 2u);
    auto later = eventAt(110u, 3u);
    auto noteParameter = eventAt(100u, 2u);
    noteParameter.kind = s3g::tracker::ScheduledEventKind::Parameter;
    noteParameter.parameterScope = s3g::tracker::ParameterScope::Note;
    auto globalParameter = eventAt(100u, 2u);
    globalParameter.kind = s3g::tracker::ScheduledEventKind::Parameter;
    globalParameter.parameterScope = s3g::tracker::ParameterScope::Global;
    const std::array<ScheduledEvent, 5u> pending {
        earlier, equal, later, noteParameter, globalParameter,
    };
    check(timeline.enqueue(pending.data(), pending.size()),
        "lifecycle cancellation fixture should fit");
    check(timeline.cancelPendingPrimaryOnset(2u, 100u)
            && timeline.cancelPendingPrimaryOnset(3u, 100u)
            && !timeline.cancelPendingPrimaryOnset(1u, 100u),
        "equal/later pending onsets should cancel while an earlier onset remains");
    std::array<ScheduledEvent, 5u> due {};
    const auto count = timeline.drain(0u, 200u, due.data(), due.size());
    check(count == 2u && due[0u].noteId == 1u
            && due[0u].kind == s3g::tracker::ScheduledEventKind::NoteOn
            && due[1u].kind
                == s3g::tracker::ScheduledEventKind::Parameter
            && due[1u].parameterScope
                == s3g::tracker::ParameterScope::Global,
        "cancellation should remove note controls but retain global controls");
}

} // namespace

int main()
{
    testCrossBlockOrderingAndOffsets();
    testLateEventAndFailClosedOutput();
    testResetAndDestinationNone();
    testCanonicalSameSampleOrdering();
    testControlInterpolationCancellation();
    testBatchInsertionIsTransactional();
    testPendingPrimaryLifecycleCancellation();
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "scheduled event timeline tests passed\n";
    return EXIT_SUCCESS;
}
