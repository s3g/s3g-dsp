// Literal Cocoa adaptation: retain geometry, graphics, controls and hit maps.
namespace resonator_encoder_canvas {
namespace drawing = s3g::portable_gui::resonator_drawing;
namespace foundation = s3g::portable_gui::foundation;
using Kind = s3g::clap_gui::ParamEventKind;
constexpr clap_id kFactoryPresetMenuId = 0x7ffffff0u;

constexpr auto membraneTitleBand() {
  auto band = s3g::gui_layout::encoderTitleBand({kGuiWidth, kGuiHeight});
  // Reserve the full name at the shared title font size, including Windows.
  // Move the existing preset/action group together; keep every hit box aligned.
  band.presetLabelX += 40.;
  band.presetMenu.x += 40.;
  band.loadButton.x += 40.;
  band.saveButton.x += 40.;
  band.randomButton.x += 40.;
  return band;
}

struct MembraneUiRow {
  clap_id id;
  const char *label;
  double y;
  int membranePage;
};

constexpr double kUiPanelX = 580.0;
constexpr double kUiPanelWidth = 324.0;
constexpr std::array<MembraneUiRow, 21u> kUiRows{{
    {kOrderParamId, "FORMAT", 80.0, -1},
    {kOutputParamId, "OUT", 104.0, -1},
    {kShapeParamId, "SHAPE", 184.0, 0},
    {kShapeAmountParamId, "AMOUNT", 208.0, 0},
    {kVelocityParamId, "VELOCITY", 232.0, 0},
    {kStrikeModeParamId, "MODE", 184.0, 1},
    {kStrikeXParamId, "X", 208.0, 1},
    {kStrikeYParamId, "Y", 232.0, 1},
    {kMidiReceiveParamId, "MIDI RECEIVE", 256.0, -1},
    {kTuneParamId, "TUNE", 314.0, -1},
    {kDecayParamId, "DECAY", 338.0, -1},
    {kDampingParamId, "DAMP", 362.0, -1},
    {kDriveParamId, "DRIVE", 386.0, -1},
    {kNoteTrackingParamId, "TRACK", 410.0, -1},
    {kPitchSweepParamId, "DROP", 468.0, -1},
    {kPitchSweepTimeParamId, "TIME", 492.0, -1},
    {kPunchParamId, "PUNCH", 516.0, -1},
    {kClickParamId, "CLICK", 540.0, -1},
    {kSpreadParamId, "SPREAD", 598.0, -1},
    {kDepthParamId, "DEPTH", 622.0, -1},
    {kRotationParamId, "ROTATE", 646.0, -1},
}};

bool uiRowVisible(const MembraneUiRow &row, int membranePage) {
  return row.membranePage < 0 || row.membranePage == membranePage;
}

bool isUiMenuParam(clap_id id) {
  return id == kOrderParamId || id == kShapeParamId ||
         id == kStrikeModeParamId || id == kMidiReceiveParamId;
}

drawing::Rect membranePageButtonRect(uint32_t index) {
  return drawing::makeRect(kUiPanelX + 206.0 + index * 52.0, 158.0, 48.0, 13.0);
}

double uiNormalizedValue(clap_id id, double value) {
  const auto *def = paramDef(id);
  if (!def)
    return 0.0;
  if ((id == kTuneParamId || id == kPitchSweepTimeParamId ||
       id == kDecayParamId) &&
      def->minimum > 0.0) {
    return std::clamp(std::log(value / def->minimum) /
                          std::log(def->maximum / def->minimum),
                      0.0, 1.0);
  }
  return std::clamp((value - def->minimum) /
                        std::max(1.0e-12, def->maximum - def->minimum),
                    0.0, 1.0);
}

double uiValueFromNormalized(clap_id id, double normalized) {
  const auto *def = paramDef(id);
  if (!def)
    return 0.0;
  normalized = std::clamp(normalized, 0.0, 1.0);
  const double value =
      (id == kTuneParamId || id == kPitchSweepTimeParamId ||
       id == kDecayParamId)
          ? def->minimum * std::pow(def->maximum / def->minimum, normalized)
          : def->minimum + (def->maximum - def->minimum) * normalized;
  return clampValue(*def, value);
}

std::array<float, 2u>
visualMembranePoint(const s3g::AmbiMembraneKickParams &params, float radius,
                    float theta) {
  constexpr float pi = 3.14159265358979323846f;
  float x = radius * std::cos(theta);
  float y = radius * std::sin(theta);
  const float amount = params.shapeAmount;
  switch (params.shape) {
  case s3g::AmbiMembraneShape::Ellipse:
    x *= 1.0f + amount * 0.42f;
    y *= 1.0f - amount * 0.30f;
    break;
  case s3g::AmbiMembraneShape::Square: {
    const float maximum =
        std::max(0.001f, std::max(std::fabs(std::cos(theta)),
                                  std::fabs(std::sin(theta))));
    const float scale = 1.0f + (1.0f / maximum - 1.0f) * amount;
    x *= scale;
    y *= scale;
    break;
  }
  case s3g::AmbiMembraneShape::Triangle: {
    const float local = std::remainder(theta, 2.0f * pi / 3.0f);
    const float boundary =
        0.5f / std::max(0.51f, std::cos(pi / 3.0f - std::fabs(local)));
    const float scale = 1.0f + (boundary - 1.0f) * amount;
    x *= scale;
    y *= scale;
    break;
  }
  case s3g::AmbiMembraneShape::Irregular: {
    const float wobble =
        1.0f + amount * (0.17f * std::sin(theta * 3.0f + 0.7f) +
                         0.09f * std::sin(theta * 5.0f - 0.4f));
    const float originalX = x;
    x = x * wobble + amount * y * y * 0.10f;
    y = y * wobble - amount * originalX * 0.08f;
    break;
  }
  case s3g::AmbiMembraneShape::Circle:
  default:
    break;
  }
  return {x, y};
}

s3g::AmbiMembraneKickParams publishedParamsSnapshot(const Plugin &p) {
  s3g::AmbiMembraneKickParams params;
  params.order = static_cast<uint32_t>(paramValue(p, kOrderParamId));
  params.shape = static_cast<s3g::AmbiMembraneShape>(
      static_cast<uint32_t>(paramValue(p, kShapeParamId)));
  params.tuneHz = static_cast<float>(paramValue(p, kTuneParamId));
  params.pitchSweepSemitones =
      static_cast<float>(paramValue(p, kPitchSweepParamId));
  params.pitchSweepMs =
      static_cast<float>(paramValue(p, kPitchSweepTimeParamId));
  params.decaySeconds = static_cast<float>(paramValue(p, kDecayParamId));
  params.damping = static_cast<float>(paramValue(p, kDampingParamId));
  params.punch = static_cast<float>(paramValue(p, kPunchParamId));
  params.click = static_cast<float>(paramValue(p, kClickParamId));
  params.drive = static_cast<float>(paramValue(p, kDriveParamId));
  params.strikeX = static_cast<float>(paramValue(p, kStrikeXParamId));
  params.strikeY = static_cast<float>(paramValue(p, kStrikeYParamId));
  params.strikeMode = static_cast<s3g::AmbiMembraneStrikeMode>(
      static_cast<uint32_t>(paramValue(p, kStrikeModeParamId)));
  params.spatialSpread = static_cast<float>(paramValue(p, kSpreadParamId));
  params.membraneDepth = static_cast<float>(paramValue(p, kDepthParamId));
  params.rotationDeg = static_cast<float>(paramValue(p, kRotationParamId));
  params.shapeAmount = static_cast<float>(paramValue(p, kShapeAmountParamId));
  params.velocitySensitivity =
      static_cast<float>(paramValue(p, kVelocityParamId));
  params.noteTracking = static_cast<float>(paramValue(p, kNoteTrackingParamId));
  params.outputGainDb = static_cast<float>(paramValue(p, kOutputParamId));
  return params;
}

class Editor final : public s3g::portable_gui::routing::Canvas {
public:
  Plugin *_plugin;
  int _dragParam;
  int _factoryPresetIndex;
  clap_id _openMenu;
  int _hoverMenuItem;
  uint32_t _menuItemCount;
  int _membranePage;

