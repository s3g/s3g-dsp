#import "s3g_tracker_controls.h"

#include "s3g_gui_layout.h"
#define S3G_COCOA_GUI_DRAWING_ONLY 1
#include "s3g_cocoa_gui.h"
#undef S3G_COCOA_GUI_DRAWING_ONLY

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace {

NSColor* literalColor(std::uint32_t rgb, CGFloat alpha)
{
    return [NSColor colorWithCalibratedRed:((rgb >> 16u) & 0xffu) / 255.0
                                     green:((rgb >> 8u) & 0xffu) / 255.0
                                      blue:(rgb & 0xffu) / 255.0
                                     alpha:alpha];
}

std::uint32_t interpolateRGB(std::uint32_t left, std::uint32_t right,
    double amount)
{
    const auto channel = [amount](std::uint32_t a, std::uint32_t b) {
        return static_cast<std::uint32_t>(std::lround(
            static_cast<double>(a)
            + (static_cast<double>(b) - static_cast<double>(a)) * amount));
    };
    const auto red = channel((left >> 16u) & 0xffu, (right >> 16u) & 0xffu);
    const auto green = channel((left >> 8u) & 0xffu, (right >> 8u) & 0xffu);
    const auto blue = channel(left & 0xffu, right & 0xffu);
    return (red << 16u) | (green << 8u) | blue;
}

std::uint32_t nightNeutral(std::uint8_t level)
{
    struct Stop { std::uint8_t level; std::uint32_t rgb; };
    constexpr std::array<Stop, 14u> stops {{
        { 0x00, 0x050505 }, { 0x0c, 0x090909 },
        { 0x13, 0x101010 }, { 0x1d, 0x181818 },
        { 0x24, 0x222222 }, { 0x30, 0x2d2d2d },
        { 0x40, 0x3b3b3b }, { 0x56, 0x505050 },
        { 0x70, 0x6a6a6a }, { 0x8f, 0x898989 },
        { 0xa8, 0xa5a5a5 }, { 0xb8, 0xbcbcbc },
        { 0xd0, 0xd4d4d4 }, { 0xff, 0xf2f2f0 },
    }};
    for (std::size_t index = 1u; index < stops.size(); ++index) {
        if (level > stops[index].level) continue;
        const auto& left = stops[index - 1u];
        const auto& right = stops[index];
        const double span = static_cast<double>(right.level - left.level);
        const double amount = span > 0.0
            ? static_cast<double>(level - left.level) / span : 0.0;
        return interpolateRGB(left.rgb, right.rgb, amount);
    }
    return stops.back().rgb;
}

} // namespace

std::uint32_t S3GTrackerThemeRGB(S3GTrackerThemeRole role)
{
    switch (role) {
    case S3GTrackerThemeRole::Canvas: return 0x060606;
    case S3GTrackerThemeRole::Workspace: return 0x0a0a0a;
    case S3GTrackerThemeRole::Panel: return 0x101010;
    case S3GTrackerThemeRole::Raised: return 0x181818;
    case S3GTrackerThemeRole::Control: return 0x262626;
    case S3GTrackerThemeRole::ControlHover: return 0x353535;
    case S3GTrackerThemeRole::Selection: return 0x424242;
    // Restored from the v8 tracker as grid-only semantic states. Keeping
    // these separate from Live/Selection prevents transport and native
    // controls from acquiring decorative tracker-cell color.
    case S3GTrackerThemeRole::GridPlayback: return 0x2e412e;
    case S3GTrackerThemeRole::GridPlaybackAccent: return 0x69826b;
    case S3GTrackerThemeRole::GridSelection: return 0x303854;
    case S3GTrackerThemeRole::GridCursor: return 0x4d4d6b;
    case S3GTrackerThemeRole::Grid: return 0x303030;
    case S3GTrackerThemeRole::Border: return 0x4c4c4c;
    case S3GTrackerThemeRole::BorderStrong: return 0x6a6a6a;
    case S3GTrackerThemeRole::TextPrimary: return 0xdededa;
    case S3GTrackerThemeRole::TextSecondary: return 0xbababa;
    case S3GTrackerThemeRole::TextMuted: return 0x878787;
    case S3GTrackerThemeRole::TextFaint: return 0x656565;
    case S3GTrackerThemeRole::Focus: return 0xc0c0bc;
    case S3GTrackerThemeRole::Note: return 0x85cbd3;
    case S3GTrackerThemeRole::Instrument: return 0xb5b5b1;
    case S3GTrackerThemeRole::Value: return 0xe8d47d;
    case S3GTrackerThemeRole::Live: return 0x7fd7e8;
    case S3GTrackerThemeRole::Success: return 0x72d68c;
    case S3GTrackerThemeRole::Warning: return 0xf0ad6d;
    case S3GTrackerThemeRole::Danger: return 0xf06a72;
    }
    return 0xff00ff;
}

NSColor* S3GTrackerThemeColor(S3GTrackerThemeRole role, CGFloat alpha)
{
    return literalColor(S3GTrackerThemeRGB(role), alpha);
}

NSColor* S3GTrackerColor(std::uint32_t rgb, CGFloat alpha)
{
    const auto red = static_cast<std::uint8_t>((rgb >> 16u) & 0xffu);
    const auto green = static_cast<std::uint8_t>((rgb >> 8u) & 0xffu);
    const auto blue = static_cast<std::uint8_t>(rgb & 0xffu);
    const auto low = std::min({ red, green, blue });
    const auto high = std::max({ red, green, blue });
    if (high - low <= 8u) {
        const auto level = static_cast<std::uint8_t>(
            (static_cast<unsigned>(red) + green + blue) / 3u);
        return literalColor(nightNeutral(level), alpha);
    }
    return literalColor(rgb, alpha);
}

