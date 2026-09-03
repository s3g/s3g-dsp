#import "s3g_song_window.h"
#import "s3g_tracker_controls.h"

#include "s3g_gui_layout.h"
#define S3G_COCOA_GUI_DRAWING_ONLY 1
#include "s3g_cocoa_gui.h"
#undef S3G_COCOA_GUI_DRAWING_ONLY

#include <algorithm>
#include <array>
#include <cmath>

namespace {

NSColor* s3gSongColor(unsigned rgb, CGFloat alpha = 1.0)
{
    return S3GTrackerColor(rgb, alpha);
}

NSFont* s3gSongFont(CGFloat size, NSFontWeight weight = NSFontWeightRegular)
{
    (void)weight;
    return s3g::clap_gui::uiFont(size);
}

NSString* const S3GSongColumnRow = @"row";
NSString* const S3GSongColumnPattern = @"pattern";
NSString* const S3GSongColumnWarp = @"warp";
NSString* const S3GSongColumnPatternLoop = @"patternLoop";
NSString* const S3GSongColumnRepeats = @"repeats";
NSString* const S3GSongColumnTicks = @"ticks";
NSString* const S3GSongColumnSwing = @"swing";
NSString* const S3GSongColumnMutes = @"mutes";
NSString* const S3GSongColumnDelete = @"delete";

constexpr std::array<NSInteger, 16u> kSongTickChoices {
    1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256,
};

NSInteger s3gClampInteger(NSInteger value, NSInteger low, NSInteger high)
{
    return std::min(high, std::max(low, value));
}

} // namespace

@interface S3GTrackerSongRow : NSObject
@property(nonatomic, copy) NSString* pattern;
@property(nonatomic) NSInteger repeats;
@property(nonatomic) NSInteger ticks;
@property(nonatomic) double swing;
@property(nonatomic) BOOL hasSwingOverride;
@property(nonatomic) NSInteger warpSlot;
@property(nonatomic) BOOL hasPatternLoop;
@property(nonatomic) NSInteger loopStart;
@property(nonatomic) NSInteger loopEnd;
@property(nonatomic, strong) NSMutableIndexSet* mutedLanes;
@end

@implementation S3GTrackerSongRow
@end

@class S3GTrackerSongWindowController;

@interface S3GTrackerSongRootView : S3GTrackerFocusReleaseView
@property(nonatomic, weak) S3GTrackerSongWindowController* layoutOwner;
@end

@implementation S3GTrackerSongRootView

- (BOOL)isFlipped { return YES; }

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    [S3GTrackerThemeColor(S3GTrackerThemeRole::Workspace) setFill];
    NSRectFill(self.bounds);
}

- (void)layout
{
    [super layout];
    if ([self.layoutOwner respondsToSelector:@selector(layoutSongInterface)])
        [self.layoutOwner performSelector:@selector(layoutSongInterface)];
}

@end

@interface S3GTrackerSongHeaderCell : NSTableHeaderCell
@end

@implementation S3GTrackerSongHeaderCell

- (void)drawWithFrame:(NSRect)cellFrame inView:(NSView*)controlView
{
    (void)controlView;
    [s3gSongColor(0x181818) setFill];
    NSRectFill(cellFrame);
    [s3gSongColor(0x3a3a3a) setStroke];
    NSBezierPath* divider = [NSBezierPath bezierPath];
    [divider moveToPoint:NSMakePoint(NSMaxX(cellFrame) - 0.5, NSMinY(cellFrame))];
    [divider lineToPoint:NSMakePoint(NSMaxX(cellFrame) - 0.5, NSMaxY(cellFrame))];
    [divider stroke];

    NSMutableParagraphStyle* paragraph = [[NSMutableParagraphStyle alloc] init];
    paragraph.alignment = NSTextAlignmentCenter;
    paragraph.lineBreakMode = NSLineBreakByClipping;
    [self.stringValue drawInRect:NSInsetRect(cellFrame, 4.0, 6.0)
        withAttributes:@{
            NSForegroundColorAttributeName: s3gSongColor(0x8e9697),
            NSFontAttributeName: s3gSongFont(10.0, NSFontWeightSemibold),
            NSParagraphStyleAttributeName: paragraph,
        }];
}

@end

@interface S3GTrackerSongRowView : NSTableRowView
@property(nonatomic) NSInteger songRow;
@property(nonatomic) BOOL playbackActive;
@property(nonatomic) BOOL playbackPending;
@end

@implementation S3GTrackerSongRowView

- (void)drawPendingIndicator
{
    [S3GTrackerThemeColor(S3GTrackerThemeRole::Warning, 0.12) setFill];
    NSRectFill(self.bounds);
    [S3GTrackerThemeColor(S3GTrackerThemeRole::Warning) setFill];
    NSRectFill(NSMakeRect(NSMinX(self.bounds), NSMinY(self.bounds),
        NSWidth(self.bounds), 2.0));
    NSRectFill(NSMakeRect(NSMinX(self.bounds), NSMaxY(self.bounds) - 2.0,
        NSWidth(self.bounds), 2.0));
    NSRectFill(NSMakeRect(NSMaxX(self.bounds) - 3.0, NSMinY(self.bounds),
        3.0, NSHeight(self.bounds)));
}

- (void)drawBackgroundInRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    NSColor* color = (self.songRow % 2 == 0)
        ? s3gSongColor(0x111111) : s3gSongColor(0x151515);
    [color setFill];
    NSRectFill(self.bounds);
    if (self.playbackActive) {
        [S3GTrackerThemeColor(S3GTrackerThemeRole::Success, 0.10) setFill];
        NSRectFill(self.bounds);
        [S3GTrackerThemeColor(S3GTrackerThemeRole::Success) setFill];
        NSRectFill(NSMakeRect(NSMinX(self.bounds), NSMinY(self.bounds),
            3.0, NSHeight(self.bounds)));
    }
    if (self.playbackPending) [self drawPendingIndicator];
}

- (void)drawSelectionInRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    [S3GTrackerThemeColor(S3GTrackerThemeRole::Selection) setFill];
    NSRectFill(self.bounds);
    [S3GTrackerThemeColor(S3GTrackerThemeRole::Focus, 0.82) setStroke];
    NSBezierPath* path = [NSBezierPath bezierPathWithRect:NSInsetRect(self.bounds, 0.5, 0.5)];
    [path setLineWidth:1.0];
    [path stroke];
    // Selection is drawn after the row background, so redraw transport marks
    // here to keep the commonly selected queued row unmistakably visible.
    if (self.playbackActive) {
        [S3GTrackerThemeColor(S3GTrackerThemeRole::Success) setFill];
        NSRectFill(NSMakeRect(NSMinX(self.bounds), NSMinY(self.bounds),
            3.0, NSHeight(self.bounds)));
    }
    if (self.playbackPending) [self drawPendingIndicator];
}

@end

@interface S3GTrackerSongMuteButton : NSButton
@end

@implementation S3GTrackerSongMuteButton

- (BOOL)becomeFirstResponder
{
    const BOOL result = [super becomeFirstResponder];
    [self setNeedsDisplay:YES];
    return result;
}

- (BOOL)resignFirstResponder
{
    const BOOL result = [super resignFirstResponder];
    [self setNeedsDisplay:YES];
    return result;
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    const BOOL available = self.enabled;
    const BOOL muted = available
        && self.state == NSControlStateValueOn;
    const NSRect box = NSInsetRect(self.bounds, 2.0, 3.0);
    [(!available ? s3gSongColor(0x151515)
        : muted ? s3gSongColor(0x303030)
                : s3gSongColor(0x242424)) setFill];
    NSRectFill(box);
    [(!available ? S3GTrackerThemeColor(S3GTrackerThemeRole::Grid, 0.55)
        : muted ? S3GTrackerThemeColor(S3GTrackerThemeRole::Danger)
                : S3GTrackerThemeColor(S3GTrackerThemeRole::Border)) setStroke];
    NSFrameRect(box);
    if (muted) {
        [S3GTrackerThemeColor(S3GTrackerThemeRole::Danger) setFill];
        NSRectFill(NSMakeRect(box.origin.x + 1.0, NSMaxY(box) - 3.0,
            box.size.width - 2.0, 2.0));
    }
    if (available && self.window.firstResponder == self) {
        [S3GTrackerThemeColor(S3GTrackerThemeRole::Focus) setStroke];
        NSFrameRect(NSInsetRect(box, 2.0, 2.0));
    }

    NSMutableParagraphStyle* paragraph = [[NSMutableParagraphStyle alloc] init];
    paragraph.alignment = NSTextAlignmentCenter;
    NSString* label = muted ? @"×"
        : (self.title != nil ? self.title : @"");
    NSColor* textColor = !available
        ? S3GTrackerThemeColor(S3GTrackerThemeRole::TextFaint, 0.55)
        : muted
        ? S3GTrackerThemeColor(S3GTrackerThemeRole::Danger)
        : S3GTrackerThemeColor(S3GTrackerThemeRole::TextSecondary);
    if (!textColor) textColor = NSColor.secondaryLabelColor;
    NSFont* font = s3gSongFont(10.0, NSFontWeightSemibold);
    if (!font) font = [NSFont systemFontOfSize:10.0
        weight:NSFontWeightSemibold];
    NSMutableDictionary<NSAttributedStringKey, id>* attributes =
        [[NSMutableDictionary alloc] initWithCapacity:3u];
    if (textColor)
        attributes[NSForegroundColorAttributeName] = textColor;
    if (font)
        attributes[NSFontAttributeName] = font;
    if (paragraph)
        attributes[NSParagraphStyleAttributeName] = paragraph;
    [label drawInRect:NSOffsetRect(self.bounds, 0.0, 5.0)
        withAttributes:attributes];
}

@end

@interface S3GTrackerSongMuteMatrixView : NSView
@end


@implementation S3GTrackerSongMuteMatrixView

- (void)layout
{
    [super layout];
    constexpr NSInteger kColumns = 16;
    constexpr NSInteger kRows = 2;
    const CGFloat slotWidth = std::max(22.0,
        (NSWidth(self.bounds) - 8.0) / kColumns);
    const CGFloat slotHeight = std::max(20.0,
        NSHeight(self.bounds) / kRows);
    NSInteger lane = 0;
    for (NSView* view in self.subviews) {
        const NSInteger row = lane / kColumns;
        const NSInteger column = lane % kColumns;
        view.frame = NSMakeRect(4.0 + column * slotWidth,
            NSHeight(self.bounds) - (row + 1) * slotHeight,
            slotWidth - 2.0, slotHeight);
        ++lane;
    }
}

@end

@interface S3GTrackerSongSwingField : NSControl
@property(nonatomic) double s3gSwingValue;
@property(nonatomic) BOOL s3gHasOverride;
@property(nonatomic) BOOL s3gDragging;
@property(nonatomic) BOOL s3gGestureChanged;
@property(nonatomic) CGFloat s3gScrollAccumulator;
- (void)setSwingValue:(double)value hasOverride:(BOOL)hasOverride;
- (void)resetToBase;
- (BOOL)adjustByScrollDelta:(CGFloat)delta
    modifierFlags:(NSEventModifierFlags)modifierFlags;
@end

@implementation S3GTrackerSongSwingField

- (NSRect)sliderTrackRect
{
    const CGFloat valueWidth = 34.0;
    return NSMakeRect(2.0, 9.0,
        std::max<CGFloat>(18.0, NSWidth(self.bounds) - valueWidth - 8.0),
        9.0);
}

