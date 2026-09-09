// Literal Cocoa S3GDrumBreakView drawing and articulation layout.
// Parameter tables, factory assignments, randomization and DSP remain shared.
namespace drum_canvas {
using namespace s3g::portable_gui::canvas;
namespace foundation = s3g::portable_gui::foundation;
constexpr const char *pluginName = "Drum Break";
constexpr const char *pluginTitle = "s3g DRUM BREAK";
constexpr int factoryCount = s3g::kDrumBreakFactoryPresetCount;
const char *factoryName(int index) {
  return s3g::drumBreakFactoryPresetInfo(index).name;
}
int factoryIndex(const Plugin &p) {
  return s3g::drumBreakFactoryPresetIndex(publishedParamsSnapshot(p));
}
bool randomizeVoice(Plugin &p, uint32_t &seed) {
  const auto randomized =
      s3g::drumBreakSafeRandomParams(publishedParamsSnapshot(p), seed);
  seed = std::random_device{}();
  if (seed == 0u)
    seed = 1u;
  return queueGuiSafeRandomParamSet(p, randomized);
}
bool loadUserPreset(Plugin &p, const clap_istream_t *stream) {
  p.preservedOutputValue.store(paramValue(p, kOutputParamId),
                               std::memory_order_release);
  p.preserveOutputOnNextStateLoad.store(true, std::memory_order_release);
  const bool loaded = stateLoad(&p.plugin, stream);
  p.preserveOutputOnNextStateLoad.store(false, std::memory_order_release);
  return loaded;
}
constexpr std::array<const char *, 4u> padLabels{
    {"KICK", "SNARE", "TOM", "HI-HAT"}};
CRect performancePadRect(uint32_t code) {
  const double gap = 4.0;
  const double padWidth = (kPanelWidth - 24.0 - gap * 3.0) / 4.0;
  return rect(kRightPanelX + 12.0 +
                  static_cast<double>(code - 1u) * (padWidth + gap),
              672.0, padWidth, 24.0);
}
#include "../common/s3g_drum_canvas.inc"
class Editor final : public DrumEditor {
public:
  explicit Editor(Plugin &p) : DrumEditor(p) {}
  void paint() override {
    const float activity =
        std::clamp(p.visualActivity.load(std::memory_order_relaxed), 0.f, 1.f);
    const uint32_t visualTrigger =
        p.visualTriggerCode.load(std::memory_order_relaxed);
    paintTitle(activity);
    panel(rect(kLeftPanelX, 50.0, kPanelWidth, 176.0), "KICK");
    panel(rect(kRightPanelX, 50.0, kPanelWidth, 176.0), "SNARE");
    panel(rect(kLeftPanelX, 242.0, kPanelWidth, 154.0), "TOM");
    panel(rect(kRightPanelX, 242.0, kPanelWidth, 154.0), "HI-HAT");
    panel(rect(kLeftPanelX, 412.0, kPanelWidth * 2.0 + 20.0, 82.0), "COHESION");
    panel(rect(kLeftPanelX, 510.0, kPanelWidth, 234.0),
          "SHARED DRUM CHARACTER");
    panel(rect(kRightPanelX, 510.0, kPanelWidth, 234.0),
          "PERFORMANCE / OUTPUT");
    paintRows();
    for (uint32_t code = 1u; code <= padLabels.size(); ++code) {
      pad(performancePadRect(code), padLabels[code - 1u],
          visualTrigger == code && activity > .02f,
          [this, code] { queueGuiTrigger(p, code); });
    }
    const double visualLeft = kRightPanelX + 58.0;
    const double visualWidth = 318.0;
    constexpr double cycles[]{1.5, 4.5, 2.7, 11.0};
    for (uint32_t band = 0u; band < 4u; ++band) {
      const double centerY = 710.0 + static_cast<double>(band) * 10.0;
      const bool struck = visualTrigger == band + 1u && activity > 0.02f;
      const double amplitude = 1.8 + static_cast<double>(band) * 0.35 +
                               (struck ? activity * 5.0 : activity);
      context->setFrameColor(
          rgba(0.50 + (struck ? 0.22 : 0.0), 0.72 + (struck ? 0.16 : 0.0),
               0.70 + (struck ? 0.13 : 0.0), 0.34 + activity * 0.46));
      auto path = owned(context->createGraphicsPath());
      for (uint32_t point = 0u; point <= 48u; ++point) {
        const double phase = static_cast<double>(point) / 48.0;
        const double taper = std::sin(static_cast<double>(s3g::kPi) * phase);
        const CPoint next =
            CPoint(visualLeft + phase * visualWidth,
                   centerY + std::sin(phase * cycles[band] * 2.0 *
                                      static_cast<double>(s3g::kPi)) *
                                 amplitude * taper);
        if (point == 0u)
          path->beginSubpath(next);
        else
          path->addLine(next);
      }
      context->setLineWidth(
          band == 0u ? 2.2 : (band == 2u ? 1.8 : (band == 1u ? 1.4 : 0.9)));
      context->drawGraphicsPath(path, CDrawContext::kPathStroked);
    }
  }
};
} // namespace drum_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new drum_canvas::Editor(p);
}
