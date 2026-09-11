// Literal Cloud Cocoa field, layout and pointer algorithms.
namespace input_encoder_canvas {
namespace drawing = s3g::portable_gui::input_encoder_drawing;
namespace foundation = s3g::portable_gui::foundation;
using namespace VSTGUI;
using namespace s3g::portable_gui::routing;
using Kind = s3g::clap_gui::ParamEventKind;
constexpr const char *pluginName = "Ambi Cloud Encoder";
constexpr const char *pluginTitle = "s3g AMBI ENCODER CLOUD";
constexpr clap_id preservedOutput = kOutputParamId;
constexpr const auto &stateExtension = stateExt;
struct Model {
  s3g::AmbiCloudEncoderParams params{};
  s3g::AmbiCloudEncoder encoder;
};
double routingReadValue(const Plugin &p, clap_id id) {
  return readEncoderValue(p, id);
}
void routingPublish(Plugin &p,
                    const std::vector<std::pair<clap_id, double>> &values) {
  Model model;
  model.params = snapshotEncoderParams(p);
  model.encoder.setParams(model.params);
  model.encoder.setClouds(snapshotEncoderClouds(p));
  model.params = model.encoder.params();
  for (auto [id, v] : values) {
    const auto canonical = canonicalEncoderId(p, id);
    rawEncoderApplyParam(model, id, v);
    if (canonical >= 100 && canonical < 116) {
      const auto &c = model.encoder.clouds()[(canonical - 100) / 4];
      const float coordinates[] = {c.azimuthDeg, c.elevationDeg, c.distance, c.gain};
      p.encoderValues[canonical].store(coordinates[(canonical - 100) % 4]);
    } else
      p.encoderValues[canonical].store(readEncoderScalar(model.params, id));
    if (id == kCloudsParamId || id == kCloudParamId)
      p.encoderValues[kCloudParamId].store(model.params.selectedCloud + 1u);
  }
}
void routingApply(Plugin &p, clap_id id, double v) {
  routingPublish(p, {{id, v}});
}
double routingRawValue(Plugin &p, clap_id id) {
  return readEncoderValue(p, id);
}
void routingInitialize(Plugin &p) {
  p.plugin.plugin_data = &p;
  init(&p.plugin);
}
#define S3G_ROUTING_CUSTOM_ACCESS 1
#include "s3g_routing_parameter_editor.inc"
#undef S3G_ROUTING_CUSTOM_ACCESS
constexpr double kToolboxX = 630.0;
constexpr double kToolboxLabelX =
    kToolboxX + s3g::gui_layout::kStandardMetrics.labelInset;
constexpr double kToolboxControlX =
    kToolboxX + s3g::gui_layout::kStandardMetrics.controlInset;
constexpr double kToolboxValueX =
    kToolboxX + s3g::gui_layout::kStandardMetrics.valueInset;
constexpr double kToolboxTrackWidth =
    s3g::gui_layout::kStandardMetrics.trackWidth;
constexpr double kToolboxMenuWidth =
    s3g::gui_layout::kStandardMetrics.menuWidth;
constexpr auto kOutputSourcePanel = s3g::gui_layout::fittedPanel(
    s3g::gui_layout::PluginClass::ProceduralEncoder,
    s3g::gui_layout::PanelRole::Output, {kToolboxX, 250.0, 42.0}, 42.0, 8u);
static_assert(s3g::gui_layout::combinedOutputSourceCardinalityControlMatches(
    kOutputSourcePanel, s3g::gui_layout::SharedControlRole::SourceCardinality));
constexpr double kInputCountRowY = static_cast<double>(s3g::gui_layout::rowY(
    kOutputSourcePanel,
    s3g::gui_layout::combinedOutputSourceCardinalityRow(
        s3g::gui_layout::SharedControlRole::SourceCardinality)));
class Editor final : public ParameterEditor {
public:
  explicit Editor(Plugin &instance) : ParameterEditor(instance) {
    auto *plugin = &p;
    _plugin = plugin;
    _openMenu = 0;
    _hoverMenuItem = -1;
    _menuItemCount = 0;
    _dragParam = 0;
    _viewMode = plugin ? plugin->guiViewMode : 0;
    _viewAzDeg = plugin ? plugin->guiViewAzDeg : 90.0;
    _viewElDeg = plugin ? plugin->guiViewElDeg : 0.0;
    _viewZoom = plugin ? plugin->guiViewZoom : 1.0;
    _dragView = false;
    _lastDragPoint = drawing::makePoint(0, 0);
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "CURRENT");

