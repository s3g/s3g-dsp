// Literal Cocoa S3GDrumFloorTomView drawing and articulation layout.
// Parameter tables, factory assignments, randomization and DSP remain shared.
namespace drum_canvas {
using namespace s3g::portable_gui::canvas;
namespace foundation = s3g::portable_gui::foundation;
constexpr const char *pluginName = "Drum Floor Tom";
constexpr const char *pluginTitle = "s3g DRUM FLOOR TOM";
constexpr int factoryCount = s3g::kDrumFloorTomFactoryPresetCount;
const char *factoryName(int index) {
  return s3g::drumFloorTomFactoryPresetInfo(index).name;
}
int factoryIndex(const Plugin &p) {
  return s3g::drumFloorTomFactoryPresetIndex(publishedParamsSnapshot(p));
}
bool randomizeVoice(Plugin &p, uint32_t &seed) {
  const auto randomized =
      s3g::drumFloorTomSafeRandomParams(publishedParamsSnapshot(p), seed);
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
constexpr std::array<const char *, 2u> padLabels{{"HEAD", "RIM"}};
CRect performancePadRect(uint32_t code) {
  if (code < 1u || code > padLabels.size())
    return {};
  constexpr double gap = 6.;
  constexpr double width = (kPanelWidth - 24. - (2 - 1) * gap) / 2.;
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
    panel(rect(kLeftPanelX, 50.0, kPanelWidth, 262.0), "HEAD / SHELL");
    panel(rect(kRightPanelX, 50.0, kPanelWidth, 262.0), "RIM / STICK");
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
    for (uint32_t ring = 0u; ring < 4u; ++ring) {
      const double baseRadius = 22.0 + ring * 17.0;
      const double pulse = activity * (26.0 - ring * 4.0);
      const double radius = baseRadius + pulse;
      const double alpha = 0.16 + activity * (0.65 - ring * 0.08);
      context->setFrameColor(rgba(0.30 + activity * 0.38,
                                  0.62 + activity * 0.24,
                                  0.66 + activity * 0.20, alpha));
      auto path = owned(context->createGraphicsPath());
      path->addEllipse(rect(center.x - radius, center.y - radius, radius * 2.0,
                            radius * 2.0));
      context->setLineWidth(1.0 + activity * 2.0);
      context->drawGraphicsPath(path, CDrawContext::kPathStroked);
    }
    context->setFillColor(rgba(0.90, 0.90, 0.90, 0.85));
    const double coreRadius = 5.0 + activity * 12.0;
    context->drawEllipse(rect(center.x - coreRadius, center.y - coreRadius,
                              coreRadius * 2.0, coreRadius * 2.0),
                         kDrawFilled);
    context->setFrameColor(rgba(0.72, 0.86, 0.82, 0.45 + activity * 0.45));
    for (uint32_t wire = 0u; wire < 7u; ++wire) {
      const double y = center.y - 21.0 + wire * 7.0;
      auto path = owned(context->createGraphicsPath());
      path->beginSubpath(CPoint(center.x - 57.0, y));
      path->addBezierCurve(CPoint(center.x - 18.0, y + activity * 5.0),
                           CPoint(center.x + 18.0, y - activity * 5.0),
                           CPoint(center.x + 57.0, y));
      context->setLineWidth(0.8 + activity * 0.8);
      context->drawGraphicsPath(path, CDrawContext::kPathStroked);
    }
    foundation::drawTextLine(*context, "SYNTHESIZED HEAD  //  RIM + STICK",
                             kRightPanelX + 78.0, 646.0, kPanelWidth - 24.,
                             style.label, font);
  }
};
} // namespace drum_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new drum_canvas::Editor(p);
}
