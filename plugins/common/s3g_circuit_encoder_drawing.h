#pragma once
#include "s3g_environment_encoder_drawing.h"
#include <chrono>
#include <random>
namespace s3g::portable_gui::circuit_drawing {
using namespace environment_drawing;
using decoder_drawing::calibratedColor;
inline double uptime() {
  return std::chrono::duration<double>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
inline void drawHeaderActionButton(Rect button, Rect, const String &label,
                                   Attrs attrs, const Style &) {
  setFill(calibratedColor(0x202020));
  rectFill(button);
  setStroke(calibratedColor(0xb8b8b8));
  frameRect(button);
  setStroke(calibratedColor(0x343434));
  frameRect(inset(button, 1., 1.));
  const auto size = sizeWithAttributes(label, attrs);
  drawAtPoint_withAttributes(
      label,
      makePoint(button.origin.x + (button.size.width - size.width) * .5,
                button.origin.y + (button.size.height - size.height) * .5 - .5),
      attrs);
}
inline void drawEncoderPresetMenu(const String &name,
                                  const s3g::gui_layout::EncoderTitleBand &band,
                                  const Attrs &attrs, const Attrs &values,
                                  const Style &style) {
  drawMenu("", name, band.titleY, attrs, values, style, band.presetLabelX,
           band.presetMenu.x, band.presetMenu.width);
  drawAtPoint_withAttributes(
      "PRESET", makePoint(band.presetLabelX, band.titleY + 1.), attrs);
}
} // namespace s3g::portable_gui::circuit_drawing
