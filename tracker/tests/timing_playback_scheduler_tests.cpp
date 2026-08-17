#include "s3g/tracker/timing_playback_scheduler.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using s3g::tracker::FxActionCell;
using s3g::tracker::FxValueCell;
using s3g::tracker::EventDestination;
using s3g::tracker::LogicalTickBoundary;
using s3g::tracker::LogicalTickBoundaryAction;
using s3g::tracker::NoteCell;
using s3g::tracker::ParameterScope;
using s3g::tracker::Pattern;
using s3g::tracker::ScheduledEvent;
using s3g::tracker::ScheduledEventKind;
using s3g::tracker::Sequencer;
using s3g::tracker::SequencerAction;
using s3g::tracker::TimingPlaybackScheduler;
using s3g::tracker::TimingWarpTransform;
using s3g::tracker::Track;
using s3g::tracker::TransportSettings;
using s3g::tracker::ValueCell;

int failures = 0;

void check(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

TransportSettings transport()
{
    TransportSettings settings;
    settings.sampleRate = 8000.0;
    settings.bpm = 60.0;
    settings.ticksPerBeat = 1u;
    settings.swing = 0.5;
    return settings;
}

Track timingTrack(SequencerAction action, float value)
{
    Track track;
    track.notes = {
        NoteCell::withNote(60u), NoteCell::rest(),
        NoteCell::rest(), NoteCell::rest(),
    };
    track.velocities = { ValueCell::withValue(0.8f) };
    track.noteColumn.length = track.notes.size();
    track.velocityColumn.length = track.velocities.size();
    auto& pair = track.fxPairs[0u];
    pair.actions = {
        FxActionCell::sequencer(action), FxActionCell::empty(),
        FxActionCell::empty(), FxActionCell::empty(),
    };
    pair.values = {
        FxValueCell::withValue(value), FxValueCell::previous(),
        FxValueCell::previous(), FxValueCell::previous(),
    };
    pair.actionColumn.length = pair.actions.size();
    pair.valueColumn.length = pair.values.size();
    return track;
}

Pattern oneTrack(Track track)
{
    Pattern pattern;
    pattern.tracks.push_back(std::move(track));
    return pattern;
}

struct TickBoundaryCapture {
    TimingPlaybackScheduler* scheduler = nullptr;
    std::array<LogicalTickBoundary, 32u> boundaries {};
    std::array<std::size_t, 32u> pendingCounts {};
    std::size_t count = 0u;
};

LogicalTickBoundaryAction captureTickBoundary(void* opaque,
    const LogicalTickBoundary& boundary) noexcept
{
    auto& capture = *static_cast<TickBoundaryCapture*>(opaque);
    if (capture.count >= capture.boundaries.size())
        return LogicalTickBoundaryAction::Continue;
    capture.boundaries[capture.count] = boundary;
    capture.pendingCounts[capture.count] = capture.scheduler
        ? capture.scheduler->pendingEventCount() : 0u;
    ++capture.count;
    return LogicalTickBoundaryAction::Continue;
}

void testLogicalTickObserverPartitionInvariance()
{
    Track track;
    track.notes = {
        NoteCell::withNote(60u), NoteCell::rest(),
        NoteCell::withNote(62u), NoteCell::rest(),
    };
    track.noteColumn.length = track.notes.size();

    const auto render = [&track](const std::vector<uint32_t>& blocks) {
        TimingPlaybackScheduler scheduler;
        TickBoundaryCapture capture;
        capture.scheduler = &scheduler;
        scheduler.setPattern(oneTrack(track));
        scheduler.setTransport(transport());
        scheduler.setLogicalTickObserver(&captureTickBoundary, &capture);
        scheduler.start();
        std::array<ScheduledEvent, 16u> events {};
        for (const auto frames : blocks)
            (void)scheduler.process(frames, events.data(), events.size());
        return capture;
    };

    const auto whole = render({ 24001u });
    const auto divided = render({ 1u, 17u, 7982u, 1u, 4096u,
        3904u, 127u, 7872u, 1u });
    check(whole.count == 4u && divided.count == whole.count,
        "the observer should report exactly one boundary per due logical tick");
    for (std::size_t index = 0u;
         index < std::min(whole.count, divided.count); ++index) {
        check(whole.boundaries[index].completedTickIndex
                    == divided.boundaries[index].completedTickIndex
                && whole.boundaries[index].completedTransportRow
                    == divided.boundaries[index].completedTransportRow
                && whole.boundaries[index].absoluteSampleTime
                    == divided.boundaries[index].absoluteSampleTime,
            "logical tick boundaries must be invariant to process partitioning");
    }

    TimingPlaybackScheduler noTick;
    TickBoundaryCapture noTickCapture;
    noTickCapture.scheduler = &noTick;
    noTick.setPattern(oneTrack(track));
    noTick.setTransport(transport());
    noTick.setLogicalTickObserver(&captureTickBoundary, &noTickCapture);
    noTick.start();
    std::array<ScheduledEvent, 4u> events {};
    (void)noTick.process(1u, events.data(), events.size());
    const auto countAfterFirstTick = noTickCapture.count;
    (void)noTick.process(7999u, events.data(), events.size());
    check(countAfterFirstTick == 1u
            && noTickCapture.count == countAfterFirstTick,
        "a block containing no due tick must not invoke the observer");
}

void testLogicalTickObserverSeesCoincidentTicksInOrder()
{
    Track track;
    track.notes.assign(4u, NoteCell::rest());
    track.noteColumn.length = track.notes.size();
    auto& pair = track.fxPairs[0u];
    pair.actions.assign(4u, FxActionCell::parameter(3u));
    pair.values.assign(4u, FxValueCell::withValue(0.5f));
    pair.actionColumn.length = pair.actions.size();
    pair.valueColumn.length = pair.values.size();
    TimingPlaybackScheduler scheduler;
    TickBoundaryCapture capture;
    capture.scheduler = &scheduler;
    scheduler.setPattern(oneTrack(std::move(track)));
    auto warped = transport();
    warped.warpCycleTicks = 4u;
    warped.timingWarp.append(TimingWarpTransform::stepQuantize(1u));
    scheduler.setTransport(warped);
    scheduler.setLogicalTickObserver(&captureTickBoundary, &capture);
    scheduler.start();
    std::array<ScheduledEvent, 16u> events {};
    const auto eventCount = scheduler.process(
        1u, events.data(), events.size());

    check(capture.count == 4u && eventCount == 4u,
        "all coincident warped ticks should cross the observer boundary independently");
    for (std::size_t index = 0u; index < capture.count; ++index) {
        check(capture.boundaries[index].completedTickIndex == index
                && capture.boundaries[index].completedTransportRow == index
                && capture.boundaries[index].absoluteSampleTime == 0u,
            "coincident callbacks must retain strict logical tick order");
        check(capture.pendingCounts[index] == index + 1u,
            "each callback must run after its complete tick bundle is enqueued and before the next tick");
    }
}

struct StopBoundaryCapture {
    std::size_t count = 0u;
};

LogicalTickBoundaryAction stopAtFirstBoundary(void* opaque,
    const LogicalTickBoundary&) noexcept
{
    auto& capture = *static_cast<StopBoundaryCapture*>(opaque);
    ++capture.count;
    return LogicalTickBoundaryAction::StopAfterBoundary;
}

void testLogicalTickObserverCanStopAfterAdmittedTick()
{
    Track track;
    track.notes.assign(4u, NoteCell::withNote(60u));
    track.noteColumn.length = track.notes.size();
    TimingPlaybackScheduler scheduler;
    StopBoundaryCapture capture;
    scheduler.setPattern(oneTrack(std::move(track)));
    scheduler.setTransport(transport());
    scheduler.setLogicalTickObserver(&stopAtFirstBoundary, &capture);
    scheduler.start();
    std::array<ScheduledEvent, 16u> events {};
    const auto count = scheduler.process(24001u,
        events.data(), events.size());
    check(capture.count == 1u && scheduler.tickIndex() == 1u
            && !scheduler.isPlaying(),
        "StopAfterBoundary must prevent a second logical tick in the same process call");
    check(count == 1u && events[0u].kind == ScheduledEventKind::NoteOn
            && events[0u].absoluteSampleTime == 0u,
        "StopAfterBoundary must preserve and drain the final admitted tick bundle");
}

void testStoppedBoundaryDrainsFutureTimingTail()
{
    TimingPlaybackScheduler scheduler;
    StopBoundaryCapture capture;
    scheduler.setPattern(oneTrack(timingTrack(
        SequencerAction::Stutter, 0.0f)));
    scheduler.setTransport(transport());
    scheduler.setLogicalTickObserver(&stopAtFirstBoundary, &capture);
    scheduler.start();
    std::array<ScheduledEvent, 8u> events {};

    auto count = scheduler.process(1u, events.data(), events.size());
    check(count == 1u && events[0u].absoluteSampleTime == 0u
            && scheduler.pendingEventCount() == 1u
            && !scheduler.isPlaying(),
        "StopAfterBoundary should retain a final tick's future stutter tail");
    const auto tick = scheduler.tickIndex();
    const auto row = scheduler.transportRow();
    count = scheduler.process(7999u, events.data(), events.size());
    check(count == 0u && scheduler.pendingEventCount() == 1u,
        "a stopped scheduler should retain a tail at the exclusive block boundary");
    count = scheduler.process(1u, events.data(), events.size());
    check(count == 1u && events[0u].absoluteSampleTime == 8000u
            && scheduler.pendingEventCount() == 0u,
        "a stopped scheduler should drain a final-tick tail in a later block");
    check(scheduler.tickIndex() == tick && scheduler.transportRow() == row,
        "tail-only clock advance must not generate ticks or move Song position");
}

struct BoundaryRetimeCapture {
    TimingPlaybackScheduler* scheduler = nullptr;
    std::array<uint64_t, 4u> sampleTimes {};
    std::size_t count = 0u;
};

LogicalTickBoundaryAction retimeAfterFirstBoundary(void* opaque,
    const LogicalTickBoundary& boundary) noexcept
{
    auto& capture = *static_cast<BoundaryRetimeCapture*>(opaque);
    if (capture.count < capture.sampleTimes.size())
        capture.sampleTimes[capture.count] = boundary.absoluteSampleTime;
    ++capture.count;
    if (capture.count == 1u) {
        auto faster = capture.scheduler->transport();
        faster.bpm = 120.0;
        capture.scheduler->setTransportAtTickBoundary(faster);
    }
    return LogicalTickBoundaryAction::Continue;
}

void testBoundaryTransportChangeRetimesNextTick()
{
    Track track;
    track.notes.assign(4u, NoteCell::rest());
    track.noteColumn.length = track.notes.size();
    TimingPlaybackScheduler scheduler;
    BoundaryRetimeCapture capture;
    capture.scheduler = &scheduler;
    scheduler.setPattern(oneTrack(std::move(track)));
    scheduler.setTransport(transport());
    scheduler.setLogicalTickObserver(&retimeAfterFirstBoundary, &capture);
    scheduler.start();
    std::array<ScheduledEvent, 4u> events {};
    (void)scheduler.process(4001u, events.data(), events.size());
    check(capture.count == 2u && capture.sampleTimes[0u] == 0u
            && capture.sampleTimes[1u] == 4000u,
        "a boundary BPM override must govern the interval into the next logical tick");
}

void testUntimedPassThroughParity()
{
    Track track;
    track.notes = {
        NoteCell::withNote(60u), NoteCell::retriggerPrevious(),
        NoteCell::kill(), NoteCell::withNote(64u),
    };
    track.velocities = {
        ValueCell::withValue(0.25f), ValueCell::withValue(0.75f),
    };
    track.noteColumn.length = track.notes.size();
    track.velocityColumn.length = track.velocities.size();

    Sequencer nominal;
    TimingPlaybackScheduler timed;
    nominal.setPattern(oneTrack(track));
    timed.setPattern(oneTrack(std::move(track)));
    nominal.setTransport(transport());
    timed.setTransport(transport());
    nominal.start();
    timed.start();
    std::array<ScheduledEvent, 16u> left {};
    std::array<ScheduledEvent, 16u> right {};
    const auto leftCount = nominal.process(24001u, left.data(), left.size());
    const auto rightCount = timed.process(24001u, right.data(), right.size());
    check(leftCount == rightCount,
        "an untimed pattern should preserve the nominal event count");
    for (std::size_t index = 0u; index < std::min(leftCount, rightCount);
         ++index) {
        check(left[index].absoluteSampleTime
                    == right[index].absoluteSampleTime
                && left[index].frameOffset == right[index].frameOffset
                && left[index].noteId == right[index].noteId
                && left[index].kind == right[index].kind
                && left[index].note == right[index].note
                && left[index].normalizedVelocity
                    == right[index].normalizedVelocity,
            "untimed scheduling should be byte-semantics compatible");
    }
}

void testRatchetSurvivesBlockBoundaries()
{
    TimingPlaybackScheduler scheduler;
    scheduler.setPattern(oneTrack(timingTrack(
        SequencerAction::Ratchet, 1.0f / 3.0f)));
    scheduler.setTransport(transport());
    scheduler.start();
    std::array<ScheduledEvent, 8u> events {};
    std::array<uint64_t, 4u> frames {};
    std::size_t total = 0u;
    for (std::size_t block = 0u; block < 4u; ++block) {
        const auto count = scheduler.process(2000u, events.data(),
            events.size());
        check(count == 1u,
            "a four-way ratchet should emit once in each quarter-tick block");
        if (count != 0u) frames[total++] = events[0u].absoluteSampleTime;
    }
    check(total == 4u && frames[0u] == 0u && frames[1u] == 2000u
            && frames[2u] == 4000u && frames[3u] == 6000u,
        "ratchets should retain exact absolute timing across process calls");
    check(scheduler.pendingEventCount() == 0u
            && scheduler.pendingEventHighWaterMark() == 4u,
        "the bounded timeline should expose ratchet retention telemetry");
}

void testStutterAndMicroTiming()
{
    TimingPlaybackScheduler stutter;
    stutter.setPattern(oneTrack(timingTrack(
        SequencerAction::Stutter, 0.0f)));
    stutter.setTransport(transport());
    stutter.start();
    std::array<ScheduledEvent, 8u> events {};
    auto count = stutter.process(8000u, events.data(), events.size());
    check(count == 1u && events[0u].absoluteSampleTime == 0u
            && stutter.pendingEventCount() == 1u,
        "ST should retain its future-tick onset after the first block");
    count = stutter.process(8000u, events.data(), events.size());
    check(count == 1u && events[0u].absoluteSampleTime == 8000u,
        "a retained stutter should become due at the next tick boundary");

    TimingPlaybackScheduler micro;
    micro.setPattern(oneTrack(timingTrack(
        SequencerAction::MicroTime, 1.0f)));
    auto settings = transport();
    settings.timingLookaheadMilliseconds = 25.0;
    settings.microTimingRangeMilliseconds = 25.0;
    micro.setTransport(settings);
    micro.start();
    check(micro.process(400u, events.data(), events.size()) == 0u,
        "the exclusive block end should retain an MT onset at frame 400");
    count = micro.process(1u, events.data(), events.size());
    check(count == 1u && events[0u].absoluteSampleTime == 400u
            && events[0u].frameOffset == 0u,
        "MT should apply lookahead plus its signed range in sample time");
}

void testMicroTimingMovesTheCanonicalRowBundle()
{
    auto track = timingTrack(SequencerAction::MicroTime, 0.5f);
    track.notes = {
        NoteCell::withNote(60u), NoteCell::retriggerPrevious(),
    };
    track.noteColumn.length = track.notes.size();
    auto& micro = track.fxPairs[0u];
    micro.actions = {
        FxActionCell::sequencer(SequencerAction::MicroTime),
        FxActionCell::previous(),
    };
    micro.values = {
        FxValueCell::withValue(0.5f), FxValueCell::previous(),
    };
    micro.actionColumn.length = 2u;
    micro.valueColumn.length = 2u;
    auto& parameter = track.fxPairs[1u];
    parameter.actions = {
        FxActionCell::parameter(3u), FxActionCell::previous(),
    };
    parameter.values = {
        FxValueCell::withValue(0.4f), FxValueCell::previous(),
    };
    parameter.actionColumn.length = 2u;
    parameter.valueColumn.length = 2u;

    TimingPlaybackScheduler scheduler;
    scheduler.setPattern(oneTrack(std::move(track)));
    scheduler.setTransport(transport());
    scheduler.start();
    std::array<ScheduledEvent, 16u> events {};
    const auto count = scheduler.process(8201u, events.data(), events.size());
    check(count == 5u
            && events[0u].absoluteSampleTime == 200u
            && events[0u].kind == ScheduledEventKind::Parameter
            && events[1u].absoluteSampleTime == 200u
            && events[1u].kind == ScheduledEventKind::NoteOn
            && events[2u].absoluteSampleTime == 8200u
            && events[2u].kind == ScheduledEventKind::NoteOff
            && events[3u].absoluteSampleTime == 8200u
            && events[3u].kind == ScheduledEventKind::Parameter
            && events[4u].absoluteSampleTime == 8200u
            && events[4u].kind == ScheduledEventKind::NoteOn,
        "MT compensation must move release, parameter, and onset as one ordered row bundle");
}

void testAccentAndStaleParameterSuppression()
{
    auto track = timingTrack(SequencerAction::Accent, 1.0f);
    TimingPlaybackScheduler accent;
    accent.setPattern(oneTrack(track));
    accent.setTransport(transport());
    accent.start();
    std::array<ScheduledEvent, 16u> events {};
    auto count = accent.process(1u, events.data(), events.size());
    check(count == 1u
            && std::abs(events[0u].normalizedVelocity - 1.0f) < 0.00001f,
        "AC should scale normalized VOL and clamp before both destinations");

    track.notes = {
        NoteCell::withNote(60u), NoteCell::rest(),
        NoteCell::withNote(62u), NoteCell::rest(),
    };
    auto& pair = track.fxPairs[0u];
    pair.actions = {
        FxActionCell::parameter(3u),
        FxActionCell::sequencer(SequencerAction::Ratchet),
        FxActionCell::previous(), FxActionCell::empty(),
    };
    pair.values.assign(4u, FxValueCell::withValue(0.0f));
    TimingPlaybackScheduler memory;
    memory.setPattern(oneTrack(std::move(track)));
    memory.setTransport(transport());
    memory.start();
    count = memory.process(16001u, events.data(), events.size());
    std::size_t parametersAtThirdTick = 0u;
    std::size_t notesAtThirdTick = 0u;
    for (std::size_t index = 0u; index < count; ++index) {
        if (events[index].absoluteSampleTime != 16000u) continue;
        if (events[index].kind == ScheduledEventKind::Parameter)
            ++parametersAtThirdTick;
        if (events[index].kind == ScheduledEventKind::NoteOn)
            ++notesAtThirdTick;
    }
    check(parametersAtThirdTick == 0u && notesAtThirdTick == 1u,
        "Previous after a sequencing action must not leak stale parameter memory");
}

void testWarpCollisionAndCrossTickOrdering()
{
    auto track = timingTrack(SequencerAction::Ratchet, 0.0f);
    track.notes.assign(4u, NoteCell::withNote(60u));
    track.fxPairs[0u].actions = {
        FxActionCell::sequencer(SequencerAction::Ratchet),
        FxActionCell::previous(), FxActionCell::previous(),
        FxActionCell::previous(),
    };
    TimingPlaybackScheduler collision;
    collision.setPattern(oneTrack(std::move(track)));
    auto warped = transport();
    warped.warpCycleTicks = 4u;
    warped.timingWarp.append(TimingWarpTransform::stepQuantize(1u));
    collision.setTransport(warped);
    collision.start();
    std::array<ScheduledEvent, 32u> events {};
    const auto collisionCount = collision.process(1u, events.data(),
        events.size());
    check(collision.tickIndex() == 4u && collisionCount == 4u
            && collision.isPlaying(),
        "timed scheduling should preserve coincident functional-warp ticks");

    track = timingTrack(SequencerAction::Delay, 1.0f);
    track.notes = {
        NoteCell::withNote(60u), NoteCell::retriggerPrevious(),
    };
    track.noteColumn.length = track.notes.size();
    track.fxPairs[0u].actions = {
        FxActionCell::sequencer(SequencerAction::Delay),
        FxActionCell::empty(),
    };
    track.fxPairs[0u].values = {
        FxValueCell::withValue(1.0f), FxValueCell::previous(),
    };
    track.fxPairs[0u].actionColumn.length = 2u;
    track.fxPairs[0u].valueColumn.length = 2u;
    TimingPlaybackScheduler ordered;
    ordered.setPattern(oneTrack(std::move(track)));
    ordered.setTransport(transport());
    ordered.start();
    const auto orderedCount = ordered.process(8001u, events.data(),
        events.size());
    check(orderedCount == 1u
            && events[0u].absoluteSampleTime == 8000u
            && events[0u].kind == ScheduledEventKind::NoteOn,
        "retrigger must cancel a not-yet-played delayed primary and emit only its replacement");
}

Pattern delayedKillPattern(bool addLateMicroTime)
{
    auto track = timingTrack(SequencerAction::Delay, 1.0f);
    track.notes = { NoteCell::withNote(60u), NoteCell::kill() };
    track.noteColumn.length = 2u;
    auto& delay = track.fxPairs[0u];
    delay.actions = {
        FxActionCell::sequencer(SequencerAction::Delay),
        FxActionCell::empty(),
    };
    delay.values = {
        FxValueCell::withValue(1.0f), FxValueCell::previous(),
    };
    delay.actionColumn.length = 2u;
    delay.valueColumn.length = 2u;
    if (addLateMicroTime) {
        auto& micro = track.fxPairs[1u];
        micro.actions = {
            FxActionCell::sequencer(SequencerAction::MicroTime),
            FxActionCell::empty(),
        };
        micro.values = {
            FxValueCell::withValue(1.0f), FxValueCell::previous(),
        };
        micro.actionColumn.length = 2u;
        micro.valueColumn.length = 2u;
    }
    return oneTrack(std::move(track));
}

void testDelayedKillCancelsPendingPrimary()
{
    std::array<ScheduledEvent, 8u> events {};
    TimingPlaybackScheduler whole;
    whole.setPattern(delayedKillPattern(false));
    whole.setTransport(transport());
    whole.start();
    check(whole.process(8001u, events.data(), events.size()) == 0u
            && whole.pendingEventCount() == 0u,
        "DL1 followed by Kill must cancel an equal-time pending onset and release");

    TimingPlaybackScheduler split;
    split.setPattern(delayedKillPattern(false));
    split.setTransport(transport());
    split.start();
    check(split.process(8000u, events.data(), events.size()) == 0u
            && split.pendingEventCount() == 1u
            && split.process(1u, events.data(), events.size()) == 0u
            && split.pendingEventCount() == 0u,
        "pending-onset cancellation must be invariant at a callback boundary");

    TimingPlaybackScheduler later;
    later.setPattern(delayedKillPattern(true));
    later.setTransport(transport());
    later.start();
    check(later.process(8401u, events.data(), events.size()) == 0u
            && later.pendingEventCount() == 0u,
        "Kill must cancel a delayed onset strictly later than its shifted release");
}

void testLiveTimingActivationUsesSequencerMemory()
{
    Track initial;
    initial.notes = { NoteCell::withNote(60u) };
    initial.noteColumn.length = 1u;
    initial.fxPairs[0u].actions = { FxActionCell::empty() };
    initial.fxPairs[0u].values = { FxValueCell::withValue(1.0f) };
    initial.fxPairs[0u].actionColumn.length = 1u;
    initial.fxPairs[0u].valueColumn.length = 1u;
    initial.fxPairs[1u].actions = { FxActionCell::parameter(3u) };
    initial.fxPairs[1u].values = { FxValueCell::withValue(0.4f) };
    initial.fxPairs[1u].actionColumn.length = 1u;
    initial.fxPairs[1u].valueColumn.length = 1u;

    TimingPlaybackScheduler rememberedValue;
    rememberedValue.setPattern(oneTrack(initial));
    rememberedValue.setTransport(transport());
    rememberedValue.start();
    std::array<ScheduledEvent, 32u> events {};
    check(rememberedValue.process(1u, events.data(), events.size()) == 2u,
        "untimed fast path should establish FX action/value memory");

    Track replacement;
    replacement.notes = { NoteCell::retriggerPrevious() };
    replacement.noteColumn.length = 1u;
    replacement.fxPairs[0u].actions = {
        FxActionCell::sequencer(SequencerAction::Ratchet),
    };
    replacement.fxPairs[0u].values = { FxValueCell::previous() };
    replacement.fxPairs[0u].actionColumn.length = 1u;
    replacement.fxPairs[0u].valueColumn.length = 1u;
    replacement.fxPairs[1u].actions = { FxActionCell::previous() };
    replacement.fxPairs[1u].values = { FxValueCell::previous() };
    replacement.fxPairs[1u].actionColumn.length = 1u;
    replacement.fxPairs[1u].valueColumn.length = 1u;
    rememberedValue.replacePattern(oneTrack(std::move(replacement)));
    const auto count = rememberedValue.process(
        15000u, events.data(), events.size());
    std::size_t onsets = 0u;
    std::size_t parameters = 0u;
    for (std::size_t index = 0u; index < count; ++index) {
        if (events[index].kind == ScheduledEventKind::NoteOn) ++onsets;
        if (events[index].kind == ScheduledEventKind::Parameter) ++parameters;
    }
    check(onsets == 8u && parameters == 1u,
        "live timing activation must recall value and unrelated parameter memory from Sequencer");

    auto rememberedActionTrack = timingTrack(
        SequencerAction::Ratchet, 1.0f);
    rememberedActionTrack.fxPairs[0u].values = {
        FxValueCell::previous(),
    };
    rememberedActionTrack.fxPairs[0u].valueColumn.length = 1u;
    TimingPlaybackScheduler rememberedAction;
    rememberedAction.setPattern(oneTrack(std::move(rememberedActionTrack)));
    rememberedAction.setTransport(transport());
    rememberedAction.start();
    check(rememberedAction.process(1u, events.data(), events.size()) == 1u,
        "an action without value should remain on the untimed fast path");
    Track actionReplacement;
    actionReplacement.notes = { NoteCell::retriggerPrevious() };
    actionReplacement.noteColumn.length = 1u;
    actionReplacement.fxPairs[0u].actions = { FxActionCell::previous() };
    actionReplacement.fxPairs[0u].values = {
        FxValueCell::withValue(1.0f),
    };
    actionReplacement.fxPairs[0u].actionColumn.length = 1u;
    actionReplacement.fxPairs[0u].valueColumn.length = 1u;
    rememberedAction.replacePattern(oneTrack(std::move(actionReplacement)));
    const auto recalledCount = rememberedAction.process(
        15000u, events.data(), events.size());
    onsets = 0u;
    for (std::size_t index = 0u; index < recalledCount; ++index) {
        if (events[index].kind == ScheduledEventKind::NoteOn) ++onsets;
    }
    check(onsets == 8u,
        "live timing activation must recall a sequencer action from the authoritative memory");
}

void testTimingEffectsUseNominalTickUnderSwingAndWarp()
{
    auto track = timingTrack(SequencerAction::Ratchet, 0.0f);
    TimingPlaybackScheduler scheduler;
    scheduler.setPattern(oneTrack(std::move(track)));
    auto settings = transport();
    settings.swing = 0.75;
    settings.warpCycleTicks = 4u;
    settings.timingWarp.append(TimingWarpTransform::exponential(2.0));
    scheduler.setTransport(settings);
    scheduler.start();
    std::array<ScheduledEvent, 8u> events {};
    const auto count = scheduler.process(5000u, events.data(), events.size());
    check(count == 2u && events[0u].absoluteSampleTime == 0u
            && events[1u].absoluteSampleTime == 4000u,
        "RR spacing must use the straight nominal tick while swing/warp place primary rows");
}

void testLiveReplacementMemoryAndPendingTail()
{
    TimingPlaybackScheduler retained;
    retained.setPattern(oneTrack(timingTrack(
        SequencerAction::Accent, 1.0f)));
    retained.setTransport(transport());
    retained.start();
    std::array<ScheduledEvent, 16u> events {};
    check(retained.process(1u, events.data(), events.size()) == 1u,
        "the retained-memory fixture should emit its first onset");
    auto previous = timingTrack(SequencerAction::Accent, 1.0f);
    previous.notes = { NoteCell::retriggerPrevious() };
    previous.noteColumn.length = 1u;
    previous.fxPairs[0u].actions = { FxActionCell::previous() };
    previous.fxPairs[0u].values = { FxValueCell::previous() };
    previous.fxPairs[0u].actionColumn.length = 1u;
    previous.fxPairs[0u].valueColumn.length = 1u;
    retained.replacePattern(oneTrack(std::move(previous)));
    const auto retainedCount = retained.process(8000u, events.data(),
        events.size());
    bool accented = false;
    for (std::size_t index = 0u; index < retainedCount; ++index) {
        if (events[index].kind == ScheduledEventKind::NoteOn
            && std::abs(events[index].normalizedVelocity - 1.0f)
                < 0.00001f) accented = true;
    }
    check(accented,
        "live Previous should recall a retained sequencing action after replacement");

    TimingPlaybackScheduler tail;
    tail.setPattern(oneTrack(timingTrack(
        SequencerAction::Stutter, 0.0f)));
    tail.setTransport(transport());
    tail.start();
    check(tail.process(1u, events.data(), events.size()) == 1u
            && tail.pendingEventCount() == 1u,
        "the replacement-tail fixture should retain one future stutter");
    auto noTiming = timingTrack(SequencerAction::Stutter, 0.0f);
    noTiming.fxPairs[0u].actions.assign(4u, FxActionCell::empty());
    tail.replacePattern(oneTrack(std::move(noTiming)));
    check(tail.process(7999u, events.data(), events.size()) == 0u,
        "a preserved tail should remain exclusive at its boundary");
    const auto tailCount = tail.process(1u, events.data(), events.size());
    check(tailCount == 1u && events[0u].absoluteSampleTime == 8000u
            && tail.pendingEventCount() == 0u,
        "removing the last timing cell must not strand an authored future hit");
}

void testPreparedBoundaryActivationPreservesPendingTimeline()
{
    auto source = oneTrack(timingTrack(SequencerAction::Stutter, 0.0f));
    Track targetTrack;
    targetTrack.notes = { NoteCell::withNote(72u) };
    targetTrack.velocities = { ValueCell::withValue(0.6f) };
    targetTrack.noteColumn.length = 1u;
    targetTrack.velocityColumn.length = 1u;
    auto target = oneTrack(std::move(targetTrack));

    TimingPlaybackScheduler scheduler;
    check(scheduler.preparePatternSet(
              { std::move(source), std::move(target) }, 0u),
        "the timing fixture should prepare both Song patterns while stopped");
    scheduler.setTransport(transport());
    scheduler.start();
    std::array<ScheduledEvent, 16u> events {};
    auto count = scheduler.process(1u, events.data(), events.size());
    check(count == 1u && events[0u].note == 60u
            && scheduler.pendingEventCount() == 1u,
        "the source pattern must leave one future stutter in the shared timeline");
    const auto tick = scheduler.tickIndex();
    const auto row = scheduler.transportRow();
    const auto rendered = scheduler.renderedFrameCount();
    const auto nextTick = scheduler.nextTickSampleFrame();
    check(scheduler.activatePreparedPatternAtTickBoundary(1u),
        "the prepared target timing state should activate at the boundary");
    scheduler.relaunchColumnsAtTickBoundary(0u);
    check(scheduler.tickIndex() == tick
            && scheduler.transportRow() == row
            && scheduler.renderedFrameCount() == rendered
            && scheduler.nextTickSampleFrame() == nextTick
            && scheduler.pendingEventCount() == 1u,
        "prepared activation must preserve the single master clock and timing timeline");

    count = scheduler.process(7999u, events.data(), events.size());
    check(count == 0u && scheduler.pendingEventCount() == 1u,
        "the source tail must remain exclusive at its original sample boundary");
    count = scheduler.process(1u, events.data(), events.size());
    std::size_t sourceTails = 0u;
    std::size_t targetOnsets = 0u;
    for (std::size_t index = 0u; index < count; ++index) {
        if (events[index].kind != ScheduledEventKind::NoteOn
            || events[index].absoluteSampleTime != 8000u) continue;
        if (events[index].note == 60u) ++sourceTails;
        if (events[index].note == 72u) ++targetOnsets;
    }
    check(sourceTails == 1u && targetOnsets == 1u
            && scheduler.pendingEventCount() == 0u,
        "the preserved source tail and unexpanded target onset must share the exact boundary in order");
}

void testPreparedContractionReleaseHasNoStaleTimingDelay()
{
    Pattern source;
    Track shared;
    shared.notes = { NoteCell::rest() };
    shared.noteColumn.length = 1u;
    source.tracks.push_back(std::move(shared));
    auto removed = timingTrack(SequencerAction::Delay, 0.5f);
    removed.destination = EventDestination::Internal;
    removed.initialInstrumentNodeId = 4u;
    removed.notes.assign(1u, NoteCell::withNote(80u));
    removed.noteColumn.length = 1u;
    source.tracks.push_back(std::move(removed));

    Pattern target;
    Track targetTrack;
    targetTrack.notes = { NoteCell::rest() };
    targetTrack.noteColumn.length = 1u;
    target.tracks.push_back(std::move(targetTrack));

    TimingPlaybackScheduler scheduler;
    check(scheduler.preparePatternSet(
              { std::move(source), std::move(target) }, 0u),
        "the contraction fixture should prepare both patterns");
    scheduler.setTransport(transport());
    scheduler.start();
    std::array<ScheduledEvent, 8u> events {};
    auto count = scheduler.process(1u, events.data(), events.size());
    check(count == 0u && scheduler.pendingEventCount() == 1u,
        "the removed lane's delayed source onset should remain pending");
    check(scheduler.activatePreparedPatternAtTickBoundary(1u),
        "the narrower target should activate at the boundary");
    scheduler.relaunchColumnsAtTickBoundary(0u);
    count = scheduler.process(7999u, events.data(), events.size());
    check(count == 1u && events[0u].kind == ScheduledEventKind::NoteOn
            && events[0u].note == 80u
            && events[0u].absoluteSampleTime == 4000u,
        "the already-admitted source onset must retain its authored delay");
    count = scheduler.process(1u, events.data(), events.size());
    check(count == 1u && events[0u].kind == ScheduledEventKind::NoteOff
            && events[0u].track == 1u && events[0u].note == 80u
            && events[0u].targetNode == 4u
            && events[0u].absoluteSampleTime == 8000u,
        "a removed-lane release must land on the target boundary without stale source timing");
}

std::vector<ScheduledEvent> renderTimingBlocks(Pattern pattern,
    const std::vector<uint32_t>& blocks)
{
    TimingPlaybackScheduler scheduler;
    scheduler.setPattern(std::move(pattern));
    scheduler.setTransport(transport());
    scheduler.start();
    std::vector<ScheduledEvent> rendered;
    std::array<ScheduledEvent, 64u> events {};
    for (const auto frames : blocks) {
        const auto count = scheduler.process(frames, events.data(),
            events.size());
        rendered.insert(rendered.end(), events.begin(),
            events.begin() + static_cast<std::ptrdiff_t>(count));
    }
    check(scheduler.droppedEventCount() == 0u,
        "block-invariance fixtures should remain below every fixed budget");
    return rendered;
}

void testBlockSizeInvarianceAndOverflowTelemetry()
{
    auto track = timingTrack(SequencerAction::Ratchet, 1.0f);
    track.notes.assign(16u, NoteCell::rest());
    track.notes[0u] = NoteCell::withNote(60u);
    track.noteColumn.length = track.notes.size();
    track.fxPairs[0u].actions.assign(16u, FxActionCell::empty());
    track.fxPairs[0u].actions[0u] = FxActionCell::sequencer(
        SequencerAction::Ratchet);
    track.fxPairs[0u].values.assign(16u, FxValueCell::previous());
    track.fxPairs[0u].values[0u] = FxValueCell::withValue(1.0f);
    track.fxPairs[0u].actionColumn.length = 16u;
    track.fxPairs[0u].valueColumn.length = 16u;
    auto& stutter = track.fxPairs[1u];
    stutter.actions.assign(16u, FxActionCell::empty());
    stutter.actions[0u] = FxActionCell::sequencer(
        SequencerAction::Stutter);
    stutter.values.assign(16u, FxValueCell::previous());
    stutter.values[0u] = FxValueCell::withValue(1.0f);
    stutter.actionColumn.length = 16u;
    stutter.valueColumn.length = 16u;
    const auto pattern = oneTrack(std::move(track));
    const auto whole = renderTimingBlocks(pattern, { 64001u });
    std::vector<uint32_t> partitions;
    uint32_t remaining = 64001u;
    const std::array<uint32_t, 7u> sizes {
        1u, 127u, 509u, 2000u, 4096u, 7777u, 16384u,
    };
    std::size_t next = 0u;
    while (remaining != 0u) {
        const auto block = std::min(remaining, sizes[next % sizes.size()]);
        partitions.push_back(block);
        remaining -= block;
        ++next;
    }
    const auto divided = renderTimingBlocks(pattern, partitions);
    check(whole.size() == 15u && divided.size() == whole.size(),
        "RR8 plus seven future ST onsets should produce fifteen events");
    for (std::size_t index = 0u;
         index < std::min(whole.size(), divided.size()); ++index) {
        check(whole[index].absoluteSampleTime
                    == divided[index].absoluteSampleTime
                && whole[index].kind == divided[index].kind
                && whole[index].note == divided[index].note
                && whole[index].normalizedVelocity
                    == divided[index].normalizedVelocity,
            "timing output must be invariant to process block partitioning");
    }

    Pattern dense;
    for (std::size_t lane = 0u; lane < 32u; ++lane) {
        auto laneTrack = timingTrack(SequencerAction::Ratchet, 1.0f);
        laneTrack.notes.assign(32u, NoteCell::withNote(
            static_cast<uint8_t>(36u + lane)));
        laneTrack.noteColumn.length = 32u;
        laneTrack.fxPairs[0u].actions.assign(32u,
            FxActionCell::previous());
        laneTrack.fxPairs[0u].actions[0u] = FxActionCell::sequencer(
            SequencerAction::Ratchet);
        laneTrack.fxPairs[0u].values.assign(32u,
            FxValueCell::withValue(1.0f));
        laneTrack.fxPairs[0u].actionColumn.length = 32u;
        laneTrack.fxPairs[0u].valueColumn.length = 32u;
        laneTrack.fxPairs[1u].actions.assign(32u,
            FxActionCell::previous());
        laneTrack.fxPairs[1u].actions[0u] = FxActionCell::sequencer(
            SequencerAction::Stutter);
        laneTrack.fxPairs[1u].values.assign(32u,
            FxValueCell::withValue(1.0f));
        laneTrack.fxPairs[1u].actionColumn.length = 32u;
        laneTrack.fxPairs[1u].valueColumn.length = 32u;
        dense.tracks.push_back(std::move(laneTrack));
    }
    TimingPlaybackScheduler overflow;
    overflow.setPattern(std::move(dense));
    auto warped = transport();
    warped.warpCycleTicks = 32u;
    warped.timingWarp.append(TimingWarpTransform::stepQuantize(1u));
    overflow.setTransport(warped);
    overflow.start();
    std::array<ScheduledEvent, 2048u> due {};
    (void)overflow.process(1u, due.data(), due.size());
    check(overflow.droppedEventCount() != 0u,
        "an impossible timing collision must report bounded timeline overflow");
}

void testInactiveStoredTimingCellsDoNotAddLatency()
{
    auto noValue = timingTrack(SequencerAction::MicroTime, 1.0f);
    noValue.fxPairs[0u].values.assign(4u, FxValueCell::previous());
    TimingPlaybackScheduler scheduler;
    scheduler.setPattern(oneTrack(noValue));
    scheduler.setTransport(transport());
    scheduler.start();
    std::array<ScheduledEvent, 4u> events {};
    auto count = scheduler.process(1u, events.data(), events.size());
    check(count == 1u && events[0u].absoluteSampleTime == 0u,
        "MT without reachable value memory must not activate compensation");

    auto muted = timingTrack(SequencerAction::MicroTime, 1.0f);
    muted.fxPairs[0u].actionColumn.muted = true;
    scheduler.setPattern(oneTrack(std::move(muted)));
    scheduler.setTransport(transport());
    scheduler.start();
    count = scheduler.process(1u, events.data(), events.size());
    check(count == 1u && events[0u].absoluteSampleTime == 0u,
        "a muted MT action column must not activate compensation");

    auto outsideLength = timingTrack(SequencerAction::MicroTime, 1.0f);
    outsideLength.fxPairs[0u].actions = {
        FxActionCell::empty(),
        FxActionCell::sequencer(SequencerAction::MicroTime),
    };
    outsideLength.fxPairs[0u].actionColumn.length = 1u;
    scheduler.setPattern(oneTrack(std::move(outsideLength)));
    scheduler.setTransport(transport());
    scheduler.start();
    count = scheduler.process(1u, events.data(), events.size());
    check(count == 1u && events[0u].absoluteSampleTime == 0u,
        "stored timing cells beyond active column length must remain inert");

    auto probability = timingTrack(SequencerAction::Probability, 1.0f);
    scheduler.setPattern(oneTrack(std::move(probability)));
    scheduler.setTransport(transport());
    scheduler.start();
    count = scheduler.process(1u, events.data(), events.size());
    check(count == 1u && scheduler.pendingEventHighWaterMark() == 0u,
        "authoritative note transforms must not activate the timing expansion timeline");
}

void testPreparedHostBeatStartUsesWarpedClock()
{
    Track track;
    track.notes = {
        NoteCell::withNote(60u), NoteCell::withNote(61u),
        NoteCell::withNote(62u), NoteCell::withNote(63u),
    };
    track.velocities.assign(4u, ValueCell::withValue(1.0f));
    track.noteColumn.length = 4u;
    track.velocityColumn.length = 4u;
    auto pattern = oneTrack(std::move(track));

    TimingPlaybackScheduler scheduler;
    check(scheduler.preparePatternSet({ pattern }),
        "host-sync start requires a prepared runtime pattern set");
    auto clock = transport();
    clock.warpCycleTicks = 4u;
    clock.timingWarp.append(TimingWarpTransform::exponential(2.0));
    scheduler.setTransport(clock);
    check(scheduler.startPreparedAtHostBeat(1.5),
        "prepared scheduler should arm at a host quarter-note position");
    check(scheduler.tickIndex() == 3u && scheduler.transportRow() == 3u,
        "host seek should select the first warped tick at or after the host position");

    std::array<ScheduledEvent, 4u> events {};
    auto count = scheduler.process(6000u, events.data(), events.size());
    check(count == 0u,
        "warped host start should preserve the remaining fractional interval");
    count = scheduler.process(1u, events.data(), events.size());
    check(count == 1u && events[0u].frameOffset == 0u
            && events[0u].note == 63u,
        "the first host-synchronized event should read the sought polymetric row");
}

void testHeldNoteOffTraversesTimingTimeline()
{
    Track track;
    track.notes = { NoteCell::withNote(60u), NoteCell::hold(),
        NoteCell::rest() };
    track.noteColumn.length = track.notes.size();
    TimingPlaybackScheduler scheduler;
    scheduler.setPattern(oneTrack(std::move(track)));
    scheduler.setTransport(transport());
    scheduler.start();

    std::array<ScheduledEvent, 4u> events {};
    const auto count = scheduler.process(16001u, events.data(),
        events.size());
    check(count == 2u
            && events[0u].kind == ScheduledEventKind::NoteOn
            && events[1u].kind == ScheduledEventKind::NoteOff
            && events[1u].absoluteSampleTime == 16000u
            && events[1u].noteId == events[0u].noteId,
        "held-note release should retain identity through the timing timeline");
}

} // namespace

