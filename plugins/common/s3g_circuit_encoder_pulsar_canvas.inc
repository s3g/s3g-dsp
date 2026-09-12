// Literal Cocoa adaptation: retain geometry, graphics, controls and hit maps.
namespace circuit_encoder_canvas {
namespace drawing = s3g::portable_gui::circuit_drawing;
namespace foundation = s3g::portable_gui::foundation;
using Kind = s3g::clap_gui::ParamEventKind;

namespace layout = s3g::gui_layout;

constexpr layout::Canvas kGuiCanvas{static_cast<double>(kGuiWidth),
                                    static_cast<double>(kGuiHeight)};
constexpr auto kOutputPanel = layout::makePanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Output,
    layout::kLargeEncoderFirstColumn, 42.0, layout::toolboxHeightForRows(2u),
    2u);
constexpr auto kClockPanel =
    layout::stackPanel(layout::PanelRole::Engine, kOutputPanel,
                       layout::toolboxHeightForRows(8u), 8u);
constexpr auto kEventPanel =
    layout::stackPanel(layout::PanelRole::EventTiming, kClockPanel,
                       layout::toolboxHeightForRows(9u), 9u);
constexpr auto kFieldOriginPanel =
    layout::stackPanel(layout::PanelRole::Projection, kEventPanel,
                       layout::toolboxHeightForRows(5u), 5u);
constexpr std::array kFirstColumnPanels{kOutputPanel, kClockPanel, kEventPanel,
                                        kFieldOriginPanel};

constexpr auto kDepthMotionPanel = layout::makePanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Motion,
    layout::kLargeEncoderSecondColumn, 42.0, layout::toolboxHeightForRows(6u),
    6u);
constexpr auto kNeuralPanel =
    layout::stackPanel(layout::PanelRole::Engine, kDepthMotionPanel,
                       layout::toolboxHeightForRows(10u), 10u);
constexpr auto kCapturePanel =
    layout::stackPanel(layout::PanelRole::Capture, kNeuralPanel, 272.0, 3u);
constexpr std::array kSecondColumnPanels{kDepthMotionPanel, kNeuralPanel,
                                         kCapturePanel};

static_assert(layout::validateColumn(kFirstColumnPanels, kGuiCanvas));
static_assert(layout::validateColumn(kSecondColumnPanels, kGuiCanvas, false));
static_assert(layout::rolesFollowTemplate(kFirstColumnPanels,
                                          layout::kProceduralEncoderTemplate,
                                          true));
static_assert(layout::rolesFollowTemplate(kSecondColumnPanels,
                                          layout::kProceduralEncoderTemplate,
                                          false));
static_assert(layout::controlMatchesSlot(kOutputPanel,
                                         layout::kLargeEncoderOrderSlot));
static_assert(layout::roleMatchesAnchorIfPresent(
    kSecondColumnPanels, layout::PanelRole::Topology,
    layout::kLargeEncoderTopologyAnchor));
static_assert(layout::sourceCardinalityControlMatches(
    kClockPanel, layout::SharedControlRole::SourceCardinality));

struct GuiSliderSpec {
  clap_id id;
  const char *label;
  double panelX;
  double y;
  double trackWidth;
};

constexpr double kListeningLeftPanelX = 38.0;
constexpr double kListeningRightPanelX = 314.0;

constexpr GuiSliderSpec kGuiSliders[]{
    {kOutputParamId, "OUT", kOutputPanel.frame.x,
     layout::rowY(kOutputPanel, 0u), layout::kStandardMetrics.trackWidth},
    {kEmissionParamId, "EMISSION", kClockPanel.frame.x,
     layout::rowY(kClockPanel, 1u), layout::kStandardMetrics.trackWidth},
    {kEmissionModRateParamId, "EMIT RATE", kClockPanel.frame.x,
     layout::rowY(kClockPanel, 2u), layout::kStandardMetrics.trackWidth},
    {kEmissionModDepthParamId, "EMIT DEPTH", kClockPanel.frame.x,
     layout::rowY(kClockPanel, 3u), layout::kStandardMetrics.trackWidth},
    {kFormantModRateParamId, "FORM RATE", kClockPanel.frame.x,
     layout::rowY(kClockPanel, 4u), layout::kStandardMetrics.trackWidth},
    {kFormantModDepthParamId, "FORM DEPTH", kClockPanel.frame.x,
     layout::rowY(kClockPanel, 5u), layout::kStandardMetrics.trackWidth},
    {kFormantScatterParamId, "FORM SCAT", kClockPanel.frame.x,
     layout::rowY(kClockPanel, 6u), layout::kStandardMetrics.trackWidth},
    {kPhaseScatterParamId, "PHASE SCAT", kClockPanel.frame.x,
     layout::rowY(kClockPanel, 7u), layout::kStandardMetrics.trackWidth},

    {kProbabilityParamId, "PROB", kEventPanel.frame.x,
     layout::rowY(kEventPanel, 0u), layout::kStandardMetrics.trackWidth},
    {kBurstOnParamId, "BURST ON", kEventPanel.frame.x,
     layout::rowY(kEventPanel, 1u), layout::kStandardMetrics.trackWidth},
    {kBurstOffParamId, "REST", kEventPanel.frame.x,
     layout::rowY(kEventPanel, 2u), layout::kStandardMetrics.trackWidth},
    {kSieveModuloParamId, "SIEVE MOD", kEventPanel.frame.x,
     layout::rowY(kEventPanel, 3u), layout::kStandardMetrics.trackWidth},
    {kSieveResidueParamId, "RESIDUE", kEventPanel.frame.x,
     layout::rowY(kEventPanel, 4u), layout::kStandardMetrics.trackWidth},
    {kDistributionParamId, "POINT RAND", kEventPanel.frame.x,
     layout::rowY(kEventPanel, 5u), layout::kStandardMetrics.trackWidth},
    {kEnvelopeEdgeParamId, "ENV EDGE", kEventPanel.frame.x,
     layout::rowY(kEventPanel, 7u), layout::kStandardMetrics.trackWidth},

    {kAzimuthParamId, "AZIMUTH", kFieldOriginPanel.frame.x,
     layout::rowY(kFieldOriginPanel, 0u), layout::kStandardMetrics.trackWidth},
    {kElevationParamId, "ELEVATION", kFieldOriginPanel.frame.x,
     layout::rowY(kFieldOriginPanel, 1u), layout::kStandardMetrics.trackWidth},
    {kDistanceParamId, "DISTANCE", kFieldOriginPanel.frame.x,
     layout::rowY(kFieldOriginPanel, 2u), layout::kStandardMetrics.trackWidth},
    {kWidthParamId, "WIDTH", kFieldOriginPanel.frame.x,
     layout::rowY(kFieldOriginPanel, 3u), layout::kStandardMetrics.trackWidth},
    {kSpatialScatterParamId, "SPAT SCAT", kFieldOriginPanel.frame.x,
     layout::rowY(kFieldOriginPanel, 4u), layout::kStandardMetrics.trackWidth},

    {kOrbitRateParamId, "ORBIT RATE", kDepthMotionPanel.frame.x,
     layout::rowY(kDepthMotionPanel, 0u), layout::kStandardMetrics.trackWidth},
    {kOrbitDepthParamId, "ORBIT DEPTH", kDepthMotionPanel.frame.x,
     layout::rowY(kDepthMotionPanel, 1u), layout::kStandardMetrics.trackWidth},
    {kSpatialFollowParamId, "INERTIA", kDepthMotionPanel.frame.x,
     layout::rowY(kDepthMotionPanel, 2u), layout::kStandardMetrics.trackWidth},
    {kAirParamId, "AIR", kDepthMotionPanel.frame.x,
     layout::rowY(kDepthMotionPanel, 3u), layout::kStandardMetrics.trackWidth},
    {kDopplerParamId, "DOPPLER", kDepthMotionPanel.frame.x,
     layout::rowY(kDepthMotionPanel, 4u), layout::kStandardMetrics.trackWidth},

    {kNeuralLevelParamId, "DIRECT", kNeuralPanel.frame.x,
     layout::rowY(kNeuralPanel, 0u), layout::kStandardMetrics.trackWidth},
    {kNeuralDriveParamId, "SIGMOID", kNeuralPanel.frame.x,
     layout::rowY(kNeuralPanel, 1u), layout::kStandardMetrics.trackWidth},
    {kNeuralFeedbackParamId, "RING FB", kNeuralPanel.frame.x,
     layout::rowY(kNeuralPanel, 2u), layout::kStandardMetrics.trackWidth},
    {kNeuralCouplingParamId, "MATRIX", kNeuralPanel.frame.x,
     layout::rowY(kNeuralPanel, 3u), layout::kStandardMetrics.trackWidth},
    {kNeuralHierarchyParamId, "HIERARCHY", kNeuralPanel.frame.x,
     layout::rowY(kNeuralPanel, 4u), layout::kStandardMetrics.trackWidth},
    {kNeuralPhaseParamId, "PHASE", kNeuralPanel.frame.x,
     layout::rowY(kNeuralPanel, 5u), layout::kStandardMetrics.trackWidth},
    {kNeuralBrownianParamId, "BROWNIAN", kNeuralPanel.frame.x,
     layout::rowY(kNeuralPanel, 6u), layout::kStandardMetrics.trackWidth},
    {kNeuralDriftParamId, "DRIFT", kNeuralPanel.frame.x,
     layout::rowY(kNeuralPanel, 7u), layout::kStandardMetrics.trackWidth},
    {kNeuralSelfModParamId, "SLOW > FAST", kNeuralPanel.frame.x,
     layout::rowY(kNeuralPanel, 8u), layout::kStandardMetrics.trackWidth},
    {kNeuralAudioFeedbackParamId, "AUDIO FB", kNeuralPanel.frame.x,
     layout::rowY(kNeuralPanel, 9u), layout::kStandardMetrics.trackWidth},

    {kNeuralPulsaretParamId, "PULSARET", kCapturePanel.frame.x,
     layout::rowY(kCapturePanel, 0u), layout::kStandardMetrics.trackWidth},
    {kNeuralEnvelopeParamId, "ENVELOPE", kCapturePanel.frame.x,
     layout::rowY(kCapturePanel, 1u), layout::kStandardMetrics.trackWidth},
    {kNeuralFmParamId, "FM DEPTH", kCapturePanel.frame.x,
     layout::rowY(kCapturePanel, 2u), layout::kStandardMetrics.trackWidth},

    {kFieldReturnParamId, "FIELD RETURN", kListeningLeftPanelX, 540.0, 82.0},
    {kPropagationParamId, "PROPAGATION", kListeningLeftPanelX, 574.0, 82.0},
    {kPickupFocusParamId, "FOCUS", kListeningRightPanelX, 540.0, 82.0},
    {kLaneListeningParamId, "ECOLOGY", kListeningRightPanelX, 574.0, 82.0},

    {kLaneFormantIds[0], "CARRIER HZ", 18.0, 702.0, 82.0},
    {kLaneCarrierRatioIds[0], "CLOCK RATIO", 18.0, 726.0, 82.0},
    {kLaneOverlapIds[0], "OVERLAP", 18.0, 750.0, 82.0},
    {kLaneLevelIds[0], "LEVEL", 18.0, 774.0, 82.0},
    {kLaneOffsetIds[0], "OFFSET", 18.0, 798.0, 82.0},
    {kLaneFmRatioIds[0], "FM RATIO", 314.0, 702.0, 82.0},
    {kLaneFmIndexIds[0], "FM INDEX", 314.0, 726.0, 82.0},
    {kLaneWindowSkewIds[0], "WIN SKEW", 314.0, 750.0, 82.0},
    {kLaneFormantIds[1], "CARRIER HZ", 18.0, 702.0, 82.0},
    {kLaneCarrierRatioIds[1], "CLOCK RATIO", 18.0, 726.0, 82.0},
    {kLaneOverlapIds[1], "OVERLAP", 18.0, 750.0, 82.0},
    {kLaneLevelIds[1], "LEVEL", 18.0, 774.0, 82.0},
    {kLaneOffsetIds[1], "OFFSET", 18.0, 798.0, 82.0},
    {kLaneFmRatioIds[1], "FM RATIO", 314.0, 702.0, 82.0},
    {kLaneFmIndexIds[1], "FM INDEX", 314.0, 726.0, 82.0},
    {kLaneWindowSkewIds[1], "WIN SKEW", 314.0, 750.0, 82.0},
    {kLaneFormantIds[2], "CARRIER HZ", 18.0, 702.0, 82.0},
    {kLaneCarrierRatioIds[2], "CLOCK RATIO", 18.0, 726.0, 82.0},
    {kLaneOverlapIds[2], "OVERLAP", 18.0, 750.0, 82.0},
    {kLaneLevelIds[2], "LEVEL", 18.0, 774.0, 82.0},
    {kLaneOffsetIds[2], "OFFSET", 18.0, 798.0, 82.0},
    {kLaneFmRatioIds[2], "FM RATIO", 314.0, 702.0, 82.0},
    {kLaneFmIndexIds[2], "FM INDEX", 314.0, 726.0, 82.0},
    {kLaneWindowSkewIds[2], "WIN SKEW", 314.0, 750.0, 82.0},
};