NSFont* S3GTrackerFont(CGFloat size, NSFontWeight weight)
{
    // The first tracker pass inherited compact Max/JSUI point sizes. Scale all
    // retained and native controls at the shared boundary so every window uses
    // the same readable coding-font baseline without changing grid geometry.
    const CGFloat readableSize = std::max<CGFloat>(8.0,
        std::round(size * 1.16 * 2.0) / 2.0);
    NSString* name = weight >= NSFontWeightSemibold ? @"IBM Plex Mono SemiBold"
        : weight >= NSFontWeightMedium ? @"IBM Plex Mono Medium"
                                       : @"IBM Plex Mono";
    NSFont* font = [NSFont fontWithName:name size:readableSize];
    return font ? font : [NSFont monospacedSystemFontOfSize:readableSize
        weight:weight];
}

static CGFloat suiteTextOriginY(NSRect rect, NSString* text,
    NSDictionary* attributes)
{
    const NSSize size = [(text ? text : @"") sizeWithAttributes:attributes];
    return std::floor(NSMidY(rect) - size.height * 0.5);
}

void S3GTrackerStyleTextField(NSTextField* field, NSTextAlignment alignment)
{
    if (!field) return;
    field.font = S3GTrackerFont(10.0);
    field.alignment = alignment;
    field.bezeled = NO;
    field.bordered = NO;
    field.editable = YES;
    field.selectable = YES;
    field.drawsBackground = YES;
    field.backgroundColor = S3GTrackerThemeColor(S3GTrackerThemeRole::Control);
    field.textColor = S3GTrackerThemeColor(S3GTrackerThemeRole::TextPrimary);
    field.focusRingType = NSFocusRingTypeNone;
    field.wantsLayer = YES;
    field.layer.backgroundColor = S3GTrackerThemeColor(
        S3GTrackerThemeRole::Control).CGColor;
    field.layer.borderColor = S3GTrackerThemeColor(
        S3GTrackerThemeRole::Border).CGColor;
    field.layer.borderWidth = 1.0;
    field.layer.cornerRadius = 0.0;
}

void S3GTrackerStyleSuiteTextField(NSTextField* field,
    NSTextAlignment alignment)
{
    S3GTrackerStyleTextField(field, alignment);
    field.font = s3g::clap_gui::uiFont(10.0);
}

void S3GTrackerStyleTextEditor(NSTextField* field)
{
    NSText* editor = field.currentEditor;
    if (!editor || ![editor respondsToSelector:
            @selector(setSelectedTextAttributes:)])
        return;
    NSTextView* textView = (NSTextView*)editor;
    textView.selectedTextAttributes = @{
        NSBackgroundColorAttributeName: S3GTrackerThemeColor(
            S3GTrackerThemeRole::Focus, 0.35),
        NSForegroundColorAttributeName: S3GTrackerThemeColor(
            S3GTrackerThemeRole::TextPrimary),
    };
    textView.insertionPointColor = S3GTrackerThemeColor(
        S3GTrackerThemeRole::Live);
}

NSRect S3GTrackerExpandedCellEditorRect(NSRect cellRect,
    NSRect visibleRect, NSString* text, NSFont* font)
{
    if (NSWidth(visibleRect) <= 0.0 || !font) return cellRect;
    const CGFloat textWidth = std::ceil([(text ? text : @"")
        sizeWithAttributes:@{ NSFontAttributeName: font }].width);
    const CGFloat width = std::min(NSWidth(visibleRect),
        std::max(NSWidth(cellRect), textWidth + 22.0));
    NSRect result = cellRect;
    result.size.width = width;
    result.origin.x = std::clamp(NSMidX(cellRect) - width * 0.5,
        NSMinX(visibleRect), NSMaxX(visibleRect) - width);
    return result;
}

void S3GTrackerRestoreWindowFrame(NSWindow* window, NSString* autosaveName)
{
    if (!window || autosaveName.length == 0u) return;
    [window center];
    [window setFrameAutosaveName:autosaveName];

    const NSRect frame = window.frame;
    BOOL sufficientlyVisible = NO;
    for (NSScreen* screen in NSScreen.screens) {
        const NSRect intersection = NSIntersectionRect(frame,
            screen.visibleFrame);
        if (NSWidth(intersection) >= 120.0
            && NSHeight(intersection) >= 80.0) {
            sufficientlyVisible = YES;
            break;
        }
    }
    if (!sufficientlyVisible) {
        [window center];
        [window saveFrameUsingName:autosaveName];
    }
}

@interface S3GTrackerActionButton ()
@property(nonatomic, strong) NSTrackingArea* s3gTrackingArea;
@property(nonatomic) BOOL s3gHovered;
@end

