#pragma once
#include "../../editor/s3g_tracker_geometry_page.h"
#include "s3g_tracker_vstgui_pilot.h"
#import <Cocoa/Cocoa.h>
@class S3GTrackerWorkspaceController;
@interface S3GTrackerGeometryPageHost : NSView <S3GTrackerTextInputOwner>
- (instancetype)initWithState:(s3g::tracker::app::TrackerViewState *)state
                    callbacks:(s3g::tracker::app::WorkspaceCallbacks *)callbacks
                        owner:(S3GTrackerWorkspaceController *)owner
                       bursts:(BOOL)bursts;
@property(nonatomic, weak) S3GTrackerWorkspaceController *owner;
@property(nonatomic, readonly) s3g::tracker::editor::GeometryPageView *page;
@property(nonatomic, readonly) NSView *playbackOverlay;
- (void)reloadModel;
- (void)refreshPlaybackDisplay;
- (void)suspendEditing;
- (void)selectBurstSlot:(std::size_t)slot;
- (void)openPitchMapFirstRow:(std::size_t)first lastRow:(std::size_t)last;
- (void)applyPitchMapContour:(s3g::tracker::PitchContour)contour
                    firstRow:(std::size_t)first
                     lastRow:(std::size_t)last;
- (NSString *)displayedPatternId;
- (NSUInteger)displayedLaneCount;
- (NSUInteger)displayedMutedLaneCount;
- (CGFloat)ringRadiusForLane:(std::size_t)lane;
- (NSArray<NSString *> *)geometryMenuItems;
- (void)selectGeometryMode:(NSInteger)mode;
- (NSRect)burstPreviewChannelMenuBoxRect;
- (NSRect)burstPreviewHeaderButtonRect;
- (BOOL)handleToolboxClickAtPoint:(NSPoint)point;
- (void)applyGeometryMenuSelection:(NSInteger)index;
@end
