#pragma once

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>

#include <clap/ext/gui.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include "s3g_gui_layout.h"
#include "s3g_math.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

namespace s3g::clap_gui {

inline bool sliderDoubleClickDefault(NSEvent* event,
                                     const clap_plugin_t* plugin,
                                     clap_id paramId,
                                     double* defaultValue)
{
    if (!event || [event clickCount] < 2 || !plugin || !defaultValue
        || paramId == CLAP_INVALID_ID || !plugin->get_extension) {
        return false;
    }
    const auto* params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    if (!params || !params->count || !params->get_info) return false;
    const uint32_t count = params->count(plugin);
    for (uint32_t index = 0u; index < count; ++index) {
        clap_param_info_t info {};
        if (params->get_info(plugin, index, &info) && info.id == paramId) {
            *defaultValue = info.default_value;
            return true;
        }
    }
    return false;
}

// Editors use a fixed-size drawing surface so visual geometry, hit testing,
// and native controls remain stable. This state adds a resizable viewport
// around that surface. Smaller viewports expose overflow with Cocoa scrollers;
// the drawing surface itself always remains at 1:1 scale.
struct ResponsiveViewport {
    void* container = nullptr;
    void* screenObserver = nullptr;
    uint32_t width = 0u;
    uint32_t height = 0u;
    uint32_t nativeWidth = 0u;
    uint32_t nativeHeight = 0u;
    uint32_t minimumWidth = 480u;
    uint32_t minimumHeight = 360u;
};

inline NSSize responsiveViewportSizeForScreen(uint32_t nativeWidth,
                                              uint32_t nativeHeight,
                                              uint32_t minimumWidth = 480u,
                                              uint32_t minimumHeight = 360u,
                                              NSScreen* screen = [NSScreen mainScreen])
{
    CGFloat width = static_cast<CGFloat>(nativeWidth);
    CGFloat height = static_cast<CGFloat>(nativeHeight);
    if (screen) {
        const NSRect visible = [screen visibleFrame];
        width = std::min(width, std::max(static_cast<CGFloat>(minimumWidth),
            std::floor(visible.size.width * 0.90)));
        height = std::min(height, std::max(static_cast<CGFloat>(minimumHeight),
            std::floor(visible.size.height * 0.82)));
    }
    return NSMakeSize(width, height);
}

inline void clampResponsiveViewportSize(const ResponsiveViewport& state,
                                        uint32_t& width,
                                        uint32_t& height)
{
    width = std::clamp(width, state.minimumWidth, state.nativeWidth);
    height = std::clamp(height, state.minimumHeight, state.nativeHeight);
}

inline bool createResponsiveViewport(ResponsiveViewport& state,
                                     NSView* content,
                                     uint32_t nativeWidth,
                                     uint32_t nativeHeight,
                                     uint32_t minimumWidth = 480u,
                                     uint32_t minimumHeight = 360u)
{
    if (!content) return false;
    if (state.container) return true;
    state.nativeWidth = nativeWidth;
    state.nativeHeight = nativeHeight;
    state.minimumWidth = std::min(minimumWidth, nativeWidth);
    state.minimumHeight = std::min(minimumHeight, nativeHeight);
    const NSSize size = responsiveViewportSizeForScreen(nativeWidth, nativeHeight,
        state.minimumWidth, state.minimumHeight);
    state.width = static_cast<uint32_t>(size.width);
    state.height = static_cast<uint32_t>(size.height);

    auto* scrollView = [[NSScrollView alloc] initWithFrame:NSMakeRect(
        0.0, 0.0, size.width, size.height)];
    if (!scrollView) return false;
    [scrollView setBorderType:NSNoBorder];
    [scrollView setDrawsBackground:NO];
    [scrollView setHasVerticalScroller:YES];
    [scrollView setHasHorizontalScroller:YES];
    [scrollView setAutohidesScrollers:YES];
    [scrollView setScrollerStyle:NSScrollerStyleOverlay];
    [scrollView setScrollerKnobStyle:NSScrollerKnobStyleLight];
    [scrollView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [scrollView setDocumentView:content];
    [[scrollView contentView] scrollToPoint:NSMakePoint(0.0, 0.0)];
    [scrollView reflectScrolledClipView:[scrollView contentView]];
    state.container = scrollView;
    return true;
}

inline void stopResponsiveScreenObservation(ResponsiveViewport& state)
{
    if (!state.screenObserver) return;
    [[NSNotificationCenter defaultCenter] removeObserver:static_cast<id>(state.screenObserver)];
    state.screenObserver = nullptr;
}

inline void destroyResponsiveViewport(ResponsiveViewport& state, void*& contentView)
{
    stopResponsiveScreenObservation(state);
    if (state.container) {
        auto* scrollView = static_cast<NSScrollView*>(state.container);
        [scrollView setDocumentView:nil];
        [scrollView removeFromSuperview];
        [scrollView release];
    }
    if (contentView) {
        auto* content = static_cast<NSView*>(contentView);
        [content removeFromSuperview];
        [content release];
    }
    state = {};
    contentView = nullptr;
}

inline bool getResponsiveViewportSize(const ResponsiveViewport& state,
                                      uint32_t nativeWidth,
                                      uint32_t nativeHeight,
                                      uint32_t* width,
                                      uint32_t* height,
                                      uint32_t minimumWidth = 480u,
                                      uint32_t minimumHeight = 360u)
{
    if (!width || !height) return false;
    if (state.width > 0u && state.height > 0u) {
        *width = state.width;
        *height = state.height;
    } else {
        const NSSize size = responsiveViewportSizeForScreen(
            nativeWidth, nativeHeight, minimumWidth, minimumHeight);
        *width = static_cast<uint32_t>(size.width);
        *height = static_cast<uint32_t>(size.height);
    }
    return true;
}

inline bool getResponsiveResizeHints(clap_gui_resize_hints_t* hints)
{
    if (!hints) return false;
    hints->can_resize_horizontally = true;
    hints->can_resize_vertically = true;
    hints->preserve_aspect_ratio = false;
    hints->aspect_ratio_width = 0u;
    hints->aspect_ratio_height = 0u;
    return true;
}

inline bool adjustResponsiveViewportSize(const ResponsiveViewport& state,
                                         uint32_t nativeWidth,
                                         uint32_t nativeHeight,
                                         uint32_t* width,
                                         uint32_t* height,
                                         uint32_t initialMinimumWidth = 480u,
                                         uint32_t initialMinimumHeight = 360u)
{
    if (!width || !height) return false;
    const uint32_t minWidth = state.nativeWidth > 0u
        ? std::min(state.minimumWidth, nativeWidth)
        : std::min(initialMinimumWidth, nativeWidth);
    const uint32_t minHeight = state.nativeHeight > 0u
        ? std::min(state.minimumHeight, nativeHeight)
        : std::min(initialMinimumHeight, nativeHeight);
    *width = std::clamp(*width, minWidth, nativeWidth);
    *height = std::clamp(*height, minHeight, nativeHeight);
    return true;
}

inline bool setResponsiveViewportSize(ResponsiveViewport& state,
                                      uint32_t width,
                                      uint32_t height)
{
    if (!state.container) return false;
    clampResponsiveViewportSize(state, width, height);
    state.width = width;
    state.height = height;
    [static_cast<NSView*>(state.container) setFrameSize:NSMakeSize(width, height)];
    return true;
}

inline void requestResponsiveViewportFit(ResponsiveViewport& state, const clap_host_t* host)
{
    if (!state.container || !host || !host->get_extension) return;
    NSView* container = static_cast<NSView*>(state.container);
    NSScreen* screen = [container window] ? [[container window] screen] : [NSScreen mainScreen];
    const NSSize fit = responsiveViewportSizeForScreen(state.nativeWidth, state.nativeHeight,
        state.minimumWidth, state.minimumHeight, screen);
    const uint32_t targetWidth = std::min(state.width, static_cast<uint32_t>(std::floor(fit.width)));
    const uint32_t targetHeight = std::min(state.height, static_cast<uint32_t>(std::floor(fit.height)));
    if (targetWidth >= state.width && targetHeight >= state.height) return;
    const auto* hostGui = static_cast<const clap_host_gui_t*>(host->get_extension(host, CLAP_EXT_GUI));
    if (hostGui && hostGui->request_resize) {
        hostGui->request_resize(host, targetWidth, targetHeight);
    }
}

inline bool setResponsiveViewportParent(ResponsiveViewport& state,
                                        NSView* parent,
                                        const clap_host_t* host)
{
    if (!state.container || !parent) return false;
    NSView* container = static_cast<NSView*>(state.container);
    [parent addSubview:container];
    [container setFrame:NSMakeRect(0.0, 0.0, state.width, state.height)];
    stopResponsiveScreenObservation(state);
    ResponsiveViewport* statePtr = &state;
    const clap_host_t* hostPtr = host;
    id observer = [[NSNotificationCenter defaultCenter]
        addObserverForName:NSWindowDidChangeScreenNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification* notification) {
                    NSWindow* window = [static_cast<NSView*>(statePtr->container) window];
                    if (!window || ([notification object] && [notification object] != window)) return;
                    requestResponsiveViewportFit(*statePtr, hostPtr);
                }];
    state.screenObserver = observer;
    requestResponsiveViewportFit(state, host);
    return true;
}

inline bool setResponsiveViewportHidden(const ResponsiveViewport& state, bool hidden)
{
    if (!state.container) return false;
    [static_cast<NSView*>(state.container) setHidden:hidden ? YES : NO];
    return true;
}

inline NSColor* color(int rgb, double alpha = 1.0)
{
    return [NSColor colorWithCalibratedRed:((rgb >> 16) & 0xff) / 255.0
                                     green:((rgb >> 8) & 0xff) / 255.0
                                      blue:(rgb & 0xff) / 255.0
                                     alpha:alpha];
}

inline NSColor* heatColor(double value, double alpha = 1.0)
{
    struct Stop {
        double t;
        int r;
        int g;
        int b;
    };
    static constexpr Stop kStops[] = {
        { 0.00, 10, 24, 94 },
        { 0.22, 0, 146, 232 },
        { 0.48, 255, 232, 42 },
        { 0.72, 255, 84, 12 },
        { 1.00, 238, 0, 0 },
    };
    value = std::clamp(value, 0.0, 1.0);
    const Stop* a = &kStops[0];
    const Stop* b = &kStops[sizeof(kStops) / sizeof(kStops[0]) - 1];
    for (size_t i = 1; i < sizeof(kStops) / sizeof(kStops[0]); ++i) {
        if (value <= kStops[i].t) {
            a = &kStops[i - 1];
            b = &kStops[i];
            break;
        }
    }
    const double span = std::max(0.0001, b->t - a->t);
    const double mix = (value - a->t) / span;
    const double r = lerp(static_cast<double>(a->r), static_cast<double>(b->r), mix) / 255.0;
    const double g = lerp(static_cast<double>(a->g), static_cast<double>(b->g), mix) / 255.0;
    const double bl = lerp(static_cast<double>(a->b), static_cast<double>(b->b), mix) / 255.0;
    return [NSColor colorWithCalibratedRed:r green:g blue:bl alpha:alpha];
}

struct Style {
    NSColor* bg = color(0x0c0c0c);
    NSColor* strip = color(0x131313);
    NSColor* cellBg = color(0x1d1d1d);
    NSColor* grid = color(0x565656);
    NSColor* dim = color(0x8f8f8f);
    NSColor* text = color(0xc9c9c9);
    NSColor* accent = color(0xb8b8b8);
    NSColor* fill = color(0x7f7f7f);
};

inline NSFont* uiFont(CGFloat size = 10.0)
{
    return [NSFont fontWithName:@"Menlo" size:size] ?: [NSFont monospacedSystemFontOfSize:size weight:NSFontWeightRegular];
}

inline NSDictionary* textAttrs(NSColor* textColor, CGFloat size = 10.0)
{
    return @{ NSForegroundColorAttributeName:textColor, NSFontAttributeName:uiFont(size) };
}

inline Style softTextStyle()
{
    Style style;
    return style;
}

inline NSDictionary* softLabelAttrs() { return textAttrs(color(0xa8a8a8), 10.0); }
inline NSDictionary* softValueAttrs() { return textAttrs(color(0x929292), 10.0); }
inline NSDictionary* softTitleAttrs() { return textAttrs(color(0xc8c8c8), 10.5); }

inline NSString* peakDbText(float peak)
{
    return [NSString stringWithFormat:@"PK %+4.1f", 20.0 * std::log10(std::max(0.000001f, peak))];
}

inline void drawRightStatus(NSString* text, CGFloat viewWidth, CGFloat y, NSDictionary* attrs, CGFloat rightInset = 18.0)
{
    [text drawAtPoint:NSMakePoint(viewWidth - [text sizeWithAttributes:attrs].width - rightInset, y)
       withAttributes:attrs];
}

inline NSString* sliderValueTextToFit(NSString* value,
                                      CGFloat maximumWidth,
                                      NSDictionary* attrs)
{
    if (!value || maximumWidth <= 0.0
        || [value sizeWithAttributes:attrs].width <= maximumWidth) {
        return value ?: @"";
    }

    NSScanner* scanner = [NSScanner scannerWithString:value];
    double number = 0.0;
    if ([scanner scanDouble:&number]) {
        NSString* suffix = [[value substringFromIndex:[scanner scanLocation]]
            stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
        for (int precision = 2; precision >= 0; --precision) {
            NSString* numeric = [NSString stringWithFormat:@"%.*f", precision, number];
            while ([numeric containsString:@"."] && [numeric hasSuffix:@"0"]) {
                numeric = [numeric substringToIndex:[numeric length] - 1u];
            }
            if ([numeric hasSuffix:@"."]) {
                numeric = [numeric substringToIndex:[numeric length] - 1u];
            }
            NSString* candidate = [suffix length] > 0u
                ? [NSString stringWithFormat:@"%@ %@", numeric, suffix]
                : numeric;
            if ([candidate sizeWithAttributes:attrs].width <= maximumWidth) {
                return candidate;
            }
        }

        NSString* compact = [suffix length] > 0u
            ? [NSString stringWithFormat:@"%.2g%@", number, suffix]
            : [NSString stringWithFormat:@"%.2g", number];
        if ([compact sizeWithAttributes:attrs].width <= maximumWidth) return compact;
    }
    return value;
}

inline void drawBoundedRightText(NSString* value,
                                 NSRect rect,
                                 NSDictionary* attrs)
{
    NSMutableParagraphStyle* paragraph = [[[NSMutableParagraphStyle alloc] init] autorelease];
    [paragraph setAlignment:NSTextAlignmentRight];
    [paragraph setLineBreakMode:NSLineBreakByClipping];
    NSMutableDictionary* boundedAttrs = [NSMutableDictionary dictionaryWithDictionary:attrs];
    [boundedAttrs setObject:paragraph forKey:NSParagraphStyleAttributeName];
    NSString* fitted = sliderValueTextToFit(value, rect.size.width, attrs);
    [fitted drawInRect:rect withAttributes:boundedAttrs];
}

inline void styleNumberTextField(NSTextField* field, CGFloat fontSize = 11.0, NSTextAlignment alignment = NSTextAlignmentRight)
{
    [field setFont:uiFont(fontSize)];
    [field setAlignment:alignment];
    [field setBezeled:YES];
    [field setBordered:YES];
    [field setEditable:YES];
    [field setSelectable:YES];
    [field setDrawsBackground:YES];
    [field setBackgroundColor:color(0x202020)];
    [field setTextColor:color(0xd0d0d0)];
    [field setFocusRingType:NSFocusRingTypeNone];
}

inline void styleActiveNumberTextField(NSTextField* field, bool active)
{
    [field setBackgroundColor:color(active ? 0x2a2a2a : 0x202020)];
}

inline void styleNumberTextEditor(NSTextField* field)
{
    NSText* editor = [field currentEditor];
    if (!editor || ![editor respondsToSelector:@selector(setSelectedTextAttributes:)]) return;
    NSTextView* textView = (NSTextView*)editor;
    [textView setSelectedTextAttributes:@{
        NSBackgroundColorAttributeName: color(0x4a4a4a),
        NSForegroundColorAttributeName: color(0xf0f0f0)
    }];
    [textView setInsertionPointColor:color(0xd8d8d8)];
}

inline void drawPanelFrame(CGFloat x, CGFloat y, CGFloat w, CGFloat h, const Style& style)
{
    [style.cellBg setFill];
    NSRectFill(NSMakeRect(x, y, w, h));
    [style.grid setStroke];
    NSFrameRect(NSMakeRect(x, y, w, h));
    [style.accent setFill];
    NSRectFill(NSMakeRect(x, y, w, 2.0));
}

inline void drawPanelHeader(NSString* title,
                            bool open,
                            CGFloat x,
                            CGFloat y,
                            CGFloat w,
                            CGFloat h,
                            NSDictionary* attrs,
                            const Style& style)
{
    (void)attrs;
    (void)open;
    [style.strip setFill];
    NSRectFill(NSMakeRect(x, y, w, h));
    [style.accent setFill];
    NSRectFill(NSMakeRect(x, y, w, 2));
    NSDictionary* headerAttrs = softLabelAttrs();
    [title drawAtPoint:NSMakePoint(
        x + static_cast<CGFloat>(
            s3g::gui_layout::kStandardMetrics.headerLabelInset),
        y + 5) withAttributes:headerAttrs];
}

inline NSRect cocoaRect(const s3g::gui_layout::Rect& rect)
{
    return NSMakeRect(static_cast<CGFloat>(rect.x),
        static_cast<CGFloat>(rect.y),
        static_cast<CGFloat>(rect.width),
        static_cast<CGFloat>(rect.height));
}

inline s3g::gui_layout::EncoderTitleBand encoderTitleBand(CGFloat width,
                                                          CGFloat height)
{
    return s3g::gui_layout::encoderTitleBand({
        static_cast<double>(width),
        static_cast<double>(height),
    });
}

inline NSRect encoderTitleActionRect(CGFloat width,
                                     CGFloat height,
                                     s3g::gui_layout::EncoderTitleAction action)
{
    return cocoaRect(s3g::gui_layout::encoderTitleActionRect(
        encoderTitleBand(width, height), action));
}

inline void drawPanelFrame(const s3g::gui_layout::Panel& panel,
                           const Style& style)
{
    const auto rect = cocoaRect(panel.frame);
    drawPanelFrame(rect.origin.x, rect.origin.y,
        rect.size.width, rect.size.height, style);
}

inline void drawPanelHeader(NSString* title,
                            bool open,
                            const s3g::gui_layout::Panel& panel,
                            NSDictionary* attrs,
                            const Style& style)
{
    const auto rect = cocoaRect(panel.frame);
    drawPanelHeader(title, open, rect.origin.x, rect.origin.y,
        rect.size.width,
        static_cast<CGFloat>(s3g::gui_layout::kStandardMetrics.headerHeight),
        attrs, style);
}

inline void drawDisclosurePanelHeader(NSString* title,
                                      bool open,
                                      CGFloat x,
                                      CGFloat y,
                                      CGFloat w,
                                      CGFloat h,
                                      NSDictionary* attrs,
                                      const Style& style)
{
    (void)attrs;
    [style.strip setFill];
    NSRectFill(NSMakeRect(x, y, w, h));
    [style.accent setFill];
    NSRectFill(NSMakeRect(x, y, w, 2));
    NSDictionary* headerAttrs = softLabelAttrs();
    [(open ? @"-" : @"+") drawAtPoint:NSMakePoint(x + 8, y + 5) withAttributes:headerAttrs];
    [title drawAtPoint:NSMakePoint(x + 24, y + 5) withAttributes:headerAttrs];
}

inline void drawSlider(NSString* name,
                       NSString* value,
                       CGFloat norm,
                       CGFloat y,
                       NSDictionary* labelAttrs,
                       NSDictionary* valueAttrs,
                       const Style& style,
                       CGFloat labelX = 654.0,
                       CGFloat trackX = 750.0,
                       CGFloat valueX = 920.0,
                       CGFloat trackW = 150.0,
                       CGFloat valueW = 36.0)
{
    [name drawAtPoint:NSMakePoint(labelX, y - 2) withAttributes:labelAttrs];
    NSRect track = NSMakeRect(trackX, y + 1, trackW, 9);
    [style.strip setFill];
    NSRectFill(track);
    [style.grid setStroke];
    NSFrameRect(track);
    norm = std::clamp(norm, static_cast<CGFloat>(0.0), static_cast<CGFloat>(1.0));
    NSRect filled = NSInsetRect(track, 1.0, 1.0);
    filled.size.width = std::max<CGFloat>(1.0, filled.size.width * norm);
    [style.fill setFill];
    NSRectFill(filled);
    const CGFloat handleX = std::clamp(track.origin.x + track.size.width * norm - 1.5,
                                       track.origin.x + 1.0,
                                       track.origin.x + track.size.width - 4.0);
    [style.text setFill];
    NSRectFill(NSMakeRect(handleX, track.origin.y - 2.0, 3.0, track.size.height + 4.0));
    drawBoundedRightText(value, NSMakeRect(valueX, y - 2, valueW, 15.0), valueAttrs);
}

inline NSString* menuDisplayText(NSString* value,
                                 CGFloat maximumWidth,
                                 NSDictionary* attrs)
{
    NSString* text = [(value ?: @"") uppercaseString];
    if (maximumWidth <= 0.0
        || [text sizeWithAttributes:attrs].width <= maximumWidth) {
        return text;
    }

    static NSArray<NSArray<NSString*>*>* substitutions = nil;
    if (!substitutions) {
        substitutions = [[NSArray alloc] initWithObjects:
            @[ @"ENERGY-NORMALIZED", @"ENERGY NORM" ],
            @[ @"ENERGY NORMALIZED", @"ENERGY NORM" ],
            @[ @"HYPERCARDIOID", @"HYPER" ],
            @[ @"SUPERCARDIOID", @"SUPER" ],
            @[ @"CARDIOID", @"CARD" ],
            @[ @"VIRTUAL", @"VIRT" ],
            @[ @"FEEDFORWARD", @"FEED FWD" ],
            @[ @"PROJECTION", @"PROJ" ],
            @[ @"ELEVATION", @"ELEV" ],
            @[ @"DIRECTIONAL", @"DIR" ],
            @[ @"INTERPOLATION", @"INTERP" ],
            @[ @"ALTERNATING", @"ALT" ],
            nil];
    }
    for (NSArray<NSString*>* substitution in substitutions) {
        text = [text stringByReplacingOccurrencesOfString:substitution[0]
            withString:substitution[1]];
        if ([text sizeWithAttributes:attrs].width <= maximumWidth) return text;
    }

    NSString* suffix = @"…";
    while ([text length] > 1u
        && [[text stringByAppendingString:suffix]
            sizeWithAttributes:attrs].width > maximumWidth) {
        text = [text substringToIndex:[text length] - 1u];
    }
    return [text stringByAppendingString:suffix];
}

inline void drawMenu(NSString* name,
                     NSString* value,
                     CGFloat y,
                     NSDictionary* labelAttrs,
                     NSDictionary* valueAttrs,
                     const Style& style,
                     CGFloat labelX = 654.0,
                     CGFloat boxX = 750.0,
                     CGFloat boxW = 178.0)
{
    [[name uppercaseString] drawAtPoint:NSMakePoint(labelX, y - 2)
        withAttributes:labelAttrs];
    NSRect box = NSMakeRect(boxX, y - 1, boxW, 15);
    [style.strip setFill];
    NSRectFill(box);
    [style.grid setStroke];
    NSFrameRect(box);
    [style.fill setFill];
    NSRectFill(NSMakeRect(box.origin.x + 1, box.origin.y + 1, 2, box.size.height - 2));
    NSString* displayValue = menuDisplayText(value,
        std::max<CGFloat>(0.0, box.size.width - 28.0), valueAttrs);
    [displayValue drawAtPoint:NSMakePoint(box.origin.x + 8, y + 1)
        withAttributes:valueAttrs];
    [@"v" drawAtPoint:NSMakePoint(box.origin.x + box.size.width - 12, y) withAttributes:valueAttrs];
}

inline NSString* menuDisplayText(NSString* value,
                                 CGFloat maximumWidth,
                                 NSDictionary* attrs);

inline void drawReadOnlyValue(NSString* name,
                              NSString* value,
                              CGFloat y,
                              NSDictionary* labelAttrs,
                              NSDictionary* valueAttrs,
                              const Style& style,
                              CGFloat labelX = 654.0,
                              CGFloat boxX = 750.0,
                              CGFloat boxW = 178.0)
{
    [[name uppercaseString] drawAtPoint:NSMakePoint(labelX, y - 2)
        withAttributes:labelAttrs];
    NSRect box = NSMakeRect(boxX, y - 1, boxW, 15);
    [style.strip setFill];
    NSRectFill(box);
    [style.grid setStroke];
    NSFrameRect(box);
    NSString* displayValue = menuDisplayText(
        value, std::max<CGFloat>(0.0, box.size.width - 16.0), valueAttrs);
    [displayValue drawAtPoint:NSMakePoint(box.origin.x + 8, y + 1)
        withAttributes:valueAttrs];
}

inline void drawProcessorSlider(NSString* name,
                                NSString* value,
                                CGFloat norm,
                                CGFloat y,
                                CGFloat panelX,
                                CGFloat panelWidth,
                                NSDictionary* labelAttrs,
                                NSDictionary* valueAttrs,
                                const Style& style)
{
    drawSlider(name, value, norm, y, labelAttrs, valueAttrs, style,
        static_cast<CGFloat>(s3g::gui_layout::processorLabelX(panelX)),
        static_cast<CGFloat>(s3g::gui_layout::processorControlX(panelX)),
        static_cast<CGFloat>(s3g::gui_layout::processorValueX(
            panelX, panelWidth)),
        static_cast<CGFloat>(s3g::gui_layout::processorTrackWidth(panelWidth)),
        static_cast<CGFloat>(
            s3g::gui_layout::kStandardMetrics.processorValueWidth));
}

inline void drawProcessorMenu(NSString* name,
                              NSString* value,
                              CGFloat y,
                              CGFloat panelX,
                              CGFloat panelWidth,
                              NSDictionary* labelAttrs,
                              NSDictionary* valueAttrs,
                              const Style& style)
{
    drawMenu(name, value, y, labelAttrs, valueAttrs, style,
        static_cast<CGFloat>(s3g::gui_layout::processorLabelX(panelX)),
        static_cast<CGFloat>(s3g::gui_layout::processorControlX(panelX)),
        static_cast<CGFloat>(s3g::gui_layout::processorMenuWidth(panelWidth)));
}

inline void drawToggle(NSString* name,
                       bool on,
                       CGFloat y,
                       NSDictionary* labelAttrs,
                       NSDictionary* valueAttrs,
                       const Style& style,
                       CGFloat labelX = 654.0,
                       CGFloat boxX = 750.0,
                       CGFloat boxW = 64.0)
{
    [name drawAtPoint:NSMakePoint(labelX, y - 2) withAttributes:labelAttrs];
    NSRect box = NSMakeRect(boxX, y - 1, boxW, 15);
    [color(on ? 0x303030 : 0x151515) setFill];
    NSRectFill(box);
    [color(on ? 0xb8b8b8 : 0x555555) setStroke];
    NSFrameRect(box);
    NSString* value = on ? @"ON" : @"OFF";
    const NSSize size = [value sizeWithAttributes:valueAttrs];
    [value drawAtPoint:NSMakePoint(box.origin.x + (box.size.width - size.width) * 0.5,
                                   box.origin.y + (box.size.height - size.height) * 0.5 - 0.5)
        withAttributes:valueAttrs];
    [style.fill setFill];
    NSRectFill(NSMakeRect(box.origin.x + 1, box.origin.y + 1, 2, box.size.height - 2));
}

inline NSRect dropdownRowRect(NSRect menuRect, CGFloat itemH, uint32_t index)
{
    return NSMakeRect(menuRect.origin.x,
                      menuRect.origin.y + itemH * static_cast<CGFloat>(index),
                      menuRect.size.width,
                      itemH);
}

inline int dropdownHitIndex(NSPoint point, NSRect menuRect, CGFloat itemH, uint32_t count)
{
    if (!NSPointInRect(point, menuRect) || count == 0u) return -1;
    return static_cast<int>(std::min<uint32_t>(
        count - 1u,
        static_cast<uint32_t>((point.y - menuRect.origin.y) / itemH)));
}

inline uint32_t multiColumnMenuRows(uint32_t count, uint32_t columns)
{
    return columns == 0u ? 0u : (count + columns - 1u) / columns;
}

inline int multiColumnDropdownHitIndex(NSPoint point,
                                       NSRect menuRect,
                                       CGFloat itemH,
                                       uint32_t count,
                                       uint32_t columns)
{
    if (!NSPointInRect(point, menuRect) || count == 0u || columns == 0u) {
        return -1;
    }
    const uint32_t rows = multiColumnMenuRows(count, columns);
    const CGFloat columnWidth = menuRect.size.width
        / static_cast<CGFloat>(columns);
    const uint32_t column = std::min<uint32_t>(
        static_cast<uint32_t>(
            (point.x - menuRect.origin.x) / columnWidth),
        columns - 1u);
    const uint32_t row = static_cast<uint32_t>(
        (point.y - menuRect.origin.y) / itemH);
    const uint32_t index = column * rows + row;
    return index < count ? static_cast<int>(index) : -1;
}

inline void drawDropdownMenu(NSRect menuRect,
                             CGFloat itemH,
                             NSString* const* items,
                             uint32_t count,
                             int selectedIndex,
                             int hoverIndex,
                             NSDictionary* attrs,
                             const Style& style)
{
    [color(0x080808) setFill];
    NSRectFill(NSInsetRect(menuRect, -2.0, -2.0));
    [color(0x151515) setFill];
    NSRectFill(menuRect);
    [color(0x6c6c6c) setStroke];
    NSFrameRect(menuRect);
    for (uint32_t i = 0; i < count; ++i) {
        const NSRect row = dropdownRowRect(menuRect, itemH, i);
        if (static_cast<int>(i) == hoverIndex) {
            [color(0x343434) setFill];
            NSRectFill(NSInsetRect(row, 1.0, 1.0));
        } else if (static_cast<int>(i) == selectedIndex) {
            [color(0x292929) setFill];
            NSRectFill(NSInsetRect(row, 1.0, 1.0));
        } else if ((i % 2u) == 1u) {
            [style.strip setFill];
            NSRectFill(NSInsetRect(row, 1.0, 1.0));
        }
        if (static_cast<int>(i) == selectedIndex || static_cast<int>(i) == hoverIndex) {
            [style.fill setFill];
            NSRectFill(NSMakeRect(row.origin.x + 2.0, row.origin.y + 2.0, 3.0, row.size.height - 4.0));
        }
        if (i > 0) {
            [color(0x3a3a3a) setStroke];
            [NSBezierPath strokeLineFromPoint:NSMakePoint(row.origin.x, row.origin.y)
                                      toPoint:NSMakePoint(NSMaxX(row), row.origin.y)];
        }
        NSString* displayItem = menuDisplayText(items[i],
            std::max<CGFloat>(0.0, row.size.width - 18.0), attrs);
        [displayItem drawAtPoint:NSMakePoint(
            row.origin.x + 9.0, row.origin.y + 4.0)
            withAttributes:attrs];
    }
}

inline void drawMultiColumnDropdownMenu(
    NSRect menuRect,
    CGFloat itemH,
    NSString* const* items,
    uint32_t count,
    uint32_t columns,
    int selectedIndex,
    int hoverIndex,
    NSDictionary* attrs,
    const Style& style)
{
    if (columns == 0u) return;
    const uint32_t rows = multiColumnMenuRows(count, columns);
    const CGFloat columnWidth = menuRect.size.width
        / static_cast<CGFloat>(columns);
    for (uint32_t column = 0u; column < columns; ++column) {
        const uint32_t first = column * rows;
        if (first >= count) break;
        const uint32_t columnCount =
            std::min<uint32_t>(rows, count - first);
        const int columnSelected =
            selectedIndex >= static_cast<int>(first)
                && selectedIndex < static_cast<int>(first + columnCount)
            ? selectedIndex - static_cast<int>(first) : -1;
        const int columnHover =
            hoverIndex >= static_cast<int>(first)
                && hoverIndex < static_cast<int>(first + columnCount)
            ? hoverIndex - static_cast<int>(first) : -1;
        const NSRect columnRect = NSMakeRect(
            menuRect.origin.x + columnWidth * column,
            menuRect.origin.y, columnWidth,
            itemH * static_cast<CGFloat>(columnCount));
        drawDropdownMenu(columnRect, itemH, items + first, columnCount,
            columnSelected, columnHover, attrs, style);
    }
}

inline void drawHeaderButton(NSRect button,
                             NSRect headerRect,
                             NSString* label,
                             bool active,
                             NSDictionary* attrs,
                             const Style& style)
{
    [color(active ? 0x303030 : 0x151515) setFill];
    NSRectFill(button);
    [color(active ? 0xb8b8b8 : 0x555555) setStroke];
    NSFrameRect(button);
    const NSSize size = [label sizeWithAttributes:attrs];
    [label drawAtPoint:NSMakePoint(button.origin.x + (button.size.width - size.width) * 0.5,
                                   button.origin.y + (button.size.height - size.height) * 0.5 - 0.5)
        withAttributes:attrs];
    (void)headerRect;
}

inline NSRect topologyProcessorFieldContentRect(NSRect fieldPanel)
{
    return NSMakeRect(
        fieldPanel.origin.x + 10.0,
        fieldPanel.origin.y + 28.0,
        fieldPanel.size.width - 20.0,
        fieldPanel.size.height - 38.0);
}

inline NSRect environmentalFieldPageButtonRect(
    NSRect fieldPanel, uint32_t index)
{
    return cocoaRect(s3g::gui_layout::environmentalFieldPageButtonRect(
        {
            static_cast<double>(fieldPanel.origin.x),
            static_cast<double>(fieldPanel.origin.y),
            static_cast<double>(fieldPanel.size.width),
            static_cast<double>(fieldPanel.size.height),
        },
        index));
}

inline NSRect topologyProcessorFieldPageButtonRect(
    NSRect fieldPanel, uint32_t index)
{
    constexpr CGFloat buttonWidth = 58.0;
    constexpr CGFloat buttonGap = 8.0;
    constexpr CGFloat totalWidth = buttonWidth * 2.0 + buttonGap;
    return NSMakeRect(
        fieldPanel.origin.x
            + (fieldPanel.size.width - totalWidth) * 0.5
            + static_cast<CGFloat>(index) * (buttonWidth + buttonGap),
        fieldPanel.origin.y + 3.0,
        buttonWidth,
        15.0);
}

inline NSRect topologyProcessorCameraButtonRect(
    NSRect fieldPanel, uint32_t index)
{
    constexpr CGFloat buttonWidth = 42.0;
    constexpr CGFloat buttonGap = 6.0;
    constexpr CGFloat rightInset = 16.0;
    constexpr CGFloat totalWidth = buttonWidth * 3.0 + buttonGap * 2.0;
    return NSMakeRect(
        NSMaxX(fieldPanel) - rightInset - totalWidth
            + static_cast<CGFloat>(index) * (buttonWidth + buttonGap),
        fieldPanel.origin.y + 3.0,
        buttonWidth,
        15.0);
}

inline void drawTopologyProcessorCameraButtons(
    NSRect fieldPanel,
    int selectedView,
    NSDictionary* attrs,
    const Style& style)
{
    NSString* labels[3] = { @"TOP", @"SIDE", @"3/4" };
    for (uint32_t index = 0u; index < 3u; ++index) {
        drawHeaderButton(
            topologyProcessorCameraButtonRect(fieldPanel, index),
            fieldPanel,
            labels[index],
            selectedView == static_cast<int>(index),
            attrs,
            style);
    }
}

struct TopologyProcessorChannelGrid {
    NSRect contentRect {};
    uint32_t columns = 0u;
    uint32_t rows = 0u;
    CGFloat gap = 0.0;
    CGFloat labelHeight = 24.0;
    CGFloat cellWidth = 0.0;
    CGFloat cellHeight = 0.0;
};

inline TopologyProcessorChannelGrid topologyProcessorChannelGrid(
    NSRect contentRect, uint32_t channelCount)
{
    TopologyProcessorChannelGrid grid;
    grid.contentRect = contentRect;
    grid.columns = channelCount <= 8u ? 2u : 4u;
    grid.rows = (channelCount + grid.columns - 1u) / grid.columns;
    grid.gap = channelCount <= 8u ? 8.0 : 5.0;
    grid.cellWidth = (contentRect.size.width
        - grid.gap * static_cast<CGFloat>(grid.columns + 1u))
        / static_cast<CGFloat>(grid.columns);
    grid.cellHeight = (contentRect.size.height - grid.labelHeight
        - grid.gap * static_cast<CGFloat>(grid.rows + 1u))
        / static_cast<CGFloat>(grid.rows);
    return grid;
}

inline NSRect topologyProcessorChannelRect(
    const TopologyProcessorChannelGrid& grid, uint32_t channel)
{
    const uint32_t column = channel % grid.columns;
    const uint32_t row = channel / grid.columns;
    return NSMakeRect(
        grid.contentRect.origin.x + grid.gap
            + static_cast<CGFloat>(column)
                * (grid.cellWidth + grid.gap),
        grid.contentRect.origin.y + grid.labelHeight + grid.gap
            + static_cast<CGFloat>(row)
                * (grid.cellHeight + grid.gap),
        grid.cellWidth,
        grid.cellHeight);
}

inline void drawHeaderActionButton(NSRect button,
                                   NSRect headerRect,
                                   NSString* label,
                                   NSDictionary* attrs,
                                   const Style& style)
{
    [color(0x202020) setFill];
    NSRectFill(button);
    [color(0xb8b8b8) setStroke];
    NSFrameRect(button);
    [color(0x343434) setStroke];
    NSFrameRect(NSInsetRect(button, 1.0, 1.0));
    const NSSize size = [label sizeWithAttributes:attrs];
    [label drawAtPoint:NSMakePoint(button.origin.x + (button.size.width - size.width) * 0.5,
                                   button.origin.y + (button.size.height - size.height) * 0.5 - 0.5)
        withAttributes:attrs];
    (void)headerRect;
    (void)style;
}

inline void drawEncoderPresetMenu(NSString* preset,
                                  const s3g::gui_layout::EncoderTitleBand& band,
                                  NSDictionary* labelAttrs,
                                  NSDictionary* valueAttrs,
                                  const Style& style)
{
    drawMenu(@"", preset, band.titleY, labelAttrs, valueAttrs, style,
        band.presetLabelX, band.presetMenu.x, band.presetMenu.width);
    [@"PRESET" drawAtPoint:NSMakePoint(band.presetLabelX, band.titleY + 1.0)
        withAttributes:labelAttrs];
}

inline void drawEncoderTitleBand(NSString* title,
                                 NSString* preset,
                                 NSString* status,
                                 const s3g::gui_layout::EncoderTitleBand& band,
                                 NSDictionary* titleAttrs,
                                 NSDictionary* labelAttrs,
                                 NSDictionary* valueAttrs,
                                 const Style& style)
{
    [title drawAtPoint:NSMakePoint(band.titleX, band.titleY)
        withAttributes:titleAttrs];
    drawEncoderPresetMenu(preset, band, labelAttrs, valueAttrs, style);
    drawHeaderActionButton(cocoaRect(band.loadButton), cocoaRect(band.loadButton),
        @"LOAD", labelAttrs, style);
    drawHeaderActionButton(cocoaRect(band.saveButton), cocoaRect(band.saveButton),
        @"SAVE", labelAttrs, style);
    drawHeaderActionButton(cocoaRect(band.randomButton), cocoaRect(band.randomButton),
        @"RANDOM", labelAttrs, style);
    if (status && [status length] > 0u) {
        drawRightStatus(status, band.canvas.width, band.titleY, valueAttrs,
            band.statusRightInset);
    }
}

inline void drawDecoderTitleBand(NSString* title,
                                 NSString* preset,
                                 NSString* status,
                                 const s3g::gui_layout::EncoderTitleBand& band,
                                 NSDictionary* titleAttrs,
                                 NSDictionary* labelAttrs,
                                 NSDictionary* valueAttrs,
                                 const Style& style)
{
    [title drawAtPoint:NSMakePoint(band.titleX, band.titleY)
        withAttributes:titleAttrs];
    drawEncoderPresetMenu(preset, band, labelAttrs, valueAttrs, style);
    drawHeaderActionButton(cocoaRect(band.loadButton), cocoaRect(band.loadButton),
        @"LOAD", labelAttrs, style);
    drawHeaderActionButton(cocoaRect(band.saveButton), cocoaRect(band.saveButton),
        @"SAVE", labelAttrs, style);
    if (status && [status length] > 0u) {
        drawRightStatus(status, band.canvas.width, band.titleY, valueAttrs,
            band.statusRightInset);
    }
}

inline void drawProcessorTitleBand(NSString* title,
                                   NSString* preset,
                                   NSString* status,
                                   const s3g::gui_layout::EncoderTitleBand& band,
                                   NSDictionary* titleAttrs,
                                   NSDictionary* labelAttrs,
                                   NSDictionary* valueAttrs,
                                   const Style& style)
{
    // Processor title typography is a family invariant. Callers may retain
    // their own content palettes, but those palettes must not alter the title
    // name, PRESET, LOAD/SAVE, or PK weight and intensity.
    (void)titleAttrs;
    (void)labelAttrs;
    (void)valueAttrs;
    drawDecoderTitleBand(title, preset, status, band,
        softTitleAttrs(), softLabelAttrs(), softValueAttrs(), style);
}

inline void drawMacroTitleBand(NSString* title,
                               NSString* preset,
                               NSString* status,
                               const s3g::gui_layout::EncoderTitleBand& band,
                               const Style& style)
{
    NSDictionary* titleAttrs = softTitleAttrs();
    NSDictionary* labelAttrs = softLabelAttrs();
    NSDictionary* valueAttrs = softValueAttrs();
    [title drawAtPoint:NSMakePoint(band.titleX, band.titleY)
        withAttributes:titleAttrs];
    drawMenu(@"", preset, band.controlY, labelAttrs, valueAttrs, style,
        band.presetLabelX, band.presetMenu.x, band.presetMenu.width);
    [@"PRESET" drawAtPoint:NSMakePoint(
        band.presetLabelX, band.controlY + 1.0)
        withAttributes:labelAttrs];
    drawHeaderActionButton(cocoaRect(band.loadButton), cocoaRect(band.loadButton),
        @"LOAD", labelAttrs, style);
    drawHeaderActionButton(cocoaRect(band.saveButton), cocoaRect(band.saveButton),
        @"SAVE", labelAttrs, style);
    if (status && [status length] > 0u) {
        drawRightStatus(status, band.canvas.width, band.titleY, valueAttrs,
            band.statusRightInset);
    }
}

inline void drawArrayTitleBand(NSString* title,
                               NSString* preset,
                               NSString* status,
                               const s3g::gui_layout::EncoderTitleBand& band,
                               const Style& style)
{
    drawProcessorTitleBand(title, preset, status, band,
        softTitleAttrs(), softLabelAttrs(), softValueAttrs(), style);
}

inline void drawTransformTitleBand(NSString* title,
                                   NSString* preset,
                                   NSString* status,
                                   const s3g::gui_layout::EncoderTitleBand& band,
                                   const Style& style)
{
    drawProcessorTitleBand(title, preset, status, band,
        softTitleAttrs(), softLabelAttrs(), softValueAttrs(), style);
}

inline void drawMatrixTitleBand(NSString* title,
                                NSString* preset,
                                NSString* status,
                                const s3g::gui_layout::EncoderTitleBand& band,
                                const Style& style)
{
    drawProcessorTitleBand(title, preset, status, band,
        softTitleAttrs(), softLabelAttrs(), softValueAttrs(), style);
    drawHeaderActionButton(cocoaRect(band.randomButton),
        cocoaRect(band.randomButton), @"RANDOM", softLabelAttrs(), style);
}

inline void drawMixerTitleBand(NSString* title,
                               NSString* preset,
                               NSString* status,
                               const s3g::gui_layout::EncoderTitleBand& band,
                               const Style& style)
{
    drawProcessorTitleBand(title, preset, status, band,
        softTitleAttrs(), softLabelAttrs(), softValueAttrs(), style);
}

inline void drawCompactEffectTitleBand(
    NSString* title,
    NSString* preset,
    NSString* status,
    const s3g::gui_layout::EncoderTitleBand& band,
    const Style& style)
{
    drawProcessorTitleBand(title, preset, status, band,
        softTitleAttrs(), softLabelAttrs(), softValueAttrs(), style);
}

inline void drawAmbiEffectTitleBand(
    NSString* title,
    NSString* preset,
    NSString* status,
    const s3g::gui_layout::EncoderTitleBand& band,
    const Style& style)
{
    drawProcessorTitleBand(title, preset, status, band,
        softTitleAttrs(), softLabelAttrs(), softValueAttrs(), style);
}

inline void drawAnalyzerTitleBand(
    NSString* title,
    NSString* preset,
    NSString* status,
    const s3g::gui_layout::EncoderTitleBand& band,
    const Style& style)
{
    drawProcessorTitleBand(title, preset, status, band,
        softTitleAttrs(), softLabelAttrs(), softValueAttrs(), style);
}

inline void drawOutputUtilityTitleBand(
    NSString* title,
    NSString* preset,
    NSString* status,
    const s3g::gui_layout::EncoderTitleBand& band,
    const Style& style)
{
    drawProcessorTitleBand(title, preset, status, band,
        softTitleAttrs(), softLabelAttrs(), softValueAttrs(), style);
}

inline void drawImprintTitleBand(
    NSString* title,
    NSString* preset,
    NSString* status,
    const s3g::gui_layout::EncoderTitleBand& band,
    const Style& style)
{
    drawProcessorTitleBand(title, preset, status, band,
        softTitleAttrs(), softLabelAttrs(), softValueAttrs(), style);
}

struct DefaultParamEventList {
    std::vector<clap_event_param_value_t> events;
    clap_input_events_t input {
        this,
        [](const clap_input_events_t* list) -> uint32_t {
            const auto* self = static_cast<const DefaultParamEventList*>(list->ctx);
            return self ? static_cast<uint32_t>(self->events.size()) : 0u;
        },
        [](const clap_input_events_t* list, uint32_t index) -> const clap_event_header_t* {
            const auto* self = static_cast<const DefaultParamEventList*>(list->ctx);
            if (!self || index >= self->events.size()) return nullptr;
            return &self->events[index].header;
        },
    };
};

inline bool resetPluginParamsToDefaults(const clap_plugin_t* plugin)
{
    if (!plugin || !plugin->get_extension) return false;
    const auto* params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    if (!params || !params->count || !params->get_info || !params->flush)
        return false;

    DefaultParamEventList defaults;
    const uint32_t count = params->count(plugin);
    defaults.events.reserve(count);
    for (uint32_t index = 0u; index < count; ++index) {
        clap_param_info_t info {};
        if (!params->get_info(plugin, index, &info)
            || (info.flags & CLAP_PARAM_IS_READONLY) != 0u) {
            continue;
        }
        clap_event_param_value_t event {};
        event.header.size = sizeof(event);
        event.header.time = 0u;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.param_id = info.id;
        event.cookie = info.cookie;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = info.default_value;
        defaults.events.push_back(event);
    }
    if (defaults.events.empty()) return false;
    params->flush(plugin, &defaults.input, nullptr);
    return true;
}

inline bool savePluginStatePreset(const clap_plugin_t* plugin,
                                  NSString* pluginName,
                                  NSString** savedName);
inline bool loadPluginStatePreset(const clap_plugin_t* plugin,
                                  NSString* pluginName,
                                  NSString** loadedName);

inline bool handleProcessorTitleClick(
    NSPoint point,
    const clap_plugin_t* plugin,
    NSString* pluginName,
    const s3g::gui_layout::EncoderTitleBand& band,
    char* presetName,
    size_t presetNameCapacity)
{
    if (!presetName || presetNameCapacity == 0u) return false;
    if (NSPointInRect(point, cocoaRect(band.presetMenu))) {
        if (!resetPluginParamsToDefaults(plugin)) {
            NSBeep();
        } else {
            std::snprintf(presetName, presetNameCapacity, "%s", "INIT");
        }
        return true;
    }
    if (NSPointInRect(point, cocoaRect(band.loadButton))) {
        NSString* name = nil;
        if (!loadPluginStatePreset(plugin, pluginName, &name)) {
            NSBeep();
        } else {
            std::snprintf(presetName, presetNameCapacity, "%s",
                name ? [name UTF8String] : "CUSTOM");
        }
        return true;
    }
    if (NSPointInRect(point, cocoaRect(band.saveButton))) {
        NSString* name = nil;
        if (!savePluginStatePreset(plugin, pluginName, &name)) {
            NSBeep();
        } else {
            std::snprintf(presetName, presetNameCapacity, "%s",
                name ? [name UTF8String] : "CUSTOM");
        }
        return true;
    }
    return false;
}

struct PluginStateFileWriter {
    FILE* file = nullptr;
    clap_ostream_t stream {
        this,
        [](const clap_ostream_t* stream, const void* buffer, uint64_t size) -> int64_t {
            auto* writer = static_cast<PluginStateFileWriter*>(stream->ctx);
            if (!writer || !writer->file) return -1;
            const size_t written = std::fwrite(buffer, 1u, static_cast<size_t>(size), writer->file);
            return written > 0u || size == 0u ? static_cast<int64_t>(written) : -1;
        },
    };
};

struct PluginStateFileReader {
    FILE* file = nullptr;
    clap_istream_t stream {
        this,
        [](const clap_istream_t* stream, void* buffer, uint64_t size) -> int64_t {
            auto* reader = static_cast<PluginStateFileReader*>(stream->ctx);
            if (!reader || !reader->file) return -1;
            const size_t read = std::fread(buffer, 1u, static_cast<size_t>(size), reader->file);
            if (read > 0u || std::feof(reader->file)) return static_cast<int64_t>(read);
            return -1;
        },
    };
};

inline NSString* encoderPresetDirectory(NSString* pluginName)
{
    NSString* root = [NSHomeDirectory()
        stringByAppendingPathComponent:@"Music/s3g/Presets"];
    return pluginName && [pluginName length] > 0u
        ? [root stringByAppendingPathComponent:pluginName]
        : root;
}

inline bool savePluginStatePreset(const clap_plugin_t* plugin,
                                  NSString* pluginName,
                                  NSString** savedName = nullptr)
{
    if (!plugin || !plugin->get_extension) return false;
    const auto* state = static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    if (!state || !state->save) return false;

    NSString* directory = encoderPresetDirectory(pluginName);
    [[NSFileManager defaultManager] createDirectoryAtPath:directory
        withIntermediateDirectories:YES attributes:nil error:nil];
    NSSavePanel* panel = [NSSavePanel savePanel];
    [panel setDirectoryURL:[NSURL fileURLWithPath:directory isDirectory:YES]];
    [panel setNameFieldStringValue:@"Preset.s3gpreset"];
    if ([panel runModal] != NSModalResponseOK) return false;

    const char* path = [[panel URL].path fileSystemRepresentation];
    PluginStateFileWriter writer;
    writer.file = std::fopen(path, "wb");
    if (!writer.file) return false;
    const bool saved = state->save(plugin, &writer.stream);
    const bool closed = std::fclose(writer.file) == 0;
    writer.file = nullptr;
    if (saved && closed && savedName) {
        *savedName = [[[panel URL] lastPathComponent] stringByDeletingPathExtension];
    }
    return saved && closed;
}

inline bool loadPluginStatePreset(const clap_plugin_t* plugin,
                                  NSString* pluginName,
                                  NSString** loadedName = nullptr)
{
    if (!plugin || !plugin->get_extension) return false;
    const auto* state = static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    if (!state || !state->load) return false;

    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setCanChooseFiles:YES];
    [panel setCanChooseDirectories:NO];
    [panel setAllowsMultipleSelection:NO];
    NSString* directory = encoderPresetDirectory(pluginName);
    if ([[NSFileManager defaultManager] fileExistsAtPath:directory]) {
        [panel setDirectoryURL:[NSURL fileURLWithPath:directory isDirectory:YES]];
    }
    if ([panel runModal] != NSModalResponseOK) return false;

    const char* path = [[panel URL].path fileSystemRepresentation];
    PluginStateFileReader reader;
    reader.file = std::fopen(path, "rb");
    if (!reader.file) return false;
    const bool loaded = state->load(plugin, &reader.stream);
    std::fclose(reader.file);
    reader.file = nullptr;
    if (loaded && loadedName) {
        *loadedName = [[[panel URL] lastPathComponent] stringByDeletingPathExtension];
    }
    return loaded;
}

struct TopologyUiValues {
    const char* shape = "";
    double amount = 0.0;
    double pull = 0.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double twist = 0.0;
    double flare = 0.0;
    double seed = 0.0;
    const char* motion = "";
    const char* variant = "";
    double rateHz = 0.0;
    double rateMinHz = 0.01;
    double rateMaxHz = 4.0;
    double depth = 0.0;
    uint32_t neighbors = 2;
    bool neighborSuffix = true;
    double radius = 0.0;
    double centroid = 0.0;
};

enum class TopologyRow : uint32_t {
    None = 0,
    Shape,
    Amount,
    Pull,
    X,
    Y,
    Z,
    Twist,
    Flare,
    Seed,
    Motion,
    Variant,
    Rate,
    Depth,
    Neighbors,
    Radius,
    Centroid,
};

inline int topologyRowIndex(TopologyRow row)
{
    switch (row) {
    case TopologyRow::Shape: return 0;
    case TopologyRow::Amount: return 1;
    case TopologyRow::Pull: return 2;
    case TopologyRow::X: return 3;
    case TopologyRow::Y: return 4;
    case TopologyRow::Z: return 5;
    case TopologyRow::Twist: return 6;
    case TopologyRow::Flare: return 7;
    case TopologyRow::Seed: return 8;
    case TopologyRow::Motion: return 9;
    case TopologyRow::Variant: return 10;
    case TopologyRow::Rate: return 11;
    case TopologyRow::Depth: return 12;
    case TopologyRow::Neighbors: return 13;
    case TopologyRow::Radius: return 14;
    case TopologyRow::Centroid: return 15;
    default: return -1;
    }
}

inline CGFloat topologyRowY(
    CGFloat panelY,
    TopologyRow row,
    CGFloat rowPitch = static_cast<CGFloat>(
        s3g::gui_layout::kStandardMetrics.rowPitch))
{
    const int index = topologyRowIndex(row);
    return index < 0 ? panelY
        : panelY
            + static_cast<CGFloat>(
                s3g::gui_layout::kStandardMetrics.firstRowOffset)
            + static_cast<CGFloat>(index) * rowPitch;
}

inline TopologyRow hitTopologyRow(
    NSPoint point,
    CGFloat panelY,
    CGFloat panelX = 644.0,
    CGFloat rowW = 344.0,
    CGFloat rowPitch = static_cast<CGFloat>(
        s3g::gui_layout::kStandardMetrics.rowPitch))
{
    constexpr TopologyRow rows[] = {
        TopologyRow::Shape,
        TopologyRow::Amount,
        TopologyRow::Pull,
        TopologyRow::X,
        TopologyRow::Y,
        TopologyRow::Z,
        TopologyRow::Twist,
        TopologyRow::Flare,
        TopologyRow::Seed,
        TopologyRow::Motion,
        TopologyRow::Variant,
        TopologyRow::Rate,
        TopologyRow::Depth,
        TopologyRow::Neighbors,
        TopologyRow::Radius,
        TopologyRow::Centroid,
    };
    for (TopologyRow row : rows) {
        const CGFloat y = topologyRowY(panelY, row, rowPitch) - 8.0;
        if (NSPointInRect(point, NSMakeRect(panelX, y, rowW, 24.0))) {
            return row;
        }
    }
    return TopologyRow::None;
}

inline void drawTopologyRows(const TopologyUiValues& values,
                             CGFloat panelY,
                             NSDictionary* labelAttrs,
                             NSDictionary* valueAttrs,
                             const Style& style,
                             CGFloat rowPitch = static_cast<CGFloat>(
                                 s3g::gui_layout::kStandardMetrics.rowPitch),
                             CGFloat panelX = 644.0,
                             CGFloat panelWidth = 344.0)
{
    drawProcessorMenu(@"SHAP", [NSString stringWithUTF8String:values.shape], topologyRowY(panelY, TopologyRow::Shape, rowPitch), panelX, panelWidth, labelAttrs, valueAttrs, style);
    drawProcessorSlider(@"AMT", [NSString stringWithFormat:@"%3.0f%%", values.amount * 100.0], values.amount, topologyRowY(panelY, TopologyRow::Amount, rowPitch), panelX, panelWidth, labelAttrs, valueAttrs, style);
    drawProcessorSlider(@"PULL", [NSString stringWithFormat:@"%3.0f%%", values.pull * 100.0], values.pull, topologyRowY(panelY, TopologyRow::Pull, rowPitch), panelX, panelWidth, labelAttrs, valueAttrs, style);
    drawProcessorSlider(@"X", [NSString stringWithFormat:@"%+3.0f%%", values.x * 100.0], (values.x + 1.0) * 0.5, topologyRowY(panelY, TopologyRow::X, rowPitch), panelX, panelWidth, labelAttrs, valueAttrs, style);
    drawProcessorSlider(@"Y", [NSString stringWithFormat:@"%+3.0f%%", values.y * 100.0], (values.y + 1.0) * 0.5, topologyRowY(panelY, TopologyRow::Y, rowPitch), panelX, panelWidth, labelAttrs, valueAttrs, style);
    drawProcessorSlider(@"Z", [NSString stringWithFormat:@"%+3.0f%%", values.z * 100.0], (values.z + 1.0) * 0.5, topologyRowY(panelY, TopologyRow::Z, rowPitch), panelX, panelWidth, labelAttrs, valueAttrs, style);
    drawProcessorSlider(@"TWST", [NSString stringWithFormat:@"%+3.0f%%", values.twist * 100.0], (values.twist + 1.0) * 0.5, topologyRowY(panelY, TopologyRow::Twist, rowPitch), panelX, panelWidth, labelAttrs, valueAttrs, style);
    drawProcessorSlider(@"FLAR", [NSString stringWithFormat:@"%+3.0f%%", values.flare * 100.0], (values.flare + 1.0) * 0.5, topologyRowY(panelY, TopologyRow::Flare, rowPitch), panelX, panelWidth, labelAttrs, valueAttrs, style);
    drawProcessorSlider(@"SEED", [NSString stringWithFormat:@"%3.0f%%", values.seed * 100.0], values.seed, topologyRowY(panelY, TopologyRow::Seed, rowPitch), panelX, panelWidth, labelAttrs, valueAttrs, style);
    drawProcessorMenu(@"ANIM", [NSString stringWithUTF8String:values.motion], topologyRowY(panelY, TopologyRow::Motion, rowPitch), panelX, panelWidth, labelAttrs, valueAttrs, style);
    drawProcessorMenu(@"VAR", [NSString stringWithUTF8String:values.variant], topologyRowY(panelY, TopologyRow::Variant, rowPitch), panelX, panelWidth, labelAttrs, valueAttrs, style);
    const double rateNorm = (values.rateHz - values.rateMinHz) / std::max(0.0001, values.rateMaxHz - values.rateMinHz);
    drawProcessorSlider(@"RATE", [NSString stringWithFormat:@"%4.2f", values.rateHz], rateNorm, topologyRowY(panelY, TopologyRow::Rate, rowPitch), panelX, panelWidth, labelAttrs, valueAttrs, style);
    drawProcessorSlider(@"DPTH", [NSString stringWithFormat:@"%3.0f%%", values.depth * 100.0], values.depth, topologyRowY(panelY, TopologyRow::Depth, rowPitch), panelX, panelWidth, labelAttrs, valueAttrs, style);
    NSString* neighborText = values.neighborSuffix ? [NSString stringWithFormat:@"%uNN", values.neighbors] : [NSString stringWithFormat:@"%u", values.neighbors];
    drawProcessorMenu(@"NBR", neighborText, topologyRowY(panelY, TopologyRow::Neighbors, rowPitch), panelX, panelWidth, labelAttrs, valueAttrs, style);
    drawProcessorSlider(@"RAD", [NSString stringWithFormat:@"%3.0f%%", values.radius * 100.0], values.radius, topologyRowY(panelY, TopologyRow::Radius, rowPitch), panelX, panelWidth, labelAttrs, valueAttrs, style);
    drawProcessorSlider(@"CENT", [NSString stringWithFormat:@"%3.0f%%", values.centroid * 100.0], values.centroid, topologyRowY(panelY, TopologyRow::Centroid, rowPitch), panelX, panelWidth, labelAttrs, valueAttrs, style);
}

} // namespace s3g::clap_gui
#endif
