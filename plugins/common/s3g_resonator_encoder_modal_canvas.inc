// Literal Cocoa adaptation: retain geometry, graphics, controls and hit maps.
namespace resonator_encoder_canvas {
namespace drawing = s3g::portable_gui::resonator_drawing;
namespace foundation = s3g::portable_gui::foundation;
using Kind = s3g::clap_gui::ParamEventKind;
constexpr s3g::gui_layout::Canvas kGuiCanvas{static_cast<double>(kGuiWidth),
                                             static_cast<double>(kGuiHeight)};
constexpr auto kTitleBand = s3g::gui_layout::encoderTitleBand(kGuiCanvas);

constexpr std::array<clap_id, 4u> kOutputControls{{
    kParamModalLift,
    kParamOutputGain,
    kParamOrder,
    kParamOutputMode,
}};
constexpr std::array<clap_id, 7u> kStructureControls{{
    kParamBodyCount,
    kParamSubstrate,
    kParamSize,
    kParamDamping,
    kParamIrregularity,
    kParamCoupling,
    kParamPropagationLoss,
}};
constexpr std::array<clap_id, 3u> kProjectionControls{{
    kParamFieldAzimuth,
    kParamFieldElevation,
    kParamSpatialExtent,
}};
constexpr std::array<clap_id, 5u> kRadiationControls{{
    kParamPickupPosition,
    kParamArraySpread,
    kParamPickupAxis,
    kParamContactRadiation,
    kParamAirRadiation,
}};
constexpr std::array<clap_id, 5u> kActuatorControls{{
    kParamListenerPickupSet,
    kParamFieldListenMode,
    kParamFieldListenAmount,
    kParamFieldListenResponse,
    kParamExternalDrive,
}};
constexpr std::array<s3g::gui_layout::EncoderFamilyControl, 4u>
    kActuatorFamilyControls{{
        s3g::gui_layout::EncoderFamilyControl::PickupSet,
        s3g::gui_layout::EncoderFamilyControl::ListeningMode,
        s3g::gui_layout::EncoderFamilyControl::ListenerAmountReturn,
        s3g::gui_layout::EncoderFamilyControl::ListenerResponseBypass,
    }};

std::array<clap_id, 3u> bodyPointControls(uint32_t body) {
  return {{
      bodyAedParamId(body, BodyAedParamKind::AzimuthOffset),
      bodyAedParamId(body, BodyAedParamKind::ElevationOffset),
      bodyAedParamId(body, BodyAedParamKind::Distance),
  }};
}

constexpr s3g::gui_layout::Rect kFieldPanelFrame{18.0, 42.0, 596.0, 706.0};
constexpr auto kOutputPanelLayout = s3g::gui_layout::fittedPanel(
    s3g::gui_layout::PluginClass::ProceduralEncoder,
    s3g::gui_layout::PanelRole::Output,
    s3g::gui_layout::kLargeEncoderFirstColumn,
    s3g::gui_layout::kStandardMetrics.contentTop,
    static_cast<uint32_t>(kOutputControls.size()));
constexpr auto kStructurePanelLayout = s3g::gui_layout::fittedStackPanel(
    s3g::gui_layout::PanelRole::Engine, kOutputPanelLayout,
    static_cast<uint32_t>(kStructureControls.size()));
constexpr auto kActuatorPanelLayout = s3g::gui_layout::fittedStackPanel(
    s3g::gui_layout::PanelRole::Listener, kStructurePanelLayout,
    static_cast<uint32_t>(kActuatorControls.size()));
constexpr auto kProjectionPanelLayout = s3g::gui_layout::fittedPanel(
    s3g::gui_layout::PluginClass::ProceduralEncoder,
    s3g::gui_layout::PanelRole::Projection,
    s3g::gui_layout::kLargeEncoderSecondColumn,
    s3g::gui_layout::kStandardMetrics.contentTop,
    static_cast<uint32_t>(kProjectionControls.size()));
constexpr auto kRadiationPanelLayout = s3g::gui_layout::fittedStackPanel(
    s3g::gui_layout::PanelRole::ToneShape, kProjectionPanelLayout,
    static_cast<uint32_t>(kRadiationControls.size()));
constexpr auto kBodyPointPanelLayout =
    s3g::gui_layout::stackPanel(s3g::gui_layout::PanelRole::SelectedObject,
                                kRadiationPanelLayout, 152.0, 3u);
constexpr auto kSignalPanelLayout = s3g::gui_layout::stackPanel(
    s3g::gui_layout::PanelRole::Utility, kBodyPointPanelLayout, 166.0, 0u);

constexpr std::array<s3g::gui_layout::Panel, 3u> kFirstColumnLayouts{{
    kOutputPanelLayout,
    kStructurePanelLayout,
    kActuatorPanelLayout,
}};
constexpr std::array<s3g::gui_layout::Panel, 4u> kSecondColumnLayouts{{
    kProjectionPanelLayout,
    kRadiationPanelLayout,
    kBodyPointPanelLayout,
    kSignalPanelLayout,
}};
static_assert(s3g::gui_layout::validateColumn(kFirstColumnLayouts, kGuiCanvas));
static_assert(s3g::gui_layout::rolesFollowTemplate(
    kFirstColumnLayouts, s3g::gui_layout::kProceduralEncoderTemplate, true));
static_assert(s3g::gui_layout::controlsFollowFamilyOrder(
    kActuatorFamilyControls, s3g::gui_layout::kListenerControlOrder));
static_assert(s3g::gui_layout::controlMatchesSlot(
    kOutputPanelLayout, s3g::gui_layout::kLargeEncoderOrderSlot));
static_assert(s3g::gui_layout::sourceCardinalityControlMatches(
    kStructurePanelLayout,
    s3g::gui_layout::SharedControlRole::SourceCardinality));
static_assert(kStructureControls[s3g::gui_layout::sourceCardinalityRow(
                  s3g::gui_layout::SharedControlRole::SourceCardinality)] ==
              kParamBodyCount);
static_assert(s3g::gui_layout::validateColumn(kSecondColumnLayouts, kGuiCanvas,
                                              false));
static_assert(s3g::gui_layout::rectFitsCanvas(kFieldPanelFrame, kGuiCanvas));
static_assert(s3g::gui_layout::kLargeEncoderFirstColumn.x -
                  (kFieldPanelFrame.x + kFieldPanelFrame.width) ==
              16.0);
static_assert(kFieldPanelFrame.y + kFieldPanelFrame.height + 12.0 ==
              kGuiCanvas.height);

drawing::Rect fieldPanelRect() { return drawing::cocoaRect(kFieldPanelFrame); }
drawing::Rect fieldPlotRect() {
  const drawing::Rect panel = fieldPanelRect();
  return drawing::makeRect(panel.origin.x + 16.0, panel.origin.y + 34.0,
                           panel.size.width - 32.0, panel.size.height - 50.0);
}
drawing::Rect outputPanelRect() {
  return drawing::cocoaRect(kOutputPanelLayout.frame);
}
drawing::Rect actuatorPanelRect() {
  return drawing::cocoaRect(kActuatorPanelLayout.frame);
}
drawing::Rect structurePanelRect() {
  return drawing::cocoaRect(kStructurePanelLayout.frame);
}
drawing::Rect projectionPanelRect() {
  return drawing::cocoaRect(kProjectionPanelLayout.frame);
}
drawing::Rect radiationPanelRect() {
  return drawing::cocoaRect(kRadiationPanelLayout.frame);
}
drawing::Rect bodyPointPanelRect() {
  return drawing::cocoaRect(kBodyPointPanelLayout.frame);
}
drawing::Rect signalPanelRect() {
  return drawing::cocoaRect(kSignalPanelLayout.frame);
}
drawing::Rect skinPadRect() {
  const drawing::Rect panel = signalPanelRect();
  const double size = std::min<double>(112.0, panel.size.height - 48.0);
  return drawing::makeRect(drawing::maxX(panel) - size - 16.0,
                           panel.origin.y + 33.0, size, size);
}
drawing::Rect modalZoomButtonRect(uint32_t index) {
  const drawing::Rect panel = fieldPanelRect();
  const double cameraLeft =
      drawing::topologyProcessorCameraButtonRect(panel, 0u).origin.x;
  constexpr double width = 22.0;
  constexpr double gap = 6.0;
  constexpr double groupWidth = width * 2.0 + gap;
  return drawing::makeRect(cameraLeft - gap - groupWidth +
                               static_cast<double>(index) * (width + gap),
                           panel.origin.y + 3.0, width, 15.0);
}
drawing::Rect modalResetLayoutButtonRect() {
  const drawing::Rect zoomOut = modalZoomButtonRect(0u);
  return drawing::makeRect(zoomOut.origin.x - 60.0,
                           fieldPanelRect().origin.y + 3.0, 52.0, 15.0);
}

drawing::Rect panelForParam(clap_id id) {
  uint32_t body = 0u;
  bool xCoordinate = false;
  if (decodeBodySkinParam(id, body, xCoordinate))
    return signalPanelRect();
  BodyAedParamKind kind = BodyAedParamKind::AzimuthOffset;
  if (decodeBodyAedParam(id, body, kind))
    return bodyPointPanelRect();
  switch (id) {
  case kParamOrder:
  case kParamOutputMode:
  case kParamOutputGain:
  case kParamModalLift:
    return outputPanelRect();
  case kParamFieldAzimuth:
  case kParamFieldElevation:
  case kParamSpatialExtent:
    return projectionPanelRect();
  case kParamFieldListenMode:
  case kParamFieldListenAmount:
  case kParamFieldListenResponse:
  case kParamExternalDrive:
  case kParamListenerPickupSet:
    return actuatorPanelRect();
  case kParamSubstrate:
  case kParamBodyCount:
  case kParamSize:
  case kParamDamping:
  case kParamIrregularity:
  case kParamCoupling:
  case kParamPropagationLoss:
    return structurePanelRect();
  case kParamPickupPosition:
  case kParamArraySpread:
  case kParamPickupAxis:
  case kParamContactRadiation:
  case kParamAirRadiation:
    return radiationPanelRect();
  default:
    return outputPanelRect();
  }
}

double controlRowY(drawing::Rect panel, uint32_t row) {
  return static_cast<double>(s3g::gui_layout::toolboxRowY(panel.origin.y, row));
}

const char *shortParamName(clap_id id) {
  uint32_t body = 0u;
  bool xCoordinate = false;
  if (decodeBodySkinParam(id, body, xCoordinate)) {
    return xCoordinate ? "SKIN X" : "SKIN Y";
  }
  BodyAedParamKind kind = BodyAedParamKind::AzimuthOffset;
  if (decodeBodyAedParam(id, body, kind)) {
    if (kind == BodyAedParamKind::AzimuthOffset)
      return "AZ OFFSET";
    if (kind == BodyAedParamKind::ElevationOffset)
      return "EL OFFSET";
    return "DISTANCE";
  }
  switch (id) {
  case kParamExcitation:
    return "EXCITER";
  case kParamEventRate:
    return "RATE";
  case kParamActivity:
    return "ACTIVE";
  case kParamForce:
    return "FORCE";
  case kParamTexture:
    return "TEXTURE";
  case kParamAmbientDrive:
    return "AMBIENT";
  case kParamExternalDrive:
    return "INPUT";
  case kParamSubstrate:
    return "BODY";
  case kParamBodyCount:
    return "BODIES";
  case kParamSize:
    return "SIZE";
  case kParamDamping:
    return "DAMP";
  case kParamIrregularity:
    return "IRREG";
  case kParamCoupling:
    return "COUPLING";
  case kParamEnergy:
    return "ENERGY";
  case kParamPropagationLoss:
    return "SKIN EXTENT";
  case kParamReadout:
    return "READOUT";
  case kParamPickupPosition:
    return "TONE CENTER";
  case kParamArraySpread:
    return "VARIATION";
  case kParamPickupAxis:
    return "MODE ANGLE";
  case kParamSensorMass:
    return "MASS";
  case kParamMountStiffness:
    return "MOUNT";
  case kParamConditionerHighpass:
    return "HPF";
  case kParamSensorNoise:
    return "NOISE";
  case kParamAirRadiation:
    return "AIR LEVEL";
  case kParamOutputMode:
    return "FORMAT";
  case kParamOrder:
    return "ORDER";
  case kParamContactRadiation:
    return "CONTACT / RAD";
  case kParamSpatialExtent:
    return "BODY SPREAD";
  case kParamFieldAzimuth:
    return "AZIMUTH";
  case kParamFieldElevation:
    return "ELEVATION";
  case kParamOutputGain:
    return "OUT";
  case kParamModalLift:
    return "LIFT";
  case kParamFieldListenMode:
    return "ROUTING";
  case kParamFieldListenAmount:
    return "DRIVE";
  case kParamFieldListenResponse:
    return "BEHAVIOR";
  case kParamListenerPickupSet:
    return "EARS";
  default:
    return "PARAM";
  }
}

double normalizedParam(const ParamSpec &spec, double value) {
  value = std::clamp(value, spec.minimum, spec.maximum);
  uint32_t body = 0u;
  BodyAedParamKind kind = BodyAedParamKind::AzimuthOffset;
  if (spec.id == kParamFieldAzimuth ||
      (decodeBodyAedParam(spec.id, body, kind) &&
       kind == BodyAedParamKind::AzimuthOffset)) {
    return s3g::aedAzimuthSliderNorm(static_cast<float>(value));
  }
  if (spec.logarithmic && spec.minimum > 0.0) {
    return std::log(value / spec.minimum) /
           std::log(spec.maximum / spec.minimum);
  }
  return (value - spec.minimum) /
         std::max(1.0e-12, spec.maximum - spec.minimum);
}

double valueFromNormalized(const ParamSpec &spec, double normalized) {
  normalized = std::clamp(normalized, 0.0, 1.0);
  uint32_t body = 0u;
  BodyAedParamKind kind = BodyAedParamKind::AzimuthOffset;
  if (spec.id == kParamFieldAzimuth ||
      (decodeBodyAedParam(spec.id, body, kind) &&
       kind == BodyAedParamKind::AzimuthOffset)) {
    return s3g::aedAzimuthFromSliderNorm(static_cast<float>(normalized));
  }
  if (spec.logarithmic && spec.minimum > 0.0) {
    return spec.minimum * std::pow(spec.maximum / spec.minimum, normalized);
  }
  return spec.minimum + (spec.maximum - spec.minimum) * normalized;
}

struct GuiControlLocation {
  clap_id id = CLAP_INVALID_ID;
  drawing::Rect panel{};
  uint32_t row = 0u;
};

template <size_t Count>
bool findInGroup(drawing::Point point, const s3g::gui_layout::Panel &layout,
                 const std::array<clap_id, Count> &controls,
                 GuiControlLocation &result) {
  const drawing::Rect panel = drawing::cocoaRect(layout.frame);
  for (uint32_t row = 0u; row < Count; ++row) {
    const drawing::Rect hit =
        drawing::cocoaRect(s3g::gui_layout::sliderHitRect(layout, row));
    if (drawing::contains(point, hit)) {
      result = {controls[row], panel, row};
      return true;
    }
  }
  return false;
}

drawing::Rect menuAnchorRect(const GuiControlLocation &location) {
  return drawing::makeRect(
      s3g::gui_layout::processorControlX(location.panel.origin.x),
      controlRowY(location.panel, location.row) - 1.0,
      s3g::gui_layout::processorMenuWidth(location.panel.size.width), 15.0);
}

float guiRandomSigned() {
  return static_cast<float>(drawing::randomBits() & 0x00ffffffu) *
             (2.0f / 16777215.0f) -
         1.0f;
}

void randomizeSafe(Plugin &plugin) {
  const auto snapshot = controlStateSnapshot(plugin);
  const uint32_t order = snapshot.params.ambisonicOrder;
  const float outputGain = snapshot.params.outputGainDb;
  const float modalLift = snapshot.params.modalLift;
  const auto listenerPickupSet = snapshot.params.listenerPickupSet;
  const auto listenerMode = snapshot.params.fieldListenMode;
  const float listenerAmount = snapshot.params.fieldListenAmount;
  const auto listenerResponse = snapshot.params.fieldListenResponse;
  const float externalDrive = snapshot.params.externalDrive;
  auto params = s3g::accelerometerFieldFactoryPreset(
      drawing::randomBounded(kFactoryPresetCount));
  const auto vary = [](float value, float amount) {
    return s3g::clamp(value + guiRandomSigned() * amount, 0.0f, 1.0f);
  };
  params.size = vary(params.size, 0.08f);
  params.damping = vary(params.damping, 0.08f);
  params.irregularity = vary(params.irregularity, 0.07f);
  params.coupling = vary(params.coupling, 0.12f);
  for (uint32_t body = 0u; body < s3g::kAccelerometerFieldMaxBodyCount;
       ++body) {
    params.bodySkinX[body] = vary(params.bodySkinX[body], 0.14f);
    params.bodySkinY[body] = vary(params.bodySkinY[body], 0.14f);
  }
  params.sourcePosition = params.bodySkinX[0u];
  params.contactDetail = params.bodySkinY[0u];
  params.propagationLoss = vary(params.propagationLoss, 0.10f);
  params.pickupPosition = vary(params.pickupPosition, 0.10f);
  params.arraySpread = s3g::clamp(vary(params.arraySpread, 0.10f), 0.42f, 1.0f);
  params.spatialExtent =
      s3g::clamp(vary(params.spatialExtent, 0.10f), 0.36f, 1.0f);
  params.ambisonicOrder = order;
  params.outputGainDb = outputGain;
  params.modalLift = modalLift;
  params.outputMode = s3g::AccelerometerFieldOutputMode::Ambisonic;
  params.listenerPickupSet = listenerPickupSet;
  params.fieldListenMode = listenerMode;
  params.fieldListenAmount = listenerAmount;
  params.fieldListenResponse = listenerResponse;
  params.externalDrive = externalDrive;
  params.seed ^= drawing::randomBits();
  replaceControlState(plugin, params, kCustomPresetIndex, true);
}

struct GuiBodyAed {
  float azimuthDeg = 0.0f;
  float elevationDeg = 0.0f;
  float distance = 1.0f;
};

s3g::Vec3 guiBodyDirection(const Plugin &plugin, uint32_t body) {
  return s3g::normalize({
      plugin.bodyDirectionX[body].load(std::memory_order_relaxed),
      plugin.bodyDirectionY[body].load(std::memory_order_relaxed),
      plugin.bodyDirectionZ[body].load(std::memory_order_relaxed),
  });
}

GuiBodyAed guiBodyAed(const Plugin &plugin, uint32_t body) {
  const s3g::Vec3 direction = guiBodyDirection(plugin, body);
  return {
      std::atan2(direction.y, direction.x) * 180.0f / s3g::kPi,
      std::asin(s3g::clamp(direction.z, -1.0f, 1.0f)) * 180.0f / s3g::kPi,
      plugin.bodyDistance[body].load(std::memory_order_relaxed),
  };
}

float wrapGuiDegrees(float value) {
  value = std::fmod(value + 180.0f, 360.0f);
  if (value < 0.0f)
    value += 360.0f;
  return value - 180.0f;
}

float modalLinearToSrgb(float value) {
  const float x = std::clamp(value, 0.0f, 1.0f);
  return x <= 0.0031308f ? x * 12.92f
                         : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}

VSTGUI::CColor modalBodyColorFromAed(float azimuthDeg, float elevationDeg,
                                     float distance, bool selected,
                                     double alpha = 1.0) {
  const float hue = std::fmod(azimuthDeg / 360.0f + 1.0f, 1.0f);
  const float light = std::clamp(
      (std::clamp(elevationDeg, -90.0f, 90.0f) + 90.0f) / 180.0f, 0.28f, 0.88f);
  const float chroma = std::clamp(distance / 2.4f, 0.08f, 1.0f) * 0.37f;
  const float a = std::cos(hue * 2.0f * static_cast<float>(M_PI)) * chroma;
  const float b = std::sin(hue * 2.0f * static_cast<float>(M_PI)) * chroma;
  const float l3 = light + 0.3963377774f * a + 0.2158037573f * b;
  const float m3 = light - 0.1055613458f * a - 0.0638541728f * b;
  const float s3 = light - 0.0894841775f * a - 1.2914855480f * b;
  const float l = l3 * l3 * l3;
  const float m = m3 * m3 * m3;
  const float s = s3 * s3 * s3;
  float red = modalLinearToSrgb(4.0767416621f * l - 3.3077115913f * m +
                                0.2309699292f * s);
  float green = modalLinearToSrgb(-1.2684380046f * l + 2.6097574011f * m -
                                  0.3413193965f * s);
  float blue = modalLinearToSrgb(-0.0041960863f * l - 0.7034186147f * m +
                                 1.7076147010f * s);
  const float grayMix = selected ? 0.08f : 0.18f;
  red = red * (1.0f - grayMix) + 0.74f * grayMix;
  green = green * (1.0f - grayMix) + 0.74f * grayMix;
  blue = blue * (1.0f - grayMix) + 0.74f * grayMix;
  return drawing::calibratedRGB(red, green, blue, std::clamp(alpha, 0.0, 1.0));
}

class Editor final : public s3g::portable_gui::routing::Canvas {
public:
  Plugin *_plugin;
  clap_id _dragParam;
  clap_id _openMenu;
  GuiControlLocation _openMenuLocation;

