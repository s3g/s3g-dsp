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

void testOptionalOverridesRemainOptional(
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
        "fully inherited BPM/swing should remain nullopt");
    check(output.rows[1u].bpm == 143.0
            && !output.rows[1u].swing.has_value(),
        "BPM-only override should not manufacture swing");
    check(!output.rows[2u].bpm.has_value()
            && output.rows[2u].swing == 0.625,
        "swing-only override should not manufacture BPM");
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

} // namespace

int main()
{
    @autoreleasepool {
        [NSApplication sharedApplication];
        S3GTrackerSongWindowController* controller =
            [[S3GTrackerSongWindowController alloc] init];
        testEmptyArrangementRemainsEmpty(controller);
        testOptionalOverridesRemainOptional(controller);
        testPlaybackPresentationDoesNotMutateArrangement(controller);
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