- (NSRect)valueTextRect
{
    const CGFloat valueWidth = 34.0;
    return NSMakeRect(NSWidth(self.bounds) - valueWidth, 6.0,
        valueWidth - 2.0, 15.0);
}

- (void)setSwingValue:(double)value hasOverride:(BOOL)hasOverride
{
    _s3gSwingValue = std::clamp(value, 50.0, 75.0);
    _s3gHasOverride = hasOverride;
    self.stringValue = hasOverride
        ? [NSString stringWithFormat:@"%.1f", _s3gSwingValue] : @"—";
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    const NSRect track = [self sliderTrackRect];
    const NSRect value = [self valueTextRect];
    const CGFloat normalized = static_cast<CGFloat>(std::clamp(
        (self.s3gSwingValue - 50.0) / 25.0, 0.0, 1.0));
    auto style = s3g::clap_gui::softTextStyle();
    NSDictionary* valueAttrs = s3g::clap_gui::softValueAttrs();
    if (!self.enabled) {
        style.fill = s3g::clap_gui::color(0x333333);
        style.text = s3g::clap_gui::color(0x656565);
        valueAttrs = s3g::clap_gui::textAttrs(
            s3g::clap_gui::color(0x656565), 10.0);
    } else if (!self.s3gHasOverride) {
        style.fill = S3GTrackerThemeColor(S3GTrackerThemeRole::TextFaint);
    }
    s3g::clap_gui::drawSlider(@"", self.stringValue, normalized,
        NSMinY(track) - 1.0, s3g::clap_gui::softLabelAttrs(), valueAttrs,
        style, -100.0, NSMinX(track), NSMinX(value), NSWidth(track),
        NSWidth(value));
}

- (void)resetCursorRects
{
    if (!self.enabled) return;
    [self addCursorRect:[self sliderTrackRect]
        cursor:NSCursor.resizeLeftRightCursor];
}

- (BOOL)acceptsFirstResponder { return NO; }

- (BOOL)acceptsFirstMouse:(NSEvent*)event
{
    (void)event;
    return self.enabled;
}

- (NSView*)hitTest:(NSPoint)point
{
    (void)point;
    return self.enabled && !self.hidden ? self : nil;
}

- (void)publishValue:(double)value
{
    const double rounded = std::round(std::clamp(value, 50.0, 75.0)
        * 10.0) / 10.0;
    if (self.s3gHasOverride && rounded == self.s3gSwingValue) return;
    [self setSwingValue:rounded hasOverride:YES];
    [self sendAction:self.action to:self.target];
}

- (void)stageGestureValue:(double)value
{
    const double rounded = std::round(std::clamp(value, 50.0, 75.0)
        * 10.0) / 10.0;
    if (self.s3gHasOverride && rounded == self.s3gSwingValue) return;
    [self setSwingValue:rounded hasOverride:YES];
    self.s3gGestureChanged = YES;
}

- (void)resetToBase
{
    if (!self.enabled || !self.s3gHasOverride) return;
    [self setSwingValue:self.s3gSwingValue hasOverride:NO];
    [self sendAction:self.action to:self.target];
}

- (void)mouseDown:(NSEvent*)event
{
    if (!self.enabled || !self.window) return;
    if ((event.modifierFlags & NSEventModifierFlagOption) != 0u) {
        self.s3gDragging = NO;
        [self resetToBase];
        return;
    }
    const NSRect track = [self sliderTrackRect];
    const NSPoint initial = [self convertPoint:event.locationInWindow
        fromView:nil];
    self.s3gDragging = NSPointInRect(initial,
        NSInsetRect(track, -2.0, -7.0));
    if (!self.s3gDragging) return;
    self.s3gGestureChanged = NO;
    const double normalized = std::clamp(static_cast<double>(
        (initial.x - NSMinX(track)) / std::max<CGFloat>(1.0,
            NSWidth(track))), 0.0, 1.0);
    [self stageGestureValue:50.0 + normalized * 25.0];
}

- (void)mouseDragged:(NSEvent*)event
{
    if (!self.enabled || !self.s3gDragging) return;
    const NSRect track = [self sliderTrackRect];
    const NSPoint point = [self convertPoint:event.locationInWindow
        fromView:nil];
    const double normalized = std::clamp(static_cast<double>(
        (point.x - NSMinX(track)) / std::max<CGFloat>(1.0,
            NSWidth(track))), 0.0, 1.0);
    [self stageGestureValue:50.0 + normalized * 25.0];
}

- (void)mouseUp:(NSEvent*)event
{
    if (self.s3gDragging) [self mouseDragged:event];
    const BOOL changed = self.s3gDragging && self.s3gGestureChanged;
    self.s3gDragging = NO;
    self.s3gGestureChanged = NO;
    // Publishing rebuilds the Song table through the coordinator. Defer it
    // until mouse-up so the cell receiving this gesture remains alive for
    // the complete drag.
    if (changed) [self sendAction:self.action to:self.target];
}

- (void)rightMouseDown:(NSEvent*)event
{
    (void)event;
    self.s3gDragging = NO;
    [self resetToBase];
}

- (BOOL)adjustByScrollDelta:(CGFloat)delta
    modifierFlags:(NSEventModifierFlags)modifierFlags
{
    if (!self.enabled || delta == 0.0) return NO;
    const double increment = (modifierFlags
        & NSEventModifierFlagOption) != 0u ? 0.1 : 0.5;
    const double direction = delta > 0.0 ? 1.0 : -1.0;
    const double next = std::clamp(std::round((self.s3gSwingValue
        + direction * increment) * 10.0) / 10.0, 50.0, 75.0);
    if (self.s3gHasOverride && next == self.s3gSwingValue) return NO;
    [self publishValue:next];
    return YES;
}

- (void)scrollWheel:(NSEvent*)event
{
    if (!self.enabled) {
        [super scrollWheel:event];
        return;
    }
    CGFloat delta = event.scrollingDeltaY;
    if (event.hasPreciseScrollingDeltas) {
        self.s3gScrollAccumulator += delta;
        const CGFloat steps = std::trunc(self.s3gScrollAccumulator);
        self.s3gScrollAccumulator -= steps;
        delta = steps;
    } else {
        self.s3gScrollAccumulator = 0.0;
        delta = delta > 0.0 ? 1.0 : delta < 0.0 ? -1.0 : 0.0;
    }
    if (delta != 0.0) {
        const NSInteger steps = static_cast<NSInteger>(std::abs(delta));
        const CGFloat direction = delta > 0.0 ? 1.0 : -1.0;
        for (NSInteger step = 0; step < steps; ++step) {
            if (![self adjustByScrollDelta:direction
                    modifierFlags:event.modifierFlags]) break;
        }
    }
}

@end

@interface S3GTrackerSongTableView : NSTableView
@end

@implementation S3GTrackerSongTableView

- (NSView*)hitTest:(NSPoint)point
{
    NSView* hit = [super hitTest:point];
    if (hit != self) return hit;
    const NSInteger column = [self columnWithIdentifier:S3GSongColumnSwing];
    if (column < 0) return hit;
    for (NSInteger row = 0; row < self.numberOfRows; ++row) {
        NSView* cell = [self viewAtColumn:column row:row makeIfNecessary:NO];
        for (NSView* child in cell.subviews) {
            if (![child isKindOfClass:S3GTrackerSongSwingField.class])
                continue;
            const NSPoint childPoint = [child convertPoint:point
                fromView:self];
            if (NSPointInRect(childPoint, child.bounds)) return child;
        }
    }
    return hit;
}

@end

@interface S3GTrackerSongLoopCellView : NSTableCellView
@end

@implementation S3GTrackerSongLoopCellView

- (void)layout
{
    [super layout];
    if (self.subviews.count < 2u) return;
    const CGFloat inset = 4.0;
    const CGFloat gap = 4.0;
    const CGFloat width = std::max<CGFloat>(24.0,
        (NSWidth(self.bounds) - inset * 2.0 - gap) * 0.5);
    const CGFloat y = std::max<CGFloat>(2.0,
        (NSHeight(self.bounds) - 15.0) * 0.5);
    self.subviews[0u].frame = NSMakeRect(inset, y, width, 15.0);
    self.subviews[1u].frame = NSMakeRect(inset + width + gap, y,
        width, 15.0);
}

@end

@interface S3GTrackerSongTicksCellView : NSTableCellView
@end

@implementation S3GTrackerSongTicksCellView

- (BOOL)isFlipped { return YES; }

- (void)layout
{
    [super layout];
    if (self.subviews.count < 2u) return;
    const CGFloat width = std::max<CGFloat>(20.0,
        NSWidth(self.bounds) - 8.0);
    self.subviews[0u].frame = NSMakeRect(4.0, 5.0, width, 15.0);
    self.subviews[1u].frame = NSMakeRect(4.0, 25.0, width, 14.0);
}

@end

@interface S3GTrackerSongWindowController ()
    <NSTableViewDataSource, NSTableViewDelegate, NSWindowDelegate>
@property(nonatomic, strong) NSMutableArray<S3GTrackerSongRow*>* rows;
@property(nonatomic, strong) S3GTrackerSongRootView* rootView;
@property(nonatomic, strong) S3GTrackerToolboxView* projectPanel;
@property(nonatomic, strong) S3GTrackerToolboxView* transportPanel;
@property(nonatomic, strong) S3GTrackerToolboxView* rowToolsPanel;
@property(nonatomic, strong) S3GTrackerToolboxView* arrangementPanel;
@property(nonatomic, strong) NSScrollView* tableScrollView;
@property(nonatomic, strong) NSTableView* tableView;
@property(nonatomic, strong) NSTextField* queueStatusLabel;
@property(nonatomic, strong) NSButton* addButton;
@property(nonatomic, strong) NSButton* duplicateButton;
@property(nonatomic, strong) NSButton* removeButton;
@property(nonatomic, strong) NSButton* moveUpButton;
@property(nonatomic, strong) NSButton* moveDownButton;
@property(nonatomic, strong) S3GTrackerActionButton* songModeButton;
@property(nonatomic, strong) S3GTrackerActionButton* songLoopButton;
@property(nonatomic, strong) S3GTrackerPopupButton* launchQuantizationPopup;
@property(nonatomic, strong) S3GTrackerActionButton* queueButton;
@property(nonatomic, strong) S3GTrackerPopupButton* projectFileMenu;
@property(nonatomic, strong) NSTextField* arrangementHintLabel;
@property(nonatomic, copy) NSString* arrangementName;
@property(nonatomic, copy) NSArray<NSString*>* availablePatternIds;
@property(nonatomic, copy) NSArray<NSString*>* availablePatternNames;
@property(nonatomic, copy) NSArray<NSNumber*>* availablePatternLengths;
@property(nonatomic, copy) NSArray<NSNumber*>* availablePatternLaneCounts;
@property(nonatomic, copy) NSArray<NSNumber*>* availableWarpSlots;
@property(nonatomic, copy) NSArray<NSString*>* availableWarpTitles;
@property(nonatomic, copy) NSString* activePatternId;
@property(nonatomic) BOOL arrangementLoops;
@property(nonatomic) NSInteger arrangementTicksPerBeat;
@property(nonatomic) NSUInteger currentPlaybackRow;
@property(nonatomic) BOOL currentPlaybackRowValid;
@property(nonatomic) NSUInteger pendingPlaybackRow;
@property(nonatomic) BOOL pendingPlaybackRowValid;
@property(nonatomic) NSInteger pendingPlaybackQuantization;
@property(nonatomic) BOOL playbackLocked;
@end