void S3GTrackerDrawSuiteActionButton(NSRect bounds, NSString* title,
    BOOL enabled, BOOL pressed, BOOL hovered, BOOL live,
    BOOL positive, BOOL binaryOff, BOOL danger, BOOL neutralTitle)
{
    const NSRect rect = NSInsetRect(bounds, 0.5, 0.5);
    NSColor* fill = pressed
        ? s3g::clap_gui::color(0x414141)
        : positive ? S3GTrackerThemeColor(
            S3GTrackerThemeRole::Success, 0.20)
        : live ? S3GTrackerThemeColor(
            S3GTrackerThemeRole::Live, 0.16)
        : binaryOff ? S3GTrackerThemeColor(
            S3GTrackerThemeRole::Danger, 0.16)
        : hovered ? s3g::clap_gui::color(0x343434)
                  : s3g::clap_gui::color(0x292929);
    NSColor* border = !enabled
        ? s3g::clap_gui::color(0x383838)
        : danger ? S3GTrackerThemeColor(
            S3GTrackerThemeRole::Danger, 0.75)
        : positive ? S3GTrackerThemeColor(
            S3GTrackerThemeRole::Success)
        : live ? S3GTrackerThemeColor(S3GTrackerThemeRole::Live)
        : binaryOff ? S3GTrackerThemeColor(S3GTrackerThemeRole::Danger)
                    : s3g::clap_gui::color(0x777777);
    [fill setFill];
    NSRectFill(rect);
    [border setStroke];
    NSFrameRect(rect);
    NSDictionary* attrs = enabled
        ? neutralTitle ? s3g::clap_gui::softValueAttrs()
        : (positive || live || binaryOff) ? @{
            NSForegroundColorAttributeName:
                S3GTrackerThemeColor(positive
                    ? S3GTrackerThemeRole::Success
                    : binaryOff ? S3GTrackerThemeRole::Danger
                                : S3GTrackerThemeRole::Live),
            NSFontAttributeName: s3g::clap_gui::uiFont(10.0),
        } : s3g::clap_gui::softValueAttrs()
        : @{
            NSForegroundColorAttributeName: s3g::clap_gui::color(0x656565),
            NSFontAttributeName: s3g::clap_gui::uiFont(10.0),
        };
    NSString* displayTitle = (title ? title : @"").uppercaseString;
    const NSSize size = [displayTitle sizeWithAttributes:attrs];
    [displayTitle drawAtPoint:NSMakePoint(
        NSMidX(rect) - size.width * 0.5,
        NSMidY(rect) - size.height * 0.5 - 0.5)
        withAttributes:attrs];
}

@implementation S3GTrackerActionButton

- (instancetype)initWithFrame:(NSRect)frameRect
{
    self = [super initWithFrame:frameRect];
    if (self) {
        self.bordered = NO;
        self.focusRingType = NSFocusRingTypeNone;
        self.buttonType = NSButtonTypeMomentaryChange;
        self.font = S3GTrackerFont(9.5);
    }
    return self;
}

- (NSSize)intrinsicContentSize
{
    const NSSize titleSize = [self.title sizeWithAttributes:@{
        NSFontAttributeName: S3GTrackerFont(9.5, NSFontWeightMedium),
    }];
    return NSMakeSize(std::ceil(titleSize.width) + 18.0, 26.0);
}

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    if (self.s3gTrackingArea)
        [self removeTrackingArea:self.s3gTrackingArea];
    self.s3gTrackingArea = [[NSTrackingArea alloc] initWithRect:self.bounds
        options:(NSTrackingMouseEnteredAndExited
            | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect)
        owner:self userInfo:nil];
    [self addTrackingArea:self.s3gTrackingArea];
}

- (void)mouseEntered:(NSEvent*)event
{
    (void)event;
    self.s3gHovered = YES;
    [self setNeedsDisplay:YES];
}

- (void)mouseExited:(NSEvent*)event
{
    (void)event;
    self.s3gHovered = NO;
    [self setNeedsDisplay:YES];
}

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
    const bool enabled = self.enabled;
    const bool live = self.tag == 1
        && self.state == NSControlStateValueOn;
    const bool success = self.tag == 3
        && self.state == NSControlStateValueOn;
    const bool binaryStatus =
        [self.identifier isEqualToString:@"binary-status"];
    const bool binaryOn = binaryStatus
        && self.state == NSControlStateValueOn;
    const bool binaryOff = binaryStatus && !binaryOn;
    const bool positive = success || binaryOn;
    const bool danger = self.tag == 2
        || [self.identifier isEqualToString:@"danger"];
    const bool pressed = self.highlighted;
    const NSRect rect = NSInsetRect(self.bounds, 0.5, 0.5);
    if (self.s3gUsesSuiteStyle) {
        S3GTrackerDrawSuiteActionButton(self.bounds, self.title,
            enabled, pressed, self.s3gHovered, live, positive, binaryOff,
            danger, self.s3gUsesNeutralTitle);
        return;
    }
    NSColor* fill = pressed
        ? S3GTrackerThemeColor(S3GTrackerThemeRole::Selection)
        : positive ? S3GTrackerThemeColor(S3GTrackerThemeRole::Success, 0.16)
        : live ? S3GTrackerThemeColor(S3GTrackerThemeRole::Live, 0.16)
        : binaryOff ? S3GTrackerThemeColor(S3GTrackerThemeRole::Danger, 0.16)
        : self.s3gHovered
            ? S3GTrackerThemeColor(S3GTrackerThemeRole::ControlHover)
            : S3GTrackerThemeColor(S3GTrackerThemeRole::Control);
    NSColor* border = !enabled
        ? S3GTrackerThemeColor(S3GTrackerThemeRole::Grid)
        : positive ? S3GTrackerThemeColor(S3GTrackerThemeRole::Success)
        : live ? S3GTrackerThemeColor(S3GTrackerThemeRole::Live)
        : binaryOff ? S3GTrackerThemeColor(S3GTrackerThemeRole::Danger)
        : danger ? S3GTrackerThemeColor(S3GTrackerThemeRole::Danger, 0.75)
        : S3GTrackerThemeColor(S3GTrackerThemeRole::BorderStrong);
    [fill setFill];
    NSRectFill(rect);
    [border setStroke];
    NSFrameRect(rect);
    if (live || positive || binaryOff) {
        [S3GTrackerThemeColor(positive ? S3GTrackerThemeRole::Success
            : binaryOff ? S3GTrackerThemeRole::Danger
                        : S3GTrackerThemeRole::Live) setFill];
        NSRectFill(NSMakeRect(rect.origin.x + 1.0,
            NSMaxY(rect) - 3.0, rect.size.width - 2.0, 2.0));
    }
    if (self.window.firstResponder == self) {
        [S3GTrackerThemeColor(S3GTrackerThemeRole::Focus) setStroke];
        NSFrameRect(NSInsetRect(rect, 2.0, 2.0));
    }
    NSDictionary* attributes = @{
        NSForegroundColorAttributeName: enabled
            ? positive ? S3GTrackerThemeColor(S3GTrackerThemeRole::Success)
            : binaryOff ? S3GTrackerThemeColor(S3GTrackerThemeRole::Danger)
            : live ? S3GTrackerThemeColor(S3GTrackerThemeRole::Live)
                   : S3GTrackerThemeColor(S3GTrackerThemeRole::TextSecondary)
            : S3GTrackerThemeColor(S3GTrackerThemeRole::TextFaint),
        NSFontAttributeName: S3GTrackerFont(9.5, NSFontWeightMedium),
    };
    NSString* uppercaseTitle = self.title.uppercaseString;
    NSString* title = uppercaseTitle ? uppercaseTitle : @"";
    const NSSize size = [title sizeWithAttributes:attributes];
    [title drawAtPoint:NSMakePoint(
        rect.origin.x + (rect.size.width - size.width) * 0.5,
        rect.origin.y + (rect.size.height - size.height) * 0.5 - 0.5)
        withAttributes:attributes];
}

