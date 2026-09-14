#include "s3g/tracker/fx_catalog.h"
#include "s3g_tracker_main_page.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/platform/iplatformfont.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace s3g::tracker::editor {
using namespace VSTGUI;
namespace {
    CRect rect(double x, double y, double w, double h) { return { x, y, x + w, y + h }; }
    bool contains(CRect r, CPoint p)
    {
        return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
    }
    MainMenuItem separator() { return { "", {}, {}, false }; }
    MainMenuItem group(std::string name, std::vector<MainMenuItem> items, bool enabled = true)
    {
        return { std::move(name), {}, std::move(items), enabled };
    }
    std::string padded(std::size_t value, unsigned width = 2)
    {
        auto s = std::to_string(value);
        return std::string(s.size() < width ? width - s.size() : 0, '0') + s;
    }
    const BurstLibrary* workspaceBurstLibrary(const app::TrackerViewState& state, AssetBankId id)
    {
        if (id == state.activeBurstBankId)
            return &state.session.burstLibrary;
        auto* bank = findBurstBank(state.burstBanks, id);
        return bank ? &bank->library : nullptr;
    }
}

void MainPageView::openMenu(CRect anchor, std::vector<MainMenuItem> items, bool child)
{
    if (items.empty())
        return;
    if (!child) {
        finishText();
        popups_.clear();
    }
    const bool flat = !child && anchor.getWidth() > 0;
    const int columns = flat ? std::max(1, (int(items.size()) + 37) / 38) : 1;
    const int rows = std::min(38, (int(items.size()) + columns - 1) / columns);
    double width = std::max(132., anchor.getWidth());
    if (child) {
        // A child is sized to its own rendered labels, never to the parent
        // menu's width. Measure the same font and uppercase text we paint;
        // UTF-8 byte counts greatly overestimate names containing symbols.
        const auto info = services_.suiteFont(10);
        auto font = services_.fontFactory ? services_.fontFactory(info.name, info.size)
                                         : portable_gui::foundation::makeUiFont(info.size);
        auto* painter = font->getFontPainter();
        width = 18.; // Nine-point left/right margins; no inherited minimum.
        for (const auto& item : items) {
            const auto title = fitMenuText(item.title, 0);
            const auto glyphs = std::count_if(title.begin(), title.end(), [](unsigned char c) {
                return (c & 0xc0) != 0x80;
            });
            const double textWidth = painter
                ? painter->getStringWidth(
                      nullptr, UTF8String(title.c_str()).getPlatformString(), true)
                : double(glyphs) * info.size;
            width = std::max(width, std::ceil(textWidth) + (item.children.empty() ? 18. : 24.));
        }
    } else if (!flat)
        for (const auto& item : items)
            width = std::max(width, 24. + double(item.title.size()) * 6.1);
    width = flat ? std::min(width * columns, 1304.) : std::min(width, 610.);
    double height = 21. * rows;
    double x = child ? anchor.right : anchor.left;
    double y = child ? anchor.top : anchor.bottom + 2;
    if (x + width > 1312)
        x = child ? anchor.left - width : 1312 - width;
    if (y + height > 812)
        y = child ? 812 - height : anchor.top - height - 2;
    popups_.push_back(
        { rect(std::clamp(x, 8., 1312. - width), std::clamp(y, 8., 812. - height), width, height),
            std::move(items), -1, 0, columns, rows });
    focusTracker();
    invalid();
}
CRect MainPageView::popupBounds(std::size_t level) const
{
    return level < popups_.size() ? popups_[level].bounds : CRect {};
}
void MainPageView::drawMenus()
{
    for (const auto& p : popups_) {
        fill(CRect(p.bounds).inset(-2, -2), 0x080808);
        fill(p.bounds, 0x151515);
        stroke(p.bounds, 0x6c6c6c);
        auto visible = p.rows * p.columns;
        for (int row = 0; row < visible && row + p.scroll < int(p.items.size()); ++row) {
            int i = row + p.scroll;
            const auto& item = p.items[std::size_t(i)];
            auto column = row / p.rows;
            auto localRow = row % p.rows;
            auto width = p.bounds.getWidth() / p.columns;
            auto b = rect(p.bounds.left + column * width, p.bounds.top + localRow * 21, width, 21);
            if (i == p.hover || item.checked)
                fill(CRect(b).inset(1, 1), i == p.hover ? 0x343434 : 0x292929);
            else if (localRow % 2)
                fill(CRect(b).inset(1, 1), 0x131313);
            if (localRow)
                fill(rect(b.left, b.top, b.getWidth(), 1), 0x3a3a3a);
            if (column)
                fill(rect(b.left, b.top, 1, b.getHeight()), 0x6c6c6c);
            if (item.checked || i == p.hover)
                fill(rect(b.left + 2, b.top + 2, 3, 17), 0x7f7f7f);
            label(fitMenuText(item.title, b.getWidth() - (item.children.empty() ? 18 : 24)),
                rect(b.left + 9, b.top + 4, b.getWidth() - 18, 16),
                item.enabled ? 0x929292 : 0x656565);
            if (!item.children.empty())
                label(">", rect(b.right - 14, b.top + 4, 9, 16));
        }
    }
}
void MainPageView::activateMenu(std::size_t level, int row)
{
    if (level >= popups_.size() || row < 0 || row >= int(popups_[level].items.size()))
        return;
    auto item = popups_[level].items[std::size_t(row)];
    if (!item.enabled)
        return;
    auto anchor = popups_[level].bounds;
    anchor.top += (row - popups_[level].scroll) % popups_[level].rows * 21;
    anchor.bottom = anchor.top + 21;
    if (!item.children.empty()) {
        popups_.resize(level + 1);
        openMenu(anchor, std::move(item.children), true);
        return;
    }
    popups_.clear();
    if (item.action)
        item.action();
    invalid();
    focusTracker();
}
bool MainPageView::menuPointer(CPoint point, bool click)
{
    if (popups_.empty())
        return false;
    for (std::size_t reverse = popups_.size(); reverse > 0; --reverse) {
        auto level = reverse - 1;
        auto& p = popups_[level];
        if (!contains(p.bounds, point))
            continue;
        int row = int((point.x - p.bounds.left) / (p.bounds.getWidth() / p.columns)) * p.rows
            + int((point.y - p.bounds.top) / 21) + p.scroll;
        if (row >= int(p.items.size()))
            return true;
        if (p.hover != row) {
            p.hover = row;
            popups_.resize(level + 1);
            invalid();
            if (!p.items[std::size_t(row)].children.empty() && p.items[std::size_t(row)].enabled)
                activateMenu(level, row);
        }
        if (click && level < popups_.size()
            && popups_[level].items[std::size_t(row)].children.empty())
            activateMenu(level, row);
        return true;
    }
    if (click) {
        popups_.clear();
        invalid();
    }
    return true;
}

