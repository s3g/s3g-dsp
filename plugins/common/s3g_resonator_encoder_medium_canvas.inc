// Literal Cocoa adaptation: retain geometry, graphics, controls and hit maps.
namespace resonator_encoder_canvas {
namespace drawing = s3g::portable_gui::resonator_drawing;
namespace foundation = s3g::portable_gui::foundation;
using Kind = s3g::clap_gui::ParamEventKind;
constexpr clap_id kFactoryPresetMenuId = 0x7ffffff0u;

drawing::Rect fieldPanelRect() {
  return drawing::makeRect(16.0, 50.0, 560.0, 614.0);
}

drawing::Rect outputPanelRect() {
  return drawing::makeRect(594.0, 50.0, 310.0, 116.0);
}

drawing::Rect mediumPanelRect() {
  return drawing::makeRect(594.0, 180.0, 310.0, 230.0);
}

drawing::Rect excitationPanelRect() {
  return drawing::makeRect(594.0, 424.0, 310.0, 240.0);
}

double rowY(drawing::Rect panel, uint32_t row) {
  return panel.origin.y + s3g::gui_layout::kStandardMetrics.firstRowOffset +
         static_cast<double>(row) * s3g::gui_layout::kStandardMetrics.rowPitch;
}

drawing::Rect rowHitRect(drawing::Rect panel, uint32_t row) {
  return drawing::makeRect(panel.origin.x + 8.0, rowY(panel, row) - 8.0,
                           panel.size.width - 16.0,
                           s3g::gui_layout::kStandardMetrics.hitHeight);
}

drawing::Rect menuBoxRect(drawing::Rect panel, uint32_t row) {
  return drawing::makeRect(panel.origin.x + 102.0, rowY(panel, row) - 1.0,
                           panel.size.width - 116.0, 15.0);
}

drawing::Rect fieldInteractionRect() {
  const drawing::Rect panel = fieldPanelRect();
  return drawing::makeRect(panel.origin.x + 8.0, panel.origin.y + 28.0,
                           panel.size.width - 16.0, panel.size.height - 66.0);
}

drawing::Rect zoomButtonRect(uint32_t index) {
  const drawing::Rect panel = fieldPanelRect();
  const drawing::Rect firstCamera =
      drawing::topologyProcessorCameraButtonRect(panel, 0u);
  constexpr double width = 22.0;
  constexpr double gap = 4.0;
  constexpr double cameraGap = 12.0;
  return drawing::makeRect(firstCamera.origin.x - cameraGap - 2.0 * width -
                               gap + static_cast<double>(index) * (width + gap),
                           panel.origin.y + 3.0, width, 15.0);
}

drawing::Rect excitationPageButtonRect(uint32_t index) {
  const drawing::Rect panel = excitationPanelRect();
  constexpr double width = 48.0;
  constexpr double gap = 4.0;
  constexpr double right = 8.0;
  constexpr double total = 3.0 * width + 2.0 * gap;
  return drawing::makeRect(drawing::maxX(panel) - right - total +
                               static_cast<double>(index) * (width + gap),
                           panel.origin.y + 3.0, width, 15.0);
}

drawing::Point projectedNodePoint(uint32_t node, float viewAzimuthDeg,
                                  float viewElevationDeg, float viewZoom) {
  const s3g::Vec3 direction{
      (node & 1u) != 0u ? 1.0f : -1.0f,
      (node & 2u) != 0u ? 1.0f : -1.0f,
      (node & 4u) != 0u ? 1.0f : -1.0f,
  };
  const auto projection =
      s3g::projectAedDirection(direction, viewAzimuthDeg, viewElevationDeg);
  const double scale = 128.0 * std::clamp<double>(viewZoom, 0.55, 1.55);
  return drawing::makePoint(
      fieldPanelRect().origin.x + fieldPanelRect().size.width * 0.50 +
          projection.horizontal * scale,
      fieldPanelRect().origin.y + fieldPanelRect().size.height * 0.52 -
          projection.vertical * scale);
}

uint32_t edgeFirst(uint32_t edge) {
  constexpr std::array<uint32_t, kEdgeCount> first{
      {0u, 0u, 0u, 1u, 1u, 2u, 2u, 3u, 4u, 4u, 5u, 6u}};
  return first[edge];
}

uint32_t edgeSecond(uint32_t edge) {
  constexpr std::array<uint32_t, kEdgeCount> second{
      {1u, 2u, 4u, 3u, 5u, 3u, 6u, 7u, 5u, 6u, 7u, 7u}};
  return second[edge];
}

drawing::String displayText(clap_id id, double value) {
  char text[64]{};
  formatParamValue(id, value, text, sizeof(text));
  return drawing::stringFromUtf8(text);
}

drawing::String midiNoteName(int32_t key) {
  if (key < 0 || key > 127)
    return "";
  static drawing::String names[12] = {"C",  "C#", "D",  "D#", "E",  "F",
                                      "F#", "G",  "G#", "A",  "A#", "B"};
  return drawing::stringFormat("%@%d", names[key % 12], key / 12 - 1);
}

void drawControlRow(Plugin &p, clap_id id, drawing::String shortName,
                    drawing::Rect panel, uint32_t row, bool menu,
                    drawing::Attrs labels, const drawing::Style &style) {
  const auto *spec = paramSpec(id);
  if (!spec)
    return;
  const double value = publishedParamValue(p, id);
  const double y = rowY(panel, row);
  if (menu) {
    drawing::drawMenu(shortName, displayText(id, value), y, labels,
                      drawing::softValueAttrs(), style, panel.origin.x + 14.0,
                      panel.origin.x + 102.0, panel.size.width - 116.0);
  } else {
    drawing::drawSlider(shortName, displayText(id, value),
                        static_cast<double>(normalizedParamValue(*spec, value)),
                        y, labels, drawing::softValueAttrs(), style,
                        panel.origin.x + 14.0, panel.origin.x + 102.0,
                        panel.origin.x + panel.size.width - 64.0,
                        panel.size.width - 178.0, 52.0);
  }
}

class Editor final : public s3g::portable_gui::routing::Canvas {
public:
  Plugin *_plugin;
  clap_id _dragParam;
  clap_id _openMenu;
  int _hoverMenuItem;
  uint32_t _menuItemCount;
  int _viewMode;
  double _viewAzDeg;
  double _viewElDeg;
  double _viewZoom;
  int _excitationPage;
  bool _dragView;
  drawing::Point _lastDragPoint;
  int _factoryPresetIndex;
  char _titlePresetName[64];

#include "s3g_resonator_encoder_editor.inc"
  Editor(Plugin &instance) : Canvas(kGuiWidth, kGuiHeight) {
    auto *plugin = &instance;

    _plugin = plugin;
    _dragParam = CLAP_INVALID_ID;
    _openMenu = CLAP_INVALID_ID;
    _hoverMenuItem = -1;
    _menuItemCount = 0u;
    auto *p = static_cast<Plugin *>(plugin);
    _viewMode = p ? p->guiViewMode : 0;
    _viewAzDeg = p ? p->guiViewAzDeg : 35.0;
    _viewElDeg = p ? p->guiViewElDeg : -34.0;
    _viewZoom = p ? p->guiViewZoom : 1.0;
    if (_viewMode == 0) {
      _viewAzDeg = 90.0;
      _viewElDeg = 0.0;
    } else if (_viewMode == 1) {
      _viewAzDeg = 90.0;
      _viewElDeg = -90.0;
    } else if (_viewMode == 2) {
      _viewAzDeg = 35.0;
      _viewElDeg = -34.0;
    }
    if (p && _viewMode >= 0 && _viewMode <= 2) {
      p->guiViewAzDeg = static_cast<float>(_viewAzDeg);
      p->guiViewElDeg = static_cast<float>(_viewElDeg);
    }
    _excitationPage = p ? p->guiExcitationPage : 0;
    _dragView = false;
    _lastDragPoint = drawing::Point{};
    _factoryPresetIndex = 0;
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "INIT");
  }
  ~Editor() override { stopRefresh(); }
  void storeViewState() {
    auto *p = static_cast<Plugin *>(_plugin);
    if (!p)
      return;
    p->guiViewMode = _viewMode;
    p->guiViewAzDeg = static_cast<float>(_viewAzDeg);
    p->guiViewElDeg = static_cast<float>(_viewElDeg);
    p->guiViewZoom = static_cast<float>(_viewZoom);
    p->guiExcitationPage = _excitationPage;
  }

