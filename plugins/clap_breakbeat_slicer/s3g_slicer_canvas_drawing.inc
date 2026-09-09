// Canvas primitives for the literal Slicer port. This file deliberately has no
// bank, playback, mapping, or mutation logic.
using namespace s3g::portable_gui::canvas;
namespace foundation = s3g::portable_gui::foundation;
inline CRect box(double x, double y, double w, double h) {
  return foundation::rect(x, y, w, h);
}
inline double minX(CRect r) { return r.left; }
inline double maxX(CRect r) { return r.right; }
inline double midX(CRect r) { return r.getCenter().x; }
inline double minY(CRect r) { return r.top; }
inline double maxY(CRect r) { return r.bottom; }
inline double midY(CRect r) { return r.getCenter().y; }
inline double rectWidth(CRect r) { return r.getWidth(); }
inline double rectHeight(CRect r) { return r.getHeight(); }
inline CRect inset(CRect r, double x, double y) { return r.inset(x, y); }
inline CRect intersection(CRect a, CRect b) { return a.bound(b); }
inline bool pointInRect(CPoint p, CRect r) {
  return foundation::contains(r, p);
}
inline std::string str(const char *s) { return s ? s : ""; }
inline const char *formatArg(const std::string &v) { return v.c_str(); }
template <class T> T formatArg(T v) { return v; }
template <class... Args>
std::string fmt(const char *pattern, const Args &... args) {
  return format(pattern, formatArg(args)...);
}
inline CRect mixerStripSliderTrackRect(double x, double w, double y) {
  const double label = x + s3g::gui_layout::kStandardMetrics.labelInset;
  const double value =
      x + w - s3g::gui_layout::kStandardMetrics.panelRightInset - 40;
  return box(label + 28, y + 1, std::max(12., value - label - 33), 9);
}
inline CRect mixerStripSliderHitRect(double x, double w, double y) {
  const auto &m = s3g::gui_layout::kStandardMetrics;
  return box(x + m.hitInset, y - 8, w - 2 * m.hitInset, m.hitHeight);
}
inline uint32_t multiColumnMenuRows(uint32_t count, uint32_t columns) {
  return (count + columns - 1) / columns;
}
struct TextStyle {
  CColor color;
  double size = 10;
};
struct TextSize {
  double width = 0, height = 0;
};
struct Style : foundation::Palette {
  CColor bg = background;
};
inline Style softTextStyle() { return {}; }
inline TextStyle slicerTextAttrs(CColor c, double size = 10, int = 0) {
  return {c, size};
}
inline TextStyle textAttrs(CColor c, double size = 10, int = 0) {
  return {c, size};
}
inline TextStyle softLabelAttrs() { return {foundation::palette().label, 10}; }
inline TextStyle softValueAttrs() { return {foundation::palette().value, 10}; }
inline TextStyle slicerSoftLabelAttrs() { return {color(0xa8a8a8), 10}; }
inline double gainDb(float gain) {
  return gain <= .001f ? -60 : 20 * std::log10(double(gain));
}
inline std::string peakDbText(float peak) {
  return fmt("PK %+.1f DB", 20 * std::log10(std::max(.000001f, peak)));
}

struct DrawPath {
  std::vector<std::pair<CPoint, bool>> points;
  double lineWidth = 1;
  CRect bounds;
  int shape = 0;
  static DrawPath rectangle(CRect r) {
    DrawPath p;
    p.bounds = r;
    p.shape = 1;
    return p;
  }
  static DrawPath oval(CRect r) {
    DrawPath p;
    p.bounds = r;
    p.shape = 2;
    return p;
  }
  void move(CPoint p) { points.push_back({p, true}); }
  void line(CPoint p) { points.push_back({p, false}); }
  void draw(CDrawContext &c, bool filled) {
    c.setLineWidth(lineWidth);
    if (shape == 1) {
      c.drawRect(bounds, filled ? kDrawFilled : kDrawStroked);
      return;
    }
    if (shape == 2) {
      c.drawEllipse(bounds, filled ? kDrawFilled : kDrawStroked);
      return;
    }
    auto path = owned(c.createGraphicsPath());
    if (!path)
      return;
    for (const auto &point : points) {
      if (point.second)
        path->beginSubpath(point.first);
      else
        path->addLine(point.first);
    }
    c.drawGraphicsPath(path, filled ? CDrawContext::kPathFilled
                                    : CDrawContext::kPathStroked);
  }
  void stroke(CDrawContext &c) { draw(c, false); }
  void fill(CDrawContext &c) { draw(c, true); }
};

