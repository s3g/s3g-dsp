#import <Cocoa/Cocoa.h>

#import "s3g_song_window.h"
#import "s3g_tracker_controls.h"

#include "s3g_gui_layout.h"

#include <cmath>
#include <iostream>

@interface NSControl (S3GSongSwingTestAccess)
@property(nonatomic) double s3gSwingValue;
@property(nonatomic) BOOL s3gHasOverride;
- (void)setSwingValue:(double)value hasOverride:(BOOL)hasOverride;
- (void)resetToBase;
- (BOOL)adjustByScrollDelta:(CGFloat)delta
    modifierFlags:(NSEventModifierFlags)modifierFlags;
- (NSRect)sliderTrackRect;
@end

@interface S3GTrackerPopupButton (S3GCanvasMenuTestAccess)
- (void)s3gOpenCanvasMenu;
- (void)s3gDismissCanvasMenu;
@end

@interface NSView (S3GCanvasMenuOverlayTestAccess)
- (NSInteger)itemIndexAtPoint:(NSPoint)point;
@end

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

bool near(CGFloat actual, double expected, double tolerance = 1.0)
{
    return std::abs(static_cast<double>(actual) - expected) <= tolerance;
}

NSEvent* songMouseEvent(NSWindow* window, NSEventType type,
    NSPoint location, NSEventModifierFlags modifiers)
{
    return [NSEvent mouseEventWithType:type location:location
        modifierFlags:modifiers timestamp:0.0
        windowNumber:window.windowNumber context:nil eventNumber:1
        clickCount:1 pressure:1.0];
}

NSPopUpButton* firstPopup(NSView* view)
{
    if ([view isKindOfClass:NSPopUpButton.class])
        return static_cast<NSPopUpButton*>(view);
    for (NSView* child in view.subviews) {
        if (NSPopUpButton* popup = firstPopup(child)) return popup;
    }
    return nil;
}

NSPopUpButton* popupWithAction(NSView* view, SEL action)
{
    if ([view isKindOfClass:NSPopUpButton.class]) {
        NSPopUpButton* popup = static_cast<NSPopUpButton*>(view);
        if (popup.action == action) return popup;
    }
    for (NSView* child in view.subviews) {
        if (NSPopUpButton* popup = popupWithAction(child, action))
            return popup;
    }
    return nil;
}

NSControl* firstSwingControl(NSView* view)
{
    if ([view isKindOfClass:
            NSClassFromString(@"S3GTrackerSongSwingField")])
        return static_cast<NSControl*>(view);
    for (NSView* child in view.subviews) {
        if (NSControl* field = firstSwingControl(child)) return field;
    }
    return nil;
}

NSControl* controlWithAction(NSView* view, SEL action)
{
    if ([view isKindOfClass:NSControl.class]) {
        NSControl* control = static_cast<NSControl*>(view);
        if (control.action == action) return control;
    }
    for (NSView* child in view.subviews) {
        if (NSControl* control = controlWithAction(child, action))
            return control;
    }
    return nil;
}

NSButton* muteButtonForLane(NSView* view, NSInteger row, NSInteger lane)
{
    Class muteClass = NSClassFromString(@"S3GTrackerSongMuteButton");
    if ([view isKindOfClass:muteClass]) {
        NSButton* button = static_cast<NSButton*>(view);
        if (button.tag == row * 32 + lane) return button;
    }
    for (NSView* child in view.subviews) {
        if (NSButton* button = muteButtonForLane(child, row, lane))
            return button;
    }
    return nil;
}

void testEmptyArrangementRemainsEmpty(
    S3GTrackerSongWindowController* controller)
{
    s3g::tracker::SongArrangement input;
    input.name.clear();
    input.loop = true;
    input.ticksPerBeat = 7u;
    [controller setSongArrangement:input];
    const auto output = [controller songArrangement];
    check(output.rows.empty(),
        "loading an empty arrangement must not synthesize an A01 row");
    check(output.name.empty() && output.loop && output.ticksPerBeat == 7u,
        "empty arrangement metadata should round trip exactly");
}