static_assert(kGuiSliders[0].id == kOutputParamId);

bool listeningGuiParam(clap_id id) {
  return id >= kNeuralSetParamId && id <= kListenerResponseParamId;
}

int guiLaneForParam(clap_id id) {
  for (uint32_t lane = 0u; lane < s3g::kAmbiPulsarLanes; ++lane) {
    if (id == kLaneFormantIds[lane] || id == kLaneCarrierRatioIds[lane] ||
        id == kLaneOverlapIds[lane] || id == kLaneLevelIds[lane] ||
        id == kLaneOffsetIds[lane] || id == kLaneFmRatioIds[lane] ||
        id == kLaneFmIndexIds[lane] || id == kLaneWindowSkewIds[lane] ||
        id == kLaneTuneModeIds[lane] || id == kLaneRetriggerModeIds[lane] ||
        id == kLaneWaveformIds[lane])
      return static_cast<int>(lane);
  }
  return -1;
}

const GuiSliderSpec *guiSlider(clap_id id) {
  for (const auto &slider : kGuiSliders)
    if (slider.id == id)
      return &slider;
  return nullptr;
}

bool logarithmicParam(clap_id id) {
  return id == kEmissionParamId || id == kEmissionModRateParamId ||
         id == kFormantModRateParamId || id == kDistanceParamId ||
         id == kLaneFormantIds[0] || id == kLaneFormantIds[1] ||
         id == kLaneFormantIds[2] || id == kLaneOverlapIds[0] ||
         id == kLaneOverlapIds[1] || id == kLaneOverlapIds[2] ||
         isLaneParam(id, kLaneCarrierRatioIds) ||
         isLaneParam(id, kLaneFmRatioIds);
}

double guiNormalize(clap_id id, double value) {
  const auto *param = findParam(id);
  if (!param)
    return 0.0;
  if (id == kAzimuthParamId)
    return static_cast<double>(
        s3g::aedAzimuthSliderNorm(static_cast<float>(value)));
  if (logarithmicParam(id)) {
    const double low = std::log(std::max(1.0e-9, param->minimum));
    const double high = std::log(std::max(1.0e-9, param->maximum));
    return static_cast<double>((std::log(std::max(1.0e-9, value)) - low) /
                               std::max(1.0e-9, high - low));
  }
  return static_cast<double>((value - param->minimum) /
                             std::max(1.0e-9, param->maximum - param->minimum));
}

double guiValue(clap_id id, double normalized) {
  const auto *param = findParam(id);
  if (!param)
    return 0.0;
  normalized = std::clamp(normalized, static_cast<double>(0.0),
                          static_cast<double>(1.0));
  if (id == kAzimuthParamId)
    return s3g::aedAzimuthFromSliderNorm(static_cast<float>(normalized));
  double value = 0.0;
  if (logarithmicParam(id)) {
    value = std::exp(std::log(std::max(1.0e-9, param->minimum)) +
                     static_cast<double>(normalized) *
                         (std::log(std::max(1.0e-9, param->maximum)) -
                          std::log(std::max(1.0e-9, param->minimum))));
  } else {
    value = param->minimum +
            static_cast<double>(normalized) * (param->maximum - param->minimum);
  }
  return param->stepped ? std::round(value) : value;
}

drawing::Rect guiSliderHitRect(const GuiSliderSpec &slider) {
  if (listeningGuiParam(slider.id)) {
    const double hitWidth = layout::kStandardMetrics.valueInset +
                            layout::kStandardMetrics.processorValueWidth;
    return drawing::makeRect(slider.panelX + layout::kStandardMetrics.hitInset,
                             slider.y - layout::kStandardMetrics.hitInset,
                             hitWidth, layout::kStandardMetrics.hitHeight);
  }
  const double hitWidth = slider.panelX < 300.0
                              ? 280.0
                              : slider.panelX < 620.0
                                    ? 292.0
                                    : slider.panelX < 890.0 ? 234.0 : 230.0;
  return drawing::makeRect(slider.panelX + 8.0, slider.y - 8.0, hitWidth, 24.0);
}

float displayWave(s3g::AmbiPulsarWaveform waveform, float phase) {
  phase -= std::floor(phase);
  switch (waveform) {
  case s3g::AmbiPulsarWaveform::Triangle:
    return 1.0f - 4.0f * std::fabs(phase - 0.5f);
  case s3g::AmbiPulsarWaveform::Saw:
    return phase * 2.0f - 1.0f;
  case s3g::AmbiPulsarWaveform::Square:
    return phase < 0.5f ? 1.0f : -1.0f;
  case s3g::AmbiPulsarWaveform::Overtone:
    return (std::sin(s3g::kAmbiPulsarTwoPi * phase) +
            0.42f * std::sin(s3g::kAmbiPulsarTwoPi * phase * 2.0f) +
            0.22f * std::sin(s3g::kAmbiPulsarTwoPi * phase * 3.0f)) /
           1.64f;
  case s3g::AmbiPulsarWaveform::Fold:
    return std::tanh((std::sin(s3g::kAmbiPulsarTwoPi * phase) +
                      0.34f * std::sin(s3g::kAmbiPulsarTwoPi * phase * 2.0f)) *
                     2.25f);
  case s3g::AmbiPulsarWaveform::Impulse:
    return std::exp(-phase * phase * 1200.0f);
  case s3g::AmbiPulsarWaveform::Noise:
    return std::sin(phase * 1927.0f) * std::sin(phase * 733.0f);
  case s3g::AmbiPulsarWaveform::Sine:
  default:
    return std::sin(s3g::kAmbiPulsarTwoPi * phase);
  }
}

class Editor final : public s3g::portable_gui::routing::Canvas {
public:
  Plugin *_plugin;

  clap_id _dragParam;
  uint32_t _selectedPoint;
  int _viewMode;
  double _viewAzDeg;
  double _viewElDeg;
  double _viewZoom;
  bool _dragView;
  drawing::Point _lastDragPoint;
  int _visualPage;
  uint32_t _selectedLane;
  int _openMenu;
  int _hoverMenuItem;
  uint32_t _menuItemCount;
  drawing::Rect _openMenuRect;

#include "s3g_circuit_encoder_editor.inc"
  Editor(Plugin &instance, bool popup = false)
      : Canvas(popup ? 596 : kGuiWidth, popup ? 608 : kGuiHeight) {
    auto *plugin = &instance;

    _plugin = plugin;
    _dragParam = CLAP_INVALID_ID;
    _selectedPoint = 0u;
    _selectedLane = 0u;
    _visualPage = 0;
    _openMenu = 0;
    _hoverMenuItem = -1;
    _menuItemCount = 0u;
    _openMenuRect = drawing::Rect{};
    _viewMode = plugin ? plugin->guiViewMode : 0;
    _viewZoom = plugin ? plugin->guiViewZoom : 1.0;
    _viewAzDeg = plugin ? plugin->guiViewAzDeg : 90.0;
    _viewElDeg = plugin ? plugin->guiViewElDeg : 0.0;
    _dragView = false;
    _lastDragPoint = drawing::Point{};

    service();
  }
  ~Editor() override { stopRefresh(); }
  bool loadPresetPath(const std::string &path) {
    CustomPresetFile file{};
    if (!loadCustomPresetFile(path.c_str(), file))
      return false;
    syncGuiParams(*_plugin);
    // Capture tables are runtime material, not portable preset data. Preserve
    // the current action serial so loading a preset does not request an
    // unrelated recapture; the captured mix and neural controls are restored.
    file.params.neuralCapture = _plugin->params.neuralCapture;
    file.params.outputGainDb = _plugin->params.outputGainDb;
    std::snprintf(_plugin->customPresetName, sizeof(_plugin->customPresetName),
                  "%s", file.name[0] ? file.name : "Custom");
    _plugin->customPresetActive.store(true, std::memory_order_relaxed);
    publishParams(*_plugin, file.params, 0u, true);
    setNeedsDisplay(true);
    return true;
  }
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

