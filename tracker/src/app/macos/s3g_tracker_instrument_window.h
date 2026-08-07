#pragma once

#import <Cocoa/Cocoa.h>

namespace s3g::tracker::app {
struct TrackerViewState;
struct WorkspaceCallbacks;
}

NS_ASSUME_NONNULL_BEGIN

// A retained, nonmodal editor for active Membrane Kick instances. The engine
// keeps bounded capacity for additional instances, but the controller shows
// only instances added to the song index and does not own the rack.
@interface S3GTrackerInstrumentWindowController : NSWindowController

- (instancetype)initWithState:
        (s3g::tracker::app::TrackerViewState*)state
    callbacks:(s3g::tracker::app::WorkspaceCallbacks*)callbacks;

- (void)reloadModel;
- (void)showWindow:(nullable id)sender;

@end

NS_ASSUME_NONNULL_END
