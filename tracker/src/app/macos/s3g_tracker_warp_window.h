#pragma once

#import <Cocoa/Cocoa.h>

namespace s3g::tracker::app {
struct TrackerViewState;
struct WorkspaceCallbacks;
}

@interface S3GTrackerWarpWindowController : NSWindowController

- (instancetype)initWithState:
    (s3g::tracker::app::TrackerViewState*)state
    callbacks:(s3g::tracker::app::WorkspaceCallbacks*)callbacks;
- (void)reloadModel;
- (void)refreshPlaybackDisplay;

@end
