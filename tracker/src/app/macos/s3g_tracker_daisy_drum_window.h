#pragma once

#import <Cocoa/Cocoa.h>

namespace s3g::tracker::app {
struct TrackerViewState;
struct WorkspaceCallbacks;
}

NS_ASSUME_NONNULL_BEGIN

// Retained editor shared by the five DaisySP drum families. It follows the
// selected indexed rack node and presents that model's real parameter set.
@interface S3GTrackerDaisyDrumWindowController : NSWindowController
- (instancetype)initWithState:(s3g::tracker::app::TrackerViewState*)state
    callbacks:(s3g::tracker::app::WorkspaceCallbacks*)callbacks;
- (void)reloadModel;
@end

NS_ASSUME_NONNULL_END