void testSharedToolboxLayout(
    S3GTrackerSongWindowController* controller)
{
    controller.window.contentView.frame = NSMakeRect(0.0, 0.0, 1080.0, 610.0);
    [controller.window.contentView layoutSubtreeIfNeeded];
    NSView* project = [controller valueForKey:@"projectPanel"];
    NSView* transport = [controller valueForKey:@"transportPanel"];
    NSView* rowTools = [controller valueForKey:@"rowToolsPanel"];
    NSView* arrangement = [controller valueForKey:@"arrangementPanel"];
    NSScrollView* tableScroll = [controller valueForKey:@"tableScrollView"];
    NSTableView* table = [controller valueForKey:@"tableView"];
    S3GTrackerPopupButton* fileMenu = [controller
        valueForKey:@"projectFileMenu"];
    S3GTrackerPopupButton* launchMenu = [controller
        valueForKey:@"launchQuantizationPopup"];
    NSButton* queueButton = [controller valueForKey:@"queueButton"];
    S3GTrackerToolboxView* projectPanel = [controller
        valueForKey:@"projectPanel"];
    S3GTrackerToolboxView* transportPanel = [controller
        valueForKey:@"transportPanel"];
    S3GTrackerToolboxView* rowToolsPanel = [controller
        valueForKey:@"rowToolsPanel"];
    NSButton* songMode = [controller valueForKey:@"songModeButton"];
    NSButton* songLoop = [controller valueForKey:@"songLoopButton"];
    NSButton* addButton = [controller valueForKey:@"addButton"];
    NSButton* duplicateButton = [controller valueForKey:@"duplicateButton"];
    NSButton* removeButton = [controller valueForKey:@"removeButton"];
    NSButton* moveUpButton = [controller valueForKey:@"moveUpButton"];
    NSButton* moveDownButton = [controller valueForKey:@"moveDownButton"];
    NSTextField* queueStatus = [controller
        valueForKey:@"queueStatusLabel"];
    check(near(NSMinY(project.frame),
                s3g::gui_layout::kTrackerPageContentTop)
            && near(NSMinX(project.frame),
                s3g::gui_layout::kTrackerPageHorizontalInset)
            && near(NSMinX(transport.frame) - NSMaxX(project.frame),
                s3g::gui_layout::kStandardMetrics.panelGap)
            && near(NSMinX(rowTools.frame) - NSMaxX(transport.frame),
                s3g::gui_layout::kStandardMetrics.panelGap)
            && near(NSMinY(arrangement.frame) - NSMaxY(project.frame),
                s3g::gui_layout::kStandardMetrics.panelGap)
            && near(NSMinY(tableScroll.frame),
                s3g::gui_layout::kStandardMetrics.headerHeight)
            && fileMenu.s3gUsesCanvasMenu
            && launchMenu.s3gUsesCanvasMenu
            && [queueButton.title isEqualToString:@"SELECT QUEUE"]
            && [queueButton.accessibilityLabel
                isEqualToString:@"Queue selected Song row"]
            && [transportPanel.toolboxTitle
                isEqualToString:@"TRANSPORT / QUEUE"]
            && projectPanel.toolboxIndex == 0u
            && transportPanel.toolboxIndex == 0u
            && rowToolsPanel.toolboxIndex == 0u
            && near(NSHeight(project.frame),
                s3g::gui_layout::toolboxHeightForRows(1u))
            && near(NSMidY(fileMenu.frame), NSMidY(songMode.frame), 0.01)
            && near(NSMidY(songMode.frame), NSMidY(songLoop.frame), 0.01)
            && near(NSMidY(songLoop.frame), NSMidY(launchMenu.frame), 0.01)
            && near(NSMidY(launchMenu.frame), NSMidY(queueButton.frame), 0.01)
            && near(NSMidY(queueButton.frame), NSMidY(queueStatus.frame), 0.01)
            && queueStatus.superview == transportPanel
            && NSMinX(queueStatus.frame) > NSMaxX(queueButton.frame)
            && NSMaxX(queueStatus.frame) <= NSWidth(transportPanel.bounds) - 7.0
            && near(NSMidY(addButton.frame), NSMidY(duplicateButton.frame), 0.01)
            && near(NSMidY(duplicateButton.frame), NSMidY(removeButton.frame), 0.01)
            && near(NSMidY(removeButton.frame), NSMidY(moveUpButton.frame), 0.01)
            && near(NSMidY(moveUpButton.frame), NSMidY(moveDownButton.frame), 0.01)
            && launchMenu.numberOfItems == 4u
            && [[launchMenu itemAtIndex:2u].title
                isEqualToString:@"END OF PASS"]
            && [[launchMenu itemAtIndex:3u].title
                isEqualToString:@"END OF ROW"]
            && launchMenu.indexOfSelectedItem == 3u
            && NSMaxX([table rectOfColumn:8])
                <= NSWidth(tableScroll.contentView.bounds) + 1.0
            && queueStatus.font.pointSize <= 10.0
            && [project isKindOfClass:
                NSClassFromString(@"S3GTrackerToolboxView")],
        "Song should use the shared toolbox/menu contract and keep DEL visible without horizontal scrolling at its default width");
    check([songMode.identifier isEqualToString:@"binary-status"]
            && songMode.state == NSControlStateValueOff
            && [songMode.title isEqualToString:@"SONG: OFF"]
            && [songLoop.title isEqualToString:@"LOOP: OFF"],
        "Song transport should use the shared red-off/green-on button semantic");
}

void testCanvasMenuRowsHitAcrossTheirFullWidth(
    S3GTrackerSongWindowController* controller)
{
    [controller showWindow:nil];
    [controller.window.contentView layoutSubtreeIfNeeded];
    S3GTrackerPopupButton* popup = [controller
        valueForKey:@"projectFileMenu"];
    const NSPoint popupPoint = [popup convertPoint:NSMakePoint(
        NSMidX(popup.bounds), NSMidY(popup.bounds)) toView:nil];
    NSView* popupHit = [controller.window.contentView hitTest:popupPoint];
    if (popupHit) [popupHit mouseDown:songMouseEvent(controller.window,
        NSEventTypeLeftMouseDown, popupPoint, 0u)];
    NSView* overlay = [popup valueForKey:@"s3gMenuOverlay"];
    const NSRect menuRect = [[overlay valueForKey:@"menuRect"] rectValue];
    const NSInteger leftIndex = [overlay itemIndexAtPoint:NSMakePoint(
        NSMinX(menuRect) + 1.0, NSMinY(menuRect) + 10.0)];
    const NSInteger rightIndex = [overlay itemIndexAtPoint:NSMakePoint(
        NSMaxX(menuRect) - 1.0, NSMinY(menuRect) + 10.0)];
    const NSPoint rightRowPoint = NSMakePoint(
        NSMaxX(menuRect) - 1.0, NSMinY(menuRect) + 10.0);
    const NSPoint rightRowInWindow = [overlay
        convertPoint:rightRowPoint toView:nil];
    NSView* rowHit = [controller.window.contentView
        hitTest:rightRowInWindow];
    if (rowHit) [rowHit mouseDown:songMouseEvent(controller.window,
        NSEventTypeLeftMouseDown, rightRowInWindow, 0u)];
    check(popupHit == popup && overlay != nil
            && NSWidth(menuRect) >= NSWidth(popup.bounds)
            && leftIndex == 0 && rightIndex == 0 && rowHit == overlay
            && [popup valueForKey:@"s3gMenuOverlay"] == nil,
        "Song canvas menus should open from a real window click and rows should hit through their full width");
}