@end

@interface S3GTrackerSwingSlider ()
@property(nonatomic) BOOL s3gDragging;
@property(nonatomic) BOOL s3gGestureChanged;
@property(nonatomic) CGFloat s3gScrollAccumulator;
@end

@implementation S3GTrackerSwingSlider

- (NSRect)sliderTrackRect
{
    const CGFloat labelWidth = self.s3gLabel.length > 0u ? 20.0 : 0.0;
    const CGFloat valueWidth = 34.0;
    return NSMakeRect(2.0 + labelWidth, 9.0,
        std::max<CGFloat>(18.0,
            NSWidth(self.bounds) - valueWidth - labelWidth - 8.0), 9.0);
}

- (NSRect)valueTextRect
{
    const CGFloat valueWidth = 34.0;
    return NSMakeRect(NSWidth(self.bounds) - valueWidth, 6.0,
        valueWidth - 2.0, 15.0);
}

- (void)setS3gLabel:(NSString*)s3gLabel
{
    _s3gLabel = [s3gLabel copy];
    [self setNeedsDisplay:YES];
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
    NSString* label = self.s3gLabel != nil ? self.s3gLabel : @"";
    s3g::clap_gui::drawSlider(label, self.stringValue,
        normalized, NSMinY(track) - 1.0,
        s3g::clap_gui::softLabelAttrs(), valueAttrs, style,
        -100.0, 2.0, NSMinX(value), NSWidth(track), NSWidth(value));
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
    // Let NSView enforce the slider's actual frame. Returning self
    // unconditionally lets this slider steal clicks from preceding siblings.
    return self.enabled && !self.hidden ? [super hitTest:point] : nil;
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
    if (delta == 0.0) return;
    const NSInteger steps = static_cast<NSInteger>(std::abs(delta));
    const CGFloat direction = delta > 0.0 ? 1.0 : -1.0;
    for (NSInteger step = 0; step < steps; ++step) {
        if (![self adjustByScrollDelta:direction
                modifierFlags:event.modifierFlags]) break;
    }
}

@end

@implementation S3GTrackerDragNumberField

- (instancetype)initWithFrame:(NSRect)frameRect
{
    self = [super initWithFrame:frameRect];
    if (self) {
        _s3gMinimumValue = 0.0;
        _s3gMaximumValue = 1.0;
        _s3gDragIncrement = 0.01;
        _s3gFractionDigits = 2u;
    }
    return self;
}

- (double)s3gValueFromStart:(double)start
    verticalDelta:(CGFloat)verticalDelta
    modifierFlags:(NSEventModifierFlags)modifierFlags
{
    double sensitivity = self.s3gDragIncrement;
    if ((modifierFlags & NSEventModifierFlagShift) != 0u)
        sensitivity *= 4.0;
    if ((modifierFlags & NSEventModifierFlagOption) != 0u)
        sensitivity *= 0.1;
    const double raw = start + static_cast<double>(verticalDelta)
        * sensitivity;
    const double scale = std::pow(10.0,
        static_cast<double>(std::min<NSUInteger>(self.s3gFractionDigits, 9u)));
    const double rounded = scale > 0.0
        ? std::round(raw * scale) / scale : raw;
    return std::clamp(rounded,
        std::min(self.s3gMinimumValue, self.s3gMaximumValue),
        std::max(self.s3gMinimumValue, self.s3gMaximumValue));
}

- (void)resetCursorRects
{
    [super resetCursorRects];
    if (self.enabled)
        [self addCursorRect:self.bounds cursor:NSCursor.resizeUpDownCursor];
}

- (void)mouseDown:(NSEvent*)event
{
    if (!self.enabled || event.clickCount >= 2 || !self.window) {
        [super mouseDown:event];
        return;
    }

    const CGFloat startY = event.locationInWindow.y;
    const double startValue = self.doubleValue;
    double lastValue = startValue;
    BOOL dragged = NO;
    const NSEventMask mask = NSEventMaskLeftMouseDragged
        | NSEventMaskLeftMouseUp;
    for (;;) {
        NSEvent* next = [self.window nextEventMatchingMask:mask
            untilDate:NSDate.distantFuture
            inMode:NSEventTrackingRunLoopMode dequeue:YES];
        if (!next || next.type == NSEventTypeLeftMouseUp) break;
        const CGFloat delta = next.locationInWindow.y - startY;
        if (!dragged && std::abs(delta) < 1.5) continue;
        dragged = YES;
        const double value = [self s3gValueFromStart:startValue
            verticalDelta:delta modifierFlags:next.modifierFlags];
        if (value == lastValue) continue;
        lastValue = value;
        self.doubleValue = value;
        [self sendAction:self.action to:self.target];
    }

    if (!dragged) {
        [self.window makeFirstResponder:self];
        [self selectText:nil];
    }
}