  drawing::Rect orderRect() {
    return drawing::cocoaRect(
        layout::menuBoxRect(kOutputPanel, layout::kLargeEncoderOrderSlot.row));
  }

  drawing::Rect envelopeRect() {
    return drawing::cocoaRect(layout::menuBoxRect(kEventPanel, 6u));
  }

  drawing::Rect qualityRect() {
    return drawing::cocoaRect(layout::menuBoxRect(kEventPanel, 8u));
  }

  drawing::Rect pointsRect() {
    return drawing::cocoaRect(layout::menuBoxRect(kClockPanel, 0u));
  }

  drawing::Rect motionRect() {
    return drawing::cocoaRect(layout::menuBoxRect(kDepthMotionPanel, 5u));
  }

  drawing::Rect captureHeaderRect() {
    const auto rect = drawing::cocoaRect(kCapturePanel.frame);
    return drawing::makeRect(rect.origin.x, rect.origin.y, rect.size.width,
                             layout::kStandardMetrics.headerHeight);
  }

  drawing::Rect captureRect() {
    return drawing::makeRect(kCapturePanel.frame.x + 126.0,
                             kCapturePanel.frame.y + 4.0, 110.0, 13.0);
  }

  drawing::Rect fieldPanelRect() { return drawing::makeRect(18, 42, 596, 608); }

  drawing::Rect fieldRect() { return drawing::makeRect(34, 76, 564, 558); }

  drawing::Rect lanePanelRect() { return drawing::makeRect(18, 666, 596, 180); }

  drawing::Rect pageButtonRect(int index) {
    const drawing::Rect panel = fieldPanelRect();
    return drawing::makeRect(panel.origin.x + 98.0 +
                                 static_cast<double>(index) * 74.0,
                             panel.origin.y + 4.0, 70.0, 13.0);
  }

  drawing::Rect listeningMenuRectForPanelX_y(double panelX, double y) {
    return drawing::makeRect(panelX + layout::kStandardMetrics.controlInset, y,
                             layout::kStandardMetrics.menuWidth, 15.0);
  }

  drawing::Rect neuralSetRect() {
    return listeningMenuRectForPanelX_y(kListeningLeftPanelX, 484.0);
  }

  drawing::Rect pickupSetRect() {
    return listeningMenuRectForPanelX_y(kListeningRightPanelX, 484.0);
  }

  drawing::Rect listeningModeRect() {
    return listeningMenuRectForPanelX_y(kListeningLeftPanelX, 510.0);
  }

  drawing::Rect listenerResponseRect() {
    return listeningMenuRectForPanelX_y(kListeningRightPanelX, 510.0);
  }

  drawing::Rect listeningEnableRect() {
    return drawing::makeRect(46.0, 610.0, 118.0, 15.0);
  }

  drawing::Rect listeningBypassRect() {
    return drawing::makeRect(172.0, 610.0, 118.0, 15.0);
  }

  drawing::Rect laneButtonRect(uint32_t lane) {
    const drawing::Rect panel = lanePanelRect();
    return drawing::makeRect(panel.origin.x + 126.0 +
                                 static_cast<double>(lane) * 44.0,
                             panel.origin.y + 4.0, 39.0, 13.0);
  }

  drawing::Rect viewButtonRect(int index) {
    const drawing::Rect panel = fieldPanelRect();
    const double width = 38.0;
    const double gap = 5.0;
    return drawing::makeRect(drawing::maxX(panel) - 10.0 -
                                 (3.0 - index) * width - (2.0 - index) * gap,
                             panel.origin.y + 4.0, width, 13.0);
  }

  drawing::Rect zoomButtonRect(int index) {
    const double width = 18.0;
    const double gap = 4.0;
    const double x = viewButtonRect(0).origin.x - 12.0 - (2.0 - index) * width -
                     (1.0 - index) * gap;
    return drawing::makeRect(x, fieldPanelRect().origin.y + 4.0, width, 13.0);
  }

  drawing::Rect waveformRect(uint32_t lane) {
    (void)lane;
    return drawing::makeRect(422.0, 822.0, 176.0, 15.0);
  }

  drawing::Rect tuneModeRect(uint32_t lane) {
    (void)lane;
    return drawing::makeRect(422.0, 774.0, 176.0, 15.0);
  }

  drawing::Rect retriggerModeRect(uint32_t lane) {
    (void)lane;
    return drawing::makeRect(422.0, 798.0, 176.0, 15.0);
  }

  drawing::Rect pulsaretLaneRect(uint32_t lane) {
    const drawing::Rect field = fieldRect();
    return drawing::makeRect(field.origin.x + 14.0,
                             field.origin.y + 38.0 +
                                 static_cast<double>(lane) * 168.0,
                             field.size.width - 28.0, 142.0);
  }

  drawing::String presetDisplayName() {
    if (_plugin->customPresetActive.load(std::memory_order_relaxed) &&
        _plugin->customPresetName[0]) {
      return drawing::stringFromUtf8(_plugin->customPresetName);
    }
    const uint32_t preset =
        std::min<uint32_t>(_plugin->presetIndex.load(std::memory_order_relaxed),
                           s3g::kAmbiPulsarFactoryPresetCount - 1u);
    return drawing::stringFromUtf8(
        s3g::ambiPulsarFactoryPresetInfo(preset).name);
  }

  drawing::Rect menuBoxRect(int menu) {
    switch (menu) {
    case 1:
      return presetRect();
    case 2:
      return orderRect();
    case 3:
      return envelopeRect();
    case 4:
      return qualityRect();
    case 5:
      return pointsRect();
    case 6:
      return tuneModeRect(_selectedLane);
    case 7:
      return retriggerModeRect(_selectedLane);
    case 8:
      return waveformRect(_selectedLane);
    case 9:
      return motionRect();
    case 10:
      return neuralSetRect();
    case 11:
      return pickupSetRect();
    case 12:
      return listeningModeRect();
    case 13:
      return listenerResponseRect();
    default:
      return drawing::Rect{};
    }
  }

  uint32_t menuCount(int menu) {
    switch (menu) {
    case 1:
      return s3g::kAmbiPulsarFactoryPresetCount;
    case 2:
      return 7u;
    case 3:
      return 5u;
    case 4:
      return 3u;
    case 5:
      return 29u;
    case 6:
      return 3u;
    case 7:
      return 3u;
    case 8:
      return 8u;
    case 9:
      return 5u;
    case 10:
      return 3u;
    case 11:
      return 2u;
    case 12:
      return 4u;
    case 13:
      return 4u;
    default:
      return 0u;
    }
  }

  void openMenu(int menu) {
    _openMenu = menu;
    _menuItemCount = menuCount(menu);
    _hoverMenuItem = -1;
    const drawing::Rect box = menuBoxRect(menu);
    const double itemHeight = 21.0;
    const double height = itemHeight * _menuItemCount;
    double y = drawing::maxY(box) + 2.0;
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
    static drawing::String envelopeItems[] = {"HANN", "TUKEY", "WELCH",
                                              "PERCUSSIVE", "REVERSE"};
    static drawing::String qualityItems[] = {"ECO", "HIGH", "ULTRA"};
    static drawing::String pointItems[] = {
        "4",  "5",  "6",  "7",  "8",  "9",  "10", "11", "12", "13",
        "14", "15", "16", "17", "18", "19", "20", "21", "22", "23",
        "24", "25", "26", "27", "28", "29", "30", "31", "32"};
    static drawing::String tuneItems[] = {"HZ", "RATIO", "SUB"};
    static drawing::String triggerItems[] = {"RETRIGGER", "FREE", "IDLE ONLY"};
    static drawing::String waveformItems[] = {"SINE",    "TRIANGLE", "SAW",
                                              "SQUARE",  "OVERTONE", "FOLD",
                                              "IMPULSE", "NOISE"};
    static drawing::String motionItems[] = {"FREE", "ORBIT", "SWAY", "FIGURE 8",
                                            "FORSY"};
    static drawing::String neuralSetItems[] = {"16 NODES", "32 NODES",
                                               "64 NODES"};
    static drawing::String pickupSetItems[] = {"TETRA 4", "CUBE 8"};
    static drawing::String listeningModeItems[] = {"LOCAL", "CROSS", "DIFFUSE",
                                                   "ROAMING"};
    static drawing::String listenerResponseItems[] = {"CLASSIC", "RESONATE",
                                                      "IMPRINT", "BALANCE"};
    static drawing::String presetItems[s3g::kAmbiPulsarFactoryPresetCount];

    for (uint32_t index = 0u; index < s3g::kAmbiPulsarFactoryPresetCount;
         ++index) {
      presetItems[index] =
          drawing::stringFromUtf8(s3g::ambiPulsarFactoryPresetInfo(index).name);
    }

    drawing::String *items = presetItems;
    int selected =
        static_cast<int>(_plugin->presetIndex.load(std::memory_order_relaxed));
    if (_openMenu == 2) {
      items = orderItems;
      selected = static_cast<int>(_plugin->params.order) - 1;
    } else if (_openMenu == 3) {
      items = envelopeItems;
      selected = static_cast<int>(_plugin->params.envelope);
    } else if (_openMenu == 4) {
      items = qualityItems;
      selected = static_cast<int>(_plugin->params.quality);
    } else if (_openMenu == 5) {
      items = pointItems;
      selected = static_cast<int>(_plugin->params.points) - 4;
    } else if (_openMenu == 6) {
      items = tuneItems;
      selected = static_cast<int>(
          _plugin->params.advancedLanes[_selectedLane].tuneMode);
    } else if (_openMenu == 7) {
      items = triggerItems;
      selected = static_cast<int>(
          _plugin->params.advancedLanes[_selectedLane].retriggerMode);
    } else if (_openMenu == 8) {
      items = waveformItems;
      selected =
          static_cast<int>(_plugin->params.lanes[_selectedLane].waveform);
    } else if (_openMenu == 9) {
      items = motionItems;
      selected = static_cast<int>(_plugin->params.motionMode);
    } else if (_openMenu == 10) {
      items = neuralSetItems;
      selected = static_cast<int>(_plugin->params.listening.neuralSet);
    } else if (_openMenu == 11) {
      items = pickupSetItems;
      selected = static_cast<int>(_plugin->params.listening.pickupSet);
    } else if (_openMenu == 12) {
      items = listeningModeItems;
      selected = static_cast<int>(_plugin->params.listening.mode);
    } else if (_openMenu == 13) {
      items = listenerResponseItems;
      selected = static_cast<int>(_plugin->params.listening.response);
    }
    drawing::drawDropdownMenu(_openMenuRect, 21.0, items, _menuItemCount,
                              selected, _hoverMenuItem, attrs, style);
  }