void MainPageView::burstAction(const std::string& kind, GridAddress address, AssetBankId bankId,
    std::size_t slot, std::size_t count)
{
    auto* model = &state_;
    if (model->songPlaybackActive)
        return;
    auto& pattern = model->session.pattern;
    auto& bursts = model->session.burstLibrary.bursts;
    const auto trackIndex = address.track;
    const auto row = address.row;
    if (trackIndex >= pattern.tracks.size() || row >= 256)
        return;
    const auto selection = grid_.effectiveGridSelection();
    auto& track = pattern.tracks[trackIndex];
    const auto findEmpty = [&]() -> std::size_t {
        const auto found = std::find_if(bursts.begin(), bursts.end(),
            [](const BurstDefinition& burst) { return burst.empty(); });
        return found == bursts.end() ? bursts.size()
                                     : static_cast<std::size_t>(found - bursts.begin());
    };
    if ((kind == "create") || (kind == "capture")) {
        slot = findEmpty();
        if (slot >= bursts.size())
            return;
        BurstDefinition burst;
        burst.name = (kind == "capture") ? "ROW CAPTURE" : "EVEN " + std::to_string(count);
        if ((kind == "create")) {
            const auto eventCount = std::clamp<std::size_t>(count, 1u, kMaximumBurstEvents);
            burst.eventCount = static_cast<uint8_t>(eventCount);
            const auto note = laneDefaultNote(model->session, trackIndex);
            for (std::size_t index = 0u; index < eventCount; ++index) {
                burst.events[index] = {
                    static_cast<uint16_t>(index * 65536u / eventCount),
                    note,
                    127u,
                    70u,
                };
            }
        } else {
            const auto first = selection.firstRow;
            const auto last = selection.lastRow;
            const auto span = std::max<std::size_t>(last - first + 1u, 1u);
            for (std::size_t sourceRow = first;
                 sourceRow <= last && burst.eventCount < kMaximumBurstEvents; ++sourceRow) {
                if (sourceRow >= track.notes.size()
                    || track.notes[sourceRow].state != NoteCellState::Note)
                    continue;
                auto& event = burst.events[burst.eventCount++];
                event.position = static_cast<uint16_t>((sourceRow - first) * 65536u / span);
                event.note = track.notes[sourceRow].note;
                event.velocity = static_cast<uint8_t>(std::clamp<long>(
                    std::lround(resolvedVelocity(track, sourceRow) * 127.0f), 1l, 127l));
                event.gatePercent = 70u;
            }
            if (burst.empty())
                return;
            if (track.notes.size() <= last)
                track.notes.resize(last + 1u, NoteCell::rest());
            for (std::size_t clear = first; clear <= last; ++clear)
                track.notes[clear] = NoteCell::rest();
        }
        bursts[slot] = burst;
        if (track.notes.size() <= row)
            track.notes.resize(row + 1u, NoteCell::rest());
        track.notes[row]
            = NoteCell::withBurst(static_cast<uint8_t>(slot), model->activeBurstBankId);
    } else if ((kind == "use")) {
        const auto* library = workspaceBurstLibrary(*model, bankId);
        if (!library || slot >= library->bursts.size() || library->bursts[slot].empty())
            return;
        if (track.notes.size() <= row)
            track.notes.resize(row + 1u, NoteCell::rest());
        track.notes[row] = NoteCell::withBurst(static_cast<uint8_t>(slot), bankId);
    } else if ((kind == "duplicate")) {
        if (bankId != model->activeBurstBankId && callbacks_.selectBurstBank)
            callbacks_.selectBurstBank(bankId);
        auto& selectedBursts = model->session.burstLibrary.bursts;
        if (slot >= selectedBursts.size() || selectedBursts[slot].empty())
            return;
        const auto found = std::find_if(selectedBursts.begin(), selectedBursts.end(),
            [](const BurstDefinition& definition) { return definition.empty(); });
        const auto destination = found == selectedBursts.end()
            ? selectedBursts.size()
            : static_cast<std::size_t>(found - selectedBursts.begin());
        if (destination >= selectedBursts.size())
            return;
        selectedBursts[destination] = selectedBursts[slot];
        selectedBursts[destination].name += " COPY";
        grid_.patternChanged();
        if (services_.editBurst)
            services_.editBurst(destination);
        return;
    } else if ((kind == "edit")) {
        if (bankId != model->activeBurstBankId && callbacks_.selectBurstBank)
            callbacks_.selectBurstBank(bankId);
        if (slot >= model->session.burstLibrary.bursts.size()
            || model->session.burstLibrary.bursts[slot].empty())
            return;
        if (services_.editBurst)
            services_.editBurst(slot);
        return;
    } else if ((kind == "expand")) {
        const auto* library = workspaceBurstLibrary(*model, bankId);
        if (!library || slot >= library->bursts.size() || library->bursts[slot].empty())
            return;
        const auto burst = library->bursts[slot];
        const auto required = std::min<std::size_t>(std::size_t(256), row + burst.eventCount);
        track.notes.resize(std::max(track.notes.size(), required), NoteCell::rest());
        track.velocities.resize(
            std::max(track.velocities.size(), required), ValueCell::defaultValue());
        for (std::size_t index = 0u; index < burst.eventCount && row + index < required; ++index) {
            track.notes[row + index] = NoteCell::withNote(burst.events[index].note);
            track.velocities[row + index]
                = ValueCell::withValue(static_cast<float>(burst.events[index].velocity) / 127.0f);
        }
        pattern.visibleRows = std::max(pattern.visibleRows, required);
        track.noteColumn.length = std::max(track.noteColumn.length, required);
        track.velocityColumn.length = std::max(track.velocityColumn.length, required);
    } else if ((kind == "convert")) {
        const auto* library = workspaceBurstLibrary(*model, bankId);
        if (!library || slot >= library->bursts.size() || library->bursts[slot].empty())
            return;
        track.notes[row] = NoteCell::withNote(library->bursts[slot].events[0u].note);
    } else
        return;
    track.noteColumn.length = std::max(track.noteColumn.length, row + 1u);
    pattern.visibleRows = std::max(pattern.visibleRows, row + 1u);
    model->session.selectedTrack = trackIndex;
    model->session.selectedRow = row;
    model->session.selectedField = 0u;
    grid_.clearGridSelection();
    grid_.patternChanged();
    focusTracker();
}

