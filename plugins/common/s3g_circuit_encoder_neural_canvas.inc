// Literal Cocoa adaptation: retain geometry, graphics, controls and hit maps.
namespace circuit_encoder_canvas {
namespace drawing = s3g::portable_gui::circuit_drawing;
namespace foundation = s3g::portable_gui::foundation;
using Kind = s3g::clap_gui::ParamEventKind;
constexpr s3g::gui_layout::Panel kCircuitPanel{
    s3g::gui_layout::PluginClass::ProceduralEncoder,
    s3g::gui_layout::PanelRole::Engine,
    {630.0, 134.0, 250.0, 366.0},
    36.0,
    26.0,
    13u,
};
static_assert(s3g::gui_layout::sourceCardinalityControlMatches(
    kCircuitPanel, s3g::gui_layout::SharedControlRole::SourceCardinality));

enum GuiActionFeedback : int {
  kFeedbackNone = 0,
  kFeedbackSave,
  kFeedbackLoad,
  kFeedbackRandom,
  kFeedbackMutate,
  kFeedbackReseed,
  kFeedbackCaptureA,
  kFeedbackRecallA,
  kFeedbackCaptureB,
  kFeedbackRecallB,
  kFeedbackBreed,
  kFeedbackScorePlay,
  kFeedbackScoreStop,
  kFeedbackScoreGo,
  kFeedbackGrowLattice,
};

struct GuiSliderSpec {
  clap_id id;
  const char *label;
  double x;
  double y;
  double minimum;
  double maximum;
  bool logarithmic;
};

constexpr GuiSliderSpec kGuiSliders[]{
    {kOutputParamId, "OUT", 630, 78, -60.0, 6.0, false},
    {kActivityParamId, "ACTIVITY", 630, 196, 0.0, 1.0, false},
    {kDriveParamId, "SIGMOID", 630, 222, 0.25, 5.0, false},
    {kRingParamId, "RING FB", 630, 248, 0.0, 1.25, false},
    {kMatrixParamId, "MATRIX", 630, 274, 0.0, 1.25, false},
    {kHierarchyParamId, "HIERARCHY", 630, 300, 0.0, 1.0, false},
    {kPhaseParamId, "PHASE", 630, 326, 0.0, 1.0, false},
    {kRegisterParamId, "REGISTER", 630, 352, -48.0, 48.0, false},
    {kTimeSpreadParamId, "TIME SPREAD", 630, 378, 0.0, 1.6, false},
    {kDiversityParamId, "DIVERSITY", 630, 404, 0.0, 1.0, false},
    {kBrownianParamId, "BROWNIAN", 630, 430, 0.0, 1.0, false},
    {kDriftParamId, "DRIFT", 630, 456, 0.0, 1.0, false},
    {kSelfModParamId, "SLOW > FAST", 630, 482, 0.0, 1.0, false},

    {kFieldReturnParamId, "FIELD RETURN", 896, 130, 0.0, 1.0, false},
    {kPropagationParamId, "PROPAGATION", 896, 154, 0.0, 180.0, true},
    {kPickupFocusParamId, "PICKUP FOCUS", 896, 178, 0.0, 1.0, false},
    {kPickupAdaptParamId, "PICKUP ADAPT", 896, 202, 0.0, 1.0, false},
    {kPickupAnchorParamId, "PICKUP ANCHOR", 896, 226, 0.0, 1.0, false},
    {kAuditoryPlasticityParamId, "AUDITORY PLASTIC", 896, 250, 0.0, 1.0, false},
    {kMetabolismParamId, "METABOLISM", 896, 274, 0.0, 1.0, false},
    {kAdaptationParamId, "ADAPTATION", 896, 298, 0.0, 1.0, false},
    {kPlasticityParamId, "CIRCUIT PLASTIC", 896, 322, 0.0, 1.0, false},

    {kScoreVariationParamId, "VARIATION", 48, 664, 0.0, 1.0, false},
    {kScoreRecombineParamId, "RECOMBINE", 48, 690, 0.0, 1.0, false},
    {kScoreMemoryParamId, "MEMORY", 48, 716, 0.0, 1.0, false},
    {kScoreAmountParamId, "AMOUNT", 896, 778, 0.0, 1.0, false},
    {kScoreDwellParamId, "DWELL", 896, 802, 0.25, 60.0, true},
    {kScoreTransitionParamId, "TRANSITION", 896, 826, 0.05, 30.0, true},

    {kAzimuthParamId, "AZIMUTH", 630, 548, -180.0, 180.0, false},
    {kElevationParamId, "ELEVATION", 630, 574, -89.0, 89.0, false},
    {kDistanceParamId, "DISTANCE", 630, 600, 0.10, 8.0, true},
    {kFieldWidthParamId, "FIELD WIDTH", 630, 626, 0.0, 1.0, false},
    {kCellWidthParamId, "CELL WIDTH", 630, 652, 0.0, 1.0, false},
    {kMobilityParamId, "MOBILITY", 630, 678, 0.0, 1.0, false},
    {kInertiaParamId, "INERTIA", 630, 704, 0.0, 1.0, false},
    {kRotationParamId, "ROTATION", 630, 730, -2.0, 2.0, false},
    {kAirParamId, "AIR", 630, 756, 0.0, 1.0, false},
    {kDopplerParamId, "DOPPLER", 630, 782, 0.0, 1.0, false},
};

const GuiSliderSpec *guiSlider(clap_id id) {
  for (const auto &slider : kGuiSliders)
    if (slider.id == id)
      return &slider;
  return nullptr;
}

double sliderNorm(const GuiSliderSpec &slider, double value) {
  if (slider.id == kAzimuthParamId)
    return s3g::aedAzimuthSliderNorm(static_cast<float>(value));
  if (slider.logarithmic) {
    const double minimum = std::max(0.001, slider.minimum);
    const double current = std::max(minimum, value);
    return std::clamp(std::log(current / minimum) /
                          std::log(slider.maximum / minimum),
                      0.0, 1.0);
  }
  return std::clamp((value - slider.minimum) /
                        std::max(0.000001, slider.maximum - slider.minimum),
                    0.0, 1.0);
}

double sliderValue(const GuiSliderSpec &slider, drawing::Point point) {
  const double normalized = std::clamp(
      (static_cast<double>(point.x) - (slider.x + 108.0)) / 82.0, 0.0, 1.0);
  if (slider.id == kAzimuthParamId)
    return s3g::aedAzimuthFromSliderNorm(static_cast<float>(normalized));
  if (slider.logarithmic) {
    const double minimum = std::max(0.001, slider.minimum);
    return minimum * std::pow(slider.maximum / minimum, normalized);
  }
  return slider.minimum + normalized * (slider.maximum - slider.minimum);
}

class Editor final : public s3g::portable_gui::routing::Canvas {
public:
  Plugin *_plugin;

  int _dragParam;
  bool _dragView;
  drawing::Point _lastDragPoint;
  uint32_t _selectedNode;
  int _viewMode;
  double _viewAzDeg;
  double _viewElDeg;
  double _viewZoom;
  int _openMenu;
  int _hoverMenuItem;
  uint32_t _menuItemCount;
  drawing::Rect _openMenuRect;
  int _feedbackAction;
  bool _feedbackSuccess;
  double _feedbackUntil;
  bool _scorePage;
  uint32_t _selectedLatticeCell;
  uint32_t _viewLatticePlane;
  bool _followLatticePlane;
  uint32_t _lastScoreMode;

#include "s3g_circuit_encoder_editor.inc"
  Editor(Plugin &instance, bool popup = false)
      : Canvas(popup ? 596 : kGuiWidth, popup ? 608 : kGuiHeight) {
    auto *plugin = &instance;

    _plugin = plugin;
    _dragParam = 0;
    _dragView = false;
    _lastDragPoint = drawing::Point{};
    _selectedNode = 0u;
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
    _openMenu = 0;
    _hoverMenuItem = -1;
    _menuItemCount = 0u;
    _openMenuRect = drawing::Rect{};
    _feedbackAction = kFeedbackNone;
    _feedbackSuccess = true;
    _feedbackUntil = 0.0;
    _scorePage = plugin ? plugin->guiScorePage != 0u : false;
    _selectedLatticeCell =
        plugin ? std::min<uint32_t>(plugin->guiSelectedLatticeCell,
                                    s3g::kAmbiNeuralLatticeCells - 1u)
               : 5u;
    _viewLatticePlane =
        plugin ? std::min<uint32_t>(plugin->guiLatticeViewPlane,
                                    plugin->guiLatticePlaneCount.load(
                                        std::memory_order_relaxed) -
                                        1u)
               : 0u;
    _followLatticePlane = true;
    _lastScoreMode =
        plugin && plugin->params.scoreMode != s3g::AmbiNeuralScoreMode::Off
            ? static_cast<uint32_t>(plugin->params.scoreMode)
            : static_cast<uint32_t>(s3g::AmbiNeuralScoreMode::Field);

    service();
  }
  ~Editor() override { stopRefresh(); }
  bool loadPresetPath(const std::string &path) {
    CustomPresetFile file{};
    if (!loadCustomPresetFile(path.c_str(), file))
      return false;
    syncGuiParams(*_plugin);
    file.params.outputGainDb = _plugin->params.outputGainDb;
    std::snprintf(_plugin->customPresetName, sizeof(_plugin->customPresetName),
                  "%s", file.name[0] ? file.name : "Custom");
    _plugin->customPresetActive.store(true, std::memory_order_relaxed);
    publishParams(*_plugin, file.params, 0u, true);
    _plugin->scoreTransportRunning.store(file.params.scoreMode !=
                                             s3g::AmbiNeuralScoreMode::Off,
                                         std::memory_order_release);
    storeGenomeSlots(*_plugin, file.genomeA, file.genomeB,
                     file.genomeAValid != 0u, file.genomeBValid != 0u);
    requestLatticeStorage(*_plugin, file.lattice);
    _plugin->resetRequest.fetch_add(1u, std::memory_order_release);
    if (file.version == kCustomPresetVersion) {
      requestGenomeRecall(*_plugin, file.liveGenome);
    } else if (file.genomeAValid || file.genomeBValid) {
      requestGenomeRecall(*_plugin, morphGenomes(file.genomeA, file.genomeB,
                                                 file.genomeAValid != 0u,
                                                 file.genomeBValid != 0u,
                                                 file.params.genomeMorph));
    }
    setNeedsDisplay(true);

    return true;
  }
  void showActionFeedback_success(int action, bool success) {
    _feedbackAction = action;
    _feedbackSuccess = success;
    _feedbackUntil = drawing::uptime() + 0.58;
    setNeedsDisplay(true);
  }