@implementation S3GTrackerSongWindowController

+ (instancetype)sharedController
{
    static S3GTrackerSongWindowController* controller = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        controller = [[S3GTrackerSongWindowController alloc] init];
    });
    return controller;
}

- (instancetype)init
{
    const NSWindowStyleMask style = NSWindowStyleMaskTitled
        | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable
        | NSWindowStyleMaskResizable;
    NSWindow* window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0.0, 0.0, 1080.0, 610.0)
                  styleMask:style
                    backing:NSBackingStoreBuffered
                      defer:NO];
    self = [super initWithWindow:window];
    if (!self) return nil;

    _rows = [[NSMutableArray alloc] init];
    [_rows addObject:[self newRowWithPattern:@"A01"]];
    _arrangementName = @"SONG";
    _availablePatternIds = @[ @"A01" ];
    _availablePatternNames = @[ @"" ];
    _availablePatternLengths = @[ @16 ];
    _availablePatternLaneCounts = @[ @32 ];
    _availableWarpSlots = @[ @0 ];
    _availableWarpTitles = @[ @"OFF" ];
    _activePatternId = @"A01";
    _arrangementLoops = NO;
    _arrangementTicksPerBeat = 4;
    _playbackEnabled = NO;
    _currentPlaybackRowValid = NO;

    window.title = @"s3g Tracker — Song";
    window.minSize = NSMakeSize(900.0, 430.0);
    window.releasedWhenClosed = NO;
    window.delegate = self;
    window.tabbingMode = NSWindowTabbingModeDisallowed;
    window.titlebarAppearsTransparent = YES;
    window.backgroundColor = s3gSongColor(0x0c0c0c);
    window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    S3GTrackerRestoreWindowFrame(window, @"S3GTrackerSongWindow");

    [self buildInterface];
    return self;
}

- (S3GTrackerSongRow*)newRowWithPattern:(NSString*)pattern
{
    S3GTrackerSongRow* row = [[S3GTrackerSongRow alloc] init];
    row.pattern = pattern;
    row.repeats = 1;
    row.ticks = 4;
    row.swing = 56.0;
    row.hasSwingOverride = YES;
    row.warpSlot = 0;
    row.hasPatternLoop = NO;
    row.loopStart = 1;
    row.loopEnd = 16;
    row.mutedLanes = [[NSMutableIndexSet alloc] init];
    return row;
}

- (S3GTrackerSongRow*)copyRow:(S3GTrackerSongRow*)source
{
    S3GTrackerSongRow* row = [self newRowWithPattern:source.pattern.copy];
    row.repeats = source.repeats;
    row.ticks = source.ticks;
    row.swing = source.swing;
    row.hasSwingOverride = source.hasSwingOverride;
    row.warpSlot = source.warpSlot;
    row.hasPatternLoop = source.hasPatternLoop;
    row.loopStart = source.loopStart;
    row.loopEnd = source.loopEnd;
    row.mutedLanes = source.mutedLanes.mutableCopy;
    return row;
}

- (NSTextField*)label:(NSString*)text size:(CGFloat)size color:(NSColor*)color
    weight:(NSFontWeight)weight
{
    NSTextField* label = [NSTextField labelWithString:text];
    label.font = s3gSongFont(size, weight);
    label.textColor = color;
    label.translatesAutoresizingMaskIntoConstraints = YES;
    return label;
}

- (NSButton*)actionButton:(NSString*)title action:(SEL)action
{
    S3GTrackerActionButton* button = [[S3GTrackerActionButton alloc]
        initWithFrame:NSZeroRect];
    button.s3gUsesSuiteStyle = YES;
    button.title = title;
    button.target = self;
    button.action = action;
    button.translatesAutoresizingMaskIntoConstraints = YES;
    return button;
}

- (void)layoutSongInterface
{
    if (!self.rootView) return;
    const auto family = s3g::gui_layout::trackerSongFamilyLayout({
        static_cast<double>(NSWidth(self.rootView.bounds)),
        static_cast<double>(NSHeight(self.rootView.bounds)),
    });
    const auto cocoaRect = [](const s3g::gui_layout::Rect& rect) {
        return NSMakeRect(static_cast<CGFloat>(rect.x),
            static_cast<CGFloat>(rect.y),
            static_cast<CGFloat>(rect.width),
            static_cast<CGFloat>(rect.height));
    };
    self.projectPanel.frame = cocoaRect(family.project.frame);
    self.transportPanel.frame = cocoaRect(family.transport.frame);
    self.rowToolsPanel.frame = cocoaRect(family.rowTools.frame);
    self.arrangementPanel.frame = cocoaRect(family.arrangement.frame);

    constexpr CGFloat inset = 8.0;
    constexpr CGFloat gap = 4.0;
    constexpr CGFloat controlY = 27.0;
    constexpr CGFloat controlHeight = 22.0;
    const auto rowFrame = ^NSRect(S3GTrackerToolboxView* panel,
        CGFloat x, CGFloat width) {
        return NSMakeRect(x, controlY,
            std::max<CGFloat>(20.0, width), controlHeight);
    };

    const CGFloat projectWidth = std::max<CGFloat>(20.0,
        NSWidth(self.projectPanel.bounds) - inset * 2.0);
    self.projectFileMenu.frame = rowFrame(
        self.projectPanel, inset, projectWidth);

    const CGFloat transportWidth = std::max<CGFloat>(20.0,
        NSWidth(self.transportPanel.bounds) - inset * 2.0);
    const CGFloat modeWidth = transportWidth * 0.28;
    const CGFloat loopWidth = transportWidth * 0.24;
    const CGFloat quantizeWidth = transportWidth * 0.25;
    CGFloat x = inset;
    self.songModeButton.frame = rowFrame(
        self.transportPanel, x, modeWidth);
    x += modeWidth + gap;
    self.songLoopButton.frame = rowFrame(
        self.transportPanel, x, loopWidth);
    x += loopWidth + gap;
    self.launchQuantizationPopup.frame = rowFrame(
        self.transportPanel, x, quantizeWidth);
    x += quantizeWidth + gap;
    self.queueButton.frame = rowFrame(self.transportPanel, x,
        NSWidth(self.transportPanel.bounds) - inset - x);

    const CGFloat toolsWidth = std::max<CGFloat>(20.0,
        NSWidth(self.rowToolsPanel.bounds) - inset * 2.0);
    const CGFloat addWidth = toolsWidth * 0.16;
    const CGFloat duplicateWidth = toolsWidth * 0.15;
    const CGFloat deleteWidth = toolsWidth * 0.16;
    const CGFloat moveWidth = std::clamp<CGFloat>(
        toolsWidth * 0.09, 28.0, 38.0);
    x = inset;
    self.addButton.frame = rowFrame(self.rowToolsPanel, x, addWidth);
    x += addWidth + gap;
    self.duplicateButton.frame = rowFrame(
        self.rowToolsPanel, x, duplicateWidth);
    x += duplicateWidth + gap;
    self.removeButton.frame = rowFrame(
        self.rowToolsPanel, x, deleteWidth);
    x += deleteWidth + gap;
    self.moveUpButton.frame = rowFrame(
        self.rowToolsPanel, x, moveWidth);
    x += moveWidth + gap;
    self.moveDownButton.frame = rowFrame(
        self.rowToolsPanel, x, moveWidth);
    x += moveWidth + gap;
    self.queueStatusLabel.frame = rowFrame(self.rowToolsPanel, x,
        NSWidth(self.rowToolsPanel.bounds) - inset - x);

    const CGFloat header = static_cast<CGFloat>(
        s3g::gui_layout::kStandardMetrics.headerHeight);
    self.tableScrollView.frame = NSMakeRect(1.0, header,
        std::max<CGFloat>(0.0, NSWidth(self.arrangementPanel.bounds) - 2.0),
        std::max<CGFloat>(0.0, NSHeight(self.arrangementPanel.bounds)
            - header - 1.0));
    NSTableColumn* muteColumn = [self.tableView tableColumnWithIdentifier:
        S3GSongColumnMutes];
    if (muteColumn) {
        CGFloat fixedWidth = self.tableView.intercellSpacing.width
            * static_cast<CGFloat>(self.tableView.tableColumns.count);
        for (NSTableColumn* column in self.tableView.tableColumns) {
            if (column != muteColumn) fixedWidth += column.width;
        }
        const CGFloat viewportWidth = NSWidth(self.tableScrollView.bounds);
        muteColumn.width = std::max(muteColumn.minWidth,
            viewportWidth - fixedWidth);
        NSSize tableSize = self.tableView.frame.size;
        tableSize.width = std::max(viewportWidth,
            fixedWidth + muteColumn.width);
        [self.tableView setFrameSize:tableSize];
    }
    const CGFloat hintWidth = std::min<CGFloat>(610.0,
        NSWidth(self.arrangementPanel.bounds) * 0.58);
    self.arrangementHintLabel.frame = NSMakeRect(
        NSWidth(self.arrangementPanel.bounds) - hintWidth - 10.0,
        3.0, hintWidth, 16.0);
}