@end

@implementation S3GTrackerProcessorSliderField

- (NSRect)sliderTrackRect
{
    const auto& metrics = s3g::gui_layout::kStandardMetrics;
    const double panelWidth = static_cast<double>(NSWidth(self.bounds))
        + metrics.controlInset + metrics.panelRightInset;
    return NSMakeRect(0.0, 9.0, static_cast<CGFloat>(
        s3g::gui_layout::processorTrackWidth(panelWidth)), 9.0);
}

- (NSRect)valueTextRect
{
    const CGFloat valueWidth = static_cast<CGFloat>(
        s3g::gui_layout::kStandardMetrics.processorValueWidth);
    return NSMakeRect(NSWidth(self.bounds) - valueWidth, 6.0,
        valueWidth, 15.0);
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    const NSRect track = [self sliderTrackRect];
    const double minimum = std::min(self.s3gMinimumValue,
        self.s3gMaximumValue);
    const double maximum = std::max(self.s3gMinimumValue,
        self.s3gMaximumValue);
    const CGFloat normalized = maximum > minimum
        ? static_cast<CGFloat>(std::clamp(
            (self.doubleValue - minimum) / (maximum - minimum), 0.0, 1.0))
        : 0.0;
    auto style = s3g::clap_gui::softTextStyle();
    NSDictionary* valueAttrs = s3g::clap_gui::softValueAttrs();
    if (!self.enabled) {
        style.fill = s3g::clap_gui::color(0x333333);
        style.text = s3g::clap_gui::color(0x656565);
        valueAttrs = s3g::clap_gui::textAttrs(
            s3g::clap_gui::color(0x656565), 10.0);
    }
    const NSRect value = [self valueTextRect];
    s3g::clap_gui::drawSlider(@"", self.stringValue, normalized, 8.0,
        s3g::clap_gui::softLabelAttrs(), valueAttrs, style,
        -100.0, NSMinX(track), NSMinX(value), NSWidth(track),
        NSWidth(value));
}

- (void)resetCursorRects
{
    if (!self.enabled) return;
    [self addCursorRect:[self sliderTrackRect]
        cursor:NSCursor.resizeLeftRightCursor];
}

- (BOOL)acceptsFirstResponder { return NO; }

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint initialPoint = [self convertPoint:event.locationInWindow
        fromView:nil];
    if (!self.enabled || !self.window) return;
    const NSRect track = [self sliderTrackRect];
    if (!NSPointInRect(initialPoint, NSInsetRect(track, 0.0, -7.0))) return;
    const double minimum = std::min(self.s3gMinimumValue,
        self.s3gMaximumValue);
    const double maximum = std::max(self.s3gMinimumValue,
        self.s3gMaximumValue);
    const double scale = std::pow(10.0, static_cast<double>(
        std::min<NSUInteger>(self.s3gFractionDigits, 9u)));
    const auto applyEvent = ^(NSEvent* trackedEvent) {
        const NSPoint point = [self convertPoint:trackedEvent.locationInWindow
            fromView:nil];
        const double normalized = std::clamp(static_cast<double>(
            (point.x - NSMinX(track)) / std::max<CGFloat>(1.0,
                NSWidth(track))), 0.0, 1.0);
        double value = minimum + normalized * (maximum - minimum);
        if (scale > 0.0) value = std::round(value * scale) / scale;
        if (value == self.doubleValue) return;
        self.doubleValue = value;
        [self setNeedsDisplay:YES];
        [self sendAction:self.action to:self.target];
    };
    applyEvent(event);
    const NSEventMask mask = NSEventMaskLeftMouseDragged
        | NSEventMaskLeftMouseUp;
    for (;;) {
        NSEvent* next = [self.window nextEventMatchingMask:mask
            untilDate:NSDate.distantFuture
            inMode:NSEventTrackingRunLoopMode dequeue:YES];
        if (!next || next.type == NSEventTypeLeftMouseUp) break;
        applyEvent(next);
    }
}

@end

void S3GTrackerConfigureProcessorSlider(
    S3GTrackerProcessorSliderField* slider,
    double minimum, double maximum, NSUInteger fractionDigits,
    id target, SEL action)
{
    if (!slider) return;
    S3GTrackerStyleSuiteTextField(slider, NSTextAlignmentRight);
    slider.editable = NO;
    slider.selectable = NO;
    slider.drawsBackground = NO;
    slider.backgroundColor = NSColor.clearColor;
    slider.layer.backgroundColor = NSColor.clearColor.CGColor;
    slider.layer.borderWidth = 0.0;
    slider.s3gMinimumValue = minimum;
    slider.s3gMaximumValue = maximum;
    slider.s3gFractionDigits = fractionDigits;
    const double unit = std::pow(10.0, -static_cast<double>(
        std::min<NSUInteger>(fractionDigits, 9u)));
    slider.s3gDragIncrement = std::max(unit,
        std::abs(maximum - minimum) / 240.0);
    slider.target = target;
    slider.action = action;
    slider.accessibilityHelp =
        @"Click or drag the standard s3g-dsp slider track; the value at right is a readout.";
}

@class S3GTrackerPopupButton;

@interface S3GTrackerCanvasMenuOverlay : NSView
@property(nonatomic, weak) S3GTrackerPopupButton* popup;
@property(nonatomic) NSRect menuRect;
@property(nonatomic) NSUInteger columns;
@property(nonatomic) NSInteger hoverIndex;
@property(nonatomic, strong) NSTrackingArea* s3gTrackingArea;
@end

