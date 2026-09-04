#pragma once

#import <Cocoa/Cocoa.h>

#include "s3g_tracker_workspace.h"

// Implemented by the Phrase page root so both an embedded CLAP page and a
// detached AppKit window can forward printable tracker entry reliably.
@protocol S3GTrackerPhraseKeyHandling <NSObject>
- (BOOL)s3gHandlePhraseKeyEquivalent:(NSEvent*)event;
@end

@interface S3GTrackerPhraseView : NSViewController

- (instancetype)initWithState:
    (s3g::tracker::app::TrackerViewState*)state
    callbacks:(s3g::tracker::app::WorkspaceCallbacks*)callbacks;
- (void)reloadModel;
- (BOOL)captureTrack:(std::size_t)track firstRow:(std::size_t)firstRow
    lastRow:(std::size_t)lastRow;
- (BOOL)placeAtTrack:(std::size_t)track row:(std::size_t)row
    merge:(BOOL)merge;

@end
