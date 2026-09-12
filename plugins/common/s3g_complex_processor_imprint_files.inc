// The field mapping below is retained from parseImprintData in the Cocoa
// wrapper. Only Foundation JSON access is replaced by the shared UTF-8 reader.
using Json = rapidjson::Value;
namespace imprintFiles = s3g::portable_gui::encoder_files;
const Json *dictionaryValue(const Json *d, const char *key) {
  if (!d)
    return nullptr;
  const auto &v = imprintFiles::member(*d, key);
  return v.IsObject() ? &v : nullptr;
}
const Json *arrayValue(const Json *d, const char *key) {
  if (!d)
    return nullptr;
  const auto &v = imprintFiles::member(*d, key);
  return v.IsArray() ? &v : nullptr;
}
float numberValue(const Json *d, const char *key, float fallback) {
  if (!d)
    return fallback;
  const auto &v = imprintFiles::member(*d, key);
  if (v.IsString()) {
    char *end = nullptr;
    const double number = std::strtod(v.GetString(), &end);
    return std::isfinite(number) &&
                   std::abs(number) <= std::numeric_limits<float>::max()
               ? float(number)
               : fallback;
  }
  return float(imprintFiles::number(v, fallback));
}
uint32_t unsignedValue(const Json *d, const char *key, uint32_t fallback) {
  if (!d)
    return fallback;
  const auto &v = imprintFiles::member(*d, key);
  const double number = v.IsString()
                            ? std::strtod(v.GetString(), nullptr)
                            : imprintFiles::number(v, double(fallback));
  if (!std::isfinite(number) || number < 0 || number > double(UINT32_MAX))
    return fallback;
  return uint32_t(number);
}
std::string stringValue(const Json *d, const char *key,
                        const char *fallback = "") {
  return d ? imprintFiles::string(*d, key, fallback) : std::string(fallback);
}
std::vector<std::array<float, 2>> parsePointPairs(const Json *values,
                                                  const char *secondKey,
                                                  uint32_t maximum = 64) {
  std::vector<std::array<float, 2>> result;
  if (!values || !values->IsArray())
    return result;
  const auto count = std::min<uint32_t>(maximum, values->Size());
  result.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    const auto &item = (*values)[i];
    if (item.IsObject())
      result.push_back(
          {numberValue(&item, "x", 0), numberValue(&item, secondKey, 0)});
  }
  return result;
}
std::array<float, 3> parseVector3(const Json *value,
                                  std::array<float, 3> fallback = {}) {
  if (!value || !value->IsObject())
    return fallback;
  return {numberValue(value, "x", fallback[0]),
          numberValue(value, "y", fallback[1]),
          numberValue(value, "z", fallback[2])};
}
std::vector<std::array<float, 3>> parsePointTriples(const Json *values,
                                                    uint32_t maximum = 48) {
  std::vector<std::array<float, 3>> result;
  if (!values || !values->IsArray())
    return result;
  const auto count = std::min<uint32_t>(maximum, values->Size());
  result.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    const auto &item = (*values)[i];
    if (item.IsObject())
      result.push_back(parseVector3(&item));
  }
  return result;
}
float defaultBranchHeight(float roomHeight, const std::string &family,
                          uint32_t level) {
  float ratio = .72f;
  if (family == "cave")
    ratio = .66f;
  else if (family == "cavern")
    ratio = 1.02f;
  else if (family == "tunnel")
    ratio = .52f;
  else if (family == "canyon")
    ratio = 1.10f;
  else if (family == "clearing")
    ratio = .34f;
  else if (family == "abstract")
    ratio = .82f;
  return roomHeight * std::clamp(ratio + float(level) * .05f, .28f, 1.18f);
}
bool parseBandArray(const Json *values,
                    std::array<float, s3g::kAmbiImprintBands> &destination) {
  if (!values || !values->IsArray() || values->Size() != s3g::kAmbiImprintBands)
    return false;
  for (uint32_t i = 0; i < s3g::kAmbiImprintBands; ++i) {
    const auto &value = (*values)[i];
    if (!value.IsNumber() && !value.IsBool() && !value.IsString())
      return false;
    const double n = value.IsString() ? std::strtod(value.GetString(), nullptr)
                                      : imprintFiles::number(value);
    if (!std::isfinite(n) || std::abs(n) > std::numeric_limits<float>::max())
      return false;
    destination[i] = float(n);
  }
  return true;
}
bool parseImprintData(const std::string &data,
                      s3g::AmbiImprintDescriptor &descriptor,
                      GuiSpaceGeometry &geometry, std::string &json,
                      std::string &error) {
  if (data.empty() || data.size() > kMaximumStateJsonBytes) {
    error = "FILE SIZE IS INVALID";
    return false;
  }
  rapidjson::Document document;
  if (!imprintFiles::parse(data, document)) {
    error = "INVALID JSON / UTF-8";
    return false;
  }
  const Json *root = &document;
  if (stringValue(root, "format") != "s3g-ambi-imprint") {
    error = "NOT AN S3G IMPRINT";
    return false;
  }
  if (unsignedValue(root, "version", 0) != s3g::kAmbiImprintFormatVersion) {
    error = "UNSUPPORTED FORMAT VERSION";
    return false;
  }
  const Json *profiles = arrayValue(root, "profiles");
  if (!profiles || profiles->Empty() ||
      profiles->Size() > s3g::kAmbiImprintMaxProfiles) {
    error = "EXPECTED 1-8 PROFILES";
    return false;
  }
  s3g::AmbiImprintDescriptor parsed;
  GuiSpaceGeometry parsedGeometry;
  parsed.version = s3g::kAmbiImprintFormatVersion;
  parsed.durationSeconds = numberValue(root, "duration_s", 2.0f);
  if (const Json *ambisonics = dictionaryValue(root, "ambisonics"))
    parsed.referenceOrder = unsignedValue(ambisonics, "reference_order", 3u);
  std::vector<std::array<float, 2>> primarySpacePolygon;
  if (const Json *space = dictionaryValue(root, "space")) {
    parsedGeometry.family = stringValue(space, "family", "room");
    primarySpacePolygon =
        parsePointPairs(arrayValue(space, "primary_polygon_xy_m"), "y");
    parsedGeometry.ceilingProfile =
        parsePointPairs(arrayValue(space, "ceiling_profile_xz_m"), "z");
    if (const Json *regions = arrayValue(space, "regions")) {
      const uint32_t count =
          std::min<uint32_t>(64u, static_cast<uint32_t>(regions->Size()));
      parsedGeometry.branches.reserve(count);
      for (uint32_t i = 0; i < count; ++i) {
        const auto &item = (*regions)[i];
        if (!item.IsObject())
          continue;
        auto *source = &item;
        if (stringValue(source, "kind", "branch") == "primary")
          continue;
        GuiBranchGeometry branch;
        branch.family =
            stringValue(source, "family", parsedGeometry.family.c_str());
        branch.attachment = stringValue(source, "attachment", "wall");
        branch.level = unsignedValue(source, "level", 0u);
        branch.baseHeight = numberValue(source, "base_z_m", 0.0f);
        branch.height =
            numberValue(source, "height_m",
                        defaultBranchHeight(parsed.room.heightMetres,
                                            branch.family, branch.level));
        branch.polygon =
            parsePointPairs(arrayValue(source, "polygon_xy_m"), "y");
        if (branch.polygon.size() >= 3u)
          parsedGeometry.branches.push_back(std::move(branch));
      }
    }
    if (const Json *portals = arrayValue(space, "portals")) {
      const uint32_t count =
          std::min<uint32_t>(64u, static_cast<uint32_t>(portals->Size()));
      parsedGeometry.portals.reserve(count);
      for (uint32_t i = 0; i < count; ++i) {
        const auto &item = (*portals)[i];
        if (!item.IsObject())
          continue;
        auto *source = &item;
        GuiPortalGeometry portal;
        portal.shape = stringValue(source, "shape", "rect");
        portal.center = parseVector3(dictionaryValue(source, "center_m"));
        portal.normal =
            parseVector3(dictionaryValue(source, "normal"), {0.0f, 1.0f, 0.0f});
        portal.axisU =
            parseVector3(dictionaryValue(source, "axis_u"), {1.0f, 0.0f, 0.0f});
        portal.axisV =
            parseVector3(dictionaryValue(source, "axis_v"), {0.0f, 0.0f, 1.0f});
        portal.width = std::max(0.01f, numberValue(source, "width_m", 1.0f));
        portal.height = std::max(0.01f, numberValue(source, "height_m", 1.0f));
        portal.outline = parsePointTriples(arrayValue(source, "outline_xyz_m"));
        if (portal.outline.size() < 3u) {
          for (const auto &corner : std::array<std::array<float, 2>, 4>{
                   std::array<float, 2>{-0.5f, -0.5f},
                   {0.5f, -0.5f},
                   {0.5f, 0.5f},
                   {-0.5f, 0.5f}}) {
            portal.outline.push_back(
                {portal.center[0] + portal.axisU[0] * corner[0] * portal.width +
                     portal.axisV[0] * corner[1] * portal.height,
                 portal.center[1] + portal.axisU[1] * corner[0] * portal.width +
                     portal.axisV[1] * corner[1] * portal.height,
                 portal.center[2] + portal.axisU[2] * corner[0] * portal.width +
                     portal.axisV[2] * corner[1] * portal.height});
          }
        }
        parsedGeometry.portals.push_back(std::move(portal));
      }
    }
  }
  if (const Json *room = dictionaryValue(root, "room")) {
    if (parsedGeometry.family.empty() || parsedGeometry.family == "room")
      parsedGeometry.family =
          stringValue(room, "family", parsedGeometry.family.c_str());
    if (const Json *dimensions = dictionaryValue(room, "dimensions_m")) {
      parsed.room.widthMetres = numberValue(dimensions, "x", 8.0f);
      parsed.room.depthMetres = numberValue(dimensions, "y", 10.0f);
      parsed.room.heightMetres = numberValue(dimensions, "z", 3.0f);
    }
    parsed.room.polygon =
        parsePointPairs(arrayValue(room, "polygon_xy_m"), "y");
    if (parsedGeometry.branches.empty())
      if (const Json *chamber = dictionaryValue(room, "chamber")) {
        if (const Json *branches = arrayValue(chamber, "chambers")) {
          const uint32_t count =
              std::min<uint32_t>(64u, static_cast<uint32_t>(branches->Size()));
          parsedGeometry.branches.reserve(count);
          for (uint32_t i = 0; i < count; ++i) {
            const auto &item = (*branches)[i];
            if (!item.IsObject())
              continue;
            auto *source = &item;
            GuiBranchGeometry branch;
            branch.family =
                stringValue(source, "family", parsedGeometry.family.c_str());
            branch.attachment =
                stringValue(source, "attachment",
                            stringValue(source, "side", "wall").c_str());
            branch.level = unsignedValue(source, "level", 0u);
            branch.baseHeight = numberValue(source, "base_z_m", 0.0f);
            branch.height =
                numberValue(source, "height_m",
                            defaultBranchHeight(parsed.room.heightMetres,
                                                branch.family, branch.level));
            branch.polygon =
                parsePointPairs(arrayValue(source, "polygon"), "y");
            if (branch.polygon.size() >= 3u)
              parsedGeometry.branches.push_back(std::move(branch));
          }
        }
      }
  }
  if (parsed.room.polygon.size() < 3u && primarySpacePolygon.size() >= 3u)
    parsed.room.polygon = std::move(primarySpacePolygon);
  if (parsedGeometry.ceilingProfile.size() < 2u) {
    parsedGeometry.ceilingProfile = {
        {0.0f, parsed.room.heightMetres},
        {parsed.room.widthMetres, parsed.room.heightMetres}};
  }
  if (const Json *listener = dictionaryValue(root, "listener_position_m")) {
    parsed.listenerPositionMetres = {
        numberValue(listener, "x", parsed.room.widthMetres * 0.5f),
        numberValue(listener, "y", parsed.room.depthMetres * 0.5f),
        numberValue(listener, "z", parsed.room.heightMetres * 0.5f)};
  }

  for (const auto &item : profiles->GetArray()) {
    if (!item.IsObject())
      continue;
    auto *source = &item;
    s3g::AmbiImprintProfile profile;
    if (const Json *direction = dictionaryValue(source, "input_direction")) {
      profile.azimuthDeg = numberValue(direction, "azimuth_deg", 0.0f);
      profile.elevationDeg = numberValue(direction, "elevation_deg", 0.0f);
    }
    if (const Json *position = dictionaryValue(source, "source_position_m")) {
      profile.sourceXMetres = numberValue(position, "x", 0.0f);
      profile.sourceYMetres = numberValue(position, "y", 0.0f);
      profile.sourceZMetres = numberValue(position, "z", 0.0f);
    }
    profile.weight = numberValue(source, "weight",
                                 1.0f / static_cast<float>(profiles->Size()));
    if (const Json *direct = dictionaryValue(source, "direct")) {
      profile.directDelayMs = numberValue(direct, "delay_ms", 0.0f);
      profile.directGain = numberValue(direct, "gain", 1.0f);
    }
    if (const Json *reflections = arrayValue(source, "early_reflections")) {
      const uint32_t count =
          std::min<uint32_t>(128u, static_cast<uint32_t>(reflections->Size()));
      profile.earlyReflections.reserve(count);
      for (uint32_t i = 0; i < count; ++i) {
        const auto &reflectionObject = (*reflections)[i];
        if (!reflectionObject.IsObject())
          continue;
        auto *reflection = &reflectionObject;
        profile.earlyReflections.push_back(
            {numberValue(reflection, "delay_ms", 0.0f),
             numberValue(reflection, "gain", 0.0f),
             numberValue(reflection, "azimuth_deg", 0.0f),
             numberValue(reflection, "elevation_deg", 0.0f)});
      }
    }
    const Json *late = dictionaryValue(source, "late");
    if (!late ||
        !parseBandArray(arrayValue(late, "absorption_by_band"),
                        profile.late.absorptionByBand) ||
        !parseBandArray(arrayValue(late, "rt60_s_by_band"),
                        profile.late.rt60SecondsByBand)) {
      error = "PROFILE BAND DATA IS INVALID";
      return false;
    }
    profile.late.startMs = numberValue(late, "start_ms", 40.0f);
    profile.late.durationSeconds =
        numberValue(late, "duration_s", parsed.durationSeconds);
    profile.late.level = numberValue(late, "level", 0.15f);
    profile.late.diffusion = numberValue(late, "diffusion", 0.5f);
    profile.late.spreadDeg = numberValue(late, "spread_deg", 45.0f);
    profile.late.highFrequencyDamping =
        numberValue(late, "high_frequency_damping", 0.4f);
    profile.late.seed = unsignedValue(late, "seed", 1u);
    parsed.profiles.push_back(std::move(profile));
  }
  if (parsed.profiles.empty()) {
    error = "NO VALID PROFILES";
    return false;
  }
  descriptor = s3g::sanitizeAmbiImprintDescriptor(std::move(parsed));
  geometry = std::move(parsedGeometry);
  json = data;
  return true;
}