  char _presetName[64];
#include "s3g_resonator_encoder_editor.inc"
  Editor(Plugin &instance) : Canvas(kGuiWidth, kGuiHeight) {
    auto *plugin = &instance;

    _plugin = plugin;
    _dragParam = -1;
    auto *p = static_cast<Plugin *>(plugin);
    _factoryPresetIndex =
        p ? s3g::ambiMembraneKickFactoryPresetIndex(publishedParamsSnapshot(*p))
          : 0;
    _openMenu = CLAP_INVALID_ID;
    _hoverMenuItem = -1;
    _menuItemCount = 0u;
    _membranePage = 0;
    if (_factoryPresetIndex >= 0) {
      std::snprintf(_presetName, sizeof(_presetName), "%s",
                    s3g::ambiMembraneKickFactoryPresetInfo(
                        static_cast<uint32_t>(_factoryPresetIndex))
                        .name);
    } else {
      std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
    }
  }
  ~Editor() override { stopRefresh(); }
  void applyFactoryPreset(int index) {
    auto *p = static_cast<Plugin *>(_plugin);
    if (!p)
      return;
    index = std::clamp(
        index, 0,
        static_cast<int>(s3g::kAmbiMembraneKickFactoryPresetCount - 1u));
    const auto preset =
        s3g::ambiMembraneKickFactoryPreset(static_cast<uint32_t>(index));
    const auto set = [&](clap_id id, double value) {
      beginEdit(id);
      editValue(id, value);
      endEdit(id);
    };
    set(kShapeParamId, static_cast<uint32_t>(preset.shape));
    set(kTuneParamId, preset.tuneHz);
    set(kPitchSweepParamId, preset.pitchSweepSemitones);
    set(kPitchSweepTimeParamId, preset.pitchSweepMs);
    set(kDecayParamId, preset.decaySeconds);
    set(kDampingParamId, preset.damping);
    set(kPunchParamId, preset.punch);
    set(kClickParamId, preset.click);
    set(kDriveParamId, preset.drive);
    set(kStrikeXParamId, preset.strikeX);
    set(kStrikeYParamId, preset.strikeY);
    set(kStrikeModeParamId, static_cast<uint32_t>(preset.strikeMode));
    set(kSpreadParamId, preset.spatialSpread);
    set(kDepthParamId, preset.membraneDepth);
    set(kRotationParamId, preset.rotationDeg);
    set(kShapeAmountParamId, preset.shapeAmount);
    set(kVelocityParamId, preset.velocitySensitivity);
    set(kNoteTrackingParamId, preset.noteTracking);
    _factoryPresetIndex = index;
    std::snprintf(
        _presetName, sizeof(_presetName), "%s",
        s3g::ambiMembraneKickFactoryPresetInfo(static_cast<uint32_t>(index))
            .name);
    setNeedsDisplay(true);
  }