void testHostTempoAndOptionalSwing(
    S3GTrackerSongWindowController* controller)
{
    s3g::tracker::SongArrangement input;
    input.name = "OPTIONAL";
    input.ticksPerBeat = 4u;
    s3g::tracker::SongRow inherited;
    inherited.patternId = "A01";
    inherited.durationTicks = 16u;
    inherited.repeats = 2u;
    input.rows.push_back(inherited);
    s3g::tracker::SongRow bpmOnly;
    bpmOnly.patternId = "A02";
    bpmOnly.durationTicks = 12u;
    bpmOnly.repeats = 1u;
    bpmOnly.bpm = 143.0;
    input.rows.push_back(bpmOnly);
    s3g::tracker::SongRow swingOnly;
    swingOnly.patternId = "A03";
    swingOnly.durationTicks = 9u;
    swingOnly.repeats = 3u;
    swingOnly.swing = 0.625;
    input.rows.push_back(swingOnly);

    [controller setSongArrangement:input];
    const auto output = [controller songArrangement];
    check(output.rows.size() == 3u,
        "song adapter should preserve row count");
    if (output.rows.size() != 3u) return;
    check(!output.rows[0u].bpm.has_value()
            && !output.rows[0u].swing.has_value(),
        "fully inherited host tempo and swing should remain optional");
    check(!output.rows[1u].bpm.has_value()
            && !output.rows[1u].swing.has_value(),
        "Song UI should discard hidden BPM overrides because REAPER owns tempo");
    check(!output.rows[2u].bpm.has_value()
            && output.rows[2u].swing == 0.625,
        "swing-only override should survive without manufacturing a BPM");

    __block int programmaticChanges = 0;
    controller.changeHandler = ^(NSString*) { ++programmaticChanges; };
    [controller setSongArrangement:input];
    check(programmaticChanges == 0,
        "programmatic project application must not report a user Song edit");
    controller.changeHandler = nil;
}

void testPlaybackPresentationDoesNotMutateArrangement(
    S3GTrackerSongWindowController* controller)
{
    const auto before = [controller songArrangement];
    controller.playbackEnabled = YES;
    [controller setAvailablePatternIds:@[ @"A01", @"A02", @"A03" ]
        activePatternId:@"A01"];
    [controller setPlaybackRow:1u valid:YES];
    [controller setPendingPlaybackRow:2u valid:YES];
    [controller setPlaybackLocked:YES];
    const auto after = [controller songArrangement];
    check(controller.playbackEnabled
            && before.rows.size() == after.rows.size()
            && before.name == after.name
            && before.loop == after.loop,
        "playback highlights and running-state presentation must not mutate the arrangement");
    [controller setPlaybackLocked:NO];
    controller.playbackEnabled = NO;
}

void testArrangementEditingRemainsAvailableDuringPlayback(
    S3GTrackerSongWindowController* controller)
{
    s3g::tracker::SongArrangement input;
    input.name = "LIVE EDIT";
    input.ticksPerBeat = 4u;
    s3g::tracker::SongRow row;
    row.patternId = "A01";
    row.durationTicks = 16u;
    row.repeats = 1u;
    input.rows.push_back(row);
    [controller setAvailablePatternIds:@[ @"A01", @"A02" ]
        patternNames:@[ @"ONE", @"TWO" ]
        patternLengths:@[ @16, @32 ]
        activePatternId:@"A01"];
    [controller setSongArrangement:input];
    [controller showWindow:nil];
    [controller.window.contentView layoutSubtreeIfNeeded];
    NSTableView* table = [controller valueForKey:@"tableView"];
    [table selectRowIndexes:[NSIndexSet indexSetWithIndex:0u]
        byExtendingSelection:NO];
    [controller setPlaybackLocked:YES];
    [controller.window.contentView layoutSubtreeIfNeeded];

    NSPopUpButton* pattern = popupWithAction(
        [table viewAtColumn:1 row:0 makeIfNecessary:YES],
        NSSelectorFromString(@"patternPopupChanged:"));
    NSPopUpButton* ticks = popupWithAction(
        [table viewAtColumn:5 row:0 makeIfNecessary:YES],
        NSSelectorFromString(@"ticksPopupChanged:"));
    NSControl* swing = firstSwingControl(
        [table viewAtColumn:6 row:0 makeIfNecessary:YES]);
    NSControl* deleteRow = controlWithAction(
        [table viewAtColumn:8 row:0 makeIfNecessary:YES],
        NSSelectorFromString(@"deleteRowButton:"));
    NSButton* add = [controller valueForKey:@"addButton"];
    NSButton* duplicate = [controller valueForKey:@"duplicateButton"];
    NSButton* songMode = [controller valueForKey:@"songModeButton"];
    NSButton* songLoop = [controller valueForKey:@"songLoopButton"];
    check(pattern.enabled && ticks.enabled && swing.enabled
            && deleteRow.enabled && add.enabled && duplicate.enabled
            && songLoop.enabled && songMode.enabled,
        "Song rows, arrangement loop, and the Song-mode escape switch should remain editable while playback is running");

    [ticks selectItemAtIndex:[ticks indexOfItemWithRepresentedObject:@8]];
    [ticks sendAction:ticks.action to:ticks.target];
    check([controller songArrangement].rows[0u].durationTicks == 8u,
        "an enabled Song menu must publish edits while playback is running");
    [controller setPlaybackLocked:NO];
}

