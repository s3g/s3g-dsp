#pragma once
#include "s3g/tracker/editor_grid.h"
#include <functional>
#include <optional>

namespace s3g::tracker::editor {

struct GridAddress {
    std::size_t track = 0, field = 0, row = 0;
};
enum class GridKey {
    None,
    Plus,
    Minus,
    Zero,
    Left,
    Right,
    Down,
    Up,
    Home,
    End,
    PageUp,
    PageDown,
    F9,
    F10,
    F11,
    F12,
    Backspace,
    Delete,
    Tab
};
enum GridModifier : uint32_t { Shift = 1, Control = 2, Alt = 4, Command = 8 };
struct GridKeyInput {
    GridKey key = GridKey::None;
    std::string text;
    uint32_t modifiers = 0;
    double visibleHeight = 200;
};
enum class GridEditKind { Cell, TrackName, Length, ReadStart };
struct GridTextEdit {
    GridAddress address;
    GridEditKind kind = GridEditKind::Cell;
    Rect bounds;
    std::string text;
};
struct GridServices {
    std::function<void()> invalidate;
    std::function<void(Rect)> reveal;
    std::function<void(const GridTextEdit&)> beginText;
    std::function<std::string()> readClipboard;
    std::function<void(const std::string&)> writeClipboard;
    std::function<uint64_t()> clipboardRevision;
    std::function<void()> invalidEdit;
    std::function<void(const std::string&)> message;
    std::function<void()> focusConsole;
    std::function<void()> zoomIn, zoomOut, zoomReset;
    std::function<void(std::size_t, std::size_t, std::size_t)> capturePhrase;
    std::function<void(std::size_t, std::size_t, bool)> placePhrase;
};

// UI-thread command controller. The host owns history and runtime publication;
// pointer gestures edit the UI model but publish only at the original commit
// boundary. No timer advances playback and no operation reaches the scheduler.
class GridController {
public:
    GridController(app::TrackerViewState& state, app::WorkspaceCallbacks& callbacks,
        GridServices services = {});
    app::GridSelection selection;
    bool selectingWholeRows = false;
    app::GridSelectionRange effectiveGridSelection();
    bool editable() const;
    void clearGridSelection();
    void select(GridAddress address, bool extend = false);
    void selectAll();
    void selectWholeRows(std::size_t anchor, std::size_t row);
    void selectLoop(std::size_t anchor, std::size_t row);
    Rect cellRect(GridAddress address) const;
    std::optional<GridAddress> addressAt(Point point, bool clampRows = false) const;
    GridTextEdit textEdit(GridEditKind kind = GridEditKind::Cell) const;
    bool commitText(const GridTextEdit& edit, std::string_view text);
    bool clearCells();
    bool copy();
    bool paste(std::string_view text);
    void beginValueDrag(GridAddress address);
    void updateValueDrag(double verticalDelta, bool fine, bool coarse);
    void finishValueDrag();
    void paintEnvelope(Point point, Rect bounds, bool clear);
    bool keyDown(const GridKeyInput& event);
    void beginCellEditing(const std::string* initial = nullptr);
    void writeCellState(NoteCellState state, bool advance);
    void toggleSelectedCell(bool advance);
    void adjustVolume(float delta);
    void adjustFxValue(int delta);
    void writeFxState(bool previous, bool clear);
    void reverseGridSelection(int tag = 0);
    void rotateGridSelection(int tag);
    void adjustSelectedValues(int tag);
    void scaleSelectedValues(int tag);
    void quantizeSelectedMicroTime(int tag);
    void fillSelectionFromEdge(int tag);
    void fillSelectionSeries(int tag = 0);
    void repeatGridSelection(int tag);
    void shiftSelectionCells(int tag);
    void moveGridSelection(int tag);
    void stretchGridSelection(int tag);
    void materializeGridSelection(int tag = 0);
    void findReplaceGridSelection(int tag = 0);
    void swapGridSelectionWithNextLane(int tag = 0);
    void moveCompleteLane(int tag);
    void pasteGridSelectionSpecial(int tag);
    void splitSelectedNoteColumnByPitch(int tag = 0);
    void mergeSelectedNoteLanes(int tag = 0);
    void insertRowsAt(std::size_t row, std::size_t count, bool pasteClipboard = false);
    void deleteRows(std::size_t row, std::size_t count);
    void copyRows(std::size_t row, std::size_t count);
    bool hasRowClipboard() const { return hasRowClipboard_; }
    bool hasGridClipboard() const { return !copiedGridCells.empty(); }
    std::string selectionStatistics();
    std::size_t rowClipboardCount() const { return rowClipboard_.visibleRows; }
    void patternChanged();
    void selectionChanged();
    void reject(const std::string& message = "This edit is not valid for the selected cells.");
    const GridServices& services() const { return services_; }

private:
    void command(const std::string& text);
    void togglePlayback();
    void toggleLoop();
    void focusConsole();
    void zoom(int direction);
    void cut();
    void pasteClipboard();
    void capturePhrase(app::GridSelectionRange range);
    void placePhrase(app::GridSelectionRange range, bool merge);
    void transpose(app::GridSelectionRange range, int amount);
    app::TrackerViewState* trackerState;
    app::WorkspaceCallbacks& callbacks_;
    GridServices services_;
    std::vector<uint8_t> copiedColumnTypes;
    std::vector<GridCell> copiedGridCells;
    std::string copiedClipboardText;
    std::size_t copiedRowCount = 0;
    uint64_t copiedRevision_ = 0;
    std::optional<GridAddress> valueDrag_;
    GridCell valueDragOriginal_ = ValueCell::defaultValue();
    float valueDragStart_ = 0;
    bool valueDragChanged_ = false;
    Pattern rowClipboard_;
    bool hasRowClipboard_ = false;
    int64_t loopAnchorRow = -1;
};
}
