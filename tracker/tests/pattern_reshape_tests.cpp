#include "s3g/tracker/pattern_reshape.h"

#include <cmath>
#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

s3g::tracker::Pattern fixture()
{
    using namespace s3g::tracker;
    Pattern pattern;
    pattern.name = "RESHAPE";
    pattern.visibleRows = 8u;
    pattern.tracks.resize(2u);
    for (std::size_t lane = 0u; lane < pattern.tracks.size(); ++lane) {
        auto& track = pattern.tracks[lane];
        track.notes.assign(8u, NoteCell::rest());
        track.notes[0u] = NoteCell::withNote(static_cast<uint8_t>(36u + lane));
        track.notes[4u] = NoteCell::withNote(static_cast<uint8_t>(36u + lane));
        track.noteColumn.length = 8u;
        track.velocities.assign(8u, ValueCell::defaultValue());
        track.velocities[0u] = ValueCell::withValue(lane == 0u ? 0.4f : 0.8f);
        track.velocities[4u] = ValueCell::withValue(lane == 0u ? 0.6f : 1.0f);
        track.velocityColumn.length = 8u;
        auto& pair = track.fxPairs[0u];
        pair.actions.assign(8u, FxActionCell::empty());
        pair.values.assign(8u, FxValueCell::previous());
        pair.actionColumn.length = 8u;
        pair.valueColumn.length = 8u;
        pair.actions[0u] = FxActionCell::sequencer(SequencerAction::MicroTime);
        pair.actions[4u] = FxActionCell::sequencer(SequencerAction::MicroTime);
        pair.values[0u] = FxValueCell::withValue(lane == 0u ? 0.55f : 0.60f);
        pair.values[4u] = FxValueCell::withValue(lane == 0u ? 0.65f : 0.70f);
    }
    return pattern;
}

} // namespace

