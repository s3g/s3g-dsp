#include "s3g/tracker/editor_grid_controller.h"
#include "s3g/tracker/fx_catalog.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <sstream>

namespace s3g::tracker::editor {

std::string GridController::selectionStatistics()
{
    if (!trackerState || trackerState->session.pattern.tracks.empty())
        return {};
    const auto range = effectiveGridSelection();
    const auto fields = gridFieldCount(trackerState->sequenceColumnsExpanded);
    const auto first = selection.active ? selection.firstColumn(fields)
                                        : range.firstTrack * fields + range.firstField;
    const auto last = selection.active ? selection.lastColumn(fields) : first;
    std::size_t hits = 0, voices = 0, values = 0, actions = 0, velocityCount = 0;
    double velocityTotal = 0;
    uint8_t low = 127, high = 0;
    for (auto row = range.firstRow; row <= range.lastRow; ++row)
        for (auto column = first; column <= last; ++column) {
            const auto cell = trackerGridCellAt(
                trackerState->session.pattern.tracks[column / fields], column % fields, row);
            if (const auto* note = std::get_if<NoteCell>(&cell)) {
                if (note->state == NoteCellState::Note) {
                    ++hits;
                    voices += note->noteVoiceCount();
                    for (std::size_t voice = 0; voice < note->noteVoiceCount(); ++voice) {
                        low = std::min(low, note->noteVoice(voice));
                        high = std::max(high, note->noteVoice(voice));
                    }
                }
            } else if (const auto* value = std::get_if<ValueCell>(&cell)) {
                if (value->state == ValueCellState::Value) {
                    ++values;
                    for (std::size_t voice = 0; voice < value->valueVoiceCount(); ++voice) {
                        velocityTotal += value->valueVoice(voice);
                        ++velocityCount;
                    }
                }
            } else if (const auto* action = std::get_if<FxActionCell>(&cell)) {
                if (action->state != FxActionCellState::Empty)
                    ++actions;
            } else if (const auto* gate = std::get_if<GateCell>(&cell)) {
                if (gate->voiceCount > 0)
                    ++values;
            } else if (std::get<FxValueCell>(cell).state == FxValueCellState::Value)
                ++values;
        }
    auto counted = [](std::size_t n, const char* unit) {
        return std::to_string(n) + " " + unit + (n == 1 ? "" : "s");
    };
    auto pitch = [](uint8_t n) {
        auto number = std::to_string(n);
        return midiNoteName(n) + " " + std::string(3 - number.size(), '0') + number;
    };
    auto result = counted(range.rowCount(), "row") + " × " + counted(last - first + 1, "column")
        + "\n" + counted(hits, "note onset") + " · " + counted(voices, "voice") + "\nPitch range "
        + (voices ? pitch(low) + " – " + pitch(high) : "—") + "\n"
        + counted(values, "written value") + " · " + counted(actions, "SEQ action");
    if (velocityCount) {
        char text[80];
        std::snprintf(text, sizeof(text), "\nAverage velocity %.1f%%",
            velocityTotal / double(velocityCount) * 100);
        result += text;
    }
    return result;
}
using app::gridClipboardColumn;

GridController::GridController(
    app::TrackerViewState& state, app::WorkspaceCallbacks& callbacks, GridServices services)
    : trackerState(&state)
    , callbacks_(callbacks)
    , services_(std::move(services))
{
}

bool GridController::editable() const
{
    return !trackerState->songPlaybackActive && !trackerState->session.pattern.tracks.empty();
}

void GridController::reject(const std::string& message)
{
    if (services_.invalidEdit)
        services_.invalidEdit();
    else if (services_.message)
        services_.message(message);
}

void GridController::patternChanged()
{
    if (callbacks_.patternChanged)
        callbacks_.patternChanged();
    if (services_.invalidate)
        services_.invalidate();
}

void GridController::selectionChanged()
{
    if (callbacks_.selectionChanged)
        callbacks_.selectionChanged();
    const auto& s = trackerState->session;
    const double width = gridLaneWidth(trackerState->sequenceColumnsExpanded);
    if (services_.reveal)
        services_.reveal(
            { std::max(0., gridLaneX(s.selectedTrack, width) - app::kTrackerRowNumberWidth),
                app::kTrackerGridHeaderHeight + double(s.selectedRow) * app::kTrackerGridRowHeight,
                width + app::kTrackerRowNumberWidth, app::kTrackerGridRowHeight });
    if (services_.invalidate)
        services_.invalidate();
}

app::GridSelectionRange GridController::effectiveGridSelection()
{
    const auto& s = trackerState->session;
    if (selection.active) {
        const auto r = selection.range();
        if (r.lastTrack >= s.pattern.tracks.size()
            || r.lastField >= gridFieldCount(trackerState->sequenceColumnsExpanded)
            || r.lastRow >= visibleRows(trackerState))
            selection.active = false;
    }
    auto range = selection.active
        ? selection.range()
        : app::GridSelectionRange { 0, s.selectedTrack, s.selectedTrack, s.selectedField,
              s.selectedField, s.selectedRow, s.selectedRow };
    const auto lanes = s.pattern.tracks.size();
    auto fields = gridFieldCount(trackerState->sequenceColumnsExpanded);
    range.firstTrack = std::min(range.firstTrack, lanes ? lanes - 1 : 0);
    range.lastTrack = std::clamp(range.lastTrack, range.firstTrack, lanes ? lanes - 1 : 0);
    range.firstField = std::min(range.firstField, fields - 1);
    range.lastField = std::clamp(range.lastField, range.firstField, fields - 1);
    range.firstRow = std::min(range.firstRow, visibleRows(trackerState) - 1);
    range.lastRow = std::clamp(range.lastRow, range.firstRow, visibleRows(trackerState) - 1);
    return range;
}

void GridController::clearGridSelection()
{
    selection.active = false;
    selectingWholeRows = false;
    if (services_.invalidate)
        services_.invalidate();
}

void GridController::select(GridAddress a, bool extend)
{
    const auto* pattern = playbackFollowPattern(trackerState);
    if (!pattern || pattern->tracks.empty())
        return;
    auto& s = trackerState->session;
    a.track = std::min(a.track, pattern->tracks.size() - 1);
    a.field = std::min(a.field, gridFieldCount(trackerState->sequenceColumnsExpanded) - 1);
    a.row = std::min(a.row, playbackFollowVisibleRows(trackerState) - 1);
    if (extend && editable()) {
        if (!selection.active) {
            selection.anchorTrack = s.selectedTrack;
            selection.anchorField = s.selectedField;
            selection.anchorRow = s.selectedRow;
        }
        selection.page = 0;
        selection.active = true;
        selection.focusTrack = a.track;
        selection.focusField = a.field;
        selection.focusRow = a.row;
    } else
        clearGridSelection();
    s.selectedTrack = a.track;
    s.selectedField = a.field;
    s.selectedRow = a.row;
    s.selectedPage = 0;
    selectionChanged();
}

void GridController::selectAll()
{
    if (!editable())
        return;
    selection = { true, 0, 0, 0, 0, trackerState->session.pattern.tracks.size() - 1,
        gridFieldCount(trackerState->sequenceColumnsExpanded) - 1, visibleRows(trackerState) - 1 };
    if (services_.invalidate)
        services_.invalidate();
}

void GridController::selectWholeRows(std::size_t anchor, std::size_t row)
{
    if (!editable())
        return;
    const auto rows = visibleRows(trackerState);
    selection = { true, 0, 0, 0, std::min(anchor, rows - 1),
        trackerState->session.pattern.tracks.size() - 1,
        gridFieldCount(trackerState->sequenceColumnsExpanded) - 1, std::min(row, rows - 1) };
    selectingWholeRows = true;
    trackerState->session.selectedRow = selection.focusRow;
    selectionChanged();
}

void GridController::selectLoop(std::size_t anchor, std::size_t row)
{
    if (!editable())
        return;
    anchor = std::min(anchor, visibleRows(trackerState) - 1);
    row = std::min(row, visibleRows(trackerState) - 1);
    auto& transport = trackerState->session.transport;
    transport.loopStartRow = static_cast<uint32_t>(std::min(anchor, row));
    transport.loopEndRow = static_cast<uint32_t>(std::max(anchor, row) + 1);
    if (callbacks_.transportChanged)
        callbacks_.transportChanged();
    select({ trackerState->session.selectedTrack, trackerState->session.selectedField, row });
}

Rect GridController::cellRect(GridAddress a) const
{
    auto w = gridLaneWidth(trackerState->sequenceColumnsExpanded);
    return gridFieldRect(gridLaneFieldX(a.track, w),
        app::kTrackerGridHeaderHeight + double(a.row) * app::kTrackerGridRowHeight,
        gridLaneFieldWidth(w), app::kTrackerGridRowHeight, trackerState->sequenceColumnsExpanded,
        a.field);
}

std::optional<GridAddress> GridController::addressAt(Point p, bool clampRows) const
{
    auto* pattern = playbackFollowPattern(trackerState);
    if (!pattern)
        return {};
    GridAddress a;
    double local = 0;
    if (!gridLaneAtX(
            p.x, pattern->tracks.size(), trackerState->sequenceColumnsExpanded, a.track, local))
        return {};
    auto row = std::floor((p.y - app::kTrackerGridHeaderHeight) / app::kTrackerGridRowHeight);
    if (!clampRows && (row < 0 || row >= double(playbackFollowVisibleRows(trackerState))))
        return {};
    a.row = static_cast<std::size_t>(
        std::clamp(row, 0., double(playbackFollowVisibleRows(trackerState) - 1)));
    a.field = gridFieldAtX(local,
        gridLaneFieldWidth(gridLaneWidth(trackerState->sequenceColumnsExpanded)),
        trackerState->sequenceColumnsExpanded);
    return a;
}

GridTextEdit GridController::textEdit(GridEditKind kind) const
{
    const auto& s = trackerState->session;
    GridTextEdit edit { { s.selectedTrack, s.selectedField, s.selectedRow }, kind, {}, {} };
    if (s.selectedTrack >= s.pattern.tracks.size())
        return edit;
    const auto& track = s.pattern.tracks[s.selectedTrack];
    edit.bounds = cellRect(edit.address);
    if (kind == GridEditKind::Cell)
        edit.text = cellText(
            track, s.selectedRow, s.selectedField, trackerState->showMidiNoteValues, false);
    else if (kind == GridEditKind::TrackName) {
        edit.text = track.name;
        const auto w = gridLaneWidth(trackerState->sequenceColumnsExpanded);
        edit.bounds = { gridLaneFieldX(s.selectedTrack, w) + 4, 3,
            std::max(44., gridLaneFieldWidth(w) - 92.), 18 };
    } else {
        // Metadata reads never mutate the underlying column.
        const auto& column = *columnForField(const_cast<Track&>(track), 0, s.selectedField);
        edit.bounds.y = kind == GridEditKind::Length ? 34. : 47.;
        edit.bounds.height = 13;
        edit.text = kind == GridEditKind::Length
            ? std::to_string(column.length)
                + (column.stride == 1 ? "" : "x" + std::to_string(column.stride))
            : std::to_string(column.phase % std::max<std::size_t>(1, column.length) + 1);
    }
    return edit;
}

bool GridController::commitText(const GridTextEdit& edit, std::string_view text)
{
    if (!editable())
        return false;
    auto& s = trackerState->session;
    const auto a = edit.address;
    if (a.track >= s.pattern.tracks.size()
        || a.field >= gridFieldCount(trackerState->sequenceColumnsExpanded))
        return false;
    auto candidate = s.pattern.tracks[a.track];
    if (edit.kind == GridEditKind::Cell) {
        if (!applyCellText(*trackerState, text, candidate, a.row, a.field))
            return false;
        s.pattern.visibleRows = std::max(s.pattern.visibleRows, a.row + 1);
        s.selectedRow = std::min(a.row + 1, visibleRows(trackerState) - 1);
    } else if (edit.kind == GridEditKind::TrackName) {
        auto name = trimCellText(text);
        if (name.empty())
            return false;
        if (name.size() > 64) {
            auto end = std::size_t(64);
            while (end && (static_cast<unsigned char>(name[end]) & 0xc0u) == 0x80u)
                --end;
            name.resize(end);
        }
        candidate.name = std::move(name);
    } else if (edit.kind == GridEditKind::Length) {
        std::size_t length = 0;
        uint32_t stride = 0;
        if (!parseColumnLengthAndStride(text, length, stride))
            return false;
        if (a.field == 0)
            candidate.notes.resize(std::max(length, candidate.notes.size()), NoteCell::rest());
        else if (a.field == 1)
            candidate.velocities.resize(
                std::max(length, candidate.velocities.size()), ValueCell::defaultValue());
        else if (a.field == 6)
            candidate.gates.resize(
                std::max(length, candidate.gates.size()), GateCell::defaultValue());
        else {
            auto& pair = candidate.fxPairs[gridSequencePair(a.field)];
            if (gridFieldIsSequenceAction(a.field))
                pair.actions.resize(std::max(length, pair.actions.size()), FxActionCell::empty());
            else
                pair.values.resize(std::max(length, pair.values.size()), FxValueCell::previous());
        }
        auto* column = columnForField(candidate, 0, a.field);
        column->length = length;
        column->stride = stride;
        column->phase %= length;
        s.pattern.visibleRows = std::max(s.pattern.visibleRows, length);
    } else {
        int64_t row = 0;
        auto* column = columnForField(candidate, 0, a.field);
        if (!parseCellInteger(text, row) || row < 1
            || uint64_t(row) > std::max<std::size_t>(1, column->length))
            return false;
        column->phase = static_cast<std::size_t>(row - 1);
    }
    s.pattern.tracks[a.track] = std::move(candidate);
    patternChanged();
    return true;
}

bool GridController::clearCells()
{
    if (!editable())
        return false;
    auto range = effectiveGridSelection();
    auto fields = gridFieldCount(trackerState->sequenceColumnsExpanded);
    auto first = selection.active ? selection.firstColumn(fields)
                                  : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    auto last = selection.active ? selection.lastColumn(fields) : first;
    auto candidate = trackerState->session.pattern;
    if (last >= candidate.tracks.size() * fields)
        return false;
    for (auto row = range.firstRow; row <= range.lastRow; ++row)
        for (auto col = first; col <= last; ++col)
            writeTrackerGridCell(candidate.tracks[col / fields], col % fields, row,
                blankTrackerGridCell(col % fields));
    candidate.visibleRows = std::max(candidate.visibleRows, range.lastRow + 1);
    trackerState->session.pattern = std::move(candidate);
    patternChanged();
    return true;
}

bool GridController::copy()
{
    if (!editable())
        return false;
    auto r = effectiveGridSelection();
    auto fields = gridFieldCount(trackerState->sequenceColumnsExpanded);
    auto first = selection.active ? selection.firstColumn(fields)
                                  : gridClipboardColumn(r.firstTrack, r.firstField, fields);
    auto last = selection.active ? selection.lastColumn(fields) : first;
    if (last >= trackerState->session.pattern.tracks.size() * fields)
        return false;
    copiedClipboardText.clear();
    copiedColumnTypes.clear();
    copiedGridCells.clear();
    copiedRowCount = r.rowCount();
    for (auto col = first; col <= last; ++col)
        copiedColumnTypes.push_back(gridClipboardFieldType(col % fields));
    for (auto row = r.firstRow; row <= r.lastRow; ++row) {
        if (row > r.firstRow)
            copiedClipboardText += '\n';
        for (auto col = first; col <= last; ++col) {
            if (col > first)
                copiedClipboardText += '\t';
            auto& track = trackerState->session.pattern.tracks[col / fields];
            copiedGridCells.push_back(trackerGridCellAt(track, col % fields, row));
            copiedClipboardText += cellText(track, row, col % fields, true, false);
        }
    }
    if (services_.writeClipboard)
        services_.writeClipboard(copiedClipboardText);
    copiedRevision_ = services_.clipboardRevision ? services_.clipboardRevision() : 0;
    return true;
}

bool GridController::paste(std::string_view source)
{
    if (!editable())
        return false;
    std::string normalized;
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (source[i] == '\r') {
            normalized += '\n';
            if (i + 1 < source.size() && source[i + 1] == '\n')
                ++i;
        } else
            normalized += source[i];
    }
    while (!normalized.empty() && normalized.back() == '\n')
        normalized.pop_back();
    std::vector<std::vector<std::string>> rows(1);
    rows.back().emplace_back();
    for (auto c : normalized) {
        if (c == '\n')
            rows.emplace_back(1);
        else if (c == '\t')
            rows.back().emplace_back();
        else
            rows.back().back() += c;
    }
    std::size_t widest = 0;
    for (const auto& row : rows)
        widest = std::max(widest, row.size());
    auto r = effectiveGridSelection();
    auto fields = gridFieldCount(trackerState->sequenceColumnsExpanded);
    auto first = selection.active ? selection.firstColumn(fields)
                                  : gridClipboardColumn(r.firstTrack, r.firstField, fields);
    const bool fill = selection.active && rows.size() == 1 && widest == 1;
    bool internal = source == copiedClipboardText && copiedRowCount == rows.size()
        && (!services_.clipboardRevision || copiedRevision_ == services_.clipboardRevision())
        && copiedColumnTypes.size() == widest && copiedGridCells.size() == rows.size() * widest;
    auto candidate = trackerState->session.pattern;
    auto last = fill ? selection.lastColumn(fields) : first + widest - 1;
    auto lastRow = fill ? r.lastRow : r.firstRow + rows.size() - 1;
    if (rows.size() > 256 || lastRow >= 256 || last >= candidate.tracks.size() * fields) {
        reject();
        return false;
    }
    if (internal)
        for (std::size_t i = 0; i < widest; ++i)
            if (copiedColumnTypes[i] != gridClipboardFieldType((first + i) % fields)) {
                reject();
                return false;
            }
    for (auto row = r.firstRow; row <= lastRow; ++row) {
        auto offset = fill ? 0 : row - r.firstRow;
        auto count = fill ? last - first + 1 : rows[offset].size();
        for (std::size_t i = 0; i < count; ++i) {
            auto col = first + i;
            if (internal && !fill)
                writeTrackerGridCell(candidate.tracks[col / fields], col % fields, row,
                    copiedGridCells[offset * widest + i]);
            else if (!applyCellText(*trackerState, rows[offset][fill ? 0 : i],
                         candidate.tracks[col / fields], row, col % fields)) {
                reject();
                return false;
            }
        }
    }
    candidate.visibleRows = std::max(candidate.visibleRows, lastRow + 1);
    trackerState->session.pattern = std::move(candidate);
    patternChanged();
    return true;
}

