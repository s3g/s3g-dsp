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
constexpr auto kOscillatorPanel =
    layout::stackPanel(layout::PanelRole::Engine, kOutputPanel,
                       layout::toolboxHeightForRows(7u), 7u);
constexpr auto kCrossPanel =
    layout::stackPanel(layout::PanelRole::Relationships, kOscillatorPanel,
                       layout::toolboxHeightForRows(8u), 8u);
constexpr auto kFilterPanel =
    layout::stackPanel(layout::PanelRole::ToneShape, kCrossPanel,
                       layout::toolboxHeightForRows(7u), 7u);
constexpr std::array kFirstColumnPanels{kOutputPanel, kOscillatorPanel,
                                        kCrossPanel, kFilterPanel};

constexpr auto kTopologyPanel = layout::makePanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Topology,
    layout::kLargeEncoderSecondColumn, 42.0, layout::toolboxHeightForRows(7u),
    7u);
constexpr auto kProjectionPanel =
    layout::stackPanel(layout::PanelRole::Projection, kTopologyPanel,
                       layout::toolboxHeightForRows(4u), 4u);
constexpr auto kVoiceMaskPanel =
    layout::stackPanel(layout::PanelRole::Modulation, kProjectionPanel,
                       layout::toolboxHeightForRows(4u), 4u);
constexpr auto kOscShapePanel =
    layout::stackPanel(layout::PanelRole::ToneShape, kVoiceMaskPanel,
                       layout::toolboxHeightForRows(
                           8u, layout::kStandardMetrics.toolboxBottomClearance,
                           layout::kStandardMetrics.compactRowPitch),
                       8u, layout::kStandardMetrics.compactRowPitch);
constexpr std::array kSecondColumnPanels{kTopologyPanel, kProjectionPanel,
                                         kVoiceMaskPanel, kOscShapePanel};

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
static_assert(layout::topologyControlMatches(
    kTopologyPanel, layout::SharedControlRole::TopologyCollapse));
static_assert(layout::sourceCardinalityControlMatches(
    kOscillatorPanel, layout::SharedControlRole::SourceCardinality));

struct GuiSliderSpec {
  clap_id id;
  double panelX;
  double y;
  double min;
  double max;
  bool logarithmic;
};

constexpr GuiSliderSpec kGuiSliders[]{
    {kOutputParamId, kOutputPanel.frame.x, layout::rowY(kOutputPanel, 0u),
     -60.0, 12.0, false},
    {kVoicesParamId, kOscillatorPanel.frame.x,
     layout::rowY(kOscillatorPanel, 0u), 1.0, 64.0, false},
    {kRateAParamId, kOscillatorPanel.frame.x,
     layout::rowY(kOscillatorPanel, 3u), 0.0, 1.0, false},
    {kRateBParamId, kOscillatorPanel.frame.x,
     layout::rowY(kOscillatorPanel, 4u), 0.0, 1.0, false},
    {kSpreadParamId, kOscillatorPanel.frame.x,
     layout::rowY(kOscillatorPanel, 5u), 0.0, 1.0, false},
    {kDeviationParamId, kOscillatorPanel.frame.x,
     layout::rowY(kOscillatorPanel, 6u), 0.0, 1.0, false},
    {kFmAtoBParamId, kCrossPanel.frame.x, layout::rowY(kCrossPanel, 1u), 0.0,
     1.0, false},
    {kFmBtoAParamId, kCrossPanel.frame.x, layout::rowY(kCrossPanel, 2u), 0.0,
     1.0, false},
    {kRunglerAParamId, kCrossPanel.frame.x, layout::rowY(kCrossPanel, 3u), 0.0,
     1.0, false},
    {kRunglerBParamId, kCrossPanel.frame.x, layout::rowY(kCrossPanel, 4u), 0.0,
     1.0, false},
    {kRungSizeParamId, kCrossPanel.frame.x, layout::rowY(kCrossPanel, 5u), 2.0,
     8.0, false},
    {kChangeParamId, kCrossPanel.frame.x, layout::rowY(kCrossPanel, 6u), 0.0,
     1.0, false},
    {kThresholdParamId, kCrossPanel.frame.x, layout::rowY(kCrossPanel, 7u), 0.0,
     1.0, false},
    {kFilterMorphParamId, kFilterPanel.frame.x, layout::rowY(kFilterPanel, 0u),
     0.0, 2.0, false},
    {kColorParamId, kFilterPanel.frame.x, layout::rowY(kFilterPanel, 1u), 0.0,
     1.0, false},
    {kFilterParamId, kFilterPanel.frame.x, layout::rowY(kFilterPanel, 2u), 0.0,
     1.0, false},
    {kResonanceParamId, kFilterPanel.frame.x, layout::rowY(kFilterPanel, 3u),
     0.0, 1.0, false},
    {kFilterRunParamId, kFilterPanel.frame.x, layout::rowY(kFilterPanel, 4u),
     0.0, 1.0, false},
    {kFilterSweepParamId, kFilterPanel.frame.x, layout::rowY(kFilterPanel, 5u),
     0.0, 1.0, false},
    {kSaturationParamId, kFilterPanel.frame.x, layout::rowY(kFilterPanel, 6u),
     0.0, 1.0, false},
    {kTopologyRateParamId, kTopologyPanel.frame.x,
     layout::rowY(kTopologyPanel,
                  layout::topologyRow(layout::SharedControlRole::TopologyRate)),
     0.001, 2.0, true},
    {kTopologyAmountParamId, kTopologyPanel.frame.x,
     layout::rowY(
         kTopologyPanel,
         layout::topologyRow(layout::SharedControlRole::TopologyAmount)),
     0.0, 1.0, false},
    {kTopologyDepthParamId, kTopologyPanel.frame.x,
     layout::rowY(
         kTopologyPanel,
         layout::topologyRow(layout::SharedControlRole::TopologyDepth)),
     0.0, 1.0, false},
    {kTopologyScaleParamId, kTopologyPanel.frame.x,
     layout::rowY(
         kTopologyPanel,
         layout::topologyRow(layout::SharedControlRole::TopologyScale)),
     0.20, 2.50, false},
    {kTopologyCollapseParamId, kTopologyPanel.frame.x,
     layout::rowY(
         kTopologyPanel,
         layout::topologyRow(layout::SharedControlRole::TopologyCollapse)),
     0.0, 1.0, false},
    {kAzimuthParamId, kProjectionPanel.frame.x,
     layout::rowY(kProjectionPanel, 0u), -180.0, 180.0, false},
    {kElevationParamId, kProjectionPanel.frame.x,
     layout::rowY(kProjectionPanel, 1u), -90.0, 90.0, false},
    {kDistanceParamId, kProjectionPanel.frame.x,
     layout::rowY(kProjectionPanel, 2u), 0.15, 2.0, false},
    {kSpatialFollowParamId, kProjectionPanel.frame.x,
     layout::rowY(kProjectionPanel, 3u), 0.0, 1.0, false},
    {kFieldParamId, kVoiceMaskPanel.frame.x, layout::rowY(kVoiceMaskPanel, 1u),
     0.0, 1.0, false},
    {kMaskDepthParamId, kVoiceMaskPanel.frame.x,
     layout::rowY(kVoiceMaskPanel, 2u), 0.0, 1.0, false},
    {kMaskRateParamId, kVoiceMaskPanel.frame.x,
     layout::rowY(kVoiceMaskPanel, 3u), 0.001, 4.0, true},
    {kRampAParamId, kOscShapePanel.frame.x, layout::rowY(kOscShapePanel, 2u),
     0.0, 1.0, false},
    {kRampBParamId, kOscShapePanel.frame.x, layout::rowY(kOscShapePanel, 3u),
     0.0, 1.0, false},
    {kSnapParamId, kOscShapePanel.frame.x, layout::rowY(kOscShapePanel, 4u),
     0.0, 1.0, false},
    {kSnapDecayParamId, kOscShapePanel.frame.x,
     layout::rowY(kOscShapePanel, 5u), 0.0, 1.0, false},
    {kPwmAParamId, kOscShapePanel.frame.x, layout::rowY(kOscShapePanel, 6u),
     0.0, 1.0, false},
    {kPwmBParamId, kOscShapePanel.frame.x, layout::rowY(kOscShapePanel, 7u),
     0.0, 1.0, false},
    {kFieldReturnParamId, 38, 540, 0.0, 1.0, false},
    {kPropagationParamId, 38, 574, 0.0, 1.0, false},
    {kFieldWriteParamId, 314, 540, 0.0, 1.0, false},
    {kRegisterMotionParamId, 314, 574, 0.0, 1.0, false},
    {kSettleAmountParamId, 38, 540, 0.0, 1.0, false},
    {kSettleTargetParamId, 38, 574, 0.0, 0.95, false},
    {kSettleRecoveryParamId, 314, 540, 0.25, 12.0, true},
};

static_assert(kGuiSliders[0].id == kOutputParamId);

const GuiSliderSpec *guiSliderSpec(clap_id id) {
  for (const auto &spec : kGuiSliders) {
    if (spec.id == id)
      return &spec;
  }
  return nullptr;
}

bool isListenerGuiParam(clap_id id) {
  return id == kFieldWriteParamId || id == kRegisterMotionParamId ||
         id == kFieldReturnParamId || id == kPropagationParamId ||
         id == kSettleAmountParamId || id == kSettleTargetParamId ||
         id == kSettleRecoveryParamId;
}

