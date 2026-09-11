// Literal Cocoa geometry and interaction adaptation; see
// DECODERS_VSTGUI_PARITY.md.
namespace decoder_canvas {
namespace drawing = s3g::portable_gui::decoder_drawing;
namespace foundation = s3g::portable_gui::foundation;
using namespace VSTGUI;
using namespace s3g::portable_gui::routing;
using Kind = s3g::clap_gui::ParamEventKind;
constexpr const char *pluginName = "Ambi Decoder Speaker";
constexpr const char *pluginTitle = "s3g AMBI DECODER SPEAKER";
constexpr clap_id preservedOutput = kOutputParamId;
constexpr const auto &stateExtension = stateExt;
double routingReadValue(const Plugin &p, clap_id id) {
  double v = 0.;
  paramsGetValue(&p.plugin, id, &v);
  return v;
}
void routingPublish(Plugin &p,
                    const std::vector<std::pair<clap_id, double>> &values) {
  for (auto [id, v] : values)
    applyParam(p, id, v);
}
void routingApply(Plugin &p, clap_id id, double v) { applyParam(p, id, v); }
double routingRawValue(Plugin &p, clap_id id) {
  return routingReadValue(p, id);
}
void routingInitialize(Plugin &p) {
  p.plugin.plugin_data = &p;
  init(&p.plugin);
}
#define S3G_ROUTING_CUSTOM_ACCESS 1
#include "s3g_routing_parameter_editor.inc"
#undef S3G_ROUTING_CUSTOM_ACCESS

static float linearToSrgb(float v) {
  const float x = std::clamp(v, 0.0f, 1.0f);
  return x <= 0.0031308f ? x * 12.92f
                         : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}

static VSTGUI::CColor speakerColorFromAed(float azDeg, float elDeg,
                                          float distance, bool selected) {
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
  const float grayMix = selected ? 0.08f : 0.18f;
  r = r * (1.0f - grayMix) + 0.74f * grayMix;
  g = g * (1.0f - grayMix) + 0.74f * grayMix;
  bl = bl * (1.0f - grayMix) + 0.74f * grayMix;
  return drawing::calibratedRGB(r, g, bl, selected ? 1.0 : 0.88);
}

class Editor final : public ParameterEditor {
public:
  explicit Editor(Plugin &instance) : ParameterEditor(instance) {
    auto *plugin = &p;

    _plugin = plugin;
    _dragSlider = -1;

    _rightPage = 0;
    if (auto *p = static_cast<Plugin *>(_plugin)) {
      _viewMode = p->guiViewMode;
      _viewAzDeg = p->guiViewAzDeg;
      _viewElDeg = p->guiViewElDeg;
      _viewZoom = p->guiViewZoom;
    } else {
      _viewMode = 2;
      _viewAzDeg = 35.0;
      _viewElDeg = 34.0;
      _viewZoom = 1.0;
    }
    _dragView = false;
    _lastDragPoint = drawing::makePoint(0, 0);
    _openMenu = 0;
    _hoverMenuItem = -1;
    _menuItemCount = 0;
    _menuOrigin = drawing::makePoint(0, 0);

    for (int i = 0; i < 16; ++i) {

      _gainFieldDirty[i] = false;
    }
    _azFieldDirty = false;
    _elFieldDirty = false;
    _distFieldDirty = false;
    _hasSpeakerSelection = false;
    _mixerPage = 0;
    _dragMixerSpeaker = -1;
    _dragMixerOutput = false;
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "CURRENT");

    service();
  }
  ~Editor() override { stopRefresh(); }
  void stopRefresh() override {
    ParameterEditor::stopRefresh();
    pointerEditing = false;
    _openMenu = 0;
    _dragSlider = -1;
    _dragView = false;
    _dragMixerSpeaker = -1;
    _dragMixerOutput = false;
  }
  double refreshPeriodMilliseconds() const override { return 1000. / 20.; }
  void service() override {
    _viewMode = p.guiViewMode;
    _viewAzDeg = p.guiViewAzDeg;
    _viewElDeg = p.guiViewElDeg;
    _viewZoom = p.guiViewZoom;
  }
  void guiApplyParam(Plugin &, clap_id id, double v) { setEditorParam(id, v); }
  void setEditorParam(clap_id id, double v) {
    if (pointerEditing) {
      if (begin(id))
        change(id, v);
    } else
      set(id, v);
    service();
    invalid();
  }
  void setParam_value(clap_id id, double v) { setEditorParam(id, v); }
  drawing::Rect bounds() const {
    return drawing::makeRect(0, 0, kGuiWidth, kGuiHeight);
  }
  drawing::Point convertPoint_fromView(drawing::Point pt,
                                       std::nullptr_t) const {
    return pt;
  }
  void setNeedsDisplay(bool) { invalid(); }
  bool pointerDown(MouseDownEvent &event) override {
    if (!event.buttonState.isLeft())
      return false;
    service();
    pointerEditing = true;
    drawing::Event e{{event.mousePosition.x, event.mousePosition.y},
                     int(event.clickCount)};
    mouseDown(&e);
    return true;
  }
  void pointerMove(MouseMoveEvent &event) override {
    drawing::Event e{{event.mousePosition.x, event.mousePosition.y}};
    if (event.buttonState.isLeft())
      mouseDragged(&e);
    else
      mouseMoved(&e);
    invalid();
    event.consumed = true;
  }
  void pointerUp(MouseUpEvent &event) override {
    drawing::Event e{{event.mousePosition.x, event.mousePosition.y}};
    mouseUp(&e);
    end();
    pointerEditing = false;
    invalid();
  }
  bool pointerEditing = false;

  void *_plugin;
  int _dragSlider;

  int _viewMode;
  int _rightPage;
  double _viewAzDeg;
  double _viewElDeg;
  double _viewZoom;
  bool _dragView;
  drawing::Point _lastDragPoint;
  int _openMenu;
  int _hoverMenuItem;
  uint32_t _menuItemCount;
  drawing::Point _menuOrigin;

  bool _azFieldDirty;
  bool _elFieldDirty;
  bool _distFieldDirty;
  bool _hasSpeakerSelection;
  int _mixerPage;