  int _viewMode;
  double _viewAzimuthDeg;
  double _viewElevationDeg;
  double _viewZoom;
  bool _dragView;
  int _dragBody;
  bool _dragSkin;
  drawing::Point _lastDragPoint;
  uint32_t _selectedBody;
  double _actuatorFlowPhase;
#include "s3g_resonator_encoder_editor.inc"
  Editor(Plugin &instance) : Canvas(kGuiWidth, kGuiHeight) {
    auto *plugin = &instance;

    _plugin = plugin;
    _dragParam = CLAP_INVALID_ID;
    _openMenu = CLAP_INVALID_ID;
    _openMenuLocation = {};
    auto *p = static_cast<Plugin *>(plugin);
    _viewMode = p ? p->guiState.viewMode : 0;
    _viewAzimuthDeg = p ? p->guiState.viewAzimuthDeg : -35.0;
    _viewElevationDeg = p ? p->guiState.viewElevationDeg : 34.0;
    _viewZoom = p ? p->guiState.viewZoom : 1.0;
    if (_viewMode == 0) {
      _viewAzimuthDeg = 90.0;
      _viewElevationDeg = 0.0;
    } else if (_viewMode == 1) {
      _viewAzimuthDeg = 90.0;
      _viewElevationDeg = 90.0;
    } else if (_viewMode == 2) {
      _viewAzimuthDeg = -35.0;
      _viewElevationDeg = 34.0;
    }
    if (p && _viewMode >= 0 && _viewMode <= 2) {
      p->guiState.viewAzimuthDeg = static_cast<float>(_viewAzimuthDeg);
      p->guiState.viewElevationDeg = static_cast<float>(_viewElevationDeg);
    }
    _selectedBody = p ? p->guiState.selectedBody : 0u;
    _dragView = false;
    _dragBody = -1;
    _dragSkin = false;
    _lastDragPoint = drawing::Point{};
    _actuatorFlowPhase = 0.0;
  }
  ~Editor() override { stopRefresh(); }
  void setParam_value(clap_id param, double value) {
    editValue(param, value);
    setNeedsDisplay(true);
  }