double sliderNorm(const GuiSliderSpec &spec, double value) {
  if (spec.id == kAzimuthParamId)
    return s3g::aedAzimuthSliderNorm(static_cast<float>(value));
  if (spec.logarithmic) {
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
  if (spec.logarithmic)
    return spec.min * std::pow(spec.max / spec.min, norm);
  return spec.min + norm * (spec.max - spec.min);
}

double rateModeScaleForDisplay(uint32_t mode) {
  switch (mode) {
  case 0u:
    return 0.01;
  case 2u:
    return 2.0;
  default:
    return 1.0;
  }
}

double rateNormToHzForDisplay(double value, uint32_t mode) {
  return 440.0 *
         std::pow(2.0, (std::clamp(value, 0.0, 1.0) * 172.0 - 109.0) / 12.0) *
         rateModeScaleForDisplay(mode);
}

class Editor final : public s3g::portable_gui::routing::Canvas {
public:
  Plugin *_plugin;
  s3g::AmbiWranglerParams _paramsSnapshot;
  s3g::AmbiWranglerParams _displayParamsSnapshot;
  WranglerSurface _surfaceSnapshot;
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
  int _curveBank;
  uint32_t _selectedCurveDimension;
  uint32_t _curveVoiceBank;
  int _dragBreakpointRow;
  int _dragBreakpointVoice;
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
#include "s3g_circuit_encoder_editor.inc"
  Editor(Plugin &instance, bool popup = false)
      : Canvas(popup ? 596 : kGuiWidth, popup ? 608 : kGuiHeight) {
    auto *plugin = &instance;

    _plugin = plugin;
    _paramsSnapshot = {};
    _displayParamsSnapshot = {};
    _surfaceSnapshot = {};
    _presetIndexSnapshot = 0u;
    _customPresetNameSnapshot[0] = '\0';
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
    _curveBank = 0;
    _selectedCurveDimension = 0u;
    _curveVoiceBank = 0u;
    _dragBreakpointRow = -1;
    _dragBreakpointVoice = -1;
    _openMenu = 0;
    _hoverMenuItem = -1;
    _menuItemCount = 0;
    _openMenuRect = drawing::Rect{};
    _surfaceEdit = true;
    _selectedSurfaceCell = -1;
    _dragSurfaceCell = -1;
    _dragSurfaceCursor = false;
    _surfacePopupChild = false;
    _surfacePopupOwner = nullptr;
    _surfacePopupView = nullptr;
    refreshControlSnapshot();
    if (_surfaceSnapshot.cellCount > 0u)
      _selectedSurfaceCell = 0;

    _surfacePopupChild = popup;
    if (popup)
      _fieldPage = 3;
    service();
  }
  ~Editor() override { stopRefresh(); }
  bool loadPresetPath(const std::string &path) {
    CustomPresetFile file{};
    if (!loadCustomPresetFile(path.c_str(), file))
      return false;
    const auto current = controlStateSnapshot(*_plugin);
    file.params.outputGainDb = current.params.outputGainDb;
    replaceControlState(*_plugin, file.params, 0u,
                        file.name[0] ? file.name : "Custom");
    refreshControlSnapshot();
    setNeedsDisplay(true);
    return true;
  }
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
    const auto snapshot = controlStateSnapshot(*_plugin);
    _paramsSnapshot = snapshot.params;
    _surfaceSnapshot = snapshot.surface;
    const bool audioActive = _plugin->active.load(std::memory_order_acquire);
    const float effectiveX =
        audioActive ? _plugin->effectiveSurfaceX.load(std::memory_order_relaxed)
                    : _paramsSnapshot.surfaceX;
    const float effectiveY =
        audioActive ? _plugin->effectiveSurfaceY.load(std::memory_order_relaxed)
                    : _paramsSnapshot.surfaceY;
    _displayParamsSnapshot =
        _surfaceEdit ? _paramsSnapshot
                     : wranglerSurfaceParams(_paramsSnapshot, _surfaceSnapshot,
                                             effectiveX, effectiveY);
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

  drawing::Rect curveBankButtonRect(int index) {
    const drawing::Rect field = fieldRect();
    return drawing::makeRect(field.origin.x + 10.0 + index * 88.0,
                             field.origin.y + 10.0, 82.0, 18.0);
  }

  drawing::Rect curvesToggleRect() {
    const drawing::Rect field = fieldRect();
    return drawing::makeRect(drawing::maxX(field) - 131.0,
                             field.origin.y + 10.0, 121.0, 18.0);
  }

  drawing::Rect curveParameterButtonRect(uint32_t localIndex) {
    const drawing::Rect field = fieldRect();
    const double left = field.origin.x + 10.0;
    const double top = field.origin.y + 39.0;
    const double gapX = 7.0;
    const double gapY = 6.0;
    const double height = 20.0;
    const uint32_t columns = _curveBank == 0 ? 4u : 5u;
    const double width = _curveBank == 0 ? 130.75 : 103.2;
    return drawing::makeRect(
        left + static_cast<double>(localIndex % columns) * (width + gapX),
        top + static_cast<double>(localIndex / columns) * (height + gapY),
        width, height);
  }

  drawing::Rect curveVoiceBankButtonRect(uint32_t index) {
    const drawing::Rect field = fieldRect();
    constexpr double gap = 7.0;
    constexpr double width = 130.75;
    return drawing::makeRect(field.origin.x + 10.0 +
                                 static_cast<double>(index) * (width + gap),
                             field.origin.y + 171.0, width, 19.0);
  }

  drawing::Rect curvePlotRect() {
    const drawing::Rect field = fieldRect();
    return drawing::makeRect(field.origin.x + 10.0, field.origin.y + 201.0,
                             field.size.width - 20.0, 278.0);
  }

  drawing::Rect curveGraphRect() {
    const drawing::Rect plot = curvePlotRect();
    return drawing::makeRect(plot.origin.x + 30.0, plot.origin.y + 14.0,
                             plot.size.width - 44.0, plot.size.height - 40.0);
  }

  drawing::Rect pageButtonRect(int index) {
    const drawing::Rect panel = fieldPanelRect();
    return drawing::makeRect(panel.origin.x + 98.0 + index * 74.0,
                             panel.origin.y + 4.0, 70.0, 13.0);
  }

  drawing::Rect surfaceEditRect() {
    const drawing::Rect field = fieldRect();
    return drawing::makeRect(field.origin.x + 10.0, field.origin.y + 10.0, 50.0,
                             18.0);
  }

  drawing::Rect surfaceEnableRect() {
    const drawing::Rect field = fieldRect();
    return drawing::makeRect(field.origin.x + 64.0, field.origin.y + 10.0, 48.0,
                             18.0);
  }

  drawing::Rect surfaceAddRect() {
    const drawing::Rect field = fieldRect();
    return drawing::makeRect(field.origin.x + 116.0, field.origin.y + 10.0,
                             42.0, 18.0);
  }

  drawing::Rect surfaceDeleteRect() {
    const drawing::Rect field = fieldRect();
    return drawing::makeRect(field.origin.x + 162.0, field.origin.y + 10.0,
                             42.0, 18.0);
  }

  drawing::Rect surfaceCaptureRect() {
    const drawing::Rect field = fieldRect();
    return drawing::makeRect(field.origin.x + 208.0, field.origin.y + 10.0,
                             52.0, 18.0);
  }

  drawing::Rect surfacePresetRect() {
    const drawing::Rect field = fieldRect();
    return drawing::makeRect(field.origin.x + 270.0, field.origin.y + 10.0,
                             174.0, 18.0);
  }

  drawing::Rect surfacePopRect() {
    const drawing::Rect field = fieldRect();
    return drawing::makeRect(field.origin.x + 448.0, field.origin.y + 10.0,
                             26.0, 18.0);
  }

  drawing::Rect surfaceFocusMinusRect() {
    const drawing::Rect field = fieldRect();
    return drawing::makeRect(field.origin.x + 178.0, field.origin.y + 38.0,
                             20.0, 18.0);
  }

  drawing::Rect surfaceFocusPlusRect() {
    const drawing::Rect field = fieldRect();
    return drawing::makeRect(field.origin.x + 238.0, field.origin.y + 38.0,
                             20.0, 18.0);
  }

  drawing::Rect surfaceCurveRect() {
    const drawing::Rect field = fieldRect();
    return drawing::makeRect(field.origin.x + 10.0, field.origin.y + 38.0,
                             112.0, 18.0);
  }

  drawing::Rect surfaceGlideMinusRect() {
    const drawing::Rect field = fieldRect();
    return drawing::makeRect(field.origin.x + 326.0, field.origin.y + 38.0,
                             20.0, 18.0);
  }

  drawing::Rect surfaceGlidePlusRect() {
    const drawing::Rect field = fieldRect();
    return drawing::makeRect(field.origin.x + 420.0, field.origin.y + 38.0,
                             20.0, 18.0);
  }

  drawing::Rect surfacePlotRect() {
    const drawing::Rect field = fieldRect();
    return drawing::makeRect(field.origin.x + 10.0, field.origin.y + 68.0,
                             field.size.width - 20.0, field.size.height - 80.0);
  }

  void syncSurfaceEditMode(bool editing) {
    _surfaceEdit = editing;
    refreshControlSnapshot();
    if (_surfacePopupChild && _surfacePopupOwner) {
      _surfacePopupOwner->_surfaceEdit = editing;
      (_surfacePopupOwner)->refreshControlSnapshot();
      (_surfacePopupOwner)->setNeedsDisplay(true);
    } else if (_surfacePopupView) {
      _surfacePopupView->_surfaceEdit = editing;
      (_surfacePopupView)->refreshControlSnapshot();
      (_surfacePopupView)->setNeedsDisplay(true);
    }
  }

  drawing::Rect listenVoiceRect() {
    return drawing::makeRect(112.0, 484.0, 104.0, 15.0);
  }

  drawing::Rect pickupSetRect() {
    return drawing::makeRect(306.0, 484.0, 104.0, 15.0);
  }

  drawing::Rect listenModeRect() {
    return drawing::makeRect(488.0, 484.0, 104.0, 15.0);
  }

  drawing::Rect listenerResponseRect() {
    return drawing::makeRect(306.0, 508.0, 104.0, 15.0);
  }

  drawing::Rect listenEnableRect() {
    return drawing::makeRect(46.0, 610.0, 118.0, 15.0);
  }

  drawing::Rect returnBypassRect() {
    return drawing::makeRect(172.0, 610.0, 118.0, 15.0);
  }

  drawing::Rect circuitLawButtonRect() {
    return drawing::makeRect(kOscillatorPanel.frame.x + 146.0,
                             kOscillatorPanel.frame.y + 4.0, 92.0, 13.0);
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
    if (!_plugin || voice >= s3g::kAmbiWranglerMaxVoices)
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

  void drawBreakpointEditor_valueAttrs_style(drawing::Attrs attrs,
                                             drawing::Attrs valueAttrs,
                                             const drawing::Style &style) {
    (void)style;
    const drawing::Rect field = fieldRect();
    drawing::setFill(drawing::calibratedColor(0x090909));
    drawing::rectFill(field);
    drawing::setStroke(drawing::calibratedColor(0x555555));
    drawing::frameRect(field);

    static drawing::String
        dimensionLabels[s3g::kAmbiWranglerCurveDimensionCount] = {
            "RATE A",        "RUNG A",        "FM B>A",     "COLOR A",
            "RATE B",        "RUNG B",        "FM A>B",     "COLOR B",
            "FILTER FREQ A", "FILTER FREQ B", "FILTER RES", "FILTER COMP",
            "FILTER TYPE",   "CROSS A",       "CROSS B",    "CROSS LPF",
            "RUNG MODE",     "RUNG THRESH",   "RUNG SIZE",  "PWM A",
            "PWM B",         "RAMP A",        "RAMP B",     "LEVEL",
        };
    static drawing::String rungModeNames[] = {
        "COMMON",
        "SPLIT",
        "SWAP",
    };

    auto drawButton = [&](drawing::Rect rect, drawing::String label,
                          bool selected, bool enabled) {
      drawing::setFill(drawing::calibratedColor(
          selected ? 0x302b16 : (enabled ? 0x171717 : 0x101010)));
      drawing::rectFill(rect);
      drawing::setStroke(drawing::calibratedColor(
          selected ? 0xf0d86a : (enabled ? 0x555555 : 0x292929)));
      drawing::frameRect(rect);
      if (selected) {
        drawing::setFill(drawing::calibratedColor(0xf0d86a));
        drawing::rectFill(drawing::makeRect(rect.origin.x + 1.0,
                                            rect.origin.y + 1.0, 3.0,
                                            rect.size.height - 2.0));
      }
      const drawing::Size size = drawing::sizeWithAttributes(label, attrs);
      drawing::drawAtPoint_withAttributes(
          label,
          drawing::makePoint(drawing::midX(rect) - size.width * 0.5,
                             drawing::midY(rect) - size.height * 0.5),
          attrs);
    };

    _curveBank = std::clamp(_curveBank, 0, 1);
    if (_curveBank == 0 &&
        _selectedCurveDimension >=
            s3g::kAmbiWranglerHistoricalCurveDimensionCount) {
      _selectedCurveDimension = 0u;
    } else if (_curveBank == 1 &&
               _selectedCurveDimension <
                   s3g::kAmbiWranglerHistoricalCurveDimensionCount) {
      _selectedCurveDimension = s3g::kAmbiWranglerHistoricalCurveDimensionCount;
    }
    _selectedCurveDimension = std::min<uint32_t>(
        _selectedCurveDimension, s3g::kAmbiWranglerCurveDimensionCount - 1u);
    _curveVoiceBank = std::min<uint32_t>(_curveVoiceBank, 3u);
    const uint32_t bankFirstVoice = _curveVoiceBank * 16u;
    if (_selectedVoice < bankFirstVoice ||
        _selectedVoice >= bankFirstVoice + 16u) {
      _selectedVoice = bankFirstVoice;
    }
    _selectedVoice =
        std::min<uint32_t>(_selectedVoice, s3g::kAmbiWranglerMaxVoices - 1u);
    const auto dimension =
        static_cast<WranglerCurveDimension>(_selectedCurveDimension);

    drawButton(curveBankButtonRect(0), "HIST · 19", _curveBank == 0, true);
    drawButton(curveBankButtonRect(1), "MODERN · 5", _curveBank == 1, true);
    drawButton(curvesToggleRect(),
               _paramsSnapshot.voiceBreakpointsEnabled ? "CURVES ON"
                                                       : "CURVES OFF",
               _paramsSnapshot.voiceBreakpointsEnabled, true);

    drawing::String bankContext =
        _curveBank == 0 ? "EXACT WRNGLR.GENDSP ORDER" : "WRANGLER EXTENSIONS";
    drawing::drawAtPoint_withAttributes(
        bankContext,
        drawing::makePoint(field.origin.x + 196.0, field.origin.y + 14.0),
        valueAttrs);

    const uint32_t firstDimension =
        _curveBank == 0 ? 0u : s3g::kAmbiWranglerHistoricalCurveDimensionCount;
    const uint32_t dimensionCount =
        _curveBank == 0 ? s3g::kAmbiWranglerHistoricalCurveDimensionCount
                        : s3g::kAmbiWranglerCurveDimensionCount -
                              s3g::kAmbiWranglerHistoricalCurveDimensionCount;
    for (uint32_t local = 0u; local < dimensionCount; ++local) {
      const uint32_t raw = firstDimension + local;
      drawButton(curveParameterButtonRect(local), dimensionLabels[raw],
                 raw == _selectedCurveDimension, true);
    }

    for (uint32_t bank = 0u; bank < 4u; ++bank) {
      drawing::String label = drawing::stringFormat(
          "VOICES %u–%u", bank * 16u + 1u, bank * 16u + 16u);
      drawButton(curveVoiceBankButtonRect(bank), label, bank == _curveVoiceBank,
                 true);
    }

    const drawing::Rect plot = curvePlotRect();
    const drawing::Rect graph = curveGraphRect();
    drawing::setFill(drawing::calibratedColor(0x0c0c0c));
    drawing::rectFill(plot);
    drawing::setStroke(drawing::calibratedColor(0x454545));
    drawing::frameRect(plot);

    for (uint32_t tick = 0u; tick <= 4u; ++tick) {
      const double normalized = static_cast<double>(tick) / 4.0;
      const double y = drawing::maxY(graph) -
                       static_cast<double>(normalized) * graph.size.height;
      drawing::setStroke(
          drawing::calibratedColor(tick == 2u ? 0x464646 : 0x292929));
      drawing::Path::strokeLineFromPoint_toPoint(
          drawing::makePoint(graph.origin.x, y),
          drawing::makePoint(drawing::maxX(graph), y));
      drawing::String tickLabel =
          dimension == WranglerCurveDimension::FilterType
              ? (tick == 0u
                     ? "LP"
                     : (tick == 2u ? "OPEN"
                                   : (tick == 4u ? "HP"
                                                 : drawing::stringFormat(
                                                       "%.2f", normalized))))
              : drawing::stringFormat("%.2f", normalized);
      drawing::drawAtPoint_withAttributes(
          tickLabel, drawing::makePoint(plot.origin.x + 4.0, y - 5.0),
          valueAttrs);
    }

    const bool stepped = breakpointIsStepped(dimension);
    drawing::Path storedPath = drawing::Path::bezierPath();
    drawing::Path effectivePath = drawing::Path::bezierPath();
    double previousStoredY = 0.0;
    double previousEffectiveY = 0.0;
    for (uint32_t slot = 0u; slot < 16u; ++slot) {
      const uint32_t voice = bankFirstVoice + slot;
      const double stored =
          breakpointStoredValue(_paramsSnapshot, dimension, voice);
      const double effective =
          breakpointFinalValue(_paramsSnapshot, dimension, voice);
      const double x =
          graph.origin.x + graph.size.width * static_cast<double>(slot) / 15.0;
      const double storedY = drawing::maxY(graph) -
                             graph.size.height * static_cast<double>(stored);
      const double effectiveY =
          drawing::maxY(graph) -
          graph.size.height * static_cast<double>(effective);
      if (slot == 0u) {
        (storedPath).moveToPoint(drawing::makePoint(x, storedY));
        (effectivePath).moveToPoint(drawing::makePoint(x, effectiveY));
      } else if (stepped) {
        (storedPath).lineToPoint(drawing::makePoint(x, previousStoredY));
        (storedPath).lineToPoint(drawing::makePoint(x, storedY));
        (effectivePath).lineToPoint(drawing::makePoint(x, previousEffectiveY));
        (effectivePath).lineToPoint(drawing::makePoint(x, effectiveY));
      } else {
        (storedPath).lineToPoint(drawing::makePoint(x, storedY));
        (effectivePath).lineToPoint(drawing::makePoint(x, effectiveY));
      }
      previousStoredY = storedY;
      previousEffectiveY = effectiveY;
    }

    double dash[] = {3.0, 3.0};
    (storedPath).setLineDash_count_phase(dash, 2, 0.0);
    (storedPath).setLineWidth(1.0);
    drawing::setStroke(drawing::calibratedColor(0x727272, 0.72));
    (storedPath).stroke();
    (effectivePath).setLineWidth(1.7);
    drawing::setStroke(drawing::withAlpha(
        drawing::calibratedColor(0xa7c7ba),
        _paramsSnapshot.voiceBreakpointsEnabled ? 0.94 : 0.38));
    (effectivePath).stroke();

    const uint32_t activeVoices = std::clamp<uint32_t>(
        _paramsSnapshot.voices, 1u, s3g::kAmbiWranglerMaxVoices);
    for (uint32_t slot = 0u; slot < 16u; ++slot) {
      const uint32_t voice = bankFirstVoice + slot;
      const bool selected = voice == _selectedVoice;
      const bool active = voice < activeVoices;
      const double effective =
          breakpointFinalValue(_paramsSnapshot, dimension, voice);
      const double x =
          graph.origin.x + graph.size.width * static_cast<double>(slot) / 15.0;
      const double y = drawing::maxY(graph) -
                       graph.size.height * static_cast<double>(effective);
      const double pointSize = selected ? 9.0 : 5.0;
      drawing::setFill(drawing::calibratedColor(
          selected ? 0xf0d86a : (active ? 0xa7c7ba : 0x4d4d4d)));
      (drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
           x - pointSize * 0.5, y - pointSize * 0.5, pointSize, pointSize)))
          .fill();
      drawing::String voiceLabel = drawing::stringFormat("%u", voice + 1u);
      const drawing::Size labelSize =
          drawing::sizeWithAttributes(voiceLabel, valueAttrs);
      drawing::drawAtPoint_withAttributes(
          voiceLabel,
          drawing::makePoint(x - labelSize.width * 0.5,
                             drawing::maxY(graph) + 7.0),
          valueAttrs);
    }

    const double stored =
        breakpointStoredValue(_paramsSnapshot, dimension, _selectedVoice);
    const double effective =
        breakpointFinalValue(_paramsSnapshot, dimension, _selectedVoice);
    drawing::String effectiveText{};
    if (dimension == WranglerCurveDimension::FilterType) {
      if (effective <= 0.0005) {
        effectiveText = "LP";
      } else if (std::fabs(effective - 0.5) <= 0.0005) {
        effectiveText = "OPEN";
      } else if (effective >= 0.9995) {
        effectiveText = "HP";
      } else {
        effectiveText =
            drawing::stringFormat("LP→OPEN→HP %.0f%%", effective * 100.0);
      }
    } else if (dimension == WranglerCurveDimension::RungMode) {
      effectiveText = rungModeNames[std::min<uint32_t>(
          static_cast<uint32_t>(std::lround(effective * 2.0)), 2u)];
    } else if (dimension == WranglerCurveDimension::RungSize) {
      effectiveText = drawing::stringFormat(
          "%u",
          2u + std::min<uint32_t>(
                   static_cast<uint32_t>(std::lround(effective * 6.0)), 6u));
    } else {
      effectiveText = drawing::stringFormat("%.3f", effective);
    }

    drawing::String status = drawing::stringFormat(
        "V%02u  %@  STORED %.3f  EFFECTIVE %@  %@", _selectedVoice + 1u,
        dimensionLabels[_selectedCurveDimension], stored, effectiveText,
        _selectedVoice < activeVoices ? "ACTIVE" : "INACTIVE");
    drawing::drawAtPoint_withAttributes(
        status,
        drawing::makePoint(field.origin.x + 10.0, field.origin.y + 491.0),
        valueAttrs);
    drawing::String instruction =
        _paramsSnapshot.voiceBreakpointsEnabled
            ? "SOLID = EFFECTIVE   DASH = STORED   CLICK PLOT TO LOCK ONE "
              "VOICE; DRAG CHANGES Y ONLY"
            : "CURVES OFF: SELECTORS ONLY ARM A ROW; TURN ON EXPLICITLY BEFORE "
              "EDITING";
    drawing::drawAtPoint_withAttributes(
        instruction,
        drawing::makePoint(field.origin.x + 10.0, drawing::maxY(field) - 20.0),
        valueAttrs);
  }

  void drawListenerMenuLabel_value_rect_style(drawing::String label,
                                              drawing::String value,
                                              drawing::Rect rect,
                                              const drawing::Style &style) {
    drawing::Attrs labels = drawing::softLabelAttrs();
    drawing::Attrs values = drawing::softValueAttrs();
    drawing::drawAtPoint_withAttributes(
        label, drawing::makePoint(rect.origin.x - 72.0, rect.origin.y + 1.0),
        labels);
    drawing::setFill(style.strip);
    drawing::rectFill(rect);
    drawing::setStroke(style.grid);
    drawing::frameRect(rect);
    drawing::setFill(style.fill);
    drawing::rectFill(drawing::makeRect(
        rect.origin.x + 1.0, rect.origin.y + 1.0, 2.0, rect.size.height - 2.0));
    drawing::drawAtPoint_withAttributes(
        value, drawing::makePoint(rect.origin.x + 8.0, rect.origin.y + 2.0),
        values);
    drawing::drawAtPoint_withAttributes(
        "v",
        drawing::makePoint(drawing::maxX(rect) - 13.0, rect.origin.y + 1.0),
        values);
  }

  void drawListenerVoiceLabel(const drawing::Style &style) {
    const drawing::Rect rect = listenVoiceRect();
    drawing::Attrs labels = drawing::softLabelAttrs();
    drawing::Attrs values = drawing::softValueAttrs();
    drawing::drawAtPoint_withAttributes(
        "VOICE", drawing::makePoint(rect.origin.x - 72.0, rect.origin.y + 1.0),
        labels);
    drawing::setFill(style.strip);
    drawing::rectFill(rect);
    drawing::setStroke(style.grid);
    drawing::frameRect(rect);
    drawing::setFill(style.fill);
    drawing::rectFill(drawing::makeRect(
        rect.origin.x + 1.0, rect.origin.y + 1.0, 2.0, rect.size.height - 2.0));
    drawing::drawAtPoint_withAttributes(
        "<", drawing::makePoint(rect.origin.x + 8.0, rect.origin.y + 1.0),
        values);
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat("VOICE %02u", _selectedVoice + 1u),
        drawing::makePoint(rect.origin.x + 25.0, rect.origin.y + 2.0), values);
    drawing::drawAtPoint_withAttributes(
        ">",
        drawing::makePoint(drawing::maxX(rect) - 15.0, rect.origin.y + 1.0),
        values);
  }

  void drawListenerPage_valueAttrs_style(drawing::Attrs attrs,
                                         drawing::Attrs valueAttrs,
                                         const drawing::Style &style) {
    const drawing::Rect field = fieldRect();
    drawing::setFill(drawing::calibratedColor(0x090909));
    drawing::rectFill(field);
    drawing::setStroke(drawing::calibratedColor(0x555555));
    drawing::frameRect(field);
    drawing::drawAtPoint_withAttributes(
        "AMBISONIC AUDITORY BODY / NEGATIVE-FEEDBACK REGISTER",
        drawing::makePoint(field.origin.x + 10.0, field.origin.y + 10.0),
        valueAttrs);

    const uint32_t voices = std::clamp<uint32_t>(
        _surfaceEdit ? _paramsSnapshot.voices
                     : _plugin->guiVoiceCount.load(std::memory_order_relaxed),
        1u, s3g::kAmbiWranglerMaxVoices);
    _selectedVoice = std::min<uint32_t>(_selectedVoice, voices - 1u);
    const uint32_t pickupCount =
        _paramsSnapshot.pickupSet == s3g::AmbiWranglerPickupSet::Tetra4 ? 4u
                                                                        : 8u;
    const uint32_t readEar =
        _plugin->guiReadEar[_selectedVoice].load(std::memory_order_relaxed) %
        pickupCount;
    const float activity =
        _plugin->guiListenerActivity.load(std::memory_order_relaxed);
    const float tension =
        std::clamp(_plugin->guiListenerTension.load(std::memory_order_relaxed),
                   0.0f, 1.0f);
    const float capture = std::clamp(
        _plugin->guiCapture[_selectedVoice].load(std::memory_order_relaxed),
        0.0f, 1.0f);
    const float evolutionRate =
        capture > 0.000001f
            ? std::clamp(_plugin->guiEvolutionRate[_selectedVoice].load(
                             std::memory_order_relaxed),
                         0.03f, 1.0f)
            : 1.0f;
    const float couplingRate =
        capture > 0.000001f
            ? std::clamp(_plugin->guiCouplingRate[_selectedVoice].load(
                             std::memory_order_relaxed),
                         0.22f, 1.0f)
            : 1.0f;
    const float topologyRate = std::clamp(
        _plugin->guiTopologyRate.load(std::memory_order_relaxed), 0.12f, 1.0f);
    const bool registerHeld = _plugin->guiRegisterHeld[_selectedVoice].load(
                                  std::memory_order_relaxed) != 0u;
    const uint64_t heldClockCount =
        _plugin->guiHeldClockCount[_selectedVoice].load(
            std::memory_order_relaxed);
    const bool settleResponse = _paramsSnapshot.listenerResponse ==
                                s3g::AmbiWranglerListenerResponse::Settle;
    const float returned =
        _plugin->guiFieldReturn[_selectedVoice].load(std::memory_order_relaxed);
    const uint32_t comparatorBit =
        _plugin->guiComparatorBit[_selectedVoice].load(
            std::memory_order_relaxed);
    const uint32_t auditoryBit =
        _plugin->guiAuditoryBit[_selectedVoice].load(std::memory_order_relaxed);
    const uint32_t writtenBit =
        _plugin->guiWrittenBit[_selectedVoice].load(std::memory_order_relaxed);
    const float writePulse = _plugin->guiFieldWritePulse[_selectedVoice].load(
        std::memory_order_relaxed);
    const uint32_t word =
        _plugin->guiRegister[_selectedVoice].load(std::memory_order_relaxed);
    const uint32_t rungSize =
        std::clamp<uint32_t>(_paramsSnapshot.rungSize, 2u, 8u);

    constexpr float k = 0.57735026919f;
    static constexpr std::array<s3g::Vec3, 4> tetra{{
        {k, k, k},
        {-k, -k, k},
        {-k, k, -k},
        {k, -k, -k},
    }};
    const auto &cube = s3g::ambiFieldListenerCubeDirections();
    const drawing::Point center =
        drawing::makePoint(drawing::midX(field), field.origin.y + 215.0);
    const double radius = 148.0;
    drawing::setStroke(drawing::calibratedColor(0x303030));
    (drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
         center.x - radius, center.y - radius, radius * 2.0, radius * 2.0)))
        .stroke();

    std::array<drawing::Point, s3g::kAmbiFieldListenerMaxLobes> ears{};
    const float cameraAz = static_cast<float>(38.0 * s3g::kPi / 180.0);
    const float cameraEl = static_cast<float>(32.0 * s3g::kPi / 180.0);
    for (uint32_t ear = 0u; ear < pickupCount; ++ear) {
      const s3g::Vec3 direction = pickupCount == 4u ? tetra[ear] : cube[ear];
      const float x1 =
          std::cos(cameraAz) * direction.x - std::sin(cameraAz) * direction.y;
      const float y1 =
          std::sin(cameraAz) * direction.x + std::cos(cameraAz) * direction.y;
      const float y2 =
          std::cos(cameraEl) * y1 + std::sin(cameraEl) * direction.z;
      ears[ear] =
          drawing::makePoint(center.x + x1 * radius, center.y - y2 * radius);
    }

    const double cellStep = 29.0;
    const double registerWidth = cellStep * static_cast<double>(rungSize);
    const double registerX = center.x - registerWidth * 0.5;
    const double registerY = center.y - 11.0;
    const drawing::Rect registerBody = drawing::makeRect(
        registerX - 8.0, registerY - 8.0, registerWidth + 10.0, 38.0);
    drawing::setFill(drawing::calibratedColor(0x101010));
    drawing::rectFill(registerBody);
    drawing::setStroke(drawing::calibratedColor(0x505050));
    drawing::frameRect(registerBody);

    const drawing::Point inputPoint = drawing::makePoint(
        registerX + (rungSize - 1u) * cellStep + 10.0, registerY + 10.0);
    drawing::setStroke(drawing::calibratedColor(0x707070, 0.42));
    drawing::Path returnPath = drawing::Path::bezierPath();
    (returnPath).moveToPoint(ears[readEar]);
    (returnPath).lineToPoint(inputPoint);
    (returnPath).setLineWidth(0.8 + std::fabs(returned) * 2.4);
    (returnPath).stroke();

    for (uint32_t bit = 0u; bit < rungSize; ++bit) {
      const uint32_t shift = rungSize - 1u - bit;
      const bool set = ((word >> shift) & 1u) != 0u;
      const bool inputCell = bit == rungSize - 1u;
      const bool fieldWrite = inputCell && writePulse > 0.08f;
      const int shade = fieldWrite ? 0xe8 : (set ? 0xb0 : 0x24);
      drawing::setFill(
          drawing::calibratedColor((shade << 16) | (shade << 8) | shade, 0.96));
      const drawing::Rect cell =
          drawing::makeRect(registerX + bit * cellStep, registerY, 20.0, 20.0);
      drawing::rectFill(cell);
      drawing::setStroke(drawing::calibratedColor(
          inputCell ? 0xb8b8b8 : 0x555555, fieldWrite ? 1.0 : 0.72));
      drawing::frameRect(cell);
      if (bit + 1u < rungSize) {
        drawing::setStroke(drawing::calibratedColor(0x686868, 0.62));
        drawing::Path::strokeLineFromPoint_toPoint(
            drawing::makePoint(drawing::maxX(cell), drawing::midY(cell)),
            drawing::makePoint(drawing::maxX(cell) + cellStep - 20.0,
                               drawing::midY(cell)));
      }
      drawing::String value = set ? "1" : "0";
      drawing::drawAtPoint_withAttributes(
          value,
          drawing::makePoint(drawing::midX(cell) - 3.0,
                             drawing::midY(cell) - 7.0),
          drawing::textAttrs(
              drawing::calibratedColor(set || fieldWrite ? 0x101010 : 0x8c8c8c),
              8.0));
    }

    for (uint32_t ear = 0u; ear < pickupCount; ++ear) {
      const float energy = std::clamp(
          _plugin->guiListenerEnvelope[ear].load(std::memory_order_relaxed) /
              0.08f,
          0.0f, 1.0f);
      const double halo = 8.0 + std::sqrt(energy) * 18.0;
      drawing::setFill(drawing::withAlpha(
          drawing::calibratedColor(0xc8c8c8, 0.04 + energy * 0.20),
          0.04 + energy * 0.20));
      (drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
           ears[ear].x - halo, ears[ear].y - halo, halo * 2.0, halo * 2.0)))
          .fill();
      const double diamondRadius = ear == readEar ? 7.5 : 6.0;
      drawing::Path diamond = drawing::Path::bezierPath();
      (diamond).moveToPoint(
          drawing::makePoint(ears[ear].x, ears[ear].y - diamondRadius));
      (diamond).lineToPoint(
          drawing::makePoint(ears[ear].x + diamondRadius, ears[ear].y));
      (diamond).lineToPoint(
          drawing::makePoint(ears[ear].x, ears[ear].y + diamondRadius));
      (diamond).lineToPoint(
          drawing::makePoint(ears[ear].x - diamondRadius, ears[ear].y));
      (diamond).closePath();
      drawing::setFill(drawing::calibratedColor(
          ear == readEar ? 0xd6d6d6 : 0x8c8c8c, 0.34 + energy * 0.66));
      (diamond).fill();
      if (ear == readEar) {
        drawing::setStroke(drawing::calibratedColor(0xf0f0f0, 0.92));
        (diamond).stroke();
      }
      drawing::drawAtPoint_withAttributes(
          drawing::stringFormat("E%u", ear + 1u),
          drawing::makePoint(ears[ear].x + 8.0, ears[ear].y - 6.0), valueAttrs);
    }

    drawing::String registerStatus =
        settleResponse
            ? drawing::stringFormat(
                  "VOICE %02u  REGISTER 0x%02X  /  %@  %llu CLOCKS",
                  _selectedVoice + 1u, word & 0xffu,
                  registerHeld ? "HELD" : "OPEN",
                  static_cast<unsigned long long>(heldClockCount))
            : drawing::stringFormat("VOICE %02u  REGISTER 0x%02X  /  READ E%u",
                                    _selectedVoice + 1u, word & 0xffu,
                                    readEar + 1u);
    drawing::drawAtPoint_withAttributes(
        registerStatus,
        drawing::makePoint(center.x - 96.0, field.origin.y + 374.0),
        valueAttrs);
    drawing::String bitStatus =
        settleResponse
            ? drawing::stringFormat("TENSION %3.0f%%    CAPTURE %3.0f%%    "
                                    "EVOLVE x%.2f    COUPLE x%.2f",
                                    tension * 100.0f, capture * 100.0f,
                                    evolutionRate, couplingRate)
            : drawing::stringFormat(
                  "COMPARATOR %u    EAR %u    WRITE %u    RETURN %+.3f",
                  comparatorBit, auditoryBit, writtenBit, returned);
    drawing::drawAtPoint_withAttributes(
        bitStatus, drawing::makePoint(center.x - 126.0, field.origin.y + 392.0),
        valueAttrs);

    static constexpr const char *modeNames[] = {"TRACE", "RING", "CROSS",
                                                "BALANCE"};
    drawListenerVoiceLabel(style);
    drawListenerMenuLabel_value_rect_style(
        "EARS", pickupCount == 4u ? "TETRA 4" : "CUBE 8", pickupSetRect(),
        style);
    drawListenerMenuLabel_value_rect_style(
        "READ",
        drawing::stringFromUtf8(modeNames[std::min<uint32_t>(
            static_cast<uint32_t>(_paramsSnapshot.listenMode), 3u)]),
        listenModeRect(), style);
    drawListenerMenuLabel_value_rect_style("RESPONSE",
                                           settleResponse ? "SETTLE" : "WRITE",
                                           listenerResponseRect(), style);

    if (settleResponse) {
      drawSlider_param_value_attrs_valueAttrs_style(
          "VISCOSITY", kSettleAmountParamId, _paramsSnapshot.settleAmount,
          attrs, valueAttrs, style);
      drawSlider_param_value_attrs_valueAttrs_style(
          "CALM TARGET", kSettleTargetParamId, _paramsSnapshot.settleTarget,
          attrs, valueAttrs, style);
      drawSlider_param_value_attrs_valueAttrs_style(
          "RECOVERY", kSettleRecoveryParamId,
          _paramsSnapshot.settleRecoverySeconds, attrs, valueAttrs, style);
      drawing::drawAtPoint_withAttributes(
          "CAPTURE", drawing::makePoint(330.0, 575.0), attrs);
      const drawing::Rect tensionTrack =
          drawing::makeRect(422.0, 579.0, 82.0, 5.0);
      drawing::setFill(style.strip);
      drawing::rectFill(tensionTrack);
      drawing::setStroke(style.grid);
      drawing::frameRect(tensionTrack);
      drawing::setFill(style.fill);
      drawing::rectFill(drawing::makeRect(
          tensionTrack.origin.x + 1.0, tensionTrack.origin.y + 1.0,
          (tensionTrack.size.width - 2.0) * capture,
          tensionTrack.size.height - 2.0));
      drawing::drawAtPoint_withAttributes(
          drawing::stringFormat("%3.0f%% / E%.2f / T%.2f", capture * 100.0f,
                                evolutionRate, topologyRate),
          drawing::makePoint(510.0, 574.0), valueAttrs);
    } else {
      drawSlider_param_value_attrs_valueAttrs_style(
          "FIELD RETURN", kFieldReturnParamId, _paramsSnapshot.fieldReturn,
          attrs, valueAttrs, style);
      drawSlider_param_value_attrs_valueAttrs_style(
          "PROPAGATION", kPropagationParamId, _paramsSnapshot.propagation,
          attrs, valueAttrs, style);
      drawSlider_param_value_attrs_valueAttrs_style(
          "FIELD WRITE", kFieldWriteParamId, _paramsSnapshot.fieldWrite, attrs,
          valueAttrs, style);
      drawSlider_param_value_attrs_valueAttrs_style(
          "REG MOTION", kRegisterMotionParamId, _paramsSnapshot.registerMotion,
          attrs, valueAttrs, style);
    }

    const drawing::Rect controlHeader =
        drawing::makeRect(field.origin.x, 604.0, field.size.width, 27.0);
    drawing::drawToolboxHeaderButton(
        listenEnableRect(), controlHeader,
        _paramsSnapshot.listeningEnabled ? "LISTEN ON" : "LISTEN OFF",
        _paramsSnapshot.listeningEnabled != 0u, attrs, style);
    drawing::drawToolboxHeaderButton(
        returnBypassRect(), controlHeader,
        settleResponse
            ? "RETURN INERT"
            : (_paramsSnapshot.returnBypass ? "BYPASSED" : "RETURN ACTIVE"),
        !settleResponse && _paramsSnapshot.returnBypass != 0u, attrs, style);
    drawing::String status =
        settleResponse
            ? drawing::stringFormat(
                  "EARS %3.0f%%  CAPTURE %3.0f%%  EVOLVE x%.2f  TOPO x%.2f",
                  activity * 100.0f, capture * 100.0f, evolutionRate,
                  topologyRate)
            : drawing::stringFormat(
                  "EARS %3.0f%%  WRITE %3.0f%%  RETURN %3.0f%%",
                  activity * 100.0f,
                  _plugin->guiListenerEnableGain.load(
                      std::memory_order_relaxed) *
                      100.0f,
                  _plugin->guiReturnEnableGain.load(std::memory_order_relaxed) *
                      100.0f);
    drawing::drawRightStatus(status, drawing::maxX(field) - 4.0, 610.0,
                             valueAttrs, 0.0);
  }

  drawing::Point surfacePointForCell(uint32_t index) {
    const drawing::Rect plot = surfacePlotRect();
    if (index >= _surfaceSnapshot.cellCount)
      return drawing::Point{};
    const auto &cell = _surfaceSnapshot.cells[index];
    return drawing::makePoint(plot.origin.x + cell.x * plot.size.width,
                              drawing::maxY(plot) - cell.y * plot.size.height);
  }

  int hitSurfaceCell(drawing::Point point) {
    if (!drawing::contains(point, surfacePlotRect()))
      return -1;
    int best = -1;
    double bestDistance = 14.0;
    for (uint32_t index = 0u; index < _surfaceSnapshot.cellCount; ++index) {
      const drawing::Point site = surfacePointForCell(index);
      const double distance = std::hypot(point.x - site.x, point.y - site.y);
      if (distance < bestDistance) {
        bestDistance = distance;
        best = static_cast<int>(index);
      }
    }
    return best;
  }

  void updateSurfacePosition_cursor(drawing::Point point, bool cursor) {
    if (!_plugin)
      return;
    const drawing::Rect plot = surfacePlotRect();
    const float x = static_cast<float>(
        std::clamp((point.x - plot.origin.x) / plot.size.width, 0.0, 1.0));
    const float y = static_cast<float>(std::clamp(
        (drawing::maxY(plot) - point.y) / plot.size.height, 0.0, 1.0));
    if (cursor) {
      editValue(kSurfaceXParamId, x);
      editValue(kSurfaceYParamId, y);
    } else if (_dragSurfaceCell >= 0) {
      setControlSurfaceCellPosition(
          *_plugin, static_cast<uint32_t>(_dragSurfaceCell), x, y);
    }
    refreshControlSnapshot();
    setNeedsDisplay(true);
  }

  void drawSurfacePage_valueAttrs_style(drawing::Attrs attrs,
                                        drawing::Attrs valueAttrs,
                                        const drawing::Style &style) {
    const drawing::Rect field = fieldRect();
    const drawing::Rect plot = surfacePlotRect();
    drawing::setFill(drawing::calibratedColor(0x090909));
    drawing::rectFill(field);
    drawing::setStroke(drawing::calibratedColor(0x555555));
    drawing::frameRect(field);
    const drawing::Rect controlHeader = drawing::makeRect(
        field.origin.x, field.origin.y, field.size.width, 36.0);
    drawing::drawToolboxHeaderButton(surfaceEditRect(), controlHeader,
                                     _surfaceEdit ? "EDIT" : "PLAY",
                                     _surfaceEdit, attrs, style);
    drawing::drawToolboxHeaderButton(surfaceEnableRect(), controlHeader,
                                     _surfaceSnapshot.enabled ? "ON" : "OFF",
                                     _surfaceSnapshot.enabled, attrs, style);
    drawing::drawToolboxHeaderButton(surfaceAddRect(), controlHeader, "ADD",
                                     false, attrs, style);
    drawing::drawToolboxHeaderButton(surfaceDeleteRect(), controlHeader, "DEL",
                                     false, attrs, style);
    drawing::drawToolboxHeaderButton(surfaceCaptureRect(), controlHeader, "CAP",
                                     false, attrs, style);
    drawing::drawToolboxHeaderButton(
        surfacePopRect(), controlHeader, "POP",
        _surfacePopupChild || (_surfacePanel && _surfacePanel->visible()),
        attrs, style);
    const drawing::Rect preset = surfacePresetRect();
    drawing::setFill(style.strip);
    drawing::rectFill(preset);
    drawing::setStroke(style.grid);
    drawing::frameRect(preset);
    drawing::String cellName = "SELECT CELL";
    if (_selectedSurfaceCell >= 0 &&
        static_cast<uint32_t>(_selectedSurfaceCell) <
            _surfaceSnapshot.cellCount) {
      const auto &cell =
          _surfaceSnapshot.cells[static_cast<uint32_t>(_selectedSurfaceCell)];
      if (cell.name[0] != '\0')
        cellName = drawing::stringFromUtf8(cell.name);
    }
    drawing::drawAtPoint_withAttributes(
        cellName,
        drawing::makePoint(preset.origin.x + 7.0, preset.origin.y + 2.0),
        valueAttrs);
    drawing::drawToolboxHeaderButton(surfaceFocusMinusRect(), controlHeader,
                                     "-", false, attrs, style);
    drawing::drawToolboxHeaderButton(surfaceFocusPlusRect(), controlHeader, "+",
                                     false, attrs, style);
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat("%.2f", _surfaceSnapshot.focus),
        drawing::makePoint(field.origin.x + 202.0, field.origin.y + 40.0),
        valueAttrs);
    drawing::drawAtPoint_withAttributes(
        "FOCUS",
        drawing::makePoint(field.origin.x + 132.0, field.origin.y + 40.0),
        attrs);
    const drawing::Rect curve = surfaceCurveRect();
    drawing::setFill(style.strip);
    drawing::rectFill(curve);
    drawing::setStroke(style.grid);
    drawing::frameRect(curve);
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat("CURVE  %s", s3g::parameterSurfaceCurveName(
                                               _surfaceSnapshot.curve)),
        drawing::makePoint(curve.origin.x + 7.0, curve.origin.y + 2.0),
        valueAttrs);
    drawing::drawAtPoint_withAttributes(
        "GLIDE",
        drawing::makePoint(field.origin.x + 278.0, field.origin.y + 40.0),
        attrs);
    drawing::drawToolboxHeaderButton(surfaceGlideMinusRect(), controlHeader,
                                     "-", false, attrs, style);
    drawing::drawToolboxHeaderButton(surfaceGlidePlusRect(), controlHeader, "+",
                                     false, attrs, style);
    drawing::String glideText =
        _surfaceSnapshot.glideMs < 0.5f
            ? "OFF"
            : drawing::stringFormat("%.0f MS", _surfaceSnapshot.glideMs);
    drawing::drawAtPoint_withAttributes(
        glideText,
        drawing::makePoint(field.origin.x + 350.0, field.origin.y + 40.0),
        valueAttrs);

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
            "T %.3f %.3f   /   A %.3f %.3f   /   %u CELLS   /   %@",
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
    drawing::drawPanelHeader("VOICE FIELD", true, panel.origin.x,
                             panel.origin.y, panel.size.width, 21, attrs,
                             style);
    const drawing::Rect header =
        drawing::makeRect(panel.origin.x, panel.origin.y, panel.size.width, 21);
    drawing::drawToolboxHeaderButton(pageButtonRect(0), header, "FIELD",
                                     _fieldPage == 0, attrs, style);
    drawing::drawToolboxHeaderButton(pageButtonRect(1), header, "CURVE",
                                     _fieldPage == 1, attrs, style);
    drawing::drawToolboxHeaderButton(pageButtonRect(2), header, "LISTEN",
                                     _fieldPage == 2, attrs, style);
    drawing::drawToolboxHeaderButton(pageButtonRect(3), header, "SURF",
                                     _fieldPage == 3, attrs, style);
    if (_fieldPage == 1) {
      drawBreakpointEditor_valueAttrs_style(attrs, valueAttrs, style);
      return;
    }
    if (_fieldPage == 2) {
      drawListenerPage_valueAttrs_style(attrs, valueAttrs, style);
      return;
    }
    if (_fieldPage == 3) {
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
        1u, s3g::kAmbiWranglerMaxVoices);
    _selectedVoice = std::min<uint32_t>(_selectedVoice, voices - 1u);
    std::array<drawing::Point, s3g::kAmbiWranglerMaxVoices> projected{};
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
      const float mask = std::clamp(
          _plugin->guiMask[voice].load(std::memory_order_relaxed), 0.0f, 1.0f);
      const float membership =
          _surfaceEdit ? 1.0f
                       : std::clamp(_plugin->guiRenderGain[voice].load(
                                        std::memory_order_relaxed),
                                    0.0f, 1.0f);
      const double base = voices > 32u ? 7.0 : 9.0;
      const double size =
          ((selected ? base + 5.0 : base) +
           std::clamp(std::sqrt(std::max(0.0f, energy)) * 34.0f, 0.0f, 8.0f)) *
          (0.55 + mask * 0.45) * (0.45 + membership * 0.55);
      const drawing::Rect marker =
          drawing::makeRect(projected[voice].x - size * 0.5,
                            projected[voice].y - size * 0.5, size, size);
      if (mask > 0.12f) {
        const double halo = size * (1.2 + mask * 1.4);
        drawing::Rect haloRect =
            drawing::makeRect(projected[voice].x - halo * 0.5,
                              projected[voice].y - halo * 0.5, halo, halo);
        drawing::setFill(
            drawing::withAlpha(voiceColor_selected(voice, selected),
                               (0.08 + mask * 0.16) * membership));
        (drawing::Path::bezierPathWithOvalInRect(haloRect)).fill();
      }
      drawing::setFill(drawing::withAlpha(
          voiceColor_selected(voice, selected),
          (selected ? 0.98 : 0.22 + mask * 0.70) * membership));
      drawing::rectFill(marker);
      drawing::setStroke(drawing::calibratedColor(
          selected ? 0xe6e6e6 : 0x4f4f4f,
          (selected ? 1.0 : 0.25 + mask * 0.55) * membership));
      drawing::frameRect(marker);
      drawing::String label = drawing::stringFormat("%u", voice + 1u);
      const drawing::Size labelSize =
          drawing::sizeWithAttributes(label, idAttrs);
      if (membership > 0.45f && (mask > 0.25f || selected)) {
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
    const float mask =
        _plugin->guiMask[_selectedVoice].load(std::memory_order_relaxed);
    drawing::String readout =
        drawing::stringFormat("V%02u  AZ%+.1f  EL%+.1f  D%.2f  M%.2f  A%.3f",
                              _selectedVoice + 1u, az, el, dist, mask, energy);
    drawing::drawRightStatus(readout, drawing::maxX(field), field.origin.y + 7,
                             valueAttrs, 8.0);
    drawing::drawAtPoint_withAttributes(
        "ONE COMPLETE RUNGLER CIRCUIT PER VOICE / AMBISONIC POINT     ACN/SN3D",
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
    if (param == kRateAParamId || param == kRateBParamId) {
      const uint32_t mode = param == kRateAParamId
                                ? _displayParamsSnapshot.rateModeA
                                : _displayParamsSnapshot.rateModeB;
      const double hz = rateNormToHzForDisplay(value, mode);
      if (hz < 1.0)
        std::snprintf(display, sizeof(display), "%.4f Hz", hz);
      else if (hz < 100.0)
        std::snprintf(display, sizeof(display), "%.2f Hz", hz);
      else
        std::snprintf(display, sizeof(display), "%.0f Hz", hz);
    } else {
      paramsValueToText(nullptr, param, value, display, sizeof(display));
    }
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
    if (_customPresetNameSnapshot[0]) {
      return drawing::stringFormat("CUSTOM: %s", _customPresetNameSnapshot);
    }
    return drawing::stringFromUtf8(
        s3g::ambiWranglerFactoryPresetInfo(_presetIndexSnapshot).name);
  }

  void drawPanels_valueAttrs_style(drawing::Attrs attrs,
                                   drawing::Attrs valueAttrs,
                                   const drawing::Style &style) {
    const auto p = _displayParamsSnapshot;
    drawing::drawPanelFrame(kOutputPanel, style);
    drawing::drawPanelHeader("OUTPUT", true, kOutputPanel, attrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "OUT", kOutputParamId, p.outputGainDb, attrs, valueAttrs, style);
    drawMenu_value_panelX_y_attrs_valueAttrs_style(
        "ORDER", drawing::stringFormat("%uOA", p.order), kOutputPanel.frame.x,
        layout::rowY(kOutputPanel, layout::kLargeEncoderOrderSlot.row), attrs,
        valueAttrs, style);

    drawing::drawPanelFrame(kOscillatorPanel, style);
    drawing::drawPanelHeader("SOURCE / OSCILLATORS", true, kOscillatorPanel,
                             attrs, style);
    drawing::drawToolboxHeaderButton(
        circuitLawButtonRect(), drawing::cocoaRect(kOscillatorPanel.frame),
        p.circuitLaw == s3g::AmbiWranglerCircuitLaw::Bounded ? "CIR BOUNDED"
                                                             : "CIR LEGACY",
        p.circuitLaw == s3g::AmbiWranglerCircuitLaw::Bounded, attrs, style);
    static constexpr const char *kRateRangeNames[] = {"LOW", "SINGLE",
                                                      "DOUBLE"};
    drawMenu_value_panelX_y_attrs_valueAttrs_style(
        "RNG A",
        drawing::stringFromUtf8(
            kRateRangeNames[std::min<uint32_t>(p.rateModeA, 2u)]),
        kOscillatorPanel.frame.x, layout::rowY(kOscillatorPanel, 1u), attrs,
        valueAttrs, style);
    drawMenu_value_panelX_y_attrs_valueAttrs_style(
        "RNG B",
        drawing::stringFromUtf8(
            kRateRangeNames[std::min<uint32_t>(p.rateModeB, 2u)]),
        kOscillatorPanel.frame.x, layout::rowY(kOscillatorPanel, 2u), attrs,
        valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "VOICES", kVoicesParamId, p.voices, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "RATE A", kRateAParamId, p.rateA, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "RATE B", kRateBParamId, p.rateB, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "SPREAD", kSpreadParamId, p.spread, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "DEV", kDeviationParamId, p.deviation, attrs, valueAttrs, style);

    drawing::drawPanelFrame(kCrossPanel, style);
    drawing::drawPanelHeader("CROSS / RUNGLER", true, kCrossPanel, attrs,
                             style);
    static constexpr const char *kLoopNames[] = {"OFF", "LOOP", "XOR"};
    drawMenu_value_panelX_y_attrs_valueAttrs_style(
        "LOOP",
        drawing::stringFromUtf8(kLoopNames[std::min<uint32_t>(p.rungLoop, 2u)]),
        kCrossPanel.frame.x, layout::rowY(kCrossPanel, 0u), attrs, valueAttrs,
        style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "FM A>B", kFmAtoBParamId, p.fmAtoB, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "FM B>A", kFmBtoAParamId, p.fmBtoA, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "RUNG A", kRunglerAParamId, p.runglerA, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "RUNG B", kRunglerBParamId, p.runglerB, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "SIZE", kRungSizeParamId, p.rungSize, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "CHANGE", kChangeParamId, p.change, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "THRESH", kThresholdParamId, p.threshold, attrs, valueAttrs, style);

    drawing::drawPanelFrame(kFilterPanel, style);
    drawing::drawPanelHeader("FILTER", true, kFilterPanel, attrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "LP→OPEN→HP", kFilterMorphParamId,
        static_cast<double>(p.filterMorph) * 2.0, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        p.circuitLaw == s3g::AmbiWranglerCircuitLaw::Bounded ? "Z COLOR"
                                                             : "COLOR",
        kColorParamId, p.color, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "FIL FRQ", kFilterParamId, p.filter, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "FIL RES", kResonanceParamId, p.resonance, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "FIL RUN", kFilterRunParamId, p.filterRun, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "FIL SWP", kFilterSweepParamId, p.filterSweep, attrs, valueAttrs,
        style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "SAT", kSaturationParamId, p.saturation, attrs, valueAttrs, style);

    drawing::drawPanelFrame(kTopologyPanel, style);
    drawing::drawPanelHeader("TOPOLOGY", true, kTopologyPanel, attrs, style);
    drawMenu_value_panelX_y_attrs_valueAttrs_style(
        "SHAPE",
        drawing::stringFromUtf8(s3g::topologyShapeName(p.topologyShape)),
        kTopologyPanel.frame.x,
        layout::rowY(
            kTopologyPanel,
            layout::topologyRow(layout::SharedControlRole::TopologyShape)),
        attrs, valueAttrs, style);
    drawMenu_value_panelX_y_attrs_valueAttrs_style(
        "MOTION",
        drawing::stringFromUtf8(s3g::topologyMotionModeName(p.topologyMotion)),
        kTopologyPanel.frame.x,
        layout::rowY(
            kTopologyPanel,
            layout::topologyRow(layout::SharedControlRole::TopologyMotion)),
        attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style("RATE", kTopologyRateParamId,
                                                  p.topologyRateHz, attrs,
                                                  valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style("AMT", kTopologyAmountParamId,
                                                  p.topologyAmount, attrs,
                                                  valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "DEPTH", kTopologyDepthParamId, p.topologyDepth, attrs, valueAttrs,
        style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "SCALE", kTopologyScaleParamId, p.topologyScale, attrs, valueAttrs,
        style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "COLL", kTopologyCollapseParamId, p.topologyCollapse, attrs, valueAttrs,
        style);

    drawing::drawPanelFrame(kProjectionPanel, style);
    drawing::drawPanelHeader("PROJECTION", true, kProjectionPanel, attrs,
                             style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "AZIM", kAzimuthParamId, p.centerAzimuthDeg, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style("ELEV", kElevationParamId,
                                                  p.centerElevationDeg, attrs,
                                                  valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "DIST", kDistanceParamId, p.centerDistance, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "FOLLOW", kSpatialFollowParamId, p.spatialFollow, attrs, valueAttrs,
        style);

    drawing::drawPanelFrame(kVoiceMaskPanel, style);
    drawing::drawPanelHeader("VOICE MASK", true, kVoiceMaskPanel, attrs, style);
    static constexpr const char *kMaskNames[] = {"ALL",   "BREATH", "CHOIR",
                                                 "CELLS", "SPARK",  "WEAVE"};
    drawMenu_value_panelX_y_attrs_valueAttrs_style(
        "MODE",
        drawing::stringFromUtf8(kMaskNames[std::min<uint32_t>(p.maskMode, 5u)]),
        kVoiceMaskPanel.frame.x, layout::rowY(kVoiceMaskPanel, 0u), attrs,
        valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "FIELD", kFieldParamId, p.field, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "DEPTH", kMaskDepthParamId, p.maskDepth, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "RATE", kMaskRateParamId, p.maskRateHz, attrs, valueAttrs, style);

    drawing::drawPanelFrame(kOscShapePanel, style);
    drawing::drawPanelHeader("OSC SHAPE / PULSE", true, kOscShapePanel, attrs,
                             style);
    drawMenu_value_panelX_y_attrs_valueAttrs_style(
        "IN A", (p.inputA == 0u ? "SQUARE" : "TRI"), kOscShapePanel.frame.x,
        layout::rowY(kOscShapePanel, 0u), attrs, valueAttrs, style);
    drawMenu_value_panelX_y_attrs_valueAttrs_style(
        "IN B", (p.inputB == 0u ? "SQUARE" : "TRI"), kOscShapePanel.frame.x,
        layout::rowY(kOscShapePanel, 1u), attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "RAMP A", kRampAParamId, p.rampA, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "RAMP B", kRampBParamId, p.rampB, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style("SNAP", kSnapParamId, p.snap,
                                                  attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "DECAY", kSnapDecayParamId, p.snapDecay, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style("PWM A", kPwmAParamId, p.pwmA,
                                                  attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style("PWM B", kPwmBParamId, p.pwmB,
                                                  attrs, valueAttrs, style);
  }

  drawing::Rect menuBoxRect(int menu) {
    switch (menu) {
    case 1:
      return presetMenuRect();
    case 2:
      return drawing::cocoaRect(layout::menuBoxRect(kOscillatorPanel, 1u));
    case 3:
      return drawing::cocoaRect(layout::menuBoxRect(kOscillatorPanel, 2u));
    case 4:
      return drawing::cocoaRect(layout::menuBoxRect(
          kTopologyPanel,
          layout::topologyRow(layout::SharedControlRole::TopologyShape)));
    case 5:
      return drawing::cocoaRect(layout::menuBoxRect(
          kTopologyPanel,
          layout::topologyRow(layout::SharedControlRole::TopologyMotion)));
    case 6:
      return drawing::cocoaRect(layout::menuBoxRect(kVoiceMaskPanel, 0u));
    case 7:
      return drawing::cocoaRect(layout::menuBoxRect(kOscShapePanel, 0u));
    case 8:
      return drawing::cocoaRect(layout::menuBoxRect(kOscShapePanel, 1u));
    case 9:
      return drawing::cocoaRect(layout::menuBoxRect(kCrossPanel, 0u));
    case 10:
      return drawing::cocoaRect(layout::menuBoxRect(
          kOutputPanel, layout::kLargeEncoderOrderSlot.row));
    case 12:
      return pickupSetRect();
    case 13:
      return listenModeRect();
    case 14:
      return listenerResponseRect();
    case 15:
      return _fieldPage == 3 ? surfacePresetRect() : drawing::Rect{};
    case 16:
      return _fieldPage == 3 ? surfaceCurveRect() : drawing::Rect{};
    default:
      return drawing::Rect{};
    }
  }

  uint32_t menuCount(int menu) {
    switch (menu) {
    case 1:
      return s3g::kAmbiWranglerFactoryPresetCount;
    case 2:
      return 3u;
    case 3:
      return 3u;
    case 4:
      return s3g::kTopologyShapeCount;
    case 5:
      return s3g::kTopologyMotionModeCount;
    case 6:
      return 6u;
    case 7:
      return 2u;
    case 8:
      return 2u;
    case 9:
      return 3u;
    case 10:
      return 7u;
    case 12:
      return 2u;
    case 13:
      return 4u;
    case 14:
      return 2u;
    case 15:
      return s3g::kAmbiWranglerFactoryPresetCount;
    case 16:
      return s3g::kParameterSurfaceCurveCount;
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
    static drawing::String rateItems[] = {"LOW", "SINGLE", "DOUBLE"};
    static drawing::String orderItems[] = {"1OA", "2OA", "3OA", "4OA",
                                           "5OA", "6OA", "7OA"};
    static drawing::String loopItems[] = {"OFF", "LOOP", "XOR"};
    static drawing::String maskItems[] = {"ALL",   "BREATH", "CHOIR",
                                          "CELLS", "SPARK",  "WEAVE"};
    static drawing::String inputItems[] = {"SQUARE", "TRI"};
    static drawing::String pickupItems[] = {"TETRA 4", "CUBE 8"};
    static drawing::String listenItems[] = {"TRACE", "RING", "CROSS",
                                            "BALANCE"};
    static drawing::String responseItems[] = {"WRITE", "SETTLE"};
    static drawing::String curveItems[] = {"SOFT", "LINEAR", "SMOOTH", "TIGHT"};
    static drawing::String presetItems[s3g::kAmbiWranglerFactoryPresetCount];
    static drawing::String shapeItems[s3g::kTopologyShapeCount];
    static drawing::String motionItems[s3g::kTopologyMotionModeCount];

    for (uint32_t i = 0; i < s3g::kAmbiWranglerFactoryPresetCount; ++i)
      presetItems[i] =
          drawing::stringFromUtf8(s3g::ambiWranglerFactoryPresetInfo(i).name);
    for (uint32_t i = 0; i < s3g::kTopologyShapeCount; ++i)
      shapeItems[i] = drawing::stringFromUtf8(s3g::topologyShapeName(i));
    for (uint32_t i = 0; i < s3g::kTopologyMotionModeCount; ++i)
      motionItems[i] = drawing::stringFromUtf8(s3g::topologyMotionModeName(i));

    const auto &displayParams = _displayParamsSnapshot;
    drawing::String *items = presetItems;
    int selected = static_cast<int>(_presetIndexSnapshot);
    if (_openMenu == 2) {
      items = rateItems;
      selected = static_cast<int>(displayParams.rateModeA);
    } else if (_openMenu == 3) {
      items = rateItems;
      selected = static_cast<int>(displayParams.rateModeB);
    } else if (_openMenu == 4) {
      items = shapeItems;
      selected = static_cast<int>(displayParams.topologyShape);
    } else if (_openMenu == 5) {
      items = motionItems;
      selected = static_cast<int>(displayParams.topologyMotion);
    } else if (_openMenu == 6) {
      items = maskItems;
      selected = static_cast<int>(displayParams.maskMode);
    } else if (_openMenu == 7) {
      items = inputItems;
      selected = static_cast<int>(displayParams.inputA);
    } else if (_openMenu == 8) {
      items = inputItems;
      selected = static_cast<int>(displayParams.inputB);
    } else if (_openMenu == 9) {
      items = loopItems;
      selected = static_cast<int>(displayParams.rungLoop);
    } else if (_openMenu == 10) {
      items = orderItems;
      selected = static_cast<int>(displayParams.order) - 1;
    } else if (_openMenu == 12) {
      items = pickupItems;
      selected = static_cast<int>(_paramsSnapshot.pickupSet);
    } else if (_openMenu == 13) {
      items = listenItems;
      selected = static_cast<int>(_paramsSnapshot.listenMode);
    } else if (_openMenu == 14) {
      items = responseItems;
      selected = static_cast<int>(_paramsSnapshot.listenerResponse);
    } else if (_openMenu == 15) {
      items = presetItems;
      selected = -1;
      if (_selectedSurfaceCell >= 0 &&
          static_cast<uint32_t>(_selectedSurfaceCell) <
              _surfaceSnapshot.cellCount) {
        selected =
            _surfaceSnapshot.cells[static_cast<uint32_t>(_selectedSurfaceCell)]
                .presetIndex;
      }
    } else if (_openMenu == 16) {
      items = curveItems;
      selected =
          static_cast<int>(static_cast<uint32_t>(_surfaceSnapshot.curve));
    }
    drawing::drawDropdownMenu(_openMenuRect, 21.0, items, _menuItemCount,
                              selected, _hoverMenuItem, attrs, style);
  }

  void drawOriginal(drawing::Rect dirtyRect) {
    (void)dirtyRect;
    if (!_plugin)
      return;
    refreshControlSnapshot();
    const drawing::Style style = drawing::softTextStyle();
    drawing::setFill(style.bg);
    drawing::rectFill(bounds());
    drawing::Attrs attrs = drawing::softLabelAttrs();
    drawing::Attrs valueAttrs = drawing::softValueAttrs();
    drawing::Attrs titleAttrs = drawing::softTitleAttrs();
    drawing::drawAtPoint_withAttributes("s3g AMBI ENCODER WRANGLER",
                                        drawing::makePoint(18, 14), titleAttrs);
    drawing::drawEncoderPresetMenu(
        presetDisplayName(), drawing::encoderTitleBand(kGuiWidth, kGuiHeight),
        attrs, valueAttrs, style);
    drawing::drawHeaderActionButton(
        loadPresetButtonRect(), loadPresetButtonRect(), "LOAD", attrs, style);
    drawing::drawHeaderActionButton(
        savePresetButtonRect(), savePresetButtonRect(), "SAVE", attrs, style);
    drawing::drawHeaderActionButton(
        randomizeButtonRect(), randomizeButtonRect(), "RANDOM", attrs, style);
    drawing::drawRightStatus(drawing::peakDbText(_plugin->outputPeak.load(
                                 std::memory_order_relaxed)),
                             kGuiWidth, 14, valueAttrs, 18);
    drawField_valueAttrs_style(attrs, valueAttrs, style);
    drawPanels_valueAttrs_style(attrs, valueAttrs, style);
    drawOpenMenu_style(valueAttrs, style);
  }

  int hitVoice(drawing::Point point) {
    if (!drawing::contains(point, fieldRect()))
      return -1;
    const uint32_t voices = std::clamp<uint32_t>(
        _surfaceEdit ? _paramsSnapshot.voices
                     : _plugin->guiVoiceCount.load(std::memory_order_relaxed),
        1u, s3g::kAmbiWranglerMaxVoices);
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

  void setParam_fromPoint(clap_id param, drawing::Point point) {
    const auto *spec = guiSliderSpec(param);
    if (!spec)
      return;
    editValue(param, sliderValue(*spec, point));
    refreshControlSnapshot();
    setNeedsDisplay(true);
  }

  bool setLockedBreakpointValueFromPoint(drawing::Point point) {
    if (!_paramsSnapshot.voiceBreakpointsEnabled || _dragBreakpointRow < 0 ||
        _dragBreakpointVoice < 0 ||
        _dragBreakpointRow >=
            static_cast<int>(s3g::kAmbiWranglerCurveDimensionCount) ||
        _dragBreakpointVoice >= static_cast<int>(s3g::kAmbiWranglerMaxVoices)) {
      return false;
    }
    const drawing::Rect graph = curveGraphRect();
    const double effective = std::clamp(
        1.0 - (static_cast<double>(point.y) - graph.origin.y) /
                  std::max(1.0, static_cast<double>(graph.size.height)),
        0.0, 1.0);
    const auto dimension = static_cast<WranglerCurveDimension>(
        static_cast<uint32_t>(_dragBreakpointRow));
    if (!setControlBreakpointEffective(
            *_plugin, dimension, static_cast<uint32_t>(_dragBreakpointVoice),
            effective)) {
      return false;
    }
    refreshControlSnapshot();
    setNeedsDisplay(true);
    return true;
  }

  bool beginBreakpointDragAtPoint(drawing::Point point) {
    const drawing::Rect graph = curveGraphRect();
    if (!_paramsSnapshot.voiceBreakpointsEnabled ||
        !drawing::contains(point, graph)) {
      return false;
    }
    const double voicePosition =
        std::clamp((static_cast<double>(point.x) - graph.origin.x) /
                       std::max(1.0, static_cast<double>(graph.size.width)),
                   0.0, 1.0);
    const uint32_t slot = std::min<uint32_t>(
        15u, static_cast<uint32_t>(std::lround(voicePosition * 15.0)));
    _dragBreakpointRow = static_cast<int>(_selectedCurveDimension);
    _dragBreakpointVoice = static_cast<int>(_curveVoiceBank * 16u + slot);
    _selectedVoice = static_cast<uint32_t>(_dragBreakpointVoice);
    return setLockedBreakpointValueFromPoint(point);
  }

  void mouseDown(drawing::Event *event) {
    drawing::Point point =
        convertPoint_fromView((event)->locationInWindow(), nullptr);
    if (_openMenu > 0) {
      const int hit =
          drawing::dropdownHitIndex(point, _openMenuRect, 21.0, _menuItemCount);
      if (hit >= 0) {
        if (_openMenu == 1)
          editValue(kPresetParamId, hit);
        else if (_openMenu == 2)
          editValue(kRateModeAParamId, hit);
        else if (_openMenu == 3)
          editValue(kRateModeBParamId, hit);
        else if (_openMenu == 4)
          editValue(kTopologyShapeParamId, hit);
        else if (_openMenu == 5)
          editValue(kTopologyMotionParamId, hit);
        else if (_openMenu == 6)
          editValue(kMaskModeParamId, hit);
        else if (_openMenu == 7)
          editValue(kInputAParamId, hit);
        else if (_openMenu == 8)
          editValue(kInputBParamId, hit);
        else if (_openMenu == 9)
          editValue(kRungLoopParamId, hit);
        else if (_openMenu == 10)
          editValue(kOrderParamId, hit + 1);
        else if (_openMenu == 12)
          editValue(kPickupSetParamId, hit);
        else if (_openMenu == 13)
          editValue(kListenModeParamId, hit);
        else if (_openMenu == 14)
          editValue(kListenerResponseParamId, hit);
        else if (_openMenu == 15 && _selectedSurfaceCell >= 0) {
          setControlSurfaceCellPreset(
              *_plugin, static_cast<uint32_t>(_selectedSurfaceCell),
              static_cast<uint32_t>(hit));
          refreshControlSnapshot();
        } else if (_openMenu == 16) {
          setControlSurfaceCurve(*_plugin,
                                 static_cast<s3g::ParameterSurfaceCurve>(
                                     static_cast<uint32_t>(hit)));
          refreshControlSnapshot();
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
      randomizeSafe(*_plugin);
      rescanValues();
      refreshControlSnapshot();
      if (_plugin->host && _plugin->host->request_process) {
        _plugin->host->request_process(_plugin->host);
      }
      _selectedVoice = 0;
      setNeedsDisplay(true);
      return;
    }
    if (drawing::contains(point, circuitLawButtonRect())) {
      editValue(kCircuitLawParamId, _paramsSnapshot.circuitLaw ==
                                            s3g::AmbiWranglerCircuitLaw::Legacy
                                        ? 1.0
                                        : 0.0);
      setNeedsDisplay(true);
      return;
    }
    constexpr int controlMenus[]{10, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16};
    for (const int menu : controlMenus) {
      if (drawing::contains(point, menuBoxRect(menu))) {
        openMenu(menu);
        return;
      }
    }
    const drawing::Rect panel = fieldPanelRect();
    if (drawing::contains(point, panel)) {
      for (int i = 0; i < 4; ++i) {
        if (drawing::contains(point, pageButtonRect(i))) {
          if (_surfacePopupChild)
            return;
          _fieldPage = i;
          if (_fieldPage == 1) {
            _curveVoiceBank = std::min<uint32_t>(_selectedVoice / 16u, 3u);
          }
          setNeedsDisplay(true);
          return;
        }
      }
      if (_fieldPage == 3) {
        if (drawing::contains(point, surfacePopRect())) {
          if (_surfacePopupChild) {
            if (_surfacePopupOwner)
              _surfacePopupOwner->hideSurfacePopup();
          } else {
            openSurfacePopup();
          }
          return;
        }
        if (drawing::contains(point, surfaceEditRect())) {
          syncSurfaceEditMode(!_surfaceEdit);
          setNeedsDisplay(true);
          return;
        }
        if (drawing::contains(point, surfaceEnableRect())) {
          if (_surfaceSnapshot.cellCount < 2u) {
            beep();
          } else {
            setControlSurfaceEnabled(*_plugin, _surfaceSnapshot.enabled == 0u);
            refreshControlSnapshot();
          }
          setNeedsDisplay(true);
          return;
        }
        if (drawing::contains(point, surfaceAddRect())) {
          if (addControlSurfaceCell(*_plugin)) {
            refreshControlSnapshot();
            _selectedSurfaceCell =
                static_cast<int>(_surfaceSnapshot.cellCount) - 1;
          } else {
            beep();
          }
          setNeedsDisplay(true);
          return;
        }
        if (drawing::contains(point, surfaceDeleteRect())) {
          if (_selectedSurfaceCell < 0 ||
              !removeControlSurfaceCell(
                  *_plugin, static_cast<uint32_t>(_selectedSurfaceCell))) {
            beep();
          }
          refreshControlSnapshot();
          _selectedSurfaceCell =
              _surfaceSnapshot.cellCount == 0u
                  ? -1
                  : std::min(_selectedSurfaceCell,
                             static_cast<int>(_surfaceSnapshot.cellCount) - 1);
          setNeedsDisplay(true);
          return;
        }
        if (drawing::contains(point, surfaceCaptureRect())) {
          if (_selectedSurfaceCell < 0 ||
              !captureControlSurfaceCell(
                  *_plugin, static_cast<uint32_t>(_selectedSurfaceCell))) {
            beep();
          }
          refreshControlSnapshot();
          setNeedsDisplay(true);
          return;
        }
        if (drawing::contains(point, surfaceFocusMinusRect()) ||
            drawing::contains(point, surfaceFocusPlusRect())) {
          const float scale =
              drawing::contains(point, surfaceFocusMinusRect()) ? 0.8f : 1.25f;
          setControlSurfaceFocus(*_plugin, _surfaceSnapshot.focus * scale);
          refreshControlSnapshot();
          setNeedsDisplay(true);
          return;
        }
        if (drawing::contains(point, surfaceGlideMinusRect()) ||
            drawing::contains(point, surfaceGlidePlusRect())) {
          const int direction =
              drawing::contains(point, surfaceGlideMinusRect()) ? -1 : 1;
          setControlSurfaceGlide(*_plugin,
                                 s3g::parameterSurfaceSteppedGlide(
                                     _surfaceSnapshot.glideMs, direction));
          refreshControlSnapshot();
          setNeedsDisplay(true);
          return;
        }
        if (drawing::contains(point, surfacePlotRect())) {
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
          setNeedsDisplay(true);
          return;
        }
        return;
      }
      if (_fieldPage == 1) {
        for (int bank = 0; bank < 2; ++bank) {
          if (drawing::contains(point, curveBankButtonRect(bank))) {
            _curveBank = bank;
            if (_curveBank == 0 &&
                _selectedCurveDimension >=
                    s3g::kAmbiWranglerHistoricalCurveDimensionCount) {
              _selectedCurveDimension = 0u;
            } else if (_curveBank == 1 &&
                       _selectedCurveDimension <
                           s3g::kAmbiWranglerHistoricalCurveDimensionCount) {
              _selectedCurveDimension =
                  s3g::kAmbiWranglerHistoricalCurveDimensionCount;
            }
            setNeedsDisplay(true);
            return;
          }
        }
        if (drawing::contains(point, curvesToggleRect())) {
          toggleControlBreakpoints(*_plugin);
          refreshControlSnapshot();
          setNeedsDisplay(true);
          return;
        }
        const uint32_t firstDimension =
            _curveBank == 0 ? 0u
                            : s3g::kAmbiWranglerHistoricalCurveDimensionCount;
        const uint32_t dimensionCount =
            _curveBank == 0
                ? s3g::kAmbiWranglerHistoricalCurveDimensionCount
                : s3g::kAmbiWranglerCurveDimensionCount -
                      s3g::kAmbiWranglerHistoricalCurveDimensionCount;
        for (uint32_t local = 0u; local < dimensionCount; ++local) {
          if (drawing::contains(point, curveParameterButtonRect(local))) {
            // Select/arm only. Parameter labels never write values
            // and never enable curves.
            _selectedCurveDimension = firstDimension + local;
            _dragBreakpointRow = -1;
            _dragBreakpointVoice = -1;
            setNeedsDisplay(true);
            return;
          }
        }
        for (uint32_t bank = 0u; bank < 4u; ++bank) {
          if (drawing::contains(point, curveVoiceBankButtonRect(bank))) {
            _curveVoiceBank = bank;
            _selectedVoice = bank * 16u;
            setNeedsDisplay(true);
            return;
          }
        }
        beginBreakpointDragAtPoint(point);
        return;
      }
      if (_fieldPage == 2) {
        if (drawing::contains(point, listenVoiceRect())) {
          const uint32_t voiceCount = std::clamp<uint32_t>(
              _paramsSnapshot.voices, 1u, s3g::kAmbiWranglerMaxVoices);
          _selectedVoice = point.x < drawing::midX(listenVoiceRect())
                               ? (_selectedVoice + voiceCount - 1u) % voiceCount
                               : (_selectedVoice + 1u) % voiceCount;
          setNeedsDisplay(true);
          return;
        }
        if (drawing::contains(point, pickupSetRect())) {
          openMenu(12);
          return;
        }
        if (drawing::contains(point, listenModeRect())) {
          openMenu(13);
          return;
        }
        if (drawing::contains(point, listenerResponseRect())) {
          openMenu(14);
          return;
        }
        if (drawing::contains(point, listenEnableRect())) {
          editValue(kListeningEnabledParamId,
                    _paramsSnapshot.listeningEnabled ? 0.0 : 1.0);
          setNeedsDisplay(true);
          return;
        }
        if (drawing::contains(point, returnBypassRect())) {
          if (_paramsSnapshot.listenerResponse ==
              s3g::AmbiWranglerListenerResponse::Write) {
            editValue(kReturnBypassParamId,
                      _paramsSnapshot.returnBypass ? 0.0 : 1.0);
          }
          setNeedsDisplay(true);
          return;
        }
      } else {
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
    }
    _dragParam = 0;
    for (const auto &spec : kGuiSliders) {
      if (isListenerGuiParam(spec.id) && _fieldPage != 2)
        continue;
      const bool settleResponse = _paramsSnapshot.listenerResponse ==
                                  s3g::AmbiWranglerListenerResponse::Settle;
      const bool settleControl = spec.id == kSettleAmountParamId ||
                                 spec.id == kSettleTargetParamId ||
                                 spec.id == kSettleRecoveryParamId;
      const bool writeControl =
          spec.id == kFieldWriteParamId || spec.id == kRegisterMotionParamId ||
          spec.id == kFieldReturnParamId || spec.id == kPropagationParamId;
      if ((settleControl && !settleResponse) ||
          (writeControl && settleResponse))
        continue;
      if (drawing::contains(
              point, drawing::makeRect(spec.panelX + 8, spec.y - 8, 230, 24))) {
        double defaultValue = 0.0;
        if (drawing::sliderDoubleClickDefault(event, &_plugin->plugin, spec.id,
                                              &defaultValue)) {
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
    if (_dragSurfaceCell >= 0) {
      updateSurfacePosition_cursor(point, false);
      return;
    }
    if (_dragSurfaceCursor) {
      updateSurfacePosition_cursor(point, true);
      return;
    }
    if (_fieldPage == 1 && _dragBreakpointRow >= 0 &&
        _dragBreakpointVoice >= 0) {
      // Both the armed dimension and the voice were locked on mouseDown;
      // horizontal drag position is intentionally ignored.
      setLockedBreakpointValueFromPoint(point);
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
    _dragBreakpointVoice = -1;
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
} // namespace circuit_encoder_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new circuit_encoder_canvas::Editor(p);
}
#include "s3g_clap_canvas_gui.inc"