void GridController::beginValueDrag(GridAddress a)
{
    if (!editable() || a.track >= trackerState->session.pattern.tracks.size()
        || (a.field != 1 && (!gridFieldIsSequence(a.field) || gridFieldIsSequenceAction(a.field))))
        return;
    valueDrag_ = a;
    auto& track = trackerState->session.pattern.tracks[a.track];
    valueDragOriginal_ = a.field == 1
        ? GridCell(resolvedVelocityCell(track, a.row))
        : GridCell(resolvedFxValueCell(track, gridSequencePair(a.field), a.row));
    valueDragStart_ = a.field == 1 ? resolvedVelocity(track, a.row)
                                   : resolvedFxValue(track, gridSequencePair(a.field), a.row);
    valueDragChanged_ = false;
}

void GridController::updateValueDrag(double delta, bool fine, bool coarse)
{
    if (!valueDrag_ || !editable())
        return;
    auto a = *valueDrag_;
    if (a.track >= trackerState->session.pattern.tracks.size())
        return;
    auto value = app::normalizedValueFromVerticalDrag(valueDragStart_, delta, fine, coarse);
    auto cell = valueDragOriginal_;
    std::visit(
        [&](auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, ValueCell> || std::is_same_v<T, FxValueCell>) {
                std::array<float, kMaximumNoteVoices> voices {};
                for (std::size_t i = 0; i < v.valueVoiceCount(); ++i)
                    voices[i] = std::clamp(a.field == 1
                            ? v.valueVoice(i) * value / std::max(valueDragStart_, .00001f)
                            : v.valueVoice(i) + value - valueDragStart_,
                        0.f, 1.f);
                if (v.valueVoiceCount() == 1)
                    voices[0] = value;
                v = T::withValues(voices, v.valueVoiceCount());
            }
        },
        cell);
    writeTrackerGridCell(trackerState->session.pattern.tracks[a.track], a.field, a.row, cell);
    trackerState->session.pattern.visibleRows
        = std::max(trackerState->session.pattern.visibleRows, a.row + 1);
    valueDragChanged_ = true;
    selectionChanged();
}

