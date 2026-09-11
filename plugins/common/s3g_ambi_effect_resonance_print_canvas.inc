// Literal adaptation of the retained Cocoa geometry, drawing and hit handlers.
namespace ambi_effect_canvas {
namespace drawing = s3g::portable_gui::ambi_effect_drawing;
using namespace VSTGUI;
namespace foundation = s3g::portable_gui::foundation;
using Kind = s3g::clap_gui::ParamEventKind;

constexpr s3g::gui_layout::Canvas kCanvas{920.0, 820.0};
constexpr s3g::gui_layout::Panel kFieldPanel{
    s3g::gui_layout::PluginClass::CompactUtility,
    s3g::gui_layout::PanelRole::Engine,
    {18.0, 42.0, 506.0, 774.0},
    34.0,
    24.0,
    0u,
};
constexpr s3g::gui_layout::Rect kFieldPlot{34.0, 78.0, 474.0, 520.0};
constexpr s3g::gui_layout::Panel kOutputPanel{
    s3g::gui_layout::PluginClass::CompactUtility,
    s3g::gui_layout::PanelRole::Output,
    {536.0, 42.0, 366.0, 78.0},
    34.0,
    24.0,
    2u,
};
constexpr s3g::gui_layout::Panel kCapturePanel{
    s3g::gui_layout::PluginClass::CompactUtility,
    s3g::gui_layout::PanelRole::Engine,
    {536.0, 132.0, 366.0, 150.0},
    34.0,
    24.0,
    5u,
};
constexpr s3g::gui_layout::Panel kResynthesisPanel{
    s3g::gui_layout::PluginClass::CompactUtility,
    s3g::gui_layout::PanelRole::Engine,
    {536.0, 294.0, 366.0, 246.0},
    34.0,
    24.0,
    9u,
};
constexpr s3g::gui_layout::Panel kTopologyPanel{
    s3g::gui_layout::PluginClass::CompactUtility,
    s3g::gui_layout::PanelRole::Topology,
    {536.0, 552.0, 366.0, 102.0},
    34.0,
    24.0,
    3u,
};
constexpr s3g::gui_layout::Panel kMaskPanel{
    s3g::gui_layout::PluginClass::CompactUtility,
    s3g::gui_layout::PanelRole::Utility,
    {536.0, 666.0, 366.0, 150.0},
    34.0,
    24.0,
    5u,
};

struct ProjectedBody {
  std::array<drawing::Point, kPickups> points{};
  std::array<float, kPickups> depths{};
};

drawing::Point projectDirection(s3g::Vec3 direction, float azimuthDeg,
                                float elevationDeg) {
  const auto projected =
      s3g::projectAedDirection(direction, azimuthDeg, -elevationDeg);
  const drawing::Rect plot = drawing::cocoaRect(kFieldPlot);
  return drawing::makePoint(drawing::midX(plot) + projected.horizontal * 176.0f,
                            plot.origin.y + 254.0 -
                                projected.vertical * 176.0f);
}

ProjectedBody projectBody(s3g::AmbiEffectBody body, float azimuthDeg,
                          float elevationDeg) {
  ProjectedBody result{};
  const auto directions = s3g::ambiEffectBodyDirections(body);
  const uint32_t count = s3g::ambiEffectBodyPickupCount(body);
  for (uint32_t node = 0u; node < count; ++node) {
    const auto direction = directions[node];
    result.depths[node] =
        s3g::projectAedDirection(direction, azimuthDeg, -elevationDeg).depth;
    result.points[node] = projectDirection(direction, azimuthDeg, elevationDeg);
  }
  return result;
}

drawing::Rect tuneAxisRect() {
  return drawing::makeRect(kFieldPlot.x + 44.0, 680.0, kFieldPlot.width - 88.0,
                           16.0);
}

drawing::Rect decayAxisRect() {
  return drawing::makeRect(kFieldPlot.x + 44.0, 748.0, kFieldPlot.width - 88.0,
                           16.0);
}

drawing::Rect captureButtonRect() {
  return drawing::makeRect(550.0, 253.0, 100.0, 20.0);
}

drawing::Rect applyButtonRect() {
  return drawing::makeRect(658.0, 253.0, 108.0, 20.0);
}

drawing::Rect clearButtonRect() {
  return drawing::makeRect(774.0, 253.0, 114.0, 20.0);
}

constexpr const char *pluginName = "Ambi Effect Resonance Print";
constexpr const char *pluginTitle = "";
constexpr clap_id preservedOutput = kParamOutput;
constexpr const auto &stateExtension = stateExt;
#include "s3g_ambi_effect_capture_editor.inc"
void storeViewState() {

  auto *instance = static_cast<Plugin *>(_plugin);
  if (!instance)
    return;
  instance->guiViewMode = _viewMode;
  instance->guiViewAzDeg = static_cast<float>(_viewAzDeg);
  instance->guiViewElDeg = static_cast<float>(_viewElDeg);
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
    const drawing::Style &style) {

  drawing::drawProcessorSlider(name, value, std::clamp(norm, 0.0, 1.0),
                               s3g::gui_layout::rowY(panel, row), panel.frame.x,
                               panel.frame.width, attrs, attrs, style);
}
void drawMenu_value_panel_row_attrs_style(drawing::String name,
                                          drawing::String value,
                                          const s3g::gui_layout::Panel &panel,
                                          uint32_t row, drawing::Attrs attrs,
                                          const drawing::Style &style) {

  drawing::drawProcessorMenu(name, value, s3g::gui_layout::rowY(panel, row),
                             panel.frame.x, panel.frame.width, attrs, attrs,
                             style);
}
void drawAxis_value_left_center_right_attrs_style(
    drawing::Rect axis, float value, drawing::String left,
    drawing::String center, drawing::String right, drawing::Attrs attrs,
    const drawing::Style &style) {

  drawing::setFill(style.strip);
  drawing::rectFill(axis);
  drawing::setStroke(style.grid);
  drawing::frameRect(axis);
  drawing::drawAtPoint_withAttributes(
      left, drawing::makePoint(axis.origin.x, axis.origin.y - 19.0), attrs);
  drawing::drawAtPoint_withAttributes(
      center,
      drawing::makePoint(drawing::midX(axis) - 8.0, axis.origin.y - 19.0),
      attrs);
  drawing::drawAtPoint_withAttributes(
      right,
      drawing::makePoint(drawing::maxX(axis) - 42.0, axis.origin.y - 19.0),
      attrs);
  const double position =
      axis.origin.x +
      std::clamp((value + 1.0f) * 0.5f, 0.0f, 1.0f) * axis.size.width;
  drawing::setFill(drawing::calibratedColor(0xbfc7c2));
  drawing::rectFill(drawing::makeRect(position - 2.0, axis.origin.y - 3.0, 4.0,
                                      axis.size.height + 6.0));
}
void paint() override {

  drawing::DrawingScope drawingScope(*this, context, font, titleFont, &error);
  service();
  auto *instance = static_cast<Plugin *>(_plugin);
  const auto params = snapshotEffectParams(*instance);
  drawing::Style style;
  drawing::setFill(style.bg);
  drawing::rectFill(bounds());
  drawing::Attrs label = drawing::softLabelAttrs();
  drawing::Attrs value = drawing::softValueAttrs();
  const float peak = instance->outputPeak.exchange(
      instance->outputPeak.load(std::memory_order_relaxed) * 0.92f,
      std::memory_order_relaxed);
  const auto body = static_cast<s3g::AmbiEffectBody>(
      instance->resolvedBody.load(std::memory_order_relaxed));
  const float safety = instance->safetyGain.load(std::memory_order_relaxed);
  const float governor =
      instance->excitationGovernorGain.load(std::memory_order_relaxed);
  drawing::String governorText =
      governor < 0.999f ? drawing::stringFormat(
                              " · GOV %.1f dB",
                              20.0f * std::log10(std::max(0.000001f, governor)))
                        : "";
  drawing::String safetyText =
      safety < 0.999f ? drawing::stringFormat(
                            " · SAFE %.1f dB",
                            20.0f * std::log10(std::max(0.000001f, safety)))
                      : "";
  drawing::String status = drawing::stringFormat(
      "%uOA · %s · %@%@%@", params.order, s3g::ambiEffectBodyName(body),
      drawing::peakDbText(peak), governorText, safetyText);
  drawing::drawTransformTitleBand(
      "s3g AMBI EFFECT RESONANCE PRINT 64CH",
      drawing::stringFromUtf8(instance->presetName), status,
      s3g::gui_layout::transformTitleBand(kCanvas), style);

  drawing::drawPanelFrame(kFieldPanel, style);
  drawing::drawPanelHeader("DIRECTIONAL RESONANCE PRINT", true, kFieldPanel,
                           label, style);
  const drawing::Rect fieldPanel = drawing::cocoaRect(kFieldPanel.frame);
  drawing::drawTopologyProcessorCameraButtons(fieldPanel, _viewMode, label,
                                              style);
  const drawing::Rect plot = drawing::cocoaRect(kFieldPlot);
  drawing::setFill(drawing::calibratedColor(0x111513));
  drawing::rectFill(plot);
  drawing::setStroke(style.grid);
  drawing::frameRect(plot);

  const bool capturingNow = instance->capturing.load(std::memory_order_relaxed);
  const bool printed = instance->hasPrint.load(std::memory_order_relaxed);
  const bool printApplied = printed && params.printEnabled != 0u;
  const float progress =
      instance->captureProgress.load(std::memory_order_relaxed);
  drawing::String printStatus =
      capturingNow
          ? drawing::stringFormat("LISTENING  %.0f%%", progress * 100.0f)
          : (printed
                 ? (printApplied ? "PRINT APPLIED" : "PRINT READY · BYPASSED")
                 : "NO PRINT — PRESS CAPTURE");
  drawing::drawAtPoint_withAttributes(
      printStatus,
      drawing::makePoint(plot.origin.x + 12.0, plot.origin.y + 10.0), value);
  if (capturingNow) {
    const drawing::Rect progressTrack =
        drawing::makeRect(plot.origin.x + 12.0, plot.origin.y + 32.0,
                          plot.size.width - 24.0, 4.0);
    drawing::setFill(style.grid);
    drawing::rectFill(progressTrack);
    drawing::setFill(drawing::calibratedColor(0x9cae9f));
    drawing::rectFill(drawing::makeRect(
        progressTrack.origin.x, progressTrack.origin.y,
        progressTrack.size.width * progress, progressTrack.size.height));
  }

  const auto projected = projectBody(body, static_cast<float>(_viewAzDeg),
                                     static_cast<float>(_viewElDeg));
  const uint32_t count = s3g::ambiEffectBodyPickupCount(body);
  for (uint32_t first = 0u; first < count; ++first) {
    for (uint32_t second = first + 1u; second < count; ++second) {
      const double dx = projected.points[first].x - projected.points[second].x;
      const double dy = projected.points[first].y - projected.points[second].y;
      if (dx * dx + dy * dy > 155.0 * 155.0)
        continue;
      drawing::setStroke(drawing::calibratedColor(0x29302c));
      drawing::Path edge = drawing::Path::bezierPath();
      (edge).moveToPoint(projected.points[first]);
      (edge).lineToPoint(projected.points[second]);
      (edge).stroke();
    }
  }
  if (params.maskAmount > 0.001f) {
    const s3g::Vec3 direction =
        s3g::directionFromAed(params.maskAzimuthDeg, params.maskElevationDeg);
    const drawing::Point target =
        projectDirection(direction, static_cast<float>(_viewAzDeg),
                         static_cast<float>(_viewElDeg));
    const double radius = 15.0 + params.maskWidth * 34.0;
    drawing::setStroke(
        drawing::calibratedColor(0xcbd0cd, 0.24 + params.maskAmount * 0.54));
    drawing::Path ring =
        drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
            target.x - radius, target.y - radius, radius * 2.0, radius * 2.0));
    double dash[]{4.0, 3.0};
    (ring).setLineDash_count_phase(dash, 2, 0.0);
    (ring).setLineWidth(1.0);
    (ring).stroke();
    drawing::Path cross = drawing::Path::bezierPath();
    (cross).moveToPoint(drawing::makePoint(target.x - 5.0, target.y));
    (cross).lineToPoint(drawing::makePoint(target.x + 5.0, target.y));
    (cross).moveToPoint(drawing::makePoint(target.x, target.y - 5.0));
    (cross).lineToPoint(drawing::makePoint(target.x, target.y + 5.0));
    (cross).stroke();
  }
  std::array<uint32_t, kPickups> depthOrder{};
  for (uint32_t node = 0u; node < count; ++node)
    depthOrder[node] = node;
  std::sort(depthOrder.begin(), depthOrder.begin() + count,
            [&](uint32_t a, uint32_t b) {
              return projected.depths[a] < projected.depths[b];
            });
  for (uint32_t rank = 0u; rank < count; ++rank) {
    const uint32_t node = depthOrder[rank];
    const float strength =
        instance->nodeStrength[node].load(std::memory_order_relaxed);
    const float level =
        instance->nodeLevel[node].load(std::memory_order_relaxed);
    const float wet =
        instance->nodeWetMask[node].load(std::memory_order_relaxed);
    const double radius = 7.0 + strength * 9.0 + std::min(5.0f, level * 30.0f);
    const drawing::Rect dot = drawing::makeRect(
        projected.points[node].x - radius, projected.points[node].y - radius,
        radius * 2.0, radius * 2.0);
    const double brightness =
        std::clamp(0.25 + strength * 0.55 + level * 0.5, 0.25, 0.95);
    drawing::setFill(drawing::calibratedRGB(brightness * wet, brightness,
                                            brightness * 0.82, 1.0));
    (drawing::Path::bezierPathWithOvalInRect(dot)).fill();
    if (node == _selectedPickup) {
      drawing::setStroke(drawing::calibratedColor(0xd5dad7));
      (drawing::Path::bezierPathWithOvalInRect(drawing::inset(dot, -4.0, -4.0)))
          .stroke();
    }
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat("%02u", node + 1u),
        drawing::makePoint(projected.points[node].x - 7.0,
                           projected.points[node].y + radius + 2.0),
        label);
  }

  const float f0 = instance->fundamentalHz.load(std::memory_order_relaxed);
  const float confidence =
      instance->pitchConfidence.load(std::memory_order_relaxed);
  const uint32_t modeCount =
      instance->printedModes.load(std::memory_order_relaxed);
  drawing::drawAtPoint_withAttributes(
      drawing::stringFormat(
          "PICKUP %02u  ·  %u MODES  ·  F0 %.1f Hz  ·  CONF %.0f%%",
          _selectedPickup + 1u, modeCount, f0, confidence * 100.0f),
      drawing::makePoint(plot.origin.x + 12.0, drawing::maxY(plot) + 20.0),
      value);
  drawAxis_value_left_center_right_attrs_style(
      tuneAxisRect(), params.pickupTuneTrim[_selectedPickup], "TUNE -12 ST",
      "0", "+12 ST", label, style);
  drawAxis_value_left_center_right_attrs_style(
      decayAxisRect(), params.pickupDecayTrim[_selectedPickup], "DECAY -", "0",
      "DECAY +", label, style);

  drawing::drawPanelFrame(kOutputPanel, style);
  drawing::drawPanelHeader("FIELD", true, kOutputPanel, label, style);
  drawing::drawPanelFrame(kCapturePanel, style);
  drawing::drawPanelHeader("ANALYZE / CAPTURE", true, kCapturePanel, label,
                           style);
  drawing::drawPanelFrame(kResynthesisPanel, style);
  drawing::drawPanelHeader("MODAL RESYNTHESIS", true, kResynthesisPanel, label,
                           style);
  drawing::drawPanelFrame(kTopologyPanel, style);
  drawing::drawPanelHeader("TOPOLOGY", true, kTopologyPanel, label, style);
  drawing::drawPanelFrame(kMaskPanel, style);
  drawing::drawPanelHeader("DIRECTIONAL WET MASK", true, kMaskPanel, label,
                           style);

  drawSlider_value_norm_panel_row_attrs_style(
      "OUT", drawing::stringFormat("%+.1f dB", params.outputGainDb),
      (params.outputGainDb + 60.0f) / 72.0f, kOutputPanel, 0u, value, style);
  drawMenu_value_panel_row_attrs_style(
      "ORDER", drawing::stringFormat("%uOA", params.order), kOutputPanel, 1u,
      value, style);
  drawing::String bodyText =
      params.body == s3g::AmbiEffectBody::Auto
          ? drawing::stringFormat("AUTO → %s", s3g::ambiEffectBodyName(body))
          : drawing::stringFromUtf8(s3g::ambiEffectBodyName(params.body));
  drawMenu_value_panel_row_attrs_style("BODY", bodyText, kCapturePanel, 0u,
                                       value, style);
  drawSlider_value_norm_panel_row_attrs_style(
      "TIME", drawing::stringFormat("%.2f s", params.captureSeconds),
      (params.captureSeconds - 0.25f) / 2.75f, kCapturePanel, 1u, value, style);
  drawSlider_value_norm_panel_row_attrs_style(
      "SENS", drawing::stringFormat("%.0f%%", params.sensitivity * 100.0f),
      params.sensitivity, kCapturePanel, 2u, value, style);
  drawMenu_value_panel_row_attrs_style(
      "MODES", drawing::stringFormat("%u / PICKUP", params.modalCount),
      kCapturePanel, 3u, value, style);

  const auto drawButton = [&](drawing::Rect rect, drawing::String text,
                              bool active) {
    drawing::setFill(drawing::calibratedColor(active ? 0x46564d : 0x202522));
    drawing::rectFill(rect);
    drawing::setStroke(style.grid);
    drawing::frameRect(rect);
    const drawing::Size size = drawing::sizeWithAttributes(text, value);
    drawing::drawAtPoint_withAttributes(
        text,
        drawing::makePoint(drawing::midX(rect) - size.width * 0.5,
                           drawing::midY(rect) - size.height * 0.5),
        value);
  };
  drawButton(captureButtonRect(), capturingNow ? "CANCEL" : "CAPTURE",
             capturingNow);
  drawButton(applyButtonRect(), printApplied ? "BYPASS PRINT" : "APPLY PRINT",
             printApplied);
  drawButton(clearButtonRect(), "CLEAR PRINT", false);

  drawSlider_value_norm_panel_row_attrs_style(
      "TRANS", drawing::stringFormat("%+.1f st", params.transposeSemitones),
      (params.transposeSemitones + 24.0f) / 48.0f, kResynthesisPanel, 0u, value,
      style);
  drawSlider_value_norm_panel_row_attrs_style(
      "PULL", drawing::stringFormat("%.0f%%", params.harmonicPull * 100.0f),
      params.harmonicPull, kResynthesisPanel, 1u, value, style);
  drawSlider_value_norm_panel_row_attrs_style(
      "STRETCH",
      drawing::stringFormat("%+.0f%%", params.harmonicStretch * 100.0f),
      (params.harmonicStretch + 1.0f) * 0.5f, kResynthesisPanel, 2u, value,
      style);
  const double decayNorm =
      std::log(params.decaySeconds / 0.08f) / std::log(8.0 / 0.08);
  drawSlider_value_norm_panel_row_attrs_style(
      "DECAY", drawing::stringFormat("%.2f s", params.decaySeconds), decayNorm,
      kResynthesisPanel, 3u, value, style);
  drawSlider_value_norm_panel_row_attrs_style(
      "TILT", drawing::stringFormat("%+.0f%%", params.decayTilt * 100.0f),
      (params.decayTilt + 1.0f) * 0.5f, kResynthesisPanel, 4u, value, style);
  drawSlider_value_norm_panel_row_attrs_style(
      "DRIVE", drawing::stringFormat("%.0f%%", params.drive * 100.0f),
      params.drive, kResynthesisPanel, 5u, value, style);
  drawSlider_value_norm_panel_row_attrs_style(
      "SPREAD", drawing::stringFormat("%.0f%%", params.spread * 100.0f),
      params.spread, kResynthesisPanel, 6u, value, style);
  drawSlider_value_norm_panel_row_attrs_style(
      "DEV", drawing::stringFormat("%.0f%%", params.deviation * 100.0f),
      params.deviation, kResynthesisPanel, 7u, value, style);
  drawSlider_value_norm_panel_row_attrs_style(
      "MIX", drawing::stringFormat("%.0f%%", params.mix * 100.0f), params.mix,
      kResynthesisPanel, 8u, value, style);

  drawMenu_value_panel_row_attrs_style(
      "MODE",
      drawing::stringFromUtf8(s3g::ambiEffectTopologyName(params.topology)),
      kTopologyPanel, 0u, value, style);
  drawSlider_value_norm_panel_row_attrs_style(
      "AMT", drawing::stringFormat("%.0f%%", params.topologyAmount * 100.0f),
      params.topologyAmount, kTopologyPanel, 1u, value, style);
  const double rateNorm =
      std::log(params.roamingRateHz / 0.005f) / std::log(2.0 / 0.005);
  drawSlider_value_norm_panel_row_attrs_style(
      "RATE", drawing::stringFormat("%.3f Hz", params.roamingRateHz), rateNorm,
      kTopologyPanel, 2u, value, style);

  drawSlider_value_norm_panel_row_attrs_style(
      "MASK", drawing::stringFormat("%.0f%%", params.maskAmount * 100.0f),
      params.maskAmount, kMaskPanel, 0u, value, style);
  drawSlider_value_norm_panel_row_attrs_style(
      "AZ", drawing::stringFormat("%+.0f deg", params.maskAzimuthDeg),
      s3g::aedAzimuthSliderNorm(params.maskAzimuthDeg), kMaskPanel, 1u, value,
      style);
  drawSlider_value_norm_panel_row_attrs_style(
      "EL", drawing::stringFormat("%+.0f deg", params.maskElevationDeg),
      (params.maskElevationDeg + 90.0f) / 180.0f, kMaskPanel, 2u, value, style);
  drawSlider_value_norm_panel_row_attrs_style(
      "WIDTH", drawing::stringFormat("%.0f%%", params.maskWidth * 100.0f),
      params.maskWidth, kMaskPanel, 3u, value, style);
  drawSlider_value_norm_panel_row_attrs_style(
      "DRY",
      params.maskDry < 0.005f
          ? "FX ONLY"
          : drawing::stringFormat("%.0f%%", (params.maskDry - 1.0f) * 100.0f),
      params.maskDry, kMaskPanel, 4u, value, style);

  if (_openMenu > 0 && _menuItems > 0u) {
    drawing::String orderItems[] = {"1OA", "2OA", "3OA", "4OA",
                                    "5OA", "6OA", "7OA"};
    drawing::String bodyItems[] = {"AUTO", "ICOSA 12", "DODECA 20",
                                   "SPHERE 24"};
    drawing::String modeItems[] = {"2", "3", "4",  "5",  "6", "7",
                                   "8", "9", "10", "11", "12"};
    drawing::String topologyItems[] = {"LOCAL", "CROSS", "DIFFUSE", "ROAMING"};
    drawing::String *items =
        _openMenu == 1
            ? orderItems
            : (_openMenu == 2 ? bodyItems
                              : (_openMenu == 3 ? modeItems : topologyItems));
    int selected = 0;
    if (_openMenu == 1)
      selected = static_cast<int>(params.order) - 1;
    else if (_openMenu == 2)
      selected =
          params.body == s3g::AmbiEffectBody::Auto
              ? 0
              : (params.body == s3g::AmbiEffectBody::Dodeca20
                     ? 2
                     : (params.body == s3g::AmbiEffectBody::Sphere24 ? 3 : 1));
    else if (_openMenu == 3)
      selected = static_cast<int>(params.modalCount) - 2;
    else
      selected = static_cast<int>(params.topology);
    const double width =
        s3g::gui_layout::processorMenuWidth(kOutputPanel.frame.width);
    drawing::drawDropdownMenu(drawing::makeRect(_menuOrigin.x, _menuOrigin.y,
                                                width, 18.0 * _menuItems),
                              18.0, items, _menuItems, selected, _hoverMenuItem,
                              value, style);
  }
}
void updateSliderAtPoint(drawing::Point point) {

  auto *instance = static_cast<Plugin *>(_plugin);
  uint32_t pickup = 0u;
  if (_dragSlider >= 0 &&
      pickupTuneIndex(static_cast<clap_id>(_dragSlider), pickup)) {
    const drawing::Rect axis = tuneAxisRect();
    const double norm =
        std::clamp((point.x - axis.origin.x) / axis.size.width, 0.0, 1.0);
    setParam_value(static_cast<clap_id>(_dragSlider), norm * 2.0 - 1.0);
    return;
  }
  if (_dragSlider >= 0 &&
      pickupDecayIndex(static_cast<clap_id>(_dragSlider), pickup)) {
    const drawing::Rect axis = decayAxisRect();
    const double norm =
        std::clamp((point.x - axis.origin.x) / axis.size.width, 0.0, 1.0);
    setParam_value(static_cast<clap_id>(_dragSlider), norm * 2.0 - 1.0);
    return;
  }
  const s3g::gui_layout::Panel *panel = &kResynthesisPanel;
  if (_dragSlider == kParamOutput)
    panel = &kOutputPanel;
  else if (_dragSlider == kParamCaptureSeconds ||
           _dragSlider == kParamSensitivity)
    panel = &kCapturePanel;
  else if (_dragSlider == kParamTopologyAmount ||
           _dragSlider == kParamRoamingRate)
    panel = &kTopologyPanel;
  else if (_dragSlider >= static_cast<int>(kParamMaskAmount) &&
           _dragSlider <= static_cast<int>(kParamMaskDry))
    panel = &kMaskPanel;
  const double norm = std::clamp(
      (point.x - s3g::gui_layout::processorControlX(panel->frame.x)) /
          s3g::gui_layout::processorTrackWidth(panel->frame.width),
      0.0, 1.0);
  switch (_dragSlider) {
  case kParamOutput:
    setParam_value(kParamOutput, -60.0 + norm * 72.0);
    break;
  case kParamCaptureSeconds:
    setParam_value(kParamCaptureSeconds, 0.25 + norm * 2.75);
    break;
  case kParamSensitivity:
    setParam_value(kParamSensitivity, norm);
    break;
  case kParamTranspose:
    setParam_value(kParamTranspose, -24.0 + norm * 48.0);
    break;
  case kParamHarmonicPull:
    setParam_value(kParamHarmonicPull, norm);
    break;
  case kParamHarmonicStretch:
    setParam_value(kParamHarmonicStretch, norm * 2.0 - 1.0);
    break;
  case kParamDecay:
    setParam_value(kParamDecay, 0.08 * std::pow(8.0 / 0.08, norm));
    break;
  case kParamDecayTilt:
    setParam_value(kParamDecayTilt, norm * 2.0 - 1.0);
    break;
  case kParamDrive:
    setParam_value(kParamDrive, norm);
    break;
  case kParamSpread:
    setParam_value(kParamSpread, norm);
    break;
  case kParamDeviation:
    setParam_value(kParamDeviation, norm);
    break;
  case kParamMix:
    setParam_value(kParamMix, norm);
    break;
  case kParamTopologyAmount:
    setParam_value(kParamTopologyAmount, norm);
    break;
  case kParamRoamingRate:
    setParam_value(kParamRoamingRate, 0.005 * std::pow(2.0 / 0.005, norm));
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
    (void)instance;
    break;
  }
}
void mouseDown(drawing::Event *event) {

  const drawing::Point point =
      convertPoint_fromView(event->locationInWindow(), nullptr);
  auto *instance = static_cast<Plugin *>(_plugin);
  if (handleTitleClick(point, &instance->plugin, "Ambi Effect Resonance Print",
                       s3g::gui_layout::transformTitleBand(kCanvas),
                       instance->presetName, sizeof(instance->presetName))) {
    setNeedsDisplay(true);
    return;
  }
  if (_openMenu > 0) {
    const double width =
        s3g::gui_layout::processorMenuWidth(kOutputPanel.frame.width);
    const int hit = drawing::dropdownHitIndex(
        point,
        drawing::makeRect(_menuOrigin.x, _menuOrigin.y, width,
                          18.0 * _menuItems),
        18.0, _menuItems);
    if (hit >= 0) {
      if (_openMenu == 1)
        set(kParamOrder, hit + 1.0);
      else if (_openMenu == 2)
        set(kParamBody, (hit == 0 ? 0.0 : hit + 2.0));
      else if (_openMenu == 3)
        set(kParamModalCount, hit + 2.0);
      else
        set(kParamTopology, hit);
    }
    _openMenu = 0;
    _hoverMenuItem = -1;
    setNeedsDisplay(true);
    return;
  }
  const drawing::Rect fieldPanel = drawing::cocoaRect(kFieldPanel.frame);
  for (uint32_t index = 0u; index < 3u; ++index) {
    if (drawing::contains(point, drawing::topologyProcessorCameraButtonRect(
                                     fieldPanel, index))) {
      setViewPreset(static_cast<int>(index));
      return;
    }
  }
  if (drawing::contains(point, captureButtonRect())) {
    begin(kParamCapture);
    setParam_value(kParamCapture, 1.0);
    setParam_value(kParamCapture, 0.0);
    end();
    return;
  }
  if (drawing::contains(point, applyButtonRect())) {
    if (!instance->capturing.load(std::memory_order_relaxed) &&
        instance->hasPrint.load(std::memory_order_relaxed)) {
      set(kParamPrintEnabled,
          getParam(*instance, kParamPrintEnabled) != 0.0 ? 0.0 : 1.0);
    }
    return;
  }
  if (drawing::contains(point, clearButtonRect())) {
    begin(kParamClear);
    setParam_value(kParamClear, 1.0);
    setParam_value(kParamClear, 0.0);
    end();
    return;
  }
  const auto body = static_cast<s3g::AmbiEffectBody>(
      instance->resolvedBody.load(std::memory_order_relaxed));
  const uint32_t count = s3g::ambiEffectBodyPickupCount(body);
  const auto projected = projectBody(body, static_cast<float>(_viewAzDeg),
                                     static_cast<float>(_viewElDeg));
  for (uint32_t node = 0u; node < count; ++node) {
    const double dx = point.x - projected.points[node].x;
    const double dy = point.y - projected.points[node].y;
    if (dx * dx + dy * dy <= 18.0 * 18.0) {
      _selectedPickup = node;
      if (event->clickCount() >= 2) {
        set(kParamPickupTuneFirst + node, 0.0);
        set(kParamPickupDecayFirst + node, 0.0);
      }
      setNeedsDisplay(true);
      return;
    }
  }
  const auto beginAxis = [&](drawing::Rect axis, clap_id id) {
    if (!drawing::contains(point, drawing::inset(axis, 0.0, -8.0)))
      return false;
    double defaultValue = 0.0;
    if (drawing::sliderDoubleClickDefault(event, &instance->plugin, id,
                                          &defaultValue)) {
      set(id, defaultValue);
    } else {
      begin(id);
      _dragSlider = static_cast<int>(id);
      updateSliderAtPoint(point);
    }
    return true;
  };
  if (beginAxis(tuneAxisRect(), kParamPickupTuneFirst + _selectedPickup))
    return;
  if (beginAxis(decayAxisRect(), kParamPickupDecayFirst + _selectedPickup))
    return;

  struct MenuHit {
    s3g::gui_layout::Rect rect;
    int menu;
    uint32_t items;
  };
  const MenuHit menus[]{
      {s3g::gui_layout::sliderHitRect(kOutputPanel, 1u), 1, 7u},
      {s3g::gui_layout::sliderHitRect(kCapturePanel, 0u), 2, 4u},
      {s3g::gui_layout::sliderHitRect(kCapturePanel, 3u), 3, 11u},
      {s3g::gui_layout::sliderHitRect(kTopologyPanel, 0u), 4, 4u},
  };
  for (const auto &menu : menus) {
    if (!drawing::contains(point, drawing::cocoaRect(menu.rect)))
      continue;
    _openMenu = menu.menu;
    _menuItems = menu.items;
    _menuOrigin = drawing::makePoint(
        s3g::gui_layout::processorControlX(kOutputPanel.frame.x),
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
      {s3g::gui_layout::sliderHitRect(kOutputPanel, 0u), kParamOutput},
      {s3g::gui_layout::sliderHitRect(kCapturePanel, 1u), kParamCaptureSeconds},
      {s3g::gui_layout::sliderHitRect(kCapturePanel, 2u), kParamSensitivity},
      {s3g::gui_layout::sliderHitRect(kResynthesisPanel, 0u), kParamTranspose},
      {s3g::gui_layout::sliderHitRect(kResynthesisPanel, 1u),
       kParamHarmonicPull},
      {s3g::gui_layout::sliderHitRect(kResynthesisPanel, 2u),
       kParamHarmonicStretch},
      {s3g::gui_layout::sliderHitRect(kResynthesisPanel, 3u), kParamDecay},
      {s3g::gui_layout::sliderHitRect(kResynthesisPanel, 4u), kParamDecayTilt},
      {s3g::gui_layout::sliderHitRect(kResynthesisPanel, 5u), kParamDrive},
      {s3g::gui_layout::sliderHitRect(kResynthesisPanel, 6u), kParamSpread},
      {s3g::gui_layout::sliderHitRect(kResynthesisPanel, 7u), kParamDeviation},
      {s3g::gui_layout::sliderHitRect(kResynthesisPanel, 8u), kParamMix},
      {s3g::gui_layout::sliderHitRect(kTopologyPanel, 1u),
       kParamTopologyAmount},
      {s3g::gui_layout::sliderHitRect(kTopologyPanel, 2u), kParamRoamingRate},
      {s3g::gui_layout::sliderHitRect(kMaskPanel, 0u), kParamMaskAmount},
      {s3g::gui_layout::sliderHitRect(kMaskPanel, 1u), kParamMaskAzimuth},
      {s3g::gui_layout::sliderHitRect(kMaskPanel, 2u), kParamMaskElevation},
      {s3g::gui_layout::sliderHitRect(kMaskPanel, 3u), kParamMaskWidth},
      {s3g::gui_layout::sliderHitRect(kMaskPanel, 4u), kParamMaskDry},
  };
  for (const auto &slider : sliders) {
    if (!drawing::contains(point, drawing::cocoaRect(slider.rect)))
      continue;
    double defaultValue = 0.0;
    if (drawing::sliderDoubleClickDefault(event, &instance->plugin,
                                          slider.param, &defaultValue)) {
      set(slider.param, defaultValue);
    } else {
      begin(slider.param);
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
  if (_dragSlider >= 0)
    updateSliderAtPoint(point);
}
void mouseMoved(drawing::Event *event) {

  if (_openMenu <= 0)
    return;
  const drawing::Point point =
      convertPoint_fromView(event->locationInWindow(), nullptr);
  const double width =
      s3g::gui_layout::processorMenuWidth(kOutputPanel.frame.width);
  _hoverMenuItem = drawing::dropdownHitIndex(
      point,
      drawing::makeRect(_menuOrigin.x, _menuOrigin.y, width, 18.0 * _menuItems),
      18.0, _menuItems);
  setNeedsDisplay(true);
}
void mouseUp(drawing::Event *event) {

  (void)event;
  if (_dragSlider >= 0) {
    end();
  }
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
