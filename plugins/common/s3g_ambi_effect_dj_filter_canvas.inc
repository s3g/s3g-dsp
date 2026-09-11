// Literal adaptation of the retained Cocoa geometry, drawing and hit handlers.
namespace ambi_effect_canvas {
namespace drawing = s3g::portable_gui::ambi_effect_drawing;
using namespace VSTGUI;
namespace foundation = s3g::portable_gui::foundation;
using Kind = s3g::clap_gui::ParamEventKind;

constexpr const auto &kLayout = s3g::gui_layout::kTransformFamilyLayout;
constexpr s3g::gui_layout::Canvas kAmbiEffectCanvas{820.0, 640.0};
constexpr s3g::gui_layout::Rect kAmbiEffectFieldPanel{18.0, 42.0, 506.0, 580.0};
constexpr s3g::gui_layout::Rect kAmbiEffectFieldPlot{34.0, 78.0, 474.0, 526.0};
constexpr s3g::gui_layout::Panel kFilterPanel{
    s3g::gui_layout::PluginClass::CompactUtility,
    s3g::gui_layout::PanelRole::Engine,
    {536.0, 134.0, 266.0, 184.0},
    36.0,
    26.0,
    6u,
};
constexpr s3g::gui_layout::Panel kTopologyPanel{
    s3g::gui_layout::PluginClass::CompactUtility,
    s3g::gui_layout::PanelRole::Topology,
    {536.0, 330.0, 266.0, 106.0},
    36.0,
    26.0,
    3u,
};
constexpr s3g::gui_layout::Panel kMaskPanel{
    s3g::gui_layout::PluginClass::CompactUtility,
    s3g::gui_layout::PanelRole::Utility,
    {536.0, 448.0, 266.0, 184.0},
    36.0,
    26.0,
    6u,
};
constexpr double kBodyRadius = 150.0;

struct ProjectedBody {
  std::array<drawing::Point, s3g::kAmbiEffectDjFilterMaxPickups> points{};
  std::array<float, s3g::kAmbiEffectDjFilterMaxPickups> depths{};
};

drawing::Point projectBodyDirection(s3g::Vec3 direction, float azimuthDeg,
                                    float elevationDeg) {
  const drawing::Rect field = drawing::cocoaRect(kAmbiEffectFieldPlot);
  const auto projected =
      s3g::projectAedDirection(direction, azimuthDeg, elevationDeg);
  return drawing::makePoint(
      drawing::midX(field) + projected.horizontal * kBodyRadius,
      field.origin.y + 190.0 - projected.vertical * kBodyRadius);
}

ProjectedBody projectBody(s3g::AmbiEffectBody body, float azimuthDeg,
                          float elevationDeg) {
  ProjectedBody result{};
  const auto directions = bodyDirections(body);
  const uint32_t count = s3g::ambiEffectBodyPickupCount(body);
  for (uint32_t node = 0u; node < count; ++node) {
    result.points[node] =
        projectBodyDirection(directions[node], azimuthDeg, elevationDeg);
    result.depths[node] =
        s3g::projectAedDirection(directions[node], azimuthDeg, elevationDeg)
            .depth;
  }
  return result;
}

drawing::Rect pickupFilterAxisRect() {
  const drawing::Rect field = drawing::cocoaRect(kAmbiEffectFieldPlot);
  return drawing::makeRect(field.origin.x + 42.0,
                           field.origin.y + field.size.height - 132.0,
                           field.size.width - 84.0, 16.0);
}

drawing::Rect pickupResonanceAxisRect() {
  const drawing::Rect field = drawing::cocoaRect(kAmbiEffectFieldPlot);
  return drawing::makeRect(field.origin.x + 42.0,
                           field.origin.y + field.size.height - 66.0,
                           field.size.width - 84.0, 16.0);
}

constexpr const char *pluginName = "Ambi Effect DJ Filter";
constexpr const char *pluginTitle = "";
constexpr clap_id preservedOutput = kParamOutput;
constexpr const auto &stateExtension = stateExt;
#include "s3g_ambi_effect_editor.inc"
void storeViewState() {

  auto *p = static_cast<Plugin *>(_plugin);
  if (!p)
    return;
  p->guiViewMode = _viewMode;
  p->guiViewAzDeg = static_cast<float>(_viewAzDeg);
  p->guiViewElDeg = static_cast<float>(_viewElDeg);
}
void setViewPreset(int mode) {

  _viewMode = mode;
  if (mode == 0) {
    _viewAzDeg = 90.0;
    _viewElDeg = 0.0;
  } else if (mode == 1) {
    _viewAzDeg = 90.0;
    _viewElDeg = 90.0;
  } else {
    _viewAzDeg = 35.0;
    _viewElDeg = 34.0;
  }
  storeViewState();
  setNeedsDisplay(true);
}
void drawSlider_value_norm_panel_row_attrs_style(
    drawing::String name, drawing::String value, double norm,
    const s3g::gui_layout::Panel &panel, uint32_t row, drawing::Attrs attrs,
    drawing::Style &style) {

  drawing::drawProcessorSlider(name, value, norm,
                               s3g::gui_layout::rowY(panel, row), panel.frame.x,
                               panel.frame.width, attrs, attrs, style);
}
void drawMenu_value_panel_row_attrs_style(drawing::String name,
                                          drawing::String value,
                                          const s3g::gui_layout::Panel &panel,
                                          uint32_t row, drawing::Attrs attrs,
                                          drawing::Style &style) {

  drawing::drawProcessorMenu(name, value, s3g::gui_layout::rowY(panel, row),
                             panel.frame.x, panel.frame.width, attrs, attrs,
                             style);
}
void paint() override {

  drawing::DrawingScope drawingScope(*this, context, font, titleFont, &error);
  service();
  auto *p = static_cast<Plugin *>(_plugin);
  drawing::Style style;
  drawing::setFill(style.bg);
  drawing::rectFill(bounds());
  drawing::Attrs text = drawing::softLabelAttrs();
  drawing::Attrs value = drawing::softValueAttrs();
  const float peak = p->outputPeak.exchange(
      p->outputPeak.load(std::memory_order_relaxed) * 0.92f,
      std::memory_order_relaxed);
  const auto resolved = static_cast<s3g::AmbiEffectBody>(
      p->resolvedBody.load(std::memory_order_relaxed));
  drawing::String status = drawing::stringFormat(
      "%uOA · %s · %@", guiParams.order, s3g::ambiEffectBodyName(resolved),
      drawing::peakDbText(peak));
  drawing::drawTransformTitleBand(
      "s3g AMBI EFFECT DJ FILTER 64CH", drawing::stringFromUtf8(p->presetName),
      status, s3g::gui_layout::transformTitleBand(kAmbiEffectCanvas), style);

  drawing::Rect fieldPanel = drawing::cocoaRect(kAmbiEffectFieldPanel);
  drawing::drawPanelFrame(fieldPanel.origin.x, fieldPanel.origin.y,
                          fieldPanel.size.width, fieldPanel.size.height, style);
  drawing::drawPanelHeader("LISTENER PICKUPS / FILTER FIELD", true,
                           fieldPanel.origin.x, fieldPanel.origin.y,
                           fieldPanel.size.width, 21.0, text, style);
  drawing::drawTopologyProcessorCameraButtons(fieldPanel, _viewMode, text,
                                              style);
  drawing::Rect field = drawing::cocoaRect(kAmbiEffectFieldPlot);
  drawing::setFill(drawing::calibratedColor(0x0d0f0f));
  drawing::rectFill(field);
  drawing::setStroke(style.grid);
  drawing::frameRect(field);

  const uint32_t bodyCount = s3g::ambiEffectBodyPickupCount(resolved);
  if (_selectedPickup >= bodyCount)
    _selectedPickup = 0u;
  const auto directions = bodyDirections(resolved);
  const ProjectedBody projected = projectBody(
      resolved, static_cast<float>(_viewAzDeg), static_cast<float>(_viewElDeg));
  const auto &points = projected.points;
  const auto &depths = projected.depths;

  float nearestDot = -2.0f;
  for (uint32_t a = 0u; a < bodyCount; ++a) {
    for (uint32_t b = a + 1u; b < bodyCount; ++b) {
      nearestDot = std::max(nearestDot, directions[a].x * directions[b].x +
                                            directions[a].y * directions[b].y +
                                            directions[a].z * directions[b].z);
    }
  }
  drawing::Path shell = drawing::Path::bezierPath();
  for (uint32_t a = 0u; a < bodyCount; ++a) {
    for (uint32_t b = a + 1u; b < bodyCount; ++b) {
      const float relation = directions[a].x * directions[b].x +
                             directions[a].y * directions[b].y +
                             directions[a].z * directions[b].z;
      if (relation < nearestDot - 0.0001f)
        continue;
      (shell).moveToPoint(points[a]);
      (shell).lineToPoint(points[b]);
    }
  }
  drawing::setStroke(drawing::calibratedColor(0x707674, 0.34));
  (shell).setLineWidth(1.0);
  (shell).stroke();

  drawing::Path heardRoute = drawing::Path::bezierPath();
  if (guiParams.topology == s3g::AmbiEffectTopology::Cross) {
    uint32_t opposite = 0u;
    float minimum = 2.0f;
    for (uint32_t other = 0u; other < bodyCount; ++other) {
      const float relation =
          directions[_selectedPickup].x * directions[other].x +
          directions[_selectedPickup].y * directions[other].y +
          directions[_selectedPickup].z * directions[other].z;
      if (relation < minimum) {
        minimum = relation;
        opposite = other;
      }
    }
    (heardRoute).moveToPoint(points[_selectedPickup]);
    (heardRoute).lineToPoint(points[opposite]);
  } else if (guiParams.topology == s3g::AmbiEffectTopology::Diffuse) {
    for (uint32_t other = 0u; other < bodyCount; ++other) {
      const float relation =
          directions[_selectedPickup].x * directions[other].x +
          directions[_selectedPickup].y * directions[other].y +
          directions[_selectedPickup].z * directions[other].z;
      if (other == _selectedPickup || relation < nearestDot - 0.0001f)
        continue;
      (heardRoute).moveToPoint(points[_selectedPickup]);
      (heardRoute).lineToPoint(points[other]);
    }
  } else {
    const double orbit =
        guiParams.topology == s3g::AmbiEffectTopology::Roaming
            ? 14.0 + 4.0 * std::sin(
                               p->roamingPhase.load(std::memory_order_relaxed) *
                               2.0f * s3g::kPi)
            : 11.0;
    (heardRoute)
        .appendBezierPathWithOvalInRect(drawing::makeRect(
            points[_selectedPickup].x - orbit,
            points[_selectedPickup].y - orbit, orbit * 2.0, orbit * 2.0));
  }
  drawing::setStroke(drawing::calibratedColor(
      0xd0d4d2, 0.24 + guiParams.topologyAmount * 0.58));
  (heardRoute).setLineWidth(1.5);
  (heardRoute).stroke();

  if (guiParams.maskAmount > 0.001f) {
    const s3g::Vec3 direction = s3g::directionFromAed(
        guiParams.maskAzimuthDeg, guiParams.maskElevationDeg);
    const drawing::Point maskPoint =
        projectBodyDirection(direction, static_cast<float>(_viewAzDeg),
                             static_cast<float>(_viewElDeg));
    const double maskRadius = 16.0 + guiParams.maskWidth * 38.0;
    drawing::setStroke(
        drawing::calibratedColor(0xcbd0cd, 0.20 + guiParams.maskAmount * 0.46));
    drawing::Path mask = drawing::Path::bezierPathWithOvalInRect(
        drawing::makeRect(maskPoint.x - maskRadius, maskPoint.y - maskRadius,
                          maskRadius * 2.0, maskRadius * 2.0));
    (mask).setLineWidth(0.8 + guiParams.maskCurve * 1.2);
    double dash[]{4.0, 3.0};
    (mask).setLineDash_count_phase(dash, 2, 0.0);
    (mask).stroke();
    drawing::Path aim = drawing::Path::bezierPath();
    (aim).moveToPoint(drawing::makePoint(maskPoint.x - 7.0, maskPoint.y));
    (aim).lineToPoint(drawing::makePoint(maskPoint.x + 7.0, maskPoint.y));
    (aim).moveToPoint(drawing::makePoint(maskPoint.x, maskPoint.y - 7.0));
    (aim).lineToPoint(drawing::makePoint(maskPoint.x, maskPoint.y + 7.0));
    (aim).stroke();
  }

  for (uint32_t pass = 0u; pass < 2u; ++pass) {
    for (uint32_t node = 0u; node < bodyCount; ++node) {
      if ((depths[node] >= 0.0f) != (pass == 1u))
        continue;
      const float level = p->nodeLevel[node].load(std::memory_order_relaxed);
      const float levelDb = 20.0f * std::log10(std::max(0.000001f, level));
      const float meter = std::clamp((levelDb + 60.0f) / 60.0f, 0.0f, 1.0f);
      const double size = 14.0;
      drawing::Rect marker = drawing::makeRect(
          points[node].x - size * 0.5, points[node].y - size * 0.5, size, size);
      const double halo = 8.0 + std::sqrt(meter) * 15.0;
      drawing::setFill(drawing::calibratedColor(
          pass == 1u ? 0xd8dcda : 0x858c89, 0.025 + meter * 0.22));
      (drawing::Path::bezierPathWithOvalInRect(
           drawing::makeRect(points[node].x - halo, points[node].y - halo,
                             halo * 2.0, halo * 2.0)))
          .fill();
      drawing::setFill(drawing::calibratedColor(
          pass == 1u ? 0xe9ecea : 0xb9bfbc,
          node == _selectedPickup ? 1.0 : (pass == 1u ? 0.94 : 0.76)));
      drawing::rectFill(marker);
      drawing::setFill(
          drawing::calibratedColor(pass == 1u ? 0x4a504d : 0x3d4240, 0.94));
      drawing::rectFill(drawing::inset(marker, 2.0, 2.0));
      drawing::setStroke(drawing::calibratedColor(
          node == _selectedPickup ? 0xffffff
                                  : (pass == 1u ? 0xe9ecea : 0xb9bfbc),
          node == _selectedPickup ? 1.0 : (pass == 1u ? 0.94 : 0.76)));
      drawing::frameRect(marker);
      const float trim = s3g::ambiEffectPickupFilterPosition(
          guiParams.filter, guiParams.pickupFilterTrim[node], guiParams.spread,
          guiParams.deviation, node, bodyCount);
      const double trimX = marker.origin.x + trim * marker.size.width;
      drawing::setFill(drawing::calibratedColor(0x363c39));
      drawing::rectFill(drawing::makeRect(marker.origin.x,
                                          drawing::maxY(marker) + 3.0,
                                          marker.size.width, 2.0));
      drawing::setFill(drawing::calibratedColor(
          0xd9dddb, node == _selectedPickup ? 1.0 : 0.72));
      drawing::rectFill(drawing::makeRect(
          trimX - 1.0, drawing::maxY(marker) + 1.0, 2.0, 6.0));
      const float resTrim = s3g::ambiEffectPickupResonance(
          guiParams.resonance, guiParams.pickupResonanceTrim[node],
          guiParams.spread, guiParams.deviation, node, bodyCount);
      const double resY =
          marker.origin.y + (1.0f - resTrim) * marker.size.height;
      drawing::setFill(drawing::calibratedColor(
          0xd9dddb, node == _selectedPickup ? 1.0 : 0.72));
      drawing::rectFill(
          drawing::makeRect(marker.origin.x - 5.0, resY - 1.0, 6.0, 2.0));
      drawing::drawAtPoint_withAttributes(
          drawing::stringFormat("%u", node + 1u),
          drawing::makePoint(drawing::maxX(marker) + 3.0,
                             marker.origin.y - 2.0),
          text);
    }
  }

  const drawing::Rect filterAxis = pickupFilterAxisRect();
  const drawing::Rect resonanceAxis = pickupResonanceAxisRect();
  const double axisX = filterAxis.origin.x;
  const double axisWidth = filterAxis.size.width;
  const float selectedTrim = guiParams.pickupFilterTrim[_selectedPickup];
  const float selectedResTrim = guiParams.pickupResonanceTrim[_selectedPickup];
  const float selectedLevel =
      p->nodeLevel[_selectedPickup].load(std::memory_order_relaxed);
  const float selectedDb =
      std::max(-120.0f, 20.0f * std::log10(std::max(0.000001f, selectedLevel)));
  const float selectedWet =
      guiParams.maskAmount < 0.005f
          ? 1.0f
          : p->nodeWetMask[_selectedPickup].load(std::memory_order_relaxed);
  const float effectiveFilter = s3g::ambiEffectPickupFilterPosition(
      guiParams.filter, selectedTrim, guiParams.spread, guiParams.deviation,
      _selectedPickup, bodyCount);
  const float effectiveResonance = s3g::ambiEffectPickupResonance(
      guiParams.resonance, selectedResTrim, guiParams.spread,
      guiParams.deviation, _selectedPickup, bodyCount);
  drawing::drawAtPoint_withAttributes(
      drawing::stringFormat("HEARD AT PICKUP %02u  ·  IN %.0f dB  ·  WET "
                            "%.0f%%  ·  F %.0f%%  R %.0f%%",
                            _selectedPickup + 1u, selectedDb,
                            selectedWet * 100.0f, effectiveFilter * 100.0f,
                            effectiveResonance * 100.0f),
      drawing::makePoint(axisX, filterAxis.origin.y - 22.0), value);
  const auto drawPickupAxis = [&](drawing::Rect axis, float trim,
                                  drawing::String left, drawing::String center,
                                  drawing::String right) {
    drawing::setFill(drawing::calibratedColor(0x171a19));
    drawing::rectFill(axis);
    drawing::setStroke(style.grid);
    drawing::frameRect(axis);
    drawing::drawAtPoint_withAttributes(
        left, drawing::makePoint(axis.origin.x, drawing::maxY(axis) + 5.0),
        value);
    drawing::drawAtPoint_withAttributes(
        center,
        drawing::makePoint(drawing::midX(axis) - 3.0,
                           drawing::maxY(axis) + 5.0),
        value);
    drawing::drawAtPoint_withAttributes(
        right,
        drawing::makePoint(drawing::maxX(axis) - 42.0,
                           drawing::maxY(axis) + 5.0),
        value);
    const double centerX = drawing::midX(axis);
    const double cursorX =
        axis.origin.x + axis.size.width * (trim + 1.0f) * 0.5f;
    drawing::setFill(drawing::calibratedColor(0x343937));
    drawing::rectFill(
        drawing::makeRect(centerX - 1.0, axis.origin.y, 2.0, axis.size.height));
    drawing::setFill(drawing::calibratedColor(0xd9dddb));
    drawing::rectFill(drawing::makeRect(cursorX - 2.0, axis.origin.y - 3.0, 4.0,
                                        axis.size.height + 6.0));
  };
  drawPickupAxis(filterAxis, selectedTrim, "LP TRIM", "0", "HP TRIM");
  drawPickupAxis(resonanceAxis, selectedResTrim, "RES -", "0", "RES +");

  drawing::drawPanelFrame(kLayout.output, style);
  drawing::drawPanelHeader("FIELD", true, kLayout.output, text, style);
  drawing::drawPanelFrame(kFilterPanel, style);
  drawing::drawPanelHeader("DJ FILTER / RELATIONSHIPS", true, kFilterPanel,
                           text, style);
  drawing::drawPanelFrame(kTopologyPanel, style);
  drawing::drawPanelHeader("TOPOLOGY", true, kTopologyPanel, text, style);
  drawing::drawPanelFrame(kMaskPanel, style);
  drawing::drawPanelHeader("DIRECTIONAL WET MASK", true, kMaskPanel, text,
                           style);

  drawSlider_value_norm_panel_row_attrs_style(
      "OUT", drawing::stringFormat("%+.1f dB", guiParams.outputGainDb),
      (guiParams.outputGainDb + 60.0) / 72.0, kLayout.output, 0u, value, style);
  drawMenu_value_panel_row_attrs_style(
      "ORDER", drawing::stringFormat("%uOA", guiParams.order), kLayout.output,
      1u, value, style);
  drawing::String bodyText =
      guiParams.body == s3g::AmbiEffectBody::Auto
          ? drawing::stringFormat("AUTO → %s",
                                  s3g::ambiEffectBodyName(resolved))
          : drawing::stringFromUtf8(s3g::ambiEffectBodyName(guiParams.body));
  drawMenu_value_panel_row_attrs_style("BODY", bodyText, kFilterPanel, 0u,
                                       value, style);
  drawing::String filterText;
  if (guiParams.filter < 0.495f) {
    filterText =
        drawing::stringFormat("LP %.0f%%", (0.5f - guiParams.filter) * 200.0f);
  } else if (guiParams.filter > 0.505f) {
    filterText =
        drawing::stringFormat("HP %.0f%%", (guiParams.filter - 0.5f) * 200.0f);
  } else
    filterText = "OPEN";
  drawSlider_value_norm_panel_row_attrs_style("LP→OPEN→HP", filterText,
                                              guiParams.filter, kFilterPanel,
                                              1u, value, style);
  drawSlider_value_norm_panel_row_attrs_style(
      "RES", drawing::stringFormat("%.0f%%", guiParams.resonance * 100.0f),
      guiParams.resonance, kFilterPanel, 2u, value, style);
  drawSlider_value_norm_panel_row_attrs_style(
      "SPRD", drawing::stringFormat("%.0f%%", guiParams.spread * 100.0f),
      guiParams.spread, kFilterPanel, 3u, value, style);
  drawSlider_value_norm_panel_row_attrs_style(
      "DEV", drawing::stringFormat("%.0f%%", guiParams.deviation * 100.0f),
      guiParams.deviation, kFilterPanel, 4u, value, style);
  drawSlider_value_norm_panel_row_attrs_style(
      "MIX", drawing::stringFormat("%.0f%%", guiParams.mix * 100.0f),
      guiParams.mix, kFilterPanel, 5u, value, style);
  drawMenu_value_panel_row_attrs_style(
      "MODE",
      drawing::stringFromUtf8(s3g::ambiEffectTopologyName(guiParams.topology)),
      kTopologyPanel, 0u, value, style);
  drawSlider_value_norm_panel_row_attrs_style(
      "AMT", drawing::stringFormat("%.0f%%", guiParams.topologyAmount * 100.0f),
      guiParams.topologyAmount, kTopologyPanel, 1u, value, style);
  const double roamNorm =
      std::log(guiParams.roamingRateHz / 0.005f) / std::log(2.0f / 0.005f);
  drawSlider_value_norm_panel_row_attrs_style(
      "RATE", drawing::stringFormat("%.3f Hz", guiParams.roamingRateHz),
      roamNorm, kTopologyPanel, 2u, value, style);
  drawSlider_value_norm_panel_row_attrs_style(
      "MASK",
      (guiParams.maskAmount < 0.005f
           ? "OFF"
           : drawing::stringFormat("%.0f%%", guiParams.maskAmount * 100.0f)),
      guiParams.maskAmount, kMaskPanel, 0u, value, style);
  drawSlider_value_norm_panel_row_attrs_style(
      "AZ", drawing::stringFormat("%+.0f deg", guiParams.maskAzimuthDeg),
      s3g::aedAzimuthSliderNorm(guiParams.maskAzimuthDeg), kMaskPanel, 1u,
      value, style);
  drawSlider_value_norm_panel_row_attrs_style(
      "EL", drawing::stringFormat("%+.0f deg", guiParams.maskElevationDeg),
      (guiParams.maskElevationDeg + 90.0f) / 180.0f, kMaskPanel, 2u, value,
      style);
  drawSlider_value_norm_panel_row_attrs_style(
      "WIDTH", drawing::stringFormat("%.0f%%", guiParams.maskWidth * 100.0f),
      guiParams.maskWidth, kMaskPanel, 3u, value, style);
  drawSlider_value_norm_panel_row_attrs_style(
      "CURVE", drawing::stringFormat("%.0f%%", guiParams.maskCurve * 100.0f),
      guiParams.maskCurve, kMaskPanel, 4u, value, style);
  drawSlider_value_norm_panel_row_attrs_style(
      "DRY",
      (guiParams.maskDry < 0.005f
           ? "-100% FX ONLY"
           : drawing::stringFormat("%.0f%%",
                                   (guiParams.maskDry - 1.0f) * 100.0f)),
      guiParams.maskDry, kMaskPanel, 5u, value, style);

  if (_openMenu > 0 && _menuItems > 0u) {
    drawing::String orderItems[] = {"1OA", "2OA", "3OA", "4OA",
                                    "5OA", "6OA", "7OA"};
    drawing::String bodyItems[] = {"AUTO", "ICOSA 12", "DODECA 20",
                                   "SPHERE 24"};
    drawing::String topologyItems[] = {"LOCAL", "CROSS", "DIFFUSE", "ROAMING"};
    drawing::String *items = _openMenu == 1
                                 ? orderItems
                                 : (_openMenu == 2 ? bodyItems : topologyItems);
    const int selected =
        _openMenu == 1
            ? static_cast<int>(guiParams.order) - 1
            : (_openMenu == 2
                   ? (guiParams.body == s3g::AmbiEffectBody::Auto
                          ? 0
                          : (guiParams.body == s3g::AmbiEffectBody::Dodeca20
                                 ? 2
                                 : (guiParams.body ==
                                            s3g::AmbiEffectBody::Sphere24
                                        ? 3
                                        : 1)))
                   : static_cast<int>(guiParams.topology));
    const double width =
        s3g::gui_layout::processorMenuWidth(kLayout.output.frame.width);
    drawing::drawDropdownMenu(drawing::makeRect(_menuOrigin.x, _menuOrigin.y,
                                                width, 18.0 * _menuItems),
                              18.0, items, _menuItems, selected, _hoverMenuItem,
                              value, style);
  }
}
void updateSliderAtPoint(drawing::Point point) {

  uint32_t pickup = 0u;
  if (_dragSlider >= 0 &&
      pickupFilterParamIndex(static_cast<clap_id>(_dragSlider), pickup)) {
    const drawing::Rect axis = pickupFilterAxisRect();
    const double norm =
        std::clamp((point.x - axis.origin.x) / axis.size.width, 0.0, 1.0);
    setParam_value(static_cast<clap_id>(_dragSlider), norm * 2.0 - 1.0);
    return;
  }
  if (_dragSlider >= 0 &&
      pickupResonanceParamIndex(static_cast<clap_id>(_dragSlider), pickup)) {
    const drawing::Rect axis = pickupResonanceAxisRect();
    const double norm =
        std::clamp((point.x - axis.origin.x) / axis.size.width, 0.0, 1.0);
    setParam_value(static_cast<clap_id>(_dragSlider), norm * 2.0 - 1.0);
    return;
  }
  const double norm = std::clamp(
      (point.x - s3g::gui_layout::processorControlX(kLayout.output.frame.x)) /
          s3g::gui_layout::processorTrackWidth(kLayout.output.frame.width),
      0.0, 1.0);
  switch (_dragSlider) {
  case kParamFilter:
    setParam_value(kParamFilter, norm);
    break;
  case kParamResonance:
    setParam_value(kParamResonance, norm);
    break;
  case kParamSpread:
    setParam_value(kParamSpread, norm);
    break;
  case kParamDeviation:
    setParam_value(kParamDeviation, norm);
    break;
  case kParamMaskCurve:
    setParam_value(kParamMaskCurve, norm);
    break;
  case kParamTopologyAmount:
    setParam_value(kParamTopologyAmount, norm);
    break;
  case kParamRoamingRate:
    setParam_value(kParamRoamingRate, 0.005 * std::pow(2.0 / 0.005, norm));
    break;
  case kParamMix:
    setParam_value(kParamMix, norm);
    break;
  case kParamOutput:
    setParam_value(kParamOutput, -60.0 + norm * 72.0);
    break;
  case kParamMaskAmount:
    setParam_value(kParamMaskAmount, norm);
    break;
  case kParamMaskAzimuth:
    setParam_value(kParamMaskAzimuth,
                   s3g::aedAzimuthFromSliderNorm(static_cast<float>(norm)));
    break;
  case kParamMaskElevation:
    setParam_value(kParamMaskElevation, -90.0 + norm * 180.0);
    break;
  case kParamMaskWidth:
    setParam_value(kParamMaskWidth, norm);
    break;
  case kParamMaskDry:
    setParam_value(kParamMaskDry, norm - 1.0);
    break;
  default:
    break;
  }
}
void updateMenuHover(drawing::Point point) {

  if (_openMenu <= 0 || _menuItems == 0u)
    return;
  const double width =
      s3g::gui_layout::processorMenuWidth(kLayout.output.frame.width);
  const int hover = drawing::dropdownHitIndex(
      point,
      drawing::makeRect(_menuOrigin.x, _menuOrigin.y, width, 18.0 * _menuItems),
      18.0, _menuItems);
  if (hover != _hoverMenuItem) {
    _hoverMenuItem = hover;
    setNeedsDisplay(true);
  }
}
void mouseDown(drawing::Event *event) {

  drawing::Point point =
      convertPoint_fromView(event->locationInWindow(), nullptr);
  auto *p = static_cast<Plugin *>(_plugin);
  if (handleTitleClick(point, &p->plugin, "Ambi Effect DJ Filter",
                       s3g::gui_layout::transformTitleBand(kAmbiEffectCanvas),
                       p->presetName, sizeof(p->presetName), kParamOutput)) {
    setNeedsDisplay(true);
    return;
  }
  if (_openMenu > 0) {
    const double width =
        s3g::gui_layout::processorMenuWidth(kLayout.output.frame.width);
    const int hit = drawing::dropdownHitIndex(
        point,
        drawing::makeRect(_menuOrigin.x, _menuOrigin.y, width,
                          18.0 * _menuItems),
        18.0, _menuItems);
    if (hit >= 0) {
      if (_openMenu == 1)
        setParam_value(kParamOrder, hit + 1.0);
      else if (_openMenu == 2)
        setParam_value(kParamBody, (hit == 0 ? 0.0 : hit + 2.0));
      else
        setParam_value(kParamTopology, hit);
    }
    _openMenu = 0;
    _hoverMenuItem = -1;
    setNeedsDisplay(true);
    return;
  }

  const drawing::Rect fieldPanel = drawing::cocoaRect(kAmbiEffectFieldPanel);
  for (uint32_t index = 0u; index < 3u; ++index) {
    if (drawing::contains(point, drawing::topologyProcessorCameraButtonRect(
                                     fieldPanel, index))) {
      setViewPreset(static_cast<int>(index));
      return;
    }
  }

  const auto resolved =
      s3g::resolveAmbiEffectBody(guiParams.body, guiParams.order);
  const uint32_t bodyCount = s3g::ambiEffectBodyPickupCount(resolved);
  const ProjectedBody projected = projectBody(
      resolved, static_cast<float>(_viewAzDeg), static_cast<float>(_viewElDeg));
  for (uint32_t node = 0u; node < bodyCount; ++node) {
    const double dx = point.x - projected.points[node].x;
    const double dy = point.y - projected.points[node].y;
    if (dx * dx + dy * dy > 15.0 * 15.0)
      continue;
    _selectedPickup = node;
    if (event->clickCount() >= 2) {
      setParam_value(kParamPickupFilterFirst + node, 0.0);
      setParam_value(kParamPickupResonanceFirst + node, 0.0);
    }
    setNeedsDisplay(true);
    return;
  }
  if (drawing::contains(point,
                        drawing::inset(pickupFilterAxisRect(), 0.0, -8.0))) {
    const clap_id pickupParam = kParamPickupFilterFirst + _selectedPickup;
    double defaultValue = 0.0;
    if (drawing::sliderDoubleClickDefault(event, &p->plugin, pickupParam,
                                          &defaultValue)) {
      setParam_value(pickupParam, defaultValue);
      _dragSlider = -1;
    } else {
      _dragSlider = static_cast<int>(pickupParam);
      updateSliderAtPoint(point);
    }
    return;
  }
  if (drawing::contains(point,
                        drawing::inset(pickupResonanceAxisRect(), 0.0, -8.0))) {
    const clap_id pickupParam = kParamPickupResonanceFirst + _selectedPickup;
    double defaultValue = 0.0;
    if (drawing::sliderDoubleClickDefault(event, &p->plugin, pickupParam,
                                          &defaultValue)) {
      setParam_value(pickupParam, defaultValue);
      _dragSlider = -1;
    } else {
      _dragSlider = static_cast<int>(pickupParam);
      updateSliderAtPoint(point);
    }
    return;
  }

  struct MenuHit {
    s3g::gui_layout::Rect rect;
    int menu;
    uint32_t items;
  };
  const MenuHit menus[]{
      {s3g::gui_layout::sliderHitRect(kLayout.output, 1u), 1, 7u},
      {s3g::gui_layout::sliderHitRect(kFilterPanel, 0u), 2, 4u},
      {s3g::gui_layout::sliderHitRect(kTopologyPanel, 0u), 3, 4u},
  };
  for (const auto &menu : menus) {
    if (!drawing::contains(point, drawing::cocoaRect(menu.rect)))
      continue;
    _openMenu = menu.menu;
    _menuItems = menu.items;
    _menuOrigin = drawing::makePoint(
        s3g::gui_layout::processorControlX(kLayout.output.frame.x),
        menu.rect.y + 24.0);
    _hoverMenuItem = -1;
    setNeedsDisplay(true);
    return;
  }

  struct SliderHit {
    s3g::gui_layout::Rect rect;
    clap_id param;
  };
  const SliderHit sliders[]{
      {s3g::gui_layout::sliderHitRect(kLayout.output, 0u), kParamOutput},
      {s3g::gui_layout::sliderHitRect(kFilterPanel, 1u), kParamFilter},
      {s3g::gui_layout::sliderHitRect(kFilterPanel, 2u), kParamResonance},
      {s3g::gui_layout::sliderHitRect(kFilterPanel, 3u), kParamSpread},
      {s3g::gui_layout::sliderHitRect(kFilterPanel, 4u), kParamDeviation},
      {s3g::gui_layout::sliderHitRect(kFilterPanel, 5u), kParamMix},
      {s3g::gui_layout::sliderHitRect(kTopologyPanel, 1u),
       kParamTopologyAmount},
      {s3g::gui_layout::sliderHitRect(kTopologyPanel, 2u), kParamRoamingRate},
      {s3g::gui_layout::sliderHitRect(kMaskPanel, 0u), kParamMaskAmount},
      {s3g::gui_layout::sliderHitRect(kMaskPanel, 1u), kParamMaskAzimuth},
      {s3g::gui_layout::sliderHitRect(kMaskPanel, 2u), kParamMaskElevation},
      {s3g::gui_layout::sliderHitRect(kMaskPanel, 3u), kParamMaskWidth},
      {s3g::gui_layout::sliderHitRect(kMaskPanel, 4u), kParamMaskCurve},
      {s3g::gui_layout::sliderHitRect(kMaskPanel, 5u), kParamMaskDry},
  };
  for (const auto &slider : sliders) {
    if (!drawing::contains(point, drawing::cocoaRect(slider.rect)))
      continue;
    double defaultValue = 0.0;
    if (drawing::sliderDoubleClickDefault(event, &p->plugin, slider.param,
                                          &defaultValue)) {
      setParam_value(slider.param, defaultValue);
      _dragSlider = -1;
    } else {
      _dragSlider = static_cast<int>(slider.param);
      updateSliderAtPoint(point);
    }
    return;
  }
  if (drawing::contains(point, drawing::cocoaRect(kAmbiEffectFieldPlot))) {
    _dragView = true;
    _lastDragPoint = point;
  }
}
void mouseDragged(drawing::Event *event) {

  drawing::Point point =
      convertPoint_fromView(event->locationInWindow(), nullptr);
  updateMenuHover(point);
  if (_dragView) {
    _viewAzDeg += (point.x - _lastDragPoint.x) * 0.35;
    _viewElDeg = std::clamp(_viewElDeg + (point.y - _lastDragPoint.y) * 0.35,
                            -85.0, 85.0);
    _viewMode = -1;
    _lastDragPoint = point;
    storeViewState();
    setNeedsDisplay(true);
    return;
  }
  if (_dragSlider >= 0)
    updateSliderAtPoint(point);
}
void mouseMoved(drawing::Event *event) {

  updateMenuHover(convertPoint_fromView(event->locationInWindow(), nullptr));
}
void mouseUp(drawing::Event *event) {

  (void)event;
  _dragSlider = -1;
  _dragView = false;
}

void *_plugin;
int _dragSlider;
int _openMenu;
int _hoverMenuItem;
drawing::Point _menuOrigin;
uint32_t _menuItems;
uint32_t _selectedPickup;
int _viewMode;
double _viewAzDeg;
double _viewElDeg;
bool _dragView;
drawing::Point _lastDragPoint;

}; // namespace ambi_effect_canvas
}
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new ambi_effect_canvas::Editor(p);
}
#include "s3g_clap_canvas_gui.inc"