void testLoopAndQueueFollowRunningHostClock(
    S3GTrackerSongWindowController* controller)
{
    s3g::tracker::SongArrangement arrangement;
    arrangement.name = "LIVE LOOP AND QUEUE";
    arrangement.ticksPerBeat = 4u;
    s3g::tracker::SongRow row;
    row.patternId = "A01";
    row.durationTicks = 8u;
    arrangement.rows.push_back(row);
    [controller setAvailablePatternIds:@[ @"A01" ]
        patternNames:@[ @"ONE" ] patternLengths:@[ @8 ]
        activePatternId:@"A01"];
    [controller setSongArrangement:arrangement];
    [controller showWindow:nil];
    [controller.window.contentView layoutSubtreeIfNeeded];
    NSTableView* table = [controller valueForKey:@"tableView"];
    [table selectRowIndexes:[NSIndexSet indexSetWithIndex:0u]
        byExtendingSelection:NO];
    controller.playbackEnabled = YES;
    [controller setPlaybackLocked:YES];

    NSButton* loop = [controller valueForKey:@"songLoopButton"];
    NSButton* queue = [controller valueForKey:@"queueButton"];
    NSPopUpButton* quantization = [controller valueForKey:
        @"launchQuantizationPopup"];
    __block NSInteger loopChanges = 0;
    __block BOOL latestLoop = NO;
    __block NSInteger genericChanges = 0;
    controller.loopChangeHandler = ^(BOOL enabled) {
        ++loopChanges;
        latestLoop = enabled;
    };
    controller.changeHandler = ^(NSString*) { ++genericChanges; };

    check(queue.enabled && quantization.enabled && loop.enabled,
        "queue and Song loop should be available whenever Song Transport and the host clock are running");
    [loop performClick:nil];
    check([controller songArrangement].loop && latestLoop
            && loopChanges == 1 && genericChanges == 0
            && queue.enabled && quantization.enabled,
        "enabling Song loop live should use its real-time callback without disabling queue");
    [loop performClick:nil];
    check(![controller songArrangement].loop && !latestLoop
            && loopChanges == 2 && genericChanges == 0
            && queue.enabled && quantization.enabled,
        "disabling Song loop live should keep queue available in non-looping mode");

    [controller setPlaybackLocked:NO];
    check(!queue.enabled && !quantization.enabled && loop.enabled,
        "queue should stop only with the host clock while LOOP SONG remains editable");
    controller.loopChangeHandler = nil;
    controller.changeHandler = nil;
    controller.playbackEnabled = NO;
}

void testPatternNamesAndQueuePresentation(
    S3GTrackerSongWindowController* controller)
{
    s3g::tracker::SongArrangement arrangement;
    arrangement.name = "MENU";
    arrangement.ticksPerBeat = 4u;
    for (const char* pattern : { "A01", "A02", "A03" }) {
        s3g::tracker::SongRow row;
        row.patternId = pattern;
        row.durationTicks = 8u;
        arrangement.rows.push_back(row);
    }
    [controller setSongArrangement:arrangement];
    [controller setAvailablePatternIds:@[ @"A01", @"A02", @"A03" ]
        patternNames:@[ @"INTRO", @"VERSE", @"FILL" ]
        activePatternId:@"A01"];
    [controller showWindow:nil];
    [controller.window.contentView layoutSubtreeIfNeeded];

    NSTableView* table = [controller valueForKey:@"tableView"];
    NSView* patternCell = [table viewAtColumn:1 row:0
        makeIfNecessary:YES];
    NSPopUpButton* popup = firstPopup(patternCell);
    check(popup.numberOfItems == 3u
            && [[popup valueForKey:@"s3gUsesCanvasMenu"] boolValue]
            && near(NSHeight(popup.frame), 15.0)
            && [[popup itemAtIndex:0].title isEqualToString:@"A01 · INTRO"]
            && [[popup itemAtIndex:1].title isEqualToString:@"A02 · VERSE"]
            && [[[popup itemAtIndex:1] representedObject]
                isEqualToString:@"A02"],
        "Song pattern menus should use the compact in-canvas face, show names, and retain stable IDs");

    [controller setPlaybackRow:0u valid:YES];
    [controller setPendingPlaybackRow:2u valid:YES quantization:1u];
    NSTextField* queueStatus = [controller valueForKey:@"queueStatusLabel"];
    NSTableRowView* pending = [table rowViewAtRow:2 makeIfNecessary:YES];
    check([queueStatus.stringValue
                isEqualToString:@"QUEUED ROW 03 · NEXT BEAT"]
            && [[pending valueForKey:@"playbackPending"] boolValue]
            && [pending.accessibilityValue isEqualToString:@"Queued"],
        "pending Song row and its actual launch boundary should be visible");
    [controller setPendingPlaybackRow:0u valid:NO quantization:0u];
    check([queueStatus.stringValue isEqualToString:@"QUEUE —"],
        "Song queue status should clear after the pending launch is consumed");
}

void testMutedLaneRedrawDoesNotThrow(
    S3GTrackerSongWindowController* controller)
{
    s3g::tracker::SongArrangement input;
    input.name = "MUTED";
    input.ticksPerBeat = 4u;
    s3g::tracker::SongRow row;
    row.patternId = "A01";
    row.durationTicks = 16u;
    row.repeats = 1u;
    row.mutedTracks = 1u;
    input.rows.push_back(row);

    [controller setSongArrangement:input];
    [controller showWindow:nil];
    [controller.window.contentView layoutSubtreeIfNeeded];
    [controller.window displayIfNeeded];
    [controller setPlaybackLocked:YES];
    [controller.window.contentView layoutSubtreeIfNeeded];
    [controller.window displayIfNeeded];
    [controller setPlaybackLocked:NO];

    Class muteButtonClass = NSClassFromString(@"S3GTrackerSongMuteButton");
    check(muteButtonClass != Nil,
        "Song mute button class should be available to the render test");
    if (muteButtonClass == Nil) return;
    NSButton* button = [[muteButtonClass alloc]
        initWithFrame:NSMakeRect(0.0, 0.0, 30.0, 25.0)];
    button.title = @"1";
    button.buttonType = NSButtonTypeToggle;
    button.bordered = NO;
    button.state = NSControlStateValueOn;
    NSBitmapImageRep* bitmap = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:nullptr pixelsWide:30 pixelsHigh:25
        bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
        colorSpaceName:NSCalibratedRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
    NSGraphicsContext* context = [NSGraphicsContext
        graphicsContextWithBitmapImageRep:bitmap];
    [NSGraphicsContext saveGraphicsState];
    NSGraphicsContext.currentContext = context;
    [button drawRect:button.bounds];
    [NSGraphicsContext restoreGraphicsState];
}

