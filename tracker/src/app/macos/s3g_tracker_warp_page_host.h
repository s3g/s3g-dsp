#pragma once
#include "../../editor/s3g_tracker_warp_page.h"
#import <Cocoa/Cocoa.h>
#include "s3g_tracker_vstgui_pilot.h"

// The platform boundary only owns/reparents the CFrame and resolves Mac fonts.
// Warps' presenter, drawing, controls, menus and text editing are portable C++.
@interface S3GTrackerWarpPageHost : NSView <S3GTrackerTextInputOwner>
- (instancetype)initWithState:(s3g::tracker::app::TrackerViewState*)state
                    callbacks:(s3g::tracker::app::WorkspaceCallbacks*)callbacks;
- (s3g::tracker::editor::WarpPageView*)page;
- (void)refreshPlaybackDisplay;
- (NSRect)warpControlRect:(NSString*)identifier;
@end