  bool feedbackActive(int action) {
    return _feedbackAction == action && drawing::uptime() < _feedbackUntil;
  }

  void storeViewState() {
    _plugin->guiViewMode = _viewMode;
    _plugin->guiViewAzDeg = static_cast<float>(_viewAzDeg);
    _plugin->guiViewElDeg = static_cast<float>(_viewElDeg);
    _plugin->guiViewZoom = static_cast<float>(_viewZoom);
    _plugin->guiScorePage = _scorePage ? 1u : 0u;
    _plugin->guiSelectedLatticeCell = _selectedLatticeCell;
    _plugin->guiLatticeViewPlane = _viewLatticePlane;
  }

  drawing::String presetDisplayName() {
    if (_plugin->customPresetActive.load(std::memory_order_relaxed) &&
        _plugin->customPresetName[0]) {
      return drawing::stringFromUtf8(_plugin->customPresetName);
    }
    const uint32_t preset =
        std::min<uint32_t>(_plugin->presetIndex.load(std::memory_order_relaxed),
                           s3g::kAmbiNeuralEcologyFactoryPresetCount - 1u);
    return drawing::stringFromUtf8(
        s3g::ambiNeuralEcologyFactoryPresetInfo(preset).name);
  }

  void captureGenomeSlot(uint32_t slot) {
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> a{};
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> b{};
    for (uint32_t index = 0u; index < a.size(); ++index) {
      a[index] = _plugin->genomeA[index].load(std::memory_order_relaxed);
      b[index] = _plugin->genomeB[index].load(std::memory_order_relaxed);
      const float live =
          _plugin->guiGenome[index].load(std::memory_order_relaxed);
      if (slot == 0u)
        a[index] = live;
      else
        b[index] = live;
    }
    const bool validA = slot == 0u || _plugin->genomeAValid.load(
                                          std::memory_order_relaxed) != 0u;
    const bool validB = slot == 1u || _plugin->genomeBValid.load(
                                          std::memory_order_relaxed) != 0u;
    storeGenomeSlots(*_plugin, a, b, validA, validB);
    if (_plugin->host && _plugin->host->request_process)
      _plugin->host->request_process(_plugin->host);
  }

  bool recallGenomeSlot(uint32_t slot) {
    const bool valid =
        (slot == 0u ? _plugin->genomeAValid : _plugin->genomeBValid)
            .load(std::memory_order_relaxed) != 0u;
    if (!valid)
      return false;
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> genome{};
    for (uint32_t index = 0u; index < genome.size(); ++index) {
      genome[index] =
          (slot == 0u ? _plugin->genomeA[index] : _plugin->genomeB[index])
              .load(std::memory_order_relaxed);
    }
    requestGenomeRecall(*_plugin, genome);
    return true;
  }

  drawing::Rect fieldPanelRect() { return drawing::makeRect(18, 42, 596, 804); }

  drawing::Rect fieldRect() { return drawing::makeRect(34, 76, 564, 752); }

  drawing::Rect presetRect() {
    return drawing::encoderTitleActionRect(
        kGuiWidth, kGuiHeight, s3g::gui_layout::EncoderTitleAction::Preset);
  }

  drawing::Rect loadPresetRect() {
    return drawing::encoderTitleActionRect(
        kGuiWidth, kGuiHeight, s3g::gui_layout::EncoderTitleAction::Load);
  }

  drawing::Rect savePresetRect() {
    return drawing::encoderTitleActionRect(
        kGuiWidth, kGuiHeight, s3g::gui_layout::EncoderTitleAction::Save);
  }

  drawing::Rect randomRect() {
    return drawing::encoderTitleActionRect(
        kGuiWidth, kGuiHeight, s3g::gui_layout::EncoderTitleAction::Random);
  }

  drawing::Rect orderRect() { return drawing::makeRect(738, 103, 124, 15); }

  drawing::Rect nodeSetRect() { return drawing::makeRect(738, 169, 124, 15); }

  drawing::Rect listeningModeRect() {
    return drawing::makeRect(1004, 77, 124, 15);
  }

  drawing::Rect pickupSetRect() {
    return drawing::makeRect(1004, 103, 124, 15);
  }

  drawing::Rect plasticityModeRect() {
    return drawing::makeRect(1004, 345, 124, 15);
  }

  drawing::Rect freezeRect() { return drawing::makeRect(1004, 369, 124, 15); }

  drawing::Rect scoreFollowRect() {
    return drawing::makeRect(48, 783, 104, 18);
  }

  drawing::Rect scorePlayRect() { return drawing::makeRect(218, 783, 58, 18); }

  drawing::Rect scoreStopRect() { return drawing::makeRect(282, 783, 58, 18); }

  drawing::Rect scoreGoRect() { return drawing::makeRect(346, 783, 58, 18); }

  drawing::Rect scoreGrowRect() { return drawing::makeRect(410, 783, 172, 18); }

  drawing::Rect scorePageButtonRect(int index) {
    const drawing::Rect panel = fieldPanelRect();
    return drawing::makeRect(panel.origin.x + 280.0 + index * 51.0,
                             panel.origin.y + 4.0, 46.0, 13.0);
  }

  drawing::Rect scoreModeButtonRect(int index) {
    return drawing::makeRect(1002.0 + index * 32.0, 754.0, 29.0, 13.0);
  }

  drawing::Rect scorePlaneCountButtonRect(int index) {
    const drawing::Rect panel = fieldPanelRect();
    return drawing::makeRect(panel.origin.x + 398.0 + index * 42.0,
                             panel.origin.y + 4.0, 38.0, 13.0);
  }

  drawing::Rect scorePlaneTabRect(uint32_t plane) {
    const drawing::Rect field = fieldRect();
    return drawing::makeRect(field.origin.x + 12.0 + plane * 66.0,
                             field.origin.y + 69.0, 58.0, 15.0);
  }

  drawing::Rect scoreCellRect(uint32_t cell) {
    const drawing::Rect field = fieldRect();
    const uint32_t local = cell % s3g::kAmbiNeuralLatticeCellsPerPlane;
    const uint32_t column = local % 4u;
    const uint32_t row = local / 4u;
    return drawing::makeRect(field.origin.x + 28.0 + column * 127.0,
                             field.origin.y + 92.0 + row * 106.0, 112.0, 84.0);
  }

  drawing::Rect viewButtonRect(int index) {
    const drawing::Rect panel = fieldPanelRect();
    return drawing::makeRect(drawing::maxX(panel) - 142.0 + index * 43.0,
                             panel.origin.y + 4.0, 38.0, 13.0);
  }

  drawing::Rect zoomButtonRect(int index) {
    const drawing::Rect panel = fieldPanelRect();
    return drawing::makeRect(drawing::maxX(panel) - 196.0 + index * 23.0,
                             panel.origin.y + 4.0, 18.0, 13.0);
  }

  s3g::Vec3 nodeWorld(uint32_t node) {
    const float azimuth =
        _plugin->guiAzimuth[node].load(std::memory_order_relaxed);
    const float elevation =
        _plugin->guiElevation[node].load(std::memory_order_relaxed);
    const float distance =
        _plugin->guiDistance[node].load(std::memory_order_relaxed);
    const auto direction = s3g::directionFromAed(azimuth, elevation);
    return {direction.x * distance, direction.y * distance,
            direction.z * distance};
  }

  drawing::Point projectWorld_depth(s3g::Vec3 point, double *depth) {
    const drawing::Rect field = fieldRect();
    const double scale = std::min(field.size.width, field.size.height) * 0.34 *
                         std::clamp(_viewZoom, 0.55, 2.20);
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
    return drawing::makePoint(drawing::midX(field) + x1 * scale,
                              drawing::midY(field) - y2 * scale);
  }

  drawing::Point projectNode_depth(uint32_t node, double *depth) {
    return projectWorld_depth(nodeWorld(node), depth);
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
  }