void testMuteMatrixMatchesEachPatternLaneCount(
    S3GTrackerSongWindowController* controller)
{
    [controller setAvailablePatternIds:@[ @"A01", @"A02" ]
        patternNames:@[ @"FOUR LANES", @"SEVEN LANES" ]
        patternLengths:@[ @16, @16 ]
        patternLaneCounts:@[ @4, @7 ]
        activePatternId:@"A01"];
    s3g::tracker::SongArrangement arrangement;
    arrangement.name = "LANE AVAILABILITY";
    arrangement.ticksPerBeat = 4u;
    s3g::tracker::SongRow row;
    row.patternId = "A01";
    row.durationTicks = 16u;
    row.repeats = 1u;
    row.mutedTracks = (1u << 1u) | (1u << 6u);
    arrangement.rows.push_back(row);
    [controller setSongArrangement:arrangement];
    [controller showWindow:nil];
    [controller.window.contentView layoutSubtreeIfNeeded];
    NSTableView* table = [controller valueForKey:@"tableView"];
    NSView* muteCell = [table viewAtColumn:7 row:0 makeIfNecessary:YES];
    NSButton* lane4 = muteButtonForLane(muteCell, 0, 3);
    NSButton* lane5 = muteButtonForLane(muteCell, 0, 4);
    NSButton* lane7 = muteButtonForLane(muteCell, 0, 6);
    check(lane4.enabled && !lane5.enabled && !lane7.enabled
            && lane7.state == NSControlStateValueOff
            && [controller songArrangement].rows[0u].mutedTracks
                == (1u << 1u),
        "Song mutes should gray lanes beyond the selected pattern and discard unavailable mute bits");

    lane5.state = NSControlStateValueOn;
    [lane5 sendAction:lane5.action to:lane5.target];
    check([controller songArrangement].rows[0u].mutedTracks == (1u << 1u),
        "the mute action must reject an unavailable lane even when invoked programmatically");

    NSView* patternCell = [table viewAtColumn:1 row:0 makeIfNecessary:YES];
    NSPopUpButton* pattern = popupWithAction(patternCell,
        NSSelectorFromString(@"patternPopupChanged:"));
    [pattern selectItemAtIndex:[pattern indexOfItemWithRepresentedObject:@"A02"]];
    [pattern sendAction:pattern.action to:pattern.target];
    muteCell = [table viewAtColumn:7 row:0 makeIfNecessary:YES];
    lane7 = muteButtonForLane(muteCell, 0, 6);
    NSButton* lane8 = muteButtonForLane(muteCell, 0, 7);
    check(lane7.enabled && !lane8.enabled,
        "changing a Song row pattern should immediately refresh that row's available mutes");
}

void testSavedMutesSurvivePatternCatalogRestore()
{
    S3GTrackerSongWindowController* controller =
        [[S3GTrackerSongWindowController alloc] init];
    s3g::tracker::SongArrangement arrangement;
    arrangement.name = "RESTORED MUTES";
    s3g::tracker::SongRow row;
    row.patternId = "B02";
    row.durationTicks = 16u;
    row.mutedTracks = (1u << 1u) | (1u << 6u);
    arrangement.rows.push_back(row);

    // A host can restore the document before the Song panel has received the
    // new pattern catalog. Unknown is not the same as a zero-lane pattern.
    [controller setSongArrangement:arrangement];
    [controller setAvailablePatternIds:@[ @"A01", @"B02" ]
        patternNames:@[ @"INTRO", @"VERSE" ]
        patternLengths:@[ @16, @16 ]
        patternLaneCounts:@[ @4, @7 ]
        activePatternId:@"A01"];
    check([controller songArrangement].rows[0u].mutedTracks
            == ((1u << 1u) | (1u << 6u)),
        "saved Song mute bits should survive restore before pattern metadata is available");
    [controller close];
}

void testProjectFileMenuDispatchesCompleteProjectActions(
    S3GTrackerSongWindowController* controller)
{
    __block int saves = 0;
    __block int loads = 0;
    controller.saveProjectHandler = ^{ ++saves; };
    controller.loadProjectHandler = ^{ ++loads; };
    NSPopUpButton* menu = [controller valueForKey:@"projectFileMenu"];
    check(menu.numberOfItems == 3u
            && [[menu itemAtIndex:1].title
                isEqualToString:@"SAVE SONG + PATTERNS…"]
            && [[menu itemAtIndex:2].title
                isEqualToString:@"LOAD SONG + PATTERNS…"],
        "Song page should expose complete-project save/load choices");
    [menu selectItemAtIndex:1u];
    [menu sendAction:menu.action to:menu.target];
    [menu selectItemAtIndex:2u];
    [menu sendAction:menu.action to:menu.target];
    check(saves == 1 && loads == 1 && menu.indexOfSelectedItem == 0,
        "Song file choices should dispatch and reset their pull-down menu");
}