void GridController::finishValueDrag()
{
    const auto changed = valueDragChanged_;
    valueDrag_.reset();
    valueDragChanged_ = false;
    if (changed)
        patternChanged();
}

void GridController::paintEnvelope(Point p, Rect bounds, bool clear)
{
    if (!editable())
        return;
    auto& session = trackerState->session;
    auto lane = std::min(session.selectedTrack, session.pattern.tracks.size() - 1);
    auto& track = session.pattern.tracks[lane];
    auto field = session.selectedField;
    if (field != 6 && (!gridFieldIsSequence(field) || gridFieldIsSequenceAction(field)))
        field = 1;
    auto* column = columnForField(track, 0, field);
    auto rows = std::clamp<std::size_t>(column->length, 16, 256);
    double left = bounds.x + 30, top = bounds.y + 34;
    double width = std::max(1., bounds.width - 40), height = std::max(1., bounds.height - 56);
    if (p.x < left || p.x > left + width || p.y < top || p.y > top + height)
        return;
    auto row = static_cast<std::size_t>(
        std::clamp(std::floor((p.x - left) / width * double(rows)), 0., double(rows - 1)));
    auto value = static_cast<float>(std::clamp(1. - (p.y - top) / height, 0., 1.));
    GridCell cell = field == 6
        ? GridCell(clear ? GateCell::defaultValue() : GateCell::withRows(std::max(.01f, value * 4)))
        : field == 1 ? GridCell(clear ? ValueCell::previous() : ValueCell::withValue(value))
                     : GridCell(clear ? FxValueCell::previous() : FxValueCell::withValue(value));
    writeTrackerGridCell(track, field, row, cell);
    session.pattern.visibleRows = std::max(session.pattern.visibleRows, row + 1);
    session.selectedRow = row;
    patternChanged();
}
namespace {
    void writeGridCell(Track& track, std::size_t field, std::size_t row, const GridCell& cell)
    {
        writeTrackerGridCell(track, field, row, cell);
    }
    GridCell blankGridCell(std::size_t field) { return blankTrackerGridCell(field); }
    const BurstLibrary* workspaceBurstLibrary(
        const app::TrackerViewState& state, AssetBankId bankId)
    {
        if (bankId == state.activeBurstBankId)
            return &state.session.burstLibrary;
        const auto* bank = findBurstBank(state.burstBanks, bankId);
        return bank ? &bank->library : nullptr;
    }
}

void GridController::reverseGridSelection(int tag)
{
    (void)tag;
    auto* model = trackerState;
    if (!model || model->songPlaybackActive)
        return;
    auto candidate = model->session.pattern;
    const auto range = effectiveGridSelection();
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto first = selection.active
        ? selection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto last = selection.active ? selection.lastColumn(fields) : first;
    for (std::size_t column = first; column <= last; ++column) {
        std::size_t track = 0u, field = 0u;
        s3g::tracker::app::gridAddressForClipboardColumn(column, fields, track, field);
        for (std::size_t offset = 0u; offset < range.rowCount() / 2u; ++offset) {
            const auto a
                = trackerGridCellAt(candidate.tracks[track], field, range.firstRow + offset);
            const auto b
                = trackerGridCellAt(candidate.tracks[track], field, range.lastRow - offset);
            writeGridCell(candidate.tracks[track], field, range.firstRow + offset, b);
            writeGridCell(candidate.tracks[track], field, range.lastRow - offset, a);
        }
    }
    model->session.pattern = std::move(candidate);
    patternChanged();
}

void GridController::rotateGridSelection(int tag)
{
    auto* model = trackerState;
    if (!model || model->songPlaybackActive || tag == 0)
        return;
    auto candidate = model->session.pattern;
    const auto range = effectiveGridSelection();
    if (range.rowCount() < 2u)
        return;
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto first = selection.active
        ? selection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto last = selection.active ? selection.lastColumn(fields) : first;
    for (std::size_t column = first; column <= last; ++column) {
        std::size_t track = 0u, field = 0u;
        s3g::tracker::app::gridAddressForClipboardColumn(column, fields, track, field);
        std::vector<GridCell> cells;
        cells.reserve(range.rowCount());
        for (std::size_t row = range.firstRow; row <= range.lastRow; ++row)
            cells.push_back(trackerGridCellAt(candidate.tracks[track], field, row));
        if (tag < 0)
            std::rotate(cells.begin(), cells.begin() + 1, cells.end());
        else
            std::rotate(cells.rbegin(), cells.rbegin() + 1, cells.rend());
        for (std::size_t offset = 0u; offset < cells.size(); ++offset)
            writeGridCell(candidate.tracks[track], field, range.firstRow + offset, cells[offset]);
    }
    model->session.pattern = std::move(candidate);
    patternChanged();
}

void GridController::adjustSelectedValues(int tag)
{
    auto* model = trackerState;
    if (!model || model->songPlaybackActive || tag == 0)
        return;
    auto candidate = model->session.pattern;
    const auto range = effectiveGridSelection();
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto first = selection.active
        ? selection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto last = selection.active ? selection.lastColumn(fields) : first;
    const float delta = static_cast<float>(tag) / 127.0f;
    bool changed = false;
    for (std::size_t column = first; column <= last; ++column) {
        std::size_t track = 0u, field = 0u;
        s3g::tracker::app::gridAddressForClipboardColumn(column, fields, track, field);
        for (std::size_t row = range.firstRow; row <= range.lastRow; ++row) {
            if (field == 1u) {
                auto cell
                    = std::get<ValueCell>(trackerGridCellAt(candidate.tracks[track], field, row));
                if (cell.state != ValueCellState::Value)
                    continue;
                std::array<float, s3g::tracker::kMaximumNoteVoices> voices {};
                for (std::size_t voice = 0u; voice < cell.valueVoiceCount(); ++voice)
                    voices[voice] = std::clamp(cell.valueVoice(voice) + delta, 0.0f, 1.0f);
                writeGridCell(candidate.tracks[track], field, row,
                    ValueCell::withValues(voices, cell.valueVoiceCount()));
                changed = true;
            } else if (gridFieldIsSequence(field) && !gridFieldIsSequenceAction(field)) {
                auto cell
                    = std::get<FxValueCell>(trackerGridCellAt(candidate.tracks[track], field, row));
                if (cell.state != FxValueCellState::Value)
                    continue;
                std::array<float, s3g::tracker::kMaximumNoteVoices> voices {};
                for (std::size_t voice = 0u; voice < cell.valueVoiceCount(); ++voice)
                    voices[voice] = std::clamp(cell.valueVoice(voice) + delta, 0.0f, 1.0f);
                writeGridCell(candidate.tracks[track], field, row,
                    FxValueCell::withValues(voices, cell.valueVoiceCount()));
                changed = true;
            }
        }
    }
    if (changed) {
        model->session.pattern = std::move(candidate);
        patternChanged();
    }
}

