#include "s3g/tracker/song_playback_planner.h"
#include "s3g/tracker/timing_playback_scheduler.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

namespace {

using namespace s3g::tracker;

int failures = 0;

void check(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

TransportSettings baseTransport()
{
    TransportSettings result;
    result.sampleRate = 8000.0;
    result.bpm = 60.0;
    result.ticksPerBeat = 1u;
    result.swing = 0.5;
    return result;
}

Track noteTrack(std::array<uint8_t, 4u> notes)
{
    Track track;
    for (const auto note : notes)
        track.notes.push_back(NoteCell::withNote(note));
    track.velocities.assign(notes.size(), ValueCell::withValue(1.0f));
    track.noteColumn.length = notes.size();
    track.velocityColumn.length = notes.size();
    return track;
}

Pattern runtimePattern()
{
    Pattern pattern;
    pattern.name = "ONLY PATTERN";
    pattern.visibleRows = 4u;
    pattern.tracks.push_back(noteTrack({ 60u, 61u, 62u, 63u }));
    pattern.tracks.push_back(noteTrack({ 70u, 71u, 72u, 73u }));
    return pattern;
}

SongArrangement runtimeSong()
{
    SongArrangement song;
    song.name = "Runtime integration";
    song.ticksPerBeat = 1u;
    SongRow first;
    first.patternId = "ONLY PATTERN";
    first.durationTicks = 2u;
    first.repeats = 2u;
    song.rows.push_back(first);
    SongRow second;
    second.patternId = "ONLY PATTERN";
    second.durationTicks = 2u;
    second.repeats = 1u;
    second.bpm = 120.0;
    second.mutedTracks = 1u << 1u;
    song.rows.push_back(second);
    return song;
}

struct SongSchedulerRuntime {
    TimingPlaybackScheduler* scheduler = nullptr;
    SongPlaybackPlanner planner;
    TransportSettings base;
    std::array<LogicalTickBoundary, 16u> boundaries {};
    std::size_t boundaryCount = 0u;

    TransportSettings rowTransport(const SongRow& row) const noexcept
    {
        auto result = base;
        result.loopEnabled = false;
        result.timingWarp.clear();
        if (row.bpm) result.bpm = *row.bpm;
        if (row.swing) result.swing = *row.swing;
        if (row.timingWarpLibraryIndex) {
            if (const auto* entry = scheduler->timingWarpLibrary().entry(
                    *row.timingWarpLibraryIndex)) {
                result.warpCycleTicks = entry->cycleTicks;
                result.timingWarp = entry->stack;
            }
        }
        return result;
    }

    void applyTransition() noexcept
    {
        const auto* row = planner.currentRow();
        if (!row) return;
        scheduler->setTransportAtTickBoundary(rowTransport(*row));
        scheduler->setRuntimeTrackMuteMask(row->mutedTracks);
        scheduler->relaunchColumnsAtTickBoundary(0u);
    }
};

LogicalTickBoundaryAction advanceSong(void* opaque,
    const LogicalTickBoundary& boundary) noexcept
{
    auto& runtime = *static_cast<SongSchedulerRuntime*>(opaque);
    if (runtime.boundaryCount < runtime.boundaries.size())
        runtime.boundaries[runtime.boundaryCount++] = boundary;
    const auto result = runtime.planner.advanceTick();
    if (result.transition) runtime.applyTransition();
    return result.finished ? LogicalTickBoundaryAction::StopAfterBoundary
                           : LogicalTickBoundaryAction::Continue;
}

void armRuntime(SongSchedulerRuntime& runtime,
    TimingPlaybackScheduler& scheduler, SongArrangement song,
    Pattern pattern)
{
    runtime.scheduler = &scheduler;
    runtime.base = baseTransport();
    check(runtime.planner.setArrangement(std::move(song)).ok()
            && runtime.planner.start(),
        "the Song runtime fixture should install and start");
    scheduler.setPattern(std::move(pattern));
    const auto* first = runtime.planner.currentRow();
    scheduler.setTransport(runtime.rowTransport(*first));
    scheduler.setRuntimeTrackMuteMask(first->mutedTracks);
    scheduler.setLogicalTickObserver(&advanceSong, &runtime);
    scheduler.start();
}

std::vector<ScheduledEvent> noteOnsForTrack(
    const ScheduledEvent* events, std::size_t count, uint32_t track)
{
    std::vector<ScheduledEvent> result;
    for (std::size_t index = 0u; index < count; ++index) {
        if (events[index].track == track
            && events[index].kind == ScheduledEventKind::NoteOn)
            result.push_back(events[index]);
    }
    return result;
}

void testNaturalRowsApplyTempoMuteAndRelaunch()
{
    TimingPlaybackScheduler scheduler;
    SongSchedulerRuntime runtime;
    armRuntime(runtime, scheduler, runtimeSong(), runtimePattern());

    std::array<ScheduledEvent, 128u> events {};
    const auto count = scheduler.process(32001u,
        events.data(), events.size());
    const auto laneOne = noteOnsForTrack(events.data(), count, 0u);
    const auto laneTwo = noteOnsForTrack(events.data(), count, 1u);

    const std::array<uint8_t, 6u> expectedNotes {
        60u, 61u, 62u, 63u, 60u, 61u
    };
    const std::array<uint64_t, 6u> expectedFrames {
        0u, 8000u, 16000u, 24000u, 28000u, 32000u
    };
    check(laneOne.size() == expectedNotes.size(),
        "both natural Song rows should emit the expected lane-one ticks");
    for (std::size_t index = 0u;
         index < std::min(laneOne.size(), expectedNotes.size()); ++index) {
        check(laneOne[index].note == expectedNotes[index]
                && laneOne[index].absoluteSampleTime == expectedFrames[index],
            "row repeats must preserve phase, then relaunch row zero at the overridden BPM");
    }
    check(laneTwo.size() == 4u,
        "the next-row runtime mute must suppress lane two without rewriting its authored mute");
    check(runtime.boundaryCount == 6u && runtime.planner.isFinished()
            && !scheduler.isPlaying(),
        "the final Song row should stop exactly after its final logical tick");
    check(count != 0u && laneOne.back().absoluteSampleTime == 32000u,
        "StopAfterBoundary must preserve the final row's admitted output");
}

void testQuantizedLaunchUsesPatternBoundary()
{
    auto song = runtimeSong();
    song.rows[0u].durationTicks = 4u;
    song.rows[0u].repeats = 2u;
    SongRow target;
    target.patternId = "ONLY PATTERN";
    target.durationTicks = 2u;
    target.repeats = 1u;
    song.rows.push_back(target);

    TimingPlaybackScheduler scheduler;
    SongSchedulerRuntime runtime;
    armRuntime(runtime, scheduler, std::move(song), runtimePattern());
    check(runtime.planner.queueRow(2u,
              SongLaunchQuantization::NextPatternCycle)
            == SongQueueResult::Queued,
        "the target Song row should queue while running");

    std::array<ScheduledEvent, 64u> events {};
    auto count = scheduler.process(24001u, events.data(), events.size());
    check(count != 0u && runtime.planner.currentRowIndex()
                == std::optional<std::size_t>(2u)
            && !runtime.planner.isFinished(),
        "NextPatternCycle should launch after four completed source ticks");
    count = scheduler.process(8000u, events.data(), events.size());
    const auto launched = noteOnsForTrack(events.data(), count, 0u);
    check(!launched.empty() && launched.front().note == 60u
            && launched.front().absoluteSampleTime == 32000u,
        "a quantized row launch must relaunch authored columns at row zero");
}

void testSongRowsRecallSavedWarpAndOffRestoresIdentity()
{
    SongArrangement song;
    song.name = "Warp rows";
    song.ticksPerBeat = 1u;
    SongRow offBefore;
    offBefore.patternId = "ONLY PATTERN";
    offBefore.durationTicks = 1u;
    song.rows.push_back(offBefore);
    SongRow warped;
    warped.patternId = "ONLY PATTERN";
    warped.durationTicks = 2u;
    warped.timingWarpLibraryIndex = 6u;
    song.rows.push_back(warped);
    SongRow offAfter;
    offAfter.patternId = "ONLY PATTERN";
    offAfter.durationTicks = 1u;
    song.rows.push_back(offAfter);

    TimingWarpStack stack;
    check(stack.append(TimingWarpTransform::exponential(2.0)).added(),
        "Song warp fixture should compile");
    TimingWarpLibrary library;
    check(library.store(6u, "Fast Start", 4u, stack),
        "Song warp fixture should occupy named slot 07");

    TimingPlaybackScheduler scheduler;
    scheduler.setTimingWarpLibrary(std::move(library));
    SongSchedulerRuntime runtime;
    armRuntime(runtime, scheduler, std::move(song), runtimePattern());

    std::array<ScheduledEvent, 32u> events {};
    const auto count = scheduler.process(16001u,
        events.data(), events.size());
    const auto notes = noteOnsForTrack(events.data(), count, 0u);
    const std::array<uint64_t, 4u> expectedFrames {
        0u, 2000u, 8000u, 16000u
    };
    check(notes.size() == expectedFrames.size(),
        "three Song rows should emit their complete test sequence");
    for (std::size_t index = 0u;
         index < std::min(notes.size(), expectedFrames.size()); ++index) {
        check(notes[index].absoluteSampleTime == expectedFrames[index],
            "Song WARP should retime its row and OFF should restore identity timing");
    }
    check(scheduler.transport().timingWarp.empty(),
        "the final OFF row must not inherit the preceding saved warp");
}

void testFinalStutterTailDrainsAfterStopBoundary()
{
    auto pattern = runtimePattern();
    pattern.tracks.resize(1u);
    auto& pair = pattern.tracks[0u].fxPairs[0u];
    pair.actions = {
        FxActionCell::sequencer(SequencerAction::Stutter),
        FxActionCell::empty(), FxActionCell::empty(), FxActionCell::empty()
    };
    pair.values = {
        FxValueCell::withValue(0.0f), FxValueCell::previous(),
        FxValueCell::previous(), FxValueCell::previous()
    };
    pair.actionColumn.length = pair.actions.size();
    pair.valueColumn.length = pair.values.size();

    SongArrangement song;
    song.ticksPerBeat = 1u;
    SongRow only;
    only.patternId = pattern.name;
    only.durationTicks = 1u;
    song.rows.push_back(only);

    TimingPlaybackScheduler scheduler;
    SongSchedulerRuntime runtime;
    armRuntime(runtime, scheduler, std::move(song), std::move(pattern));
    std::array<ScheduledEvent, 16u> events {};
    auto count = scheduler.process(1u, events.data(), events.size());
    check(count == 1u && events[0u].absoluteSampleTime == 0u
            && !scheduler.isPlaying()
            && scheduler.pendingEventCount() == 1u,
        "the final Song tick should stop generation but retain its future stutter");
    count = scheduler.process(7999u, events.data(), events.size());
    check(count == 0u && scheduler.pendingEventCount() == 1u,
        "the stopped final tail must respect the exclusive block boundary");
    count = scheduler.process(1u, events.data(), events.size());
    check(count == 1u && events[0u].absoluteSampleTime == 8000u
            && scheduler.pendingEventCount() == 0u,
        "the stopped Song scheduler must deliver the future final-tick tail");
}

} // namespace

int main()
{
    testNaturalRowsApplyTempoMuteAndRelaunch();
    testQuantizedLaunchUsesPatternBoundary();
    testSongRowsRecallSavedWarpAndOffRestoresIdentity();
    testFinalStutterTailDrainsAfterStopBoundary();
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "Song timing runtime tests passed\n";
    return EXIT_SUCCESS;
}
