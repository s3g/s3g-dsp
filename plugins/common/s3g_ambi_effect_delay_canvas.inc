// Literal adaptation of the retained Cocoa geometry, drawing and hit handlers.
namespace ambi_effect_canvas {
namespace drawing = s3g::portable_gui::ambi_effect_drawing;
using namespace VSTGUI;
namespace foundation = s3g::portable_gui::foundation;
using Kind = s3g::clap_gui::ParamEventKind;

constexpr const auto &kLayout = s3g::gui_layout::kTransformFamilyLayout;
constexpr s3g::gui_layout::Canvas kCanvas{820.0, 640.0};
constexpr s3g::gui_layout::Rect kFieldPanel{18.0, 42.0, 506.0, 580.0};
constexpr s3g::gui_layout::Rect kFieldPlot{34.0, 78.0, 474.0, 526.0};
constexpr s3g::gui_layout::Panel kDelayPanel{
    s3g::gui_layout::PluginClass::CompactUtility,
    s3g::gui_layout::PanelRole::Engine,
    {536.0, 134.0, 266.0, 198.0},
    34.0,
    24.0,
    7u,
};
constexpr s3g::gui_layout::Panel kTopologyPanel{
    s3g::gui_layout::PluginClass::CompactUtility,
    s3g::gui_layout::PanelRole::Topology,
    {536.0, 344.0, 266.0, 102.0},
    34.0,
    24.0,
    3u,
};
constexpr s3g::gui_layout::Panel kMaskPanel{
    s3g::gui_layout::PluginClass::CompactUtility,
    s3g::gui_layout::PanelRole::Utility,
    {536.0, 458.0, 266.0, 174.0},
    34.0,
    24.0,
    6u,
};
constexpr double kBodyRadius = 150.0;

std::array<s3g::Vec3, s3g::kAmbiEffectDelayMaxPickups>
bodyDirections(s3g::AmbiEffectBody body) {
  return s3g::ambiEffectBodyDirections(body);
}

struct ProjectedBody {
  std::array<drawing::Point, s3g::kAmbiEffectDelayMaxPickups> points{};
  std::array<float, s3g::kAmbiEffectDelayMaxPickups> depths{};
};

drawing::Point projectDirection(s3g::Vec3 direction, float azDeg, float elDeg) {
  const drawing::Rect field = drawing::cocoaRect(kFieldPlot);
  const auto projected = s3g::projectAedDirection(direction, azDeg, elDeg);
  return drawing::makePoint(
      drawing::midX(field) + projected.horizontal * kBodyRadius,
      field.origin.y + 190.0 - projected.vertical * kBodyRadius);
}

ProjectedBody projectBody(s3g::AmbiEffectBody body, float azDeg, float elDeg) {
  ProjectedBody result{};
  const auto directions = bodyDirections(body);
  const uint32_t count = s3g::ambiEffectBodyPickupCount(body);
  for (uint32_t node = 0u; node < count; ++node) {
    result.points[node] = projectDirection(directions[node], azDeg, elDeg);
    result.depths[node] =
        s3g::projectAedDirection(directions[node], azDeg, elDeg).depth;
  }
  return result;
}

drawing::Rect timeAxisRect() {
  const drawing::Rect field = drawing::cocoaRect(kFieldPlot);
  return drawing::makeRect(field.origin.x + 42.0,
                           field.origin.y + field.size.height - 132.0,
                           field.size.width - 84.0, 16.0);
}
drawing::Rect feedbackAxisRect() {
  const drawing::Rect field = drawing::cocoaRect(kFieldPlot);
  return drawing::makeRect(field.origin.x + 42.0,
                           field.origin.y + field.size.height - 66.0,
                           field.size.width - 84.0, 16.0);
}

constexpr const char *pluginName = "Ambi Effect Delay";
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
      "s3g AMBI EFFECT DELAY 64CH", drawing::stringFromUtf8(p->presetName),
      status, s3g::gui_layout::transformTitleBand(kCanvas), style);

  const drawing::Rect fieldPanel = drawing::cocoaRect(kFieldPanel);
  drawing::drawPanelFrame(fieldPanel.origin.x, fieldPanel.origin.y,
                          fieldPanel.size.width, fieldPanel.size.height, style);
  drawing::drawPanelHeader("LISTENER PICKUPS / DELAY FIELD", true,
                           fieldPanel.origin.x, fieldPanel.origin.y,
                           fieldPanel.size.width, 21.0, text, style);
  drawing::drawTopologyProcessorCameraButtons(fieldPanel, _viewMode, text,
                                              style);
  const drawing::Rect field = drawing::cocoaRect(kFieldPlot);
  drawing::setFill(drawing::calibratedColor(0x0d0f0f));
  drawing::rectFill(field);
  drawing::setStroke(style.grid);
  drawing::frameRect(field);

  const uint32_t bodyCount = s3g::ambiEffectBodyPickupCount(resolved);
  if (_selectedPickup >= bodyCount)
    _selectedPickup = 0u;
  const auto directions = bodyDirections(resolved);
  const auto projected = projectBody(resolved, static_cast<float>(_viewAzDeg),
                                     static_cast<float>(_viewElDeg));
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
      (shell).moveToPoint(projected.points[a]);
      (shell).lineToPoint(projected.points[b]);
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
    (heardRoute).moveToPoint(projected.points[_selectedPickup]);
    (heardRoute).lineToPoint(projected.points[opposite]);
  } else if (guiParams.topology == s3g::AmbiEffectTopology::Diffuse) {
    for (uint32_t other = 0u; other < bodyCount; ++other) {
      const float relation =
          directions[_selectedPickup].x * directions[other].x +
          directions[_selectedPickup].y * directions[other].y +
          directions[_selectedPickup].z * directions[other].z;
      if (other == _selectedPickup || relation < nearestDot - 0.0001f)
        continue;
      (heardRoute).moveToPoint(projected.points[_selectedPickup]);
      (heardRoute).lineToPoint(projected.points[other]);
    }
  } else {
    const double orbit =
        guiParams.topology == s3g::AmbiEffectTopology::Roaming
            ? 14.0 + 4.0 * std::sin(
                               p->roamingPhase.load(std::memory_order_relaxed) *
                               2.0f * s3g::kPi)
            : 11.0;
    (heardRoute)
        .appendBezierPathWithOvalInRect(
            drawing::makeRect(projected.points[_selectedPickup].x - orbit,
                              projected.points[_selectedPickup].y - orbit,
                              orbit * 2.0, orbit * 2.0));
  }
  drawing::setStroke(drawing::calibratedColor(
      0xd0d4d2, 0.24 + guiParams.topologyAmount * 0.58));
  (heardRoute).setLineWidth(1.5);
  (heardRoute).stroke();

  if (guiParams.maskAmount > 0.001f) {
    const s3g::Vec3 direction = s3g::directionFromAed(
        guiParams.maskAzimuthDeg, guiParams.maskElevationDeg);
    const drawing::Point point =
        projectDirection(direction, static_cast<float>(_viewAzDeg),
                         static_cast<float>(_viewElDeg));
    const double radius = 16.0 + guiParams.maskWidth * 38.0;
    drawing::setStroke(
        drawing::calibratedColor(0xcbd0cd, 0.20 + guiParams.maskAmount * 0.46));
    drawing::Path mask =
        drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
            point.x - radius, point.y - radius, radius * 2.0, radius * 2.0));
    (mask).setLineWidth(0.8 + guiParams.maskCurve * 1.2);
    double dash[]{4.0, 3.0};
    (mask).setLineDash_count_phase(dash, 2, 0.0);
    (mask).stroke();
  }

  for (uint32_t pass = 0u; pass < 2u; ++pass) {
    for (uint32_t node = 0u; node < bodyCount; ++node) {
      if ((projected.depths[node] >= 0.0f) != (pass == 1u))
        continue;
      const float level = p->nodeLevel[node].load(std::memory_order_relaxed);
      const float db = 20.0f * std::log10(std::max(0.000001f, level));
      const float meter = std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
      constexpr double size = 14.0;
      drawing::Rect marker =
          drawing::makeRect(projected.points[node].x - size * 0.5,
                            projected.points[node].y - size * 0.5, size, size);
      const double halo = 8.0 + std::sqrt(meter) * 15.0;
      drawing::setFill(drawing::calibratedColor(pass ? 0xd8dcda : 0x858c89,
                                                0.025 + meter * 0.22));
      (drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
           projected.points[node].x - halo, projected.points[node].y - halo,
           halo * 2.0, halo * 2.0)))
          .fill();
      drawing::setFill(drawing::calibratedColor(
          pass ? 0xe9ecea : 0xb9bfbc,
          node == _selectedPickup ? 1.0 : (pass ? 0.94 : 0.76)));
      drawing::rectFill(marker);
      drawing::setFill(
          drawing::calibratedColor(pass ? 0x4a504d : 0x3d4240, 0.94));
      drawing::rectFill(drawing::inset(marker, 2.0, 2.0));
      drawing::setStroke(drawing::calibratedColor(
          node == _selectedPickup ? 0xffffff : (pass ? 0xe9ecea : 0xb9bfbc),
          node == _selectedPickup ? 1.0 : (pass ? 0.94 : 0.76)));
      drawing::frameRect(marker);
      const float effectiveTime = s3g::ambiEffectPickupDelayMs(
          guiParams.timeMs, guiParams.pickupTimeTrim[node], guiParams.spread,
          guiParams.deviation, node, bodyCount);
      const float timeTrim =
          std::log(effectiveTime / 5.0f) / std::log(2000.0f / 5.0f);
      const double timeX = marker.origin.x + timeTrim * marker.size.width;
      drawing::setFill(drawing::calibratedColor(0xd9dddb, 0.78));
      drawing::rectFill(drawing::makeRect(
          timeX - 1.0, drawing::maxY(marker) + 1.0, 2.0, 6.0));
      const float feedbackTrim =
          s3g::ambiEffectPickupDelayFeedback(
              guiParams.feedback, guiParams.pickupFeedbackTrim[node],
              guiParams.spread, guiParams.deviation, node, bodyCount) /
          0.88f;
      const double feedbackY =
          marker.origin.y + (1.0f - feedbackTrim) * marker.size.height;
      drawing::rectFill(
          drawing::makeRect(marker.origin.x - 5.0, feedbackY - 1.0, 6.0, 2.0));
      drawing::drawAtPoint_withAttributes(
          drawing::stringFormat("%u", node + 1u),
          drawing::makePoint(drawing::maxX(marker) + 3.0,
                             marker.origin.y - 2.0),
          text);
    }
  }

  const drawing::Rect timeAxis = timeAxisRect();
  const drawing::Rect feedbackAxis = feedbackAxisRect();
  const float timeTrim = guiParams.pickupTimeTrim[_selectedPickup];
  const float feedbackTrim = guiParams.pickupFeedbackTrim[_selectedPickup];
  const float effectiveTime = s3g::ambiEffectPickupDelayMs(
      guiParams.timeMs, timeTrim, guiParams.spread, guiParams.deviation,
      _selectedPickup, bodyCount);
  const float effectiveFeedback = s3g::ambiEffectPickupDelayFeedback(
      guiParams.feedback, feedbackTrim, guiParams.spread, guiParams.deviation,
      _selectedPickup, bodyCount);
  const float selectedLevel =
      p->nodeLevel[_selectedPickup].load(std::memory_order_relaxed);
  const float selectedDb =
      std::max(-120.0f, 20.0f * std::log10(std::max(0.000001f, selectedLevel)));
  drawing::drawAtPoint_withAttributes(
      drawing::stringFormat(
          "HEARD AT PICKUP %02u  ·  IN %.0f dB  ·  %.1f ms  ·  FB %.0f%%",
          _selectedPickup + 1u, selectedDb, effectiveTime,
          effectiveFeedback * 100.0f),
      drawing::makePoint(timeAxis.origin.x, timeAxis.origin.y - 22.0), value);
  const auto drawAxis = [&](drawing::Rect axis, float trim,
                            drawing::String left, drawing::String right) {
    drawing::setFill(drawing::calibratedColor(0x171a19));
    drawing::rectFill(axis);
    drawing::setStroke(style.grid);
    drawing::frameRect(axis);
    drawing::drawAtPoint_withAttributes(
        left, drawing::makePoint(axis.origin.x, drawing::maxY(axis) + 5.0),
        value);
    drawing::drawAtPoint_withAttributes(
        "0",
        drawing::makePoint(drawing::midX(axis) - 3.0,
                           drawing::maxY(axis) + 5.0),
        value);
    drawing::drawAtPoint_withAttributes(
        right,
        drawing::makePoint(drawing::maxX(axis) - 46.0,
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
  drawAxis(timeAxis, timeTrim, "TIME -", "TIME +");
  drawAxis(feedbackAxis, feedbackTrim, "FB -", "FB +");

  drawing::drawPanelFrame(kLayout.output, style);
  drawing::drawPanelHeader("FIELD", true, kLayout.output, text, style);
  drawing::drawPanelFrame(kDelayPanel, style);
  drawing::drawPanelHeader("DELAY / OLA / RELATIONSHIPS", true, kDelayPanel,
                           text, style);
  drawing::drawPanelFrame(kTopologyPanel, style);
  drawing::drawPanelHeader("TOPOLOGY", true, kTopologyPanel, text, style);
  drawing::drawPanelFrame(kMaskPanel, style);
  drawing::drawPanelHeader("DIRECTIONAL WET MASK", true, kMaskPanel, text,
                           style);
  drawSlider_value_norm_panel_row_attrs_style(
      "OUT", drawing::stringFormat("%+.1f dB", guiParams.outputGainDb),
      (guiParams.outputGainDb + 60.0f) / 72.0f, kLayout.output, 0u, value,
      style);
  drawMenu_value_panel_row_attrs_style(
      "ORDER", drawing::stringFormat("%uOA", guiParams.order), kLayout.output,
      1u, value, style);
  drawing::String bodyText =
      guiParams.body == s3g::AmbiEffectBody::Auto
          ? drawing::stringFormat("AUTO → %s",
                                  s3g::ambiEffectBodyName(resolved))
          : drawing::stringFromUtf8(s3g::ambiEffectBodyName(guiParams.body));
  drawMenu_value_panel_row_attrs_style("BODY", bodyText, kDelayPanel, 0u, value,
                                       style);
  const double timeNorm =
      std::log(guiParams.timeMs / 5.0f) / std::log(2000.0 / 5.0);
  drawSlider_value_norm_panel_row_attrs_style(
      "TIME", drawing::stringFormat("%.1f ms", guiParams.timeMs), timeNorm,
      kDelayPanel, 1u, value, style);
  drawSlider_value_norm_panel_row_attrs_style(
      "FEEDBACK", drawing::stringFormat("%.0f%%", guiParams.feedback * 100.0f),
      guiParams.feedback / 0.88f, kDelayPanel, 2u, value, style);
  drawSlider_value_norm_panel_row_attrs_style(
      "TONE", drawing::stringFormat("%.0f%%", guiParams.tone * 100.0f),
      guiParams.tone, kDelayPanel, 3u, value, style);
  drawSlider_value_norm_panel_row_attrs_style(
      "SPRD", drawing::stringFormat("%.0f%%", guiParams.spread * 100.0f),
      guiParams.spread, kDelayPanel, 4u, value, style);
  drawSlider_value_norm_panel_row_attrs_style(
      "DEV", drawing::stringFormat("%.0f%%", guiParams.deviation * 100.0f),
      guiParams.deviation, kDelayPanel, 5u, value, style);
  drawSlider_value_norm_panel_row_attrs_style(
      "MIX", drawing::stringFormat("%.0f%%", guiParams.mix * 100.0f),
      guiParams.mix, kDelayPanel, 6u, value, style);
  drawMenu_value_panel_row_attrs_style(
      "MODE",
      drawing::stringFromUtf8(s3g::ambiEffectTopologyName(guiParams.topology)),
      kTopologyPanel, 0u, value, style);
  drawSlider_value_norm_panel_row_attrs_style(
      "AMT", drawing::stringFormat("%.0f%%", guiParams.topologyAmount * 100.0f),
      guiParams.topologyAmount, kTopologyPanel, 1u, value, style);
  const double rateNorm =
      std::log(guiParams.roamingRateHz / 0.005f) / std::log(2.0 / 0.005);
  drawSlider_value_norm_panel_row_attrs_style(
      "RATE", drawing::stringFormat("%.3f Hz", guiParams.roamingRateHz),
      rateNorm, kTopologyPanel, 2u, value, style);
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
      pickupTimeIndex(static_cast<clap_id>(_dragSlider), pickup)) {
    const drawing::Rect axis = timeAxisRect();
    const double norm =
        std::clamp((point.x - axis.origin.x) / axis.size.width, 0.0, 1.0);
    setParam_value(static_cast<clap_id>(_dragSlider), norm * 2.0 - 1.0);
    return;
  }
  if (_dragSlider >= 0 &&
      pickupFeedbackIndex(static_cast<clap_id>(_dragSlider), pickup)) {
    const drawing::Rect axis = feedbackAxisRect();
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
  case kParamTime:
    setParam_value(kParamTime, 5.0 * std::pow(2000.0 / 5.0, norm));
    break;
  case kParamFeedback:
    setParam_value(kParamFeedback, norm * 0.88);
    break;
  case kParamTone:
    setParam_value(kParamTone, norm);
    break;
  case kParamSpread:
    setParam_value(kParamSpread, norm);
    break;
  case kParamDeviation:
    setParam_value(kParamDeviation, norm);
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
  case kParamMaskCurve:
    setParam_value(kParamMaskCurve, norm);
    break;
  case kParamMaskDry:
    setParam_value(kParamMaskDry, norm - 1.0);
    break;
  default:
    break;
  }
}
void mouseDown(drawing::Event *event) {

  drawing::Point point =
      convertPoint_fromView(event->locationInWindow(), nullptr);
  auto *p = static_cast<Plugin *>(_plugin);
  if (handleTitleClick(point, &p->plugin, "Ambi Effect Delay",
                       s3g::gui_layout::transformTitleBand(kCanvas),
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
  const drawing::Rect fieldPanel = drawing::cocoaRect(kFieldPanel);
  for (uint32_t i = 0u; i < 3u; ++i) {
    if (drawing::contains(
            point, drawing::topologyProcessorCameraButtonRect(fieldPanel, i))) {
      setViewPreset(static_cast<int>(i));
      return;
    }
  }
  const auto resolved =
      s3g::resolveAmbiEffectBody(guiParams.body, guiParams.order);
  const uint32_t count = s3g::ambiEffectBodyPickupCount(resolved);
  const auto projected = projectBody(resolved, static_cast<float>(_viewAzDeg),
                                     static_cast<float>(_viewElDeg));
  for (uint32_t node = 0u; node < count; ++node) {
    const double dx = point.x - projected.points[node].x;
    const double dy = point.y - projected.points[node].y;
    if (dx * dx + dy * dy > 15.0 * 15.0)
      continue;
    _selectedPickup = node;
    if (event->clickCount() >= 2) {
      setParam_value(kParamPickupTimeFirst + node, 0.0);
      setParam_value(kParamPickupFeedbackFirst + node, 0.0);
    }
    setNeedsDisplay(true);
    return;
  }
  const auto beginPickup = [&](drawing::Rect axis, clap_id id) {
    if (!drawing::contains(point, drawing::inset(axis, 0.0, -8.0)))
      return false;
    double defaultValue = 0.0;
    if (drawing::sliderDoubleClickDefault(event, &p->plugin, id,
                                          &defaultValue)) {
      setParam_value(id, defaultValue);
      _dragSlider = -1;
    } else {
      _dragSlider = static_cast<int>(id);
      updateSliderAtPoint(point);
    }
    return true;
  };
  if (beginPickup(timeAxisRect(), kParamPickupTimeFirst + _selectedPickup))
    return;
  if (beginPickup(feedbackAxisRect(),
                  kParamPickupFeedbackFirst + _selectedPickup))
    return;

  struct MenuHit {
    s3g::gui_layout::Rect rect;
    int menu;
    uint32_t items;
  };
  const MenuHit menus[]{
      {s3g::gui_layout::sliderHitRect(kLayout.output, 1u), 1, 7u},
      {s3g::gui_layout::sliderHitRect(kDelayPanel, 0u), 2, 4u},
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
      {s3g::gui_layout::sliderHitRect(kDelayPanel, 1u), kParamTime},
      {s3g::gui_layout::sliderHitRect(kDelayPanel, 2u), kParamFeedback},
      {s3g::gui_layout::sliderHitRect(kDelayPanel, 3u), kParamTone},
      {s3g::gui_layout::sliderHitRect(kDelayPanel, 4u), kParamSpread},
      {s3g::gui_layout::sliderHitRect(kDelayPanel, 5u), kParamDeviation},
      {s3g::gui_layout::sliderHitRect(kDelayPanel, 6u), kParamMix},
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
  if (drawing::contains(point, drawing::cocoaRect(kFieldPlot))) {
    _dragView = true;
    _lastDragPoint = point;
  }
}
void mouseDragged(drawing::Event *event) {

  const drawing::Point point =
      convertPoint_fromView(event->locationInWindow(), nullptr);
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
  if (_openMenu > 0) {
    const double width =
        s3g::gui_layout::processorMenuWidth(kLayout.output.frame.width);
    _hoverMenuItem = drawing::dropdownHitIndex(
        point,
        drawing::makeRect(_menuOrigin.x, _menuOrigin.y, width,
                          18.0 * _menuItems),
        18.0, _menuItems);
    setNeedsDisplay(true);
  }
  if (_dragSlider >= 0)
    updateSliderAtPoint(point);
}
void mouseMoved(drawing::Event *event) {

  if (_openMenu <= 0)
    return;
  const drawing::Point point =
      convertPoint_fromView(event->locationInWindow(), nullptr);
  const double width =
      s3g::gui_layout::processorMenuWidth(kLayout.output.frame.width);
  _hoverMenuItem = drawing::dropdownHitIndex(
      point,
      drawing::makeRect(_menuOrigin.x, _menuOrigin.y, width, 18.0 * _menuItems),
      18.0, _menuItems);
  setNeedsDisplay(true);
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