  void setViewPreset(int mode) {
    _viewMode = std::clamp(mode, 0, 2);
    if (_viewMode == 0) {
      _viewAzDeg = 90.0;
      _viewElDeg = 0.0;
    } else if (_viewMode == 1) {
      _viewAzDeg = 90.0;
      _viewElDeg = -90.0;
    } else {
      _viewAzDeg = 35.0;
      _viewElDeg = -34.0;
    }
    storeViewState();
    setNeedsDisplay(true);
  }

  void applyFactoryPreset(int index) {
    auto *p = static_cast<Plugin *>(_plugin);
    if (!p)
      return;
    struct FactoryPreset {
      const char *name;
      double speed;
      double decay;
      double absorption;
      double nonlinearity;
      double radiation;
      double size;
      double node;
      double input;
      double strike;
      double selfExcitation;
      double selfRate;
      double exciter;
      double sustain;
      double character;
      double dispersion;
      uint32_t euclideanSteps;
      std::array<uint32_t, kNodeCount> pulses;
      std::array<uint32_t, kNodeCount> rotations;
    };
    static constexpr FactoryPreset presets[7]{
        {"INIT",
         343.0,
         2.5,
         0.22,
         0.00,
         0.20,
         0.50,
         1.0,
         1.00,
         1.00,
         0.32,
         0.80,
         0.0,
         0.00,
         0.50,
         0.00,
         16u,
         {{5u, 4u, 3u, 2u, 5u, 3u, 4u, 2u}},
         {{0u, 3u, 6u, 9u, 2u, 5u, 8u, 11u}}},
        {"RESONANT CUBE",
         210.0,
         8.0,
         0.07,
         0.16,
         0.08,
         0.72,
         1.0,
         0.65,
         0.75,
         0.22,
         0.35,
         0.0,
         0.00,
         0.45,
         0.08,
         16u,
         {{3u, 2u, 4u, 2u, 3u, 2u, 4u, 2u}},
         {{0u, 4u, 8u, 12u, 2u, 6u, 10u, 14u}}},
        {"BOWED GLASS",
         480.0,
         10.0,
         0.18,
         0.12,
         0.38,
         0.34,
         2.0,
         0.30,
         0.40,
         0.00,
         0.50,
         1.0,
         0.62,
         0.44,
         0.35,
         16u,
         {{4u, 3u, 2u, 4u, 3u, 2u, 4u, 2u}},
         {{0u, 3u, 7u, 11u, 2u, 6u, 10u, 14u}}},
        {"REED CHAMBER",
         343.0,
         6.0,
         0.16,
         0.08,
         0.66,
         0.28,
         3.0,
         0.20,
         0.30,
         0.00,
         0.50,
         2.0,
         0.58,
         0.52,
         0.08,
         16u,
         {{3u, 2u, 3u, 2u, 3u, 2u, 3u, 2u}},
         {{0u, 4u, 8u, 12u, 2u, 6u, 10u, 14u}}},
        {"AIR JET LATTICE",
         680.0,
         4.8,
         0.33,
         0.18,
         0.82,
         0.20,
         5.0,
         0.20,
         0.30,
         0.00,
         0.50,
         3.0,
         0.52,
         0.68,
         0.18,
         13u,
         {{5u, 3u, 4u, 2u, 5u, 3u, 4u, 2u}},
         {{0u, 2u, 5u, 8u, 11u, 1u, 4u, 9u}}},
        {"GLASS LATTICE",
         980.0,
         4.2,
         0.36,
         0.08,
         0.72,
         0.18,
         3.0,
         0.45,
         0.55,
         0.18,
         1.70,
         0.0,
         0.00,
         0.65,
         0.52,
         13u,
         {{5u, 4u, 3u, 6u, 2u, 5u, 4u, 3u}},
         {{0u, 2u, 5u, 8u, 11u, 1u, 4u, 9u}}},
        {"DARK DRONE",
         72.0,
         18.0,
         0.62,
         0.48,
         0.16,
         1.40,
         6.0,
         0.20,
         0.35,
         0.06,
         0.18,
         1.0,
         0.38,
         0.22,
         0.26,
         24u,
         {{3u, 2u, 1u, 3u, 2u, 1u, 2u, 1u}},
         {{0u, 5u, 11u, 17u, 3u, 9u, 15u, 21u}}},
    };
    index = std::clamp(index, 0, 6);
    const auto &preset = presets[index];
    const auto set = [&](clap_id id, double value) {
      beginEdit(id);
      editValue(id, value);
      endEdit(id);
    };
    set(kSpeedParamId, preset.speed);
    set(kDecayParamId, preset.decay);
    set(kAbsorptionParamId, preset.absorption);
    set(kNonlinearityParamId, preset.nonlinearity);
    set(kRadiationParamId, preset.radiation);
    set(kSizeParamId, preset.size);
    set(kActuatorNodeParamId, preset.node);
    set(kActuatorGainParamId, preset.input);
    set(kStrikeGainParamId, preset.strike);
    set(kSelfExcitationGainParamId, preset.selfExcitation);
    set(kSelfExcitationRateParamId, preset.selfRate);
    set(kExciterTypeParamId, preset.exciter);
    set(kSustainedExcitationParamId, preset.sustain);
    set(kExciterCharacterParamId, preset.character);
    set(kDispersionParamId, preset.dispersion);
    set(kEuclideanStepsParamId, preset.euclideanSteps);
    constexpr std::array<double, 7u> presetScales{
        {0.0, 0.0, 2.0, 1.0, 3.0, 4.0, 6.0}};
    constexpr std::array<double, 7u> presetNoteCounts{
        {1.0, 3.0, 5.0, 4.0, 6.0, 8.0, 2.0}};
    set(kSequencerScaleParamId, presetScales[index]);
    set(kSequencerNoteCountParamId, presetNoteCounts[index]);
    constexpr std::array<double, 7u> presetDirectivity{
        {0.55, 0.68, 0.82, 0.74, 0.86, 0.62, 0.46}};
    for (uint32_t node = 0u; node < kNodeCount; ++node) {
      set(euclideanPulsesParamId(node), preset.pulses[node]);
      set(euclideanRotationParamId(node), preset.rotations[node]);
      set(nodeDirectivityParamId(node), presetDirectivity[index]);
    }
    _factoryPresetIndex = index;
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                  preset.name);
    setNeedsDisplay(true);
  }

  drawing::Rect openMenuRect() {
    if (_openMenu == kFactoryPresetMenuId) {
      const drawing::Rect box = drawing::cocoaRect(
          drawing::encoderTitleBand(kGuiWidth, kGuiHeight).presetMenu);
      return drawing::makeRect(box.origin.x, drawing::maxY(box) + 2.0,
                               box.size.width, 18.0 * _menuItemCount);
    }
    if (_openMenu == kOrderParamId) {
      const drawing::Rect box = menuBoxRect(outputPanelRect(), 0u);
      return drawing::makeRect(box.origin.x, drawing::maxY(box) + 2.0,
                               box.size.width, 18.0 * _menuItemCount);
    }
    if (_openMenu == kActuatorNodeParamId) {
      const drawing::Rect box = menuBoxRect(excitationPanelRect(), 0u);
      return drawing::makeRect(box.origin.x, drawing::maxY(box) + 2.0,
                               box.size.width, 18.0 * _menuItemCount);
    }
    if (_openMenu == kExciterTypeParamId) {
      const drawing::Rect box =
          menuBoxRect(excitationPanelRect(), _excitationPage == 1 ? 0u : 1u);
      return drawing::makeRect(box.origin.x, drawing::maxY(box) + 2.0,
                               box.size.width, 18.0 * _menuItemCount);
    }
    if (_openMenu == kMidiModeParamId) {
      const drawing::Rect box = menuBoxRect(excitationPanelRect(), 0u);
      return drawing::makeRect(box.origin.x, drawing::maxY(box) + 2.0,
                               box.size.width, 18.0 * _menuItemCount);
    }
    if (_openMenu == kSequencerScaleParamId) {
      const drawing::Rect box = menuBoxRect(excitationPanelRect(), 6u);
      const double height = 18.0 * _menuItemCount;
      return drawing::makeRect(box.origin.x, box.origin.y - 2.0 - height,
                               box.size.width, height);
    }
    return drawing::Rect{};
  }

  void drawOpenMenu_style(drawing::Attrs attrs, const drawing::Style &style) {
    if (_openMenu == CLAP_INVALID_ID || _menuItemCount == 0u)
      return;
    drawing::String presetItems[7] = {
        "INIT",         "RESONANT CUBE",   "BOWED GLASS",
        "REED CHAMBER", "AIR JET LATTICE", "GLASS LATTICE",
        "DARK DRONE"};
    drawing::String orderItems[3] = {"1OA / 4CH", "2OA / 9CH", "3OA / 16CH"};
    drawing::String nodeItems[8] = {"NODE 1", "NODE 2", "NODE 3", "NODE 4",
                                    "NODE 5", "NODE 6", "NODE 7", "NODE 8"};
    drawing::String exciterItems[4] = {"PERCUSSIVE", "BOW", "REED", "AIR JET"};
    drawing::String midiItems[4] = {"STRIKE", "MONO NOTE", "POLY SEQ",
                                    "POLY RANDOM"};
    drawing::String scaleItems[8] = {
        "MAJOR",          "NATURAL MINOR",    "DORIAN",
        "MIXOLYDIAN",     "MAJOR PENTATONIC", "MINOR PENTATONIC",
        "HARMONIC MINOR", "CHROMATIC"};
    drawing::String const *items =
        _openMenu == kFactoryPresetMenuId
            ? presetItems
            : (_openMenu == kOrderParamId
                   ? orderItems
                   : (_openMenu == kExciterTypeParamId
                          ? exciterItems
                          : (_openMenu == kMidiModeParamId
                                 ? midiItems
                                 : (_openMenu == kSequencerScaleParamId
                                        ? scaleItems
                                        : nodeItems))));
    const int selected =
        _openMenu == kFactoryPresetMenuId
            ? _factoryPresetIndex
            : static_cast<int>(std::lround(publishedParamValue(
                  *static_cast<Plugin *>(_plugin), _openMenu))) -
                  ((_openMenu == kExciterTypeParamId ||
                    _openMenu == kMidiModeParamId ||
                    _openMenu == kSequencerScaleParamId)
                       ? 0
                       : 1);
    drawing::drawDropdownMenu(openMenuRect(), 18.0, items, _menuItemCount,
                              selected, _hoverMenuItem, attrs, style);
  }

  void drawOriginal(drawing::Rect dirty) {
    (void)dirty;
    auto *p = static_cast<Plugin *>(_plugin);
    drawing::Style style;
    drawing::setFill(style.bg);
    drawing::rectFill(bounds());
    drawing::Attrs labels = drawing::softLabelAttrs();
    drawing::Attrs values = drawing::softValueAttrs();
    drawing::Attrs title = drawing::softTitleAttrs();
    const float peak = p->outputPeak.load(std::memory_order_relaxed);
    const float guard = p->guardGain.load(std::memory_order_relaxed);
    drawing::String status =
        guard < 0.999f ? drawing::stringFormat(
                             "SAFE %+.1f dB",
                             20.0f * std::log10(std::max(guard, 0.000001f)))
                       : drawing::peakDbText(peak);
    drawing::drawEncoderTitleBand(
        "s3g AMBI ENCODER MEDIUM 16", drawing::stringFromUtf8(_titlePresetName),
        status, drawing::encoderTitleBand(kGuiWidth, kGuiHeight), title, labels,
        values, style);

    const auto drawPanel = [&](drawing::String name, drawing::Rect panel) {
      drawing::drawPanelFrame(panel.origin.x, panel.origin.y, panel.size.width,
                              panel.size.height, style);
      drawing::drawPanelHeader(name, true, panel.origin.x, panel.origin.y,
                               panel.size.width, 24.0, labels, style);
    };
    drawPanel("WAVEGUIDE FIELD", fieldPanelRect());
    drawPanel("OUTPUT", outputPanelRect());
    drawPanel("MEDIUM / STRUCTURE", mediumPanelRect());
    drawPanel("EXCITATION", excitationPanelRect());

    drawing::drawTopologyProcessorCameraButtons(fieldPanelRect(), _viewMode,
                                                values, style);
    drawing::String zoomLabels[2] = {"-", "+"};
    for (uint32_t index = 0u; index < 2u; ++index) {
      drawing::drawToolboxHeaderButton(zoomButtonRect(index), fieldPanelRect(),
                                       zoomLabels[index], false, values, style);
    }
    drawing::String excitationPages[3] = {"SOURCE", "SEQ", "MIDI"};
    for (uint32_t index = 0u; index < 3u; ++index) {
      drawing::drawToolboxHeaderButton(
          excitationPageButtonRect(index), excitationPanelRect(),
          excitationPages[index], _excitationPage == static_cast<int>(index),
          values, style);
    }

    drawing::drawAtPoint_withAttributes(
        "CLICK NODE TO STRIKE / DRAG EMPTY FIELD TO ORBIT",
        drawing::makePoint(fieldPanelRect().origin.x + 16.0,
                           fieldPanelRect().origin.y + 36.0),
        values);
    drawing::drawAtPoint_withAttributes(
        "WHITE RING = SELECTED   CYAN RING + NOTE = MIDI HELD   HALO = "
        "TRIGGERED",
        drawing::makePoint(fieldPanelRect().origin.x + 16.0,
                           fieldPanelRect().origin.y + 51.0),
        values);
    drawing::drawAtPoint_withAttributes(
        "CUBE EDGES ARE BIDIRECTIONAL THIRAN WAVEGUIDES",
        drawing::makePoint(fieldPanelRect().origin.x + 16.0,
                           fieldPanelRect().origin.y +
                               fieldPanelRect().size.height - 27.0),
        values);

    for (uint32_t edge = 0u; edge < kEdgeCount; ++edge) {
      const float energy =
          std::clamp(p->edgeEnergy[edge].load(std::memory_order_relaxed) * 1.8f,
                     0.0f, 1.0f);
      const drawing::Point first = projectedNodePoint(
          edgeFirst(edge), static_cast<float>(_viewAzDeg),
          static_cast<float>(_viewElDeg), static_cast<float>(_viewZoom));
      const drawing::Point second = projectedNodePoint(
          edgeSecond(edge), static_cast<float>(_viewAzDeg),
          static_cast<float>(_viewElDeg), static_cast<float>(_viewZoom));
      drawing::Path path = drawing::Path::bezierPath();
      (path).moveToPoint(first);
      (path).lineToPoint(second);
      (path).setLineWidth(1.0 + 4.0 * std::sqrt(energy));
      drawing::setStroke(drawing::calibratedWhite(0.28 + 0.62 * energy, 1.0));
      (path).stroke();
    }
    const uint32_t selected = static_cast<uint32_t>(
        std::clamp(std::lround(publishedParamValue(*p, kActuatorNodeParamId)),
                   1l, 8l) -
        1l);
    for (uint32_t node = 0u; node < kNodeCount; ++node) {
      const drawing::Point point = projectedNodePoint(
          node, static_cast<float>(_viewAzDeg), static_cast<float>(_viewElDeg),
          static_cast<float>(_viewZoom));
      const float energy =
          std::clamp(p->nodeEnergy[node].load(std::memory_order_relaxed) * 3.2f,
                     0.0f, 1.0f);
      const float strike = std::clamp(
          p->nodeStrikeActivity[node].load(std::memory_order_relaxed), 0.0f,
          1.0f);
      const float midi =
          std::clamp(p->midiNodeActivity[node].load(std::memory_order_relaxed),
                     0.0f, 1.0f);
      const int32_t midiKey =
          p->midiNodeKey[node].load(std::memory_order_relaxed);
      const float sounding = std::max(energy, strike);
      if (sounding > 0.001f) {
        const double radius = 15.0 + 30.0 * std::sqrt(sounding);
        drawing::Path halo = drawing::Path::bezierPathWithOvalInRect(
            drawing::makeRect(point.x - radius, point.y - radius, radius * 2.0,
                              radius * 2.0));
        drawing::setFill(
            drawing::calibratedRGB(0.42, 0.78, 0.86, 0.12 + 0.34 * sounding));
        (halo).fill();
      }
      if (midi > 0.001f) {
        const double glowRadius = 13.0 + 8.0 * std::sqrt(midi);
        drawing::Path midiGlow = drawing::Path::bezierPathWithOvalInRect(
            drawing::makeRect(point.x - glowRadius, point.y - glowRadius,
                              glowRadius * 2.0, glowRadius * 2.0));
        drawing::setFill(
            drawing::calibratedRGB(0.40, 0.88, 1.0, 0.15 + 0.22 * midi));
        (midiGlow).fill();
      }
      const bool isSelected = node == selected;
      const double radius = midi > 0.001f || sounding > 0.08f ? 8.0 : 6.0;
      drawing::Path body =
          drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
              point.x - radius, point.y - radius, radius * 2.0, radius * 2.0));
      VSTGUI::CColor bodyColor =
          midi > 0.001f
              ? drawing::calibratedRGB(0.72, 0.95, 1.0, 1.0)
              : (sounding > 0.02f
                     ? drawing::calibratedWhite(0.60 + 0.34 * sounding, 1.0)
                     : drawing::calibratedColor(0x777777));
      drawing::setFill(bodyColor);
      (body).fill();
      if (midi > 0.001f) {
        const double ringRadius = 12.0 + 3.0 * std::sqrt(midi);
        drawing::Path midiRing = drawing::Path::bezierPathWithOvalInRect(
            drawing::makeRect(point.x - ringRadius, point.y - ringRadius,
                              ringRadius * 2.0, ringRadius * 2.0));
        (midiRing).setLineWidth(2.2);
        drawing::setStroke(
            drawing::calibratedRGB(0.52, 0.92, 1.0, 0.72 + 0.28 * midi));
        (midiRing).stroke();
      }
      if (isSelected) {
        constexpr double selectedRadius = 10.0;
        drawing::Path selection =
            drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
                point.x - selectedRadius, point.y - selectedRadius,
                selectedRadius * 2.0, selectedRadius * 2.0));
        (selection).setLineWidth(1.4);
        drawing::setStroke(drawing::calibratedWhite(0.95, 0.95));
        (selection).stroke();
      }
      drawing::String nodeLabel =
          midiKey >= 0 ? drawing::stringFormat("N%u  %@", node + 1u,
                                               midiNoteName(midiKey))
                       : drawing::stringFormat("N%u", node + 1u);
      drawing::drawAtPoint_withAttributes(
          nodeLabel, drawing::makePoint(point.x + 10.0, point.y - 7.0), values);
    }

    drawControlRow(*p, kOrderParamId, "ORDER", outputPanelRect(), 0u, true,
                   labels, style);
    drawControlRow(*p, kOutputGainParamId, "OUT", outputPanelRect(), 1u, false,
                   labels, style);
    drawControlRow(*p, kSpeedParamId, "SPEED", mediumPanelRect(), 0u, false,
                   labels, style);
    drawControlRow(*p, kSizeParamId, "SIZE", mediumPanelRect(), 1u, false,
                   labels, style);
    drawControlRow(*p, kDecayParamId, "DECAY", mediumPanelRect(), 2u, false,
                   labels, style);
    drawControlRow(*p, kAbsorptionParamId, "ABSORB", mediumPanelRect(), 3u,
                   false, labels, style);
    drawControlRow(*p, kNonlinearityParamId, "NONLINEAR", mediumPanelRect(), 4u,
                   false, labels, style);
    drawControlRow(*p, kRadiationParamId, "RADIATE", mediumPanelRect(), 5u,
                   false, labels, style);
    drawControlRow(*p, kDispersionParamId, "DISPERSE", mediumPanelRect(), 6u,
                   false, labels, style);
    if (_excitationPage == 0) {
      drawControlRow(*p, kActuatorNodeParamId, "NODE", excitationPanelRect(),
                     0u, true, labels, style);
      drawControlRow(*p, kExciterTypeParamId, "EXCITER", excitationPanelRect(),
                     1u, true, labels, style);
      drawControlRow(*p, kSustainedExcitationParamId, "SUSTAIN",
                     excitationPanelRect(), 2u, false, labels, style);
      drawControlRow(*p, kExciterCharacterParamId, "CHARACTER",
                     excitationPanelRect(), 3u, false, labels, style);
      drawControlRow(*p, kActuatorGainParamId, "INPUT", excitationPanelRect(),
                     4u, false, labels, style);
      drawControlRow(*p, kStrikeGainParamId, "STRIKE", excitationPanelRect(),
                     5u, false, labels, style);
      drawControlRow(*p, nodeDirectivityParamId(selected), "MASK",
                     excitationPanelRect(), 6u, false, labels, style);
    } else if (_excitationPage == 1) {
      drawControlRow(*p, kExciterTypeParamId, "EXCITER", excitationPanelRect(),
                     0u, true, labels, style);
      drawControlRow(*p, kSelfExcitationGainParamId, "SELF",
                     excitationPanelRect(), 1u, false, labels, style);
      drawControlRow(*p, kSelfExcitationRateParamId, "RATE",
                     excitationPanelRect(), 2u, false, labels, style);
      drawControlRow(*p, kEuclideanStepsParamId, "STEPS", excitationPanelRect(),
                     3u, false, labels, style);
      drawControlRow(*p, euclideanPulsesParamId(selected), "PULSES",
                     excitationPanelRect(), 4u, false, labels, style);
      drawControlRow(*p, euclideanRotationParamId(selected), "ROTATE",
                     excitationPanelRect(), 5u, false, labels, style);
      drawControlRow(*p, kSequencerScaleParamId, "SCALE", excitationPanelRect(),
                     6u, true, labels, style);
      drawControlRow(*p, kSequencerNoteCountParamId, "NOTES",
                     excitationPanelRect(), 7u, false, labels, style);
    } else {
      drawControlRow(*p, kMidiModeParamId, "MODE", excitationPanelRect(), 0u,
                     true, labels, style);
      drawControlRow(*p, kMidiTransposeParamId, "TRANSPOSE",
                     excitationPanelRect(), 1u, false, labels, style);
      drawControlRow(*p, kMidiAttackParamId, "ATTACK", excitationPanelRect(),
                     2u, false, labels, style);
      drawControlRow(*p, kMidiReleaseParamId, "RELEASE", excitationPanelRect(),
                     3u, false, labels, style);
      drawControlRow(*p, kMidiVelocityParamId, "VELOCITY",
                     excitationPanelRect(), 4u, false, labels, style);
    }
    drawOpenMenu_style(values, style);
  }

  void updateSlider(drawing::Point point) {
    if (_dragParam == CLAP_INVALID_ID)
      return;
    auto *p = static_cast<Plugin *>(_plugin);
    const auto *spec = paramSpec(_dragParam);
    if (!spec)
      return;
    drawing::Rect panel = outputPanelRect();
    if (_dragParam == kSpeedParamId || _dragParam == kSizeParamId ||
        _dragParam == kDecayParamId || _dragParam == kAbsorptionParamId ||
        _dragParam == kNonlinearityParamId || _dragParam == kRadiationParamId ||
        _dragParam == kDispersionParamId) {
      panel = mediumPanelRect();
    } else if (_dragParam == kActuatorGainParamId ||
               _dragParam == kStrikeGainParamId ||
               _dragParam == kSelfExcitationGainParamId ||
               _dragParam == kSelfExcitationRateParamId ||
               _dragParam == kEuclideanStepsParamId ||
               _dragParam == kSustainedExcitationParamId ||
               _dragParam == kExciterCharacterParamId ||
               _dragParam == kMidiTransposeParamId ||
               _dragParam == kMidiAttackParamId ||
               _dragParam == kMidiReleaseParamId ||
               _dragParam == kMidiVelocityParamId ||
               _dragParam == kSequencerNoteCountParamId ||
               isEuclideanPulsesParam(_dragParam) ||
               isEuclideanRotationParam(_dragParam) ||
               isNodeDirectivityParam(_dragParam)) {
      panel = excitationPanelRect();
    }
    const double trackX = panel.origin.x + 102.0;
    const double trackWidth = panel.size.width - 178.0;
    const double normalized =
        std::clamp((point.x - trackX) / trackWidth, 0.0, 1.0);
    editValue(_dragParam, valueFromNormalized(*spec, normalized));
    setNeedsDisplay(true);
  }

  void mouseDown(drawing::Event *event) {
    const drawing::Point point =
        convertPoint_fromView((event)->locationInWindow(), nullptr);
    auto *p = static_cast<Plugin *>(_plugin);

    if (_openMenu != CLAP_INVALID_ID) {
      const int hit = drawing::dropdownHitIndex(point, openMenuRect(), 18.0,
                                                _menuItemCount);
      if (hit >= 0) {
        if (_openMenu == kFactoryPresetMenuId) {
          applyFactoryPreset(hit);
        } else {
          beginEdit(_openMenu);
          editValue(_openMenu, hit + ((_openMenu == kExciterTypeParamId ||
                                       _openMenu == kMidiModeParamId ||
                                       _openMenu == kSequencerScaleParamId)
                                          ? 0.0
                                          : 1.0));
          endEdit(_openMenu);
          _factoryPresetIndex = -1;
          std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                        "CUSTOM");
        }
      }
      _openMenu = CLAP_INVALID_ID;
      _hoverMenuItem = -1;
      _menuItemCount = 0u;
      setNeedsDisplay(true);
      return;
    }

    const auto titleBand = drawing::encoderTitleBand(kGuiWidth, kGuiHeight);
    if (drawing::contains(point, drawing::cocoaRect(titleBand.presetMenu))) {
      _openMenu = kFactoryPresetMenuId;
      _hoverMenuItem = -1;
      _menuItemCount = 7u;
      setNeedsDisplay(true);
      return;
    }
    if (drawing::contains(point, drawing::cocoaRect(titleBand.loadButton))) {
      presetDialog(false);
      return;
    }
    if (drawing::contains(point, drawing::cocoaRect(titleBand.saveButton))) {
      presetDialog(true);
      return;
    }
    if (drawing::contains(point, drawing::cocoaRect(titleBand.randomButton))) {
      const auto randomUnit = [] {
        return static_cast<double>(drawing::randomBits()) / 4294967295.0;
      };
      const auto set = [&](clap_id id, double value) {
        beginEdit(id);
        editValue(id, value);
        endEdit(id);
      };
      set(kSpeedParamId, 40.0 * std::pow(35.0, randomUnit()));
      set(kDecayParamId, 0.25 * std::pow(80.0, randomUnit()));
      set(kAbsorptionParamId, 0.04 + randomUnit() * 0.72);
      set(kNonlinearityParamId, randomUnit() * 0.58);
      set(kRadiationParamId, 0.04 + randomUnit() * 0.78);
      set(kSizeParamId, 0.05 * std::pow(36.0, randomUnit()));
      set(kActuatorNodeParamId,
          static_cast<double>(drawing::randomBounded(kNodeCount) + 1u));
      const uint32_t exciter = drawing::randomBounded(4u);
      set(kExciterTypeParamId, exciter);
      set(kSustainedExcitationParamId,
          exciter > 0u ? 0.18 + randomUnit() * 0.58 : 0.0);
      set(kExciterCharacterParamId, randomUnit());
      set(kDispersionParamId, randomUnit() * 0.68);
      set(kSelfExcitationGainParamId,
          exciter > 0u ? randomUnit() * 0.10 : 0.12 + randomUnit() * 0.42);
      set(kSelfExcitationRateParamId, 0.08 * std::pow(50.0, randomUnit()));
      const uint32_t euclideanSteps = 8u + drawing::randomBounded(17u);
      set(kEuclideanStepsParamId, euclideanSteps);
      set(kSequencerScaleParamId, drawing::randomBounded(8u));
      set(kSequencerNoteCountParamId, 1u + drawing::randomBounded(8u));
      for (uint32_t node = 0u; node < kNodeCount; ++node) {
        const uint32_t pulseRange = std::max<uint32_t>(1u, euclideanSteps / 2u);
        set(euclideanPulsesParamId(node),
            1u + drawing::randomBounded(pulseRange));
        set(euclideanRotationParamId(node),
            drawing::randomBounded(euclideanSteps));
        set(nodeDirectivityParamId(node), 0.18 + randomUnit() * 0.82);
      }
      _factoryPresetIndex = -1;
      std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "RANDOM");
      setNeedsDisplay(true);
      return;
    }

    for (uint32_t index = 0u; index < 2u; ++index) {
      if (drawing::contains(point, zoomButtonRect(index))) {
        _viewZoom = std::clamp<double>(_viewZoom + (index == 0u ? -0.15 : 0.15),
                                       0.55, 1.55);
        storeViewState();
        setNeedsDisplay(true);
        return;
      }
    }
    for (uint32_t index = 0u; index < 3u; ++index) {
      if (drawing::contains(point, drawing::topologyProcessorCameraButtonRect(
                                       fieldPanelRect(), index))) {
        setViewPreset(static_cast<int>(index));
        return;
      }
    }
    for (uint32_t index = 0u; index < 3u; ++index) {
      if (drawing::contains(point, excitationPageButtonRect(index))) {
        _excitationPage = static_cast<int>(index);
        storeViewState();
        setNeedsDisplay(true);
        return;
      }
    }

    for (uint32_t node = 0u; node < kNodeCount; ++node) {
      const drawing::Point center = projectedNodePoint(
          node, static_cast<float>(_viewAzDeg), static_cast<float>(_viewElDeg),
          static_cast<float>(_viewZoom));
      const double dx = point.x - center.x;
      const double dy = point.y - center.y;
      if (dx * dx + dy * dy <= 18.0 * 18.0) {
        beginEdit(kActuatorNodeParamId);
        editValue(kActuatorNodeParamId, node + 1.0);
        endEdit(kActuatorNodeParamId);
        queuePreviewStrike(*p, node);
        setNeedsDisplay(true);
        return;
      }
    }

    const auto openMenu = [&](clap_id id, uint32_t itemCount) {
      _openMenu = id;
      _hoverMenuItem = -1;
      _menuItemCount = itemCount;
      setNeedsDisplay(true);
    };
    if (drawing::contains(point, rowHitRect(outputPanelRect(), 0u))) {
      openMenu(kOrderParamId, 3u);
      return;
    }
    if (_excitationPage == 0 &&
        drawing::contains(point, rowHitRect(excitationPanelRect(), 0u))) {
      openMenu(kActuatorNodeParamId, 8u);
      return;
    }
    if (((_excitationPage == 0 &&
          drawing::contains(point, rowHitRect(excitationPanelRect(), 1u))) ||
         (_excitationPage == 1 &&
          drawing::contains(point, rowHitRect(excitationPanelRect(), 0u))))) {
      openMenu(kExciterTypeParamId, 4u);
      return;
    }
    if (_excitationPage == 2 &&
        drawing::contains(point, rowHitRect(excitationPanelRect(), 0u))) {
      openMenu(kMidiModeParamId, 4u);
      return;
    }
    if (_excitationPage == 1 &&
        drawing::contains(point, rowHitRect(excitationPanelRect(), 6u))) {
      openMenu(kSequencerScaleParamId, 8u);
      return;
    }
    if (drawing::contains(point, fieldInteractionRect())) {
      _dragView = true;
      _lastDragPoint = point;
      _viewMode = -1;
      storeViewState();
      return;
    }

    const uint32_t selectedNode = static_cast<uint32_t>(
        std::clamp(std::lround(publishedParamValue(*p, kActuatorNodeParamId)),
                   1l, 8l) -
        1l);
    struct HitRow {
      clap_id id;
      drawing::Rect panel;
      uint32_t row;
    };
    std::array<HitRow, 16u> rows{};
    uint32_t rowCount = 0u;
    const auto addRow = [&](clap_id id, drawing::Rect panel, uint32_t row) {
      if (rowCount < rows.size())
        rows[rowCount++] = {id, panel, row};
    };
    addRow(kOutputGainParamId, outputPanelRect(), 1u);
    addRow(kSpeedParamId, mediumPanelRect(), 0u);
    addRow(kSizeParamId, mediumPanelRect(), 1u);
    addRow(kDecayParamId, mediumPanelRect(), 2u);
    addRow(kAbsorptionParamId, mediumPanelRect(), 3u);
    addRow(kNonlinearityParamId, mediumPanelRect(), 4u);
    addRow(kRadiationParamId, mediumPanelRect(), 5u);
    addRow(kDispersionParamId, mediumPanelRect(), 6u);
    if (_excitationPage == 0) {
      addRow(kSustainedExcitationParamId, excitationPanelRect(), 2u);
      addRow(kExciterCharacterParamId, excitationPanelRect(), 3u);
      addRow(kActuatorGainParamId, excitationPanelRect(), 4u);
      addRow(kStrikeGainParamId, excitationPanelRect(), 5u);
      addRow(nodeDirectivityParamId(selectedNode), excitationPanelRect(), 6u);
    } else if (_excitationPage == 1) {
      addRow(kSelfExcitationGainParamId, excitationPanelRect(), 1u);
      addRow(kSelfExcitationRateParamId, excitationPanelRect(), 2u);
      addRow(kEuclideanStepsParamId, excitationPanelRect(), 3u);
      addRow(euclideanPulsesParamId(selectedNode), excitationPanelRect(), 4u);
      addRow(euclideanRotationParamId(selectedNode), excitationPanelRect(), 5u);
      addRow(kSequencerNoteCountParamId, excitationPanelRect(), 7u);
    } else {
      addRow(kMidiTransposeParamId, excitationPanelRect(), 1u);
      addRow(kMidiAttackParamId, excitationPanelRect(), 2u);
      addRow(kMidiReleaseParamId, excitationPanelRect(), 3u);
      addRow(kMidiVelocityParamId, excitationPanelRect(), 4u);
    }
    for (uint32_t index = 0u; index < rowCount; ++index) {
      const auto &row = rows[index];
      if (!drawing::contains(point, rowHitRect(row.panel, row.row)))
        continue;
      double defaultValue = 0.0;
      if (drawing::sliderDoubleClickDefault(event, &p->plugin, row.id,
                                            &defaultValue)) {
        beginEdit(row.id);
        editValue(row.id, defaultValue);
        endEdit(row.id);
        _dragParam = CLAP_INVALID_ID;
      } else {
        _dragParam = row.id;
        beginEdit(row.id);
        updateSlider(point);
      }
      return;
    }
  }

  void mouseDragged(drawing::Event *event) {
    const drawing::Point point =
        convertPoint_fromView((event)->locationInWindow(), nullptr);
    if (_dragView) {
      _viewAzDeg += (point.x - _lastDragPoint.x) * 0.35;
      _viewElDeg = std::clamp<double>(
          _viewElDeg + (point.y - _lastDragPoint.y) * 0.35, -85.0, 85.0);
      _viewMode = -1;
      _lastDragPoint = point;
      storeViewState();
      setNeedsDisplay(true);
      return;
    }
    if (_dragParam != CLAP_INVALID_ID) {
      updateSlider(point);
    }
  }

  void mouseUp(drawing::Event *event) {
    (void)event;
    if (_dragParam != CLAP_INVALID_ID) {
      endEdit(_dragParam);
    }
    _dragParam = CLAP_INVALID_ID;
    _dragView = false;
  }

  void mouseMoved(drawing::Event *event) {
    if (_openMenu == CLAP_INVALID_ID)
      return;
    const drawing::Point point =
        convertPoint_fromView((event)->locationInWindow(), nullptr);
    const int hover =
        drawing::dropdownHitIndex(point, openMenuRect(), 18.0, _menuItemCount);
    if (hover != _hoverMenuItem) {
      _hoverMenuItem = hover;
      setNeedsDisplay(true);
    }
  }
};
} // namespace resonator_encoder_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new resonator_encoder_canvas::Editor(p);
}
#include "s3g_clap_canvas_gui.inc"