void MainPageView::appendBurstMenu(std::vector<MainMenuItem>& items, GridAddress address)
{
    const auto& bursts = state_.session.burstLibrary.bursts;
    const auto& notes = state_.session.pattern.tracks[address.track].notes;
    auto current = address.row < notes.size() ? notes[address.row] : NoteCell::rest();
    const bool hasEmpty = std::any_of(
        bursts.begin(), bursts.end(), [](const BurstDefinition& b) { return b.empty(); });
    std::vector<MainMenuItem> menu;
    auto add = [&](std::string title, std::string kind, bool enabled, AssetBankId bank,
                   std::size_t slot, std::size_t count = 0) {
        menu.push_back({ std::move(title),
            [this, address, kind, bank, slot, count] {
                burstAction(kind, address, bank, slot, count);
                reloadModel();
            },
            {}, enabled && grid_.editable() });
    };
    for (std::size_t n : { 2, 3, 4, 6, 8 })
        add("CREATE " + std::to_string(n) + " EVEN SUBSTEPS", "create", hasEmpty,
            state_.activeBurstBankId, 0, n);
    auto r = grid_.effectiveGridSelection();
    auto fields = gridFieldCount(state_.sequenceColumnsExpanded);
    bool convertible = grid_.selection.active
        && grid_.selection.firstColumn(fields) == address.track * fields
        && grid_.selection.lastColumn(fields) == address.track * fields && r.rowCount() >= 2
        && r.rowCount() <= kMaximumBurstEvents;
    add("CREATE FROM SELECTED NOTE ROWS", "capture", hasEmpty && convertible,
        state_.activeBurstBankId, 0);
    menu.push_back(separator());
    std::vector<MainMenuItem> banks;
    for (const auto& bank : state_.burstBanks) {
        auto* library = workspaceBurstLibrary(state_, bank.id);
        if (!library)
            continue;
        std::vector<MainMenuItem> entries;
        for (std::size_t slot = 0; slot < library->bursts.size(); ++slot) {
            const auto& b = library->bursts[slot];
            if (b.empty())
                continue;
            entries.push_back({ assetBankToken(bank.id) + ":" + burstSlotToken(slot) + "  ·  "
                    + b.name + "  ·  " + std::to_string(b.eventCount) + " STEPS",
                [this, address, id = bank.id, slot] {
                    burstAction("use", address, id, slot);
                    reloadModel();
                },
                {}, grid_.editable(),
                current.state == NoteCellState::Burst && current.burstBankId == bank.id
                    && current.note == slot });
        }
        if (!entries.empty())
            banks.push_back(
                group(assetBankToken(bank.id) + "  ·  " + bank.name, std::move(entries)));
    }
    menu.push_back(group("USE BURST", banks, !banks.empty()));
    const auto* library = current.state == NoteCellState::Burst
        ? workspaceBurstLibrary(state_, current.burstBankId)
        : nullptr;
    if (library && current.note < library->bursts.size()
        && !library->bursts[current.note].empty()) {
        menu.push_back(separator());
        auto identity = assetBankToken(current.burstBankId) + ":" + burstSlotToken(current.note);
        add("EDIT " + identity + " IN BURSTS", "edit", true, current.burstBankId, current.note);
        add("DUPLICATE AND EDIT", "duplicate",
            std::any_of(library->bursts.begin(), library->bursts.end(),
                [](const BurstDefinition& b) { return b.empty(); }),
            current.burstBankId, current.note);
        add("EXPAND TO TRACKER ROWS", "expand", true, current.burstBankId, current.note);
        add("CONVERT TO FIRST NOTE", "convert", true, current.burstBankId, current.note);
    }
    items.push_back(group("BURST", std::move(menu)));
}

