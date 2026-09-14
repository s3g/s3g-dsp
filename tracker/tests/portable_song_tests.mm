#include "s3g_song_window.h"
#include "s3g_tracker_controls.h"
#include "s3g_tracker_song_page_host.h"
#include "vstgui/lib/events.h"
#include <cmath>
#include <iostream>

using namespace s3g::tracker;
using namespace s3g::tracker::editor;
using namespace VSTGUI;

@interface S3GTrackerPopupButton (SongParityMenu)
- (void)s3gOpenCanvasMenu;
- (void)s3gDismissCanvasMenu;
@end

namespace {
NSControl* control(NSView* view, SEL action)
{
    if ([view isKindOfClass:NSControl.class] && [(NSControl*)view action] == action)
        return (NSControl*)view;
    for (NSView* child in view.subviews)
        if (auto* found = control(child, action))
            return found;
    return nil;
}
bool equivalent(const SongArrangement& a, const SongArrangement& b)
{
    if (a.name != b.name || a.loop != b.loop || a.ticksPerBeat != b.ticksPerBeat
        || a.rows.size() != b.rows.size())
        return false;
    for (std::size_t i = 0; i < a.rows.size(); ++i) {
        const auto& x = a.rows[i];
        const auto& y = b.rows[i];
        if (x.id != y.id || x.patternId != y.patternId || x.durationTicks != y.durationTicks
            || x.repeats != y.repeats || std::abs(x.energy - y.energy) > 1e-6
            || std::abs(x.tempoMultiplier - y.tempoMultiplier) > 1e-9
            || x.swing.has_value() != y.swing.has_value()
            || (x.swing && std::abs(*x.swing - *y.swing) > 1e-9) || x.mutedTracks != y.mutedTracks
            || x.timingWarpLibraryIndex != y.timingWarpLibraryIndex
            || x.patternLoop.has_value() != y.patternLoop.has_value())
            return false;
        if (x.patternLoop
            && (x.patternLoop->startRow != y.patternLoop->startRow
                || x.patternLoop->endRow != y.patternLoop->endRow))
            return false;
    }
    return true;
}
}
int main()
{
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        int failures = 0, changes = 0;
        auto check = [&](bool ok, const char* message) {
            if (!ok) {
                ++failures;
                std::cerr << message << '\n';
            }
        };
        auto* host = [[S3GTrackerSongPageHost alloc] initWithFrame:NSMakeRect(0, 0, 1320, 820)];
        auto* window = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(0, 0, 1320, 820)
                      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskResizable
                        backing:NSBackingStoreBuffered
                          defer:NO];
        window.releasedWhenClosed = NO;
        window.contentView = host;
        [NSApp activateIgnoringOtherApps:YES];
        [window makeKeyAndOrderFront:nil];
        auto* page = host.page;
        page->editor.callbacks.changed = [&] { ++changes; };
        auto* cocoa = [[S3GTrackerSongWindowController alloc] init];
        [cocoa.window setContentSize:NSMakeSize(1320, 820)];
        auto pump = [&] {
            [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.03]];
            [cocoa.window.contentView layoutSubtreeIfNeeded];
            [host displayIfNeeded];
        };
        pump();
        check(page && page->getFrame() && page->drawCount(), "Song CFrame attach/draw");
        auto* platform = static_cast<IPlatformFrameCallback*>(page->getFrame());
        auto clickPoint = [&](CPoint p, Modifiers modifiers = Modifiers {}) {
            MouseDownEvent d;
            d.mousePosition = p;
            d.buttonState = MouseButton::Left;
            d.modifiers = modifiers;
            platform->platformOnEvent(d);
            MouseUpEvent u;
            u.mousePosition = p;
            u.buttonState = MouseButton::Left;
            u.modifiers = modifiers;
            platform->platformOnEvent(u);
            pump();
        };
        auto click = [&](const std::string& id) {
            auto r = page->controlBounds(id);
            check(!r.isEmpty(), id.c_str());
            clickPoint({ (r.left + r.right) * .5, (r.top + r.bottom) * .5 });
        };
        auto key = [&](VirtualKey virt) {
            KeyboardEvent e(EventType::KeyDown);
            e.virt = virt;
            platform->platformOnEvent(e);
            pump();
        };
        auto invoke = [&](NSString* name, SEL selector) {
            id c = [cocoa valueForKey:name];
            [NSApp sendAction:selector to:cocoa from:c];
        };
        SongArrangement fixture;
        fixture.name = "PORTABLE SONG";
        fixture.loop = true;
        for (uint32_t i = 0; i < 6; ++i) {
            SongRow r;
            r.id = i + 1;
            r.patternId = i == 2 ? "MISSING" : "A01";
            r.durationTicks = i == 1 ? 19 : 16;
            r.repeats = i == 1 ? 70 : 1;
            r.energy = i == 1 ? .37f : 1.f;
            r.tempoMultiplier = i == 1 ? 1.3 : 1;
            if (i != 1)
                r.swing = .56;
            r.mutedTracks = i == 0 ? 0x80010001 : 0;
            if (i == 1)
                r.patternLoop = SongPatternLoop { 3, 20 };
            if (i == 2)
                r.timingWarpLibraryIndex = 63;
            fixture.rows.push_back(r);
        }
        auto reset = [&] {
            page->editor.setPatterns(
                { { "A01", "FIRST", 32, 32 }, { "A02", "SHORT", 8, 3 } }, "A02");
            [cocoa setAvailablePatternIds:@[ @"A01", @"A02" ]
                             patternNames:@[ @"FIRST", @"SHORT" ]
                           patternLengths:@[ @32, @8 ]
                        patternLaneCounts:@[ @32, @3 ]
                          activePatternId:@"A02"];
            page->editor.setArrangement(fixture);
            page->modelChanged();
            [cocoa setSongArrangement:fixture];
            pump();
        };
        auto compare = [&] {
            check(equivalent(page->editor.snapshot(), [cocoa songArrangement]),
                "Cocoa/portable arrangement differs");
        };
        reset();
        compare();
        auto capture = [&](NSString* name) {
            const char* dir = std::getenv("S3G_TRACKER_SONG_CAPTURE_DIR");
            if (!dir)
                return;
            NSString* path = @(dir);
            [[NSFileManager defaultManager] createDirectoryAtPath:path
                                      withIntermediateDirectories:YES
                                                       attributes:nil
                                                            error:nil];
            [[host dataWithPDFInsideRect:host.bounds]
                writeToFile:[path stringByAppendingPathComponent:
                                      [name stringByAppendingString:@"-vstgui.pdf"]]
                 atomically:YES];
            [[cocoa.window.contentView dataWithPDFInsideRect:cocoa.window.contentView.bounds]
                writeToFile:[path stringByAppendingPathComponent:
                                      [name stringByAppendingString:@"-cocoa.pdf"]]
                 atomically:YES];
        };
        capture(@"arrangement");
        NSTableView* table = [cocoa valueForKey:@"tableView"];
        NSScrollView* scroll = [cocoa valueForKey:@"tableScrollView"];
        NSRect header = [table.headerView convertRect:table.headerView.bounds
                                               toView:cocoa.window.contentView];
        NSRect row = [table convertRect:[table rectOfRow:0] toView:cocoa.window.contentView];
        auto pc = page->cellBounds(0, 0);
        std::cout << "Song Cocoa header " << NSStringFromRect(header).UTF8String << " row "
                  << NSStringFromRect(row).UTF8String << " viewport "
                  << NSStringFromRect([scroll convertRect:scroll.bounds
                                                   toView:cocoa.window.contentView])
                         .UTF8String
                  << '\n';
        check(std::abs(row.origin.y - pc.top) < .01, "Cocoa row/header vertical geometry");
        for (int col = 0; col < 11; ++col) {
            auto native = [table convertRect:[table frameOfCellAtColumn:col row:0]
                                      toView:cocoa.window.contentView];
            auto portable = page->cellBounds(0, col);
            if (std::abs(native.origin.x - portable.left) > .01
                || std::abs(native.size.width - portable.getWidth()) > .01)
                std::cerr << "column " << col << " native " << NSStringFromRect(native).UTF8String
                          << " portable " << portable.left << "," << portable.getWidth() << '\n';
            check(std::abs(native.origin.x - portable.left) < .01
                    && std::abs(native.size.width - portable.getWidth()) < .01,
                "Cocoa column geometry");
        }
        // Compare every menu choice, including saved/custom/missing references.
        const int columns[] = { 1, 2, 3, 3, 4, 5, 6, 8 };
        const SEL selectors[]
            = { @selector(patternPopupChanged:),
                  @selector(warpPopupChanged:),
                  @selector(loopStartPopupChanged:),
                  @selector(loopEndPopupChanged:),
                  @selector(repeatsPopupChanged:),
                  @selector(ticksPopupChanged:),
                  @selector(tempoMultiplierPopupChanged:),
                  @selector(energyPopupChanged:) };
        for (std::size_t r = 0; r < fixture.rows.size(); ++r)
            for (int f = 0; f < 8; ++f) {
                auto* cell = [table viewAtColumn:columns[f] row:NSInteger(r) makeIfNecessary:YES];
                auto* native = (NSPopUpButton*)control(cell, selectors[f]);
                auto choices = page->editor.choices(r, SongField(f));
                check(native && native.numberOfItems == NSInteger(choices.size()),
                    "menu choice count parity");
                for (std::size_t i = 0; i < choices.size() && i < NSUInteger(native.numberOfItems);
                     ++i) {
                    if (choices[i].title != [native itemAtIndex:NSInteger(i)].title.UTF8String)
                        std::cerr << "menu " << f << " " << choices[i].title << " != " <<
                            [native itemAtIndex:NSInteger(i)].title.UTF8String << '\n';
                    check(choices[i].title == [native itemAtIndex:NSInteger(i)].title.UTF8String,
                        "menu title parity");
                    check(choices[i].selected == (native.indexOfSelectedItem == NSInteger(i)),
                        "menu selection parity");
                }
            }
        click("row0:4");
        auto* repeats = (S3GTrackerPopupButton*)control(
            [table viewAtColumn:4 row:0 makeIfNecessary:YES], @selector(repeatsPopupChanged:));
        [repeats s3gOpenCanvasMenu];
        pump();
        capture(@"repeats-menu");
        [repeats s3gDismissCanvasMenu];
        key(VirtualKey::Escape);
        click("add");
        invoke(@"addButton", @selector(addRow:));
        compare();
        click("duplicate");
        invoke(@"duplicateButton", @selector(duplicateSelectedRow:));
        compare();
        click("up");
        invoke(@"moveUpButton", @selector(moveSelectedRowUp:));
        compare();
        click("down");
        invoke(@"moveDownButton", @selector(moveSelectedRowDown:));
        compare();
        click("delete");
        invoke(@"removeButton", @selector(removeSelectedRow:));
        compare();
        reset();
        // Actual frame menus dispatch all arrangement fields, retaining native callbacks.
        for (int f = 0; f < 8; ++f) {
            if (f == 3) {
                auto cs = page->editor.choices(0, SongField::LoopIn);
                page->editor.choose(0, SongField::LoopIn, cs[2]);
                [cocoa setSongArrangement:page->editor.snapshot()];
                pump();
            }
            auto choices = page->editor.choices(0, SongField(f));
            std::size_t selection = std::min<std::size_t>(2, choices.size() - 1);
            click("row0:" + std::to_string(f));
            auto bounds = page->popupItemBounds(selection);
            check(!bounds.isEmpty(), "row menu opens");
            clickPoint({ bounds.left + 20, bounds.top + 10 });
            auto* native = (NSPopUpButton*)control(
                [table viewAtColumn:columns[f] row:0 makeIfNecessary:YES], selectors[f]);
            [native selectItemAtIndex:NSInteger(selection)];
            [NSApp sendAction:selectors[f] to:cocoa from:native];
            compare();
        }
        reset();
        for (uint32_t lane = 0; lane < 32; ++lane) {
            click("row0:mute" + std::to_string(lane));
            NSView* cell = [table viewAtColumn:9 row:0 makeIfNecessary:YES];
            auto* matrix = cell.subviews.firstObject;
            NSButton* mute = (NSButton*)matrix.subviews[lane];
            [NSApp sendAction:@selector(toggleLaneMute:) to:cocoa from:mute];
            compare();
        }
        // Swing publishes once on release, never in redraw/snapshot refresh.
        reset();
        auto swing = page->swingBounds(0);
        int before = changes;
        MouseDownEvent d;
        d.mousePosition = { swing.left + 3, swing.top + 12 };
        d.buttonState = MouseButton::Left;
        platform->platformOnEvent(d);
        MouseMoveEvent m;
        m.mousePosition = { swing.left + 28, swing.top + 12 };
        m.buttonState = MouseButton::Left;
        platform->platformOnEvent(m);
        for (int i = 0; i < 5; ++i) {
            page->editor.playbackRow = std::size_t(i);
            page->playbackChanged();
            pump();
        }
        check(changes == before && page->editor.arrangement.rows[0].swing == fixture.rows[0].swing,
            "swing published before mouse-up / FPS refresh changed authoring");
        MouseUpEvent u;
        u.mousePosition = m.mousePosition;
        u.buttonState = MouseButton::Left;
        platform->platformOnEvent(u);
        pump();
        check(changes == before + 1 && page->editor.arrangement.rows[0].swing > .7,
            "swing drag release");
        clickPoint({ swing.left + 3, swing.top + 12 }, Modifiers { ModifierKey::Alt });
        check(!page->editor.arrangement.rows[0].swing, "Option-click resets swing to base");
        page->editor.swing(0, 56);
        MouseWheelEvent wheel;
        wheel.mousePosition = { swing.left + 3, swing.top + 12 };
        wheel.deltaY = 1;
        platform->platformOnEvent(wheel);
        pump();
        check(std::abs(page->editor.swingPercent(0) - 56.5) < 1e-8,
            "coarse wheel must be one half-percent increment");
        wheel.flags = MouseWheelEvent::PreciseDeltas;
        wheel.deltaY = .04;
        platform->platformOnEvent(wheel);
        platform->platformOnEvent(wheel);
        pump();
        check(std::abs(page->editor.swingPercent(0) - 56.5) < 1e-8, "precise wheel remainder");
        platform->platformOnEvent(wheel);
        pump();
        check(std::abs(page->editor.swingPercent(0) - 57) < 1e-8, "precise whole-pixel increment");
        before = changes;
        platform->platformOnEvent(d);
        page->modelChanged();
        platform->platformOnEvent(u);
        pump();
        check(changes == before, "model restore cancels stale swing gesture");
        // Mouse gutter move and Option-copy preserve IDs, including while playing.
        reset();
        page->editor.playing = true;
        page->editor.playbackEnabled = true;
        page->invalid();
        pump();
        auto drag = [&](bool copy) {
            auto a = page->cellBounds(0, 0), b = page->cellBounds(2, 0);
            MouseDownEvent down;
            down.mousePosition = { a.left + 15, a.top + 15 };
            down.buttonState = MouseButton::Left;
            platform->platformOnEvent(down);
            MouseMoveEvent move;
            move.mousePosition = { b.left + 15, b.bottom - 3 };
            move.buttonState = MouseButton::Left;
            if (copy)
                move.modifiers = Modifiers { ModifierKey::Alt };
            platform->platformOnEvent(move);
            MouseUpEvent up;
            up.mousePosition = move.mousePosition;
            up.buttonState = MouseButton::Left;
            up.modifiers = move.modifiers;
            platform->platformOnEvent(up);
            pump();
        };
        drag(false);
        check(page->editor.arrangement.rows[2].id == 1, "gutter move");
        drag(true);
        check(page->editor.arrangement.rows.size() == 7 && page->editor.arrangement.rows[3].id != 2,
            "Option-gutter copy");
        int launches = 0, saves = 0, loads = 0;
        page->editor.callbacks.launch = [&](std::size_t r, SongLaunchQuantization q) {
            ++launches;
            check(r == std::size_t(page->editor.selected) && q == page->editor.quantization,
                "queued launch callback");
        };
        click("queue");
        check(launches == 1 && !page->editor.pendingRow, "queue must not invent playback position");
        page->editor.callbacks.saveProject = [&] { ++saves; };
        page->editor.callbacks.loadProject = [&] { ++loads; };
        click("file");
        key(VirtualKey::Return);
        click("file");
        key(VirtualKey::Down);
        key(VirtualKey::Return);
        check(saves == 1 && loads == 1, "Song project file callbacks");
        page->editor.pendingRow = 3;
        page->editor.pendingQuantization = SongLaunchQuantization::NextBeat;
        page->playbackChanged();
        [cocoa setSongArrangement:page->editor.snapshot()];
        [table selectRowIndexes:[NSIndexSet indexSetWithIndex:NSUInteger(page->editor.selected)]
            byExtendingSelection:NO];
        cocoa.playbackEnabled = page->editor.playbackEnabled;
        [cocoa setPlaybackLocked:page->editor.playing];
        [cocoa setPlaybackRow:4 valid:YES];
        [cocoa setPendingPlaybackRow:3 valid:YES quantization:1];
        pump();
        capture(@"playing-queued");
        // Full-length loop menus and arrangement scrolling remain reachable.
        page->editor.setPatterns({ { "A01", "LONG", 256, 32 } }, "A01");
        page->editor.setArrangement(fixture);
        page->modelChanged();
        pump();
        click("row0:2");
        auto last = page->popupItemBounds(256);
        check(!last.isEmpty() && last.bottom <= page->getViewSize().getHeight() - 8
                && last.right <= page->getViewSize().getWidth() - 8,
            "256-row loop menu bounded");
        clickPoint({ last.left + 20, last.top + 10 });
        check(page->editor.arrangement.rows[0].patternLoop->startRow == 255,
            "last loop entry selection");
        for (int i = 0; i < 80; ++i)
            page->editor.add();
        page->modelChanged();
        pump();
        key(VirtualKey::End);
        check(page->editor.selected == int(page->editor.arrangement.rows.size()) - 1
                && page->scrollY() > 0,
            "End selects/reveals last row");
        key(VirtualKey::Home);
        before = changes;
        auto firstRow = page->cellBounds(0, 0);
        MouseDownEvent edgeDown;
        edgeDown.mousePosition = { firstRow.left + 15, firstRow.top + 15 };
        edgeDown.buttonState = MouseButton::Left;
        platform->platformOnEvent(edgeDown);
        MouseMoveEvent edgeMove;
        edgeMove.mousePosition = { firstRow.left + 15, page->tableViewport().bottom - 2 };
        edgeMove.buttonState = MouseButton::Left;
        platform->platformOnEvent(edgeMove);
        for (int i = 0; i < 6; ++i)
            pump();
        check(page->scrollY() > 0 && changes == before,
            "stationary edge drag autoscroll without premature publication");
        key(VirtualKey::Escape);
        double stoppedScroll = page->scrollY();
        for (int i = 0; i < 4; ++i)
            pump();
        check(page->scrollY() == stoppedScroll && changes == before,
            "Escape stops drag timer without committing");
        auto playback = page->editor.playbackRow, pending = page->editor.pendingRow;
        for (int fps : { 15, 30, 60, 120 })
            for (int frame = 0; frame < fps; ++frame)
                page->playbackChanged();
        check(page->editor.playbackRow == playback && page->editor.pendingRow == pending
                && changes == before,
            "display FPS cannot advance Song or publish edits");
        key(VirtualKey::End);
        [window setContentSize:NSMakeSize(980, 408)];
        pump();
        page->scrollTo(10000, 10000);
        pump();
        check(page->scrollX() > 0
                && !page->controlBounds("row" + std::to_string(page->editor.selected) + ":delete")
                        .isEmpty(),
            "narrow table horizontal access");
        [window setContentSize:NSMakeSize(480, 360)];
        pump();
        check(page->getViewSize().getWidth() >= 980, "small view proportionally scaled");
        page->stopRefresh();
        [window close];
        [cocoa close];
        std::cout << "Portable Song failures: " << failures << '\n';
        return failures ? 1 : 0;
    }
}