  void storeViewState() {
    auto *p = static_cast<Plugin *>(_plugin);
    if (!p)
      return;
    p->guiState.viewMode = _viewMode;
    p->guiState.viewAzimuthDeg = static_cast<float>(_viewAzimuthDeg);
    p->guiState.viewElevationDeg = static_cast<float>(_viewElevationDeg);
    p->guiState.viewZoom = static_cast<float>(_viewZoom);
    p->guiState.selectedBody = _selectedBody;
  }

  void setViewPreset(int mode) {
    _viewMode = mode;
    if (mode == 0) {
      _viewAzimuthDeg = 90.0;
      _viewElevationDeg = 0.0;
    } else if (mode == 1) {
      _viewAzimuthDeg = 90.0;
      _viewElevationDeg = 90.0;
    } else {
      _viewAzimuthDeg = -35.0;
      _viewElevationDeg = 34.0;
    }
    storeViewState();
    setNeedsDisplay(true);
  }

  double viewScaleForRect(drawing::Rect rect) {
    return std::min(rect.size.width, rect.size.height) * 0.34 *
           std::clamp<double>(_viewZoom, 0.55, 2.20);
  }

  drawing::Point projectWorldPoint_rect_depth(s3g::Vec3 point,
                                              drawing::Rect rect,
                                              double *depth) {
    const double centerX = drawing::midX(rect);
    const double centerY = rect.origin.y + rect.size.height * 0.54;
    const double scale = viewScaleForRect(rect);
    if (_viewMode == 0) {
      if (depth)
        *depth = static_cast<double>(point.z);
      return drawing::makePoint(centerX - static_cast<double>(point.y) * scale,
                                centerY - static_cast<double>(point.x) * scale);
    }
    if (_viewMode == 1) {
      if (depth)
        *depth = static_cast<double>(point.x);
      return drawing::makePoint(centerX - static_cast<double>(point.y) * scale,
                                centerY - static_cast<double>(point.z) * scale);
    }
    const float azimuth = static_cast<float>(_viewAzimuthDeg * M_PI / 180.0);
    const float elevation =
        static_cast<float>(_viewElevationDeg * M_PI / 180.0);
    const float ca = std::cos(azimuth);
    const float sa = std::sin(azimuth);
    const float ce = std::cos(elevation);
    const float se = std::sin(elevation);
    const float x1 = ca * point.x - sa * point.y;
    const float y1 = sa * point.x + ca * point.y;
    const float y2 = ce * y1 + se * point.z;
    const float z2 = -se * y1 + ce * point.z;
    if (depth)
      *depth = static_cast<double>(z2);
    return drawing::makePoint(centerX + static_cast<double>(x1) * scale,
                              centerY - static_cast<double>(y2) * scale);
  }