int main()
{
    using namespace s3g::tracker;
    const Pattern source = fixture();
    const auto analysis = analyzePatternReshape(source, 4u);
    check(analysis.rows == 8u && analysis.cycleRows == 4u
            && analysis.passes == 2u,
        "analysis should retain explicit cycle and pass counts");
    check(analysis.noteEvents == 4u && analysis.timingValues == 4u
            && analysis.velocityValues == 4u,
        "analysis should count authored note, timing, and velocity material");
    check(analysis.confidence > 0.25f,
        "repeated timing material should establish confidence");

    PatternReshapeSettings timing;
    timing.cycleRows = 4u;
    timing.pocket = 1.0f;
    timing.tighten = 1.0f;
    timing.timingDepth = 1.0f;
    const auto tightened = reshapePattern(source, timing);
    check(tightened.timingChanged == 4u,
        "timing reshape should rewrite every direct MT value");
    check(tightened.pattern.tracks[0u].notes[0u].note
            == source.tracks[0u].notes[0u].note,
        "timing reshape must preserve pitches");
    check(std::abs(tightened.pattern.tracks[0u].fxPairs[0u]
            .values[0u].normalized - 0.5f) < 0.0001f,
        "full tightening should move signed microtime to the row grid");

    Pattern sparseTiming = source;
    sparseTiming.tracks[1u].fxPairs[0u].actions.assign(
        8u, FxActionCell::empty());
    sparseTiming.tracks[1u].fxPairs[0u].values.assign(
        8u, FxValueCell::previous());
    PatternReshapeSettings sparseTighten;
    sparseTighten.cycleRows = 8u;
    sparseTighten.pocket = 0.0f;
    sparseTighten.tighten = 1.0f;
    const auto sparseTightened = reshapePattern(
        sparseTiming, sparseTighten);
    check(sparseTightened.timingChanged == 2u
            && std::abs(sparseTightened.pattern.tracks[0u].fxPairs[0u]
                .values[0u].normalized - 0.5f) < 0.0001f
            && std::abs(sparseTightened.pattern.tracks[0u].fxPairs[0u]
                .values[4u].normalized - 0.5f) < 0.0001f,
        "tighten should remain effective when each phase has only one MT point");

    PatternReshapeSettings dynamics;
    dynamics.cycleRows = 4u;
    dynamics.pocket = 0.0f;
    dynamics.velocityRange = 0.0f;
    dynamics.accentDepth = 1.0f;
    const auto compressed = reshapePattern(source, dynamics);
    check(compressed.velocityChanged == 4u,
        "zero velocity range should collapse authored values per lane");
    check(std::abs(compressed.pattern.tracks[0u].velocities[0u].normalized
            - 0.5f) < 0.0001f,
        "velocity compression should retain the lane median");

    Pattern unsupported = source;
    unsupported.tracks[0u].fxPairs[0u].actionColumn.direction
        = Direction::Reverse;
    const auto safe = reshapePattern(unsupported, timing);
    check(safe.timingSkipped == 2u,
        "polymetric MT columns should be reported rather than realigned");
    check(safe.pattern.tracks[0u].fxPairs[0u].values[0u].normalized
            == unsupported.tracks[0u].fxPairs[0u].values[0u].normalized,
        "unsupported MT columns must remain unchanged");

    Pattern inferred = source;
    for (auto& track : inferred.tracks) {
        for (auto& pair : track.fxPairs) {
            pair.actions.assign(8u, FxActionCell::empty());
            pair.values.assign(8u, FxValueCell::previous());
            pair.actionColumn = { 8u, 1u, 0u, Direction::Forward, false };
            pair.valueColumn = { 8u, 1u, 0u, Direction::Forward, false };
        }
    }
    inferred.tracks[0u].notes[2u] = NoteCell::withNote(42u);
    const auto inferredAnalysis = analyzePatternReshape(inferred, 4u);
    check(inferredAnalysis.timingValues == 0u
            && inferredAnalysis.writableTimingOnsets == 5u
            && inferredAnalysis.phaseNoteEvents[0u] == 4u
            && inferredAnalysis.phaseNoteEvents[2u] == 1u,
        "analysis should expose rhythmic support and safe empty MT targets");
    PatternReshapeSettings authorTiming;
    authorTiming.cycleRows = 4u;
    authorTiming.pocket = 1.0f;
    authorTiming.microTimingWrite = PatternReshapeWriteMode::FillMissing;
    const auto authoredTiming = reshapePattern(inferred, authorTiming);
    check(authoredTiming.timingCreated == 1u
            && authoredTiming.timingChanged == 1u
            && authoredTiming.pattern.tracks[0u].fxPairs[0u]
                .actions[2u].state == FxActionCellState::Sequencer
            && authoredTiming.pattern.tracks[0u].fxPairs[0u]
                .actions[2u].sequencerAction
                    == SequencerAction::MicroTime
            && authoredTiming.pattern.tracks[0u].fxPairs[0u]
                .values[2u].normalized > 0.5f,
        "ADD TO ONSETS should infer a late weak onset and occupy only its empty direct SEQ cell");

    PatternReshapeSettings authorVelocity;
    authorVelocity.cycleRows = 4u;
    authorVelocity.pocket = 0.0f;
    authorVelocity.velocityWrite = PatternReshapeWriteMode::FillMissing;
    authorVelocity.accentDepth = 2.0f;
    const auto authoredVelocity = reshapePattern(source, authorVelocity);
    check(authoredVelocity.velocityCreated == 12u
            && authoredVelocity.velocityChanged >= 12u
            && authoredVelocity.pattern.tracks[0u].velocities[1u].state
                == ValueCellState::Value,
        "FILL DEFAULTS should materialize the independent velocity cycles");

    Pattern mutationSource = source;
    mutationSource.bursts[0u].name = "TEST RUFF";
    mutationSource.bursts[0u].eventCount = 2u;
    mutationSource.bursts[0u].events[0u] = { 0u, 36u, 110u, 45u };
    mutationSource.bursts[0u].events[1u] = { 32768u, 36u, 92u, 45u };
    PatternReshapeSettings mutation;
    mutation.pocket = 0.0f;
    mutation.mutationAmount = 1.0f;
    mutation.densityChange = 1.0f;
    mutation.syncopation = 0.8f;
    mutation.displacementRows = 2u;
    mutation.burstChance = 1.0f;
    mutation.cycleDrift = 1.0f;
    mutation.mutationSeed = 1729u;
    mutation.laneDefaultNotes = { 36u, 38u };
    const auto mutatedA = reshapePattern(mutationSource, mutation);
    const auto mutatedB = reshapePattern(mutationSource, mutation);
    bool deterministic = mutatedA.notesAdded == mutatedB.notesAdded
        && mutatedA.notesMoved == mutatedB.notesMoved
        && mutatedA.burstsCreated == mutatedB.burstsCreated
        && mutatedA.cyclesChanged == mutatedB.cyclesChanged;
    for (std::size_t lane = 0u;
         deterministic && lane < mutatedA.pattern.tracks.size(); ++lane) {
        const auto& left = mutatedA.pattern.tracks[lane];
        const auto& right = mutatedB.pattern.tracks[lane];
        deterministic = left.noteColumn.length == right.noteColumn.length
            && left.notes.size() == right.notes.size();
        for (std::size_t row = 0u;
             deterministic && row < left.notes.size(); ++row) {
            deterministic = left.notes[row].state == right.notes[row].state
                && left.notes[row].note == right.notes[row].note;
        }
    }
    check(deterministic,
        "rhythm mutation should be exactly repeatable for a fixed seed");
    check(mutatedA.cyclesChanged == 2u && mutatedA.notesAdded > 0u
            && mutatedA.notesMoved + mutatedA.burstsCreated > 0u,
        "full mutation should create structural, density, displacement, and Burst variation");
    check(mutatedA.pattern.tracks[0u].notes[0u].state
                == NoteCellState::Note
            && mutatedA.pattern.tracks[1u].notes[0u].state
                == NoteCellState::Note,
        "high-confidence downbeat anchors should survive rhythm mutation");
    check(mutatedA.after.noteEvents != mutatedA.before.noteEvents,
        "the after profile should expose changed hit density");

    if (failures == 0) {
        std::cout << "pattern reshape tests passed\n";
        return 0;
    }
    return 1;
}
