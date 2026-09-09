// Literal Cocoa S3GDrumCowbellView drawing and articulation layout.
// Parameter tables, factory assignments, randomization and DSP remain shared.
namespace drum_canvas {
using namespace s3g::portable_gui::canvas;
namespace foundation = s3g::portable_gui::foundation;
constexpr const char *pluginName = "Drum Cowbell";
constexpr const char *pluginTitle = "s3g DRUM COWBELL";
constexpr int factoryCount = s3g::kDrumCowbellFactoryPresetCount;
const char *factoryName(int index) {
  return s3g::drumCowbellFactoryPresetInfo(index).name;
}
int factoryIndex(const Plugin &p) {
  return s3g::drumCowbellFactoryPresetIndex(publishedParamsSnapshot(p));
}
bool randomizeVoice(Plugin &p, uint32_t &seed) {
  const auto randomized =
      s3g::drumCowbellSafeRandomParams(publishedParamsSnapshot(p), seed);
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
constexpr std::array<const char *, 3u> padLabels{{"COW", "MUTED", "HIGH"}};
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
    panel(rect(kLeftPanelX, 50.0, kPanelWidth, 262.0), "OSCILLATORS / STRIKE");
    panel(rect(kRightPanelX, 50.0, kPanelWidth, 262.0), "SHELL / MOTION");
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
    for (uint32_t shell = 0u; shell < 3u; ++shell) {
      const double inset = static_cast<double>(shell) * 9.0;
      context->setFrameColor(
          rgba(0.30 + activity * 0.38, 0.62 + activity * 0.24,
               0.66 + activity * 0.20, 0.30 + activity * 0.52));
      auto path = owned(context->createGraphicsPath());
      path->beginSubpath(
          CPoint(center.x - 52.0 + inset, center.y + 31.0 - inset * 0.28));
      path->addLine(
          CPoint(center.x + 52.0 - inset, center.y + 31.0 - inset * 0.28));
      path->addLine(
          CPoint(center.x + 34.0 - inset * 0.58, center.y - 36.0 + inset));
      path->addLine(
          CPoint(center.x - 34.0 + inset * 0.58, center.y - 36.0 + inset));
      path->closeSubpath();
      context->setLineWidth(1.0 + activity * 1.8);
      context->drawGraphicsPath(path, CDrawContext::kPathStroked);
    }
    context->setFrameColor(rgba(0.72, 0.86, 0.82, 0.45 + activity * 0.45));
    for (uint32_t ray = 0u; ray < 7u; ++ray) {
      const double angle = -0.72 + static_cast<double>(ray) * 0.24;
      const double radius = 52.0 + activity * 13.0;
      auto path = owned(context->createGraphicsPath());
      path->beginSubpath(CPoint(center.x, center.y + 32.0));
      path->addLine(CPoint(center.x + std::cos(angle) * radius,
                           center.y + 32.0 + std::sin(angle) * radius));
      context->setLineWidth(0.7 + activity * 0.7);
      context->drawGraphicsPath(path, CDrawContext::kPathStroked);
    }
    context->setFillColor(rgba(0.90, 0.90, 0.90, 0.85));
    const double clutchRadius = 4.0 + activity * 4.0;
    context->drawEllipse(rect(center.x - clutchRadius,
                              center.y - 44.0 - clutchRadius,
                              clutchRadius * 2.0, clutchRadius * 2.0),
                         kDrawFilled);
    foundation::drawTextLine(
        *context, "SYNTHESIZED COWBELL  //  MUTED  //  HIGH",
        kRightPanelX + 58.0, 646.0, kPanelWidth - 24., style.label, font);
  }
};
} // namespace drum_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new drum_canvas::Editor(p);
}
