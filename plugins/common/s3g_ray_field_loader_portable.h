#pragma once
#include "s3g_ambi_ray_encoder.h"
#include "s3g_input_encoder_files.h"

// Literal runtime schema from s3g_ray_field_loader_macos.h, including the
// legacy Ray Sketch azimuth correction and display-provenance geometry.
namespace s3g::portable_gui::ray_field_files {
constexpr uint32_t kMaximumJsonBytes = 8u * 1024u * 1024u;
constexpr const char *kWorldToAedConvention =
    "azimuth_deg=atan2(-x_right,y_front)";
using Json = rapidjson::Value;
struct Region {
  std::string family = "room";
  std::string attachment = "wall";
  float baseHeight = 0.0f;
  float height = 3.0f;
  std::vector<std::array<float, 2>> polygon;
};

struct Portal {
  Vec3 center{};
  float width = 1.f, height = 2.f;
};
struct VisualGeometry {
  std::string family = "room";
  std::vector<Region> regions;
  std::vector<Vec3> portals;
  std::vector<Portal> portalDetails;
};

inline const Json *member(const Json *object, const char *key) {
  if (!object)
    return nullptr;
  const auto &v = encoder_files::member(*object, key);
  return v.IsNull() ? nullptr : &v;
}
inline float number(const Json *object, const char *key, float fallback) {
  auto *v = member(object, key);
  return v ? float(encoder_files::number(*v, fallback)) : fallback;
}
inline uint32_t unsignedNumber(const Json *object, const char *key,
                               uint32_t fallback) {
  auto *v = member(object, key);
  if (!v || !v->IsNumber())
    return fallback;
  double n = v->GetDouble();
  return std::isfinite(n) && n >= 0 && n <= double(UINT32_MAX) ? uint32_t(n)
                                                               : fallback;
}
inline const Json *dictionary(const Json *object, const char *key) {
  auto *v = member(object, key);
  return v && v->IsObject() ? v : nullptr;
}
inline const Json *array(const Json *object, const char *key) {
  auto *v = member(object, key);
  return v && v->IsArray() ? v : nullptr;
}
inline std::string string(const Json *object, const char *key,
                          const char *fallback = "") {
  return object ? encoder_files::string(*object, key, fallback) : fallback;
}
inline Vec3 vector3(const Json *v, Vec3 fallback = {}) {
  if (!v || !v->IsObject())
    return fallback;
  return {number(v, "x", fallback.x), number(v, "y", fallback.y),
          number(v, "z", fallback.z)};
}
inline std::vector<std::array<float, 2>>
pointPairs(const Json *values, const char *key, uint32_t maximum = 64) {
  std::vector<std::array<float, 2>> result;
  if (!values || !values->IsArray())
    return result;
  const auto count = std::min<unsigned>(maximum, values->Size());
  result.reserve(count);
  for (unsigned i = 0; i < count; ++i) {
    auto *v = &(*values)[i];
    if (v->IsObject())
      result.push_back({number(v, "x", 0.f), number(v, key, 0.f)});
  }
  return result;
}
inline bool parse(const std::string &data, AmbiRayDescriptor &descriptor,
                  VisualGeometry &visual, std::string &json,
                  std::string &error) {
  if (data.empty() || data.size() > kMaximumJsonBytes) {
    error = "FILE SIZE IS INVALID";
    return false;
  }
  rapidjson::Document doc;
  if (!encoder_files::parse(data, doc)) {
    error = "INVALID JSON";
    return false;
  }
  const auto *root = &doc;
  if (string(root, "format") != "s3g-ambi-ray-field") {
    error = "NOT AN S3G RAY FIELD";
    return false;
  }
  if (unsignedNumber(root, "version", 0) != kAmbiRayFormatVersion) {
    error = "UNSUPPORTED FORMAT VERSION";
    return false;
  }
  const Json *cells = array(root, "cells");
  if (!cells || cells->Size() == 0u || cells->Size() > kAmbiRayMaxCells) {
    error = "EXPECTED 1-256 RAY CELLS";
    return false;
  }

  bool invertLegacyAzimuth = false;
  if (const Json *generator = dictionary(root, "generator")) {
    if (string(generator, "name") == "s3g-mc Ray Sketch") {
      const Json *coordinates = dictionary(root, "coordinate_system");
      invertLegacyAzimuth =
          !coordinates ||
          string(coordinates, "world_to_aed") != kWorldToAedConvention;
    }
  }

  AmbiRayDescriptor parsed;
  VisualGeometry parsedVisual;
  parsed.durationSeconds = number(root, "duration_s", 3.0f);
  std::vector<std::array<float, 2>> spacePolygon;
  if (const Json *space = dictionary(root, "space")) {
    parsedVisual.family = string(space, "family", "room");
    spacePolygon = pointPairs(array(space, "primary_polygon_xy_m"), "y");
    if (const Json *regions = array(space, "regions")) {
      const uint32_t count =
          std::min<uint32_t>(64u, static_cast<uint32_t>(regions->Size()));
      for (uint32_t index = 0u; index < count; ++index) {
        const auto *item = &(*regions)[index];
        if (!(item && item->IsObject()))
          continue;
        auto *regionData = item;
        if (string(regionData, "kind", "branch") == "primary")
          continue;
        Region region;
        region.family =
            string(regionData, "family", parsedVisual.family.c_str());
        region.attachment = string(regionData, "attachment", "wall");
        region.baseHeight = number(regionData, "base_z_m", 0.0f);
        region.height = number(regionData, "height_m", 3.0f);
        region.polygon = pointPairs(array(regionData, "polygon_xy_m"), "y");
        if (region.polygon.size() >= 3u)
          parsedVisual.regions.push_back(std::move(region));
      }
    }
    if (const Json *portals = array(space, "portals")) {
      const uint32_t count =
          std::min<uint32_t>(64u, static_cast<uint32_t>(portals->Size()));
      for (uint32_t index = 0u; index < count; ++index) {
        const auto *item = &(*portals)[index];
        if (!(item && item->IsObject()))
          continue;
        const auto center = vector3(dictionary(item, "center_m"));
        parsedVisual.portals.push_back(center);
        parsedVisual.portalDetails.push_back({center,
                                              number(item, "width_m", 1.f),
                                              number(item, "height_m", 2.f)});
      }
    }
  }
  if (const Json *room = dictionary(root, "room")) {
    parsedVisual.family = string(room, "family", parsedVisual.family.c_str());
    if (const Json *dimensions = dictionary(room, "dimensions_m")) {
      parsed.room.widthMetres = number(dimensions, "x", 8.0f);
      parsed.room.depthMetres = number(dimensions, "y", 10.0f);
      parsed.room.heightMetres = number(dimensions, "z", 3.0f);
    }
    parsed.room.polygon = pointPairs(array(room, "polygon_xy_m"), "y");
    parsed.room.ceilingProfile =
        pointPairs(array(room, "ceiling_profile_xz_m"), "z");
    if (const Json *bounds = dictionary(room, "navigation_bounds_m")) {
      parsed.room.navigationMinimumMetres =
          vector3(dictionary(bounds, "minimum"), {0.0f, 0.0f, 0.0f});
      parsed.room.navigationMaximumMetres =
          vector3(dictionary(bounds, "maximum"),
                  {parsed.room.widthMetres, parsed.room.depthMetres,
                   parsed.room.heightMetres});
    } else {
      parsed.room.navigationMinimumMetres = {0.0f, 0.0f, 0.0f};
      parsed.room.navigationMaximumMetres = {parsed.room.widthMetres,
                                             parsed.room.depthMetres,
                                             parsed.room.heightMetres};
    }
  }
  if (parsed.room.polygon.size() < 3u)
    parsed.room.polygon = std::move(spacePolygon);
  if (parsed.room.ceilingProfile.size() < 2u) {
    parsed.room.ceilingProfile = {
        {0.0f, parsed.room.heightMetres},
        {parsed.room.widthMetres, parsed.room.heightMetres}};
  }
  parsed.listenerPositionMetres =
      vector3(dictionary(root, "listener_position_m"),
              {parsed.room.widthMetres * 0.5f, parsed.room.depthMetres * 0.5f,
               parsed.room.heightMetres * 0.5f});
  parsed.defaultSourcePositionMetres =
      vector3(dictionary(root, "default_source_position_m"),
              {parsed.room.widthMetres * 0.5f, parsed.room.depthMetres * 0.25f,
               parsed.room.heightMetres * 0.5f});

  const uint32_t cellCount = static_cast<uint32_t>(cells->Size());
  parsed.cells.reserve(cellCount);
  for (uint32_t cellIndex = 0u; cellIndex < cellCount; ++cellIndex) {
    const auto *item = &(*cells)[cellIndex];
    if (!(item && item->IsObject()))
      continue;
    auto *source = item;
    AmbiRayCell cell;
    cell.positionMetres = vector3(dictionary(source, "position_m"),
                                  parsed.defaultSourcePositionMetres);
    const Json *reflections = array(source, "early_reflections");
    if (!reflections)
      reflections = array(source, "reflections");
    const uint32_t reflectionCount = std::min<uint32_t>(
        kAmbiRayMaxReflections,
        reflections ? static_cast<uint32_t>(reflections->Size()) : 0u);
    cell.reflections.reserve(reflectionCount);
    for (uint32_t reflectionIndex = 0u; reflectionIndex < reflectionCount;
         ++reflectionIndex) {
      const auto *reflectionItem = &(*reflections)[reflectionIndex];
      if (!(reflectionItem && reflectionItem->IsObject()))
        continue;
      auto *reflection = reflectionItem;
      float azimuth = number(reflection, "azimuth_deg", 0.0f);
      if (invertLegacyAzimuth)
        azimuth = -azimuth;
      AmbiRayReflection event{
          unsignedNumber(reflection, "slot", reflectionIndex),
          number(reflection, "delay_ms", 20.0f),
          number(reflection, "gain", 0.0f),
          azimuth,
          number(reflection, "elevation_deg", 0.0f),
          number(reflection, "damping", 0.25f)};
      if (const Json *bounce = dictionary(reflection, "bounce_position_m")) {
        event.bouncePositionMetres = vector3(bounce);
        event.hasBouncePosition = true;
      }
      cell.reflections.push_back(event);
    }
    if (const Json *late = dictionary(source, "late")) {
      cell.late.startMs = number(late, "start_ms", 45.0f);
      cell.late.decaySeconds = number(late, "decay_s", 1.8f);
      cell.late.level = number(late, "level", 0.18f);
      cell.late.diffusion = number(late, "diffusion", 0.72f);
      cell.late.damping = number(late, "damping", 0.38f);
    }
    parsed.cells.push_back(std::move(cell));
  }
  if (parsed.cells.empty()) {
    error = "NO VALID RAY CELLS";
    return false;
  }
  descriptor = sanitizeAmbiRayDescriptor(std::move(parsed));
  visual = std::move(parsedVisual);

  json = data;
  error.clear();
  return true;
}
} // namespace s3g::portable_gui::ray_field_files
