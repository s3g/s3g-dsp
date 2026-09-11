#pragma once
#include "s3g_clap_gui_param_queue.h"
#include "s3g_clap_vstgui.h"
#include "s3g_gui_layout.h"
#include "s3g_topology_colors.h"
#include "s3g_vstgui_canvas.h"
#include <clap/ext/state.h>
#include <memory>

namespace s3g::portable_gui::routing {
using namespace canvas;
using Kind = s3g::clap_gui::ParamEventKind;
// Literal source geometry uses origin/size notation, independent of AppKit.
struct Size {
  double width = 0., height = 0.;
};
struct Box {
  CPoint origin;
  Size size;
  operator CRect() const {
    return rect(origin.x, origin.y, size.width, size.height);
  }
};
inline Box box(double x, double y, double w, double h) {
  return {{x, y}, {w, h}};
}
inline Box box(gui_layout::Rect r) { return box(r.x, r.y, r.width, r.height); }
inline double maxX(Box r) { return r.origin.x + r.size.width; }
inline double maxY(Box r) { return r.origin.y + r.size.height; }
inline Box inset(Box r, double x, double y) {
  return box(r.origin.x + x, r.origin.y + y, r.size.width - 2 * x,
             r.size.height - 2 * y);
}
inline CColor calibratedGray(unsigned rgb, double alpha = 1.) {
  auto c = foundation::color(topology_colors::gray[rgb & 255u]);
  c.alpha = uint8_t(std::clamp(alpha, 0., 1.) * 255.);
  return c;
}
// Common controls only. Family-specific field geometry remains in its canvas.
class Canvas : public View {
public:
  Canvas(double w, double h) : View(w, h) {}
  void framed(Box r, CColor c) {
    fill(rect(r.origin.x, r.origin.y, r.size.width, 1.), c);
    fill(rect(r.origin.x, maxY(r) - 1., r.size.width, 1.), c);
    fill(rect(r.origin.x, r.origin.y, 1., r.size.height), c);
    fill(rect(maxX(r) - 1., r.origin.y, 1., r.size.height), c);
  }
  void at(const std::string &s, double x, double y, CColor c,
          CFontRef face = nullptr) {
    text(s, rect(x, y, 600., 15.), c, kLeftText, face);
  }
  void header(Box b, const std::string &label, std::function<void()> action,
              bool active = false) {
    foundation::drawButton(*context, b, label, font, active, style.strip);
    hit(b, std::move(action));
  }
  void slider(const std::string &label, const std::string &value, double norm,
              Box track, Box hitBox, Box labelBox, Box valueBox,
              std::function<void(double)> change, std::function<void()> begin,
              std::function<void()> end, std::function<void()> reset,
              bool enabled = true) {
    auto colors = style;
    if (!enabled) {
      colors.label = colors.value = colors.text = color(0x686868);
      colors.fill = color(0x424242);
    }
    text(label, labelBox, colors.label);
    foundation::drawHorizontalSlider(*context, track, norm, track.origin.y - 2.,
                                     track.size.height + 4., colors);
    text(foundation::sliderValueTextToFit(*context, value, valueBox.size.width,
                                          font),
         valueBox, colors.value, kRightText);
    drag(
        hitBox,
        [track, change](CPoint p) {
          change(std::clamp((p.x - track.origin.x) / track.size.width, 0., 1.));
        },
        std::move(begin), std::move(end), std::move(reset), enabled);
  }
};
} // namespace s3g::portable_gui::routing