  drawing::Point projectWorldPointX_y_z(double x, double y, double z) {
    return projectWorldPoint_rect_depth(
        {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)},
        fieldPlotRect(), nullptr);
  }

  int hitBodyAtPoint_inRect(drawing::Point point, drawing::Rect rect) {
    auto *p = static_cast<Plugin *>(_plugin);
    if (!p)
      return -1;
    const auto snapshot = controlStateSnapshot(*p);
    const uint32_t count =
        std::clamp<uint32_t>(snapshot.params.bodyCount, 4u, 8u);
    const double drawnRadius = 15.0 + (8.0 - static_cast<double>(count)) * 0.8;
    const double hitRadius = drawnRadius + 4.0;
    int hit = -1;
    double best = hitRadius * hitRadius;
    double bestDepth = -std::numeric_limits<double>::infinity();
    for (uint32_t body = 0u; body < count; ++body) {
      const s3g::Vec3 direction = guiBodyDirection(*p, body);
      const float distance =
          p->bodyDistance[body].load(std::memory_order_relaxed);
      const s3g::Vec3 world{
          direction.x * distance,
          direction.y * distance,
          direction.z * distance,
      };
      double depth = 0.0;
      const drawing::Point projected =
          projectWorldPoint_rect_depth(world, rect, &depth);
      const double dx = point.x - projected.x;
      const double dy = point.y - projected.y;
      const double squared = dx * dx + dy * dy;
      if (squared < best - 0.5 ||
          (std::fabs(squared - best) <= 0.5 && depth > bestDepth)) {
        best = squared;
        bestDepth = depth;
        hit = static_cast<int>(body);
      }
    }
    return hit;
  }

  void updateDraggedBodyAtPoint_inRect(drawing::Point point,
                                       drawing::Rect rect) {
    if (_dragBody < 0 ||
        _dragBody >= static_cast<int>(s3g::kAccelerometerFieldMaxBodyCount) ||
        (_viewMode != 0 && _viewMode != 1)) {
      return;
    }
    auto *p = static_cast<Plugin *>(_plugin);
    const auto snapshot = controlStateSnapshot(*p);
    const uint32_t body = static_cast<uint32_t>(_dragBody);
    const GuiBodyAed current = guiBodyAed(*p, body);
    const double centerX = drawing::midX(rect);
    const double centerY = rect.origin.y + rect.size.height * 0.54;
    const double scale = viewScaleForRect(rect);
    if (scale <= 1.0)
      return;

    float desiredAzimuth = current.azimuthDeg;
    float desiredElevation = current.elevationDeg;
    float desiredDistance = current.distance;
    if (_viewMode == 0) {
      const float x = static_cast<float>(
          std::clamp((centerY - point.y) / scale, -2.0, 2.0));
      const float y = static_cast<float>(
          std::clamp((centerX - point.x) / scale, -2.0, 2.0));
      const float elevationRadians = current.elevationDeg * s3g::kPi / 180.0f;
      const float planar = std::sqrt(x * x + y * y);
      desiredDistance = std::clamp(
          planar / std::max(0.05f, std::cos(elevationRadians)), 0.15f, 2.0f);
      desiredAzimuth = std::atan2(y, x) * 180.0f / s3g::kPi;
    } else {
      const float azimuthRadians = current.azimuthDeg * s3g::kPi / 180.0f;
      const float elevationRadians = current.elevationDeg * s3g::kPi / 180.0f;
      const float preservedX = std::cos(elevationRadians) *
                               std::cos(azimuthRadians) * current.distance;
      const float y = static_cast<float>(
          std::clamp((centerX - point.x) / scale, -2.0, 2.0));
      const float z = static_cast<float>(
          std::clamp((centerY - point.y) / scale, -2.0, 2.0));
      desiredDistance = std::clamp(
          std::sqrt(preservedX * preservedX + y * y + z * z), 0.15f, 2.0f);
      desiredAzimuth = std::atan2(y, preservedX) * 180.0f / s3g::kPi;
      desiredElevation =
          std::asin(
              std::clamp(z / std::max(0.15f, desiredDistance), -1.0f, 1.0f)) *
          180.0f / s3g::kPi;
    }

    const float azimuthOffset =
        wrapGuiDegrees(snapshot.params.bodyAzimuthOffsetDeg[body] +
                       wrapGuiDegrees(desiredAzimuth - current.azimuthDeg));
    const float elevationOffset =
        std::clamp(snapshot.params.bodyElevationOffsetDeg[body] +
                       desiredElevation - current.elevationDeg,
                   -180.0f, 180.0f);
    editValue(bodyAedParamId(body, BodyAedParamKind::AzimuthOffset),
              azimuthOffset);
    editValue(bodyAedParamId(body, BodyAedParamKind::ElevationOffset),
              elevationOffset);
    editValue(bodyAedParamId(body, BodyAedParamKind::Distance),
              desiredDistance);
    _selectedBody = body;
    storeViewState();
    setNeedsDisplay(true);
  }

  void drawControl_panel_row_labelAttrs_valueAttrs_style(
      clap_id param, drawing::Rect panel, uint32_t row,
      drawing::Attrs labelAttrs, drawing::Attrs valueAttrs,
      drawing::Style &style) {
    auto *p = static_cast<Plugin *>(_plugin);
    const auto *spec = paramSpec(param);
    if (!p || !spec)
      return;
    const double value = getParam(*p, param);
    char formatted[64]{};
    formatParam(param, value, formatted, sizeof(formatted));
    drawing::String label = drawing::stringFromUtf8(shortParamName(param));
    drawing::String display = drawing::stringFromUtf8(formatted);
    const double y = controlRowY(panel, row);
    if (spec->display == DisplayKind::Menu) {
      drawing::drawProcessorMenu(label, display, y, panel.origin.x,
                                 panel.size.width, labelAttrs, valueAttrs,
                                 style);
    } else {
      drawing::drawProcessorSlider(
          label, display, normalizedParam(*spec, value), y, panel.origin.x,
          panel.size.width, labelAttrs, valueAttrs, style);
    }
  }

  void drawFieldWithStyle_attrs_peak(drawing::Style &style,
                                     drawing::Attrs attrs, float peak) {
    (void)peak;
    auto *p = static_cast<Plugin *>(_plugin);
    const auto params = controlStateSnapshot(*p).params;
    const drawing::Rect panel = fieldPanelRect();
    const drawing::Rect field = fieldPlotRect();
    drawing::drawPanelFrame(panel.origin.x, panel.origin.y, panel.size.width,
                            panel.size.height, style);
    drawing::drawPanelHeader(
        "MODAL BODY FIELD", true, panel.origin.x, panel.origin.y,
        panel.size.width,
        static_cast<double>(s3g::gui_layout::kStandardMetrics.headerHeight),
        attrs, style);
    drawing::drawTopologyProcessorCameraButtons(panel, _viewMode, attrs, style);
    drawing::drawToolboxHeaderButton(modalZoomButtonRect(0u), panel, "-", false,
                                     attrs, style);
    drawing::drawToolboxHeaderButton(modalZoomButtonRect(1u), panel, "+", false,
                                     attrs, style);
    drawing::drawToolboxHeaderButton(modalResetLayoutButtonRect(), panel,
                                     "RESET", false, attrs, style);
    drawing::setFill(drawing::calibratedColor(0x0a0a0a));
    drawing::rectFill(field);
    drawing::setStroke(style.grid);
    drawing::frameRect(field);
    drawing::GraphicsScope graphics(*drawing::current->context);
    drawing::clipRect(drawing::inset(field, 1.0, 1.0));
    const char *bodyName = menuName(
        kParamSubstrate,
        menuIndexForValue(kParamSubstrate, getParam(*p, kParamSubstrate)));
    const char *listenerName =
        menuName(kParamListenerPickupSet,
                 menuIndexForValue(kParamListenerPickupSet,
                                   getParam(*p, kParamListenerPickupSet)));
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat("%@  /  %u BODIES  /  96 MODES  /  %s LISTENER",
                              drawing::stringFromUtf8(bodyName),
                              params.bodyCount, listenerName),
        drawing::makePoint(field.origin.x + 12.0, field.origin.y + 12.0),
        attrs);