- (void)buildInterface
{
    self.rootView = [[S3GTrackerSongRootView alloc]
        initWithFrame:self.window.contentView.bounds];
    self.rootView.layoutOwner = self;
    self.rootView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    self.window.contentView = self.rootView;

    self.projectPanel = [[S3GTrackerToolboxView alloc] initWithFrame:NSZeroRect];
    self.projectPanel.toolboxIndex = 0;
    self.projectPanel.toolboxTitle = @"SONG / FILE";
    [self.rootView addSubview:self.projectPanel];
    self.transportPanel = [[S3GTrackerToolboxView alloc] initWithFrame:NSZeroRect];
    self.transportPanel.toolboxIndex = 0;
    self.transportPanel.toolboxTitle = @"TRANSPORT / QUEUE";
    [self.rootView addSubview:self.transportPanel];
    self.rowToolsPanel = [[S3GTrackerToolboxView alloc] initWithFrame:NSZeroRect];
    self.rowToolsPanel.toolboxIndex = 0;
    self.rowToolsPanel.toolboxTitle = @"ROW EDIT";
    [self.rootView addSubview:self.rowToolsPanel];
    self.arrangementPanel = [[S3GTrackerToolboxView alloc] initWithFrame:NSZeroRect];
    self.arrangementPanel.toolboxIndex = 0;
    self.arrangementPanel.toolboxTitle = @"ARRANGEMENT / ROWS";
    [self.rootView addSubview:self.arrangementPanel];

    _queueStatusLabel = [self label:@"QUEUE —" size:8.5
        color:s3gSongColor(0x737879) weight:NSFontWeightSemibold];
    _queueStatusLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    _queueStatusLabel.accessibilityLabel = @"Song queue status";
    [self.rowToolsPanel addSubview:_queueStatusLabel];

    self.tableScrollView = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    self.tableScrollView.hasVerticalScroller = YES;
    self.tableScrollView.hasHorizontalScroller = YES;
    self.tableScrollView.autohidesScrollers = YES;
    self.tableScrollView.scrollerStyle = NSScrollerStyleOverlay;
    self.tableScrollView.scrollerKnobStyle = NSScrollerKnobStyleLight;
    self.tableScrollView.borderType = NSNoBorder;
    self.tableScrollView.drawsBackground = YES;
    self.tableScrollView.backgroundColor = s3gSongColor(0x0e0e0e);
    [self.arrangementPanel addSubview:self.tableScrollView];

    _tableView = [[S3GTrackerSongTableView alloc] initWithFrame:NSZeroRect];
    _tableView.delegate = self;
    _tableView.dataSource = self;
    _tableView.style = NSTableViewStylePlain;
    _tableView.rowHeight = 48.0;
    _tableView.intercellSpacing = NSMakeSize(1.0, 1.0);
    _tableView.backgroundColor = s3gSongColor(0x0e0e0e);
    _tableView.gridColor = s3gSongColor(0x303030);
    _tableView.gridStyleMask = NSTableViewSolidVerticalGridLineMask
        | NSTableViewSolidHorizontalGridLineMask;
    _tableView.selectionHighlightStyle = NSTableViewSelectionHighlightStyleRegular;
    _tableView.columnAutoresizingStyle = NSTableViewNoColumnAutoresizing;
    _tableView.allowsColumnReordering = NO;
    _tableView.allowsColumnResizing = NO;
    _tableView.allowsMultipleSelection = NO;

    [self addColumn:S3GSongColumnRow title:@"ROW" width:44.0 minWidth:40.0];
    [self addColumn:S3GSongColumnPattern title:@"PATTERN" width:124.0 minWidth:100.0];
    [self addColumn:S3GSongColumnWarp title:@"WARP" width:110.0 minWidth:92.0];
    [self addColumn:S3GSongColumnPatternLoop title:@"LOOP IN–OUT"
        width:152.0 minWidth:152.0];
    [self addColumn:S3GSongColumnRepeats title:@"REP" width:52.0 minWidth:48.0];
    [self addColumn:S3GSongColumnTicks title:@"TICKS / SPAN"
        width:78.0 minWidth:68.0];
    [self addColumn:S3GSongColumnSwing title:@"SWING %" width:84.0 minWidth:78.0];
    [self addColumn:S3GSongColumnMutes
        title:@"LANE MUTES  1–16 TOP · 17–32 BOTTOM"
        width:344.0 minWidth:344.0];
    [self addColumn:S3GSongColumnDelete title:@"DEL" width:44.0 minWidth:40.0];
    CGFloat tableWidth = 0.0;
    for (NSTableColumn* column in _tableView.tableColumns)
        tableWidth += column.width + _tableView.intercellSpacing.width;
    _tableView.frame = NSMakeRect(0.0, 0.0, tableWidth,
        _tableView.rowHeight);
    _tableView.autoresizingMask = NSViewWidthSizable;
    self.tableScrollView.documentView = _tableView;

    _addButton = [self actionButton:@"＋ ADD" action:@selector(addRow:)];
    _addButton.accessibilityLabel = @"Add Song row after selection";
    [self.rowToolsPanel addSubview:_addButton];
    _duplicateButton = [self actionButton:@"DUP"
        action:@selector(duplicateSelectedRow:)];
    _duplicateButton.accessibilityLabel = @"Duplicate selected Song row";
    _duplicateButton.enabled = NO;
    [self.rowToolsPanel addSubview:_duplicateButton];
    _removeButton = [self actionButton:@"− DEL"
        action:@selector(removeSelectedRow:)];
    _removeButton.tag = 2;
    _removeButton.accessibilityLabel = @"Delete selected Song row";
    _removeButton.enabled = NO;
    [self.rowToolsPanel addSubview:_removeButton];
    _moveUpButton = [self actionButton:@"↑"
        action:@selector(moveSelectedRowUp:)];
    _moveUpButton.accessibilityLabel = @"Move selected Song row up";
    _moveUpButton.enabled = NO;
    [self.rowToolsPanel addSubview:_moveUpButton];
    _moveDownButton = [self actionButton:@"↓"
        action:@selector(moveSelectedRowDown:)];
    _moveDownButton.accessibilityLabel = @"Move selected Song row down";
    _moveDownButton.enabled = NO;
    [self.rowToolsPanel addSubview:_moveDownButton];

    _songModeButton = (S3GTrackerActionButton*)[self
        actionButton:@"SONG: OFF" action:@selector(toggleSongMode:)];
    _songModeButton.buttonType = NSButtonTypeToggle;
    _songModeButton.identifier = @"binary-status";
    [self.transportPanel addSubview:_songModeButton];

    _songLoopButton = (S3GTrackerActionButton*)[self
        actionButton:@"LOOP: OFF" action:@selector(toggleSongLoop:)];
    _songLoopButton.buttonType = NSButtonTypeToggle;
    _songLoopButton.tag = 1;
    [self.transportPanel addSubview:_songLoopButton];

    _launchQuantizationPopup = [[S3GTrackerPopupButton alloc]
        initWithFrame:NSZeroRect pullsDown:NO];
    _launchQuantizationPopup.s3gUsesCanvasMenu = YES;
    _launchQuantizationPopup.translatesAutoresizingMaskIntoConstraints = YES;
    [_launchQuantizationPopup addItemsWithTitles:@[
        @"NEXT TICK", @"NEXT BEAT", @"END OF PASS", @"END OF ROW"
    ]];
    [_launchQuantizationPopup selectItemAtIndex:3];
    _launchQuantizationPopup.accessibilityLabel = @"Song row queue boundary";
    _launchQuantizationPopup.enabled = NO;
    [self.transportPanel addSubview:_launchQuantizationPopup];

    _queueButton = (S3GTrackerActionButton*)[self
        actionButton:@"SELECT QUEUE" action:@selector(queueSelectedRow:)];
    _queueButton.accessibilityLabel = @"Queue selected Song row";
    _queueButton.enabled = NO;
    [self.transportPanel addSubview:_queueButton];

    _projectFileMenu = [[S3GTrackerPopupButton alloc]
        initWithFrame:NSZeroRect pullsDown:YES];
    _projectFileMenu.s3gUsesCanvasMenu = YES;
    _projectFileMenu.translatesAutoresizingMaskIntoConstraints = YES;
    [_projectFileMenu addItemsWithTitles:@[
        @"SONG FILE", @"SAVE SONG + PATTERNS…", @"LOAD SONG + PATTERNS…"
    ]];
    [_projectFileMenu itemAtIndex:1].tag = 1;
    [_projectFileMenu itemAtIndex:2].tag = 2;
    _projectFileMenu.target = self;
    _projectFileMenu.action = @selector(projectFileSelected:);
    _projectFileMenu.toolTip =
        @"Save or load the Song arrangement and its complete pattern bank";
    _projectFileMenu.accessibilityLabel = @"Song and pattern project file";
    [self.projectPanel addSubview:_projectFileMenu];

    self.arrangementHintLabel = [self label:
        @"TICKS = ROWS / PASS · MENU SHOWS PATTERN SPAN · LOOP USES LONGEST COLUMN · — = BASE"
        size:9.0 color:s3gSongColor(0x737879) weight:NSFontWeightMedium];
    self.arrangementHintLabel.alignment = NSTextAlignmentRight;
    self.arrangementHintLabel.lineBreakMode = NSLineBreakByTruncatingHead;
    [self.arrangementPanel addSubview:self.arrangementHintLabel];
    [self layoutSongInterface];

    [_tableView selectRowIndexes:[NSIndexSet indexSetWithIndex:0]
        byExtendingSelection:NO];
    [self updateRowToolAvailability];
}

- (void)projectFileSelected:(S3GTrackerPopupButton*)sender
{
    const NSInteger action = sender.selectedItem.tag;
    if (action == 1 && self.saveProjectHandler) {
        self.saveProjectHandler();
    } else if (action == 2 && self.loadProjectHandler) {
        self.loadProjectHandler();
    } else if (action != 0) {
        NSBeep();
    }
    [sender selectItemAtIndex:0];
}

- (void)addColumn:(NSString*)identifier title:(NSString*)title
    width:(CGFloat)width minWidth:(CGFloat)minWidth
{
    NSTableColumn* column = [[NSTableColumn alloc] initWithIdentifier:identifier];
    column.title = title;
    column.width = width;
    column.minWidth = minWidth;
    column.headerCell = [[S3GTrackerSongHeaderCell alloc] initTextCell:title];
    [_tableView addTableColumn:column];
}

- (void)showWindow:(id)sender
{
    [super showWindow:sender];
    if (self.window.miniaturized) [self.window deminiaturize:sender];
    [self.window makeKeyAndOrderFront:sender];
}

- (BOOL)windowShouldClose:(NSWindow*)sender
{
    (void)sender;
    // releasedWhenClosed is NO and this singleton retains the row model, so
    // normal close semantics preserve the draft while still allowing Cocoa's
    // last-window termination behavior to work.
    return YES;
}

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView
{
    (void)tableView;
    return static_cast<NSInteger>(self.rows.count);
}

- (NSTableRowView*)tableView:(NSTableView*)tableView rowViewForRow:(NSInteger)row
{
    (void)tableView;
    S3GTrackerSongRowView* view = [[S3GTrackerSongRowView alloc] initWithFrame:NSZeroRect];
    view.songRow = row;
    view.playbackActive = self.currentPlaybackRowValid
        && self.currentPlaybackRow == static_cast<NSUInteger>(row);
    view.playbackPending = self.pendingPlaybackRowValid
        && self.pendingPlaybackRow == static_cast<NSUInteger>(row);
    if (view.playbackActive && view.playbackPending) {
        view.accessibilityValue = @"Playing and queued";
    } else if (view.playbackActive) {
        view.accessibilityValue = @"Playing";
    } else if (view.playbackPending) {
        view.accessibilityValue = @"Queued";
    }
    return view;
}

- (NSTextField*)cellText:(NSString*)text row:(NSInteger)row
    column:(NSString*)column editable:(BOOL)editable alignment:(NSTextAlignment)alignment
{
    NSTextField* field = [[NSTextField alloc] initWithFrame:NSZeroRect];
    field.stringValue = text;
    field.font = s3gSongFont(11.0, editable ? NSFontWeightMedium : NSFontWeightRegular);
    field.textColor = editable ? s3gSongColor(0xd4d4d4) : s3gSongColor(0x858b8c);
    field.alignment = alignment;
    field.bordered = NO;
    field.drawsBackground = NO;
    const BOOL canEdit = editable;
    field.editable = canEdit;
    field.selectable = canEdit;
    field.identifier = column;
    field.tag = row;
    const NSInteger columnIndex = [self.tableView columnWithIdentifier:column];
    const CGFloat columnWidth = columnIndex >= 0
        ? self.tableView.tableColumns[(NSUInteger)columnIndex].width : 108.0;
    field.frame = NSMakeRect(4.0,
        std::max(2.0, (self.tableView.rowHeight - 23.0) * 0.5),
        std::max(20.0, columnWidth - 8.0), 23.0);
    field.autoresizingMask = NSViewWidthSizable;
    return field;
}

- (NSInteger)patternLengthForPatternId:(NSString*)patternId
{
    const NSUInteger index = [self.availablePatternIds indexOfObject:patternId];
    if (index == NSNotFound || index >= self.availablePatternLengths.count)
        return 16;
    NSNumber* length = self.availablePatternLengths[index];
    return s3gClampInteger(length.integerValue, 1,
        static_cast<NSInteger>(s3g::tracker::kMaximumSongPatternRows));
}

