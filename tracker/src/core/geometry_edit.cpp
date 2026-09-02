#include "s3g/tracker/geometry_edit.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace s3g::tracker {
namespace {

std::size_t activeLength(const Track& track) noexcept
{
    return std::clamp<std::size_t>(track.noteColumn.length, 1u, 256u);
}

bool sameNoteCell(const NoteCell& left, const NoteCell& right) noexcept
{
    return left.state == right.state && left.note == right.note;
}

} // namespace

bool geometryCellIsHit(const Track& track, std::size_t row) noexcept
{
    if (row >= activeLength(track) || row >= track.notes.size()) return false;
    const auto state = track.notes[row].state;
    return state == NoteCellState::Note
        || state == NoteCellState::RetriggerPrevious;
}

std::size_t geometryHitCount(const Track& track) noexcept
{
    const auto length = activeLength(track);
    std::size_t count = 0u;
    for (std::size_t row = 0u; row < length; ++row)
        if (geometryCellIsHit(track, row)) ++count;
    return count;
}

bool setGeometryHit(Track& track, std::size_t row, bool hit,
    uint8_t defaultNote)
{
    if (row >= 256u) return false;
    track.noteColumn.length = std::max(track.noteColumn.length, row + 1u);
    track.notes.resize(std::max(track.notes.size(), row + 1u),
        NoteCell::rest());
    const NoteCell replacement = hit
        ? NoteCell::withNote(defaultNote) : NoteCell::rest();
    if (sameNoteCell(track.notes[row], replacement)) return false;
    track.notes[row] = replacement;
    return true;
}

bool setGeometryVelocity(Track& track, std::size_t row, float normalized)
{
    if (row >= 256u) return false;
    normalized = std::clamp(normalized, 0.0f, 1.0f);
    track.velocityColumn.length = std::max(
        track.velocityColumn.length, row + 1u);
    track.velocities.resize(std::max(track.velocities.size(), row + 1u),
        ValueCell::defaultValue());
    auto& cell = track.velocities[row];
    if (cell.state == ValueCellState::Value
        && std::abs(cell.normalized - normalized) < 0.0001f) return false;
    cell = ValueCell::withValue(normalized);
    return true;
}

bool rotateGeometryPhase(Track& track, int delta) noexcept
{
    const auto length = activeLength(track);
    const auto oldPhase = track.noteColumn.phase % length;
    const auto signedLength = static_cast<long long>(length);
    const auto wrapped = (static_cast<long long>(oldPhase)
        + static_cast<long long>(delta) % signedLength + signedLength)
        % signedLength;
    track.noteColumn.phase = static_cast<std::size_t>(wrapped);
    return track.noteColumn.phase != oldPhase;
}

bool setGeometryDensity(Track& track, std::size_t pulses,
    uint8_t defaultNote)
{
    const auto length = activeLength(track);
    pulses = std::min(pulses, length);
    std::vector<NoteCell> replacement(length, NoteCell::rest());
    if (pulses > 0u) {
        for (std::size_t pulse = 0u; pulse < pulses; ++pulse) {
            const auto row = pulse * length / pulses;
            uint8_t note = defaultNote;
            if (row < track.notes.size()
                && track.notes[row].state == NoteCellState::Note)
                note = track.notes[row].note;
            replacement[row] = NoteCell::withNote(note);
        }
    }
    track.notes.resize(std::max(track.notes.size(), length),
        NoteCell::rest());
    bool changed = false;
    for (std::size_t row = 0u; row < length; ++row) {
        changed |= !sameNoteCell(track.notes[row], replacement[row]);
        track.notes[row] = replacement[row];
    }
    return changed;
}

bool reverseGeometry(Track& track)
{
    const auto length = activeLength(track);
    track.notes.resize(std::max(track.notes.size(), length),
        NoteCell::rest());
    const auto original = track.notes;
    bool changed = false;
    for (std::size_t row = 0u; row < length; ++row) {
        const auto& replacement = original[length - 1u - row];
        changed |= !sameNoteCell(track.notes[row], replacement);
        track.notes[row] = replacement;
    }
    return changed;
}

bool reflectGeometry(Track& track, std::size_t pivot)
{
    const auto length = activeLength(track);
    pivot %= length;
    track.notes.resize(std::max(track.notes.size(), length),
        NoteCell::rest());
    const auto original = track.notes;
    bool changed = false;
    for (std::size_t row = 0u; row < length; ++row) {
        const auto source = (2u * pivot + length - row) % length;
        changed |= !sameNoteCell(track.notes[row], original[source]);
        track.notes[row] = original[source];
    }
    return changed;
}

bool morphGeometry(Track& track, const Track& target, float amount,
    uint8_t defaultNote)
{
    const auto length = activeLength(track);
    const auto targetLength = activeLength(target);
    amount = std::clamp(amount, 0.0f, 1.0f);
    track.notes.resize(std::max(track.notes.size(), length),
        NoteCell::rest());
    const auto original = track.notes;
    bool changed = false;
    for (std::size_t row = 0u; row < length; ++row) {
        const float threshold = static_cast<float>((row * 37u + 13u)
            % length) / static_cast<float>(length);
        if (amount < 1.0f && threshold >= amount) continue;
        const auto targetRow = row * targetLength / length;
        NoteCell replacement = NoteCell::rest();
        if (geometryCellIsHit(target, targetRow)) {
            replacement = target.notes[targetRow].state == NoteCellState::Note
                ? target.notes[targetRow]
                : NoteCell::withNote(defaultNote);
        }
        changed |= !sameNoteCell(original[row], replacement);
        track.notes[row] = replacement;
    }
    return changed;
}

} // namespace s3g::tracker