class Drawing : public View {
public:
  Drawing() : View(kGuiWidth, kGuiHeight) {}
  void fillRect(CRect r) { context->drawRect(r, kDrawFilled); }
  void frameRect(CRect r, double weight = 1) {
    context->setLineWidth(weight);
    context->drawRect(r, kDrawStroked);
  }
  SharedPointer<CFontDesc> face(TextStyle a) {
    const double size = a.size + foundation::fontMetrics().body - 10.;
    for (const auto &item : fonts)
      if (item.first == size)
        return item.second;
    auto f = foundation::makeUiFont(size);
    fonts.push_back({size, f});
    return f;
  }
  TextSize textSize(const std::string &s, TextStyle a) {
    auto f = face(a);
    context->setFont(f);
    return {context->getStringWidth(s.c_str()), a.size + 3};
  }
  void drawAt(const std::string &s, CPoint p, TextStyle a) {
    auto f = face(a);
    foundation::drawTextLine(*context, s, p.x, p.y,
                             std::max(0., double(kGuiWidth) - p.x - 8), a.color,
                             f);
  }
  void drawText(const std::string &s, CRect r, TextStyle a) {
    auto f = face(a);
    text(s, r, a.color, kLeftText, f);
  }
  void drawCentered(CRect r, const std::string &s, TextStyle a) {
    auto f = face(a);
    text(s, r, a.color, kCenterText, f);
  }
  void drawCenteredTruncatedFilename(CRect r, std::string s, TextStyle a) {
    const auto fits = [&](const std::string &t) {
      return textSize(t, a).width <= r.getWidth();
    };
    // Middle truncation retains the file extension and both ends, as Cocoa did.
    if (!fits(s)) {
      std::vector<size_t> positions{0};
      for (size_t i = 0; i < s.size();) {
        ++i;
        while (i < s.size() && (uint8_t(s[i]) & 0xc0) == 0x80)
          ++i;
        positions.push_back(i);
      }
      size_t keep = positions.size() - 1;
      do {
        --keep;
        const auto left = (keep + 1) / 2, right = keep / 2;
        auto shown = s.substr(0, positions[left]) + "…" +
                     s.substr(positions[positions.size() - 1 - right]);
        if (fits(shown) || keep == 0) {
          s = shown;
          break;
        }
      } while (keep);
    }
    drawCentered(r, s, a);
  }
  void drawBoundedRightText(const std::string &s, CRect r, TextStyle a) {
    auto f = face(a);
    text(s, r, a.color, kRightText, f);
  }
  void drawRightStatus(const std::string &s, double w, double y, TextStyle a) {
    drawBoundedRightText(s, box(300, y, w - 318, 15), a);
  }
  void drawPanelFrame(double x, double y, double w, double h, const Style &s) {
    fill(box(x, y, w, h), s.cell);
  }
  void drawPanelHeader(const std::string &title, bool active, double x,
                       double y, double w, double h, TextStyle a,
                       const Style &s) {
    fill(box(x, y, w, h), s.strip);
    fill(box(x, y, w, 2), active ? s.accent : s.dim);
    drawAt(title, CPoint(x + 16, y + 5), a);
  }
  void drawHeaderButton(CRect r, CRect, const std::string &label, bool active,
                        TextStyle a, const Style &) {
    fill(r, color(active ? 0x3f3f3f : 0x191919));
    drawCentered(r, label, a);
  }
  void drawToolboxHeaderButton(CRect r, CRect panel, const std::string &label,
                               bool active, TextStyle a, const Style &s) {
    (void)panel;
    (void)s;
    auto f = face(a);
    foundation::drawButton(*context, r, label, f, active);
  }
  void drawButton(CRect r, const std::string &label, bool active = false) {
    fill(r, color(active ? 0x2b3730 : 0x303030));
    stroke(r, color(active ? 0x526459 : 0x565656));
    drawCentered(r, label, {color(active ? 0xb8c0bb : 0xb8b8b8), 10});
  }
  void drawLockedControlBar(CRect r, const std::string &label) {
    fill(r, color(0x181818));
    stroke(r, color(0x3c3c3c));
    drawCentered(r, label, {color(0x777777), 10});
  }
  void drawCompactMenu(CRect r, const std::string &label, bool, TextStyle a,
                       const Style &s) {
    auto f = face(a);
    foundation::drawMenuBox(*context, r, label, f, s);
  }
  void drawProcessorMenu(const std::string &label, const std::string &value,
                         double y, double x, double w, TextStyle a, TextStyle v,
                         const Style &s) {
    const double cx = s3g::gui_layout::processorControlX(x);
    drawText(label, box(x + 16, y - 1, cx - x - 20, 15), a);
    drawCompactMenu(box(cx, y - 1, s3g::gui_layout::processorMenuWidth(w), 15),
                    value, false, v, s);
  }
  void drawSlider(const std::string &label, const std::string &value, double n,
                  double y, TextStyle a, TextStyle v, const Style &s,
                  double labelX, double trackX, double valueX, double trackW,
                  double valueW) {
    const auto r = box(trackX, y + 1, trackW, 9);
    foundation::drawHorizontalSlider(*context, r, n, r.getCenter().y - 6.5, 13,
                                     s);
    drawText(upper(label),
             box(labelX, r.getCenter().y - 7.5, trackX - labelX - 4, 15), a);
    auto f = face(v);
    text(foundation::sliderValueTextToFit(*context, value, valueW, f),
         box(valueX, r.getCenter().y - 7.5, valueW, 15), v.color, kRightText,
         f);
  }
  void drawProcessorSlider(const std::string &label, const std::string &value,
                           double n, double y, double x, double w, TextStyle a,
                           TextStyle v, const Style &s) {
    drawSlider(label, value, n, y, a, v, s, x + 16,
               s3g::gui_layout::processorControlX(x),
               s3g::gui_layout::processorValueX(x, w),
               s3g::gui_layout::processorTrackWidth(w),
               s3g::gui_layout::kStandardMetrics.processorValueWidth);
  }
  void drawMixerStripSlider(const std::string &label, const std::string &value,
                            double n, double y, double x, double w, TextStyle a,
                            TextStyle v, const Style &s) {
    auto track = mixerStripSliderTrackRect(x, w, y);
    drawSlider(label, value, n, y, a, v, s,
               x + s3g::gui_layout::kStandardMetrics.labelInset, track.left,
               x + w - s3g::gui_layout::kStandardMetrics.panelRightInset - 40,
               track.getWidth(), 40);
  }
  void drawMixerFader(double n, CRect r, const Style &s) {
    n = std::clamp(n, 0., 1.);
    const auto t = box(midX(r) - 3, r.top, 6, r.getHeight());
    fill(t, s.strip);
    fill(box(t.left + 1, t.top + 1 + (t.getHeight() - 2) * (1 - n), 4,
             (t.getHeight() - 2) * n),
         s.fill);
    fill(
        box(r.left + 5, r.bottom - n * r.getHeight() - 4, r.getWidth() - 10, 9),
        s.text);
  }
  void drawDial(const std::string &label, const std::string &value, double n,
                CRect r, TextStyle a, TextStyle v, const Style &s) {
    drawCentered(box(r.left, r.top, r.getWidth(), 13), upper(label), a);
    const double d = std::min(32., r.getWidth() - 8);
    auto b = box(midX(r) - d * .5, r.top + 14, d, d);
    ellipse(b, s.strip);
    ellipse(b, s.grid, false);
    const double angle =
        (135 + 270 * std::clamp(n, 0., 1.)) * 3.141592653589793 / 180;
    line({midX(b) + std::cos(angle) * d * .13,
          midY(b) + std::sin(angle) * d * .13},
         {midX(b) + std::cos(angle) * d * .4,
          midY(b) + std::sin(angle) * d * .4},
         s.text, 2);
    fill(box(midX(b) - 1.5, midY(b) - 1.5, 3, 3), s.fill);
    drawCentered(box(r.left, b.bottom + 3, r.getWidth(), 13), value, v);
  }
  void drawVerticalVuMeter(double n, CRect r, const Style &s) {
    fill(r, s.strip);
    auto b = inset(r, 1, 1);
    const auto range = [&](double lo, double hi, CColor c) {
      const double top = std::min(n, hi);
      if (top > lo)
        fill(box(b.left, b.bottom - b.getHeight() * top, b.getWidth(),
                 b.getHeight() * (top - lo)),
             c);
    };
    range(0, .76, color(0x4f9a63));
    range(.76, .92, color(0xc59b3d));
    range(.92, 1, color(0xbd514d));
    stroke(r, s.grid);
  }

private:
  std::vector<std::pair<double, SharedPointer<CFontDesc>>> fonts;
};