  void drawScore_valueAttrs_style(drawing::Attrs attrs,
                                  drawing::Attrs valueAttrs,
                                  const drawing::Style &style) {
    const drawing::Rect field = fieldRect();
    drawing::setFill(drawing::calibratedColor(0x090909));
    drawing::rectFill(field);
    drawing::setStroke(drawing::calibratedColor(0x555555));
    drawing::frameRect(field);

    const uint32_t planeCount = std::clamp<uint32_t>(
        _plugin->guiLatticePlaneCount.load(std::memory_order_relaxed), 1u,
        s3g::kAmbiNeuralLatticeMaxPlanes);
    const uint32_t activeCells =
        planeCount * s3g::kAmbiNeuralLatticeCellsPerPlane;
    const uint32_t current = std::min<uint32_t>(
        _plugin->guiLatticeCurrentCell.load(std::memory_order_relaxed),
        activeCells - 1u);
    const uint32_t target = std::min<uint32_t>(
        _plugin->guiLatticeTargetCell.load(std::memory_order_relaxed),
        activeCells - 1u);
    if (_followLatticePlane) {
      _viewLatticePlane = current / s3g::kAmbiNeuralLatticeCellsPerPlane;
    }
    _viewLatticePlane = std::min<uint32_t>(_viewLatticePlane, planeCount - 1u);
    const float transition =
        _plugin->guiLatticeTransition.load(std::memory_order_relaxed);
    const float dwell =
        _plugin->guiLatticeDwell.load(std::memory_order_relaxed);
    const bool scoreEnabled =
        _plugin->params.scoreMode != s3g::AmbiNeuralScoreMode::Off;
    const bool transportRunning =
        _plugin->scoreTransportRunning.load(std::memory_order_relaxed);
    drawing::String transportLabel =
        !scoreEnabled ? "OFF" : (transportRunning ? "PLAYING" : "STOPPED");
    static drawing::String directionNames[] = {"N", "NE", "E", "SE",
                                               "S", "SW", "W", "NW"};

    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat(
            "%@   VIEW P%u/%u   ACTIVE P%u:%c%u  >  P%u:%c%u   MOVE %3.0f%%   "
            "DWELL %3.0f%%",
            transportLabel, _viewLatticePlane + 1u, planeCount,
            current / 16u + 1u, 'A' + static_cast<int>((current % 16u) / 4u),
            current % 4u + 1u, target / 16u + 1u,
            'A' + static_cast<int>((target % 16u) / 4u), target % 4u + 1u,
            transition * 100.0f, dwell * 100.0f),
        drawing::makePoint(field.origin.x + 12.0, field.origin.y + 12.0),
        valueAttrs);
    drawing::drawAtPoint_withAttributes(
        "PICKUP VOTE",
        drawing::makePoint(field.origin.x + 12.0, field.origin.y + 36.0),
        attrs);
    const uint32_t pickupCount =
        _plugin->params.pickupSet == s3g::AmbiNeuralPickupSet::Cube8 ? 8u : 4u;
    for (uint32_t slot = 0u; slot < pickupCount; ++slot) {
      const uint32_t direction = pickupCount == 4u ? slot * 2u : slot;
      const float vote = std::min(
          1.0f,
          _plugin->guiLatticeVotes[direction].load(std::memory_order_relaxed) *
              5.0f);
      const double x =
          field.origin.x + 104.0 + slot * (pickupCount == 4u ? 95.0 : 49.0);
      drawing::drawAtPoint_withAttributes(
          directionNames[direction],
          drawing::makePoint(x, field.origin.y + 35.0), valueAttrs);
      const drawing::Rect meter =
          drawing::makeRect(x + 18.0, field.origin.y + 39.0,
                            pickupCount == 4u ? 62.0 : 25.0, 6.0);
      drawing::setFill(drawing::calibratedColor(0x303030));
      drawing::rectFill(meter);
      drawing::setFill(drawing::calibratedColor(0xd8d8d8, 0.28 + vote * 0.72));
      drawing::rectFill(drawing::makeRect(meter.origin.x, meter.origin.y,
                                          meter.size.width * vote,
                                          meter.size.height));
    }
    drawing::drawAtPoint_withAttributes(
        "3D ROUTE: OUT ↑ ENTERS THE NEXT PLANE AT IN ↓  •  LAST PLANE WRAPS TO "
        "P1",
        drawing::makePoint(field.origin.x + 12.0, field.origin.y + 52.0),
        valueAttrs);
    for (uint32_t plane = 0u; plane < planeCount; ++plane) {
      const drawing::Rect tab = scorePlaneTabRect(plane);
      const bool viewed = plane == _viewLatticePlane;
      const bool active =
          plane == current / s3g::kAmbiNeuralLatticeCellsPerPlane;
      const bool targeted =
          transition < 1.0f &&
          plane == target / s3g::kAmbiNeuralLatticeCellsPerPlane;
      drawing::setFill(drawing::calibratedColor(
          active ? 0x686868
                 : (viewed ? 0x505050 : (targeted ? 0x383838 : 0x242424))));
      drawing::rectFill(tab);
      drawing::setStroke(drawing::calibratedColor(
          active ? 0xf0f0f0 : (viewed ? 0xb0b0b0 : 0x5c5c5c)));
      drawing::frameRect(tab);
      if (viewed)
        drawing::frameRect(drawing::inset(tab, 2.0, 2.0));
      drawing::String tabLabel =
          active ? drawing::stringFormat(
                       "P%u %c%u", plane + 1u,
                       'A' + static_cast<int>((current % 16u) / 4u),
                       current % 4u + 1u)
                 : (targeted ? drawing::stringFormat(
                                   "P%u >%c%u", plane + 1u,
                                   'A' + static_cast<int>((target % 16u) / 4u),
                                   target % 4u + 1u)
                             : drawing::stringFormat("P%u", plane + 1u));
      drawing::drawAtPoint_withAttributes(
          tabLabel, drawing::makePoint(tab.origin.x + 6.0, tab.origin.y + 1.0),
          viewed ? attrs : valueAttrs);
    }

    const uint32_t trailCount = std::clamp<uint32_t>(
        _plugin->guiLatticeTrailCount.load(std::memory_order_relaxed), 1u,
        s3g::kAmbiNeuralLatticeTrail);
    drawing::setStroke(drawing::calibratedColor(0xa0a0a0, 0.30));
    drawing::Path trailPath = drawing::Path::bezierPath();
    bool pathOpen = false;
    for (uint32_t index = 0u; index < trailCount; ++index) {
      const uint32_t cell = std::min<uint32_t>(
          _plugin->guiLatticeTrail[index].load(std::memory_order_relaxed),
          activeCells - 1u);
      if (cell / s3g::kAmbiNeuralLatticeCellsPerPlane != _viewLatticePlane) {
        pathOpen = false;
        continue;
      }
      const drawing::Rect rect = scoreCellRect(cell);
      const drawing::Point center =
          drawing::makePoint(drawing::midX(rect), drawing::midY(rect));
      if (!pathOpen) {
        (trailPath).moveToPoint(center);
        pathOpen = true;
      } else {
        const uint32_t previousCell =
            std::min<uint32_t>(_plugin->guiLatticeTrail[index - 1u].load(
                                   std::memory_order_relaxed),
                               activeCells - 1u);
        const drawing::Rect previous = scoreCellRect(previousCell);
        if (previousCell / s3g::kAmbiNeuralLatticeCellsPerPlane !=
                _viewLatticePlane ||
            std::hypot(center.x - drawing::midX(previous),
                       center.y - drawing::midY(previous)) > 170.0) {
          (trailPath).moveToPoint(center);
        } else {
          (trailPath).lineToPoint(center);
        }
      }
    }
    (trailPath).setLineWidth(1.5);
    (trailPath).stroke();

