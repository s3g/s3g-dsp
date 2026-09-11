#pragma once
#include "s3g_panner_mesh.h"
#include "s3g_routing_gui.h"
#include "vstgui/thirdparty/rapidjson/include/rapidjson/document.h"
#include "vstgui/thirdparty/rapidjson/include/rapidjson/prettywriter.h"
#include "vstgui/thirdparty/rapidjson/include/rapidjson/stringbuffer.h"
#include <fstream>

namespace s3g::portable_gui::routing {
inline LayoutPannerCustomShape shapeFromName(std::string name) {
  name = canvas::upper(name);
  for (auto shape :
       {LayoutPannerCustomShape::Auto, LayoutPannerCustomShape::Ring,
        LayoutPannerCustomShape::Dome, LayoutPannerCustomShape::Tetra,
        LayoutPannerCustomShape::Octa, LayoutPannerCustomShape::Cube,
        LayoutPannerCustomShape::Icosa, LayoutPannerCustomShape::Dodeca,
        LayoutPannerCustomShape::Geo, LayoutPannerCustomShape::Stack})
    if (name == canvas::upper(layoutPannerCustomShapeName(shape)))
      return shape;
  if (name == "ICOSA")
    return LayoutPannerCustomShape::Icosa;
  return LayoutPannerCustomShape::Auto;
}
inline bool readSpeakerJson(
    const std::string &path,
    std::array<LayoutPannerSpeaker, kLayoutPannerMaxSpeakers> &speakers,
    uint32_t &count, LayoutPannerCustomShape &shape) {
  std::ifstream input(foundation::pathFromUtf8(path.c_str()),
                      std::ios::binary | std::ios::ate);
  if (!input || input.tellg() <= 0 || input.tellg() > 8 * 1024 * 1024)
    return false;
  std::string data(size_t(input.tellg()), '\0');
  input.seekg(0);
  if (!input.read(data.data(), std::streamsize(data.size())))
    return false;
  rapidjson::Document doc;
  doc.Parse<rapidjson::kParseValidateEncodingFlag>(data.data(), data.size());
  if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("speakers") ||
      !doc["speakers"].IsArray() || doc["speakers"].Size() < 2)
    return false;
  const auto &list = doc["speakers"];
  uint32_t requested =
      std::min<uint32_t>(list.Size(), kLayoutPannerMaxSpeakers);
  if (doc.HasMember("speaker_count")) {
    if (!doc["speaker_count"].IsUint())
      return false;
    requested = std::clamp(doc["speaker_count"].GetUint(), 2u,
                           kLayoutPannerMaxSpeakers);
    requested = std::min<uint32_t>(requested, list.Size());
  }
  auto loaded = speakers;
  for (uint32_t i = 0; i < requested; ++i) {
    const auto &item = list[i];
    if (!item.IsObject())
      return false;
    const char *keys[] = {"azimuth", "elevation", "distance"};
    float values[] = {0.f, 0.f, 1.f};
    for (unsigned j = 0; j < 3; ++j)
      if (item.HasMember(keys[j])) {
        if (!item[keys[j]].IsNumber())
          return false;
        const double v = item[keys[j]].GetDouble();
        if (!std::isfinite(v) ||
            std::abs(v) > std::numeric_limits<float>::max())
          return false;
        values[j] = float(v);
      }
    // Avoid unbounded repeated-subtraction wrapping in the original DSP for
    // unusually large, but finite, angles in external layout files.
    loaded[i].azimuthDeg =
        std::abs(values[0]) <= 360.f ? values[0] : std::fmod(values[0], 360.f);
    loaded[i].elevationDeg = values[1];
    loaded[i].distance = values[2];
  }
  auto loadedShape = shape;
  if (doc.HasMember("shape")) {
    if (!doc["shape"].IsString())
      return false;
    loadedShape = shapeFromName(doc["shape"].GetString());
  }
  speakers = loaded;
  count = requested;
  shape = loadedShape;
  return true;
}
inline bool writeSpeakerJson(
    const std::string &path,
    const std::array<LayoutPannerSpeaker, kLayoutPannerMaxSpeakers> &speakers,
    uint32_t count, LayoutPannerCustomShape shape) {
  if (count < 2 || count > kLayoutPannerMaxSpeakers)
    return false;
  rapidjson::StringBuffer buffer;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  writer.StartObject();
  writer.Key("format");
  writer.String("s3g-layout-panner-speakers-v1");
  writer.Key("shape");
  writer.String(layoutPannerCustomShapeName(shape));
  writer.Key("speaker_count");
  writer.Uint(count);
  writer.Key("speakers");
  writer.StartArray();
  for (uint32_t i = 0; i < count; ++i) {
    const auto &s = speakers[i];
    if (!std::isfinite(s.azimuthDeg) || !std::isfinite(s.elevationDeg) ||
        !std::isfinite(s.distance))
      return false;
    writer.StartObject();
    writer.Key("azimuth");
    writer.Double(s.azimuthDeg);
    writer.Key("elevation");
    writer.Double(s.elevationDeg);
    writer.Key("distance");
    writer.Double(s.distance);
    writer.EndObject();
  }
  writer.EndArray();
  writer.EndObject();
  std::ofstream output(foundation::pathFromUtf8(path.c_str()),
                       std::ios::binary);
  output.write(buffer.GetString(), std::streamsize(buffer.GetSize()));
  return bool(output);
}
// The original Cocoa AED source-marker calculation, including its gray mix.
inline CColor pannerAedColor(float az, float el, float distance,
                             bool selected) {
  const float hue = std::fmod(az / 360.f + 1.f, 1.f);
  const float light =
      std::clamp((std::clamp(el, -90.f, 90.f) + 90.f) / 180.f, .30f, .86f);
  const float chroma = std::clamp(distance / 2.6f, .08f, 1.f) * .36f;
  const float a = std::cos(hue * 2.f * float(M_PI)) * chroma,
              b = std::sin(hue * 2.f * float(M_PI)) * chroma;
  const float l3 = light + .3963377774f * a + .2158037573f * b;
  const float m3 = light - .1055613458f * a - .0638541728f * b;
  const float s3 = light - .0894841775f * a - 1.2914855480f * b;
  const float l = l3 * l3 * l3, m = m3 * m3 * m3, s = s3 * s3 * s3;
  const auto srgb = [](float v) {
    v = std::clamp(v, 0.f, 1.f);
    return v <= .0031308f ? v * 12.92f
                          : 1.055f * std::pow(v, 1.f / 2.4f) - .055f;
  };
  const float mix = selected ? .06f : .18f;
  return rgba(srgb(4.0767416621f * l - 3.3077115913f * m + .2309699292f * s) *
                      (1.f - mix) +
                  .74f * mix,
              srgb(-1.2684380046f * l + 2.6097574011f * m - .3413193965f * s) *
                      (1.f - mix) +
                  .74f * mix,
              srgb(-.0041960863f * l - .7034186147f * m + 1.7076147010f * s) *
                      (1.f - mix) +
                  .74f * mix,
              selected ? 1. : .88);
}
} // namespace s3g::portable_gui::routing
