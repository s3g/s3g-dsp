#pragma once
// Drawing primitives for the literal Ambi Effect ports. These are C++/VSTGUI
// types, not AppKit emulation: the editor retains its original coordinates,
// paths, and pointer handlers while the common controls use the family style.
#include "s3g_ambi_effect_colors.h"
#include "s3g_routing_gui.h"
#include <memory>
#include <type_traits>

namespace s3g::portable_gui::ambi_effect_drawing {
namespace f = foundation;
using VSTGUI::CColor;
using VSTGUI::CPoint;
using VSTGUI::CRect;
using CGFloat = double;
using BOOL = bool;
struct Point {
  double x = 0, y = 0;
  operator CPoint() const { return {x, y}; }
};
struct Size {
  double width = 0, height = 0;
};
struct Rect {
  Point origin;
  Size size;
  operator CRect() const {
    return f::rect(origin.x, origin.y, size.width, size.height);
  }
};
constexpr Point makePoint(double x, double y) { return {x, y}; }
constexpr Rect makeRect(double x, double y, double w, double h) {
  return {{x, y}, {w, h}};
}
constexpr double maxX(Rect r) { return r.origin.x + r.size.width; }
constexpr double maxY(Rect r) { return r.origin.y + r.size.height; }
constexpr double midX(Rect r) { return r.origin.x + r.size.width * .5; }
constexpr double midY(Rect r) { return r.origin.y + r.size.height * .5; }
constexpr Rect inset(Rect r, double x, double y) {
  return makeRect(r.origin.x + x, r.origin.y + y, r.size.width - 2 * x,
                  r.size.height - 2 * y);
}
constexpr bool contains(Point p, Rect r) {
  return p.x >= r.origin.x && p.x < maxX(r) && p.y >= r.origin.y &&
         p.y < maxY(r);
}
constexpr bool containsRect(Rect outer, Rect inner) {
  return inner.origin.x >= outer.origin.x && inner.origin.y >= outer.origin.y &&
         maxX(inner) <= maxX(outer) && maxY(inner) <= maxY(outer);
}
constexpr bool intersectsRect(Rect a, Rect b) {
  return a.origin.x < maxX(b) && a.origin.y < maxY(b) && maxX(a) > b.origin.x &&
         maxY(a) > b.origin.y;
}
constexpr Rect cocoaRect(gui_layout::Rect r) {
  return makeRect(r.x, r.y, r.width, r.height);
}
struct Attrs {
  CColor color;
  bool title = false;
  double size = 0.;
};
using String = std::string;
inline const char *formatArg(const std::string &v) { return v.c_str(); }
template <class T> T formatArg(T v) { return v; }
template <class... Args>
String stringFormat(const char *pattern, Args... args) {
  String converted(pattern);
  size_t n = 0;
  while ((n = converted.find("%@", n)) != String::npos)
    converted.replace(n, 2, "%s");
  return canvas::format(converted.c_str(), formatArg(args)...);
}
inline String stringFromUtf8(const char *s) { return s ? s : ""; }
inline String uppercaseString(String s) { return canvas::upper(std::move(s)); }

struct Drawing {
  routing::Canvas &view;
  VSTGUI::CDrawContext *context;
  VSTGUI::CFontRef font, titleFont;
  CColor fill = f::palette().text, stroke = f::palette().text;
  const std::string *error = nullptr;
};
inline thread_local Drawing *current = nullptr;
struct DrawingScope {
  Drawing drawing;
  Drawing *previous;
  DrawingScope(routing::Canvas &view, VSTGUI::CDrawContext *context,
               VSTGUI::CFontRef font, VSTGUI::CFontRef titleFont,
               const std::string *error = nullptr)
      : drawing{view, context, font, titleFont}, previous(current) {
    current = &drawing;
    drawing.error = error;
  }
  ~DrawingScope() { current = previous; }
};
inline CColor calibratedColor(uint32_t rgb, double alpha = 1.) {
  uint32_t converted = rgb;
  for (const auto &entry : calibratedColors)
    if (entry.first == rgb) {
      converted = entry.second;
      break;
    }
  auto c = f::color(converted);
  c.alpha = uint8_t(std::clamp(alpha, 0., 1.) * 255.);
  return c;
}
inline void setFill(CColor c) { current->fill = c; }
inline CColor calibratedRGB(double r, double g, double b, double a) {
  // Apple's Generic RGB ICC matrix/TRC, converted to sRGB. Retain the
  // original calibrated colors on Windows as well as in a Cocoa frame.
  r = std::pow(std::clamp(r, 0., 1.), 461. / 256.);
  g = std::pow(std::clamp(g, 0., 1.), 461. / 256.);
  b = std::pow(std::clamp(b, 0., 1.), 461. / 256.);
  const auto channel = [](double v) {
    v = std::clamp(v, 0., 1.);
    return uint8_t(std::round(
        255. *
        (v <= .0031308 ? 12.92 * v : 1.055 * std::pow(v, 1. / 2.4) - .055)));
  };
  return {channel(1.0252521192 * r - .0265617706 * g + .0013006288 * b),
          channel(.0194078704 * r + .9480428846 * g + .0325933005 * b),
          channel(-.0017629181 * r - .0014364516 * g + 1.0032149244 * b),
          uint8_t(std::clamp(a, 0., 1.) * 255.)};
}
inline void setStroke(CColor c) { current->stroke = c; }
inline CColor withAlpha(CColor c, double a) {
  c.alpha = uint8_t(std::clamp(a, 0., 1.) * 255.);
  return c;
}
inline void rectFill(Rect r) { current->view.fill(r, current->fill); }
inline void frameRect(Rect r) {
  // NSFrameRect in the original uses the fill color, not the stroke color.
  current->view.framed(
      routing::box(r.origin.x, r.origin.y, r.size.width, r.size.height),
      current->fill);
}
inline Attrs softLabelAttrs() { return {f::palette().label}; }
inline Attrs softValueAttrs() { return {f::palette().value}; }
inline Attrs softTitleAttrs() { return {f::palette().text, true}; }
inline void drawAtPoint_withAttributes(const String &s, Point p, Attrs attrs) {
  if (attrs.size > 0.) {
    auto face = VSTGUI::owned(new VSTGUI::CFontDesc(current->font->getName(), attrs.size));
    current->view.text(s, f::rect(p.x, p.y, 1200., attrs.size + 5.), attrs.color,
                      VSTGUI::kLeftText, face);
    return;
  }
  current->view.text(s, f::rect(p.x, p.y, 1200., 15.), attrs.color,
                     VSTGUI::kLeftText,
                     attrs.title ? current->titleFont : current->font);
}
inline Size sizeWithAttributes(const String &s, Attrs attrs) {
  if (attrs.size > 0.) {
    auto face = VSTGUI::owned(new VSTGUI::CFontDesc(current->font->getName(), attrs.size));
    current->context->setFont(face);
    return {current->context->getStringWidth(s.c_str()), attrs.size + 5.};
  }
  auto *font = attrs.title ? current->titleFont : current->font;
  current->context->setFont(font);
  return {current->context->getStringWidth(s.c_str()), 15.};
}
struct Style {
  CColor bg = f::palette().background, strip = f::palette().strip,
         cell = f::palette().cell, grid = f::palette().grid,
         fill = f::palette().fill, text = f::palette().text,
         dim = f::palette().dim, accent = f::palette().accent;
};
inline Style softTextStyle() { return {}; }
inline String peakDbText(float peak) {
  return canvas::format("PK %+.1f", 20. * std::log10(std::max(1.e-6f, peak)));
}
// Retains individual subpaths and absolute-point dash lengths from Cocoa.
class Path {
public:
  Path() : path(VSTGUI::owned(current->context->createGraphicsPath())) {}
  explicit operator bool() const { return bool(path); }
  static Path bezierPath() { return {}; }
  static void strokeLineFromPoint_toPoint(Point a, Point b) {
    Path p;
    p.moveToPoint(a);
    p.lineToPoint(b);
    p.stroke();
  }
  static Path bezierPathWithOvalInRect(Rect r) {
    Path p;
    p.appendBezierPathWithOvalInRect(r);
    return p;
  }
  static Path bezierPathWithRect(Rect r) {
    Path p;
    p.appendBezierPathWithRect(r);
    return p;
  }
  void moveToPoint(Point p) {
    if (path)
      path->beginSubpath(p);
  }
  void lineToPoint(Point p) {
    if (path)
      path->addLine(p);
  }
  void curveToPoint_controlPoint1_controlPoint2(Point p, Point a, Point b) {
    if (path)
      path->addBezierCurve(a, b, p);
  }
  void closePath() {
    if (path)
      path->closeSubpath();
  }
  void appendBezierPathWithOvalInRect(Rect r) {
    if (path)
      path->addEllipse(r);
  }
  void appendBezierPathWithRect(Rect r) {
    if (path)
      path->addRect(r);
  }
  void setLineWidth(double value) { width = value; }
  void setLineDash_count_phase(const double *values, size_t count,
                               double value) {
    dash.assign(values, values + count);
    phase = value;
  }
  void stroke() {
    if (!path)
      return;
    auto *c = current->context;
    c->saveGlobalState();
    c->setFrameColor(current->stroke);
    c->setLineWidth(width);
    std::vector<VSTGUI::CCoord> lengths;
    for (auto v : dash)
      lengths.push_back(v / width);
    double offset = phase;
#if defined(_WIN32)
    offset /= width;
#endif
    c->setLineStyle(VSTGUI::CLineStyle(
        VSTGUI::CLineStyle::kLineCapButt, VSTGUI::CLineStyle::kLineJoinMiter,
        offset, static_cast<uint32_t>(lengths.size()), lengths.data()));
    c->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
    c->restoreGlobalState();
  }
  void fill() {
    if (!path)
      return;
    current->context->setFillColor(current->fill);
    current->context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilled);
  }
  VSTGUI::SharedPointer<VSTGUI::CGraphicsPath> path;
  double width = 1., phase = 0.;
  std::vector<double> dash;
};

