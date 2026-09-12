// Literal Cocoa adaptation: original geometry, paths, draw order and hit maps.
namespace score_encoder_canvas {
namespace drawing = s3g::portable_gui::resonator_drawing;
namespace foundation = s3g::portable_gui::foundation;
using Kind = s3g::clap_gui::ParamEventKind;
namespace layout = s3g::gui_layout;

inline constexpr layout::Canvas kCanvas{static_cast<double>(kGuiWidth),
                                        static_cast<double>(kGuiHeight)};
inline constexpr auto kOutputPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Output,
    layout::kLargeEncoderFirstColumn, 42.0, 2u);
inline constexpr auto kEcologyPanel =
    layout::fittedStackPanel(layout::PanelRole::Engine, kOutputPanel, 5u);
inline constexpr auto kGeneratorsPanel = layout::fittedStackPanel(
    layout::PanelRole::Source, kEcologyPanel, 8u, 24.0, 34.0);
inline constexpr auto kScorePanel = layout::fittedStackPanel(
    layout::PanelRole::Modulation, kGeneratorsPanel, 5u);
inline constexpr std::array<layout::Panel, 4u> kFirstColumnPanels{
    {kOutputPanel, kEcologyPanel, kGeneratorsPanel, kScorePanel}};
inline constexpr auto kHorizonPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Topology,
    layout::kLargeEncoderSecondColumn, 42.0, 6u);
inline constexpr auto kAtmospherePanel =
    layout::fittedStackPanel(layout::PanelRole::Listener, kHorizonPanel, 6u);
inline constexpr auto kListenerPanel =
    layout::fittedStackPanel(layout::PanelRole::Listener, kAtmospherePanel, 3u);
inline constexpr auto kIdentityPanel =
    layout::fittedStackPanel(layout::PanelRole::Utility, kListenerPanel, 1u);
inline constexpr std::array<layout::Panel, 4u> kSecondColumnPanels{
    {kHorizonPanel, kAtmospherePanel, kListenerPanel, kIdentityPanel}};
static_assert(layout::validateColumn(kFirstColumnPanels, kCanvas));
static_assert(layout::validateColumn(kSecondColumnPanels, kCanvas, false));
static_assert(layout::controlMatchesSlot(kOutputPanel,
                                         layout::kLargeEncoderOrderSlot));
static_assert(layout::panelMatchesAnchor(kHorizonPanel,
                                         layout::kLargeEncoderTopologyAnchor));
static_assert(layout::sourceCardinalityControlMatches(
    kEcologyPanel, layout::SharedControlRole::SourceCardinality));

struct GuiSliderSpec {
  clap_id id;
  const layout::Panel *panel;
  uint32_t row;
};

inline constexpr std::array<GuiSliderSpec, 23u> kGuiSliders{{
    {kOutputParamId, &kOutputPanel, 0u},
    {kEntitiesParamId, &kEcologyPanel, 0u},
    {kActivityParamId, &kEcologyPanel, 2u},
    {kOccupancyParamId, &kEcologyPanel, 3u},
    {kPaceParamId, &kEcologyPanel, 4u},
    {kMemoryParamId, &kScorePanel, 0u},
    {kCascadeParamId, &kScorePanel, 1u},
    {kSignalsParamId, &kScorePanel, 2u},
    {kBedParamId, &kScorePanel, 3u},
    {kFloorParamId, &kScorePanel, 4u},
    {kSeedParamId, &kIdentityPanel, 0u},
    {kRangeParamId, &kHorizonPanel, 0u},
    {kAzimuthParamId, &kHorizonPanel, 1u},
    {kElevationParamId, &kHorizonPanel, 2u},
    {kArcParamId, &kHorizonPanel, 3u},
    {kDetailParamId, &kHorizonPanel, 4u},
    {kEdgeParamId, &kHorizonPanel, 5u},
    {kAirParamId, &kAtmospherePanel, 0u},
    {kAirNoiseParamId, &kAtmospherePanel, 1u},
    {kTerrainParamId, &kAtmospherePanel, 3u},
    {kCarryParamId, &kAtmospherePanel, 4u},
    {kTurbulenceParamId, &kAtmospherePanel, 5u},
    {kFieldListenAmountParamId, &kListenerPanel, 1u},
}};

struct GeneratorControl {
  clap_id id = CLAP_INVALID_ID;
  const char *label = "";
};

struct GeneratorPanelControls {
  std::array<GeneratorControl, 8u> rows{};
};

