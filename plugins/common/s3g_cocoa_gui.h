#pragma once

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>

#include "s3g_math.h"

#include <algorithm>
#include <cmath>

namespace s3g::clap_gui {

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
    NSColor* grid = color(0x636363);
    NSColor* dim = color(0x9e9e9e);
    NSColor* text = color(0xf0f0f0);
    NSColor* accent = color(0xd1d1d1);
    NSColor* fill = color(0x8f8f8f);
};

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
    [style.strip setFill];
    NSRectFill(NSMakeRect(x, y, w, h));
    [style.accent setFill];
    NSRectFill(NSMakeRect(x, y, w, 2));
    [(open ? @"-" : @"+") drawAtPoint:NSMakePoint(x + 8, y + 5) withAttributes:attrs];
    [title drawAtPoint:NSMakePoint(x + 24, y + 5) withAttributes:attrs];
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

inline CGFloat topologyRowY(CGFloat panelY, TopologyRow row)
{
    switch (row) {
    case TopologyRow::Shape: return panelY + 30.0;
    case TopologyRow::Amount: return panelY + 48.0;
    case TopologyRow::Pull: return panelY + 66.0;
    case TopologyRow::X: return panelY + 84.0;
    case TopologyRow::Y: return panelY + 102.0;
    case TopologyRow::Z: return panelY + 120.0;
    case TopologyRow::Twist: return panelY + 138.0;
    case TopologyRow::Flare: return panelY + 156.0;
    case TopologyRow::Seed: return panelY + 174.0;
    case TopologyRow::Motion: return panelY + 192.0;
    case TopologyRow::Variant: return panelY + 210.0;
    case TopologyRow::Rate: return panelY + 228.0;
    case TopologyRow::Depth: return panelY + 246.0;
    case TopologyRow::Neighbors: return panelY + 264.0;
    case TopologyRow::Radius: return panelY + 282.0;
    case TopologyRow::Centroid: return panelY + 300.0;
    default: return panelY;
    }
}

inline TopologyRow hitTopologyRow(NSPoint point, CGFloat panelY, CGFloat panelX = 650.0, CGFloat rowW = 330.0)
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
        const CGFloat y = topologyRowY(panelY, row) - 2.0;
        if (NSPointInRect(point, NSMakeRect(panelX, y, rowW, 20.0))) {
            return row;
        }
    }
    return TopologyRow::None;
}

inline void drawTopologyRows(const TopologyUiValues& values,
                             CGFloat panelY,
                             NSDictionary* labelAttrs,
                             NSDictionary* valueAttrs,
                             const Style& style)
{
    drawMenu(@"SHAP", [NSString stringWithUTF8String:values.shape], topologyRowY(panelY, TopologyRow::Shape), labelAttrs, valueAttrs, style);
    drawSlider(@"AMT", [NSString stringWithFormat:@"%3.0f%%", values.amount * 100.0], values.amount, topologyRowY(panelY, TopologyRow::Amount), labelAttrs, valueAttrs, style);
    drawSlider(@"PULL", [NSString stringWithFormat:@"%3.0f%%", values.pull * 100.0], values.pull, topologyRowY(panelY, TopologyRow::Pull), labelAttrs, valueAttrs, style);
    drawSlider(@"X", [NSString stringWithFormat:@"%+3.0f%%", values.x * 100.0], (values.x + 1.0) * 0.5, topologyRowY(panelY, TopologyRow::X), labelAttrs, valueAttrs, style);
    drawSlider(@"Y", [NSString stringWithFormat:@"%+3.0f%%", values.y * 100.0], (values.y + 1.0) * 0.5, topologyRowY(panelY, TopologyRow::Y), labelAttrs, valueAttrs, style);
    drawSlider(@"Z", [NSString stringWithFormat:@"%+3.0f%%", values.z * 100.0], (values.z + 1.0) * 0.5, topologyRowY(panelY, TopologyRow::Z), labelAttrs, valueAttrs, style);
    drawSlider(@"TWST", [NSString stringWithFormat:@"%+3.0f%%", values.twist * 100.0], (values.twist + 1.0) * 0.5, topologyRowY(panelY, TopologyRow::Twist), labelAttrs, valueAttrs, style);
    drawSlider(@"FLAR", [NSString stringWithFormat:@"%+3.0f%%", values.flare * 100.0], (values.flare + 1.0) * 0.5, topologyRowY(panelY, TopologyRow::Flare), labelAttrs, valueAttrs, style);
    drawSlider(@"SEED", [NSString stringWithFormat:@"%3.0f%%", values.seed * 100.0], values.seed, topologyRowY(panelY, TopologyRow::Seed), labelAttrs, valueAttrs, style);
    drawMenu(@"ANIM", [NSString stringWithUTF8String:values.motion], topologyRowY(panelY, TopologyRow::Motion), labelAttrs, valueAttrs, style);
    drawMenu(@"VAR", [NSString stringWithUTF8String:values.variant], topologyRowY(panelY, TopologyRow::Variant), labelAttrs, valueAttrs, style);
    const double rateNorm = (values.rateHz - values.rateMinHz) / std::max(0.0001, values.rateMaxHz - values.rateMinHz);
    drawSlider(@"RATE", [NSString stringWithFormat:@"%4.2f", values.rateHz], rateNorm, topologyRowY(panelY, TopologyRow::Rate), labelAttrs, valueAttrs, style);
    drawSlider(@"DPTH", [NSString stringWithFormat:@"%3.0f%%", values.depth * 100.0], values.depth, topologyRowY(panelY, TopologyRow::Depth), labelAttrs, valueAttrs, style);
    NSString* neighborText = values.neighborSuffix ? [NSString stringWithFormat:@"%uNN", values.neighbors] : [NSString stringWithFormat:@"%u", values.neighbors];
    drawMenu(@"NBR", neighborText, topologyRowY(panelY, TopologyRow::Neighbors), labelAttrs, valueAttrs, style);
    drawSlider(@"RAD", [NSString stringWithFormat:@"%3.0f%%", values.radius * 100.0], values.radius, topologyRowY(panelY, TopologyRow::Radius), labelAttrs, valueAttrs, style);
    drawSlider(@"CENT", [NSString stringWithFormat:@"%3.0f%%", values.centroid * 100.0], values.centroid, topologyRowY(panelY, TopologyRow::Centroid), labelAttrs, valueAttrs, style);
}

} // namespace s3g::clap_gui
#endif
