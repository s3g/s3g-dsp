// Literal Path Cocoa field, layout and pointer algorithms.
namespace input_encoder_canvas {
namespace drawing = s3g::portable_gui::input_encoder_drawing;
namespace foundation = s3g::portable_gui::foundation;
using namespace VSTGUI;
using namespace s3g::portable_gui::routing;
using Kind = s3g::clap_gui::ParamEventKind;
constexpr const char *pluginName = "Ambi Path Encoder";
constexpr const char *pluginTitle = "s3g AMBI ENCODER PATH";
constexpr clap_id preservedOutput = kOutputParamId;
constexpr const auto &stateExtension = stateExt;
struct Model {
  s3g::AmbiPathEncoderParams params{};
  s3g::AmbiPathEncoder encoder;
};
double routingReadValue(const Plugin &p, clap_id id) {
  return readEncoderValue(p, id);
}
void routingPublish(Plugin &p,
                    const std::vector<std::pair<clap_id, double>> &values) {
  Model model;
  model.params = snapshotEncoderParams(p);
  model.encoder.setParams(model.params);
  model.params = model.encoder.params();
  for (auto [id, v] : values) {
    rawEncoderApplyParam(model, id, v);
    p.encoderValues[id].store(readEncoderScalar(model.params, id));
    if (id == kInputsParamId)
      p.encoderValues[kSelectedSourceParamId].store(
          model.params.selectedSource + 1u);
    if (id == kPathsParamId)
      p.encoderValues[kSelectedPathParamId].store(model.params.selectedPath +
                                                  1u);
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
constexpr s3g::gui_layout::Panel kOutputSourcePanel{
    s3g::gui_layout::PluginClass::ProceduralEncoder,
    s3g::gui_layout::PanelRole::Output,
    {630.0, 42.0, 250.0, 326.0},
    36.0,
    26.0,
    11u,
};
static_assert(s3g::gui_layout::combinedOutputSourceCardinalityControlMatches(
    kOutputSourcePanel, s3g::gui_layout::SharedControlRole::SourceCardinality));
constexpr double kInputCountRowY = static_cast<double>(s3g::gui_layout::rowY(
    kOutputSourcePanel,
    s3g::gui_layout::combinedOutputSourceCardinalityRow(
        s3g::gui_layout::SharedControlRole::SourceCardinality)));

s3g::AmbiPath pathFromXYNumbers(const std::vector<double> &nums) {
  s3g::AmbiPath path{};
  if (nums.size() < 4)
    return path;
  double minX = nums[0], maxX = nums[0], minY = nums[1], maxY = nums[1];
  for (size_t i = 0; i + 1 < nums.size(); i += 2) {
    minX = std::min(minX, nums[i]);
    maxX = std::max(maxX, nums[i]);
    minY = std::min(minY, nums[i + 1]);
    maxY = std::max(maxY, nums[i + 1]);
  }
  const double cx = (minX + maxX) * 0.5;
  const double cy = (minY + maxY) * 0.5;
  const double scale = std::max(1.0, std::max(maxX - minX, maxY - minY) * 0.5);
  const uint32_t count = std::min<uint32_t>(
      s3g::kAmbiPathEncoderMaxPoints, static_cast<uint32_t>(nums.size() / 2));
  path.pointCount = count;
  for (uint32_t i = 0; i < count; ++i) {
    const double x = nums[i * 2u];
    const double y = nums[i * 2u + 1u];
    path.points[i].x = static_cast<float>((x - cx) / scale);
    path.points[i].y = static_cast<float>(-(y - cy) / scale);
    path.points[i].z = 0.0f;
    path.points[i].time =
        count > 1u ? static_cast<float>(i) / static_cast<float>(count - 1u)
                   : 0.0f;
  }
  return path;
}
float linearToSrgb(float v) {
  const float x = std::clamp(v, 0.0f, 1.0f);
  return x <= 0.0031308f ? x * 12.92f
                         : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}
VSTGUI::CColor pathPointColorFromXyz(float x, float y, float z, bool selected,
                                     bool activePath) {
  const float distance = std::sqrt(x * x + y * y + z * z);
  if (distance < 0.0001f)
    return drawing::calibratedColor(0x707070, activePath ? 0.85 : 0.42);
  const float azDeg = std::atan2(y, x) * 180.0f / static_cast<float>(M_PI);
  const float elDeg =
      std::asin(std::clamp(z / std::max(0.0001f, distance), -1.0f, 1.0f)) *
      180.0f / static_cast<float>(M_PI);
  const float hue = std::fmod((azDeg / 360.0f) + 1.0f, 1.0f);
  const float light = std::clamp(
      (std::clamp(elDeg, -90.0f, 90.0f) + 90.0f) / 180.0f, 0.28f, 0.88f);
  const float chroma = std::clamp(distance / 2.4f, 0.08f, 1.0f) * 0.37f;
  const float a = std::cos(hue * 2.0f * static_cast<float>(M_PI)) * chroma;
  const float b = std::sin(hue * 2.0f * static_cast<float>(M_PI)) * chroma;
  const float l3 = light + 0.3963377774f * a + 0.2158037573f * b;
  const float m3 = light - 0.1055613458f * a - 0.0638541728f * b;
  const float s3 = light - 0.0894841775f * a - 1.2914855480f * b;
  const float l = l3 * l3 * l3;
  const float m = m3 * m3 * m3;
  const float s = s3 * s3 * s3;
  float r =
      linearToSrgb(4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s);
  float g =
      linearToSrgb(-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s);
  float bl =
      linearToSrgb(-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s);
  const float grayMix = selected ? 0.05f : 0.14f;
  r = r * (1.0f - grayMix) + 0.72f * grayMix;
  g = g * (1.0f - grayMix) + 0.72f * grayMix;
  bl = bl * (1.0f - grayMix) + 0.72f * grayMix;
  return drawing::calibratedRGB(r, g, bl,
                                selected ? 1.0 : (activePath ? 0.9 : 0.45));
}
VSTGUI::CColor sourceMarkerColor(uint32_t source, bool selected) {
  static constexpr int kPalette[]{
      0x00e5ff, 0xfff000, 0xff4fd8, 0x00ff7a, 0xff5a36, 0x7c6cff,
      0x00b7ff, 0xc8ff00, 0xff9f1c, 0x45ffdd, 0xff3f7f, 0xb7ff5a,
  };
  const int rgb = kPalette[source % (sizeof(kPalette) / sizeof(kPalette[0]))];
  return drawing::calibratedColor(rgb, selected ? 1.0 : 0.92);
}
class Editor final : public ParameterEditor {
public:
  explicit Editor(Plugin &instance) : ParameterEditor(instance) {
    auto *plugin = &p;
    _plugin = plugin;
    _dragParam = 0;
    _openMenu = 0;
    _hoverMenuItem = -1;
    _menuItemCount = 0;
    _viewMode = plugin ? plugin->guiViewMode : 0;
    _viewAzDeg = plugin ? plugin->guiViewAzDeg : 90.0;
    _viewElDeg = plugin ? plugin->guiViewElDeg : 0.0;
    _viewZoom = plugin ? plugin->guiViewZoom : 1.0;
    _dragView = false;
    _dragPoint = false;
    _dragRandomDev = false;
    _dragRandomPoints = false;
    _editMode = false;
    _randomDev = 0.45;
    _randomPoints = 8;
    _randomState = 0x5137a11du;
    _selectedPoint = -1;
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "CURRENT");

    setWantsFocus(true);
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
    _dragPoint = false;
    _dragRandomDev = false;
    _dragRandomPoints = false;
  }
  double refreshPeriodMilliseconds() const override { return 1000. / 60.; }
  void service() override {
    _viewMode = p.guiViewMode;
    _viewAzDeg = p.guiViewAzDeg;
    _viewElDeg = p.guiViewElDeg;
    _viewZoom = p.guiViewZoom;
    guiParams = snapshotEncoderParams(p);
    guiEncoder.setParams(guiParams);
    guiEncoder.setPaths(p.guiPaths);
    guiParams = guiEncoder.params();
    for (unsigned i = 0; i < kInputChannels; ++i)
      sourcePositions[i] = guiEncoder.sourcePositionForDisplay(i);
    if (p.encoderProcessing.load())
      p.encoderDisplay.read(
          [&](const auto &points) { sourcePositions = points; });
  }
  void preparePresetSave(Plugin &candidate) override {
    publishEncoderParams(candidate, snapshotEncoderParams(p));
    publishEncoderPaths(candidate, p.guiPaths);
    candidate.guiViewMode = p.guiViewMode;
    candidate.guiViewAzDeg = p.guiViewAzDeg;
    candidate.guiViewElDeg = p.guiViewElDeg;
    candidate.guiViewZoom = p.guiViewZoom;
  }
  void finishPresetLoad(const Plugin &candidate) override {
    auto params = snapshotEncoderParams(candidate);
    params.outputGainDb = value(preservedOutput);
    publishEncoderParams(p, params);
    publishEncoderPaths(p, candidate.guiPaths);
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
    if (getFrame())
      getFrame()->setFocusView(this);
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
  s3g::AmbiPathEncoderParams guiParams{};
  s3g::AmbiPathEncoder guiEncoder;
  std::array<s3g::Vec3, kInputChannels> sourcePositions{};
  std::mt19937 randomGenerator{std::random_device{}()};

  void setEditorPaths(const EncoderPaths &paths) {
    guiEncoder.setPaths(paths);
    publishEncoderPaths(p, guiEncoder.paths());
    markStateDirty();
    invalid();
  }
  void setEditorPath(unsigned index, const s3g::AmbiPath &path) {
    guiEncoder.setPath(index, path);
    publishEncoderPaths(p, guiEncoder.paths());
    markStateDirty();
    invalid();
  }
  void onKeyboardEvent(KeyboardEvent &e) override {
    if (e.type == EventType::KeyDown &&
        (e.virt == VirtualKey::Delete || e.virt == VirtualKey::Back)) {
      deleteSelectedPoint();
      e.consumed = true;
    }
  }

  Plugin *_plugin;
  int _dragParam;
  int _openMenu;
  int _hoverMenuItem;
  uint32_t _menuItemCount;
  int _viewMode;
  double _viewAzDeg;
  double _viewElDeg;
  double _viewZoom;
  bool _dragView;
  bool _dragPoint;
  bool _dragRandomDev;
  bool _dragRandomPoints;
  bool _editMode;
  double _randomDev;
  uint32_t _randomPoints;
  uint32_t _randomState;
  drawing::Point _lastDragPoint;
  int _selectedPoint;
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
  drawing::Point project_inRect(s3g::Vec3 p, drawing::Rect rect) {
    const double scale = std::min(rect.size.width, rect.size.height) * 0.38 *
                         std::clamp(_viewZoom, 0.55, 2.4);
    const float az = static_cast<float>(_viewAzDeg * M_PI / 180.0);
    const float el = static_cast<float>(_viewElDeg * M_PI / 180.0);
    const float ca = std::cos(az), sa = std::sin(az);
    const float ce = std::cos(el), se = std::sin(el);
    const float x1 = ca * p.x - sa * p.y;
    const float y1 = sa * p.x + ca * p.y;
    const float y2 = ce * y1 + se * p.z;
    return drawing::makePoint(drawing::midX(rect) + x1 * scale,
                              drawing::midY(rect) - y2 * scale);
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
      _viewAzDeg = 38.0;
      _viewElDeg = 32.0;
    }
    storeViewState();
    setNeedsDisplay(true);
  }
  drawing::Rect fieldPanelRect() { return drawing::makeRect(18, 42, 596, 732); }
  drawing::Rect fieldRect() { return drawing::makeRect(34, 76, 564, 638); }
  drawing::Rect fileButtonRect(int index) {
    const double w = 126.0;
    const double gap = 10.0;
    const double x = 48.0 + static_cast<double>(index) * (w + gap);
    return drawing::makeRect(x, 736.0, w, 22.0);
  }
  drawing::Rect viewButtonRect_inRect(int index, drawing::Rect rect) {
    const double w = 38.0;
    const double gap = 6.0;
    const double start = drawing::maxX(zoomButtonRect_inRect(1, rect)) + 12.0;
    return drawing::makeRect(start + static_cast<double>(index) * (w + gap),
                             rect.origin.y + 3.0, w, 16.0);
  }
  drawing::Rect editButtonRect_inRect(int index, drawing::Rect rect) {
    const double w = 48.0;
    const double gap = 6.0;
    const double start = rect.origin.x + 104.0;
    return drawing::makeRect(start + static_cast<double>(index) * (w + gap),
                             rect.origin.y + 3.0, w, 16.0);
  }
  drawing::Rect zoomButtonRect_inRect(int index, drawing::Rect rect) {
    const double w = 22.0;
    const double gap = 6.0;
    const double viewTotal = 3.0 * 38.0 + 2.0 * 6.0;
    const double zoomTotal = 2.0 * w + gap;
    const double viewStart = drawing::maxX(rect) - 10.0 - viewTotal;
    const double start = viewStart - 12.0 - zoomTotal;
    return drawing::makeRect(start + static_cast<double>(index) * (w + gap),
                             rect.origin.y + 3.0, w, 16.0);
  }
  void drawViewButtonsInRect_attrs_style(drawing::Rect rect,
                                         drawing::Attrs attrs,
                                         const drawing::Style &style) {
    static drawing::String labels[] = {"TOP", "SIDE", "3/4"};
    for (int i = 0; i < 3; ++i)
      drawing::drawToolboxHeaderButton(viewButtonRect_inRect(i, rect), rect,
                                       labels[i], i == _viewMode, attrs, style);
    drawing::drawToolboxHeaderButton(editButtonRect_inRect(0, rect), rect,
                                     "EDIT", _editMode, attrs, style);
    drawing::drawToolboxHeaderButton(editButtonRect_inRect(1, rect), rect,
                                     "PLAY", !_editMode, attrs, style);
  }
  void drawZoomButtonsInRect_attrs_style(drawing::Rect rect,
                                         drawing::Attrs attrs,
                                         const drawing::Style &style) {
    static drawing::String labels[] = {"-", "+"};
    for (int i = 0; i < 2; ++i)
      drawing::drawToolboxHeaderButton(zoomButtonRect_inRect(i, rect), rect,
                                       labels[i], false, attrs, style);
  }
  drawing::String displayValueForParam_value(clap_id param, double value) {
    char text[64]{};
    paramsValueToText(&_plugin->plugin, param, value, text, sizeof(text));
    return drawing::stringFromUtf8(text);
  }
  void drawSlider_param_value_min_max_y_attrs_style(
      drawing::String name, clap_id param, double value, double min, double max,
      double y, drawing::Attrs attrs, const drawing::Style &style) {
    double norm = static_cast<double>(
        std::clamp((value - min) / std::max(0.000001, max - min), 0.0, 1.0));
    if (param == kRateParamId || param == kDivisionParamId) {
      const double safeMin = std::max(0.000001, min);
      norm = static_cast<double>(
          std::clamp(std::log(std::max(safeMin, value) / safeMin) /
                         std::log(max / safeMin),
                     0.0, 1.0));
    }
    // Use the free space at the right of this original value column so Fira
    // Code can retain fractional rates and the numeric part of beat divisions.
    drawing::drawSlider(name, displayValueForParam_value(param, value), norm, y,
                        attrs, drawing::softValueAttrs(), style, 646.0, 738.0,
                        826.0, 82.0, 50.0);
  }
  void drawMenu_value_y_attrs_style(drawing::String name, drawing::String value,
                                    double y, drawing::Attrs attrs,
                                    const drawing::Style &style) {
    drawing::drawMenu(name, value, y, attrs, drawing::softValueAttrs(), style,
                      646.0, 738.0, 126.0);
  }
  void drawActionButton_rect_attrs_style(drawing::String label,
                                         drawing::Rect rect,
                                         drawing::Attrs attrs,
                                         const drawing::Style &style) {
    drawing::setFill(drawing::calibratedColor(0x202020));
    drawing::rectFill(rect);
    drawing::setStroke(drawing::calibratedColor(0xb8b8b8));
    drawing::frameRect(rect);
    drawing::setStroke(drawing::calibratedColor(0x343434));
    drawing::frameRect(drawing::inset(rect, 1.0, 1.0));
    const drawing::Size size = drawing::sizeWithAttributes(label, attrs);
    drawing::drawAtPoint_withAttributes(
        label,
        drawing::makePoint(rect.origin.x + (rect.size.width - size.width) * 0.5,
                           rect.origin.y +
                               (rect.size.height - size.height) * 0.5 - 1.0),
        attrs);
    (void)style;
  }
  drawing::Rect randomButtonRect() {
    return drawing::makeRect(642, 264, 104, 22);
  }
  drawing::Rect deleteButtonRect() {
    return drawing::makeRect(758, 264, 104, 22);
  }
  drawing::Rect clearButtonRect() {
    return drawing::makeRect(642, 294, 104, 22);
  }
  drawing::Rect randomDevSliderRect() {
    return drawing::makeRect(738, 325, 82, 12);
  }
  drawing::Rect randomPointsSliderRect() {
    return drawing::makeRect(738, 349, 82, 12);
  }
  float randomUnit() {
    _randomState = _randomState * 1664525u + 1013904223u;
    return static_cast<float>((_randomState >> 8) & 0x00ffffffu) / 16777215.0f;
  }
  float randomSigned() { return randomUnit() * 2.0f - 1.0f; }
  void randomizePaths() {
    auto paths = guiEncoder.paths();
    const uint32_t active = std::clamp<uint32_t>(guiParams.activePaths, 1u,
                                                 s3g::kAmbiPathEncoderMaxPaths);
    const uint32_t count =
        std::clamp<uint32_t>(_randomPoints, 2u, s3g::kAmbiPathEncoderMaxPoints);
    const float dev = static_cast<float>(std::clamp(
        _randomDev, static_cast<double>(0.0), static_cast<double>(1.0)));
    for (uint32_t pi = 0; pi < active; ++pi) {
      auto &path = paths[pi];
      path.pointCount = count;
      const float pathTurn = static_cast<float>(pi) * 2.39996323f;
      const float baseRadius = 0.55f + 0.34f * randomUnit();
      const float swirl = 1.0f + std::floor(randomUnit() * 3.0f);
      const float elevBase = randomSigned() * 0.18f * dev;
      for (uint32_t i = 0; i < count; ++i) {
        const float t = count > 1u
                            ? static_cast<float>(i) / static_cast<float>(count)
                            : 0.0f;
        const float wander = randomSigned() * dev;
        const float az = t * 2.0f * static_cast<float>(M_PI) + pathTurn +
                         wander * (0.18f + dev * 0.75f);
        const float radius =
            std::clamp(baseRadius + randomSigned() * dev * 0.46f, 0.08f, 1.65f);
        const float elevWave =
            std::sin(t * 2.0f * static_cast<float>(M_PI) * swirl + pathTurn) *
            (0.10f + dev * 0.42f);
        path.points[i].x = std::cos(az) * radius;
        path.points[i].y = std::sin(az) * radius;
        path.points[i].z = std::clamp(
            elevBase + elevWave + randomSigned() * dev * 0.34f, -1.35f, 1.35f);
        path.points[i].time =
            count > 1u ? static_cast<float>(i) / static_cast<float>(count - 1u)
                       : 0.0f;
      }
    }
    _selectedPoint = -1;
    setEditorPaths(paths);
    setNeedsDisplay(true);
  }
  void loadDocumentationPaths() {
    // Keep the publication scene visibly different from the radial default:
    // four asymmetric, crossing gestures with distinct extents and folds.
    static constexpr float points[4][12][3]{
        {
            {-1.12f, -0.62f, -0.55f},
            {-0.44f, 0.88f, 0.46f},
            {0.72f, -0.38f, 0.92f},
            {0.12f, 0.18f, -0.88f},
            {-0.86f, 0.58f, 0.72f},
            {1.02f, 0.74f, -0.18f},
            {0.34f, -0.94f, -0.70f},
            {-0.58f, -0.08f, 1.02f},
            {0.94f, -0.70f, 0.32f},
            {-0.20f, 0.96f, -0.62f},
            {-1.02f, 0.04f, 0.08f},
            {0.22f, -0.44f, 0.66f},
        },
        {
            {-0.90f, 0.72f, -0.86f},
            {-0.14f, -0.22f, 1.12f},
            {0.88f, 0.42f, 0.28f},
            {0.38f, -1.04f, -0.32f},
            {-0.72f, -0.46f, 0.82f},
            {0.62f, 0.92f, -0.58f},
            {1.08f, -0.12f, 0.74f},
            {-0.34f, 0.28f, -1.02f},
            {-1.10f, -0.78f, 0.24f},
            {0.18f, -0.72f, 0.98f},
            {0.74f, 0.10f, -0.92f},
            {-0.54f, 1.02f, 0.12f},
        },
        {
            {-1.04f, 0.18f, 0.94f},
            {0.04f, -0.94f, -0.18f},
            {0.98f, 0.18f, -0.76f},
            {-0.16f, 0.66f, 1.06f},
            {-0.74f, -0.72f, -0.58f},
            {0.56f, -0.18f, 0.62f},
            {0.18f, 1.04f, -0.40f},
            {-0.92f, 0.46f, 0.20f},
            {0.82f, -0.82f, 0.42f},
            {1.10f, 0.62f, 0.06f},
            {-0.36f, -0.16f, -1.08f},
            {-0.62f, 0.92f, 0.64f},
        },
        {
            {-0.76f, -0.96f, 0.76f},
            {0.52f, 0.06f, -1.08f},
            {0.96f, 0.86f, 0.52f},
            {-0.46f, 0.36f, -0.64f},
            {-1.08f, 0.82f, 0.06f},
            {0.28f, -0.62f, 1.08f},
            {1.06f, -0.42f, -0.46f},
            {-0.18f, 1.04f, 0.78f},
            {-0.94f, -0.24f, -0.92f},
            {0.66f, 0.54f, -0.10f},
            {0.08f, -1.06f, 0.38f},
            {-0.58f, 0.06f, 1.02f},
        },
    };
    static constexpr float times[12]{
        0.00f, 0.05f, 0.13f, 0.19f, 0.31f, 0.40f,
        0.54f, 0.61f, 0.73f, 0.81f, 0.93f, 1.00f,
    };
    auto paths = guiEncoder.paths();
    for (uint32_t pi = 0u; pi < 4u; ++pi) {
      paths[pi].pointCount = 12u;
      for (uint32_t i = 0u; i < 12u; ++i) {
        paths[pi].points[i].x = points[pi][i][0];
        paths[pi].points[i].y = points[pi][i][1];
        paths[pi].points[i].z = points[pi][i][2];
        paths[pi].points[i].time = times[i];
      }
    }
    _selectedPoint = -1;
    setEditorPaths(paths);
    setNeedsDisplay(true);
  }
  void updateRandomDev(drawing::Point pt) {
    const drawing::Rect r = randomDevSliderRect();
    _randomDev =
        std::clamp<double>((pt.x - r.origin.x) / r.size.width, 0.0, 1.0);
    setNeedsDisplay(true);
  }
  void updateRandomPoints(drawing::Point pt) {
    const drawing::Rect r = randomPointsSliderRect();
    const double n =
        std::clamp<double>((pt.x - r.origin.x) / r.size.width, 0.0, 1.0);
    _randomPoints = std::clamp<uint32_t>(
        static_cast<uint32_t>(std::lround(2.0 + n * 62.0)), 2u, 64u);
    setNeedsDisplay(true);
  }
  double menuRowY(int menu) {
    switch (menu) {
    case 1:
      return 104.0;
    case 2:
      return 234.0;
    case 3:
      return 416.0;
    case 4:
      return 438.0;
    case 5:
      return 482.0;
    case 6:
      return 504.0;
    default:
      return 0.0;
    }
  }
  double menuY() { return menuRowY(_openMenu) + 17.0; }
  double menuW() { return 126.0; }
  void drawOpenMenu_style(drawing::Attrs attrs, const drawing::Style &style) {
    if (_openMenu <= 0 || _menuItemCount == 0u)
      return;
    static drawing::String orderItems[] = {"1OA", "2OA", "3OA", "4OA",
                                           "5OA", "6OA", "7OA"};
    static drawing::String assignItems[] = {"ONE", "ROUND", "SOURCE"};
    static drawing::String playbackItems[] = {"OFF", "RUN", "SCRUB"};
    static drawing::String syncItems[] = {"FREE", "SYNC"};
    static drawing::String loopItems[] = {"ONE", "LOOP", "PAL"};
    static drawing::String interpItems[] = {"LINEAR", "CATMULL", "HOLD"};
    drawing::String const *items = orderItems;
    int selected = 0;
    if (_openMenu == 1) {
      items = orderItems;
      selected = static_cast<int>(guiParams.order) - 1;
      _menuItemCount = 7;
    } else if (_openMenu == 2) {
      items = assignItems;
      selected = static_cast<int>(static_cast<uint32_t>(guiParams.assignMode));
      _menuItemCount = 3;
    } else if (_openMenu == 3) {
      items = playbackItems;
      selected = static_cast<int>(static_cast<uint32_t>(guiParams.playback));
      _menuItemCount = 3;
    } else if (_openMenu == 4) {
      items = syncItems;
      selected = static_cast<int>(static_cast<uint32_t>(guiParams.syncMode));
      _menuItemCount = 2;
    } else if (_openMenu == 5) {
      items = loopItems;
      selected = static_cast<int>(static_cast<uint32_t>(guiParams.loopMode));
      _menuItemCount = 3;
    } else if (_openMenu == 6) {
      items = interpItems;
      selected =
          static_cast<int>(static_cast<uint32_t>(guiParams.interpolation));
      _menuItemCount = 3;
    }
    const double itemH = 18.0;
    drawing::drawDropdownMenu(
        drawing::makeRect(738, menuY(), menuW(), itemH * _menuItemCount), itemH,
        items, _menuItemCount, selected, _hoverMenuItem, attrs, style);
  }
  s3g::AmbiPathPoint
  pointFromScreen_inRect_preserving(drawing::Point pt, drawing::Rect rect,
                                    s3g::AmbiPathPoint oldPoint) {
    const double scale = std::min(rect.size.width, rect.size.height) * 0.38 *
                         std::clamp(_viewZoom, 0.55, 2.4);
    const double x1 = std::clamp(
        (pt.x - drawing::midX(rect)) / std::max<double>(1.0, scale), -2.0, 2.0);
    const double y2 = std::clamp(
        (drawing::midY(rect) - pt.y) / std::max<double>(1.0, scale), -2.0, 2.0);
    const double az = _viewAzDeg * M_PI / 180.0;
    const double el = _viewElDeg * M_PI / 180.0;
    const double ca = std::cos(az), sa = std::sin(az);
    const double ce = std::cos(el), se = std::sin(el);
    const double oldY1 = sa * oldPoint.x + ca * oldPoint.y;
    const double oldZ2 = -se * oldY1 + ce * oldPoint.z;
    const double y1 = ce * y2 - se * oldZ2;
    const double z = se * y2 + ce * oldZ2;
    oldPoint.x = static_cast<float>(std::clamp(ca * x1 + sa * y1, -2.0, 2.0));
    oldPoint.y = static_cast<float>(std::clamp(-sa * x1 + ca * y1, -2.0, 2.0));
    oldPoint.z = static_cast<float>(std::clamp(z, -2.0, 2.0));
    return oldPoint;
  }
  int hitPathPoint_inRect(drawing::Point pt, drawing::Rect rect) {
    const auto paths = guiEncoder.paths();
    const auto p = guiParams;
    const auto &path = paths[p.selectedPath];
    int hit = -1;
    double best = 9.0;
    for (uint32_t i = 0; i < path.pointCount; ++i) {
      const auto &pp = path.points[i];
      const s3g::Vec3 v{pp.x, pp.y, pp.z};
      const drawing::Point sp = project_inRect(v, rect);
      const double d = std::hypot(sp.x - pt.x, sp.y - pt.y);
      if (d < best) {
        best = d;
        hit = static_cast<int>(i);
      }
    }
    return hit;
  }
  void updateSelectedPointFromScreen(drawing::Point pt) {
    if (_selectedPoint < 0)
      return;
    auto paths = guiEncoder.paths();
    const uint32_t pathIndex = guiParams.selectedPath;
    auto &path = paths[pathIndex];
    if (static_cast<uint32_t>(_selectedPoint) >= path.pointCount)
      return;
    path.points[_selectedPoint] = pointFromScreen_inRect_preserving(
        pt, fieldRect(), path.points[_selectedPoint]);
    path.points[_selectedPoint].time =
        path.pointCount > 1u ? static_cast<float>(_selectedPoint) /
                                   static_cast<float>(path.pointCount - 1u)
                             : 0.0f;
    setEditorPath(pathIndex, path);
    setNeedsDisplay(true);
  }
  void retimePath(s3g::AmbiPath &path) {
    for (uint32_t i = 0; i < path.pointCount; ++i)
      path.points[i].time =
          path.pointCount > 1u
              ? static_cast<float>(i) / static_cast<float>(path.pointCount - 1u)
              : 0.0f;
  }
  void addPointAtScreen(drawing::Point pt) {
    auto paths = guiEncoder.paths();
    const uint32_t pathIndex = guiParams.selectedPath;
    auto &path = paths[pathIndex];
    if (path.pointCount >= s3g::kAmbiPathEncoderMaxPoints)
      return;
    s3g::AmbiPathPoint seed{};
    if (path.pointCount > 0u)
      seed = path.points[path.pointCount - 1u];
    path.points[path.pointCount] =
        pointFromScreen_inRect_preserving(pt, fieldRect(), seed);
    ++path.pointCount;
    retimePath(path);
    _selectedPoint = static_cast<int>(path.pointCount - 1u);
    setEditorPath(pathIndex, path);
  }
  void deleteSelectedPoint() {
    if (!_editMode)
      return;
    if (_selectedPoint < 0)
      return;
    auto paths = guiEncoder.paths();
    const uint32_t pathIndex = guiParams.selectedPath;
    auto &path = paths[pathIndex];
    if (static_cast<uint32_t>(_selectedPoint) >= path.pointCount)
      return;
    for (uint32_t i = static_cast<uint32_t>(_selectedPoint);
         i + 1u < path.pointCount; ++i)
      path.points[i] = path.points[i + 1u];
    if (path.pointCount > 0u)
      --path.pointCount;
    retimePath(path);
    _selectedPoint =
        path.pointCount == 0u
            ? -1
            : static_cast<int>(std::min<uint32_t>(
                  static_cast<uint32_t>(_selectedPoint), path.pointCount - 1u));
    setEditorPath(pathIndex, path);
    setNeedsDisplay(true);
  }
  void clearSelectedPath() {
    if (!_editMode)
      return;
    auto paths = guiEncoder.paths();
    const uint32_t pathIndex = guiParams.selectedPath;
    paths[pathIndex].pointCount = 0;
    _selectedPoint = -1;
    setEditorPath(pathIndex, paths[pathIndex]);
    setNeedsDisplay(true);
  }
  void drawField_attrs_style(drawing::Rect rect, drawing::Attrs attrs,
                             const drawing::Style &style) {
    drawing::setFill(drawing::calibratedColor(0x111111));
    drawing::rectFill(rect);
    drawing::setStroke(style.grid);
    drawing::frameRect(rect);
    const auto paths = guiEncoder.paths();
    const auto params = guiParams;
    for (uint32_t pi = 0; pi < params.activePaths; ++pi) {
      const auto &path = paths[pi];
      if (path.pointCount == 0u)
        continue;
      const bool pathSelected = _editMode && pi == params.selectedPath;
      if (path.pointCount >= 2u) {
        drawing::Path bez = drawing::Path::bezierPath();
        (bez).setLineWidth(pathSelected ? 1.4 : 0.7);
        for (uint32_t i = 0; i < path.pointCount; ++i) {
          const auto &pp = path.points[i];
          const s3g::Vec3 v{pp.x, pp.y, pp.z};
          drawing::Point pt = project_inRect(v, rect);
          if (i == 0)
            (bez).moveToPoint(pt);
          else
            (bez).lineToPoint(pt);
        }
        const auto &p0 = path.points[0];
        const s3g::Vec3 v0{p0.x, p0.y, p0.z};
        (bez).lineToPoint(project_inRect(v0, rect));
        drawing::setStroke(drawing::calibratedWhite(
            (pathSelected ? 0.78 : 0.42), (pathSelected ? 0.90 : 0.45)));
        (bez).stroke();
      }
      for (uint32_t i = 0; i < path.pointCount; ++i) {
        const auto &pp = path.points[i];
        const s3g::Vec3 v{pp.x, pp.y, pp.z};
        drawing::Point pt = project_inRect(v, rect);
        const bool selected = _editMode && pi == params.selectedPath &&
                              static_cast<int>(i) == _selectedPoint;
        drawing::setFill(
            pathPointColorFromXyz(pp.x, pp.y, pp.z, selected, pathSelected));
        const double r = selected ? 5.0 : 3.5;
        drawing::Rect box = drawing::makeRect(
            std::round(pt.x - r), std::round(pt.y - r), r * 2.0, r * 2.0);
        drawing::rectFill(box);
        if (selected) {
          drawing::setStroke(drawing::calibratedWhite(0.05, 0.95));
          drawing::frameRect(box);
          drawing::setStroke(style.text);
          drawing::frameRect(drawing::inset(box, -2.0, -2.0));
        }
      }
    }
    const uint32_t active =
        std::min<uint32_t>(params.activeInputs, s3g::kAmbiPathEncoderMaxInputs);
    for (uint32_t src = 0; src < active; ++src) {
      s3g::Vec3 pos = sourcePositions[src];
      drawing::Point pt = project_inRect(pos, rect);
      const bool selected = src == params.selectedSource;
      const double r = selected ? 5.0 : 4.0;
      const drawing::Rect outer = drawing::makeRect(
          std::round(pt.x - r - 1.0), std::round(pt.y - r - 1.0),
          (r + 1.0) * 2.0, (r + 1.0) * 2.0);
      drawing::setFill(drawing::calibratedWhite(0.02, 0.95));
      drawing::rectFill(outer);
      drawing::setFill(sourceMarkerColor(src, selected));
      drawing::rectFill(drawing::makeRect(
          std::round(pt.x - r), std::round(pt.y - r), r * 2.0, r * 2.0));
      drawing::setStroke(drawing::calibratedWhite(selected ? 0.96 : 0.12,
                                                  selected ? 0.95 : 0.75));
      drawing::frameRect(drawing::makeRect(
          std::round(pt.x - r), std::round(pt.y - r), r * 2.0, r * 2.0));
    }
    drawing::String viewText =
        _editMode
            ? (_viewMode == 0 ? "EDIT TOP: CLICK/DRAG POINTS"
                              : (_viewMode == 1 ? "EDIT SIDE: CLICK/DRAG POINTS"
                                                : "EDIT: CLICK/DRAG POINTS"))
            : "PLAY: CLICK/DRAG CAMERA";
    drawing::drawAtPoint_withAttributes(
        viewText,
        drawing::makePoint(rect.origin.x + 10, drawing::maxY(rect) - 22),
        attrs);
  }
  void paint() override {
    drawing::DrawingScope scope(*this, context, font, titleFont, &error);
    service();

    drawing::Style style = drawing::softTextStyle();
    drawing::setFill(style.bg);
    drawing::rectFill(bounds());
    drawing::Attrs attrs = drawing::softLabelAttrs();
    drawing::Attrs dim = drawing::softValueAttrs();
    drawing::Attrs titleAttrs = drawing::softTitleAttrs();

    const float pk = _plugin->outputPeak.load(std::memory_order_relaxed);
    drawing::drawEncoderTitleBand(
        "s3g AMBI ENCODER PATH", drawing::stringFromUtf8(_titlePresetName),
        drawing::peakDbText(pk), drawing::encoderTitleBand(900.0, 792.0),
        titleAttrs, attrs, dim, style);

    const drawing::Rect fieldPanel = fieldPanelRect();
    drawing::drawPanelFrame(fieldPanel.origin.x, fieldPanel.origin.y,
                            fieldPanel.size.width, fieldPanel.size.height,
                            style);
    drawing::drawPanelHeader("PATH FIELD", true, fieldPanel.origin.x,
                             fieldPanel.origin.y, fieldPanel.size.width, 21,
                             attrs, style);
    drawViewButtonsInRect_attrs_style(fieldPanel, dim, style);
    drawZoomButtonsInRect_attrs_style(fieldPanel, dim, style);
    drawField_attrs_style(fieldRect(), dim, style);

    drawing::drawPanelFrame(630, 42, 250, 326, style);
    drawing::drawPanelHeader("OUTPUT / INPUT", true, 630, 42, 250, 21, attrs,
                             style);
    drawing::drawPanelFrame(630, 380, 250, 322, style);
    drawing::drawPanelHeader("MOTION", true, 630, 380, 250, 21, attrs, style);

    const auto p = guiParams;
    drawSlider_param_value_min_max_y_attrs_style(
        "OUT", kOutputParamId, p.outputGainDb, -60, 12, 78, attrs, style);
    drawMenu_value_y_attrs_style(
        "ORDER", drawing::stringFormat("%uOA", p.order), 104, attrs, style);
    drawSlider_param_value_min_max_y_attrs_style("INPUTS", kInputsParamId,
                                                 p.activeInputs, 1, 64,
                                                 kInputCountRowY, attrs, style);
    drawSlider_param_value_min_max_y_attrs_style(
        "PATHS", kPathsParamId, p.activePaths, 1, 16, 156, attrs, style);
    drawSlider_param_value_min_max_y_attrs_style("PATH", kSelectedPathParamId,
                                                 p.selectedPath + 1u, 1, 16,
                                                 182, attrs, style);
    drawSlider_param_value_min_max_y_attrs_style(
        "SOURCE", kSelectedSourceParamId, p.selectedSource + 1u, 1, 64, 208,
        attrs, style);
    drawMenu_value_y_attrs_style("ASSIGN",
                                 drawing::stringFromUtf8(assignName(
                                     static_cast<uint32_t>(p.assignMode))),
                                 234, attrs, style);
    drawActionButton_rect_attrs_style("REGEN", randomButtonRect(), dim, style);
    drawActionButton_rect_attrs_style("DEL PT", deleteButtonRect(), dim, style);
    drawActionButton_rect_attrs_style("CLEAR", clearButtonRect(), dim, style);
    drawing::drawSlider("DEV",
                        drawing::stringFormat(
                            "%.0f%%", static_cast<double>(_randomDev * 100.0)),
                        _randomDev, 326, attrs, dim, style, 646.0, 738.0, 826.0,
                        82.0);
    drawing::drawSlider("PTS", drawing::stringFormat("%u", _randomPoints),
                        (static_cast<double>(_randomPoints) - 2.0) / 62.0, 350,
                        attrs, dim, style, 646.0, 738.0, 826.0, 82.0);

    drawMenu_value_y_attrs_style("PLAY",
                                 drawing::stringFromUtf8(playbackName(
                                     static_cast<uint32_t>(p.playback))),
                                 416, attrs, style);
    drawMenu_value_y_attrs_style(
        "SYNC",
        drawing::stringFromUtf8(syncName(static_cast<uint32_t>(p.syncMode))),
        438, attrs, style);
    drawSlider_param_value_min_max_y_attrs_style("DIV", kDivisionParamId,
                                                 p.syncDivisionBeats, 0.25, 64,
                                                 460, attrs, style);
    drawMenu_value_y_attrs_style(
        "MODE",
        drawing::stringFromUtf8(loopName(static_cast<uint32_t>(p.loopMode))),
        482, attrs, style);
    drawMenu_value_y_attrs_style("INTERP",
                                 drawing::stringFromUtf8(interpName(
                                     static_cast<uint32_t>(p.interpolation))),
                                 504, attrs, style);
    drawSlider_param_value_min_max_y_attrs_style("RATE", kRateParamId, p.rateHz,
                                                 0.001, 4, 530, attrs, style);
    drawSlider_param_value_min_max_y_attrs_style(
        "PHASE", kPhaseParamId, p.phase, 0, 1, 552, attrs, style);
    drawSlider_param_value_min_max_y_attrs_style(
        "SPREAD", kPhaseSpreadParamId, p.phaseSpread, 0, 1, 574, attrs, style);
    drawSlider_param_value_min_max_y_attrs_style(
        "SMOOTH", kSmoothParamId, p.smooth, 0, 0.995, 596, attrs, style);
    drawSlider_param_value_min_max_y_attrs_style("EASE", kEaseParamId, p.ease,
                                                 0, 1, 618, attrs, style);
    drawSlider_param_value_min_max_y_attrs_style("DIST", kDistanceScaleParamId,
                                                 p.distanceScale, 0.05, 8, 640,
                                                 attrs, style);
    drawSlider_param_value_min_max_y_attrs_style(
        "DOPPLER", kDopplerParamId, p.doppler, 0, 1, 662, attrs, style);
    drawSlider_param_value_min_max_y_attrs_style("AIR", kAirParamId, p.air, 0,
                                                 1, 684, attrs, style);

    drawActionButton_rect_attrs_style("LOAD JSON", fileButtonRect(0), dim,
                                      style);
    drawActionButton_rect_attrs_style("SAVE JSON", fileButtonRect(1), dim,
                                      style);
    drawActionButton_rect_attrs_style("IMPORT SVG", fileButtonRect(2), dim,
                                      style);
    drawActionButton_rect_attrs_style("EXPORT SVG", fileButtonRect(3), dim,
                                      style);
    drawOpenMenu_style(attrs, style);
  }
  void setParam_fromPoint(clap_id param, drawing::Point pt) {
    auto slider = [&](double lo, double hi) {
      const double n = std::clamp((pt.x - 738.0) / 82.0, 0.0, 1.0);
      guiApplyParam(*_plugin, param, lo + n * (hi - lo));
    };
    auto logSlider = [&](double lo, double hi) {
      const double n = std::clamp((pt.x - 738.0) / 82.0, 0.0, 1.0);
      guiApplyParam(*_plugin, param, lo * std::pow(hi / lo, n));
    };
    switch (param) {
    case kInputsParamId:
      slider(1, 64);
      break;
    case kOrderParamId:
      slider(1, 7);
      break;
    case kPathsParamId:
      slider(1, 16);
      break;
    case kSelectedPathParamId:
      slider(1, 16);
      break;
    case kSelectedSourceParamId:
      slider(1, 64);
      break;
    case kRateParamId:
      logSlider(0.001, 4.0);
      break;
    case kDivisionParamId:
      logSlider(0.25, 64.0);
      break;
    case kPhaseParamId:
      slider(0, 1);
      break;
    case kPhaseSpreadParamId:
      slider(0, 1);
      break;
    case kSmoothParamId:
      slider(0, 0.995);
      break;
    case kEaseParamId:
      slider(0, 1);
      break;
    case kDistanceScaleParamId:
      slider(0.05, 8.0);
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
    const auto titleBand = drawing::encoderTitleBand(900.0, 792.0);
    if (handleTitleClick(pt, titleBand))
      return;
    if (drawing::contains(pt, drawing::cocoaRect(titleBand.randomButton))) {
      randomizePaths();
      guiApplyParam(*_plugin, kPlaybackParamId,
                    static_cast<double>(std::uniform_int_distribution<unsigned>(
                        0, 2)(randomGenerator)));
      guiApplyParam(*_plugin, kLoopParamId,
                    static_cast<double>(std::uniform_int_distribution<unsigned>(
                        0, 2)(randomGenerator)));
      guiApplyParam(*_plugin, kRateParamId,
                    0.01 *
                        std::pow(160.0, static_cast<double>(randomGenerator()) /
                                            4294967295.0));
      guiApplyParam(*_plugin, kPhaseSpreadParamId,
                    static_cast<double>(randomGenerator()) / 4294967295.0);
      std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "RANDOM");
      setNeedsDisplay(true);
      return;
    }
    if (_openMenu > 0) {
      const double itemH = 18.0;
      const int hit = drawing::dropdownHitIndex(
          pt, drawing::makeRect(738, menuY(), menuW(), itemH * _menuItemCount),
          itemH, _menuItemCount);
      if (hit >= 0) {
        if (_openMenu == 1)
          guiApplyParam(*_plugin, kOrderParamId, hit + 1);
        else if (_openMenu == 2)
          guiApplyParam(*_plugin, kAssignParamId, hit);
        else if (_openMenu == 3)
          guiApplyParam(*_plugin, kPlaybackParamId, hit);
        else if (_openMenu == 4)
          guiApplyParam(*_plugin, kSyncParamId, hit);
        else if (_openMenu == 5)
          guiApplyParam(*_plugin, kLoopParamId, hit);
        else if (_openMenu == 6)
          guiApplyParam(*_plugin, kInterpParamId, hit);
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
    if (drawing::contains(pt, fileButtonRect(0))) {
      loadPathJson();
      return;
    }
    if (drawing::contains(pt, fileButtonRect(1))) {
      savePathJson();
      return;
    }
    if (drawing::contains(pt, fileButtonRect(2))) {
      importSvg();
      return;
    }
    if (drawing::contains(pt, fileButtonRect(3))) {
      exportSvg();
      return;
    }
    if (drawing::contains(pt, randomButtonRect())) {
      randomizePaths();
      return;
    }
    if (drawing::contains(pt, deleteButtonRect())) {
      deleteSelectedPoint();
      return;
    }
    if (drawing::contains(pt, clearButtonRect())) {
      clearSelectedPath();
      return;
    }
    if (drawing::contains(pt,
                          drawing::inset(randomDevSliderRect(), -40.0, -8.0))) {
      if (event->clickCount() >= 2) {
        _randomDev = 0.45;
        _dragRandomDev = false;
        setNeedsDisplay(true);
        return;
      }
      _dragRandomDev = true;
      updateRandomDev(pt);
      return;
    }
    if (drawing::contains(
            pt, drawing::inset(randomPointsSliderRect(), -40.0, -8.0))) {
      if (event->clickCount() >= 2) {
        _randomPoints = 8u;
        _dragRandomPoints = false;
        setNeedsDisplay(true);
        return;
      }
      _dragRandomPoints = true;
      updateRandomPoints(pt);
      return;
    }

    const drawing::Rect fieldPanel = fieldPanelRect();
    if (drawing::contains(pt, fieldPanel)) {
      for (int i = 0; i < 2; ++i) {
        if (drawing::contains(pt, zoomButtonRect_inRect(i, fieldPanel))) {
          _viewZoom =
              std::clamp(_viewZoom + (i == 0 ? -0.15 : 0.15), 0.55, 2.4);
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
      for (int i = 0; i < 2; ++i) {
        if (drawing::contains(pt, editButtonRect_inRect(i, fieldPanel))) {
          _editMode = (i == 0);
          _dragPoint = false;
          _dragView = false;
          setNeedsDisplay(true);
          return;
        }
      }
      const drawing::Rect field = fieldRect();
      if (drawing::contains(pt, field)) {
        const int hit = hitPathPoint_inRect(pt, field);
        if (hit >= 0) {
          _selectedPoint = hit;
          _dragPoint = _editMode;
          setNeedsDisplay(true);
          if (_editMode)
            return;
        }
        if (_editMode) {
          addPointAtScreen(pt);
          _dragPoint = true;
          setNeedsDisplay(true);
          return;
        }
        if (_editMode)
          return;
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
    else if (drawing::contains(pt, drawing::makeRect(738, 103, 126, 17))) {
      openMenu(1, 7);
      return;
    } else if (drawing::contains(
                   pt, drawing::makeRect(638, kInputCountRowY - 8.0, 230, 24)))
      _dragParam = kInputsParamId;
    else if (drawing::contains(pt, drawing::makeRect(638, 148, 230, 24)))
      _dragParam = kPathsParamId;
    else if (drawing::contains(pt, drawing::makeRect(638, 174, 230, 24))) {
      _selectedPoint = -1;
      _dragParam = kSelectedPathParamId;
    } else if (drawing::contains(pt, drawing::makeRect(638, 200, 230, 24)))
      _dragParam = kSelectedSourceParamId;
    else if (drawing::contains(pt, drawing::makeRect(738, 233, 126, 17))) {
      openMenu(2, 3);
      return;
    } else if (drawing::contains(pt, drawing::makeRect(738, 415, 126, 17))) {
      openMenu(3, 3);
      return;
    } else if (drawing::contains(pt, drawing::makeRect(738, 437, 126, 17))) {
      openMenu(4, 2);
      return;
    } else if (drawing::contains(pt, drawing::makeRect(638, 452, 230, 24)))
      _dragParam = kDivisionParamId;
    else if (drawing::contains(pt, drawing::makeRect(738, 481, 126, 17))) {
      openMenu(5, 3);
      return;
    } else if (drawing::contains(pt, drawing::makeRect(738, 503, 126, 17))) {
      openMenu(6, 3);
      return;
    } else if (drawing::contains(pt, drawing::makeRect(638, 522, 230, 24)))
      _dragParam = kRateParamId;
    else if (drawing::contains(pt, drawing::makeRect(638, 544, 230, 24)))
      _dragParam = kPhaseParamId;
    else if (drawing::contains(pt, drawing::makeRect(638, 566, 230, 24)))
      _dragParam = kPhaseSpreadParamId;
    else if (drawing::contains(pt, drawing::makeRect(638, 588, 230, 24)))
      _dragParam = kSmoothParamId;
    else if (drawing::contains(pt, drawing::makeRect(638, 610, 230, 24)))
      _dragParam = kEaseParamId;
    else if (drawing::contains(pt, drawing::makeRect(638, 632, 230, 24)))
      _dragParam = kDistanceScaleParamId;
    else if (drawing::contains(pt, drawing::makeRect(638, 654, 230, 24)))
      _dragParam = kDopplerParamId;
    else if (drawing::contains(pt, drawing::makeRect(638, 676, 230, 24)))
      _dragParam = kAirParamId;
    if (_dragParam) {
      double defaultValue = 0.0;
      if (drawing::sliderDoubleClickDefault(event, &_plugin->plugin, _dragParam,
                                            &defaultValue)) {
        guiApplyParam(*_plugin, _dragParam, defaultValue);
        _dragParam = 0;
        setNeedsDisplay(true);
        return;
      }
      setParam_fromPoint(_dragParam, pt);
    }
  }
  void mouseDragged(drawing::Event *event) {
    drawing::Point pt =
        convertPoint_fromView(event->locationInWindow(), nullptr);
    if (_dragPoint) {
      updateSelectedPointFromScreen(pt);
      return;
    }
    if (_dragRandomDev) {
      updateRandomDev(pt);
      return;
    }
    if (_dragRandomPoints) {
      updateRandomPoints(pt);
      return;
    }
    if (_dragView) {
      const double dx = pt.x - _lastDragPoint.x;
      const double dy = pt.y - _lastDragPoint.y;
      _viewAzDeg += dx * 0.45;
      _viewElDeg = std::clamp<double>(_viewElDeg + dy * 0.35, -85.0, 85.0);
      _viewMode = -1;
      _lastDragPoint = pt;
      storeViewState();
      setNeedsDisplay(true);
      return;
    }
    if (_dragParam)
      setParam_fromPoint(_dragParam, pt);
  }
  void mouseUp(drawing::Event *event) {
    (void)event;
    _dragParam = 0;
    _dragView = false;
    _dragPoint = false;
    _dragRandomDev = false;
    _dragRandomPoints = false;
  }
  void mouseMoved(drawing::Event *event) {
    if (_openMenu <= 0)
      return;
    drawing::Point pt =
        convertPoint_fromView(event->locationInWindow(), nullptr);
    const double itemH = 18.0;
    const int next = drawing::dropdownHitIndex(
        pt, drawing::makeRect(738, menuY(), menuW(), itemH * _menuItemCount),
        itemH, _menuItemCount);
    if (next != _hoverMenuItem) {
      _hoverMenuItem = next;
      setNeedsDisplay(true);
    }
  }
#include "s3g_input_encoder_path_files.inc"
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
      auto initial = s3g::AmbiPathEncoderParams{};
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
