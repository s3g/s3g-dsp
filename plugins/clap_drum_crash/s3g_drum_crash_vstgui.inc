// Literal Cocoa S3GDrumCrashView drawing and articulation layout.
// Parameter tables, factory assignments, randomization and DSP remain shared.
namespace drum_canvas {
using namespace s3g::portable_gui::canvas;
namespace foundation = s3g::portable_gui::foundation;
constexpr const char *pluginName = "Drum Crash";
constexpr const char *pluginTitle = "s3g DRUM CRASH";
constexpr int factoryCount = s3g::kDrumCrashFactoryPresetCount;
const char *factoryName(int index) {
  return s3g::drumCrashFactoryPresetInfo(index).name;
}
int factoryIndex(const Plugin &p) {
  return s3g::drumCrashFactoryPresetIndex(publishedParamsSnapshot(p));
}
bool randomizeVoice(Plugin &p, uint32_t &seed) {
  const auto randomized =
      s3g::drumCrashSafeRandomParams(publishedParamsSnapshot(p), seed);
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
constexpr std::array<const char *, 3u> padLabels{{"CRASH", "CHOKE", "BELL"}};
CRect performancePadRect(uint32_t code) {
  if (code < 1u || code > padLabels.size())
    return {};
  constexpr double gap = 6.;
  constexpr double width = (kPanelWidth - 24. - (3 - 1) * gap) / 3.;
  return rect(kRightPanelX + 12. + (code - 1u) * (width + gap), 480., width,
              24.);
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
    panel(rect(kLeftPanelX, 50.0, kPanelWidth, 262.0), "METAL FIELD");
    panel(rect(kRightPanelX, 50.0, kPanelWidth, 262.0), "WASH / BELL");
    panel(rect(kLeftPanelX, 344.0, kPanelWidth, 218.0),
          "SHARED DRUM CHARACTER");
    panel(rect(kRightPanelX, 344.0, kPanelWidth, 320.0),
          "PERFORMANCE / OUTPUT");
    paintRows();
    for (uint32_t code = 1u; code <= padLabels.size(); ++code) {
      pad(performancePadRect(code), padLabels[code - 1u],
          visualTrigger == code && activity > .02f,
          [this, code] { queueGuiTrigger(p, code); });
    }
    const CPoint center = CPoint(687.0, 568.0);
    for (uint32_t ring = 0u; ring < 5u; ++ring) {
      const double inset = static_cast<double>(ring) * 13.0;
      const double width = 132.0 + activity * 14.0 - inset;
      const double height = 54.0 + activity * 8.0 - inset * 0.30;
      const double alpha = 0.30 + activity * 0.52;
      context->setFrameColor(rgba(0.30 + activity * 0.38,
                                  0.62 + activity * 0.24,
                                  0.66 + activity * 0.20, alpha));
      auto path = owned(context->createGraphicsPath());
      path->addEllipse(
          rect(center.x - width * 0.5, center.y - height * 0.5, width, height));
      context->setLineWidth(1.0 + activity * 2.0);
      context->drawGraphicsPath(path, CDrawContext::kPathStroked);
    }
    context->setFrameColor(rgba(0.72, 0.86, 0.82, 0.45 + activity * 0.45));
    for (uint32_t ray = 0u; ray < 12u; ++ray) {
      const double angle = static_cast<double>(ray) * s3g::kPi / 6.0;
      const double radius = 65.0 + activity * 13.0;
      auto path = owned(context->createGraphicsPath());
      path->beginSubpath(center);
      path->addLine(CPoint(center.x + std::cos(angle) * radius,
                           center.y + std::sin(angle) * radius * 0.62));
      context->setLineWidth(0.7 + activity * 0.7);
      context->drawGraphicsPath(path, CDrawContext::kPathStroked);
    }
    context->setFillColor(rgba(0.90, 0.90, 0.90, 0.85));
    const double clutchRadius = 5.0 + activity * 5.0;
    context->drawEllipse(rect(center.x - clutchRadius, center.y - clutchRadius,
                              clutchRadius * 2.0, clutchRadius * 2.0),
                         kDrawFilled);
    foundation::drawTextLine(*context, "SYNTHESIZED CRASH  //  CHOKE  //  BELL",
                             kRightPanelX + 58.0, 646.0, kPanelWidth - 24.,
                             style.label, font);
  }
};
} // namespace drum_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new drum_canvas::Editor(p);
}
