namespace ambi_order_canvas {
using namespace s3g::portable_gui::routing;
namespace foundation = s3g::portable_gui::foundation;
namespace layout = s3g::gui_layout;
constexpr const auto &family = layout::kTransformFamilyLayout;
constexpr const char *pluginName = "Ambi Transform Order Band";
constexpr const char *pluginTitle = "s3g AMBI TRANSFORM ORDER BAND 64CH";
constexpr clap_id preservedOutput = kParamOutput;
constexpr const auto &stateExtension = stateExt;
#include "s3g_routing_parameter_editor.inc"
class Editor final : public ParameterEditor {
public:
  explicit Editor(Plugin &p) : ParameterEditor(p) {}
  double weight(unsigned order) const {
    const auto activeOrder = unsigned(value(kParamOrder));
    const auto weighting = static_cast<s3g::AmbiUtilityWeighting>(
        unsigned(value(kParamWeighting)));
    const float standard = s3g::ambiUtilityStandardOrderWeight(weighting, order,
                                                               activeOrder),
                custom = float(value(kParamOrder0 + order));
    return order <= activeOrder
               ? s3g::lerp(custom, standard * custom, float(value(kParamBlend)))
               : 0.;
  }
  void paint() override {
    titleBand(layout::transformTitleBand(family.canvas));
    panel(box(family.fieldPanel), "ORDER WEIGHT MAP");
    const auto r = box(family.fieldPlot);
    fill(r, calibratedGray(0x10));
    const double slotW = r.size.width / 8.;
    for (unsigned order = 0; order < 8; ++order) {
      const bool enabled = order <= unsigned(value(kParamOrder));
      const double x = r.origin.x + slotW * order + 12., w = slotW - 24.,
                   active = weight(order),
                   h = (r.size.height - 66.) * std::clamp(active / 2., 0., 1.);
      const auto slot = box(x, r.origin.y + 28., w, r.size.height - 66.);
      fill(slot, calibratedGray(enabled ? 0x1a : 0x0b));
      fill(rect(x + 2., maxY(r) - 38. - h, w - 4., h),
           calibratedGray(0xd0, enabled ? .70 : .16));
      framed(slot, calibratedGray(0xd0, enabled ? .70 : .16));
      at(format("%u", order), x + w * .5 - 3., maxY(r) - 28., style.value);
      at(format("%.0f", active * 100.), x + 1., r.origin.y + 12., style.value);
    }
    at("ORDER 0..7 / PER-ORDER GAIN AFTER WEIGHTING", r.origin.x, maxY(r) + 12.,
       style.value);
    panel(box(family.output.frame), "OUTPUT");
    panel(box(family.weighting.frame), "WEIGHTING");
    panel(box(family.orderBands.frame), "ORDER BANDS");
    sliderRow("OUT", kParamOutput, family.output, 0);
    menuRow("ORDER", kParamOrder, family.output, 1,
            {"1OA", "2OA", "3OA", "4OA", "5OA", "6OA", "7OA"}, 1);
    menuRow("WEIGHTING", kParamWeighting, family.weighting, 0,
            {"FLAT", "MAXRE", "IN-PHASE", "CUSTOM"});
    sliderRow("AMOUNT", kParamBlend, family.weighting, 1,
              format("%.0f%%", value(kParamBlend) * 100.));
    for (unsigned order = 0; order < 8; ++order)
      sliderRow(format("ORDER %u", order), kParamOrder0 + order,
                family.orderBands, order,
                format("%.0f%%", value(kParamOrder0 + order) * 100.));
  }
};
} // namespace ambi_order_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new ambi_order_canvas::Editor(p);
}
