#import <Cocoa/Cocoa.h>

#import "s3g_song_window.h"

#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
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
        "playback highlights and edit locking must remain presentation-only");
    [controller setPlaybackLocked:NO];
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
            && [[popup itemAtIndex:0].title isEqualToString:@"A01 · INTRO"]
            && [[popup itemAtIndex:1].title isEqualToString:@"A02 · VERSE"]
            && [[[popup itemAtIndex:1] representedObject]
                isEqualToString:@"A02"],
        "Song pattern menus should show names while retaining stable IDs");

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
            && [[[popup itemAtIndex:0] title] isEqualToString:@"OFF"]
            && [[[popup itemAtIndex:1] title]
                isEqualToString:@"07 · Fast Start"]
            && popup.indexOfSelectedItem == 1
            && output.rows.size() == 1u
            && output.rows[0u].timingWarpLibraryIndex
                == std::optional<std::size_t>(6u),
        "Song WARP should show named saved slots and preserve its selection");
}

} // namespace

int main()
{
    @autoreleasepool {
        [NSApplication sharedApplication];
        S3GTrackerSongWindowController* controller =
            [[S3GTrackerSongWindowController alloc] init];
        testEmptyArrangementRemainsEmpty(controller);
        testHostTempoAndOptionalSwing(controller);
        testPlaybackPresentationDoesNotMutateArrangement(controller);
        testPatternNamesAndQueuePresentation(controller);
        testNamedWarpMenuAndRoundTrip(controller);
        testProjectFileMenuDispatchesCompleteProjectActions(controller);
        testMutedLaneRedrawDoesNotThrow(controller);
        [controller close];
    }
    if (failures != 0) {
        std::cerr << failures << " song window round-trip test(s) failed\n";
        return 1;
    }
    std::cout << "Song window optionality round-trip tests passed\n";
    return 0;
}