inline constexpr GeneratorPanelControls kMixedGeneratorControls{{{
    {kMachinesParamId, "MACHINES"},
    {kMachineToneParamId, "MACHINE TONE"},
    {kBellsParamId, "BELLS"},
    {kBellPitchParamId, "BELL PITCH"},
    {kTrafficParamId, "TRAFFIC"},
    {kEngineLoadParamId, "ENGINE LOAD"},
    {kAircraftParamId, "AIRCRAFT"},
    {kAircraftFlightParamId, "FLIGHT"},
}}};
inline constexpr GeneratorPanelControls kRuralGeneratorControls{{{
    {kBellsParamId, "BELLS"},
    {kBellPitchParamId, "BELL PITCH"},
    {kBellDecayParamId, "BELL DECAY"},
    {kMachinesParamId, "MACHINES"},
    {kMachineToneParamId, "MACHINE TONE"},
    {kAircraftParamId, "AIRCRAFT"},
    {kAircraftFlightParamId, "FLIGHT"},
    {},
}}};
inline constexpr GeneratorPanelControls kTrafficGeneratorControls{{{
    {kTrafficParamId, "TRAFFIC"},
    {kTrafficSpeedParamId, "ROAD SPEED"},
    {kEngineLoadParamId, "ENGINE LOAD"},
    {kMachinesParamId, "MACHINES"},
    {kMachineToneParamId, "MACHINE TONE"},
    {kAircraftParamId, "AIRCRAFT"},
    {kAircraftFlightParamId, "FLIGHT"},
    {kAircraftSpeedParamId, "PASS SPEED"},
}}};
inline constexpr GeneratorPanelControls kIndustrialGeneratorControls{{{
    {kMachinesParamId, "MACHINES"},
    {kMachineToneParamId, "MACHINE TONE"},
    {kBellsParamId, "BELLS"},
    {kBellPitchParamId, "BELL PITCH"},
    {kBellDecayParamId, "BELL DECAY"},
    {kTrafficParamId, "TRAFFIC"},
    {kTrafficSpeedParamId, "ROAD SPEED"},
    {kEngineLoadParamId, "ENGINE LOAD"},
}}};
inline constexpr GeneratorPanelControls kWeatherGeneratorControls{{{
    {kMachinesParamId, "MACHINES"},
    {kMachineToneParamId, "MACHINE TONE"},
    {kAircraftParamId, "AIRCRAFT"},
    {kAircraftFlightParamId, "FLIGHT"},
    {kSurfParamId, "SURF"},
    {kWaveRateParamId, "WAVE RATE"},
    {kWaveBreakParamId, "WAVE BREAK"},
    {},
}}};
inline constexpr GeneratorPanelControls kAirportGeneratorControls{{{
    {kAircraftParamId, "AIRCRAFT"},
    {kAircraftFlightParamId, "FLIGHT"},
    {kAircraftSpeedParamId, "PASS SPEED"},
    {kAircraftPowerParamId, "ENGINE POWER"},
    {kAircraftToneParamId, "ENGINE TONE"},
    {kTrafficParamId, "TRAFFIC"},
    {kTrafficSpeedParamId, "ROAD SPEED"},
    {kEngineLoadParamId, "ENGINE LOAD"},
}}};
inline constexpr GeneratorPanelControls kCoastGeneratorControls{{{
    {kFoghornsParamId, "FOGHORNS"},
    {kFoghornPitchParamId, "HORN PITCH"},
    {kFoghornPressureParamId, "HORN PRESSURE"},
    {kFoghornLengthParamId, "CALL LENGTH"},
    {kSurfParamId, "SURF"},
    {kWaveRateParamId, "WAVE RATE"},
    {kWaveBreakParamId, "WAVE BREAK"},
    {},
}}};

const GeneratorPanelControls &
generatorPanelControls(s3g::AmbiHorizonEcology ecology) {
  switch (ecology) {
  case s3g::AmbiHorizonEcology::Rural:
    return kRuralGeneratorControls;
  case s3g::AmbiHorizonEcology::Traffic:
  case s3g::AmbiHorizonEcology::City:
    return kTrafficGeneratorControls;
  case s3g::AmbiHorizonEcology::Industrial:
    return kIndustrialGeneratorControls;
  case s3g::AmbiHorizonEcology::Water:
  case s3g::AmbiHorizonEcology::Coast:
    return kCoastGeneratorControls;
  case s3g::AmbiHorizonEcology::Weather:
    return kWeatherGeneratorControls;
  case s3g::AmbiHorizonEcology::Airport:
    return kAirportGeneratorControls;
  default:
    return kMixedGeneratorControls;
  }
}

bool guiSliderSpec(clap_id id, s3g::AmbiHorizonEcology ecology,
                   GuiSliderSpec &result) {
  for (const auto &spec : kGuiSliders) {
    if (spec.id == id) {
      result = spec;
      return true;
    }
  }
  const auto &controls = generatorPanelControls(ecology);
  for (uint32_t row = 0u; row < controls.rows.size(); ++row) {
    if (controls.rows[row].id == id) {
      result = {id, &kGeneratorsPanel, row};
      return true;
    }
  }
  return false;
}

double sliderNorm(const GuiSliderSpec &spec, double value) {
  const auto *def = paramDef(spec.id);
  if (!def)
    return 0.0;
  if (spec.id == kAzimuthParamId) {
    return s3g::aedAzimuthSliderNorm(static_cast<float>(value));
  }
  if (def->logarithmic) {
    return std::log(std::max(def->min, value) / def->min) /
           std::log(def->max / def->min);
  }
  return (value - def->min) / (def->max - def->min);
}

double sliderValue(const GuiSliderSpec &spec, drawing::Point point) {
  const auto *def = paramDef(spec.id);
  if (!def)
    return 0.0;
  constexpr double kTrackOffset = 108.0;
  constexpr double kTrackWidth = 82.0;
  const double norm = std::clamp(
      (point.x - (spec.panel->frame.x + kTrackOffset)) / kTrackWidth, 0.0, 1.0);
  if (spec.id == kAzimuthParamId) {
    return s3g::aedAzimuthFromSliderNorm(static_cast<float>(norm));
  }
  double value = def->logarithmic
                     ? def->min * std::pow(def->max / def->min, norm)
                     : def->min + norm * (def->max - def->min);
  if (def->stepped)
    value = std::round(value);
  return value;
}

bool savePresetFile(const char *path, const PresetFile &preset) {
  if (!path)
    return false;
  std::FILE *file = std::fopen(path, "wb");
  if (!file)
    return false;
  const bool ok = std::fwrite(&preset, sizeof(preset), 1u, file) == 1u;
  std::fclose(file);
  return ok;
}

