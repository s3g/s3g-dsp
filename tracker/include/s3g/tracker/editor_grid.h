#pragma once

#include "s3g/tracker/editor_drawing.h"
#include "s3g/tracker/editor_grid_input.h"
#include "s3g/tracker/editor_grid_selection.h"
#include "s3g/tracker/editor_state.h"
#include "s3g/tracker/editor_workspace_layout.h"
#include <string_view>
#include <variant>

// The literal Tracker cell/column contract, shared by the native reference and
// portable editor. No view, event, font or clipboard implementation lives here.
namespace s3g::tracker::editor {
using app::TrackerViewState;
using GridCell = std::variant<NoteCell, ValueCell, FxActionCell, FxValueCell, GateCell>;

const Pattern* playbackFollowPattern(const TrackerViewState* state);
std::string playbackFollowPatternId(const TrackerViewState* state);
std::size_t visibleRows(const TrackerViewState* state);
std::size_t playbackFollowVisibleRows(const TrackerViewState* state);
std::size_t gridFieldCount(bool expanded) noexcept;
double gridFieldStartFraction(bool expanded, std::size_t field) noexcept;
double gridFieldEndFraction(bool expanded, std::size_t field) noexcept;
Rect gridFieldRect(
    double x, double y, double width, double height, bool expanded, std::size_t field) noexcept;
Rect gridLaneChannelRect(double x, double width) noexcept;
Rect gridLaneResyncRect(double x, double width) noexcept;
std::size_t gridFieldAtX(double x, double width, bool expanded) noexcept;
double gridLaneWidth(bool expanded) noexcept;
double gridLaneX(std::size_t lane, double width) noexcept;
double gridLaneFieldX(std::size_t lane, double width) noexcept;
double gridLaneFieldWidth(double width) noexcept;
bool gridLaneAtX(
    double x, std::size_t lanes, bool expanded, std::size_t& lane, double& fieldX) noexcept;
ColumnDefinition* columnForField(Track& track, std::size_t page, std::size_t field) noexcept;
bool gridFieldIsGate(std::size_t field) noexcept;
bool gridFieldIsSequence(std::size_t field) noexcept;
bool gridFieldIsSequenceAction(std::size_t field) noexcept;
std::size_t gridSequencePair(std::size_t field) noexcept;
uint8_t gridClipboardFieldType(std::size_t field) noexcept;
GridCell trackerGridCellAt(const Track& track, std::size_t field, std::size_t row);
void writeTrackerGridCell(Track& track, std::size_t field, std::size_t row, const GridCell& cell);
GridCell blankTrackerGridCell(std::size_t field);
bool trackerGridCellEmpty(const GridCell& cell) noexcept;
bool trackerGridCellsEqual(const GridCell& a, const GridCell& b) noexcept;
std::size_t gridPlaybackRow(
    const TrackerViewState* state, std::size_t lane, std::size_t field) noexcept;
float resolvedVelocity(const Track& track, std::size_t row);
ValueCell resolvedVelocityCell(const Track& track, std::size_t row);
float resolvedFxValue(const Track& track, std::size_t pair, std::size_t row);
FxValueCell resolvedFxValueCell(const Track& track, std::size_t pair, std::size_t row);
bool resolvedSequencerAction(
    const Track& track, std::size_t pair, std::size_t row, SequencerAction& action) noexcept;
bool pairContainsMidiControlChange(const FxPair& pair) noexcept;
bool noteCellIsActivePulse(const NoteCell& cell) noexcept;
Direction nextDirection(Direction direction);
std::string directionMark(Direction direction);
std::string midiNoteName(uint8_t note);
std::string noteText(const NoteCell& cell, bool midi, bool compact = true);
std::string volumeText(const Track& track, std::size_t row, bool compact = true);
std::string fxActionText(const Track& track, std::size_t pair, std::size_t row);
std::string fxValueText(const Track& track, std::size_t pair, std::size_t row, bool compact = true);
std::string gateText(const Track& track, std::size_t row, bool compact = true);
std::string cellText(
    const Track& track, std::size_t row, std::size_t field, bool midi = true, bool compact = true);
std::string trimCellText(std::string_view text);
bool parseCellInteger(std::string_view text, int64_t& value);
bool parseCellDouble(std::string_view text, double& value);
bool parseColumnLengthAndStride(std::string_view text, std::size_t& length, uint32_t& stride);
// Transactional: rejected text must not resize or modify the original track.
bool applyCellText(const TrackerViewState& state, std::string_view source, Track& track,
    std::size_t row, std::size_t field);
}