  void markCustomPreset() {
    _factoryPresetIndex = -1;
    std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
  }

  drawing::Rect openMenuRect() {
    if (_openMenu == kFactoryPresetMenuId) {
      const auto band = membraneTitleBand();
      const drawing::Rect anchor = drawing::cocoaRect(band.presetMenu);
      return drawing::makeRect(anchor.origin.x, drawing::maxY(anchor) + 2.0,
                               anchor.size.width, 18.0 * _menuItemCount);
    }
    double y = 0.0;
    if (_openMenu == kOrderParamId)
      y = 80.0;
    else if (_openMenu == kShapeParamId || _openMenu == kStrikeModeParamId)
      y = 184.0;
    else if (_openMenu == kMidiReceiveParamId)
      y = 256.0;
    else
      return drawing::Rect{};
    const drawing::Rect anchor = drawing::makeRect(
        s3g::gui_layout::processorControlX(kUiPanelX), y - 1.0,
        s3g::gui_layout::processorMenuWidth(kUiPanelWidth), 15.0);
    return drawing::makeRect(anchor.origin.x, drawing::maxY(anchor) + 2.0,
                             anchor.size.width, 18.0 * _menuItemCount);
  }

  void drawOpenMenu_style(drawing::Attrs attrs, const drawing::Style &style) {
    if (_openMenu == CLAP_INVALID_ID || _menuItemCount == 0u)
      return;
    drawing::String items[17u]{};
    if (_openMenu == kFactoryPresetMenuId) {
      for (uint32_t index = 0u;
           index < s3g::kAmbiMembraneKickFactoryPresetCount; ++index) {
        items[index] = drawing::stringFromUtf8(
            s3g::ambiMembraneKickFactoryPresetInfo(index).name);
      }
    } else if (_openMenu == kOrderParamId) {
      items[0u] = "1OA / 4CH";
      items[1u] = "2OA / 9CH";
      items[2u] = "3OA / 16CH";
      items[3u] = "16 PICKUPS";
      items[4u] = "STEREO DOWNMIX";
    } else if (_openMenu == kShapeParamId) {
      items[0u] = "CIRCLE";
      items[1u] = "ELLIPSE";
      items[2u] = "SQUARE";
      items[3u] = "TRIANGLE";
      items[4u] = "IRREGULAR";
    } else if (_openMenu == kStrikeModeParamId) {
      items[0u] = "FIXED";
      items[1u] = "RANDOM AREA";
      items[2u] = "RANDOM RIM";
    } else if (_openMenu == kMidiReceiveParamId) {
      items[0u] = "OMNI";
      for (uint32_t channel = 1u; channel <= 16u; ++channel) {
        items[channel] = drawing::stringFormat("CH %u", channel);
      }
    }
    const int selected =
        _openMenu == kFactoryPresetMenuId
            ? _factoryPresetIndex
            : static_cast<int>(std::lround(
                  paramValue(*static_cast<Plugin *>(_plugin), _openMenu))) -
                  (_openMenu == kOrderParamId ? 1 : 0);
    drawing::drawDropdownMenu(openMenuRect(), 18.0, items, _menuItemCount,
                              selected, _hoverMenuItem, attrs, style);
  }