- (NSInteger)patternLaneCountForPatternId:(NSString*)patternId
{
    const NSUInteger index = [self.availablePatternIds indexOfObject:patternId];
    if (index == NSNotFound
        || index >= self.availablePatternLaneCounts.count)
        return 0;
    NSNumber* count = self.availablePatternLaneCounts[index];
    return s3gClampInteger(count.integerValue, 0, 32);
}

- (void)removeUnavailableMutesFromRow:(S3GTrackerSongRow*)row
{
    const NSUInteger patternIndex = [self.availablePatternIds
        indexOfObject:row.pattern];
    if (patternIndex == NSNotFound
        || patternIndex >= self.availablePatternLaneCounts.count) return;
    const NSInteger laneCount = s3gClampInteger(
        self.availablePatternLaneCounts[patternIndex].integerValue, 0, 32);
    if (laneCount < 32) {
        [row.mutedLanes removeIndexesInRange:NSMakeRange(
            static_cast<NSUInteger>(laneCount),
            static_cast<NSUInteger>(32 - laneCount))];
    }
}

- (NSInteger)playbackSpanForRow:(S3GTrackerSongRow*)row
{
    if (row.hasPatternLoop)
        return std::max<NSInteger>(1, row.loopEnd - row.loopStart + 1);
    return [self patternLengthForPatternId:row.pattern];
}

- (NSString*)ticksTitle:(NSInteger)ticks forRow:(S3GTrackerSongRow*)row
{
    const NSInteger span = [self playbackSpanForRow:row];
    if (ticks == span)
        return [NSString stringWithFormat:@"%ld · FULL", ticks];
    if (ticks < span) {
        const NSInteger percent = static_cast<NSInteger>(std::lround(
            static_cast<double>(ticks) * 100.0
            / static_cast<double>(span)));
        return [NSString stringWithFormat:@"%ld · %ld%%", ticks, percent];
    }
    const double passes = static_cast<double>(ticks)
        / static_cast<double>(span);
    return [NSString stringWithFormat:@"%ld · %.2g×", ticks, passes];
}

- (NSString*)ticksSpanSummaryForRow:(S3GTrackerSongRow*)row
{
    const NSInteger span = [self playbackSpanForRow:row];
    return [NSString stringWithFormat:@"%ld/%ld %@",
        row.ticks, span, row.hasPatternLoop ? @"LOOP" : @"PAT"];
}

- (NSString*)ticksToolTipForRow:(S3GTrackerSongRow*)row
{
    const NSInteger span = [self playbackSpanForRow:row];
    NSString* source = row.hasPatternLoop
        ? [NSString stringWithFormat:@"the %ld-row Loop In–Out span", span]
        : [NSString stringWithFormat:
            @"the selected pattern's %ld-row longest column", span];
    return [NSString stringWithFormat:
        @"%ld ticks advances %ld tracker rows through %@ per repetition. Column phase continues when REP is greater than 1.",
        row.ticks, row.ticks, source];
}

- (S3GTrackerPopupButton*)cellPopupForColumn:(NSTableColumn*)tableColumn
    row:(NSInteger)row action:(SEL)action accessibilityLabel:(NSString*)label
{
    S3GTrackerPopupButton* popup = [[S3GTrackerPopupButton alloc]
        initWithFrame:NSMakeRect(4.0,
            std::max(2.0, (self.tableView.rowHeight - 15.0) * 0.5),
            std::max(28.0, tableColumn.width - 8.0), 15.0)
        pullsDown:NO];
    popup.s3gUsesCanvasMenu = YES;
    popup.autoresizingMask = NSViewWidthSizable;
    popup.target = self;
    popup.action = action;
    popup.tag = row;
    popup.enabled = YES;
    popup.accessibilityLabel = label;
    return popup;
}

- (void)reloadSongRow:(NSInteger)row column:(NSString*)identifier
{
    const NSInteger column = [self.tableView columnWithIdentifier:identifier];
    if (row < 0 || row >= static_cast<NSInteger>(self.rows.count)
        || column < 0) return;
    [self.tableView reloadDataForRowIndexes:
        [NSIndexSet indexSetWithIndex:static_cast<NSUInteger>(row)]
        columnIndexes:[NSIndexSet indexSetWithIndex:
            static_cast<NSUInteger>(column)]];
}

