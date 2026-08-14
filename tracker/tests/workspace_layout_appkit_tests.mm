#import <Cocoa/Cocoa.h>

#import "s3g_tracker_controls.h"
#import "s3g_tracker_workspace.h"

#include "s3g_tracker_workspace_layout.h"

#include "s3g/tracker/fx_catalog.h"

#include <cmath>
#include <iostream>
#include <string>
#include <utility>

@interface NSView (S3GTrackerGridTestAccess)
- (void)laneMidiChannelSelected:(NSMenuItem*)sender;
- (void)laneMidiBusSelected:(NSMenuItem*)sender;
- (void)beginTrackNameEditingForTrack:(std::size_t)track rect:(NSRect)rect;
- (NSMenu*)sequenceActionMenuForTrack:(std::size_t)track
    row:(std::size_t)row field:(std::size_t)field;
- (void)sequenceActionSelected:(NSMenuItem*)sender;
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

NSEvent* mouseDownEvent(NSWindow* window, NSPoint location,
    NSInteger clickCount)
{
    return [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
        location:location modifierFlags:0u timestamp:0.0
        windowNumber:window.windowNumber context:nil eventNumber:1
        clickCount:clickCount pressure:1.0];
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
        int transportChangeRequests = 0;
        int restartRequests = 0;
        int trackResyncRequests = 0;
        std::size_t resyncedTrack = s3g::tracker::kMaximumTrackCount;
        callbacks.selectPattern = [&](const std::string& patternId) {
            selectedPattern = patternId;
            (void)state.patternBank.selectPattern(patternId);
        };
        callbacks.addPattern = [&](bool) { ++addPatternRequests; };
        callbacks.renamePattern = [&] { ++renamePatternRequests; };
        callbacks.deletePattern = [&] { ++deletePatternRequests; };
        callbacks.patternChanged = [&] { ++patternChangeRequests; };
        callbacks.transportChanged = [&] { ++transportChangeRequests; };
        callbacks.restartPlayback = [&] { ++restartRequests; };
        callbacks.resyncTrack = [&](std::size_t track) {
            ++trackResyncRequests;
            resyncedTrack = track;
        };

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
        NSTextField* columnSummary = [controller valueForKey:@"columnSummary"];
        NSTextField* bpmDisplay = [controller valueForKey:@"bpmDisplay"];
        NSPopUpButton* tempoScalePopup = [controller
            valueForKey:@"tempoScalePopup"];
        NSTextField* swingField = [controller valueForKey:@"swingField"];
        NSButton* restartButton = [controller valueForKey:@"restartButton"];
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
        NSView* envelopePlaybackOverlay = [envelope
            valueForKey:@"playbackOverlay"];
        NSView* geometryPlaybackOverlay = [geometryPage
            valueForKey:@"playbackOverlay"];
        check(envelopePlaybackOverlay.wantsLayer
                && geometryPlaybackOverlay.wantsLayer,
            "animated envelope and geometry marks should use isolated overlays");
        [window displayIfNeeded];
        grid.documentView.needsDisplay = NO;
        envelope.needsDisplay = NO;
        envelopePlaybackOverlay.needsDisplay = NO;
        geometryPlaybackOverlay.needsDisplay = NO;
        const double hiddenGeometryAnimationTime = [[geometryPage
            valueForKey:@"lastReadHeadAnimationTime"] doubleValue];
        state.playing = true;
        state.notePlayheads[0u] = 3u;
        state.velocityPlayheads[0u] = 4u;
        [controller refreshPlaybackDisplay];
        check([[grid.documentView
                    valueForKey:@"playbackPresentationPrimed"] boolValue],
            "visible tracker playback should advance its incremental grid presentation state");
        check(near([[geometryPage valueForKey:
                    @"lastReadHeadAnimationTime"] doubleValue],
                hiddenGeometryAnimationTime),
            "hidden geometry should not advance its playback animation");
        state.playing = false;
        [controller refreshPlaybackDisplay];
        check([columnSummary.stringValue containsString:@"SEQ2"]
                && ![columnSummary.stringValue containsString:@"BUS"],
            "tracker should expose one unified sequencing grid without INS/BUS cells");
        state.hostBpm = 128.25;
        state.tempoScale = 0.5;
        [controller reloadModel];
        check([bpmDisplay.stringValue isEqualToString:@"128.25"]
                && [tempoScalePopup.selectedItem.title isEqualToString:@"1/2×"],
            "transport should display host BPM and the persisted musical rate");
        [tempoScalePopup selectItemAtIndex:4u];
        [tempoScalePopup sendAction:tempoScalePopup.action
            to:tempoScalePopup.target];
        check(std::abs(state.tempoScale - 1.5) < 1.0e-9
                && transportChangeRequests == 1,
            "rate menu should publish the selected musical host-tempo ratio");
        check(NSWidth(grid.documentView.frame) >
                NSWidth(grid.contentView.bounds) + 1000.0
                && grid.hasHorizontalScroller,
            "many tracks should widen the grid document and scroll");
        check(NSHeight(grid.documentView.frame) >
                NSHeight(grid.contentView.bounds)
                && grid.hasVerticalScroller,
            "many rows should heighten the grid document and scroll");
        const CGFloat laneWidth = (NSWidth(grid.documentView.bounds)
                - s3g::tracker::app::kTrackerRowNumberWidth
                - 11.0 * s3g::tracker::app::kTrackerLaneGutter) / 12.0;
        const CGFloat firstFieldCenterX
            = s3g::tracker::app::kTrackerRowNumberWidth + 3.0
                + (laneWidth - 6.0) * 0.095;
        const CGFloat firstLaneSyncCenterX
            = s3g::tracker::app::kTrackerRowNumberWidth + 3.0
                + (laneWidth - 6.0) - 104.0;
        const auto headerClick = [&](CGFloat y, NSInteger clicks) {
            const NSPoint inWindow = [grid.documentView convertPoint:
                NSMakePoint(firstFieldCenterX, y) toView:nil];
            [grid.documentView mouseDown:mouseDownEvent(
                window, inWindow, clicks)];
        };
        const bool initiallyMuted
            = state.session.pattern.tracks[0u].noteColumn.muted;
        const NSPoint syncInWindow = [grid.documentView convertPoint:
            NSMakePoint(firstLaneSyncCenterX, 12.0) toView:nil];
        [grid.documentView mouseDown:mouseDownEvent(
            window, syncInWindow, 1)];
        check(trackResyncRequests == 1 && resyncedTrack == 0u
                && patternChangeRequests == 0,
            "the lane-header SYNC control should target only that track without editing the pattern");
        headerClick(44.0, 1);
        check(state.session.pattern.tracks[0u].noteColumn.muted
                    == initiallyMuted
                && patternChangeRequests == 0,
            "clicking the dedicated length row must not toggle column mute");
        const auto initialDirection
            = state.session.pattern.tracks[0u].noteColumn.direction;
        headerClick(60.0, 1);
        check(state.session.pattern.tracks[0u].noteColumn.direction
                    != initialDirection
                && state.session.pattern.tracks[0u].noteColumn.muted
                    == initiallyMuted
                && patternChangeRequests == 1,
            "the dedicated direction row should cycle without toggling mute");
        headerClick(60.0, 1);
        headerClick(60.0, 1);
        headerClick(60.0, 1);
        check(state.session.pattern.tracks[0u].noteColumn.direction
                    == initialDirection
                && patternChangeRequests == 4,
            "four direction-row clicks should cycle back to the original mode");
        headerClick(76.0, 1);
        check(state.session.pattern.tracks[0u].noteColumn.muted
                    != initiallyMuted
                && patternChangeRequests == 5,
            "the dedicated MUTE row should be the column mute mouse target");
        headerClick(76.0, 1);
        check(state.session.pattern.tracks[0u].noteColumn.muted
                    == initiallyMuted,
            "the dedicated MUTE row should toggle independently");
        patternChangeRequests = 0;
        check(NSWidth(transport.documentView.frame) >
                NSWidth(transport.contentView.bounds) + 200.0
                && transport.hasHorizontalScroller,
            "pattern and transport controls should remain scrollable");
        check(patternPopup.numberOfItems == 2u && patternPopup.enabled
                && patternPopup.target == controller
                && patternPopup.action == @selector(patternSelectionChanged:),
            "compact pattern popup should expose both bank entries");
        check([patternPopup isKindOfClass:S3GTrackerPopupButton.class]
                && patternPopup.menu.font != nil
                && near(patternPopup.intrinsicContentSize.height, 26.0),
            "tracker popups should share centered mono menu typography");
        check([swingField isKindOfClass:S3GTrackerDragNumberField.class],
            "toolbar numeric fields should support vertical tracker dragging");
        if ([swingField isKindOfClass:S3GTrackerDragNumberField.class]) {
            auto* dragField = (S3GTrackerDragNumberField*)swingField;
            check(near([dragField s3gValueFromStart:60.0 verticalDelta:10.0
                           modifierFlags:0u], 61.0)
                    && near([dragField s3gValueFromStart:50.0
                           verticalDelta:-100.0 modifierFlags:0u], 50.0),
                "vertical number dragging should increase upward and clamp to range");
        }
        check([root isKindOfClass:S3GTrackerFocusReleaseView.class]
                && [controller valueForKey:@"toolbar"] != nil,
            "workspace backgrounds should support click-away field release");
        const double unchangedSwing = swingField.doubleValue;
        [window makeFirstResponder:swingField];
        [swingField selectText:nil];
        NSText* activeFieldEditor = swingField.currentEditor;
        check(activeFieldEditor != nil && window.firstResponder == activeFieldEditor,
            "click-away test should begin with the numeric field editor active");
        [(S3GTrackerFocusReleaseView*)root mouseDown:mouseDownEvent(
            window, NSMakePoint(1.0, 1.0), 1)];
        check(window.firstResponder == root
                && swingField.currentEditor == nil
                && near(swingField.doubleValue, unchangedSwing, 0.0001),
            "clicking blank workspace should release an unchanged text field");
        check(restartButton != nil
                && [restartButton.accessibilityLabel
                    containsString:@"Restart tracker"],
            "embedded transport should expose tracker restart instead of host stop/pause");
        [restartButton sendAction:restartButton.action to:restartButton.target];
        check(restartRequests == 1,
            "restart control should dispatch an internal scheduler restart");
        check(S3GTrackerThemeRGB(S3GTrackerThemeRole::GridPlayback)
                    == 0x2e412e
                && S3GTrackerThemeRGB(
                    S3GTrackerThemeRole::GridPlaybackAccent) == 0x69826b
                && S3GTrackerThemeRGB(S3GTrackerThemeRole::GridSelection)
                    == 0x303854
                && S3GTrackerThemeRGB(S3GTrackerThemeRole::GridCursor)
                    == 0x4d4d6b,
            "tracker playback and selection cells should retain the v8 semantic palette");
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

        [grid.documentView beginTrackNameEditingForTrack:2u
            rect:NSMakeRect(40.0, 3.0, 110.0, 18.0)];
        NSTextField* trackNameEditor = [grid.documentView
            valueForKey:@"cellEditor"];
        trackNameEditor.stringValue = @"BREAKS A";
        [trackNameEditor sendAction:trackNameEditor.action
            to:trackNameEditor.target];
        check(state.session.pattern.tracks[2u].name == "BREAKS A"
                && patternChangeRequests == 3,
            "lane-name editor should commit a renamed track and publish it");

        NSMenu* sequenceMenu = [grid.documentView
            sequenceActionMenuForTrack:0u row:4u field:2u];
        NSMenuItem* flamItem = nil;
        for (NSMenuItem* item in sequenceMenu.itemArray) {
            NSDictionary* represented = item.representedObject;
            if (![represented isKindOfClass:NSDictionary.class]
                || ![represented[@"kind"] isEqualToString:@"action"])
                continue;
            const auto* action = s3g::tracker::sequencerAction(
                [represented[@"action"] unsignedIntegerValue]);
            if (action && action->action
                    == s3g::tracker::SequencerAction::Flam) {
                flamItem = item;
                break;
            }
        }
        check(sequenceMenu.numberOfItems
                    == static_cast<NSInteger>(
                        s3g::tracker::sequencerActionCount() + 5u)
                && sequenceMenu.font != nil && flamItem != nil
                && [flamItem.title containsString:@"FL"]
                && [flamItem.title containsString:@"FLAM"],
            "SEQ context menu should expose every named sequencing action");
        [grid.documentView sequenceActionSelected:flamItem];
        const auto& chosenPair = state.session.pattern.tracks[0u].fxPairs[0u];
        check(chosenPair.actions.size() > 4u
                && chosenPair.actions[4u].state
                    == s3g::tracker::FxActionCellState::Sequencer
                && chosenPair.actions[4u].sequencerAction
                    == s3g::tracker::SequencerAction::Flam
                && chosenPair.values.size() > 4u
                && chosenPair.values[4u].state
                    == s3g::tracker::FxValueCellState::Value
                && near(chosenPair.values[4u].normalized, 0.5)
                && patternChangeRequests == 4,
            "choosing a SEQ action should author it with a visible default value");

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
