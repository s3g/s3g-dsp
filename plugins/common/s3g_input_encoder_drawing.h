#pragma once
#include "s3g_decoder_drawing.h"

namespace s3g::portable_gui::input_encoder_drawing {
using namespace decoder_drawing;
// Some of the original maps have a frame without a separate header. Retain
// their white top rule as well as the shared, outline-free panel background.
inline void drawPanelFrame(double x, double y, double w, double h,
                           const Style &) {
  current->view.fill(makeRect(x, y, w, h), f::palette().cell);
  current->view.fill(makeRect(x, y, w, 2.), f::palette().accent);
}
inline void drawPanelFrame(const gui_layout::Panel &p, const Style &style) {
  const auto r = cocoaRect(p.frame);
  input_encoder_drawing::drawPanelFrame(r.origin.x, r.origin.y, r.size.width,
                                        r.size.height, style);
}
inline CColor calibratedWhite(double v, double alpha) {
  return calibratedRGB(v, v, v, alpha);
}
inline void drawHeaderButton(Rect b, Rect panel, const String &text,
                             bool selected, Attrs attrs, const Style &style) {
  drawToolboxHeaderButton(b, panel, text, selected, attrs, style);
}
struct GraphicsScope {
  VSTGUI::CDrawContext &context;
  CColor fill = current->fill, stroke = current->stroke;
  bool active = true;
  explicit GraphicsScope(VSTGUI::CDrawContext &c) : context(c) {
    context.saveGlobalState();
  }
  void restore() {
    if (active) {
      context.restoreGlobalState();
      current->fill = fill;
      current->stroke = stroke;
      active = false;
    }
  }
  ~GraphicsScope() { restore(); }
};
inline void clipRect(Rect r) {
  VSTGUI::CRect clip;
  current->context->getClipRect(clip);
  clip.bound(r);
  current->context->setClipRect(clip);
}
inline void drawBoundedRightText(const String &value, Rect r, Attrs attrs) {
  auto fitted = f::sliderValueTextToFit(*current->context, value, r.size.width,
                                        current->font);
  current->view.text(fitted, r, attrs.color, VSTGUI::kRightText);
}
inline CColor orientationGuideColor(double alpha) {
  return calibratedRGB(.10, .92, .30, alpha);
}
template <class ProjectPoint>
inline void drawAmbisonicOrientationGuides(ProjectPoint &&projectPoint,
                                           double radius = 1.0) {
  constexpr uint32_t kCircleSegments = 96u;
  constexpr double kTau = 6.28318530717958647692;
  Path zeroElevation = Path::bezierPath();
  for (uint32_t segment = 0u; segment <= kCircleSegments; ++segment) {
    const double phase = static_cast<double>(segment) /
                         static_cast<double>(kCircleSegments) * kTau;
    const Point point =
        projectPoint(std::cos(phase) * radius, std::sin(phase) * radius, 0.0);
    if (segment == 0u)
      (zeroElevation).moveToPoint(point);
    else
      (zeroElevation).lineToPoint(point);
  }
  setStroke(orientationGuideColor(0.24));
  (zeroElevation).setLineWidth(0.70);
  (zeroElevation).stroke();

  const auto drawAxis = [&](double ax, double ay, double az, double bx,
                            double by, double bz, double alpha) {
    const Point a = projectPoint(ax * radius, ay * radius, az * radius);
    const Point b = projectPoint(bx * radius, by * radius, bz * radius);
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    if (dx * dx + dy * dy < 0.25)
      return;
    setStroke(orientationGuideColor(alpha));
    Path axis = Path::bezierPath();
    (axis).moveToPoint(a);
    (axis).lineToPoint(b);
    (axis).setLineWidth(0.80);
    (axis).stroke();
  };
  drawAxis(-1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.44);
  drawAxis(0.0, -1.0, 0.0, 0.0, 1.0, 0.0, 0.44);
  drawAxis(0.0, 0.0, -1.0, 0.0, 0.0, 1.0, 0.52);
}

inline void drawEncoderTitleBand(const String &title, const String &preset,
                                 const String &status,
                                 const gui_layout::EncoderTitleBand &b, Attrs,
                                 Attrs, Attrs, const Style &style) {
  // Reserve the RANDOM button's space before drawing status/errors.
  auto &v = current->view;
  f::drawPluginTitle(
      *current->context, title,
      f::rect(b.titleX, 13., b.presetLabelX - b.titleX - 10., 15.),
      current->titleFont);
  v.text("PRESET", f::rect(b.presetLabelX, 13., 60., 15.), f::palette().label);
  f::drawMenuBox(*current->context, cocoaRect(b.presetMenu), preset,
                 current->font, f::palette());
  f::drawButton(*current->context, cocoaRect(b.loadButton), "LOAD",
                current->font, false, f::palette().strip);
  f::drawButton(*current->context, cocoaRect(b.saveButton), "SAVE",
                current->font, false, f::palette().strip);
  f::drawButton(*current->context, cocoaRect(b.randomButton), "RANDOM",
                current->font, false, f::palette().strip);
  const double x = b.randomButton.x + b.randomButton.width + 12.;
  v.text(current->error && !current->error->empty() ? *current->error : status,
         f::rect(x, 13., std::max(0., b.canvas.width - b.statusRightInset - x),
                 15.),
         f::palette().value, VSTGUI::kRightText);
}
} // namespace s3g::portable_gui::input_encoder_drawing
