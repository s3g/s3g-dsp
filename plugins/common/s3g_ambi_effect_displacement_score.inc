// The same version-1 score schema and defaults as parseScoreData in Cocoa.
constexpr size_t maximumScoreBytes = 4u * 1024u * 1024u;
const rapidjson::Value *scoreMember(const rapidjson::Value &value,
                                    const char *name) {
  if (!value.IsObject())
    return nullptr;
  const auto member = value.FindMember(name);
  return member == value.MemberEnd() ? nullptr : &member->value;
}
double scoreNumber(const rapidjson::Value &value, const char *key,
                   double fallback) {
  const auto *item = scoreMember(value, key);
  if (!item)
    return fallback;
  if (item->IsNumber())
    return item->GetDouble();
  if (item->IsBool())
    return item->GetBool() ? 1. : 0.;
  if (item->IsString())
    return std::atof(item->GetString());
  return fallback;
}
uint32_t scoreUnsigned(const rapidjson::Value &v, const char *key,
                       uint32_t fallback) {
  const double n = scoreNumber(v, key, fallback);
  return std::isfinite(n) ? static_cast<uint32_t>(static_cast<int64_t>(
                                std::clamp(n, -2147483648., 4294967295.)))
                          : fallback;
}
bool parseScorePoints(
    const rapidjson::Value *values,
    const std::array<s3g::AmbiEffectDisplacementPoint, 24> &fallback,
    std::array<s3g::AmbiEffectDisplacementPoint, 24> &output) {
  if (!values || !values->IsArray() || values->Size() < 24)
    return false;
  output = fallback;
  std::array<bool, 24> seen{};
  for (uint32_t n = 0; n < 24; ++n) {
    const auto &item = (*values)[n];
    if (!item.IsObject())
      return false;
    const uint32_t channel = scoreUnsigned(item, "channel", n + 1);
    if (channel == 0 || channel > 24)
      return false;
    const uint32_t i = channel - 1;
    output[i] = s3g::sanitizeAmbiEffectDisplacementPoint(
        {float(scoreNumber(item, "azimuth", fallback[i].azimuthDeg)),
         float(scoreNumber(item, "elevation", fallback[i].elevationDeg)),
         float(scoreNumber(item, "radius", fallback[i].radius))});
    seen[i] = true;
  }
  return std::all_of(seen.begin(), seen.end(), [](bool v) { return v; });
}
bool parsePortableScore(const std::string &bytes,
                        s3g::AmbiEffectDisplacementScore &score, float &amount,
                        float &azimuthScale, float &elevationScale,
                        float &radiusScale, std::string &name,
                        std::string &error) {
  if (bytes.empty() || bytes.size() > maximumScoreBytes) {
    error = "FILE SIZE IS INVALID";
    return false;
  }
  rapidjson::Document root;
  root.Parse<rapidjson::kParseValidateEncodingFlag>(bytes.data(), bytes.size());
  if (root.HasParseError() || !root.IsObject()) {
    error = "INVALID JSON";
    return false;
  }
  const auto *format = scoreMember(root, "format");
  if (!format || !format->IsString() ||
      std::strcmp(format->GetString(), "s3g-mc-displacement-score")) {
    error = "NOT A DISPLACEMENT SCORE";
    return false;
  }
  if (scoreUnsigned(root, "version", 0) != 1) {
    error = "UNSUPPORTED SCORE VERSION";
    return false;
  }
  auto parsed = s3g::makeDefaultAmbiEffectDisplacementScore();
  parseScorePoints(scoreMember(root, "source"), parsed.source, parsed.source);
  parsed.sceneCount = 0;
  if (const auto *timeline = scoreMember(root, "timeline");
      timeline && timeline->IsObject())
    parsed.durationSeconds =
        float(scoreNumber(*timeline, "duration_seconds", 16.));
  if (const auto *scenes = scoreMember(root, "scenes");
      scenes && scenes->IsArray()) {
    const uint32_t count = std::min<uint32_t>(
        scenes->Size(), s3g::kAmbiEffectDisplacementMaxScenes);
    for (uint32_t n = 0; n < count; ++n) {
      const auto &scene = (*scenes)[n];
      if (!scene.IsObject())
        continue;
      auto points = parsed.source;
      if (!parseScorePoints(scoreMember(scene, "target"), parsed.source,
                            points))
        continue;
      auto &target = parsed.scenes[parsed.sceneCount++];
      target.time = float(scoreNumber(scene, "t",
                                      parsed.sceneCount > 1
                                          ? double(parsed.sceneCount - 1) /
                                                std::max(1u, count - 1)
                                          : 0.));
      target.points = points;
    }
  }
  if (parsed.sceneCount == 0) {
    parsed.sceneCount = 1;
    parsed.scenes[0].time = 0.;
    parsed.scenes[0].points = parsed.source;
    auto target = parsed.source;
    if (parseScorePoints(scoreMember(root, "target"), parsed.source, target)) {
      parsed.sceneCount = 2;
      parsed.scenes[1].time = 1.;
      parsed.scenes[1].points = target;
    }
  }
  parsed.scenes[0].time = 0.;
  parsed.scenes[0].points = parsed.source;
  score = s3g::sanitizeAmbiEffectDisplacementScore(parsed);
  amount = float(scoreNumber(root, "amount", .65));
  azimuthScale = elevationScale = radiusScale = 1.f;
  if (const auto *scales = scoreMember(root, "scales");
      scales && scales->IsObject()) {
    azimuthScale = float(scoreNumber(*scales, "azimuth", 1.));
    elevationScale = float(scoreNumber(*scales, "elevation", 1.));
    radiusScale = float(scoreNumber(*scales, "radius", 1.));
  }
  if (const auto *title = scoreMember(root, "name"); title && title->IsString())
    name.assign(title->GetString(), title->GetStringLength());
  return true;
}