- (NSView*)tableView:(NSTableView*)tableView
    viewForTableColumn:(NSTableColumn*)tableColumn row:(NSInteger)rowIndex
{
    (void)tableView;
    if (rowIndex < 0 || rowIndex >= (NSInteger)self.rows.count) return nil;
    S3GTrackerSongRow* row = self.rows[(NSUInteger)rowIndex];
    NSString* column = tableColumn.identifier;
    Class cellClass = [column isEqualToString:S3GSongColumnPatternLoop]
        ? S3GTrackerSongLoopCellView.class
        : [column isEqualToString:S3GSongColumnTicks]
            ? S3GTrackerSongTicksCellView.class
            : NSTableCellView.class;
    NSTableCellView* cell = [[cellClass alloc]
        initWithFrame:NSMakeRect(0.0, 0.0, tableColumn.width, tableView.rowHeight)];

    if ([column isEqualToString:S3GSongColumnRow]) {
        [cell addSubview:[self cellText:[NSString stringWithFormat:@"%02ld", rowIndex + 1]
            row:rowIndex column:column editable:NO alignment:NSTextAlignmentCenter]];
    } else if ([column isEqualToString:S3GSongColumnPattern]) {
        S3GTrackerPopupButton* pattern = [[S3GTrackerPopupButton alloc]
            initWithFrame:NSMakeRect(4.0,
                std::max(2.0, (tableView.rowHeight - 15.0) * 0.5),
                std::max(40.0, tableColumn.width - 8.0), 15.0)
            pullsDown:NO];
        pattern.s3gUsesCanvasMenu = YES;
        pattern.autoresizingMask = NSViewWidthSizable;
        pattern.target = self;
        pattern.action = @selector(patternPopupChanged:);
        pattern.tag = rowIndex;
        pattern.enabled = YES;
        pattern.accessibilityLabel = [NSString stringWithFormat:
            @"Song row %ld pattern", rowIndex + 1];
        [self.availablePatternIds enumerateObjectsUsingBlock:
            ^(NSString* patternId, NSUInteger index, BOOL* stop) {
            (void)stop;
            NSString* patternName = index < self.availablePatternNames.count
                ? self.availablePatternNames[index] : @"";
            NSString* title = patternName.length > 0u
                ? [NSString stringWithFormat:@"%@ · %@", patternId,
                    patternName]
                : patternId;
            [pattern addItemWithTitle:title];
            pattern.lastItem.representedObject = patternId;
        }];
        const NSUInteger selectionIndex = [self.availablePatternIds
            indexOfObject:row.pattern];
        NSInteger selection = selectionIndex == NSNotFound
            ? -1 : static_cast<NSInteger>(selectionIndex);
        if (selectionIndex == NSNotFound) {
            NSString* missing = [NSString stringWithFormat:@"MISSING · %@",
                row.pattern.length > 0u ? row.pattern : @"—"];
            [pattern addItemWithTitle:missing];
            pattern.lastItem.representedObject = row.pattern.length > 0u
                ? row.pattern : @"";
            selection = static_cast<NSInteger>(pattern.numberOfItems - 1u);
            pattern.toolTip = @"This Song row references a pattern that is not in the bank";
        }
        if (selection >= 0) [pattern selectItemAtIndex:selection];
        [cell addSubview:pattern];
    } else if ([column isEqualToString:S3GSongColumnWarp]) {
        S3GTrackerPopupButton* warp = [[S3GTrackerPopupButton alloc]
            initWithFrame:NSMakeRect(4.0,
                std::max(2.0, (tableView.rowHeight - 15.0) * 0.5),
                std::max(40.0, tableColumn.width - 8.0), 15.0)
            pullsDown:NO];
        warp.s3gUsesCanvasMenu = YES;
        warp.autoresizingMask = NSViewWidthSizable;
        warp.target = self;
        warp.action = @selector(warpPopupChanged:);
        warp.tag = rowIndex;
        warp.enabled = YES;
        warp.accessibilityLabel = [NSString stringWithFormat:
            @"Song row %ld timing warp", rowIndex + 1];
        [self.availableWarpSlots enumerateObjectsUsingBlock:
            ^(NSNumber* slot, NSUInteger index, BOOL* stop) {
            (void)stop;
            NSString* title = index < self.availableWarpTitles.count
                ? self.availableWarpTitles[index] : @"OFF";
            [warp addItemWithTitle:title];
            warp.lastItem.representedObject = slot;
        }];
        const NSUInteger selectionIndex = [self.availableWarpSlots
            indexOfObject:@(row.warpSlot)];
        NSInteger selection = selectionIndex == NSNotFound
            ? -1 : static_cast<NSInteger>(selectionIndex);
        if (selectionIndex == NSNotFound && row.warpSlot > 0) {
            [warp addItemWithTitle:[NSString stringWithFormat:
                @"%02ld · MISSING", row.warpSlot]];
            warp.lastItem.representedObject = @(row.warpSlot);
            selection = static_cast<NSInteger>(warp.numberOfItems - 1u);
            warp.toolTip = @"This Song row references an empty saved warp slot; playback uses OFF";
        }
        if (selection >= 0) [warp selectItemAtIndex:selection];
        [cell addSubview:warp];
    } else if ([column isEqualToString:S3GSongColumnPatternLoop]) {
        const NSInteger rowCount = [self patternLengthForPatternId:row.pattern];
        S3GTrackerPopupButton* loopIn = [self cellPopupForColumn:tableColumn
            row:rowIndex action:@selector(loopStartPopupChanged:)
            accessibilityLabel:[NSString stringWithFormat:
                @"Song row %ld pattern loop in", rowIndex + 1]];
        [loopIn addItemWithTitle:@"OFF"];
        loopIn.lastItem.representedObject = @0;
        for (NSInteger patternRow = 1; patternRow <= rowCount; ++patternRow) {
            [loopIn addItemWithTitle:[NSString stringWithFormat:
                @"IN %03ld", patternRow]];
            loopIn.lastItem.representedObject = @(patternRow);
        }
        NSInteger loopInSelection = row.hasPatternLoop
            ? [loopIn indexOfItemWithRepresentedObject:@(row.loopStart)] : 0;
        if (loopInSelection < 0 && row.hasPatternLoop) {
            [loopIn addItemWithTitle:[NSString stringWithFormat:
                @"IN %03ld · SAVED", row.loopStart]];
            loopIn.lastItem.representedObject = @(row.loopStart);
            loopInSelection = loopIn.numberOfItems - 1;
        }
        [loopIn selectItemAtIndex:std::max<NSInteger>(0, loopInSelection)];
        loopIn.toolTip = @"Choose OFF or the inclusive first row of this pattern loop";
        [cell addSubview:loopIn];

        S3GTrackerPopupButton* loopOut = [self cellPopupForColumn:tableColumn
            row:rowIndex action:@selector(loopEndPopupChanged:)
            accessibilityLabel:[NSString stringWithFormat:
                @"Song row %ld pattern loop out", rowIndex + 1]];
        if (!row.hasPatternLoop) {
            [loopOut addItemWithTitle:@"—"];
            loopOut.lastItem.representedObject = @0;
            loopOut.enabled = NO;
        } else {
            const NSInteger firstEnd = s3gClampInteger(
                row.loopStart, 1, rowCount);
            for (NSInteger patternRow = firstEnd;
                 patternRow <= rowCount; ++patternRow) {
                [loopOut addItemWithTitle:[NSString stringWithFormat:
                    @"OUT %03ld", patternRow]];
                loopOut.lastItem.representedObject = @(patternRow);
            }
            NSInteger loopOutSelection = [loopOut
                indexOfItemWithRepresentedObject:@(row.loopEnd)];
            if (loopOutSelection < 0) {
                [loopOut addItemWithTitle:[NSString stringWithFormat:
                    @"OUT %03ld · SAVED", row.loopEnd]];
                loopOut.lastItem.representedObject = @(row.loopEnd);
                loopOutSelection = loopOut.numberOfItems - 1;
            }
            [loopOut selectItemAtIndex:std::max<NSInteger>(0,
                loopOutSelection)];
            loopOut.toolTip = @"Choose the inclusive last row; choices cannot precede Loop In";
        }
        [cell addSubview:loopOut];
        [cell layoutSubtreeIfNeeded];
    } else if ([column isEqualToString:S3GSongColumnRepeats]) {
        S3GTrackerPopupButton* repeats = [self cellPopupForColumn:tableColumn
            row:rowIndex action:@selector(repeatsPopupChanged:)
            accessibilityLabel:[NSString stringWithFormat:
                @"Song row %ld repetitions", rowIndex + 1]];
        for (NSInteger value = 1; value <= 64; ++value) {
            [repeats addItemWithTitle:[NSString stringWithFormat:@"%ld", value]];
            repeats.lastItem.representedObject = @(value);
        }
        NSInteger selection = [repeats
            indexOfItemWithRepresentedObject:@(row.repeats)];
        if (selection < 0) {
            [repeats addItemWithTitle:[NSString stringWithFormat:
                @"%ld · SAVED", row.repeats]];
            repeats.lastItem.representedObject = @(row.repeats);
            selection = repeats.numberOfItems - 1;
        }
        [repeats selectItemAtIndex:selection];
        repeats.toolTip = @"Number of pattern-cycle repetitions, 1–64";
        [cell addSubview:repeats];
    } else if ([column isEqualToString:S3GSongColumnTicks]) {
        S3GTrackerPopupButton* ticks = [self cellPopupForColumn:tableColumn
            row:rowIndex action:@selector(ticksPopupChanged:)
            accessibilityLabel:[NSString stringWithFormat:
                @"Song row %ld ticks", rowIndex + 1]];
        const NSInteger fullSpan = [self playbackSpanForRow:row];
        [ticks addItemWithTitle:[NSString stringWithFormat:
            @"FULL · 1× · %ld", fullSpan]];
        ticks.lastItem.representedObject = @(fullSpan);
        for (const NSInteger value : kSongTickChoices) {
            if (value == fullSpan) continue;
            [ticks addItemWithTitle:[self ticksTitle:value forRow:row]];
            ticks.lastItem.representedObject = @(value);
        }
        NSInteger selection = [ticks
            indexOfItemWithRepresentedObject:@(row.ticks)];
        if (selection < 0) {
            [ticks addItemWithTitle:[NSString stringWithFormat:
                @"%ld · SAVED", row.ticks]];
            ticks.lastItem.representedObject = @(row.ticks);
            selection = ticks.numberOfItems - 1;
        }
        [ticks selectItemAtIndex:selection];
        ticks.toolTip = [self ticksToolTipForRow:row];
        [cell addSubview:ticks];
        NSTextField* span = [self label:[self ticksSpanSummaryForRow:row]
            size:7.0 color:S3GTrackerThemeColor(
                S3GTrackerThemeRole::TextMuted)
            weight:NSFontWeightMedium];
        span.alignment = NSTextAlignmentCenter;
        span.lineBreakMode = NSLineBreakByTruncatingTail;
        span.toolTip = ticks.toolTip;
        span.accessibilityLabel = [NSString stringWithFormat:
            @"Song row %ld tick span", rowIndex + 1];
        span.accessibilityValue = span.stringValue;
        [cell addSubview:span];
        [cell layoutSubtreeIfNeeded];
    } else if ([column isEqualToString:S3GSongColumnSwing]) {
        S3GTrackerSongSwingField* field = [[S3GTrackerSongSwingField alloc]
            initWithFrame:NSZeroRect];
        [field setSwingValue:row.swing
            hasOverride:row.hasSwingOverride];
        field.target = self;
        field.action = @selector(swingFieldChanged:);
        field.identifier = column;
        field.tag = rowIndex;
        field.enabled = YES;
        field.frame = NSMakeRect(4.0,
            std::max(2.0, (self.tableView.rowHeight - 23.0) * 0.5),
            std::max(20.0, tableColumn.width - 8.0), 23.0);
        field.autoresizingMask = NSViewWidthSizable;
        field.toolTip = @"Swing override: drag the short slider or scroll; Option-click or right-click returns to base";
        field.accessibilityLabel = [NSString stringWithFormat:
            @"Song row %ld swing percentage", rowIndex + 1];
        [cell addSubview:field];
    } else if ([column isEqualToString:S3GSongColumnMutes]) {
        const NSInteger laneCount = [self patternLaneCountForPatternId:
            row.pattern];
        NSView* matrix = [[S3GTrackerSongMuteMatrixView alloc]
            initWithFrame:NSMakeRect(2.0, 1.0,
            MAX(340.0, tableColumn.width - 4.0), tableView.rowHeight - 2.0)];
        matrix.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        for (NSInteger lane = 0; lane < 32; ++lane) {
            S3GTrackerSongMuteButton* button = [[S3GTrackerSongMuteButton alloc]
                initWithFrame:NSZeroRect];
            button.title = [NSString stringWithFormat:@"%ld", lane + 1];
            button.buttonType = NSButtonTypeToggle;
            button.bordered = NO;
            const BOOL available = lane < laneCount;
            button.state = available
                && [row.mutedLanes containsIndex:(NSUInteger)lane]
                ? NSControlStateValueOn : NSControlStateValueOff;
            button.tag = rowIndex * 32 + lane;
            button.target = self;
            button.action = @selector(toggleLaneMute:);
            button.enabled = available;
            button.toolTip = available
                ? [NSString stringWithFormat:@"Toggle lane %ld mute", lane + 1]
                : [NSString stringWithFormat:
                    @"Lane %ld is not present in pattern %@",
                    lane + 1, row.pattern];
            button.accessibilityLabel = [NSString stringWithFormat:
                @"Song row %ld lane %ld mute", rowIndex + 1, lane + 1];
            [matrix addSubview:button];
        }
        [cell addSubview:matrix];
    } else if ([column isEqualToString:S3GSongColumnDelete]) {
        S3GTrackerActionButton* button = [[S3GTrackerActionButton alloc]
            initWithFrame:NSZeroRect];
        button.s3gUsesSuiteStyle = YES;
        button.title = @"×";
        button.target = self;
        button.action = @selector(deleteRowButton:);
        button.enabled = YES;
        button.frame = NSMakeRect(4.0,
            std::max(2.0, (tableView.rowHeight - 30.0) * 0.5),
            40.0, 30.0);
        button.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        button.identifier = @"danger";
        button.tag = rowIndex;
        button.toolTip = @"Delete this song row";
        button.accessibilityLabel = [NSString stringWithFormat:
            @"Delete song row %ld", rowIndex + 1];
        [cell addSubview:button];
    }
    return cell;
}

- (void)tableViewSelectionDidChange:(NSNotification*)notification
{
    (void)notification;
    [self updateRowToolAvailability];
}

- (void)loopStartPopupChanged:(S3GTrackerPopupButton*)sender
{
    const NSInteger rowIndex = sender.tag;
    if (rowIndex < 0 || rowIndex >= static_cast<NSInteger>(self.rows.count))
        return;
    NSNumber* represented = sender.selectedItem.representedObject;
    if (![represented isKindOfClass:NSNumber.class]) return;
    S3GTrackerSongRow* row = self.rows[static_cast<NSUInteger>(rowIndex)];
    const NSInteger rowCount = [self patternLengthForPatternId:row.pattern];
    const NSInteger start = represented.integerValue;
    if (start < 0 || start > rowCount) return;
    const BOOL wasEnabled = row.hasPatternLoop;
    const NSInteger oldStart = row.loopStart;
    const NSInteger oldEnd = row.loopEnd;
    if (start == 0) {
        row.hasPatternLoop = NO;
    } else {
        row.hasPatternLoop = YES;
        row.loopStart = start;
        row.loopEnd = wasEnabled
            ? std::clamp(oldEnd, start, rowCount) : rowCount;
    }
    [self reloadSongRow:rowIndex column:S3GSongColumnPatternLoop];
    [self reloadSongRow:rowIndex column:S3GSongColumnTicks];
    if (wasEnabled != row.hasPatternLoop || oldStart != row.loopStart
        || oldEnd != row.loopEnd) [self songDidChange];
}

- (void)loopEndPopupChanged:(S3GTrackerPopupButton*)sender
{
    const NSInteger rowIndex = sender.tag;
    if (rowIndex < 0 || rowIndex >= static_cast<NSInteger>(self.rows.count))
        return;
    NSNumber* represented = sender.selectedItem.representedObject;
    if (![represented isKindOfClass:NSNumber.class]) return;
    S3GTrackerSongRow* row = self.rows[static_cast<NSUInteger>(rowIndex)];
    const NSInteger end = represented.integerValue;
    const NSInteger rowCount = [self patternLengthForPatternId:row.pattern];
    if (!row.hasPatternLoop || end < row.loopStart || end > rowCount) return;
    if (end == row.loopEnd) return;
    row.loopEnd = end;
    [self reloadSongRow:rowIndex column:S3GSongColumnTicks];
    [self songDidChange];
}

- (void)repeatsPopupChanged:(S3GTrackerPopupButton*)sender
{
    const NSInteger rowIndex = sender.tag;
    if (rowIndex < 0 || rowIndex >= static_cast<NSInteger>(self.rows.count))
        return;
    NSNumber* represented = sender.selectedItem.representedObject;
    const NSInteger value = [represented isKindOfClass:NSNumber.class]
        ? represented.integerValue : 0;
    if (value < 1 || value > 64) return;
    S3GTrackerSongRow* row = self.rows[static_cast<NSUInteger>(rowIndex)];
    if (value == row.repeats) return;
    row.repeats = value;
    [self songDidChange];
}

- (void)ticksPopupChanged:(S3GTrackerPopupButton*)sender
{
    const NSInteger rowIndex = sender.tag;
    if (rowIndex < 0 || rowIndex >= static_cast<NSInteger>(self.rows.count))
        return;
    NSNumber* represented = sender.selectedItem.representedObject;
    const NSInteger value = [represented isKindOfClass:NSNumber.class]
        ? represented.integerValue : 0;
    S3GTrackerSongRow* row = self.rows[static_cast<NSUInteger>(rowIndex)];
    const bool fixedChoice = std::find(kSongTickChoices.begin(),
        kSongTickChoices.end(), value) != kSongTickChoices.end();
    if (!fixedChoice && value != [self playbackSpanForRow:row]) return;
    if (value == row.ticks) return;
    row.ticks = value;
    [self reloadSongRow:rowIndex column:S3GSongColumnTicks];
    [self songDidChange];
}