int main()
{
    testLogicalTickObserverPartitionInvariance();
    testLogicalTickObserverSeesCoincidentTicksInOrder();
    testLogicalTickObserverCanStopAfterAdmittedTick();
    testStoppedBoundaryDrainsFutureTimingTail();
    testBoundaryTransportChangeRetimesNextTick();
    testUntimedPassThroughParity();
    testRatchetSurvivesBlockBoundaries();
    testStutterAndMicroTiming();
    testMicroTimingMovesTheCanonicalRowBundle();
    testAccentAndStaleParameterSuppression();
    testWarpCollisionAndCrossTickOrdering();
    testDelayedKillCancelsPendingPrimary();
    testLiveTimingActivationUsesSequencerMemory();
    testTimingEffectsUseNominalTickUnderSwingAndWarp();
    testLiveReplacementMemoryAndPendingTail();
    testPreparedBoundaryActivationPreservesPendingTimeline();
    testPreparedContractionReleaseHasNoStaleTimingDelay();
    testBlockSizeInvarianceAndOverflowTelemetry();
    testInactiveStoredTimingCellsDoNotAddLatency();
    testPreparedHostBeatStartUsesWarpedClock();
    testHeldNoteOffTraversesTimingTimeline();
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "timing playback scheduler tests passed\n";
    return EXIT_SUCCESS;
}