@interface S3GTrackerPopupButton ()
@property(nonatomic, strong) NSTrackingArea* s3gTrackingArea;
@property(nonatomic) BOOL s3gHovered;
@property(nonatomic, strong) S3GTrackerCanvasMenuOverlay* s3gMenuOverlay;
- (void)s3gOpenCanvasMenu;
- (void)s3gDismissCanvasMenu;
@end

@implementation S3GTrackerCanvasMenuOverlay

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event
{
    (void)event;
    return YES;
}

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    if (self.s3gTrackingArea)
        [self removeTrackingArea:self.s3gTrackingArea];
    self.s3gTrackingArea = [[NSTrackingArea alloc] initWithRect:self.bounds
        options:(NSTrackingMouseMoved | NSTrackingActiveInKeyWindow
            | NSTrackingInVisibleRect)
        owner:self userInfo:nil];
    [self addTrackingArea:self.s3gTrackingArea];
}

- (NSInteger)itemIndexAtPoint:(NSPoint)point
{
    const auto count = static_cast<uint32_t>(
        std::max<NSInteger>(0, self.popup.numberOfItems));
    return s3g::clap_gui::multiColumnDropdownHitIndex(point,
        self.menuRect, 21.0, count,
        static_cast<uint32_t>(std::max<NSUInteger>(1u, self.columns)));
}

- (void)mouseMoved:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:event.locationInWindow
        fromView:nil];
    const NSInteger hover = [self itemIndexAtPoint:point];
    if (hover == self.hoverIndex) return;
    self.hoverIndex = hover;
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:event.locationInWindow
        fromView:nil];
    const NSInteger index = [self itemIndexAtPoint:point];
    S3GTrackerPopupButton* popup = self.popup;
    if (index < 0 || index >= popup.numberOfItems) {
        [popup s3gDismissCanvasMenu];
        return;
    }
    NSMenuItem* item = [popup itemAtIndex:index];
    if (!item.enabled || item.separatorItem) return;
    [popup selectItemAtIndex:index];
    [popup s3gDismissCanvasMenu];
    [popup sendAction:popup.action to:popup.target];
}

- (void)keyDown:(NSEvent*)event
{
    if (event.keyCode == 53u) {
        [self.popup s3gDismissCanvasMenu];
        return;
    }
    [super keyDown:event];
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    S3GTrackerPopupButton* popup = self.popup;
    const uint32_t count = static_cast<uint32_t>(
        std::max<NSInteger>(0, popup.numberOfItems));
    if (!popup || count == 0u) return;
    const auto style = s3g::clap_gui::softTextStyle();
    NSDictionary* attrs = s3g::clap_gui::softValueAttrs();
    const uint32_t columns = static_cast<uint32_t>(
        std::max<NSUInteger>(1u, self.columns));
    std::vector<NSString*> items;
    items.reserve(count);
    for (uint32_t index = 0u; index < count; ++index) {
        NSString* title = [popup itemAtIndex:index].title;
        items.push_back(title ? title : @"");
    }
    s3g::clap_gui::drawMultiColumnDropdownMenu(self.menuRect, 21.0,
        items.data(), count, columns,
        static_cast<int>(popup.indexOfSelectedItem),
        static_cast<int>(self.hoverIndex), attrs, style);
}

@end

@implementation S3GTrackerPopupButton

