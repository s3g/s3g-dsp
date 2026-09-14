#include "s3g/tracker/editor_grid_controller.h"
#include <algorithm>
namespace s3g::tracker::editor {
namespace {
    constexpr std::size_t kGridMaximumRows = 256;
    template <typename Cell>
    void insertPatternRowCell(std::vector<Cell>& cells, std::size_t row, std::size_t oldRows,
        std::size_t count, const Cell& blank)
    {
        cells.resize(oldRows, blank);
        cells.insert(cells.begin() + static_cast<std::ptrdiff_t>(row), count, blank);
    }

    template <typename Cell>
    void deletePatternRowCell(std::vector<Cell>& cells, std::size_t row, std::size_t oldRows,
        std::size_t count, const Cell& blank)
    {
        cells.resize(oldRows, blank);
        const auto first = cells.begin() + static_cast<std::ptrdiff_t>(row);
        cells.erase(first, first + static_cast<std::ptrdiff_t>(count));
    }

    void insertPatternColumnRows(ColumnDefinition& column, std::size_t row, std::size_t count)
    {
        if (row >= column.length || column.length >= kGridMaximumRows)
            return;
        if (column.phase >= row)
            column.phase += count;
        column.length = std::min(kGridMaximumRows, column.length + count);
        column.phase %= column.length;
    }

