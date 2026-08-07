#import "s3g_tracker_controls.h"

#include <algorithm>
#include <array>
#include <cmath>

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
    const bool danger = self.tag == 2
        || [self.identifier isEqualToString:@"danger"];
    const bool pressed = self.highlighted;
    const NSRect rect = NSInsetRect(self.bounds, 0.5, 0.5);
    NSColor* fill = pressed
        ? S3GTrackerThemeColor(S3GTrackerThemeRole::Selection)
        : success ? S3GTrackerThemeColor(S3GTrackerThemeRole::Success, 0.16)
        : live ? S3GTrackerThemeColor(S3GTrackerThemeRole::Live, 0.16)
        : self.s3gHovered
            ? S3GTrackerThemeColor(S3GTrackerThemeRole::ControlHover)
            : S3GTrackerThemeColor(S3GTrackerThemeRole::Control);
    NSColor* border = !enabled
        ? S3GTrackerThemeColor(S3GTrackerThemeRole::Grid)
        : success ? S3GTrackerThemeColor(S3GTrackerThemeRole::Success)
        : live ? S3GTrackerThemeColor(S3GTrackerThemeRole::Live)
        : danger ? S3GTrackerThemeColor(S3GTrackerThemeRole::Danger, 0.75)
        : S3GTrackerThemeColor(S3GTrackerThemeRole::BorderStrong);
    [fill setFill];
    NSRectFill(rect);
    [border setStroke];
    NSFrameRect(rect);
    if (live || success) {
        [S3GTrackerThemeColor(success ? S3GTrackerThemeRole::Success
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
            ? success ? S3GTrackerThemeColor(S3GTrackerThemeRole::Success)
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

@implementation S3GTrackerPopupButton

- (instancetype)initWithFrame:(NSRect)frameRect pullsDown:(BOOL)flag
{
    self = [super initWithFrame:frameRect pullsDown:flag];
    if (self) {
        self.bordered = NO;
        self.focusRingType = NSFocusRingTypeNone;
        self.font = S3GTrackerFont(10.0);
    }
    return self;
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    const bool enabled = self.enabled;
    const NSRect rect = NSInsetRect(self.bounds, 0.5, 0.5);
    [S3GTrackerThemeColor(enabled ? S3GTrackerThemeRole::Control
                                  : S3GTrackerThemeRole::Panel) setFill];
    NSRectFill(rect);
    [S3GTrackerThemeColor(enabled ? S3GTrackerThemeRole::BorderStrong
                                  : S3GTrackerThemeRole::Grid) setStroke];
    NSFrameRect(rect);
    if (self.window.firstResponder == self) {
        [S3GTrackerThemeColor(S3GTrackerThemeRole::Focus) setStroke];
        NSFrameRect(NSInsetRect(rect, 2.0, 2.0));
    }

    NSMutableParagraphStyle* paragraph = [[NSMutableParagraphStyle alloc] init];
    paragraph.lineBreakMode = NSLineBreakByTruncatingMiddle;
    paragraph.alignment = NSTextAlignmentLeft;
    NSDictionary* attributes = @{
        NSForegroundColorAttributeName: S3GTrackerThemeColor(enabled
            ? S3GTrackerThemeRole::TextSecondary
            : S3GTrackerThemeRole::TextFaint),
        NSFontAttributeName: S3GTrackerFont(10.0),
        NSParagraphStyleAttributeName: paragraph,
    };
    NSString* uppercaseTitle = self.titleOfSelectedItem.uppercaseString;
    NSString* title = uppercaseTitle ? uppercaseTitle : @"—";
    [title drawInRect:NSMakeRect(rect.origin.x + 9.0,
        rect.origin.y + (rect.size.height - 14.0) * 0.5,
        std::max<CGFloat>(0.0, rect.size.width - 29.0), 14.0)
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
