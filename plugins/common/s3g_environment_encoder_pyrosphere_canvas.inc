// Literal adaptation of pyrosphere's Cocoa editor: preserve geometry, graphics,
// menus and interactions. DSP/state remain in the original CLAP wrapper.
namespace environment_encoder_canvas {
namespace drawing = s3g::portable_gui::environment_drawing;
namespace foundation = s3g::portable_gui::foundation;
using Kind = s3g::clap_gui::ParamEventKind;
constexpr s3g::gui_layout::Panel kSourcePanel{
    s3g::gui_layout::PluginClass::ProceduralEncoder,
    s3g::gui_layout::PanelRole::Source,
    {630.0, 134.0, 250.0, 210.0},
    36.0,
    26.0,
    7u,
};
static_assert(s3g::gui_layout::sourceCardinalityControlMatches(
    kSourcePanel, s3g::gui_layout::SharedControlRole::SourceCardinality));

struct GuiSliderSpec {
  clap_id id;
  double panelX;
  double y;
  double min;
  double max;
  bool logarithmic;
};

constexpr GuiSliderSpec kGuiSliders[]{
    {kOutputParamId, 630, 78, -60.0, 12.0, false},
    {kPwmAParamId, 630, 614, 0.0, 1.0, false},
    {kPwmBParamId, 630, 640, 0.0, 1.0, false},
    {kRateModeAParamId, 630, 196, s3g::kAmbiPyrosphereMinPlumeWanderHz,
     s3g::kAmbiPyrosphereMaxPlumeWanderHz, true},
    {kVoicesParamId, 630, 170, 1.0, 64.0, false},
    {kRateAParamId, 630, 248, 0.0, 1.0, false},
    {kRateBParamId, 630, 274, s3g::kAmbiPyrosphereMinIgnitionRateHz,
     s3g::kAmbiPyrosphereMaxIgnitionRateHz, true},
    {kSpreadParamId, 630, 300, 0.0, 1.0, false},
    {kDeviationParamId, 630, 326, 0.0, 1.0, false},
    {kFmAtoBParamId, 630, 418, 0.0, 1.0, false},
    {kFmBtoAParamId, 630, 444, 0.0, 1.0, false},
    {kFlutterParamId, 630, 470, 0.0, 1.0, false},
    {kMaterialParamId, 630, 496, 0.0, 1.0, false},
    {kGustShapeParamId, 630, 522, 0.0, 5.0, false},
    {kThresholdParamId, 630, 548, 0.0, 1.0, false},
    {kColorParamId, 630, 666, 0.0, 1.0, false},
    {kFilterParamId, 630, 692, 0.0, 1.0, false},
    {kResonanceParamId, 630, 718, 0.0, 1.0, false},
    {kFilterRunParamId, 630, 744, 0.0, 1.0, false},
    {kFilterSweepParamId, 630, 770, 0.0, 1.0, false},
    {kSaturationParamId, 630, 796, 0.0, 1.0, false},
    {kMotionRateParamId, 896, 78, 0.001, 2.0, true},
    {kMotionFlowParamId, 896, 104, 0.0, 1.0, false},
    {kMotionShearParamId, 896, 130, 0.0, 1.0, false},
    {kMotionCurlParamId, 896, 156, 0.0, 1.0, false},
    {kMotionUpdraftParamId, 896, 182, 0.0, 1.0, false},
    {kFieldParamId, 896, 208, 0.0, 1.0, false},
    {kSpatialFollowParamId, 896, 234, 0.0, 1.0, false},
    {kAzimuthParamId, 896, 300, -180.0, 180.0, false},
    {kElevationParamId, 896, 326, -90.0, 90.0, false},
    {kDistanceParamId, 896, 352, 0.15, 2.0, false},
    {kSpaceParamId, 896, 444, 0.0, 1.0, false},
    {kEnvironmentSizeParamId, 896, 470, 0.0, 1.0, false},
    {kEnvironmentDecayParamId, 896, 496, 0.0, 1.0, false},
    {kEnvironmentDampingParamId, 896, 522, 0.0, 1.0, false},
    {kFieldListenAmountParamId, 896, 574, 0.0, 1.0, false},
    {kParticlesParamId, 896, 666, 0.0, 1.0, false},
    {kVortexParamId, 896, 692, 0.0, 1.0, false},
    {kPressureParamId, 896, 718, 0.0, 1.0, false},
    {kStructuralLoadParamId, 896, 744, 0.0, 1.0, false},
    {kSnapParamId, 896, 770, 0.0, 1.0, false},
    {kFallParamId, 896, 796, 0.0, 1.0, false},
    {kScorePaceParamId, 18, 698, 0.0, 1.0, false},
    {kScoreOccupancyParamId, 18, 724, 0.0, 1.0, false},
    {kScoreCascadeParamId, 18, 750, 0.0, 1.0, false},
    {kScoreMemoryParamId, 310, 698, 0.0, 1.0, false},
    {kScoreRestParamId, 310, 724, 0.0, 1.0, false},
};

const GuiSliderSpec *guiSliderSpec(clap_id id) {
  for (const auto &spec : kGuiSliders) {
    if (spec.id == id)
      return &spec;
  }
  return nullptr;
}

double sliderNorm(const GuiSliderSpec &spec, double value) {
  if (spec.id == kAzimuthParamId)
    return s3g::aedAzimuthSliderNorm(static_cast<float>(value));
  if (spec.logarithmic) {
    if (spec.min <= 0.0) {
      if (value <= 0.0)
        return 0.0;
      constexpr double zeroZone = 0.02;
      const double minPositive = std::max(0.000001, spec.max * 0.001);
      const double logNorm =
          std::log(std::max(minPositive, value) / minPositive) /
          std::log(spec.max / minPositive);
      return std::clamp(zeroZone + (1.0 - zeroZone) * logNorm, zeroZone, 1.0);
    }
    const double minValue = std::max(0.000001, spec.min);
    return std::clamp(std::log(std::max(minValue, value) / minValue) /
                          std::log(spec.max / minValue),
                      0.0, 1.0);
  }
  return std::clamp(
      (value - spec.min) / std::max(0.000001, spec.max - spec.min), 0.0, 1.0);
}

double sliderValue(const GuiSliderSpec &spec, drawing::Point point) {
  const double norm = std::clamp(
      (static_cast<double>(point.x) - (spec.panelX + 108.0)) / 82.0, 0.0, 1.0);
  if (spec.id == kAzimuthParamId)
    return s3g::aedAzimuthFromSliderNorm(static_cast<float>(norm));
  if (spec.logarithmic) {
    if (spec.min <= 0.0) {
      constexpr double zeroZone = 0.02;
      if (norm <= zeroZone)
        return 0.0;
      const double minPositive = std::max(0.000001, spec.max * 0.001);
      const double logNorm = (norm - zeroZone) / (1.0 - zeroZone);
      return minPositive * std::pow(spec.max / minPositive, logNorm);
    }
    return spec.min * std::pow(spec.max / spec.min, norm);
  }
  return spec.min + norm * (spec.max - spec.min);
}

class Editor final : public s3g::portable_gui::routing::Canvas {
public:
  Plugin *_plugin;
  s3g::AmbiPyrosphereParams _paramsSnapshot;
  s3g::AmbiPyrosphereParams _displayParamsSnapshot;
  PyrosphereSurface _surfaceSnapshot;
  uint32_t _presetIndexSnapshot;
  char _customPresetNameSnapshot[64];

