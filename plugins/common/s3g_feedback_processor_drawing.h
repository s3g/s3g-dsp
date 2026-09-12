#pragma once
#include "s3g_stereo_processor_drawing.h"
#include "vstgui/lib/cgradient.h"

namespace s3g::portable_gui::feedback_drawing {
using namespace stereo_drawing;
inline VSTGUI::CColor color(uint32_t rgb) { return decoder_drawing::calibratedColor(rgb); }
inline void frameRectWithWidth(Rect rect, double width) {
  current->context->setFrameColor(current->fill);
  current->context->setLineWidth(width);
  current->context->drawRect(foundation::rect(rect.origin.x, rect.origin.y, rect.size.width, rect.size.height), VSTGUI::kDrawStroked);
}
inline void sceneGradient(Rect rect, VSTGUI::CColor a, VSTGUI::CColor b) {
  auto path = VSTGUI::owned(current->context->createGraphicsPath());
  if (!path) return;
  path->addRect(foundation::rect(rect.origin.x, rect.origin.y, rect.size.width, rect.size.height));
  auto gradient = VSTGUI::owned(VSTGUI::CGradient::create(0., 1., a, b));
  current->context->fillLinearGradient(path, *gradient,
      {rect.origin.x, rect.origin.y}, {maxX(rect), rect.origin.y});
}
struct Style : ambi_effect_drawing::Style {
  VSTGUI::CColor cellBg = decoder_drawing::calibratedColor(0x1d1d1d);
};
inline void drawProcessorSlider(const String& name, const String& value, double norm,
    double y, double x, double w, Attrs, Attrs, const Style&) {
  const double control = gui_layout::processorControlX(x);
  const double track = gui_layout::processorTrackWidth(w);
  const auto box = foundation::rect(gui_layout::processorValueX(x, w), y - 2.,
      gui_layout::kStandardMetrics.processorValueWidth, 15.);
  drawProcessorLabel(name, x, y);
  foundation::drawHorizontalSlider(*current->context,
      foundation::rect(control, y + 1., track, 9.), norm, y - 1., 13., foundation::palette());
  // Preserve prefixed numbers AND units (e.g. "E 16.0%", "GAP 1.2s").
  // The original Cocoa readout has a fixed width; fit the complete Fira string
  // into that same rectangle instead of discarding words to make it fit.
  current->context->setFont(current->font);
  const double width = current->context->getStringWidth(value.c_str());
  if (width <= box.getWidth())
    current->view.text(value, box, foundation::palette().value, VSTGUI::kRightText);
  else {
    auto font = VSTGUI::owned(new VSTGUI::CFontDesc(current->font->getName(),
        current->font->getSize() * box.getWidth() / width));
    current->view.text(value, box, foundation::palette().value, VSTGUI::kRightText, font);
  }
}
inline VSTGUI::CColor calibratedWhite(double white, double alpha) {
  return calibratedRGB(white, white, white, alpha);
}
inline void drawMiddleTruncated(const String& text, Rect box, Attrs attrs) {
  String shown = text;
  std::vector<size_t> boundary;
  for (size_t n = 0; n < text.size(); ++n)
    if ((static_cast<unsigned char>(text[n]) & 0xc0u) != 0x80u) boundary.push_back(n);
  boundary.push_back(text.size());
  const size_t count = boundary.size() - 1u;
  for (size_t keep = count; sizeWithAttributes(shown, attrs).width > box.size.width && keep > 0;) {
    --keep;
    const size_t left = (keep + 1u) / 2u, right = keep / 2u;
    shown = text.substr(0, boundary[left]) + "…" + text.substr(boundary[count - right]);
  }
  drawAtPoint_withAttributes(shown, box.origin, attrs);
}
inline constexpr uint32_t kMidiChannelMenuColumns = 2u;
inline constexpr uint32_t kMidiReceiveMenuItemCount = 17u;
inline int midiReceiveDropdownHitIndex(Point p, Rect menu, double height) {
  return multiColumnDropdownHitIndex(p, menu, height, 17u, 2u);
}
inline void drawMidiReceiveDropdownMenu(Rect menu, double height, int selected,
    int hover, Attrs attrs, const Style& style) {
  String items[17] = {"OMNI"};
  for (uint32_t i = 1; i < 17; ++i) items[i] = "CHANNEL " + std::to_string(i);
  drawMultiColumnDropdownMenu(menu, height, items, 17, 2, selected, hover, attrs, style);
}
inline void drawProcessorTitleBand(const String& title, const String& preset,
    const String& status, const gui_layout::EncoderTitleBand& band,
    Attrs, Attrs, Attrs, const Style& style) {
  auto titles = softTitleAttrs(), labels = softLabelAttrs(), values = softValueAttrs();
  drawAtPoint_withAttributes(title, makePoint(band.titleX, band.titleY), titles);
  circuit_drawing::drawEncoderPresetMenu(canvas::upper(preset), band, labels, values, style);
  drawHeaderActionButton(cocoaRect(band.loadButton), cocoaRect(band.loadButton), "LOAD", labels, style);
  drawHeaderActionButton(cocoaRect(band.saveButton), cocoaRect(band.saveButton), "SAVE", labels, style);
  const auto& shown = current->error && !current->error->empty() ? *current->error : status;
  if (!shown.empty()) drawRightStatus(shown, band.canvas.width, band.titleY, values, band.statusRightInset);
}
}