  void drawOriginal(drawing::Rect dirty) {
    (void)dirty;
    auto *p = static_cast<Plugin *>(_plugin);
    if (!p)
      return;
    drawing::Style style;
    drawing::setFill(style.bg);
    drawing::rectFill(bounds());
    drawing::Attrs titleAttrs = drawing::softTitleAttrs();
    drawing::Attrs labelAttrs = drawing::softLabelAttrs();
    drawing::Attrs valueAttrs = drawing::softValueAttrs();
    const auto titleBand = membraneTitleBand();
    const float activity = p->visualActivity.load(std::memory_order_relaxed);
    drawing::drawEncoderTitleBand(
        "s3g AMBI ENCODER MEMBRANE KICK 16",
        drawing::stringFromUtf8(_presetName),
        drawing::stringFormat("BODY %3.0f%%", activity * 100.0f), titleBand,
        titleAttrs, labelAttrs, valueAttrs, style);

    const drawing::Rect field = drawing::makeRect(16.0, 50.0, 550.0, 614.0);
    const drawing::Rect outputPanel =
        drawing::makeRect(kUiPanelX, 50.0, kUiPanelWidth, 96.0);
    const drawing::Rect membranePanel =
        drawing::makeRect(kUiPanelX, 154.0, kUiPanelWidth, 122.0);
    const drawing::Rect bodyPanel =
        drawing::makeRect(kUiPanelX, 284.0, kUiPanelWidth, 146.0);
    const drawing::Rect impactPanel =
        drawing::makeRect(kUiPanelX, 438.0, kUiPanelWidth, 122.0);
    const drawing::Rect spacePanel =
        drawing::makeRect(kUiPanelX, 568.0, kUiPanelWidth, 96.0);
    const auto drawPanel = [&](drawing::String name, drawing::Rect rect) {
      drawing::drawPanelFrame(rect.origin.x, rect.origin.y, rect.size.width,
                              rect.size.height, style);
      drawing::drawPanelHeader(
          name, true, rect.origin.x, rect.origin.y, rect.size.width,
          s3g::gui_layout::kStandardMetrics.headerHeight, labelAttrs, style);
    };
    drawPanel("DISTRIBUTED MEMBRANE · +AZ LEFT / +EL UP", field);
    drawPanel("OUTPUT", outputPanel);
    drawPanel("MEMBRANE", membranePanel);
    drawPanel("LOW BODY", bodyPanel);
    drawPanel("IMPACT", impactPanel);
    const uint32_t outputFormat =
        static_cast<uint32_t>(std::lround(paramValue(*p, kOrderParamId)));
    const bool directPickups =
        outputFormat == s3g::kAmbiMembraneKickDirectPickups;
    const bool stereoDownmix =
        outputFormat == s3g::kAmbiMembraneKickStereoDownmix;
    drawPanel(directPickups ? "DIRECT PICKUPS / SPACE OFF"
                            : (stereoDownmix ? "STEREO DOWNMIX / SPACE OFF"
                                             : "AMBISONIC SPACE"),
              spacePanel);
    drawing::drawToolboxHeaderButton(membranePageButtonRect(0u), membranePanel,
                                     "BODY", _membranePage == 0, valueAttrs,
                                     style);
    drawing::drawToolboxHeaderButton(membranePageButtonRect(1u), membranePanel,
                                     "STRIKE", _membranePage == 1, valueAttrs,
                                     style);

    auto paramsSnapshot = publishedParamsSnapshot(*p);
    paramsSnapshot.strikeX = p->visualStrikeX.load(std::memory_order_relaxed);
    paramsSnapshot.strikeY = p->visualStrikeY.load(std::memory_order_relaxed);
    const drawing::Point center = drawing::makePoint(291.0, 338.0);
    constexpr double radiusPixels = 202.0;
    drawing::Path membrane = drawing::Path::bezierPath();
    for (uint32_t point = 0u; point <= 128u; ++point) {
      const float theta =
          static_cast<float>(point) / 128.0f * 6.28318530717958647692f;
      const auto position = visualMembranePoint(paramsSnapshot, 1.0f, theta);
      const drawing::Point screen =
          drawing::makePoint(center.x - position[0] * radiusPixels,
                             center.y - position[1] * radiusPixels);
      point == 0u ? (membrane).moveToPoint(screen)
                  : (membrane).lineToPoint(screen);
    }
    (membrane).closePath();
    drawing::setFill(drawing::calibratedRGB(0.055, 0.070, 0.074, 1.0));
    (membrane).fill();
    drawing::setStroke(drawing::calibratedRGB(0.30 + activity * 0.40,
                                              0.62 + activity * 0.22,
                                              0.66 + activity * 0.18, 0.88));
    (membrane).setLineWidth(1.5 + activity * 3.5);
    (membrane).stroke();

    for (uint32_t ring = 0u; ring < 4u; ++ring) {
      for (uint32_t spoke = 0u; spoke < 4u; ++spoke) {
        const float localRadius = 0.18f + static_cast<float>(ring) * 0.22f;
        const float theta =
            static_cast<float>(spoke) * 1.57079632679489661923f +
            static_cast<float>(ring & 1u) * 0.78539816339744830962f;
        const auto position =
            visualMembranePoint(paramsSnapshot, localRadius, theta);
        const drawing::Point node =
            drawing::makePoint(center.x - position[0] * radiusPixels,
                               center.y - position[1] * radiusPixels);
        const double nodeRadius =
            3.0 + activity * (2.0 + static_cast<double>(3u - ring));
        drawing::setFill(
            drawing::calibratedRGB(0.32, 0.76, 0.80, 0.42 + activity * 0.52));
        (drawing::Path::bezierPathWithOvalInRect(
             drawing::makeRect(node.x - nodeRadius, node.y - nodeRadius,
                               nodeRadius * 2.0, nodeRadius * 2.0)))
            .fill();
        if (directPickups) {
          drawing::drawAtPoint_withAttributes(
              drawing::stringFormat("P%u", ring * 4u + spoke + 1u),
              drawing::makePoint(node.x + 6.0, node.y - 7.0), labelAttrs);
        }
      }
    }
    const drawing::Point strike =
        drawing::makePoint(center.x - paramsSnapshot.strikeX * radiusPixels,
                           center.y - paramsSnapshot.strikeY * radiusPixels);
    drawing::setStroke(drawing::calibratedWhite(0.96, 0.94));
    drawing::Path strikeMark = drawing::Path::bezierPath();
    (strikeMark)
        .appendBezierPathWithOvalInRect(
            drawing::makeRect(strike.x - 9.0, strike.y - 9.0, 18.0, 18.0));
    (strikeMark).moveToPoint(drawing::makePoint(strike.x - 13.0, strike.y));
    (strikeMark).lineToPoint(drawing::makePoint(strike.x + 13.0, strike.y));
    (strikeMark).moveToPoint(drawing::makePoint(strike.x, strike.y - 13.0));
    (strikeMark).lineToPoint(drawing::makePoint(strike.x, strike.y + 13.0));
    (strikeMark).setLineWidth(1.2);
    (strikeMark).stroke();
    drawing::drawAtPoint_withAttributes(
        "CLICK THE MEMBRANE TO PLACE + STRIKE",
        drawing::makePoint(field.origin.x + 14.0, drawing::maxY(field) - 29.0),
        labelAttrs);

    for (const auto &row : kUiRows) {
      if (!uiRowVisible(row, _membranePage))
        continue;
      const double value = paramValue(*p, row.id);
      char text[64]{};
      paramsValueToText(&p->plugin, row.id, value, text, sizeof(text));
      drawControl_value_normalized_y_menu_labelAttrs_valueAttrs_style(
          drawing::stringFromUtf8(row.label), drawing::stringFromUtf8(text),
          uiNormalizedValue(row.id, value), row.y, isUiMenuParam(row.id),
          labelAttrs, valueAttrs, style);
    }
    const drawing::Rect trigger =
        drawing::makeRect(kUiPanelX + 12.0, 122.0, kUiPanelWidth - 24.0, 18.0);
    drawing::drawHeaderButton(trigger, outputPanel, "STRIKE", activity > 0.05f,
                              valueAttrs, style);
    drawOpenMenu_style(valueAttrs, style);
  }