    const uint32_t viewedIngress =
        _plugin->latticeIngressCells[_viewLatticePlane].load(
            std::memory_order_relaxed);
    const uint32_t viewedEgress =
        _plugin->latticeEgressCells[_viewLatticePlane].load(
            std::memory_order_relaxed);
    for (uint32_t localCell = 0u;
         localCell < s3g::kAmbiNeuralLatticeCellsPerPlane; ++localCell) {
      const uint32_t cell =
          _viewLatticePlane * s3g::kAmbiNeuralLatticeCellsPerPlane + localCell;
      const drawing::Rect rect = scoreCellRect(cell);
      std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> genome{};
      for (uint32_t gene = 0u; gene < genome.size(); ++gene) {
        genome[gene] =
            _plugin
                ->latticeGenomes[cell * s3g::kAmbiNeuralEcologyGenomeValues +
                                 gene]
                .load(std::memory_order_relaxed);
      }
      const auto signature = s3g::ambiNeuralGenomeSignature(genome);
      float mean = 0.0f;
      for (const float value : signature)
        mean += value;
      mean /= static_cast<float>(signature.size());
      const bool isCurrent = cell == current;
      const bool isTarget = cell == target && transition < 1.0f;
      const bool isSelected = cell == _selectedLatticeCell;
      const bool isStoppedSelection =
          isSelected && scoreEnabled && !transportRunning;
      const bool isIngress = planeCount > 1u && cell == viewedIngress;
      const bool isEgress = planeCount > 1u && cell == viewedEgress;
      const int shade =
          static_cast<int>(24.0f + mean * 32.0f + (isCurrent ? 34.0f : 0.0f));
      drawing::setFill(drawing::calibratedColor(
          isStoppedSelection ? 0x514722
                             : ((shade << 16) | (shade << 8) | shade)));
      drawing::rectFill(rect);
      drawing::setStroke(drawing::calibratedColor(
          isStoppedSelection
              ? 0xf0cb55
              : (isSelected ? 0xf0f0f0 : (isCurrent ? 0xc8c8c8 : 0x686868))));
      drawing::frameRect(rect);
      if (isSelected) {
        drawing::setStroke(drawing::calibratedColor(
            isStoppedSelection ? 0xffdf70 : 0xf0f0f0, 0.82));
        drawing::frameRect(drawing::inset(rect, 2.0, 2.0));
      }
      if (isTarget) {
        drawing::setStroke(drawing::calibratedColor(0xd8d8d8, 0.72));
        drawing::frameRect(drawing::inset(rect, 3.0, 3.0));
      }
      if (isIngress || isEgress) {
        drawing::setStroke(
            drawing::calibratedColor(isEgress ? 0xe0e0e0 : 0x909090, 0.88));
        drawing::frameRect(drawing::inset(rect, 4.0, 4.0));
      }
      drawing::drawAtPoint_withAttributes(
          drawing::stringFormat("%c%u", 'A' + static_cast<int>(localCell / 4u),
                                localCell % 4u + 1u),
          drawing::makePoint(rect.origin.x + 7.0, rect.origin.y + 6.0),
          isStoppedSelection
              ? drawing::textAttrs(drawing::calibratedColor(0xffdf70), 9.0)
              : (isCurrent ? drawing::softLabelAttrs() : valueAttrs));
      drawing::drawAtPoint_withAttributes(
          drawing::stringFormat("G%u", _plugin->latticeGenerations[cell].load(
                                           std::memory_order_relaxed)),
          drawing::makePoint(rect.origin.x + 82.0, rect.origin.y + 6.0),
          drawing::softLabelAttrs());
      if (isIngress || isEgress) {
        drawing::drawAtPoint_withAttributes(
            (isEgress ? "OUT↑" : "IN↓"),
            drawing::makePoint(rect.origin.x + 38.0, rect.origin.y + 6.0),
            isEgress ? drawing::softLabelAttrs() : valueAttrs);
      }
      static drawing::String signatureNames[] = {"C", "L", "E", "P"};
      for (uint32_t signatureIndex = 0u; signatureIndex < signature.size();
           ++signatureIndex) {
        const double rowY = rect.origin.y + 27.0 + signatureIndex * 13.0;
        drawing::drawAtPoint_withAttributes(
            signatureNames[signatureIndex],
            drawing::makePoint(rect.origin.x + 8.0, rowY - 3.0),
            drawing::softLabelAttrs());
        const drawing::Rect bar =
            drawing::makeRect(rect.origin.x + 22.0, rowY, 82.0, 6.0);
        drawing::setFill(drawing::calibratedColor(0x181818));
        drawing::rectFill(bar);
        drawing::setFill(drawing::calibratedColor(
            isStoppedSelection ? 0xe0bc4f : (isCurrent ? 0xe0e0e0 : 0x9a9a9a),
            0.32 + signature[signatureIndex] * 0.68));
        drawing::rectFill(drawing::makeRect(
            bar.origin.x, bar.origin.y,
            bar.size.width * signature[signatureIndex], bar.size.height));
      }
    }