void GridController::scaleSelectedValues(int tag)
{
    auto* model = trackerState;
    if (!model || model->songPlaybackActive || tag <= 0)
        return;
    auto candidate = model->session.pattern;
    const auto range = effectiveGridSelection();
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto first = selection.active
        ? selection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto last = selection.active ? selection.lastColumn(fields) : first;
    const float scale = static_cast<float>(tag) / 100.0f;
    bool changed = false;
    for (std::size_t column = first; column <= last; ++column) {
        std::size_t track = 0u, field = 0u;
        s3g::tracker::app::gridAddressForClipboardColumn(column, fields, track, field);
        for (std::size_t row = range.firstRow; row <= range.lastRow; ++row) {
            if (field == 1u) {
                auto cell
                    = std::get<ValueCell>(trackerGridCellAt(candidate.tracks[track], field, row));
                if (cell.state != ValueCellState::Value)
                    continue;
                std::array<float, s3g::tracker::kMaximumNoteVoices> voices {};
                for (std::size_t voice = 0u; voice < cell.valueVoiceCount(); ++voice)
                    voices[voice] = std::clamp(cell.valueVoice(voice) * scale, 0.0f, 1.0f);
                writeGridCell(candidate.tracks[track], field, row,
                    ValueCell::withValues(voices, cell.valueVoiceCount()));
                changed = true;
            } else if (gridFieldIsSequence(field) && !gridFieldIsSequenceAction(field)) {
                auto cell
                    = std::get<FxValueCell>(trackerGridCellAt(candidate.tracks[track], field, row));
                if (cell.state != FxValueCellState::Value)
                    continue;
                std::array<float, s3g::tracker::kMaximumNoteVoices> voices {};
                for (std::size_t voice = 0u; voice < cell.valueVoiceCount(); ++voice)
                    voices[voice] = std::clamp(cell.valueVoice(voice) * scale, 0.0f, 1.0f);
                writeGridCell(candidate.tracks[track], field, row,
                    FxValueCell::withValues(voices, cell.valueVoiceCount()));
                changed = true;
            }
        }
    }
    if (changed) {
        model->session.pattern = std::move(candidate);
        patternChanged();
    }
}

void GridController::quantizeSelectedMicroTime(int tag)
{
    auto* model = trackerState;
    if (!model || model->songPlaybackActive || tag == 0)
        return;
    auto candidate = model->session.pattern;
    const auto range = effectiveGridSelection();
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto first = selection.active
        ? selection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto last = selection.active ? selection.lastColumn(fields) : first;
    const bool nudge = std::abs(tag) > 1000;
    const float strength = nudge ? 0.0f : std::clamp(static_cast<float>(tag) / 100.0f, 0.0f, 1.0f);
    const float rangeMs
        = static_cast<float>(std::max(1.0, model->session.transport.microTimingRangeMilliseconds));
    const float nudgeDelta
        = nudge ? static_cast<float>(tag < 0 ? -1.0 : 1.0) / (2.0f * rangeMs) : 0.0f;
    bool changed = false;
    for (std::size_t column = first; column <= last; ++column) {
        std::size_t track = 0u, field = 0u;
        s3g::tracker::app::gridAddressForClipboardColumn(column, fields, track, field);
        if (!gridFieldIsSequence(field) || gridFieldIsSequenceAction(field))
            continue;
        auto& pair = candidate.tracks[track].fxPairs[gridSequencePair(field)];
        for (std::size_t row = range.firstRow; row <= range.lastRow; ++row) {
            if (row >= pair.values.size() || pair.values[row].state != FxValueCellState::Value)
                continue;
            bool microTime = false;
            for (std::size_t scan = 0u; scan <= row && scan < pair.actions.size(); ++scan) {
                if (pair.actions[scan].state == FxActionCellState::Sequencer)
                    microTime = pair.actions[scan].sequencerAction == SequencerAction::MicroTime;
            }
            if (!microTime)
                continue;
            auto& cell = pair.values[row];
            std::array<float, s3g::tracker::kMaximumNoteVoices> voices {};
            for (std::size_t voice = 0u; voice < cell.valueVoiceCount(); ++voice)
                voices[voice] = std::clamp(nudge
                        ? cell.valueVoice(voice) + nudgeDelta
                        : cell.valueVoice(voice) + (0.5f - cell.valueVoice(voice)) * strength,
                    0.0f, 1.0f);
            cell = FxValueCell::withValues(voices, cell.valueVoiceCount());
            changed = true;
        }
    }
    if (changed) {
        model->session.pattern = std::move(candidate);
        patternChanged();
    }
}

void GridController::fillSelectionFromEdge(int tag)
{
    auto* model = trackerState;
    if (!model || model->songPlaybackActive)
        return;
    auto candidate = model->session.pattern;
    const auto range = effectiveGridSelection();
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto firstColumn = selection.active
        ? selection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto lastColumn = selection.active ? selection.lastColumn(fields) : firstColumn;
    const auto sourceRow = tag < 0 ? range.lastRow : range.firstRow;
    for (std::size_t column = firstColumn; column <= lastColumn; ++column) {
        std::size_t track = 0u, field = 0u;
        s3g::tracker::app::gridAddressForClipboardColumn(column, fields, track, field);
        const auto source = trackerGridCellAt(candidate.tracks[track], field, sourceRow);
        for (std::size_t row = range.firstRow; row <= range.lastRow; ++row)
            writeGridCell(candidate.tracks[track], field, row, source);
    }
    model->session.pattern = std::move(candidate);
    patternChanged();
}

void GridController::fillSelectionSeries(int tag)
{
    (void)tag;
    auto* model = trackerState;
    if (!model || model->songPlaybackActive)
        return;
    auto candidate = model->session.pattern;
    const auto range = effectiveGridSelection();
    if (range.rowCount() < 2u)
        return;
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto firstColumn = selection.active
        ? selection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto lastColumn = selection.active ? selection.lastColumn(fields) : firstColumn;
    bool changed = false;
    for (std::size_t column = firstColumn; column <= lastColumn; ++column) {
        std::size_t track = 0u, field = 0u;
        s3g::tracker::app::gridAddressForClipboardColumn(column, fields, track, field);
        const auto first = trackerGridCellAt(candidate.tracks[track], field, range.firstRow);
        const auto last = trackerGridCellAt(candidate.tracks[track], field, range.lastRow);
        for (std::size_t row = range.firstRow + 1u; row < range.lastRow; ++row) {
            const float phase = static_cast<float>(row - range.firstRow)
                / static_cast<float>(range.lastRow - range.firstRow);
            if (field == 0u) {
                const auto& a = std::get<NoteCell>(first);
                const auto& b = std::get<NoteCell>(last);
                if (a.state != NoteCellState::Note || b.state != NoteCellState::Note)
                    continue;
                const auto voices = std::max(a.noteVoiceCount(), b.noteVoiceCount());
                std::array<uint8_t, s3g::tracker::kMaximumNoteVoices> notes {};
                for (std::size_t voice = 0u; voice < voices; ++voice) {
                    const int av = a.noteVoice(std::min(voice, a.noteVoiceCount() - 1u));
                    const int bv = b.noteVoice(std::min(voice, b.noteVoiceCount() - 1u));
                    notes[voice] = static_cast<uint8_t>(std::clamp<int>(
                        static_cast<int>(std::lround(av + (bv - av) * phase)), 0, 127));
                }
                std::sort(notes.begin(), notes.begin() + static_cast<std::ptrdiff_t>(voices));
                writeGridCell(
                    candidate.tracks[track], field, row, NoteCell::withNotes(notes, voices));
                changed = true;
            } else if (field == 1u) {
                const auto& a = std::get<ValueCell>(first);
                const auto& b = std::get<ValueCell>(last);
                if (a.state != ValueCellState::Value || b.state != ValueCellState::Value)
                    continue;
                const auto voices = std::max(a.valueVoiceCount(), b.valueVoiceCount());
                std::array<float, s3g::tracker::kMaximumNoteVoices> values {};
                for (std::size_t voice = 0u; voice < voices; ++voice) {
                    const float av = a.valueVoice(std::min(voice, a.valueVoiceCount() - 1u));
                    const float bv = b.valueVoice(std::min(voice, b.valueVoiceCount() - 1u));
                    values[voice] = std::clamp(av + (bv - av) * phase, 0.0f, 1.0f);
                }
                writeGridCell(
                    candidate.tracks[track], field, row, ValueCell::withValues(values, voices));
                changed = true;
            } else if (gridFieldIsGate(field)) {
                const auto& a = std::get<GateCell>(first);
                const auto& b = std::get<GateCell>(last);
                if (a.voiceCount == 0u || b.voiceCount == 0u)
                    continue;
                const auto voices = std::max(a.gateVoiceCount(), b.gateVoiceCount());
                std::array<GateVoice, s3g::tracker::kMaximumNoteVoices> gates {};
                bool numeric = true;
                for (std::size_t voice = 0u; voice < voices; ++voice) {
                    const auto av = a.gateVoice(std::min(voice, a.gateVoiceCount() - 1u));
                    const auto bv = b.gateVoice(std::min(voice, b.gateVoiceCount() - 1u));
                    if (av.mode != GateVoiceMode::Rows || bv.mode != GateVoiceMode::Rows) {
                        numeric = false;
                        break;
                    }
                    gates[voice] = { GateVoiceMode::Rows,
                        std::clamp(av.rows + (bv.rows - av.rows) * phase, 0.01f, 64.0f) };
                }
                if (!numeric)
                    continue;
                writeGridCell(
                    candidate.tracks[track], field, row, GateCell::withVoices(gates, voices));
                changed = true;
            } else if (!gridFieldIsSequenceAction(field)) {
                const auto& a = std::get<FxValueCell>(first);
                const auto& b = std::get<FxValueCell>(last);
                if (a.state != FxValueCellState::Value || b.state != FxValueCellState::Value)
                    continue;
                const auto voices = std::max(a.valueVoiceCount(), b.valueVoiceCount());
                std::array<float, s3g::tracker::kMaximumNoteVoices> values {};
                for (std::size_t voice = 0u; voice < voices; ++voice) {
                    const float av = a.valueVoice(std::min(voice, a.valueVoiceCount() - 1u));
                    const float bv = b.valueVoice(std::min(voice, b.valueVoiceCount() - 1u));
                    values[voice] = std::clamp(av + (bv - av) * phase, 0.0f, 1.0f);
                }
                writeGridCell(
                    candidate.tracks[track], field, row, FxValueCell::withValues(values, voices));
                changed = true;
            }
        }
    }
    if (!changed) {
        reject();
        return;
    }
    model->session.pattern = std::move(candidate);
    patternChanged();
}

