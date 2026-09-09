// Literal compact Cocoa effect layout, using the original panel/index tables.
namespace fx_canvas {
using namespace s3g::portable_gui::canvas;
namespace foundation = s3g::portable_gui::foundation;
constexpr const char *pluginName = "Drum Echo";
constexpr const char *pluginTitle = "s3g DRUM ECHO";
constexpr clap_id preservedOutput = kOutputParamId;
#include "../common/s3g_drum_effect_canvas.inc"

class Editor final : public EffectEditor {
public:
  explicit Editor(Plugin &p) : EffectEditor(p) {}
  void rows(const s3g::gui_layout::Panel &panel, const uint32_t *indices,
            uint32_t count) {
    const auto &metrics = s3g::gui_layout::kStandardMetrics;
    for (uint32_t row = 0; row < count; ++row) {
      const auto &def = kParamDefs[indices[row]];
      const auto id = def.id;
      const double y = s3g::gui_layout::rowY(panel, row);
      const double x = s3g::gui_layout::processorControlX(panel.frame.x);
      const auto label =
          rect(s3g::gui_layout::processorLabelX(panel.frame.x), y - 2.,
               x - panel.frame.x - metrics.labelInset - 4., 15.);
      std::vector<std::string> items;
      if (id == kHeadModeParamId)
        items = {"HEAD 1",   "HEAD 2",   "HEAD 3",   "HEAD 1+2",
                 "HEAD 2+3", "HEAD 1+3", "ALL HEADS"};
      if (id == kClockParamId)
        items = {"FREE", "1/32", "1/16T", "1/16", "1/8T",
                 "1/8",  "1/4T", "1/4",   "1/2",  "1 BAR"};
      if (!items.empty()) {
        fittedText(def.label, label, style.label);
        controlMenu(id,
                    rect(x, y - 1.,
                         s3g::gui_layout::processorMenuWidth(panel.frame.width),
                         15.),
                    items);
      } else {
        slider(id, def.label, box(s3g::gui_layout::sliderHitRect(panel, row)),
               rect(x, y + 1.,
                    s3g::gui_layout::processorTrackWidth(panel.frame.width),
                    9.),
               label,
               rect(s3g::gui_layout::processorValueX(panel.frame.x,
                                                     panel.frame.width),
                    y - 2., metrics.processorValueWidth, 15.),
               (value(id) - def.minimum) / (def.maximum - def.minimum));
      }
    }
  }
  void paint() override {
    paintTitle(peakText(p.outputPeak.load(std::memory_order_relaxed)));
    panel(box(kOutputPanel.frame), "OUTPUT");
    panel(box(kEchoPanel.frame), "MULTI-HEAD TAPE");
    panel(box(kResponsePanel.frame), "DRUM RESPONSE");
    rows(kOutputPanel, kOutputParamIndices, std::size(kOutputParamIndices));
    rows(kEchoPanel, kEchoParamIndices, std::size(kEchoParamIndices));
    rows(kResponsePanel, kResponseParamIndices,
         std::size(kResponseParamIndices));
    text(format("HIT %.0f%%  //  WET DUCK %.0f%%",
                p.transientActivity.load(std::memory_order_relaxed) * 100.,
                (1. - p.duckGain.load(std::memory_order_relaxed)) * 100.),
         rect(kResponsePanel.frame.x + 16.,
              kResponsePanel.frame.y + kResponsePanel.frame.height + 12.,
              kResponsePanel.frame.width - 32., 15.),
         style.value);
  }
};
} // namespace fx_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new fx_canvas::Editor(p);
}
