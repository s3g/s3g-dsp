#pragma once

#import <Cocoa/Cocoa.h>

#include "s3g_tracker_workspace.h"

@protocol S3GTrackerAssembleKeyHandling <NSObject>
- (BOOL)s3gHandleAssembleKeyEquivalent:(NSEvent *)event;
@end

@interface S3GTrackerAssembleView : NSViewController

- (instancetype)initWithState:(s3g::tracker::app::TrackerViewState *)state
                    callbacks:
                        (s3g::tracker::app::WorkspaceCallbacks *)callbacks;
- (void)reloadModel;
- (void)refreshPlaybackDisplay;

@end
