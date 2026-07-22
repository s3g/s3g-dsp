#pragma once

#if defined(__APPLE__)

#import <Cocoa/Cocoa.h>

#include "s3g_ambi_ray_encoder.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace s3g::ray_field_loader {

constexpr uint32_t kMaximumJsonBytes = 8u * 1024u * 1024u;
constexpr const char* kWorldToAedConvention = "azimuth_deg=atan2(-x_right,y_front)";

struct Region {
    std::string family = "room";
    float baseHeight = 0.0f;
    float height = 3.0f;
    std::vector<std::array<float, 2>> polygon;
};

struct VisualGeometry {
    std::string family = "room";
    std::vector<Region> regions;
    std::vector<Vec3> portals;
};

inline float number(NSDictionary* dictionary, NSString* key, float fallback)
{
    id value = [dictionary objectForKey:key];
    return [value respondsToSelector:@selector(doubleValue)] ? static_cast<float>([value doubleValue]) : fallback;
}

inline uint32_t unsignedNumber(NSDictionary* dictionary, NSString* key, uint32_t fallback)
{
    id value = [dictionary objectForKey:key];
    return [value respondsToSelector:@selector(unsignedIntValue)] ? static_cast<uint32_t>([value unsignedIntValue]) : fallback;
}

inline NSDictionary* dictionary(NSDictionary* source, NSString* key)
{
    id value = [source objectForKey:key];
    return [value isKindOfClass:[NSDictionary class]] ? static_cast<NSDictionary*>(value) : nil;
}

inline NSArray* array(NSDictionary* source, NSString* key)
{
    id value = [source objectForKey:key];
    return [value isKindOfClass:[NSArray class]] ? static_cast<NSArray*>(value) : nil;
}

inline std::string string(NSDictionary* dictionary, NSString* key, const char* fallback = "")
{
    id value = [dictionary objectForKey:key];
    if (![value isKindOfClass:[NSString class]]) return fallback ? std::string(fallback) : std::string {};
    const char* text = [static_cast<NSString*>(value) UTF8String];
    return text ? std::string(text) : (fallback ? std::string(fallback) : std::string {});
}

inline Vec3 vector3(NSDictionary* value, Vec3 fallback = {})
{
    if (![value isKindOfClass:[NSDictionary class]]) return fallback;
    return { number(value, @"x", fallback.x), number(value, @"y", fallback.y), number(value, @"z", fallback.z) };
}

inline std::vector<std::array<float, 2>> pointPairs(NSArray* values, NSString* secondKey, uint32_t maximum = 64u)
{
    std::vector<std::array<float, 2>> result;
    if (![values isKindOfClass:[NSArray class]]) return result;
    const uint32_t count = std::min<uint32_t>(maximum, static_cast<uint32_t>([values count]));
    result.reserve(count);
    for (uint32_t index = 0u; index < count; ++index) {
        id item = [values objectAtIndex:index];
        if (![item isKindOfClass:[NSDictionary class]]) continue;
        auto* point = static_cast<NSDictionary*>(item);
        result.push_back({ number(point, @"x", 0.0f), number(point, secondKey, 0.0f) });
    }
    return result;
}

