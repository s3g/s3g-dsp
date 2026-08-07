#pragma once

#import <Cocoa/Cocoa.h>

namespace s3g::tracker::app {
struct TrackerViewState;
struct WorkspaceCallbacks;
}

@interface S3GTrackerSamplerWindowController : NSWindowController

- (instancetype)initWithState:
    (s3g::tracker::app::TrackerViewState*)state
    callbacks:(s3g::tracker::app::WorkspaceCallbacks*)callbacks;
- (void)reloadModel;
/// Rehydrate decoded PCM/analysis from project-persisted file paths without
/// replacing the saved slice definitions.
- (void)reloadPersistedAssets;

@end