    const double centerX = drawing::midX(field);
    const double centerY = field.origin.y + field.size.height * 0.54;
    const auto projectGuide = [&](double x, double y, double z) {
      return projectWorldPoint_rect_depth(
          {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)},
          field, nullptr);
    };
    for (const double ringRadius : {0.34, 0.67}) {
      drawing::Path distanceRing = drawing::Path::bezierPath();
      for (uint32_t segment = 0u; segment <= 96u; ++segment) {
        const double phase =
            static_cast<double>(segment) / 96.0 * 6.28318530717958647692;
        const drawing::Point point = projectGuide(
            std::cos(phase) * ringRadius, std::sin(phase) * ringRadius, 0.0);
        if (segment == 0u)
          (distanceRing).moveToPoint(point);
        else
          (distanceRing).lineToPoint(point);
      }
      drawing::setStroke(drawing::ambisonicOrientationGuideColor(0.12));
      (distanceRing).setLineWidth(0.55);
      (distanceRing).stroke();
    }
    drawing::drawAmbisonicOrientationGuides(projectGuide);

    const uint32_t count = std::clamp<uint32_t>(params.bodyCount, 4u, 8u);
    _selectedBody = std::min<uint32_t>(_selectedBody, count - 1u);
    p->guiState.selectedBody = _selectedBody;
    std::array<drawing::Point, s3g::kAccelerometerFieldMaxBodyCount> points{};
    std::array<double, s3g::kAccelerometerFieldMaxBodyCount> depths{};
    std::array<GuiBodyAed, s3g::kAccelerometerFieldMaxBodyCount> bodyAed{};
    std::array<s3g::Vec3, s3g::kAccelerometerFieldMaxBodyCount>
        bodyDirections{};
    std::array<uint32_t, s3g::kAccelerometerFieldMaxBodyCount> order{};
    for (uint32_t body = 0u; body < count; ++body) {
      order[body] = body;
      bodyAed[body] = guiBodyAed(*p, body);
      const s3g::Vec3 direction = guiBodyDirection(*p, body);
      bodyDirections[body] = direction;
      const float distance =
          p->bodyDistance[body].load(std::memory_order_relaxed);
      const s3g::Vec3 world{
          direction.x * distance,
          direction.y * distance,
          direction.z * distance,
      };
      points[body] = projectWorldPoint_rect_depth(world, field, &depths[body]);
    }
    std::sort(order.begin(), order.begin() + count,
              [&](uint32_t first, uint32_t second) {
                return depths[first] < depths[second];
              });

    const bool tetraListener = params.listenerPickupSet ==
                               s3g::AccelerometerFieldListenerPickupSet::Tetra4;
    const uint32_t pickupCount = tetraListener ? 4u : 8u;
    std::array<s3g::Vec3, s3g::kAmbiFieldListenerMaxLobes> listenerDirections{};
    if (tetraListener) {
      const auto &tetra = s3g::ambiFieldListenerTetraDirections();
      std::copy(tetra.begin(), tetra.end(), listenerDirections.begin());
    } else {
      listenerDirections = s3g::ambiFieldListenerCubeDirections();
    }
    std::array<drawing::Point, s3g::kAmbiFieldListenerMaxLobes> ears{};
    std::array<double, s3g::kAmbiFieldListenerMaxLobes> earDepths{};
    std::array<uint32_t, s3g::kAmbiFieldListenerMaxLobes> earOrder{};
    float peakEarEnergy = 0.0f;
    for (uint32_t ear = 0u; ear < pickupCount; ++ear) {
      earOrder[ear] = ear;
      const s3g::Vec3 direction = listenerDirections[ear];
      ears[ear] = projectWorldPoint_rect_depth(
          s3g::Vec3{
              direction.x * 1.18f,
              direction.y * 1.18f,
              direction.z * 1.18f,
          },
          field, &earDepths[ear]);
      peakEarEnergy = std::max(peakEarEnergy, p->listenerPickupEnergy[ear].load(
                                                  std::memory_order_relaxed));
    }
    std::sort(earOrder.begin(), earOrder.begin() + pickupCount,
              [&](uint32_t first, uint32_t second) {
                return earDepths[first] < earDepths[second];
              });

    // The fixed listener shell is world-aligned and camera-projected. Its
    // neutral geometry stays distinct from the bodies' AED/OKLCH identity.
    drawing::setStroke(drawing::calibratedColor(0x565656, 0.34));
    for (uint32_t first = 0u; first < pickupCount; ++first) {
      for (uint32_t second = first + 1u; second < pickupCount; ++second) {
        const auto &a = listenerDirections[first];
        const auto &b = listenerDirections[second];
        const float dot = a.x * b.x + a.y * b.y + a.z * b.z;
        const bool edge =
            tetraListener || std::fabs(dot - 0.3333333333f) < 0.05f;
        if (edge) {
          drawing::Path::strokeLineFromPoint_toPoint(ears[first], ears[second]);
        }
      }
    }
    for (uint32_t sorted = 0u; sorted < pickupCount; ++sorted) {
      const uint32_t ear = earOrder[sorted];
      const float raw =
          p->listenerPickupEnergy[ear].load(std::memory_order_relaxed);
      const double relative =
          peakEarEnergy > 1.0e-7f
              ? std::clamp<double>(raw / peakEarEnergy, 0.0, 1.0)
              : 0.0;
      const double absolute =
          std::clamp<double>(raw / (raw + 0.015f), 0.0, 1.0);
      const double strength = std::sqrt(relative * absolute);
      const double halo = 6.0 + strength * 15.0;
      drawing::setFill(
          drawing::calibratedColor(0xc8c8c8, 0.025 + strength * 0.17));
      (drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
           ears[ear].x - halo, ears[ear].y - halo, halo * 2.0, halo * 2.0)))
          .fill();
      const double radius = 5.5 + strength * 1.5;
      drawing::Path diamond = drawing::Path::bezierPath();
      (diamond).moveToPoint(
          drawing::makePoint(ears[ear].x, ears[ear].y - radius));
      (diamond).lineToPoint(
          drawing::makePoint(ears[ear].x + radius, ears[ear].y));
      (diamond).lineToPoint(
          drawing::makePoint(ears[ear].x, ears[ear].y + radius));
      (diamond).lineToPoint(
          drawing::makePoint(ears[ear].x - radius, ears[ear].y));
      (diamond).closePath();
      drawing::setFill(
          drawing::calibratedColor(0x8c8c8c, 0.36 + strength * 0.54));
      (diamond).fill();
      drawing::setStroke(
          drawing::calibratedColor(0xd2d2d2, 0.42 + strength * 0.46));
      (diamond).setLineWidth(0.8 + strength * 1.2);
      (diamond).stroke();
      drawing::String label = drawing::stringFormat("E%u", ear + 1u);
      const drawing::Size labelSize = drawing::sizeWithAttributes(label, attrs);
      const double labelX = ears[ear].x < centerX
                                ? ears[ear].x + 8.0
                                : ears[ear].x - labelSize.width - 8.0;
      drawing::drawAtPoint_withAttributes(
          label, drawing::makePoint(labelX, ears[ear].y - 6.0), attrs);
    }

    std::array<double, s3g::kAccelerometerFieldMaxBodyCount> actuatorStrength{};
    std::array<uint32_t, s3g::kAccelerometerFieldMaxBodyCount> actuatorEar{};
    for (uint32_t body = 0u; body < count; ++body) {
      const float rawDrive =
          p->actuatorBodyDrive[body].load(std::memory_order_relaxed);
      const double drive =
          std::clamp<double>(rawDrive / (rawDrive + 0.04f), 0.0, 1.0);
      actuatorStrength[body] = drive;
      s3g::Vec3 routeDirection = bodyDirections[body];
      if (params.fieldListenMode == s3g::AmbiFieldListenMode::Counter) {
        routeDirection = {
            -routeDirection.x,
            -routeDirection.y,
            -routeDirection.z,
        };
      }
      uint32_t bestEar = 0u;
      float bestDot = -2.0f;
      for (uint32_t ear = 0u; ear < pickupCount; ++ear) {
        const auto &direction = listenerDirections[ear];
        const float dot = routeDirection.x * direction.x +
                          routeDirection.y * direction.y +
                          routeDirection.z * direction.z;
        if (dot > bestDot) {
          bestDot = dot;
          bestEar = ear;
        }
      }
      actuatorEar[body] = bestEar;
      if (drive > 0.008) {
        drawing::setStroke(
            drawing::calibratedColor(0xc0c0c0, 0.08 + drive * 0.56));
        drawing::Path route = drawing::Path::bezierPath();
        (route).moveToPoint(ears[bestEar]);
        (route).lineToPoint(points[body]);
        (route).setLineWidth(0.6 + drive * 2.0);
        (route).stroke();
      }
    }
    for (uint32_t sorted = 0u; sorted < count; ++sorted) {
      const uint32_t body = order[sorted];
      const bool selected = body == _selectedBody;
      const double energy = std::clamp<double>(
          p->bodyEnergy[body].load(std::memory_order_relaxed) * 18.0f, 0.0,
          1.0);
      const double radius = 15.0 + (8.0 - static_cast<double>(count)) * 0.8;
      if (energy > 0.004) {
        const double halo = radius + 4.0 + energy * 7.0;
        drawing::setFill(modalBodyColorFromAed(
            bodyAed[body].azimuthDeg, bodyAed[body].elevationDeg,
            bodyAed[body].distance, selected, 0.04 + energy * 0.22));
        (drawing::Path::bezierPathWithOvalInRect(
             drawing::makeRect(points[body].x - halo, points[body].y - halo,
                               halo * 2.0, halo * 2.0)))
            .fill();
      }
      drawing::setFill(drawing::calibratedColor(0x1d1d1d, 0.96));
      (drawing::Path::bezierPathWithOvalInRect(
           drawing::makeRect(points[body].x - radius, points[body].y - radius,
                             radius * 2.0, radius * 2.0)))
          .fill();
      drawing::setFill(modalBodyColorFromAed(
          bodyAed[body].azimuthDeg, bodyAed[body].elevationDeg,
          bodyAed[body].distance, selected, 0.88));
      (drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
           points[body].x - radius + 2.0, points[body].y - radius + 2.0,
           radius * 2.0 - 4.0, radius * 2.0 - 4.0)))
          .fill();
      drawing::setStroke(modalBodyColorFromAed(
          bodyAed[body].azimuthDeg, bodyAed[body].elevationDeg,
          bodyAed[body].distance, true, 0.34 + energy * 0.62));
      drawing::Path outline = drawing::Path::bezierPathWithOvalInRect(
          drawing::makeRect(points[body].x - radius, points[body].y - radius,
                            radius * 2.0, radius * 2.0));
      (outline).setLineWidth(1.0 + energy * 2.2);
      (outline).stroke();
      drawing::setStroke(drawing::calibratedColor(0x111111, 0.42));
      for (const double modeRing : {0.38, 0.68}) {
        (drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
             points[body].x - radius * modeRing,
             points[body].y - radius * modeRing, radius * modeRing * 2.0,
             radius * modeRing * 2.0)))
            .stroke();
      }
      drawing::setFill(drawing::calibratedColor(0x111111, 0.88));
      drawing::rectFill(drawing::makeRect(points[body].x - 6.0,
                                          points[body].y - 6.0, 12.0, 12.0));
      drawing::String number = drawing::stringFormat("%u", body + 1u);
      const drawing::Size numberSize =
          drawing::sizeWithAttributes(number, attrs);
      drawing::drawAtPoint_withAttributes(
          number,
          drawing::makePoint(points[body].x - numberSize.width * 0.5,
                             points[body].y - numberSize.height * 0.5 - 0.5),
          attrs);
      if (selected) {
        drawing::setStroke(drawing::calibratedColor(0xb8b8b8, 0.92));
        drawing::frameRect(drawing::makeRect(
            points[body].x - radius - 4.0, points[body].y - radius - 4.0,
            radius * 2.0 + 8.0, radius * 2.0 + 8.0));
      }
    }

    uint32_t dominantBody = 0u;
    for (uint32_t body = 1u; body < count; ++body) {
      if (actuatorStrength[body] > actuatorStrength[dominantBody]) {
        dominantBody = body;
      }
    }
    for (uint32_t body = 0u; body < count; ++body) {
      const double drive = actuatorStrength[body];
      if (drive <= 0.008)
        continue;
      const drawing::Point source = ears[actuatorEar[body]];
      const drawing::Point destination = points[body];
      const double phase =
          std::fmod(_actuatorFlowPhase +
                        static_cast<double>(body) /
                            static_cast<double>(std::max<uint32_t>(1u, count)),
                    1.0);
      const drawing::Point flow =
          drawing::makePoint(source.x + (destination.x - source.x) * phase,
                             source.y + (destination.y - source.y) * phase);
      const double radius =
          body == dominantBody ? 3.5 + drive * 3.0 : 2.0 + drive * 2.0;
      drawing::Path actuator = drawing::Path::bezierPath();
      (actuator).moveToPoint(drawing::makePoint(flow.x, flow.y - radius));
      (actuator).lineToPoint(drawing::makePoint(flow.x + radius, flow.y));
      (actuator).lineToPoint(drawing::makePoint(flow.x, flow.y + radius));
      (actuator).lineToPoint(drawing::makePoint(flow.x - radius, flow.y));
      (actuator).closePath();
      drawing::setFill(drawing::calibratedColor(0xe0e0e0, 0.46 + drive * 0.48));
      (actuator).fill();
      drawing::setStroke(
          drawing::calibratedColor(0xf0f0f0, 0.52 + drive * 0.42));
      (actuator).setLineWidth(0.8 + drive);
      (actuator).stroke();
    }

    const double listenerActivity = std::clamp<double>(
        p->listenerActivity.load(std::memory_order_relaxed), 0.0, 1.0);
    const double listenerRadius = 5.0 + listenerActivity * 2.0;
    drawing::setFill(drawing::calibratedColor(0x0a0a0a, 0.92));
    (drawing::Path::bezierPathWithOvalInRect(
         drawing::makeRect(centerX - listenerRadius, centerY - listenerRadius,
                           listenerRadius * 2.0, listenerRadius * 2.0)))
        .fill();
    drawing::setStroke(
        drawing::calibratedColor(0xc8c8c8, 0.46 + listenerActivity * 0.42));
    (drawing::Path::bezierPathWithOvalInRect(
         drawing::makeRect(centerX - listenerRadius, centerY - listenerRadius,
                           listenerRadius * 2.0, listenerRadius * 2.0)))
        .stroke();
    drawing::drawAtPoint_withAttributes(
        "L", drawing::makePoint(centerX - 3.0, centerY - 6.0), attrs);
    if (actuatorStrength[dominantBody] > 0.008) {
      drawing::String actuatorLabel =
          params.fieldListenMode == s3g::AmbiFieldListenMode::Balance
              ? "DISTRIBUTED ACTUATOR"
              : "ACTUATOR FLOW";
      const drawing::Size labelSize =
          drawing::sizeWithAttributes(actuatorLabel, attrs);
      const drawing::Point labelOrigin = drawing::makePoint(
          centerX - labelSize.width * 0.5, centerY + listenerRadius + 8.0);
      drawing::setFill(drawing::calibratedColor(0x0a0a0a, 0.84));
      drawing::rectFill(
          drawing::inset(drawing::makeRect(labelOrigin.x, labelOrigin.y,
                                           labelSize.width, labelSize.height),
                         -3.0, -2.0));
      drawing::drawAtPoint_withAttributes(actuatorLabel, labelOrigin, attrs);
    }

    const GuiBodyAed selectedAed = bodyAed[_selectedBody];
    drawing::String selectedReadout = drawing::stringFormat(
        "B%u  AZ %+5.1f  EL %+5.1f  DST %.2f  /  %@", _selectedBody + 1u,
        selectedAed.azimuthDeg, selectedAed.elevationDeg, selectedAed.distance,
        (_viewMode == 0 || _viewMode == 1) ? "DRAG BODY"
                                           : "SELECT BODY / DRAG BLANK CAMERA");
    drawing::drawAtPoint_withAttributes(
        selectedReadout,
        drawing::makePoint(field.origin.x + 12.0, drawing::maxY(field) - 57.0),
        attrs);
    drawing::drawAtPoint_withAttributes(
        "OKLCH  H=AZ  L=EL  C=DIST",
        drawing::makePoint(field.origin.x + 12.0, drawing::maxY(field) - 39.0),
        attrs);
    drawing::String format =
        params.outputMode == s3g::AccelerometerFieldOutputMode::Ambisonic
            ? drawing::stringFormat("%uOA  ACN/SN3D", params.ambisonicOrder)
            : drawing::stringFormat("%u BODY STEMS + %u ZERO", count,
                                    8u - count);
    drawing::drawAtPoint_withAttributes(
        format,
        drawing::makePoint(field.origin.x + 12.0, drawing::maxY(field) - 21.0),
        attrs);
    graphics.restore();
  }

  void drawOpenMenuWithStyle_attrs(drawing::Style &style,
                                   drawing::Attrs attrs) {
    if (_openMenu == CLAP_INVALID_ID)
      return;
    auto *p = static_cast<Plugin *>(_plugin);
    const uint32_t count = menuCount(_openMenu);
    constexpr uint32_t kMaximumMenuItems = kFactoryPresetCount + 1u;
    if (!p || count == 0u || count > kMaximumMenuItems)
      return;
    drawing::Rect anchor = _openMenu == kParamPreset
                               ? drawing::cocoaRect(kTitleBand.presetMenu)
                               : menuAnchorRect(_openMenuLocation);
    const double itemHeight = 19.0;
    const uint32_t columns = menuColumnCount(count);
    const uint32_t rows = drawing::multiColumnMenuRows(count, columns);
    drawing::Rect menu =
        drawing::makeRect(anchor.origin.x, drawing::maxY(anchor) + 2.0,
                          anchor.size.width * static_cast<double>(columns),
                          itemHeight * static_cast<double>(rows));
    std::array<drawing::String, kMaximumMenuItems> items{};
    for (uint32_t index = 0u; index < count; ++index) {
      items[index] = drawing::stringFromUtf8(menuName(_openMenu, index));
    }
    const int selected =
        static_cast<int>(menuIndexForValue(_openMenu, getParam(*p, _openMenu)));
    if (columns > 1u) {
      drawing::drawMultiColumnDropdownMenu(menu, itemHeight, items.data(),
                                           count, columns, selected, -1, attrs,
                                           style);
    } else {
      drawing::drawDropdownMenu(menu, itemHeight, items.data(), count, selected,
                                -1, attrs, style);
    }
  }

  void drawOriginal(drawing::Rect dirtyRect) {
    (void)dirtyRect;
    auto *p = static_cast<Plugin *>(_plugin);
    if (!_dragView && _dragBody < 0) {
      _viewMode = p->guiState.viewMode;
      _viewAzimuthDeg = p->guiState.viewAzimuthDeg;
      _viewElevationDeg = p->guiState.viewElevationDeg;
      _viewZoom = p->guiState.viewZoom;
      _selectedBody = p->guiState.selectedBody;
    }
    drawing::Style style;
    drawing::setFill(style.bg);
    drawing::rectFill(bounds());
    drawing::Attrs labels = drawing::softLabelAttrs();
    drawing::Attrs values = drawing::softValueAttrs();
    const float peak = p->outputPeak.exchange(
        p->outputPeak.load(std::memory_order_relaxed) * 0.92f,
        std::memory_order_relaxed);
    drawing::drawEncoderTitleBand(
        "s3g AMBI ENCODER MODAL", drawing::stringFromUtf8(p->presetName),
        drawing::peakDbText(peak), kTitleBand, drawing::softTitleAttrs(),
        labels, values, style);

    drawFieldWithStyle_attrs_peak(style, values, peak);
    const auto drawPanel = [&](drawing::String title,
                               const s3g::gui_layout::Panel &panel) {
      drawing::drawPanelFrame(panel, style);
      drawing::drawPanelHeader(title, true, panel, labels, style);
    };
    drawPanel("OUTPUT", kOutputPanelLayout);
    drawPanel("SOURCE / MODAL BODY", kStructurePanelLayout);
    drawPanel("LISTENER / ACTUATOR", kActuatorPanelLayout);
    drawPanel("PROJECTION", kProjectionPanelLayout);
    drawPanel("BODY CHARACTER", kRadiationPanelLayout);
    drawPanel("BODY AED", kBodyPointPanelLayout);
    drawPanel("DISTRIBUTED SKIN", kSignalPanelLayout);

    for (uint32_t row = 0u; row < kOutputControls.size(); ++row) {
      drawControl_panel_row_labelAttrs_valueAttrs_style(
          kOutputControls[row], outputPanelRect(), row, labels, values, style);
    }
    for (uint32_t row = 0u; row < kActuatorControls.size(); ++row) {
      drawControl_panel_row_labelAttrs_valueAttrs_style(
          kActuatorControls[row], actuatorPanelRect(), row, labels, values,
          style);
    }
    for (uint32_t row = 0u; row < kStructureControls.size(); ++row) {
      drawControl_panel_row_labelAttrs_valueAttrs_style(
          kStructureControls[row], structurePanelRect(), row, labels, values,
          style);
    }
    for (uint32_t row = 0u; row < kProjectionControls.size(); ++row) {
      drawControl_panel_row_labelAttrs_valueAttrs_style(
          kProjectionControls[row], projectionPanelRect(), row, labels, values,
          style);
    }
    for (uint32_t row = 0u; row < kRadiationControls.size(); ++row) {
      drawControl_panel_row_labelAttrs_valueAttrs_style(
          kRadiationControls[row], radiationPanelRect(), row, labels, values,
          style);
    }
    const auto selectedControls = bodyPointControls(_selectedBody);
    for (uint32_t row = 0u; row < selectedControls.size(); ++row) {
      drawControl_panel_row_labelAttrs_valueAttrs_style(
          selectedControls[row], bodyPointPanelRect(), row, labels, values,
          style);
    }
    const GuiBodyAed selectedAed = guiBodyAed(*p, _selectedBody);
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat("B%u  ACTUAL  %+5.1f / %+5.1f / %.2f",
                              _selectedBody + 1u, selectedAed.azimuthDeg,
                              selectedAed.elevationDeg, selectedAed.distance),
        drawing::makePoint(bodyPointPanelRect().origin.x + 16.0,
                           bodyPointPanelRect().origin.y + 122.0),
        values);
    const drawing::Rect signal = signalPanelRect();
    const double signalX = signal.origin.x + 16.0;
    const float skinX =
        static_cast<float>(getParam(*p, bodySkinParamId(_selectedBody, true)));
    const float skinY =
        static_cast<float>(getParam(*p, bodySkinParamId(_selectedBody, false)));
    const float skinExtent =
        static_cast<float>(getParam(*p, kParamPropagationLoss));
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat("B%u SKIN", _selectedBody + 1u),
        drawing::makePoint(signalX, signal.origin.y + 38.0), values);
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat("X  %3.0f%%", skinX * 100.0f),
        drawing::makePoint(signalX, signal.origin.y + 61.0), values);
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat("Y  %3.0f%%", skinY * 100.0f),
        drawing::makePoint(signalX, signal.origin.y + 81.0), values);
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat("EXT %3.0f%%", skinExtent * 100.0f),
        drawing::makePoint(signalX, signal.origin.y + 101.0), values);
    drawing::drawAtPoint_withAttributes(
        "DRAG CONTACT", drawing::makePoint(signalX, signal.origin.y + 126.0),
        labels);
    drawing::drawAtPoint_withAttributes(
        "4 PATCH HOA", drawing::makePoint(signalX, signal.origin.y + 143.0),
        labels);

    const drawing::Rect skin = skinPadRect();
    drawing::setFill(drawing::calibratedColor(0x0a0a0a));
    drawing::rectFill(skin);
    drawing::setStroke(style.grid);
    drawing::frameRect(skin);
    drawing::setStroke(drawing::calibratedColor(0x343f44, 0.82));
    for (uint32_t line = 1u; line < 4u; ++line) {
      const double unit = static_cast<double>(line) / 4.0;
      drawing::Path::strokeLineFromPoint_toPoint(
          drawing::makePoint(skin.origin.x + skin.size.width * unit,
                             skin.origin.y),
          drawing::makePoint(skin.origin.x + skin.size.width * unit,
                             drawing::maxY(skin)));
      drawing::Path::strokeLineFromPoint_toPoint(
          drawing::makePoint(skin.origin.x,
                             skin.origin.y + skin.size.height * unit),
          drawing::makePoint(drawing::maxX(skin),
                             skin.origin.y + skin.size.height * unit));
    }
    constexpr std::array<std::array<float, 2u>, 4u> patchOffset{{
        {{-1.0f, -1.0f}},
        {{1.0f, -1.0f}},
        {{-1.0f, 1.0f}},
        {{1.0f, 1.0f}},
    }};
    const float patchRadius = skinExtent * 0.44f;
    drawing::setFill(drawing::calibratedColor(0x55a6b6, 0.78));
    for (const auto &offset : patchOffset) {
      const float px = s3g::clamp(skinX + offset[0] * patchRadius, 0.0f, 1.0f);
      const float py = s3g::clamp(skinY + offset[1] * patchRadius, 0.0f, 1.0f);
      const drawing::Point point =
          drawing::makePoint(skin.origin.x + px * skin.size.width,
                             drawing::maxY(skin) - py * skin.size.height);
      (drawing::Path::bezierPathWithOvalInRect(
           drawing::makeRect(point.x - 3.5, point.y - 3.5, 7.0, 7.0)))
          .fill();
    }
    const drawing::Point contact =
        drawing::makePoint(skin.origin.x + skinX * skin.size.width,
                           drawing::maxY(skin) - skinY * skin.size.height);
    drawing::setStroke(drawing::calibratedWhite(0.96, 0.94));
    drawing::Path crosshair = drawing::Path::bezierPath();
    (crosshair).appendBezierPathWithOvalInRect(
        drawing::makeRect(contact.x - 7.0, contact.y - 7.0, 14.0, 14.0));
    (crosshair).moveToPoint(drawing::makePoint(contact.x - 10.0, contact.y));
    (crosshair).lineToPoint(drawing::makePoint(contact.x + 10.0, contact.y));
    (crosshair).moveToPoint(drawing::makePoint(contact.x, contact.y - 10.0));
    (crosshair).lineToPoint(drawing::makePoint(contact.x, contact.y + 10.0));
    (crosshair).stroke();
    drawOpenMenuWithStyle_attrs(style, values);
  }

  void updateSkinContactAtPoint(drawing::Point point) {
    const drawing::Rect skin = skinPadRect();
    const double x =
        std::clamp((point.x - skin.origin.x) / skin.size.width, 0.0, 1.0);
    const double y = std::clamp(
        (drawing::maxY(skin) - point.y) / skin.size.height, 0.0, 1.0);
    setParam_value(bodySkinParamId(_selectedBody, true), x);
    setParam_value(bodySkinParamId(_selectedBody, false), y);
  }

  void updateDraggedParamAtPoint(drawing::Point point) {
    const auto *spec = paramSpec(_dragParam);
    if (!spec || spec->display == DisplayKind::Menu)
      return;
    GuiControlLocation location{};
    const bool found =
        findInGroup(point, kOutputPanelLayout, kOutputControls, location) ||
        findInGroup(point, kActuatorPanelLayout, kActuatorControls, location) ||
        findInGroup(point, kStructurePanelLayout, kStructureControls,
                    location) ||
        findInGroup(point, kProjectionPanelLayout, kProjectionControls,
                    location) ||
        findInGroup(point, kRadiationPanelLayout, kRadiationControls,
                    location) ||
        findInGroup(point, kBodyPointPanelLayout,
                    bodyPointControls(_selectedBody), location);
    const drawing::Rect panel = found && location.id == _dragParam
                                    ? location.panel
                                    : panelForParam(_dragParam);
    const double x = s3g::gui_layout::processorControlX(panel.origin.x);
    const double width = s3g::gui_layout::processorTrackWidth(panel.size.width);
    const double normalized = std::clamp((point.x - x) / width, 0.0, 1.0);
    setParam_value(_dragParam, valueFromNormalized(*spec, normalized));
  }

  void mouseDown(drawing::Event *event) {
    const drawing::Point point =
        convertPoint_fromView((event)->locationInWindow(), nullptr);
    auto *p = static_cast<Plugin *>(_plugin);
    const double itemHeight = 19.0;
    if (_openMenu != CLAP_INVALID_ID) {
      const uint32_t count = menuCount(_openMenu);
      drawing::Rect anchor = _openMenu == kParamPreset
                                 ? drawing::cocoaRect(kTitleBand.presetMenu)
                                 : menuAnchorRect(_openMenuLocation);
      const uint32_t columns = menuColumnCount(count);
      const uint32_t rows = drawing::multiColumnMenuRows(count, columns);
      const drawing::Rect menu =
          drawing::makeRect(anchor.origin.x, drawing::maxY(anchor) + 2.0,
                            anchor.size.width * static_cast<double>(columns),
                            itemHeight * static_cast<double>(rows));
      const int selected =
          columns > 1u
              ? drawing::multiColumnDropdownHitIndex(point, menu, itemHeight,
                                                     count, columns)
              : drawing::dropdownHitIndex(point, menu, itemHeight, count);
      if (selected >= 0) {
        setParam_value(
            _openMenu,
            menuValueForIndex(_openMenu, static_cast<uint32_t>(selected)));
        _openMenu = CLAP_INVALID_ID;
        return;
      }
      _openMenu = CLAP_INVALID_ID;
    }

    if (drawing::contains(point, drawing::cocoaRect(kTitleBand.presetMenu))) {
      _openMenu = kParamPreset;
      setNeedsDisplay(true);
      return;
    }
    if (drawing::contains(point, drawing::cocoaRect(kTitleBand.loadButton)) ||
        drawing::contains(point, drawing::cocoaRect(kTitleBand.saveButton))) {
      if (presetDialog(drawing::contains(
              point, drawing::cocoaRect(kTitleBand.saveButton)))) {
        setNeedsDisplay(true);
      }
      return;
    }
    if (drawing::contains(point, drawing::cocoaRect(kTitleBand.randomButton))) {
      randomizeSafe(*p);
      rescanValues();
      std::snprintf(p->presetName, sizeof(p->presetName), "%s", "RANDOM");
      setNeedsDisplay(true);
      return;
    }

    const drawing::Rect fieldPanel = fieldPanelRect();
    for (uint32_t index = 0u; index < 2u; ++index) {
      if (drawing::contains(point, modalZoomButtonRect(index))) {
        _viewZoom = std::clamp<double>(_viewZoom + (index == 0u ? -0.15 : 0.15),
                                       0.55, 2.20);
        storeViewState();
        setNeedsDisplay(true);
        return;
      }
    }
    for (uint32_t index = 0u; index < 3u; ++index) {
      if (drawing::contains(point, drawing::topologyProcessorCameraButtonRect(
                                       fieldPanel, index))) {
        setViewPreset(static_cast<int>(index));
        return;
      }
    }
    if (drawing::contains(point, modalResetLayoutButtonRect())) {
      for (uint32_t body = 0u; body < s3g::kAccelerometerFieldMaxBodyCount;
           ++body) {
        editValue(bodyAedParamId(body, BodyAedParamKind::AzimuthOffset), 0.0);
        editValue(bodyAedParamId(body, BodyAedParamKind::ElevationOffset), 0.0);
        editValue(bodyAedParamId(body, BodyAedParamKind::Distance), 1.0);
      }
      setNeedsDisplay(true);
      return;
    }
    if (drawing::contains(point, fieldPlotRect())) {
      const int hit = hitBodyAtPoint_inRect(point, fieldPlotRect());
      if (hit >= 0) {
        _selectedBody = static_cast<uint32_t>(hit);
        p->guiState.selectedBody = _selectedBody;
        if (_viewMode == 0 || _viewMode == 1) {
          _dragBody = hit;
          updateDraggedBodyAtPoint_inRect(point, fieldPlotRect());
        }
        storeViewState();
        setNeedsDisplay(true);
        return;
      }
      _dragView = true;
      _lastDragPoint = point;
      setNeedsDisplay(true);
      return;
    }

    if (drawing::contains(point, skinPadRect())) {
      if ((event)->clickCount() > 1) {
        setParam_value(bodySkinParamId(_selectedBody, true), 0.43);
        setParam_value(bodySkinParamId(_selectedBody, false), 0.50);
      } else {
        _dragSkin = true;
        updateSkinContactAtPoint(point);
      }
      setNeedsDisplay(true);
      return;
    }

    GuiControlLocation location{};
    if (!findInGroup(point, kOutputPanelLayout, kOutputControls, location) &&
        !findInGroup(point, kActuatorPanelLayout, kActuatorControls,
                     location) &&
        !findInGroup(point, kStructurePanelLayout, kStructureControls,
                     location) &&
        !findInGroup(point, kProjectionPanelLayout, kProjectionControls,
                     location) &&
        !findInGroup(point, kRadiationPanelLayout, kRadiationControls,
                     location) &&
        !findInGroup(point, kBodyPointPanelLayout,
                     bodyPointControls(_selectedBody), location)) {
      return;
    }
    const auto *spec = paramSpec(location.id);
    if (!spec)
      return;
    if (spec->display == DisplayKind::Menu) {
      _openMenu = location.id;
      _openMenuLocation = location;
      setNeedsDisplay(true);
      return;
    }
    double defaultValue = 0.0;
    if (drawing::sliderDoubleClickDefault(event, &p->plugin, location.id,
                                          &defaultValue)) {
      setParam_value(location.id, defaultValue);
      return;
    }
    _dragParam = location.id;
    updateDraggedParamAtPoint(point);
  }

  void mouseDragged(drawing::Event *event) {
    const drawing::Point point =
        convertPoint_fromView((event)->locationInWindow(), nullptr);
    if (_dragBody >= 0) {
      updateDraggedBodyAtPoint_inRect(point, fieldPlotRect());
      return;
    }
    if (_dragSkin) {
      updateSkinContactAtPoint(point);
      return;
    }
    if (_dragView) {
      _viewAzimuthDeg += (point.x - _lastDragPoint.x) * 0.35;
      _viewElevationDeg = std::clamp<double>(
          _viewElevationDeg + (point.y - _lastDragPoint.y) * 0.35, -85.0, 85.0);
      _viewMode = -1;
      _lastDragPoint = point;
      storeViewState();
      setNeedsDisplay(true);
      return;
    }
    if (_dragParam != CLAP_INVALID_ID) {
      updateDraggedParamAtPoint(point);
    }
  }

  void mouseUp(drawing::Event *event) {
    (void)event;
    _dragParam = CLAP_INVALID_ID;
    _dragView = false;
    _dragBody = -1;
    _dragSkin = false;
  }
};
} // namespace resonator_encoder_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new resonator_encoder_canvas::Editor(p);
}
#include "s3g_clap_canvas_gui.inc"
