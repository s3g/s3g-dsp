#pragma once
#include "../../editor/s3g_tracker_shell_view.h"
#import <Cocoa/Cocoa.h>
// Native lifetime/input adapter only; header paint and hit testing are C++.
@interface S3GTrackerShellHost : NSView
- (instancetype)initWithModel:(s3g::tracker::editor::ShellController *)model
                     services:(s3g::tracker::editor::ShellServices)services;
- (void)refresh;
- (void)suspend;
- (s3g::tracker::editor::ShellView *)shellView;
- (NSRect)shellControlRect:(NSString *)identifier;
- (NSString *)shellStatusText:(NSString *)identifier;
@end
