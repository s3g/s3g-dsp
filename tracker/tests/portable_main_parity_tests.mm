#include "s3g_tracker_main_page_host.h"
#include "s3g_tracker_workspace.h"
#include <iostream>
#include <set>
#include <sstream>

using namespace s3g::tracker;
using namespace s3g::tracker::editor;

@interface NSView (PortableParityAccess)
- (BOOL)applyCellText:(NSString*)text
              toTrack:(Track&)track
                  row:(std::size_t)row
                 page:(std::size_t)page
                field:(std::size_t)field;
- (void)beginGridSelectionAtTrack:(std::size_t)track
                            field:(std::size_t)field
                              row:(std::size_t)row
                             page:(std::size_t)page;
- (void)extendGridSelectionToTrack:(std::size_t)track field:(std::size_t)field row:(std::size_t)row;
- (NSMenu*)noteMenuForTrack:(std::size_t)track row:(std::size_t)row;
- (NSMenu*)velocityMenuForTrack:(std::size_t)track row:(std::size_t)row;
- (NSMenu*)sequenceActionMenuForTrack:(std::size_t)track
                                  row:(std::size_t)row
                                field:(std::size_t)field;
@end

static int compareCocoa(MainPageView& page, app::TrackerViewState& state,
    app::TrackerViewState& nativeState, app::WorkspaceCallbacks& callbacks)
{
    const auto saved = state;
    state.sequenceColumnsExpanded = nativeState.sequenceColumnsExpanded = true;
    S3GTrackerWorkspaceController* native =
        [[S3GTrackerWorkspaceController alloc] initWithState:&nativeState callbacks:&callbacks];
    (void)native.view;
    NSView* nativeGrid = [native valueForKey:@"gridView"];
    int failures = 0, grammarChecks = 0, operationChecks = 0, keyChecks = 0;
    auto check = [&](bool value, const std::string& message) {
        if (!value) {
            ++failures;
            std::cerr << "Cocoa parity: " << message << '\n';
        }
    };
    const std::vector<std::string> tokens
        = { "", "---", "...", "PRV", "DEF", "OFF", "CUT", "KIL", "REL", "TIE", "HLD", "RPT", "0",
              "1", "60", "127", "128", "-1", "0.5", ".25", "1.1", "60+64+67", "C4", "C-4", "C#4",
              "Db4", "60+bad", "0.2+0.4+0.6", "TIE+0.5", "DEF+2", "CC74", "CC000", "CC127", "CC128",
              "MT", "PR", "RR", "CD", "1:2", "FIRST", "FILL", "!FILL", "30 MS", "  60  ", "1e-1",
              "nan", "inf", "1junk", "B01", "P01:B01", "\u00a0 60 \u00a0", "\u2002.5\u2002" };
    for (std::size_t field = 0; field < 7; ++field)
        for (const auto& token : tokens) {
            Track nativeTrack = saved.session.pattern.tracks[0], portableTrack = nativeTrack;
            if (field == 3 || field == 5) {
                auto& p = nativeTrack.fxPairs[gridSequencePair(field)];
                p.actions.resize(3, FxActionCell::sequencer(SequencerAction::MicroTime));
                portableTrack = nativeTrack;
            }
            auto original = [nativeGrid applyCellText:[NSString stringWithUTF8String:token.c_str()]
                                              toTrack:nativeTrack
                                                  row:2
                                                 page:0
                                                field:field];
            auto converted = applyCellText(state, token, portableTrack, 2, field);
            ++grammarChecks;
            check(bool(original) == converted,
                "grammar acceptance field " + std::to_string(field) + " token [" + token + "]");
            if (original && converted)
                check(trackerGridCellsEqual(trackerGridCellAt(nativeTrack, field, 2),
                          trackerGridCellAt(portableTrack, field, 2)),
                    "grammar value field " + std::to_string(field) + " token [" + token + "]");
        }
    struct Operation {
        const char* selector;
        void (GridController::*method)(int);
        std::vector<int> tags;
    };
    const std::vector<Operation> operations {
        { "reverseGridSelection:", &GridController::reverseGridSelection, { 0 } },
        { "rotateGridSelection:", &GridController::rotateGridSelection, { -1, 1 } },
        { "adjustSelectedValues:", &GridController::adjustSelectedValues, { -5, 5 } },
        { "scaleSelectedValues:", &GridController::scaleSelectedValues, { 80, 120 } },
        { "quantizeSelectedMicroTime:", &GridController::quantizeSelectedMicroTime,
            { -1001, 1001, 50, 100 } },
        { "fillSelectionFromEdge:", &GridController::fillSelectionFromEdge, { -1, 1 } },
        { "fillSelectionSeries:", &GridController::fillSelectionSeries, { 0 } },
        { "repeatGridSelection:", &GridController::repeatGridSelection, { 1, 2, -1 } },
        { "shiftSelectionCells:", &GridController::shiftSelectionCells, { -1, 1 } },
        { "moveGridSelection:", &GridController::moveGridSelection, { -1, 1, -100, 100 } },
        { "stretchGridSelection:", &GridController::stretchGridSelection, { 50, 200 } },
        { "materializeGridSelection:", &GridController::materializeGridSelection, { 0 } },
        { "findReplaceGridSelection:", &GridController::findReplaceGridSelection, { 0 } },
        { "swapGridSelectionWithNextLane:", &GridController::swapGridSelectionWithNextLane, { 0 } },
        { "splitSelectedNoteColumnByPitch:", &GridController::splitSelectedNoteColumnByPitch,
            { 0 } },
        { "mergeSelectedNoteLanes:", &GridController::mergeSelectedNoteLanes, { 0 } },
    };
    for (const auto& operation : operations)
        for (int tag : operation.tags)
            for (std::size_t field : { 0u, 1u, 3u, 6u }) {
                state = saved;
                state.sequenceColumnsExpanded = true;
                state.session.selectedTrack = state.session.selectedField
                    = state.session.selectedRow = 0;
                for (std::size_t lane = 0; lane < state.session.pattern.tracks.size(); ++lane) {
                    auto& track = state.session.pattern.tracks[lane];
                    for (std::size_t row = 0; row < 16; ++row) {
                        writeTrackerGridCell(track, 0, row,
                            row % 3 ? NoteCell::withNote(static_cast<uint8_t>(48 + row + lane))
                                    : NoteCell::rest());
                        writeTrackerGridCell(track, 1, row,
                            row % 3 ? ValueCell::withValue(float(row) / 16)
                                    : ValueCell::previous());
                        writeTrackerGridCell(
                            track, 2, row, FxActionCell::sequencer(SequencerAction::MicroTime));
                        writeTrackerGridCell(
                            track, 3, row, FxValueCell::withValue(float(row) / 16));
                        writeTrackerGridCell(track, 6, row,
                            row % 3 ? GateCell::withRows(float(row) / 8)
                                    : GateCell::defaultValue());
                    }
                }
                nativeState = state;
                page.controller().select({ 0, field, 2 });
                page.controller().select({ 0, field, 7 }, true);
                nativeState.session.selectedTrack = 0;
                nativeState.session.selectedField = field;
                nativeState.session.selectedRow = 7;
                [nativeGrid beginGridSelectionAtTrack:0 field:field row:2 page:0];
                [nativeGrid extendGridSelectionToTrack:0 field:field row:7];
                NSMenuItem* item = [[NSMenuItem alloc] init];
                item.tag = tag;
                SEL selector
                    = NSSelectorFromString([NSString stringWithUTF8String:operation.selector]);
                check([nativeGrid respondsToSelector:selector],
                    std::string("missing source selector ") + operation.selector);
                [NSApp sendAction:selector to:nativeGrid from:item];
                (page.controller().*operation.method)(tag);
                ++operationChecks;
                auto& a = nativeState.session.pattern;
                auto& b = state.session.pattern;
                bool same = a.tracks.size() == b.tracks.size() && a.visibleRows == b.visibleRows;
                for (std::size_t lane = 0; same && lane < a.tracks.size(); ++lane)
                    for (std::size_t f = 0; same && f < 7; ++f) {
                        same = columnForField(a.tracks[lane], 0, f)->length
                            == columnForField(b.tracks[lane], 0, f)->length;
                        for (std::size_t row = 0; same && row < 256; ++row)
                            same = trackerGridCellsEqual(trackerGridCellAt(a.tracks[lane], f, row),
                                trackerGridCellAt(b.tracks[lane], f, row));
                    }
                check(same,
                    std::string(operation.selector) + " tag " + std::to_string(tag) + " field "
                        + std::to_string(field));
            }
    struct Key {
        unsigned short code;
        GridKey key;
    };
    for (const Key event : std::vector<Key> { { 123, GridKey::Left }, { 124, GridKey::Right },
             { 125, GridKey::Down }, { 126, GridKey::Up }, { 115, GridKey::Home },
             { 119, GridKey::End }, { 101, GridKey::F9 }, { 109, GridKey::F10 },
             { 103, GridKey::F11 }, { 111, GridKey::F12 } })
        for (uint32_t mods :
            { 0u, uint32_t(Shift), uint32_t(Control | Alt), uint32_t(Control | Shift) })
            for (uint32_t jump : { 1u, 3u, 16u }) {
                state = saved;
                state.sequenceColumnsExpanded = true;
                state.trackerRowJump = jump;
                state.session.selectedTrack = 1;
                state.session.selectedField = 1;
                state.session.selectedRow = 8;
                nativeState = state;
                page.controller().clearGridSelection();
                [nativeGrid beginGridSelectionAtTrack:1 field:1 row:8 page:0];
                NSEventModifierFlags flags = 0;
                if (mods & Shift)
                    flags |= NSEventModifierFlagShift;
                if (mods & Control)
                    flags |= NSEventModifierFlagControl;
                if (mods & Alt)
                    flags |= NSEventModifierFlagOption;
                NSEvent* key = [NSEvent keyEventWithType:NSEventTypeKeyDown
                                                location:NSZeroPoint
                                           modifierFlags:flags
                                               timestamp:0
                                            windowNumber:0
                                                 context:nil
                                              characters:@""
                             charactersIgnoringModifiers:@""
                                               isARepeat:NO
                                                 keyCode:event.code];
                [nativeGrid keyDown:key];
                page.controller().keyDown({ event.key, "", mods, 478 });
                ++keyChecks;
                check(state.session.selectedTrack == nativeState.session.selectedTrack
                        && state.session.selectedField == nativeState.session.selectedField
                        && state.session.selectedRow == nativeState.session.selectedRow,
                    "key " + std::to_string(event.code) + " modifiers " + std::to_string(mods)
                        + " jump " + std::to_string(jump));
            }
    // Every original context-menu leaf must remain reachable, including
    // operations whose algorithms are exercised above and CC/condition menus.
    const auto normalize = [](std::string text) {
        for (auto& c : text)
            if (c >= 'a' && c <= 'z')
                c = static_cast<char>(c - 'a' + 'A');
        return text;
    };
    std::function<void(NSMenu*, std::set<std::string>&)> nativeLeaves;
    nativeLeaves = [&](NSMenu* menu, std::set<std::string>& leaves) {
        for (NSMenuItem* item in menu.itemArray) {
            if (item.submenu)
                nativeLeaves(item.submenu, leaves);
            else if (!item.separatorItem && item.title.length)
                leaves.insert(normalize(item.title.UTF8String));
        }
    };
    std::function<void(const std::vector<MainMenuItem>&, std::set<std::string>&)> portableLeaves;
    portableLeaves = [&](const std::vector<MainMenuItem>& items, std::set<std::string>& leaves) {
        for (const auto& item : items) {
            if (!item.children.empty())
                portableLeaves(item.children, leaves);
            else if (!item.title.empty())
                leaves.insert(normalize(item.title));
        }
    };
    state = saved;
    state.sequenceColumnsExpanded = true;
    nativeState = state;
    page.controller().select({ 0, 0, 2 });
    page.controller().select({ 0, 0, 7 }, true);
    [nativeGrid beginGridSelectionAtTrack:0 field:0 row:2 page:0];
    [nativeGrid extendGridSelectionToTrack:0 field:0 row:7];
    for (std::size_t field : { 0u, 1u, 2u }) {
        NSMenu* menu = field == 0
            ? [nativeGrid noteMenuForTrack:0 row:2]
            : field == 1 ? [nativeGrid velocityMenuForTrack:0 row:2]
                         : [nativeGrid sequenceActionMenuForTrack:0 row:2 field:field];
        std::set<std::string> original, converted;
        nativeLeaves(menu, original);
        portableLeaves(page.contextMenu({ 0, field, 2 }), converted);
        for (const auto& title : original)
            check(converted.count(title) > 0, "missing context-menu item: " + title);
    }
    state = saved;
    page.controller().clearGridSelection();
    std::cout << "Tracker direct Cocoa comparison: " << grammarChecks << " grammar / "
              << operationChecks << " selection-operation / " << keyChecks << " keyboard cases\n";
    return failures;
}

int trackerCocoaParity(MainPageView& page, app::TrackerViewState& state)
{
    app::TrackerViewState nativeState = state;
    app::WorkspaceCallbacks callbacks;
    // The reference views borrow these C++ objects. Drain their autoreleased
    // windows/views while the owners still exist, before the caller next pumps
    // AppKit tracking-area updates on the portable page.
    @autoreleasepool {
        return compareCocoa(page, state, nativeState, callbacks);
    }
}
