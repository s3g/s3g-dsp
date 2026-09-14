#pragma once
#include "s3g/tracker/editor_drawing.h"
#include <array>
#include <string>

namespace s3g::tracker::editor {
enum class ShellPage : unsigned {
  Tracker,
  Song,
  Geometry,
  Bursts,
  Phrases,
  Assemble,
  Reshape,
  Warps,
  Console,
  Help,
  Count
};
constexpr std::size_t kShellPageCount = std::size_t(ShellPage::Count);
struct ShellLayout {
  std::array<Rect, kShellPageCount> tabs;
  Rect detach, bpm, events, content, placeholder;
  bool eventsVisible = false;
};
// Portable navigation policy. Native adapters own window handles only; a
// detach state is confirmed after creation succeeds, not optimistically.
class ShellController {
public:
  ShellPage selected() const { return selected_; }
  bool select(ShellPage);
  ShellPage adjacent(bool next) const;
  static bool valid(ShellPage);
  static bool canDetach(ShellPage);
  bool detached(ShellPage) const;
  bool setDetached(ShellPage, bool);
  void resetDetached();
  static const char *title(ShellPage);
  static const char *windowTitle(ShellPage);
  ShellLayout layout(double width, double height) const;
  void setHostBpm(double bpm);
  void setEventText(std::string text);
  const std::string &bpmText() const { return bpm_; }
  const std::string &eventText() const { return events_; }

private:
  ShellPage selected_ = ShellPage::Tracker;
  std::array<bool, kShellPageCount> detached_{};
  std::string bpm_ = "HOST BPM  —";
  std::string events_ = "0 MIDI EVENTS  •  SEND 0  DROP 0  LATE 0  CLK 0";
};
} // namespace s3g::tracker::editor