void MainPageView::appendSelectionMenu(std::vector<MainMenuItem>& items)
{
    auto range = grid_.effectiveGridSelection();
    const bool editable = grid_.editable();
    auto fields = gridFieldCount(state_.sequenceColumnsExpanded);
    auto first = grid_.selection.active ? grid_.selection.firstColumn(fields)
                                        : range.firstTrack * fields + range.firstField;
    auto last = grid_.selection.active ? grid_.selection.lastColumn(fields) : first;
    auto action = [this, editable](const char* title, void (GridController::*method)(int),
                      int tag = 0, bool enabled = true) {
        return MainMenuItem { title,
            [this, method, tag] {
                (grid_.*method)(tag);
                reloadModel();
            },
            {}, editable && enabled };
    };
    std::vector<MainMenuItem> fill { action("FILL DOWN", &GridController::fillSelectionFromEdge, 1),
        action("FILL UP", &GridController::fillSelectionFromEdge, -1),
        action("LINEAR SERIES BETWEEN ENDS", &GridController::fillSelectionSeries, 0,
            range.rowCount() > 1),
        separator(), action("REPEAT ONCE BELOW", &GridController::repeatGridSelection, 1),
        action("REPEAT 2× BELOW", &GridController::repeatGridSelection, 2),
        action("REPEAT 4× BELOW", &GridController::repeatGridSelection, 4),
        action("REPEAT TO ROW 256", &GridController::repeatGridSelection, -1) };
    std::vector<MainMenuItem> cells { action("INSERT CELLS DOWN",
                                          &GridController::shiftSelectionCells, 1),
        action("DELETE CELLS UP", &GridController::shiftSelectionCells, -1), separator(),
        action("MOVE UP 1", &GridController::moveGridSelection, -1),
        action("MOVE DOWN 1", &GridController::moveGridSelection, 1),
        action("MOVE UP BY JUMP", &GridController::moveGridSelection, -100),
        action("MOVE DOWN BY JUMP", &GridController::moveGridSelection, 100) };
    std::vector<MainMenuItem> paste;
    constexpr const char* pasteNames[] { "REPLACE", "MERGE INTO EMPTY", "RHYTHM ONLY", "NOTES ONLY",
        "VALUES ONLY" };
    for (int n = 0; n < 5; ++n)
        paste.push_back(action(pasteNames[n], &GridController::pasteGridSelectionSpecial, n,
            grid_.hasGridClipboard()));
    std::vector<MainMenuItem> transform {
        action("REVERSE ROW ORDER", &GridController::reverseGridSelection, 0, range.rowCount() > 1),
        action("ROTATE UP 1", &GridController::rotateGridSelection, -1, range.rowCount() > 1),
        action("ROTATE DOWN 1", &GridController::rotateGridSelection, 1, range.rowCount() > 1),
        separator(),
        action("COMPRESS TO 50%", &GridController::stretchGridSelection, 50, range.rowCount() > 1),
        action("STRETCH TO 200%", &GridController::stretchGridSelection, 200, range.rowCount() > 1),
        action("MATERIALIZE PRV / DEF", &GridController::materializeGridSelection),
        action("REPLACE FIRST VALUE WITH LAST", &GridController::findReplaceGridSelection, 0,
            range.rowCount() > 1),
        action("SWAP SELECTED CELLS WITH NEXT LANE", &GridController::swapGridSelectionWithNextLane,
            0,
            first / fields == last / fields
                && last / fields + 1 < state_.session.pattern.tracks.size()),
        action("SEPARATE NOTES INTO LANES", &GridController::splitSelectedNoteColumnByPitch, 0,
            first == last && first % fields == 0),
        action("MERGE NOTES INTO ONE LANE", &GridController::mergeSelectedNoteLanes, 0,
            first % fields == 0 && last % fields == 0 && first / fields < last / fields)
    };
    bool numeric = true, sequence = false;
    for (auto column = first; column <= last; ++column) {
        auto f = column % fields;
        bool seq = gridFieldIsSequence(f) && !gridFieldIsSequenceAction(f);
        numeric &= f == 1 || seq;
        sequence |= seq;
    }
    transform.push_back(group("VALUE MATH",
        { action("ADD 5 / 127", &GridController::adjustSelectedValues, 5, numeric),
            action("SUBTRACT 5 / 127", &GridController::adjustSelectedValues, -5, numeric),
            action("SCALE 80%", &GridController::scaleSelectedValues, 80, numeric),
            action("SCALE 120%", &GridController::scaleSelectedValues, 120, numeric) }));
    std::vector<MainMenuItem> timing {
        action("NUDGE EARLIER 1 MS", &GridController::quantizeSelectedMicroTime, -1001, sequence),
        action("NUDGE LATER 1 MS", &GridController::quantizeSelectedMicroTime, 1001, sequence),
        separator()
    };
    for (int amount : { 25, 50, 75, 100 }) {
        auto title = "QUANTIZE " + std::to_string(amount) + "% TO GRID";
        timing.push_back(
            action(title.c_str(), &GridController::quantizeSelectedMicroTime, amount, sequence));
    }
    transform.push_back(group("MICROTIME", std::move(timing)));
    transform.push_back(separator());
    transform.push_back({ "SELECTION STATISTICS…", [this] {
                             if (services_.grid.message)
                                 services_.grid.message(grid_.selectionStatistics());
                         } });
    const bool oneLane = range.firstTrack == range.lastTrack;
    auto place = [this, range](bool merge, std::size_t slot) {
        state_.selectedPhrase = slot;
        if (services_.grid.placePhrase)
            services_.grid.placePhrase(range.firstTrack, range.firstRow, merge);
        reloadModel();
    };
    std::string slot = "P" + padded(state_.selectedPhrase + 1);
    std::vector<MainMenuItem> phrase { { "CAPTURE SELECTION AS " + slot,
                                           [this, range] {
                                               if (services_.grid.capturePhrase)
                                                   services_.grid.capturePhrase(range.firstTrack,
                                                       range.firstRow, range.lastRow);
                                           },
                                           {},
                                           editable && oneLane && range.rowCount() >= 2
                                               && range.rowCount() <= kMaximumPhraseRows },
        separator() };
    std::vector<MainMenuItem> library;
    for (std::size_t i = 0; i < state_.phraseLibrary.phrases.size(); ++i) {
        const auto& p = state_.phraseLibrary.phrases[i];
        if (p.empty() && p.name.empty())
            continue;
        library.push_back({ "P" + padded(i + 1) + " · " + (p.name.empty() ? "UNTITLED" : p.name)
                + " · " + std::to_string(p.length) + " ROWS",
            [=] { place(false, i); }, {}, editable && oneLane });
    }
    phrase.push_back(group("COPY FROM LIBRARY", library, !library.empty()));
    phrase.push_back({ "COPY " + slot + " HERE", [=] { place(false, state_.selectedPhrase); }, {},
        editable && oneLane });
    phrase.push_back({ "MERGE " + slot + " INTO EMPTY", [=] { place(true, state_.selectedPhrase); },
        {}, editable && oneLane });
    phrase.push_back({ "REAPPLY LAST P" + padded(state_.lastPlacedPhrase + 1) + " HERE",
        [=] { place(false, state_.lastPlacedPhrase); }, {}, editable && oneLane });
    items.push_back(separator());
    items.push_back(group("SELECTION  ·  " + std::to_string(range.rowCount()) + " ROW"
            + (range.rowCount() == 1 ? "" : "S") + " × " + std::to_string(last - first + 1)
            + " COLUMN" + (last == first ? "" : "S"),
        { group("FILL / REPEAT", std::move(fill)), group("CELLS", std::move(cells)),
            group("PASTE SPECIAL", std::move(paste)), group("TRANSFORM", std::move(transform)),
            group("PHRASE", std::move(phrase)) }));
}

