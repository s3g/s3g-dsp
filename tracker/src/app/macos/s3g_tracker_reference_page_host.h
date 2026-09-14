#pragma once
#include "../../editor/s3g_tracker_reference_page.h"
#include "s3g_tracker_vstgui_pilot.h"
#import <Cocoa/Cocoa.h>
@interface S3GTrackerReferencePageHost : NSView <S3GTrackerTextInputOwner>
- (instancetype)initWithFrame:(NSRect)rect
                      console:(std::shared_ptr<s3g::tracker::editor::ConsoleModel>)console
                     services:(s3g::tracker::editor::ReferencePageServices)services;
- (s3g::tracker::editor::ReferencePageView*)page;
- (void)refresh;
@end