  void storeViewState() {
    if (!_plugin)
      return;
    _plugin->guiViewMode = _viewMode;
    _plugin->guiViewZoom = static_cast<float>(_viewZoom);
    _plugin->guiViewAzDeg = static_cast<float>(_viewAzDeg);
    _plugin->guiViewElDeg = static_cast<float>(_viewElDeg);
    _plugin->guiSelectedPoint.store(_selectedPoint, std::memory_order_relaxed);
  }

  void setViewPreset(int mode) {
    _viewMode = std::clamp(mode, 0, 2);
    if (_viewMode == 0) {
      _viewAzDeg = 90.0;
      _viewElDeg = 0.0;
    } else if (_viewMode == 1) {
      _viewAzDeg = 90.0;
      _viewElDeg = 90.0;
    } else {
      _viewAzDeg = 38.0;
      _viewElDeg = 32.0;
    }
    storeViewState();
    setNeedsDisplay(true);
  }

  s3g::Vec3 pointWorld(uint32_t index) {
    const float azimuth =
        _plugin->guiAzimuth[index].load(std::memory_order_relaxed);
    const float elevation =
        _plugin->guiElevation[index].load(std::memory_order_relaxed);
    const float distance =
        _plugin->guiDistance[index].load(std::memory_order_relaxed);
    const float referenceDistance =
        std::max(0.10f, _plugin->params.centerDistance);
    const float displayDistance =
        std::clamp(distance / referenceDistance, 0.42f, 1.48f);
    const s3g::Vec3 direction = s3g::directionFromAed(azimuth, elevation);
    return {direction.x * displayDistance, direction.y * displayDistance,
            direction.z * displayDistance};
  }

  drawing::Point projectWorld_depth(s3g::Vec3 point, double *depth) {
    const drawing::Rect field = fieldRect();
    const double scale = std::min(field.size.width, field.size.height) * 0.36 *
                         std::clamp(_viewZoom, static_cast<double>(0.55),
                                    static_cast<double>(2.20));
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

  drawing::Point projectPoint_depth(uint32_t index, double *depth) {
    return projectWorld_depth(pointWorld(index), depth);
  }

  VSTGUI::CColor pointColor_selected(uint32_t index, bool selected) {
    const float azimuth =
        _plugin->guiAzimuth[index].load(std::memory_order_relaxed);
    const float elevation =
        _plugin->guiElevation[index].load(std::memory_order_relaxed);
    const float hue = std::fmod((azimuth + 180.0f) / 360.0f + 0.08f, 1.0f);
    const float saturation = selected ? 0.72f : 0.52f;
    const float brightness =
        selected ? 0.96f : 0.72f + std::max(0.0f, elevation) / 90.0f * 0.18f;
    return drawing::calibratedHSV(hue, saturation, brightness,
                                  selected ? 1.0 : 0.84);
  }

  void drawMenuLabel_value_rect_style(drawing::String label,
                                      drawing::String value, drawing::Rect rect,
                                      const drawing::Style &style) {
    drawing::drawMenu(label, value, rect.origin.y + 1.0,
                      drawing::softLabelAttrs(), drawing::softValueAttrs(),
                      style, rect.origin.x - 92.0, rect.origin.x,
                      rect.size.width);
  }

  void drawSlider_style(const GuiSliderSpec &slider,
                        const drawing::Style &style) {
    char text[64]{};
    const double value = paramValue(_plugin->params, slider.id);
    paramsValueToText(nullptr, slider.id, value, text, sizeof(text));
    drawing::String label = drawing::stringFromUtf8(slider.label);
    drawing::String valueText = drawing::stringFromUtf8(text);
    drawing::drawSlider(label, valueText, guiNormalize(slider.id, value),
                        slider.y, drawing::softLabelAttrs(),
                        drawing::softValueAttrs(), style, slider.panelX + 16.0,
                        slider.panelX + 108.0, slider.panelX + 196.0,
                        slider.trackWidth);
  }

  void drawPulsaret_rect_style(uint32_t lane, drawing::Rect rect,
                               const drawing::Style &style) {
    drawing::setFill(style.strip);
    drawing::rectFill(rect);
    drawing::setStroke(style.grid);
    drawing::frameRect(rect);
    drawing::setStroke(drawing::calibratedColor(0x303030));
    drawing::Path::strokeLineFromPoint_toPoint(
        drawing::makePoint(drawing::minX(rect) + 1.0, drawing::midY(rect)),
        drawing::makePoint(drawing::maxX(rect) - 1.0, drawing::midY(rect)));
    drawing::Path path = drawing::Path::bezierPath();
    drawing::Path windowPath = drawing::Path::bezierPath();
    const auto waveform = _plugin->params.lanes[lane].waveform;
    const auto &advanced = _plugin->params.advancedLanes[lane];
    const float skew = advanced.windowSkew;
    constexpr uint32_t points = 192u;
    for (uint32_t index = 0u; index < points; ++index) {
      const float phase =
          static_cast<float>(index) / static_cast<float>(points - 1u);
      const float warped = phase < skew
                               ? 0.5f * phase / skew
                               : 0.5f + 0.5f * (phase - skew) / (1.0f - skew);
      const float window =
          0.5f - 0.5f * std::cos(s3g::kAmbiPulsarTwoPi * warped);
      const float fmOffset =
          advanced.fmIndex *
          std::sin(s3g::kAmbiPulsarTwoPi * phase * advanced.fmRatio) /
          s3g::kAmbiPulsarTwoPi;
      const float value = displayWave(waveform, phase + fmOffset) * window;
      const drawing::Point point = drawing::makePoint(
          rect.origin.x + phase * rect.size.width,
          drawing::midY(rect) - value * rect.size.height * 0.40);
      const drawing::Point windowPoint = drawing::makePoint(
          rect.origin.x + phase * rect.size.width,
          drawing::maxY(rect) - 4.0 - window * (rect.size.height - 8.0));
      index == 0u ? (path).moveToPoint(point) : (path).lineToPoint(point);
      index == 0u ? (windowPath).moveToPoint(windowPoint)
                  : (windowPath).lineToPoint(windowPoint);
    }
    drawing::setStroke(drawing::calibratedColor(0x666666, 0.52));
    (windowPath).setLineWidth(0.75);
    (windowPath).stroke();
    drawing::setStroke(style.accent);
    (path).setLineWidth(1.15);
    (path).stroke();
  }

  void drawPointField(const drawing::Style &style) {
    const drawing::Rect field = fieldRect();
    drawing::Attrs valueAttrs = drawing::softValueAttrs();
    drawing::setFill(drawing::calibratedColor(0x090909));
    drawing::rectFill(field);
    drawing::setStroke(drawing::calibratedColor(0x555555));
    drawing::frameRect(field);
    drawing::GraphicsScope graphics(*drawing::current->context);
    drawing::clipRect(drawing::inset(field, 1.0, 1.0));
    const double sphereRadius =
        std::min(field.size.width, field.size.height) * 0.36 * _viewZoom;
    drawing::setStroke(drawing::calibratedColor(0x303030));
    drawing::Path sphere = drawing::Path::bezierPathWithOvalInRect(
        drawing::makeRect(drawing::midX(field) - sphereRadius,
                          drawing::midY(field) - sphereRadius,
                          sphereRadius * 2.0, sphereRadius * 2.0));
    (sphere).setLineWidth(0.8);
    (sphere).stroke();
    drawing::drawAmbisonicOrientationGuides([&](double x, double y, double z) {
      return projectWorld_depth(
          {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)},
          nullptr);
    });

    const uint32_t points =
        std::clamp<uint32_t>(_plugin->params.points, s3g::kAmbiPulsarMinPoints,
                             s3g::kAmbiPulsarMaxPoints);
    _selectedPoint = std::min<uint32_t>(_selectedPoint, points - 1u);
    _plugin->guiSelectedPoint.store(_selectedPoint, std::memory_order_relaxed);
    std::array<drawing::Point, s3g::kAmbiPulsarMaxPoints> projected{};
    for (uint32_t index = 0u; index < points; ++index) {
      projected[index] = projectPoint_depth(index, nullptr);
    }
    drawing::setStroke(drawing::calibratedColor(0x5c5c5c, 0.22));
    for (uint32_t index = 0u; index < points; ++index) {
      drawing::Path link = drawing::Path::bezierPath();
      (link).moveToPoint(projected[index]);
      (link).lineToPoint(projected[(index + 1u) % points]);
      (link).setLineWidth(0.55);
      (link).stroke();
    }

    drawing::Attrs idAttrs =
        drawing::textAttrs(drawing::calibratedColor(0x080808), 6.5);
    for (uint32_t index = 0u; index < points; ++index) {
      const bool selected = index == _selectedPoint;
      const float energy =
          _plugin->guiEnergy[index].load(std::memory_order_relaxed);
      const float event = std::clamp(
          _plugin->guiEvent[index].load(std::memory_order_relaxed), 0.0f, 1.0f);
      const float activity =
          std::clamp(std::sqrt(std::max(0.0f, energy)) * 18.0f, 0.0f, 1.0f);
      const double size =
          (selected ? 14.0 : 10.0) + activity * 5.0 + event * 7.0;
      const drawing::Rect marker =
          drawing::makeRect(projected[index].x - size * 0.5,
                            projected[index].y - size * 0.5, size, size);
      VSTGUI::CColor pointColor = pointColor_selected(index, selected);
      if (event > 0.04f || activity > 0.04f) {
        const double halo = size * (1.15 + event * 1.9 + activity * 0.6);
        const drawing::Rect haloRect =
            drawing::makeRect(projected[index].x - halo * 0.5,
                              projected[index].y - halo * 0.5, halo, halo);
        drawing::setFill(drawing::withAlpha(pointColor, 0.05 + event * 0.18 +
                                                            activity * 0.08));
        (drawing::Path::bezierPathWithOvalInRect(haloRect)).fill();
      }
      drawing::setFill(drawing::withAlpha(
          pointColor, selected ? 0.98 : 0.22 + event * 0.54 + activity * 0.20));
      drawing::rectFill(marker);
      drawing::setStroke(drawing::calibratedColor(
          selected ? 0xe6e6e6 : 0x4f4f4f,
          selected ? 1.0 : 0.22 + event * 0.46 + activity * 0.18));
      drawing::frameRect(marker);
      drawing::String pointLabel = drawing::stringFormat("%u", index + 1u);
      const drawing::Size labelSize =
          drawing::sizeWithAttributes(pointLabel, idAttrs);
      drawing::drawAtPoint_withAttributes(
          pointLabel,
          drawing::makePoint(drawing::midX(marker) - labelSize.width * 0.5,
                             drawing::midY(marker) - labelSize.height * 0.5 -
                                 0.5),
          idAttrs);
    }
    graphics.restore();

    const float azimuth =
        _plugin->guiAzimuth[_selectedPoint].load(std::memory_order_relaxed);
    const float elevation =
        _plugin->guiElevation[_selectedPoint].load(std::memory_order_relaxed);
    const float distance =
        _plugin->guiDistance[_selectedPoint].load(std::memory_order_relaxed);
    const float energy =
        _plugin->guiEnergy[_selectedPoint].load(std::memory_order_relaxed);
    drawing::String readout = drawing::stringFormat(
        "P%02u  AZ%+.1f  EL%+.1f  D%.2f  E%.3f", _selectedPoint + 1u, azimuth,
        elevation, distance, energy);
    drawing::drawRightStatus(readout, drawing::maxX(field),
                             field.origin.y + 7.0, valueAttrs, 8.0);
    drawing::drawAtPoint_withAttributes(
        "A+B+C PULSARET OBJECTS     DIRECT ACN/SN3D",
        drawing::makePoint(field.origin.x + 9.0, drawing::maxY(field) - 19.0),
        valueAttrs);
  }

