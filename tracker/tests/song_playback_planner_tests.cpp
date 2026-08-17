#include "s3g/tracker/song_playback_planner.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <utility>

using namespace s3g::tracker;

namespace {

int failures = 0;

void check(bool condition, const std::string& message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

SongArrangement arrangement(bool loop = false)
{
    SongArrangement song;
    song.name = "Native Fixture";
    song.loop = loop;
    song.ticksPerBeat = 4u;

    SongRow a;
    a.patternId = "A01";
    a.durationTicks = 3u;
    a.repeats = 2u;
    a.bpm = 132.0;
    a.swing = 0.57;
    a.mutedTracks = 1u << 3u;
    song.rows.push_back(a);

    SongRow b;
    b.patternId = "B01";
    b.durationTicks = 2u;
    b.repeats = 1u;
    song.rows.push_back(b);

    SongRow fill;
    fill.patternId = "FILL";
    fill.durationTicks = 1u;
    fill.repeats = 1u;
    song.rows.push_back(fill);
    return song;
}

void testValidationAndTransactionalInstall()
{
    SongPlaybackPlanner planner;
    check(!planner.hasArrangement()
            && !planner.currentRowIndex().has_value(),
        "a fresh planner should expose no phantom current row");
    check(planner.queueRow(0u, SongLaunchQuantization::NextTick)
            == SongQueueResult::NoArrangement,
        "queueing without an arrangement should fail explicitly");

    auto song = arrangement();
    const auto accepted = planner.setArrangement(song);
    check(accepted.ok() && planner.hasArrangement()
            && planner.currentRowIndex() == 0u && !planner.isRunning(),
        "a valid arrangement should install at row zero in stopped state");
    check(planner.currentRow() && planner.currentRow()->patternId == "A01"
            && planner.currentRow()->bpm == 132.0
            && planner.currentRow()->mutedTracks == (1u << 3u),
        "native song rows should retain pattern and performance metadata");

    check(planner.start() && planner.advanceTick().consumed,
        "a valid arrangement should begin deterministic tick playback");
    check(planner.absoluteTick() == 1u
            && planner.ticksCompletedInRow() == 1u,
        "the planner should expose completed global and row ticks");

    auto rejected = song;
    rejected.rows[1].patternId.clear();
    const auto invalid = planner.setArrangement(std::move(rejected));
    check(invalid.code == SongValidationCode::EmptyPatternId
            && invalid.row == 1u,
        "validation should identify the exact invalid song row");
    check(planner.isRunning() && planner.absoluteTick() == 1u
            && planner.currentRow()->patternId == "A01",
        "a rejected arrangement must not mutate installed runtime state");

    SongArrangement empty;
    check(validateSongArrangement(empty).code == SongValidationCode::Empty,
        "empty arrangements should be rejected");
    auto invalidTicks = song;
    invalidTicks.ticksPerBeat = 0u;
    check(validateSongArrangement(invalidTicks).code
            == SongValidationCode::InvalidTicksPerBeat,
        "beat quantization requires a nonzero native tick grid");
    auto invalidDuration = song;
    invalidDuration.rows[0].durationTicks = 0u;
    check(validateSongArrangement(invalidDuration).code
            == SongValidationCode::InvalidDurationTicks,
        "song row durations must be explicit and nonzero");
    auto invalidRepeats = song;
    invalidRepeats.rows[0].repeats = 0u;
    check(validateSongArrangement(invalidRepeats).code
            == SongValidationCode::InvalidRepeats,
        "song repeats must be explicit and nonzero");
    auto invalidBpm = song;
    invalidBpm.rows[0].bpm = std::numeric_limits<double>::quiet_NaN();
    check(validateSongArrangement(invalidBpm).code
            == SongValidationCode::InvalidBpm,
        "non-finite row tempo overrides should be rejected");
    auto invalidSwing = song;
    invalidSwing.rows[0].swing = 0.49;
    check(validateSongArrangement(invalidSwing).code
            == SongValidationCode::InvalidSwing,
        "row swing overrides should use the transport's native range");
    auto invalidWarp = song;
    invalidWarp.rows[0].timingWarpLibraryIndex
        = kMaximumTimingWarpLibraryEntries;
    check(validateSongArrangement(invalidWarp).code
            == SongValidationCode::InvalidTimingWarpLibraryIndex,
        "Song warp references should remain inside the fixed project library");
}

void testNaturalRepeatRowAndFinishBoundaries()
{
    SongPlaybackPlanner planner;
    check(planner.setArrangement(arrangement()).ok() && planner.start(),
        "natural-advance fixture should start");

    auto result = planner.advanceTick();
    check(result.consumed && !result.patternCycleBoundary
            && !result.songRowBoundary && !result.transition
            && planner.tickInPatternCycle() == 1u
            && planner.currentRepeatIndex() == 0u,
        "ordinary ticks should remain inside the current pattern cycle");
    result = planner.advanceTick();
    check(!result.patternCycleBoundary && !result.transition,
        "a three-tick cycle must not end after two ticks");
    result = planner.advanceTick();
    check(result.patternCycleBoundary && !result.songRowBoundary
            && !result.transition && planner.currentRepeatIndex() == 1u
            && planner.tickInPatternCycle() == 0u,
        "a repeat should continue pattern phase without relaunching its row");

    planner.advanceTick();
    planner.advanceTick();
    result = planner.advanceTick();
    check(result.patternCycleBoundary && result.songRowBoundary
            && result.transition
            && result.transition->fromRow == 0u
            && result.transition->toRow == 1u
            && result.transition->reason
                == SongTransitionReason::NaturalAdvance,
        "the final repeat boundary should launch the next ordered song row");
    check(planner.currentRowIndex() == 1u
            && planner.currentRow()->patternId == "B01"
            && planner.ticksCompletedInRow() == 0u
            && planner.absoluteTick() == 6u,
        "a natural row launch should reset local phase without rewinding time");

    result = planner.advanceTick();
    check(!result.songRowBoundary && planner.tickInPatternCycle() == 1u,
        "the next row should use its own duration");
    result = planner.advanceTick();
    check(result.songRowBoundary && result.transition
            && result.transition->toRow == 2u,
        "ordered playback should reach the final row");
    result = planner.advanceTick();
    check(result.finished && result.songRowBoundary && !result.transition
            && planner.isFinished() && !planner.isRunning()
            && planner.currentRowIndex() == 2u
            && planner.ticksCompletedInRow() == 1u
            && planner.tickInPatternCycle() == 0u,
        "a non-looping song should stop after consuming its final tick");
    check(!planner.advanceTick().consumed && planner.absoluteTick() == 9u,
        "finished playback must remain stable when the scheduler calls again");
    check(!planner.resume() && !planner.isRunning(),
        "a completed non-looping song requires a fresh explicit start");
}

void testLoopWrap()
{
    auto song = arrangement(true);
    song.rows = { song.rows[2] };
    SongPlaybackPlanner planner;
    check(planner.setArrangement(std::move(song)).ok() && planner.start(),
        "loop fixture should start");
    const auto result = planner.advanceTick();
    check(result.songRowBoundary && result.transition
            && result.transition->fromRow == 0u
            && result.transition->toRow == 0u
            && result.transition->reason == SongTransitionReason::LoopWrap
            && planner.isRunning() && !planner.isFinished()
            && planner.absoluteTick() == 1u
            && planner.ticksCompletedInRow() == 0u,
        "a one-row loop should report an explicit same-row relaunch");
}

void testNextTickQueueAndReplacement()
{
    SongPlaybackPlanner planner;
    check(planner.setArrangement(arrangement()).ok() && planner.start(),
        "next-tick queue fixture should start");
    check(planner.queueRow(1u, SongLaunchQuantization::NextSongRow)
            == SongQueueResult::Queued
            && planner.pendingRowIndex() == 1u,
        "a running planner should expose its queued row");
    check(planner.queueRow(2u, SongLaunchQuantization::NextTick)
            == SongQueueResult::Queued
            && planner.pendingRowIndex() == 2u
            && planner.pendingQuantization()
                == SongLaunchQuantization::NextTick,
        "a newer live launch request should atomically replace the old one");

    const auto result = planner.advanceTick();
    check(result.transition
            && result.transition->reason
                == SongTransitionReason::QuantizedLaunch
            && result.transition->toRow == 2u
            && planner.currentRowIndex() == 2u
            && planner.ticksCompletedInRow() == 0u
            && planner.absoluteTick() == 1u
            && !planner.pendingRowIndex(),
        "next-tick launch should occur after one consumed tick and clear queue");
    check(planner.queueRow(9u, SongLaunchQuantization::NextTick)
            == SongQueueResult::RowOutOfRange,
        "an out-of-range launch should fail without changing playback");
    check(planner.queueRow(0u, static_cast<SongLaunchQuantization>(255u))
            == SongQueueResult::InvalidQuantization
            && !planner.pendingRowIndex(),
        "invalid typed quantization values should fail without sticking queue");
    planner.stop();
    check(planner.queueRow(0u, SongLaunchQuantization::NextTick)
            == SongQueueResult::NotRunning,
        "stopped song form should require an explicit start rather than queue");
}

void testBeatQuantizationUsesStableGlobalClock()
{
    auto song = arrangement();
    song.rows[0].durationTicks = 12u;
    song.rows[0].repeats = 1u;
    song.rows[1].durationTicks = 12u;
    SongPlaybackPlanner planner;
    check(planner.setArrangement(std::move(song)).ok() && planner.start(),
        "beat queue fixture should start");
    check(planner.queueRow(1u, SongLaunchQuantization::NextBeat)
            == SongQueueResult::Queued,
        "beat-quantized row should queue");
    for (int tick = 0; tick < 3; ++tick)
        check(!planner.advanceTick().transition,
            "beat launch must wait for its exact global boundary");
    auto result = planner.advanceTick();
    check(result.transition && result.transition->toRow == 1u
            && planner.absoluteTick() == 4u,
        "four ticks per beat should launch on completed global tick four");

    check(planner.queueRow(2u, SongLaunchQuantization::NextBeat)
            == SongQueueResult::Queued,
        "a second beat launch should queue on the new row");
    for (int tick = 0; tick < 3; ++tick)
        check(!planner.advanceTick().transition,
            "queueing exactly at a boundary must wait for the next beat");
    result = planner.advanceTick();
    check(result.transition && result.transition->toRow == 2u
            && planner.absoluteTick() == 8u,
        "row changes must not reset the beat quantization clock");
}

void testPatternAndSongRowQuantization()
{
    SongPlaybackPlanner planner;
    check(planner.setArrangement(arrangement()).ok() && planner.start(),
        "cycle queue fixture should start");
    planner.advanceTick();
    check(planner.queueRow(2u,
              SongLaunchQuantization::NextPatternCycle)
            == SongQueueResult::Queued,
        "pattern-cycle launch should queue");
    check(!planner.advanceTick().transition,
        "cycle launch should not fire before the current cycle ends");
    auto result = planner.advanceTick();
    check(result.patternCycleBoundary && !result.songRowBoundary
            && result.transition && result.transition->toRow == 2u,
        "cycle launch should preempt remaining repeats at the next cycle edge");

    check(planner.start(0u), "song-row queue fixture should restart row zero");
    planner.advanceTick();
    check(planner.queueRow(2u, SongLaunchQuantization::NextSongRow)
            == SongQueueResult::Queued,
        "full-row launch should queue");
    for (int tick = 0; tick < 4; ++tick)
        check(!planner.advanceTick().transition,
            "full-row launch should wait through the remaining repeat body");
    result = planner.advanceTick();
    check(result.patternCycleBoundary && result.songRowBoundary
            && result.transition && result.transition->toRow == 2u
            && result.transition->reason
                == SongTransitionReason::QuantizedLaunch,
        "queued row launch should win over natural ordered advance at boundary");
}

void testStartResetAndCancelContracts()
{
    SongPlaybackPlanner planner;
    check(planner.setArrangement(arrangement()).ok(),
        "control-state fixture should install");
    check(!planner.start(3u) && !planner.isRunning(),
        "invalid start row should not mutate transport state");
    check(planner.start(1u) && planner.currentRowIndex() == 1u,
        "start should allow a direct fresh row launch");
    planner.advanceTick();
    planner.queueRow(0u, SongLaunchQuantization::NextSongRow);
    planner.stop();
    check(!planner.advanceTick().consumed
            && planner.ticksCompletedInRow() == 1u
            && planner.pendingRowIndex() == 0u,
        "paused planner calls should preserve song phase and queued launch");
    check(planner.resume() && planner.isRunning()
            && planner.ticksCompletedInRow() == 1u
            && planner.pendingRowIndex() == 0u,
        "resume should continue the preserved song-form state");
    planner.cancelQueuedRow();
    check(!planner.pendingRowIndex(),
        "cancel should clear a queued launch without moving song phase");
    planner.reset();
    check(!planner.isRunning() && !planner.isFinished()
            && planner.currentRowIndex() == 0u
            && planner.absoluteTick() == 0u
            && planner.ticksCompletedInRow() == 0u,
        "reset should restore the installed arrangement's initial stopped state");
}

} // namespace

int main()
{
    testValidationAndTransactionalInstall();
    testNaturalRepeatRowAndFinishBoundaries();
    testLoopWrap();
    testNextTickQueueAndReplacement();
    testBeatQuantizationUsesStableGlobalClock();
    testPatternAndSongRowQuantization();
    testStartResetAndCancelContracts();

    if (failures != 0) {
        std::cerr << failures << " song playback planner test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "song playback planner tests passed\n";
    return EXIT_SUCCESS;
}
