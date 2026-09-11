// Literal adaptation of the original Displacement projections and timeline.
namespace ambi_effect_canvas {
namespace drawing = s3g::portable_gui::ambi_effect_drawing;
using namespace VSTGUI;
namespace foundation = s3g::portable_gui::foundation;
using Kind = s3g::clap_gui::ParamEventKind;
using namespace s3g::portable_gui::routing;
struct GuiProjection {
  drawing::Point point{};
  float depth = 0.0f;
};

float linearToSrgb(float value) {
  const float x = std::clamp(value, 0.0f, 1.0f);
  return x <= 0.0031308f ? x * 12.92f
                         : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}

CColor displacementPointColor(const s3g::AmbiEffectDisplacementPoint &point) {
  const float hue = std::fmod(point.azimuthDeg / 360.0f + 1.0f, 1.0f);
  const float light =
      std::clamp((point.elevationDeg + 90.0f) / 180.0f, 0.30f, 0.84f);
  const float chroma = std::clamp(point.radius / 3.0f, 0.10f, 1.0f) * 0.34f;
  const float a = std::cos(hue * 2.0f * s3g::kPi) * chroma;
  const float b = std::sin(hue * 2.0f * s3g::kPi) * chroma;
  const float l3 = light + 0.3963377774f * a + 0.2158037573f * b;
  const float m3 = light - 0.1055613458f * a - 0.0638541728f * b;
  const float s3 = light - 0.0894841775f * a - 1.2914855480f * b;
  const float l = l3 * l3 * l3;
  const float m = m3 * m3 * m3;
  const float s = s3 * s3 * s3;
  return drawing::calibratedRGB(
      linearToSrgb(4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s),
      linearToSrgb(-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s),
      linearToSrgb(-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s),
      0.94f);
}

std::vector<std::array<uint32_t, 2>> displacementMeshEdges(
    const std::array<s3g::AmbiEffectDisplacementPoint,
                     s3g::kAmbiEffectDisplacementMaxPickups> &points,
    uint32_t count) {
  std::vector<std::array<uint32_t, 2>> result;
  count = std::min<uint32_t>(count, s3g::kAmbiEffectDisplacementMaxPickups);
  for (uint32_t point = 0u; point < count; ++point) {
    std::vector<std::pair<float, uint32_t>> neighbors;
    const auto a = s3g::directionFromAed(points[point].azimuthDeg,
                                         points[point].elevationDeg);
    for (uint32_t other = 0u; other < count; ++other) {
      if (point == other)
        continue;
      const auto b = s3g::directionFromAed(points[other].azimuthDeg,
                                           points[other].elevationDeg);
      const float dx = a.x - b.x;
      const float dy = a.y - b.y;
      const float dz = a.z - b.z;
      neighbors.push_back({dx * dx + dy * dy + dz * dz, other});
    }
    std::sort(neighbors.begin(), neighbors.end());
    for (uint32_t index = 0u;
         index <
         std::min<uint32_t>(3u, static_cast<uint32_t>(neighbors.size()));
         ++index) {
      const uint32_t aIndex = std::min(point, neighbors[index].second);
      const uint32_t bIndex = std::max(point, neighbors[index].second);
      const std::array<uint32_t, 2> edge{aIndex, bIndex};
      if (std::find(result.begin(), result.end(), edge) == result.end()) {
        result.push_back(edge);
      }
    }
  }
  return result;
}

constexpr auto kDisplacementOutputPanel =
    s3g::gui_layout::ambiEffectDisplacementOutputPanel();
constexpr auto kDisplacementPlaybackPanel =
    s3g::gui_layout::fittedStackPanel(s3g::gui_layout::PanelRole::EventTiming,
                                      kDisplacementOutputPanel, 5u, 24.0, 32.0);
constexpr auto kDisplacementWarpPanel = s3g::gui_layout::fittedStackPanel(
    s3g::gui_layout::PanelRole::Motion, kDisplacementPlaybackPanel, 4u, 24.0,
    32.0);
constexpr auto kDisplacementDistancePanel =
    s3g::gui_layout::fittedStackPanel(s3g::gui_layout::PanelRole::Projection,
                                      kDisplacementWarpPanel, 3u, 24.0, 32.0);
constexpr auto kDisplacementMaskPanel = s3g::gui_layout::fittedStackPanel(
    s3g::gui_layout::PanelRole::Routing, kDisplacementDistancePanel, 6u, 24.0,
    32.0);
constexpr std::array kDisplacementColumnPanels{
    kDisplacementOutputPanel, kDisplacementPlaybackPanel,
    kDisplacementWarpPanel,   kDisplacementDistancePanel,
    kDisplacementMaskPanel,
};
static_assert(s3g::gui_layout::validateColumn(
    kDisplacementColumnPanels,
    s3g::gui_layout::kAmbiEffectFamilyLayout.displacementCanvas));

const s3g::gui_layout::Panel &displacementPanelForParam(clap_id param) {
  if (param == kParamOutput || param == kParamBypass || param == kParamOrder ||
      param == kParamBody || param == kParamMix) {
    return kDisplacementOutputPanel;
  }
  if (param >= kParamTransport && param <= kParamLength) {
    return kDisplacementPlaybackPanel;
  }
  if (param >= kParamAmount && param <= kParamRadiusScale) {
    return kDisplacementWarpPanel;
  }
  if (param >= kParamMaskAmount && param <= kParamMaskDry) {
    return kDisplacementMaskPanel;
  }
  return kDisplacementDistancePanel;
}

uint32_t displacementRowForParam(clap_id param) {
  switch (param) {
  case kParamOutput:
    return 0u;
  case kParamOrder:
    return 1u;
  case kParamBody:
    return 2u;
  case kParamMix:
    return 3u;
  case kParamBypass:
    return 4u;
  case kParamTransport:
    return 0u;
  case kParamPlayback:
    return 1u;
  case kParamPosition:
    return 2u;
  case kParamRate:
    return 3u;
  case kParamLength:
    return 4u;
  case kParamAmount:
    return 0u;
  case kParamAzimuthScale:
    return 1u;
  case kParamElevationScale:
    return 2u;
  case kParamRadiusScale:
    return 3u;
  case kParamDistanceMode:
    return 0u;
  case kParamReferenceMeters:
    return 1u;
  case kParamEnergy:
    return 2u;
  case kParamMaskAmount:
    return 0u;
  case kParamMaskAzimuth:
    return 1u;
  case kParamMaskElevation:
    return 2u;
  case kParamMaskWidth:
    return 3u;
  case kParamMaskCurve:
    return 4u;
  case kParamMaskDry:
    return 5u;
  default:
    return 0u;
  }
}

constexpr const char *pluginName = "Ambi Effect Displacement";
constexpr const char *pluginTitle = "s3g AMBI EFFECT DISPLACEMENT 64";
constexpr clap_id preservedOutput = kParamOutput;
constexpr const auto &stateExtension = stateExt;
#include "s3g_ambi_effect_displacement_editor.inc"
void storeViewState() {

  if (!_plugin)
    return;
  _plugin->guiViewMode = _viewMode;
  _plugin->guiViewAzimuthDeg = _viewAzimuthDeg;
  _plugin->guiViewElevationDeg = _viewElevationDeg;
  _plugin->guiViewZoom = _viewZoom;
}
drawing::Rect fieldPanelRect() {

  return drawing::cocoaRect(
      s3g::gui_layout::kAmbiEffectFamilyLayout.displacementFieldPanel);
}
drawing::Rect fieldRect() {

  return drawing::cocoaRect(
      s3g::gui_layout::kAmbiEffectFamilyLayout.displacementField);
}
drawing::Rect timelineRect() { return drawing::makeRect(42, 746, 564, 12); }
drawing::Rect loadButtonRect() { return drawing::makeRect(176, 45, 92, 16); }
drawing::Rect viewButtonRect(int index) {

  const double width = index == 3 ? 42.0 : 38.0;
  const double starts[] = {426.0, 470.0, 514.0, 558.0};
  return drawing::makeRect(starts[index], 45, width, 16);
}
drawing::Rect zoomButtonRect(int index) {

  return drawing::makeRect(368.0 + static_cast<double>(index) * 26.0, 45, 20,
                           16);
}
drawing::Rect menuRectForId(int menu) {

  clap_id param = CLAP_INVALID_ID;
  uint32_t count = 0u;
  if (menu == 1) {
    param = kParamTransport;
    count = 3u;
  } else if (menu == 2) {
    param = kParamPlayback;
    count = 3u;
  } else if (menu == 3) {
    param = kParamDistanceMode;
    count = 2u;
  } else if (menu == 4) {
    param = kParamOrder;
    count = 7u;
  } else if (menu == 5) {
    param = kParamBody;
    count = 4u;
  }
  if (param == CLAP_INVALID_ID)
    return drawing::Rect{};
  const auto &panel = displacementPanelForParam(param);
  const uint32_t row = displacementRowForParam(param);
  const auto box = s3g::gui_layout::menuBoxRect(panel, row);
  return drawing::makeRect(box.x, box.y + box.height + 3.0, box.width,
                           19.0 * count);
}
void setViewPreset(int mode) {

  _viewMode = mode;
  if (mode == 0) {
    _viewAzimuthDeg = 90.0;
    _viewElevationDeg = 0.0;
  } else if (mode == 1) {
    _viewAzimuthDeg = 90.0;
    _viewElevationDeg = 90.0;
  } else if (mode == 2) {
    _viewAzimuthDeg = 38.0;
    _viewElevationDeg = 28.0;
  }
  storeViewState();
  setNeedsDisplay(true);
}
GuiProjection projectPoint_inRect(const s3g::AmbiEffectDisplacementPoint &point,
                                  drawing::Rect rect) {

  if (_viewMode == 3) {
    const double x =
        drawing::midX(rect) -
        static_cast<double>(point.azimuthDeg / 180.0f) * rect.size.width * 0.47;
    const double y =
        drawing::midY(rect) - std::sin(point.elevationDeg * s3g::kPi / 180.0f) *
                                  rect.size.height * 0.44;
    return {drawing::makePoint(x, y), point.radius};
  }
  const s3g::Vec3 vector =
      s3g::directionFromAed(point.azimuthDeg, point.elevationDeg);
  const float radius = std::min(1.65f, std::max(0.10f, point.radius));
  const float azimuth = static_cast<float>(_viewAzimuthDeg * s3g::kPi / 180.0);
  const float elevation =
      static_cast<float>(_viewElevationDeg * s3g::kPi / 180.0);
  const float ca = std::cos(azimuth);
  const float sa = std::sin(azimuth);
  const float ce = std::cos(elevation);
  const float se = std::sin(elevation);
  const float x1 = (ca * vector.x - sa * vector.y) * radius;
  const float y1 = (sa * vector.x + ca * vector.y) * radius;
  const float z1 = vector.z * radius;
  const float y2 = ce * y1 + se * z1;
  const float depth = -se * y1 + ce * z1;
  const double scale = std::min(rect.size.width, rect.size.height) * 0.34 *
                       std::clamp(_viewZoom, 0.55, 2.4);
  return {drawing::makePoint(drawing::midX(rect) + x1 * scale,
                             drawing::midY(rect) - y2 * scale),
          depth};
}
drawing::String displayValueForParam(clap_id param) {

  char text[64]{};
  paramsValueToText(&_plugin->plugin, param, getParam(*_plugin, param), text,
                    sizeof(text));
  return drawing::stringFromUtf8(text);
}
void drawSlider_param_minimum_maximum_y_attrs_style(
    drawing::String label, clap_id param, double minimum, double maximum,
    double y, drawing::Attrs attrs, const drawing::Style &style) {

  const double value = getParam(*_plugin, param);
  double normalized = static_cast<double>(std::clamp(
      (value - minimum) / std::max(0.000001, maximum - minimum), 0.0, 1.0));
  if (param == kParamMaskAzimuth) {
    normalized = s3g::aedAzimuthSliderNorm(static_cast<float>(value));
  } else if (param == kParamRate || param == kParamLength) {
    normalized = static_cast<double>(
        std::clamp(std::log(std::max(minimum, value) / minimum) /
                       std::log(maximum / minimum),
                   0.0, 1.0));
  }
  const auto &panel = displacementPanelForParam(param);
  drawing::drawProcessorSlider(label, displayValueForParam(param), normalized,
                               y, panel.frame.x, panel.frame.width, attrs,
                               drawing::softValueAttrs(), style);
}
void drawMenu_param_y_attrs_style(drawing::String label, clap_id param,
                                  double y, drawing::Attrs attrs,
                                  const drawing::Style &style) {

  const auto &panel = displacementPanelForParam(param);
  drawing::drawProcessorMenu(label, displayValueForParam(param), y,
                             panel.frame.x, panel.frame.width, attrs,
                             drawing::softValueAttrs(), style);
}
void drawFieldWithScore_params_phase_rect_attrs(
    const s3g::AmbiEffectDisplacementScore &score, const PluginParams &params,
    float phase, drawing::Rect rect, drawing::Attrs attrs) {

  drawing::setFill(drawing::calibratedColor(0x111111));
  drawing::rectFill(rect);
  const auto geometry = s3g::ambiEffectDisplacementBodyGeometry(
      score, params.dsp, phase, params.playback);
  const uint32_t pointCount = geometry.count;
  std::array<GuiProjection, s3g::kAmbiEffectDisplacementMaxPickups>
      sourceProjected{};
  std::array<GuiProjection, s3g::kAmbiEffectDisplacementMaxPickups>
      targetProjected{};
  for (uint32_t point = 0u; point < pointCount; ++point) {
    sourceProjected[point] = projectPoint_inRect(geometry.source[point], rect);
    targetProjected[point] = projectPoint_inRect(geometry.target[point], rect);
  }

  auto drawEdgeSet = [&](const auto &projected, const auto &points,
                         CColor color, double width) {
    drawing::setStroke(color);
    for (const auto &edge : displacementMeshEdges(points, pointCount)) {
      const drawing::Point a = projected[edge[0]].point;
      const drawing::Point b = projected[edge[1]].point;
      if (_viewMode == 3 && std::abs(a.x - b.x) > rect.size.width * 0.55)
        continue;
      drawing::Path line = drawing::Path::bezierPath();
      (line).setLineWidth(width);
      (line).moveToPoint(a);
      (line).lineToPoint(b);
      (line).stroke();
    }
  };
  drawEdgeSet(sourceProjected, geometry.source,
              drawing::calibratedColor(0x747474, 0.13), 0.7);
  drawEdgeSet(targetProjected, geometry.target,
              drawing::calibratedColor(0x74bdc6, 0.30), 0.9);

  drawing::setStroke(drawing::calibratedColor(0x777777, 0.24));
  for (uint32_t point = 0u; point < pointCount; ++point) {
    drawing::Path spoke = drawing::Path::bezierPath();
    (spoke).setLineWidth(0.7);
    (spoke).moveToPoint(sourceProjected[point].point);
    (spoke).lineToPoint(targetProjected[point].point);
    (spoke).stroke();
  }

  std::vector<uint32_t> order(pointCount);
  for (uint32_t point = 0u; point < pointCount; ++point)
    order[point] = point;
  std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
    return targetProjected[a].depth < targetProjected[b].depth;
  });
  for (uint32_t point : order) {
    const drawing::Point source = sourceProjected[point].point;
    drawing::setFill(drawing::calibratedColor(0x777777, 0.70));
    drawing::rectFill(
        drawing::makeRect(source.x - 1.5, source.y - 1.5, 3.0, 3.0));
    const drawing::Point target = targetProjected[point].point;
    const float energy =
        _plugin->nodeLevel[point].load(std::memory_order_relaxed);
    const double haloRadius =
        5.0 + 15.0 * std::sqrt(std::clamp<double>(energy, 0.0, 1.0));
    drawing::setFill(drawing::withAlpha(
        displacementPointColor(geometry.target[point]), 0.08 + 0.26 * energy));
    (drawing::Path::bezierPathWithOvalInRect(
         drawing::makeRect(target.x - haloRadius, target.y - haloRadius,
                           haloRadius * 2.0, haloRadius * 2.0)))
        .fill();
    drawing::setFill(displacementPointColor(geometry.target[point]));
    (drawing::Path::bezierPathWithOvalInRect(
         drawing::makeRect(target.x - 3.0, target.y - 3.0, 6.0, 6.0)))
        .fill();
  }

  std::vector<drawing::Rect> occupied;
  occupied.reserve(pointCount * 2u);
  for (uint32_t point = 0u; point < pointCount; ++point) {
    const drawing::Point target = targetProjected[point].point;
    occupied.push_back(
        drawing::makeRect(target.x - 5.0, target.y - 5.0, 10.0, 10.0));
  }
  for (uint32_t point : order) {
    const drawing::Point target = targetProjected[point].point;
    drawing::String label = drawing::stringFormat("%u", point + 1u);
    const drawing::Size size = drawing::sizeWithAttributes(label, attrs);
    const std::array<drawing::Point, 8> offsets{{
        drawing::makePoint(6.0, -size.height * 0.55),
        drawing::makePoint(6.0, 3.0),
        drawing::makePoint(-size.width - 6.0, -size.height * 0.55),
        drawing::makePoint(-size.width - 6.0, 3.0),
        drawing::makePoint(-size.width * 0.5, -size.height - 5.0),
        drawing::makePoint(-size.width * 0.5, 6.0),
        drawing::makePoint(10.0, -size.height - 4.0),
        drawing::makePoint(-size.width - 10.0, -size.height - 4.0),
    }};
    drawing::Rect chosen = drawing::makeRect(
        target.x + 6.0, target.y - size.height * 0.55, size.width, size.height);
    for (const drawing::Point offset : offsets) {
      const drawing::Rect candidate = drawing::makeRect(
          target.x + offset.x, target.y + offset.y, size.width, size.height);
      const drawing::Rect padded = drawing::inset(candidate, -1.5, -1.0);
      const bool inside =
          drawing::containsRect(drawing::inset(rect, 2.0, 2.0), candidate);
      const bool clear = std::none_of(
          occupied.begin(), occupied.end(), [&](drawing::Rect used) {
            return drawing::intersectsRect(padded, used);
          });
      if (inside && clear) {
        chosen = candidate;
        break;
      }
    }
    drawing::drawAtPoint_withAttributes(label, chosen.origin, attrs);
    occupied.push_back(drawing::inset(chosen, -1.5, -1.0));
  }
}
void drawTimelineWithScore_phase_attrs(
    const s3g::AmbiEffectDisplacementScore &score, float phase,
    drawing::Attrs attrs) {

  const drawing::Rect rect = timelineRect();
  drawing::setFill(drawing::calibratedColor(0x151515));
  drawing::rectFill(rect);
  drawing::setStroke(drawing::calibratedColor(0x4b4b4b));
  drawing::frameRect(rect);
  const double x =
      rect.origin.x + std::clamp<double>(phase, 0.0, 1.0) * rect.size.width;
  drawing::setFill(drawing::calibratedColor(0x4d777d, 0.50));
  drawing::rectFill(drawing::makeRect(
      rect.origin.x + 1.0, rect.origin.y + 1.0,
      std::max<double>(0.0, x - rect.origin.x - 1.0), rect.size.height - 2.0));
  drawing::setStroke(drawing::calibratedColor(0xb9b9b9));
  for (uint32_t scene = 0u; scene < score.sceneCount; ++scene) {
    const double marker =
        rect.origin.x + score.scenes[scene].time * rect.size.width;
    drawing::Path::strokeLineFromPoint_toPoint(
        drawing::makePoint(marker, rect.origin.y - 2.0),
        drawing::makePoint(marker, drawing::maxY(rect) + 2.0));
  }
  drawing::setFill(drawing::calibratedColor(0xd0d0d0));
  drawing::rectFill(drawing::makeRect(x - 1.5, rect.origin.y - 3.0, 3.0,
                                      rect.size.height + 6.0));
  (void)attrs;
}
void drawOpenMenu_style(drawing::Attrs attrs, const drawing::Style &style) {

  if (_openMenu == 0)
    return;
  static drawing::String transportItems[] = {"SYNC", "FREE", "SCRUB"};
  static drawing::String playbackItems[] = {"LOOP", "PALINDROME", "ONCE"};
  static drawing::String distanceItems[] = {"GAIN", "PHYSICAL"};
  static drawing::String orderItems[] = {"1OA", "2OA", "3OA", "4OA",
                                         "5OA", "6OA", "7OA"};
  static drawing::String bodyItems[] = {"AUTO", "ICOSA 12", "DODECA 20",
                                        "SPHERE 24"};
  drawing::String const *items = transportItems;
  uint32_t count = 3u;
  int selected = static_cast<int>(static_cast<uint32_t>(guiParams.transport));
  if (_openMenu == 2) {
    items = playbackItems;
    count = 3u;
    selected = static_cast<int>(static_cast<uint32_t>(guiParams.playback));
  } else if (_openMenu == 3) {
    items = distanceItems;
    count = 2u;
    selected =
        static_cast<int>(static_cast<uint32_t>(guiParams.dsp.distanceMode));
  } else if (_openMenu == 4) {
    items = orderItems;
    count = 7u;
    selected = static_cast<int>(guiParams.dsp.order - 1u);
  } else if (_openMenu == 5) {
    items = bodyItems;
    count = 4u;
    const uint32_t body = static_cast<uint32_t>(guiParams.dsp.body);
    selected = body == 0u ? 0 : static_cast<int>(body - 2u);
  }
  drawing::drawDropdownMenu(menuRectForId(_openMenu), 19.0, items, count,
                            selected, _hoverMenuItem, attrs, style);
}
void paint() override {

  drawing::DrawingScope scope(*this, context, font, titleFont, &error);
  service();
  const auto style = drawing::softTextStyle();
  drawing::Attrs labels = drawing::softLabelAttrs();
  drawing::Attrs values = drawing::softValueAttrs();
  drawing::Attrs titles = drawing::softTitleAttrs();
  drawing::setFill(style.bg);
  drawing::rectFill(bounds());

  s3g::AmbiEffectDisplacementScore score;
  std::string scoreName;
  std::string status;
  {
    std::lock_guard<std::mutex> lock(_plugin->stateMutex);
    score = _plugin->score;
    scoreName = _plugin->scoreName;
    status = _plugin->status;
  }
  const PluginParams params = guiParams;
  const float phase = params.transport == TransportMode::Scrub
                          ? params.position
                          : _plugin->guiPhase.load(std::memory_order_relaxed);

  const float peak = _plugin->outputPeak.exchange(
      _plugin->outputPeak.load(std::memory_order_relaxed) * 0.92f,
      std::memory_order_relaxed);
  const auto titleBand = s3g::gui_layout::ambiEffectTitleBand(
      s3g::gui_layout::kAmbiEffectFamilyLayout.displacementCanvas);
  drawing::drawAmbiEffectTitleBand("s3g AMBI EFFECT DISPLACEMENT 64",
                                   drawing::stringFromUtf8(_titlePresetName),
                                   drawing::peakDbText(peak), titleBand, style);

  const drawing::Rect fieldPanel = fieldPanelRect();
  drawing::drawPanelFrame(fieldPanel.origin.x, fieldPanel.origin.y,
                          fieldPanel.size.width, fieldPanel.size.height, style);
  drawing::drawPanelHeader("LISTENER PICKUPS / DISPLACEMENT FIELD", true,
                           fieldPanel.origin.x, fieldPanel.origin.y,
                           fieldPanel.size.width, 21, labels, style);
  drawing::drawToolboxHeaderActionButton(loadButtonRect(), fieldPanel,
                                         "LOAD SCORE", labels, style);
  static drawing::String viewLabels[] = {"TOP", "SIDE", "3/4", "MAP"};
  for (int index = 0; index < 2; ++index) {
    drawing::drawToolboxHeaderButton(zoomButtonRect(index), fieldPanel,
                                     index == 0 ? "-" : "+", false, labels,
                                     style);
  }
  for (int index = 0; index < 4; ++index) {
    drawing::drawToolboxHeaderButton(viewButtonRect(index), fieldPanel,
                                     viewLabels[index], _viewMode == index,
                                     labels, style);
  }
  drawFieldWithScore_params_phase_rect_attrs(score, params, phase, fieldRect(),
                                             values);
  drawTimelineWithScore_phase_attrs(score, phase, values);
  drawing::String nameText =
      drawing::uppercaseString(drawing::stringFromUtf8(scoreName.c_str()));
  drawing::String statusText =
      drawing::uppercaseString(drawing::stringFromUtf8(status.c_str()));
  const auto resolvedBody = static_cast<s3g::AmbiEffectBody>(
      _plugin->resolvedBody.load(std::memory_order_relaxed));
  drawing::String bodyText = drawing::stringFormat(
      "%@ / %u PICKUPS",
      drawing::stringFromUtf8(s3g::ambiEffectBodyName(resolvedBody)),
      s3g::ambiEffectBodyPickupCount(resolvedBody));
  drawing::drawAtPoint_withAttributes(nameText, drawing::makePoint(38, 770),
                                      labels);
  drawing::drawAtPoint_withAttributes(bodyText, drawing::makePoint(255, 770),
                                      values);
  drawing::drawAtPoint_withAttributes(statusText, drawing::makePoint(410, 770),
                                      values);

  const auto drawPanel = [&](drawing::String name,
                             const s3g::gui_layout::Panel &panel) {
    drawing::drawPanelFrame(panel.frame.x, panel.frame.y, panel.frame.width,
                            panel.frame.height, style);
    drawing::drawPanelHeader(
        name, true, panel.frame.x, panel.frame.y, panel.frame.width,
        s3g::gui_layout::kStandardMetrics.headerHeight, labels, style);
  };
  drawPanel("OUTPUT", kDisplacementOutputPanel);
  drawPanel("PLAYBACK", kDisplacementPlaybackPanel);
  drawPanel("WARP", kDisplacementWarpPanel);
  drawPanel("DISTANCE", kDisplacementDistancePanel);
  drawPanel("DIRECTION MASK", kDisplacementMaskPanel);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "OUT", kParamOutput, -60.0, 12.0,
      s3g::gui_layout::rowY(kDisplacementOutputPanel, 0u), labels, style);
  drawMenu_param_y_attrs_style(
      "ORDER", kParamOrder, s3g::gui_layout::rowY(kDisplacementOutputPanel, 1u),
      labels, style);
  drawMenu_param_y_attrs_style(
      "BODY", kParamBody, s3g::gui_layout::rowY(kDisplacementOutputPanel, 2u),
      labels, style);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "MIX", kParamMix, 0.0, 1.0,
      s3g::gui_layout::rowY(kDisplacementOutputPanel, 3u), labels, style);
  drawing::drawToggle(
      "BYP", params.dsp.bypass,
      s3g::gui_layout::rowY(kDisplacementOutputPanel, 4u), labels, values,
      style, s3g::gui_layout::processorLabelX(kDisplacementOutputPanel.frame.x),
      s3g::gui_layout::processorControlX(kDisplacementOutputPanel.frame.x),
      64.0);
  drawMenu_param_y_attrs_style(
      "CLOCK", kParamTransport,
      s3g::gui_layout::rowY(kDisplacementPlaybackPanel, 0u), labels, style);
  drawMenu_param_y_attrs_style(
      "PLAY", kParamPlayback,
      s3g::gui_layout::rowY(kDisplacementPlaybackPanel, 1u), labels, style);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "POS", kParamPosition, 0.0, 1.0,
      s3g::gui_layout::rowY(kDisplacementPlaybackPanel, 2u), labels, style);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "RATE", kParamRate, 0.001, 2.0,
      s3g::gui_layout::rowY(kDisplacementPlaybackPanel, 3u), labels, style);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "BEATS", kParamLength, 0.25, 128.0,
      s3g::gui_layout::rowY(kDisplacementPlaybackPanel, 4u), labels, style);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "AMT", kParamAmount, 0.0, 1.0,
      s3g::gui_layout::rowY(kDisplacementWarpPanel, 0u), labels, style);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "AZ", kParamAzimuthScale, 0.0, 2.0,
      s3g::gui_layout::rowY(kDisplacementWarpPanel, 1u), labels, style);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "EL", kParamElevationScale, 0.0, 2.0,
      s3g::gui_layout::rowY(kDisplacementWarpPanel, 2u), labels, style);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "RAD", kParamRadiusScale, 0.0, 2.0,
      s3g::gui_layout::rowY(kDisplacementWarpPanel, 3u), labels, style);
  drawMenu_param_y_attrs_style(
      "DIST", kParamDistanceMode,
      s3g::gui_layout::rowY(kDisplacementDistancePanel, 0u), labels, style);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "MTR", kParamReferenceMeters, 0.5, 10.0,
      s3g::gui_layout::rowY(kDisplacementDistancePanel, 1u), labels, style);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "ENRG", kParamEnergy, 0.0, 1.0,
      s3g::gui_layout::rowY(kDisplacementDistancePanel, 2u), labels, style);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "AMT", kParamMaskAmount, 0.0, 1.0,
      s3g::gui_layout::rowY(kDisplacementMaskPanel, 0u), labels, style);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "AZ", kParamMaskAzimuth, -180.0, 180.0,
      s3g::gui_layout::rowY(kDisplacementMaskPanel, 1u), labels, style);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "EL", kParamMaskElevation, -90.0, 90.0,
      s3g::gui_layout::rowY(kDisplacementMaskPanel, 2u), labels, style);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "WIDTH", kParamMaskWidth, 0.0, 1.0,
      s3g::gui_layout::rowY(kDisplacementMaskPanel, 3u), labels, style);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "CURVE", kParamMaskCurve, 0.0, 1.0,
      s3g::gui_layout::rowY(kDisplacementMaskPanel, 4u), labels, style);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "DRY", kParamMaskDry, -1.0, 0.0,
      s3g::gui_layout::rowY(kDisplacementMaskPanel, 5u), labels, style);
  drawOpenMenu_style(values, style);
}
void drawRect(drawing::Rect dirtyRect) {

  drawing::DrawingScope scope(*this, context, font, titleFont);
  service();
  const auto style = drawing::softTextStyle();
  drawing::Attrs labels = drawing::softLabelAttrs();
  drawing::Attrs values = drawing::softValueAttrs();
  drawing::Attrs titles = drawing::softTitleAttrs();
  drawing::setFill(style.bg);
  drawing::rectFill(bounds());

  s3g::AmbiEffectDisplacementScore score;
  std::string scoreName;
  std::string status;
  {
    std::lock_guard<std::mutex> lock(_plugin->stateMutex);
    score = _plugin->score;
    scoreName = _plugin->scoreName;
    status = _plugin->status;
  }
  const PluginParams params = guiParams;
  const float phase = params.transport == TransportMode::Scrub
                          ? params.position
                          : _plugin->guiPhase.load(std::memory_order_relaxed);

  const float peak = _plugin->outputPeak.exchange(
      _plugin->outputPeak.load(std::memory_order_relaxed) * 0.92f,
      std::memory_order_relaxed);
  const auto titleBand = s3g::gui_layout::ambiEffectTitleBand(
      s3g::gui_layout::kAmbiEffectFamilyLayout.displacementCanvas);
  drawing::drawAmbiEffectTitleBand("s3g AMBI EFFECT DISPLACEMENT 64",
                                   drawing::stringFromUtf8(_titlePresetName),
                                   drawing::peakDbText(peak), titleBand, style);

  const drawing::Rect fieldPanel = fieldPanelRect();
  drawing::drawPanelFrame(fieldPanel.origin.x, fieldPanel.origin.y,
                          fieldPanel.size.width, fieldPanel.size.height, style);
  drawing::drawPanelHeader("LISTENER PICKUPS / DISPLACEMENT FIELD", true,
                           fieldPanel.origin.x, fieldPanel.origin.y,
                           fieldPanel.size.width, 21, labels, style);
  drawing::drawToolboxHeaderActionButton(loadButtonRect(), fieldPanel,
                                         "LOAD SCORE", labels, style);
  static drawing::String viewLabels[] = {"TOP", "SIDE", "3/4", "MAP"};
  for (int index = 0; index < 2; ++index) {
    drawing::drawToolboxHeaderButton(zoomButtonRect(index), fieldPanel,
                                     index == 0 ? "-" : "+", false, labels,
                                     style);
  }
  for (int index = 0; index < 4; ++index) {
    drawing::drawToolboxHeaderButton(viewButtonRect(index), fieldPanel,
                                     viewLabels[index], _viewMode == index,
                                     labels, style);
  }
  drawFieldWithScore_params_phase_rect_attrs(score, params, phase, fieldRect(),
                                             values);
  drawTimelineWithScore_phase_attrs(score, phase, values);
  drawing::String nameText =
      drawing::uppercaseString(drawing::stringFromUtf8(scoreName.c_str()));
  drawing::String statusText =
      drawing::uppercaseString(drawing::stringFromUtf8(status.c_str()));
  const auto resolvedBody = static_cast<s3g::AmbiEffectBody>(
      _plugin->resolvedBody.load(std::memory_order_relaxed));
  drawing::String bodyText = drawing::stringFormat(
      "%@ / %u PICKUPS",
      drawing::stringFromUtf8(s3g::ambiEffectBodyName(resolvedBody)),
      s3g::ambiEffectBodyPickupCount(resolvedBody));
  drawing::drawAtPoint_withAttributes(nameText, drawing::makePoint(38, 770),
                                      labels);
  drawing::drawAtPoint_withAttributes(bodyText, drawing::makePoint(255, 770),
                                      values);
  drawing::drawAtPoint_withAttributes(statusText, drawing::makePoint(410, 770),
                                      values);

  const auto drawPanel = [&](drawing::String name,
                             const s3g::gui_layout::Panel &panel) {
    drawing::drawPanelFrame(panel.frame.x, panel.frame.y, panel.frame.width,
                            panel.frame.height, style);
    drawing::drawPanelHeader(
        name, true, panel.frame.x, panel.frame.y, panel.frame.width,
        s3g::gui_layout::kStandardMetrics.headerHeight, labels, style);
  };
  drawPanel("OUTPUT", kDisplacementOutputPanel);
  drawPanel("PLAYBACK", kDisplacementPlaybackPanel);
  drawPanel("WARP", kDisplacementWarpPanel);
  drawPanel("DISTANCE", kDisplacementDistancePanel);
  drawPanel("DIRECTION MASK", kDisplacementMaskPanel);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "OUT", kParamOutput, -60.0, 12.0,
      s3g::gui_layout::rowY(kDisplacementOutputPanel, 0u), labels, style);
  drawMenu_param_y_attrs_style(
      "ORDER", kParamOrder, s3g::gui_layout::rowY(kDisplacementOutputPanel, 1u),
      labels, style);
  drawMenu_param_y_attrs_style(
      "BODY", kParamBody, s3g::gui_layout::rowY(kDisplacementOutputPanel, 2u),
      labels, style);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "MIX", kParamMix, 0.0, 1.0,
      s3g::gui_layout::rowY(kDisplacementOutputPanel, 3u), labels, style);
  drawing::drawToggle(
      "BYP", params.dsp.bypass,
      s3g::gui_layout::rowY(kDisplacementOutputPanel, 4u), labels, values,
      style, s3g::gui_layout::processorLabelX(kDisplacementOutputPanel.frame.x),
      s3g::gui_layout::processorControlX(kDisplacementOutputPanel.frame.x),
      64.0);
  drawMenu_param_y_attrs_style(
      "CLOCK", kParamTransport,
      s3g::gui_layout::rowY(kDisplacementPlaybackPanel, 0u), labels, style);
  drawMenu_param_y_attrs_style(
      "PLAY", kParamPlayback,
      s3g::gui_layout::rowY(kDisplacementPlaybackPanel, 1u), labels, style);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "POS", kParamPosition, 0.0, 1.0,
      s3g::gui_layout::rowY(kDisplacementPlaybackPanel, 2u), labels, style);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "RATE", kParamRate, 0.001, 2.0,
      s3g::gui_layout::rowY(kDisplacementPlaybackPanel, 3u), labels, style);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "BEATS", kParamLength, 0.25, 128.0,
      s3g::gui_layout::rowY(kDisplacementPlaybackPanel, 4u), labels, style);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "AMT", kParamAmount, 0.0, 1.0,
      s3g::gui_layout::rowY(kDisplacementWarpPanel, 0u), labels, style);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "AZ", kParamAzimuthScale, 0.0, 2.0,
      s3g::gui_layout::rowY(kDisplacementWarpPanel, 1u), labels, style);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "EL", kParamElevationScale, 0.0, 2.0,
      s3g::gui_layout::rowY(kDisplacementWarpPanel, 2u), labels, style);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "RAD", kParamRadiusScale, 0.0, 2.0,
      s3g::gui_layout::rowY(kDisplacementWarpPanel, 3u), labels, style);
  drawMenu_param_y_attrs_style(
      "DIST", kParamDistanceMode,
      s3g::gui_layout::rowY(kDisplacementDistancePanel, 0u), labels, style);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "MTR", kParamReferenceMeters, 0.5, 10.0,
      s3g::gui_layout::rowY(kDisplacementDistancePanel, 1u), labels, style);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "ENRG", kParamEnergy, 0.0, 1.0,
      s3g::gui_layout::rowY(kDisplacementDistancePanel, 2u), labels, style);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "AMT", kParamMaskAmount, 0.0, 1.0,
      s3g::gui_layout::rowY(kDisplacementMaskPanel, 0u), labels, style);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "AZ", kParamMaskAzimuth, -180.0, 180.0,
      s3g::gui_layout::rowY(kDisplacementMaskPanel, 1u), labels, style);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "EL", kParamMaskElevation, -90.0, 90.0,
      s3g::gui_layout::rowY(kDisplacementMaskPanel, 2u), labels, style);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "WIDTH", kParamMaskWidth, 0.0, 1.0,
      s3g::gui_layout::rowY(kDisplacementMaskPanel, 3u), labels, style);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "CURVE", kParamMaskCurve, 0.0, 1.0,
      s3g::gui_layout::rowY(kDisplacementMaskPanel, 4u), labels, style);
  drawSlider_param_minimum_maximum_y_attrs_style(
      "DRY", kParamMaskDry, -1.0, 0.0,
      s3g::gui_layout::rowY(kDisplacementMaskPanel, 5u), labels, style);
  drawOpenMenu_style(values, style);
}
void loadDocumentationScore() {

  auto score = s3g::makeDefaultAmbiEffectDisplacementScore();
  score.sceneCount = 3u;
  score.durationSeconds = 18.0f;
  score.scenes[0].time = 0.0f;
  score.scenes[0].points = score.source;
  score.scenes[1].time = 0.50f;
  score.scenes[2].time = 1.0f;
  for (uint32_t point = 0u; point < s3g::kAmbiEffectDisplacementScorePoints;
       ++point) {
    const float sign = (point & 1u) ? -1.0f : 1.0f;
    const float band = static_cast<float>(point % 6u) / 5.0f;
    auto middle = score.source[point];
    middle.azimuthDeg += sign * (34.0f + 42.0f * band);
    middle.elevationDeg =
        std::clamp(middle.elevationDeg * -0.55f + sign * (18.0f + 24.0f * band),
                   -86.0f, 86.0f);
    middle.radius = 0.42f + 1.72f * band;
    score.scenes[1].points[point] =
        s3g::sanitizeAmbiEffectDisplacementPoint(middle);

    auto ending = score.source[point];
    ending.azimuthDeg -= sign * (52.0f + 58.0f * band);
    ending.elevationDeg =
        std::clamp(ending.elevationDeg * 0.30f - sign * (28.0f + 30.0f * band),
                   -86.0f, 86.0f);
    ending.radius = 2.45f - 1.85f * band;
    score.scenes[2].points[point] =
        s3g::sanitizeAmbiEffectDisplacementPoint(ending);
  }
  installScore(*_plugin, score, 1.0f, 1.35f, 1.35f, 1.45f,
               "DOCUMENTATION / DEFORMED SCORE");
  setNeedsDisplay(true);
}
void updateParam_fromPoint(clap_id param, drawing::Point point) {

  const auto *definition =
      std::find_if(std::begin(kParamDefs), std::end(kParamDefs),
                   [&](const ParamDef &item) { return item.id == param; });
  if (definition == std::end(kParamDefs))
    return;
  const auto &panel = displacementPanelForParam(param);
  const double controlX = s3g::gui_layout::processorControlX(panel.frame.x);
  const double trackWidth =
      s3g::gui_layout::processorTrackWidth(panel.frame.width);
  const double normalized =
      std::clamp((point.x - controlX) / trackWidth, 0.0, 1.0);
  double value = definition->minimum +
                 normalized * (definition->maximum - definition->minimum);
  if (param == kParamMaskAzimuth) {
    value = s3g::aedAzimuthFromSliderNorm(static_cast<float>(normalized));
  } else if (param == kParamRate || param == kParamLength) {
    value = definition->minimum *
            std::pow(definition->maximum / definition->minimum, normalized);
  }
  setParam_value(param, value);
}
void updateTimeline(drawing::Point point) {

  const drawing::Rect rect = timelineRect();
  const double position =
      std::clamp((point.x - rect.origin.x) / rect.size.width, 0.0, 1.0);
  setParam_value(kParamPosition, position);
  setParam_value(kParamTransport, static_cast<double>(static_cast<uint32_t>(
                                      TransportMode::Scrub)));
}
void mouseDown(drawing::Event *event) {

  const drawing::Point point =
      convertPoint_fromView(event->locationInWindow(), nullptr);
  const auto titleBand = s3g::gui_layout::ambiEffectTitleBand(
      s3g::gui_layout::kAmbiEffectFamilyLayout.displacementCanvas);
  if (handleTitleClick(point, &_plugin->plugin, "Ambi Effect Displacement",
                       titleBand, _titlePresetName, sizeof(_titlePresetName),
                       kParamOutput)) {
    setNeedsDisplay(true);
    return;
  }
  if (_openMenu != 0) {
    const drawing::Rect menu = menuRectForId(_openMenu);
    const uint32_t count =
        _openMenu == 1 || _openMenu == 2
            ? 3u
            : (_openMenu == 4 ? 7u : (_openMenu == 5 ? 4u : 2u));
    const int hit = drawing::dropdownHitIndex(point, menu, 19.0, count);
    if (hit >= 0) {
      const clap_id param =
          _openMenu == 1
              ? kParamTransport
              : (_openMenu == 2
                     ? kParamPlayback
                     : (_openMenu == 3
                            ? kParamDistanceMode
                            : (_openMenu == 4 ? kParamOrder : kParamBody)));
      double value = hit;
      if (_openMenu == 4)
        value = hit + 1;
      if (_openMenu == 5)
        value = hit == 0 ? 0 : hit + 2;
      setParam_value(param, value);
      _openMenu = 0;
      _hoverMenuItem = -1;
      return;
    }
    _openMenu = 0;
    _hoverMenuItem = -1;
  }
  if (drawing::contains(point, loadButtonRect())) {
    loadScore();
    return;
  }
  for (int index = 0; index < 4; ++index) {
    if (drawing::contains(point, viewButtonRect(index))) {
      setViewPreset(index);
      return;
    }
  }
  for (int index = 0; index < 2; ++index) {
    if (drawing::contains(point, zoomButtonRect(index))) {
      _viewZoom = std::clamp(_viewZoom * (index == 0 ? 0.88 : 1.14), 0.55, 2.4);
      storeViewState();
      setNeedsDisplay(true);
      return;
    }
  }
  const struct {
    int menu;
    clap_id param;
  } menus[]{
      {1, kParamTransport}, {2, kParamPlayback}, {3, kParamDistanceMode},
      {4, kParamOrder},     {5, kParamBody},
  };
  for (const auto &menu : menus) {
    const auto &panel = displacementPanelForParam(menu.param);
    const drawing::Rect box = drawing::cocoaRect(s3g::gui_layout::menuBoxRect(
        panel, displacementRowForParam(menu.param)));
    if (drawing::contains(point, box)) {
      _openMenu = menu.menu;
      _hoverMenuItem = -1;
      setNeedsDisplay(true);
      return;
    }
  }
  if (drawing::contains(point,
                        drawing::cocoaRect(s3g::gui_layout::sliderHitRect(
                            kDisplacementOutputPanel, 4u)))) {
    setParam_value(kParamBypass, guiParams.dsp.bypass ? 0.0 : 1.0);
    return;
  }
  const clap_id sliders[]{
      kParamOutput,       kParamMix,
      kParamPosition,     kParamRate,
      kParamLength,       kParamAmount,
      kParamAzimuthScale, kParamElevationScale,
      kParamRadiusScale,  kParamReferenceMeters,
      kParamEnergy,       kParamMaskAmount,
      kParamMaskAzimuth,  kParamMaskElevation,
      kParamMaskWidth,    kParamMaskCurve,
      kParamMaskDry,
  };
  for (const clap_id slider : sliders) {
    const auto &panel = displacementPanelForParam(slider);
    if (drawing::contains(point,
                          drawing::cocoaRect(s3g::gui_layout::sliderHitRect(
                              panel, displacementRowForParam(slider))))) {
      _dragParam = slider;
      double defaultValue = 0.0;
      if (drawing::sliderDoubleClickDefault(event, &_plugin->plugin, slider,
                                            &defaultValue)) {
        setParam_value(slider, defaultValue);
      } else {
        updateParam_fromPoint(slider, point);
      }
      return;
    }
  }
  if (drawing::contains(point, timelineRect())) {
    _dragTimeline = true;
    updateTimeline(point);
    return;
  }
  if (drawing::contains(point, fieldRect()) && _viewMode != 3) {
    _dragView = true;
    _lastDragPoint = point;
  }
}
void mouseDragged(drawing::Event *event) {

  const drawing::Point point =
      convertPoint_fromView(event->locationInWindow(), nullptr);
  if (_dragParam != CLAP_INVALID_ID) {
    updateParam_fromPoint(_dragParam, point);
  } else if (_dragTimeline) {
    updateTimeline(point);
  } else if (_dragView) {
    _viewAzimuthDeg = std::clamp(
        _viewAzimuthDeg + (point.x - _lastDragPoint.x) * 0.45, -180.0, 180.0);
    _viewElevationDeg = std::clamp(
        _viewElevationDeg + (point.y - _lastDragPoint.y) * 0.35, -90.0, 90.0);
    _viewMode = -1;
    _lastDragPoint = point;
    storeViewState();
    setNeedsDisplay(true);
  }
}
void mouseUp(drawing::Event *event) {

  (void)event;
  _dragParam = CLAP_INVALID_ID;
  _dragTimeline = false;
  _dragView = false;
}
void mouseMoved(drawing::Event *event) {

  if (_openMenu == 0)
    return;
  const drawing::Point point =
      convertPoint_fromView(event->locationInWindow(), nullptr);
  const uint32_t count =
      _openMenu == 1 || _openMenu == 2
          ? 3u
          : (_openMenu == 4 ? 7u : (_openMenu == 5 ? 4u : 2u));
  _hoverMenuItem =
      drawing::dropdownHitIndex(point, menuRectForId(_openMenu), 19.0, count);
  setNeedsDisplay(true);
}
void scrollWheel(drawing::Event *event) {

  const drawing::Point point =
      convertPoint_fromView(event->locationInWindow(), nullptr);
  if (!drawing::contains(point, fieldRect()) || _viewMode == 3)
    return;
  _viewZoom = std::clamp(
      _viewZoom * std::exp(-event->scrollingDeltaY() * 0.035), 0.55, 2.4);
  storeViewState();
  setNeedsDisplay(true);
}

Plugin *_plugin;

clap_id _dragParam;
int _openMenu;
int _hoverMenuItem;
int _viewMode;
double _viewAzimuthDeg;
double _viewElevationDeg;
double _viewZoom;
bool _dragView;
bool _dragTimeline;
drawing::Point _lastDragPoint;
char _titlePresetName[64];

}; // namespace ambi_effect_canvas
}
namespace ambi_effect_canvas {
const s3g::gui_documentation::Extension documentation{
    [](const clap_plugin_t *plugin) {
      auto *host = self(plugin)->portableGuiEditor;
      auto *editor =
          host ? dynamic_cast<Editor *>(host->contentView()) : nullptr;
      if (!editor)
        return false;
      editor->loadDocumentationScore();
      return true;
    },
    nullptr, nullptr};
}
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new ambi_effect_canvas::Editor(p);
}
#include "s3g_clap_canvas_gui.inc"
