#include "s3g_tracker_controls.h"
#include "s3g_tracker_warp_page_host.h"
#include "s3g_tracker_warp_window.h"
#include "vstgui/lib/events.h"
#include <cmath>
#include <iostream>

using namespace s3g::tracker;
using namespace s3g::tracker::editor;
using namespace VSTGUI;

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
        app::TrackerViewState state, reference;
        app::WorkspaceCallbacks callbacks, referenceCallbacks;
        callbacks.transportChanged = [&] { ++changes; };
        auto* host = [[S3GTrackerWarpPageHost alloc] initWithState:&state callbacks:&callbacks];
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
        auto pump = [&] {
            [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.04]];
            [host refreshPlaybackDisplay];
            [host displayIfNeeded];
        };
        pump();
        check(page && page->getFrame() && page->drawCount(), "Warps CFrame attach/draw");
        if (!page || !page->getFrame())
            return 1;
        auto* platform = static_cast<IPlatformFrameCallback*>(page->getFrame());
        auto clickPoint = [&](CPoint p, int count = 1) {
            MouseDownEvent down;
            down.mousePosition = p;
            down.buttonState = MouseButton::Left;
            down.clickCount = count;
            platform->platformOnEvent(down);
            MouseUpEvent up;
            up.mousePosition = p;
            up.buttonState = MouseButton::Left;
            platform->platformOnEvent(up);
            pump();
        };
        auto click = [&](const char* id, int count = 1) {
            auto r = page->controlBounds(id);
            check(!r.isEmpty(), id);
            clickPoint({ r.left + r.getWidth() * .5, r.top + r.getHeight() * .5 }, count);
        };
        auto key = [&](VirtualKey virt, char32_t ch = 0) {
            KeyboardEvent e(EventType::KeyDown);
            e.virt = virt;
            e.character = ch;
            platform->platformOnEvent(e);
            pump();
        };
        auto entry = [&](const char* id, const std::string& text) {
            click(id, 2);
            check(dynamic_cast<CTextEdit*>(page->getFrame()->getFocusView()),
                "generic numeric/name text focus");
            for (char c : text)
                key(VirtualKey::None, static_cast<char32_t>(c));
            key(VirtualKey::Return);
        };
        auto* cocoa = [[S3GTrackerWarpWindowController alloc] initWithState:&reference
                                                                  callbacks:&referenceCallbacks];
        [cocoa.window setContentSize:NSMakeSize(1320, 820)];
        [cocoa.window.contentView layoutSubtreeIfNeeded];
        auto invoke = [&](NSString* name, SEL selector) {
            id control = [cocoa valueForKey:name];
            [NSApp sendAction:selector to:cocoa from:control];
        };
        auto equivalent = [&] {
            auto& a = state.session.transport;
            auto& b = reference.session.transport;
            bool same = a.timingWarpEnabled == b.timingWarpEnabled
                && a.warpCycleTicks == b.warpCycleTicks
                && a.timingWarp.size() == b.timingWarp.size();
            for (int i = 0; i <= 256; ++i)
                same &= std::abs(a.timingWarp.map(i / 256.) - b.timingWarp.map(i / 256.)) < 1e-12;
            check(same, "portable authoring differs from Cocoa");
        };
        auto capture = [&](const char* name) {
            const char* directory = std::getenv("S3G_TRACKER_WARP_CAPTURE_DIR");
            if (!directory)
                return;
            NSString* dir = @(directory);
            [[NSFileManager defaultManager] createDirectoryAtPath:dir
                                      withIntermediateDirectories:YES
                                                       attributes:nil
                                                            error:nil];
            [[host dataWithPDFInsideRect:host.bounds]
                writeToFile:[dir stringByAppendingPathComponent:
                                     [@(name) stringByAppendingString:@"-vstgui.pdf"]]
                 atomically:YES];
            reference = state;
            [cocoa reloadModel];
            [cocoa setValue:@(page->editor().selected) forKey:@"selectedTransform"];
            [cocoa setValue:@(page->editor().slot) forKey:@"selectedLibrarySlot"];
            [cocoa reloadModel];
            [[cocoa.window.contentView dataWithPDFInsideRect:cocoa.window.contentView.bounds]
                writeToFile:[dir stringByAppendingPathComponent:
                                     [@(name) stringByAppendingString:@"-cocoa.pdf"]]
                 atomically:YES];
        };
        capture("empty");
        // Compare real Cocoa selectors to CFrame pointer events and generic text.
        click("add0");
        NSButton* add = [[cocoa valueForKey:@"addButtons"] objectAtIndex:0];
        [NSApp sendAction:@selector(addTransform:) to:cocoa from:add];
        entry("primary", "1.75");
        NSTextField* primary = [cocoa valueForKey:@"primaryField"];
        primary.doubleValue = 1.75;
        invoke(@"primaryField", @selector(transformChanged:));
        entry("cycle", "7");
        NSTextField* cycle = [cocoa valueForKey:@"cycleField"];
        cycle.integerValue = 7;
        invoke(@"cycleField", @selector(cycleChanged:));
        equivalent();
        auto field = page->controlBounds("cycle");
        NSView* native = [cocoa valueForKey:@"cycleField"];
        NSRect nr = [native convertRect:native.bounds toView:cocoa.window.contentView];
        check(std::abs(nr.origin.x - field.left) < .01 && std::abs(nr.origin.y - field.top) < .01
                && std::abs(nr.size.width - field.getWidth()) < .01,
            "slider geometry differs from Cocoa");
        capture("exponential");
        click("mode");
        NSButton* mode = [cocoa valueForKey:@"warpModeButton"];
        mode.state = state.session.transport.timingWarpEnabled;
        invoke(@"warpModeButton", @selector(toggleWarpMode:));
        equivalent();
        capture("bypass");
        click("slot");
        auto last = page->popupItemBounds(63);
        check(!last.isEmpty() && last.bottom <= page->getViewSize().getHeight() - 8,
            "64th slot unreachable");
        capture("slot-menu");
        clickPoint({ last.left + 12, last.top + 10 });
        check(page->editor().slot == 63, "64th slot selection");
        entry("name", "PORTABLE WARP");
        check(state.session.warpLibrary.entry(63) && page->editor().name() == "PORTABLE WARP",
            "name Return should save");
        click("clear");
        click("slot");
        key(VirtualKey::Up);
        key(VirtualKey::Return); // empty slot 63
        check(page->editor().slot == 62 && state.session.transport.timingWarp.empty(),
            "empty slot semantics");
        click("slot");
        key(VirtualKey::Down);
        key(VirtualKey::Return);
        check(state.session.transport.timingWarp.size() == 1
                && state.session.transport.warpCycleTicks == 7,
            "saved slot auto-recall");
        click("delete");
        check(
            !state.session.warpLibrary.entry(63) && state.session.transport.timingWarp.size() == 1,
            "delete slot cleared active stack");
        click("type");
        key(VirtualKey::Down);
        key(VirtualKey::Down);
        key(VirtualKey::Return);
        check(page->editor().field(WarpField::Pulses).visible
                && page->editor().field(WarpField::Primary).value == 8,
            "Euclidean type controls");
        entry("mix", ".37");
        entry("begin", ".2");
        entry("end", ".8");
        entry("repeats", "3");
        check(page->editor().field(WarpField::Mix).value == .37
                && page->editor().field(WarpField::Begin).value == .2
                && page->editor().field(WarpField::End).value == .8
                && page->editor().field(WarpField::Repeats).value == 3,
            "numeric option edits");
        auto before = changes;
        entry("begin", ".9");
        check(changes == before && page->editor().field(WarpField::Begin).value == .2,
            "invalid numeric edit must revert atomically");
        click("mix", 2);
        key(VirtualKey::None, '9');
        key(VirtualKey::Escape);
        check(page->editor().field(WarpField::Mix).value == .37, "Escape must cancel");
        // Pointer drag, not click-only stepping, using real frame event dispatch.
        field = page->controlBounds("cycle");
        MouseDownEvent down;
        down.mousePosition = { field.left + 1, field.top + 12 };
        down.buttonState = MouseButton::Left;
        platform->platformOnEvent(down);
        MouseMoveEvent move;
        move.mousePosition = { field.left + 150, field.top + 12 };
        move.buttonState = MouseButton::Left;
        platform->platformOnEvent(move);
        MouseUpEvent up;
        up.mousePosition = move.mousePosition;
        up.buttonState = MouseButton::Left;
        platform->platformOnEvent(up);
        pump();
        check(state.session.transport.warpCycleTicks == 16, "continuous slider drag");
        capture("euclidean-options");
        state.playing = state.timingWarpPlaybackActive = state.timingWarpPlaybackFromSong = true;
        state.timingWarpPlaybackCycleTicks = 8;
        state.timingWarpPlaybackTick = 3;
        state.timingWarpPlaybackStack.append(TimingWarpTransform::exponential(2));
        pump();
        capture("song-playing");
        check([host.accessibilityValue isEqualToString:@"Song warp playback, step 4 of 8"],
            "Song sounding snapshot accessibility");
        before = changes;
        for (int fps : { 15, 30, 60, 120 }) {
            for (int i = 0; i < fps; ++i)
                page->refreshPlaybackDisplay();
            check(state.timingWarpPlaybackTick == 3 && changes == before,
                "UI FPS must not advance time or publish transport edits");
        }
        auto* detached = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(0, 0, 920, 660)
                      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskResizable
                        backing:NSBackingStoreBuffered
                          defer:NO];
        detached.releasedWhenClosed = NO;
        [host removeFromSuperview];
        window.contentView = [[NSView alloc] init];
        detached.contentView = host;
        // Reparenting must update the hit map before the first paint: a click
        // at the new width must not search the previous 1320-point controls.
        check(page->controlBounds("cycle").right < 920,
            "detached hit map waited for a paint");
        MouseDownEvent firstClick;
        firstClick.mousePosition = {880, 186};
        firstClick.buttonState = MouseButton::Left;
        firstClick.clickCount = 2;
        page->onMouseDownEvent(firstClick);
        check(dynamic_cast<CTextEdit*>(page->getFrame()->getFocusView()) != nullptr,
            "first pre-paint detached numeric click did not enter text");
        page->stopRefresh();
        [detached makeKeyAndOrderFront:nil];
        auto draws = page->drawCount();
        state.timingWarpPlaybackTick = 5;
        pump();
        check(page->drawCount() > draws &&
                [host.accessibilityValue isEqualToString:@"Song warp playback, step 6 of 8"],
            "detached playback frozen");
        [cocoa.window setContentSize:NSMakeSize(920, 660)];
        [cocoa.window.contentView layoutSubtreeIfNeeded];
        capture("detached");
        entry("cycle", "9");
        check(state.session.transport.warpCycleTicks == 9, "text focus after detach");
        [detached setContentSize:NSMakeSize(480, 360)];
        pump();
        check(page->getViewSize().getHeight() >= 580 && page->getViewSize().getWidth() >= 720,
            "small detached window clips authoring controls");
        // At minimum size dispatch native coordinates through CFrame's transform.
        auto r = page->controlBounds("mode");
        double scale = std::min(480. / 720, 360. / 580);
        bool oldMode = state.session.transport.timingWarpEnabled;
        clickPoint({ (r.left + r.getWidth() / 2) * scale, (r.top + 7) * scale });
        check(oldMode != state.session.transport.timingWarpEnabled, "scaled detached hit testing");
        [host removeFromSuperview];
        detached.contentView = [[NSView alloc] init];
        window.contentView = host;
        [window setContentSize:NSMakeSize(1320, 820)];
        [window makeKeyAndOrderFront:nil];
        pump();
        entry("cycle", "11");
        check(state.session.transport.warpCycleTicks == 11, "text focus after reattach");
        host.hidden = YES;
        draws = page->drawCount();
        pump();
        check(page->drawCount() == draws, "hidden host redraw");
        host.hidden = NO;
        pump();
        check(page->drawCount() > draws, "reopened host redraw");
        click("cycle", 2);
        key(VirtualKey::None, '5');
        state.session.transport.warpCycleTicks = 4;
        page->reloadModel();
        pump();
        check(!dynamic_cast<CTextEdit*>(page->getFrame()->getFocusView())
                && state.session.transport.warpCycleTicks == 4,
            "project recall must cancel stale inline edits");
        page->stopRefresh();
        [host removeFromSuperview];
        window.contentView = [[NSView alloc] init];
        [window close];
        [detached close];
        [cocoa close];
        host = nil;
        std::cout << "Warps parity failures: " << failures << '\n';
        return failures ? 1 : 0;
    }
}