bool loadPresetFile(const char *path, PresetFile &preset) {
  if (!path)
    return false;
  std::FILE *file = std::fopen(path, "rb");
  if (!file)
    return false;
  uint32_t header[2]{};
  bool ok = std::fread(header, sizeof(header), 1u, file) == 1u &&
            header[0] == 0x53483331u;
  if (ok && header[1] == 1u) {
    PresetFileV1 legacy{};
    legacy.magic = header[0];
    legacy.version = header[1];
    ok = std::fread(reinterpret_cast<uint8_t *>(&legacy) + sizeof(header),
                    sizeof(legacy) - sizeof(header), 1u, file) == 1u;
    if (ok) {
      preset = {};
      preset.params = migrateParams(legacy.params);
      std::memcpy(preset.name, legacy.name, sizeof(preset.name));
    }
  } else if (ok && header[1] == 2u) {
    PresetFileV2 legacy{};
    legacy.magic = header[0];
    legacy.version = header[1];
    ok = std::fread(reinterpret_cast<uint8_t *>(&legacy) + sizeof(header),
                    sizeof(legacy) - sizeof(header), 1u, file) == 1u;
    if (ok) {
      preset = {};
      preset.params = migrateParams(legacy.params);
      std::memcpy(preset.name, legacy.name, sizeof(preset.name));
    }
  } else if (ok && header[1] == 3u) {
    PresetFileV3 legacy{};
    legacy.magic = header[0];
    legacy.version = header[1];
    ok = std::fread(reinterpret_cast<uint8_t *>(&legacy) + sizeof(header),
                    sizeof(legacy) - sizeof(header), 1u, file) == 1u;
    if (ok) {
      preset = {};
      preset.params = migrateParams(legacy.params);
      std::memcpy(preset.name, legacy.name, sizeof(preset.name));
    }
  } else if (ok && header[1] == 4u) {
    PresetFileV4 legacy{};
    legacy.magic = header[0];
    legacy.version = header[1];
    ok = std::fread(reinterpret_cast<uint8_t *>(&legacy) + sizeof(header),
                    sizeof(legacy) - sizeof(header), 1u, file) == 1u;
    if (ok) {
      preset = {};
      preset.params = migrateParams(legacy.params);
      std::memcpy(preset.name, legacy.name, sizeof(preset.name));
    }
  } else if (ok && header[1] == 5u) {
    preset.magic = header[0];
    preset.version = header[1];
    ok = std::fread(reinterpret_cast<uint8_t *>(&preset) + sizeof(header),
                    sizeof(preset) - sizeof(header), 1u, file) == 1u;
  } else {
    ok = false;
  }
  std::fclose(file);
  return ok;
}

class Editor final : public s3g::portable_gui::routing::Canvas {
public:
  Plugin *_plugin;

  s3g::AmbiHorizonEncoderParams _snapshot;
  uint32_t _presetSnapshot;
  int _dragParam;
  bool _dragView;
  drawing::Point _lastDragPoint;
  int _viewMode;
  double _viewAzDeg;
  double _viewElDeg;
  double _viewZoom;
  int _openMenu;
  int _hoverMenuItem;
  uint32_t _menuItemCount;
  drawing::Rect _openMenuRect;

#include "s3g_score_encoder_editor.inc"
  Editor(Plugin &instance) : Canvas(kGuiWidth, kGuiHeight) {
    auto *plugin = &instance;

    _plugin = plugin;
    _snapshot =
        plugin ? visibleParams(*plugin) : s3g::AmbiHorizonEncoderParams{};
    _presetSnapshot =
        plugin ? plugin->presetIndex.load(std::memory_order_acquire) : 0u;
    _dragParam = 0;
    _dragView = false;
    _lastDragPoint = drawing::Point{};
    _viewMode =
        plugin ? plugin->guiViewMode.load(std::memory_order_acquire) : 0;
    _viewAzDeg =
        plugin ? plugin->guiViewAzDeg.load(std::memory_order_acquire) : 38.0;
    _viewElDeg =
        plugin ? plugin->guiViewElDeg.load(std::memory_order_acquire) : 30.0;
    _viewZoom =
        plugin ? plugin->guiViewZoom.load(std::memory_order_acquire) : 1.0;
    // Fixed presets are semantic state, so migrate their old camera
    // angles to the canonical AED views when an older session is opened.
    if (_viewMode == 0) {
      _viewAzDeg = 90.0;
      _viewElDeg = 0.0;
    } else if (_viewMode == 1) {
      _viewAzDeg = 90.0;
      _viewElDeg = 90.0;
    } else if (_viewMode == 2) {
      _viewAzDeg = 38.0;
      _viewElDeg = 30.0;
    }
    if (plugin && _viewMode >= 0 && _viewMode <= 2) {
      plugin->guiViewAzDeg.store(static_cast<float>(_viewAzDeg),
                                 std::memory_order_release);
      plugin->guiViewElDeg.store(static_cast<float>(_viewElDeg),
                                 std::memory_order_release);
    }
    _openMenu = 0;
    _hoverMenuItem = -1;
    _menuItemCount = 0u;
    _openMenuRect = drawing::Rect{};

    setWantsFocus(true);
    service();
  }
  ~Editor() override { stopRefresh(); }
  void service() override {
    refreshSnapshot();
    _viewMode = _plugin->guiViewMode.load(std::memory_order_acquire);
    _viewAzDeg = _plugin->guiViewAzDeg.load(std::memory_order_acquire);
    _viewElDeg = _plugin->guiViewElDeg.load(std::memory_order_acquire);
    _viewZoom = _plugin->guiViewZoom.load(std::memory_order_acquire);
    if (_viewMode == 0) {
      _viewAzDeg = 90.;
      _viewElDeg = 0.;
    } else if (_viewMode == 1) {
      _viewAzDeg = 90.;
      _viewElDeg = 90.;
    } else if (_viewMode == 2) {
      _viewAzDeg = 38.;
      _viewElDeg = 30.;
    }
  }
  void refreshSnapshot() {
    if (!_plugin)
      return;
    _snapshot = visibleParams(*_plugin);
    _presetSnapshot = _plugin->presetIndex.load(std::memory_order_acquire);
  }

  drawing::Rect fieldPanelRect() { return drawing::makeRect(18, 42, 596, 700); }

  drawing::Rect fieldRect() { return drawing::makeRect(34, 76, 564, 650); }

  drawing::Rect presetMenuRect() {
    return drawing::encoderTitleActionRect(kGuiWidth, kGuiHeight,
                                           layout::EncoderTitleAction::Preset);
  }

  drawing::Rect loadPresetButtonRect() {
    return drawing::encoderTitleActionRect(kGuiWidth, kGuiHeight,
                                           layout::EncoderTitleAction::Load);
  }

