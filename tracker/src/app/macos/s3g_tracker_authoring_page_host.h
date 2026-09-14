#pragma once
#include "../../editor/s3g_tracker_authoring_page.h"
#import "s3g_tracker_assemble_view.h"
#import "s3g_tracker_phrase_view.h"
#include "s3g_tracker_vstgui_pilot.h"
@interface S3GTrackerAuthoringPageHost
    : NSView <S3GTrackerTextInputOwner, S3GTrackerPhraseKeyHandling,
              S3GTrackerAssembleKeyHandling>
- (instancetype)initWithState:(s3g::tracker::app::TrackerViewState *)state
                    callbacks:(s3g::tracker::app::WorkspaceCallbacks *)callbacks
                         kind:(s3g::tracker::editor::AuthoringPage)kind;
@property(nonatomic, readonly) s3g::tracker::editor::AuthoringPageView *page;
- (void)reloadModel;
- (void)refreshPlaybackDisplay;
- (void)suspendEditing;
- (NSRect)authoringControlRect:(NSString *)identifier;
- (NSRect)authoringPopupItemRect:(NSUInteger)index;
@end
@interface S3GTrackerPortablePhraseController : NSViewController
- (instancetype)initWithState:(s3g::tracker::app::TrackerViewState *)state
                    callbacks:
                        (s3g::tracker::app::WorkspaceCallbacks *)callbacks;
- (void)reloadModel;
- (void)refreshPlaybackDisplay;
- (BOOL)captureTrack:(std::size_t)track
            firstRow:(std::size_t)first
             lastRow:(std::size_t)last;
- (BOOL)placeAtTrack:(std::size_t)track row:(std::size_t)row merge:(BOOL)merge;
@end
@interface S3GTrackerPortableAssembleController : NSViewController
- (instancetype)initWithState:(s3g::tracker::app::TrackerViewState *)state
                    callbacks:
                        (s3g::tracker::app::WorkspaceCallbacks *)callbacks;
- (void)reloadModel;
- (void)refreshPlaybackDisplay;
@end
@interface S3GTrackerPortableReshapeController : NSWindowController
@property(nonatomic, readonly) NSView *pageView;
- (instancetype)initWithState:(s3g::tracker::app::TrackerViewState *)state
                    callbacks:
                        (s3g::tracker::app::WorkspaceCallbacks *)callbacks;
- (void)reloadModel;
- (void)refreshPlaybackDisplay;
- (void)clearPreview;
@end
