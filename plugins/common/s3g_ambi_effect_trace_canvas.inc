// Literal adaptation of the retained Cocoa geometry, drawing and hit handlers.
namespace ambi_effect_canvas {
namespace drawing = s3g::portable_gui::ambi_effect_drawing;
using namespace VSTGUI;
namespace foundation = s3g::portable_gui::foundation;
using Kind = s3g::clap_gui::ParamEventKind;

constexpr s3g::gui_layout::Canvas kCanvas{920.0, 820.0};
constexpr s3g::gui_layout::Panel kFieldPanel{
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Engine,
    {18.0, 42.0, 612.0, 760.0},
    36.0,
    26.0,
    0u,
};
constexpr drawing::Rect kPlot = {{34.0, 76.0}, {580.0, 710.0}};
constexpr s3g::gui_layout::Panel kOutputPanel{
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Output,
    {648.0, 42.0, 258.0, 106.0},
    36.0,
    26.0,
    3u,
};
#if S3G_PARTIAL_TRACE
constexpr s3g::gui_layout::Panel kEnginePanel{
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Engine,
    {648.0, 160.0, 258.0, 292.0},
    34.0,
    24.0,
    11u,
};
constexpr s3g::gui_layout::Panel kTopologyPanel{
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Topology,
    {648.0, 464.0, 258.0, 106.0},
    36.0,
    26.0,
    3u,
};
constexpr s3g::gui_layout::Panel kMaskPanel{
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Utility,
    {648.0, 582.0, 258.0, 172.0},
    34.0,
    24.0,
    6u,
};
#else
constexpr s3g::gui_layout::Panel kEnginePanel{
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Engine,
    {648.0, 160.0, 258.0, 148.0},
    34.0,
    24.0,
    5u,
};
constexpr s3g::gui_layout::Panel kTopologyPanel{
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Topology,
    {648.0, 320.0, 258.0, 106.0},
    36.0,
    26.0,
    3u,
};
constexpr s3g::gui_layout::Panel kMaskPanel{
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Utility,
    {648.0, 438.0, 258.0, 172.0},
    34.0,
    24.0,
    6u,
};
#endif

struct ProjectedBody {
  std::array<drawing::Point, kPickups> points{};
  std::array<float, kPickups> depths{};
};

drawing::Point projectTraceDirection(s3g::Vec3 direction, float azimuthDeg,
                                     float elevationDeg) {
  const auto projected =
      s3g::projectAedDirection(direction, azimuthDeg, -elevationDeg);
  return drawing::makePoint(drawing::midX(kPlot) +
                                projected.horizontal * 188.0f,
                            drawing::midY(kPlot) - projected.vertical * 188.0f);
}

ProjectedBody projectTraceBody(s3g::AmbiEffectBody body, float azimuthDeg,
                               float elevationDeg) {
  ProjectedBody result{};
  const auto directions = s3g::ambiEffectBodyDirections(body);
  const uint32_t count = s3g::ambiEffectBodyPickupCount(body);
  for (uint32_t node = 0u; node < count; ++node) {
    const auto direction = directions[node];
    result.depths[node] =
        s3g::projectAedDirection(direction, azimuthDeg, -elevationDeg).depth;
    result.points[node] =
        projectTraceDirection(direction, azimuthDeg, elevationDeg);
  }
  return result;
}

struct GuiSlider {
  clap_id id;
  const char *label;
  double minimum;
  double maximum;
  double defaultValue;
  bool logarithmic;
  bool reversed = false;
};

#if S3G_PARTIAL_TRACE
constexpr GuiSlider engineSliders[]{
    {kParamPartialCount, "PARTIALS", 1, 16, 10, false},
    {kParamSensitivity, "SENS", 0, 1, 0.62, false},
    {kParamMinimumFrequency, "LOW", 20, 4000, 55, true},
    {kParamMaximumFrequency, "HIGH", 200, 20000, 12000, true},
    {kParamTracking, "TRACK", 20, 1000, 120, true},
    {kParamRelease, "RELEASE", 40, 4000, 420, true},
    {kParamSmear, "SMEAR", 0, 1, 0, false},
    {kParamTranspose, "TRANS", -24, 24, 0, false},
    {kParamEngineGain, "TRACE", -36, 12, -3, false},
};
#else
constexpr GuiSlider engineSliders[]{
    {kParamCaptureSeconds, "TIME", 0.05, 1.5, 0.5, false},
    {kParamEngineGain, "RESPONSE", -36, 12, -6, false},
    {kParamTone, "TONE", 0, 1, 0.72, false},
};
#endif

constexpr GuiSlider topologySliders[]{
    {kParamTopologyAmount, "AMOUNT", 0, 1, 0.65, false},
    {kParamRoamingRate, "ROAM RATE", 0.005, 2, 0.08, true},
};

constexpr GuiSlider outputSlider{
    kParamOutput, "OUT", -60, 12, 0, false,
};
constexpr GuiSlider mixSlider{
    kParamMix, "MIX", 0, 1, kPartial ? 0.55 : 0.50, false,
};

constexpr GuiSlider maskSliders[]{
    {kParamMaskAmount, "MASK", 0, 1, 0, false},
    {kParamMaskAzimuth, "AZIMUTH", -180, 180, 0, false, true},
    {kParamMaskElevation, "ELEVATION", -90, 90, 0, false, true},
    {kParamMaskWidth, "WIDTH", 0, 1, 0.35, false},
    {kParamMaskCurve, "CURVE", 0, 1, 0.5, false},
    {kParamMaskDry, "MASK DRY", -1, 0, 0, false},
};

double sliderNorm(const GuiSlider &slider, double value) {
  double norm = 0.0;
  if (slider.logarithmic) {
    norm = std::log(std::max(slider.minimum, value) / slider.minimum) /
           std::log(slider.maximum / slider.minimum);
  } else {
    norm = (value - slider.minimum) / (slider.maximum - slider.minimum);
  }
  return slider.reversed ? 1.0 - norm : norm;
}

double sliderValue(const GuiSlider &slider, double norm) {
  norm = std::clamp(norm, 0.0, 1.0);
  if (slider.reversed)
    norm = 1.0 - norm;
  if (slider.logarithmic) {
    return slider.minimum * std::pow(slider.maximum / slider.minimum, norm);
  }
  return slider.minimum + (slider.maximum - slider.minimum) * norm;
}

drawing::Rect actionRect(uint32_t index) {
  const double x = static_cast<double>(kEnginePanel.frame.x + 16.0);
  const double width = static_cast<double>(kEnginePanel.frame.width - 32.0);
  const double y =
      static_cast<double>(s3g::gui_layout::rowY(kEnginePanel, 1u) - 4.0);
#if S3G_PARTIAL_TRACE
  constexpr double gap = 8.0;
  const double buttonWidth = (width - gap) * 0.5;
  return drawing::makeRect(x + index * (buttonWidth + gap), y, buttonWidth,
                           20.0);
#else
  constexpr double gap = 6.0;
  const double buttonWidth = (width - gap * 2.0) / 3.0;
  return drawing::makeRect(x + index * (buttonWidth + gap), y, buttonWidth,
                           20.0);
#endif
}

bool sliderLocation(clap_id id, const s3g::gui_layout::Panel *&panel,
                    uint32_t &row) {
  if (id == kParamOutput) {
    panel = &kOutputPanel;
    row = 0u;
    return true;
  }
  if (id == kParamMix) {
    panel = &kOutputPanel;
    row = 1u;
    return true;
  }
  for (uint32_t index = 0u; index < std::size(engineSliders); ++index) {
    if (engineSliders[index].id != id)
      continue;
    panel = &kEnginePanel;
    row = index + 2u;
    return true;
  }
  for (uint32_t index = 0u; index < std::size(topologySliders); ++index) {
    if (topologySliders[index].id != id)
      continue;
    panel = &kTopologyPanel;
    row = index + 1u;
    return true;
  }
  for (uint32_t index = 0u; index < std::size(maskSliders); ++index) {
    if (maskSliders[index].id != id)
      continue;
    panel = &kMaskPanel;
    row = index;
    return true;
  }
  return false;
}

constexpr const char *pluginName =
    kPartial ? "Ambi Effect Partial Trace" : "Ambi Effect Response Trace";
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
void drawPanel_title_style_attrs(const s3g::gui_layout::Panel &panel,
                                 drawing::String title,
                                 const drawing::Style &style,
                                 drawing::Attrs attrs) {

  drawing::drawPanelFrame(panel, style);
  drawing::drawPanelHeader(title, true, panel, attrs, style);
}
void drawMenuRow_text_panel_row_style_attrs_valueAttrs(
    drawing::String label, drawing::String text,
    const s3g::gui_layout::Panel &panel, uint32_t row,
    const drawing::Style &style, drawing::Attrs attrs,
    drawing::Attrs valueAttrs) {

  drawing::drawProcessorMenu(
      label, text, static_cast<double>(s3g::gui_layout::rowY(panel, row)),
      static_cast<double>(panel.frame.x),
      static_cast<double>(panel.frame.width), attrs, valueAttrs, style);
}
void drawSlider_panel_row_style_attrs_valueAttrs(
    const GuiSlider &slider, const s3g::gui_layout::Panel &panel, uint32_t row,
    const drawing::Style &style, drawing::Attrs attrs,
    drawing::Attrs valueAttrs) {

  auto *instance = static_cast<Plugin *>(_plugin);
  const double raw = getParam(*instance, slider.id);
  char valueText[64]{};
  paramsValueToText(&instance->plugin, slider.id, raw, valueText,
                    sizeof(valueText));
  const double norm = std::clamp(sliderNorm(slider, raw), 0.0, 1.0);
  drawing::drawProcessorSlider(
      drawing::stringFromUtf8(slider.label), drawing::stringFromUtf8(valueText),
      norm, static_cast<double>(s3g::gui_layout::rowY(panel, row)),
      static_cast<double>(panel.frame.x),
      static_cast<double>(panel.frame.width), attrs, valueAttrs, style);
}
void drawButton_text_active_style_attrs(drawing::Rect rect,
                                        drawing::String text, bool active,
                                        const drawing::Style &style,
                                        drawing::Attrs attrs) {

  drawing::setFill(drawing::calibratedColor(active ? 0x4b554f : 0x1b1f1d));
  drawing::rectFill(rect);
  drawing::setStroke(style.grid);
  drawing::frameRect(rect);
  const drawing::Size size = drawing::sizeWithAttributes(text, attrs);
  drawing::drawAtPoint_withAttributes(
      text,
      drawing::makePoint(drawing::midX(rect) - size.width * 0.5,
                         drawing::midY(rect) - size.height * 0.5),
      attrs);
}
void paint() override {

  drawing::DrawingScope drawingScope(*this, context, font, titleFont, &error);
  service();
  auto *instance = static_cast<Plugin *>(_plugin);
  drawing::Style style;
  drawing::setFill(style.bg);
  drawing::rectFill(bounds());
  drawing::Attrs label = drawing::softLabelAttrs();
  drawing::Attrs value = drawing::softValueAttrs();
  const float peak = instance->outputPeak.exchange(
      instance->outputPeak.load(std::memory_order_relaxed) * 0.92f,
      std::memory_order_relaxed);
  const float safety = instance->safetyGain.load(std::memory_order_relaxed);
  const auto body = static_cast<s3g::AmbiEffectBody>(
      instance->resolvedBody.load(std::memory_order_relaxed));
  const float governor =
      instance->excitationGain.load(std::memory_order_relaxed);
  drawing::String governorText =
      governor < 0.999f ? drawing::stringFormat(
                              " · GOV %.1f dB",
                              20.0f * std::log10(std::max(0.000001f, governor)))
                        : "";
  drawing::String status = drawing::stringFormat(
      "%uOA · %s · %@%@%@", guiParams.order, s3g::ambiEffectBodyName(body),
      drawing::peakDbText(peak), governorText,
      safety < 0.999f ? drawing::stringFormat(
                            " · SAFE %.1f dB",
                            20.0f * std::log10(std::max(0.000001f, safety)))
                      : "");
  drawing::drawAmbiEffectTitleBand(
      kPartial ? "s3g AMBI EFFECT PARTIAL TRACE 64CH"
               : "s3g AMBI EFFECT RESPONSE TRACE 64CH",
      drawing::stringFromUtf8(instance->presetName), status,
      s3g::gui_layout::transformTitleBand(kCanvas), style);

  drawPanel_title_style_attrs(kFieldPanel,
                              kPartial ? "LISTENER / SINUSOIDAL FIELD"
                                       : "LISTENER / RESPONSE FIELD",
                              style, label);
  const drawing::Rect fieldPanel = drawing::cocoaRect(kFieldPanel.frame);
  drawing::drawTopologyProcessorCameraButtons(fieldPanel, _viewMode, value,
                                              style);
  drawing::setFill(drawing::calibratedColor(0x101412));
  drawing::rectFill(kPlot);
  drawing::setStroke(style.grid);
  drawing::frameRect(kPlot);

  drawing::String fieldStatus;
#if S3G_PARTIAL_TRACE
  fieldStatus = drawing::stringFormat(
      "%@%u ACTIVE PARTIALS · STRONGEST %.1f Hz",
      guiParams.freeze ? "FROZEN · " : "",
      instance->activePartials.load(std::memory_order_relaxed),
      instance->strongestFrequency.load(std::memory_order_relaxed));
#else
  const bool capturingNow = instance->capturing.load(std::memory_order_relaxed);
  const bool preparingNow =
      instance->preparingResponse.load(std::memory_order_relaxed);
  const bool ready = instance->hasResponse.load(std::memory_order_relaxed);
  const bool requested = guiParams.responseEnabled != 0u;
  const bool audible =
      instance->responseAudible.load(std::memory_order_relaxed);
  const uint32_t capturedCount =
      instance->capturedPickupCount.load(std::memory_order_relaxed);
  fieldStatus =
      capturingNow
          ? drawing::stringFormat(
                "CAPTURING RESPONSE · %.0f%%",
                instance->captureProgress.load(std::memory_order_relaxed) *
                    100.0f)
          : (preparingNow
                 ? drawing::stringFormat("PREPARING RESPONSE · %.0f%%%@",
                                         instance->preparationProgress.load(
                                             std::memory_order_relaxed) *
                                             100.0f,
                                         requested ? " · APPLY QUEUED" : "")
                 : (ready ? drawing::stringFormat(
                                "%u DIRECTIONAL KERNELS · %@", capturedCount,
                                audible ? "APPLIED"
                                        : (requested ? "APPLYING"
                                                     : "READY · BYPASSED"))
                          : "NO RESPONSE · PRESS CAPTURE"));
#endif
  drawing::drawAtPoint_withAttributes(
      fieldStatus,
      drawing::makePoint(kPlot.origin.x + 12.0, kPlot.origin.y + 12.0), value);

  const auto projected = projectTraceBody(body, static_cast<float>(_viewAzDeg),
                                          static_cast<float>(_viewElDeg));
  const uint32_t count = s3g::ambiEffectBodyPickupCount(body);
  for (uint32_t first = 0u; first < count; ++first) {
    for (uint32_t second = first + 1u; second < count; ++second) {
      const double dx = projected.points[first].x - projected.points[second].x;
      const double dy = projected.points[first].y - projected.points[second].y;
      if (dx * dx + dy * dy > 166.0 * 166.0)
        continue;
      drawing::setStroke(drawing::calibratedColor(0x29302c));
      drawing::Path edge = drawing::Path::bezierPath();
      (edge).moveToPoint(projected.points[first]);
      (edge).lineToPoint(projected.points[second]);
      (edge).stroke();
    }
  }
  if (guiParams.maskAmount > 0.001f) {
    const s3g::Vec3 direction = s3g::directionFromAed(
        guiParams.maskAzimuthDeg, guiParams.maskElevationDeg);
    const drawing::Point target =
        projectTraceDirection(direction, static_cast<float>(_viewAzDeg),
                              static_cast<float>(_viewElDeg));
    const double radius = 15.0 + guiParams.maskWidth * 38.0;
    drawing::Path ring =
        drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
            target.x - radius, target.y - radius, radius * 2.0, radius * 2.0));
    double dash[]{4.0, 3.0};
    (ring).setLineDash_count_phase(dash, 2, 0.0);
    drawing::setStroke(drawing::calibratedColor(0xcbd0cd, 0.65));
    (ring).stroke();
  }
  std::array<uint32_t, kPickups> order{};
  for (uint32_t node = 0u; node < count; ++node)
    order[node] = node;
  std::sort(order.begin(), order.begin() + count, [&](uint32_t a, uint32_t b) {
    return projected.depths[a] < projected.depths[b];
  });
  for (uint32_t rank = 0u; rank < count; ++rank) {
    const uint32_t node = order[rank];
    const float inputLevel =
        instance->nodeLevel[node].load(std::memory_order_relaxed);
    const float effectLevel =
        instance->nodeEffectLevel[node].load(std::memory_order_relaxed);
    const float wet =
        instance->nodeWetMask[node].load(std::memory_order_relaxed);
    const double radius =
        5.0 + std::min(8.0f, inputLevel * 26.0f + effectLevel * 34.0f);
    const double brightness = std::clamp(
        0.24f + inputLevel * 0.8f + effectLevel * 1.4f, 0.24f, 0.95f);
    drawing::setFill(drawing::calibratedRGB(brightness * wet, brightness,
                                            brightness * 0.82, 1.0));
    (drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
         projected.points[node].x - radius, projected.points[node].y - radius,
         radius * 2.0, radius * 2.0)))
        .fill();
  }

  drawPanel_title_style_attrs(kOutputPanel, "OUTPUT", style, label);
  drawPanel_title_style_attrs(kEnginePanel,
                              kPartial ? "PARTIAL TRACE" : "RESPONSE TRACE",
                              style, label);
  drawPanel_title_style_attrs(kTopologyPanel, "TOPOLOGY", style, label);
  drawPanel_title_style_attrs(kMaskPanel, "DIRECTIONAL WET MASK", style, label);

  drawSlider_panel_row_style_attrs_valueAttrs(outputSlider, kOutputPanel, 0u,
                                              style, label, value);
  drawSlider_panel_row_style_attrs_valueAttrs(mixSlider, kOutputPanel, 1u,
                                              style, label, value);
  drawMenuRow_text_panel_row_style_attrs_valueAttrs(
      "ORDER", drawing::stringFormat("%uOA", guiParams.order), kOutputPanel, 2u,
      style, label, value);
  drawing::String bodyName =
      guiParams.body == s3g::AmbiEffectBody::Auto
          ? drawing::stringFormat("AUTO → %s", s3g::ambiEffectBodyName(body))
          : drawing::stringFromUtf8(s3g::ambiEffectBodyName(guiParams.body));
  drawMenuRow_text_panel_row_style_attrs_valueAttrs(
      "BODY", bodyName, kEnginePanel, 0u, style, label, value);
  drawMenuRow_text_panel_row_style_attrs_valueAttrs(
      "MODE",
      drawing::stringFromUtf8(s3g::ambiEffectTopologyName(guiParams.topology)),
      kTopologyPanel, 0u, style, label, value);