void testNamedWarpMenuAndRoundTrip(
    S3GTrackerSongWindowController* controller)
{
    s3g::tracker::TimingWarpStack stack;
    check(stack.append(
            s3g::tracker::TimingWarpTransform::exponential(2.0)).added(),
        "Song window warp fixture should compile");
    s3g::tracker::TimingWarpLibrary library;
    check(library.store(6u, "Fast Start", 4u, stack),
        "Song window warp fixture should occupy slot 07");
    [controller setTimingWarpLibrary:library];

    s3g::tracker::SongArrangement arrangement;
    arrangement.name = "WARPS";
    arrangement.ticksPerBeat = 4u;
    s3g::tracker::SongRow row;
    row.patternId = "A01";
    row.durationTicks = 16u;
    row.timingWarpLibraryIndex = 6u;
    arrangement.rows.push_back(row);
    [controller setSongArrangement:arrangement];
    [controller showWindow:nil];
    [controller.window.contentView layoutSubtreeIfNeeded];

    NSTableView* table = [controller valueForKey:@"tableView"];
    NSView* warpCell = [table viewAtColumn:2 row:0 makeIfNecessary:YES];
    NSPopUpButton* popup = firstPopup(warpCell);
    const auto output = [controller songArrangement];
    check(popup.numberOfItems == 2u
            && [[popup valueForKey:@"s3gUsesCanvasMenu"] boolValue]
            && [[[popup itemAtIndex:0] title] isEqualToString:@"OFF"]
            && [[[popup itemAtIndex:1] title]
                isEqualToString:@"07 · Fast Start"]
            && popup.indexOfSelectedItem == 1
            && output.rows.size() == 1u
            && output.rows[0u].timingWarpLibraryIndex
                == std::optional<std::size_t>(6u),
        "Song WARP should show named saved slots and preserve its selection");
}

void testSelectedRowDuplicateAndMove(
    S3GTrackerSongWindowController* controller)
{
    [controller setAvailablePatternIds:@[ @"A01", @"A02", @"A03" ]
        patternNames:@[ @"ONE", @"TWO", @"THREE" ]
        patternLengths:@[ @16, @12, @8 ] activePatternId:@"A01"];
    s3g::tracker::SongArrangement arrangement;
    arrangement.name = "ROW EDIT";
    for (const char* pattern : { "A01", "A02", "A03" }) {
        s3g::tracker::SongRow row;
        row.patternId = pattern;
        arrangement.rows.push_back(row);
    }
    auto& source = arrangement.rows[1u];
    source.durationTicks = 12u;
    source.repeats = 3u;
    source.swing = 0.625;
    source.mutedTracks = 0x15u;
    source.patternLoop = s3g::tracker::SongPatternLoop { 2u, 8u };
    [controller setSongArrangement:arrangement];
    [controller showWindow:nil];
    [controller.window.contentView layoutSubtreeIfNeeded];
    NSTableView* table = [controller valueForKey:@"tableView"];
    NSButton* duplicate = [controller valueForKey:@"duplicateButton"];
    NSButton* moveUp = [controller valueForKey:@"moveUpButton"];
    NSButton* moveDown = [controller valueForKey:@"moveDownButton"];
    [table selectRowIndexes:[NSIndexSet indexSetWithIndex:1u]
        byExtendingSelection:NO];
    [duplicate sendAction:duplicate.action to:duplicate.target];
    auto output = [controller songArrangement];
    const auto& copy = output.rows[2u];
    check(output.rows.size() == 4u && table.selectedRow == 2
            && copy.patternId == source.patternId
            && copy.durationTicks == source.durationTicks
            && copy.repeats == source.repeats
            && copy.swing == source.swing
            && copy.mutedTracks == source.mutedTracks
            && copy.patternLoop && source.patternLoop
            && copy.patternLoop->startRow
                == source.patternLoop->startRow
            && copy.patternLoop->endRow
                == source.patternLoop->endRow,
        "DUP should insert an exact independent copy after the selected Song row and select it");

    [table selectRowIndexes:[NSIndexSet indexSetWithIndex:0u]
        byExtendingSelection:NO];
    check(!moveUp.enabled && moveDown.enabled,
        "Song row movement should disable only the unavailable boundary direction");
    [moveDown sendAction:moveDown.action to:moveDown.target];
    output = [controller songArrangement];
    check(table.selectedRow == 1
            && output.rows[0u].patternId == "A02"
            && output.rows[1u].patternId == "A01"
            && moveUp.enabled,
        "the down control should reorder the selected Song row and retain selection");
    [moveUp sendAction:moveUp.action to:moveUp.target];
    output = [controller songArrangement];
    check(table.selectedRow == 0
            && output.rows[0u].patternId == "A01"
            && output.rows[1u].patternId == "A02",
        "the up control should reverse a row move without changing row contents");
}

