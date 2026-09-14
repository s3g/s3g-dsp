#pragma once
#include "../../editor/s3g_tracker_song_page.h"
#import <Cocoa/Cocoa.h>

// Mac window/font boundary only; arrangement authoring and paint live in C++.
@interface S3GTrackerSongPageHost : NSView
- (s3g::tracker::editor::SongPageView*)page;
@end
