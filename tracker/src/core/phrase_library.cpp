#include "s3g/tracker/phrase_library.h"

#include <algorithm>

namespace s3g::tracker {
namespace {

constexpr std::size_t kMaximumTrackerRows = 256u;

template <typename Cell>
Cell cellAtOr(const std::vector<Cell>& cells, std::size_t row,
    const Cell& fallback)
{
    return row < cells.size() ? cells[row] : fallback;
}

bool noteIsEmpty(const NoteCell& cell) noexcept
{
    return cell.state == NoteCellState::Rest;
}

bool valueIsEmpty(const ValueCell& cell) noexcept
{
    return cell.state == ValueCellState::Default;
}

bool gateIsEmpty(const GateCell& cell) noexcept
{
    return cell.voiceCount == 0u;
}

bool actionIsEmpty(const FxActionCell& cell) noexcept
{
    return cell.state == FxActionCellState::Empty;
}

bool fxValueIsEmpty(const FxValueCell& cell) noexcept
{
    return cell.state == FxValueCellState::Previous;
}

template <typename Cell, typename EmptyPredicate>
void placeCells(std::vector<Cell>& destination,
    const std::vector<Cell>& source, std::size_t row,
    std::size_t count, const Cell& fallback, PhrasePlacementMode mode,
    EmptyPredicate empty)
{
    destination.resize(std::max(destination.size(), row + count), fallback);
    for (std::size_t index = 0u; index < count; ++index) {
        const Cell cell = cellAtOr(source, index, fallback);
        if (mode == PhrasePlacementMode::Replace
            || empty(destination[row + index]))
            destination[row + index] = cell;
    }
}

} // namespace

bool PhraseDefinition::empty() const noexcept
{
    const auto note = std::any_of(notes.begin(), notes.end(),
        [](const NoteCell& cell) { return !noteIsEmpty(cell); });
    const auto velocity = std::any_of(velocities.begin(), velocities.end(),
        [](const ValueCell& cell) { return !valueIsEmpty(cell); });
    const auto gate = std::any_of(gates.begin(), gates.end(),
        [](const GateCell& cell) { return !gateIsEmpty(cell); });
    if (note || velocity || gate) return false;
    for (const auto& pair : fxPairs) {
        if (std::any_of(pair.actions.begin(), pair.actions.end(),
                [](const FxActionCell& cell) { return !actionIsEmpty(cell); })
            || std::any_of(pair.values.begin(), pair.values.end(),
                [](const FxValueCell& cell) { return !fxValueIsEmpty(cell); }))
            return false;
    }
    return true;
}

PhraseDefinition makeBlankPhrase(std::size_t length)
{
    PhraseDefinition phrase;
    phrase.length = std::clamp(length, kMinimumPhraseRows,
        kMaximumPhraseRows);
    phrase.notes.resize(phrase.length, NoteCell::rest());
    phrase.velocities.resize(phrase.length, ValueCell::defaultValue());
    phrase.gates.resize(phrase.length, GateCell::defaultValue());
    for (auto& pair : phrase.fxPairs) {
        pair.actions.resize(phrase.length, FxActionCell::empty());
        pair.values.resize(phrase.length, FxValueCell::previous());
        pair.actionColumn.length = phrase.length;
        pair.valueColumn.length = phrase.length;
    }
    return phrase;
}

bool capturePhrase(const Track& source, std::size_t firstRow,
    std::size_t lastRow, PhraseDefinition& destination) noexcept
{
    if (lastRow < firstRow) std::swap(firstRow, lastRow);
    if (firstRow >= kMaximumTrackerRows) return false;
    lastRow = std::min(lastRow, kMaximumTrackerRows - 1u);
    const auto requested = lastRow - firstRow + 1u;
    if (requested > kMaximumPhraseRows) return false;
    const auto length = std::max(requested, kMinimumPhraseRows);
    PhraseDefinition candidate = makeBlankPhrase(length);
    candidate.name = destination.name;
    candidate.previewMidiChannel = std::clamp<uint8_t>(
        source.midiChannel, 1u, 16u);
    for (std::size_t row = 0u; row < requested; ++row) {
        candidate.notes[row] = cellAtOr(source.notes, firstRow + row,
            NoteCell::rest());
        candidate.velocities[row] = cellAtOr(source.velocities,
            firstRow + row, ValueCell::defaultValue());
        candidate.gates[row] = cellAtOr(source.gates,
            firstRow + row, GateCell::defaultValue());
        for (std::size_t pair = 0u; pair < kFxPairCount; ++pair) {
            candidate.fxPairs[pair].actions[row] = cellAtOr(
                source.fxPairs[pair].actions, firstRow + row,
                FxActionCell::empty());
            candidate.fxPairs[pair].values[row] = cellAtOr(
                source.fxPairs[pair].values, firstRow + row,
                FxValueCell::previous());
        }
    }
    destination = std::move(candidate);
    return true;
}

bool capturePhrase(const Pattern& source, std::size_t track,
    std::size_t firstRow, std::size_t lastRow,
    PhraseDefinition& destination) noexcept
{
    if (track >= source.tracks.size()) return false;
    PhraseDefinition candidate = destination;
    if (!capturePhrase(source.tracks[track], firstRow, lastRow, candidate))
        return false;
    destination = std::move(candidate);
    return true;
}

bool placePhrase(Track& destination, const PhraseDefinition& phrase,
    std::size_t destinationRow, PhrasePlacementMode mode) noexcept
{
    if (phrase.length < kMinimumPhraseRows
        || phrase.length > kMaximumPhraseRows
        || destinationRow >= kMaximumTrackerRows
        || phrase.length > kMaximumTrackerRows - destinationRow) return false;

    const auto end = destinationRow + phrase.length;
    placeCells(destination.notes, phrase.notes, destinationRow,
        phrase.length, NoteCell::rest(), mode, noteIsEmpty);
    placeCells(destination.velocities, phrase.velocities, destinationRow,
        phrase.length, ValueCell::defaultValue(), mode, valueIsEmpty);
    placeCells(destination.gates, phrase.gates, destinationRow,
        phrase.length, GateCell::defaultValue(), mode, gateIsEmpty);
    destination.noteColumn.length = std::max(destination.noteColumn.length, end);
    destination.velocityColumn.length = std::max(
        destination.velocityColumn.length, end);
    destination.gateColumn.length = std::max(destination.gateColumn.length, end);
    for (std::size_t pair = 0u; pair < kFxPairCount; ++pair) {
        placeCells(destination.fxPairs[pair].actions,
            phrase.fxPairs[pair].actions, destinationRow, phrase.length,
            FxActionCell::empty(), mode, actionIsEmpty);
        placeCells(destination.fxPairs[pair].values,
            phrase.fxPairs[pair].values, destinationRow, phrase.length,
            FxValueCell::previous(), mode, fxValueIsEmpty);
        destination.fxPairs[pair].actionColumn.length = std::max(
            destination.fxPairs[pair].actionColumn.length, end);
        destination.fxPairs[pair].valueColumn.length = std::max(
            destination.fxPairs[pair].valueColumn.length, end);
    }
    return true;
}

bool placePhrase(Pattern& destination, std::size_t track,
    const PhraseDefinition& phrase, std::size_t destinationRow,
    PhrasePlacementMode mode) noexcept
{
    if (track >= destination.tracks.size()) return false;
    Pattern candidate = destination;
    if (!placePhrase(candidate.tracks[track], phrase, destinationRow, mode))
        return false;
    destination = std::move(candidate);
    return true;
}

} // namespace s3g::tracker
