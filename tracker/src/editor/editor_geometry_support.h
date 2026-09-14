#pragma once
#include "s3g/tracker/editor_geometry.h"
#include "s3g_musical_scales.h"
#include <cmath>
#include <cstdio>
#include <limits>
namespace s3g::tracker::editor::geometry_support {
inline constexpr double kGeometryPi = 3.14159265358979323846;
inline constexpr double kGeometryHalfPi = kGeometryPi * 0.5;
inline constexpr double kGeometryNoteValueWidth = 72.;
inline constexpr std::array<uint32_t, 8> kLaneColors{
    0x78918c, 0x9a826c, 0x817a99, 0x956f73,
    0x71889a, 0x87916f, 0x987b6d, 0x748c7b};
inline Rect makeRect(double x, double y, double w, double h) {
  return {x, y, w, h};
}
inline Point makePoint(double x, double y) { return {x, y}; }
inline Rect logicalRect(layout::Rect r) {
  return {r.x, r.y, r.width, r.height};
}
inline double rMinX(Rect r) { return r.x; }
inline double rMinY(Rect r) { return r.y; }
inline double rMaxX(Rect r) { return r.x + r.width; }
inline double rMaxY(Rect r) { return r.y + r.height; }
inline double rMidX(Rect r) { return r.x + r.width * .5; }
inline double rMidY(Rect r) { return r.y + r.height * .5; }
inline double rWidth(Rect r) { return r.width; }
inline double rHeight(Rect r) { return r.height; }
inline Rect rInsetRect(Rect r, double x, double y) {
  return {r.x + x, r.y + y, r.width - 2 * x, r.height - 2 * y};
}
inline bool rEqual(Rect a, Rect b) {
  return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}
inline bool rContains(Point p, Rect r) {
  return p.x >= r.x && p.y >= r.y && p.x < rMaxX(r) && p.y < rMaxY(r);
}
inline const char *arg(const std::string &s) { return s.c_str(); }
template <class T> T arg(T v) { return v; }
template <class... A>
std::string format(const char *pattern, const A &... args) {
  const int n = std::snprintf(nullptr, 0, pattern, arg(args)...);
  if (n < 0)
    return {};
  std::vector<char> text(static_cast<std::size_t>(n) + 1);
  std::snprintf(text.data(), text.size(), pattern, arg(args)...);
  return {text.data(), static_cast<std::size_t>(n)};
}
inline const std::string &nsString(const std::string &s) { return s; }
inline std::string nsString(const char *s) { return s ? s : ""; }
inline Color withAlpha(Color c, double a) {
  c.alpha = static_cast<uint8_t>(std::lround(std::clamp(a, 0., 1.) * 255));
  return c;
}
inline std::string uppercase(std::string s) {
  for (auto &c : s)
    if (c >= 'a' && c <= 'z')
      c -= 32;
  return s;
}
inline std::string joined(const std::vector<std::string> &a,
                          const std::string &separator) {
  std::string s;
  for (const auto &part : a) {
    if (!s.empty())
      s += separator;
    s += part;
  }
  return s;
}
template <class T> std::size_t indexOf(const std::vector<T> &a, const T &v) {
  const auto i = std::find(a.begin(), a.end(), v);
  return i == a.end() ? std::string::npos : std::size_t(i - a.begin());
}
inline int multiColumnDropdownHitIndex(Point p, Rect r, double h, uint32_t n,
                                       uint32_t columns) {
  if (!n || !columns || !rContains(p, r))
    return -1;
  const auto rows = (n + columns - 1) / columns;
  const int col = static_cast<int>((p.x - r.x) / (r.width / columns));
  const int row = static_cast<int>((p.y - r.y) / h);
  const int index = col * static_cast<int>(rows) + row;
  return row >= 0 && row < static_cast<int>(rows) && index < static_cast<int>(n)
             ? index
             : -1;
}
inline int dropdownHitIndex(Point p, Rect r, double h, uint32_t n) {
  return multiColumnDropdownHitIndex(p, r, h, n, 1);
}
struct GeometryLaneSet {
  std::array<std::size_t, kMaximumTrackCount> indices{};
  std::size_t count = 0;
};
inline const Pattern *playbackFollowPattern(const TrackerViewState *state) {
  if (!state)
    return nullptr;
  if (state->songPlaybackActive && !state->songPlaybackPatternId.empty()) {
    if (const auto *pattern =
            state->patternBank.findPattern(state->songPlaybackPatternId))
      return pattern;
  }
  return &state->session.pattern;
}

inline std::string playbackFollowPatternId(const TrackerViewState *state) {
  if (!state)
    return {};
  if (state->songPlaybackActive &&
      state->patternBank.findPattern(state->songPlaybackPatternId))
    return state->songPlaybackPatternId;
  return state->patternBank.activePatternId;
}

inline std::string directionMark(Direction direction) {
  switch (direction) {
  case Direction::Reverse:
    return "<";
  case Direction::Random:
    return "RND";
  case Direction::Palindrome:
    return "<>";
  case Direction::Forward:
  default:
    return ">";
  }
}

inline std::string pitchAssignmentText(const PitchMapAssignment &assignment) {
  std::string text;
  for (std::size_t voice = 0; voice < assignment.voiceCount; ++voice) {
    if (voice)
      text += "  +  ";
    text += format("%s · MIDI %03u", midiNoteName(assignment.notes[voice]),
                   static_cast<unsigned>(assignment.notes[voice]));
  }
  return text;
}

inline void setGeometryBurstTiming(BurstDefinition &burst,
                                   std::string_view shape) noexcept {
  const auto count = static_cast<std::size_t>(burst.eventCount);
  if (count == 0u)
    return;
  for (std::size_t index = 0u; index < count; ++index) {
    const double phase =
        static_cast<double>(index) / static_cast<double>(count);
    double shaped = phase;
    if (shape == "accelerate")
      shaped = 1.0 - (1.0 - phase) * (1.0 - phase);
    else if (shape == "decelerate")
      shaped = phase * phase;
    burst.events[index].position = static_cast<uint16_t>(
        std::clamp<long>(std::lround(shaped * 65536.0), 0l, 65535l));
  }
}

inline std::size_t geometryBurstUsageCount(const Pattern &pattern,
                                           s3g::tracker::AssetBankId bankId,
                                           std::size_t slot) noexcept {
  std::size_t count = 0u;
  for (const auto &track : pattern.tracks)
    count += static_cast<std::size_t>(
        std::count_if(track.notes.begin(), track.notes.end(),
                      [bankId, slot](const NoteCell &cell) {
                        return cell.state == NoteCellState::Burst &&
                               cell.burstBankId == bankId && cell.note == slot;
                      }));
  return count;
}

inline std::size_t projectBurstUsageCount(const TrackerViewState &state,
                                          std::size_t slot) noexcept {
  std::size_t count = 0u;
  for (const auto &entry : state.patternBank.entries) {
    const Pattern *pattern = &entry.pattern;
    if (entry.id == state.patternBank.activePatternId)
      pattern = &state.session.pattern;
    count += geometryBurstUsageCount(*pattern, state.activeBurstBankId, slot);
  }
  for (const auto &bank : state.phraseBanks) {
    const auto &library = bank.id == state.activePhraseBankId
                              ? state.phraseLibrary
                              : bank.library;
    for (const auto &phrase : library.phrases) {
      count += static_cast<std::size_t>(std::count_if(
          phrase.notes.begin(), phrase.notes.end(), [&](const NoteCell &cell) {
            return cell.state == NoteCellState::Burst &&
                   cell.burstBankId == state.activeBurstBankId &&
                   cell.note == slot;
          }));
    }
  }
  return count;
}

inline const Pattern *geometryPattern(const TrackerViewState *state) {
  return playbackFollowPattern(state);
}

inline std::string geometryPatternId(const TrackerViewState *state) {
  return playbackFollowPatternId(state);
}

inline GeometryLaneSet geometryLanes(const Pattern *pattern) {
  GeometryLaneSet result;
  if (!pattern)
    return result;
  const auto lanes = std::min<std::size_t>(s3g::tracker::kMaximumTrackCount,
                                           pattern->tracks.size());
  for (std::size_t lane = 0u; lane < lanes; ++lane)
    result.indices[result.count++] = lane;
  return result;
}

inline bool geometryLaneMuted(const TrackerViewState *state,
                              const Pattern *pattern, std::size_t lane) {
  if (!pattern || lane >= pattern->tracks.size())
    return true;
  if (pattern->tracks[lane].noteColumn.muted)
    return true;
  return state && state->songPlaybackActive && lane < 32u &&
         (state->songPlaybackMutedTracks & (uint32_t{1u} << lane)) != 0u;
}

inline GeometryLaneSet visibleGeometryLanes(const Pattern *pattern) {
  GeometryLaneSet result;
  const auto lanes = geometryLanes(pattern);
  for (std::size_t ordinal = 0u; ordinal < lanes.count; ++ordinal) {
    const auto lane = lanes.indices[ordinal];
    if (!pattern->tracks[lane].noteColumn.muted)
      result.indices[result.count++] = lane;
  }
  return result;
}

inline GeometryLaneSet visibleGeometryLanes(const TrackerViewState *state) {
  GeometryLaneSet result;
  const auto *pattern = geometryPattern(state);
  const auto lanes = geometryLanes(pattern);
  for (std::size_t ordinal = 0u; ordinal < lanes.count; ++ordinal) {
    const auto lane = lanes.indices[ordinal];
    if (!geometryLaneMuted(state, pattern, lane))
      result.indices[result.count++] = lane;
  }
  return result;
}

} // namespace s3g::tracker::editor::geometry_support