    void deletePatternColumnRows(ColumnDefinition& column, std::size_t row, std::size_t count)
    {
        if (row >= column.length)
            return;
        const std::size_t removed = std::min(count, column.length - row);
        if (column.phase >= row + removed)
            column.phase -= removed;
        else if (column.phase >= row)
            column.phase = row;
        column.length = std::max<std::size_t>(1u, column.length - removed);
        column.phase %= column.length;
    }

}
void GridController::insertRowsAt(std::size_t row, std::size_t requested, bool pasteClipboard)
{
    auto* model = trackerState;
    if (!model || model->songPlaybackActive || requested == 0u)
        return;
    auto& pattern = model->session.pattern;
    const std::size_t oldRows = std::clamp<std::size_t>(pattern.visibleRows, 1u, kGridMaximumRows);
    if (oldRows >= kGridMaximumRows)
        return;
    row = std::min(row, oldRows);
    const std::size_t count = std::min(requested, kGridMaximumRows - oldRows);
    for (auto& track : pattern.tracks) {
        insertPatternRowCell(track.notes, row, oldRows, count, NoteCell::rest());
        insertPatternRowCell(track.instruments, row, oldRows, count, InstrumentCell::empty());
        insertPatternRowCell(track.velocities, row, oldRows, count, ValueCell::defaultValue());
        insertPatternRowCell(track.gates, row, oldRows, count, GateCell::defaultValue());
        insertPatternColumnRows(track.noteColumn, row, count);
        insertPatternColumnRows(track.instrumentColumn, row, count);
        insertPatternColumnRows(track.velocityColumn, row, count);
        insertPatternColumnRows(track.gateColumn, row, count);
        for (auto& pair : track.fxPairs) {
            insertPatternRowCell(pair.actions, row, oldRows, count, FxActionCell::empty());
            insertPatternRowCell(pair.values, row, oldRows, count, FxValueCell::previous());
            insertPatternColumnRows(pair.actionColumn, row, count);
            insertPatternColumnRows(pair.valueColumn, row, count);
        }
    }
    if (pasteClipboard && hasRowClipboard_) {
        const std::size_t lanes = std::min(pattern.tracks.size(), rowClipboard_.tracks.size());
        for (std::size_t lane = 0u; lane < lanes; ++lane) {
            auto& destination = pattern.tracks[lane];
            const auto& source = rowClipboard_.tracks[lane];
            const auto paste = [row, count](auto& into, const auto& from) {
                const std::size_t cells = std::min(count, from.size());
                for (std::size_t index = 0u; index < cells; ++index)
                    into[row + index] = from[index];
            };
            paste(destination.notes, source.notes);
            paste(destination.instruments, source.instruments);
            paste(destination.velocities, source.velocities);
            paste(destination.gates, source.gates);
            for (std::size_t pair = 0u; pair < destination.fxPairs.size(); ++pair) {
                paste(destination.fxPairs[pair].actions, source.fxPairs[pair].actions);
                paste(destination.fxPairs[pair].values, source.fxPairs[pair].values);
            }
        }
    }
    pattern.visibleRows = oldRows + count;
    auto& transport = model->session.transport;
    if (transport.loopStartRow >= row)
        transport.loopStartRow += count;
    if (transport.loopEndRow > row)
        transport.loopEndRow += count;
    model->session.selectedRow = row;
    selectWholeRows(row, row + count - 1u);
    patternChanged();
}
void GridController::deleteRows(std::size_t requestedRow, std::size_t requestedCount)
{
    auto* model = trackerState;
    if (!model || model->songPlaybackActive)
        return;
    auto& pattern = model->session.pattern;
    const std::size_t oldRows = std::clamp<std::size_t>(pattern.visibleRows, 1u, kGridMaximumRows);
    if (oldRows <= 16u)
        return;
    const std::size_t row = std::min<std::size_t>(requestedRow, oldRows - 1u);
    const std::size_t count
        = std::min<std::size_t>({ requestedCount, oldRows - row, oldRows - 16u });
    if (count == 0u)
        return;
    for (auto& track : pattern.tracks) {
        deletePatternRowCell(track.notes, row, oldRows, count, NoteCell::rest());
        deletePatternRowCell(track.instruments, row, oldRows, count, InstrumentCell::empty());
        deletePatternRowCell(track.velocities, row, oldRows, count, ValueCell::defaultValue());
        deletePatternRowCell(track.gates, row, oldRows, count, GateCell::defaultValue());
        deletePatternColumnRows(track.noteColumn, row, count);
        deletePatternColumnRows(track.instrumentColumn, row, count);
        deletePatternColumnRows(track.velocityColumn, row, count);
        deletePatternColumnRows(track.gateColumn, row, count);
        for (auto& pair : track.fxPairs) {
            deletePatternRowCell(pair.actions, row, oldRows, count, FxActionCell::empty());
            deletePatternRowCell(pair.values, row, oldRows, count, FxValueCell::previous());
            deletePatternColumnRows(pair.actionColumn, row, count);
            deletePatternColumnRows(pair.valueColumn, row, count);
        }
    }
    pattern.visibleRows = oldRows - count;
    auto& transport = model->session.transport;
    if (transport.loopStartRow >= row + count)
        transport.loopStartRow -= count;
    else if (transport.loopStartRow >= row)
        transport.loopStartRow = static_cast<uint32_t>(row);
    if (transport.loopEndRow >= row + count)
        transport.loopEndRow -= count;
    else if (transport.loopEndRow > row)
        transport.loopEndRow = static_cast<uint32_t>(row);
    transport.loopStartRow = std::min<uint32_t>(
        transport.loopStartRow, static_cast<uint32_t>(pattern.visibleRows - 1u));
    transport.loopEndRow = std::clamp<uint32_t>(transport.loopEndRow, transport.loopStartRow + 1u,
        static_cast<uint32_t>(pattern.visibleRows));
    model->session.selectedRow = std::min(row, pattern.visibleRows - 1u);
    clearGridSelection();
    patternChanged();
}
void GridController::copyRows(std::size_t requestedRow, std::size_t requestedCount)
{
    auto* model = trackerState;
    if (!model)
        return;
    const std::size_t rows = std::max<std::size_t>(model->session.pattern.visibleRows, 1u);
    const std::size_t row = std::min<std::size_t>(requestedRow, rows - 1u);
    const std::size_t count = std::min<std::size_t>(requestedCount, rows - row);
    if (count == 0u)
        return;
    rowClipboard_ = model->session.pattern;
    rowClipboard_.visibleRows = count;
    const auto copyRange = [row, count, rows](auto& cells, const auto& blank) {
        cells.resize(rows, blank);
        std::vector<std::decay_t<decltype(cells.front())>> copied(
            cells.begin() + static_cast<std::ptrdiff_t>(row),
            cells.begin() + static_cast<std::ptrdiff_t>(row + count));
        cells = std::move(copied);
    };
    for (auto& track : rowClipboard_.tracks) {
        copyRange(track.notes, NoteCell::rest());
        copyRange(track.instruments, InstrumentCell::empty());
        copyRange(track.velocities, ValueCell::defaultValue());
        copyRange(track.gates, GateCell::defaultValue());
        for (auto& pair : track.fxPairs) {
            copyRange(pair.actions, FxActionCell::empty());
            copyRange(pair.values, FxValueCell::previous());
        }
    }
    hasRowClipboard_ = true;
}

}