  uint32_t _selectedVoice;
  int _dragParam;
  bool _dragView;
  drawing::Point _lastDragPoint;
  int _viewMode;
  double _viewAzDeg;
  double _viewElDeg;
  double _viewZoom;
  int _fieldPage;
  int _dragBreakpointRow;
  int _openMenu;
  int _hoverMenuItem;
  uint32_t _menuItemCount;
  drawing::Rect _openMenuRect;
  bool _surfaceEdit;
  int _selectedSurfaceCell;
  int _dragSurfaceCell;
  bool _dragSurfaceCursor;
  bool _surfacePopupChild;
  Editor *_surfacePopupOwner;

  Editor *_surfacePopupView;

  std::unique_ptr<foundation::AuxiliaryWindow> _surfacePanel;
  std::vector<clap_id> gestures;
  bool pointerEditing = false;
  std::string error;
  Editor(Plugin &instance, bool popup = false)
      : Canvas(popup ? 596 : kGuiWidth, popup ? 608 : kGuiHeight) {
    auto *plugin = &instance;

    _plugin = plugin;
    if (plugin) {
      const auto snapshot = controlSnapshot(*plugin);
      _paramsSnapshot = snapshot.params;
      _displayParamsSnapshot = snapshot.effectiveParams;
      _surfaceSnapshot = snapshot.surface;
      _presetIndexSnapshot = snapshot.presetIndex;
      std::snprintf(_customPresetNameSnapshot,
                    sizeof(_customPresetNameSnapshot), "%s",
                    snapshot.customPresetName);
    } else {
      _paramsSnapshot = {};
      _displayParamsSnapshot = {};
      _surfaceSnapshot = {};
      _presetIndexSnapshot = 0u;
      _customPresetNameSnapshot[0] = '\0';
    }
    _selectedVoice = 0;
    _dragParam = 0;
    _dragView = false;
    _lastDragPoint = drawing::makePoint(0, 0);
    _viewMode = plugin ? plugin->guiViewMode : 0;
    _viewAzDeg = plugin ? plugin->guiViewAzDeg : 38.0;
    _viewElDeg = plugin ? plugin->guiViewElDeg : 32.0;
    _viewZoom = plugin ? plugin->guiViewZoom : 1.0;
    if (_viewMode == 0) {
      _viewAzDeg = 90.0;
      _viewElDeg = 0.0;
    } else if (_viewMode == 1) {
      _viewAzDeg = 90.0;
      _viewElDeg = 90.0;
    } else if (_viewMode == 2) {
      _viewAzDeg = 38.0;
      _viewElDeg = 32.0;
    }
    if (plugin && _viewMode >= 0 && _viewMode <= 2) {
      plugin->guiViewAzDeg = static_cast<float>(_viewAzDeg);
      plugin->guiViewElDeg = static_cast<float>(_viewElDeg);
    }
    _fieldPage = 0;
    _dragBreakpointRow = -1;
    _openMenu = 0;
    _hoverMenuItem = -1;
    _menuItemCount = 0;
    _openMenuRect = drawing::Rect{};
    _surfaceEdit = false;
    _selectedSurfaceCell = -1;
    _dragSurfaceCell = -1;
    _dragSurfaceCursor = false;
    _surfacePopupChild = false;
    _surfacePopupOwner = nullptr;
    _surfacePopupView = nullptr;

    _surfacePopupChild = popup;
    if (popup)
      _fieldPage = 1;
    service();
  }
  ~Editor() override { stopRefresh(); }
#include "s3g_environment_encoder_editor.inc"
  void storeViewState() {
    if (!_plugin)
      return;
    _plugin->guiViewMode = _viewMode;
    _plugin->guiViewAzDeg = static_cast<float>(_viewAzDeg);
    _plugin->guiViewElDeg = static_cast<float>(_viewElDeg);
    _plugin->guiViewZoom = static_cast<float>(_viewZoom);
  }

  void refreshControlSnapshot() {
    if (!_plugin)
      return;
    const auto snapshot = controlSnapshot(*_plugin);
    _paramsSnapshot = snapshot.params;
    _displayParamsSnapshot = snapshot.effectiveParams;
    if (_plugin->appliedSurfaceCommandSequence.load(
            std::memory_order_acquire) >=
        _plugin->queuedSurfaceCommandSequence.load(std::memory_order_acquire)) {
      _surfaceSnapshot = snapshot.surface;
    }
    _presetIndexSnapshot = snapshot.presetIndex;
    std::snprintf(_customPresetNameSnapshot, sizeof(_customPresetNameSnapshot),
                  "%s", snapshot.customPresetName);
  }

  drawing::Rect fieldPanelRect() { return drawing::makeRect(18, 42, 596, 608); }

  drawing::Rect fieldRect() { return drawing::makeRect(34, 76, 564, 558); }

  drawing::Rect presetMenuRect() {
    return drawing::encoderTitleActionRect(
        kGuiWidth, kGuiHeight, s3g::gui_layout::EncoderTitleAction::Preset);
  }

  drawing::Rect loadPresetButtonRect() {
    return drawing::encoderTitleActionRect(
        kGuiWidth, kGuiHeight, s3g::gui_layout::EncoderTitleAction::Load);
  }

  drawing::Rect savePresetButtonRect() {
    return drawing::encoderTitleActionRect(
        kGuiWidth, kGuiHeight, s3g::gui_layout::EncoderTitleAction::Save);
  }

  drawing::Rect randomizeButtonRect() {
    return drawing::encoderTitleActionRect(
        kGuiWidth, kGuiHeight, s3g::gui_layout::EncoderTitleAction::Random);
  }

  drawing::Rect pageButtonRect(int index) {
    return drawing::environmentalFieldPageButtonRect(
        fieldPanelRect(), static_cast<uint32_t>(index));
  }

  drawing::Rect surfacePlotRect() {
    const drawing::Rect field = fieldRect();
    return drawing::makeRect(field.origin.x + 10.0, field.origin.y + 76.0,
                             field.size.width - 20.0, field.size.height - 88.0);
  }

  drawing::Rect surfaceButtonRect(int index) {
    const drawing::Rect field = fieldRect();
    return drawing::makeRect(field.origin.x + 10.0 + index * 52.0,
                             field.origin.y + 10.0, 46.0, 16.0);
  }

  void syncSurfaceEditMode(bool editing) {
    _surfaceEdit = editing;
    if (_surfacePopupChild && _surfacePopupOwner) {
      _surfacePopupOwner->_surfaceEdit = editing;
      (_surfacePopupOwner)->setNeedsDisplay(true);
    } else if (_surfacePopupView) {
      _surfacePopupView->_surfaceEdit = editing;
      (_surfacePopupView)->setNeedsDisplay(true);
    }
  }

  drawing::Rect surfaceCurveRect() {
    const drawing::Rect field = fieldRect();
    return drawing::makeRect(field.origin.x + 10.0, field.origin.y + 38.0,
                             104.0, 18.0);
  }