- (instancetype)initWithFrame:(NSRect)frameRect pullsDown:(BOOL)flag
{
    self = [super initWithFrame:frameRect pullsDown:flag];
    if (self) {
        self.bordered = NO;
        self.focusRingType = NSFocusRingTypeNone;
        self.font = S3GTrackerFont(10.0);
        self.menu.font = self.font;
    }
    return self;
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event
{
    (void)event;
    return self.enabled;
}

- (void)setS3gUsesCanvasMenu:(BOOL)usesCanvasMenu
{
    if (_s3gUsesCanvasMenu == usesCanvasMenu) return;
    _s3gUsesCanvasMenu = usesCanvasMenu;
    self.font = usesCanvasMenu ? s3g::clap_gui::uiFont(10.0)
                              : S3GTrackerFont(10.0);
    self.menu.font = self.font;
    if (!usesCanvasMenu) [self s3gDismissCanvasMenu];
    [self setNeedsDisplay:YES];
}

- (void)s3gOpenCanvasMenu
{
    if (!self.s3gUsesCanvasMenu || !self.enabled
        || self.numberOfItems <= 0 || !self.window.contentView) return;
    if (self.s3gMenuOverlay) {
        [self s3gDismissCanvasMenu];
        return;
    }
    NSView* root = self.window.contentView;
    const NSRect source = [root convertRect:self.bounds fromView:self];
    constexpr CGFloat itemHeight = 21.0;
    constexpr CGFloat outerInset = 8.0;
    const CGFloat availableHeight = std::max<CGFloat>(itemHeight,
        NSHeight(root.bounds) - outerInset * 2.0);
    const NSUInteger maximumRows = std::max<NSUInteger>(1u,
        static_cast<NSUInteger>(std::floor(
            availableHeight / itemHeight)));
    const NSUInteger count = static_cast<NSUInteger>(self.numberOfItems);
    const NSUInteger columns = std::max<NSUInteger>(1u,
        (count + maximumRows - 1u) / maximumRows);
    const NSUInteger rows = (count + columns - 1u) / columns;
    const CGFloat menuHeight = itemHeight * static_cast<CGFloat>(rows);
    const CGFloat naturalColumnWidth = std::max<CGFloat>(
        NSWidth(source), 132.0);
    const CGFloat menuWidth = std::min<CGFloat>(
        naturalColumnWidth * static_cast<CGFloat>(columns),
        std::max<CGFloat>(naturalColumnWidth,
            NSWidth(root.bounds) - outerInset * 2.0));
    CGFloat x = std::clamp(NSMinX(source), outerInset,
        std::max<CGFloat>(outerInset,
            NSWidth(root.bounds) - menuWidth - outerInset));
    CGFloat y = NSMaxY(source) + 2.0;
    if (y + menuHeight > NSHeight(root.bounds) - outerInset)
        y = NSMinY(source) - menuHeight - 2.0;
    y = std::clamp(y, outerInset,
        std::max<CGFloat>(outerInset,
            NSHeight(root.bounds) - menuHeight - outerInset));

    S3GTrackerCanvasMenuOverlay* overlay =
        [[S3GTrackerCanvasMenuOverlay alloc] initWithFrame:root.bounds];
    overlay.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    overlay.popup = self;
    overlay.menuRect = NSMakeRect(x, y, menuWidth, menuHeight);
    overlay.columns = columns;
    overlay.hoverIndex = -1;
    self.s3gMenuOverlay = overlay;
    [root addSubview:overlay positioned:NSWindowAbove relativeTo:nil];
    [self.window makeFirstResponder:overlay];
    [self setNeedsDisplay:YES];
}

- (void)s3gDismissCanvasMenu
{
    if (!self.s3gMenuOverlay) return;
    [self.s3gMenuOverlay removeFromSuperview];
    self.s3gMenuOverlay = nil;
    [self setNeedsDisplay:YES];
}

- (void)viewWillMoveToWindow:(NSWindow*)newWindow
{
    if (!newWindow) [self s3gDismissCanvasMenu];
    [super viewWillMoveToWindow:newWindow];
}

- (NSSize)intrinsicContentSize
{
    NSString* title = self.s3gDisplayTitle.length > 0u
        ? self.s3gDisplayTitle : self.titleOfSelectedItem;
    if (!title) title = @"—";
    const NSSize titleSize = [title sizeWithAttributes:@{
        NSFontAttributeName: self.s3gUsesCanvasMenu
            ? s3g::clap_gui::uiFont(10.0) : S3GTrackerFont(10.0),
    }];
    return NSMakeSize(std::ceil(titleSize.width) + 34.0, 26.0);
}

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    if (self.s3gTrackingArea)
        [self removeTrackingArea:self.s3gTrackingArea];
    self.s3gTrackingArea = [[NSTrackingArea alloc] initWithRect:self.bounds
        options:(NSTrackingMouseEnteredAndExited
            | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect)
        owner:self userInfo:nil];
    [self addTrackingArea:self.s3gTrackingArea];
}

- (void)mouseEntered:(NSEvent*)event
{
    (void)event;
    self.s3gHovered = YES;
    [self setNeedsDisplay:YES];
}

- (void)mouseExited:(NSEvent*)event
{
    (void)event;
    self.s3gHovered = NO;
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    const bool enabled = self.enabled;
    const NSRect rect = NSInsetRect(self.bounds, 0.5, 0.5);
    if (self.s3gUsesCanvasMenu) {
        auto style = s3g::clap_gui::softTextStyle();
        NSDictionary* attrs = s3g::clap_gui::softValueAttrs();
        if (self.s3gMenuOverlay)
            style.strip = s3g::clap_gui::color(0x292929);
        if (!enabled) {
            style.grid = S3GTrackerThemeColor(S3GTrackerThemeRole::Grid);
            style.fill = S3GTrackerThemeColor(S3GTrackerThemeRole::Grid);
            attrs = s3g::clap_gui::textAttrs(
                S3GTrackerThemeColor(S3GTrackerThemeRole::Grid), 10.0);
        }
        NSString* value = self.s3gDisplayTitle.length > 0u
            ? self.s3gDisplayTitle : self.titleOfSelectedItem;
        // Use the very same renderer as the code-native Geometry/Burst menu.
        // y=1 makes its 15 px box and text baselines land at 0, 2 and 1 in
        // this control, exactly matching a processor menu at rowY.
        s3g::clap_gui::drawMenu(@"", value ? value : @"—", 1.0,
            attrs, attrs, style, -100.0, 0.0, NSWidth(self.bounds));
        return;
    }
    [S3GTrackerThemeColor(!enabled ? S3GTrackerThemeRole::Panel
        : self.highlighted ? S3GTrackerThemeRole::Selection
        : self.s3gHovered ? S3GTrackerThemeRole::ControlHover
                          : S3GTrackerThemeRole::Control) setFill];
    NSRectFill(rect);
    [S3GTrackerThemeColor(enabled ? S3GTrackerThemeRole::BorderStrong
                                  : S3GTrackerThemeRole::Grid) setStroke];
    NSFrameRect(rect);
    if (self.window.firstResponder == self) {
        [S3GTrackerThemeColor(S3GTrackerThemeRole::Focus) setStroke];
        NSFrameRect(NSInsetRect(rect, 2.0, 2.0));
    }

    NSFont* font = S3GTrackerFont(10.0, NSFontWeightMedium);
    self.menu.font = font;
    NSMutableParagraphStyle* paragraph = [[NSMutableParagraphStyle alloc]
        init];
    paragraph.lineBreakMode = NSLineBreakByTruncatingMiddle;
    paragraph.alignment = NSTextAlignmentCenter;
    const CGFloat lineHeight = std::ceil(
        font.ascender - font.descender + font.leading);
    paragraph.minimumLineHeight = lineHeight;
    paragraph.maximumLineHeight = lineHeight;
    NSDictionary* attributes = @{
        NSForegroundColorAttributeName: S3GTrackerThemeColor(enabled
            ? S3GTrackerThemeRole::TextSecondary
            : S3GTrackerThemeRole::TextFaint),
        NSFontAttributeName: font,
        NSParagraphStyleAttributeName: paragraph,
    };
    NSString* uppercaseTitle = self.titleOfSelectedItem.uppercaseString;
    NSString* title = uppercaseTitle ? uppercaseTitle : @"—";
    const NSRect textRect = NSMakeRect(rect.origin.x + 6.0,
        std::floor(NSMidY(rect) - lineHeight * 0.5),
        std::max<CGFloat>(0.0, rect.size.width - 27.0), lineHeight);
    [title drawInRect:textRect
        withAttributes:attributes];

    const CGFloat arrowX = NSMaxX(rect) - 13.0;
    const CGFloat arrowY = NSMidY(rect) - 1.5;
    NSBezierPath* arrow = [NSBezierPath bezierPath];
    [arrow moveToPoint:NSMakePoint(arrowX - 4.0, arrowY + 3.0)];
    [arrow lineToPoint:NSMakePoint(arrowX + 4.0, arrowY + 3.0)];
    [arrow lineToPoint:NSMakePoint(arrowX, arrowY - 2.0)];
    [arrow closePath];
    [S3GTrackerThemeColor(enabled ? S3GTrackerThemeRole::TextSecondary
                                  : S3GTrackerThemeRole::Grid) setFill];
    [arrow fill];
}

