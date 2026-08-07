#import <Cocoa/Cocoa.h>

#import "s3g_tracker_workspace.h"

#include <cmath>
#include <iostream>
#include <string>
#include <utility>

@interface NSView (S3GTrackerGridTestAccess)
- (void)laneMidiChannelSelected:(NSMenuItem*)sender;
- (void)laneMidiBusSelected:(NSMenuItem*)sender;
@end

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

bool near(CGFloat actual, CGFloat expected, CGFloat tolerance = 1.0) noexcept
{
    return std::abs(actual - expected) <= tolerance;
}

NSEvent* keyEvent(NSWindow* window, NSString* characters,
    unsigned short keyCode, NSEventModifierFlags modifiers)
{
    return [NSEvent keyEventWithType:NSEventTypeKeyDown
        location:NSZeroPoint modifierFlags:modifiers timestamp:0.0
        windowNumber:window.windowNumber context:nil characters:characters
        charactersIgnoringModifiers:characters isARepeat:NO keyCode:keyCode];
}

void seedTracks(s3g::tracker::app::TrackerViewState& state)
{
    state.session.pattern.name = "P01";
    state.session.pattern.visibleRows = 64u;
    state.session.pattern.tracks.resize(12u);
    for (std::size_t lane = 0u;
         lane < state.session.pattern.tracks.size(); ++lane) {
        auto& track = state.session.pattern.tracks[lane];
        track.name = "TRACK " + std::to_string(lane + 1u);
        track.notes.resize(64u, s3g::tracker::NoteCell::rest());
        track.velocities.resize(64u,
            s3g::tracker::ValueCell::defaultValue());
        track.noteColumn.length = 64u;
        track.velocityColumn.length = 64u;
    }

    auto* first = state.patternBank.findEntry(
        state.patternBank.activePatternId);
    if (!first) return;
    first->pattern = state.session.pattern;
    first->pattern.name = "MAIN";
    s3g::tracker::PatternBankEntry second = *first;
    second.id = "A02";
    second.pattern.name = "BREAK";
    state.patternBank.entries.push_back(std::move(second));
}

} // namespace

