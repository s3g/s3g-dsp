// Cocoa field geometry; groupState is a pure DSP helper, not live audio state.
namespace ambi_depth_canvas {
using namespace s3g::portable_gui::routing;
namespace foundation = s3g::portable_gui::foundation;
namespace layout = s3g::gui_layout;
constexpr const auto &family = layout::kTransformFamilyLayout;
constexpr const char *pluginName = kHostName + 4;
constexpr const char *pluginTitle = kHeaderTitle;
constexpr clap_id preservedOutput = kParamOutput;
constexpr const auto &stateExtension = stateExt;
#include "s3g_routing_parameter_editor.inc"
class Editor final : public ParameterEditor {
public:
  explicit Editor(Plugin &p) : ParameterEditor(p) {}
  s3g::AmbiGroupDepthParams parameters() const {
    return {float(value(kParamDepth)),
            kSingleField ? 0.f : float(value(kParamSpread)),
            float(value(kParamFocus)),
            float(value(kParamAir)),
            float(value(kParamTail)),
            float(value(kParamLow)),
            float(value(kParamWidth)),
            float(value(kParamOutput)),
            kSingleField ? float(value(kParamEnvironmentSize)) : .5f,
            kSingleField ? float(value(kParamEnvironmentDecay)) : .5f,
            kSingleField ? float(value(kParamEnvironmentDamping)) : .5f};
  }
  CPoint groupPoint(unsigned group) const {
    const auto params = parameters();
    const auto r = box(family.fieldPlot);
    const auto state = s3g::ambiGroupDepthStateForGroup(params, group, kGroups);
    const double u = kGroups <= 1 ? .5 : (group + .5) / kGroups,
                 y = r.origin.y + 42. + state.depth * (r.size.height - 86.),
                 wobble = std::sin((group + .25) * 1.9) * 18. *
                          std::abs(params.spread);
    return {r.origin.x + 34. + u * (r.size.width - 68.), y + wobble};
  }
  void paint() override {
    const auto params = parameters();
    titleBand(layout::transformTitleBand(family.canvas));
    panel(box(family.fieldPanel),
          kSingleField ? "DEPTH FIELD" : "GROUP DEPTH FIELD");
    const auto r = box(family.fieldPlot);
    fill(r, calibratedGray(0x10));
    for (unsigned i = 1; i < 4; ++i) {
      const double y = r.origin.y + r.size.height * i / 4.;
      line({r.origin.x + 14., y}, {maxX(r) - 14., y},
           calibratedGray(0x57, .34));
    }
    at("NEAR / DIRECT", r.origin.x + 14., r.origin.y + 10., style.value);
    at("NEUTRAL", maxX(r) - 62., r.origin.y + r.size.height * .5 - 25.,
       style.value);
    at("FAR / SOFT", r.origin.x + 14., maxY(r) - 22., style.value);
    std::array<CPoint, kGroups> points;
    for (unsigned g = 0; g < kGroups; ++g)
      points[g] = groupPoint(g);
    const double air = std::abs(params.air);
    if (air > .01) {
      context->saveGlobalState();
      context->setFrameColor(
          calibratedGray(params.air >= 0.f ? 184 : 235, .07 + .18 * air));
      context->setLineWidth(.35 + 1.2 * air);
      for (unsigned band = 0; band < 4; ++band) {
        const double t = band / 3.,
                     y = r.origin.y + 34. + t * (r.size.height - 68.);
        auto path = owned(context->createGraphicsPath());
        if (!path)
          continue;
        path->beginSubpath({r.origin.x + 22., y});
        path->addBezierCurve(
            {r.origin.x + r.size.width * .35, y - 18. * air},
            {r.origin.x + r.size.width * .65, y + 18. * air},
            {maxX(r) - 22., y + std::sin(t * 4.5 + params.air) * 10. * air});
        // VSTGUI dash lengths are multiples of line width on both platforms;
        // Cocoa's source pattern is expressed in absolute canvas points.
        const double lineWidth = .35 + 1.2 * air;
        const CCoord dash[] = {2. / lineWidth,
                               (4. + 5. * (1. - air)) / lineWidth};
        const double phase = band * 1.5;
#if defined(_WIN32)
        const double platformPhase = phase / lineWidth;
#else
        const double platformPhase = phase;
#endif
        context->setLineStyle(CLineStyle(CLineStyle::kLineCapButt,
                                         CLineStyle::kLineJoinMiter,
                                         platformPhase, 2, dash));
        context->drawGraphicsPath(path, CDrawContext::kPathStroked);
      }
      context->restoreGlobalState();
    }
    const double tailDepth =
                     kSingleField
                         ? .52 +
                               .43 * std::pow(std::max(0.f, params.depth), 2.f)
                         : std::max(0.f, params.depth),
                 tail = params.tail * tailDepth;
    if (tail > .01) {
      const double w = (28. + 52. * tail) *
                       (kSingleField ? .72 + params.environmentSize * .56 : 1.),
                   h = (12. + 24. * tail) *
                       (kSingleField ? .74 + params.environmentDecay * .52
                                     : 1.);
      for (auto pt : points)
        ellipse(rect(pt.x - w * .5, pt.y - h * .5, w, h),
                calibratedGray(199, .06 + .20 * tail), false, .5 + 1.1 * tail);
    }
    for (unsigned g = 0; g + 1 < kGroups; ++g)
      line(points[g], points[g + 1], calibratedGray(0x66, .38));
    const auto meter = box(r.origin.x, maxY(r) + 12., r.size.width - 18., 10.);
    fill(meter, style.strip);
    for (unsigned g = 0; g < kGroups; ++g) {
      const auto state = s3g::ambiGroupDepthStateForGroup(params, g, kGroups);
      const double direct = 1.f - state.depth, size = 8. + direct * 8.;
      const auto b =
          box(points[g].x - size * .5, points[g].y - size * .5, size, size);
      fill(b, calibratedGray(unsigned((.32 + direct * .58) * 255.)));
      at(format("G%u", g + 1), points[g].x + size * .62, points[g].y - 7.,
         style.value);
      fill(rect(meter.origin.x + (meter.size.width - 3.) * state.depth,
                meter.origin.y - 3., 3., 16.),
           calibratedGray(unsigned((.88 - state.depth * .52) * 255.)));
    }
    at(kHeaderInfo, r.origin.x, maxY(meter) + 5., style.value);
    panel(box(family.output.frame), "OUTPUT");
    sliderRow("OUT", kParamOutput, family.output, 0);
    sliderRow("WIDTH", kParamWidth, family.output, 1);
    const auto &primary = kSingleField ? family.primaryFour : family.primarySix;
    panel(box(primary.frame), "DEPTH");
    sliderRow("DEPTH", kParamDepth, primary, 0);
    if constexpr (kSingleField) {
      sliderRow("FOCUS", kParamFocus, primary, 1);
      sliderRow("AIR DAMPING", kParamAir, primary, 2);
      sliderRow("LOW BODY", kParamLow, primary, 3);
      panel(box(family.secondaryFour.frame), "ENVIRONMENT");
      const clap_id ids[] = {kParamTail, kParamEnvironmentSize,
                             kParamEnvironmentDecay, kParamEnvironmentDamping};
      const char *names[] = {"TAIL", "SIZE", "DECAY", "DAMPING"};
      for (unsigned i = 0; i < 4; ++i)
        sliderRow(names[i], ids[i], family.secondaryFour, i);
    } else {
      sliderRow("GROUP SPREAD", kParamSpread, primary, 1);
      sliderRow("FOCUS", kParamFocus, primary, 2);
      sliderRow("AIR DAMPING", kParamAir, primary, 3);
      sliderRow("TAIL", kParamTail, primary, 4);
      sliderRow("LOW BODY", kParamLow, primary, 5);
    }
  }
};
} // namespace ambi_depth_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new ambi_depth_canvas::Editor(p);
}
