// Literal compact Cocoa effect layout, using the original panel/index tables.
namespace fx_canvas {
using namespace s3g::portable_gui::canvas;
namespace foundation = s3g::portable_gui::foundation;
constexpr const char *pluginName = "Drum Overload";
constexpr const char *pluginTitle = "s3g EFFECT DRUM OVERLOAD";
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
      if (id == kCircuitParamId)
        items = {"CONSOLE", "VALVE",       "CLIP",  "RUPTURE",
                 "TAPE",    "TRANSFORMER", "DIODE", "SPEAKER"};
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
    panel(box(kDrivePanel.frame), "DRIVE");
    panel(box(kColorPanel.frame), "COLOR");
    rows(kOutputPanel, kOutputParamIndices, std::size(kOutputParamIndices));
    rows(kDrivePanel, kDriveParamIndices, std::size(kDriveParamIndices));
    rows(kColorPanel, kColorParamIndices, std::size(kColorParamIndices));
    text(format("GR %+.1f dB  //  CORE %.0f%%",
                double(p.gainReductionDb.load(std::memory_order_relaxed)),
                p.overloadActivity.load(std::memory_order_relaxed) * 100.),
         rect(kColorPanel.frame.x + 16.,
              kColorPanel.frame.y + kColorPanel.frame.height + 12.,
              kColorPanel.frame.width - 32., 15.),
         style.value);
  }
};
} // namespace fx_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new fx_canvas::Editor(p);
}
