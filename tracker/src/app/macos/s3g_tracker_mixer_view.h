#pragma once

#import <Cocoa/Cocoa.h>

#include "s3g_tracker_workspace.h"

typedef void (^S3GTrackerMixerActionHandler)(void);
typedef void (^S3GTrackerMixerMasterGainHandler)(float normalized);

// Renoise-inspired performance mixer over the shared TrackerViewState.
// Track strips deliberately expose event-stage controls until the internal
// graph has independent post-DSP lane buses. MAIN OUT is a real audio fader.
@interface S3GTrackerMixerView : NSView

- (instancetype)initWithState:
    (s3g::tracker::app::TrackerViewState*)state;
- (CGFloat)preferredContentWidth;
- (void)reloadModel;

@property(nonatomic, assign)
    s3g::tracker::app::TrackerViewState* trackerState;
@property(nonatomic, copy) S3GTrackerMixerActionHandler patternChangeHandler;
@property(nonatomic, copy) S3GTrackerMixerActionHandler selectionChangeHandler;
@property(nonatomic, copy) S3GTrackerMixerActionHandler playbackHandler;
@property(nonatomic, copy) S3GTrackerMixerActionHandler focusConsoleHandler;
@property(nonatomic, copy)
    S3GTrackerMixerMasterGainHandler masterGainChangeHandler;

@end
