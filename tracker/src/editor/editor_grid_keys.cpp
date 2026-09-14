#include "s3g/tracker/editor_grid_controller.h"
#include <algorithm>
#include <cmath>
namespace s3g::tracker::editor {
using app::gridClipboardColumn;
constexpr double kGridRowHeight = app::kTrackerGridRowHeight;
void GridController::command(const std::string& text)
{
    if (callbacks_.executeCommand)
        callbacks_.executeCommand(text);
}
void GridController::togglePlayback()
{
    if (callbacks_.togglePlayback)
        callbacks_.togglePlayback();
}
void GridController::toggleLoop()
{
    if (!editable())
        return;
    auto& loop = trackerState->session.transport.loopEnabled;
    loop = !loop;
    if (callbacks_.transportChanged)
        callbacks_.transportChanged();
    if (services_.invalidate)
        services_.invalidate();
}
void GridController::focusConsole()
{
    if (services_.focusConsole)
        services_.focusConsole();
}
void GridController::zoom(int direction)
{
    auto& fn = direction < 0 ? services_.zoomOut
                             : direction > 0 ? services_.zoomIn : services_.zoomReset;
    if (fn)
        fn();
}
void GridController::cut()
{
    if (copy())
        clearCells();
}
void GridController::pasteClipboard()
{
    paste(services_.readClipboard ? services_.readClipboard() : copiedClipboardText);
}
void GridController::capturePhrase(app::GridSelectionRange range)
{
    if (editable() && services_.capturePhrase)
        services_.capturePhrase(range.firstTrack, range.firstRow, range.lastRow);
}
void GridController::placePhrase(app::GridSelectionRange range, bool merge)
{
    if (editable() && services_.placePhrase)
        services_.placePhrase(range.firstTrack, range.firstRow, merge);
}
void GridController::transpose(app::GridSelectionRange range, int amount)
{
    if (editable()
        && transposeNoteRows(
            trackerState->session, range.firstTrack, range.firstRow, range.lastRow, amount))
        patternChanged();
}
void GridController::beginCellEditing(const std::string* initial)
{
    if (!editable() || !services_.beginText)
        return;
    auto edit = textEdit();
    if (initial)
        edit.text = *initial;
    services_.beginText(edit);
}
void GridController::writeCellState(NoteCellState state, bool advance)
{
    auto* model = trackerState;
    if (!model || model->session.pattern.tracks.empty())
        return;
    auto& session = model->session;
    const auto lane = std::min(session.selectedTrack, session.pattern.tracks.size() - 1u);
    auto& track = session.pattern.tracks[lane];
    const auto row = session.selectedRow;
    if (track.notes.size() <= row)
        track.notes.resize(row + 1u, NoteCell::rest());
    if (state == NoteCellState::Note)
        track.notes[row] = NoteCell::withNote(s3g::tracker::laneDefaultNote(session, lane));
    else if (state == NoteCellState::RetriggerPrevious)
        track.notes[row] = NoteCell::retriggerPrevious();
    else if (state == NoteCellState::Kill)
        track.notes[row] = NoteCell::kill();
    else if (state == NoteCellState::Hold)
        track.notes[row] = NoteCell::hold();
    else
        track.notes[row] = NoteCell::rest();
    track.noteColumn.length = std::max(track.noteColumn.length, row + 1u);
    session.pattern.visibleRows = std::max(session.pattern.visibleRows, row + 1u);
    if (advance)
        session.selectedRow = (row + 1u) % visibleRows(model);
    patternChanged();
}

void GridController::toggleSelectedCell(bool advance)
{
    auto* model = trackerState;
    if (!model || model->session.pattern.tracks.empty())
        return;
    const auto& session = model->session;
    const auto lane = std::min(session.selectedTrack, session.pattern.tracks.size() - 1u);
    const auto& notes = session.pattern.tracks[lane].notes;
    const bool hit = session.selectedRow < notes.size()
        && notes[session.selectedRow].state == NoteCellState::Note;
    writeCellState(hit ? NoteCellState::Rest : NoteCellState::Note, advance);
}

void GridController::adjustVolume(float delta)
{
    auto* model = trackerState;
    if (!model || model->session.pattern.tracks.empty())
        return;
    auto& session = model->session;
    auto& track = session.pattern
                      .tracks[std::min(session.selectedTrack, session.pattern.tracks.size() - 1u)];
    const auto row = session.selectedRow;
    if (track.velocities.size() <= row)
        track.velocities.resize(row + 1u, ValueCell::defaultValue());
    const float current = resolvedVelocity(track, row);
    auto& cell = track.velocities[row];
    if (cell.state == ValueCellState::Value && cell.valueVoiceCount() > 1u) {
        std::array<float, s3g::tracker::kMaximumNoteVoices> voices {};
        const auto count = cell.valueVoiceCount();
        for (std::size_t voice = 0u; voice < count; ++voice)
            voices[voice] = std::clamp(cell.valueVoice(voice) + delta, 0.0f, 1.0f);
        cell = ValueCell::withValues(voices, count);
    } else {
        cell = ValueCell::withValue(std::clamp(current + delta, 0.0f, 1.0f));
    }
    track.velocityColumn.length = std::max(track.velocityColumn.length, row + 1u);
    session.pattern.visibleRows = std::max(session.pattern.visibleRows, row + 1u);
    patternChanged();
}

void GridController::adjustFxValue(int delta)
{
    auto* model = trackerState;
    if (!model || model->session.pattern.tracks.empty()
        || !gridFieldIsSequence(model->session.selectedField))
        return;
    auto& session = model->session;
    auto& track = session.pattern
                      .tracks[std::min(session.selectedTrack, session.pattern.tracks.size() - 1u)];
    const auto pairIndex = gridSequencePair(session.selectedField);
    auto& pair = track.fxPairs[pairIndex];
    const auto row = session.selectedRow;
    if (pair.values.size() <= row)
        pair.values.resize(row + 1u, FxValueCell::previous());
    const float current = resolvedFxValue(track, pairIndex, row);
    auto& cell = pair.values[row];
    if (cell.state == FxValueCellState::Value && cell.valueVoiceCount() > 1u) {
        std::array<float, s3g::tracker::kMaximumNoteVoices> values {};
        const auto count = cell.valueVoiceCount();
        for (std::size_t voice = 0u; voice < count; ++voice) {
            const int scaled = static_cast<int>(std::lround(cell.valueVoice(voice) * 100.0f));
            values[voice] = static_cast<float>(std::clamp(scaled + delta, 0, 100)) / 100.0f;
        }
        cell = FxValueCell::withValues(values, count);
    } else {
        const int scaled = static_cast<int>(std::lround(current * 100.0f));
        cell = FxValueCell::withValue(
            static_cast<float>(std::clamp(scaled + delta, 0, 100)) / 100.0f);
    }
    pair.valueColumn.length = std::max(pair.valueColumn.length, row + 1u);
    session.pattern.visibleRows = std::max(session.pattern.visibleRows, row + 1u);
    patternChanged();
}

void GridController::writeFxState(bool previous, bool clear)
{
    auto* model = trackerState;
    if (!model || model->session.pattern.tracks.empty()
        || !gridFieldIsSequence(model->session.selectedField))
        return;
    auto& session = model->session;
    auto& track = session.pattern
                      .tracks[std::min(session.selectedTrack, session.pattern.tracks.size() - 1u)];
    auto& pair = track.fxPairs[gridSequencePair(session.selectedField)];
    const auto row = session.selectedRow;
    if (gridFieldIsSequenceAction(session.selectedField)) {
        if (pair.actions.size() <= row)
            pair.actions.resize(row + 1u, FxActionCell::empty());
        if (clear)
            pair.actions[row] = FxActionCell::empty();
        else if (previous)
            pair.actions[row] = FxActionCell::previous();
        else {
            pair.actions[row] = FxActionCell::sequencer(s3g::tracker::SequencerAction::Probability);
            if (pair.values.size() <= row)
                pair.values.resize(row + 1u, FxValueCell::previous());
            if (pair.values[row].state == FxValueCellState::Previous)
                pair.values[row] = FxValueCell::withValue(0.5f);
            pair.valueColumn.length = std::max(pair.valueColumn.length, row + 1u);
        }
        pair.actionColumn.length = std::max(pair.actionColumn.length, row + 1u);
    } else {
        if (pair.values.size() <= row)
            pair.values.resize(row + 1u, FxValueCell::previous());
        if (clear || previous)
            pair.values[row] = FxValueCell::previous();
        else
            pair.values[row] = FxValueCell::withValue(0.5f);
        pair.valueColumn.length = std::max(pair.valueColumn.length, row + 1u);
    }
    session.pattern.visibleRows = std::max(session.pattern.visibleRows, row + 1u);
    session.selectedRow = (row + 1u) % visibleRows(model);
    patternChanged();
}

bool GridController::keyDown(const GridKeyInput& event)
{
    const auto earlyModifiers = event.modifiers & (Command | Control | Alt | Shift);
    if (earlyModifiers == Control) {
        if (event.key == GridKey::Plus) {
            zoom(1);
            return true;
        }
        if (event.key == GridKey::Minus) {
            zoom(-1);
            return true;
        }
        if (event.key == GridKey::Zero) {
            zoom(0);
            return true;
        }
    }
    auto* model = trackerState;
    if (!model || model->session.pattern.tracks.empty()) {
        return false;
    }
    std::string key = event.text;
    for (auto& c : key)
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
    if (model->songPlaybackActive) {
        const auto navigationModifiers = event.modifiers & (Command | Control | Alt | Shift);
        if ((key == " ") && navigationModifiers == 0u) {
            togglePlayback();
            return true;
        }
        if ((key == ":") || (key == "`")) {
            focusConsole();
            return true;
        }
        auto& session = model->session;
        if (navigationModifiers == 0u
            && (event.key == GridKey::Left || event.key == GridKey::Right)) {
            const auto fieldCount = gridFieldCount(model->sequenceColumnsExpanded);
            session.selectedField = event.key == GridKey::Left
                ? (session.selectedField == 0u ? fieldCount - 1u : session.selectedField - 1u)
                : (session.selectedField + 1u) % fieldCount;
            selectionChanged();
            return true;
        }
        if (navigationModifiers == 0u && (event.key == GridKey::Down || event.key == GridKey::Up)) {
            const auto rows = playbackFollowVisibleRows(model);
            const auto jump
                = static_cast<std::size_t>(std::clamp<uint32_t>(model->trackerRowJump, 1u, 16u));
            session.selectedRow = event.key == GridKey::Up
                ? (session.selectedRow < jump ? 0u : session.selectedRow - jump)
                : std::min(session.selectedRow + jump, rows - 1u);
            selectionChanged();
            return true;
        }
        return false;
    }
    auto& session = model->session;
    const auto shortcutModifiers = event.modifiers & (Command | Control | Alt | Shift);
    if ((shortcutModifiers & Command) != 0u) {
        return false;
    }
    const bool trackerControl = shortcutModifiers == Control;
    const bool trackerControlShift = shortcutModifiers == (Control | Shift);
    const bool trackerControlOption = shortcutModifiers == (Control | Alt);
    const bool trackerControlOptionShift = shortcutModifiers == (Control | Alt | Shift);
    if (trackerControl && (key == "z")) {
        command("undo");
        return true;
    }
    if (trackerControlShift && (key == "z")) {
        command("redo");
        return true;
    }
    if (trackerControl && (key == "a")) {
        selectAll();
        return true;
    }
    if (trackerControl && (key == "c")) {
        copy();
        return true;
    }
    if (trackerControl && (key == "x")) {
        cut();
        return true;
    }
    if (trackerControl && (key == "v")) {
        pasteClipboard();
        return true;
    }
    if (trackerControlShift
        && (event.key == GridKey::Left || event.key == GridKey::Right || event.key == GridKey::Down
            || event.key == GridKey::Up)) {
        const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
        if (!selection.active) {
            selection.page = 0u;
            selection.anchorTrack = selection.focusTrack = session.selectedTrack;
            selection.anchorField = selection.focusField = session.selectedField;
            selection.anchorRow = selection.focusRow = session.selectedRow;
        }
        if (event.key == GridKey::Left || event.key == GridKey::Right) {
            auto column = gridClipboardColumn(selection.focusTrack, selection.focusField, fields);
            const auto maximum = model->session.pattern.tracks.size() * fields - 1u;
            column = event.key == GridKey::Left ? (column == 0u ? 0u : column - 1u)
                                                : std::min(column + 1u, maximum);
            s3g::tracker::app::gridAddressForClipboardColumn(
                column, fields, selection.focusTrack, selection.focusField);
        } else {
            selection.focusRow = event.key == GridKey::Up
                ? (selection.focusRow == 0u ? 0u : selection.focusRow - 1u)
                : std::min(selection.focusRow + 1u, visibleRows(model) - 1u);
        }
        selection.active = true;
        session.selectedTrack = selection.focusTrack;
        session.selectedField = selection.focusField;
        session.selectedRow = selection.focusRow;
        selectingWholeRows = false;

        selectionChanged();
        return true;
    }
    if (trackerControlShift && (key == "p")) {
        const auto range = effectiveGridSelection();
        if (range.firstTrack == range.lastTrack && range.rowCount() >= 2u
            && range.rowCount() <= s3g::tracker::kMaximumPhraseRows)
            capturePhrase(range);
        else
            reject();
        return true;
    }
    if (trackerControl && (key == "p")) {
        const auto range = effectiveGridSelection();
        placePhrase(range, false);
        return true;
    }
    if (trackerControlOption && key.size() == 1u) {
        const char digit = key.front();
        if (digit >= '0' && digit <= '9') {
            model->trackerRowJump = digit == '0' ? 10u : static_cast<uint32_t>(digit - '0');
            if (true && callbacks_.viewPreferencesChanged)
                callbacks_.viewPreferencesChanged();
            selectionChanged();
            return true;
        }
    }
    if ((trackerControlOption || trackerControlOptionShift)
        && (event.key == GridKey::Down || event.key == GridKey::Up)) {
        const auto range = effectiveGridSelection();
        const auto fields = gridFieldCount(model->sequenceColumnsExpanded);
        const bool oneNoteColumn = selection.active
            ? selection.firstColumn(fields) == selection.lastColumn(fields)
                && selection.firstColumn(fields) % fields == 0u
            : session.selectedField == 0u;
        if (!oneNoteColumn) {
            reject();
            return true;
        }
        transpose(
            range, (event.key == GridKey::Up ? 1 : -1) * (trackerControlOptionShift ? 12 : 1));
        return true;
    }
    const auto editingModifiers = event.modifiers & (Command | Control | Alt);
    if (editingModifiers != 0u) {
        return false;
    }
    if ((event.key == GridKey::Backspace || event.key == GridKey::Delete) && selection.active) {
        clearCells();
        return true;
    }
    clearGridSelection();
    if ((key == ":") || (key == "`")) {
        focusConsole();
        return true;
    }
    if ((key == " ")) {
        if ((event.modifiers & Shift) != 0u) {
            toggleLoop();
            return true;
        }
        togglePlayback();
        return true;
    }
    if ((key == "\r")) {
        beginCellEditing();
        return true;
    }
    if (event.key == GridKey::Tab) {
        const bool backwards = (event.modifiers & Shift) != 0u;
        if (backwards) {
            if (session.selectedField > 0u) {
                --session.selectedField;
            } else {
                session.selectedField = gridFieldCount(model->sequenceColumnsExpanded) - 1u;
            }
        } else if (session.selectedField + 1u < gridFieldCount(model->sequenceColumnsExpanded)) {
            ++session.selectedField;
        } else {
            session.selectedField = 0u;
        }
        selectionChanged();
        return true;
    }
    const bool shift = (event.modifiers & Shift) != 0u;
    if (event.key == GridKey::Left || event.key == GridKey::Right) {
        if (shift) {
            const auto nextTrack = event.key == GridKey::Left
                ? (session.selectedTrack == 0u ? 0u : session.selectedTrack - 1u)
                : std::min(session.selectedTrack + 1u, model->session.pattern.tracks.size() - 1u);
            select({ nextTrack, trackerState->session.selectedField, session.selectedRow });
            return true;
        }
        if (event.key == GridKey::Left) {
            if (session.selectedField > 0u) {
                --session.selectedField;
            } else {
                session.selectedField = gridFieldCount(model->sequenceColumnsExpanded) - 1u;
            }
        } else if (session.selectedField + 1u < gridFieldCount(model->sequenceColumnsExpanded)) {
            ++session.selectedField;
        } else {
            session.selectedField = 0u;
        }
        selectionChanged();
        return true;
    }
    if (event.key == GridKey::Home || event.key == GridKey::End) {
        select({ session.selectedTrack, trackerState->session.selectedField,
            event.key == GridKey::Home ? 0u : visibleRows(model) - 1u });
        return true;
    }
    if (event.key == GridKey::PageUp || event.key == GridKey::PageDown) {
        const double visibleHeight = event.visibleHeight;
        const auto pageRows
            = std::max<std::size_t>(1u, static_cast<std::size_t>(visibleHeight / kGridRowHeight));
        const auto next = event.key == GridKey::PageUp
            ? session.selectedRow > pageRows ? session.selectedRow - pageRows : 0u
            : std::min(session.selectedRow + pageRows, visibleRows(model) - 1u);
        select({ session.selectedTrack, trackerState->session.selectedField, next });
        return true;
    }
    if (event.key == GridKey::F9 || event.key == GridKey::F10 || event.key == GridKey::F11
        || event.key == GridKey::F12) {
        std::size_t numerator = 0u;
        if (event.key == GridKey::F10)
            numerator = 1u;
        else if (event.key == GridKey::F11)
            numerator = 2u;
        else if (event.key == GridKey::F12)
            numerator = 3u;
        const auto row = numerator * (visibleRows(model) - 1u) / 4u;
        select({ session.selectedTrack, trackerState->session.selectedField, row });
        return true;
    }
    if (!shift && session.selectedField == 0u && key.size() == 1u) {
        const char direct = key.front();
        if ((direct >= '0' && direct <= '9') || (direct >= 'a' && direct <= 'g')) {
            beginCellEditing(&key);
            return true;
        }
    }
    if (!shift && session.selectedField == 1u && key.size() == 1u) {
        const char direct = key.front();
        if ((direct >= '0' && direct <= '9') || direct == '.') {
            beginCellEditing(&key);
            return true;
        }
    }
    if (!shift && gridFieldIsSequence(session.selectedField) && key.size() == 1u) {
        const char direct = key.front();
        if ((gridFieldIsSequenceAction(session.selectedField)
                && ((direct >= 'a' && direct <= 'z') || direct == '-'))
            || (!gridFieldIsSequenceAction(session.selectedField)
                && ((direct >= '0' && direct <= '9') || direct == '.'))) {
            beginCellEditing(&key);
            return true;
        }
    }
    if (!shift && gridFieldIsGate(session.selectedField) && key.size() == 1u) {
        const char direct = key.front();
        if ((direct >= '0' && direct <= '9') || direct == '.' || direct == 'd' || direct == 't') {
            beginCellEditing(&key);
            return true;
        }
    }
    if (event.key == GridKey::Down) {
        const auto jump
            = static_cast<std::size_t>(std::clamp<uint32_t>(model->trackerRowJump, 1u, 16u));
        const auto next = std::min(session.selectedRow + jump, visibleRows(model) - 1u);
        if (shift) {
            if (loopAnchorRow < 0)
                loopAnchorRow = static_cast<int64_t>(session.selectedRow);
            selectLoop(static_cast<std::size_t>(loopAnchorRow), next);
        } else
            loopAnchorRow = -1;
        select({ session.selectedTrack, trackerState->session.selectedField, next });
        return true;
    }
    if (event.key == GridKey::Up) {
        const auto jump
            = static_cast<std::size_t>(std::clamp<uint32_t>(model->trackerRowJump, 1u, 16u));
        const auto next = session.selectedRow < jump ? 0u : session.selectedRow - jump;
        if (shift) {
            if (loopAnchorRow < 0)
                loopAnchorRow = static_cast<int64_t>(session.selectedRow);
            selectLoop(static_cast<std::size_t>(loopAnchorRow), next);
        } else
            loopAnchorRow = -1;
        select({ session.selectedTrack, trackerState->session.selectedField, next });
        return true;
    }
    if ((key == "x") && session.selectedField == 0u) {
        toggleSelectedCell(true);
        return true;
    }
    if (event.key == GridKey::Backspace || event.key == GridKey::Delete) {
        if (session.selectedField == 0u)
            writeCellState(NoteCellState::Rest, true);
        else if (session.selectedField == 1u)
            adjustVolume(0.0f);
        else if (gridFieldIsGate(session.selectedField)) {
            auto& track
                = session.pattern
                      .tracks[std::min(session.selectedTrack, session.pattern.tracks.size() - 1u)];
            if (applyCellText(
                    *trackerState, "DEF", track, session.selectedRow, session.selectedField))
                patternChanged();
        } else
            writeFxState(false, true);
        return true;
    }
    if ((key == "r")) {
        if (session.selectedField == 0u)
            writeCellState(NoteCellState::RetriggerPrevious, true);
        else if (gridFieldIsSequence(session.selectedField))
            writeFxState(true, false);
        else
            return false;
        return true;
    }
    if ((key == "k")) {
        if (session.selectedField == 0u)
            writeCellState(NoteCellState::Kill, true);
        else
            return false;
        return true;
    }
    if ((key == "h")) {
        if (session.selectedField == 0u)
            writeCellState(NoteCellState::Hold, true);
        else
            return false;
        return true;
    }
    if ((key == "[")) {
        if (session.selectedField == 1u)
            adjustVolume(-0.05f);
        else if (gridFieldIsSequence(session.selectedField)
            && !gridFieldIsSequenceAction(session.selectedField))
            adjustFxValue(-5);
        else
            return false;
        return true;
    }
    if ((key == "]")) {
        if (session.selectedField == 1u)
            adjustVolume(0.05f);
        else if (gridFieldIsSequence(session.selectedField)
            && !gridFieldIsSequenceAction(session.selectedField))
            adjustFxValue(5);
        else
            return false;
        return true;
    }
    if ((key == "m")) {
        const auto lane = std::min(session.selectedTrack, session.pattern.tracks.size() - 1u);
        session.selectedTrack = lane;
        auto& track = session.pattern.tracks[lane];
        auto& muted = columnForField(track, 0u, session.selectedField)->muted;
        muted = !muted;
        patternChanged();
        return true;
    }
    return false;
}

}