  void drawControl_value_normalized_y_menu_labelAttrs_valueAttrs_style(
      drawing::String label, drawing::String value, double normalized, double y,
      bool menu, drawing::Attrs labelAttrs, drawing::Attrs valueAttrs,
      const drawing::Style &style) {
    if (menu) {
      drawing::drawProcessorMenu(label, value, y, kUiPanelX, kUiPanelWidth,
                                 labelAttrs, valueAttrs, style);
      return;
    }
    drawing::drawProcessorSlider(label, value, static_cast<double>(normalized),
                                 y, kUiPanelX, kUiPanelWidth, labelAttrs,
                                 valueAttrs, style);
  }

  void updateDraggedParam(drawing::Point point) {
    if (_dragParam <= 0)
      return;
    const double controlX = s3g::gui_layout::processorControlX(kUiPanelX);
    const double trackWidth =
        s3g::gui_layout::processorTrackWidth(kUiPanelWidth);
    const double normalized =
        std::clamp((point.x - controlX) / trackWidth, 0.0, 1.0);
    auto *p = static_cast<Plugin *>(_plugin);
    editValue(
        static_cast<clap_id>(_dragParam),
        uiValueFromNormalized(static_cast<clap_id>(_dragParam), normalized));
    setNeedsDisplay(true);
  }

