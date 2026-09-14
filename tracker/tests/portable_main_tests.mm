#include "s3g_tracker_main_page_host.h"
#include "s3g_tracker_vstgui_pilot.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/events.h"
#import <Cocoa/Cocoa.h>
#include <algorithm>
#include <cmath>
#include <iostream>

using namespace s3g::tracker;
using namespace s3g::tracker::editor;
using namespace VSTGUI;
int trackerCocoaParity(MainPageView&, app::TrackerViewState&);

int main()
{
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        int failures = 0, publications = 0, selections = 0, transport = 0, outputChanges = 0;
        std::vector<std::string> commands, history;
        std::string draft, consoleMessage;
        auto check = [&](bool ok, const char* message) {
            if (!ok) {
                ++failures;
                std::cerr << message << '\n';
            }
        };
        app::TrackerViewState state;
        state.session.pattern = state.patternBank.entries.front().pattern;
        state.session.pattern.tracks.resize(4);
        state.session.pattern.visibleRows = 64;
        for (auto& track : state.session.pattern.tracks) {
            track.notes.resize(64, NoteCell::rest());
            track.velocities.resize(64, ValueCell::defaultValue());
            track.noteColumn.length = track.velocityColumn.length = 64;
        }
        app::WorkspaceCallbacks callbacks;
        callbacks.patternChanged = [&] { ++publications; };
        callbacks.selectionChanged = [&] { ++selections; };
        callbacks.transportChanged = [&] { ++transport; };
        callbacks.outputChanged = [&] { ++outputChanges; };
        callbacks.executeCommand = [&](const std::string& text) { commands.push_back(text); };
        MainPageServices services;
        services.consoleDraft = [&] { return draft; };
        services.consoleHistory = [&] { return history; };
        services.consoleDraftChanged = [&](const std::string& text) { draft = text; };
        services.submitConsole = [&](const std::string& text) {
            history.push_back(text);
            commands.push_back(text);
        };
        services.consoleMessage = [&](const std::string& text) { consoleMessage = text; };
        services.grid.message = [](const std::string& message) {
            std::cerr << "Tracker message: " << message << '\n';
        };
        S3GTrackerMainPageHost* host = [[S3GTrackerMainPageHost alloc] initWithState:&state
                                                                           callbacks:&callbacks
                                                                            services:services];
        NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1320, 820)
                                                       styleMask:NSWindowStyleMaskTitled
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        window.releasedWhenClosed = NO;
        window.contentView = host;
        [NSApp activateIgnoringOtherApps:YES];
        [window makeKeyAndOrderFront:nil];
        auto* page = host.page;
        auto pump = [&] {
            [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.025]];
            page->refreshPlaybackDisplay();
            [host displayIfNeeded];
        };
        pump();
        check(page && page->getFrame() && page->drawCount() > 0,
            "portable CFrame did not attach/draw");
        if (!page || !page->getFrame())
            return 1;
        auto* frame = page->getFrame();
        failures += trackerCocoaParity(*page, state);
        page->reloadModel();
        pump();
        auto* platform = static_cast<IPlatformFrameCallback*>(frame);
        platform->platformOnActivate(true);
        auto key = [&](VirtualKey virt, char32_t character = 0, Modifiers mods = {}) {
            KeyboardEvent event(EventType::KeyDown);
            event.virt = virt;
            event.character = character;
            event.modifiers = mods;
            platform->platformOnEvent(event);
            pump();
            return bool(event.consumed);
        };
        auto click
            = [&](double x, double y, int clicks = 1, Modifiers mods = {}, bool right = false) {
                  MouseDownEvent down;
                  down.mousePosition = { x, y };
                  down.clickCount = clicks;
                  down.buttonState = right ? MouseButton::Right : MouseButton::Left;
                  down.modifiers = mods;
                  platform->platformOnEvent(down);
                  MouseUpEvent up;
                  up.mousePosition = { x, y };
                  up.buttonState = down.buttonState;
                  up.modifiers = mods;
                  platform->platformOnEvent(up);
                  pump();
              };
        auto capture = [&](const char* name) {
            const char* directory = std::getenv("S3G_TRACKER_MAIN_CAPTURE_DIR");
            if (!directory)
                return;
            NSString* dir = [NSString stringWithUTF8String:directory];
            [[NSFileManager defaultManager] createDirectoryAtPath:dir
                                      withIntermediateDirectories:YES
                                                       attributes:nil
                                                            error:nil];
            NSData* pdf = [host dataWithPDFInsideRect:host.bounds];
            [pdf writeToFile:
                     [dir stringByAppendingPathComponent:[[NSString stringWithUTF8String:name]
                                                             stringByAppendingPathExtension:@"pdf"]]
                  atomically:YES];
        };
        capture("main-compact");
        click(950, 44);
        check(state.sequenceColumnsExpanded, "expand control not dispatched");
        capture("main-expanded");
        click(1270, 44);
        check(std::abs(page->gridZoom() - 1.16) < 1e-6, "grid zoom step differs from Cocoa");
        click(1220, 44);
        check(page->gridZoom() == 1, "grid zoom reset failed");
        auto& grid = page->controller();
        page->scrollTo(0, 0);
        grid.select({ 0, 0, 0 });
        click(68, 125, 2);
        key(VirtualKey::None, 'K');
        key(VirtualKey::Return);
        check(state.session.pattern.tracks[0].name == "K",
            "track-name editor did not select existing text");
        page->focusConsole();
        auto* space = [NSEvent keyEventWithType:NSEventTypeKeyDown
                                       location:NSZeroPoint
                                  modifierFlags:0
                                      timestamp:0
                                   windowNumber:window.windowNumber
                                        context:nil
                                     characters:@" "
                    charactersIgnoringModifiers:@" "
                                      isARepeat:NO
                                        keyCode:49];
        check([host performKeyEquivalent:space], "Live Code Space leaked to host shortcuts");
        pump();
        check(draft == " ", "Live Code key-equivalent Space must insert exactly once");
        key(VirtualKey::Left, 0, Modifiers { ModifierKey::Shift });
        check([host performKeyEquivalent:space], "selected Live Code Space leaked to host");
        pump();
        check(draft == " ", "Space must replace the selection rather than append twice");
        key(VirtualKey::Back);
        key(VirtualKey::None, 'p');
        key(VirtualKey::None, 'a');
        key(VirtualKey::None, 'n');
        key(VirtualKey::Tab);
        check(draft == "panic ", "console completion did not synchronize draft");
        key(VirtualKey::Return);
        check(!commands.empty() && commands.back() == "panic" && draft.empty(),
            "Live Code did not submit or clear its draft");
        check(dynamic_cast<CTextEdit*>(frame->getFocusView()) != nullptr,
            "Live Code Return lost command-entry focus");
        const auto activeId = state.patternBank.activePatternId;
        state.patternBank.activePatternId = "A02";
        page->reloadModel();
        pump();
        check(dynamic_cast<CTextEdit*>(frame->getFocusView()) != nullptr,
            "pattern change interrupted Live Code command-entry focus");
        state.patternBank.activePatternId = activeId;
        page->reloadModel();
        key(VirtualKey::Up);
        check(draft == "panic", "Live Code history did not recall submitted command");
        key(VirtualKey::None, '!');
        check(draft == "panic!", "history recall did not move caret to end");
        key(VirtualKey::Down);
        check(draft.empty(), "Live Code history did not restore original draft");
        key(VirtualKey::None, 'r');
        key(VirtualKey::Tab);
        check(consoleMessage.find("matches: ") == 0,
            "ambiguous console completion did not report candidates");
        key(VirtualKey::Escape);
        draft = "from other Console page";
        check(!macTrackerTextKeyEquivalent(host, frame, space),
            "text shortcut guard must release Space outside a text edit");
        page->focusConsole();
        auto* sharedEdit = dynamic_cast<CTextEdit*>(frame->getFocusView());
        check(sharedEdit && sharedEdit->getText() == draft.c_str(),
            "main page did not pick up shared Console draft");
        key(VirtualKey::Escape);
        draft.clear();
        pump();
        click(495, 789);
        key(VirtualKey::Home);
        key(VirtualKey::Return);
        check(outputChanges == 1, "gate menu did not notify the output callback");
        // Exercise actual CFrame pointer/keyboard routing, not just menu trees.
        // Child widths must follow their own rendered labels at every depth.
        auto menuFontInfo = macTrackerSuiteFont(10);
        NSFont* menuFont = [NSFont fontWithName:@(menuFontInfo.name.c_str())
                                           size:menuFontInfo.size];
        auto compactMenu = [&](std::size_t level, const std::vector<MainMenuItem>& items) {
            const auto bounds = page->popupBounds(level);
            double expected = 18;
            for (const auto& item : items) {
                auto title = item.title;
                for (auto& c : title)
                    if (c >= 'a' && c <= 'z')
                        c = char(c - 'a' + 'A');
                const auto width =
                    [@(title.c_str()) sizeWithAttributes:@ { NSFontAttributeName : menuFont }]
                        .width;
                expected
                    = std::max(expected, std::ceil(width) + (item.children.empty() ? 18. : 24.));
            }
            expected = std::min(expected, 610.);
            check(!bounds.isEmpty() && std::abs(bounds.getWidth() - expected) <= 1.,
                "submenu is not snug to its own rendered labels/arrow gutter");
            check(
                bounds.left >= 8 && bounds.right <= 1312 && bounds.top >= 8 && bounds.bottom <= 812,
                "compact submenu escaped the logical viewport");
        };
        auto hoverMenu = [&](std::size_t level, const std::vector<MainMenuItem>& items,
                             const std::string& prefix) {
            const auto found = std::find_if(items.begin(), items.end(),
                [&](const auto& item) { return item.title.find(prefix) == 0; });
            check(found != items.end(), "submenu fixture item not found");
            if (found == items.end())
                return std::vector<MainMenuItem> {};
            const auto bounds = page->popupBounds(level);
            check(!bounds.isEmpty(), "submenu parent did not open");
            MouseMoveEvent move;
            move.mousePosition
                = { bounds.getCenter().x, bounds.top + double(found - items.begin()) * 21 + 10 };
            platform->platformOnEvent(move);
            pump();
            compactMenu(level + 1, found->children);
            return found->children;
        };
        auto dismissMenus = [&] {
            for (int level = 0; level < 8 && !page->popupBounds(0).isEmpty(); ++level)
                key(VirtualKey::Escape);
            check(page->popupBounds(0).isEmpty(), "Escape did not dismiss nested menus");
        };
        grid.select({ 0, 0, 0 });
        click(12, 213, 1, {}, true);
        const auto rowMenu = page->contextMenu({ 0, 0, 0 }, true);
        const auto rhythm = hoverMenu(0, rowMenu, "RHYTHM");
        hoverMenu(1, rhythm, "THIN HITS");
        check(page->popupBounds(2).getWidth() < 70,
            "short percentage submenu retained a parent/minimum width");
        capture("main-submenu-rhythm");
        key(VirtualKey::Left);
        check(page->popupBounds(2).isEmpty() && !page->popupBounds(1).isEmpty(),
            "Left did not return from the compact child to its parent");
        dismissMenus();

        const auto savedNote = state.session.pattern.tracks[0].notes[0];
        state.session.pattern.tracks[0].notes[0] = NoteCell::withNote(60);
        click(68, 213, 1, {}, true);
        const auto noteMenu = page->contextMenu({ 0, 0, 0 });
        hoverMenu(0, noteMenu, "PITCH");
        const auto pitchBounds = page->popupBounds(1);
        click(pitchBounds.getCenter().x, pitchBounds.top + 10);
        check(state.session.pattern.tracks[0].notes[0].note == 61 && page->popupBounds(0).isEmpty(),
            "compact submenu pointer selection lost its action/dismissal");
        state.session.pattern.tracks[0].notes[0] = savedNote;

        const auto savedPhraseName = state.phraseLibrary.phrases[0].name;
        for (const auto& name : { std::string("écho × ø · loop"), std::string(160, 'W') }) {
            state.phraseLibrary.phrases[0].name = name;
            click(68, 213, 1, {}, true);
            const auto menuItems = page->contextMenu({ 0, 0, 0 });
            const auto selection = hoverMenu(0, menuItems, "SELECTION");
            const auto phrase = hoverMenu(1, selection, "PHRASE");
            hoverMenu(2, phrase, "COPY FROM LIBRARY");
            capture(name.size() < 160 ? "main-submenu-unicode" : "main-submenu-long");
            dismissMenus();
        }
        state.phraseLibrary.phrases[0].name = savedPhraseName;

        click(1280, 125, 1, {}, true);
        // FOLLOW THIS LANE plus its separator precede the original four-item
        // reorder menu. This anchor forces MOVE LANE TO's child to open left.
        auto edgeParent = page->popupBounds(0);
        MouseMoveEvent edgeHover;
        edgeHover.mousePosition = { edgeParent.getCenter().x, edgeParent.top + 5 * 21 + 10 };
        platform->platformOnEvent(edgeHover);
        pump();
        check(!page->popupBounds(1).isEmpty() && page->popupBounds(1).right <= edgeParent.left,
            "right-edge submenu did not open to the left of its parent");
        capture("main-submenu-edge");
        dismissMenus();
        grid.select({ 0, 0, 0 });
        page->focusTracker();
        key(VirtualKey::None, '6');
        key(VirtualKey::None, '0');
        key(VirtualKey::Return);
        check(state.session.pattern.tracks[0].notes[0].state == NoteCellState::Note
                && state.session.pattern.tracks[0].notes[0].note == 60,
            "portable text entry lost initial token/caret/commit");
        check(dynamic_cast<CTextEdit*>(frame->getFocusView()) == nullptr,
            "committed text field retained focus");
        grid.select({ 0, 0, 1 });
        page->focusTracker();
        key(VirtualKey::None, '7');
        key(VirtualKey::Escape);
        check(state.session.pattern.tracks[0].notes[1].state == NoteCellState::Rest,
            "Escape wrote a cell");
        grid.select({ 0, 0, 0 });
        grid.beginCellEditing();
        auto* edit = dynamic_cast<CTextEdit*>(frame->getFocusView());
        check(edit != nullptr, "cell editor did not receive focus");
        if (edit)
            edit->setText("60+64+67");
        key(VirtualKey::Return);
        check(state.session.pattern.tracks[0].notes[0].noteVoiceCount() == 3,
            "chord text entry lost polyphony");
        grid.select({ 0, 0, 0 });
        grid.select({ 0, 0, 3 }, true);
        grid.copy();
        grid.select({ 1, 0, 4 });
        grid.keyDown({ GridKey::None, "v", Control, 478 });
        check(state.session.pattern.tracks[1].notes[4].noteVoiceCount() == 3,
            "typed clipboard lost chord");
        const auto before = publications;
        grid.select({ 0, 1, 0 });
        grid.beginValueDrag({ 0, 1, 0 });
        grid.updateValueDrag(20, false, false);
        auto first = state.session.pattern.tracks[0].velocities[0].valueVoice(0);
        grid.updateValueDrag(20, false, false);
        check(state.session.pattern.tracks[0].velocities[0].valueVoice(0) == first,
            "drag accumulated the same delta twice");
        check(publications == before, "drag published before mouse-up");
        grid.finishValueDrag();
        check(publications <= before + 1, "drag published more than once");
        state.session.selectedRow = 2;
        page->scrollTo(0, 0);
        click(12, 115 + 86 + 25 * 5 + 12, 1, Modifiers { ModifierKey::Shift });
        check(grid.selectingWholeRows && grid.effectiveGridSelection().firstRow == 2
                && grid.effectiveGridSelection().lastRow == 5,
            "Shift gutter selection differs from Cocoa");
        click(12, 115 + 86 + 25 * 7 + 12);
        check(state.session.transport.loopStartRow == 7 && state.session.transport.loopEndRow == 8,
            "gutter loop gesture failed");
        const auto transportBefore = transport;
        click(396, 790);
        check(transport > transportBefore, "swing drag did not publish");
        state.playing = true;
        state.notePlayheads[0] = 10;
        state.velocityPlayheads[0] = 9;
        auto notesBefore = state.session.pattern.tracks[0].notes;
        auto sentBefore = state.sentEventCount;
        auto publishBefore = publications;
        for (int fps : { 15, 30, 60, 120 })
            for (int i = 0; i < fps; ++i)
                page->refreshPlaybackDisplay();
        check(state.notePlayheads[0] == 10 && state.velocityPlayheads[0] == 9
                && state.sentEventCount == sentBefore && publications == publishBefore,
            "GUI refresh rate advanced MIDI/playback or published edits");
        pump();
        const auto muteDraws = page->drawCount();
        state.songPlaybackMutedTracks = 1;
        page->refreshPlaybackDisplay();
        for (int attempt = 0; attempt < 20 && page->drawCount() == muteDraws; ++attempt)
            pump();
        check(
            page->drawCount() > muteDraws, "Song-follow mute change did not invalidate main grid");
        state.songPlaybackMutedTracks = 0;
        state.songPlaybackActive = true;
        auto readOnlyBefore = publications;
        grid.select({ 0, 0, 0 });
        grid.keyDown({ GridKey::Delete, "", 0, 478 });
        check(publications == readOnlyBefore,
            "Song-follow keyboard edit changed the playing pattern");
        state.songPlaybackActive = false;
        state.playing = false;
        page->setGridZoom(.55);
        capture("main-55");
        page->setGridZoom(1.8);
        page->scrollTo(180, 220);
        capture("main-scrolled-180");
        page->setGridZoom(1);
        page->scrollTo(0, 0);
        grid.select({ 0, 0, 0 });
        grid.beginCellEditing();
        edit = dynamic_cast<CTextEdit*>(frame->getFocusView());
        if (edit)
            edit->setText("invalid-note");
        key(VirtualKey::Return);
        check(dynamic_cast<CTextEdit*>(frame->getFocusView()) != nullptr,
            "invalid cell input dismissed silently");
        [host setHidden:YES];
        pump();
        [host setHidden:NO];
        pump();
        check(dynamic_cast<CTextEdit*>(frame->getFocusView()) == nullptr,
            "hidden page retained invalid text editor");

        // Playback-follow uses the published NOTE rows, never an inferred
        // clock, latest note-on, longest lane, or the editing cursor.
        state.session.pattern.visibleRows = 64;
        for (auto& track : state.session.pattern.tracks) {
            track.notes.resize(64, NoteCell::rest());
            track.noteColumn.length = 64;
        }
        state.sequenceColumnsExpanded = false;
        state.songPlaybackActive = false;
        state.playing = true;
        state.session.selectedTrack = 0;
        state.notePlayheads[0] = 40;
        state.notePlayheads[1] = 7;
        page->reloadModel();
        page->scrollTo(0, 0);
        pump();
        check(state.trackerFollow.mode == TrackerFollowMode::Static && page->gridScrollY() == 0,
            "default STATIC scrolled during playback");
        capture("main-follow-static");
        const auto followPublications = publications, followTransport = transport;
        const auto followEvents = state.sentEventCount;
        click(985, 20); // Actual shared custom dropdown, not test-only state.
        key(VirtualKey::Down);
        key(VirtualKey::Down);
        key(VirtualKey::Return);
        check(state.trackerFollow.mode == TrackerFollowMode::Center,
            "VIEW follow menu did not select CENTER");
        auto guide = page->playbackGuideRect();
        const auto center = (guide.top + guide.bottom) * .5;
        check(!guide.isEmpty() && !page->followingPaused()
                && std::abs(center - (115. + 86. + (478. - 86. - 10.) * .5)) < .01,
            "CENTER guide is not centered in the row viewport");
        capture("main-follow-center");
        for (std::size_t row : { 41u, 63u, 0u, 9u, 8u, 47u, 4u }) {
            state.notePlayheads[0] = row;
            state.notePlayheads[1] = 63 - row;
            pump();
            guide = page->playbackGuideRect();
            check(std::abs((guide.top + guide.bottom) * .5 - center) < .01,
                "CENTER moved its guide at a reverse/stride/random/wrap step");
        }
        capture("main-follow-center-start");
        const auto pinnedPosition = page->gridScrollY();
        const auto secondLaneX = gridLaneFieldX(1, gridLaneWidth(false)) + 10;
        click(secondLaneX, 125); // Header selects without scrolling to edit row.
        check(state.session.selectedTrack == 1 && page->followLane() == 0
                && page->gridScrollY() == pinnedPosition && !page->followingPaused(),
            "clicking another header stole a pinned follow source");
        page->setFollowSource(true, 0);
        pump();
        check(page->followLane() == 1 && !page->followingPaused(), "selected NOTE source failed");
        page->setFollowSource(false, 0);
        state.notePlayheads[0] = 0;
        pump();
        const auto blankSelection = state.session.selectedRow;
        click(80,
            115 + 86 + 10); // Leading CENTER padding is not an editable header.
        check(state.session.selectedRow == blankSelection && !page->followingPaused(),
            "CENTER blank padding dispatched a cell/header gesture");

        MouseWheelEvent wheel;
        wheel.mousePosition = { 350, 430 };
        wheel.deltaY = -1;
        platform->platformOnEvent(wheel);
        pump();
        const auto manuallyScrolled = page->gridScrollY();
        state.notePlayheads[0] = 32;
        pump();
        check(page->followingPaused() && page->gridScrollY() == manuallyScrolled,
            "manual scrolling did not hold follow");
        capture("main-follow-held");
        click(1260, 20);
        check(!page->followingPaused() && page->gridScrollY() != manuallyScrolled,
            "RESUME did not return to the source row");

        page->setFollowMode(TrackerFollowMode::Page);
        for (double zoom : { .55, 1., 1.8 }) {
            page->setGridZoom(zoom);
            const auto count = page->followPageRows();
            check(count == (zoom == .55 ? 16u : zoom == 1. ? 8u : 4u), "PAGE size at grid zoom");
            for (auto row : { count - 1, count, count + 1, std::size_t(63), std::size_t(0) }) {
                state.notePlayheads[0] = row;
                pump();
                check(page->gridScrollY() == double(row / count * count) * 25.,
                    "PAGE advanced at the wrong row boundary");
            }
        }
        page->setGridZoom(.55);
        state.notePlayheads[0] = 42;
        pump();
        capture("main-follow-page16");
        page->setGridZoom(1);
        pump();
        capture("main-follow-page8");
        page->setFollowMode(TrackerFollowMode::Center);
        const auto noteX = gridLaneFieldX(0, gridLaneWidth(false)) + 10;
        guide = page->playbackGuideRect();
        click(noteX, (guide.top + guide.bottom) * .5, 2);
        check(page->followingPaused() && state.session.selectedRow == 42
                && dynamic_cast<CTextEdit*>(frame->getFocusView()) != nullptr,
            "CENTER cell editing used the wrong row or failed to hold");
        const auto editScroll = page->gridScrollY();
        state.notePlayheads[0] = 55;
        pump();
        check(page->gridScrollY() == editScroll, "playback moved a live cell editor");
        key(VirtualKey::Escape);
        check(page->followingPaused(), "ending an edit silently resumed follow");
        page->resumeFollowing();
        state.playing = false;
        const auto stoppedScroll = page->gridScrollY();
        state.notePlayheads[0] = 0;
        pump();
        check(page->gridScrollY() == stoppedScroll && page->playbackGuideRect().isEmpty(),
            "stopping playback jumped the viewport");
        state.playing = true;
        page->setFollowSource(false, 31);
        const auto missingScroll = page->gridScrollY();
        pump();
        check(!page->followLane() && page->gridScrollY() == missingScroll,
            "missing source silently followed another lane");
        page->setFollowSource(false, 0);
        page->scrollTo(0, 10);
        ++state.trackerFollowRevision;
        pump();
        check(!page->followingPaused(), "document recall retained a stale manual hold");
        state.notePlayheads[0] = 19;
        for (int fps : { 15, 30, 60, 120 })
            for (int i = 0; i < fps; ++i)
                page->refreshPlaybackDisplay();
        check(state.notePlayheads[0] == 19 && publications == followPublications
                && transport == followTransport && state.sentEventCount == followEvents,
            "follow changed MIDI timing/events or published musical edits");
        page->setFollowMode(TrackerFollowMode::Static);
        check(page->playbackGuideRect().isEmpty(), "STATIC retained a follow guide");
        page->scrollTo(0, 0);
        pump();
        click(secondLaneX, 125, 1, {}, true);
        const auto followPopup = page->popupBounds(0);
        click(followPopup.getCenter().x, followPopup.top + 10);
        check(state.trackerFollow.mode == TrackerFollowMode::Center
                && !state.trackerFollow.selectedLane && page->followLane() == 1,
            "FOLLOW THIS LANE shortcut did not pin and enable CENTER");
        state.songPlaybackActive = true;
        page->reloadModel();
        pump();
        click(noteX, 125, 1, {}, true);
        const auto songFollowPopup = page->popupBounds(0);
        check(!songFollowPopup.isEmpty(), "Song read-only view hid the follow-source shortcut");
        click(songFollowPopup.getCenter().x, songFollowPopup.top + 10);
        check(page->followLane() == 0 && !page->followingPaused(),
            "Song playback could not select a follow source");
        // Maximum lane count, long rows and a skipped UI refresh: the guide
        // lands on the latest snapshot, not a queue of obsolete display rows.
        state.songPlaybackActive = false;
        state.session.pattern.tracks.resize(kMaximumTrackCount);
        state.session.pattern.visibleRows = 256; // Existing Tracker row limit.
        state.session.pattern.tracks[31].noteColumn.length = 256;
        state.session.pattern.tracks[31].notes.resize(256, NoteCell::rest());
        page->setFollowSource(false, 31);
        state.notePlayheads[31] = 255;
        pump();
        check(std::abs(page->gridScrollY()
                  - TrackerFollowLayout(TrackerFollowMode::Center, 256, 478).scrollForRow(255))
                < .01,
            "long-pattern last lane did not follow the latest published row");
        state.notePlayheads[31] = 3;
        pump();
        check(std::abs(page->gridScrollY()
                  - TrackerFollowLayout(TrackerFollowMode::Center, 256, 478).scrollForRow(3))
                < .01,
            "GUI stall recovery replayed intermediate rows");
        [window close];
        window.contentView = nil;
        host = nil;
        window = nil;
        std::cout << "Tracker portable main page: " << (failures ? "FAILED" : "ok") << " ("
                  << failures << " failures)\n";
        return failures ? 1 : 0;
    }
}