  drawing::Rect savePresetButtonRect() {
    return drawing::encoderTitleActionRect(kGuiWidth, kGuiHeight,
                                           layout::EncoderTitleAction::Save);
  }

  drawing::Rect randomizeButtonRect() {
    return drawing::encoderTitleActionRect(kGuiWidth, kGuiHeight,
                                           layout::EncoderTitleAction::Random);
  }

  drawing::Rect viewButtonRect(int index) {
    const drawing::Rect panel = fieldPanelRect();
    return drawing::makeRect(drawing::maxX(panel) - 148.0 + index * 46.0,
                             panel.origin.y + 4.0, 40.0, 13.0);
  }

  drawing::Rect zoomButtonRect(int index) {
    return drawing::makeRect(drawing::maxX(fieldPanelRect()) - 204.0 +
                                 index * 24.0,
                             fieldPanelRect().origin.y + 4.0, 19.0, 13.0);
  }

  void storeViewState() {
    if (!_plugin)
      return;
    _plugin->guiViewMode.store(_viewMode, std::memory_order_release);
    _plugin->guiViewAzDeg.store(static_cast<float>(_viewAzDeg),
                                std::memory_order_release);
    _plugin->guiViewElDeg.store(static_cast<float>(_viewElDeg),
                                std::memory_order_release);
    _plugin->guiViewZoom.store(static_cast<float>(_viewZoom),
                               std::memory_order_release);
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
      _viewElDeg = 30.0;
    }
    storeViewState();
    setNeedsDisplay(true);
  }

  s3g::Vec3 entityWorld(uint32_t index) {
    if (!_plugin || index >= s3g::kAmbiHorizonMaxEntities)
      return {};
    const float az = _plugin->guiAzimuth[index].load(std::memory_order_relaxed);
    const float el =
        _plugin->guiElevation[index].load(std::memory_order_relaxed);
    const float range =
        _plugin->guiRange[index].load(std::memory_order_relaxed);
    const auto direction = s3g::directionFromAed(az, el);
    const float radius = 0.16f + range * 0.84f;
    return {direction.x * radius, direction.y * radius, direction.z * radius};
  }

  drawing::Point projectWorld_depth(s3g::Vec3 point, double *depth) {
    const drawing::Rect field = fieldRect();
    const double scale = std::min(field.size.width, field.size.height) * 0.40 *
                         std::clamp(_viewZoom, 0.55, 2.20);
    const float azimuth = static_cast<float>(_viewAzDeg * s3g::kPi / 180.0);
    const float elevation = static_cast<float>(_viewElDeg * s3g::kPi / 180.0);
    const float ca = std::cos(azimuth), sa = std::sin(azimuth);
    const float ce = std::cos(elevation), se = std::sin(elevation);
    const float x1 = ca * point.x - sa * point.y;
    const float y1 = sa * point.x + ca * point.y;
    const float y2 = ce * y1 + se * point.z;
    const float z2 = -se * y1 + ce * point.z;
    if (depth)
      *depth = z2;
    return drawing::makePoint(drawing::midX(field) + x1 * scale,
                              drawing::midY(field) - y2 * scale);
  }

  drawing::Point projectGroundPointX_y(double x, double y) {
    return projectWorld_depth(
        {static_cast<float>(x), static_cast<float>(y), 0.0f}, nullptr);
  }

  drawing::Point projectWorldPointX_y_z(double x, double y, double z) {
    return projectWorld_depth(
        {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)},
        nullptr);
  }

  void drawField_valueAttrs_style(drawing::Attrs attrs,
                                  drawing::Attrs valueAttrs,
                                  const drawing::Style &style) {
    const drawing::Rect panel = fieldPanelRect();
    const drawing::Rect field = fieldRect();
    drawing::drawPanelFrame(panel.origin.x, panel.origin.y, panel.size.width,
                            panel.size.height, style);
    drawing::drawPanelHeader("ACOUSTIC HORIZON", true, panel.origin.x,
                             panel.origin.y, panel.size.width, 21.0, attrs,
                             style);
    // Camera mode 0 projects the horizontal XY plane; mode 1 rotates to the
    // XZ elevation plane. Keep the labels aligned with the shared encoder
    // family convention and with the actual AED projection.
    static drawing::String viewNames[] = {"TOP", "SIDE", "3/4"};
    for (int index = 0; index < 3; ++index) {
      drawing::drawToolboxHeaderButton(viewButtonRect(index), panel,
                                       viewNames[index], _viewMode == index,
                                       attrs, style);
    }
    drawing::drawToolboxHeaderButton(zoomButtonRect(0), panel, "-", false,
                                     attrs, style);
    drawing::drawToolboxHeaderButton(zoomButtonRect(1), panel, "+", false,
                                     attrs, style);

    drawing::setFill(drawing::calibratedColor(0x090909));
    drawing::rectFill(field);
    drawing::setStroke(drawing::calibratedColor(0x4c4c4c));
    drawing::frameRect(field);

    // A sparse ground lattice gives the field a real world-space reference
    // without suggesting that the range display itself is a sounding object.
    // Every endpoint shares the entity camera transform, so the lattice turns,
    // tilts, and becomes edge-on in SIDE view instead of remaining a fixed
    // screen-space ellipse.
    drawing::GraphicsScope graphics(*drawing::current->context);
    drawing::clipRect(drawing::inset(field, 1.0, 1.0));
    constexpr std::array<double, 7> kGridOffsets{-0.75, -0.50, -0.25, 0.0,
                                                 0.25,  0.50,  0.75};
    for (double offset : kGridOffsets) {
      const double extent = std::sqrt(std::max(0.0, 1.0 - offset * offset));
      const bool axis = std::fabs(offset) < 0.001;
      drawing::setStroke(
          drawing::ambisonicOrientationGuideColor(axis ? 0.40 : 0.13));

      drawing::Path northSouth = drawing::Path::bezierPath();
      (northSouth).moveToPoint(projectGroundPointX_y(offset, -extent));
      (northSouth).lineToPoint(projectGroundPointX_y(offset, extent));
      (northSouth).setLineWidth(axis ? 0.75 : 0.45);
      (northSouth).stroke();

      drawing::Path eastWest = drawing::Path::bezierPath();
      (eastWest).moveToPoint(projectGroundPointX_y(-extent, offset));
      (eastWest).lineToPoint(projectGroundPointX_y(extent, offset));
      (eastWest).setLineWidth(axis ? 0.75 : 0.45);
      (eastWest).stroke();
    }
    drawing::drawAmbisonicOrientationGuides([&](double x, double y, double z) {
      return projectWorld_depth(
          {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)},
          nullptr);
    });

    const uint32_t count = std::min<uint32_t>(
        _plugin->guiEntityCount.load(std::memory_order_relaxed),
        s3g::kAmbiHorizonMaxEntities);
    for (uint32_t index = 0u; index < count; ++index) {
      double depth = 0.0;
      const drawing::Point point =
          projectWorld_depth(entityWorld(index), &depth);
      const float energy =
          _plugin->guiEnergy[index].load(std::memory_order_relaxed);
      const uint32_t layer =
          _plugin->guiLayer[index].load(std::memory_order_relaxed);
      const double size = std::clamp<double>(3.0 + energy * 90.0, 3.0, 10.0);
      const double shade =
          std::clamp<double>(0.34 + energy * 10.0 + depth * 0.06, 0.30, 0.86);
      drawing::setFill(drawing::calibratedWhite(shade, 0.90));
      if (layer ==
          static_cast<uint32_t>(s3g::AmbiHorizonLayer::HorizonSignal)) {
        drawing::Path diamond = drawing::Path::bezierPath();
        (diamond).moveToPoint(drawing::makePoint(point.x, point.y - size));
        (diamond).lineToPoint(drawing::makePoint(point.x + size, point.y));
        (diamond).lineToPoint(drawing::makePoint(point.x, point.y + size));
        (diamond).lineToPoint(drawing::makePoint(point.x - size, point.y));
        (diamond).closePath();
        (diamond).fill();
      } else if (layer ==
                 static_cast<uint32_t>(s3g::AmbiHorizonLayer::LocalFloor)) {
        (drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
             point.x - size, point.y - size, size * 2.0, size * 2.0)))
            .fill();
      } else {
        drawing::rectFill(
            drawing::makeRect(point.x - size, point.y - 1.5, size * 2.0, 3.0));
      }
    }
    graphics.restore();
    drawing::drawAtPoint_withAttributes(
        "CIRCLE LOCAL FLOOR    BAR LANDSCAPE BODY    DIAMOND DISTANT SIGNAL",
        drawing::makePoint(field.origin.x + 9.0, drawing::maxY(field) - 19.0),
        valueAttrs);
    drawing::String readout = drawing::stringFormat(
        "%.2f KM    %u ENTITIES    %@ GROUND    ACN/SN3D", _snapshot.rangeKm,
        _snapshot.entities,
        drawing::stringFromUtf8(kGroundNames[std::min<uint32_t>(
            static_cast<uint32_t>(_snapshot.ground), 4u)]));
    drawing::drawAtPoint_withAttributes(
        readout, drawing::makePoint(field.origin.x + 9.0, field.origin.y + 8.0),
        valueAttrs);
  }

  void drawSlider_param_value_attrs_valueAttrs_style(
      drawing::String name, clap_id param, double value, drawing::Attrs attrs,
      drawing::Attrs valueAttrs, const drawing::Style &style) {
    GuiSliderSpec spec{};
    if (!guiSliderSpec(param, _snapshot.ecology, spec))
      return;
    char display[64]{};
    paramsValueToText(nullptr, param, value, display, sizeof(display));
    const double panelX = spec.panel->frame.x;
    drawing::drawSlider(
        name, drawing::stringFromUtf8(display), sliderNorm(spec, value),
        layout::rowY(*spec.panel, spec.row), attrs, valueAttrs, style,
        panelX + 16.0, panelX + 108.0, panelX + 196.0, 82.0);
  }

  void drawMenu_value_panel_row_attrs_valueAttrs_style(
      drawing::String name, drawing::String value, const layout::Panel &panel,
      uint32_t row, drawing::Attrs attrs, drawing::Attrs valueAttrs,
      const drawing::Style &style) {
    drawing::drawMenu(name, value, layout::rowY(panel, row), attrs, valueAttrs,
                      style, panel.frame.x + 16.0, panel.frame.x + 108.0,
                      124.0);
  }

  void drawPanels_valueAttrs_style(drawing::Attrs attrs,
                                   drawing::Attrs valueAttrs,
                                   const drawing::Style &style) {
    for (const auto &panel : kFirstColumnPanels)
      drawing::drawPanelFrame(panel, style);
    for (const auto &panel : kSecondColumnPanels)
      drawing::drawPanelFrame(panel, style);
    drawing::drawPanelHeader("OUTPUT", true, kOutputPanel, attrs, style);
    drawing::drawPanelHeader("SOURCE / ECOLOGY", true, kEcologyPanel, attrs,
                             style);
    drawing::String generatorTitle = drawing::stringFormat(
        "%s GENERATORS", kEcologyNames[std::min<uint32_t>(
                             static_cast<uint32_t>(_snapshot.ecology), 8u)]);
    drawing::drawPanelHeader(generatorTitle, true, kGeneratorsPanel, attrs,
                             style);
    drawing::drawPanelHeader("ENVIRONMENTAL SCORE", true, kScorePanel, attrs,
                             style);
    drawing::drawPanelHeader("IDENTITY", true, kIdentityPanel, attrs, style);
    drawing::drawPanelHeader("HORIZON", true, kHorizonPanel, attrs, style);
    drawing::drawPanelHeader("ATMOSPHERE", true, kAtmospherePanel, attrs,
                             style);
    drawing::drawPanelHeader("FIELD LISTENER", true, kListenerPanel, attrs,
                             style);
    for (uint32_t lobe = 0u; lobe < s3g::kAmbiFieldListenerMaxLobes; ++lobe) {
      const float envelope =
          _plugin->guiListenerEnvelope[lobe].load(std::memory_order_relaxed);
      const double level =
          std::clamp<double>(envelope / (envelope + 0.012f), 0.0, 1.0);
      drawing::setFill(drawing::calibratedWhite(0.28 + level * 0.62, 0.92));
      drawing::rectFill(drawing::makeRect(
          kListenerPanel.frame.x + kListenerPanel.frame.width - 62.0 +
              static_cast<double>(lobe) * 6.0,
          kListenerPanel.frame.y + 8.0, 3.0, 6.0));
    }

    drawSlider_param_value_attrs_valueAttrs_style("OUT", kOutputParamId,
                                                  _snapshot.outputGainDb, attrs,
                                                  valueAttrs, style);
    drawMenu_value_panel_row_attrs_valueAttrs_style(
        "ORDER", drawing::stringFormat("%uOA", _snapshot.order), kOutputPanel,
        1u, attrs, valueAttrs, style);
    drawMenu_value_panel_row_attrs_valueAttrs_style(
        "ECOLOGY",
        drawing::stringFromUtf8(kEcologyNames[std::min<uint32_t>(
            static_cast<uint32_t>(_snapshot.ecology), 8u)]),
        kEcologyPanel, 1u, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style("VOICES", kEntitiesParamId,
                                                  _snapshot.entities, attrs,
                                                  valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style("ACTIVITY", kActivityParamId,
                                                  _snapshot.activity, attrs,
                                                  valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "OCCUPANCY", kOccupancyParamId, _snapshot.occupancy, attrs, valueAttrs,
        style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "PACE", kPaceParamId, _snapshot.pace, attrs, valueAttrs, style);
    const auto &generatorControls = generatorPanelControls(_snapshot.ecology);
    for (const auto &control : generatorControls.rows) {
      if (control.id == CLAP_INVALID_ID)
        continue;
      drawSlider_param_value_attrs_valueAttrs_style(
          drawing::stringFromUtf8(control.label), control.id,
          valueFromParams(_snapshot, control.id), attrs, valueAttrs, style);
    }
    drawSlider_param_value_attrs_valueAttrs_style(
        "MEMORY", kMemoryParamId, _snapshot.memory, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style("CASCADE", kCascadeParamId,
                                                  _snapshot.cascade, attrs,
                                                  valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style("SIGNALS", kSignalsParamId,
                                                  _snapshot.signals, attrs,
                                                  valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style("LANDSCAPE", kBedParamId,
                                                  _snapshot.horizonBed, attrs,
                                                  valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style("LOCAL FLOOR", kFloorParamId,
                                                  _snapshot.localFloor, attrs,
                                                  valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "SEED", kSeedParamId, _snapshot.seed, attrs, valueAttrs, style);

    drawSlider_param_value_attrs_valueAttrs_style(
        "RANGE", kRangeParamId, _snapshot.rangeKm, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style("AZIMUTH", kAzimuthParamId,
                                                  _snapshot.azimuthDeg, attrs,
                                                  valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "ELEVATION", kElevationParamId, _snapshot.elevationDeg, attrs,
        valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "ARC", kArcParamId, _snapshot.arcDeg, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "DETAIL", kDetailParamId, _snapshot.detail, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "EDGE", kEdgeParamId, _snapshot.edgeDb, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "AIR LOSS", kAirParamId, _snapshot.air, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style("AIR NOISE", kAirNoiseParamId,
                                                  _snapshot.airNoise, attrs,
                                                  valueAttrs, style);
    drawMenu_value_panel_row_attrs_valueAttrs_style(
        "GROUND",
        drawing::stringFromUtf8(kGroundNames[std::min<uint32_t>(
            static_cast<uint32_t>(_snapshot.ground), 4u)]),
        kAtmospherePanel, 2u, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style("TERRAIN", kTerrainParamId,
                                                  _snapshot.terrain, attrs,
                                                  valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "CARRY", kCarryParamId, _snapshot.carry, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "TURBULENCE", kTurbulenceParamId, _snapshot.turbulence, attrs,
        valueAttrs, style);
    drawMenu_value_panel_row_attrs_valueAttrs_style(
        "LISTEN",
        drawing::stringFromUtf8(kFieldListenNames[std::min<uint32_t>(
            static_cast<uint32_t>(_snapshot.fieldListenMode), 3u)]),
        kListenerPanel, 0u, attrs, valueAttrs, style);
    drawSlider_param_value_attrs_valueAttrs_style(
        "AMOUNT", kFieldListenAmountParamId, _snapshot.fieldListenAmount, attrs,
        valueAttrs, style);
    drawMenu_value_panel_row_attrs_valueAttrs_style(
        "RESPONSE",
        drawing::stringFromUtf8(kFieldListenResponseNames[std::min<uint32_t>(
            static_cast<uint32_t>(_snapshot.fieldListenResponse), 3u)]),
        kListenerPanel, 2u, attrs, valueAttrs, style);
  }

  drawing::String presetName() {
    if (_presetSnapshot == kRandomPresetIndex)
      return "RANDOM";
    return drawing::stringFromUtf8(
        s3g::kAmbiHorizonPresetInfo
            [std::min<uint32_t>(_presetSnapshot,
                                s3g::kAmbiHorizonFactoryPresetCount - 1u)]
                .name);
  }

  void openMenu(int menu) {
    _openMenu = menu;
    _hoverMenuItem = -1;
    drawing::Rect anchor = drawing::Rect{};
    if (menu == 1) {
      anchor = presetMenuRect();
      _menuItemCount = s3g::kAmbiHorizonFactoryPresetCount;
    } else if (menu == 2) {
      anchor = drawing::cocoaRect(layout::menuBoxRect(kOutputPanel, 1u));
      anchor.origin.x = kOutputPanel.frame.x + 108.0;
      anchor.size.width = 124.0;
      _menuItemCount = 7u;
    } else if (menu == 3) {
      anchor = drawing::cocoaRect(layout::menuBoxRect(kEcologyPanel, 1u));
      anchor.origin.x = kEcologyPanel.frame.x + 108.0;
      anchor.size.width = 124.0;
      _menuItemCount = static_cast<uint32_t>(std::size(kEcologyNames));
    } else if (menu == 4) {
      anchor = drawing::cocoaRect(layout::menuBoxRect(kAtmospherePanel, 2u));
      anchor.origin.x = kAtmospherePanel.frame.x + 108.0;
      anchor.size.width = 124.0;
      _menuItemCount = 5u;
    } else if (menu == 5) {
      anchor = drawing::cocoaRect(layout::menuBoxRect(kListenerPanel, 0u));
      anchor.origin.x = kListenerPanel.frame.x + 108.0;
      anchor.size.width = 124.0;
      _menuItemCount = 4u;
    } else {
      anchor = drawing::cocoaRect(layout::menuBoxRect(kListenerPanel, 2u));
      anchor.origin.x = kListenerPanel.frame.x + 108.0;
      anchor.size.width = 124.0;
      _menuItemCount = 4u;
    }
    _openMenuRect = drawing::makeRect(
        anchor.origin.x, drawing::maxY(anchor) + 2.0,
        menu == 1 ? 230.0 : anchor.size.width, 21.0 * _menuItemCount);
    setNeedsDisplay(true);
  }

  void drawOpenMenu_style(drawing::Attrs attrs, const drawing::Style &style) {
    if (_openMenu == 0)
      return;
    static drawing::String presetItems[s3g::kAmbiHorizonFactoryPresetCount]{};
    static drawing::String orderItems[] = {"1OA", "2OA", "3OA", "4OA",
                                           "5OA", "6OA", "7OA"};
    static drawing::String ecologyItems[] = {"MIXED",   "RURAL",      "TRAFFIC",
                                             "CITY",    "INDUSTRIAL", "WATER",
                                             "WEATHER", "AIRPORT",    "COAST"};
    static drawing::String groundItems[] = {"WATER", "HARD", "MIXED", "GRASS",
                                            "FOREST"};
    static drawing::String listenItems[] = {"OFF", "FOLLOW", "COUNTER",
                                            "BALANCE"};
    static drawing::String responseItems[] = {"REACH", "GLIMPSE", "SETTLE",
                                              "DISTANCE"};
    for (uint32_t index = 0u; index < s3g::kAmbiHorizonFactoryPresetCount;
         ++index) {
      if (presetItems[index].empty())
        presetItems[index] =
            drawing::stringFromUtf8(s3g::kAmbiHorizonPresetInfo[index].name);
    }
    drawing::String const *items = presetItems;
    int selected = static_cast<int>(_presetSnapshot);
    if (_openMenu == 2) {
      items = orderItems;
      selected = static_cast<int>(_snapshot.order) - 1;
    } else if (_openMenu == 3) {
      items = ecologyItems;
      selected = static_cast<int>(_snapshot.ecology);
    } else if (_openMenu == 4) {
      items = groundItems;
      selected = static_cast<int>(_snapshot.ground);
    } else if (_openMenu == 5) {
      items = listenItems;
      selected = static_cast<int>(_snapshot.fieldListenMode);
    } else if (_openMenu == 6) {
      items = responseItems;
      selected = static_cast<int>(_snapshot.fieldListenResponse);
    }
    drawing::drawDropdownMenu(_openMenuRect, 21.0, items, _menuItemCount,
                              selected, _hoverMenuItem, attrs, style);
  }

  void randomize() {
    uint32_t rng = _snapshot.seed ^ 0x68bc21ebu;
    auto params = s3g::ambiHorizonSafeRandomParams(_snapshot, rng);
    queueParams(*_plugin, params, kRandomPresetIndex);
    _snapshot = params;
    _presetSnapshot = kRandomPresetIndex;
  }

  void drawOriginal(drawing::Rect dirtyRect) {
    (void)dirtyRect;
    if (!_plugin)
      return;
    refreshSnapshot();
    const auto style = drawing::softTextStyle();
    drawing::setFill(style.bg);
    drawing::rectFill(bounds());
    drawing::Attrs attrs = drawing::softLabelAttrs();
    drawing::Attrs valueAttrs = drawing::softValueAttrs();
    drawing::drawEncoderTitleBand(
        "s3g AMBI ENCODER HORIZON", presetName(),
        drawing::peakDbText(
            _plugin->outputPeak.load(std::memory_order_relaxed)),
        drawing::encoderTitleBand(kGuiWidth, kGuiHeight),
        drawing::softTitleAttrs(), attrs, valueAttrs, style);
    drawField_valueAttrs_style(attrs, valueAttrs, style);
    drawPanels_valueAttrs_style(attrs, valueAttrs, style);
    drawOpenMenu_style(valueAttrs, style);
  }

  void setParam_fromPoint(clap_id param, drawing::Point point) {
    GuiSliderSpec spec{};
    if (!guiSliderSpec(param, _snapshot.ecology, spec))
      return;
    const double value = sliderValue(spec, point);
    assignParam(_snapshot, param, value);
    (void)editValue(param, value);
    setNeedsDisplay(true);
  }

  void mouseDown(drawing::Event *event) {
    refreshSnapshot();
    const drawing::Point point =
        convertPoint_fromView((event)->locationInWindow(), nullptr);
    if (_openMenu != 0) {
      const int hit =
          drawing::dropdownHitIndex(point, _openMenuRect, 21.0, _menuItemCount);
      if (hit >= 0) {
        if (_openMenu == 1) {
          const uint32_t order = _snapshot.order;
          const float output = _snapshot.outputGainDb;
          const auto listenMode = _snapshot.fieldListenMode;
          const float listenAmount = _snapshot.fieldListenAmount;
          const auto listenResponse = _snapshot.fieldListenResponse;
          _snapshot = s3g::ambiHorizonFactoryPreset(static_cast<uint32_t>(hit));
          _snapshot.order = order;
          _snapshot.outputGainDb = output;
          _snapshot.fieldListenMode = listenMode;
          _snapshot.fieldListenAmount = listenAmount;
          _snapshot.fieldListenResponse = listenResponse;
          _presetSnapshot = static_cast<uint32_t>(hit);
          (void)editValue(kPresetParamId, hit);
        } else if (_openMenu == 2) {
          _snapshot.order = static_cast<uint32_t>(hit + 1);
          (void)editValue(kOrderParamId, hit + 1);
        } else if (_openMenu == 3) {
          _snapshot.ecology = static_cast<s3g::AmbiHorizonEcology>(hit);
          (void)editValue(kEcologyParamId, hit);
        } else if (_openMenu == 4) {
          _snapshot.ground = static_cast<s3g::AmbiHorizonGround>(hit);
          (void)editValue(kGroundParamId, hit);
        } else if (_openMenu == 5) {
          _snapshot.fieldListenMode =
              static_cast<s3g::AmbiFieldListenMode>(hit);
          (void)editValue(kFieldListenModeParamId, hit);
        } else {
          _snapshot.fieldListenResponse =
              static_cast<s3g::AmbiFieldListenerResponse>(hit);
          (void)editValue(kFieldListenResponseParamId, hit);
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
    if (drawing::contains(point, loadPresetButtonRect())) {
      loadCustomPreset();
      return;
    }
    if (drawing::contains(point, savePresetButtonRect())) {
      saveCustomPreset();
      return;
    }
    if (drawing::contains(point, randomizeButtonRect())) {
      randomize();
      return;
    }

    drawing::Rect orderRect =
        drawing::makeRect(kOutputPanel.frame.x + 108.0,
                          layout::rowY(kOutputPanel, 1u) - 1.0, 124.0, 15.0);
    drawing::Rect ecologyRect =
        drawing::makeRect(kEcologyPanel.frame.x + 108.0,
                          layout::rowY(kEcologyPanel, 1u) - 1.0, 124.0, 15.0);
    drawing::Rect groundRect = drawing::makeRect(
        kAtmospherePanel.frame.x + 108.0,
        layout::rowY(kAtmospherePanel, 2u) - 1.0, 124.0, 15.0);
    drawing::Rect listenRect =
        drawing::makeRect(kListenerPanel.frame.x + 108.0,
                          layout::rowY(kListenerPanel, 0u) - 1.0, 124.0, 15.0);
    drawing::Rect responseRect =
        drawing::makeRect(kListenerPanel.frame.x + 108.0,
                          layout::rowY(kListenerPanel, 2u) - 1.0, 124.0, 15.0);
    if (drawing::contains(point, orderRect)) {
      openMenu(2);
      return;
    }
    if (drawing::contains(point, ecologyRect)) {
      openMenu(3);
      return;
    }
    if (drawing::contains(point, groundRect)) {
      openMenu(4);
      return;
    }
    if (drawing::contains(point, listenRect)) {
      openMenu(5);
      return;
    }
    if (drawing::contains(point, responseRect)) {
      openMenu(6);
      return;
    }

    const drawing::Rect fieldPanel = fieldPanelRect();
    if (drawing::contains(point, fieldPanel)) {
      for (int index = 0; index < 2; ++index) {
        if (drawing::contains(point, zoomButtonRect(index))) {
          _viewZoom =
              std::clamp(_viewZoom + (index == 0 ? -0.15 : 0.15), 0.55, 2.20);
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
      const drawing::Rect hit =
          drawing::cocoaRect(layout::sliderHitRect(*spec.panel, spec.row));
      if (!drawing::contains(point, hit))
        continue;
      double defaultValue = 0.0;
      if (drawing::sliderDoubleClickDefault(event, &_plugin->plugin, spec.id,
                                            &defaultValue)) {
        assignParam(_snapshot, spec.id, defaultValue);
        (void)editValue(spec.id, defaultValue);
        setNeedsDisplay(true);
        return;
      }
      _dragParam = static_cast<int>(spec.id);
      setParam_fromPoint(spec.id, point);
      return;
    }
    const auto &generatorControls = generatorPanelControls(_snapshot.ecology);
    for (uint32_t row = 0u; row < generatorControls.rows.size(); ++row) {
      const clap_id id = generatorControls.rows[row].id;
      if (id == CLAP_INVALID_ID)
        continue;
      const GuiSliderSpec spec{id, &kGeneratorsPanel, row};
      const drawing::Rect hit =
          drawing::cocoaRect(layout::sliderHitRect(*spec.panel, spec.row));
      if (!drawing::contains(point, hit))
        continue;
      double defaultValue = 0.0;
      if (drawing::sliderDoubleClickDefault(event, &_plugin->plugin, id,
                                            &defaultValue)) {
        assignParam(_snapshot, id, defaultValue);
        (void)editValue(id, defaultValue);
        setNeedsDisplay(true);
        return;
      }
      _dragParam = static_cast<int>(id);
      setParam_fromPoint(id, point);
      return;
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
      setNeedsDisplay(true);
    } else if (_dragParam != 0) {
      setParam_fromPoint(static_cast<clap_id>(_dragParam), point);
    }
  }

  void mouseUp(drawing::Event *event) {
    (void)event;
    _dragParam = 0;
    _dragView = false;
  }

  void mouseMoved(drawing::Event *event) {
    if (_openMenu == 0)
      return;
    const drawing::Point point =
        convertPoint_fromView((event)->locationInWindow(), nullptr);
    const int hover =
        drawing::dropdownHitIndex(point, _openMenuRect, 21.0, _menuItemCount);
    if (hover != _hoverMenuItem) {
      _hoverMenuItem = hover;
      setNeedsDisplay(true);
    }
  }
};
} // namespace score_encoder_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new score_encoder_canvas::Editor(p);
}
#include "s3g_clap_canvas_gui.inc"