int main()
{
    @autoreleasepool {
        [NSApplication sharedApplication];
        s3g::tracker::app::TrackerViewState state;
        s3g::tracker::app::WorkspaceCallbacks callbacks;
        seedTracks(state);
        std::string selectedPattern;
        int addPatternRequests = 0;
        int renamePatternRequests = 0;
        int deletePatternRequests = 0;
        int patternChangeRequests = 0;
        callbacks.selectPattern = [&](const std::string& patternId) {
            selectedPattern = patternId;
            (void)state.patternBank.selectPattern(patternId);
        };
        callbacks.addPattern = [&](bool) { ++addPatternRequests; };
        callbacks.renamePattern = [&] { ++renamePatternRequests; };
        callbacks.deletePattern = [&] { ++deletePatternRequests; };
        callbacks.patternChanged = [&] { ++patternChangeRequests; };

        S3GTrackerWorkspaceController* controller =
            [[S3GTrackerWorkspaceController alloc]
                initWithState:&state callbacks:&callbacks];
        NSWindow* window = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(0.0, 0.0, 1320.0, 780.0)
            styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
            backing:NSBackingStoreBuffered defer:NO];
        window.contentMinSize = NSMakeSize(760.0, 560.0);
        window.contentViewController = controller;
        [window makeKeyAndOrderFront:nil];
        [window setContentSize:NSMakeSize(760.0, 560.0)];
        NSView* root = controller.view;
        [root layoutSubtreeIfNeeded];
        check(near(NSWidth(window.contentView.bounds), 760.0)
                && near(NSWidth(window.frame), 760.0),
            "real workspace window should reach its 760-point minimum width");

        NSScrollView* grid = [controller valueForKey:@"gridScroll"];
        NSScrollView* transport = [controller valueForKey:@"transportScroll"];
        NSScrollView* modules = [controller valueForKey:@"moduleScroll"];
        NSStackView* moduleControls = [controller valueForKey:@"moduleControls"];
        NSPopUpButton* patternPopup = [controller valueForKey:@"patternPopup"];
        NSButton* createPatternButton = [controller
            valueForKey:@"createPatternButton"];
        NSButton* duplicatePatternButton = [controller
            valueForKey:@"duplicatePatternButton"];
        NSButton* renamePatternButton = [controller
            valueForKey:@"renamePatternButton"];
        NSButton* deletePatternButton = [controller
            valueForKey:@"deletePatternButton"];
        NSView* envelope = [controller valueForKey:@"envelopeView"];
        NSView* consoleOutput = [controller consolePageView];
        NSView* geometryPage = [controller geometryPageView];
        NSView* warpPage = [controller warpPageView];

        check(near(NSWidth(grid.frame), NSWidth(root.bounds)),
            "compact tracker should use the full embedded page width");
        check(near(NSHeight(envelope.frame), 100.8),
            "compact AppKit layout should shrink the envelope");
        check(consoleOutput && geometryPage && warpPage
                && consoleOutput != geometryPage
                && geometryPage != warpPage,
            "console, geometry, and warp modules should expose distinct pages");
        check(NSWidth(grid.documentView.frame) >
                NSWidth(grid.contentView.bounds) + 1000.0
                && grid.hasHorizontalScroller,
            "many tracks should widen the grid document and scroll");
        check(NSHeight(grid.documentView.frame) >
                NSHeight(grid.contentView.bounds)
                && grid.hasVerticalScroller,
            "many rows should heighten the grid document and scroll");
        check(NSWidth(transport.documentView.frame) >
                NSWidth(transport.contentView.bounds) + 200.0
                && transport.hasHorizontalScroller,
            "pattern and transport controls should remain scrollable");
        check(patternPopup.numberOfItems == 2u && patternPopup.enabled
                && patternPopup.target == controller
                && patternPopup.action == @selector(patternSelectionChanged:),
            "compact pattern popup should expose both bank entries");
        [patternPopup selectItemAtIndex:1];
        [patternPopup sendAction:patternPopup.action to:patternPopup.target];
        check(selectedPattern == "A02",
            "compact pattern popup should dispatch pattern selection");
        state.songPlaybackActive = true;
        [controller reloadModel];
        check(!patternPopup.enabled && !createPatternButton.enabled
                && !duplicatePatternButton.enabled
                && !renamePatternButton.enabled
                && !deletePatternButton.enabled,
            "active Song playback should lock every pattern-bank control");
        [createPatternButton sendAction:createPatternButton.action
            to:createPatternButton.target];
        [duplicatePatternButton sendAction:duplicatePatternButton.action
            to:duplicatePatternButton.target];
        [renamePatternButton sendAction:renamePatternButton.action
            to:renamePatternButton.target];
        check(addPatternRequests == 0 && renamePatternRequests == 0
                && deletePatternRequests == 0,
            "locked pattern-bank actions should not dispatch callbacks");
        state.songPlaybackActive = false;
        [controller reloadModel];
        check(patternPopup.enabled && createPatternButton.enabled
                && duplicatePatternButton.enabled
                && renamePatternButton.enabled
                && deletePatternButton.enabled,
            "pattern-bank controls should unlock after Song playback");
        const CGFloat moduleDocumentWidth = NSWidth(
            modules.documentView.frame);
        const NSView* lastModule = moduleControls.arrangedSubviews.lastObject;
        check(moduleDocumentWidth > 0.0 && lastModule
                && NSMaxX(lastModule.frame) <= moduleDocumentWidth + 1.0
                && (moduleDocumentWidth
                        <= NSWidth(modules.contentView.bounds) + 1.0
                    || modules.hasHorizontalScroller),
            "module buttons should fit or remain horizontally scrollable");
        check(!grid.hasAmbiguousLayout && !envelope.hasAmbiguousLayout,
            "compact workspace constraints should be unambiguous");

        NSMenuItem* laneChannel = [[NSMenuItem alloc] initWithTitle:@"CH 07"
            action:nil keyEquivalent:@""];
        laneChannel.representedObject = @{ @"track": @3, @"channel": @7 };
        [grid.documentView laneMidiChannelSelected:laneChannel];
        check(state.session.pattern.tracks[3u].midiChannel == 7u
                && state.session.pattern.tracks[2u].midiChannel == 1u,
            "lane channel menu should change only the targeted track");

        NSMenuItem* laneBus = [[NSMenuItem alloc] initWithTitle:@"BUS 06"
            action:nil keyEquivalent:@""];
        laneBus.representedObject = @{ @"track": @5, @"bus": @5 };
        [grid.documentView laneMidiBusSelected:laneBus];
        check(state.session.pattern.tracks[5u].initialInstrumentNodeId
                    == s3g::tracker::midiOutNodeForRackSlot(5u)
                && state.session.pattern.tracks[4u].initialInstrumentNodeId
                    != s3g::tracker::midiOutNodeForRackSlot(5u)
                && patternChangeRequests == 2,
            "lane bus menu should change only the targeted track and publish");

        NSPasteboard* pasteboard = NSPasteboard.generalPasteboard;
        [pasteboard clearContents];
        NSEvent* commandCopy = keyEvent(window, @"c", 8u,
            NSEventModifierFlagCommand);
        const BOOL commandCopyHandled =
            [grid.documentView performKeyEquivalent:commandCopy];
        check(!commandCopyHandled,
            "Command-C should report unhandled for REAPER");
        NSEvent* commandPaste = keyEvent(window, @"v", 9u,
            NSEventModifierFlagCommand);
        check(![grid.documentView performKeyEquivalent:commandPaste],
            "Command-V should report unhandled for REAPER");

        const NSInteger clipboardBefore = [[grid.documentView
            valueForKey:@"copiedPasteboardChangeCount"] integerValue];
        NSEvent* trackerCopy = keyEvent(window, @"c", 8u,
            NSEventModifierFlagControl);
        check([grid.documentView performKeyEquivalent:trackerCopy]
                && [[grid.documentView
                    valueForKey:@"copiedPasteboardChangeCount"] integerValue]
                    != clipboardBefore,
            "Control-C should invoke tracker-local copy");

        NSEvent* trackerPaste = keyEvent(window, @"v", 9u,
            NSEventModifierFlagControl);
        const BOOL trackerPasteHandled =
            [grid.documentView performKeyEquivalent:trackerPaste];
        check(trackerPasteHandled,
            "Control-V should report handled by the tracker");

        state.session.selectedTrack = 11u;
        state.session.selectedRow = 63u;
        [controller reloadModel];
        [root layoutSubtreeIfNeeded];
        check(NSMinX(grid.documentVisibleRect) > 1000.0
                && NSMinY(grid.documentVisibleRect) > 500.0,
            "selection navigation should reveal off-screen lanes and rows");

        [window setContentSize:NSMakeSize(1320.0, 780.0)];
        [root layoutSubtreeIfNeeded];
        check(near(NSWidth(window.contentView.bounds), 1320.0)
                && near(NSWidth(grid.frame), 1320.0),
            "spacious AppKit layout should give the tracker the full page");
        check(NSWidth(grid.documentView.frame) >
                NSWidth(grid.contentView.bounds),
            "track count should never force the main window wider");

        [[controller valueForKey:@"geometryWindowController"] close];
        [[controller valueForKey:@"warpWindowController"] close];
    }

    if (failures == 0) {
        std::cout << "workspace AppKit layout tests passed\n";
        return 0;
    }
    std::cerr << failures << " workspace AppKit assertion(s) failed\n";
    return 1;
}
