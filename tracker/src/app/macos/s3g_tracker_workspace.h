#pragma once

#import <Cocoa/Cocoa.h>
#include "s3g/tracker/editor_state.h"

@interface S3GTrackerWorkspaceController : NSViewController

- (instancetype)initWithState:(s3g::tracker::app::TrackerViewState*)state
    callbacks:(s3g::tracker::app::WorkspaceCallbacks*)callbacks;

- (void)reloadModel;
- (void)refreshPlaybackDisplay;
- (void)appendConsoleMessage:(const std::string&)message error:(BOOL)isError;
- (void)showGeometryWindow:(id)sender;
- (void)showWarpWindow:(id)sender;
- (void)showMixerPage:(id)sender;
- (void)showTrackerPage:(id)sender;
- (NSView*)geometryPageView;
- (NSView*)burstPageView;
- (NSView*)reshapePageView;
- (NSView*)phrasePageView;
- (NSView*)assemblePageView;
- (NSView*)warpPageView;
- (NSView*)consolePageView;
- (void)focusConsole;
- (void)focusTracker;
- (void)zoomTrackerIn;
- (void)zoomTrackerOut;
- (void)resetTrackerZoom;
- (NSView*)mainPageView;
- (void)suspendMainPage;

@end
