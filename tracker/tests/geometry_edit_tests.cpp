#include "s3g/tracker/geometry_edit.h"

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

s3g::tracker::Track trackWithLength(std::size_t length)
{
    s3g::tracker::Track track;
    track.noteColumn.length = length;
    track.velocityColumn.length = length;
    track.notes.resize(length, s3g::tracker::NoteCell::rest());
    return track;
}

} // namespace

int main()
{
    using namespace s3g::tracker;
    auto track = trackWithLength(8u);
    check(setGeometryHit(track, 2u, true, 60u)
            && geometryCellIsHit(track, 2u)
            && geometryHitCount(track) == 1u,
        "paint should author one note hit");
    check(!setGeometryHit(track, 2u, true, 60u),
        "painting the same note should be idempotent");
    check(setGeometryHit(track, 2u, false, 60u)
            && !geometryCellIsHit(track, 2u),
        "erase should write a rest");

    check(setGeometryVelocity(track, 3u, 1.5f)
            && track.velocities[3u].state == ValueCellState::Value
            && std::abs(track.velocities[3u].normalized - 1.0f) < 0.0001f,
        "velocity paint should clamp and author a value cell");

    track.velocityColumn.length = 3u;
    track.noteColumn.phase = 7u;
    track.velocityColumn.phase = 2u;
    check(setGeometryNoteLength(track, 6u, false)
            && track.noteColumn.length == 6u
            && track.noteColumn.phase == 1u
            && track.velocityColumn.length == 3u,
        "unlinked note length should retain the independent velocity cycle");
    check(setGeometryNoteLength(track, 6u, true)
            && track.velocityColumn.length == 6u
            && track.velocityColumn.phase == 2u
            && track.velocities.size() >= 6u,
        "linked note length should align the velocity cycle and storage");
    check(!setGeometryNoteLength(track, 0u, true),
        "geometry length should reject an invalid zero cycle");

    track = trackWithLength(8u);
    track.noteColumn.phase = 3u;
    track.notes[0u] = NoteCell::withNote(60u);
    track.notes[6u] = NoteCell::withNote(62u);
    check(rotateGeometryRows(track, 2)
            && track.notes[2u].note == 60u
            && track.notes[0u].note == 62u
            && track.noteColumn.phase == 3u,
        "row rotation should move authored cells without changing playback phase");
    check(rotateGeometryRows(track, -2)
            && track.notes[0u].note == 60u
            && track.notes[6u].note == 62u,
        "row rotation should wrap backwards and restore the source cycle");

    track.notes[0u] = NoteCell::withNote(99u);
    check(setGeometryDensity(track, 3u, 36u)
            && geometryHitCount(track) == 3u
            && geometryCellIsHit(track, 0u)
            && geometryCellIsHit(track, 2u)
            && geometryCellIsHit(track, 5u)
            && track.notes[0u].note == 36u
            && track.notes[2u].note == 36u
            && track.notes[5u].note == 36u,
        "density should distribute exact hits using only the lane default pitch");

    const auto beforeReverse = track.notes;
    check(reverseGeometry(track)
            && track.notes[7u].state == beforeReverse[0u].state,
        "reverse should mirror the active note cycle");
    check(reverseGeometry(track),
        "reversing an asymmetric cycle twice should report both changes");
    check(track.notes[0u].state == beforeReverse[0u].state,
        "double reverse should restore the source cycle");

    track = trackWithLength(8u);
    track.notes[1u] = NoteCell::withNote(48u);
    check(reflectGeometry(track, 0u)
            && geometryCellIsHit(track, 7u)
            && !geometryCellIsHit(track, 1u),
        "reflect should mirror notes around the selected pivot");

    auto target = trackWithLength(4u);
    target.notes[0u] = NoteCell::withNote(72u);
    target.notes[2u] = NoteCell::withNote(74u);
    auto source = trackWithLength(8u);
    check(morphGeometry(source, target, 1.0f, 60u)
            && geometryHitCount(source) == 4u
            && source.notes[0u].note == 72u
            && source.notes[4u].note == 74u,
        "full morph should resample the target cycle and retain its pitches");
    const auto stable = source.notes;
    check(!morphGeometry(source, target, 1.0f, 60u)
            && source.notes[4u].note == stable[4u].note,
        "repeating the same morph should be stable");

    if (failures == 0) std::cout << "geometry edit tests passed\n";
    return failures == 0 ? 0 : 1;
}
