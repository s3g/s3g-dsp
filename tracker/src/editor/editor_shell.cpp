#include "s3g/tracker/editor_shell.h"
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
namespace s3g::tracker::editor {
bool ShellController::valid(ShellPage p) {
  return std::size_t(p) < kShellPageCount;
}
bool ShellController::select(ShellPage p) {
  if (!valid(p))
    return false;
  selected_ = p;
  return true;
}
ShellPage ShellController::adjacent(bool next) const {
  return ShellPage((std::size_t(selected_) + (next ? 1 : kShellPageCount - 1)) %
                   kShellPageCount);
}
bool ShellController::canDetach(ShellPage p) {
  return valid(p) && p != ShellPage::Tracker && p != ShellPage::Song;
}
bool ShellController::detached(ShellPage p) const {
  return valid(p) && detached_[std::size_t(p)];
}
bool ShellController::setDetached(ShellPage p, bool value) {
  if (!canDetach(p))
    return false;
  detached_[std::size_t(p)] = value;
  return true;
}
void ShellController::resetDetached() { detached_.fill(false); }
const char *ShellController::title(ShellPage p) {
  constexpr const char *titles[] = {"TRACKER", "SONG",     "GEOMETRY", "BURSTS",
                                    "PHRASES", "ASSEMBLE", "RESHAPE",  "WARPS",
                                    "CONSOLE", "HELP"};
  return valid(p) ? titles[std::size_t(p)] : "";
}
const char *ShellController::windowTitle(ShellPage p) {
  constexpr const char *titles[] = {"Tracker",         "Song",
                                    "Rhythm Geometry", "Bursts",
                                    "MIDI Phrases",    "Phrase Assembly",
                                    "Pattern Reshape", "Timing Warps",
                                    "Console",         "Help"};
  return valid(p) ? titles[std::size_t(p)] : "";
}
ShellLayout ShellController::layout(double w, double h) const {
  ShellLayout l;
  constexpr double widths[] = {70, 50, 72, 60, 64, 72, 66, 54, 60, 46};
  double x = 12;
  for (std::size_t i = 0; i < kShellPageCount; ++i) {
    l.tabs[i] = {x, 6, widths[i], 28};
    x += widths[i] + 6;
  }
  l.detach = {std::max(12., w - 48), 6, 36, 28};
  l.bpm = {std::max(x + 8, w - (canDetach(selected_) ? 62 : 18) - 138), 10, 138,
           20};
  l.events = {x + 8, 10, std::max(0., l.bpm.x - x - 18), 20};
  l.eventsVisible = l.events.width >= 40;
  l.content = {0, 40, w, std::max(0., h - 40)};
  l.placeholder = {20, 40 + std::max(20., l.content.height * .5 - 10),
                   std::max(1., w - 40), 20};
  return l;
}
void ShellController::setHostBpm(double bpm) {
  if (!(bpm > 0) || !std::isfinite(bpm)) {
    bpm_ = "HOST BPM  —";
    return;
  }
  std::ostringstream s;
  s.imbue(std::locale::classic());
  s << "HOST BPM  " << std::fixed << std::setprecision(2) << bpm;
  bpm_ = s.str();
}
void ShellController::setEventText(std::string text) {
  events_ = text.empty() ? "0 MIDI EVENTS" : std::move(text);
}
} // namespace s3g::tracker::editor