void GridController::repeatGridSelection(int tag)
{
    auto* model = trackerState;
    if (!model || model->songPlaybackActive)
        return;
    auto candidate = model->session.pattern;
    const auto range = effectiveGridSelection();
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto firstColumn = selection.active
        ? selection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto lastColumn = selection.active ? selection.lastColumn(fields) : firstColumn;
    const auto rows = range.rowCount();
    std::size_t repeats
        = tag < 0 ? (256u - range.lastRow - 1u) / rows : static_cast<std::size_t>(tag);
    if (repeats == 0u)
        return;
    std::vector<GridCell> source;
    source.reserve(rows * (lastColumn - firstColumn + 1u));
    for (std::size_t row = range.firstRow; row <= range.lastRow; ++row)
        for (std::size_t column = firstColumn; column <= lastColumn; ++column) {
            std::size_t track = 0u, field = 0u;
            s3g::tracker::app::gridAddressForClipboardColumn(column, fields, track, field);
            source.push_back(trackerGridCellAt(candidate.tracks[track], field, row));
        }
    const auto columns = lastColumn - firstColumn + 1u;
    std::size_t finalRow = range.lastRow;
    for (std::size_t repeat = 0u; repeat < repeats; ++repeat) {
        for (std::size_t rowOffset = 0u; rowOffset < rows; ++rowOffset) {
            const auto row = range.lastRow + 1u + repeat * rows + rowOffset;
            if (row >= 256u)
                break;
            finalRow = row;
            for (std::size_t columnOffset = 0u; columnOffset < columns; ++columnOffset) {
                std::size_t track = 0u, field = 0u;
                s3g::tracker::app::gridAddressForClipboardColumn(
                    firstColumn + columnOffset, fields, track, field);
                writeGridCell(candidate.tracks[track], field, row,
                    source[rowOffset * columns + columnOffset]);
            }
        }
    }
    candidate.visibleRows = std::max(candidate.visibleRows, finalRow + 1u);
    model->session.pattern = std::move(candidate);
    patternChanged();
}

void GridController::shiftSelectionCells(int tag)
{
    auto* model = trackerState;
    if (!model || model->songPlaybackActive)
        return;
    auto candidate = model->session.pattern;
    const auto range = effectiveGridSelection();
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto firstColumn = selection.active
        ? selection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto lastColumn = selection.active ? selection.lastColumn(fields) : firstColumn;
    const auto count = range.rowCount();
    const bool insert = tag > 0;
    const auto edit = [=](auto& values, const auto& blank, ColumnDefinition& definition) {
        if (insert && values.size() < range.firstRow)
            values.resize(range.firstRow, blank);
        const auto position = std::min(range.firstRow, values.size());
        if (insert) {
            values.insert(values.begin() + static_cast<std::ptrdiff_t>(position), count, blank);
            if (values.size() > 256u)
                values.resize(256u);
            definition.length
                = std::min<std::size_t>(256u, std::max(definition.length, range.firstRow) + count);
        } else {
            const auto end = std::min(values.size(), range.lastRow + 1u);
            if (position < end)
                values.erase(values.begin() + static_cast<std::ptrdiff_t>(position),
                    values.begin() + static_cast<std::ptrdiff_t>(end));
            const auto removed = range.firstRow < definition.length
                ? std::min(count, definition.length - range.firstRow)
                : 0u;
            definition.length = std::max<std::size_t>(1u, definition.length - removed);
        }
    };
    for (std::size_t column = firstColumn; column <= lastColumn; ++column) {
        std::size_t trackIndex = 0u, field = 0u;
        s3g::tracker::app::gridAddressForClipboardColumn(column, fields, trackIndex, field);
        auto& track = candidate.tracks[trackIndex];
        if (field == 0u)
            edit(track.notes, NoteCell::rest(), track.noteColumn);
        else if (field == 1u)
            edit(track.velocities, ValueCell::defaultValue(), track.velocityColumn);
        else if (gridFieldIsGate(field))
            edit(track.gates, GateCell::defaultValue(), track.gateColumn);
        else {
            auto& pair = track.fxPairs[gridSequencePair(field)];
            if (gridFieldIsSequenceAction(field))
                edit(pair.actions, FxActionCell::empty(), pair.actionColumn);
            else
                edit(pair.values, FxValueCell::previous(), pair.valueColumn);
        }
    }
    model->session.pattern = std::move(candidate);
    patternChanged();
}

void GridController::moveGridSelection(int tag)
{
    auto* model = trackerState;
    if (!model || model->songPlaybackActive)
        return;
    int offset = static_cast<int>(tag);
    if (std::abs(offset) == 100)
        offset = offset < 0
            ? -static_cast<int>(std::clamp<uint32_t>(model->trackerRowJump, 1u, 16u))
            : static_cast<int>(std::clamp<uint32_t>(model->trackerRowJump, 1u, 16u));
    const auto range = effectiveGridSelection();
    const int destinationFirst = static_cast<int>(range.firstRow) + offset;
    const int destinationLast = static_cast<int>(range.lastRow) + offset;
    if (destinationFirst < 0 || destinationLast >= 256) {
        reject();
        return;
    }
    auto candidate = model->session.pattern;
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto firstColumn = selection.active
        ? selection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto lastColumn = selection.active ? selection.lastColumn(fields) : firstColumn;
    const auto columns = lastColumn - firstColumn + 1u;
    std::vector<GridCell> cells;
    cells.reserve(range.rowCount() * columns);
    for (std::size_t row = range.firstRow; row <= range.lastRow; ++row)
        for (std::size_t column = firstColumn; column <= lastColumn; ++column) {
            std::size_t track = 0u, field = 0u;
            s3g::tracker::app::gridAddressForClipboardColumn(column, fields, track, field);
            cells.push_back(trackerGridCellAt(candidate.tracks[track], field, row));
            writeGridCell(candidate.tracks[track], field, row, blankGridCell(field));
        }
    for (std::size_t rowOffset = 0u; rowOffset < range.rowCount(); ++rowOffset)
        for (std::size_t columnOffset = 0u; columnOffset < columns; ++columnOffset) {
            std::size_t track = 0u, field = 0u;
            s3g::tracker::app::gridAddressForClipboardColumn(
                firstColumn + columnOffset, fields, track, field);
            writeGridCell(candidate.tracks[track], field,
                static_cast<std::size_t>(destinationFirst) + rowOffset,
                cells[rowOffset * columns + columnOffset]);
        }
    candidate.visibleRows
        = std::max(candidate.visibleRows, static_cast<std::size_t>(destinationLast) + 1u);
    model->session.pattern = std::move(candidate);
    selection.anchorRow = static_cast<std::size_t>(static_cast<int>(selection.anchorRow) + offset);
    selection.focusRow = static_cast<std::size_t>(static_cast<int>(selection.focusRow) + offset);
    model->session.selectedRow = static_cast<std::size_t>(destinationFirst);
    patternChanged();
}

void GridController::stretchGridSelection(int tag)
{
    auto* model = trackerState;
    if (!model || model->songPlaybackActive)
        return;
    const auto range = effectiveGridSelection();
    if (range.rowCount() < 2u)
        return;
    const double factor = static_cast<double>(tag) / 100.0;
    const auto destinationRows
        = std::clamp<std::size_t>(static_cast<std::size_t>(std::lround(range.rowCount() * factor)),
            1u, 256u - range.firstRow);
    auto candidate = model->session.pattern;
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto firstColumn = selection.active
        ? selection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto lastColumn = selection.active ? selection.lastColumn(fields) : firstColumn;
    for (std::size_t column = firstColumn; column <= lastColumn; ++column) {
        std::size_t track = 0u, field = 0u;
        s3g::tracker::app::gridAddressForClipboardColumn(column, fields, track, field);
        std::vector<std::pair<std::size_t, GridCell>> occupied;
        for (std::size_t row = range.firstRow; row <= range.lastRow; ++row) {
            auto cell = trackerGridCellAt(candidate.tracks[track], field, row);
            if (!trackerGridCellEmpty(cell))
                occupied.emplace_back(row - range.firstRow, cell);
            writeGridCell(candidate.tracks[track], field, row, blankGridCell(field));
        }
        for (const auto& event : occupied) {
            const auto mapped = range.rowCount() <= 1u
                ? 0u
                : static_cast<std::size_t>(std::lround(static_cast<double>(event.first)
                    * static_cast<double>(destinationRows - 1u)
                    / static_cast<double>(range.rowCount() - 1u)));
            writeGridCell(candidate.tracks[track], field, range.firstRow + mapped, event.second);
        }
    }
    candidate.visibleRows = std::max(candidate.visibleRows, range.firstRow + destinationRows);
    model->session.pattern = std::move(candidate);
    selection.focusRow = range.firstRow + destinationRows - 1u;
    selection.anchorRow = range.firstRow;
    patternChanged();
}

