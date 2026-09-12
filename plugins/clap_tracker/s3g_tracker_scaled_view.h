#pragma once

#include "s3g_clap_vstgui.h"
#import "s3g_tracker_controls.h"
#import <Cocoa/Cocoa.h>

// Temporary native host for the Mac drawing pilot. Keep every original view
// in its original coordinates: AppKit then scales drawing, hit testing, field
// editors and nested scroll views together. The grid's own magnification is
// independent. This is not a second VSTGUI CFrame or a rasterized screenshot.
@interface S3GTrackerClapScaledView : NSScrollView
@property(nonatomic, strong) NSView* content;
@property(nonatomic) NSSize nativeSize;
- (instancetype)initWithContent:(NSView*)content nativeSize:(NSSize)size;
@end

@implementation S3GTrackerClapScaledView

- (instancetype)initWithContent:(NSView*)content nativeSize:(NSSize)size
{
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, size.width, size.height)];
    if (!self) return nil;
    self.nativeSize = size;
    self.content = content;
    content.identifier = S3GTrackerScaledWorkspaceIdentifier;
    // CLAP set_size owns this frame. A host may resize its parent before OR
    // after that call; an autoresizing mask would apply the delta twice.
    self.autoresizingMask = NSViewNotSizable;
    self.borderType = NSNoBorder;
    self.drawsBackground = NO;
    self.hasHorizontalScroller = NO;
    self.hasVerticalScroller = NO;
    self.horizontalScrollElasticity = NSScrollElasticityNone;
    self.verticalScrollElasticity = NSScrollElasticityNone;
    self.allowsMagnification = NO; // Programmatic scaling only; inner grid owns pinch zoom.
    self.minMagnification = s3g::portable_gui::foundation::kMinimumEditorScale;
    self.maxMagnification = s3g::portable_gui::foundation::kMaximumEditorScale;
    content.autoresizingMask = NSViewNotSizable;
    self.documentView = content;
    [self setFrameSize:size];
    return self;
}

- (void)setFrameSize:(NSSize)size
{
    [super setFrameSize:size];
    if (!self.content || self.nativeSize.width <= 0.0
        || self.nativeSize.height <= 0.0) return;
    const double scale = std::clamp(std::min(
        size.width / self.nativeSize.width,
        size.height / self.nativeSize.height),
        s3g::portable_gui::foundation::kMinimumEditorScale,
        s3g::portable_gui::foundation::kMaximumEditorScale);
    // NSScrollView's native magnification isolates the logical Auto Layout
    // tree; changing the page view's own frame/bounds transform does not.
    [self setMagnification:scale centeredAtPoint:NSZeroPoint];
    [self.contentView scrollToPoint:NSZeroPoint];
    [self reflectScrolledClipView:self.contentView];
    self.content.needsDisplay = YES;
}

@end