  void mouseDown(drawing::Event *event) {
    drawing::Point point =
        convertPoint_fromView((event)->locationInWindow(), nullptr);
    auto *p = static_cast<Plugin *>(_plugin);
    if (!p)
      return;
    const auto titleBand = membraneTitleBand();
    if (_openMenu != CLAP_INVALID_ID) {
      const int hit = drawing::dropdownHitIndex(point, openMenuRect(), 18.0,
                                                _menuItemCount);
      if (hit >= 0) {
        if (_openMenu == kFactoryPresetMenuId) {
          applyFactoryPreset(hit);
        } else {
          const clap_id menuParam = _openMenu;
          const double value = hit + (menuParam == kOrderParamId ? 1.0 : 0.0);
          singleEdit(menuParam, value);
          if (menuParam != kOrderParamId && menuParam != kMidiReceiveParamId) {
            markCustomPreset();
          }
        }
      }
      _openMenu = CLAP_INVALID_ID;
      _hoverMenuItem = -1;
      _menuItemCount = 0u;
      setNeedsDisplay(true);
      return;
    }
    if (drawing::contains(point, drawing::cocoaRect(titleBand.presetMenu))) {
      _openMenu = kFactoryPresetMenuId;
      _hoverMenuItem = -1;
      _menuItemCount = s3g::kAmbiMembraneKickFactoryPresetCount;
      setNeedsDisplay(true);
      return;
    }
    if (drawing::contains(point, drawing::cocoaRect(titleBand.loadButton))) {
      presetDialog(false);
      setNeedsDisplay(true);
      return;
    }
    if (drawing::contains(point, drawing::cocoaRect(titleBand.saveButton))) {
      presetDialog(true);
      setNeedsDisplay(true);
      return;
    }
    if (drawing::contains(point, drawing::cocoaRect(titleBand.randomButton))) {
      applyFactoryPreset(static_cast<int>(
          drawing::randomBounded(s3g::kAmbiMembraneKickFactoryPresetCount)));
      return;
    }
    for (uint32_t page = 0u; page < 2u; ++page) {
      if (drawing::contains(point, membranePageButtonRect(page))) {
        _membranePage = static_cast<int>(page);
        setNeedsDisplay(true);
        return;
      }
    }
    const drawing::Rect trigger =
        drawing::makeRect(kUiPanelX + 12.0, 122.0, kUiPanelWidth - 24.0, 18.0);
    if (drawing::contains(point, trigger)) {
      singleEdit(kTriggerParamId, 0.0);
      singleEdit(kTriggerParamId, 1.0);
      setNeedsDisplay(true);
      return;
    }

    const drawing::Point center = drawing::makePoint(291.0, 338.0);
    constexpr double radiusPixels = 202.0;
    const float x = static_cast<float>((center.x - point.x) / radiusPixels);
    const float y = static_cast<float>((center.y - point.y) / radiusPixels);
    if (point.x >= 42.0 && point.x <= 540.0 && point.y >= 86.0 &&
        point.y <= 594.0 && x * x + y * y <= 1.0f) {
      singleEdit(kStrikeModeParamId, 0.0);
      singleEdit(kStrikeXParamId, x);
      singleEdit(kStrikeYParamId, y);
      singleEdit(kTriggerParamId, 0.0);
      singleEdit(kTriggerParamId, 1.0);
      markCustomPreset();
      setNeedsDisplay(true);
      return;
    }

    for (const auto &row : kUiRows) {
      if (!uiRowVisible(row, _membranePage))
        continue;
      const drawing::Rect hit = drawing::makeRect(
          kUiPanelX + s3g::gui_layout::kStandardMetrics.hitInset, row.y - 9.0,
          kUiPanelWidth - s3g::gui_layout::kStandardMetrics.hitInset * 2.0,
          s3g::gui_layout::kStandardMetrics.hitHeight);
      if (!drawing::contains(point, hit))
        continue;
      const auto *def = paramDef(row.id);
      if (!def)
        return;
      if (isUiMenuParam(row.id)) {
        _openMenu = row.id;
        _hoverMenuItem = -1;
        _menuItemCount = row.id == kMidiReceiveParamId
                             ? 17u
                             : (row.id == kShapeParamId
                                    ? 5u
                                    : (row.id == kOrderParamId ? 5u : 3u));
        setNeedsDisplay(true);
        return;
      }
      double defaultValue = 0.0;
      if (drawing::sliderDoubleClickDefault(event, &p->plugin, row.id,
                                            &defaultValue)) {
        singleEdit(row.id, defaultValue);
        _dragParam = -1;
      } else {
        _dragParam = static_cast<int>(row.id);
        beginEdit(row.id);
        updateDraggedParam(point);
      }
      if (row.id != kOutputParamId)
        markCustomPreset();
      return;
    }
  }

  void mouseDragged(drawing::Event *event) {
    if (_dragParam > 0) {
      updateDraggedParam(
          convertPoint_fromView((event)->locationInWindow(), nullptr));
    }
  }

  void mouseUp(drawing::Event *event) {
    (void)event;
    if (_dragParam > 0) {
      endEdit(static_cast<clap_id>(_dragParam));
    }
    _dragParam = -1;
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