    guiEncoder.prepare(p.sampleRate);
    service();
  }
  ~Editor() override { stopRefresh(); }
  void stopRefresh() override {
    ParameterEditor::stopRefresh();
    pointerEditing = false;
    _openMenu = 0;
    _dragParam = 0;
    _dragView = false;
  }
  double refreshPeriodMilliseconds() const override { return 1000. / 60.; }
  void service() override {
    _viewMode = p.guiViewMode;
    _viewAzDeg = p.guiViewAzDeg;
    _viewElDeg = p.guiViewElDeg;
    _viewZoom = p.guiViewZoom;
    guiParams = snapshotEncoderParams(p);
    guiEncoder.setParams(guiParams);
    guiEncoder.setClouds(snapshotEncoderClouds(p));
    guiParams = guiEncoder.params();
    for (unsigned i = 0; i < kInputChannels; ++i)
      sourcePositions[i] = guiEncoder.sourcePositionForDisplay(i);
    if (p.encoderProcessing.load())
      p.encoderDisplay.read(
          [&](const auto &points) { sourcePositions = points; });
  }
  void preparePresetSave(Plugin &candidate) override {
    publishEncoderState(candidate, snapshotEncoderParams(p),
                        snapshotEncoderClouds(p));
    candidate.guiViewMode = p.guiViewMode;
    candidate.guiViewAzDeg = p.guiViewAzDeg;
    candidate.guiViewElDeg = p.guiViewElDeg;
    candidate.guiViewZoom = p.guiViewZoom;
  }
  void finishPresetLoad(const Plugin &candidate) override {
    auto params = snapshotEncoderParams(candidate);
    params.outputGainDb = value(preservedOutput);
    publishEncoderState(p, params, snapshotEncoderClouds(candidate));
    p.guiViewMode = candidate.guiViewMode;
    p.guiViewAzDeg = candidate.guiViewAzDeg;
    p.guiViewElDeg = candidate.guiViewElDeg;
    p.guiViewZoom = candidate.guiViewZoom;
    markStateDirty();
    service();
  }
  void guiApplyParam(Plugin &, clap_id id, double v) {
    if (pointerEditing) {
      if (begin(id))
        change(id, v);
    } else
      set(id, v);
    service();
    invalid();
  }
  drawing::Rect bounds() const {
    return drawing::makeRect(0, 0, kGuiWidth, kGuiHeight);
  }
  drawing::Point convertPoint_fromView(drawing::Point pt,
                                       std::nullptr_t) const {
    return pt;
  }
  void setNeedsDisplay(bool) { invalid(); }
  bool pointerDown(MouseDownEvent &e) override {
    if (!e.buttonState.isLeft())
      return false;
    service();
    pointerEditing = true;
    drawing::Event event{{e.mousePosition.x, e.mousePosition.y},
                         int(e.clickCount)};
    mouseDown(&event);
    return true;
  }
  void pointerMove(MouseMoveEvent &e) override {
    drawing::Event event{{e.mousePosition.x, e.mousePosition.y}};
    if (e.buttonState.isLeft())
      mouseDragged(&event);
    else
      mouseMoved(&event);
    invalid();
    e.consumed = true;
  }
  void pointerUp(MouseUpEvent &e) override {
    drawing::Event event{{e.mousePosition.x, e.mousePosition.y}};
    mouseUp(&event);
    end();
    pointerEditing = false;
    invalid();
  }
  bool pointerEditing = false;
  s3g::AmbiCloudEncoderParams guiParams{};
  s3g::AmbiCloudEncoder guiEncoder;
  std::array<s3g::Vec3, kInputChannels> sourcePositions{};
  std::mt19937 randomGenerator{std::random_device{}()};

