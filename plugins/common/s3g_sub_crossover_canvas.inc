namespace crossover_canvas {
using namespace s3g::portable_gui::routing;
namespace foundation = s3g::portable_gui::foundation;
namespace layout = s3g::gui_layout;
constexpr const auto &family = layout::kOutputUtilityFamilyLayout;
constexpr auto outputPanel =
    layout::outputUtilityPanel(layout::PanelRole::Output, 42., 4u);
constexpr auto routingPanel =
    layout::fittedStackPanel(layout::PanelRole::Routing, outputPanel, 5u);
constexpr auto crossoverPanel =
    layout::fittedStackPanel(layout::PanelRole::Engine, routingPanel, 2u);
constexpr const char *pluginName = "Output Crossover";
constexpr const char *pluginTitle = "s3g OUTPUT CROSSOVER";
constexpr clap_id preservedOutput =
    CLAP_INVALID_ID; // Cocoa presets restore both output gains.
constexpr const auto &stateExtension = state;
#include "s3g_routing_parameter_editor.inc"
class Editor final : public ParameterEditor {
public:
  explicit Editor(Plugin &p) : ParameterEditor(p) { probe.prepare(48000.); }
  double refreshPeriodMilliseconds() const override { return 1000. / 30.; }
  CPoint fieldPointForSpeaker(float az, float el, float distance, double radius,
                              CPoint center) const {
    const auto dir = s3g::directionFromAed(az, el);
    return {center.x + double(-dir.y * distance) * radius,
            center.y + double(-dir.x * distance) * radius};
  }
  bool overlaps(CPoint a, CPoint b) const {
    return std::abs(a.x - b.x) <= 1.5 && std::abs(a.y - b.y) <= 1.5;
  }
  void drawField() {
    const auto r = box(family.field);
    fill(r, calibratedGray(0x18));
    const CPoint c{r.origin.x + r.size.width * .5,
                   r.origin.y + r.size.height * .5 + 8.};
    const double radius = std::min(r.size.width, r.size.height) * .34;
    at("FRONT", c.x - 18., r.origin.y + 16., style.value);
    s3g::LayoutPannerParams lp{};
    lp.layout =
        static_cast<s3g::LayoutPannerPreset>(unsigned(value(kParamLayout)));
    lp.activeSpeakers = unsigned(value(kParamHighChannels));
    lp.customShape = s3g::LayoutPannerCustomShape::Ring;
    probe.setParams(lp);
    if (probe.activeSpeakers() != unsigned(value(kParamHighChannels))) {
      lp.layout = s3g::LayoutPannerPreset::Custom;
      probe.setParams(lp);
    }
    const unsigned count =
        std::min(unsigned(value(kParamHighChannels)), kChannelCount);
    std::array<s3g::LayoutPannerSpeaker, kChannelCount> speakers{};
    for (unsigned i = 0; i < count; ++i) {
      speakers[i] = probe.speaker(i);
      auto sp = speakers[i];
      speakerPoints[i] = fieldPointForSpeaker(sp.azimuthDeg, sp.elevationDeg,
                                              sp.distance, radius, c);
    }
    lp.layout =
        static_cast<s3g::LayoutPannerPreset>(unsigned(value(kParamLayout)));
    std::array<std::array<bool, kChannelCount>, kChannelCount> seen{};
    const auto segment = [&](CPoint a, CPoint b) {
      line(a, b, calibratedGray(0x56, .62));
    };
    pannerMesh(
        lp, speakers, count,
        [&](s3g::Vec3 v) {
          return CPoint{c.x - double(v.y) * radius, c.y - double(v.x) * radius};
        },
        [&](unsigned a, unsigned b) {
          if (a >= count || b >= count || a == b)
            return;
          const auto lo = std::min(a, b), hi = std::max(a, b);
          if (seen[lo][hi])
            return;
          seen[lo][hi] = true;
          segment(speakerPoints[a], speakerPoints[b]);
        },
        segment);
    for (unsigned i = 0; i < count; ++i)
      fill(rect(speakerPoints[i].x - 4., speakerPoints[i].y - 4., 8., 8.),
           calibratedGray(0x7a));
    std::array<bool, kChannelCount> used{};
    groupedLabels.clear();
    for (unsigned i = 0; i < count; ++i) {
      if (used[i])
        continue;
      used[i] = true;
      std::string label = format("%u", i + 1);
      for (unsigned j = i + 1; j < count; ++j)
        if (!used[j] && overlaps(speakerPoints[i], speakerPoints[j])) {
          used[j] = true;
          label += format("/%u", j + 1);
        }
      groupedLabels.push_back(label);
      at(label, speakerPoints[i].x + 7., speakerPoints[i].y - 7., style.value);
    }
    const unsigned subs = std::max(1u, unsigned(value(kParamSubCount)));
    for (unsigned i = 0; i < subs; ++i) {
      const float az =
          s3g::layoutPannerWrapDeg(-45.f - float(i) * 360.f / float(subs));
      const auto pt = fieldPointForSpeaker(az, 0.f, 1.2f, radius, c);
      fill(rect(pt.x - 6.5, pt.y - 6.5, 13., 13.), calibratedGray(0xbd));
      at(format("S%u", unsigned(value(kParamSubOffset)) + i), pt.x + 13. * .55,
         pt.y - 7., style.value);
    }
    text(format("%s  high:%u  subs:%u  offset:%u",
                s3g::layoutPannerPresetName(lp.layout), count, subs,
                unsigned(value(kParamSubOffset))),
         rect(r.origin.x + 12., maxY(r) - 26., r.size.width - 24., 15.),
         style.value);
  }
  void toggleRow(const char *label, clap_id id, unsigned row) {
    const double y = layout::rowY(outputPanel, row),
                 x = layout::processorControlX(outputPanel.frame.x),
                 w = layout::processorMenuWidth(outputPanel.frame.width);
    at(label, layout::processorLabelX(outputPanel.frame.x), y - 2.,
       style.label);
    button(
        rect(x, y - 1., w, 15.), value(id) >= .5 ? "ON" : "OFF",
        [this, id] { set(id, value(id) < .5 ? 1. : 0.); }, value(id) >= .5);
  }
  bool chooseLayout(unsigned index) {
    const auto preset =
        static_cast<s3g::LayoutPannerPreset>(layoutPresetForMenuIndex(index));
    const unsigned channels =
        std::clamp(s3g::layoutPannerPresetSpeakerCount(
                       preset, unsigned(value(kParamHighChannels))),
                   1u, kChannelCount);
    return batch(
        {{kParamLayout, double(unsigned(preset))},
         {kParamHighChannels, double(channels)},
         {kParamSubOffset, double(defaultSubOffsetForHighChannels(channels))}});
  }
  void paint() override {
    titleBand(
        layout::outputUtilityTitleBand(family.canvas), {},
        format("%s / %.1f Hz",
               s3g::subCrossoverModeName(static_cast<s3g::SubCrossoverMode>(
                   unsigned(value(kParamMode)))),
               value(kParamCutoff)));
    panel(box(family.fieldPanel), "SPEAKER MAP");
    drawField();
    panel(box(outputPanel.frame), "OUTPUT");
    panel(box(routingPanel.frame), "ROUTING");
    panel(box(crossoverPanel.frame), "CROSSOVER");
    sliderRow("SUB", kParamSubGain, outputPanel, 0,
              format("%+.1f DB", value(kParamSubGain)));
    sliderRow("HIGH", kParamHighGain, outputPanel, 1,
              format("%+.1f DB", value(kParamHighGain)));
    toggleRow("BYP", kParamBypass, 2);
    toggleRow("FOLD", kParamFoldBypass, 3);
    for (unsigned row = 0; row < 2; ++row) {
      const auto b = box(layout::menuBoxRect(routingPanel, row));
      const bool isLayout = row == 0;
      const clap_id id = isLayout ? kParamLayout : kParamMode;
      at(isLayout ? "LAYOUT" : "MODE",
         layout::processorLabelX(routingPanel.frame.x),
         layout::rowY(routingPanel, row) - 2., style.label);
      foundation::drawMenuBox(*context, b, upper(display(id)), font, style);
      hit(b, [this, b, isLayout] {
        std::vector<std::string> items;
        if (isLayout)
          for (unsigned i = 0; i < kLayoutCount; ++i)
            items.emplace_back(upper(s3g::layoutPannerPresetName(
                static_cast<s3g::LayoutPannerPreset>(
                    layoutPresetForMenuIndex(i)))));
        else
          for (unsigned i = 0; i < 2; ++i)
            items.emplace_back(upper(s3g::subCrossoverModeName(
                static_cast<s3g::SubCrossoverMode>(i))));
        openPopup(
            b, items,
            isLayout
                ? int(menuIndexForLayoutPreset(unsigned(value(kParamLayout))))
                : int(value(kParamMode)),
            [this, isLayout](int i) {
              if (isLayout)
                chooseLayout(i);
              else
                set(kParamMode, i);
            },
            1,
            box(b.origin.x, isLayout ? 44. : maxY(b) + 3., b.size.width,
                isLayout ? 480. : 32.),
            16.);
      });
    }
    // HIGH CH automatically relocates SUB OFFSET, just like Cocoa.
    sliderRow("HIGH CH", kParamHighChannels, routingPanel, 2);
    sliderRow("SUBS", kParamSubCount, routingPanel, 3);
    sliderRow("OFFSET", kParamSubOffset, routingPanel, 4);
    sliderRow("CUTOFF", kParamCutoff, crossoverPanel, 0,
              format("%.1f HZ", value(kParamCutoff)));
    sliderRow("FOCUS", kParamSubFocus, crossoverPanel, 1,
              format("%.2f", value(kParamSubFocus)));
  }
  s3g::LayoutPanner probe;
  std::array<CPoint, kChannelCount> speakerPoints{};
  std::vector<std::string> groupedLabels;
};
} // namespace crossover_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new crossover_canvas::Editor(p);
}