inline bool parse(NSData* data,
                  AmbiRayDescriptor& descriptor,
                  VisualGeometry& visual,
                  std::string& json,
                  std::string& error)
{
    if (!data || [data length] == 0u || [data length] > kMaximumJsonBytes) {
        error = "FILE SIZE IS INVALID";
        return false;
    }
    NSError* parseError = nil;
    id rootObject = [NSJSONSerialization JSONObjectWithData:data options:0 error:&parseError];
    if (parseError || ![rootObject isKindOfClass:[NSDictionary class]]) {
        error = "INVALID JSON";
        return false;
    }
    auto* root = static_cast<NSDictionary*>(rootObject);
    NSString* format = [root objectForKey:@"format"];
    if (![format isKindOfClass:[NSString class]] || ![format isEqualToString:@"s3g-ambi-ray-field"]) {
        error = "NOT AN S3G RAY FIELD";
        return false;
    }
    if (unsignedNumber(root, @"version", 0u) != kAmbiRayFormatVersion) {
        error = "UNSUPPORTED FORMAT VERSION";
        return false;
    }
    NSArray* cells = array(root, @"cells");
    if (!cells || [cells count] == 0u || [cells count] > kAmbiRayMaxCells) {
        error = "EXPECTED 1-256 RAY CELLS";
        return false;
    }

    bool invertLegacyAzimuth = false;
    if (NSDictionary* generator = dictionary(root, @"generator")) {
        if (string(generator, @"name") == "s3g-mc Ray Sketch") {
            NSDictionary* coordinates = dictionary(root, @"coordinate_system");
            invertLegacyAzimuth = !coordinates
                || string(coordinates, @"world_to_aed") != kWorldToAedConvention;
        }
    }

    AmbiRayDescriptor parsed;
    VisualGeometry parsedVisual;
    parsed.durationSeconds = number(root, @"duration_s", 3.0f);
    std::vector<std::array<float, 2>> spacePolygon;
    if (NSDictionary* space = dictionary(root, @"space")) {
        parsedVisual.family = string(space, @"family", "room");
        spacePolygon = pointPairs(array(space, @"primary_polygon_xy_m"), @"y");
        if (NSArray* regions = array(space, @"regions")) {
            const uint32_t count = std::min<uint32_t>(64u, static_cast<uint32_t>([regions count]));
            for (uint32_t index = 0u; index < count; ++index) {
                id item = [regions objectAtIndex:index];
                if (![item isKindOfClass:[NSDictionary class]]) continue;
                auto* regionData = static_cast<NSDictionary*>(item);
                if (string(regionData, @"kind", "branch") == "primary") continue;
                Region region;
                region.family = string(regionData, @"family", parsedVisual.family.c_str());
                region.baseHeight = number(regionData, @"base_z_m", 0.0f);
                region.height = number(regionData, @"height_m", 3.0f);
                region.polygon = pointPairs(array(regionData, @"polygon_xy_m"), @"y");
                if (region.polygon.size() >= 3u) parsedVisual.regions.push_back(std::move(region));
            }
        }
        if (NSArray* portals = array(space, @"portals")) {
            const uint32_t count = std::min<uint32_t>(64u, static_cast<uint32_t>([portals count]));
            for (uint32_t index = 0u; index < count; ++index) {
                id item = [portals objectAtIndex:index];
                if (![item isKindOfClass:[NSDictionary class]]) continue;
                parsedVisual.portals.push_back(vector3(dictionary(static_cast<NSDictionary*>(item), @"center_m")));
            }
        }
    }
    if (NSDictionary* room = dictionary(root, @"room")) {
        parsedVisual.family = string(room, @"family", parsedVisual.family.c_str());
        if (NSDictionary* dimensions = dictionary(room, @"dimensions_m")) {
            parsed.room.widthMetres = number(dimensions, @"x", 8.0f);
            parsed.room.depthMetres = number(dimensions, @"y", 10.0f);
            parsed.room.heightMetres = number(dimensions, @"z", 3.0f);
        }
        parsed.room.polygon = pointPairs(array(room, @"polygon_xy_m"), @"y");
        parsed.room.ceilingProfile = pointPairs(array(room, @"ceiling_profile_xz_m"), @"z");
        if (NSDictionary* bounds = dictionary(room, @"navigation_bounds_m")) {
            parsed.room.navigationMinimumMetres = vector3(dictionary(bounds, @"minimum"), { 0.0f, 0.0f, 0.0f });
            parsed.room.navigationMaximumMetres = vector3(dictionary(bounds, @"maximum"),
                { parsed.room.widthMetres, parsed.room.depthMetres, parsed.room.heightMetres });
        } else {
            parsed.room.navigationMinimumMetres = { 0.0f, 0.0f, 0.0f };
            parsed.room.navigationMaximumMetres = { parsed.room.widthMetres, parsed.room.depthMetres, parsed.room.heightMetres };
        }
    }
    if (parsed.room.polygon.size() < 3u) parsed.room.polygon = std::move(spacePolygon);
    if (parsed.room.ceilingProfile.size() < 2u) {
        parsed.room.ceilingProfile = { { 0.0f, parsed.room.heightMetres }, { parsed.room.widthMetres, parsed.room.heightMetres } };
    }
    parsed.listenerPositionMetres = vector3(dictionary(root, @"listener_position_m"),
        { parsed.room.widthMetres * 0.5f, parsed.room.depthMetres * 0.5f, parsed.room.heightMetres * 0.5f });
    parsed.defaultSourcePositionMetres = vector3(dictionary(root, @"default_source_position_m"),
        { parsed.room.widthMetres * 0.5f, parsed.room.depthMetres * 0.25f, parsed.room.heightMetres * 0.5f });

    const uint32_t cellCount = static_cast<uint32_t>([cells count]);
    parsed.cells.reserve(cellCount);
    for (uint32_t cellIndex = 0u; cellIndex < cellCount; ++cellIndex) {
        id item = [cells objectAtIndex:cellIndex];
        if (![item isKindOfClass:[NSDictionary class]]) continue;
        auto* source = static_cast<NSDictionary*>(item);
        AmbiRayCell cell;
        cell.positionMetres = vector3(dictionary(source, @"position_m"), parsed.defaultSourcePositionMetres);
        NSArray* reflections = array(source, @"early_reflections");
        if (!reflections) reflections = array(source, @"reflections");
        const uint32_t reflectionCount = std::min<uint32_t>(kAmbiRayMaxReflections,
            reflections ? static_cast<uint32_t>([reflections count]) : 0u);
        cell.reflections.reserve(reflectionCount);
        for (uint32_t reflectionIndex = 0u; reflectionIndex < reflectionCount; ++reflectionIndex) {
            id reflectionItem = [reflections objectAtIndex:reflectionIndex];
            if (![reflectionItem isKindOfClass:[NSDictionary class]]) continue;
            auto* reflection = static_cast<NSDictionary*>(reflectionItem);
            float azimuth = number(reflection, @"azimuth_deg", 0.0f);
            if (invertLegacyAzimuth) azimuth = -azimuth;
            AmbiRayReflection event {
                unsignedNumber(reflection, @"slot", reflectionIndex),
                number(reflection, @"delay_ms", 20.0f),
                number(reflection, @"gain", 0.0f),
                azimuth,
                number(reflection, @"elevation_deg", 0.0f),
                number(reflection, @"damping", 0.25f)
            };
            if (NSDictionary* bounce = dictionary(reflection, @"bounce_position_m")) {
                event.bouncePositionMetres = vector3(bounce);
                event.hasBouncePosition = true;
            }
            cell.reflections.push_back(event);
        }
        if (NSDictionary* late = dictionary(source, @"late")) {
            cell.late.startMs = number(late, @"start_ms", 45.0f);
            cell.late.decaySeconds = number(late, @"decay_s", 1.8f);
            cell.late.level = number(late, @"level", 0.18f);
            cell.late.diffusion = number(late, @"diffusion", 0.72f);
            cell.late.damping = number(late, @"damping", 0.38f);
        }
        parsed.cells.push_back(std::move(cell));
    }
    if (parsed.cells.empty()) {
        error = "NO VALID RAY CELLS";
        return false;
    }
    descriptor = sanitizeAmbiRayDescriptor(std::move(parsed));
    visual = std::move(parsedVisual);
    NSString* text = [[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] autorelease];
    if (!text) {
        error = "JSON IS NOT UTF-8";
        return false;
    }
    json.assign([text UTF8String], [text lengthOfBytesUsingEncoding:NSUTF8StringEncoding]);
    return true;
}

} // namespace s3g::ray_field_loader

#endif
