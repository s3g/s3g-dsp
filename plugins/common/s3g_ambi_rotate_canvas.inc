// Literal projection and field geometry from the retained Cocoa rotation views.
namespace ambi_rotate_canvas {
using namespace s3g::portable_gui::routing;
namespace foundation = s3g::portable_gui::foundation;
namespace layout = s3g::gui_layout;
constexpr const auto &family = layout::kTransformFamilyLayout;
#if S3G_AMBI_ROTATE_GROUP
constexpr const char *pluginName = kHostName + 4;
constexpr const char *pluginTitle = kHeaderTitle;
constexpr bool grouped = true;
#else
constexpr const char *pluginName = "Ambi Transform Rotate";
constexpr const char *pluginTitle = "s3g AMBI TRANSFORM ROTATE 64CH";
constexpr bool grouped = false;
#endif
constexpr clap_id preservedOutput = kParamOutput;
constexpr const auto &stateExtension = stateExt;
double routingReadValue(const Plugin &p, clap_id id) { return getParam(p, id); }
void routingPublish(Plugin &p,
                    const std::vector<std::pair<clap_id, double>> &v) {
  for (auto [id, value] : v)
    applyParam(p, id, value);
}
void routingApply(Plugin &p, clap_id id, double v) { applyParam(p, id, v); }
double routingRawValue(Plugin &p, clap_id id) { return getParam(p, id); }
void routingInitialize(Plugin &p) {
  p.plugin.plugin_data = &p;
  publishParams(p, p.audioParams, true);
  init(&p.plugin);
}
#define S3G_ROUTING_CUSTOM_ACCESS 1
#include "s3g_routing_parameter_editor.inc"
#undef S3G_ROUTING_CUSTOM_ACCESS
class Editor final : public ParameterEditor {
public:
  explicit Editor(Plugin &p) : ParameterEditor(p) { setView(p.guiViewMode); }
  double refreshPeriodMilliseconds() const override { return 1000. / 24.; }
  void setView(int mode) {
    viewMode = std::clamp(mode, -1, 2);
    p.guiViewMode = viewMode;
    viewAz = mode == 0 ? 0. : mode == 1 ? 180. : -45.;
    viewEl = mode == 0 ? 90. : mode == 1 ? 0. : 26.;
  }
  void service() override {
    if (viewMode != p.guiViewMode)
      setView(p.guiViewMode);
  }
  void preparePresetSave(Plugin &candidate) override {
    candidate.guiViewMode = p.guiViewMode;
  }
  void finishPresetLoad(const Plugin &candidate) override {
    setView(candidate.guiViewMode);
  }
  Box viewButton(int i) const {
    const auto r = box(family.fieldPanel);
    return box(maxX(r) - 10. - (3 - i) * 38. - (2 - i) * 5., r.origin.y + 4.,
               38., 13.);
  }
  CPoint project(s3g::Vec3 v, double scale) const {
    const auto r = box(family.fieldPlot);
    const double cx = r.origin.x + r.size.width * .5,
                 cy = r.origin.y + r.size.height * .54;
    if (viewMode == 0)
      return {cx - v.y * scale, cy - v.x * scale};
    if (viewMode == 1)
      return {cx - v.y * scale, cy - v.z * scale};
    const double az = viewAz * s3g::kPi / 180., el = viewEl * s3g::kPi / 180.,
                 x1 = v.x * std::cos(az) - v.y * std::sin(az),
                 y1 = v.x * std::sin(az) + v.y * std::cos(az),
                 x2 = x1 * std::cos(el) + v.z * std::sin(el),
                 z2 = -x1 * std::sin(el) + v.z * std::cos(el);
    return {cx - y1 * scale + x2 * scale * .14,
            cy - z2 * scale - x2 * scale * .10};
  }
  void paint() override {
    service();
    const auto params = snapshotPublishedParams(p);
    titleBand(layout::transformTitleBand(family.canvas));
    panel(box(family.fieldPanel),
          grouped ? "GROUP ROTATION FIELD" : "ROTATION FIELD");
    const char *labels[] = {"TOP", "SIDE", "3/4"};
    for (int i = 0; i < 3; ++i) {
      auto b = viewButton(i);
      fill(b, calibratedGray(viewMode == i ? 0x50 : 0x38));
      text(labels[i], b, style.value, kCenterText);
      hit(b, [this, i] { setView(i); });
    }
    const auto r = box(family.fieldPlot);
    fill(r, calibratedGray(0x10));
    const double scale = std::min(r.size.width, r.size.height) *
                         (grouped ? .34 : .36),
                 cx = r.origin.x + r.size.width * .5,
                 cy = r.origin.y + r.size.height * .54;
    line({r.origin.x + 24., cy}, {maxX(r) - 24., cy},
         calibratedGray(0x77, .22));
    line({cx, r.origin.y + 18.}, {cx, maxY(r) - 18.},
         calibratedGray(0x77, .22));
    const s3g::Vec3 axes[] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const char *axisLabels[] = {"0deg", "+90", "+EL"};
    for (int i = 0; i < 3; ++i) {
      auto a = project(axes[i], scale);
      line({cx, cy}, a, calibratedGray(0xbd, grouped ? .40 : .46));
      at(axisLabels[i], a.x + 6., a.y - 6., style.value);
    }
#if S3G_AMBI_ROTATE_GROUP
    std::array<CPoint, kGroups> points;
    for (unsigned g = 0; g < kGroups; ++g) {
      auto gp = s3g::ambiGroupRotateParamsForGroup(params, g, kGroups);
      const float angle = float(g) / kGroups * 2.f * s3g::kPi;
      auto v = s3g::normalize({std::cos(angle), std::sin(angle), 0.f});
      v = s3g::ambiUtilityRotate(v, gp.yawDeg, gp.pitchDeg, gp.rollDeg);
      points[g] = project(v, scale * .88);
    }
    for (unsigned g = 0; g < kGroups; ++g)
      line(points[g], points[(g + 1) % kGroups], calibratedGray(0x5d, .42));
    for (unsigned g = 0; g < kGroups; ++g) {
      const double energy = std::min(1., .44 + std::abs(params.spread) * .18 +
                                             std::abs(params.tilt) * .12 +
                                             std::abs(params.twist) * .12);
      fill(rect(points[g].x - 5., points[g].y - 5., 10., 10.),
           calibratedGray(184, energy));
      at(format("%u", g + 1), points[g].x + 8., points[g].y - 7., style.value);
      if (g == 0 || kGroups <= 4) {
        auto gp = s3g::ambiGroupRotateParamsForGroup(params, g, kGroups);
        at(format("G%u Y%+.0f P%+.0f R%+.0f", g + 1, gp.yawDeg, gp.pitchDeg,
                  gp.rollDeg),
           34., 382. + (g % 2) * 14., style.value);
      }
    }
    at(kHeaderInfo, r.origin.x, maxY(r) + 12., style.value);
#else
    const s3g::Vec3 vertices[] = {{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1},
                                  {1, 1, -1},  {1, -1, 1},   {-1, -1, 1},
                                  {-1, 1, 1},  {1, 1, 1}};
    const unsigned edges[][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0},
                                 {4, 5}, {5, 6}, {6, 7}, {7, 4},
                                 {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    std::array<CPoint, 8> points;
    for (unsigned i = 0; i < 8; ++i) {
      auto v = s3g::normalize(vertices[i]);
      const float az = std::atan2(v.y, v.x);
      v = s3g::ambiUtilityRotate(
          v, params.yawDeg + std::sin(az) * params.spread * 54.f,
          params.pitchDeg + std::sin(az) * params.tilt * 42.f,
          params.rollDeg + std::cos(az) * params.twist * 90.f);
      points[i] = project(v, scale * .88);
    }
    for (auto &e : edges)
      line(points[e[0]], points[e[1]], calibratedGray(0xd0, .48));
    for (auto pt : points)
      fill(rect(pt.x - 3., pt.y - 3., 6., 6.), style.text);
    at(format("%uOA ACN/SN3D  /  YAW %+.0f  PITCH %+.0f  ROLL %+.0f",
              params.order, params.yawDeg, params.pitchDeg, params.rollDeg),
       r.origin.x, maxY(r) + 12., style.value);
#endif
    panel(box(family.output.frame), "OUTPUT");
    sliderRow("OUT", kParamOutput, family.output, 0);
    const auto &rotation = grouped ? family.primarySix : family.primarySeven;
    panel(box(rotation.frame), "ROTATION");
#if S3G_AMBI_ROTATE_GROUP
    sliderRow("WIDTH", kParamWidth, family.output, 1);
#else
    menuRow("ORDER", kParamOrder, family.output, 1,
            {"1OA", "2OA", "3OA", "4OA", "5OA", "6OA", "7OA"}, 1);
    sliderRow("WIDTH", kParamWidth, rotation, 6);
#endif
    const clap_id ids[] = {kParamYaw,    kParamPitch, kParamRoll,
                           kParamSpread, kParamTilt,  kParamTwist};
    const char *names[] = {"YAW", "PITCH", "ROLL", "SPREAD", "TILT", "TWIST"};
    for (unsigned i = 0; i < 6; ++i)
      sliderRow(names[i], ids[i], rotation, i,
                format(i < 3 ? "%+.0f°" : "%+.0f%%",
                       value(ids[i]) * (i < 3 ? 1. : 100.)));
  }
  bool pointerDown(MouseDownEvent &e) override {
    if (!e.buttonState.isLeft() ||
        !foundation::contains(box(family.fieldPlot), e.mousePosition))
      return false;
    cameraDrag = true;
    lastPoint = e.mousePosition;
    return true;
  }
  void pointerMove(MouseMoveEvent &e) override {
    if (!cameraDrag)
      return;
    viewAz += (e.mousePosition.x - lastPoint.x) * .35;
    viewEl =
        std::clamp(viewEl + (e.mousePosition.y - lastPoint.y) * .35, -85., 85.);
    viewMode = p.guiViewMode = -1;
    lastPoint = e.mousePosition;
    invalid();
  }
  void pointerUp(MouseUpEvent &) override { cameraDrag = false; }
  void stopRefresh() override {
    ParameterEditor::stopRefresh();
    cameraDrag = false;
  }
  int viewMode = 2;
  double viewAz = -45., viewEl = 26.;
  bool cameraDrag = false;
  CPoint lastPoint;
};
} // namespace ambi_rotate_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new ambi_rotate_canvas::Editor(p);
}