void testPatternLoopColumnAndRoundTrip(
    S3GTrackerSongWindowController* controller)
{
    [controller setAvailablePatternIds:@[ @"A01", @"A02" ]
        patternNames:@[ @"SIXTEEN", @"EIGHT" ]
        patternLengths:@[ @16, @8 ] activePatternId:@"A01"];
    s3g::tracker::SongArrangement arrangement;
    arrangement.name = "SUB PATTERNS";
    arrangement.ticksPerBeat = 4u;
    s3g::tracker::SongRow off;
    off.patternId = "A01";
    off.durationTicks = 16u;
    arrangement.rows.push_back(off);
    s3g::tracker::SongRow looped = off;
    looped.patternLoop = s3g::tracker::SongPatternLoop { 4u, 8u };
    arrangement.rows.push_back(looped);

    [controller setSongArrangement:arrangement];
    [controller showWindow:nil];
    [controller.window.contentView layoutSubtreeIfNeeded];
    NSTableView* table = [controller valueForKey:@"tableView"];
    const auto output = [controller songArrangement];
    NSView* offCell = [table viewAtColumn:3 row:0 makeIfNecessary:YES];
    NSView* loopCell = [table viewAtColumn:3 row:1 makeIfNecessary:YES];
    NSPopUpButton* offIn = popupWithAction(
        offCell, NSSelectorFromString(@"loopStartPopupChanged:"));
    NSPopUpButton* offOut = popupWithAction(
        offCell, NSSelectorFromString(@"loopEndPopupChanged:"));
    NSPopUpButton* loopIn = popupWithAction(
        loopCell, NSSelectorFromString(@"loopStartPopupChanged:"));
    NSPopUpButton* loopOut = popupWithAction(
        loopCell, NSSelectorFromString(@"loopEndPopupChanged:"));
    NSPopUpButton* loopTicks = popupWithAction(
        [table viewAtColumn:5 row:1 makeIfNecessary:YES],
        NSSelectorFromString(@"ticksPopupChanged:"));
    check(table.tableColumns.count == 9u
            && [table.tableColumns[3u].title isEqualToString:@"LOOP IN–OUT"]
            && offIn.numberOfItems == 17u
            && [offIn.titleOfSelectedItem isEqualToString:@"OFF"]
            && !offOut.enabled
            && [offOut.titleOfSelectedItem isEqualToString:@"—"]
            && [loopIn.titleOfSelectedItem isEqualToString:@"IN 005"]
            && [loopOut.titleOfSelectedItem isEqualToString:@"OUT 008"]
            && [loopTicks.titleOfSelectedItem containsString:@"4×"]
            && output.rows.size() == 2u
            && !output.rows[0u].patternLoop
            && output.rows[1u].patternLoop
            && output.rows[1u].patternLoop->startRow == 4u
            && output.rows[1u].patternLoop->endRow == 8u,
        "Song should expose pattern-bounded OFF, Loop In, and Loop Out menus while round-tripping ranges");

    [loopIn selectItemAtIndex:[loopIn
        indexOfItemWithRepresentedObject:@9]];
    [loopIn sendAction:loopIn.action to:loopIn.target];
    loopCell = [table viewAtColumn:3 row:1 makeIfNecessary:YES];
    loopOut = popupWithAction(
        loopCell, NSSelectorFromString(@"loopEndPopupChanged:"));
    BOOL endChoicesRespectStart = loopOut.numberOfItems == 8u;
    for (NSMenuItem* item in loopOut.itemArray) {
        NSNumber* value = item.representedObject;
        endChoicesRespectStart = endChoicesRespectStart
            && [value isKindOfClass:NSNumber.class]
            && value.integerValue >= 9;
    }
    [loopOut selectItemAtIndex:[loopOut
        indexOfItemWithRepresentedObject:@12]];
    [loopOut sendAction:loopOut.action to:loopOut.target];
    const auto edited = [controller songArrangement];
    check(endChoicesRespectStart && edited.rows[1u].patternLoop
            && edited.rows[1u].patternLoop->startRow == 8u
            && edited.rows[1u].patternLoop->endRow == 12u,
        "Loop Out should offer only rows at or after Loop In and preserve inclusive playback bounds");
    loopCell = [table viewAtColumn:3 row:1 makeIfNecessary:YES];
    loopIn = popupWithAction(
        loopCell, NSSelectorFromString(@"loopStartPopupChanged:"));
    [loopIn selectItemAtIndex:0u];
    [loopIn sendAction:loopIn.action to:loopIn.target];
    check(![controller songArrangement].rows[1u].patternLoop,
        "Loop In should return the row explicitly to OFF");
}