  void drawNeuralNetwork(const drawing::Style &style) {
    const drawing::Rect field = fieldRect();
    drawing::setFill(drawing::calibratedColor(0x090909));
    drawing::rectFill(field);
    drawing::setStroke(drawing::calibratedColor(0x555555));
    drawing::frameRect(field);
    drawing::drawAtPoint_withAttributes(
        "FOUR LOBES / SIXTEEN SIGNED R-C RINGS",
        drawing::makePoint(field.origin.x + 12.0, field.origin.y + 12.0),
        drawing::softValueAttrs());

    const float progress =
        _plugin->guiCaptureProgress.load(std::memory_order_relaxed);
    const uint32_t generation =
        _plugin->guiCaptureGeneration.load(std::memory_order_relaxed);
    const uint32_t activeLobes =
        _plugin->params.listening.neuralSet == s3g::AmbiPulsarNeuralSet::Nodes64
            ? 4u
            : _plugin->params.listening.neuralSet ==
                      s3g::AmbiPulsarNeuralSet::Nodes32
                  ? 2u
                  : 1u;
    drawing::String state = drawing::stringFormat(
        "%u NEURONS / %@ %02u", activeLobes * s3g::kNeuralSynthesisNodes,
        generation > 0u ? "TABLE" : "BUFFER",
        generation > 0u ? generation
                        : static_cast<uint32_t>(progress * 100.0f));
    drawing::drawRightStatus(state, drawing::maxX(field), field.origin.y + 12.0,
                             drawing::softValueAttrs(), 12.0);

    static drawing::String timeLabels[] = {"80", "32", "9.5", "2"};
    for (uint32_t lobe = 0u; lobe < s3g::kAmbiPulsarNeuralLobes; ++lobe) {
      const uint32_t column = lobe % 2u;
      const uint32_t row = lobe / 2u;
      const drawing::Rect lobeBox =
          drawing::makeRect(field.origin.x + 10.0 + column * 272.0,
                            field.origin.y + 45.0 + row * 224.0, 264.0, 210.0);
      const bool active = lobe < activeLobes;
      drawing::setFill(drawing::calibratedColor(active ? 0x121212 : 0x0d0d0d));
      drawing::rectFill(lobeBox);
      drawing::setStroke(
          drawing::calibratedColor(active ? 0x5b5b5b : 0x292929));
      drawing::frameRect(lobeBox);
      drawing::drawAtPoint_withAttributes(
          drawing::stringFormat("LOBE %c / %@", static_cast<char>('A' + lobe),
                                active ? "ACTIVE" : "STANDBY"),
          drawing::makePoint(lobeBox.origin.x + 8.0, lobeBox.origin.y + 7.0),
          drawing::softLabelAttrs());

      for (uint32_t cluster = 0u; cluster < s3g::kNeuralSynthesisClusters;
           ++cluster) {
        const uint32_t clusterColumn = cluster % 2u;
        const uint32_t clusterRow = cluster / 2u;
        const drawing::Point center =
            drawing::makePoint(lobeBox.origin.x + 67.0 + clusterColumn * 130.0,
                               lobeBox.origin.y + 70.0 + clusterRow * 91.0);
        std::array<drawing::Point, s3g::kNeuralNodesPerCluster> points{{
            drawing::makePoint(center.x - 20.0, center.y - 20.0),
            drawing::makePoint(center.x + 20.0, center.y - 20.0),
            drawing::makePoint(center.x + 20.0, center.y + 20.0),
            drawing::makePoint(center.x - 20.0, center.y + 20.0),
        }};
        drawing::drawAtPoint_withAttributes(
            drawing::stringFormat("C%u / %@ MS", cluster + 1u,
                                  timeLabels[cluster]),
            drawing::makePoint(center.x - 42.0, center.y - 39.0),
            drawing::softValueAttrs());
        drawing::Path ring = drawing::Path::bezierPath();
        (ring).moveToPoint(points[0]);
        for (uint32_t local = 1u; local < points.size(); ++local)
          (ring).lineToPoint(points[local]);
        (ring).closePath();
        drawing::setStroke(
            drawing::calibratedColor(active ? 0x686868 : 0x303030, 0.72));
        (ring).setLineWidth(0.8);
        (ring).stroke();

        for (uint32_t local = 0u; local < s3g::kNeuralNodesPerCluster;
             ++local) {
          const uint32_t node = lobe * s3g::kNeuralSynthesisNodes +
                                cluster * s3g::kNeuralNodesPerCluster + local;
          const float value =
              _plugin->guiNeuralNode[node].load(std::memory_order_relaxed);
          const double radius = active ? 3.5 + std::fabs(value) * 5.5 : 3.0;
          const int shade =
              active ? static_cast<int>(
                           std::clamp(108.0f + value * 116.0f, 20.0f, 238.0f))
                     : 38;
          drawing::setFill(drawing::calibratedColor(
              (shade << 16) | (shade << 8) | shade, active ? 0.94 : 0.5));
          (drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
               points[local].x - radius, points[local].y - radius, radius * 2.0,
               radius * 2.0)))
              .fill();
          drawing::setStroke(drawing::calibratedColor(
              value >= 0.0f ? 0xd8d8d8 : 0x777777, active ? 0.74 : 0.24));
          (drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
               points[local].x - radius, points[local].y - radius, radius * 2.0,
               radius * 2.0)))
              .stroke();
        }
      }
    }

    drawing::drawAtPoint_withAttributes(
        "BROWNIAN + DRIFT + LOBE MATRIX + DIRECTIONAL FIELD RETURN",
        drawing::makePoint(field.origin.x + 12.0, drawing::maxY(field) - 27.0),
        drawing::softValueAttrs());
    (void)style;
  }

  void drawPulsaretPage(const drawing::Style &style) {
    const drawing::Rect field = fieldRect();
    drawing::setFill(drawing::calibratedColor(0x090909));
    drawing::rectFill(field);
    drawing::setStroke(drawing::calibratedColor(0x555555));
    drawing::frameRect(field);
    static constexpr const char *laneNames[]{"LANE A", "LANE B", "LANE C"};
    for (uint32_t lane = 0u; lane < s3g::kAmbiPulsarLanes; ++lane) {
      const drawing::Rect row = pulsaretLaneRect(lane);
      drawing::setFill(drawing::calibratedColor(
          lane == _selectedLane ? 0x181818 : 0x101010));
      drawing::rectFill(row);
      drawing::setStroke(drawing::calibratedColor(
          lane == _selectedLane ? 0xb0b0b0 : 0x3b3b3b));
      drawing::frameRect(row);
      const auto &laneParams = _plugin->params.lanes[lane];
      const auto &advanced = _plugin->params.advancedLanes[lane];
      drawing::String title = drawing::stringFormat(
          "%s  %s / %s / %s", laneNames[lane],
          kWaveformNames[static_cast<uint32_t>(laneParams.waveform)],
          kTuneModeNames[static_cast<uint32_t>(advanced.tuneMode)],
          kRetriggerModeNames[static_cast<uint32_t>(advanced.retriggerMode)]);
      drawing::String detail = drawing::stringFormat(
          "%.1f Hz   CLK %.3gx   FM %.3gx / %.2f   WIN %.2f   N%02.0f",
          laneParams.formantHz, advanced.carrierRatio, advanced.fmRatio,
          advanced.fmIndex, advanced.windowSkew,
          _plugin->params.neuralPulsaretMix * 100.0f);
      // Keep both original anchors and all readouts. Long waveform names
      // must not collide with the detail line when using the bundled font.
      auto labelAttrs = drawing::softLabelAttrs(),
           valueAttrs = drawing::softValueAttrs();
      const auto total = drawing::sizeWithAttributes(title, labelAttrs).width +
                         drawing::sizeWithAttributes(detail, valueAttrs).width;
      const double fit =
          std::min(1., (row.size.width - 28.) / std::max(1., total));
      if (fit < 1.)
        labelAttrs.size = valueAttrs.size = font->getSize() * fit;
      drawing::drawAtPoint_withAttributes(
          title, drawing::makePoint(row.origin.x + 10.0, row.origin.y + 8.0),
          labelAttrs);
      drawing::drawRightStatus(detail, drawing::maxX(row), row.origin.y + 8.0,
                               valueAttrs, 10.0);
      drawPulsaret_rect_style(
          lane,
          drawing::makeRect(row.origin.x + 10.0, row.origin.y + 31.0,
                            row.size.width - 20.0, row.size.height - 42.0),
          style);
    }
  }

  void drawListeningPage(const drawing::Style &style) {
    const drawing::Rect field = fieldRect();
    drawing::setFill(drawing::calibratedColor(0x090909));
    drawing::rectFill(field);
    drawing::setStroke(drawing::calibratedColor(0x555555));
    drawing::frameRect(field);
    drawing::Attrs labels = drawing::softLabelAttrs();
    drawing::Attrs values = drawing::softValueAttrs();
    drawing::drawAtPoint_withAttributes(
        "AMBISONIC AUDITORY BODY / CHARGE, IMPRINT + RETURN",
        drawing::makePoint(field.origin.x + 10.0, field.origin.y + 10.0),
        values);

    const uint32_t pickupCount =
        _plugin->params.listening.pickupSet == s3g::AmbiPulsarPickupSet::Cube8
            ? 8u
            : 4u;
    constexpr float inverseRootThree = 0.5773502691896258f;
    static constexpr std::array<s3g::Vec3, s3g::kAmbiPulsarListeningPickups>
        directions{{
            {inverseRootThree, inverseRootThree, inverseRootThree},
            {-inverseRootThree, -inverseRootThree, inverseRootThree},
            {-inverseRootThree, inverseRootThree, -inverseRootThree},
            {inverseRootThree, -inverseRootThree, -inverseRootThree},
            {-inverseRootThree, -inverseRootThree, -inverseRootThree},
            {inverseRootThree, inverseRootThree, -inverseRootThree},
            {inverseRootThree, -inverseRootThree, inverseRootThree},
            {-inverseRootThree, inverseRootThree, inverseRootThree},
        }};
    const drawing::Point center =
        drawing::makePoint(drawing::midX(field), field.origin.y + 215.0);
    const double radius = 148.0;
    drawing::setStroke(drawing::calibratedColor(0x303030));
    (drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
         center.x - radius, center.y - radius, radius * 2.0, radius * 2.0)))
        .stroke();

    std::array<drawing::Point, s3g::kAmbiPulsarListeningPickups> ears{};
    const float cameraAz = static_cast<float>(38.0 * s3g::kPi / 180.0);
    const float cameraEl = static_cast<float>(32.0 * s3g::kPi / 180.0);
    for (uint32_t pickup = 0u; pickup < pickupCount; ++pickup) {
      const auto direction = directions[pickup];
      const float x1 =
          std::cos(cameraAz) * direction.x - std::sin(cameraAz) * direction.y;
      const float y1 =
          std::sin(cameraAz) * direction.x + std::cos(cameraAz) * direction.y;
      const float y2 =
          std::cos(cameraEl) * y1 + std::sin(cameraEl) * direction.z;
      ears[pickup] =
          drawing::makePoint(center.x + x1 * radius, center.y - y2 * radius);
    }
    std::array<drawing::Point, s3g::kAmbiPulsarNeuralLobes> lobes{{
        drawing::makePoint(center.x - 40.0, center.y - 26.0),
        drawing::makePoint(center.x + 40.0, center.y - 26.0),
        drawing::makePoint(center.x + 40.0, center.y + 26.0),
        drawing::makePoint(center.x - 40.0, center.y + 26.0),
    }};

    drawing::setStroke(drawing::calibratedColor(0x707070, 0.38));
    for (uint32_t lobe = 0u; lobe < s3g::kAmbiPulsarNeuralLobes; ++lobe) {
      uint32_t pickup = lobe % pickupCount;
      if (_plugin->params.listening.mode ==
          s3g::AmbiPulsarListeningMode::Cross) {
        pickup = (lobe + 1u) % pickupCount;
      } else if (_plugin->params.listening.mode ==
                 s3g::AmbiPulsarListeningMode::Roaming) {
        pickup = (lobe + static_cast<uint32_t>(drawing::uptime() * 0.037 *
                                               pickupCount)) %
                 pickupCount;
      }
      drawing::Path path = drawing::Path::bezierPath();
      (path).moveToPoint(ears[pickup]);
      (path).lineToPoint(lobes[lobe]);
      (path).setLineWidth(0.7 +
                          std::fabs(_plugin->guiListeningReturn[lobe].load(
                              std::memory_order_relaxed)) *
                              2.2);
      (path).stroke();
    }

    for (uint32_t pickup = 0u; pickup < pickupCount; ++pickup) {
      const float energy = std::clamp(
          _plugin->guiListeningEnergy[pickup].load(std::memory_order_relaxed),
          0.0f, 1.0f);
      const float novelty = std::clamp(
          _plugin->guiListeningNovelty[pickup].load(std::memory_order_relaxed),
          0.0f, 1.0f);
      const float charge = std::clamp(
          _plugin->guiListeningCharge[pickup].load(std::memory_order_relaxed),
          0.0f, 1.0f);
      const double size = 8.0 + std::sqrt(energy) * 18.0;
      drawing::setFill(drawing::withAlpha(
          drawing::calibratedColor(0xc8c8c8, 0.04 + energy * 0.20),
          0.04 + energy * 0.20));
      (drawing::Path::bezierPathWithOvalInRect(
           drawing::makeRect(ears[pickup].x - size, ears[pickup].y - size,
                             size * 2.0, size * 2.0)))
          .fill();
      if (charge > 0.01f || novelty > 0.01f) {
        const double chargeSize = size + 4.0 + charge * 10.0;
        drawing::setStroke(
            drawing::calibratedColor(0xd8d8d8, 0.12 + charge * 0.58));
        drawing::Path chargeRing =
            drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
                ears[pickup].x - chargeSize, ears[pickup].y - chargeSize,
                chargeSize * 2.0, chargeSize * 2.0));
        (chargeRing).setLineWidth(0.6 + novelty * 2.4);
        (chargeRing).stroke();
      }
      drawing::Path diamond = drawing::Path::bezierPath();
      (diamond).moveToPoint(
          drawing::makePoint(ears[pickup].x, ears[pickup].y - 6.0));
      (diamond).lineToPoint(
          drawing::makePoint(ears[pickup].x + 6.0, ears[pickup].y));
      (diamond).lineToPoint(
          drawing::makePoint(ears[pickup].x, ears[pickup].y + 6.0));
      (diamond).lineToPoint(
          drawing::makePoint(ears[pickup].x - 6.0, ears[pickup].y));
      (diamond).closePath();
      drawing::setFill(
          drawing::calibratedColor(0x9a9a9a, 0.35 + energy * 0.65));
      (diamond).fill();
      drawing::drawAtPoint_withAttributes(
          drawing::stringFormat("E%u", pickup + 1u),
          drawing::makePoint(ears[pickup].x + 8.0, ears[pickup].y - 6.0),
          values);
    }
    for (uint32_t lobe = 0u; lobe < s3g::kAmbiPulsarNeuralLobes; ++lobe) {
      const float value =
          _plugin->guiListeningReturn[lobe].load(std::memory_order_relaxed);
      const int shade =
          static_cast<int>(std::clamp(118.0f + value * 116.0f, 22.0f, 234.0f));
      drawing::setFill(
          drawing::calibratedColor((shade << 16) | (shade << 8) | shade, 0.94));
      drawing::rectFill(drawing::makeRect(lobes[lobe].x - 10.0,
                                          lobes[lobe].y - 10.0, 20.0, 20.0));
      drawing::drawAtPoint_withAttributes(
          drawing::stringFormat("%c", static_cast<char>('A' + lobe)),
          drawing::makePoint(lobes[lobe].x - 3.0, lobes[lobe].y - 7.0),
          drawing::textAttrs(drawing::calibratedColor(0x080808), 8.0));
    }

    drawMenuLabel_value_rect_style(
        "NODES",
        drawing::stringFromUtf8(kNeuralSetNames[static_cast<uint32_t>(
            _plugin->params.listening.neuralSet)]),
        neuralSetRect(), style);
    drawMenuLabel_value_rect_style(
        "EARS",
        drawing::stringFromUtf8(kPickupSetNames[static_cast<uint32_t>(
            _plugin->params.listening.pickupSet)]),
        pickupSetRect(), style);
    drawMenuLabel_value_rect_style(
        "LISTENING",
        drawing::stringFromUtf8(kListeningModeNames[static_cast<uint32_t>(
            _plugin->params.listening.mode)]),
        listeningModeRect(), style);
    drawMenuLabel_value_rect_style(
        "RESPONSE",
        drawing::stringFromUtf8(kListenerResponseNames[static_cast<uint32_t>(
            _plugin->params.listening.response)]),
        listenerResponseRect(), style);
    for (const auto &slider : kGuiSliders) {
      if (listeningGuiParam(slider.id))
        drawSlider_style(slider, style);
    }
    const drawing::Rect controlHeader =
        drawing::makeRect(field.origin.x, 604.0, field.size.width, 27.0);
    drawing::drawToolboxHeaderButton(
        listeningEnableRect(), controlHeader,
        _plugin->params.listening.enabled ? "LISTEN ON" : "LISTEN OFF",
        _plugin->params.listening.enabled != 0u, labels, style);
    drawing::drawToolboxHeaderButton(
        listeningBypassRect(), controlHeader,
        _plugin->params.listening.bypass ? "BYPASSED" : "RETURN ACTIVE",
        _plugin->params.listening.bypass != 0u, labels, style);
    drawing::String status = drawing::stringFormat(
        "A %3.0f  C %3.0f  N %3.0f  R %3.0f",
        _plugin->guiListeningEcologyActivity.load(std::memory_order_relaxed) *
            100.0f,
        _plugin->guiListeningEcologyCharge.load(std::memory_order_relaxed) *
            100.0f,
        _plugin->guiListeningEcologyNovelty.load(std::memory_order_relaxed) *
            100.0f,
        _plugin->guiListeningInfluence.load(std::memory_order_relaxed) *
            100.0f);
    drawing::drawRightStatus(status, drawing::maxX(field) - 4.0, 610.0, values,
                             0.0);
  }

  void drawVisualPanel(const drawing::Style &style) {
    const drawing::Rect panel = fieldPanelRect();
    const drawing::Rect header = drawing::makeRect(
        panel.origin.x, panel.origin.y, panel.size.width, 21.0);
    drawing::Attrs attrs = drawing::softLabelAttrs();
    drawing::drawPanelFrame(panel.origin.x, panel.origin.y, panel.size.width,
                            panel.size.height, style);
    drawing::drawPanelHeader("PULSAR VISUAL", true, panel.origin.x,
                             panel.origin.y, panel.size.width, 21.0, attrs,
                             style);
    static drawing::String pageNames[] = {"FIELD", "PULSARETS", "NEURAL",
                                          "LISTEN"};
    for (int index = 0; index < 4; ++index) {
      drawing::drawToolboxHeaderButton(pageButtonRect(index), header,
                                       pageNames[index], index == _visualPage,
                                       attrs, style);
    }
    if (_visualPage == 0) {
      drawing::drawToolboxHeaderButton(zoomButtonRect(0), header, "-", false,
                                       attrs, style);
      drawing::drawToolboxHeaderButton(zoomButtonRect(1), header, "+", false,
                                       attrs, style);
      static drawing::String viewNames[] = {"TOP", "SIDE", "3/4"};
      for (int index = 0; index < 3; ++index) {
        drawing::drawToolboxHeaderButton(viewButtonRect(index), header,
                                         viewNames[index], index == _viewMode,
                                         attrs, style);
      }
      drawPointField(style);
    } else if (_visualPage == 1) {
      drawPulsaretPage(style);
    } else if (_visualPage == 2) {
      drawNeuralNetwork(style);
    } else {
      drawListeningPage(style);
    }
  }

  void drawCaptureState(const drawing::Style &style) {
    const float captureProgress =
        _plugin->guiCaptureProgress.load(std::memory_order_relaxed);
    const uint32_t captureGeneration =
        _plugin->guiCaptureGeneration.load(std::memory_order_relaxed);
    const bool capturePending =
        _plugin->guiCaptureRequestSerial.load(std::memory_order_acquire) !=
        _plugin->guiCaptureCompletedSerial.load(std::memory_order_acquire);
    const bool capturedPulsaret = _plugin->params.neuralPulsaretMix > 0.001f;
    const bool capturedEnvelope = _plugin->params.neuralEnvelopeMix > 0.001f;
    const bool capturedFm = _plugin->params.neuralFmDepthSemitones > 0.01f;
    const bool captureInUse =
        captureGeneration > 0u &&
        (capturedPulsaret || capturedEnvelope || capturedFm);
    const drawing::Rect captureStateRect =
        drawing::makeRect(908.0, 696.0, 222.0, 38.0);
    drawing::setFill(
        drawing::calibratedColor(captureInUse ? 0xb8b8b8 : 0x151515));
    drawing::rectFill(captureStateRect);
    drawing::setStroke(
        drawing::calibratedColor(captureInUse ? 0xe0e0e0 : 0x454545));
    drawing::frameRect(captureStateRect);
    if (captureGeneration == 0u) {
      drawing::setFill(drawing::calibratedColor(0x343434));
      drawing::Rect progress = drawing::inset(captureStateRect, 1.0, 1.0);
      progress.size.width *= std::clamp(captureProgress, 0.0f, 1.0f);
      drawing::rectFill(progress);
    }
    drawing::String tableStatus =
        capturePending
            ? "CAPTURE REQUESTED / WAITING FOR AUDIO"
            : captureGeneration > 0u
                  ? drawing::stringFormat("TABLE %02u / %@", captureGeneration,
                                          captureInUse ? "ACTIVE"
                                                       : "STORED / MIX OFF")
                  : drawing::stringFormat("BUFFER %3.0f%% / NO TABLE",
                                          captureProgress * 100.0f);
    drawing::String pathStatus = drawing::stringFormat(
        "PUL %@    ENV %@    FM %@",
        capturedPulsaret ? (captureGeneration > 0u ? "ON" : "ARM") : "--",
        capturedEnvelope ? (captureGeneration > 0u ? "ON" : "ARM") : "--",
        capturedFm ? (captureGeneration > 0u ? "ON" : "ARM") : "--");
    drawing::Attrs captureAttrs = drawing::textAttrs(
        drawing::calibratedColor(captureInUse ? 0x101010 : 0xa8a8a8), 9.0);
    drawing::drawAtPoint_withAttributes(
        tableStatus,
        drawing::makePoint(captureStateRect.origin.x + 7.0,
                           captureStateRect.origin.y + 5.0),
        captureAttrs);
    drawing::drawAtPoint_withAttributes(
        pathStatus,
        drawing::makePoint(captureStateRect.origin.x + 7.0,
                           captureStateRect.origin.y + 20.0),
        captureAttrs);
    drawing::drawAtPoint_withAttributes(
        "CAPTURE BUILDS NEW 2048-SAMPLE TABLES.",
        drawing::makePoint(908.0, 748.0), drawing::softLabelAttrs());
    drawing::drawAtPoint_withAttributes("MIX CHANGES APPLY ON THE NEXT EVENT.",
                                        drawing::makePoint(908.0, 766.0),
                                        drawing::softLabelAttrs());
    (void)style;
  }

  void drawControlPanels(const drawing::Style &style) {
    drawing::Attrs attrs = drawing::softLabelAttrs();
    drawing::drawPanelFrame(kOutputPanel, style);
    drawing::drawPanelHeader("OUTPUT", true, kOutputPanel, attrs, style);
    drawing::drawPanelFrame(kClockPanel, style);
    drawing::drawPanelHeader("SOURCE / CLOCK", true, kClockPanel, attrs, style);
    drawing::drawPanelFrame(kEventPanel, style);
    drawing::drawPanelHeader("EVENT MASK / WINDOW", true, kEventPanel, attrs,
                             style);
    drawing::drawPanelFrame(kFieldOriginPanel, style);
    drawing::drawPanelHeader("PROJECTION / ORIGIN", true, kFieldOriginPanel,
                             attrs, style);
    drawing::drawPanelFrame(kDepthMotionPanel, style);
    drawing::drawPanelHeader("DEPTH / MOTION", true, kDepthMotionPanel, attrs,
                             style);
    drawing::drawPanelFrame(kNeuralPanel, style);
    drawing::drawPanelHeader("NEURAL CIRCUIT", true, kNeuralPanel, attrs,
                             style);
    drawing::drawPanelFrame(kCapturePanel, style);
    drawing::drawPanelHeader("CAPTURE UTILITY", true, kCapturePanel, attrs,
                             style);

    for (const auto &slider : kGuiSliders) {
      if (guiLaneForParam(slider.id) < 0 && !listeningGuiParam(slider.id)) {
        drawSlider_style(slider, style);
      }
    }
    drawMenuLabel_value_rect_style(
        "ORDER", drawing::stringFormat("%uOA", _plugin->params.order),
        orderRect(), style);
    drawMenuLabel_value_rect_style(
        "POINTS", drawing::stringFormat("%u", _plugin->params.points),
        pointsRect(), style);
    drawMenuLabel_value_rect_style(
        "ENVELOPE",
        drawing::stringFromUtf8(
            kEnvelopeNames[static_cast<uint32_t>(_plugin->params.envelope)]),
        envelopeRect(), style);
    drawMenuLabel_value_rect_style(
        "QUALITY",
        drawing::stringFromUtf8(
            kQualityNames[static_cast<uint32_t>(_plugin->params.quality)]),
        qualityRect(), style);
    drawMenuLabel_value_rect_style(
        "MOTION",
        drawing::stringFromUtf8(kMotionModeNames[static_cast<uint32_t>(
            _plugin->params.motionMode)]),
        motionRect(), style);
    const uint32_t captureGeneration =
        _plugin->guiCaptureGeneration.load(std::memory_order_relaxed);
    const bool capturePending =
        _plugin->guiCaptureRequestSerial.load(std::memory_order_acquire) !=
        _plugin->guiCaptureCompletedSerial.load(std::memory_order_acquire);
    drawing::drawToolboxHeaderActionButton(
        captureRect(), captureHeaderRect(),
        capturePending ? "CAPTURING"
                       : captureGeneration > 0u ? "RECAPTURE" : "CAPTURE",
        attrs, style);
    drawCaptureState(style);
  }

  void drawLanePanel(const drawing::Style &style) {
    _selectedLane =
        std::min<uint32_t>(_selectedLane, s3g::kAmbiPulsarLanes - 1u);
    const drawing::Rect panel = lanePanelRect();
    const drawing::Rect header = drawing::makeRect(
        panel.origin.x, panel.origin.y, panel.size.width, 21.0);
    drawing::Attrs attrs = drawing::softLabelAttrs();
    drawing::drawPanelFrame(panel.origin.x, panel.origin.y, panel.size.width,
                            panel.size.height, style);
    drawing::drawPanelHeader("PULSARET LANE", true, panel.origin.x,
                             panel.origin.y, panel.size.width, 21.0, attrs,
                             style);
    static drawing::String laneNames[] = {"A", "B", "C"};
    for (uint32_t lane = 0u; lane < s3g::kAmbiPulsarLanes; ++lane) {
      drawing::drawToolboxHeaderButton(laneButtonRect(lane), header,
                                       laneNames[lane], lane == _selectedLane,
                                       attrs, style);
    }
    for (const auto &slider : kGuiSliders) {
      if (guiLaneForParam(slider.id) == static_cast<int>(_selectedLane))
        drawSlider_style(slider, style);
    }
    const auto &lane = _plugin->params.lanes[_selectedLane];
    const auto &advanced = _plugin->params.advancedLanes[_selectedLane];
    drawMenuLabel_value_rect_style(
        "TUNING",
        drawing::stringFromUtf8(
            kTuneModeNames[static_cast<uint32_t>(advanced.tuneMode)]),
        tuneModeRect(_selectedLane), style);
    drawMenuLabel_value_rect_style(
        "TRIGGER",
        drawing::stringFromUtf8(
            kRetriggerModeNames[static_cast<uint32_t>(advanced.retriggerMode)]),
        retriggerModeRect(_selectedLane), style);
    drawMenuLabel_value_rect_style(
        "PULSARET",
        drawing::stringFromUtf8(
            kWaveformNames[static_cast<uint32_t>(lane.waveform)]),
        waveformRect(_selectedLane), style);
  }

  void drawOriginal(drawing::Rect dirtyRect) {
    (void)dirtyRect;
    if (!_plugin)
      return;
    syncGuiParams(*_plugin);
    const auto style = drawing::softTextStyle();
    drawing::setFill(style.bg);
    drawing::rectFill(bounds());
    drawing::drawAtPoint_withAttributes("s3g AMBI ENCODER PULSAR",
                                        drawing::makePoint(18, 14),
                                        drawing::softTitleAttrs());
    drawing::drawEncoderPresetMenu(
        presetDisplayName(), drawing::encoderTitleBand(kGuiWidth, kGuiHeight),
        drawing::softLabelAttrs(), drawing::softValueAttrs(), style);
    drawing::drawHeaderActionButton(loadPresetRect(), loadPresetRect(), "LOAD",
                                    drawing::softLabelAttrs(), style);
    drawing::drawHeaderActionButton(savePresetRect(), savePresetRect(), "SAVE",
                                    drawing::softLabelAttrs(), style);
    drawing::drawHeaderActionButton(randomRect(), randomRect(), "RANDOM",
                                    drawing::softLabelAttrs(), style);
    drawing::drawRightStatus(drawing::peakDbText(_plugin->outputPeak.load(
                                 std::memory_order_relaxed)),
                             kGuiWidth, 14, drawing::softValueAttrs(), 18);
    drawVisualPanel(style);
    drawControlPanels(style);
    drawLanePanel(style);
    drawOpenMenu_style(drawing::softValueAttrs(), style);
  }

  void setSlider_point(const GuiSliderSpec &slider, drawing::Point point) {
    const double normalized =
        (point.x - (slider.panelX + 108.0)) / slider.trackWidth;
    editValue(slider.id, guiValue(slider.id, normalized));
    setNeedsDisplay(true);
  }

  int hitPoint(drawing::Point point) {
    if (!drawing::contains(point, fieldRect()))
      return -1;
    const uint32_t points =
        std::clamp<uint32_t>(_plugin->params.points, s3g::kAmbiPulsarMinPoints,
                             s3g::kAmbiPulsarMaxPoints);
    int best = -1;
    double bestDistance = 18.0;
    for (uint32_t index = 0u; index < points; ++index) {
      const drawing::Point projected = projectPoint_depth(index, nullptr);
      const double distance =
          std::hypot(point.x - projected.x, point.y - projected.y);
      if (distance < bestDistance) {
        bestDistance = distance;
        best = static_cast<int>(index);
      }
    }
    return best;
  }

  void mouseDown(drawing::Event *event) {
    const drawing::Point point =
        convertPoint_fromView((event)->locationInWindow(), nullptr);
    _dragView = false;
    if (_openMenu > 0) {
      const int hit =
          drawing::dropdownHitIndex(point, _openMenuRect, 21.0, _menuItemCount);
      if (hit >= 0) {
        if (_openMenu == 1)
          editValue(kPresetParamId, hit);
        else if (_openMenu == 2)
          editValue(kOrderParamId, hit + 1);
        else if (_openMenu == 3)
          editValue(kEnvelopeParamId, hit);
        else if (_openMenu == 4)
          editValue(kQualityParamId, hit);
        else if (_openMenu == 5) {
          editValue(kPointsParamId, hit + 4);
          _selectedPoint =
              std::min<uint32_t>(_selectedPoint, _plugin->params.points - 1u);
          storeViewState();
        } else if (_openMenu == 6)
          editValue(kLaneTuneModeIds[_selectedLane], hit);
        else if (_openMenu == 7)
          editValue(kLaneRetriggerModeIds[_selectedLane], hit);
        else if (_openMenu == 8)
          editValue(kLaneWaveformIds[_selectedLane], hit);
        else if (_openMenu == 9)
          editValue(kMotionModeParamId, hit);
        else if (_openMenu == 10)
          editValue(kNeuralSetParamId, hit);
        else if (_openMenu == 11)
          editValue(kPickupSetParamId, hit);
        else if (_openMenu == 12)
          editValue(kListeningModeParamId, hit);
        else if (_openMenu == 13)
          editValue(kListenerResponseParamId, hit);
      }
      _openMenu = 0;
      _hoverMenuItem = -1;
      setNeedsDisplay(true);
      return;
    }
    if (drawing::contains(point, presetRect())) {
      openMenu(1);
      return;
    }
    if (drawing::contains(point, savePresetRect())) {
      saveCustomPreset();
      return;
    }
    if (drawing::contains(point, loadPresetRect())) {
      loadCustomPreset();
      return;
    }
    if (drawing::contains(point, randomRect())) {
      randomizeSafe(*_plugin);
      rescanValues();
      setNeedsDisplay(true);
      return;
    }
    if (drawing::contains(point, orderRect())) {
      openMenu(2);
      return;
    }
    if (drawing::contains(point, envelopeRect())) {
      openMenu(3);
      return;
    }
    if (drawing::contains(point, qualityRect())) {
      openMenu(4);
      return;
    }
    if (drawing::contains(point, pointsRect())) {
      openMenu(5);
      return;
    }
    if (drawing::contains(point, motionRect())) {
      openMenu(9);
      return;
    }
    if (drawing::contains(point, captureRect())) {
      _plugin->guiCaptureRequestSerial.fetch_add(1u, std::memory_order_release);
      if (_plugin->host && _plugin->host->request_process)
        _plugin->host->request_process(_plugin->host);
      setNeedsDisplay(true);
      return;
    }
    if (drawing::contains(point, fieldPanelRect())) {
      for (int index = 0; index < 4; ++index) {
        if (drawing::contains(point, pageButtonRect(index))) {
          _visualPage = index;
          setNeedsDisplay(true);
          return;
        }
      }
      if (_visualPage == 0) {
        for (int index = 0; index < 2; ++index) {
          if (drawing::contains(point, zoomButtonRect(index))) {
            _viewZoom = std::clamp(_viewZoom + (index == 0 ? -0.15 : 0.15),
                                   static_cast<double>(0.55),
                                   static_cast<double>(2.20));
            storeViewState();
            setNeedsDisplay(true);
            return;
          }
        }
        for (int index = 0; index < 3; ++index) {
          if (drawing::contains(point, viewButtonRect(index))) {
            setViewPreset(index);
            return;
          }
        }
        const int hit = hitPoint(point);
        if (hit >= 0) {
          _selectedPoint = static_cast<uint32_t>(hit);
          storeViewState();
          setNeedsDisplay(true);
          return;
        }
        if (drawing::contains(point, fieldRect())) {
          _dragView = true;
          _lastDragPoint = point;
          return;
        }
      } else if (_visualPage == 1) {
        for (uint32_t lane = 0u; lane < s3g::kAmbiPulsarLanes; ++lane) {
          if (drawing::contains(point, pulsaretLaneRect(lane))) {
            _selectedLane = lane;
            setNeedsDisplay(true);
            return;
          }
        }
      } else if (_visualPage == 3) {
        if (drawing::contains(point, neuralSetRect())) {
          openMenu(10);
          return;
        }
        if (drawing::contains(point, pickupSetRect())) {
          openMenu(11);
          return;
        }
        if (drawing::contains(point, listeningModeRect())) {
          openMenu(12);
          return;
        }
        if (drawing::contains(point, listenerResponseRect())) {
          openMenu(13);
          return;
        }
        if (drawing::contains(point, listeningEnableRect())) {
          editValue(kListeningEnableParamId,
                    _plugin->params.listening.enabled ? 0.0 : 1.0);
          setNeedsDisplay(true);
          return;
        }
        if (drawing::contains(point, listeningBypassRect())) {
          editValue(kListeningBypassParamId,
                    _plugin->params.listening.bypass ? 0.0 : 1.0);
          setNeedsDisplay(true);
          return;
        }
      }
    }
    if (drawing::contains(point, lanePanelRect())) {
      for (uint32_t lane = 0u; lane < s3g::kAmbiPulsarLanes; ++lane) {
        if (drawing::contains(point, laneButtonRect(lane))) {
          _selectedLane = lane;
          setNeedsDisplay(true);
          return;
        }
      }
      if (drawing::contains(point, tuneModeRect(_selectedLane))) {
        openMenu(6);
        return;
      }
      if (drawing::contains(point, retriggerModeRect(_selectedLane))) {
        openMenu(7);
        return;
      }
      if (drawing::contains(point, waveformRect(_selectedLane))) {
        openMenu(8);
        return;
      }
    }
    _dragParam = CLAP_INVALID_ID;
    for (const auto &slider : kGuiSliders) {
      if (listeningGuiParam(slider.id) && _visualPage != 3)
        continue;
      const int sliderLane = guiLaneForParam(slider.id);
      if (sliderLane >= 0 && sliderLane != static_cast<int>(_selectedLane))
        continue;
      if (drawing::contains(point, guiSliderHitRect(slider))) {
        double defaultValue = 0.0;
        if (drawing::sliderDoubleClickDefault(event, &_plugin->plugin,
                                              slider.id, &defaultValue)) {
          editValue(slider.id, defaultValue);
          _dragParam = CLAP_INVALID_ID;
          setNeedsDisplay(true);
          return;
        }
        _dragParam = slider.id;
        setSlider_point(slider, point);
        return;
      }
    }
  }

  void mouseDragged(drawing::Event *event) {
    const drawing::Point point =
        convertPoint_fromView((event)->locationInWindow(), nullptr);
    if (_dragView) {
      _viewMode = -1;
      _viewAzDeg =
          std::fmod(_viewAzDeg + (point.x - _lastDragPoint.x) * 0.55 + 540.0,
                    360.0) -
          180.0;
      _viewElDeg = std::clamp(_viewElDeg + (point.y - _lastDragPoint.y) * 0.45,
                              -85.0, 85.0);
      _lastDragPoint = point;
      storeViewState();
      setNeedsDisplay(true);
      return;
    }
    if (_dragParam == CLAP_INVALID_ID)
      return;
    const auto *slider = guiSlider(_dragParam);
    if (!slider)
      return;
    setSlider_point(*slider, point);
  }

  void mouseUp(drawing::Event *event) {
    (void)event;
    _dragParam = CLAP_INVALID_ID;
    _dragView = false;
  }

  void mouseMoved(drawing::Event *event) {
    if (_openMenu <= 0)
      return;
    const drawing::Point point =
        convertPoint_fromView((event)->locationInWindow(), nullptr);
    const int next =
        drawing::dropdownHitIndex(point, _openMenuRect, 21.0, _menuItemCount);
    if (next != _hoverMenuItem) {
      _hoverMenuItem = next;
      setNeedsDisplay(true);
    }
  }
};
} // namespace circuit_encoder_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new circuit_encoder_canvas::Editor(p);
}
#include "s3g_clap_canvas_gui.inc"
