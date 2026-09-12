#pragma once
#include "s3g_input_encoder_drawing.h"
#include "s3g_vstgui_auxiliary_window.h"
#include "vstgui/lib/platform/iplatformframe.h"
#include <limits>

namespace s3g::portable_gui::environment_drawing {
namespace drawing = s3g::portable_gui::environment_drawing;
using namespace input_encoder_drawing;
inline double minX(Rect r) { return r.origin.x; }
inline double minY(Rect r) { return r.origin.y; }
inline CColor calibratedHSV(double h, double s, double v, double a) {
  h = (h - std::floor(h)) * 6.;
  const int sector = static_cast<int>(h);
  const double f = h - sector, p = v * (1. - s), q = v * (1. - s * f),
               t = v * (1. - s * (1. - f));
  switch (sector) {
  case 0:
    return calibratedRGB(v, t, p, a);
  case 1:
    return calibratedRGB(q, v, p, a);
  case 2:
    return calibratedRGB(p, v, t, a);
  case 3:
    return calibratedRGB(p, q, v, a);
  case 4:
    return calibratedRGB(t, p, v, a);
  default:
    return calibratedRGB(v, p, q, a);
  }
}
inline Attrs textAttrs(CColor c, double size = 10.) { return {c, false, size}; }
inline void drawRightStatus(const String &value, double right, double y,
                            Attrs attrs, double inset = 18.) {
  const auto size = sizeWithAttributes(value, attrs);
  drawAtPoint_withAttributes(value, makePoint(right - size.width - inset, y),
                             attrs);
}
inline Rect encoderTitleActionRect(double w, double h,
                                   gui_layout::EncoderTitleAction action) {
  return cocoaRect(gui_layout::encoderTitleActionRect(
      gui_layout::encoderTitleBand({w, h}), action));
}
inline Rect environmentalFieldPageButtonRect(Rect r, uint32_t index) {
  return cocoaRect(gui_layout::environmentalFieldPageButtonRect(
      {r.origin.x, r.origin.y, r.size.width, r.size.height}, index));
}
#include "s3g_environment_surface_drawing.inc"
} // namespace s3g::portable_gui::environment_drawing
