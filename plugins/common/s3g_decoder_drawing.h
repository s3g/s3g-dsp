#pragma once
#include "s3g_ambi_effect_drawing.h"
#include <optional>

namespace s3g::portable_gui::decoder_drawing {
using namespace ambi_effect_drawing;
inline CColor srgbColor(double r, double g, double b, double alpha) {
  return {uint8_t(std::round(std::clamp(r, 0., 1.) * 255.)),
          uint8_t(std::round(std::clamp(g, 0., 1.) * 255.)),
          uint8_t(std::round(std::clamp(b, 0., 1.) * 255.)),
          uint8_t(std::clamp(alpha, 0., 1.) * 255.)};
}
inline CColor calibratedColor(uint32_t rgb, double alpha = 1.) {
  return calibratedRGB(((rgb >> 16) & 255) / 255., ((rgb >> 8) & 255) / 255.,
                       (rgb & 255) / 255., alpha);
}
inline auto encoderTitleBand(double w, double h) {
  return gui_layout::encoderTitleBand({w, h});
}
inline String peakDbText(float left, float right) {
  return ambi_effect_drawing::peakDbText(std::max(left, right));
}
using ambi_effect_drawing::peakDbText;
inline void drawDecoderTitleBand(const String &title, const String &preset,
                                 const String &status,
                                 const gui_layout::EncoderTitleBand &band,
                                 Attrs, Attrs, Attrs, const Style &style) {
  drawTransformTitleBand(title, preset, status, band, style);
}
inline void drawSlider(const String &name, const String &value, double norm,
                       double y, Attrs label, Attrs values, const Style &style,
                       double labelX = 654., double trackX = 750.,
                       double valueX = 920., double trackW = 150.,
                       double valueW = 36.) {
  current->view.text(canvas::upper(name),
                     f::rect(labelX, y - 2., trackX - labelX - 8., 15.),
                     label.color);
  f::drawHorizontalSlider(*current->context,
                          f::rect(trackX, y + 1., trackW, 9.), norm, y - 1.,
                          13., f::palette());
  auto fitted =
      f::sliderValueTextToFit(*current->context, value, valueW, current->font);
  current->view.text(fitted, f::rect(valueX, y - 2., valueW, 15.), values.color,
                     VSTGUI::kRightText);
}
inline void drawMenu(const String &name, const String &value, double y,
                     Attrs label, Attrs, const Style &, double labelX = 654.,
                     double boxX = 750., double boxW = 178.) {
  current->view.text(canvas::upper(name),
                     f::rect(labelX, y - 2., boxX - labelX - 8., 15.),
                     label.color);
  f::drawMenuBox(*current->context, f::rect(boxX, y - 1., boxW, 15.),
                 canvas::upper(value), current->font, f::palette());
}
inline void drawDropdownMenu(Rect bounds, double rowHeight, const String *items,
                             unsigned count, int selected, int hover,
                             Attrs attrs, const Style &style) {
  std::vector<String> labels;
  labels.reserve(count);
  for (unsigned n = 0; n < count; ++n)
    labels.push_back(canvas::upper(items[n]));
  ambi_effect_drawing::drawDropdownMenu(bounds, rowHeight, labels.data(), count,
                                        selected, hover, attrs, style);
}
inline CColor heatColor(double value, double alpha = 1.) {
  struct Stop {
    double t;
    int r, g, b;
  };
  constexpr Stop stops[] = {{0., 10, 24, 94},
                            {.22, 0, 146, 232},
                            {.48, 255, 232, 42},
                            {.72, 255, 84, 12},
                            {1., 238, 0, 0}};
  value = std::clamp(value, 0., 1.);
  const Stop *a = &stops[0];
  const Stop *b = &stops[4];
  for (unsigned n = 1; n < 5; ++n)
    if (value <= stops[n].t) {
      a = &stops[n - 1];
      b = &stops[n];
      break;
    }
  const double mix = (value - a->t) / std::max(.0001, b->t - a->t);
  return calibratedRGB((a->r + (b->r - a->r) * mix) / 255.,
                       (a->g + (b->g - a->g) * mix) / 255.,
                       (a->b + (b->b - a->b) * mix) / 255., alpha);
}
} // namespace s3g::portable_gui::decoder_drawing