  drawing::Rect surfaceFocusRect(int index) {
    const drawing::Rect field = fieldRect();
    return drawing::makeRect(field.origin.x + 170.0 + index * 24.0,
                             field.origin.y + 38.0, 20.0, 18.0);
  }

  drawing::Rect surfaceGlideRect(int index) {
    const drawing::Rect field = fieldRect();
    return drawing::makeRect(field.origin.x + 336.0 + index * 24.0,
                             field.origin.y + 38.0, 20.0, 18.0);
  }

  drawing::Rect viewButtonRect(int index) {
    const drawing::Rect panel = fieldPanelRect();
    const double w = 38.0;
    const double gap = 5.0;
    return drawing::makeRect(drawing::maxX(panel) - 10.0 - (3.0 - index) * w -
                                 (2.0 - index) * gap,
                             panel.origin.y + 4.0, w, 13.0);
  }

  drawing::Rect zoomButtonRect(int index) {
    const double w = 18.0;
    const double gap = 4.0;
    const double x = viewButtonRect(0).origin.x - 12.0 - (2.0 - index) * w -
                     (1.0 - index) * gap;
    return drawing::makeRect(x, fieldPanelRect().origin.y + 4.0, w, 13.0);
  }

  s3g::Vec3 voiceWorld(uint32_t voice) {
    if (!_plugin || voice >= s3g::kAmbiPyrosphereMaxVoices)
      return {0.0f, 0.0f, 0.0f};
    const float az = _plugin->guiAzimuth[voice].load(std::memory_order_relaxed);
    const float el =
        _plugin->guiElevation[voice].load(std::memory_order_relaxed);
    const float dist =
        _plugin->guiDistance[voice].load(std::memory_order_relaxed);
    const s3g::Vec3 dir = s3g::directionFromAed(az, el);
    return {dir.x * dist, dir.y * dist, dir.z * dist};
  }

  drawing::Point projectWorld_depth(s3g::Vec3 point, double *depth) {
    const drawing::Rect field = fieldRect();
    const double scale = std::min(field.size.width, field.size.height) * 0.36 *
                         std::clamp(_viewZoom, 0.55, 2.20);
    const double centerX = drawing::midX(field);
    const double centerY = drawing::midY(field);
    const float azimuth = static_cast<float>(_viewAzDeg * s3g::kPi / 180.0);
    const float elevation = static_cast<float>(_viewElDeg * s3g::kPi / 180.0);
    const float ca = std::cos(azimuth);
    const float sa = std::sin(azimuth);
    const float ce = std::cos(elevation);
    const float se = std::sin(elevation);
    const float x1 = ca * point.x - sa * point.y;
    const float y1 = sa * point.x + ca * point.y;
    const float y2 = ce * y1 + se * point.z;
    const float z2 = -se * y1 + ce * point.z;
    if (depth)
      *depth = z2;
    return drawing::makePoint(centerX + x1 * scale, centerY - y2 * scale);
  }