    const uint32_t selectedGeneration =
        _plugin->latticeGenerations[_selectedLatticeCell].load(
            std::memory_order_relaxed);
    const uint32_t selectedPlane =
        _selectedLatticeCell / s3g::kAmbiNeuralLatticeCellsPerPlane;
    drawing::String selectedPortal = "";
    if (planeCount > 1u && _selectedLatticeCell ==
                               _plugin->latticeIngressCells[selectedPlane].load(
                                   std::memory_order_relaxed)) {
      selectedPortal = drawing::stringFormat(
          "   •   IN FROM P%u",
          selectedPlane == 0u ? planeCount : selectedPlane);
    } else if (planeCount > 1u &&
               _selectedLatticeCell ==
                   _plugin->latticeEgressCells[selectedPlane].load(
                       std::memory_order_relaxed)) {
      selectedPortal = drawing::stringFormat(
          "   •   OUT TO P%u", (selectedPlane + 1u) % planeCount + 1u);
    }
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat(
            "SELECTED P%u:%c%u   —   RESIDENT GENERATION %u%@",
            _selectedLatticeCell / 16u + 1u,
            'A' + static_cast<int>((_selectedLatticeCell % 16u) / 4u),
            _selectedLatticeCell % 4u + 1u, selectedGeneration, selectedPortal),
        drawing::makePoint(field.origin.x + 12.0, field.origin.y + 556.0),
        scoreEnabled && !transportRunning
            ? drawing::textAttrs(drawing::calibratedColor(0xf0cb55), 9.0)
            : attrs);
    for (const auto &slider : kGuiSliders) {
      if (slider.id == kScoreVariationParamId ||
          slider.id == kScoreRecombineParamId ||
          slider.id == kScoreMemoryParamId) {
        drawSlider_style(slider, style);
      }
    }
    drawScoreToggleButton_rect_active(
        (_followLatticePlane ? "FOLLOWING ACTIVE" : "FOLLOW ACTIVE"),
        scoreFollowRect(), _followLatticePlane);
    drawOrganismActionButton_rect_action("PLAY", scorePlayRect(),
                                         kFeedbackScorePlay);
    drawOrganismActionButton_rect_action("STOP", scoreStopRect(),
                                         kFeedbackScoreStop);
    drawOrganismActionButton_rect_action("GO", scoreGoRect(), kFeedbackScoreGo);
    drawOrganismActionButton_rect_action("GROW LATTICE", scoreGrowRect(),
                                         kFeedbackGrowLattice);
    drawing::drawAtPoint_withAttributes(
        "STOP: CLICK A CELL TO AUDITION ITS RESIDENT.  TRANSITION SETS "
        "GESTATION.",
        drawing::makePoint(field.origin.x + 12.0, field.origin.y + 730.0),
        valueAttrs);
  }

  void drawField_valueAttrs_style(drawing::Attrs attrs,
                                  drawing::Attrs valueAttrs,
                                  const drawing::Style &style) {
    const auto p = _plugin->params;
    const drawing::Rect panel = fieldPanelRect();
    const drawing::Rect field = fieldRect();
    drawing::drawPanelFrame(panel.origin.x, panel.origin.y, panel.size.width,
                            panel.size.height, style);
    drawing::drawPanelHeader("SELF-LISTENING AMBISONIC FIELD", true,
                             panel.origin.x, panel.origin.y, panel.size.width,
                             21, attrs, style);
    const drawing::Rect header =
        drawing::makeRect(panel.origin.x, panel.origin.y, panel.size.width, 21);
    drawing::drawToolboxHeaderButton(scorePageButtonRect(0), header, "FIELD",
                                     !_scorePage, attrs, style);
    drawing::drawToolboxHeaderButton(scorePageButtonRect(1), header, "SCORE",
                                     _scorePage, attrs, style);
    if (_scorePage) {
      static drawing::String planeCountLabels[] = {"1P", "2P", "4P", "8P"};
      for (int index = 0; index < 4; ++index) {
        drawing::drawToolboxHeaderButton(
            scorePlaneCountButtonRect(index), header, planeCountLabels[index],
            static_cast<int>(p.scorePlanes) == index, attrs, style);
      }
      drawScore_valueAttrs_style(attrs, valueAttrs, style);
      return;
    }
    drawing::drawToolboxHeaderButton(zoomButtonRect(0), header, "-", false,
                                     attrs, style);
    drawing::drawToolboxHeaderButton(zoomButtonRect(1), header, "+", false,
                                     attrs, style);
    static drawing::String viewLabels[] = {"TOP", "SIDE", "3/4"};
    for (int index = 0; index < 3; ++index) {
      drawing::drawToolboxHeaderButton(viewButtonRect(index), header,
                                       viewLabels[index], _viewMode == index,
                                       attrs, style);
    }

    drawing::setFill(drawing::calibratedColor(0x090909));
    drawing::rectFill(field);
    drawing::setStroke(drawing::calibratedColor(0x555555));
    drawing::frameRect(field);
    drawing::GraphicsScope graphics(*drawing::current->context);
    drawing::clipRect(drawing::inset(field, 1.0, 1.0));
    const double radius =
        std::min(field.size.width, field.size.height) * 0.34 * _viewZoom;
    drawing::setStroke(drawing::calibratedColor(0x303030));
    drawing::Path sphere =
        drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
            drawing::midX(field) - radius, drawing::midY(field) - radius,
            radius * 2.0, radius * 2.0));
    (sphere).setLineWidth(0.8);
    (sphere).stroke();

    std::array<drawing::Point, 64> projected{};
    std::array<float, 64> activation{};
    for (uint32_t node = 0u; node < 64u; ++node) {
      projected[node] = projectNode_depth(node, nullptr);
      activation[node] =
          _plugin->guiActivation[node].load(std::memory_order_relaxed);
    }

    const uint32_t pickupCount =
        p.pickupSet == s3g::AmbiNeuralPickupSet::Cube8 ? 8u : 4u;
    std::array<drawing::Point, s3g::kAmbiNeuralEcologyPickups>
        pickupProjected{};
    std::array<drawing::Point, s3g::kAmbiNeuralEcologyPickups>
        pickupAnchorProjected{};
    for (uint32_t pickup = 0u; pickup < pickupCount; ++pickup) {
      float azimuth =
          _plugin->guiPickupAzimuth[pickup].load(std::memory_order_relaxed);
      float elevation =
          _plugin->guiPickupElevation[pickup].load(std::memory_order_relaxed);
      float anchorAzimuth = _plugin->guiPickupAnchorAzimuth[pickup].load(
          std::memory_order_relaxed);
      float anchorElevation = _plugin->guiPickupAnchorElevation[pickup].load(
          std::memory_order_relaxed);
      if (!_plugin->guiPickupDirectionsReady.load(std::memory_order_acquire)) {
        const auto fallback = s3g::ambi_neural_ecology_detail::kCube[pickup];
        azimuth = anchorAzimuth =
            std::atan2(fallback.y, fallback.x) * 180.0f / s3g::kPi;
        elevation = anchorElevation = std::asin(fallback.z) * 180.0f / s3g::kPi;
      }
      const auto direction = s3g::directionFromAed(azimuth, elevation);
      const auto anchor = s3g::directionFromAed(anchorAzimuth, anchorElevation);
      pickupProjected[pickup] = projectWorld_depth(
          {direction.x * 1.18f, direction.y * 1.18f, direction.z * 1.18f},
          nullptr);
      pickupAnchorProjected[pickup] = projectWorld_depth(
          {anchor.x * 1.18f, anchor.y * 1.18f, anchor.z * 1.18f}, nullptr);
      const double displacement = std::hypot(
          pickupProjected[pickup].x - pickupAnchorProjected[pickup].x,
          pickupProjected[pickup].y - pickupAnchorProjected[pickup].y);
      if (displacement > 0.75) {
        drawing::setStroke(drawing::calibratedColor(0xa0a0a0, 0.42));
        drawing::Path trail = drawing::Path::bezierPath();
        (trail).moveToPoint(pickupAnchorProjected[pickup]);
        (trail).lineToPoint(pickupProjected[pickup]);
        (trail).setLineWidth(0.85);
        (trail).stroke();
        drawing::setStroke(drawing::calibratedColor(0x777777, 0.70));
        drawing::frameRect(
            drawing::makeRect(pickupAnchorProjected[pickup].x - 1.5,
                              pickupAnchorProjected[pickup].y - 1.5, 3.0, 3.0));
      }
    }
    for (uint32_t lobe = 0u; lobe < 4u; ++lobe) {
      drawing::Point center = drawing::Point{};
      double weightSum = 0.0;
      for (uint32_t node = lobe * 16u; node < lobe * 16u + 16u; ++node) {
        center.x += projected[node].x * activation[node];
        center.y += projected[node].y * activation[node];
        weightSum += activation[node];
      }
      if (weightSum < 0.01)
        continue;
      center.x /= weightSum;
      center.y /= weightSum;
      for (uint32_t pickup = 0u; pickup < pickupCount; ++pickup) {
        const float auditoryWeight =
            _plugin
                ->guiAuditoryWeight[lobe * s3g::kAmbiNeuralEcologyPickups +
                                    pickup]
                .load(std::memory_order_relaxed);
        const float magnitude = std::min(1.0f, std::fabs(auditoryWeight));
        if (magnitude < 0.025f)
          continue;
        drawing::setStroke(drawing::calibratedColor(
            auditoryWeight >= 0.0f ? 0xbcbcbc : 0x555555,
            0.06 + magnitude * 0.34));
        drawing::Path path = drawing::Path::bezierPath();
        (path).moveToPoint(pickupProjected[pickup]);
        (path).lineToPoint(center);
        (path).setLineWidth(0.45 + magnitude * 2.1);
        (path).stroke();
      }
      const float bias =
          _plugin->guiHomeostaticBias[lobe].load(std::memory_order_relaxed);
      const float energy = std::min(
          1.0f,
          _plugin->guiLobeEnergy[lobe].load(std::memory_order_relaxed) * 2.5f);
      const double haloSize = 13.0 + std::fabs(bias) * 80.0 + energy * 8.0;
      drawing::setStroke(drawing::calibratedColor(
          bias >= 0.0f ? 0xb0b0b0 : 0x484848, 0.10 + std::fabs(bias) * 1.4));
      drawing::Path halo = drawing::Path::bezierPathWithOvalInRect(
          drawing::makeRect(center.x - haloSize * 0.5,
                            center.y - haloSize * 0.5, haloSize, haloSize));
      (halo).setLineWidth(0.8);
      (halo).stroke();
    }

    for (uint32_t cluster = 0u; cluster < 16u; ++cluster) {
      const uint32_t base = cluster * 4u;
      if (activation[base] < 0.002f)
        continue;
      drawing::Path ring = drawing::Path::bezierPath();
      (ring).moveToPoint(projected[base]);
      for (uint32_t local = 1u; local < 4u; ++local)
        (ring).lineToPoint(projected[base + local]);
      (ring).closePath();
      drawing::setStroke(
          drawing::calibratedColor(0x787878, 0.10 + activation[base] * 0.34));
      (ring).setLineWidth(0.7);
      (ring).stroke();
    }

    for (uint32_t lobe = 0u; lobe < 4u; ++lobe) {
      const uint32_t base = lobe * 16u;
      if (activation[base] < 0.002f)
        continue;
      drawing::setStroke(
          drawing::calibratedColor(0x9a9a9a, 0.12 + activation[base] * 0.20));
      for (uint32_t cluster = 0u; cluster < 4u; ++cluster) {
        const uint32_t next = (cluster + 1u) % 4u;
        drawing::Path::strokeLineFromPoint_toPoint(
            projected[base + cluster * 4u], projected[base + next * 4u]);
      }
    }

    for (uint32_t node = 0u; node < 64u; ++node) {
      if (activation[node] < 0.002f)
        continue;
      const float value =
          _plugin->guiValue[node].load(std::memory_order_relaxed);
      const float energy =
          _plugin->guiEnergy[node].load(std::memory_order_relaxed);
      const bool selected = node == _selectedNode;
      const double size =
          (selected ? 11.0 : 6.0) + std::min(9.0f, energy * 18.0f);
      const int shade =
          static_cast<int>(std::clamp(112.0f + value * 112.0f, 20.0f, 236.0f));
      const drawing::Rect marker =
          drawing::makeRect(projected[node].x - size * 0.5,
                            projected[node].y - size * 0.5, size, size);
      drawing::setFill(
          drawing::calibratedColor((shade << 16) | (shade << 8) | shade,
                                   std::min(1.0f, activation[node] * 0.94f)));
      (drawing::Path::bezierPathWithOvalInRect(marker)).fill();
      drawing::setStroke(drawing::calibratedColor(
          selected ? 0xf0f0f0 : (value >= 0.0f ? 0xb0b0b0 : 0x555555),
          activation[node]));
      (drawing::Path::bezierPathWithOvalInRect(marker)).stroke();
    }

    for (uint32_t pickup = 0u; pickup < pickupCount; ++pickup) {
      const drawing::Point point = pickupProjected[pickup];
      const float heard =
          std::min(1.0f, std::fabs(_plugin->guiPickup[pickup].load(
                             std::memory_order_relaxed)) *
                             5.0f);
      const double size = 9.0 + heard * 13.0;
      drawing::Path ear = drawing::Path::bezierPath();
      (ear).moveToPoint(drawing::makePoint(point.x, point.y - size * 0.62));
      (ear).lineToPoint(drawing::makePoint(point.x + size * 0.55, point.y));
      (ear).lineToPoint(drawing::makePoint(point.x, point.y + size * 0.62));
      (ear).lineToPoint(drawing::makePoint(point.x - size * 0.55, point.y));
      (ear).closePath();
      drawing::setStroke(
          drawing::calibratedColor(0xd8d8d8, 0.22 + heard * 0.66f));
      (ear).setLineWidth(1.0 + heard * 1.5);
      (ear).stroke();
    }
    graphics.restore();

    _selectedNode = std::min<uint32_t>(_selectedNode, 63u);
    const float value =
        _plugin->guiValue[_selectedNode].load(std::memory_order_relaxed);
    const float energy =
        _plugin->guiEnergy[_selectedNode].load(std::memory_order_relaxed);
    drawing::String status = drawing::stringFormat(
        "NODE %02u / LOBE %u / CELL %u   VALUE %+.3f   ENERGY %.3f",
        _selectedNode + 1u, _selectedNode / 16u + 1u,
        (_selectedNode % 16u) / 4u + 1u, value, energy);
    drawing::drawRightStatus(status, drawing::maxX(field), field.origin.y + 7.0,
                             valueAttrs, 8.0);
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat("HOA FIELD  >  %u ADAPTIVE PICKUPS  >  EVOLVING "
                              "AUDITORY BODY  >  LOBE RETURN",
                              pickupCount),
        drawing::makePoint(field.origin.x + 9.0, drawing::maxY(field) - 19.0),
        valueAttrs);
  }

  void drawSlider_style(const GuiSliderSpec &slider,
                        const drawing::Style &style) {
    double value = 0.0;
    paramsGetValue(&_plugin->plugin, slider.id, &value);
    const bool expressed =
        latticeExpressionGovernsParam(slider.id) &&
        _plugin->params.scoreMode != s3g::AmbiNeuralScoreMode::Off &&
        _plugin->params.scoreAmount > 0.0001f;
    if (expressed) {
      const drawing::Rect row =
          drawing::makeRect(slider.x + 8.0, slider.y - 5.0, 224.0, 19.0);
      drawing::setFill(drawing::calibratedColor(0x343434, 0.78));
      drawing::rectFill(row);
      drawing::setFill(drawing::calibratedColor(0xe2e2e2, 0.82));
      drawing::rectFill(
          drawing::makeRect(row.origin.x, row.origin.y, 2.0, row.size.height));
    }
    char display[64]{};
    paramsValueToText(nullptr, slider.id, value, display, sizeof(display));
    drawing::drawSlider(
        drawing::stringFromUtf8(slider.label), drawing::stringFromUtf8(display),
        sliderNorm(slider, value), slider.y, drawing::softLabelAttrs(),
        drawing::softValueAttrs(), style, slider.x + 16.0, slider.x + 108.0,
        slider.x + 196.0, 82.0);
    if (expressed &&
        _plugin->guiEffectiveParametersReady.load(std::memory_order_acquire)) {
      const double effective =
          _plugin->guiEffectiveParameterValues[slider.id].load(
              std::memory_order_relaxed);
      if (std::fabs(effective - value) > 1.0e-5) {
        const double markerX =
            slider.x + 108.0 +
            static_cast<double>(sliderNorm(slider, effective)) * 82.0;
        drawing::setStroke(drawing::calibratedColor(0xffffff));
        drawing::frameRect(
            drawing::makeRect(markerX - 2.0, slider.y - 1.0, 5.0, 13.0));
      }
    }
  }

  void drawMenu_value_x_y_style(drawing::String label, drawing::String value,
                                double x, double y,
                                const drawing::Style &style) {
    drawing::drawMenu(label, value, y, drawing::softLabelAttrs(),
                      drawing::softValueAttrs(), style, x + 16.0, x + 108.0,
                      124.0);
  }

  void drawFeedbackActionButton_rect_action_attrs_style(
      drawing::String label, drawing::Rect rect, int action,
      drawing::Attrs attrs, const drawing::Style &style) {
    if (!feedbackActive(action)) {
      drawing::drawHeaderActionButton(rect, rect, label, attrs, style);
      return;
    }
    drawing::setFill(
        drawing::calibratedColor(_feedbackSuccess ? 0xe4e4e4 : 0x747474));
    drawing::rectFill(rect);
    drawing::setStroke(drawing::calibratedColor(0xffffff));
    drawing::frameRect(rect);
    drawing::Attrs feedbackAttrs = drawing::textAttrs(
        drawing::calibratedColor(_feedbackSuccess ? 0x101010 : 0xffffff), 9.0);
    const drawing::Size size =
        drawing::sizeWithAttributes(label, feedbackAttrs);
    drawing::drawAtPoint_withAttributes(
        label,
        drawing::makePoint(rect.origin.x + (rect.size.width - size.width) * 0.5,
                           rect.origin.y +
                               (rect.size.height - size.height) * 0.5 - 0.5),
        feedbackAttrs);
  }

  void drawOrganismActionButton_rect_action(drawing::String label,
                                            drawing::Rect rect, int action) {
    const bool active = feedbackActive(action);
    drawing::setFill(drawing::calibratedColor(
        active ? (_feedbackSuccess ? 0xe4e4e4 : 0x747474) : 0x484848));
    drawing::rectFill(rect);
    drawing::setStroke(drawing::calibratedColor(active ? 0xffffff : 0xe0e0e0));
    drawing::frameRect(rect);
    drawing::setStroke(drawing::calibratedColor(0x707070));
    drawing::frameRect(drawing::inset(rect, 1.0, 1.0));
    drawing::Attrs buttonAttrs = drawing::textAttrs(
        drawing::calibratedColor(active && _feedbackSuccess ? 0x101010
                                                            : 0xf2f2f2),
        9.0);
    const drawing::Size size = drawing::sizeWithAttributes(label, buttonAttrs);
    drawing::drawAtPoint_withAttributes(
        label,
        drawing::makePoint(rect.origin.x + (rect.size.width - size.width) * 0.5,
                           rect.origin.y +
                               (rect.size.height - size.height) * 0.5 - 0.5),
        buttonAttrs);
  }

  void drawScoreToggleButton_rect_active(drawing::String label,
                                         drawing::Rect rect, bool active) {
    drawing::setFill(drawing::calibratedColor(active ? 0xd8d8d8 : 0x484848));
    drawing::rectFill(rect);
    drawing::setStroke(drawing::calibratedColor(active ? 0xffffff : 0xa0a0a0));
    drawing::frameRect(rect);
    drawing::Attrs buttonAttrs = drawing::textAttrs(
        drawing::calibratedColor(active ? 0x101010 : 0xf0f0f0), 9.0);
    const drawing::Size size = drawing::sizeWithAttributes(label, buttonAttrs);
    drawing::drawAtPoint_withAttributes(
        label,
        drawing::makePoint(rect.origin.x + (rect.size.width - size.width) * 0.5,
                           rect.origin.y +
                               (rect.size.height - size.height) * 0.5 - 0.5),
        buttonAttrs);
  }

  void drawPanels(const drawing::Style &style) {
    const auto p = _plugin->params;
    drawing::Attrs attrs = drawing::softLabelAttrs();

    drawing::drawPanelFrame(630, 42, 250, 80, style);
    drawing::drawPanelHeader("OUTPUT", true, 630, 42, 250, 21, attrs, style);
    if (const auto *output = guiSlider(kOutputParamId)) {
      drawSlider_style(*output, style);
    }
    drawMenu_value_x_y_style("ORDER", drawing::stringFormat("%uOA", p.order),
                             630, 104, style);

    drawing::drawPanelFrame(630, 134, 250, 366, style);
    drawing::drawPanelHeader("SOURCE / CIRCUIT", true, 630, 134, 250, 21, attrs,
                             style);
    drawMenu_value_x_y_style(
        "NODE SET",
        drawing::stringFromUtf8(kNodeSetNames[std::min<uint32_t>(
            static_cast<uint32_t>(p.nodeSet), 4u)]),
        630, 170, style);
    for (const auto &slider : kGuiSliders) {
      if (slider.x == 630 && slider.y >= 196 && slider.y < 510) {
        drawSlider_style(slider, style);
      }
    }

    drawing::drawPanelFrame(896, 42, 246, 392, style);
    drawing::drawPanelHeader("LISTENER / METABOLISM", true, 896, 42, 246, 21,
                             attrs, style);
    drawMenu_value_x_y_style(
        "LISTENING",
        drawing::stringFromUtf8(kListeningNames[std::min<uint32_t>(
            static_cast<uint32_t>(p.listeningMode), 3u)]),
        896, 78, style);
    drawMenu_value_x_y_style(
        "PICKUPS",
        drawing::stringFromUtf8(kPickupSetNames[std::min<uint32_t>(
            static_cast<uint32_t>(p.pickupSet), 1u)]),
        896, 104, style);
    for (const auto &slider : kGuiSliders) {
      if (slider.x == 896 && slider.y < 430)
        drawSlider_style(slider, style);
    }
    drawMenu_value_x_y_style(
        "RULE",
        drawing::stringFromUtf8(kPlasticityNames[std::min<uint32_t>(
            static_cast<uint32_t>(p.plasticityMode), 3u)]),
        896, 346, style);
    drawMenu_value_x_y_style("EVOLUTION", p.freeze ? "FROZEN" : "EVOLVING", 896,
                             370, style);
    const uint32_t pickupCount =
        p.pickupSet == s3g::AmbiNeuralPickupSet::Cube8 ? 8u : 4u;
    drawing::drawAtPoint_withAttributes("PICKUP ENERGY",
                                        drawing::makePoint(912, 394), attrs);
    for (uint32_t pickup = 0u; pickup < pickupCount; ++pickup) {
      const float heard =
          std::min(1.0f, std::fabs(_plugin->guiPickup[pickup].load(
                             std::memory_order_relaxed)) *
                             5.0f);
      const drawing::Rect meter =
          drawing::makeRect(1004 + pickup * 15.4, 395, 12, 9);
      drawing::setFill(drawing::calibratedColor(0x303030));
      drawing::rectFill(meter);
      drawing::setFill(drawing::calibratedColor(0xd0d0d0, 0.28 + heard * 0.72));
      drawing::rectFill(drawing::makeRect(meter.origin.x, meter.origin.y,
                                          meter.size.width * heard,
                                          meter.size.height));
    }
    drawing::drawAtPoint_withAttributes("LOBE ENERGY",
                                        drawing::makePoint(912, 414), attrs);
    for (uint32_t lobe = 0u; lobe < 4u; ++lobe) {
      const float energy = std::min(
          1.0f,
          _plugin->guiLobeEnergy[lobe].load(std::memory_order_relaxed) * 2.5f);
      const drawing::Rect meter =
          drawing::makeRect(1004 + lobe * 31.0, 415, 25, 9);
      drawing::setFill(drawing::calibratedColor(0x303030));
      drawing::rectFill(meter);
      drawing::setFill(
          drawing::calibratedColor(0xc8c8c8, 0.32 + energy * 0.68));
      drawing::rectFill(drawing::makeRect(meter.origin.x, meter.origin.y,
                                          meter.size.width * energy,
                                          meter.size.height));
    }

    drawing::drawPanelFrame(630, 512, 250, 288, style);
    drawing::drawPanelHeader("AMBISONIC FIELD", true, 630, 512, 250, 21, attrs,
                             style);
    for (const auto &slider : kGuiSliders)
      if (slider.x == 630 && slider.y >= 512)
        drawSlider_style(slider, style);

    drawing::drawPanelFrame(896, 450, 246, 284, style);
    drawing::drawPanelHeader("LATTICE STATUS", true, 896, 450, 246, 21, attrs,
                             style);
    const uint32_t latticeCells =
        _plugin->guiLatticePlaneCount.load(std::memory_order_relaxed) *
        s3g::kAmbiNeuralLatticeCellsPerPlane;
    const uint32_t activeResident = std::min<uint32_t>(
        _plugin->guiLatticeCurrentCell.load(std::memory_order_relaxed),
        latticeCells - 1u);
    drawing::drawAtPoint_withAttributes("RESIDENTS",
                                        drawing::makePoint(912, 486), attrs);
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat("%u", latticeCells),
        drawing::makePoint(1020, 486), drawing::softValueAttrs());
    drawing::drawAtPoint_withAttributes("BIRTHS", drawing::makePoint(912, 512),
                                        attrs);
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat(
            "%u", _plugin->latticeBirthCount.load(std::memory_order_relaxed)),
        drawing::makePoint(1020, 512), drawing::softValueAttrs());
    drawing::drawAtPoint_withAttributes("ACTIVE CELL",
                                        drawing::makePoint(912, 538), attrs);
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat("P%u:%c%u", activeResident / 16u + 1u,
                              'A' +
                                  static_cast<int>((activeResident % 16u) / 4u),
                              activeResident % 4u + 1u),
        drawing::makePoint(1020, 538), drawing::softValueAttrs());
    drawing::drawAtPoint_withAttributes("GENERATION",
                                        drawing::makePoint(912, 564), attrs);
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat("%u",
                              _plugin->latticeGenerations[activeResident].load(
                                  std::memory_order_relaxed)),
        drawing::makePoint(1020, 564), drawing::softValueAttrs());
    drawing::drawAtPoint_withAttributes("OPEN SCORE TO SET GENETICS",
                                        drawing::makePoint(912, 616),
                                        drawing::softValueAttrs());
    drawing::drawAtPoint_withAttributes("GROW ONCE • THEN PLAY",
                                        drawing::makePoint(912, 642),
                                        drawing::softValueAttrs());
    drawing::drawAtPoint_withAttributes("MOVES CREATE LIVE OFFSPRING",
                                        drawing::makePoint(912, 668),
                                        drawing::softValueAttrs());

    drawing::drawPanelFrame(896, 750, 246, 94, style);
    drawing::drawPanelHeader("FIELD SCORE", true, 896, 750, 246, 21, attrs,
                             style);
    const drawing::Rect scoreHeader = drawing::makeRect(896, 750, 246, 21);
    static drawing::String shortModeNames[] = {"OFF", "FLD", "MIDI", "CPL"};
    for (int mode = 0; mode < 4; ++mode) {
      drawing::drawToolboxHeaderButton(
          scoreModeButtonRect(mode), scoreHeader, shortModeNames[mode],
          static_cast<int>(p.scoreMode) == mode, attrs, style);
    }
    for (const auto &slider : kGuiSliders) {
      if (slider.x == 896 && slider.y >= 750)
        drawSlider_style(slider, style);
    }
  }

  drawing::Rect menuRect(int menu) {
    switch (menu) {
    case 1:
      return presetRect();
    case 2:
      return nodeSetRect();
    case 3:
      return orderRect();
    case 4:
      return plasticityModeRect();
    case 5:
      return listeningModeRect();
    case 6:
      return pickupSetRect();
    default:
      return drawing::Rect{};
    }
  }

  uint32_t menuCount(int menu) {
    switch (menu) {
    case 1:
      return s3g::kAmbiNeuralEcologyFactoryPresetCount;
    case 2:
      return 5u;
    case 3:
      return 7u;
    case 4:
      return 4u;
    case 5:
      return 4u;
    case 6:
      return 2u;
    default:
      return 0u;
    }
  }

  void openMenu(int menu) {
    _openMenu = menu;
    _menuItemCount = menuCount(menu);
    _hoverMenuItem = -1;
    const drawing::Rect box = menuRect(menu);
    const double height = _menuItemCount * 21.0;
    double y = drawing::maxY(box) + 2.0;
    if (y + height > kGuiHeight - 8.0)
      y = box.origin.y - height - 2.0;
    _openMenuRect = drawing::makeRect(box.origin.x, y, box.size.width, height);
  }

  void drawOpenMenu_style(drawing::Attrs attrs, const drawing::Style &style) {
    if (_openMenu == 0 || _menuItemCount == 0u)
      return;
    static drawing::String nodeItems[] = {"RING 4", "DUAL 8", "CELL 16",
                                          "PAIR 32", "FIELD 64"};
    static drawing::String orderItems[] = {"1OA", "2OA", "3OA", "4OA",
                                           "5OA", "6OA", "7OA"};
    static drawing::String ruleItems[] = {"REINFORCE", "INHIBIT", "BALANCE",
                                          "PRUNE"};
    static drawing::String listeningItems[] = {"LOCAL", "CROSS", "DIFFUSE",
                                               "ROAMING"};
    static drawing::String pickupItems[] = {"TETRA 4", "CUBE 8"};
    static drawing::String
        presetItems[s3g::kAmbiNeuralEcologyFactoryPresetCount];

    for (uint32_t index = 0u; index < s3g::kAmbiNeuralEcologyFactoryPresetCount;
         ++index) {
      presetItems[index] = drawing::stringFromUtf8(
          s3g::ambiNeuralEcologyFactoryPresetInfo(index).name);
    }

    drawing::String *items = presetItems;
    int selected =
        static_cast<int>(_plugin->presetIndex.load(std::memory_order_relaxed));
    if (_openMenu == 2) {
      items = nodeItems;
      selected = static_cast<int>(_plugin->params.nodeSet);
    } else if (_openMenu == 3) {
      items = orderItems;
      selected = static_cast<int>(_plugin->params.order) - 1;
    } else if (_openMenu == 4) {
      items = ruleItems;
      selected = static_cast<int>(_plugin->params.plasticityMode);
    } else if (_openMenu == 5) {
      items = listeningItems;
      selected = static_cast<int>(_plugin->params.listeningMode);
    } else if (_openMenu == 6) {
      items = pickupItems;
      selected = static_cast<int>(_plugin->params.pickupSet);
    }
    drawing::drawDropdownMenu(_openMenuRect, 21.0, items, _menuItemCount,
                              selected, _hoverMenuItem, attrs, style);
  }

  void drawOriginal(drawing::Rect dirtyRect) {
    (void)dirtyRect;
    if (!_plugin)
      return;
    syncGuiParams(*_plugin);
    const auto style = drawing::softTextStyle();
    drawing::setFill(style.bg);
    drawing::rectFill(bounds());
    drawing::Attrs attrs = drawing::softLabelAttrs();
    drawing::Attrs values = drawing::softValueAttrs();
    drawing::drawAtPoint_withAttributes("s3g AMBI ENCODER NEURAL ECOLOGY",
                                        drawing::makePoint(18, 14),
                                        drawing::softTitleAttrs());
    drawing::drawEncoderPresetMenu(
        presetDisplayName(), drawing::encoderTitleBand(kGuiWidth, kGuiHeight),
        attrs, values, style);
    drawFeedbackActionButton_rect_action_attrs_style(
        "LOAD", loadPresetRect(), kFeedbackLoad, attrs, style);
    drawFeedbackActionButton_rect_action_attrs_style(
        "SAVE", savePresetRect(), kFeedbackSave, attrs, style);
    drawFeedbackActionButton_rect_action_attrs_style(
        "RANDOM", randomRect(), kFeedbackRandom, attrs, style);
    drawing::drawRightStatus(drawing::peakDbText(_plugin->outputPeak.load(
                                 std::memory_order_relaxed)),
                             kGuiWidth, 14, values, 18);
    drawField_valueAttrs_style(attrs, values, style);
    drawPanels(style);
    drawOpenMenu_style(values, style);
  }

  int hitNode(drawing::Point point) {
    if (!drawing::contains(point, fieldRect()))
      return -1;
    int best = -1;
    double bestDistance = 14.0;
    for (uint32_t node = 0u; node < 64u; ++node) {
      if (_plugin->guiActivation[node].load(std::memory_order_relaxed) < 0.05f)
        continue;
      const drawing::Point projected = projectNode_depth(node, nullptr);
      const double distance =
          std::hypot(point.x - projected.x, point.y - projected.y);
      if (distance < bestDistance) {
        bestDistance = distance;
        best = static_cast<int>(node);
      }
    }
    return best;
  }

  void setParam_fromPoint(clap_id param, drawing::Point point) {
    const auto *slider = guiSlider(param);
    if (!slider)
      return;
    editValue(param, sliderValue(*slider, point));
  }

  void mouseDown(drawing::Event *event) {
    const drawing::Point point =
        convertPoint_fromView((event)->locationInWindow(), nullptr);
    if (_openMenu > 0) {
      const int hit =
          drawing::dropdownHitIndex(point, _openMenuRect, 21.0, _menuItemCount);
      if (hit >= 0) {
        if (_openMenu == 1)
          editValue(kPresetParamId, hit);
        else if (_openMenu == 2)
          editValue(kNodeSetParamId, hit);
        else if (_openMenu == 3)
          editValue(kOrderParamId, hit + 1);
        else if (_openMenu == 4)
          editValue(kPlasticityModeParamId, hit);
        else if (_openMenu == 5)
          editValue(kListeningModeParamId, hit);
        else if (_openMenu == 6)
          editValue(kPickupSetParamId, hit);
      }
      _openMenu = 0;
      setNeedsDisplay(true);
      return;
    }
    if (drawing::contains(point, presetRect())) {
      openMenu(1);
      return;
    }
    if (drawing::contains(point, nodeSetRect())) {
      openMenu(2);
      return;
    }
    if (drawing::contains(point, orderRect())) {
      openMenu(3);
      return;
    }
    if (drawing::contains(point, plasticityModeRect())) {
      openMenu(4);
      return;
    }
    if (drawing::contains(point, listeningModeRect())) {
      openMenu(5);
      return;
    }
    if (drawing::contains(point, pickupSetRect())) {
      openMenu(6);
      return;
    }
    for (int mode = 0; mode < 4; ++mode) {
      if (drawing::contains(point, scoreModeButtonRect(mode))) {
        if (mode > 0)
          _lastScoreMode = static_cast<uint32_t>(mode);
        editValue(kScoreModeParamId, mode);
        return;
      }
    }
    if (drawing::contains(point, freezeRect())) {
      editValue(kFreezeParamId, _plugin->params.freeze ? 0.0 : 1.0);
      return;
    }
    if (drawing::contains(point, savePresetRect())) {
      if (saveCustomPreset())
        showActionFeedback_success(kFeedbackSave, true);
      return;
    }
    if (drawing::contains(point, loadPresetRect())) {
      if (loadCustomPreset())
        showActionFeedback_success(kFeedbackLoad, true);
      return;
    }
    if (drawing::contains(point, randomRect())) {
      randomizeSafe(*_plugin);
      rescanValues();
      showActionFeedback_success(kFeedbackRandom, true);
      return;
    }
    for (int index = 0; index < 2; ++index) {
      if (drawing::contains(point, scorePageButtonRect(index))) {
        _scorePage = index == 1 ? true : false;
        storeViewState();
        setNeedsDisplay(true);
        return;
      }
    }
    if (_scorePage) {
      if (drawing::contains(point, scoreFollowRect())) {
        _followLatticePlane = !_followLatticePlane;
        if (_followLatticePlane) {
          const uint32_t current =
              _plugin->guiLatticeCurrentCell.load(std::memory_order_relaxed);
          _viewLatticePlane = current / s3g::kAmbiNeuralLatticeCellsPerPlane;
          _selectedLatticeCell = current;
          _plugin->guiSelectedLatticeCell = current;
        }
        storeViewState();
        setNeedsDisplay(true);
        return;
      }
      if (drawing::contains(point, scorePlayRect())) {
        const uint32_t mode = std::clamp<uint32_t>(_lastScoreMode, 1u, 3u);
        editValue(kScoreModeParamId, mode);
        _plugin->scoreTransportRunning.store(true, std::memory_order_release);
        _followLatticePlane = true;
        showActionFeedback_success(kFeedbackScorePlay, true);
        return;
      }
      if (drawing::contains(point, scoreStopRect())) {
        if (_plugin->params.scoreMode != s3g::AmbiNeuralScoreMode::Off) {
          _lastScoreMode = static_cast<uint32_t>(_plugin->params.scoreMode);
        } else {
          editValue(kScoreModeParamId,
                    std::clamp<uint32_t>(_lastScoreMode, 1u, 3u));
        }
        _plugin->scoreTransportRunning.store(false, std::memory_order_release);
        if (_plugin->host && _plugin->host->request_process) {
          _plugin->host->request_process(_plugin->host);
        }
        showActionFeedback_success(kFeedbackScoreStop, true);
        return;
      }
      for (int index = 0; index < 4; ++index) {
        if (drawing::contains(point, scorePlaneCountButtonRect(index))) {
          const uint32_t planes = 1u << index;
          if (s3g::ambiNeuralScorePlaneCount(_plugin->params.scorePlanes) !=
              planes) {
            editValue(kScorePlanesParamId, index);
            _viewLatticePlane =
                std::min<uint32_t>(_viewLatticePlane, planes - 1u);
            const uint32_t local =
                _selectedLatticeCell % s3g::kAmbiNeuralLatticeCellsPerPlane;
            _selectedLatticeCell =
                _viewLatticePlane * s3g::kAmbiNeuralLatticeCellsPerPlane +
                local;
            storeViewState();
          }
          return;
        }
      }
      const uint32_t planeCount = std::clamp<uint32_t>(
          _plugin->guiLatticePlaneCount.load(std::memory_order_relaxed), 1u,
          s3g::kAmbiNeuralLatticeMaxPlanes);
      for (uint32_t plane = 0u; plane < planeCount; ++plane) {
        if (drawing::contains(point, scorePlaneTabRect(plane))) {
          const uint32_t local =
              _selectedLatticeCell % s3g::kAmbiNeuralLatticeCellsPerPlane;
          _viewLatticePlane = plane;
          _followLatticePlane = false;
          _selectedLatticeCell =
              plane * s3g::kAmbiNeuralLatticeCellsPerPlane + local;
          _plugin->guiSelectedLatticeCell = _selectedLatticeCell;
          if (_plugin->params.scoreMode != s3g::AmbiNeuralScoreMode::Off &&
              !_plugin->scoreTransportRunning.load(std::memory_order_acquire)) {
            requestLatticeRecall(*_plugin, _selectedLatticeCell);
          }
          storeViewState();
          setNeedsDisplay(true);
          return;
        }
      }
      for (const auto &slider : kGuiSliders) {
        if ((slider.id == kScoreVariationParamId ||
             slider.id == kScoreRecombineParamId ||
             slider.id == kScoreMemoryParamId) &&
            drawing::contains(point,
                              drawing::makeRect(slider.x + 8.0, slider.y - 8.0,
                                                230.0, 24.0))) {
          double defaultValue = 0.0;
          if (drawing::sliderDoubleClickDefault(event, &_plugin->plugin,
                                                slider.id, &defaultValue)) {
            editValue(slider.id, defaultValue);
            _dragParam = 0;
            setNeedsDisplay(true);
            return;
          }
          _dragParam = static_cast<int>(slider.id);
          setParam_fromPoint(slider.id, point);
          return;
        }
      }
      if (drawing::contains(point, scoreGoRect())) {
        if (_plugin->params.scoreMode == s3g::AmbiNeuralScoreMode::Off) {
          editValue(kScoreModeParamId,
                    std::clamp<uint32_t>(_lastScoreMode, 1u, 3u));
        }
        _plugin->scoreTransportRunning.store(true, std::memory_order_release);
        requestLatticeCell(*_plugin, _selectedLatticeCell, 1.0f);
        _followLatticePlane = true;
        showActionFeedback_success(kFeedbackScoreGo, true);
        return;
      }
      if (drawing::contains(point, scoreGrowRect())) {
        growCurrentLattice(*_plugin);
        _selectedLatticeCell =
            _plugin->guiLatticeCurrentCell.load(std::memory_order_relaxed);
        _plugin->guiSelectedLatticeCell = _selectedLatticeCell;
        _viewLatticePlane =
            _selectedLatticeCell / s3g::kAmbiNeuralLatticeCellsPerPlane;
        showActionFeedback_success(kFeedbackGrowLattice, true);
        return;
      }
      for (uint32_t localCell = 0u;
           localCell < s3g::kAmbiNeuralLatticeCellsPerPlane; ++localCell) {
        if (drawing::contains(point, scoreCellRect(localCell))) {
          _followLatticePlane = false;
          _selectedLatticeCell =
              _viewLatticePlane * s3g::kAmbiNeuralLatticeCellsPerPlane +
              localCell;
          _plugin->guiSelectedLatticeCell = _selectedLatticeCell;
          if (_plugin->params.scoreMode != s3g::AmbiNeuralScoreMode::Off &&
              !_plugin->scoreTransportRunning.load(std::memory_order_acquire)) {
            requestLatticeRecall(*_plugin, _selectedLatticeCell);
          }
          setNeedsDisplay(true);
          return;
        }
      }
      if (drawing::contains(point, fieldRect()))
        return;
    }
    for (int index = 0; index < 2; ++index) {
      if (drawing::contains(point, zoomButtonRect(index))) {
        _viewZoom =
            std::clamp(_viewZoom + (index == 0 ? -0.15 : 0.15), 0.55, 2.20);
        storeViewState();
        return;
      }
    }
    for (int index = 0; index < 3; ++index) {
      if (drawing::contains(point, viewButtonRect(index))) {
        setViewPreset(index);
        return;
      }
    }
    const int node = hitNode(point);
    if (node >= 0) {
      _selectedNode = static_cast<uint32_t>(node);
      return;
    }
    if (drawing::contains(point, fieldRect())) {
      _dragView = true;
      _lastDragPoint = point;
      _viewMode = -1;
      return;
    }
    _dragParam = 0;
    for (const auto &slider : kGuiSliders) {
      if (drawing::contains(
              point,
              drawing::makeRect(slider.x + 8.0, slider.y - 8.0, 230.0, 24.0))) {
        double defaultValue = 0.0;
        if (drawing::sliderDoubleClickDefault(event, &_plugin->plugin,
                                              slider.id, &defaultValue)) {
          editValue(slider.id, defaultValue);
          _dragParam = 0;
          setNeedsDisplay(true);
          return;
        }
        _dragParam = static_cast<int>(slider.id);
        setParam_fromPoint(slider.id, point);
        return;
      }
    }
  }

  void mouseDragged(drawing::Event *event) {
    const drawing::Point point =
        convertPoint_fromView((event)->locationInWindow(), nullptr);
    if (_dragView) {
      _viewAzDeg += (point.x - _lastDragPoint.x) * 0.35;
      _viewElDeg = std::clamp(_viewElDeg + (point.y - _lastDragPoint.y) * 0.35,
                              -85.0, 85.0);
      _lastDragPoint = point;
      storeViewState();
    } else if (_dragParam) {
      setParam_fromPoint(static_cast<clap_id>(_dragParam), point);
    }
  }

  void mouseUp(drawing::Event *event) {
    (void)event;
    _dragParam = 0;
    _dragView = false;
  }

  void mouseMoved(drawing::Event *event) {
    if (_openMenu <= 0)
      return;
    const drawing::Point point =
        convertPoint_fromView((event)->locationInWindow(), nullptr);
    _hoverMenuItem =
        drawing::dropdownHitIndex(point, _openMenuRect, 21.0, _menuItemCount);
  }
};
} // namespace circuit_encoder_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new circuit_encoder_canvas::Editor(p);
}
#include "s3g_clap_canvas_gui.inc"
