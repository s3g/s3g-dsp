#include "s3g/tracker/pitch_map.h"

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
    pattern.visibleRows = 8u;
    pattern.tracks.resize(1u);
    auto& track = pattern.tracks[0u];
    track.noteColumn.length = 8u;
    track.notes.assign(8u, NoteCell::rest());
    track.notes[0u] = NoteCell::withNote(60u);
    track.notes[1u] = NoteCell::retriggerPrevious();
    track.notes[2u] = NoteCell::withNote(62u);
    track.notes[3u] = NoteCell::hold();
    track.notes[4u] = NoteCell::withNote(64u);
    track.notes[5u] = NoteCell::withBurst(0u);
    track.notes[6u] = NoteCell::withNote(67u);
    track.notes[7u] = NoteCell::kill();
    return pattern;
}

} // namespace

int main()
{
    using namespace s3g::tracker;
    const auto source = fixture();
    const auto analysis = analyzePitchMap(source, 0u, 0u, 7u);
    check(analysis.noteCount == 4u && analysis.uniquePitchClasses == 4u,
        "analysis should count explicit NOTE pitches only");
    check(analysis.rootPitchClass == 0u && analysis.scale == 1u
            && analysis.matchingNotes == 4u && analysis.confidence > 0.99f,
        "C major evidence should resolve to the familiar C major scale");

    PitchMapSettings fit;
    fit.rootPitchClass = 0u;
    fit.scale = 1u;
    fit.minimumNote = 48u;
    fit.maximumNote = 72u;
    fit.contour = PitchContour::Fit;
    auto fitted = source;
    fitted.tracks[0u].notes[2u] = NoteCell::withNote(61u);
    const auto fitChanged = applyPitchMap(fitted, 0u, 0u, 7u, fit);
    check(fitChanged == 1u && fitted.tracks[0u].notes[2u].note == 60u,
        "fit should move one chromatic note to the nearest scale degree");
    check(fitted.tracks[0u].notes[1u].state
                == NoteCellState::RetriggerPrevious
            && fitted.tracks[0u].notes[3u].state == NoteCellState::Hold
            && fitted.tracks[0u].notes[5u].state == NoteCellState::Burst
            && fitted.tracks[0u].notes[7u].state == NoteCellState::Kill,
        "fit must preserve NOTE symbols and Burst references");

    PitchMapSettings walk = fit;
    walk.contour = PitchContour::RandomWalk;
    walk.maximumLeapDegrees = 3u;
    walk.preserveEndpoints = false;
    walk.seed = 1729u;
    const auto first = previewPitchMap(source, 0u, 0u, 7u, walk);
    const auto second = previewPitchMap(source, 0u, 0u, 7u, walk);
    check(first.assignments.size() == 4u
            && first.changed == second.changed,
        "generation should retain every explicit NOTE onset");
    bool identical = first.assignments.size() == second.assignments.size();
    for (std::size_t index = 0u;
         identical && index < first.assignments.size(); ++index)
        identical = first.assignments[index].row == second.assignments[index].row
            && first.assignments[index].note == second.assignments[index].note;
    check(identical, "a fixed seed should reproduce the same contour");
    for (const auto& assignment : first.assignments) {
        const uint8_t pc = assignment.note % 12u;
        check(pc == 0u || pc == 2u || pc == 4u || pc == 5u
                || pc == 7u || pc == 9u || pc == 11u,
            "generated notes must stay inside the requested scale");
    }

    PitchMapSettings rise = fit;
    rise.contour = PitchContour::Rise;
    rise.maximumLeapDegrees = 1u;
    rise.preserveEndpoints = false;
    const auto ascending = previewPitchMap(source, 0u, 0u, 7u, rise);
    check(ascending.assignments.size() == 4u
            && ascending.assignments[0u].note == 60u
            && ascending.assignments[1u].note == 62u
            && ascending.assignments[2u].note == 64u
            && ascending.assignments[3u].note == 65u,
        "rise should walk upward through adjacent scale degrees");

    PitchMapSettings transformed = fit;
    transformed.transposeSemitones = 12;
    auto transposed = previewPitchMap(source, 0u, 0u, 7u, transformed);
    check(transposed.assignments.size() == 4u
            && transposed.assignments[0u].note == 72u
            && transposed.assignments[1u].note == 74u
            && transposed.assignments[2u].note == 76u
            && transposed.assignments[3u].note == 79u,
        "transpose should shift the complete fitted contour by exact semitones");

    transformed.transposeSemitones = 0;
    transformed.invertScaleDegrees = true;
    const auto inverted = previewPitchMap(source, 0u, 0u, 7u,
        transformed);
    check(inverted.assignments.size() == 4u
            && inverted.assignments[0u].note == 60u
            && inverted.assignments[1u].note == 59u
            && inverted.assignments[2u].note == 57u
            && inverted.assignments[3u].note == 53u,
        "inversion should mirror intervals around the first pitch on scale degrees");

    transformed.invertScaleDegrees = false;
    transformed.reversePitchOrder = true;
    const auto reversed = previewPitchMap(source, 0u, 0u, 7u,
        transformed);
    check(reversed.assignments.size() == 4u
            && reversed.assignments[0u].note == 67u
            && reversed.assignments[1u].note == 64u
            && reversed.assignments[2u].note == 62u
            && reversed.assignments[3u].note == 60u,
        "reverse should reverse pitches across existing hit positions");

    PitchMapSettings manual = fit;
    manual.contour = PitchContour::Manual;
    const auto handShaped = previewPitchMap(source, 0u, 0u, 7u, manual);
    check(handShaped.changed == 0u
            && handShaped.assignments[0u].note == 60u
            && handShaped.assignments[1u].note == 62u
            && handShaped.assignments[2u].note == 64u
            && handShaped.assignments[3u].note == 67u,
        "manual contour should leave every pitch unchanged until a point or explicit transform moves it");

    if (failures == 0) {
        std::cout << "pitch map tests passed\n";
        return 0;
    }
    return 1;
}
