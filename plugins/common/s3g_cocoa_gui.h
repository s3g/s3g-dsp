#pragma once

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>

#include "s3g_math.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace s3g::clap_gui {

// Encoder editors use a fixed-size drawing surface so their visual geometry,
// hit testing, and native controls remain stable.  This state adds a resizable
// viewport around that surface; hosts may make the window smaller and expose
// the rest of the editor with standard Cocoa scrollers.
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
                                      uint32_t* height)
{
    if (!width || !height) return false;
    if (state.width > 0u && state.height > 0u) {
        *width = state.width;
        *height = state.height;
    } else {
        const NSSize size = responsiveViewportSizeForScreen(nativeWidth, nativeHeight);
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
                                         uint32_t* height)
{
    if (!width || !height) return false;
    const uint32_t minWidth = state.minimumWidth > 0u ? state.minimumWidth : std::min(480u, nativeWidth);
    const uint32_t minHeight = state.minimumHeight > 0u ? state.minimumHeight : std::min(360u, nativeHeight);
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
    [title drawAtPoint:NSMakePoint(x + 8, y + 5) withAttributes:headerAttrs];
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
                       CGFloat trackW = 150.0)
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
    [value drawAtPoint:NSMakePoint(valueX, y - 2) withAttributes:valueAttrs];
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
    [name drawAtPoint:NSMakePoint(labelX, y - 2) withAttributes:labelAttrs];
    NSRect box = NSMakeRect(boxX, y - 1, boxW, 15);
    [style.strip setFill];
    NSRectFill(box);
    [style.grid setStroke];
    NSFrameRect(box);
    [style.fill setFill];
    NSRectFill(NSMakeRect(box.origin.x + 1, box.origin.y + 1, 2, box.size.height - 2));
    [value drawAtPoint:NSMakePoint(box.origin.x + 8, y + 1) withAttributes:valueAttrs];
    [@"v" drawAtPoint:NSMakePoint(box.origin.x + box.size.width - 12, y) withAttributes:valueAttrs];
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
        [items[i] drawAtPoint:NSMakePoint(row.origin.x + 9.0, row.origin.y + 4.0) withAttributes:attrs];
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

inline CGFloat topologyRowY(CGFloat panelY, TopologyRow row, CGFloat rowPitch = 18.0)
{
    const int index = topologyRowIndex(row);
    return index < 0 ? panelY : panelY + 30.0 + static_cast<CGFloat>(index) * rowPitch;
}

inline TopologyRow hitTopologyRow(NSPoint point, CGFloat panelY, CGFloat panelX = 650.0, CGFloat rowW = 330.0, CGFloat rowPitch = 18.0)
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
                             CGFloat rowPitch = 18.0)
{
    drawMenu(@"SHAP", [NSString stringWithUTF8String:values.shape], topologyRowY(panelY, TopologyRow::Shape, rowPitch), labelAttrs, valueAttrs, style);
    drawSlider(@"AMT", [NSString stringWithFormat:@"%3.0f%%", values.amount * 100.0], values.amount, topologyRowY(panelY, TopologyRow::Amount, rowPitch), labelAttrs, valueAttrs, style);
    drawSlider(@"PULL", [NSString stringWithFormat:@"%3.0f%%", values.pull * 100.0], values.pull, topologyRowY(panelY, TopologyRow::Pull, rowPitch), labelAttrs, valueAttrs, style);
    drawSlider(@"X", [NSString stringWithFormat:@"%+3.0f%%", values.x * 100.0], (values.x + 1.0) * 0.5, topologyRowY(panelY, TopologyRow::X, rowPitch), labelAttrs, valueAttrs, style);
    drawSlider(@"Y", [NSString stringWithFormat:@"%+3.0f%%", values.y * 100.0], (values.y + 1.0) * 0.5, topologyRowY(panelY, TopologyRow::Y, rowPitch), labelAttrs, valueAttrs, style);
    drawSlider(@"Z", [NSString stringWithFormat:@"%+3.0f%%", values.z * 100.0], (values.z + 1.0) * 0.5, topologyRowY(panelY, TopologyRow::Z, rowPitch), labelAttrs, valueAttrs, style);
    drawSlider(@"TWST", [NSString stringWithFormat:@"%+3.0f%%", values.twist * 100.0], (values.twist + 1.0) * 0.5, topologyRowY(panelY, TopologyRow::Twist, rowPitch), labelAttrs, valueAttrs, style);
    drawSlider(@"FLAR", [NSString stringWithFormat:@"%+3.0f%%", values.flare * 100.0], (values.flare + 1.0) * 0.5, topologyRowY(panelY, TopologyRow::Flare, rowPitch), labelAttrs, valueAttrs, style);
    drawSlider(@"SEED", [NSString stringWithFormat:@"%3.0f%%", values.seed * 100.0], values.seed, topologyRowY(panelY, TopologyRow::Seed, rowPitch), labelAttrs, valueAttrs, style);
    drawMenu(@"ANIM", [NSString stringWithUTF8String:values.motion], topologyRowY(panelY, TopologyRow::Motion, rowPitch), labelAttrs, valueAttrs, style);
    drawMenu(@"VAR", [NSString stringWithUTF8String:values.variant], topologyRowY(panelY, TopologyRow::Variant, rowPitch), labelAttrs, valueAttrs, style);
    const double rateNorm = (values.rateHz - values.rateMinHz) / std::max(0.0001, values.rateMaxHz - values.rateMinHz);
    drawSlider(@"RATE", [NSString stringWithFormat:@"%4.2f", values.rateHz], rateNorm, topologyRowY(panelY, TopologyRow::Rate, rowPitch), labelAttrs, valueAttrs, style);
    drawSlider(@"DPTH", [NSString stringWithFormat:@"%3.0f%%", values.depth * 100.0], values.depth, topologyRowY(panelY, TopologyRow::Depth, rowPitch), labelAttrs, valueAttrs, style);
    NSString* neighborText = values.neighborSuffix ? [NSString stringWithFormat:@"%uNN", values.neighbors] : [NSString stringWithFormat:@"%u", values.neighbors];
    drawMenu(@"NBR", neighborText, topologyRowY(panelY, TopologyRow::Neighbors, rowPitch), labelAttrs, valueAttrs, style);
    drawSlider(@"RAD", [NSString stringWithFormat:@"%3.0f%%", values.radius * 100.0], values.radius, topologyRowY(panelY, TopologyRow::Radius, rowPitch), labelAttrs, valueAttrs, style);
    drawSlider(@"CENT", [NSString stringWithFormat:@"%3.0f%%", values.centroid * 100.0], values.centroid, topologyRowY(panelY, TopologyRow::Centroid, rowPitch), labelAttrs, valueAttrs, style);
}

} // namespace s3g::clap_gui
#endif