  Plugin *_plugin;
  int _openMenu;
  int _hoverMenuItem;
  uint32_t _menuItemCount;
  int _dragParam;
  int _viewMode;
  double _viewAzDeg;
  double _viewElDeg;
  double _viewZoom;
  bool _dragView;
  drawing::Point _lastDragPoint;
  char _titlePresetName[64];
  void storeViewState() {
    if (!_plugin)
      return;
    _plugin->guiViewMode = _viewMode;
    _plugin->guiViewAzDeg = _viewAzDeg;
    _plugin->guiViewElDeg = _viewElDeg;
    _plugin->guiViewZoom = _viewZoom;

    markStateDirty();
  }
  drawing::Point projectDirection_inRect(s3g::Vec3 dir, drawing::Rect rect) {
    const double scale = std::min(rect.size.width, rect.size.height) * 0.38 *
                         std::clamp(_viewZoom, 0.55, 2.20);
    const double cx = drawing::midX(rect);
    const double cy = drawing::midY(rect);
    const float az = static_cast<float>(_viewAzDeg * M_PI / 180.0);
    const float el = static_cast<float>(_viewElDeg * M_PI / 180.0);
    const float ca = std::cos(az);
    const float sa = std::sin(az);
    const float ce = std::cos(el);
    const float se = std::sin(el);
    const float x1 = ca * dir.x - sa * dir.y;
    const float y1 = sa * dir.x + ca * dir.y;
    const float y2 = ce * y1 + se * dir.z;
    return drawing::makePoint(cx + static_cast<double>(x1) * scale,
                              cy - static_cast<double>(y2) * scale);
  }
  drawing::Rect viewButtonRect_inRect(int index, drawing::Rect rect) {
    const double w = 38.0;
    const double h = 13.0;
    const double gap = 5.0;
    const double x = drawing::maxX(rect) - 10.0 -
                     (3.0 - static_cast<double>(index)) * w -
                     (2.0 - static_cast<double>(index)) * gap;
    return drawing::makeRect(x, rect.origin.y + 4.0, w, h);
  }
  drawing::Rect zoomButtonRect_inRect(int index, drawing::Rect rect) {
    const double w = 18.0;
    const double h = 13.0;
    const double gap = 4.0;
    const double viewStart = viewButtonRect_inRect(0, rect).origin.x;
    const double x = viewStart - 12.0 - (2.0 - static_cast<double>(index)) * w -
                     (1.0 - static_cast<double>(index)) * gap;
    return drawing::makeRect(x, rect.origin.y + 4.0, w, h);
  }
  void drawViewButtonsInRect_attrs(drawing::Rect rect, drawing::Attrs attrs) {
    static drawing::String labels[] = {"TOP", "SIDE", "3/4"};
    drawing::Style style;
    for (int i = 0; i < 3; ++i) {
      drawing::drawToolboxHeaderButton(viewButtonRect_inRect(i, rect), rect,
                                       labels[i], i == _viewMode, attrs, style);
    }
  }
  void drawZoomButtonsInRect_attrs(drawing::Rect rect, drawing::Attrs attrs) {
    static drawing::String labels[] = {"-", "+"};
    drawing::Style style;
    for (int i = 0; i < 2; ++i) {
      drawing::drawToolboxHeaderButton(zoomButtonRect_inRect(i, rect), rect,
                                       labels[i], false, attrs, style);
    }
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
  void drawSlider_value_min_max_y_suffix_attrs_style(
      drawing::String name, double value, double min, double max, double y,
      drawing::String suffix, drawing::Attrs attrs,
      const drawing::Style &style) {
    double norm = (value - min) / std::max(0.000001, max - min);
    if ((name == "AZIM")) {
      norm = s3g::aedAzimuthSliderNorm(static_cast<float>(value));
    } else if ((name == "RATE")) {
      const double safeMin = std::max(0.000001, min);
      norm = std::log(std::max(safeMin, value) / safeMin) /
             std::log(max / safeMin);
    }
    drawing::String text;
    if (!suffix.empty() && (suffix == "OA"))
      text = drawing::stringFormat("%.0fOA", value);
    else if (!suffix.empty() && (suffix == "deg"))
      text = drawing::stringFormat("%+.0f", value);
    else if (!suffix.empty() && (suffix == "%"))
      text = drawing::stringFormat("%.0f%%", value * 100.0);
    else if (!suffix.empty() && (suffix == "Hz"))
      text = drawing::stringFormat("%.3f", value);
    else if (!suffix.empty() && (suffix == "dB"))
      text = drawing::stringFormat("%+.1f dB", value);
    else if (!suffix.empty() && (suffix == "count"))
      text = drawing::stringFormat("%.0f", value);
    else if (!suffix.empty() && (suffix == "shape"))
      text = drawing::stringFromUtf8(
          shapeName(static_cast<uint32_t>(std::lround(value))));
    else if (!suffix.empty() && (suffix == "force"))
      text = drawing::stringFromUtf8(
          forceName(static_cast<uint32_t>(std::lround(value))));
    else
      text = drawing::stringFormat("%.2f", value);
    drawing::drawSlider(name, text, norm, y, attrs, drawing::softValueAttrs(),
                        style, kToolboxLabelX, kToolboxControlX, kToolboxValueX,
                        kToolboxTrackWidth);
  }
  void drawMenu_value_y_attrs_style(drawing::String name, drawing::String value,
                                    double y, drawing::Attrs attrs,
                                    const drawing::Style &style) {
    drawing::drawMenu(name, value, y, attrs, drawing::softValueAttrs(), style,
                      kToolboxLabelX, kToolboxControlX, kToolboxMenuWidth);
  }
  double menuY() {
    switch (_openMenu) {
    case 1:
      return 174.0;
    case 2:
      return 200.0;
    case 3:
      return 122.0;
    case 4:
      return 396.0;
    case 5:
      return 422.0;
    default:
      return 0.0;
    }
  }
  double menuW() { return kToolboxMenuWidth; }
  void drawOpenMenu_style(drawing::Attrs attrs, const drawing::Style &style) {
    if (_openMenu <= 0 || _menuItemCount == 0)
      return;
    drawing::String cloudItems[] = {"1", "2", "3", "4"};
    drawing::String orderItems[] = {"1OA", "2OA", "3OA", "4OA",
                                    "5OA", "6OA", "7OA"};
    drawing::String shapeItems[] = {"CUMULUS", "STRATUS", "CIRRUS", "LENTIC",
                                    "STORM"};
    drawing::String forceItems[] = {"CALM", "ADVECT", "SHEAR", "CONVECT",
                                    "TURB"};
    drawing::String *items = cloudItems;
    int selected = 0;
    if (_openMenu == 1) {
      items = cloudItems;
      selected = static_cast<int>(guiParams.activeClouds) - 1;
      _menuItemCount = 4;
    } else if (_openMenu == 2) {
      items = cloudItems;
      selected = static_cast<int>(guiParams.selectedCloud);
      _menuItemCount = 4;
    } else if (_openMenu == 3) {
      items = orderItems;
      selected = static_cast<int>(guiParams.order) - 1;
      _menuItemCount = 7;
    } else if (_openMenu == 4) {
      items = shapeItems;
      selected = static_cast<int>(static_cast<uint32_t>(guiParams.shape));
      _menuItemCount = 5;
    } else if (_openMenu == 5) {
      items = forceItems;
      selected = static_cast<int>(static_cast<uint32_t>(guiParams.force));
      _menuItemCount = 5;
    }
    const double itemH = 18.0;
    drawing::drawDropdownMenu(
        drawing::makeRect(kToolboxControlX, menuY(), menuW(),
                          itemH * _menuItemCount),
        itemH, items, _menuItemCount, selected, _hoverMenuItem, attrs, style);
  }
  void drawField_attrs_style(drawing::Rect rect, drawing::Attrs attrs,
                             const drawing::Style &style) {
    drawing::setFill(drawing::calibratedColor(0x111111));
    drawing::rectFill(rect);
    drawing::setStroke(style.grid);
    drawing::frameRect(rect);

    const auto params = guiParams;
    const auto &clouds = guiEncoder.clouds();
    drawing::drawAmbisonicOrientationGuides([&](double x, double y, double z) {
      return projectDirection_inRect(
          {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)},
          rect);
    });

    const uint32_t activeInputs = std::min<uint32_t>(
        params.activeInputs, s3g::kAmbiCloudEncoderMaxInputs);
    for (uint32_t i = 0; i < activeInputs; ++i) {
      const s3g::Vec3 dir = sourcePositions[i];
      const drawing::Point p = projectDirection_inRect(dir, rect);
      const uint32_t cloudIndex =
          i % std::max<uint32_t>(1u, params.activeClouds);
      const int colors[]{0x9fd0ff, 0xf0d35d, 0xe59bd8, 0x8bd5a5};
      drawing::setFill(drawing::calibratedColor(
          colors[std::min<uint32_t>(cloudIndex, 3u)], 0.58));
      const double r = 2.4;
      drawing::rectFill(drawing::makeRect(
          std::round(p.x - r), std::round(p.y - r), r * 2.0, r * 2.0));
    }

    for (uint32_t i = 0; i < params.activeClouds; ++i) {
      const auto &c = clouds[i];
      s3g::Vec3 centerDir = s3g::directionFromAed(c.azimuthDeg, c.elevationDeg);
      const float centerRadius =
          s3g::AmbiCloudEncoder::displayDistance(c.distance);
      centerDir = {centerDir.x * centerRadius, centerDir.y * centerRadius,
                   centerDir.z * centerRadius};
      const drawing::Point p = projectDirection_inRect(centerDir, rect);
      const double r = i == params.selectedCloud ? 8.0 : 6.0;
      drawing::setFill(drawing::calibratedColor(
          0xd8ddd8, i == params.selectedCloud ? 0.94 : 0.65));
      drawing::rectFill(drawing::makeRect(
          std::round(p.x - r), std::round(p.y - r), r * 2.0, r * 2.0));
      drawing::setStroke(drawing::calibratedColor(0x050505, 0.95));
      drawing::frameRect(drawing::makeRect(
          std::round(p.x - r), std::round(p.y - r), r * 2.0, r * 2.0));
      drawing::String label = drawing::stringFormat("%u", i + 1u);
      drawing::Attrs idAttrs{drawing::calibratedColor(0x111111), false, 8.0};
      drawing::Size sz = drawing::sizeWithAttributes(label, idAttrs);
      drawing::drawAtPoint_withAttributes(
          label,
          drawing::makePoint(p.x - sz.width * 0.5, p.y - sz.height * 0.5 - 0.5),
          idAttrs);
    }

    drawing::String viewText =
        _viewMode == 0 ? "TOP VIEW"
                       : (_viewMode == 1 ? "SIDE VIEW" : "3/4 VIEW");
    drawing::drawAtPoint_withAttributes(
        viewText,
        drawing::makePoint(rect.origin.x + 10, drawing::maxY(rect) - 22),
        attrs);
  }
  void paint() override {
    drawing::DrawingScope scope(*this, context, font, titleFont, &error);
    service();

    const drawing::Style style = drawing::softTextStyle();
    drawing::setFill(style.bg);
    drawing::rectFill(bounds());
    drawing::Attrs labelAttrs = drawing::softLabelAttrs();
    drawing::Attrs valueAttrs = drawing::softValueAttrs();
    drawing::Attrs titleAttrs = drawing::softTitleAttrs();
    const float peak = _plugin->outputPeak.load(std::memory_order_relaxed);
    drawing::drawEncoderTitleBand(
        "s3g AMBI ENCODER CLOUD", drawing::stringFromUtf8(_titlePresetName),
        drawing::peakDbText(peak), drawing::encoderTitleBand(900.0, 672.0),
        titleAttrs, labelAttrs, valueAttrs, style);

    drawing::drawPanelFrame(18, 42, 596, 608, style);
    drawing::drawPanelHeader("CLOUD FIELD", true, 18, 42, 596, 21, labelAttrs,
                             style);
    drawZoomButtonsInRect_attrs(drawing::makeRect(18, 42, 596, 608),
                                valueAttrs);
    drawViewButtonsInRect_attrs(drawing::makeRect(18, 42, 596, 608),
                                valueAttrs);
    drawField_attrs_style(drawing::makeRect(34, 76, 564, 558), valueAttrs,
                          style);

    drawing::drawPanelFrame(630, 42, 250, 236, style);
    drawing::drawPanelHeader("OUTPUT / INPUT", true, 630, 42, 250, 21,
                             labelAttrs, style);
    drawing::drawPanelFrame(630, 290, 250, 314, style);
    drawing::drawPanelHeader("MATERIAL", true, 630, 290, 250, 21, labelAttrs,
                             style);
    const auto p = guiParams;
    drawSlider_value_min_max_y_suffix_attrs_style(
        "OUT", p.outputGainDb, -60, 12, 78, "dB", labelAttrs, style);
    drawMenu_value_y_attrs_style("ORDER",
                                 drawing::stringFormat("%uOA", p.order), 104,
                                 labelAttrs, style);
    drawSlider_value_min_max_y_suffix_attrs_style("INPUTS", p.activeInputs, 1,
                                                  64, kInputCountRowY, "count",
                                                  labelAttrs, style);
    drawMenu_value_y_attrs_style("CLOUDS",
                                 drawing::stringFormat("%u", p.activeClouds),
                                 156, labelAttrs, style);
    drawMenu_value_y_attrs_style(
        "CLOUD", drawing::stringFormat("%u", p.selectedCloud + 1u), 182,
        labelAttrs, style);
    drawSlider_value_min_max_y_suffix_attrs_style(
        "AZIM", p.selectedAzimuthDeg, -180, 180, 208, "deg", labelAttrs, style);
    drawSlider_value_min_max_y_suffix_attrs_style(
        "ELEV", p.selectedElevationDeg, -90, 90, 234, "deg", labelAttrs, style);
    drawSlider_value_min_max_y_suffix_attrs_style(
        "DIST", p.selectedDistance, 0.05, 8.0, 260, "", labelAttrs, style);
    drawSlider_value_min_max_y_suffix_attrs_style("SPREAD", p.spread, 0, 1, 326,
                                                  "%", labelAttrs, style);
    drawSlider_value_min_max_y_suffix_attrs_style(
        "ELEVSP", p.elevationSpread, 0, 1, 352, "%", labelAttrs, style);
    drawMenu_value_y_attrs_style(
        "SHAPE",
        drawing::stringFromUtf8(shapeName(static_cast<uint32_t>(p.shape))), 378,
        labelAttrs, style);
    drawMenu_value_y_attrs_style(
        "FORCE",
        drawing::stringFromUtf8(forceName(static_cast<uint32_t>(p.force))), 404,
        labelAttrs, style);
    drawSlider_value_min_max_y_suffix_attrs_style("JITTER", p.jitter, 0, 1, 430,
                                                  "%", labelAttrs, style);
    drawSlider_value_min_max_y_suffix_attrs_style("DRIFT", p.drift, 0, 1, 456,
                                                  "%", labelAttrs, style);
    drawSlider_value_min_max_y_suffix_attrs_style("RATE", p.rateHz, 0.001, 2.0,
                                                  482, "Hz", labelAttrs, style);
    drawSlider_value_min_max_y_suffix_attrs_style("DECOR", p.decorrelate, 0, 1,
                                                  508, "%", labelAttrs, style);
    drawSlider_value_min_max_y_suffix_attrs_style("GAIN", p.selectedGain, 0, 2,
                                                  534, "", labelAttrs, style);
    drawSlider_value_min_max_y_suffix_attrs_style("DOPPLER", p.doppler, 0, 1,
                                                  560, "%", labelAttrs, style);
    drawSlider_value_min_max_y_suffix_attrs_style("AIR", p.air, 0, 1, 586, "%",
                                                  labelAttrs, style);
    drawOpenMenu_style(labelAttrs, style);
  }
  void setParam_fromPoint(clap_id param, drawing::Point pt) {
    auto slider = [&](double min, double max) {
      const double norm = std::clamp(
          (static_cast<double>(pt.x) - kToolboxControlX) / kToolboxTrackWidth,
          0.0, 1.0);
      guiApplyParam(*_plugin, param, min + norm * (max - min));
    };
    auto logSlider = [&](double min, double max) {
      const double norm = std::clamp(
          (static_cast<double>(pt.x) - kToolboxControlX) / kToolboxTrackWidth,
          0.0, 1.0);
      guiApplyParam(*_plugin, param, min * std::pow(max / min, norm));
    };
    switch (param) {
    case kInputsParamId:
      slider(1, 64);
      break;
    case kCloudsParamId:
      slider(1, 4);
      break;
    case kCloudParamId:
      slider(1, 4);
      break;
    case kOrderParamId:
      slider(1, 7);
      break;
    case kAzimuthParamId: {
      const double norm = std::clamp(
          (static_cast<double>(pt.x) - kToolboxControlX) / kToolboxTrackWidth,
          0.0, 1.0);
      guiApplyParam(*_plugin, param,
                    s3g::aedAzimuthFromSliderNorm(static_cast<float>(norm)));
      break;
    }
    case kElevationParamId:
      slider(-90, 90);
      break;
    case kDistanceParamId:
      slider(0.05, 8.0);
      break;
    case kSpreadParamId:
      slider(0, 1);
      break;
    case kElevationSpreadParamId:
      slider(0, 1);
      break;
    case kShapeParamId:
      slider(0, 4);
      break;
    case kForceParamId:
      slider(0, 4);
      break;
    case kJitterParamId:
      slider(0, 1);
      break;
    case kDriftParamId:
      slider(0, 1);
      break;
    case kRateParamId:
      logSlider(0.001, 2.0);
      break;
    case kDecorrelateParamId:
      slider(0, 1);
      break;
    case kCloudGainParamId:
      slider(0, 2);
      break;
    case kDopplerParamId:
      slider(0, 1);
      break;
    case kAirParamId:
      slider(0, 1);
      break;
    case kOutputParamId:
      slider(-60, 12);
      break;
    default:
      break;
    }
    setNeedsDisplay(true);
  }
  void mouseDown(drawing::Event *event) {
    drawing::Point pt =
        convertPoint_fromView(event->locationInWindow(), nullptr);
    const auto titleBand = drawing::encoderTitleBand(900.0, 672.0);
    if (handleTitleClick(pt, titleBand))
      return;
    if (drawing::contains(pt, drawing::cocoaRect(titleBand.randomButton))) {
      auto randomUnit = [this] {
        return static_cast<double>(randomGenerator()) / 4294967295.0;
      };
      guiApplyParam(*_plugin, kShapeParamId, std::floor(randomUnit() * 5.0));
      guiApplyParam(*_plugin, kForceParamId, std::floor(randomUnit() * 5.0));
      guiApplyParam(*_plugin, kSpreadParamId, 0.20 + randomUnit() * 0.72);
      guiApplyParam(*_plugin, kElevationSpreadParamId,
                    0.10 + randomUnit() * 0.64);
      guiApplyParam(*_plugin, kJitterParamId, randomUnit() * 0.72);
      guiApplyParam(*_plugin, kDriftParamId, randomUnit() * 0.72);
      guiApplyParam(*_plugin, kRateParamId,
                    0.004 * std::pow(250.0, randomUnit()));
      guiApplyParam(*_plugin, kDecorrelateParamId, 0.20 + randomUnit() * 0.72);
      std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "RANDOM");
      setNeedsDisplay(true);
      return;
    }
    if (_openMenu > 0) {
      const double itemH = 18.0;
      const int hit = drawing::dropdownHitIndex(
          pt,
          drawing::makeRect(kToolboxControlX, menuY(), menuW(),
                            itemH * _menuItemCount),
          itemH, _menuItemCount);
      if (hit >= 0) {
        if (_openMenu == 1)
          guiApplyParam(*_plugin, kCloudsParamId, hit + 1);
        else if (_openMenu == 2)
          guiApplyParam(*_plugin, kCloudParamId, hit + 1);
        else if (_openMenu == 3)
          guiApplyParam(*_plugin, kOrderParamId, hit + 1);
        else if (_openMenu == 4)
          guiApplyParam(*_plugin, kShapeParamId, hit);
        else if (_openMenu == 5)
          guiApplyParam(*_plugin, kForceParamId, hit);
      }
      _openMenu = 0;
      _hoverMenuItem = -1;
      setNeedsDisplay(true);
      return;
    }
    auto openMenu = [&](int menu, uint32_t count) {
      _openMenu = menu;
      _menuItemCount = count;
      _hoverMenuItem = -1;
      setNeedsDisplay(true);
    };
    const drawing::Rect fieldPanel = drawing::makeRect(18, 42, 596, 608);
    if (drawing::contains(pt, fieldPanel)) {
      for (int i = 0; i < 2; ++i) {
        if (drawing::contains(pt, zoomButtonRect_inRect(i, fieldPanel))) {
          _viewZoom =
              std::clamp(_viewZoom + (i == 0 ? -0.15 : 0.15), 0.55, 2.20);
          storeViewState();
          setNeedsDisplay(true);
          return;
        }
      }
      for (int i = 0; i < 3; ++i) {
        if (drawing::contains(pt, viewButtonRect_inRect(i, fieldPanel))) {
          setViewPreset(i);
          return;
        }
      }
      if (drawing::contains(pt, drawing::makeRect(34, 76, 564, 558))) {
        _dragView = true;
        _lastDragPoint = pt;
        _viewMode = -1;
        storeViewState();
        return;
      }
    }
    _dragParam = 0;
    if (drawing::contains(pt, drawing::makeRect(638, 70, 230, 24)))
      _dragParam = kOutputParamId;
    else if (drawing::contains(pt, drawing::makeRect(kToolboxControlX, 103,
                                                     kToolboxMenuWidth, 17))) {
      openMenu(3, 7);
      return;
    } else if (drawing::contains(
                   pt, drawing::makeRect(638, kInputCountRowY - 8.0, 230, 24)))
      _dragParam = kInputsParamId;
    else if (drawing::contains(pt, drawing::makeRect(kToolboxControlX, 155,
                                                     kToolboxMenuWidth, 17))) {
      openMenu(1, 4);
      return;
    } else if (drawing::contains(pt,
                                 drawing::makeRect(kToolboxControlX, 181,
                                                   kToolboxMenuWidth, 17))) {
      openMenu(2, 4);
      return;
    } else if (drawing::contains(pt, drawing::makeRect(638, 200, 230, 24)))
      _dragParam = kAzimuthParamId;
    else if (drawing::contains(pt, drawing::makeRect(638, 226, 230, 24)))
      _dragParam = kElevationParamId;
    else if (drawing::contains(pt, drawing::makeRect(638, 252, 230, 24)))
      _dragParam = kDistanceParamId;
    else if (drawing::contains(pt, drawing::makeRect(638, 318, 230, 24)))
      _dragParam = kSpreadParamId;
    else if (drawing::contains(pt, drawing::makeRect(638, 344, 230, 24)))
      _dragParam = kElevationSpreadParamId;
    else if (drawing::contains(pt, drawing::makeRect(kToolboxControlX, 377,
                                                     kToolboxMenuWidth, 17))) {
      openMenu(4, 5);
      return;
    } else if (drawing::contains(pt,
                                 drawing::makeRect(kToolboxControlX, 403,
                                                   kToolboxMenuWidth, 17))) {
      openMenu(5, 5);
      return;
    } else if (drawing::contains(pt, drawing::makeRect(638, 422, 230, 24)))
      _dragParam = kJitterParamId;
    else if (drawing::contains(pt, drawing::makeRect(638, 448, 230, 24)))
      _dragParam = kDriftParamId;
    else if (drawing::contains(pt, drawing::makeRect(638, 474, 230, 24)))
      _dragParam = kRateParamId;
    else if (drawing::contains(pt, drawing::makeRect(638, 500, 230, 24)))
      _dragParam = kDecorrelateParamId;
    else if (drawing::contains(pt, drawing::makeRect(638, 526, 230, 24)))
      _dragParam = kCloudGainParamId;
    else if (drawing::contains(pt, drawing::makeRect(638, 552, 230, 24)))
      _dragParam = kDopplerParamId;
    else if (drawing::contains(pt, drawing::makeRect(638, 578, 230, 24)))
      _dragParam = kAirParamId;
    if (_dragParam) {
      double defaultValue = 0.0;
      if (drawing::sliderDoubleClickDefault(event, &_plugin->plugin,
                                            static_cast<clap_id>(_dragParam),
                                            &defaultValue)) {
        guiApplyParam(*_plugin, static_cast<clap_id>(_dragParam), defaultValue);
        _dragParam = 0;
        setNeedsDisplay(true);
        return;
      }
      setParam_fromPoint(static_cast<clap_id>(_dragParam), pt);
    }
  }
  void mouseDragged(drawing::Event *event) {
    drawing::Point pt =
        convertPoint_fromView(event->locationInWindow(), nullptr);
    if (_dragView) {
      const double dx = pt.x - _lastDragPoint.x;
      const double dy = pt.y - _lastDragPoint.y;
      _viewAzDeg += dx * 0.35;
      _viewElDeg = std::clamp(_viewElDeg + dy * 0.35, -85.0, 85.0);
      _viewMode = -1;
      _lastDragPoint = pt;
      storeViewState();
      setNeedsDisplay(true);
      return;
    }
    if (!_dragParam)
      return;
    setParam_fromPoint(static_cast<clap_id>(_dragParam), pt);
  }
  void mouseUp(drawing::Event *event) {
    (void)event;
    _dragParam = 0;
    _dragView = false;
  }
  void mouseMoved(drawing::Event *event) {
    if (_openMenu <= 0)
      return;
    drawing::Point pt =
        convertPoint_fromView(event->locationInWindow(), nullptr);
    const double itemH = 18.0;
    const int next = drawing::dropdownHitIndex(
        pt,
        drawing::makeRect(kToolboxControlX, menuY(), menuW(),
                          itemH * _menuItemCount),
        itemH, _menuItemCount);
    if (next != _hoverMenuItem) {
      _hoverMenuItem = next;
      setNeedsDisplay(true);
    }
  }
  void markStateDirty() {
    if (p.host && p.host->get_extension) {
      auto *state = static_cast<const clap_host_state_t *>(
          p.host->get_extension(p.host, CLAP_EXT_STATE));
      if (state && state->mark_dirty)
        state->mark_dirty(p.host);
    }
  }
  bool handleTitleClick(drawing::Point pt,
                        const s3g::gui_layout::EncoderTitleBand &b) {
    if (drawing::contains(pt, drawing::cocoaRect(b.presetMenu))) {
      auto initial = s3g::AmbiCloudEncoderParams{};
      initial.activeInputs = guiParams.activeInputs;
      initial.order = guiParams.order;
      initial.outputGainDb = guiParams.outputGainDb;
      guiEncoder.setParams(initial);
      initial = guiEncoder.params();
      std::vector<std::pair<clap_id, double>> values;
      for (auto i : infos)
        if (i.id < 100)
          values.emplace_back(i.id, readEncoderScalar(initial, i.id));
      end();
      if (batch(values))
        presetName = "INIT";
      markStateDirty();
    } else if (drawing::contains(pt, drawing::cocoaRect(b.loadButton)))
      presetDialog(false);
    else if (drawing::contains(pt, drawing::cocoaRect(b.saveButton)))
      presetDialog(true);
    else
      return false;
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                  presetName.c_str());
    service();
    invalid();
    return true;
  }
};
} // namespace input_encoder_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new input_encoder_canvas::Editor(p);
}
#include "s3g_clap_canvas_gui.inc"
