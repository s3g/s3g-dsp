#include "s3g/tracker/editor_grid_controller.h"
#include <iostream>

using namespace s3g::tracker;
using namespace s3g::tracker::editor;

// No AppKit, VSTGUI, window server or system clipboard is needed here.
int main()
{
    int failures = 0, publications = 0;
    auto check = [&](bool value, const char* message) {
        if (!value) {
            ++failures;
            std::cerr << message << '\n';
        }
    };
    app::TrackerViewState state;
    state.session.pattern = state.patternBank.entries.front().pattern;
    state.session.pattern.tracks.resize(2);
    state.session.pattern.visibleRows = 32;
    state.sequenceColumnsExpanded = true;
    app::WorkspaceCallbacks callbacks;
    callbacks.patternChanged = [&] { ++publications; };
    std::string clipboard;
    uint64_t revision = 0;
    GridServices services;
    services.readClipboard = [&] { return clipboard; };
    services.writeClipboard = [&](const std::string& value) {
        clipboard = value;
        ++revision;
    };
    services.clipboardRevision = [&] { return revision; };
    GridController grid(state, callbacks, services);

    auto& source = state.session.pattern.tracks[0];
    writeTrackerGridCell(source, 0, 0, NoteCell::withNote(60));
    writeTrackerGridCell(source, 1, 0, ValueCell::withValue(.123456f));
    grid.select({ 0, 1, 0 });
    check(grid.copy(), "copy rejected a value cell");
    const auto copied = clipboard;
    grid.select({ 1, 1, 0 });
    check(grid.paste(clipboard), "typed paste rejected matching column");
    check(state.session.pattern.tracks[1].velocities[0].normalized == .123456f,
        "typed clipboard rounded the original value");
    ++revision; // Another application copied identical text, not our typed cell.
    grid.select({ 1, 1, 1 });
    check(grid.paste(copied), "external plain-text paste failed");
    check(state.session.pattern.tracks[1].velocities[1].normalized == .123f,
        "stale typed clipboard survived a system clipboard replacement");

    grid.select({ 0, 0, 0 });
    const auto before = state.session.pattern.tracks[0].notes[0];
    const int publicationsBefore = publications;
    check(!grid.paste("72\tinvalid-value"), "partial invalid paste was accepted");
    check(trackerGridCellsEqual(before, state.session.pattern.tracks[0].notes[0])
            && publications == publicationsBefore,
        "rejected paste partially modified/published the pattern");
    grid.select({ 0, 0, 0 });
    check(grid.copy(), "note copy failed");
    grid.select({ 1, 1, 2 });
    check(!grid.paste(clipboard), "typed note clipboard accepted a value destination");

    const auto bankA = NoteCell::withBurst(0, 1);
    const auto bankB = NoteCell::withBurst(0, 2);
    check(!trackerGridCellsEqual(bankA, bankB), "burst equality ignored bank identity");
    writeTrackerGridCell(state.session.pattern.tracks[0], 0, 5, bankB);
    grid.select({ 0, 0, 5 });
    check(grid.copy(), "burst copy failed");
    grid.select({ 1, 0, 5 });
    check(grid.paste(clipboard)
            && trackerGridCellsEqual(bankB, state.session.pattern.tracks[1].notes[5]),
        "typed paste lost qualified burst identity");

    const auto oldRows = state.session.pattern.visibleRows;
    grid.copyRows(5, 1);
    grid.insertRowsAt(6, 1, true);
    check(state.session.pattern.visibleRows == oldRows + 1
            && trackerGridCellsEqual(bankB, state.session.pattern.tracks[1].notes[6]),
        "structural row clipboard lost non-visible column data or bank identity");
    grid.deleteRows(6, 1);
    check(state.session.pattern.visibleRows == oldRows, "row deletion did not restore length");

    grid.select({ 0, 0, 0 });
    auto edit = grid.textEdit(GridEditKind::Length);
    check(grid.commitText(edit, "24x2"), "length/stride edit rejected native grammar");
    check(state.session.pattern.tracks[0].noteColumn.length == 24
            && state.session.pattern.tracks[0].noteColumn.stride == 2,
        "length/stride edit wrote wrong values");
    edit = grid.textEdit(GridEditKind::ReadStart);
    check(grid.commitText(edit, "24") && state.session.pattern.tracks[0].noteColumn.phase == 23,
        "read-start edit did not convert one-based rows");
    check(!grid.commitText(edit, "25"), "read start exceeded column length");

    state.songPlaybackActive = true;
    const auto readonlyPublications = publications;
    check(!grid.commitText(grid.textEdit(), "72") && !grid.paste("72") && !grid.clearCells(),
        "Song follow accepted a persistent edit");
    grid.insertRowsAt(0, 1);
    grid.deleteRows(0, 1);
    check(publications == readonlyPublications, "Song follow published a persistent edit");
    std::cout << "Tracker portable grid: " << (failures ? "FAILED" : "ok") << '\n';
    return failures ? 1 : 0;
}
