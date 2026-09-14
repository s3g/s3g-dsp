#pragma once
#include "../../editor/s3g_tracker_main_page.h"
#include "s3g_tracker_vstgui_pilot.h"
#import <Cocoa/Cocoa.h>

// Platform boundary only: ownership/attachment, exact Mac font metrics.
// All main-page drawing, controls, events and editing live in portable C++.
@interface S3GTrackerMainPageHost : NSView <S3GTrackerTextInputOwner>
- (instancetype)initWithState:(s3g::tracker::app::TrackerViewState*)state
                    callbacks:(s3g::tracker::app::WorkspaceCallbacks*)callbacks
                     services:(s3g::tracker::editor::MainPageServices)services;
- (s3g::tracker::editor::MainPageView*)page;
@end