inline void drawPanelFrame(const gui_layout::Panel &p, const Style &) {
  current->view.fill(cocoaRect(p.frame), f::palette().cell);
}
inline void drawPanelFrame(double x, double y, double w, double h,
                           const Style &) {
  current->view.fill(makeRect(x, y, w, h), f::palette().cell);
}
inline void drawPanelHeader(const String &name, bool, double x, double y,
                            double w, double h, Attrs, const Style &) {
  auto &v = current->view;
  v.fill(makeRect(x, y, w, h), f::palette().strip);
  v.fill(makeRect(x, y, w, 2.), f::palette().accent);
  v.text(name, f::rect(x + 8., y + 3., w - 16., h - 3.), f::palette().label);
}
inline void drawPanelHeader(const String &name, bool,
                            const gui_layout::Panel &p, Attrs, const Style &) {
  current->view.panel(cocoaRect(p.frame), name);
}
inline void drawProcessorSlider(const String &name, const String &value,
                                double norm, double y, double x, double w,
                                Attrs, Attrs, const Style &) {
  auto &v = current->view;
  const double control = gui_layout::processorControlX(x);
  const double track = gui_layout::processorTrackWidth(w);
  const double vx = gui_layout::processorValueX(x, w);
  v.text(canvas::upper(name), f::rect(x + 16., y - 2., control - x - 24., 15.),
         f::palette().label);
  f::drawHorizontalSlider(*current->context,
                          f::rect(control, y + 1., track, 9.), norm, y - 1.,
                          13., f::palette());
  const double vw = gui_layout::kStandardMetrics.processorValueWidth;
  auto fitted =
      f::sliderValueTextToFit(*current->context, value, vw, current->font);
  if (current->context->getStringWidth(fitted.c_str()) > vw) {
    // Keep the complete number readable in the original narrow readout.
    const auto split = value.find(' ');
    if (split != String::npos)
      fitted = f::sliderValueTextToFit(
          *current->context, value.substr(0, split), vw, current->font);
  }
  v.text(fitted, f::rect(vx, y - 2., vw, 15.), f::palette().value,
         VSTGUI::kRightText);
}
inline void drawProcessorMenu(const String &name, const String &value, double y,
                              double x, double w, Attrs, Attrs, const Style &) {
  auto &v = current->view;
  const double control = gui_layout::processorControlX(x);
  v.text(canvas::upper(name), f::rect(x + 16., y - 2., control - x - 24., 15.),
         f::palette().label);
  f::drawMenuBox(
      *current->context,
      f::rect(control, y - 1., gui_layout::processorMenuWidth(w), 17.), value,
      current->font, f::palette());
}
inline void drawToggle(const String &name, bool on, double y, Attrs, Attrs,
                       const Style &, double labelX, double boxX, double boxW) {
  current->view.text(name, f::rect(labelX, y - 2., boxX - labelX - 8., 15.),
                     f::palette().label);
  f::drawButton(*current->context, f::rect(boxX, y - 1., boxW, 15.),
                on ? "ON" : "OFF", current->font, on, f::palette().cell);
  current->view.fill(f::rect(boxX + 1., y, 2., 13.), f::palette().fill);
}
inline Rect topologyProcessorCameraButtonRect(Rect p, unsigned index) {
  return makeRect(maxX(p) - 16. - 138. + index * 48., p.origin.y + 3., 42.,
                  15.);
}
inline void drawToolboxHeaderButton(Rect b, Rect, const String &name,
                                    bool active, Attrs, const Style &) {
  f::drawButton(*current->context, b, name, current->font, active,
                f::palette().strip);
}
inline void drawToolboxHeaderActionButton(Rect b, Rect h, const String &name,
                                          Attrs a, const Style &s) {
  drawToolboxHeaderButton(b, h, name, false, a, s);
}
inline void drawTopologyProcessorCameraButtons(Rect p, int selected, Attrs a,
                                               const Style &s) {
  const char *labels[] = {"TOP", "SIDE", "3/4"};
  for (unsigned i = 0; i < 3; ++i)
    drawToolboxHeaderButton(topologyProcessorCameraButtonRect(p, i), p,
                            labels[i], selected == int(i), a, s);
}
inline int dropdownHitIndex(Point p, Rect b, double h, unsigned count) {
  return contains(p, b) ? std::min(int(count) - 1, int((p.y - b.origin.y) / h))
                        : -1;
}
inline void drawDropdownMenu(Rect b, double h, const String *items,
                             unsigned count, int selected, int hover, Attrs,
                             const Style &) {
  auto &v = current->view;
  v.fill(b, f::palette().strip);
  for (unsigned n = 0; n < count; ++n) {
    const auto row = f::rect(b.origin.x, b.origin.y + n * h, b.size.width, h);
    if (int(n) == hover)
      v.fill(row, f::palette().button);
    if (int(n) == selected)
      v.fill(f::rect(row.left + 1., row.top + 2., 2., h - 4.),
             f::palette().fill);
    v.text(items[n], f::rect(row.left + 8., row.top, row.getWidth() - 12., h),
           f::palette().value);
  }
}
inline void drawTransformTitleBand(const String &title, const String &preset,
                                   const String &status,
                                   const gui_layout::EncoderTitleBand &b,
                                   const Style &) {
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
  const double x = b.saveButton.x + b.saveButton.width + 12.;
  v.text(current->error && !current->error->empty() ? *current->error : status,
         f::rect(x, 13., b.canvas.width - b.statusRightInset - x, 15.),
         f::palette().value, VSTGUI::kRightText);
}
inline void drawAmbiEffectTitleBand(const String &t, const String &p,
                                    const String &s,
                                    const gui_layout::EncoderTitleBand &b,
                                    const Style &style) {
  drawTransformTitleBand(t, p, s, b, style);
}
struct Event {
  Point point;
  int clicks = 1;
  double delta = 0;
  Point locationInWindow() const { return point; }
  int clickCount() const { return clicks; }
  double scrollingDeltaY() const { return delta; }
};
inline bool sliderDoubleClickDefault(Event *e, const clap_plugin_t *p,
                                     clap_id id, double *value) {
  if (e->clicks < 2)
    return false;
  auto *params = static_cast<const clap_plugin_params_t *>(
      p->get_extension(p, CLAP_EXT_PARAMS));
  if (!params)
    return false;
  for (uint32_t n = 0; n < params->count(p); ++n) {
    clap_param_info_t i{};
    if (params->get_info(p, n, &i) && i.id == id) {
      *value = i.default_value;
      return true;
    }
  }
  return false;
}
} // namespace s3g::portable_gui::ambi_effect_drawing