- (void)swingFieldChanged:(S3GTrackerSongSwingField*)sender
{
    const NSInteger rowIndex = sender.tag;
    if (rowIndex < 0 || rowIndex >= static_cast<NSInteger>(self.rows.count))
        return;
    S3GTrackerSongRow* row = self.rows[static_cast<NSUInteger>(rowIndex)];
    const BOOL changed = row.hasSwingOverride != sender.s3gHasOverride
        || (sender.s3gHasOverride && row.swing != sender.s3gSwingValue);
    row.hasSwingOverride = sender.s3gHasOverride;
    if (sender.s3gHasOverride) row.swing = sender.s3gSwingValue;
    if (changed) [self songDidChange];
}

- (void)patternPopupChanged:(S3GTrackerPopupButton*)sender
{
    const NSInteger rowIndex = sender.tag;
    if (rowIndex < 0 || rowIndex >= (NSInteger)self.rows.count) return;
    NSString* patternId = sender.selectedItem.representedObject;
    if (![patternId isKindOfClass:NSString.class]
        || patternId.length == 0u) return;
    S3GTrackerSongRow* row = self.rows[(NSUInteger)rowIndex];
    if ([row.pattern isEqualToString:patternId]) return;
    row.pattern = patternId;
    [self removeUnavailableMutesFromRow:row];
    if (row.hasPatternLoop) {
        const NSInteger rowCount = [self patternLengthForPatternId:patternId];
        row.loopStart = s3gClampInteger(row.loopStart, 1, rowCount);
        row.loopEnd = s3gClampInteger(
            row.loopEnd, row.loopStart, rowCount);
    }
    [self reloadSongRow:rowIndex column:S3GSongColumnPatternLoop];
    [self reloadSongRow:rowIndex column:S3GSongColumnTicks];
    [self reloadSongRow:rowIndex column:S3GSongColumnMutes];
    [self songDidChange];
}

- (void)warpPopupChanged:(S3GTrackerPopupButton*)sender
{
    const NSInteger rowIndex = sender.tag;
    if (rowIndex < 0 || rowIndex >= (NSInteger)self.rows.count) return;
    NSNumber* represented = sender.selectedItem.representedObject;
    if (![represented isKindOfClass:NSNumber.class]) return;
    const NSInteger slot = s3gClampInteger(represented.integerValue, 0,
        static_cast<NSInteger>(
            s3g::tracker::kMaximumTimingWarpLibraryEntries));
    S3GTrackerSongRow* row = self.rows[(NSUInteger)rowIndex];
    if (row.warpSlot == slot) return;
    row.warpSlot = slot;
    [self songDidChange];
}

- (void)addRow:(id)sender
{
    (void)sender;
    if (self.rows.count >= s3g::tracker::kMaximumSongRows) return;
    NSInteger insertion = self.tableView.selectedRow;
    insertion = insertion < 0 ? (NSInteger)self.rows.count : insertion + 1;
    NSString* pattern = self.activePatternId;
    if (pattern.length == 0u) pattern = self.rows.firstObject.pattern;
    if (pattern.length == 0u) pattern = self.availablePatternIds.firstObject;
    if (pattern.length == 0u) pattern = @"A01";
    S3GTrackerSongRow* row = [self newRowWithPattern:pattern];
    if (insertion > 0 && insertion <= (NSInteger)self.rows.count) {
        S3GTrackerSongRow* prior = self.rows[(NSUInteger)insertion - 1u];
        row.ticks = prior.ticks;
        row.swing = prior.swing;
        row.hasSwingOverride = prior.hasSwingOverride;
        row.warpSlot = prior.warpSlot;
    }
    [self.rows insertObject:row atIndex:(NSUInteger)insertion];
    [self.tableView reloadData];
    [self.tableView selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)insertion]
        byExtendingSelection:NO];
    [self.tableView scrollRowToVisible:insertion];
    [self updateRowToolAvailability];
    [self songDidChange];
}

- (void)duplicateSelectedRow:(id)sender
{
    (void)sender;
    if (self.rows.count >= s3g::tracker::kMaximumSongRows) return;
    const NSInteger sourceIndex = self.tableView.selectedRow;
    if (sourceIndex < 0
        || sourceIndex >= static_cast<NSInteger>(self.rows.count)) return;
    const NSInteger insertion = sourceIndex + 1;
    S3GTrackerSongRow* copy = [self copyRow:
        self.rows[static_cast<NSUInteger>(sourceIndex)]];
    [self.rows insertObject:copy atIndex:static_cast<NSUInteger>(insertion)];
    [self.tableView reloadData];
    [self.tableView selectRowIndexes:[NSIndexSet indexSetWithIndex:
        static_cast<NSUInteger>(insertion)] byExtendingSelection:NO];
    [self.tableView scrollRowToVisible:insertion];
    [self updateRowToolAvailability];
    [self songDidChange];
}

- (void)moveSelectedRowBy:(NSInteger)offset
{
    if (offset != -1 && offset != 1) return;
    const NSInteger source = self.tableView.selectedRow;
    const NSInteger destination = source + offset;
    if (source < 0 || source >= static_cast<NSInteger>(self.rows.count)
        || destination < 0
        || destination >= static_cast<NSInteger>(self.rows.count)) return;
    [self.rows exchangeObjectAtIndex:static_cast<NSUInteger>(source)
        withObjectAtIndex:static_cast<NSUInteger>(destination)];
    [self.tableView reloadData];
    [self.tableView selectRowIndexes:[NSIndexSet indexSetWithIndex:
        static_cast<NSUInteger>(destination)] byExtendingSelection:NO];
    [self.tableView scrollRowToVisible:destination];
    [self updateRowToolAvailability];
    [self songDidChange];
}

- (void)moveSelectedRowUp:(id)sender
{
    (void)sender;
    [self moveSelectedRowBy:-1];
}

- (void)moveSelectedRowDown:(id)sender
{
    (void)sender;
    [self moveSelectedRowBy:1];
}

- (void)removeSelectedRow:(id)sender
{
    (void)sender;
    const NSInteger row = self.tableView.selectedRow;
    [self removeRowAtIndex:row];
}

- (void)deleteRowButton:(NSButton*)sender
{
    [self removeRowAtIndex:sender.tag];
}

- (void)removeRowAtIndex:(NSInteger)row
{
    if (row < 0 || row >= (NSInteger)self.rows.count) return;
    [self.rows removeObjectAtIndex:(NSUInteger)row];
    [self.tableView reloadData];
    if (self.rows.count > 0) {
        const NSUInteger selection = std::min((NSUInteger)row, self.rows.count - 1u);
        [self.tableView selectRowIndexes:[NSIndexSet indexSetWithIndex:selection]
            byExtendingSelection:NO];
    } else {
        [self.tableView deselectAll:nil];
    }
    [self updateRowToolAvailability];
    [self songDidChange];
}

- (void)toggleLaneMute:(S3GTrackerSongMuteButton*)sender
{
    const NSInteger rowIndex = sender.tag / 32;
    const NSInteger lane = sender.tag % 32;
    if (rowIndex < 0 || rowIndex >= (NSInteger)self.rows.count
        || lane < 0 || lane >= 32) return;
    S3GTrackerSongRow* row = self.rows[(NSUInteger)rowIndex];
    if (lane >= [self patternLaneCountForPatternId:row.pattern]) {
        sender.state = NSControlStateValueOff;
        [sender setNeedsDisplay:YES];
        return;
    }
    if ([row.mutedLanes containsIndex:(NSUInteger)lane])
        [row.mutedLanes removeIndex:(NSUInteger)lane];
    else
        [row.mutedLanes addIndex:(NSUInteger)lane];
    sender.state = [row.mutedLanes containsIndex:(NSUInteger)lane]
        ? NSControlStateValueOn : NSControlStateValueOff;
    [sender setNeedsDisplay:YES];
    [self songDidChange];
}

- (NSString*)songSummary
{
    const NSUInteger count = self.rows.count;
    if (count == 0) return @"0 ROWS · EMPTY ARRANGEMENT";
    NSInteger passes = 0;
    NSMutableSet<NSString*>* patterns = [[NSMutableSet alloc] init];
    for (S3GTrackerSongRow* row in self.rows) {
        passes += row.repeats;
        [patterns addObject:row.pattern];
    }
    return [NSString stringWithFormat:@"%lu ROW%@ · %lu PATTERN%@ · %ld PASS%@ · HOST TEMPO",
        (unsigned long)count, count == 1 ? @"" : @"S",
        (unsigned long)patterns.count, patterns.count == 1 ? @"" : @"S",
        passes, passes == 1 ? @"" : @"ES"];
}

- (void)songDidChange
{
    NSString* summary = self.songSummary;
    if (self.changeHandler) self.changeHandler(summary);
}

- (void)updateRowToolAvailability
{
    const NSInteger selected = self.tableView.selectedRow;
    const BOOL hasSelection = selected >= 0
        && selected < static_cast<NSInteger>(self.rows.count);
    const BOOL canEdit = hasSelection;
    self.addButton.enabled =
        self.rows.count < s3g::tracker::kMaximumSongRows;
    self.duplicateButton.enabled = canEdit
        && self.rows.count < s3g::tracker::kMaximumSongRows;
    self.removeButton.enabled = canEdit;
    self.moveUpButton.enabled = canEdit && selected > 0;
    self.moveDownButton.enabled = canEdit
        && selected + 1 < static_cast<NSInteger>(self.rows.count);
    self.queueButton.enabled = self.playbackEnabled && self.playbackLocked
        && hasSelection;
}

- (void)setPlaybackEnabled:(BOOL)playbackEnabled
{
    _playbackEnabled = playbackEnabled;
    self.songModeButton.state = playbackEnabled
        ? NSControlStateValueOn : NSControlStateValueOff;
    self.songModeButton.title = playbackEnabled
        ? @"SONG: ON" : @"SONG: OFF";
    self.launchQuantizationPopup.enabled = playbackEnabled
        && self.playbackLocked;
    [self updateRowToolAvailability];
    [self.songModeButton setNeedsDisplay:YES];
}

- (void)toggleSongMode:(NSButton*)sender
{
    self.playbackEnabled = sender.state == NSControlStateValueOn;
    if (self.modeChangeHandler)
        self.modeChangeHandler(self.playbackEnabled);
}

- (void)toggleSongLoop:(NSButton*)sender
{
    self.arrangementLoops = sender.state == NSControlStateValueOn;
    self.songLoopButton.title = self.arrangementLoops
        ? @"LOOP: ON" : @"LOOP: OFF";
    [self.songLoopButton setNeedsDisplay:YES];
    if (self.loopChangeHandler)
        self.loopChangeHandler(self.arrangementLoops);
    else
        [self songDidChange];
}

- (void)queueSelectedRow:(id)sender
{
    (void)sender;
    const NSInteger row = self.tableView.selectedRow;
    if (row < 0 || !self.launchHandler) return;
    const NSInteger quantization = std::clamp<NSInteger>(
        self.launchQuantizationPopup.indexOfSelectedItem, 0, 3);
    self.launchHandler(static_cast<NSUInteger>(row), quantization);
}

