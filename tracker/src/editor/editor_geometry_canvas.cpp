#include "editor_geometry_support.h"
#include "s3g/tracker/editor_geometry.h"

namespace s3g::tracker::editor {
using namespace geometry_support;
GeometryPath GeometryPath::ellipse(Rect r) {
  GeometryPath path;
  path.bounds = r;
  path.elements.push_back({r, true, {}});
  return path;
}
GeometryPath GeometryPath::rectangle(Rect r) {
  GeometryPath path;
  path.bounds = r;
  path.elements.push_back({r, false, {}});
  return path;
}
void GeometryPath::moveToPoint(Point p) {
  elements.push_back({{}, false, {p}});
}
void GeometryPath::lineToPoint(Point p) {
  if (elements.empty())
    moveToPoint(p);
  else
    elements.back().points.push_back(p);
}
void GeometryPath::closePath() {
  if (!elements.empty() && !elements.back().points.empty())
    elements.back().points.push_back(elements.back().points.front());
}
void GeometryPath::setLineDash(const double *dash, std::size_t count, double) {
  dashes.assign(dash, dash + count);
}
Color GeometryEditor::literal(uint32_t rgb, double alpha) const {
  return services_.paint.color
             ? services_.paint.color(rgb, alpha)
             : Color{uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb),
                     uint8_t(std::lround(std::clamp(alpha, 0., 1.) * 255))};
}
Color GeometryEditor::trackerColor(uint32_t rgb, double alpha) const {
  const auto r = uint8_t(rgb >> 16), g = uint8_t(rgb >> 8), b = uint8_t(rgb);
  if (std::max({r, g, b}) - std::min({r, g, b}) <= 8)
    rgb = nightNeutral(uint8_t((unsigned(r) + g + b) / 3));
  return literal(rgb, alpha);
}
Color GeometryEditor::themeColor(ThemeRole role, double alpha) const {
  return literal(themeRGB(role), alpha);
}
GridFont GeometryEditor::font(double size, FontWeight weight,
                              bool centered) const {
  if (services_.paint.font)
    return services_.paint.font(size, weight, centered);
  size = std::max(8., std::round(size * 1.16 * 2) / 2);
  return {weight == FontWeight::Semibold
              ? "IBM Plex Mono SemiBold"
              : weight == FontWeight::Medium ? "IBM Plex Mono Medium"
                                             : "IBM Plex Mono",
          size, size * .95, std::ceil(size * 1.3)};
}
GridFont GeometryEditor::uiFont(double size) const {
  return services_.suiteFont
             ? services_.suiteFont(size)
             : GridFont{"Menlo", size, size, std::ceil(size * 1.3)};
}
GeometryEditor::TextStyle GeometryEditor::softLabelAttrs() const {
  return {literal(0xa8a8a8), uiFont(10)};
}
GeometryEditor::TextStyle GeometryEditor::softValueAttrs() const {
  return {literal(0x929292), uiFont(10)};
}
GeometryStyle GeometryEditor::softTextStyle() const {
  return {literal(0x0c0c0c), literal(0x1d1d1d), literal(0x131313),
          literal(0x565656), literal(0xc9c9c9), literal(0x8f8f8f),
          literal(0x131313), literal(0x7f7f7f), literal(0xb8b8b8)};
}
Point GeometryEditor::measure(std::string_view text,
                              const TextStyle &style) const {
  double width = 0;
  if (services_.measure)
    width = services_.measure(text, style.font);
  else
    for (char c : text)
      if ((static_cast<unsigned char>(c) & 0xc0) != 0x80)
        width += style.font.size * .6;
  return {width, style.font.lineHeight};
}
std::string GeometryEditor::menuDisplayText(std::string text, double width,
                                            const TextStyle &style) const {
  text = uppercase(std::move(text));
  if (width <= 0 || measure(text, style).x <= width)
    return text;
  const std::pair<const char *, const char *> substitutions[]{
      {"ENERGY-NORMALIZED", "ENERGY NORM"},
      {"ENERGY NORMALIZED", "ENERGY NORM"},
      {"HYPERCARDIOID", "HYPER"},
      {"SUPERCARDIOID", "SUPER"},
      {"CARDIOID", "CARD"},
      {"VIRTUAL", "VIRT"},
      {"FEEDFORWARD", "FEED FWD"},
      {"PROJECTION", "PROJ"},
      {"ELEVATION", "ELEV"},
      {"DIRECTIONAL", "DIR"},
      {"INTERPOLATION", "INTERP"},
      {"ALTERNATING", "ALT"}};
  for (auto [from, to] : substitutions) {
    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
      text.replace(pos, std::char_traits<char>::length(from), to);
      pos += std::char_traits<char>::length(to);
    }
    if (measure(text, style).x <= width)
      return text;
  }
  while (!text.empty() && measure(text + "…", style).x > width) {
    auto pos = text.size() - 1;
    while (pos && (static_cast<unsigned char>(text[pos]) & 0xc0) == 0x80)
      --pos;
    text.erase(pos);
  }
  return text + "…";
}
void GeometryEditor::fillRect(Rect r, Color c) {
  fillColor_ = c;
  list_.shape(Primitive::FillRect, r, c);
}
void GeometryEditor::strokeRect(Rect r, Color c, double width) {
  strokeColor_ = c;
  list_.shape(Primitive::StrokeRect, r, c, width);
}
void GeometryEditor::fillPath(const GeometryPath &path) {
  for (const auto &e : path.elements) {
    if (!e.points.empty())
      list_.polygon(e.points, fillColor_);
    else
      list_.shape(e.ellipse ? Primitive::FillEllipse : Primitive::FillRect,
                  e.rect, fillColor_);
  }
}
void GeometryEditor::strokePath(const GeometryPath &path) {
  for (const auto &e : path.elements) {
    if (!e.points.empty())
      list_.polyline(e.points, strokeColor_, path.lineWidth, path.dashes,
                     path.roundCaps);
    else
      list_.shape(e.ellipse ? Primitive::StrokeEllipse : Primitive::StrokeRect,
                  e.rect, strokeColor_, path.lineWidth, path.dashes);
  }
}
void GeometryEditor::drawAtPoint(std::string text, Point p,
                                 const TextStyle &style) {
  const auto size = measure(text, style);
  list_.text(std::move(text), {p.x, p.y, size.x + 2, size.y + 2}, style.color,
             style.font.name, style.font.size, p.y + style.font.baseline,
             Alignment::Left);
}
void GeometryEditor::drawInRect(std::string text, Rect r,
                                const TextStyle &style) {
  list_.text(std::move(text), r, style.color, style.font.name, style.font.size,
             r.y + style.font.baseline, Alignment::Left);
}
void GeometryEditor::drawText(std::string text, Rect r, Color color,
                              double size, FontWeight weight,
                              Alignment alignment) {
  const auto f = font(size, weight, false);
  list_.text(std::move(text), r, color, f.name, f.size, r.y + f.baseline,
             alignment);
}
void GeometryEditor::drawCenteredText(std::string text, Rect r, Color color,
                                      double size, FontWeight weight,
                                      Alignment alignment) {
  const auto f = font(size, weight, true);
  r.y = std::floor(rMidY(r) - f.lineHeight * .5);
  r.height = f.lineHeight;
  list_.text(std::move(text), r, color, f.name, f.size, r.y + f.baseline,
             alignment);
}
void GeometryEditor::drawPanelFrame(double x, double y, double w, double h,
                                    const GeometryStyle &style) {
  // Cocoa NSFrameRect uses the fill color, not the preceding setStroke.
  // Its frame is indistinguishable from the panel fill: preserve that look.
  const Rect r{x, y, w, h};
  fillRect(r, style.panel);
  fillRect({x, y, w, 2}, style.handle);
}
void GeometryEditor::drawPanelFrame(const layout::Panel &p,
                                    const GeometryStyle &style) {
  drawPanelFrame(p.frame.x, p.frame.y, p.frame.width, p.frame.height, style);
}
void GeometryEditor::drawPanelHeader(std::string title, bool, double x,
                                     double y, double w, double h,
                                     const TextStyle &,
                                     const GeometryStyle &style) {
  fillRect({x, y, w, h}, style.header);
  fillRect({x, y, w, 2}, style.handle);
  drawAtPoint(std::move(title),
              {x + layout::kStandardMetrics.headerLabelInset, y + 5},
              softLabelAttrs());
}
void GeometryEditor::drawPanelHeader(std::string title, bool open,
                                     const layout::Panel &p,
                                     const TextStyle &attrs,
                                     const GeometryStyle &style) {
  drawPanelHeader(std::move(title), open, p.frame.x, p.frame.y, p.frame.width,
                  layout::kStandardMetrics.headerHeight, attrs, style);
}
void GeometryEditor::drawToolboxHeaderButton(Rect r, Rect, std::string text,
                                             bool active,
                                             const TextStyle &attrs,
                                             const GeometryStyle &style) {
  fillRect(r, literal(active ? 0x505050 : 0x383838));
  (void)style;
  const double cap = services_.capHeight ? services_.capHeight(attrs.font)
                                         : attrs.font.size * .72;
  list_.text(std::move(text), r, attrs.color, attrs.font.name, attrs.font.size,
             centeredCapsBaseline(r, cap), Alignment::Center);
}
void GeometryEditor::drawToolboxHeaderActionButton(Rect r, Rect header,
                                                   std::string title,
                                                   const TextStyle &attrs,
                                                   const GeometryStyle &style) {
  drawToolboxHeaderButton(r, header, std::move(title), false, attrs, style);
}
void GeometryEditor::S3GTrackerDrawSuiteActionButton(
    Rect bounds, std::string title, bool enabled, bool pressed, bool hovered,
    bool live, bool positive, bool binaryOff, bool danger, bool neutralTitle) {
  const auto r = rInsetRect(bounds, .5, .5);
  const auto accent =
      themeColor(positive ? ThemeRole::Success
                          : binaryOff ? ThemeRole::Danger : ThemeRole::Live);
  const auto fill =
      pressed ? literal(0x414141)
              : positive ? withAlpha(accent, .20)
                         : (live || binaryOff)
                               ? withAlpha(accent, .16)
                               : literal(hovered ? 0x343434 : 0x292929);
  (void)danger;
  fillRect(r, fill);
  auto attrs = softValueAttrs();
  if (!enabled)
    attrs.color = literal(0x656565);
  else if (!neutralTitle && (positive || live || binaryOff))
    attrs.color = accent;
  const double cap = services_.capHeight ? services_.capHeight(attrs.font)
                                         : attrs.font.size * .72;
  list_.text(uppercase(std::move(title)), bounds, attrs.color, attrs.font.name,
             attrs.font.size, centeredCapsBaseline(bounds, cap),
             Alignment::Center);
}
void GeometryEditor::drawTrackerProcessorMenu(std::string name,
                                              std::string value, double y,
                                              double x, double width,
                                              const TextStyle &labels,
                                              const TextStyle &values,
                                              const GeometryStyle &style) {
  const Rect r{layout::processorControlX(x), y - 1,
               layout::processorMenuWidth(width), 15};
  fillRect(r, style.header);
  fillRect({r.x + 1, r.y + 1, 2, r.height - 2}, style.fill);
  drawAtPoint(
      menuDisplayText(std::move(value), std::max(0., r.width - 28), values),
      {r.x + 8, y + 1}, values);
  drawAtPoint("v", {rMaxX(r) - 12, y}, values);
  drawAtPoint(uppercase(std::move(name)), {layout::processorLabelX(x), y + 1},
              labels);
}
void GeometryEditor::drawProcessorSliderWithValueWidth(
    std::string name, std::string value, double norm, double y, double x,
    double width, double valueWidth, const TextStyle &labels,
    const TextStyle &values, const GeometryStyle &style) {
  drawAtPoint(std::move(name), {layout::processorLabelX(x), y - 2}, labels);
  const Rect track{layout::processorControlX(x), y + 1,
                   layout::processorTrackWidth(width, valueWidth), 9};
  fillRect(track, style.track);
  norm = std::clamp(norm, 0., 1.);
  auto filled = rInsetRect(track, 1, 1);
  filled.width = std::max(1., filled.width * norm);
  fillRect(filled, style.fill);
  const auto handleX = std::clamp(track.x + track.width * norm - 1.5,
                                  track.x + 1, track.x + track.width - 4);
  fillRect({handleX, track.y - 2, 3, track.height + 4}, style.text);
  const Rect valueRect{layout::processorValueX(x, width, valueWidth), y - 2,
                       valueWidth, 15};
  list_.text(std::move(value), valueRect, values.color, values.font.name,
             values.font.size, valueRect.y + values.font.baseline,
             Alignment::Right);
}
void GeometryEditor::drawProcessorSlider(std::string name, std::string value,
                                         double norm, double y, double x,
                                         double width, const TextStyle &labels,
                                         const TextStyle &values,
                                         const GeometryStyle &style) {
  drawProcessorSliderWithValueWidth(
      std::move(name), std::move(value), norm, y, x, width,
      layout::kStandardMetrics.processorValueWidth, labels, values, style);
}
void GeometryEditor::drawProcessorToggle(std::string name, bool on, double y,
                                         double x, double width,
                                         const TextStyle &labels,
                                         const TextStyle &values,
                                         const GeometryStyle &style) {
  drawAtPoint(std::move(name), {layout::processorLabelX(x), y - 2}, labels);
  const Rect r{layout::processorControlX(x), y - 1,
               layout::processorMenuWidth(width), 15};
  fillRect(r, literal(on ? 0x303030 : 0x151515));
  const double cap = services_.capHeight ? services_.capHeight(values.font)
                                         : values.font.size * .72;
  list_.text(on ? "ON" : "OFF", r, values.color, values.font.name,
             values.font.size, centeredCapsBaseline(r, cap), Alignment::Center);
  fillRect({r.x + 1, r.y + 1, 2, r.height - 2}, style.fill);
}
void GeometryEditor::drawOpenGeometryMenu() {
  if (_openGeometryMenu == GeometryMenuNone)
    return;
  const auto items = itemsForGeometryMenu(_openGeometryMenu);
  if (items.empty())
    return;
  const auto rect = dropdownRectForGeometryMenu(_openGeometryMenu);
  const unsigned columns =
      _openGeometryMenu == GeometryMenuPitchScale
          ? 4
          : _openGeometryMenu == GeometryMenuBurstSlot ? 2 : 1;
  const auto rows = (items.size() + columns - 1) / columns;
  const int selected = selectedIndexForGeometryMenu(_openGeometryMenu);
  const auto style = softTextStyle();
  const auto attrs = softValueAttrs();
  for (unsigned column = 0; column < columns; ++column) {
    const auto first = column * rows;
    if (first >= items.size())
      break;
    const auto count = std::min(rows, items.size() - first);
    const Rect box{rect.x + rect.width / columns * column, rect.y,
                   rect.width / columns, 21. * count};
    fillRect(rInsetRect(box, -2, -2), literal(0x080808));
    fillRect(box, literal(0x151515));
    for (std::size_t row = 0; row < count; ++row) {
      const auto index = first + row;
      const Rect r{box.x, box.y + 21. * row, box.width, 21};
      const bool hover = static_cast<int>(index) == _geometryMenuHoverIndex,
                 checked = static_cast<int>(index) == selected;
      if (hover || checked || row % 2)
        fillRect(rInsetRect(r, 1, 1),
                 hover ? literal(0x343434)
                       : checked ? literal(0x292929) : style.header);
      if (hover || checked)
        fillRect({r.x + 2, r.y + 2, 3, r.height - 4}, style.fill);
      if (row)
        list_.polyline({{r.x, r.y}, {rMaxX(r), r.y}}, literal(0x3a3a3a), 1);
      drawAtPoint(menuDisplayText(items[index], r.width - 18, attrs),
                  {r.x + 9, r.y + 4}, attrs);
    }
  }
}
} // namespace s3g::tracker::editor