void testMenuTimingAndScrollableSwing(
    S3GTrackerSongWindowController* controller)
{
    s3g::tracker::SongArrangement arrangement;
    arrangement.name = "TIMING MENUS";
    arrangement.ticksPerBeat = 4u;
    s3g::tracker::SongRow row;
    row.patternId = "A01";
    row.durationTicks = 8u;
    row.repeats = 1u;
    row.swing = 0.56;
    arrangement.rows.push_back(row);
    [controller setSongArrangement:arrangement];
    [controller showWindow:nil];
    [controller.window.contentView layoutSubtreeIfNeeded];
    NSTableView* table = [controller valueForKey:@"tableView"];
    NSPopUpButton* repeats = popupWithAction(
        [table viewAtColumn:4 row:0 makeIfNecessary:YES],
        NSSelectorFromString(@"repeatsPopupChanged:"));
    NSPopUpButton* ticks = popupWithAction(
        [table viewAtColumn:5 row:0 makeIfNecessary:YES],
        NSSelectorFromString(@"ticksPopupChanged:"));
    NSControl* swing = firstSwingControl(
        [table viewAtColumn:6 row:0 makeIfNecessary:YES]);
    check(repeats.numberOfItems == 64u
            && repeats.indexOfSelectedItem == 0
            && [[[ticks itemAtIndex:0u] title]
                isEqualToString:@"FULL · 1× · 16"]
            && [[[ticks itemAtIndex:0u] representedObject]
                isEqualToNumber:@16]
            && [ticks indexOfItemWithRepresentedObject:@7] < 0
            && [ticks indexOfItemWithRepresentedObject:@8] >= 0
            && [ticks indexOfItemWithRepresentedObject:@128] >= 0
            && [ticks indexOfItemWithRepresentedObject:@192] >= 0
            && [ticks indexOfItemWithRepresentedObject:@256] >= 0
            && [ticks.titleOfSelectedItem containsString:@"50%"]
            && [[ticks itemAtIndex:[ticks
                indexOfItemWithRepresentedObject:@16]].title
                    containsString:@"FULL"]
            && [swing isKindOfClass:
                NSClassFromString(@"S3GTrackerSongSwingField")]
            && !swing.acceptsFirstResponder
            && swing.s3gHasOverride
            && near(swing.s3gSwingValue, 56.0, 0.001)
            && [swing.stringValue isEqualToString:@"56.0"],
        "REP and TICKS should be constrained menus while SWING uses a non-editing suite slider with a persistent value readout");

    const BOOL scrolled = [swing adjustByScrollDelta:1.0 modifierFlags:0u];
    const auto scrolledArrangement = [controller songArrangement];
    check(scrolled && scrolledArrangement.rows[0u].swing
            && near(*scrolledArrangement.rows[0u].swing, 0.565, 0.0001),
        "scrolling the Swing slider should change and publish its value in half-percent steps");

    const NSRect swingTrack = [swing sliderTrackRect];
    const NSPoint dragStart = [swing convertPoint:NSMakePoint(
        NSMinX(swingTrack) + NSWidth(swingTrack) * 0.2,
        NSMidY(swingTrack)) toView:nil];
    const NSPoint dragEnd = [swing convertPoint:NSMakePoint(
        NSMinX(swingTrack) + NSWidth(swingTrack) * 0.8,
        NSMidY(swingTrack)) toView:nil];
    const NSPoint dragHit = [swing convertPoint:NSMakePoint(
        NSMidX(swingTrack), NSMidY(swingTrack))
        toView:nil];
    NSView* swingHitView = [controller.window.contentView hitTest:dragHit];
    check(swingHitView == swing,
        "the embedded Song table should route slider-track events to Swing");
    __block NSInteger dragCommits = 0;
    __weak S3GTrackerSongWindowController* weakController = controller;
    controller.changeHandler = ^(NSString*) {
        ++dragCommits;
        [weakController setAvailablePatternIds:@[ @"A01" ]
            activePatternId:@"A01"];
    };
    [swing mouseDown:songMouseEvent(controller.window,
        NSEventTypeLeftMouseDown, dragStart, 0u)];
    [swing mouseDragged:songMouseEvent(controller.window,
        NSEventTypeLeftMouseDragged, dragEnd, 0u)];
    check(dragCommits == 0 && swing.superview != nil,
        "Swing drag preview should not rebuild its table cell before mouse-up");
    [swing mouseUp:songMouseEvent(controller.window,
        NSEventTypeLeftMouseUp, dragEnd, 0u)];
    controller.changeHandler = nil;
    auto draggedArrangement = [controller songArrangement];
    check(dragCommits == 1 && draggedArrangement.rows[0u].swing
            && near(*draggedArrangement.rows[0u].swing, 0.70, 0.001),
        "hosted Swing dragging should commit once after the complete gesture");
    swing = firstSwingControl(
        [table viewAtColumn:6 row:0 makeIfNecessary:YES]);
    [swing mouseDown:songMouseEvent(controller.window,
        NSEventTypeLeftMouseDown, dragEnd, NSEventModifierFlagOption)];
    check(![controller songArrangement].rows[0u].swing,
        "Option-click should clear the Song-row Swing override");
    [swing setSwingValue:62.5 hasOverride:YES];
    [swing sendAction:swing.action to:swing.target];
    [swing rightMouseDown:songMouseEvent(controller.window,
        NSEventTypeRightMouseDown, dragEnd, 0u)];
    check(![controller songArrangement].rows[0u].swing,
        "right-click should clear the Song-row Swing override");

    [repeats selectItemAtIndex:[repeats
        indexOfItemWithRepresentedObject:@64]];
    [repeats sendAction:repeats.action to:repeats.target];
    [ticks selectItemAtIndex:[ticks indexOfItemWithRepresentedObject:@16]];
    [ticks sendAction:ticks.action to:ticks.target];
    [swing setSwingValue:62.5 hasOverride:YES];
    [swing sendAction:swing.action to:swing.target];
    auto output = [controller songArrangement];
    check(output.rows[0u].repeats == 64u
            && output.rows[0u].durationTicks == 16u
            && output.rows[0u].swing == 0.625,
        "Song timing menus and swing adjustment should publish their exact selected values");

    [swing resetToBase];
    output = [controller songArrangement];
    check(!output.rows[0u].swing,
        "the Swing slider should retain an explicit reset-to-base action");

    [controller setAvailablePatternIds:@[ @"A01" ]
        patternNames:@[ @"ODD LENGTH" ] patternLengths:@[ @7 ]
        activePatternId:@"A01"];
    ticks = popupWithAction(
        [table viewAtColumn:5 row:0 makeIfNecessary:YES],
        NSSelectorFromString(@"ticksPopupChanged:"));
    check([[[ticks itemAtIndex:0u] title]
                isEqualToString:@"FULL · 1× · 7"]
            && [[[ticks itemAtIndex:0u] representedObject]
                isEqualToNumber:@7],
        "TICKS should always include a contextual full-span choice even when the pattern length is not a fixed interval");
    [ticks selectItemAtIndex:0u];
    [ticks sendAction:ticks.action to:ticks.target];
    check([controller songArrangement].rows[0u].durationTicks == 7u,
        "the contextual FULL / 1x choice should set the row to its complete available pattern span");
}

} // namespace

int main()
{
    @autoreleasepool {
        [NSApplication sharedApplication];
        S3GTrackerSongWindowController* controller =
            [[S3GTrackerSongWindowController alloc] init];
        testSharedToolboxLayout(controller);
        testCanvasMenuRowsHitAcrossTheirFullWidth(controller);
        testEmptyArrangementRemainsEmpty(controller);
        testHostTempoAndOptionalSwing(controller);
        testPlaybackPresentationDoesNotMutateArrangement(controller);
        testArrangementEditingRemainsAvailableDuringPlayback(controller);
        testLoopAndQueueFollowRunningHostClock(controller);
        testPatternNamesAndQueuePresentation(controller);
        testNamedWarpMenuAndRoundTrip(controller);
        testSelectedRowDuplicateAndMove(controller);
        testPatternLoopColumnAndRoundTrip(controller);
        testMenuTimingAndScrollableSwing(controller);
        testProjectFileMenuDispatchesCompleteProjectActions(controller);
        testMutedLaneRedrawDoesNotThrow(controller);
        testMuteMatrixMatchesEachPatternLaneCount(controller);
        testSavedMutesSurvivePatternCatalogRestore();
        [controller close];
    }
    if (failures != 0) {
        std::cerr << failures << " song window round-trip test(s) failed\n";
        return 1;
    }
    std::cout << "Song window optionality round-trip tests passed\n";
    return 0;
}