- (void)setPlaybackRow:(NSUInteger)row valid:(BOOL)valid
{
    if (self.currentPlaybackRow == row
        && self.currentPlaybackRowValid == valid) return;
    self.currentPlaybackRow = row;
    self.currentPlaybackRowValid = valid;
    [self.tableView reloadData];
}

- (void)setPendingPlaybackRow:(NSUInteger)row valid:(BOOL)valid
{
    [self setPendingPlaybackRow:row valid:valid
        quantization:self.launchQuantizationPopup.indexOfSelectedItem];
}

- (void)setPendingPlaybackRow:(NSUInteger)row valid:(BOOL)valid
    quantization:(NSInteger)quantization
{
    quantization = std::clamp<NSInteger>(quantization, 0, 3);
    if (self.pendingPlaybackRow == row
        && self.pendingPlaybackRowValid == valid
        && self.pendingPlaybackQuantization == quantization) return;
    self.pendingPlaybackRow = row;
    self.pendingPlaybackRowValid = valid;
    self.pendingPlaybackQuantization = quantization;
    if (valid) {
        static NSArray<NSString*>* titles = nil;
        static dispatch_once_t onceToken;
        dispatch_once(&onceToken, ^{
            titles = @[ @"NEXT TICK", @"NEXT BEAT", @"END OF PASS",
                @"END OF ROW" ];
        });
        self.queueStatusLabel.stringValue = [NSString stringWithFormat:
            @"QUEUED ROW %02lu · %@", static_cast<unsigned long>(row + 1u),
            titles[(NSUInteger)quantization]];
        self.queueStatusLabel.textColor =
            S3GTrackerThemeColor(S3GTrackerThemeRole::Warning);
        self.queueStatusLabel.accessibilityValue =
            self.queueStatusLabel.stringValue;
    } else {
        self.queueStatusLabel.stringValue = @"QUEUE —";
        self.queueStatusLabel.textColor = s3gSongColor(0x737879);
        self.queueStatusLabel.accessibilityValue = @"No queued Song row";
    }
    [self.tableView reloadData];
}

- (void)setPlaybackLocked:(BOOL)locked
{
    if (self.playbackLocked == locked) return;
    _playbackLocked = locked;
    // Arrangement edits stay available while REAPER is running. The plug-in
    // coordinator defers their playback-runtime publication until transport
    // stops, so editing cannot reset the currently sounding Song row.
    self.songModeButton.enabled = YES;
    self.songLoopButton.enabled = YES;
    self.launchQuantizationPopup.enabled = locked && self.playbackEnabled;
    [self updateRowToolAvailability];
    [self.tableView reloadData];
}

- (void)setAvailablePatternIds:(NSArray<NSString*>*)patternIds
    activePatternId:(NSString*)activePatternId
{
    [self setAvailablePatternIds:patternIds patternNames:@[]
        patternLengths:@[] patternLaneCounts:@[]
        activePatternId:activePatternId];
}

- (void)setAvailablePatternIds:(NSArray<NSString*>*)patternIds
    patternNames:(NSArray<NSString*>*)patternNames
    activePatternId:(NSString*)activePatternId
{
    [self setAvailablePatternIds:patternIds patternNames:patternNames
        patternLengths:@[] patternLaneCounts:@[]
        activePatternId:activePatternId];
}

- (void)setAvailablePatternIds:(NSArray<NSString*>*)patternIds
    patternNames:(NSArray<NSString*>*)patternNames
    patternLengths:(NSArray<NSNumber*>*)patternLengths
    activePatternId:(NSString*)activePatternId
{
    [self setAvailablePatternIds:patternIds patternNames:patternNames
        patternLengths:patternLengths patternLaneCounts:@[]
        activePatternId:activePatternId];
}

- (void)setAvailablePatternIds:(NSArray<NSString*>*)patternIds
    patternNames:(NSArray<NSString*>*)patternNames
    patternLengths:(NSArray<NSNumber*>*)patternLengths
    patternLaneCounts:(NSArray<NSNumber*>*)patternLaneCounts
    activePatternId:(NSString*)activePatternId
{
    NSMutableArray<NSString*>* available = [[NSMutableArray alloc] init];
    NSMutableArray<NSString*>* names = [[NSMutableArray alloc] init];
    NSMutableArray<NSNumber*>* lengths = [[NSMutableArray alloc] init];
    NSMutableArray<NSNumber*>* laneCounts = [[NSMutableArray alloc] init];
    [patternIds enumerateObjectsUsingBlock:
        ^(NSString* patternId, NSUInteger index, BOOL* stop) {
        (void)stop;
        if (![patternId isKindOfClass:NSString.class]
            || patternId.length == 0u
            || [available containsObject:patternId]) return;
        [available addObject:patternId.copy];
        NSString* name = index < patternNames.count
            && [patternNames[index] isKindOfClass:NSString.class]
            ? patternNames[index] : @"";
        [names addObject:name.copy];
        NSNumber* count = index < patternLengths.count
            && [patternLengths[index] isKindOfClass:NSNumber.class]
            ? patternLengths[index] : nil;
        [lengths addObject:@(s3gClampInteger(
            count ? count.integerValue : 16,
            1, static_cast<NSInteger>(
                s3g::tracker::kMaximumSongPatternRows)))];
        NSNumber* laneCount = index < patternLaneCounts.count
            && [patternLaneCounts[index] isKindOfClass:NSNumber.class]
            ? patternLaneCounts[index] : nil;
        [laneCounts addObject:@(s3gClampInteger(
            laneCount ? laneCount.integerValue : 32, 0, 32))];
    }];
    if (available.count == 0u) {
        [available addObject:@"A01"];
        [names addObject:@""];
        [lengths addObject:@16];
        [laneCounts addObject:@32];
    }
    _availablePatternIds = available.copy;
    _availablePatternNames = names.copy;
    _availablePatternLengths = lengths.copy;
    _availablePatternLaneCounts = laneCounts.copy;
    _activePatternId = [available containsObject:activePatternId]
        ? activePatternId.copy : available.firstObject;
    for (S3GTrackerSongRow* row in self.rows)
        [self removeUnavailableMutesFromRow:row];
    [self.tableView reloadData];
}

- (void)setTimingWarpLibrary:
    (const s3g::tracker::TimingWarpLibrary&)library
{
    NSMutableArray<NSNumber*>* slots = [[NSMutableArray alloc] init];
    NSMutableArray<NSString*>* titles = [[NSMutableArray alloc] init];
    [slots addObject:@0];
    [titles addObject:@"OFF"];
    for (std::size_t index = 0u;
         index < s3g::tracker::kMaximumTimingWarpLibraryEntries; ++index) {
        const auto* entry = library.entry(index);
        if (!entry) continue;
        NSString* name = [NSString stringWithUTF8String:entry->name.c_str()];
        [slots addObject:@(index + 1u)];
        [titles addObject:[NSString stringWithFormat:@"%02lu · %@",
            static_cast<unsigned long>(index + 1u),
            name.length > 0u ? name : @"UNTITLED"]];
    }
    _availableWarpSlots = slots.copy;
    _availableWarpTitles = titles.copy;
    [self.tableView reloadData];
}

- (s3g::tracker::SongArrangement)songArrangement
{
    s3g::tracker::SongArrangement arrangement;
    const char* name = self.arrangementName.UTF8String;
    arrangement.name = name ? name : "SONG";
    arrangement.loop = self.arrangementLoops;
    arrangement.ticksPerBeat = static_cast<uint32_t>(std::clamp<NSInteger>(
        self.arrangementTicksPerBeat, 1, 96));
    arrangement.rows.reserve(self.rows.count);
    for (S3GTrackerSongRow* source in self.rows) {
        s3g::tracker::SongRow row;
        const char* pattern = source.pattern.UTF8String;
        row.patternId = pattern ? pattern : "";
        row.durationTicks = static_cast<uint32_t>(std::clamp<NSInteger>(
            source.ticks, 1, 1 << 20));
        row.repeats = static_cast<uint32_t>(std::clamp<NSInteger>(
            source.repeats, 1, 65535));
        if (source.hasSwingOverride)
            row.swing = std::clamp(source.swing * 0.01, 0.5, 0.75);
        if (source.warpSlot > 0)
            row.timingWarpLibraryIndex
                = static_cast<std::size_t>(source.warpSlot - 1);
        if (source.hasPatternLoop) {
            row.patternLoop = s3g::tracker::SongPatternLoop {
                static_cast<uint32_t>(source.loopStart - 1),
                static_cast<uint32_t>(source.loopEnd),
            };
        }
        __block uint32_t muteMask = 0u;
        [source.mutedLanes enumerateIndexesUsingBlock:
            ^(NSUInteger index, BOOL* stop) {
                (void)stop;
                const NSUInteger laneCount = static_cast<NSUInteger>(
                    [self patternLaneCountForPatternId:source.pattern]);
                if (index < laneCount && index < 32u)
                    muteMask |= (1u << index);
            }];
        row.mutedTracks = muteMask;
        arrangement.rows.push_back(std::move(row));
    }
    return arrangement;
}

- (void)setSongArrangement:(const s3g::tracker::SongArrangement&)arrangement
{
    [self.rows removeAllObjects];
    self.arrangementName = [NSString stringWithUTF8String:
        arrangement.name.c_str()];
    self.arrangementLoops = arrangement.loop;
    self.songLoopButton.state = arrangement.loop
        ? NSControlStateValueOn : NSControlStateValueOff;
    self.songLoopButton.title = arrangement.loop
        ? @"LOOP SONG: ON" : @"LOOP SONG: OFF";
    self.arrangementTicksPerBeat = static_cast<NSInteger>(
        arrangement.ticksPerBeat == 0u ? 4u : arrangement.ticksPerBeat);
    for (const auto& source : arrangement.rows) {
        NSString* pattern = [NSString stringWithUTF8String:
            source.patternId.c_str()];
        S3GTrackerSongRow* row = [self newRowWithPattern:
            pattern ? pattern : @"A01"];
        row.ticks = static_cast<NSInteger>(source.durationTicks);
        row.repeats = static_cast<NSInteger>(source.repeats);
        row.swing = source.swing.value_or(0.56) * 100.0;
        row.hasSwingOverride = source.swing.has_value();
        row.warpSlot = source.timingWarpLibraryIndex
            ? static_cast<NSInteger>(*source.timingWarpLibraryIndex + 1u)
            : 0;
        row.hasPatternLoop = source.patternLoop.has_value();
        if (source.patternLoop) {
            row.loopStart = static_cast<NSInteger>(
                source.patternLoop->startRow + 1u);
            row.loopEnd = static_cast<NSInteger>(
                source.patternLoop->endRow);
        }
        [row.mutedLanes removeAllIndexes];
        for (NSUInteger lane = 0u; lane < 32u; ++lane) {
            if ((source.mutedTracks & (1u << lane)) != 0u)
                [row.mutedLanes addIndex:lane];
        }
        [self removeUnavailableMutesFromRow:row];
        [self.rows addObject:row];
    }
    [self.tableView reloadData];
    if (self.rows.count > 0u) {
        [self.tableView selectRowIndexes:[NSIndexSet indexSetWithIndex:0u]
            byExtendingSelection:NO];
    } else {
        [self.tableView deselectAll:nil];
    }
    [self updateRowToolAvailability];
    // Applying a project is presentation synchronization, not a user edit.
    // The coordinator publishes file loads and history restores exactly once.
}

@end