void GridController::materializeGridSelection(int tag)
{
    (void)tag;
    auto* model = trackerState;
    if (!model || model->songPlaybackActive)
        return;
    auto candidate = model->session.pattern;
    const auto range = effectiveGridSelection();
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto firstColumn = selection.active
        ? selection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto lastColumn = selection.active ? selection.lastColumn(fields) : firstColumn;
    bool changed = false;
    for (std::size_t column = firstColumn; column <= lastColumn; ++column) {
        std::size_t trackIndex = 0u, field = 0u;
        s3g::tracker::app::gridAddressForClipboardColumn(column, fields, trackIndex, field);
        auto& track = candidate.tracks[trackIndex];
        GridCell memory = blankGridCell(field);
        bool hasMemory = false;
        for (std::size_t row = 0u; row <= range.lastRow; ++row) {
            auto cell = trackerGridCellAt(track, field, row);
            if (field == 0u) {
                auto note = std::get<NoteCell>(cell);
                if (note.state == NoteCellState::Note) {
                    memory = note;
                    hasMemory = true;
                } else if (row >= range.firstRow && hasMemory
                    && (note.state == NoteCellState::RetriggerPrevious
                        || note.state == NoteCellState::Hold)) {
                    writeGridCell(track, field, row, memory);
                    changed = true;
                }
            } else if (field == 1u) {
                auto value = std::get<ValueCell>(cell);
                if (value.state == ValueCellState::Value) {
                    memory = value;
                    hasMemory = true;
                } else if (value.state == ValueCellState::Default) {
                    memory = ValueCell::withValue(0.787f);
                    hasMemory = true;
                    if (row >= range.firstRow) {
                        writeGridCell(track, field, row, memory);
                        changed = true;
                    }
                } else if (row >= range.firstRow && hasMemory) {
                    writeGridCell(track, field, row, memory);
                    changed = true;
                }
            } else if (gridFieldIsGate(field)) {
                // Gate defaults are intentional fallbacks to the transport gate;
                // there is no previous-value state to materialize.
                continue;
            } else if (gridFieldIsSequenceAction(field)) {
                auto action = std::get<FxActionCell>(cell);
                if (action.state != FxActionCellState::Empty
                    && action.state != FxActionCellState::Previous) {
                    memory = action;
                    hasMemory = true;
                } else if (row >= range.firstRow && hasMemory
                    && action.state == FxActionCellState::Previous) {
                    writeGridCell(track, field, row, memory);
                    changed = true;
                }
            } else {
                auto value = std::get<FxValueCell>(cell);
                if (value.state == FxValueCellState::Value) {
                    memory = value;
                    hasMemory = true;
                } else if (row >= range.firstRow) {
                    if (!hasMemory)
                        memory = FxValueCell::withValue(0.0f);
                    writeGridCell(track, field, row, memory);
                    changed = true;
                }
            }
        }
    }
    if (!changed)
        return;
    model->session.pattern = std::move(candidate);
    patternChanged();
}

void GridController::findReplaceGridSelection(int tag)
{
    (void)tag;
    auto* model = trackerState;
    if (!model || model->songPlaybackActive)
        return;
    auto candidate = model->session.pattern;
    const auto range = effectiveGridSelection();
    if (range.rowCount() < 2u)
        return;
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto firstColumn = selection.active
        ? selection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto lastColumn = selection.active ? selection.lastColumn(fields) : firstColumn;
    std::size_t changed = 0u;
    for (std::size_t column = firstColumn; column <= lastColumn; ++column) {
        std::size_t track = 0u, field = 0u;
        s3g::tracker::app::gridAddressForClipboardColumn(column, fields, track, field);
        const auto find = trackerGridCellAt(candidate.tracks[track], field, range.firstRow);
        const auto replacement = trackerGridCellAt(candidate.tracks[track], field, range.lastRow);
        for (std::size_t row = range.firstRow; row <= range.lastRow; ++row) {
            const auto current = trackerGridCellAt(candidate.tracks[track], field, row);
            if (!trackerGridCellsEqual(current, find)
                || trackerGridCellsEqual(current, replacement))
                continue;
            writeGridCell(candidate.tracks[track], field, row, replacement);
            ++changed;
        }
    }
    if (changed == 0u)
        return;
    model->session.pattern = std::move(candidate);
    patternChanged();
}

void GridController::swapGridSelectionWithNextLane(int tag)
{
    (void)tag;
    auto* model = trackerState;
    if (!model || model->songPlaybackActive)
        return;
    auto candidate = model->session.pattern;
    const auto range = effectiveGridSelection();
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto firstColumn = selection.active
        ? selection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto lastColumn = selection.active ? selection.lastColumn(fields) : firstColumn;
    const auto lane = firstColumn / fields;
    if (lastColumn / fields != lane || lane + 1u >= candidate.tracks.size())
        return;
    for (std::size_t row = range.firstRow; row <= range.lastRow; ++row)
        for (std::size_t column = firstColumn; column <= lastColumn; ++column) {
            const auto field = column % fields;
            const auto a = trackerGridCellAt(candidate.tracks[lane], field, row);
            const auto b = trackerGridCellAt(candidate.tracks[lane + 1u], field, row);
            writeGridCell(candidate.tracks[lane], field, row, b);
            writeGridCell(candidate.tracks[lane + 1u], field, row, a);
        }
    model->session.pattern = std::move(candidate);
    patternChanged();
}

void GridController::moveCompleteLane(int tag)
{
    auto* model = trackerState;
    if (!model || model->songPlaybackActive)
        return;
    const auto source = model->session.selectedTrack;
    const auto destination = static_cast<std::size_t>(std::max<int64_t>(0, tag));
    auto& session = model->session;
    if (source >= session.pattern.tracks.size() || destination >= session.pattern.tracks.size()
        || source == destination)
        return;
    const auto moveIndex = [source, destination](std::size_t index) {
        if (index == source)
            return destination;
        if (source < destination && index > source && index <= destination)
            return index - 1u;
        if (source > destination && index >= destination && index < source)
            return index + 1u;
        return index;
    };
    auto movedTrack = std::move(session.pattern.tracks[source]);
    session.pattern.tracks.erase(
        session.pattern.tracks.begin() + static_cast<std::ptrdiff_t>(source));
    session.pattern.tracks.insert(
        session.pattern.tracks.begin() + static_cast<std::ptrdiff_t>(destination),
        std::move(movedTrack));
    if (source < session.laneDefaultNotes.size()) {
        const uint8_t note = session.laneDefaultNotes[source];
        session.laneDefaultNotes.erase(
            session.laneDefaultNotes.begin() + static_cast<std::ptrdiff_t>(source));
        session.laneDefaultNotes.insert(session.laneDefaultNotes.begin()
                + static_cast<std::ptrdiff_t>(
                    std::min(destination, session.laneDefaultNotes.size())),
            note);
    }
    for (auto& alias : session.aliases)
        alias.second = moveIndex(alias.second);
    session.selectedTrack = moveIndex(session.selectedTrack);
    model->midiRecordTrack = moveIndex(model->midiRecordTrack);
    model->trackerFollow.lane = static_cast<uint32_t>(moveIndex(model->trackerFollow.lane));
    if (model->assembly.targetPatternId.empty()
        || model->assembly.targetPatternId == model->patternBank.activePatternId)
        model->assembly.targetTrack = static_cast<uint32_t>(moveIndex(model->assembly.targetTrack));
    clearGridSelection();
    if (true && callbacks_.tracksReordered) {
        callbacks_.tracksReordered(model->patternBank.activePatternId, source, destination);
    }
    if (true && callbacks_.midiRecordTrackChanged)
        callbacks_.midiRecordTrackChanged(model->midiRecordTrack);
    patternChanged();
}

void GridController::pasteGridSelectionSpecial(int tag)
{
    if (tag == 0) {
        paste(services_.readClipboard ? services_.readClipboard() : copiedClipboardText);
        return;
    }
    auto* model = trackerState;
    if (!model || model->songPlaybackActive || copiedGridCells.empty() || copiedColumnTypes.empty()
        || copiedRowCount == 0u)
        return;
    const auto range = effectiveGridSelection();
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto firstColumn = selection.active
        ? selection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    if (firstColumn + copiedColumnTypes.size() > model->session.pattern.tracks.size() * fields
        || range.firstRow + copiedRowCount > 256u) {
        reject();
        return;
    }
    for (std::size_t offset = 0u; offset < copiedColumnTypes.size(); ++offset) {
        const auto sourceType = copiedColumnTypes[offset];
        const bool applies = tag == 3
            ? sourceType == 0u
            : tag == 4 ? sourceType == 1u || sourceType == 3u : tag == 2 ? sourceType == 0u : true;
        if (applies && sourceType != gridClipboardFieldType((firstColumn + offset) % fields)) {
            reject();
            return;
        }
    }
    auto candidate = model->session.pattern;
    const auto columns = copiedColumnTypes.size();
    std::size_t changed = 0u;
    for (std::size_t rowOffset = 0u; rowOffset < copiedRowCount; ++rowOffset) {
        for (std::size_t columnOffset = 0u; columnOffset < columns; ++columnOffset) {
            const auto sourceType = copiedColumnTypes[columnOffset];
            if ((tag == 3 && sourceType != 0u) || (tag == 4 && sourceType != 1u && sourceType != 3u)
                || (tag == 2 && sourceType != 0u))
                continue;
            std::size_t track = 0u, field = 0u;
            s3g::tracker::app::gridAddressForClipboardColumn(
                firstColumn + columnOffset, fields, track, field);
            const auto row = range.firstRow + rowOffset;
            const auto current = trackerGridCellAt(candidate.tracks[track], field, row);
            if (tag == 1 && !trackerGridCellEmpty(current))
                continue;
            GridCell replacement = copiedGridCells[rowOffset * columns + columnOffset];
            if (tag == 2) {
                const auto& rhythm = std::get<NoteCell>(replacement);
                if (rhythm.state == NoteCellState::Note) {
                    const auto& existing = std::get<NoteCell>(current);
                    replacement = existing.state == NoteCellState::Note
                        ? existing
                        : NoteCell::withNote(laneDefaultNote(model->session, track));
                }
            }
            if (trackerGridCellsEqual(current, replacement))
                continue;
            writeGridCell(candidate.tracks[track], field, row, replacement);
            ++changed;
        }
    }
    if (changed == 0u)
        return;
    candidate.visibleRows = std::max(candidate.visibleRows, range.firstRow + copiedRowCount);
    model->session.pattern = std::move(candidate);
    patternChanged();
}

