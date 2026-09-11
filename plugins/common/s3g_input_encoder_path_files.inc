// Original JSON schema and practical SVG numeric-pair import, using the
// shared UTF-8 paths/dialogs. JSON is the lossless canonical 3D format.
bool loadPathText(const std::string &text) {
  namespace files = s3g::portable_gui::encoder_files;
  rapidjson::Document doc;
  if (!files::parse(text, doc))
    return false;
  const auto &list = files::member(doc, "paths");
  if (!list.IsArray() || list.Empty())
    return false;
  auto paths = guiEncoder.paths();
  const auto count =
      std::min<unsigned>(list.Size(), s3g::kAmbiPathEncoderMaxPaths);
  for (unsigned pi = 0; pi < count; ++pi) {
    const auto &points = files::member(list[pi], "points");
    if (!points.IsArray())
      return false;
    auto &path = paths[pi];
    path.pointCount =
        std::min<unsigned>(points.Size(), s3g::kAmbiPathEncoderMaxPoints);
    for (unsigned i = 0; i < path.pointCount; ++i) {
      if (!points[i].IsObject())
        return false;
      auto &point = path.points[i];
      point.x = float(files::number(points[i], "x", 0.));
      point.y = float(files::number(points[i], "y", 1.));
      point.z = float(files::number(points[i], "z", 0.));
      point.time = float(files::number(
          points[i], "time",
          path.pointCount > 1 ? double(i) / double(path.pointCount - 1) : 0.));
    }
  }
  if (!set(kPathsParamId, count))
    return false;
  setEditorPaths(paths);
  _selectedPoint = -1;
  service();
  return true;
}
std::string pathJsonText() const {
  rapidjson::StringBuffer buffer;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> w(buffer);
  w.StartObject();
  w.Key("type");
  w.String("s3g_ambi_path");
  w.Key("version");
  w.Uint(1);
  w.Key("paths");
  w.StartArray();
  for (unsigned pi = 0; pi < guiParams.activePaths; ++pi) {
    w.StartObject();
    w.Key("name");
    w.String(drawing::stringFormat("path %u", pi + 1).c_str());
    w.Key("points");
    w.StartArray();
    const auto &path = guiEncoder.paths()[pi];
    for (unsigned i = 0; i < path.pointCount; ++i) {
      const auto &pt = path.points[i];
      w.StartObject();
      w.Key("x");
      w.Double(pt.x);
      w.Key("y");
      w.Double(pt.y);
      w.Key("z");
      w.Double(pt.z);
      w.Key("time");
      w.Double(pt.time);
      w.EndObject();
    }
    w.EndArray();
    w.EndObject();
  }
  w.EndArray();
  w.EndObject();
  return {buffer.GetString(), buffer.GetSize()};
}
static std::vector<double> numbersFromString(const std::string &text) {
  std::vector<double> numbers;
  const auto digit = [](char c) { return c >= '0' && c <= '9'; };
  for (size_t i = 0; i < text.size();) {
    const auto start = i;
    if (text[i] == '-' || text[i] == '+')
      ++i;
    const auto integer = i;
    while (i < text.size() && digit(text[i]))
      ++i;
    bool valid = i > integer;
    if (i < text.size() && text[i] == '.') {
      ++i;
      const auto fraction = i;
      while (i < text.size() && digit(text[i]))
        ++i;
      valid = valid || i > fraction;
    }
    if (!valid) {
      i = start + 1;
      continue;
    }
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
      const auto exponent = i++;
      if (i < text.size() && (text[i] == '-' || text[i] == '+'))
        ++i;
      const auto exponentDigits = i;
      while (i < text.size() && digit(text[i]))
        ++i;
      if (i == exponentDigits)
        i = exponent;
    }
    const double v =
        std::strtod(text.substr(start, i - start).c_str(), nullptr);
    if (!std::isfinite(v))
      return {};
    numbers.push_back(v);
  }
  return numbers;
}
static std::string attributeValue(const std::string &tag,
                                  const std::string &name) {
  auto lower = tag;
  for (auto &c : lower)
    if (c >= 'A' && c <= 'Z')
      c += 32;
  auto at = lower.find(name + "=\"");
  if (at == std::string::npos)
    at = lower.find(name + "='");
  if (at == std::string::npos)
    return {};
  const auto start = at + name.size() + 2,
             end = tag.find(tag[start - 1], start);
  return end == std::string::npos ? "" : tag.substr(start, end - start);
}
bool importSvgText(const std::string &text) {
  if (text.size() > s3g::portable_gui::encoder_files::maximumBytes)
    return false;
  auto paths = guiEncoder.paths();
  unsigned count = 0;
  for (size_t at = 0; count < s3g::kAmbiPathEncoderMaxPaths;) {
    at = text.find('<', at);
    if (at == std::string::npos)
      break;
    const auto end = text.find('>', at);
    if (end == std::string::npos)
      break;
    auto tag = text.substr(at, end - at + 1);
    at = end + 1;
    auto lower = tag;
    for (auto &c : lower)
      if (c >= 'A' && c <= 'Z')
        c += 32;
    bool matched = false;
    for (const char *name : {"path", "polyline", "polygon"}) {
      const std::string prefix = std::string("<") + name;
      if (lower.compare(0, prefix.size(), prefix) == 0 &&
          lower.size() > prefix.size()) {
        const char c = lower[prefix.size()];
        matched = matched || !(c == '_' || (c >= 'a' && c <= 'z') ||
                               (c >= '0' && c <= '9'));
      }
    }
    if (!matched)
      continue;
    auto data = attributeValue(tag, "d");
    if (data.empty())
      data = attributeValue(tag, "points");
    paths[count++] = pathFromXYNumbers(numbersFromString(data));
  }
  if (!count || !set(kPathsParamId, count))
    return false;
  setEditorPaths(paths);
  _selectedPoint = -1;
  service();
  return true;
}
std::string pathSvgText() const {
  std::string out =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg "
      "xmlns=\"http://www.w3.org/2000/svg\" width=\"1000\" height=\"1000\" "
      "viewBox=\"0 0 1000 1000\">\n<metadata>s3g-ambi-path-svg-v1; JSON "
      "remains the canonical format.</metadata>\n<g fill=\"none\" "
      "stroke=\"#444\" stroke-width=\"2\">\n";
  for (unsigned pi = 0; pi < guiParams.activePaths; ++pi) {
    const auto &path = guiEncoder.paths()[pi];
    if (!path.pointCount)
      continue;
    out +=
        drawing::stringFormat("<polyline data-path=\"%u\" points=\"", pi + 1);
    for (unsigned i = 0; i < path.pointCount; ++i)
      out += drawing::stringFormat("%.3f,%.3f ", 500. + path.points[i].x * 400.,
                                   500. - path.points[i].y * 400.);
    out += "\"/>\n";
  }
  return out + "</g>\n</svg>\n";
}
void pathFileDialog(bool save, bool svg) {
  foundation::FileDialogOptions o;
  o.save = save;
  o.extension = svg ? "svg" : "json";
  o.extensionDescription = svg ? "SVG paths" : "s3g Path JSON";
  o.title = std::string(save ? "Save " : "Load ") + o.extensionDescription;
  o.defaultSaveName = std::string("s3g-ambi-path.") + o.extension;
  const auto path = foundation::runFileDialog(getFrame(), o);
  if (path.empty())
    return;
  namespace files = s3g::portable_gui::encoder_files;
  std::string text;
  bool ok = false;
  if (save)
    ok = files::writeText(path, svg ? pathSvgText() : pathJsonText());
  else if (files::readText(path, text))
    ok = svg ? importSvgText(text) : loadPathText(text);
  error = ok ? "" : "PATH FILE ERROR";
  invalid();
}
void loadPathJson() { pathFileDialog(false, false); }
void savePathJson() { pathFileDialog(true, false); }
void importSvg() { pathFileDialog(false, true); }
void exportSvg() { pathFileDialog(true, true); }