#if S3G_PARTIAL_TRACE
  const bool enabled = guiParams.enabled != 0u;
  drawButton_text_active_style_attrs(actionRect(0u),
                                     enabled ? "BYPASS TRACE" : "APPLY TRACE",
                                     enabled, style, value);
  const bool frozen = guiParams.freeze != 0u;
  drawButton_text_active_style_attrs(
      actionRect(1u), frozen ? "UNFREEZE" : "FREEZE", frozen, style, value);
#else
  const bool captureButtonActive =
      instance->capturing.load(std::memory_order_relaxed);
  const bool responseApplied = guiParams.responseEnabled != 0u;
  drawButton_text_active_style_attrs(actionRect(0u),
                                     captureButtonActive ? "LISTEN" : "CAPTURE",
                                     captureButtonActive, style, value);
  drawButton_text_active_style_attrs(actionRect(1u),
                                     responseApplied ? "BYPASS" : "APPLY",
                                     responseApplied, style, value);
  drawButton_text_active_style_attrs(actionRect(2u), "CLEAR", false, style,
                                     value);
#endif
  for (uint32_t index = 0u; index < std::size(engineSliders); ++index) {
    drawSlider_panel_row_style_attrs_valueAttrs(
        engineSliders[index], kEnginePanel, index + 2u, style, label, value);
  }
  for (uint32_t index = 0u; index < std::size(topologySliders); ++index) {
    drawSlider_panel_row_style_attrs_valueAttrs(topologySliders[index],
                                                kTopologyPanel, index + 1u,
                                                style, label, value);
  }
  for (uint32_t index = 0u; index < std::size(maskSliders); ++index) {
    drawSlider_panel_row_style_attrs_valueAttrs(maskSliders[index], kMaskPanel,
                                                index, style, label, value);
  }

  if (_openMenu > 0) {
    drawing::String orderItems[]{"1OA", "2OA", "3OA", "4OA",
                                 "5OA", "6OA", "7OA"};
#if S3G_PARTIAL_TRACE
    drawing::String bodyItems[]{"AUTO", "ICOSA 12", "DODECA 20", "SPHERE 24"};
#else
    drawing::String bodyItems[]{"AUTO",     "TETRA 4",   "CUBE 8",
                                "ICOSA 12", "DODECA 20", "SPHERE 24"};
#endif
    drawing::String topologyItems[]{"LOCAL", "CROSS", "DIFFUSE", "ROAMING"};
    drawing::String *items = _openMenu == 1
                                 ? orderItems
                                 : (_openMenu == 2 ? bodyItems : topologyItems);
    int selected = 0;
    if (_openMenu == 1) {
      selected = static_cast<int>(guiParams.order) - 1;
    } else if (_openMenu == 2) {
#if S3G_PARTIAL_TRACE
      const uint32_t bodyValue = static_cast<uint32_t>(guiParams.body);
      selected = bodyValue == 0u ? 0 : static_cast<int>(bodyValue) - 2;
#else
      selected = static_cast<int>(guiParams.body);
#endif
    } else {
      selected = static_cast<int>(guiParams.topology);
    }
    const double width = static_cast<double>(
        s3g::gui_layout::processorMenuWidth(kOutputPanel.frame.width));
    drawing::drawDropdownMenu(drawing::makeRect(_menuOrigin.x, _menuOrigin.y,
                                                width, 18.0 * _menuItems),
                              18.0, items, _menuItems, selected, _hoverMenuItem,
                              value, style);
  }
}
const GuiSlider *sliderForParam(clap_id param) {

  if (param == kParamOutput)
    return &outputSlider;
  if (param == kParamMix)
    return &mixSlider;
  for (const auto &slider : engineSliders)
    if (slider.id == param)
      return &slider;
  for (const auto &slider : topologySliders)
    if (slider.id == param)
      return &slider;
  for (const auto &slider : maskSliders)
    if (slider.id == param)
      return &slider;
  return nullptr;
}
void updateSliderAtPoint(drawing::Point point) {

  const GuiSlider *slider = sliderForParam(static_cast<clap_id>(_dragSlider));
  if (!slider)
    return;
  const s3g::gui_layout::Panel *panel = nullptr;
  uint32_t row = 0u;
  if (!sliderLocation(slider->id, panel, row) || !panel)
    return;
  const double norm =
      (point.x - s3g::gui_layout::processorControlX(panel->frame.x)) /
      s3g::gui_layout::processorTrackWidth(panel->frame.width);
  double value = sliderValue(*slider, norm);
  if (slider->id == kParamPartialCount)
    value = std::round(value);
  setParam_value(slider->id, value);
}
bool hitSliderRows_event(drawing::Point point, drawing::Event *event) {

  std::array<const GuiSlider *, 20u> sliders{};
  uint32_t count = 0u;
  sliders[count++] = &outputSlider;
  sliders[count++] = &mixSlider;
  for (const auto &slider : engineSliders)
    sliders[count++] = &slider;
  for (const auto &slider : topologySliders)
    sliders[count++] = &slider;
  for (const auto &slider : maskSliders)
    sliders[count++] = &slider;
  auto *instance = static_cast<Plugin *>(_plugin);
  for (uint32_t index = 0u; index < count; ++index) {
    const auto *slider = sliders[index];
    const s3g::gui_layout::Panel *panel = nullptr;
    uint32_t row = 0u;
    if (!sliderLocation(slider->id, panel, row) || !panel)
      continue;
    const drawing::Rect hit =
        drawing::cocoaRect(s3g::gui_layout::sliderHitRect(*panel, row));
    if (!drawing::contains(point, hit))
      continue;
    double defaultValue = 0.0;
    if (drawing::sliderDoubleClickDefault(event, &instance->plugin, slider->id,
                                          &defaultValue)) {
      setParam_value(slider->id, defaultValue);
    } else {
      _dragSlider = static_cast<int>(slider->id);
      updateSliderAtPoint(point);
    }
    return true;
  }
  return false;
}
void mouseDown(drawing::Event *event) {

  const drawing::Point point =
      convertPoint_fromView(event->locationInWindow(), nullptr);
  auto *instance = static_cast<Plugin *>(_plugin);
  if (handleTitleClick(point, &instance->plugin,
                       kPartial ? "Ambi Effect Partial Trace"
                                : "Ambi Effect Response Trace",
                       s3g::gui_layout::transformTitleBand(kCanvas),
                       instance->presetName, sizeof(instance->presetName))) {
    setNeedsDisplay(true);
    return;
  }
  if (_openMenu > 0) {
    const double width = static_cast<double>(
        s3g::gui_layout::processorMenuWidth(kOutputPanel.frame.width));
    const int hit = drawing::dropdownHitIndex(
        point,
        drawing::makeRect(_menuOrigin.x, _menuOrigin.y, width,
                          18.0 * _menuItems),
        18.0, _menuItems);
    if (hit >= 0) {
      if (_openMenu == 1) {
        setParam_value(kParamOrder, hit + 1.0);
      } else if (_openMenu == 2) {
#if S3G_PARTIAL_TRACE
        setParam_value(kParamBody, hit == 0 ? 0.0 : hit + 2.0);
#else
        setParam_value(kParamBody, hit);
#endif
      } else {
        setParam_value(kParamTopology, hit);
      }
    }
    _openMenu = 0;
    _hoverMenuItem = -1;
    setNeedsDisplay(true);
    return;
  }
  const drawing::Rect fieldPanel = drawing::cocoaRect(kFieldPanel.frame);
  for (uint32_t camera = 0u; camera < 3u; ++camera) {
    if (drawing::contains(point, drawing::topologyProcessorCameraButtonRect(
                                     fieldPanel, camera))) {
      setViewPreset(static_cast<int>(camera));
      return;
    }
  }
  struct MenuHit {
    const s3g::gui_layout::Panel *panel;
    uint32_t row;
    int menu;
    uint32_t items;
  };
  const MenuHit menus[] {
    {&kOutputPanel, 2u, 1, 7u},
#if S3G_PARTIAL_TRACE
        {&kEnginePanel, 0u, 2, 4u},
#else
        {&kEnginePanel, 0u, 2, 6u},
#endif
        {&kTopologyPanel, 0u, 3, 4u},
  };
  for (const auto &menu : menus) {
    const drawing::Rect hit = drawing::cocoaRect(
        s3g::gui_layout::sliderHitRect(*menu.panel, menu.row));
    if (!drawing::contains(point, hit))
      continue;
    _openMenu = menu.menu;
    _menuItems = menu.items;
    _menuOrigin = drawing::makePoint(
        static_cast<double>(
            s3g::gui_layout::processorControlX(menu.panel->frame.x)),
        hit.origin.y + 24.0);
    _hoverMenuItem = -1;
    setNeedsDisplay(true);
    return;
  }
#if S3G_PARTIAL_TRACE
  if (drawing::contains(point, actionRect(0u))) {
    setParam_value(kParamEnabled, getParam(*static_cast<Plugin *>(_plugin),
                                           kParamEnabled) > 0.5
                                      ? 0.0
                                      : 1.0);
    return;
  }
  if (drawing::contains(point, actionRect(1u))) {
    setParam_value(kParamFreeze,
                   getParam(*static_cast<Plugin *>(_plugin), kParamFreeze) > 0.5
                       ? 0.0
                       : 1.0);
    return;
  }
#else
  if (drawing::contains(point, actionRect(0u))) {
    setParam_value(kParamCapture, 1.0);
    setParam_value(kParamCapture, 0.0);
    return;
  }
  if (drawing::contains(point, actionRect(1u))) {
    setParam_value(kParamEnabled, getParam(*static_cast<Plugin *>(_plugin),
                                           kParamEnabled) > 0.5
                                      ? 0.0
                                      : 1.0);
    return;
  }
  if (drawing::contains(point, actionRect(2u))) {
    setParam_value(kParamClear, 1.0);
    setParam_value(kParamClear, 0.0);
    return;
  }
#endif
  if (hitSliderRows_event(point, event))
    return;
  if (drawing::contains(point, kPlot)) {
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
  if (_dragSlider < 0)
    return;
  updateSliderAtPoint(point);
}
void mouseMoved(drawing::Event *event) {

  if (_openMenu <= 0)
    return;
  const drawing::Point point =
      convertPoint_fromView(event->locationInWindow(), nullptr);
  const double width = static_cast<double>(
      s3g::gui_layout::processorMenuWidth(kOutputPanel.frame.width));
  const int hover = drawing::dropdownHitIndex(
      point,
      drawing::makeRect(_menuOrigin.x, _menuOrigin.y, width, 18.0 * _menuItems),
      18.0, _menuItems);
  if (hover == _hoverMenuItem)
    return;
  _hoverMenuItem = hover;
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