  bool _gainFieldDirty[16];
  int _dragMixerSpeaker;
  bool _dragMixerOutput;
  char _titlePresetName[64];
  void storeViewState() {

    auto *p = static_cast<Plugin *>(_plugin);
    if (!p)
      return;
    p->guiViewMode = _viewMode;
    p->guiViewAzDeg = _viewAzDeg;
    p->guiViewElDeg = _viewElDeg;
    p->guiViewZoom = _viewZoom;

    markStateDirty();
  }
  void drawSlider_value_norm_y_attrs_style(drawing::String name,
                                           drawing::String value, double norm,
                                           double y, drawing::Attrs attrs,
                                           const drawing::Style &style) {

    drawing::drawSlider(name, value, norm, y, attrs, attrs, style, 642, 738,
                        826, 82);
  }
  void drawMenu_value_y_attrs_style(drawing::String name, drawing::String value,
                                    double y, drawing::Attrs attrs,
                                    const drawing::Style &style) {

    drawing::drawMenu(name, value, y, attrs, attrs, style, 642, 738, 102);
  }
  void drawOpenMenu(drawing::Attrs attrs) {

    if (_openMenu <= 0 || _menuItemCount == 0)
      return;
    std::array<drawing::String, kLayoutMenuCount> layoutItems{};
    for (uint32_t i = 0; i < kLayoutMenuCount; ++i)
      layoutItems[i] =
          drawing::stringFromUtf8(layoutName(layoutPresetForMenuIndex(i)));
    static drawing::String modeItems[] = {"BASIC", "EPAD", "MMD", "ALLRAD"};
    static drawing::String orderItems[] = {"1OA", "2OA", "3OA", "4OA",
                                           "5OA", "6OA", "7OA"};
    static drawing::String weightItems[] = {"NONE", "MAXRE", "INPHASE"};
    static drawing::String fieldItems[] = {"SPHERE", "HEMI"};
    drawing::String *items = layoutItems.data();
    if (_openMenu == 2)
      items = modeItems;
    else if (_openMenu == 3)
      items = orderItems;
    else if (_openMenu == 4)
      items = weightItems;
    else if (_openMenu == 5)
      items = fieldItems;
    const double itemH = 18.0;
    const double w = _openMenu == 1 ? 150.0 : 124.0;
    drawing::Rect menuRect =
        drawing::makeRect(_menuOrigin.x, _menuOrigin.y, w,
                          itemH * static_cast<double>(_menuItemCount));
    int selected = 0;
    if (_plugin) {
      auto *p = static_cast<Plugin *>(_plugin);
      auto snapshot = acquireDecoderSnapshot(*p);
      const auto prm =
          visibleDecoderParams(*p, snapshot.decoder(), snapshot.runtimeMixer());
      if (_openMenu == 1) {
        const uint32_t layoutValue = static_cast<uint32_t>(prm.layout);
        for (uint32_t i = 0; i < _menuItemCount; ++i) {
          if (layoutPresetForMenuIndex(i) == layoutValue)
            selected = static_cast<int>(i);
        }
      } else if (_openMenu == 2)
        selected = static_cast<int>(static_cast<uint32_t>(prm.mode));
      else if (_openMenu == 3)
        selected = static_cast<int>(prm.order - 1u);
      else if (_openMenu == 4)
        selected = static_cast<int>(static_cast<uint32_t>(prm.weighting));
      else if (_openMenu == 5)
        selected = static_cast<int>(static_cast<uint32_t>(prm.customField));
    }
    drawing::Style style;
    drawing::drawDropdownMenu(menuRect, itemH, items, _menuItemCount, selected,
                              _hoverMenuItem, attrs, style);
  }
  void updateMenuHover(drawing::Point point) {

    if (_openMenu <= 0 || _menuItemCount == 0)
      return;
    const double itemH = 18.0;
    const double w = _openMenu == 1 ? 150.0 : 124.0;
    const drawing::Rect menuRect =
        drawing::makeRect(_menuOrigin.x, _menuOrigin.y, w,
                          itemH * static_cast<double>(_menuItemCount));
    const int next =
        drawing::dropdownHitIndex(point, menuRect, itemH, _menuItemCount);
    if (next != _hoverMenuItem) {
      _hoverMenuItem = next;
      setNeedsDisplay(true);
    }
  }
  drawing::Rect fieldPageButtonRect_inRect(int index, drawing::Rect rect) {

    const double w = 56.0;
    const double h = 13.0;
    const double gap = 5.0;
    return drawing::makeRect(rect.origin.x + 132.0 +
                                 static_cast<double>(index) * (w + gap),
                             rect.origin.y + 4.0, w, h);
  }
  void drawFieldPageButtonsInRect_attrs(drawing::Rect rect,
                                        drawing::Attrs attrs) {

    static drawing::String labels[] = {"FIELD", "MIXER", "MAP"};
    drawing::Style style;
    for (int i = 0; i < 3; ++i) {
      drawing::drawToolboxHeaderButton(fieldPageButtonRect_inRect(i, rect),
                                       rect, labels[i], i == _rightPage, attrs,
                                       style);
    }
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
  double viewScaleForRect(drawing::Rect rect) {

    double layoutScale = 1.0;
    if (_plugin && _viewMode != 0 && _viewMode != 1) {
      auto *p = static_cast<Plugin *>(_plugin);
      auto snapshot = acquireDecoderSnapshot(*p);
      const auto layout = snapshot->params().layout;
      if (layout == s3g::AmbiSpeakerLayoutPreset::Cube8 ||
          layout == s3g::AmbiSpeakerLayoutPreset::Cube17) {
        layoutScale = 0.82;
      }
    }
    return std::min(rect.size.width, rect.size.height) * 0.34 * layoutScale *
           std::clamp(_viewZoom, 0.55, 2.20);
  }
  drawing::Point projectWorldPoint_rect_depth(s3g::Vec3 p, drawing::Rect rect,
                                              double *depth) {

    const double cx = rect.origin.x + rect.size.width * 0.50;
    const double cy = rect.origin.y + rect.size.height * 0.54;
    const double scale = viewScaleForRect(rect);
    const float az = static_cast<float>(_viewAzDeg * M_PI / 180.0);
    const float el = static_cast<float>(_viewElDeg * M_PI / 180.0);
    const float ca = std::cos(az);
    const float sa = std::sin(az);
    const float ce = std::cos(el);
    const float se = std::sin(el);
    const float x1 = ca * p.x - sa * p.y;
    const float y1 = sa * p.x + ca * p.y;
    const float y2 = ce * y1 + se * p.z;
    const float z2 = -se * y1 + ce * p.z;
    if (depth)
      *depth = static_cast<double>(z2);
    return drawing::makePoint(cx + static_cast<double>(x1) * scale,
                              cy - static_cast<double>(y2) * scale);
  }
  void addPolyhedronShellToPath_dodecaShell_rect(drawing::Path &links,
                                                 bool dodecaShell,
                                                 drawing::Rect rect) {

    if (!links)
      return;
    constexpr float phi = 1.61803398875f;
    constexpr float invPhi = 1.0f / phi;
    std::array<s3g::Vec3, 20> verts{};
    const uint32_t count = dodecaShell ? 20u : 12u;
    if (dodecaShell) {
      const float pts[20][3]{
          {1, 1, 1},          {1, 1, -1},         {1, -1, 1},
          {1, -1, -1},        {-1, 1, 1},         {-1, 1, -1},
          {-1, -1, 1},        {-1, -1, -1},       {0, invPhi, phi},
          {0, invPhi, -phi},  {0, -invPhi, phi},  {0, -invPhi, -phi},
          {invPhi, phi, 0},   {invPhi, -phi, 0},  {-invPhi, phi, 0},
          {-invPhi, -phi, 0}, {phi, 0, invPhi},   {phi, 0, -invPhi},
          {-phi, 0, invPhi},  {-phi, 0, -invPhi},
      };
      for (uint32_t i = 0; i < count; ++i)
        verts[i] = {pts[i][0], pts[i][1], pts[i][2]};
    } else {
      const float pts[12][3]{
          {0, 1, phi}, {0, -1, phi}, {0, 1, -phi}, {0, -1, -phi},
          {1, phi, 0}, {-1, phi, 0}, {1, -phi, 0}, {-1, -phi, 0},
          {phi, 0, 1}, {-phi, 0, 1}, {phi, 0, -1}, {-phi, 0, -1},
      };
      for (uint32_t i = 0; i < count; ++i)
        verts[i] = {pts[i][0], pts[i][1], pts[i][2]};
    }

    std::array<drawing::Point, 20> points{};
    for (uint32_t i = 0; i < count; ++i) {
      const float d =
          std::sqrt(verts[i].x * verts[i].x + verts[i].y * verts[i].y +
                    verts[i].z * verts[i].z);
      if (d > 0.000001f) {
        verts[i].x /= d;
        verts[i].y /= d;
        verts[i].z /= d;
      }
      points[i] = projectWorldPoint_rect_depth(verts[i], rect, nullptr);
    }

    float minD2 = 999999.0f;
    for (uint32_t a = 0; a < count; ++a) {
      for (uint32_t b = a + 1u; b < count; ++b) {
        const float dx = verts[a].x - verts[b].x;
        const float dy = verts[a].y - verts[b].y;
        const float dz = verts[a].z - verts[b].z;
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > 0.0001f)
          minD2 = std::min(minD2, d2);
      }
    }
    const float maxD2 = minD2 * 1.08f;
    for (uint32_t a = 0; a < count; ++a) {
      for (uint32_t b = a + 1u; b < count; ++b) {
        const float dx = verts[a].x - verts[b].x;
        const float dy = verts[a].y - verts[b].y;
        const float dz = verts[a].z - verts[b].z;
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 <= maxD2) {
          (links).moveToPoint(points[a]);
          (links).lineToPoint(points[b]);
        }
      }
    }
  }
  void drawAllRadTopology_attrs_decoder_speakers(
      drawing::Rect rect, drawing::Attrs attrs,
      const s3g::AmbiSpeakerDecoder &decoder,
      const std::array<s3g::AmbiSpeaker, s3g::kAmbiSpeakerDecoderMaxSpeakers>
          &speakers) {

    const auto &topology = decoder.allRadTopology();
    const uint32_t realSpeakerCount = std::min<uint32_t>(
        decoder.params().activeSpeakers, s3g::kAmbiSpeakerDecoderMaxSpeakers);
    const uint32_t nodeCount = std::min<uint32_t>(
        topology.nodeCount, static_cast<uint32_t>(topology.nodes.size()));

    auto pointForNode = [&](uint32_t index) {
      const auto &node = topology.nodes[index];
      s3g::Vec3 world = node.direction;
      if (node.kind == s3g::AllRadNodeKind::Real &&
          node.speakerIndex < realSpeakerCount) {
        const float distance = speakers[node.speakerIndex].distance;
        world = {node.direction.x * distance, node.direction.y * distance,
                 node.direction.z * distance};
      }
      return projectWorldPoint_rect_depth(world, rect, nullptr);
    };

    drawing::Path realEdges = drawing::Path::bezierPath();
    drawing::Path supportEdges = drawing::Path::bezierPath();
    const uint32_t edgeCount = std::min<uint32_t>(
        topology.edgeCount, static_cast<uint32_t>(topology.edges.size()));
    for (uint32_t i = 0; i < edgeCount; ++i) {
      const auto &edge = topology.edges[i];
      if (edge.a >= nodeCount || edge.b >= nodeCount || edge.a == edge.b)
        continue;
      const bool involvesSupport =
          topology.nodes[edge.a].kind != s3g::AllRadNodeKind::Real ||
          topology.nodes[edge.b].kind != s3g::AllRadNodeKind::Real;
      drawing::Path path = involvesSupport ? supportEdges : realEdges;
      (path).moveToPoint(pointForNode(edge.a));
      (path).lineToPoint(pointForNode(edge.b));
    }

    drawing::setStroke(drawing::calibratedColor(0xd9c46a, 0.18));
    (realEdges).setLineWidth(0.65);
    (realEdges).stroke();

    const double dash[]{2.5, 2.5};
    (supportEdges).setLineDash_count_phase(dash, 2, 0.0);
    (supportEdges).setLineWidth(0.55);
    drawing::setStroke(drawing::calibratedColor(0xd9c46a, 0.13));
    (supportEdges).stroke();

    uint32_t topologyRealCount = 0u;
    uint32_t dropCount = 0u;
    uint32_t foldCount = 0u;
    drawing::Path dropPoints = drawing::Path::bezierPath();
    drawing::Path foldPoints = drawing::Path::bezierPath();
    for (uint32_t i = 0; i < nodeCount; ++i) {
      if (topology.nodes[i].kind == s3g::AllRadNodeKind::Real) {
        ++topologyRealCount;
        continue;
      }
      const drawing::Point point = pointForNode(i);
      if (topology.nodes[i].kind == s3g::AllRadNodeKind::SupportDrop) {
        ++dropCount;
        (dropPoints)
            .appendBezierPathWithOvalInRect(
                drawing::makeRect(point.x - 2.5, point.y - 2.5, 5.0, 5.0));
      } else {
        ++foldCount;
        (foldPoints).moveToPoint(drawing::makePoint(point.x, point.y - 3.0));
        (foldPoints).lineToPoint(drawing::makePoint(point.x + 3.0, point.y));
        (foldPoints).lineToPoint(drawing::makePoint(point.x, point.y + 3.0));
        (foldPoints).lineToPoint(drawing::makePoint(point.x - 3.0, point.y));
        (foldPoints).closePath();
      }
    }
    (dropPoints).setLineWidth(0.75);
    drawing::setStroke(drawing::calibratedColor(0xe7a35a, 0.48));
    (dropPoints).stroke();
    (foldPoints).setLineWidth(0.75);
    drawing::setStroke(drawing::calibratedColor(0x8fc3c9, 0.52));
    (foldPoints).stroke();

    drawing::String dimension = "INVALID";
    if (topology.valid) {
      if (topology.dimension == s3g::AllRadDimension::Ring2D)
        dimension = "2D";
      else if (topology.dimension == s3g::AllRadDimension::Sphere3D)
        dimension = "3D";
    }
    drawing::String status = drawing::stringFormat(
        "ALLRAD  %u REAL  +%u GAP  +%u FOLD  %@", topologyRealCount, dropCount,
        foldCount, dimension);
    drawing::drawAtPoint_withAttributes(
        status, drawing::makePoint(rect.origin.x + 12.0, rect.origin.y + 9.0),
        attrs);
  }
  void drawSpeakerField_attrs_small_style(drawing::Rect rect,
                                          drawing::Attrs attrs,
                                          drawing::Attrs small,
                                          const drawing::Style &style) {

    auto *p = static_cast<Plugin *>(_plugin);
    auto snapshot = acquireDecoderSnapshot(*p);
    const auto prm =
        visibleDecoderParams(*p, snapshot.decoder(), snapshot.runtimeMixer());
    drawing::setFill(drawing::calibratedColor(0x111111));
    drawing::rectFill(rect);
    drawing::setStroke(style.grid);
    drawing::frameRect(rect);

    const auto speakers =
        visibleDecoderSpeakers(*p, snapshot.decoder(), snapshot.runtimeMixer());
    const uint32_t n = std::min<uint32_t>(prm.activeSpeakers,
                                          s3g::kAmbiSpeakerDecoderMaxSpeakers);
    std::array<drawing::Point, s3g::kAmbiSpeakerDecoderMaxSpeakers> points{};
    for (uint32_t i = 0; i < n; ++i) {
      const auto &sp = speakers[i];
      const s3g::Vec3 dir =
          s3g::directionFromAed(sp.azimuthDeg, sp.elevationDeg);
      const s3g::Vec3 world{dir.x * sp.distance, dir.y * sp.distance,
                            dir.z * sp.distance};
      points[i] = projectWorldPoint_rect_depth(world, rect, nullptr);
      points[i].x = std::round(points[i].x);
      points[i].y = std::round(points[i].y);
    }
    const bool allRad = prm.mode == s3g::AmbiSpeakerDecoderMode::AllRad;
    if (allRad) {
      drawAllRadTopology_attrs_decoder_speakers(rect, small, snapshot.decoder(),
                                                speakers);
    } else {
      drawing::setStroke(drawing::calibratedColor(0x777777, 0.72));
      drawing::Path links = drawing::Path::bezierPath();
      auto edge = [&](uint32_t a, uint32_t b) {
        if (a >= n || b >= n)
          return;
        (links).moveToPoint(points[a]);
        (links).lineToPoint(points[b]);
      };
      auto ring = [&](uint32_t base, uint32_t count) {
        if (count < 2u)
          return;
        for (uint32_t i = 0; i < count; ++i)
          edge(base + i, base + ((i + 1u) % count));
      };
      const auto layout = prm.layout;
      if (layout == s3g::AmbiSpeakerLayoutPreset::Quad) {
        ring(0, 4);
      } else if (layout == s3g::AmbiSpeakerLayoutPreset::Cube8) {
        ring(0, 4);
        ring(4, 4);
        for (uint32_t i = 0; i < 4; ++i)
          edge(i, i + 4u);
      } else if (layout == s3g::AmbiSpeakerLayoutPreset::Cube17) {
        ring(0, 4);
        edge(4, 5);
        edge(5, 6);
        edge(6, 7);
        edge(7, 8);
        edge(8, 9);
        edge(9, 10);
        edge(10, 11);
        edge(11, 4);
        ring(12, 4);
        edge(0, 4);
        edge(1, 6);
        edge(2, 8);
        edge(3, 10);
        edge(4, 12);
        edge(6, 13);
        edge(8, 14);
        edge(10, 15);
        edge(12, 16);
        edge(13, 16);
        edge(14, 16);
        edge(15, 16);
      } else if (layout == s3g::AmbiSpeakerLayoutPreset::Cube41 ||
                 layout == s3g::AmbiSpeakerLayoutPreset::Lpac41) {
        ring(0, 16);
        ring(16, 12);
        ring(28, 8);
        ring(36, 4);
      } else if (layout == s3g::AmbiSpeakerLayoutPreset::Dome24 ||
                 layout == s3g::AmbiSpeakerLayoutPreset::Dome25 ||
                 layout == s3g::AmbiSpeakerLayoutPreset::Srst25) {
        ring(0, 12);
        ring(12, 8);
        ring(20, 4);
        for (uint32_t i = 0; i < 8; ++i) {
          const uint32_t lowerA = (i * 3u) / 2u;
          const uint32_t lowerB = (lowerA + 1u) % 12u;
          edge(12u + i, lowerA);
          edge(12u + i, lowerB);
        }
        for (uint32_t i = 0; i < 4; ++i) {
          edge(20u + i, 12u + i * 2u);
          edge(20u + i, 12u + ((i * 2u + 1u) % 8u));
        }
        if (layout == s3g::AmbiSpeakerLayoutPreset::Dome25 ||
            layout == s3g::AmbiSpeakerLayoutPreset::Srst25) {
          for (uint32_t i = 0; i < 4; ++i)
            edge(24, 20u + i);
        }
      } else if (layout == s3g::AmbiSpeakerLayoutPreset::QuadOverhead6) {
        ring(0, 4);
        edge(4, 0);
        edge(4, 3);
        edge(5, 1);
        edge(5, 2);
        edge(4, 5);
      } else if (layout == s3g::AmbiSpeakerLayoutPreset::OctophonicRing) {
        ring(0, 8);
      } else if (layout == s3g::AmbiSpeakerLayoutPreset::Dodeca12) {
        addPolyhedronShellToPath_dodecaShell_rect(links, true, rect);
      } else if (layout == s3g::AmbiSpeakerLayoutPreset::Icosahedron20) {
        addPolyhedronShellToPath_dodecaShell_rect(links, false, rect);
      }
      (links).setLineWidth(1.0);
      (links).stroke();
    }

    for (uint32_t i = 0; i < n; ++i) {
      const auto &sp = speakers[i];
      const bool selected = _hasSpeakerSelection && i == prm.selectedSpeaker;
      const double r = selected ? 6.0 : 4.8;
      drawing::setFill(speakerColorFromAed(sp.azimuthDeg, sp.elevationDeg,
                                           sp.distance, selected));
      drawing::rectFill(drawing::makeRect(points[i].x - r, points[i].y - r,
                                          r * 2.0, r * 2.0));
      drawing::setStroke(drawing::calibratedColor(0x050505, 0.90));
      drawing::frameRect(drawing::makeRect(points[i].x - r, points[i].y - r,
                                           r * 2.0, r * 2.0));
      if (selected) {
        drawing::setStroke(drawing::calibratedColor(0xf2f2f2));
        drawing::frameRect(drawing::makeRect(points[i].x - 10.0,
                                             points[i].y - 10.0, 20.0, 20.0));
      }
      drawing::String label = drawing::stringFormat("%u", i + 1u);
      drawing::Attrs idAttrs{selected ? drawing::calibratedColor(0xc8c8c8)
                                      : drawing::calibratedColor(0x151515),
                             false, 7.5};
      drawing::Size labelSize = drawing::sizeWithAttributes(label, idAttrs);
      drawing::drawAtPoint_withAttributes(
          label,
          drawing::makePoint(points[i].x - labelSize.width * 0.5,
                             points[i].y - labelSize.height * 0.5 - 0.5),
          idAttrs);
    }
    drawing::String viewText =
        _viewMode == 0
            ? "TOP VIEW   0 front/top  -90 right  +90 left"
            : (_viewMode == 1 ? "SIDE VIEW   +90 elevation up"
                              : "3/4 VIEW   drag blank space to rotate");
    drawing::drawAtPoint_withAttributes(
        viewText,
        drawing::makePoint(rect.origin.x + 12,
                           rect.origin.y + rect.size.height - 23),
        attrs);
  }
  void drawDecodeMap_attrs_small_style(drawing::Rect rect, drawing::Attrs attrs,
                                       drawing::Attrs small,
                                       const drawing::Style &style) {

    auto *p = static_cast<Plugin *>(_plugin);
    auto snapshot = acquireDecoderSnapshot(*p);
    const auto prm =
        visibleDecoderParams(*p, snapshot.decoder(), snapshot.runtimeMixer());
    const auto speakers =
        visibleDecoderSpeakers(*p, snapshot.decoder(), snapshot.runtimeMixer());
    const auto &matrix = snapshot->matrix();
    const uint32_t n = std::min<uint32_t>(prm.activeSpeakers,
                                          s3g::kAmbiSpeakerDecoderMaxSpeakers);
    const uint32_t ambiCh = s3g::ambiChannelsForOrder(prm.order);
    drawing::setFill(drawing::calibratedColor(0x111111));
    drawing::rectFill(rect);
    drawing::setStroke(style.grid);
    drawing::frameRect(rect);

    std::array<drawing::Point, s3g::kAmbiSpeakerDecoderMaxSpeakers> points{};
    for (uint32_t i = 0; i < n; ++i) {
      const auto &sp = speakers[i];
      const s3g::Vec3 dir =
          s3g::directionFromAed(sp.azimuthDeg, sp.elevationDeg);
      const s3g::Vec3 world{dir.x * sp.distance, dir.y * sp.distance,
                            dir.z * sp.distance};
      points[i] = projectWorldPoint_rect_depth(world, rect, nullptr);
    }

    const bool allRad = prm.mode == s3g::AmbiSpeakerDecoderMode::AllRad;
    drawing::Path links = drawing::Path::bezierPath();
    if (allRad) {
      drawAllRadTopology_attrs_decoder_speakers(rect, small, snapshot.decoder(),
                                                speakers);
    } else {
      auto edge = [&](uint32_t a, uint32_t b) {
        if (a >= n || b >= n)
          return;
        (links).moveToPoint(points[a]);
        (links).lineToPoint(points[b]);
      };
      auto ring = [&](uint32_t base, uint32_t count) {
        if (count < 2u || base >= n)
          return;
        count = std::min<uint32_t>(count, n - base);
        for (uint32_t i = 0; i < count; ++i)
          edge(base + i, base + ((i + 1u) % count));
      };
      const auto layout = prm.layout;
      if (layout == s3g::AmbiSpeakerLayoutPreset::Quad) {
        ring(0, 4);
      } else if (layout == s3g::AmbiSpeakerLayoutPreset::Cube8) {
        ring(0, 4);
        ring(4, 4);
        for (uint32_t i = 0; i < 4; ++i)
          edge(i, i + 4u);
      } else if (layout == s3g::AmbiSpeakerLayoutPreset::Cube17) {
        ring(0, 4);
        edge(4, 5);
        edge(5, 6);
        edge(6, 7);
        edge(7, 8);
        edge(8, 9);
        edge(9, 10);
        edge(10, 11);
        edge(11, 4);
        ring(12, 4);
        edge(0, 4);
        edge(1, 6);
        edge(2, 8);
        edge(3, 10);
        edge(4, 12);
        edge(6, 13);
        edge(8, 14);
        edge(10, 15);
        edge(12, 16);
        edge(13, 16);
        edge(14, 16);
        edge(15, 16);
      } else if (layout == s3g::AmbiSpeakerLayoutPreset::Cube41 ||
                 layout == s3g::AmbiSpeakerLayoutPreset::Lpac41) {
        ring(0, 16);
        ring(16, 12);
        ring(28, 8);
        ring(36, 4);
      } else if (layout == s3g::AmbiSpeakerLayoutPreset::Dome24 ||
                 layout == s3g::AmbiSpeakerLayoutPreset::Dome25 ||
                 layout == s3g::AmbiSpeakerLayoutPreset::Srst25) {
        ring(0, 12);
        ring(12, 8);
        ring(20, 4);
        for (uint32_t i = 0; i < 8; ++i) {
          const uint32_t lowerA = (i * 3u) / 2u;
          edge(12u + i, lowerA);
          edge(12u + i, (lowerA + 1u) % 12u);
        }
        for (uint32_t i = 0; i < 4; ++i) {
          edge(20u + i, 12u + i * 2u);
          edge(20u + i, 12u + ((i * 2u + 1u) % 8u));
        }
        if (layout == s3g::AmbiSpeakerLayoutPreset::Dome25 ||
            layout == s3g::AmbiSpeakerLayoutPreset::Srst25) {
          for (uint32_t i = 0; i < 4; ++i)
            edge(24, 20u + i);
        }
      } else if (layout == s3g::AmbiSpeakerLayoutPreset::QuadOverhead6) {
        ring(0, 4);
        edge(4, 0);
        edge(4, 3);
        edge(5, 1);
        edge(5, 2);
        edge(4, 5);
      } else if (layout == s3g::AmbiSpeakerLayoutPreset::OctophonicRing) {
        ring(0, 8);
      } else if (layout == s3g::AmbiSpeakerLayoutPreset::Dodeca12) {
        addPolyhedronShellToPath_dodecaShell_rect(links, true, rect);
      } else if (layout == s3g::AmbiSpeakerLayoutPreset::Icosahedron20) {
        addPolyhedronShellToPath_dodecaShell_rect(links, false, rect);
      }
    }
    const uint32_t probeIndex =
        std::min<uint32_t>(prm.selectedSpeaker, n > 0u ? n - 1u : 0u);
    const auto &probeSpeaker = speakers[probeIndex];
    const auto probeDirection = s3g::directionFromAed(
        probeSpeaker.azimuthDeg, probeSpeaker.elevationDeg);
    const auto basis = allRad ? s3g::acnSn3dBasis7Canonical(probeDirection)
                              : s3g::acnSn3dBasis7(probeDirection);
    std::array<float, s3g::kAmbiSpeakerDecoderMaxSpeakers> energy{};
    float maxEnergy = 0.000001f;
    for (uint32_t sp = 0; sp < n; ++sp) {
      float value = 0.0f;
      for (uint32_t ch = 0; ch < ambiCh; ++ch) {
        value += basis[ch] * matrix[sp][ch];
      }
      value *= speakers[sp].enabled ? speakers[sp].gain : 0.0f;
      energy[sp] = std::fabs(value);
      maxEnergy = std::max(maxEnergy, energy[sp]);
    }

    for (uint32_t i = 0; i < n; ++i) {
      const double e =
          std::pow(std::clamp<double>(energy[i] / maxEnergy, 0.0, 1.0), 0.70);
      const double r = 7.0;
      const drawing::Rect square =
          drawing::makeRect(points[i].x - r, points[i].y - r, r * 2.0, r * 2.0);
      drawing::setFill(drawing::calibratedColor(0x151515));
      drawing::rectFill(square);
      drawing::setFill(
          drawing::heatColor(e, speakers[i].enabled ? 0.96 : 0.24));
      drawing::rectFill(drawing::inset(square, 2.0, 2.0));
      drawing::setStroke(drawing::calibratedColor(
          i == probeIndex ? 0xf5f5f5 : (e > 0.92 ? 0xf0f0f0 : 0x777777)));
      drawing::frameRect(square);
      if (i == probeIndex) {
        drawing::setStroke(drawing::calibratedColor(0xf5f5f5, 0.92));
        drawing::frameRect(drawing::inset(square, -4.0, -4.0));
        drawing::Path::strokeLineFromPoint_toPoint(
            drawing::makePoint(drawing::midX(square) - 13.0,
                               drawing::midY(square)),
            drawing::makePoint(drawing::midX(square) + 13.0,
                               drawing::midY(square)));
        drawing::Path::strokeLineFromPoint_toPoint(
            drawing::makePoint(drawing::midX(square),
                               drawing::midY(square) - 13.0),
            drawing::makePoint(drawing::midX(square),
                               drawing::midY(square) + 13.0));
      }
      drawing::String label = drawing::stringFormat("%u", i + 1u);
      drawing::Size labelSize = drawing::sizeWithAttributes(label, small);
      drawing::drawAtPoint_withAttributes(
          label,
          drawing::makePoint(drawing::midX(square) - labelSize.width * 0.5,
                             drawing::midY(square) - labelSize.height * 0.5 -
                                 0.5),
          small);
    }
    if (!allRad) {
      drawing::setStroke(drawing::calibratedColor(0xd0d0d0, 0.76));
      (links).setLineWidth(1.15);
      (links).stroke();
    }
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat("PROBE S%u   AZ %+.1f  EL %+.1f   %uOA",
                              probeIndex + 1u, probeSpeaker.azimuthDeg,
                              probeSpeaker.elevationDeg, prm.order),
        drawing::makePoint(rect.origin.x + 12,
                           rect.origin.y + rect.size.height - 23),
        attrs);
  }
  drawing::Rect mixerOutputTrackRect(drawing::Rect rect) {

    return drawing::makeRect(rect.origin.x + 88.0, rect.origin.y + 42.0,
                             rect.size.width - 178.0, 9.0);
  }
  drawing::Rect mixerSpeakerRect_inRect(uint32_t index, drawing::Rect rect) {

    const uint32_t slot = index % 16u;
    const double laneW = 34.0;
    const double x = rect.origin.x + 12.0 + static_cast<double>(slot) * laneW;
    return drawing::makeRect(x + 8.0, rect.origin.y + 128.0, 12.0,
                             rect.size.height - 196.0);
  }
  drawing::Rect mixerGainFieldRect_inRect(uint32_t slot, drawing::Rect rect) {

    const drawing::Rect fader = mixerSpeakerRect_inRect(slot, rect);
    const double w = 30.0;
    return drawing::makeRect(drawing::midX(fader) - w * 0.5,
                             drawing::maxY(fader) + 7.0, w, 15.0);
  }
  drawing::Rect mixerMuteRect_inRect(uint32_t slot, drawing::Rect rect) {

    const double laneW = 34.0;
    const double x = rect.origin.x + 12.0 + static_cast<double>(slot) * laneW;
    return drawing::makeRect(x + 1.0, drawing::maxY(rect) - 42.0, 14.0, 14.0);
  }
  drawing::Rect mixerSoloRect_inRect(uint32_t slot, drawing::Rect rect) {

    const double laneW = 34.0;
    const double x = rect.origin.x + 12.0 + static_cast<double>(slot) * laneW;
    return drawing::makeRect(x + 17.0, drawing::maxY(rect) - 42.0, 14.0, 14.0);
  }
  drawing::Rect mixerPageButtonRect_inRect(int index, drawing::Rect rect) {

    const double w = 54.0;
    const double h = 13.0;
    const double gap = 5.0;
    const int count = 4;
    return drawing::makeRect(drawing::maxX(rect) - 18.0 -
                                 static_cast<double>(count - index) * w -
                                 static_cast<double>(count - index - 1) * gap,
                             rect.origin.y + 17.0, w, h);
  }
  void drawMixerPageButtonsInRect_attrs(drawing::Rect rect,
                                        drawing::Attrs attrs) {

    static drawing::String labels[] = {"1-16", "17-32", "33-48", "49-64"};
    auto *p = static_cast<Plugin *>(_plugin);
    uint32_t n = 0u;
    if (p) {
      auto snapshot = acquireDecoderSnapshot(*p);
      n = std::min<uint32_t>(snapshot->params().activeSpeakers,
                             s3g::kAmbiSpeakerDecoderMaxSpeakers);
    }
    const int pageCount = static_cast<int>((n + 15u) / 16u);
    if (pageCount <= 1)
      return;
    for (int i = 0; i < pageCount; ++i) {
      drawing::Rect button = mixerPageButtonRect_inRect(i, rect);
      drawing::setFill(
          drawing::calibratedColor(i == _mixerPage ? 0x303030 : 0x151515));
      drawing::rectFill(button);
      drawing::setStroke(
          drawing::calibratedColor(i == _mixerPage ? 0xd1d1d1 : 0x555555));
      drawing::frameRect(button);
      drawing::Size size = drawing::sizeWithAttributes(labels[i], attrs);
      drawing::drawAtPoint_withAttributes(
          labels[i],
          drawing::makePoint(
              button.origin.x + (button.size.width - size.width) * 0.5,
              button.origin.y + (button.size.height - size.height) * 0.5 - 0.5),
          attrs);
    }
  }
  void drawSpeakerMixer_attrs_style(drawing::Rect rect, drawing::Attrs attrs,
                                    const drawing::Style &style) {

    auto *p = static_cast<Plugin *>(_plugin);
    auto snapshot = acquireDecoderSnapshot(*p);
    const auto speakers =
        visibleDecoderSpeakers(*p, snapshot.decoder(), snapshot.runtimeMixer());
    const auto params =
        visibleDecoderParams(*p, snapshot.decoder(), snapshot.runtimeMixer());
    const uint32_t n = std::min<uint32_t>(params.activeSpeakers,
                                          s3g::kAmbiSpeakerDecoderMaxSpeakers);
    const int pageCount = std::max<int>(1, static_cast<int>((n + 15u) / 16u));
    _mixerPage = std::clamp(_mixerPage, 0, pageCount - 1);
    const uint32_t pageStart = static_cast<uint32_t>(_mixerPage) * 16u;
    drawMixerPageButtonsInRect_attrs(rect, attrs);
    drawing::drawSlider(
        "OUT", drawing::stringFormat("%+.1f dB", params.outputGainDb),
        (params.outputGainDb + 60.0f) / 72.0f, rect.origin.y + 36.0, attrs,
        attrs, style, rect.origin.x + 18.0, rect.origin.x + 88.0,
        rect.origin.x + rect.size.width - 70.0, rect.size.width - 178.0);
    bool anySolo = false;
    for (uint32_t i = 0; i < n; ++i)
      anySolo = anySolo || speakers[i].solo;
    const uint32_t pageEnd = std::min<uint32_t>(n, pageStart + 16u);
    for (uint32_t i = pageStart; i < pageEnd; ++i) {
      const auto &sp = speakers[i];
      const bool selected = _hasSpeakerSelection && i == params.selectedSpeaker;
      const bool audible = sp.enabled && (!anySolo || sp.solo);
      const uint32_t pageSlot = i - pageStart;
      const double laneW = 34.0;
      const double laneX =
          rect.origin.x + 12.0 + static_cast<double>(pageSlot) * laneW;
      if (selected) {
        drawing::setFill(drawing::calibratedColor(0x242424));
        drawing::rectFill(drawing::makeRect(laneX - 2.0, rect.origin.y + 96.0,
                                            laneW - 3.0,
                                            rect.size.height - 102.0));
        drawing::setStroke(drawing::calibratedColor(0x777777));
        drawing::frameRect(drawing::makeRect(laneX - 2.0, rect.origin.y + 96.0,
                                             laneW - 3.0,
                                             rect.size.height - 102.0));
      }
      const drawing::Rect slot = mixerSpeakerRect_inRect(i, rect);
      drawing::String label = drawing::stringFormat("%u", i + 1u);
      drawing::drawAtPoint_withAttributes(
          label,
          drawing::makePoint(laneX + (i < 9 ? 10.0 : 7.0),
                             rect.origin.y + 97.0),
          attrs);
      drawing::setFill(drawing::calibratedColor(audible ? 0x181818 : 0x0d0d0d));
      drawing::rectFill(slot);
      drawing::setStroke(
          drawing::calibratedColor(audible ? 0x545454 : 0x333333));
      drawing::frameRect(slot);
      const double norm = std::clamp<double>(sp.gain / 2.0f, 0.0, 1.0);
      drawing::Rect fill = drawing::inset(slot, 2.0, 2.0);
      const double fullH = fill.size.height;
      fill.origin.y += fullH * (1.0 - norm);
      fill.size.height = std::max<double>(1.0, fullH * norm);
      drawing::setFill(speakerColorFromAed(sp.azimuthDeg, sp.elevationDeg,
                                           sp.distance, selected));
      drawing::rectFill(fill);
      drawing::setFill(
          drawing::calibratedColor(selected ? 0xf2f2f2 : 0x9a9a9a));
      drawing::rectFill(drawing::makeRect(
          slot.origin.x - 2.0,
          slot.origin.y + slot.size.height * (1.0 - norm) - 1.0,
          slot.size.width + 4.0, 3.0));
      drawing::Rect mute = mixerMuteRect_inRect(pageSlot, rect);
      drawing::setFill(
          drawing::calibratedColor(sp.enabled ? 0x151515 : 0x3a3a3a));
      drawing::rectFill(mute);
      drawing::setStroke(
          drawing::calibratedColor(sp.enabled ? 0x5a5a5a : 0xd1d1d1));
      drawing::frameRect(mute);
      text("M", mute, attrs.color, kCenterText);
      drawing::Rect solo = mixerSoloRect_inRect(pageSlot, rect);
      drawing::setFill(drawing::calibratedColor(sp.solo ? 0xd1d1d1 : 0x151515));
      drawing::rectFill(solo);
      drawing::setStroke(
          drawing::calibratedColor(sp.solo ? 0xf2f2f2 : 0x5a5a5a));
      drawing::frameRect(solo);
      drawing::Attrs soloAttrs =
          sp.solo ? drawing::Attrs{drawing::calibratedColor(0x111111)} : attrs;
      text("S", solo, soloAttrs.color, kCenterText);
    }
  }
  void updateMixerSpeakerGain_inRect(drawing::Point point, drawing::Rect rect) {

    if (_dragMixerSpeaker < 0)
      return;
    const uint32_t index = static_cast<uint32_t>(_dragMixerSpeaker);
    drawing::Rect track = mixerSpeakerRect_inRect(index, rect);
    const double norm = std::clamp(
        (drawing::maxY(track) - point.y) / track.size.height, 0.0, 1.0);
    auto *p = static_cast<Plugin *>(_plugin);
    guiApplyParam(*p, kSelectedSpeakerParamId, static_cast<double>(index + 1u));
    guiSpeakerGain(*p, index, norm * 2.0);
    _hasSpeakerSelection = true;
    setNeedsDisplay(true);
  }
  void updateMixerOutput_inRect(drawing::Point point, drawing::Rect rect) {

    drawing::Rect track = mixerOutputTrackRect(rect);
    const double norm =
        std::clamp((point.x - track.origin.x) / track.size.width, 0.0, 1.0);
    auto *p = static_cast<Plugin *>(_plugin);
    guiApplyParam(*p, kOutputParamId, -60.0 + norm * 72.0);
    setNeedsDisplay(true);
  }
  void toggleMixerSpeakerMute(uint32_t index) {

    auto *p = static_cast<Plugin *>(_plugin);
    auto snapshot = acquireDecoderSnapshot(*p);
    const uint32_t n = std::min<uint32_t>(snapshot->params().activeSpeakers,
                                          s3g::kAmbiSpeakerDecoderMaxSpeakers);
    if (index >= n)
      return;
    const auto speakers =
        visibleDecoderSpeakers(*p, snapshot.decoder(), snapshot.runtimeMixer());
    const bool enabled = speakers[index].enabled;
    guiSpeakerEnabled(*p, index, !enabled);
    _hasSpeakerSelection = true;
    setNeedsDisplay(true);
  }
  void toggleMixerSpeakerSolo(uint32_t index) {

    auto *p = static_cast<Plugin *>(_plugin);
    auto snapshot = acquireDecoderSnapshot(*p);
    const uint32_t n = std::min<uint32_t>(snapshot->params().activeSpeakers,
                                          s3g::kAmbiSpeakerDecoderMaxSpeakers);
    if (index >= n)
      return;
    const auto speakers =
        visibleDecoderSpeakers(*p, snapshot.decoder(), snapshot.runtimeMixer());
    const bool solo = speakers[index].solo;
    guiSpeakerSolo(*p, index, !solo);
    _hasSpeakerSelection = true;
    setNeedsDisplay(true);
  }
  uint32_t hitSpeakerAt_inRect_found(drawing::Point pt, drawing::Rect rect,
                                     bool *found) {

    auto *p = static_cast<Plugin *>(_plugin);
    auto snapshot = acquireDecoderSnapshot(*p);
    const auto prm =
        visibleDecoderParams(*p, snapshot.decoder(), snapshot.runtimeMixer());
    const auto speakers =
        visibleDecoderSpeakers(*p, snapshot.decoder(), snapshot.runtimeMixer());
    const uint32_t n = std::min<uint32_t>(prm.activeSpeakers,
                                          s3g::kAmbiSpeakerDecoderMaxSpeakers);
    uint32_t best = prm.selectedSpeaker;
    double bestD = 999999.0;
    for (uint32_t i = 0; i < n; ++i) {
      const auto &sp = speakers[i];
      const s3g::Vec3 dir =
          s3g::directionFromAed(sp.azimuthDeg, sp.elevationDeg);
      const s3g::Vec3 world{dir.x * sp.distance, dir.y * sp.distance,
                            dir.z * sp.distance};
      const drawing::Point spPt =
          projectWorldPoint_rect_depth(world, rect, nullptr);
      const double d = std::hypot(pt.x - spPt.x, pt.y - spPt.y);
      if (d < bestD) {
        bestD = d;
        best = i;
      }
    }
    if (found)
      *found = bestD < 18.0;
    return best;
  }
  void paint() override {
    drawing::DrawingScope scope(*this, context, font, titleFont, &error);
    service();

    auto *p = static_cast<Plugin *>(_plugin);
    drawing::Style style;
    drawing::setFill(style.bg);
    drawing::rectFill(bounds());
    drawing::Attrs small = drawing::softValueAttrs();
    drawing::Attrs lab = drawing::softLabelAttrs();
    drawing::Attrs titleAttrs = drawing::softTitleAttrs();
    const float pk = p->outputPeak.load(std::memory_order_relaxed);
    drawing::drawDecoderTitleBand(
        "s3g AMBI DECODER SPEAKER", drawing::stringFromUtf8(_titlePresetName),
        drawing::peakDbText(pk), drawing::encoderTitleBand(900.0, 620.0),
        titleAttrs, lab, small, style);

    drawing::drawPanelFrame(18, 42, 596, 556, style);
    drawing::drawPanelHeader(
        _rightPage == 0 ? "SPEAKER FIELD"
                        : (_rightPage == 1 ? "SPEAKER MIXER" : "DECODE MAP"),
        true, 18, 42, 596, 21, lab, style);
    drawFieldPageButtonsInRect_attrs(drawing::makeRect(18, 42, 596, 556),
                                     small);
    if (_rightPage == 0 || _rightPage == 2) {
      // Portable gain fields are registered only on the mixer page.
      drawZoomButtonsInRect_attrs(drawing::makeRect(18, 42, 596, 556), small);
      drawViewButtonsInRect_attrs(drawing::makeRect(18, 42, 596, 556), small);
      if (_rightPage == 0) {
        drawSpeakerField_attrs_small_style(drawing::makeRect(34, 76, 564, 506),
                                           small, small, style);
      } else {
        drawDecodeMap_attrs_small_style(drawing::makeRect(34, 76, 564, 506),
                                        small, small, style);
      }
    } else {
      drawSpeakerMixer_attrs_style(drawing::makeRect(34, 76, 564, 506), small,
                                   style);
    }

    const drawing::Rect lowerPanel = drawing::makeRect(630, 330, 250, 132);
    drawing::drawPanelFrame(630, 42, 250, 54, style);
    drawing::drawPanelHeader("OUTPUT", true, 630, 42, 250, 21, lab, style);
    drawing::drawPanelFrame(630, 108, 250, 210, style);
    drawing::drawPanelHeader("DECODER", true, 630, 108, 250, 21, lab, style);
    drawing::drawPanelFrame(lowerPanel.origin.x, lowerPanel.origin.y,
                            lowerPanel.size.width, lowerPanel.size.height,
                            style);
    drawing::drawPanelHeader("SPEAKER", true, lowerPanel.origin.x,
                             lowerPanel.origin.y, lowerPanel.size.width, 21,
                             lab, style);

    auto decoderSnapshot = acquireDecoderSnapshot(*p);
    const auto prm = visibleDecoderParams(*p, decoderSnapshot.decoder(),
                                          decoderSnapshot.runtimeMixer());
    drawSlider_value_norm_y_attrs_style(
        "OUT", drawing::stringFormat("%+.1f dB", prm.outputGainDb),
        (prm.outputGainDb + 60.0f) / 72.0f, 78, small, style);
    drawMenu_value_y_attrs_style(
        "LAYOUT",
        drawing::stringFromUtf8(layoutName(static_cast<uint32_t>(prm.layout))),
        144, small, style);
    drawMenu_value_y_attrs_style(
        "MODE",
        drawing::stringFromUtf8(modeName(static_cast<uint32_t>(prm.mode))), 170,
        small, style);
    drawMenu_value_y_attrs_style(
        "ORDER", drawing::stringFormat("%uOA", prm.order), 196, small, style);
    drawMenu_value_y_attrs_style("WGT",
                                 drawing::stringFromUtf8(weightingName(
                                     static_cast<uint32_t>(prm.weighting))),
                                 222, small, style);
    drawMenu_value_y_attrs_style("FIELD",
                                 drawing::stringFromUtf8(customFieldName(
                                     static_cast<uint32_t>(prm.customField))),
                                 248, small, style);
    drawSlider_value_norm_y_attrs_style(
        "COUNT", drawing::stringFormat("%u", prm.activeSpeakers),
        (prm.activeSpeakers - 2.0) / 62.0, 274, small, style);
    drawSlider_value_norm_y_attrs_style(
        "WID", drawing::stringFormat("%.2f", prm.width), prm.width / 1.50f, 300,
        small, style);

    const double selectedNorm =
        prm.activeSpeakers > 1u
            ? static_cast<double>(prm.selectedSpeaker) /
                  static_cast<double>(prm.activeSpeakers - 1u)
            : 0.0;
    drawSlider_value_norm_y_attrs_style(
        "SEL", drawing::stringFormat("%u", prm.selectedSpeaker + 1u),
        selectedNorm, 366, small, style);
    drawSlider_value_norm_y_attrs_style(
        "AZ", "", s3g::aedAzimuthSliderNorm(prm.selectedAzimuthDeg), 392, small,
        style);
    drawSlider_value_norm_y_attrs_style(
        "EL", "", (prm.selectedElevationDeg + 90.0f) / 180.0f, 418, small,
        style);
    drawSlider_value_norm_y_attrs_style(
        "DST", "", (prm.selectedDistance - 0.15f) / 1.85f, 444, small, style);

    drawNumericFields();
    drawOpenMenu(small);
  }
  void updateSlider(drawing::Point point) {

    auto *p = static_cast<Plugin *>(_plugin);
    auto snapshot = acquireDecoderSnapshot(*p);
    const auto params =
        visibleDecoderParams(*p, snapshot.decoder(), snapshot.runtimeMixer());
    const double n = std::clamp((point.x - 738.0) / 82.0, 0.0, 1.0);
    switch (_dragSlider) {
    case 4:
      guiApplyParam(*p, kActiveSpeakersParamId, 2.0 + n * 62.0);
      break;
    case 6:
      guiApplyParam(*p, kWidthParamId, n * 1.50);
      break;
    case 7:
      guiApplyParam(
          *p, kSelectedSpeakerParamId,
          1.0 + n * static_cast<double>(
                        std::max<uint32_t>(1u, params.activeSpeakers) - 1u));
      break;
    case 8:
      guiApplyParam(*p, kAzimuthParamId,
                    s3g::aedAzimuthFromSliderNorm(static_cast<float>(n)));
      break;
    case 9:
      guiApplyParam(*p, kElevationParamId, -90.0 + n * 180.0);
      break;
    case 10:
      guiApplyParam(*p, kDistanceParamId, 0.15 + n * 1.85);
      break;
    case 11:
      guiApplyParam(*p, kSpeakerGainParamId, n * 2.0);
      break;
    case 13:
      guiApplyParam(*p, kOutputParamId, -60.0 + n * 72.0);
      break;
    default:
      break;
    }
    setNeedsDisplay(true);
  }
  void mouseDown(drawing::Event *event) {

    drawing::Point pt =
        convertPoint_fromView(event->locationInWindow(), nullptr);
    auto *p = static_cast<Plugin *>(_plugin);
    const auto titleBand = drawing::encoderTitleBand(900.0, 620.0);
    if (handleTitleClick(pt, titleBand))
      return;
    auto decoderSnapshot = acquireDecoderSnapshot(*p);
    const auto decoderParams = visibleDecoderParams(
        *p, decoderSnapshot.decoder(), decoderSnapshot.runtimeMixer());
    if (_openMenu > 0) {
      const double itemH = 18.0;
      const double menuW = _openMenu == 1 ? 150.0 : 124.0;
      const drawing::Rect menuRect =
          drawing::makeRect(_menuOrigin.x, _menuOrigin.y, menuW,
                            itemH * static_cast<double>(_menuItemCount));
      if (drawing::contains(pt, menuRect)) {
        const uint32_t index = std::min<uint32_t>(
            _menuItemCount - 1u,
            static_cast<uint32_t>((pt.y - _menuOrigin.y) / itemH));
        switch (_openMenu) {
        case 1:
          guiApplyParam(*p, kLayoutParamId, layoutPresetForMenuIndex(index));
          break;
        case 2:
          guiApplyParam(*p, kModeParamId, index);
          break;
        case 3:
          guiApplyParam(*p, kOrderParamId, index + 1u);
          break;
        case 4:
          guiApplyParam(*p, kWeightingParamId, index);
          break;
        case 5:
          guiApplyParam(*p, kCustomFieldParamId, index);
          break;
        default:
          break;
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuItemCount = 0;
        setNeedsDisplay(true);
        return;
      }
      _openMenu = 0;
      _hoverMenuItem = -1;
      _menuItemCount = 0;
      setNeedsDisplay(true);
    }
    auto openMenu = [&](int menu, uint32_t count, double preferredY) {
      const double itemH = 18.0;
      _openMenu = menu;
      _hoverMenuItem = -1;
      _menuItemCount = count;
      _menuOrigin = drawing::makePoint(
          738.0, std::clamp(preferredY, 28.0,
                            616.0 - itemH * static_cast<double>(count)));
      setNeedsDisplay(true);
    };
    const drawing::Rect fieldPanel = drawing::makeRect(18, 42, 596, 556);
    const drawing::Rect fieldRect = drawing::makeRect(34, 76, 564, 506);
    const drawing::Rect mixerRect = drawing::makeRect(34, 76, 564, 506);
    if (_rightPage == 1 && drawing::contains(pt, mixerRect)) {
      const uint32_t n = std::min<uint32_t>(
          decoderParams.activeSpeakers, s3g::kAmbiSpeakerDecoderMaxSpeakers);
      const int pageCount = static_cast<int>((n + 15u) / 16u);
      if (pageCount > 1) {
        for (int i = 0; i < pageCount; ++i) {
          if (drawing::contains(pt, mixerPageButtonRect_inRect(i, mixerRect))) {
            _mixerPage = i;
            _dragMixerSpeaker = -1;
            _dragMixerOutput = false;

            setNeedsDisplay(true);
            return;
          }
        }
      }
      if (drawing::contains(pt, drawing::inset(mixerOutputTrackRect(mixerRect),
                                               -8.0, -8.0))) {
        double defaultValue = 0.0;
        if (drawing::sliderDoubleClickDefault(event, &p->plugin, kOutputParamId,
                                              &defaultValue)) {
          guiApplyParam(*p, kOutputParamId, defaultValue);
          _dragMixerOutput = false;
          setNeedsDisplay(true);
          return;
        }
        _dragMixerOutput = true;
        updateMixerOutput_inRect(pt, mixerRect);
        return;
      }
      const uint32_t pageStart = static_cast<uint32_t>(_mixerPage) * 16u;
      const uint32_t pageEnd = std::min<uint32_t>(n, pageStart + 16u);
      for (uint32_t i = pageStart; i < pageEnd; ++i) {
        const uint32_t slot = i - pageStart;
        if (drawing::contains(
                pt, drawing::inset(mixerMuteRect_inRect(slot, mixerRect), -4.0,
                                   -4.0))) {
          toggleMixerSpeakerMute(i);
          return;
        }
        if (drawing::contains(
                pt, drawing::inset(mixerSoloRect_inRect(slot, mixerRect), -4.0,
                                   -4.0))) {
          toggleMixerSpeakerSolo(i);
          return;
        }
        if (drawing::contains(
                pt, drawing::inset(mixerSpeakerRect_inRect(i, mixerRect), -6.0,
                                   -7.0))) {
          if (event->clickCount() >= 2) {
            guiApplyParam(*p, kSelectedSpeakerParamId,
                          static_cast<double>(i + 1u));
            guiSpeakerGain(*p, i, 1.0);
            _dragMixerSpeaker = -1;
            _hasSpeakerSelection = true;
            setNeedsDisplay(true);
            return;
          }
          _dragMixerSpeaker = static_cast<int>(i);
          updateMixerSpeakerGain_inRect(pt, mixerRect);
          return;
        }
      }
    }
    if (drawing::contains(pt, fieldPanel)) {
      for (int i = 0; i < 3; ++i) {
        if (drawing::contains(pt, fieldPageButtonRect_inRect(i, fieldPanel))) {
          _rightPage = i;
          _dragMixerSpeaker = -1;
          _dragMixerOutput = false;
          if (_rightPage == 1)
            _hasSpeakerSelection = false;

          setNeedsDisplay(true);
          return;
        }
      }
    }
    if ((_rightPage == 0 || _rightPage == 2) &&
        drawing::contains(pt, fieldPanel)) {
      for (int i = 0; i < 2; ++i) {
        if (drawing::contains(pt, zoomButtonRect_inRect(i, fieldPanel))) {
          const double step = i == 0 ? -0.15 : 0.15;
          _viewZoom = std::clamp(_viewZoom + step, 0.55, 2.20);
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
      if (_rightPage == 0 && drawing::contains(pt, fieldRect)) {
        bool found = false;
        const uint32_t best = hitSpeakerAt_inRect_found(pt, fieldRect, &found);
        if (found) {
          guiApplyParam(*p, kSelectedSpeakerParamId, best + 1u);
          _hasSpeakerSelection = true;
          setNeedsDisplay(true);
          return;
        }
        _hasSpeakerSelection = false;
        _dragView = true;
        _lastDragPoint = pt;
        setNeedsDisplay(true);
        return;
      }
      if (_rightPage == 2 && drawing::contains(pt, fieldRect)) {
        _dragView = true;
        _lastDragPoint = pt;
        setNeedsDisplay(true);
        return;
      }
    }
    if (drawing::contains(pt, drawing::makeRect(738, 143, 102, 17))) {
      openMenu(1, kLayoutMenuCount, 162);
      return;
    }
    if (drawing::contains(pt, drawing::makeRect(738, 169, 102, 17))) {
      openMenu(2, 4, 188);
      return;
    }
    if (drawing::contains(pt, drawing::makeRect(738, 195, 102, 17))) {
      openMenu(3, 7, 214);
      return;
    }
    if (drawing::contains(pt, drawing::makeRect(738, 221, 102, 17))) {
      openMenu(4, 3, 240);
      return;
    }
    if (drawing::contains(pt, drawing::makeRect(738, 247, 102, 17))) {
      openMenu(5, 2, 266);
      return;
    }
    const double rows[] = {78, 274, 300, 366, 392, 418, 444};
    const int ids[] = {13, 4, 6, 7, 8, 9, 10};
    const clap_id params[] = {kOutputParamId,  kActiveSpeakersParamId,
                              kWidthParamId,   kSelectedSpeakerParamId,
                              kAzimuthParamId, kElevationParamId,
                              kDistanceParamId};
    for (int i = 0; i < 7; ++i) {
      if (drawing::contains(pt, drawing::makeRect(638, rows[i] - 8, 230, 24))) {
        double defaultValue = 0.0;
        if (drawing::sliderDoubleClickDefault(event, &p->plugin, params[i],
                                              &defaultValue)) {
          guiApplyParam(*p, params[i], defaultValue);
          _dragSlider = -1;

          setNeedsDisplay(true);
          return;
        }
        _dragSlider = ids[i];
        if (_dragSlider >= 7 && _dragSlider <= 10)
          _hasSpeakerSelection = true;
        updateSlider(pt);
        return;
      }
    }
  }
  void mouseMoved(drawing::Event *event) {

    updateMenuHover(convertPoint_fromView(event->locationInWindow(), nullptr));
  }
  void mouseDragged(drawing::Event *event) {

    drawing::Point pt =
        convertPoint_fromView(event->locationInWindow(), nullptr);
    updateMenuHover(pt);
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
    if (_dragMixerSpeaker >= 0) {
      updateMixerSpeakerGain_inRect(pt, drawing::makeRect(34, 76, 564, 506));
      return;
    }
    if (_dragMixerOutput) {
      updateMixerOutput_inRect(pt, drawing::makeRect(34, 76, 564, 506));
      return;
    }
    if (_dragSlider > 0)
      updateSlider(pt);
  }
  void mouseUp(drawing::Event *event) {
    (void)event;
    _dragSlider = -1;
    _dragView = false;
    _dragMixerSpeaker = -1;
    _dragMixerOutput = false;
  }
  void markStateDirty() {
    if (p.host && p.host->get_extension) {
      auto *state = static_cast<const clap_host_state_t *>(
          p.host->get_extension(p.host, CLAP_EXT_STATE));
      if (state && state->mark_dirty)
        state->mark_dirty(p.host);
    }
  }
  bool handleTitleClick(drawing::Point point,
                        const s3g::gui_layout::EncoderTitleBand &band) {
    if (drawing::contains(point, drawing::cocoaRect(band.presetMenu)))
      resetDecoder();
    else if (drawing::contains(point, drawing::cocoaRect(band.loadButton)))
      presetDialog(false);
    else if (drawing::contains(point, drawing::cocoaRect(band.saveButton)))
      presetDialog(true);
    else
      return false;
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                  presetName.c_str());
    service();
    invalid();
    return true;
  }
#include "s3g_decoder_speaker_editor.inc"
};
} // namespace decoder_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new decoder_canvas::Editor(p);
}
#include "s3g_clap_canvas_gui.inc"