void GridController::splitSelectedNoteColumnByPitch(int tag)
{
    (void)tag;
    auto* model = trackerState;
    if (!model || model->songPlaybackActive)
        return;
    const auto range = effectiveGridSelection();
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto firstColumn = selection.active
        ? selection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto lastColumn = selection.active ? selection.lastColumn(fields) : firstColumn;
    if (firstColumn != lastColumn || firstColumn % fields != 0u)
        return;
    const auto sourceLane = firstColumn / fields;
    if (sourceLane >= model->session.pattern.tracks.size())
        return;
    std::vector<uint8_t> pitches;
    const auto& source = model->session.pattern.tracks[sourceLane];
    const auto burstRoutingPitch = [&](const NoteCell& cell, uint8_t* pitch) {
        if (cell.state != NoteCellState::Burst || !pitch)
            return false;
        const auto* library = workspaceBurstLibrary(*model, cell.burstBankId);
        if (!library || cell.note >= library->bursts.size())
            return false;
        const auto& burst = library->bursts[cell.note];
        if (burst.empty())
            return false;
        *pitch = burst.events[0u].note;
        return true;
    };
    for (std::size_t row = range.firstRow; row <= range.lastRow; ++row) {
        if (row >= source.notes.size())
            continue;
        const auto& note = source.notes[row];
        if (note.state == NoteCellState::Note) {
            for (std::size_t voice = 0u; voice < note.noteVoiceCount(); ++voice) {
                const auto pitch = note.noteVoice(voice);
                if (std::find(pitches.begin(), pitches.end(), pitch) == pitches.end())
                    pitches.push_back(pitch);
            }
        } else {
            uint8_t pitch = 0u;
            if (burstRoutingPitch(note, &pitch)
                && std::find(pitches.begin(), pitches.end(), pitch) == pitches.end())
                pitches.push_back(pitch);
        }
    }
    if (pitches.size() < 2u) {
        reject();
        return;
    }
    if (model->session.pattern.tracks.size() + pitches.size() - 1u
        > s3g::tracker::kMaximumTrackCount) {
        reject();
        return;
    }
    auto candidate = model->session.pattern;
    const Track original = candidate.tracks[sourceLane];
    std::vector<std::size_t> destinationLanes { sourceLane };
    auto rows = std::max<std::size_t>(candidate.visibleRows,
        std::max({ original.notes.size(), original.instruments.size(), original.velocities.size(),
            original.gates.size(), original.noteColumn.length, original.instrumentColumn.length,
            original.velocityColumn.length, original.gateColumn.length }));
    for (const auto& pair : original.fxPairs)
        rows = std::max({ rows, pair.actions.size(), pair.values.size(), pair.actionColumn.length,
            pair.valueColumn.length });
    candidate.tracks[sourceLane].notes.resize(rows, NoteCell::rest());
    candidate.tracks[sourceLane].velocities.resize(rows, ValueCell::defaultValue());
    candidate.tracks[sourceLane].gates.resize(rows, GateCell::defaultValue());
    for (std::size_t index = 1u; index < pitches.size(); ++index) {
        Track split;
        split.name = (original.name.empty() ? "LANE" : original.name) + " · "
            + std::to_string(pitches[index]);
        split.velocityScale = original.velocityScale;
        split.midiChannel = original.midiChannel;
        split.destination = original.destination;
        split.initialInstrumentNodeId = original.initialInstrumentNodeId;
        split.chokeGroup = original.chokeGroup;
        split.notes.assign(rows, NoteCell::rest());
        split.instruments.assign(rows, InstrumentCell::empty());
        split.velocities.assign(rows, ValueCell::defaultValue());
        split.gates.assign(rows, GateCell::defaultValue());
        split.noteColumn = original.noteColumn;
        split.instrumentColumn = original.instrumentColumn;
        split.velocityColumn = original.velocityColumn;
        split.gateColumn = original.gateColumn;
        for (std::size_t pairIndex = 0u; pairIndex < split.fxPairs.size(); ++pairIndex) {
            auto& pair = split.fxPairs[pairIndex];
            const auto& originalPair = original.fxPairs[pairIndex];
            pair.actions.assign(rows, FxActionCell::empty());
            pair.values.assign(rows, FxValueCell::previous());
            pair.actionColumn = originalPair.actionColumn;
            pair.valueColumn = originalPair.valueColumn;
            pair.valueInterpolation = originalPair.valueInterpolation;
        }
        candidate.tracks.push_back(std::move(split));
        destinationLanes.push_back(candidate.tracks.size() - 1u);
    }
    for (std::size_t row = range.firstRow; row <= range.lastRow; ++row) {
        const NoteCell note = row < original.notes.size() ? original.notes[row] : NoteCell::rest();
        const ValueCell velocity = row < original.velocities.size() ? original.velocities[row]
                                                                    : ValueCell::defaultValue();
        const GateCell gate
            = row < original.gates.size() ? original.gates[row] : GateCell::defaultValue();
        uint8_t burstPitch = 0u;
        const bool routedBurst = burstRoutingPitch(note, &burstPitch);
        if (note.state != NoteCellState::Note && !routedBurst)
            continue;
        for (const auto lane : destinationLanes) {
            candidate.tracks[lane].notes[row] = NoteCell::rest();
            candidate.tracks[lane].velocities[row] = ValueCell::defaultValue();
            candidate.tracks[lane].gates[row] = GateCell::defaultValue();
        }
        if (routedBurst) {
            const auto found = std::find(pitches.begin(), pitches.end(), burstPitch);
            if (found != pitches.end()) {
                const auto lane
                    = destinationLanes[static_cast<std::size_t>(found - pitches.begin())];
                candidate.tracks[lane].notes[row] = note;
                candidate.tracks[lane].velocities[row] = velocity;
                candidate.tracks[lane].gates[row] = gate;
            }
        } else {
            for (std::size_t voice = 0u; voice < note.noteVoiceCount(); ++voice) {
                const auto pitch = note.noteVoice(voice);
                const auto found = std::find(pitches.begin(), pitches.end(), pitch);
                if (found == pitches.end())
                    continue;
                const auto index = static_cast<std::size_t>(found - pitches.begin());
                const auto lane = destinationLanes[index];
                candidate.tracks[lane].notes[row] = NoteCell::withNote(pitch);
                if (velocity.state == ValueCellState::Value) {
                    const auto velocityVoice
                        = std::min<std::size_t>(voice, velocity.valueVoiceCount() - 1u);
                    candidate.tracks[lane].velocities[row]
                        = ValueCell::withValue(velocity.valueVoice(velocityVoice));
                } else
                    candidate.tracks[lane].velocities[row] = velocity;
                const auto voiceGate = gate.gateVoice(voice);
                if (voiceGate.mode == GateVoiceMode::Tie)
                    candidate.tracks[lane].gates[row] = GateCell::tie();
                else if (voiceGate.mode == GateVoiceMode::Rows)
                    candidate.tracks[lane].gates[row] = GateCell::withRows(voiceGate.rows);
            }
        }
        for (std::size_t index = 1u; index < destinationLanes.size(); ++index) {
            auto& destination = candidate.tracks[destinationLanes[index]];
            if (destination.notes[row].state != NoteCellState::Note
                && destination.notes[row].state != NoteCellState::Burst)
                continue;
            for (std::size_t pairIndex = 0u; pairIndex < original.fxPairs.size(); ++pairIndex) {
                SequencerAction action = SequencerAction::Count;
                if (!resolvedSequencerAction(original, pairIndex, row, action))
                    continue;
                destination.fxPairs[pairIndex].actions[row] = FxActionCell::sequencer(action);
                float value = resolvedFxValue(original, pairIndex, row);
                if (action == SequencerAction::MicroTime) {
                    const auto resolved = resolvedFxValueCell(original, pairIndex, row);
                    std::size_t sourceVoice = 0u;
                    if (note.state == NoteCellState::Note)
                        while (sourceVoice < note.noteVoiceCount()
                            && note.noteVoice(sourceVoice) != destination.notes[row].note)
                            ++sourceVoice;
                    value = resolved.valueVoice(
                        std::min<std::size_t>(sourceVoice, resolved.valueVoiceCount() - 1u));
                }
                destination.fxPairs[pairIndex].values[row] = FxValueCell::withValue(value);
            }
        }
    }
    candidate.tracks[sourceLane].name = (original.name.empty() ? "LANE" : original.name) + " · "
        + std::to_string(pitches.front());
    model->session.pattern = std::move(candidate);
    if (model->session.laneDefaultNotes.size() < model->session.pattern.tracks.size())
        model->session.laneDefaultNotes.resize(model->session.pattern.tracks.size(), 60u);
    model->session.laneDefaultNotes[sourceLane] = pitches.front();
    for (std::size_t index = 1u; index < pitches.size(); ++index)
        model->session.laneDefaultNotes[destinationLanes[index]] = pitches[index];
    clearGridSelection();
    patternChanged();
}