std::vector<MainMenuItem> MainPageView::contextMenu(GridAddress a, bool rowMenu)
{
    std::vector<MainMenuItem> result;
    if (a.track >= state_.session.pattern.tracks.size())
        return result;
    auto range = grid_.effectiveGridSelection();
    const bool editable = grid_.editable();
    if (rowMenu) {
        const bool selected
            = grid_.selection.active && a.row >= range.firstRow && a.row <= range.lastRow;
        auto first = selected ? range.firstRow : a.row;
        auto count = selected ? range.rowCount() : 1;
        bool grow = state_.session.pattern.visibleRows < 256;
        result.push_back(
            { count > 1 ? "INSERT " + std::to_string(count) + " ROWS ABOVE" : "INSERT ROW ABOVE",
                [this, first, count] {
                    grid_.insertRowsAt(first, count);
                    reloadModel();
                },
                {}, editable && grow });
        result.push_back(
            { count > 1 ? "INSERT " + std::to_string(count) + " ROWS BELOW" : "INSERT ROW BELOW",
                [this, first, count] {
                    grid_.insertRowsAt(first + count, count);
                    reloadModel();
                },
                {}, editable && grow });
        result.push_back({ count > 1 ? "DELETE " + std::to_string(count) + " ROWS" : "DELETE ROW",
            [this, first, count] {
                grid_.deleteRows(first, count);
                reloadModel();
            },
            {}, editable && state_.session.pattern.visibleRows > 16 });
        result.push_back(separator());
        result.push_back({ count > 1 ? "COPY " + std::to_string(count) + " ROWS" : "COPY ROW",
            [this, first, count] { grid_.copyRows(first, count); } });
        result.push_back({ "PASTE ROWS ABOVE",
            [this, first] {
                grid_.insertRowsAt(first, grid_.rowClipboardCount(), true);
                reloadModel();
            },
            {}, editable && grow && grid_.hasRowClipboard() });
        result.push_back(separator());
        result.push_back({ "QUANTIZE MT TO ROW GRID",
            [this, first, count] {
                if (quantizeMicroTimeRows(state_.session, first, first + count - 1))
                    grid_.patternChanged();
            },
            {}, editable });
        auto rhythm = [this, first, count, editable](std::string title, int kind, int amount) {
            return MainMenuItem { title,
                [this, first, count, kind, amount] {
                    auto rng = state_.session.commandRngState;
                    std::size_t changed = 0;
                    for (std::size_t lane = 0; lane < state_.session.pattern.tracks.size();
                         ++lane) {
                        if (kind == 0)
                            changed += humanizeNoteRows(
                                state_.session, lane, first, first + count - 1, amount / 100.);
                        if (kind == 1)
                            changed
                                += reverseNoteRows(state_.session, lane, first, first + count - 1);
                        if (kind == 2)
                            changed += rotateNoteRows(
                                state_.session, lane, first, first + count - 1, amount);
                        if (kind == 3)
                            changed += thinNoteRows(
                                state_.session, lane, first, first + count - 1, amount / 100.);
                        if (kind == 4)
                            changed += densityNoteRows(
                                state_.session, lane, first, first + count - 1, amount / 100.);
                    }
                    if (changed || rng != state_.session.commandRngState)
                        grid_.patternChanged();
                },
                {}, editable };
        };
        std::vector<MainMenuItem> human, thin, density;
        for (int n : { 10, 25, 50 }) {
            human.push_back(rhythm(std::to_string(n) + "%", 0, n));
            thin.push_back(rhythm(std::to_string(n) + "%", 3, n));
        }
        for (int n : { 25, 50, 75 })
            density.push_back(rhythm(std::to_string(n) + "%", 4, n));
        result.push_back(group("HUMANIZE HIT PLACEMENT", std::move(human), editable && count > 1));
        result.push_back(group("RHYTHM",
            { rhythm("REVERSE NOTES", 1, 0), rhythm("ROTATE NOTES LEFT 1", 2, -1),
                rhythm("ROTATE NOTES RIGHT 1", 2, 1), separator(),
                group("THIN HITS", std::move(thin)), group("SET HIT DENSITY", std::move(density)) },
            editable));
        return result;
    }
    const auto fields = gridFieldCount(state_.sequenceColumnsExpanded);
    const auto column = a.track * fields + a.field;
    if (!(grid_.selection.active && grid_.selection.firstColumn(fields) == column
            && grid_.selection.lastColumn(fields) == column && a.row >= range.firstRow
            && a.row <= range.lastRow))
        range.firstRow = range.lastRow = a.row;
    if (a.field == 0) {
        std::vector<MainMenuItem> pitch;
        for (auto n : { 1, -1, 12, -12 })
            pitch.push_back(
                { n == 1 ? "UP 1 SEMITONE"
                         : n == -1 ? "DOWN 1 SEMITONE" : n == 12 ? "UP 1 OCTAVE" : "DOWN 1 OCTAVE",
                    [this, a, range, n] {
                        if (transposeNoteRows(
                                state_.session, a.track, range.firstRow, range.lastRow, n))
                            grid_.patternChanged();
                    },
                    {}, editable });
        pitch.push_back(separator());
        pitch.push_back({ "FIT TO CURRENT SCALE",
            [this, range] {
                if (services_.pitchContour)
                    services_.pitchContour(PitchContour::Fit, range.firstRow, range.lastRow);
            },
            {}, editable });
        pitch.push_back({ "GENERATE CONTOUR",
            [this, range] {
                if (services_.pitchContour)
                    services_.pitchContour(
                        PitchContour::VaryExisting, range.firstRow, range.lastRow);
            },
            {}, editable });
        pitch.push_back({ "OPEN PITCH MAP…", [this, range] {
                             if (services_.openPitchMap)
                                 services_.openPitchMap(range.firstRow, range.lastRow);
                         } });
        result.push_back(group("PITCH", std::move(pitch)));
        appendBurstMenu(result, a);
    } else if (a.field == 1) {
        for (int n : { 75, 90, 110, 125 })
            result.push_back({ "SCALE WRITTEN VALUES " + std::to_string(n) + "%",
                [this, a, range, n] {
                    if (scaleVelocityRows(
                            state_.session, a.track, range.firstRow, range.lastRow, n / 100.))
                        grid_.patternChanged();
                },
                {}, editable });
        result.push_back(separator());
        for (int n : { 96, 64, 1 })
            result.push_back({ "RANDOMIZE VALUES " + std::to_string(n) + "–127",
                [this, a, range, n] {
                    randomizeVelocityRows(state_.session, a.track, range.firstRow, range.lastRow,
                        static_cast<uint8_t>(n), 127);
                    grid_.patternChanged();
                },
                {}, editable });
    } else if (gridFieldIsSequenceAction(a.field)) {
        const auto& pair
            = state_.session.pattern.tracks[a.track].fxPairs[gridSequencePair(a.field)];
        auto current = a.row < pair.actions.size() ? pair.actions[a.row] : FxActionCell::empty();
        auto set = [this, a](FxActionCell cell) {
            if (!grid_.editable())
                return;
            auto& track = state_.session.pattern.tracks[a.track];
            writeTrackerGridCell(track, a.field, a.row, cell);
            if (cell.state == FxActionCellState::Sequencer
                || cell.state == FxActionCellState::MidiControlChange) {
                auto& p = track.fxPairs[gridSequencePair(a.field)];
                if (p.values.size() <= a.row)
                    p.values.resize(a.row + 1, FxValueCell::previous());
                if (p.values[a.row].state == FxValueCellState::Previous)
                    p.values[a.row]
                        = FxValueCell::withValue(cell.state == FxActionCellState::Sequencer
                                    && cell.sequencerAction == SequencerAction::Condition
                                ? normalizedFromSequencerCondition(SequencerCondition::FirstOf2)
                                : .5f);
                p.valueColumn.length = std::max(p.valueColumn.length, a.row + 1);
            }
            state_.session.pattern.visibleRows
                = std::max(state_.session.pattern.visibleRows, a.row + 1);
            grid_.clearGridSelection();
            grid_.patternChanged();
        };
        result.push_back({ "SEQ ACTION  ·  NORMALIZED VALUE 0.000–1.000", {}, {}, false });
        result.push_back(separator());
        result.push_back({ "---   CLEAR", [=] { set(FxActionCell::empty()); }, {}, editable,
            current.state == FxActionCellState::Empty });
        result.push_back({ "PRV   PREVIOUS / RECALL", [=] { set(FxActionCell::previous()); }, {},
            editable, current.state == FxActionCellState::Previous });
        result.push_back(separator());
        for (std::size_t i = 0; i < sequencerActionCount(); ++i) {
            auto* d = sequencerAction(i);
            if (!d)
                continue;
            std::string name(d->displayName);
            for (auto& c : name)
                if (c >= 'a' && c <= 'z')
                    c -= 32;
            result.push_back(
                { std::string(d->mnemonic) + "   " + name + "  ·  " + std::string(d->valueMeaning),
                    [=] { set(FxActionCell::sequencer(d->action)); }, {}, editable,
                    current.state == FxActionCellState::Sequencer
                        && current.sequencerAction == d->action });
        }
        std::vector<MainMenuItem> cc;
        for (unsigned g = 0; g < 4; ++g) {
            std::vector<MainMenuItem> controllers;
            for (unsigned n = g * 32; n < g * 32 + 32; ++n)
                controllers.push_back({ "CC" + padded(n, 3),
                    [=] { set(FxActionCell::midiControlChange(static_cast<uint8_t>(n))); }, {},
                    editable,
                    current.state == FxActionCellState::MidiControlChange
                        && current.midiController == n });
            cc.push_back(group(
                "CC" + padded(g * 32, 3) + "–CC" + padded(g * 32 + 31, 3), std::move(controllers)));
        }
        result.push_back(separator());
        result.push_back(group("MIDI CONTROL CHANGE", std::move(cc)));
    } else if (gridFieldIsSequence(a.field)) {
        auto& pair = state_.session.pattern.tracks[a.track].fxPairs[gridSequencePair(a.field)];
        if (a.row < pair.actions.size() && pair.actions[a.row].state == FxActionCellState::Sequencer
            && pair.actions[a.row].sequencerAction == SequencerAction::Condition) {
            auto current
                = a.row < pair.values.size() && pair.values[a.row].state == FxValueCellState::Value
                ? sequencerConditionFromNormalized(pair.values[a.row].normalized)
                : SequencerCondition::FirstOf2;
            result.push_back({ "CD  ·  PLAY THIS NOTE WHEN", {}, {}, false });
            result.push_back(separator());
            for (std::size_t i = 0; i < kSequencerConditionCount; ++i) {
                auto* d = sequencerCondition(i);
                if (!d)
                    continue;
                if (d->condition == SequencerCondition::First
                    || d->condition == SequencerCondition::Fill
                    || d->condition == SequencerCondition::SongFirst
                    || d->condition == SequencerCondition::SongFirstOf2)
                    result.push_back(separator());
                result.push_back({ std::string(d->token) + "   " + std::string(d->displayName),
                    [this, a, d] {
                        writeTrackerGridCell(state_.session.pattern.tracks[a.track], a.field, a.row,
                            FxValueCell::withValue(normalizedFromSequencerCondition(d->condition)));
                        grid_.clearGridSelection();
                        grid_.patternChanged();
                    },
                    {}, editable, current == d->condition });
            }
        }
    }
    appendSelectionMenu(result);
    return result;
}
} // namespace s3g::tracker::editor