- (void)mouseDown:(NSEvent*)event
{
    if (!self.s3gUsesCanvasMenu) {
        [super mouseDown:event];
        return;
    }
    (void)event;
    [self s3gOpenCanvasMenu];
}

@end

@implementation S3GTrackerSuiteLabel

- (instancetype)initWithFrame:(NSRect)frameRect
{
    self = [super initWithFrame:frameRect];
    if (self) {
        self.editable = NO;
        self.selectable = NO;
        self.bezeled = NO;
        self.bordered = NO;
        self.drawsBackground = NO;
        self.focusRingType = NSFocusRingTypeNone;
        self.font = s3g::clap_gui::uiFont(10.0);
        self.textColor = s3g::clap_gui::color(0xa8a8a8);
        self.lineBreakMode = NSLineBreakByClipping;
        self.usesSingleLineMode = YES;
    }
    return self;
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    NSFont* font = self.font ? self.font : s3g::clap_gui::uiFont(10.0);
    NSColor* color = self.textColor
        ? self.textColor : s3g::clap_gui::color(0xa8a8a8);
    NSMutableParagraphStyle* paragraph = [[NSMutableParagraphStyle alloc]
        init];
    paragraph.alignment = self.alignment;
    paragraph.lineBreakMode = self.lineBreakMode;
    NSDictionary* attributes = @{
        NSForegroundColorAttributeName: color,
        NSFontAttributeName: font,
        NSParagraphStyleAttributeName: paragraph,
    };
    NSString* text = self.stringValue ? self.stringValue : @"";
    NSRect textRect = self.bounds;
    textRect.origin.y = suiteTextOriginY(self.bounds, text, attributes);
    textRect.size.height = [text sizeWithAttributes:attributes].height;
    [text drawInRect:textRect withAttributes:attributes];
}

@end

@implementation S3GTrackerFocusReleaseView

- (BOOL)acceptsFirstResponder { return YES; }

- (void)mouseDown:(NSEvent*)event
{
    (void)event;
    [self.window makeFirstResponder:self];
}

@end

@implementation S3GTrackerFocusReleaseStackView

- (BOOL)acceptsFirstResponder { return YES; }

- (void)mouseDown:(NSEvent*)event
{
    (void)event;
    [self.window makeFirstResponder:self];
}

@end

@implementation S3GTrackerPanelView

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    [S3GTrackerThemeColor(S3GTrackerThemeRole::Raised) setFill];
    NSRectFill(self.bounds);
    [S3GTrackerThemeColor(S3GTrackerThemeRole::Panel) setFill];
    NSRectFill(NSMakeRect(0.0, NSMaxY(self.bounds) - 21.0,
        NSWidth(self.bounds), 21.0));
    [S3GTrackerThemeColor(S3GTrackerThemeRole::Border) setStroke];
    NSFrameRect(NSInsetRect(self.bounds, 0.5, 0.5));
    [S3GTrackerThemeColor(S3GTrackerThemeRole::TextMuted) setFill];
    NSRectFill(NSMakeRect(0.0, NSMaxY(self.bounds) - 2.0,
        NSWidth(self.bounds), 2.0));
}

@end

@implementation S3GTrackerToolboxView

- (BOOL)isFlipped { return YES; }

- (void)setToolboxTitle:(NSString*)toolboxTitle
{
    _toolboxTitle = [toolboxTitle copy];
    [self setNeedsDisplay:YES];
}

- (void)setToolboxIndex:(NSInteger)toolboxIndex
{
    _toolboxIndex = toolboxIndex;
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    const CGFloat headerHeight = static_cast<CGFloat>(
        s3g::gui_layout::kStandardMetrics.headerHeight);
    const auto style = s3g::clap_gui::softTextStyle();
    NSString* title = self.toolboxTitle != nil ? self.toolboxTitle : @"";
    if (self.toolboxIndex > 0) {
        title = [NSString stringWithFormat:@"%ld  %@",
            static_cast<long>(self.toolboxIndex), title];
    }
    s3g::clap_gui::drawPanelFrame(0.0, 0.0,
        NSWidth(self.bounds), NSHeight(self.bounds), style);
    s3g::clap_gui::drawPanelHeader(title.uppercaseString, true,
        0.0, 0.0, NSWidth(self.bounds), headerHeight,
        s3g::clap_gui::softLabelAttrs(), style);
}

@end
