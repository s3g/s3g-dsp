#pragma once
#include "s3g_map_encoder_drawing.h"

namespace s3g::portable_gui::resonator_drawing {
using namespace map_drawing;
using decoder_drawing::calibratedColor;
inline void drawEncoderTitleBand(const String &title, const String &preset,
                                 const String &status,
                                 const gui_layout::EncoderTitleBand &band,
                                 Attrs titles, Attrs labels, Attrs values,
                                 const Style &style) {
  input_encoder_drawing::drawEncoderTitleBand(title, canvas::upper(preset),
                                              status, band, titles, labels,
                                              values, style);
}
inline void drawProcessorMenu(const String &name, const String &value, double y,
                              double x, double w, Attrs labels, Attrs values,
                              const Style &style) {
  ambi_effect_drawing::drawProcessorMenu(name, canvas::upper(value), y, x, w,
                                         labels, values, style);
}
inline uint32_t randomBits() {
  static thread_local std::mt19937 generator(std::random_device{}());
  return generator();
}
inline uint32_t randomBounded(uint32_t upper) {
  if (!upper)
    return 0;
  const uint32_t threshold = uint32_t(-upper) % upper;
  uint32_t value;
  do {
    value = randomBits();
  } while (value < threshold);
  return value % upper;
}
} // namespace s3g::portable_gui::resonator_drawing