void GridController::mergeSelectedNoteLanes(int tag)
{
    (void)tag;
    auto* model = trackerState;
    if (!model || model->songPlaybackActive)
        return;
    const auto range = effectiveGridSelection();
    const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
    const auto firstColumn = selection.active
        ? selection.firstColumn(fields)
        : gridClipboardColumn(range.firstTrack, range.firstField, fields);
    const auto lastColumn = selection.active ? selection.lastColumn(fields) : firstColumn;
    if (firstColumn % fields != 0u || lastColumn % fields != 0u
        || firstColumn / fields >= lastColumn / fields) {
        reject();
        return;
    }
    const auto targetLane = firstColumn / fields;
    const auto lastLane = lastColumn / fields;
    if (lastLane >= model->session.pattern.tracks.size())
        return;

    const auto original = model->session.pattern;
    auto candidate = original;
    struct MergedVoice {
        uint8_t note = 0u;
        float velocity = 0.787f;
        float microTime = 0.5f;
        bool hasMicroTime = false;
        GateVoice gate {};
    };
    struct MergedSequence {
        SequencerAction action = SequencerAction::Count;
        float value = 0.0f;
    };
    bool changed = false;
    for (std::size_t row = range.firstRow; row <= range.lastRow; ++row) {
        std::vector<MergedVoice> voices;
        std::array<std::vector<MergedSequence>, s3g::tracker::kFxPairCount> sourceSequences;
        for (std::size_t lane = targetLane; lane <= lastLane; ++lane) {
            const auto& track = original.tracks[lane];
            if (row >= track.notes.size() || track.notes[row].state != NoteCellState::Note)
                continue;
            const auto& note = track.notes[row];
            const auto velocity = resolvedVelocityCell(track, row);
            bool hasMicroTime = false;
            FxValueCell microTime = FxValueCell::withValue(0.5f);
            for (std::size_t pairIndex = 0u; pairIndex < track.fxPairs.size(); ++pairIndex) {
                SequencerAction action = SequencerAction::Count;
                if (resolvedSequencerAction(track, pairIndex, row, action)
                    && action == SequencerAction::MicroTime) {
                    hasMicroTime = true;
                    microTime = resolvedFxValueCell(track, pairIndex, row);
                }
            }
            for (std::size_t voice = 0u; voice < note.noteVoiceCount(); ++voice) {
                const auto pitch = note.noteVoice(voice);
                if (std::find_if(voices.begin(), voices.end(),
                        [pitch](const auto& item) { return item.note == pitch; })
                    != voices.end())
                    continue;
                const auto velocityVoice
                    = std::min<std::size_t>(voice, velocity.valueVoiceCount() - 1u);
                voices.push_back({ pitch,
                    std::clamp(velocity.valueVoice(velocityVoice), 0.0f, 1.0f),
                    std::clamp(microTime.valueVoice(
                                   std::min<std::size_t>(voice, microTime.valueVoiceCount() - 1u)),
                        0.0f, 1.0f),
                    hasMicroTime,
                    row < track.gates.size() ? track.gates[row].gateVoice(voice) : GateVoice {} });
            }
            if (lane == targetLane)
                continue;
            for (std::size_t pairIndex = 0u; pairIndex < track.fxPairs.size(); ++pairIndex) {
                SequencerAction action = SequencerAction::Count;
                if (!resolvedSequencerAction(track, pairIndex, row, action))
                    continue;
                if (action == SequencerAction::MicroTime)
                    continue;
                sourceSequences[pairIndex].push_back(
                    { action, resolvedFxValue(track, pairIndex, row) });
            }
        }
        if (voices.empty())
            continue;
        if (voices.size() > s3g::tracker::kMaximumNoteVoices) {
            reject();
            return;
        }

        std::sort(voices.begin(), voices.end(),
            [](const auto& a, const auto& b) { return a.note < b.note; });

        auto& target = candidate.tracks[targetLane];
        if (std::any_of(voices.begin(), voices.end(),
                [](const auto& voice) { return voice.hasMicroTime; })) {
            std::size_t microTimePair = target.fxPairs.size();
            for (std::size_t pairIndex = 0u; pairIndex < target.fxPairs.size(); ++pairIndex) {
                SequencerAction action = SequencerAction::Count;
                if (resolvedSequencerAction(target, pairIndex, row, action)
                    && action == SequencerAction::MicroTime)
                    microTimePair = pairIndex;
            }
            if (microTimePair >= target.fxPairs.size()) {
                for (std::size_t pairIndex = 0u; pairIndex < target.fxPairs.size(); ++pairIndex) {
                    const auto& actions = target.fxPairs[pairIndex].actions;
                    if (row >= actions.size() || actions[row].state == FxActionCellState::Empty) {
                        microTimePair = pairIndex;
                        break;
                    }
                }
            }
            if (microTimePair >= target.fxPairs.size()) {
                reject();
                return;
            }
            std::array<float, s3g::tracker::kMaximumNoteVoices> values {};
            for (std::size_t voice = 0u; voice < voices.size(); ++voice)
                values[voice] = voices[voice].hasMicroTime ? voices[voice].microTime : 0.5f;
            auto& pair = target.fxPairs[microTimePair];
            if (pair.actions.size() <= row)
                pair.actions.resize(row + 1u, FxActionCell::empty());
            if (pair.values.size() <= row)
                pair.values.resize(row + 1u, FxValueCell::previous());
            pair.actions[row] = FxActionCell::sequencer(SequencerAction::MicroTime);
            pair.values[row] = FxValueCell::withValues(values, voices.size());
            pair.actionColumn.length = std::max(pair.actionColumn.length, row + 1u);
            pair.valueColumn.length = std::max(pair.valueColumn.length, row + 1u);
        }
        for (std::size_t sourcePair = 0u; sourcePair < sourceSequences.size(); ++sourcePair) {
            for (const auto& sequence : sourceSequences[sourcePair]) {
                bool alreadyPresent = false;
                bool conflictingValue = false;
                for (std::size_t targetPair = 0u; targetPair < target.fxPairs.size();
                     ++targetPair) {
                    SequencerAction targetAction = SequencerAction::Count;
                    if (!resolvedSequencerAction(target, targetPair, row, targetAction)
                        || targetAction != sequence.action)
                        continue;
                    const auto targetValue = resolvedFxValue(target, targetPair, row);
                    alreadyPresent = std::abs(targetValue - sequence.value) <= 0.000001f;
                    conflictingValue = !alreadyPresent;
                    break;
                }
                if (alreadyPresent)
                    continue;
                if (conflictingValue) {
                    reject();
                    return;
                }
                std::size_t available = target.fxPairs.size();
                for (std::size_t targetPair = 0u; targetPair < target.fxPairs.size();
                     ++targetPair) {
                    const auto& actions = target.fxPairs[targetPair].actions;
                    if (row >= actions.size() || actions[row].state == FxActionCellState::Empty) {
                        available = targetPair;
                        break;
                    }
                }
                if (available >= target.fxPairs.size()) {
                    reject();
                    return;
                }
                auto& pair = target.fxPairs[available];
                if (pair.actions.size() <= row)
                    pair.actions.resize(row + 1u, FxActionCell::empty());
                if (pair.values.size() <= row)
                    pair.values.resize(row + 1u, FxValueCell::previous());
                pair.actions[row] = FxActionCell::sequencer(sequence.action);
                pair.values[row] = FxValueCell::withValue(sequence.value);
                pair.actionColumn.length = std::max(pair.actionColumn.length, row + 1u);
                pair.valueColumn.length = std::max(pair.valueColumn.length, row + 1u);
            }
        }

        std::array<uint8_t, s3g::tracker::kMaximumNoteVoices> notes {};
        std::array<float, s3g::tracker::kMaximumNoteVoices> velocities {};
        std::array<GateVoice, s3g::tracker::kMaximumNoteVoices> gates {};
        bool hasExplicitGate = false;
        for (std::size_t voice = 0u; voice < voices.size(); ++voice) {
            notes[voice] = voices[voice].note;
            velocities[voice] = voices[voice].velocity;
            gates[voice] = voices[voice].gate;
            hasExplicitGate |= gates[voice].mode != GateVoiceMode::Default;
        }
        if (target.notes.size() <= row)
            target.notes.resize(row + 1u, NoteCell::rest());
        if (target.velocities.size() <= row)
            target.velocities.resize(row + 1u, ValueCell::defaultValue());
        if (target.gates.size() <= row)
            target.gates.resize(row + 1u, GateCell::defaultValue());
        target.notes[row] = NoteCell::withNotes(notes, voices.size());
        target.velocities[row] = ValueCell::withValues(velocities, voices.size());
        target.gates[row] = hasExplicitGate ? GateCell::withVoices(gates, voices.size())
                                            : GateCell::defaultValue();
        changed = true;
        target.noteColumn.length = std::max(target.noteColumn.length, row + 1u);
        target.velocityColumn.length = std::max(target.velocityColumn.length, row + 1u);
        target.gateColumn.length = std::max(target.gateColumn.length, row + 1u);
        for (std::size_t lane = targetLane + 1u; lane <= lastLane; ++lane) {
            const auto& source = original.tracks[lane];
            if (row >= source.notes.size() || source.notes[row].state != NoteCellState::Note)
                continue;
            auto& moved = candidate.tracks[lane];
            moved.notes[row] = NoteCell::rest();
            if (row < moved.velocities.size())
                moved.velocities[row] = ValueCell::defaultValue();
            if (row < moved.gates.size())
                moved.gates[row] = GateCell::defaultValue();
        }
    }
    if (!changed) {
        reject();
        return;
    }
    model->session.pattern = std::move(candidate);
    model->session.selectedTrack = targetLane;
    model->session.selectedRow = range.firstRow;
    model->session.selectedField = 0u;
    clearGridSelection();
    patternChanged();
}

}
