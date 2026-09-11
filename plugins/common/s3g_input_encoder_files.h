#pragma once
#include "s3g_vstgui_foundation.h"
#include "vstgui/thirdparty/rapidjson/include/rapidjson/document.h"
#include "vstgui/thirdparty/rapidjson/include/rapidjson/prettywriter.h"
#include "vstgui/thirdparty/rapidjson/include/rapidjson/stringbuffer.h"
#include <cmath>
#include <fstream>

namespace s3g::portable_gui::encoder_files {
// Main-thread only. Keep malformed/imported files bounded before DOM parsing.
constexpr size_t maximumBytes = 64 * 1024 * 1024;
inline bool readText(const std::string &path, std::string &text) {
  std::ifstream in(foundation::pathFromUtf8(path.c_str()),
                   std::ios::binary | std::ios::ate);
  if (!in || in.tellg() <= 0 || in.tellg() > std::streamoff(maximumBytes))
    return false;
  std::string candidate(size_t(in.tellg()), '\0');
  in.seekg(0);
  if (!in.read(candidate.data(), std::streamsize(candidate.size())))
    return false;
  text = std::move(candidate);
  return true;
}
inline bool writeText(const std::string &path, const std::string &text) {
  std::ofstream out(foundation::pathFromUtf8(path.c_str()), std::ios::binary);
  out.write(text.data(), std::streamsize(text.size()));
  out.close();
  return bool(out);
}
inline bool parse(const std::string &text, rapidjson::Document &doc) {
  if (text.empty() || text.size() > maximumBytes)
    return false;
  unsigned depth = 0;
  bool quoted = false, escaped = false;
  for (char c : text) {
    if (quoted) {
      if (escaped)
        escaped = false;
      else if (c == '\\')
        escaped = true;
      else if (c == '"')
        quoted = false;
    } else if (c == '"')
      quoted = true;
    else if (c == '[' || c == '{') {
      if (++depth > 64)
        return false;
    } else if (c == ']' || c == '}') {
      if (!depth)
        return false;
      --depth;
    }
  }
  if (quoted || depth)
    return false;
  doc.Parse<rapidjson::kParseValidateEncodingFlag |
            rapidjson::kParseFullPrecisionFlag>(text.data(), text.size());
  return !doc.HasParseError() && doc.IsObject();
}
inline const rapidjson::Value &member(const rapidjson::Value &object,
                                      const char *key) {
  static const rapidjson::Value absent;
  if (!object.IsObject())
    return absent;
  auto it = object.FindMember(key);
  return it == object.MemberEnd() ? absent : it->value;
}
inline double number(const rapidjson::Value &value, double fallback = 0.) {
  const double v = value.IsNumber()
                       ? value.GetDouble()
                       : value.IsBool() ? double(value.GetBool()) : fallback;
  return std::isfinite(v) &&
                 std::abs(v) <= double(std::numeric_limits<float>::max())
             ? v
             : fallback;
}
inline double number(const rapidjson::Value &object, const char *key,
                     double fallback) {
  return number(member(object, key), fallback);
}
inline std::string string(const rapidjson::Value &object, const char *key,
                          const char *fallback = "") {
  const auto &v = member(object, key);
  return v.IsString() ? std::string(v.GetString(), v.GetStringLength())
                      : fallback;
}
} // namespace s3g::portable_gui::encoder_files
