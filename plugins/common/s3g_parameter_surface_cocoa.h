#pragma once

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace s3g::clap_gui {

inline NSColor* parameterSurfaceCellColor(
    uint32_t index, CGFloat alpha = 0.36)
{
    // Snapshot Surface uses a low-saturation HSV walk with an irrational-ish
    // hue increment so adjacent capture regions remain distinct without
    // turning the otherwise neutral editor into a rainbow.
    const CGFloat hue = std::fmod(static_cast<CGFloat>(index) * 0.137, 1.0);
    return [NSColor colorWithCalibratedHue:hue saturation:0.58
        brightness:0.42 alpha:alpha];
}

inline std::vector<NSPoint> clipParameterSurfacePolygon(
    const std::vector<NSPoint>& polygon, NSPoint site, NSPoint other)
{
    if (polygon.empty()) return {};
    const CGFloat a = 2.0 * (other.x - site.x);
    const CGFloat b = 2.0 * (other.y - site.y);
    const CGFloat c = other.x * other.x + other.y * other.y
        - site.x * site.x - site.y * site.y;
    const auto inside = [=](NSPoint point) {
        return a * point.x + b * point.y <= c + 0.0001;
    };
    const auto intersect = [=](NSPoint first, NSPoint second) {
        const CGFloat denominator = a * (second.x - first.x)
            + b * (second.y - first.y);
        if (std::abs(denominator) < 0.000001) return first;
        const CGFloat amount = std::clamp(
            (c - a * first.x - b * first.y) / denominator, 0.0, 1.0);
        return NSMakePoint(
            first.x + (second.x - first.x) * amount,
            first.y + (second.y - first.y) * amount);
    };

    std::vector<NSPoint> result;
    result.reserve(polygon.size() + 2u);
    NSPoint previous = polygon.back();
    bool previousInside = inside(previous);
    for (const NSPoint current : polygon) {
        const bool currentInside = inside(current);
        if (currentInside) {
            if (!previousInside) result.push_back(intersect(previous, current));
            result.push_back(current);
        } else if (previousInside) {
            result.push_back(intersect(previous, current));
        }
        previous = current;
        previousInside = currentInside;
    }
    return result;
}

inline NSBezierPath* parameterSurfacePolygonPath(
    const std::vector<NSPoint>& polygon)
{
    if (polygon.empty()) return nil;
    NSBezierPath* path = [NSBezierPath bezierPath];
    [path moveToPoint:polygon.front()];
    for (size_t index = 1u; index < polygon.size(); ++index) {
        [path lineToPoint:polygon[index]];
    }
    [path closePath];
    return path;
}

template <typename Surface>
inline void drawParameterSurfaceVoronoi(
    const Surface& surface, NSRect plot, float cursorX, float cursorY,
    int selectedCell, NSDictionary* labelAttrs,
    float targetX = std::numeric_limits<float>::quiet_NaN(),
    float targetY = std::numeric_limits<float>::quiet_NaN())
{
    const uint32_t count = std::min<uint32_t>(surface.cellCount,
        static_cast<uint32_t>(surface.cells.size()));
    const NSPoint cursor = NSMakePoint(
        plot.origin.x + std::clamp(cursorX, 0.0f, 1.0f) * plot.size.width,
        NSMaxY(plot) - std::clamp(cursorY, 0.0f, 1.0f) * plot.size.height);
    const bool hasTarget = std::isfinite(targetX) && std::isfinite(targetY);
    const NSPoint target = hasTarget ? NSMakePoint(
        plot.origin.x + std::clamp(targetX, 0.0f, 1.0f) * plot.size.width,
        NSMaxY(plot) - std::clamp(targetY, 0.0f, 1.0f) * plot.size.height)
        : cursor;
    std::vector<NSPoint> sites;
    sites.reserve(count);
    for (uint32_t index = 0u; index < count; ++index) {
        const auto& cell = surface.cells[index];
        sites.push_back(NSMakePoint(
            plot.origin.x + cell.x * plot.size.width,
            NSMaxY(plot) - cell.y * plot.size.height));
    }

    [NSGraphicsContext saveGraphicsState];
    [[NSBezierPath bezierPathWithRect:NSInsetRect(plot, 1.0, 1.0)] addClip];
    [[NSColor colorWithCalibratedRed:0.035 green:0.038 blue:0.041 alpha:1.0]
        setFill];
    NSRectFill(plot);

    const std::vector<NSPoint> bounds {
        NSMakePoint(NSMinX(plot), NSMinY(plot)),
        NSMakePoint(NSMaxX(plot), NSMinY(plot)),
        NSMakePoint(NSMaxX(plot), NSMaxY(plot)),
        NSMakePoint(NSMinX(plot), NSMaxY(plot)),
    };
    for (uint32_t index = 0u; index < count; ++index) {
        std::vector<NSPoint> polygon = bounds;
        for (uint32_t other = 0u; other < count && !polygon.empty(); ++other) {
            if (other == index) continue;
            polygon = clipParameterSurfacePolygon(
                polygon, sites[index], sites[other]);
        }
        NSBezierPath* path = parameterSurfacePolygonPath(polygon);
        if (!path) continue;
        [parameterSurfaceCellColor(index, 0.36) setFill];
        [path fill];
        [[NSColor colorWithCalibratedRed:0.48 green:0.51 blue:0.53
            alpha:0.78] setStroke];
        [path setLineWidth:1.5];
        [path stroke];
    }

    [[NSColor colorWithCalibratedRed:0.52 green:0.56 blue:0.58
        alpha:0.16] setStroke];
    for (uint32_t division = 1u; division < 8u; ++division) {
        const CGFloat x = plot.origin.x
            + plot.size.width * static_cast<CGFloat>(division) / 8.0;
        const CGFloat y = plot.origin.y
            + plot.size.height * static_cast<CGFloat>(division) / 8.0;
        [NSBezierPath strokeLineFromPoint:NSMakePoint(x, NSMinY(plot))
            toPoint:NSMakePoint(x, NSMaxY(plot))];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(NSMinX(plot), y)
            toPoint:NSMakePoint(NSMaxX(plot), y)];
    }

    for (uint32_t index = 0u; index < count; ++index) {
        const NSPoint site = sites[index];
        const CGFloat distance = std::hypot(site.x - cursor.x, site.y - cursor.y);
        const CGFloat radius = std::clamp(
            42.0 / std::max(0.25, distance / 42.0 + 0.25), 8.0, 42.0);
        [parameterSurfaceCellColor(index, 0.16) setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
            site.x - radius * 1.9, site.y - radius * 1.9,
            radius * 3.8, radius * 3.8)] fill];
        [parameterSurfaceCellColor(index, 0.30) setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
            site.x - radius, site.y - radius, radius * 2.0, radius * 2.0)] fill];

        const bool selected = static_cast<int>(index) == selectedCell;
        const CGFloat nodeRadius = selected ? 6.0 : 4.5;
        [parameterSurfaceCellColor(index, 0.95) setFill];
        NSBezierPath* node = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
            site.x - nodeRadius, site.y - nodeRadius,
            nodeRadius * 2.0, nodeRadius * 2.0)];
        [node fill];
        [[NSColor colorWithCalibratedRed:(selected ? 0.86 : 0.35)
            green:(selected ? 0.90 : 0.38)
            blue:(selected ? 0.92 : 0.40) alpha:1.0] setStroke];
        [node setLineWidth:selected ? 2.0 : 1.0];
        [node stroke];

        NSString* label = surface.cells[index].name[0] != '\0'
            ? [NSString stringWithUTF8String:surface.cells[index].name]
            : [NSString stringWithFormat:@"CELL %u", index + 1u];
        if ([label length] > 18u) {
            label = [[label substringToIndex:16u] stringByAppendingString:@"..."];
        }
        [label drawAtPoint:NSMakePoint(site.x + nodeRadius + 5.0, site.y - 7.0)
            withAttributes:labelAttrs];
    }

    [[NSColor colorWithCalibratedRed:0.78 green:0.84 blue:0.86 alpha:1.0]
        setStroke];
    NSBezierPath* cross = [NSBezierPath bezierPath];
    [cross moveToPoint:NSMakePoint(cursor.x - 13.0, cursor.y)];
    [cross lineToPoint:NSMakePoint(cursor.x + 13.0, cursor.y)];
    [cross moveToPoint:NSMakePoint(cursor.x, cursor.y - 13.0)];
    [cross lineToPoint:NSMakePoint(cursor.x, cursor.y + 13.0)];
    [cross setLineWidth:2.0];
    [cross stroke];
    NSBezierPath* cursorBox = [NSBezierPath bezierPathWithRect:NSMakeRect(
        cursor.x - 5.0, cursor.y - 5.0, 10.0, 10.0)];
    [cursorBox setLineWidth:1.5];
    [cursorBox stroke];
    if (hasTarget && std::hypot(target.x - cursor.x,
            target.y - cursor.y) > 2.0) {
        [[NSColor colorWithCalibratedRed:0.62 green:0.66 blue:0.68
            alpha:0.72] setStroke];
        NSBezierPath* targetMarker = [NSBezierPath bezierPath];
        [targetMarker moveToPoint:NSMakePoint(target.x, target.y - 6.0)];
        [targetMarker lineToPoint:NSMakePoint(target.x + 6.0, target.y)];
        [targetMarker lineToPoint:NSMakePoint(target.x, target.y + 6.0)];
        [targetMarker lineToPoint:NSMakePoint(target.x - 6.0, target.y)];
        [targetMarker closePath];
        [targetMarker setLineWidth:1.2];
        [targetMarker stroke];
    }
    [NSGraphicsContext restoreGraphicsState];

    [[NSColor colorWithCalibratedRed:0.35 green:0.38 blue:0.40 alpha:1.0]
        setStroke];
    NSBezierPath* border = [NSBezierPath bezierPathWithRect:plot];
    [border setLineWidth:1.4];
    [border stroke];
}

} // namespace s3g::clap_gui
#endif
