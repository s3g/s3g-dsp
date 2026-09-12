#pragma once
#include "s3g_midi_tool_drawing.h"

namespace s3g::portable_gui::complex_drawing {
using namespace midi_drawing;
using Style = midi_drawing::Style;
using circuit_drawing::drawEncoderPresetMenu;
inline constexpr uint32_t kMidiChannelMenuItemCount = 16u;
inline int midiChannelDropdownHitIndex(Point p, Rect r, double height) {
  return multiColumnDropdownHitIndex(p, r, height, 16u, 2u);
}
inline void drawMidiChannelDropdownMenu(Rect r, double height, int selected,
                                        int hover, Attrs attrs,
                                        const Style &style) {
  String items[16];
  for (uint32_t n = 0; n < 16; ++n)
    items[n] = "CHANNEL " + std::to_string(n + 1);
  drawMultiColumnDropdownMenu(r, height, items, 16u, 2u, selected, hover, attrs,
                              style);
}
inline void drawWrappedText(const String &text, Rect r, Attrs attrs) {
  GraphicsScope guard(*current->context);
  current->context->setClipRect(
      foundation::rect(r.origin.x, r.origin.y, r.size.width, r.size.height));
  std::string line;
  std::istringstream words(text);
  std::string word;
  double y = r.origin.y;
  while (words >> word) {
    auto next = line.empty() ? word : line + " " + word;
    if (!line.empty() && sizeWithAttributes(next, attrs).width > r.size.width) {
      drawAtPoint_withAttributes(line, makePoint(r.origin.x, y), attrs);
      y += 12.;
      line = word;
    } else
      line = std::move(next);
    if (y >= maxY(r))
      break;
  }
  if (y < maxY(r) && !line.empty())
    drawAtPoint_withAttributes(line, makePoint(r.origin.x, y), attrs);
}
inline VSTGUI::CColor color(uint32_t rgb, double alpha = 1.) {
  auto c = feedback_drawing::color(rgb);
  c.alpha = static_cast<uint8_t>(std::clamp(alpha, 0., 1.) * 255. + .5);
  return c;
}
inline void drawImprintTitleBand(const String &title, const String &preset,
                                 const String &status,
                                 const gui_layout::EncoderTitleBand &band,
                                 const Style &style) {
  feedback_drawing::drawProcessorTitleBand(title, preset, status, band,
                                           softTitleAttrs(), softLabelAttrs(),
                                           softValueAttrs(), style);
}
} // namespace s3g::portable_gui::complex_drawing