  drawing::Point projectVoice_depth(uint32_t voice, double *depth) {
    return projectWorld_depth(voiceWorld(voice), depth);
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

  VSTGUI::CColor voiceColor_selected(uint32_t voice, bool selected) {
    const float az = _plugin->guiAzimuth[voice].load(std::memory_order_relaxed);
    const float el =
        _plugin->guiElevation[voice].load(std::memory_order_relaxed);
    const float hue = std::fmod((az + 180.0f) / 360.0f + 0.08f, 1.0f);
    const float sat = selected ? 0.72f : 0.52f;
    const float bri =
        selected ? 0.96f : 0.72f + std::max(0.0f, el) / 90.0f * 0.18f;
    return drawing::calibratedHSV(hue, sat, bri, selected ? 1.0 : 0.84);
  }

  void drawSurfacePage_valueAttrs_style(drawing::Attrs attrs,
                                        drawing::Attrs valueAttrs,
                                        const drawing::Style &style) {
    const drawing::Rect field = fieldRect();
    drawing::setFill(drawing::calibratedColor(0x090909));
    drawing::rectFill(field);
    drawing::setStroke(drawing::calibratedColor(0x555555));
    drawing::frameRect(field);
    const drawing::Rect header = drawing::makeRect(
        field.origin.x, field.origin.y, field.size.width, 28.0);
    static drawing::String labels[] = {"PLAY", "ON",  "ADD",
                                       "DEL",  "CAP", "POP"};
    labels[0] = _surfaceEdit ? "EDIT" : "PLAY";
    labels[1] = _surfaceSnapshot.enabled ? "ON" : "OFF";
    for (int index = 0; index < 6; ++index) {
      const bool active =
          (index == 0 && _surfaceEdit) ||
          (index == 1 && _surfaceSnapshot.enabled) ||
          (index == 5 &&
           (_surfacePopupChild || (_surfacePanel && _surfacePanel->visible())));
      drawing::drawToolboxHeaderButton(surfaceButtonRect(index), header,
                                       labels[index], active, attrs, style);
    }
    const drawing::Rect curve = surfaceCurveRect();
    drawing::setFill(style.strip);
    drawing::rectFill(curve);
    drawing::setStroke(style.grid);
    drawing::frameRect(curve);
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat("CURVE  %s", s3g::parameterSurfaceCurveName(
                                               _surfaceSnapshot.curve)),
        drawing::makePoint(curve.origin.x + 5.0, curve.origin.y + 2.0),
        valueAttrs);
    drawing::drawAtPoint_withAttributes(
        "FOCUS",
        drawing::makePoint(field.origin.x + 122.0, field.origin.y + 40.0),
        attrs);
    drawing::drawToolboxHeaderButton(surfaceFocusRect(0), header, "-", false,
                                     attrs, style);
    drawing::drawToolboxHeaderButton(surfaceFocusRect(1), header, "+", false,
                                     attrs, style);
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat("%.2f", _surfaceSnapshot.focus),
        drawing::makePoint(field.origin.x + 222.0, field.origin.y + 40.0),
        valueAttrs);
    drawing::drawAtPoint_withAttributes(
        "GLIDE",
        drawing::makePoint(field.origin.x + 280.0, field.origin.y + 40.0),
        attrs);
    drawing::drawToolboxHeaderButton(surfaceGlideRect(0), header, "-", false,
                                     attrs, style);
    drawing::drawToolboxHeaderButton(surfaceGlideRect(1), header, "+", false,
                                     attrs, style);
    drawing::String glide =
        _surfaceSnapshot.glideMs < 0.5f
            ? "OFF"
            : drawing::stringFormat("%.0f MS", _surfaceSnapshot.glideMs);
    drawing::drawAtPoint_withAttributes(
        glide,
        drawing::makePoint(field.origin.x + 388.0, field.origin.y + 40.0),
        valueAttrs);
    const drawing::Rect plot = surfacePlotRect();
    const bool audioActive = _plugin->active.load(std::memory_order_acquire);
    const float effectiveX =
        audioActive ? _plugin->effectiveSurfaceX.load(std::memory_order_relaxed)
                    : _paramsSnapshot.surfaceX;
    const float effectiveY =
        audioActive ? _plugin->effectiveSurfaceY.load(std::memory_order_relaxed)
                    : _paramsSnapshot.surfaceY;
    drawing::drawParameterSurfaceVoronoi(
        _surfaceSnapshot, plot, effectiveX, effectiveY, _selectedSurfaceCell,
        valueAttrs, _paramsSnapshot.surfaceX, _paramsSnapshot.surfaceY);
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat(
            "T %.3f %.3f   /   A %.3f %.3f   /   %u CELLS   /   %s",
            _paramsSnapshot.surfaceX, _paramsSnapshot.surfaceY, effectiveX,
            effectiveY, _surfaceSnapshot.cellCount,
            _surfaceSnapshot.cellCount < 2u
                ? "ADD TWO CELLS TO ENABLE"
                : (_surfaceSnapshot.enabled ? "INTERPOLATING" : "BYPASSED")),
        drawing::makePoint(plot.origin.x + 8.0, drawing::maxY(plot) - 18.0),
        valueAttrs);
  }

  void drawField_valueAttrs_style(drawing::Attrs attrs,
                                  drawing::Attrs valueAttrs,
                                  const drawing::Style &style) {
    const drawing::Rect panel = fieldPanelRect();
    const drawing::Rect field = fieldRect();
    drawing::drawPanelFrame(panel.origin.x, panel.origin.y, panel.size.width,
                            panel.size.height, style);
    drawing::drawPanelHeader("PYROSPHERE GEO FIELD", true, panel.origin.x,
                             panel.origin.y, panel.size.width, 21, attrs,
                             style);
    const drawing::Rect header =
        drawing::makeRect(panel.origin.x, panel.origin.y, panel.size.width, 21);
    drawing::drawToolboxHeaderButton(pageButtonRect(0), header, "FIELD",
                                     _fieldPage == 0, attrs, style);
    drawing::drawToolboxHeaderButton(pageButtonRect(1), header, "SURF",
                                     _fieldPage == 1, attrs, style);
    if (_fieldPage == 1) {
      drawSurfacePage_valueAttrs_style(attrs, valueAttrs, style);
      return;
    }
    drawing::drawToolboxHeaderButton(zoomButtonRect(0), header, "-", false,
                                     attrs, style);
    drawing::drawToolboxHeaderButton(zoomButtonRect(1), header, "+", false,
                                     attrs, style);
    static drawing::String labels[] = {"TOP", "SIDE", "3/4"};
    for (int i = 0; i < 3; ++i)
      drawing::drawToolboxHeaderButton(viewButtonRect(i), header, labels[i],
                                       i == _viewMode, attrs, style);
    drawing::setFill(drawing::calibratedColor(0x090909));
    drawing::rectFill(field);
    drawing::setStroke(drawing::calibratedColor(0x555555));
    drawing::frameRect(field);
    drawing::GraphicsScope graphics(*drawing::current->context);
    drawing::clipRect(drawing::inset(field, 1, 1));
    const double radius =
        std::min(field.size.width, field.size.height) * 0.36 * _viewZoom;
    drawing::setStroke(drawing::calibratedColor(0x303030));
    drawing::Path sphere =
        drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
            drawing::midX(field) - radius, drawing::midY(field) - radius,
            radius * 2.0, radius * 2.0));
    (sphere).setLineWidth(0.8);
    (sphere).stroke();
    drawing::drawAmbisonicOrientationGuides([&](double x, double y, double z) {
      return projectWorld_depth(
          {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)},
          nullptr);
    });

    const uint32_t voices = std::clamp<uint32_t>(
        _surfaceEdit ? _paramsSnapshot.voices
                     : _plugin->guiVoiceCount.load(std::memory_order_relaxed),
        1u, s3g::kAmbiPyrosphereMaxVoices);
    _selectedVoice = std::min<uint32_t>(_selectedVoice, voices - 1u);
    std::array<drawing::Point, s3g::kAmbiPyrosphereMaxVoices> projected{};
    for (uint32_t voice = 0; voice < voices; ++voice)
      projected[voice] = projectVoice_depth(voice, nullptr);
    for (uint32_t voice = 0; voice < voices; ++voice) {
      const uint32_t next = (voice + 1u) % voices;
      const float edgeMembership =
          _surfaceEdit ? 1.0f
                       : std::min(_plugin->guiRenderGain[voice].load(
                                      std::memory_order_relaxed),
                                  _plugin->guiRenderGain[next].load(
                                      std::memory_order_relaxed));
      drawing::setStroke(drawing::calibratedColor(
          0x5c5c5c, 0.18 * std::clamp(edgeMembership, 0.0f, 1.0f)));
      drawing::Path path = drawing::Path::bezierPath();
      (path).moveToPoint(projected[voice]);
      (path).lineToPoint(projected[next]);
      (path).setLineWidth(0.55);
      (path).stroke();
    }
    drawing::Attrs idAttrs = drawing::textAttrs(
        drawing::calibratedColor(0x080808), voices > 32u ? 5.5 : 7.0);
    for (uint32_t voice = 0; voice < voices; ++voice) {
      const bool selected = voice == _selectedVoice;
      const float energy =
          _plugin->guiEnergy[voice].load(std::memory_order_relaxed);
      const float gust = std::clamp(
          _plugin->guiGust[voice].load(std::memory_order_relaxed), 0.0f, 1.0f);
      const float membership =
          _surfaceEdit ? 1.0f
                       : std::clamp(_plugin->guiRenderGain[voice].load(
                                        std::memory_order_relaxed),
                                    0.0f, 1.0f);
      const float activity =
          std::clamp(std::sqrt(std::max(0.0f, energy)) * 18.0f, 0.0f, 1.0f);
      const double base = voices > 32u ? 7.0 : 9.0;
      const double size =
          ((selected ? base + 5.0 : base) + activity * 5.0f + gust * 7.0f) *
          (0.45f + membership * 0.55f);
      const drawing::Rect marker =
          drawing::makeRect(projected[voice].x - size * 0.5,
                            projected[voice].y - size * 0.5, size, size);
      if (gust > 0.04f || activity > 0.04f) {
        const double halo = size * (1.15 + gust * 1.9 + activity * 0.6);
        drawing::Rect haloRect =
            drawing::makeRect(projected[voice].x - halo * 0.5,
                              projected[voice].y - halo * 0.5, halo, halo);
        drawing::setFill(drawing::withAlpha(
            voiceColor_selected(voice, selected),
            (0.05 + gust * 0.18 + activity * 0.08) * membership));
        (drawing::Path::bezierPathWithOvalInRect(haloRect)).fill();
      }
      drawing::setFill(drawing::withAlpha(
          voiceColor_selected(voice, selected),
          (selected ? 0.98 : 0.22 + gust * 0.54 + activity * 0.20) *
              membership));
      drawing::rectFill(marker);
      drawing::setStroke(drawing::calibratedColor(
          selected ? 0xe6e6e6 : 0x4f4f4f,
          (selected ? 1.0 : 0.22 + gust * 0.46 + activity * 0.18) *
              membership));
      drawing::frameRect(marker);
      drawing::String label = drawing::stringFormat("%u", voice + 1u);
      const drawing::Size labelSize =
          drawing::sizeWithAttributes(label, idAttrs);
      if (membership > 0.45f && (gust > 0.28f || selected)) {
        drawing::drawAtPoint_withAttributes(
            label,
            drawing::makePoint(drawing::midX(marker) - labelSize.width * 0.5,
                               drawing::midY(marker) - labelSize.height * 0.5 -
                                   0.5),
            idAttrs);
      }
    }
    graphics.restore();

    const float az =
        _plugin->guiAzimuth[_selectedVoice].load(std::memory_order_relaxed);
    const float el =
        _plugin->guiElevation[_selectedVoice].load(std::memory_order_relaxed);
    const float dist =
        _plugin->guiDistance[_selectedVoice].load(std::memory_order_relaxed);
    const float energy =
        _plugin->guiEnergy[_selectedVoice].load(std::memory_order_relaxed);
    const float gust =
        _plugin->guiGust[_selectedVoice].load(std::memory_order_relaxed);
    drawing::String readout =
        drawing::stringFormat("V%02u  AZ%+.1f  EL%+.1f  D%.2f  G%.2f  E%.3f",
                              _selectedVoice + 1u, az, el, dist, gust, energy);
    drawing::drawRightStatus(readout, drawing::maxX(field), field.origin.y + 7,
                             valueAttrs, 8.0);
    drawing::drawAtPoint_withAttributes(
        "HEAT / FRACTURE FIELD     MATERIAL FAILURE + PLUME     ACN/SN3D",
        drawing::makePoint(field.origin.x + 9, drawing::maxY(field) - 19),
        valueAttrs);
  }

  void drawSlider_param_value_attrs_valueAttrs_style(
      drawing::String name, clap_id param, double value, drawing::Attrs attrs,
      drawing::Attrs valueAttrs, const drawing::Style &style) {
    const auto *spec = guiSliderSpec(param);
    if (!spec)
      return;
    char display[64]{};
    paramsValueToText(nullptr, param, value, display, sizeof(display));
    drawing::drawSlider(name, drawing::stringFromUtf8(display),
                        sliderNorm(*spec, value), spec->y, attrs, valueAttrs,
                        style, spec->panelX + 16, spec->panelX + 108,
                        spec->panelX + 196, 82);
  }

  void drawMenu_value_panelX_y_attrs_valueAttrs_style(
      drawing::String name, drawing::String value, double panelX, double y,
      drawing::Attrs attrs, drawing::Attrs valueAttrs,
      const drawing::Style &style) {
    drawing::drawMenu(name, value, y, attrs, valueAttrs, style, panelX + 16,
                      panelX + 108, 124);
  }

  drawing::String presetDisplayName() {
    if (_customPresetNameSnapshot[0])
      return drawing::stringFormat("CUSTOM: %s", _customPresetNameSnapshot);
    return drawing::stringFromUtf8(
        s3g::ambiPyrosphereFactoryPresetInfo(_presetIndexSnapshot).name);
  }

  void drawPanels_valueAttrs_style(drawing::Attrs attrs,
                                   drawing::Attrs valueAttrs,
                                   const drawing::Style &style) {
    const auto p = _surfaceEdit ? _paramsSnapshot : _displayParamsSnapshot;
    drawing::drawPanelFrame(18, 662, 596, 152, style);
    drawing::drawPanelHeader("ALEATORIC ENTITY SCORE", true, 18, 662, 596, 21,
                             attrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "PACE", kScorePaceParamId, p.scorePace, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "OCCUPANCY", kScoreOccupancyParamId, p.scoreOccupancy, attrs,
        valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "CASCADE", kScoreCascadeParamId, p.scoreCascade, attrs, valueAttrs,
        style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "MEMORY", kScoreMemoryParamId, p.scoreMemory, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "REST", kScoreRestParamId, p.scoreRest, attrs, valueAttrs, style);
    drawing::String scoreStatus = drawing::stringFormat(
        "%u ENT / %llu ARCS / %.2f ACT",
        _plugin->guiScoreEntities.load(std::memory_order_relaxed),
        static_cast<unsigned long long>(
            _plugin->guiScoreArcs.load(std::memory_order_relaxed)),
        _plugin->guiScoreActivity.load(std::memory_order_relaxed));
    drawing::drawRightStatus(scoreStatus, 614, 669, valueAttrs, 8.0);

    drawing::drawPanelFrame(630, 42, 250, 80, style);
    drawing::drawPanelHeader("OUTPUT", true, 630, 42, 250, 21, attrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "OUT", kOutputParamId, p.outputGainDb, attrs, valueAttrs, style);
    drawMenu_value_panelX_y_attrs_valueAttrs_style(
        "ORDER", drawing::stringFormat("%uOA", p.order), 630, 104, attrs,
        valueAttrs, style);

    drawing::drawPanelFrame(630, 134, 250, 210, style);
    drawing::drawPanelHeader("SOURCE / PYROSPHERE", true, 630, 134, 250, 21,
                             attrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "PLUME WANDER", kRateModeAParamId, p.vectorRateHz, attrs, valueAttrs,
        style);
    drawMenu_value_panelX_y_attrs_valueAttrs_style(
        "MATERIAL",
        drawing::stringFromUtf8(kMaterialNames[std::min<uint32_t>(
            p.materialMode, s3g::kAmbiPyrosphereMaterialCount - 1u)]),
        630, 222, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "VOICES", kVoicesParamId, p.voices, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style("HEAT", kRateAParamId, p.wind,
                                                  attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "IGNITION RATE", kRateBParamId, p.gustRate, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "SPREAD", kSpreadParamId, p.spread, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "DEVIATION", kDeviationParamId, p.deviation, attrs, valueAttrs, style);

    drawing::drawPanelFrame(630, 356, 250, 210, style);
    drawing::drawPanelHeader("COMBUSTION DYNAMICS", true, 630, 356, 250, 21,
                             attrs, style);
    static constexpr const char *kEdgeNames[] = {"SOFT", "BEND", "HARD"};
    drawMenu_value_panelX_y_attrs_valueAttrs_style(
        "IGNITION EDGE",
        drawing::stringFromUtf8(kEdgeNames[std::min<uint32_t>(p.gustEdge, 2u)]),
        630, 392, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "FLARE DEPTH", kFmAtoBParamId, p.gustDepth, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "ROAR", kFmBtoAParamId, p.turbulence, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "FLICKER", kFlutterParamId, p.flutter, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style("MATERIAL LOAD",
                                                  kMaterialParamId, p.material,
                                                  attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "FLAME SHAPE", kGustShapeParamId, p.gustShape, attrs, valueAttrs,
        style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "BURN CORE", kThresholdParamId, p.center, attrs, valueAttrs, style);

    drawing::drawPanelFrame(630, 578, 250, 236, style);
    drawing::drawPanelHeader("AIR / MATERIAL RESPONSE", true, 630, 578, 250, 21,
                             attrs, style);
    drawSlider_param_value_attrs_valueAttrs_style("AIR", kPwmAParamId, p.air,
                                                  attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style("HISS", kPwmBParamId, p.hiss,
                                                  attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "FLAME COLOR", kColorParamId, p.sweep, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "BRITTLENESS", kFilterParamId, p.q, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "COMB DETAIL", kResonanceParamId, p.shrill, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "PYROLYSIS", kFilterRunParamId, p.body, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "OXYGEN", kFilterSweepParamId, p.breath, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "CHAR", kSaturationParamId, p.grit, attrs, valueAttrs, style);

    drawing::drawPanelFrame(896, 42, 246, 210, style);
    drawing::drawPanelHeader("THERMAL TRANSPORT", true, 896, 42, 246, 21, attrs,
                             style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "FIELD RATE", kMotionRateParamId, p.motionRateHz, attrs, valueAttrs,
        style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "FLOW PUSH", kMotionFlowParamId, p.motionFlow, attrs, valueAttrs,
        style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "SHEAR", kMotionShearParamId, p.motionShear, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "CURL", kMotionCurlParamId, p.motionCurl, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "UPDRAFT", kMotionUpdraftParamId, p.motionUpdraft, attrs, valueAttrs,
        style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "DEPTH PUSH", kFieldParamId, p.field, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "INERTIA", kSpatialFollowParamId, p.spatialFollow, attrs, valueAttrs,
        style);

    drawing::drawPanelFrame(896, 264, 246, 106, style);
    drawing::drawPanelHeader("PROJECTION / PLUME VECTOR", true, 896, 264, 246,
                             21, attrs, style);
    drawSlider_param_value_attrs_valueAttrs_style("DIRECTION", kAzimuthParamId,
                                                  p.centerAzimuthDeg, attrs,
                                                  valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "ELEVATION", kElevationParamId, p.centerElevationDeg, attrs, valueAttrs,
        style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "RANGE", kDistanceParamId, p.centerDistance, attrs, valueAttrs, style);

    drawing::drawPanelFrame(896, 382, 246, 236, style);
    drawing::drawPanelHeader("ENVIRONMENT / LISTENER", true, 896, 382, 246, 21,
                             attrs, style);
    drawMenu_value_panelX_y_attrs_valueAttrs_style(
        "PLACE",
        drawing::stringFromUtf8(kPlaceNames[std::min<uint32_t>(
            p.place, s3g::kAmbiPyrospherePlaceCount - 1u)]),
        896, 418, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "ENV RETURN", kSpaceParamId, p.space, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "ENV SIZE", kEnvironmentSizeParamId, p.environmentSize, attrs,
        valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "ENV DECAY", kEnvironmentDecayParamId, p.environmentDecay, attrs,
        valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "ENV DAMPING", kEnvironmentDampingParamId, p.environmentDamping, attrs,
        valueAttrs, style);
    drawMenu_value_panelX_y_attrs_valueAttrs_style(
        "LISTEN",
        drawing::stringFromUtf8(kFieldListenNames[std::min<uint32_t>(
            static_cast<uint32_t>(p.fieldListenMode), 3u)]),
        896, 548, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "LISTEN AMOUNT", kFieldListenAmountParamId, p.fieldListenAmount, attrs,
        valueAttrs, style);
    drawMenu_value_panelX_y_attrs_valueAttrs_style(
        "RESPONSE",
        drawing::stringFromUtf8(kFieldListenResponseNames[std::min<uint32_t>(
            static_cast<uint32_t>(p.fieldListenResponse), 3u)]),
        896, 600, attrs, valueAttrs, style);

    drawing::drawPanelFrame(896, 630, 246, 184, style);
    drawing::drawPanelHeader("STRUCTURAL CONSEQUENCES", true, 896, 630, 246, 21,
                             attrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "FRAGMENTS", kParticlesParamId, p.particles, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "PLUME VORTEX", kVortexParamId, p.vortex, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "PRESSURE", kPressureParamId, p.pressure, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "STRUCT LOAD", kStructuralLoadParamId, p.structuralLoad, attrs,
        valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "BRANCH SNAP", kSnapParamId, p.snap, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "TREE FALL", kFallParamId, p.fall, attrs, valueAttrs, style);
  }

  drawing::Rect menuBoxRect(int menu) {
    switch (menu) {
    case 1:
      return presetMenuRect();
    case 2:
      return drawing::Rect{};
    case 3:
      return drawing::makeRect(738, 221, 124, 15);
    case 4:
      return drawing::makeRect(1004, 417, 124, 15);
    case 5:
      return drawing::makeRect(1004, 547, 124, 15);
    case 6:
      return drawing::makeRect(1004, 599, 124, 15);
    case 7:
      return drawing::Rect{};
    case 8:
      return drawing::Rect{};
    case 9:
      return drawing::makeRect(738, 391, 124, 15);
    case 10:
      return drawing::makeRect(738, 103, 124, 15);
    default:
      return drawing::Rect{};
    }
  }

  uint32_t menuCount(int menu) {
    switch (menu) {
    case 1:
      return s3g::kAmbiPyrosphereFactoryPresetCount;
    case 2:
      return 0u;
    case 3:
      return s3g::kAmbiPyrosphereMaterialCount;
    case 4:
      return s3g::kAmbiPyrospherePlaceCount;
    case 5:
      return 4u;
    case 6:
      return 4u;
    case 7:
      return 0u;
    case 8:
      return 0u;
    case 9:
      return 3u;
    case 10:
      return 7u;
    default:
      return 0u;
    }
  }

  void openMenu(int menu) {
    _openMenu = menu;
    _menuItemCount = menuCount(menu);
    _hoverMenuItem = -1;
    const drawing::Rect box = menuBoxRect(menu);
    const double itemH = 21.0;
    double y = drawing::maxY(box) + 2.0;
    const double height = itemH * _menuItemCount;
    if (y + height > kGuiHeight - 8.0)
      y = box.origin.y - height - 2.0;
    _openMenuRect = drawing::makeRect(box.origin.x, y, box.size.width, height);
    setNeedsDisplay(true);
  }

  void drawOpenMenu_style(drawing::Attrs attrs, const drawing::Style &style) {
    if (_openMenu <= 0 || _menuItemCount == 0u)
      return;
    static drawing::String orderItems[] = {"1OA", "2OA", "3OA", "4OA",
                                           "5OA", "6OA", "7OA"};
    static drawing::String materialItems[] = {"GAS",
                                              "WICK / WAX",
                                              "DUFF / PEAT",
                                              "TIMBER",
                                              "COAL",
                                              "OIL",
                                              "MASONRY",
                                              "METAL",
                                              "EMBERS",
                                              "RESIN",
                                              "GRASS",
                                              "FOREST",
                                              "ROCK / TALUS",
                                              "PRESSURE JET",
                                              "SILICATE CELLS",
                                              "SUPERCRITICAL FRONT",
                                              "SULFUR SLUG VENTS",
                                              "THERMOELASTIC LATTICE"};
    static_assert(std::size(materialItems) ==
                  s3g::kAmbiPyrosphereMaterialCount);
    static drawing::String placeItems[] = {"OPEN",   "CANOPY", "PORCH", "ROOM",
                                           "HANGAR", "CANYON", "TUNNEL"};
    static drawing::String listenItems[] = {"OFF", "FOLLOW", "COUNTER",
                                            "BALANCE"};
    static drawing::String responseItems[] = {"DIRECT", "CHARGE", "SETTLE",
                                              "RESONATE"};
    static drawing::String edgeItems[] = {"SOFT", "BEND", "HARD"};
    static drawing::String presetItems[s3g::kAmbiPyrosphereFactoryPresetCount];

    for (uint32_t i = 0; i < s3g::kAmbiPyrosphereFactoryPresetCount; ++i)
      presetItems[i] =
          drawing::stringFromUtf8(s3g::ambiPyrosphereFactoryPresetInfo(i).name);

    drawing::String *items = presetItems;
    int selected = static_cast<int>(_presetIndexSnapshot);
    if (_openMenu == 3) {
      items = materialItems;
      selected = static_cast<int>(_paramsSnapshot.materialMode);
    } else if (_openMenu == 4) {
      items = placeItems;
      selected = static_cast<int>(_paramsSnapshot.place);
    } else if (_openMenu == 5) {
      items = listenItems;
      selected = static_cast<int>(_paramsSnapshot.fieldListenMode);
    } else if (_openMenu == 6) {
      items = responseItems;
      selected = static_cast<int>(_paramsSnapshot.fieldListenResponse);
    } else if (_openMenu == 9) {
      items = edgeItems;
      selected = static_cast<int>(_paramsSnapshot.gustEdge);
    } else if (_openMenu == 10) {
      items = orderItems;
      selected = static_cast<int>(_paramsSnapshot.order) - 1;
    }
    drawing::drawDropdownMenu(_openMenuRect, 21.0, items, _menuItemCount,
                              selected, _hoverMenuItem, attrs, style);
  }

  int hitVoice(drawing::Point point) {
    if (!drawing::contains(point, fieldRect()))
      return -1;
    const uint32_t voices = std::clamp<uint32_t>(
        _surfaceEdit ? _paramsSnapshot.voices
                     : _plugin->guiVoiceCount.load(std::memory_order_relaxed),
        1u, s3g::kAmbiPyrosphereMaxVoices);
    int best = -1;
    double bestDistance = 15.0;
    for (uint32_t voice = 0; voice < voices; ++voice) {
      const drawing::Point projected = projectVoice_depth(voice, nullptr);
      const double distance =
          std::hypot(point.x - projected.x, point.y - projected.y);
      if (distance < bestDistance) {
        bestDistance = distance;
        best = static_cast<int>(voice);
      }
    }
    return best;
  }

  int hitSurfaceCell(drawing::Point point) {
    const drawing::Rect plot = surfacePlotRect();
    int best = -1;
    double bestDistance = 15.0;
    for (uint32_t index = 0u; index < _surfaceSnapshot.cellCount; ++index) {
      const auto &cell = _surfaceSnapshot.cells[index];
      const drawing::Point site =
          drawing::makePoint(plot.origin.x + cell.x * plot.size.width,
                             drawing::maxY(plot) - cell.y * plot.size.height);
      const double distance = std::hypot(point.x - site.x, point.y - site.y);
      if (distance < bestDistance) {
        bestDistance = distance;
        best = static_cast<int>(index);
      }
    }
    return best;
  }

  void updateSurfacePosition_cursor(drawing::Point point, bool cursor) {
    const drawing::Rect plot = surfacePlotRect();
    const float x = std::clamp(
        static_cast<float>((point.x - plot.origin.x) / plot.size.width), 0.0f,
        1.0f);
    const float y = std::clamp(
        static_cast<float>((drawing::maxY(plot) - point.y) / plot.size.height),
        0.0f, 1.0f);
    if (cursor) {
      _paramsSnapshot.surfaceX = x;
      _paramsSnapshot.surfaceY = y;
      editValue(kSurfaceXParamId, x);
      editValue(kSurfaceYParamId, y);
    } else if (_dragSurfaceCell >= 0 &&
               static_cast<uint32_t>(_dragSurfaceCell) <
                   _surfaceSnapshot.cellCount) {
      auto &cell =
          _surfaceSnapshot.cells[static_cast<uint32_t>(_dragSurfaceCell)];
      cell.x = x;
      cell.y = y;
      submitSurface();
    }
    requestSurfaceProcess(*_plugin);
  }

  void setParam_fromPoint(clap_id param, drawing::Point point) {
    const auto *spec = guiSliderSpec(param);
    if (!spec)
      return;
    const double value = sliderValue(*spec, point);
    assignParam(_paramsSnapshot, param, value);
    editValue(param, value);
    setNeedsDisplay(true);
  }

  void mouseDown(drawing::Event *event) {
    refreshControlSnapshot();
    drawing::Point point =
        convertPoint_fromView((event)->locationInWindow(), nullptr);
    if (_openMenu > 0) {
      const int hit =
          drawing::dropdownHitIndex(point, _openMenuRect, 21.0, _menuItemCount);
      if (hit >= 0) {
        clap_id param = CLAP_INVALID_ID;
        double value = hit;
        if (_openMenu == 1)
          param = kPresetParamId;
        else if (_openMenu == 3)
          param = kRateModeBParamId;
        else if (_openMenu == 4)
          param = kPlaceParamId;
        else if (_openMenu == 5)
          param = kFieldListenModeParamId;
        else if (_openMenu == 6)
          param = kFieldListenResponseParamId;
        else if (_openMenu == 9)
          param = kGustEdgeParamId;
        else if (_openMenu == 10) {
          param = kOrderParamId;
          value = hit + 1;
        }
        if (param != CLAP_INVALID_ID) {
          if (param == kPresetParamId) {
            const float outputGainDb = _paramsSnapshot.outputGainDb;
            _presetIndexSnapshot = static_cast<uint32_t>(hit);
            _paramsSnapshot =
                s3g::ambiPyrosphereFactoryPreset(_presetIndexSnapshot);
            _paramsSnapshot.outputGainDb = outputGainDb;
            _customPresetNameSnapshot[0] = '\0';
          } else {
            assignParam(_paramsSnapshot, param, value);
          }
          editValue(param, value);
        }
      }
      _openMenu = 0;
      _hoverMenuItem = -1;
      setNeedsDisplay(true);
      return;
    }
    if (drawing::contains(point, presetMenuRect())) {
      openMenu(1);
      return;
    }
    if (drawing::contains(point, savePresetButtonRect())) {
      saveCustomPreset();
      return;
    }
    if (drawing::contains(point, loadPresetButtonRect())) {
      loadCustomPreset();
      return;
    }
    if (drawing::contains(point, randomizeButtonRect())) {
      editValue(kRandomizeActionId, 0.0);
      requestSurfaceProcess(*_plugin);
      _selectedVoice = 0;
      setNeedsDisplay(true);
      return;
    }
    if (drawing::contains(point, menuBoxRect(10))) {
      openMenu(10);
      return;
    }
    if (drawing::contains(point, menuBoxRect(3))) {
      openMenu(3);
      return;
    }
    if (drawing::contains(point, menuBoxRect(4))) {
      openMenu(4);
      return;
    }
    if (drawing::contains(point, menuBoxRect(5))) {
      openMenu(5);
      return;
    }
    if (drawing::contains(point, menuBoxRect(6))) {
      openMenu(6);
      return;
    }
    if (drawing::contains(point, menuBoxRect(9))) {
      openMenu(9);
      return;
    }
    const drawing::Rect panel = fieldPanelRect();
    if (drawing::contains(point, panel)) {
      for (int i = 0; i < 2; ++i) {
        if (drawing::contains(point, pageButtonRect(i))) {
          if (_surfacePopupChild)
            return;
          _fieldPage = i;
          setNeedsDisplay(true);
          return;
        }
      }
      if (_fieldPage == 1) {
        if (drawing::contains(point, surfaceButtonRect(5))) {
          if (_surfacePopupChild)
            (_surfacePopupOwner)->hideSurfacePopup();
          else
            openSurfacePopup();
          setNeedsDisplay(true);
          return;
        }
        if (drawing::contains(point, surfaceButtonRect(0))) {
          syncSurfaceEditMode(!_surfaceEdit);
        } else if (drawing::contains(point, surfaceButtonRect(1))) {
          if (_surfaceSnapshot.cellCount < 2u)
            beep();
          else {
            _surfaceSnapshot.enabled = !_surfaceSnapshot.enabled;
            submitSurface();
            requestSurfaceProcess(*_plugin);
          }
        } else if (drawing::contains(point, surfaceButtonRect(2))) {
          drawing::String name = presetDisplayName();
          if (s3g::addParameterSurfaceCell(
                  _surfaceSnapshot, _paramsSnapshot,
                  static_cast<int32_t>(_presetIndexSnapshot), (name).c_str())) {
            _selectedSurfaceCell =
                static_cast<int>(_surfaceSnapshot.cellCount) - 1;
            submitSurface();
          } else
            beep();
        } else if (drawing::contains(point, surfaceButtonRect(3))) {
          if (_selectedSurfaceCell < 0 ||
              !s3g::removeParameterSurfaceCell(
                  _surfaceSnapshot,
                  static_cast<uint32_t>(_selectedSurfaceCell)))
            beep();
          else
            submitSurface();
          _selectedSurfaceCell =
              _surfaceSnapshot.cellCount == 0u
                  ? -1
                  : std::min(_selectedSurfaceCell,
                             static_cast<int>(_surfaceSnapshot.cellCount) - 1);
          requestSurfaceProcess(*_plugin);
        } else if (drawing::contains(point, surfaceButtonRect(4))) {
          if (_selectedSurfaceCell < 0 ||
              static_cast<uint32_t>(_selectedSurfaceCell) >=
                  _surfaceSnapshot.cellCount)
            beep();
          else {
            auto &cell =
                _surfaceSnapshot
                    .cells[static_cast<uint32_t>(_selectedSurfaceCell)];
            cell.params = _paramsSnapshot;
            cell.presetIndex = static_cast<int32_t>(_presetIndexSnapshot);
            std::snprintf(cell.name, sizeof(cell.name), "%s",
                          (presetDisplayName()).c_str());
            submitSurface();
            requestSurfaceProcess(*_plugin);
          }
        } else if (drawing::contains(point, surfaceCurveRect())) {
          const uint32_t next =
              (static_cast<uint32_t>(_surfaceSnapshot.curve) + 1u) %
              s3g::kParameterSurfaceCurveCount;
          _surfaceSnapshot.curve =
              static_cast<s3g::ParameterSurfaceCurve>(next);
          submitSurface();
          requestSurfaceProcess(*_plugin);
        } else if (drawing::contains(point, surfaceFocusRect(0)) ||
                   drawing::contains(point, surfaceFocusRect(1))) {
          const float scale =
              drawing::contains(point, surfaceFocusRect(0)) ? 0.8f : 1.25f;
          _surfaceSnapshot.focus =
              std::clamp(_surfaceSnapshot.focus * scale, 0.25f, 8.0f);
          submitSurface();
          requestSurfaceProcess(*_plugin);
        } else if (drawing::contains(point, surfaceGlideRect(0)) ||
                   drawing::contains(point, surfaceGlideRect(1))) {
          const int direction =
              drawing::contains(point, surfaceGlideRect(0)) ? -1 : 1;
          _surfaceSnapshot.glideMs = s3g::parameterSurfaceSteppedGlide(
              _surfaceSnapshot.glideMs, direction);
          submitSurface();
          requestSurfaceProcess(*_plugin);
        } else if (drawing::contains(point, surfacePlotRect())) {
          if (_surfaceEdit) {
            const int cell = hitSurfaceCell(point);
            if (cell >= 0) {
              _selectedSurfaceCell = cell;
              _dragSurfaceCell = cell;
            }
          } else {
            _dragSurfaceCursor = true;
            updateSurfacePosition_cursor(point, true);
          }
        }
        setNeedsDisplay(true);
        return;
      }
      for (int i = 0; i < 2; ++i) {
        if (drawing::contains(point, zoomButtonRect(i))) {
          _viewZoom =
              std::clamp(_viewZoom + (i == 0 ? -0.15 : 0.15), 0.55, 2.20);
          storeViewState();
          setNeedsDisplay(true);
          return;
        }
      }
      for (int i = 0; i < 3; ++i) {
        if (drawing::contains(point, viewButtonRect(i))) {
          setViewPreset(i);
          return;
        }
      }
      const int hit = hitVoice(point);
      if (hit >= 0) {
        _selectedVoice = static_cast<uint32_t>(hit);
        setNeedsDisplay(true);
        return;
      }
      if (drawing::contains(point, fieldRect())) {
        _dragView = true;
        _lastDragPoint = point;
        _viewMode = -1;
        storeViewState();
        return;
      }
    }
    _dragParam = 0;
    for (const auto &spec : kGuiSliders) {
      if (drawing::contains(
              point, drawing::makeRect(spec.panelX + 8, spec.y - 8, 230, 24))) {
        double defaultValue = 0.0;
        if (drawing::sliderDoubleClickDefault(event, &_plugin->plugin, spec.id,
                                              &defaultValue)) {
          assignParam(_paramsSnapshot, spec.id, defaultValue);
          editValue(spec.id, defaultValue);
          _dragParam = 0;
          setNeedsDisplay(true);
          return;
        }
        _dragParam = static_cast<int>(spec.id);
        setParam_fromPoint(spec.id, point);
        return;
      }
    }
  }

  void mouseDragged(drawing::Event *event) {
    drawing::Point point =
        convertPoint_fromView((event)->locationInWindow(), nullptr);
    if (_fieldPage == 1 && (_dragSurfaceCursor || _dragSurfaceCell >= 0)) {
      updateSurfacePosition_cursor(point, _dragSurfaceCursor);
      setNeedsDisplay(true);
      return;
    }
    if (_dragView) {
      const double dx = point.x - _lastDragPoint.x;
      const double dy = point.y - _lastDragPoint.y;
      _viewAzDeg += dx * 0.35;
      _viewElDeg = std::clamp(_viewElDeg + dy * 0.35, -85.0, 85.0);
      _lastDragPoint = point;
      storeViewState();
      setNeedsDisplay(true);
      return;
    }
    if (_dragParam)
      setParam_fromPoint(static_cast<clap_id>(_dragParam), point);
  }

  void mouseUp(drawing::Event *event) {
    (void)event;
    _dragParam = 0;
    _dragView = false;
    _dragBreakpointRow = -1;
    _dragSurfaceCell = -1;
    _dragSurfaceCursor = false;
  }

  void mouseMoved(drawing::Event *event) {
    if (_openMenu <= 0)
      return;
    drawing::Point point =
        convertPoint_fromView((event)->locationInWindow(), nullptr);
    const int next =
        drawing::dropdownHitIndex(point, _openMenuRect, 21.0, _menuItemCount);
    if (next != _hoverMenuItem) {
      _hoverMenuItem = next;
      setNeedsDisplay(true);
    }
  }
};
} // namespace environment_encoder_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new environment_encoder_canvas::Editor(p);
}
#include "s3g_clap_canvas_gui.inc"
