// Literal No Input Mixer Cocoa geometry, scopes and interaction maps.
namespace complex_processor_canvas {
namespace drawing = s3g::portable_gui::complex_drawing;
namespace foundation = s3g::portable_gui::foundation;
enum OpenMenu : int {
  kMenuNone = -1,
  kMenuPreset = 0,
  kMenuRandomEnergy,
  kMenuLane,
  kMenuSource,
  kMenuDestination,
  kMenuSlot0,
  kMenuSlot1,
  kMenuSlot2,
  kMenuMotionShape,
  kMenuBehavior,
  kMenuQuality,
  kMenuMixerInsert,
  kMenuMixerAux,
  kMenuReactMode,
  kMenuFieldDivision,
  kMenuEventDivision,
  kMenuAuxTapA,
  kMenuAuxTapB,
  kMenuOutputFormat,
};

drawing::Rect processorMenuRect(const s3g::gui_layout::Panel &panel,
                                uint32_t row);

drawing::Rect seedNewButtonRect() {
  const auto &panel = s3g::gui_layout::kNoInputMixerFamilyLayout.network;
  const drawing::Rect row = processorMenuRect(panel, 0u);
  return drawing::makeRect(row.origin.x, row.origin.y, 58.0, row.size.height);
}

drawing::Rect randomButtonRect() {
  const drawing::Rect seed = seedNewButtonRect();
  return drawing::makeRect(drawing::maxX(seed) + 6.0, seed.origin.y, 66.0,
                           seed.size.height);
}

drawing::Rect forgetButtonRect() {
  const drawing::Rect random = randomButtonRect();
  return drawing::makeRect(drawing::maxX(random) + 6.0, random.origin.y, 66.0,
                           random.size.height);
}

drawing::Rect processorMenuRect(const s3g::gui_layout::Panel &panel,
                                uint32_t row) {
  return drawing::makeRect(
      s3g::gui_layout::processorControlX(panel.frame.x),
      s3g::gui_layout::rowY(panel, row) - 1.0,
      s3g::gui_layout::processorMenuWidth(panel.frame.width), 15.0);
}

drawing::Rect channelInsertMenuRect(uint32_t slot) {
  const auto &panel = s3g::gui_layout::kNoInputMixerFamilyLayout.inserts;
  drawing::Rect rect = processorMenuRect(panel, slot);
  rect.size.width -= 58.0;
  return rect;
}

drawing::Rect channelInsertEditRect(uint32_t slot) {
  const auto &panel = s3g::gui_layout::kNoInputMixerFamilyLayout.inserts;
  drawing::Rect rect = processorMenuRect(panel, slot);
  rect.origin.x = drawing::maxX(rect) - 52.0;
  rect.size.width = 52.0;
  return rect;
}

drawing::Rect movementBankButtonRect(uint32_t bank) {
  const auto &panel = s3g::gui_layout::kNoInputMixerFamilyLayout.movement;
  return drawing::makeRect(panel.frame.x + panel.frame.width - 153.0 +
                               static_cast<double>(bank) * 48.0,
                           panel.frame.y + 7.0, 44.0, 15.0);
}

drawing::Rect fieldTabRect(uint32_t index) {
  const auto &family = s3g::gui_layout::kNoInputMixerFamilyLayout;
  constexpr double width = 52.0;
  constexpr double gap = 3.0;
  return drawing::makeRect(family.fieldPanel.x + family.fieldPanel.width -
                               static_cast<double>(kPageCount) * width -
                               static_cast<double>(kPageCount - 1u) * gap -
                               10.0 +
                               static_cast<double>(index) * (width + gap),
                           family.fieldPanel.y + 4.0, width, 14.0);
}

drawing::Rect mixerPopButtonRect() {
  const auto &field = s3g::gui_layout::kNoInputMixerFamilyLayout.fieldPanel;
  return drawing::makeRect(fieldTabRect(0u).origin.x - 56.0, field.y + 4.0,
                           48.0, 14.0);
}

drawing::Rect widePageRect() {
  return drawing::makeRect(28.0, 78.0, 1300.0, 714.0);
}

drawing::Rect auxPageColumnRect(uint32_t lane) {
  const drawing::Rect page = widePageRect();
  constexpr double gap = 8.0;
  const double width = (page.size.width - 28.0 - gap * 7.0) / 8.0;
  return drawing::makeRect(page.origin.x + 14.0 + lane * (width + gap),
                           page.origin.y + 54.0, width,
                           page.size.height - 72.0);
}

drawing::Rect auxPageTrackRect(uint32_t lane, uint32_t row) {
  const drawing::Rect column = auxPageColumnRect(lane);
  return drawing::makeRect(column.origin.x + 12.0,
                           column.origin.y + 70.0 + row * 84.0,
                           column.size.width - 24.0, 11.0);
}

drawing::Rect auxPageTapRect(uint32_t lane, uint32_t bus) {
  drawing::Rect rect = auxPageTrackRect(lane, bus == 0u ? 1u : 4u);
  rect.origin.y -= 5.0;
  rect.size.height = 21.0;
  return rect;
}

drawing::Rect surfacePlotRect() {
  const drawing::Rect page = widePageRect();
  return drawing::makeRect(page.origin.x + 14.0, page.origin.y + 116.0,
                           page.size.width - 28.0, page.size.height - 134.0);
}

drawing::Rect surfaceButtonRect(uint32_t index) {
  const drawing::Rect page = widePageRect();
  static constexpr double widths[]{
      62.0, 62.0, 54.0, 54.0, 54.0,
  };
  double x = page.origin.x + 14.0;
  for (uint32_t item = 0u; item < index; ++item)
    x += widths[item] + 6.0;
  return drawing::makeRect(x, page.origin.y + 43.0, widths[index], 22.0);
}

drawing::Rect surfaceCurveRect() {
  const drawing::Rect page = widePageRect();
  return drawing::makeRect(page.origin.x + 14.0, page.origin.y + 76.0, 112.0,
                           22.0);
}

drawing::Rect surfaceFocusRect(uint32_t index) {
  const drawing::Rect page = widePageRect();
  return drawing::makeRect(page.origin.x + 186.0 +
                               static_cast<double>(index) * 26.0,
                           page.origin.y + 76.0, 22.0, 22.0);
}

drawing::Rect surfaceGlideRect(uint32_t index) {
  const drawing::Rect page = widePageRect();
  return drawing::makeRect(page.origin.x + 360.0 +
                               static_cast<double>(index) * 26.0,
                           page.origin.y + 76.0, 22.0, 22.0);
}

drawing::Rect surfaceTopologyRect() {
  const drawing::Rect page = widePageRect();
  return drawing::makeRect(page.origin.x + 500.0, page.origin.y + 76.0, 138.0,
                           22.0);
}

drawing::Rect movementGlobalToggleRect(uint32_t index) {
  const auto &panel = s3g::gui_layout::kNoInputMixerFamilyLayout.movement;
  const double x = s3g::gui_layout::processorControlX(panel.frame.x);
  const double width = 52.0;
  return drawing::makeRect(x + index * (width + 5.0),
                           s3g::gui_layout::rowY(panel, 8u) - 1.0, width, 17.0);
}

drawing::Rect movementSectionRandomRect() {
  const auto &panel = s3g::gui_layout::kNoInputMixerFamilyLayout.movement;
  return drawing::makeRect(panel.frame.x + panel.frame.width - 199.0,
                           panel.frame.y + 7.0, 40.0, 15.0);
}

drawing::Rect reactDirectionButtonRect() {
  const auto &panel = s3g::gui_layout::kNoInputMixerFamilyLayout.movement;
  return processorMenuRect(panel, 5u);
}

drawing::Rect patchVisualRect() {
  return drawing::makeRect(28.0, 78.0, 896.0, 714.0);
}

drawing::Rect channelOverviewRect() { return patchVisualRect(); }

drawing::Rect wiringModeButtonRect(uint32_t index) {
  const drawing::Rect visual = patchVisualRect();
  return drawing::makeRect(drawing::maxX(visual) - 116.0 + index * 54.0,
                           visual.origin.y + 10.0, 48.0, 18.0);
}

drawing::Rect matrixMidiModeButtonRect(uint32_t index) {
  const drawing::Rect visual = patchVisualRect();
  return drawing::makeRect(visual.origin.x + 18.0 +
                               static_cast<double>(index) * 96.0,
                           visual.origin.y + 10.0, 90.0, 18.0);
}

drawing::Rect matrixMidiSignButtonRect(uint32_t index) {
  const drawing::Rect visual = patchVisualRect();
  return drawing::makeRect(visual.origin.x + 222.0 +
                               static_cast<double>(index) * 86.0,
                           visual.origin.y + 10.0, 80.0, 18.0);
}

drawing::Rect matrixMidiRampTrackRect() {
  const drawing::Rect visual = patchVisualRect();
  return drawing::makeRect(visual.origin.x + 459.0, visual.origin.y + 17.0,
                           142.0, 4.0);
}

drawing::Rect matrixMidiRampHitRect() {
  const drawing::Rect track = matrixMidiRampTrackRect();
  return drawing::makeRect(track.origin.x - 42.0, track.origin.y - 9.0,
                           track.size.width + 123.0, 22.0);
}

drawing::Rect clearConnectionsButtonRect() {
  const drawing::Rect visual = patchVisualRect();
  return drawing::makeRect(drawing::maxX(visual) - 196.0,
                           visual.origin.y + 10.0, 74.0, 18.0);
}

drawing::Rect wiringGridRect() {
  const drawing::Rect visual = patchVisualRect();
  return drawing::makeRect(visual.origin.x + 154.0, visual.origin.y + 34.0,
                           588.0, visual.size.height - 46.0);
}

drawing::Point wiringPortPoint(bool destination, uint32_t lane) {
  const drawing::Rect visual = patchVisualRect();
  const double top = visual.origin.y + 72.0;
  const double gap = (visual.size.height - 144.0) / 7.0;
  return drawing::makePoint(destination ? drawing::maxX(visual) - 54.0
                                        : visual.origin.x + 54.0,
                            top + lane * gap);
}

drawing::Point cubicPoint(drawing::Point a, drawing::Point c1,
                          drawing::Point c2, drawing::Point b, double t) {
  const double u = 1.0 - t;
  const double aa = u * u * u;
  const double bb = 3.0 * u * u * t;
  const double cc = 3.0 * u * t * t;
  const double dd = t * t * t;
  return drawing::makePoint(aa * a.x + bb * c1.x + cc * c2.x + dd * b.x,
                            aa * a.y + bb * c1.y + cc * c2.y + dd * b.y);
}

drawing::Point cubicTangent(drawing::Point a, drawing::Point c1,
                            drawing::Point c2, drawing::Point b, double t) {
  const double u = 1.0 - t;
  return drawing::makePoint(
      3.0 * u * u * (c1.x - a.x) + 6.0 * u * t * (c2.x - c1.x) +
          3.0 * t * t * (b.x - c2.x),
      3.0 * u * u * (c1.y - a.y) + 6.0 * u * t * (c2.y - c1.y) +
          3.0 * t * t * (b.y - c2.y));
}

void wiringControlPoints(drawing::Point a, drawing::Point b, float vortex,
                         drawing::Point &c1, drawing::Point &c2) {
  const double centerX = (a.x + b.x) * 0.5;
  const double arc = (b.y - a.y) * 0.18 + static_cast<double>(vortex) * 76.0;
  const double centerY = (a.y + b.y) * 0.5 + arc;
  c1 = drawing::makePoint(centerX - 106.0, centerY);
  c2 = drawing::makePoint(centerX + 106.0, centerY);
}

drawing::Point mixerSurfaceOffset(drawing::Rect surface) {
  constexpr double contentWidth = 1216.0;
  constexpr double contentHeight = 706.0;
  return drawing::makePoint(
      surface.origin.x - 12.0 +
          std::max<double>(0.0, (surface.size.width - contentWidth) * 0.5),
      surface.origin.y - 42.0 +
          std::max<double>(0.0, (surface.size.height - contentHeight) * 0.5));
}

drawing::Rect translatedRect(drawing::Rect rect, drawing::Point offset) {
  rect.origin.x += offset.x;
  rect.origin.y += offset.y;
  return rect;
}

drawing::Rect popupStripRect(uint32_t lane) {
  constexpr double gap = 6.0;
  constexpr double left = 12.0;
  constexpr double areaWidth = 858.0;
  const double width = (areaWidth - gap * 7.0) / 8.0;
  return drawing::makeRect(left + lane * (width + gap), 42.0, width,
                           kPerformanceMixerReferenceHeight - 54.0);
}

drawing::Rect popupAuxPanelRect() {
  return drawing::makeRect(884.0, 42.0, 344.0, 706.0);
}

drawing::Rect popupBodyRect(drawing::Rect strip) {
  return drawing::makeRect(strip.origin.x + 10.0, strip.origin.y + 54.0,
                           strip.size.width - 20.0, 10.0);
}

drawing::Rect popupLossRect(drawing::Rect strip) {
  return drawing::makeRect(strip.origin.x + 10.0, strip.origin.y + 88.0,
                           strip.size.width - 20.0, 10.0);
}

drawing::Rect popupLoopRect(drawing::Rect strip) {
  return drawing::makeRect(drawing::midX(strip) - 7.0, strip.origin.y + 132.0,
                           14.0, 108.0);
}

drawing::Rect popupEqRect(drawing::Rect strip, uint32_t band) {
  constexpr double width = 11.0;
  constexpr double gap = 11.0;
  const double total = width * 3.0 + gap * 2.0;
  return drawing::makeRect(drawing::midX(strip) - total * 0.5 +
                               band * (width + gap),
                           strip.origin.y + 286.0, width, 88.0);
}

drawing::Rect popupSendRect(drawing::Rect strip, uint32_t bus) {
  return drawing::makeRect(strip.origin.x + 10.0,
                           strip.origin.y + 414.0 + bus * 36.0,
                           strip.size.width - 20.0, 10.0);
}

drawing::Rect popupInsertRect(drawing::Rect strip, uint32_t slot) {
  return drawing::makeRect(strip.origin.x + 8.0,
                           strip.origin.y + 500.0 + slot * 21.0,
                           strip.size.width - 16.0, 18.0);
}

drawing::Rect popupInsertMenuRect(drawing::Rect strip, uint32_t slot) {
  drawing::Rect rect = popupInsertRect(strip, slot);
  rect.size.width -= 31.0;
  return rect;
}

drawing::Rect popupInsertEditRect(drawing::Rect strip, uint32_t slot) {
  drawing::Rect rect = popupInsertRect(strip, slot);
  rect.origin.x = drawing::maxX(rect) - 28.0;
  rect.size.width = 28.0;
  return rect;
}

drawing::Rect popupFaderRect(drawing::Rect strip) {
  return drawing::makeRect(drawing::midX(strip) - 7.0, strip.origin.y + 582.0,
                           14.0, 74.0);
}

drawing::Rect popupMuteRect(drawing::Rect strip) {
  return drawing::makeRect(strip.origin.x + 8.0, strip.origin.y + 672.0,
                           strip.size.width - 16.0, 20.0);
}

drawing::Rect popupAuxTypeRect(uint32_t bus) {
  const drawing::Rect panel = popupAuxPanelRect();
  return drawing::makeRect(panel.origin.x + 114.0,
                           panel.origin.y + 58.0 + bus * 248.0, 204.0, 18.0);
}

drawing::Rect popupAuxTypeMenuRect(uint32_t bus) {
  drawing::Rect rect = popupAuxTypeRect(bus);
  rect.size.width = 146.0;
  return rect;
}

drawing::Rect popupAuxTypeEditRect(uint32_t bus) {
  drawing::Rect rect = popupAuxTypeRect(bus);
  rect.origin.x += 150.0;
  rect.size.width = 54.0;
  return rect;
}

drawing::Rect popupAuxMuteRect(uint32_t bus) {
  const drawing::Rect panel = popupAuxPanelRect();
  return drawing::makeRect(panel.origin.x + 14.0,
                           panel.origin.y + 58.0 + bus * 248.0, 82.0, 18.0);
}

drawing::Rect popupAuxSliderRect(uint32_t bus, uint32_t local) {
  const drawing::Rect panel = popupAuxPanelRect();
  return drawing::makeRect(panel.origin.x + 114.0,
                           panel.origin.y + 96.0 + bus * 248.0 + local * 36.0,
                           204.0, 10.0);
}

drawing::Rect popupToneRect(uint32_t row) {
  const drawing::Rect panel = popupAuxPanelRect();
  return drawing::makeRect(panel.origin.x + 114.0,
                           panel.origin.y + 594.0 + row * 40.0, 204.0, 10.0);
}

VSTGUI::CColor mixerColor(int rgb, double alpha = 1.0) {
  return drawing::color(rgb, alpha);
}

void drawFlatButton(drawing::Rect rect, drawing::String text, bool active,
                    drawing::Attrs attrs) {
  drawing::setFill(mixerColor(active ? 0x303030 : 0x151515));
  drawing::rectFill(rect);
  drawing::setStroke(mixerColor(active ? 0xb8b8b8 : 0x555555));
  drawing::frameRect(rect);
  const drawing::Size size = drawing::sizeWithAttributes(text, attrs);
  drawing::drawAtPoint_withAttributes(
      text,
      drawing::makePoint(rect.origin.x + (rect.size.width - size.width) * 0.5,
                         rect.origin.y +
                             (rect.size.height - size.height) * 0.5 - 0.5),
      attrs);
}

void drawPolarityButton(drawing::Rect rect, drawing::String text, bool active,
                        int rgb, drawing::Attrs attrs) {
  drawing::setFill(mixerColor(active ? rgb : 0x151515, active ? 0.34 : 1.0));
  drawing::rectFill(rect);
  drawing::setStroke(mixerColor(active ? rgb : 0x555555));
  drawing::frameRect(rect);
  const drawing::Size size = drawing::sizeWithAttributes(text, attrs);
  drawing::drawAtPoint_withAttributes(
      text,
      drawing::makePoint(rect.origin.x + (rect.size.width - size.width) * 0.5,
                         rect.origin.y +
                             (rect.size.height - size.height) * 0.5 - 0.5),
      attrs);
}

struct EffectControlLabels {
  const char *gain;
  const char *tone;
  const char *bias;
};

EffectControlLabels effectControlLabels(s3g::NoInputDistortionType type) {
  using Type = s3g::NoInputDistortionType;
  switch (type) {
  case Type::Wool:
    return {"SUSTAIN", "CONTOUR", "ASYM"};
  case Type::Rat:
    return {"DIST", "FILTER", "BIAS"};
  case Type::ZoneA:
  case Type::ZoneB:
    return {"DIST", "FOCUS", "BIAS"};
  case Type::FuzzI:
    return {"GAIN", "SAG", "BIAS"};
  case Type::FuzzII:
    return {"GAIN", "GATE", "BIAS"};
  case Type::Diode:
    return {"GAIN", "SHAPE", "BIAS"};
  case Type::Ring:
    return {"DEPTH", "CARRIER", "BIAS"};
  case Type::Relay:
    return {"THRESH", "CHATTER", "ASYM"};
  case Type::Crush:
    return {"BITS", "RATE", "DITHER"};
  case Type::Splice:
    return {"MIX", "LENGTH", "DIRECTION"};
  case Type::Logic:
    return {"DEPTH", "THRESH", "BALANCE"};
  case Type::Shred:
    return {"DRIVE", "TILT", "ASYM"};
  case Type::Void:
    return {"DEPTH", "RECOVER", "SKEW"};
  case Type::Rotor:
    return {"DEPTH", "RATE", "SHAPE"};
  case Type::Phase:
    return {"DEPTH", "RATE", "CENTER"};
  case Type::Chorus:
    return {"MIX", "RATE", "REGEN"};
  case Type::Throat:
    return {"MIX", "VOWEL", "SHIFT"};
  case Type::Robot:
    return {"DEPTH", "CARRIER", "CHARACTER"};
  case Type::OctDown:
    return {"MIX", "FILTER", "TRACK"};
  case Type::OctUp:
    return {"MIX", "TONE", "SHAPE"};
  case Type::OctStack:
    return {"MIX", "TONE", "BALANCE"};
  case Type::Bypass:
  case Type::Count:
    return {"AMOUNT", "TONE", "BIAS"};
  }
  return {"AMOUNT", "TONE", "BIAS"};
}

drawing::Rect effectEditorTrackRect(uint32_t row) {
  return drawing::makeRect(126.0, 88.0 + row * 40.0, 292.0, 12.0);
}

drawing::Rect effectEditorToggleRect(uint32_t row) {
  return drawing::makeRect(126.0, 82.0 + row * 40.0, 292.0, 24.0);
}

class EffectEditor final : public s3g::portable_gui::routing::Canvas {
public:
  using Canvas = s3g::portable_gui::routing::Canvas;

  Plugin *_plugin;
  std::function<void(clap_id, double, bool)> apply;
  bool _isAux;
  uint32_t _lane;
  uint32_t _slot;
  uint32_t _bus;
  clap_id _dragParam;
  drawing::Rect _dragRect;
  double _dragMinimum;
  double _dragMaximum;
#include "s3g_complex_processor_nim_effect_editor.inc"
  EffectEditor(Plugin &plugin,
               std::function<void(clap_id, double, bool)> callback)
      : Canvas(454, 292) {
    _plugin = &plugin;
    apply = std::move(callback);
    _isAux = false;
    _lane = 0u;
    _slot = 0u;
    _bus = 0u;
    _dragParam = CLAP_INVALID_ID;
    _dragRect = drawing::Rect{};
    _dragMinimum = 0.0;
    _dragMaximum = 1.0;
  }

  void setLane_slot(uint32_t lane, uint32_t slot) {
    _isAux = false;
    _lane = std::min<uint32_t>(lane, kChannelCount - 1u);
    _slot = std::min<uint32_t>(slot, s3g::kNoInputMixerInsertSlots - 1u);
    _dragParam = CLAP_INVALID_ID;
    setNeedsDisplay(true);
  }

  void setAux(uint32_t bus) {
    _isAux = true;
    _bus = std::min<uint32_t>(bus, 1u);
    _dragParam = CLAP_INVALID_ID;
    setNeedsDisplay(true);
  }

  void clearOwner() { apply = {}; }

  s3g::NoInputDistortionType effectType() {
    auto *plugin = static_cast<Plugin *>(_plugin);
    if (!plugin)
      return s3g::NoInputDistortionType::Bypass;
    const auto visible = uiSnapshot(*plugin);
    return _isAux ? visible.params.aux[_bus].effect.type
                  : visible.params.lanes[_lane].inserts[_slot].type;
  }

  uint32_t controlCount() { return 5u; }

  clap_id paramForRow(uint32_t row) {
    if (_isAux) {
      const clap_id ids[5] = {
          _bus == 0u ? kAuxAGainParamId : kAuxBGainParamId,
          _bus == 0u ? kAuxAToneParamId : kAuxBToneParamId,
          _bus == 0u ? kAuxABiasParamId : kAuxBBiasParamId,
          _bus == 0u ? kAuxAReturnParamId : kAuxBReturnParamId,
          _bus == 0u ? kAuxAFeedbackParamId : kAuxBFeedbackParamId,
      };
      return row < 5u ? ids[row] : CLAP_INVALID_ID;
    }
    const clap_id offsets[5] = {
        kInsertGainOffset,  kInsertToneOffset,   kInsertBiasOffset,
        kInsertLevelOffset, kInsertBypassOffset,
    };
    return row < 5u ? insertParamId(_lane, _slot, offsets[row])
                    : CLAP_INVALID_ID;
  }

  drawing::String labelForRow(uint32_t row) {
    const EffectControlLabels labels = effectControlLabels(effectType());
    if (row == 0u)
      return drawing::stringFromUtf8(labels.gain);
    if (row == 1u)
      return drawing::stringFromUtf8(labels.tone);
    if (row == 2u)
      return drawing::stringFromUtf8(labels.bias);
    if (row == 3u)
      return _isAux ? "RETURN" : "LEVEL";
    return _isAux ? "LOOP" : "BYPASS";
  }

  void drawOriginal(drawing::Rect dirty) {
    (void)dirty;
    auto *plugin = static_cast<Plugin *>(_plugin);
    if (!plugin)
      return;
    const auto visible = uiSnapshot(*plugin);
    drawing::Style style;
    drawing::setFill(style.bg);
    drawing::rectFill(bounds());
    drawing::Attrs title = drawing::softTitleAttrs();
    drawing::Attrs label = drawing::softLabelAttrs();
    drawing::Attrs valueAttrs = drawing::softValueAttrs();

    drawing::setFill(mixerColor(0x151515));
    drawing::rectFill(drawing::inset(bounds(), 12.0, 12.0));
    drawing::setStroke(mixerColor(0x595959));
    drawing::frameRect(drawing::inset(bounds(), 12.0, 12.0));
    drawing::setFill(
        mixerColor(_isAux ? (_bus == 0u ? 0xc95e3b : 0x57bfc4) : 0xb8b8b8));
    drawing::rectFill(drawing::makeRect(12.0, 12.0, 5.0, 50.0));

    const auto type = effectType();
    drawing::String typeName =
        drawing::stringFromUtf8(s3g::noInputDistortionName(type));
    drawing::drawAtPoint_withAttributes("EFFECT EDITOR",
                                        drawing::makePoint(28.0, 21.0), title);
    drawing::String target =
        _isAux ? drawing::stringFormat("AUX %c  /  %@", 'A' + _bus, typeName)
               : drawing::stringFormat("LANE %u  /  SLOT %u  /  %@", _lane + 1u,
                                       _slot + 1u, typeName);
    drawing::drawAtPoint_withAttributes(target, drawing::makePoint(28.0, 43.0),
                                        valueAttrs);

    for (uint32_t row = 0u; row < controlCount(); ++row) {
      const clap_id id = paramForRow(row);
      double current = 0.0;
      ParamRange range;
      if (!uiParameterValue(*plugin, id, current) || !paramRange(id, range))
        continue;
      drawing::String name = labelForRow(row);
      drawing::drawAtPoint_withAttributes(
          name, drawing::makePoint(28.0, 82.0 + row * 40.0), label);
      if (!_isAux && row == 4u) {
        drawFlatButton(effectEditorToggleRect(row),
                       current >= 0.5 ? "BYPASSED" : "ACTIVE", current >= 0.5,
                       valueAttrs);
        continue;
      }
      const drawing::Rect track = effectEditorTrackRect(row);
      drawing::setFill(mixerColor(0x080808));
      drawing::rectFill(track);
      drawing::setStroke(mixerColor(0x454545));
      drawing::frameRect(track);
      const double normalized = static_cast<double>(std::clamp(
          (current - range.minimum) / (range.maximum - range.minimum), 0.0,
          1.0));
      drawing::Rect fill = drawing::inset(track, 1.0, 1.0);
      fill.size.width *= normalized;
      drawing::setFill(mixerColor(
          _isAux ? (_bus == 0u ? 0xc95e3b : 0x57bfc4) : 0xb8b8b8, 0.88));
      drawing::rectFill(fill);
      char display[64]{};
      if (type == s3g::NoInputDistortionType::Splice && row == 0u) {
        std::snprintf(display, sizeof(display), "%.0f%% WET",
                      (0.25 + current * 0.75) * 100.0);
      } else if (type == s3g::NoInputDistortionType::Splice && row == 1u) {
        const double maximumMs =
            std::min(80.0, 8190.0 * 1000.0 / std::max(1.0, plugin->sampleRate));
        const double lengthMs = std::pow(maximumMs, current * current);
        std::snprintf(display, sizeof(display), "%.1f ms", lengthMs);
      } else if (type == s3g::NoInputDistortionType::Splice && row == 2u) {
        std::snprintf(display, sizeof(display), "%s",
                      current < -0.12
                          ? "REVERSE"
                          : (current > 0.12 ? "FORWARD" : "ALTERNATE"));
      } else if (type == s3g::NoInputDistortionType::Rotor && row == 1u) {
        std::snprintf(display, sizeof(display), "%.2f Hz",
                      0.08 * std::pow(562.5, current));
      } else if (type == s3g::NoInputDistortionType::Rotor && row == 2u) {
        std::snprintf(display, sizeof(display), "%s",
                      current < -0.12 ? "NARROW"
                                      : (current > 0.12 ? "WIDE" : "SINE"));
      } else if (type == s3g::NoInputDistortionType::Phase && row == 1u) {
        std::snprintf(display, sizeof(display), "%.2f Hz",
                      0.03 * std::pow(266.6667, current));
      } else if (type == s3g::NoInputDistortionType::Phase && row == 2u) {
        std::snprintf(display, sizeof(display), "%.0f Hz",
                      520.0 * std::pow(2.0, current * 2.0));
      } else if (type == s3g::NoInputDistortionType::Chorus && row == 1u) {
        std::snprintf(display, sizeof(display), "%.2f Hz",
                      0.05 * std::pow(120.0, current));
      } else if (type == s3g::NoInputDistortionType::Chorus && row == 2u) {
        std::snprintf(display, sizeof(display), "%+.0f%%", current * 38.0);
      } else if (type == s3g::NoInputDistortionType::Throat && row == 2u) {
        std::snprintf(display, sizeof(display), "%+.1f st", current * 8.64);
      } else if (type == s3g::NoInputDistortionType::Robot && row == 1u) {
        std::snprintf(display, sizeof(display), "%.0f Hz",
                      28.0 * std::pow(40.0, current));
      } else if (type == s3g::NoInputDistortionType::Robot && row == 2u) {
        std::snprintf(display, sizeof(display), "%.0f%% SQUARE",
                      (current + 1.0) * 50.0);
      } else if (type == s3g::NoInputDistortionType::OctDown && row == 2u) {
        std::snprintf(display, sizeof(display), "%.3f",
                      0.001 + (current + 1.0) * 0.0495);
      } else if (type == s3g::NoInputDistortionType::OctStack && row == 2u) {
        std::snprintf(display, sizeof(display), "%s",
                      current < -0.12 ? "DOWN"
                                      : (current > 0.12 ? "UP" : "EVEN"));
      } else {
        paramsValueToText(nullptr, id, current, display, sizeof(display));
      }
      drawing::String text = drawing::stringFromUtf8(display);
      const drawing::Size textSize =
          drawing::sizeWithAttributes(text, valueAttrs);
      drawing::drawAtPoint_withAttributes(
          text,
          drawing::makePoint(drawing::maxX(track) - textSize.width,
                             track.origin.y - 18.0),
          valueAttrs);
    }
    drawing::drawAtPoint_withAttributes("DRAG TRACKS  ·  DOUBLE-CLICK DEFAULT",
                                        drawing::makePoint(28.0, 270.0),
                                        valueAttrs);
  }

  void updateDrag(drawing::Point point) {
    if (_dragParam == CLAP_INVALID_ID || !apply)
      return;
    const double normalized =
        std::clamp((point.x - _dragRect.origin.x) /
                       std::max(1.0, static_cast<double>(_dragRect.size.width)),
                   0.0, 1.0);
    apply(_dragParam, _dragMinimum + normalized * (_dragMaximum - _dragMinimum),
          true);
    setNeedsDisplay(true);
  }

  void mouseDown(drawing::Event *event) {
    focus();
    const drawing::Point point =
        convertPoint_fromView(event->locationInWindow(), nullptr);
    auto *plugin = static_cast<Plugin *>(_plugin);
    if (!plugin || !apply)
      return;
    for (uint32_t row = 0u; row < controlCount(); ++row) {
      const clap_id id = paramForRow(row);
      if (!_isAux && row == 4u) {
        if (!drawing::contains(point, effectEditorToggleRect(row)))
          continue;
        double current = 0.0;
        if (uiParameterValue(*plugin, id, current)) {
          apply(id, current >= 0.5 ? 0.0 : 1.0, _dragParam == id);
        }
        return;
      }
      const drawing::Rect track = effectEditorTrackRect(row);
      if (!drawing::contains(point, drawing::inset(track, -8.0, -8.0)))
        continue;
      double defaultValue = 0.0;
      if (drawing::sliderDoubleClickDefault(event, &plugin->plugin, id,
                                            &defaultValue)) {
        apply(id, defaultValue, _dragParam == id);
        _dragParam = CLAP_INVALID_ID;
        return;
      }
      ParamRange range;
      if (!paramRange(id, range))
        return;
      _dragParam = id;
      _dragRect = track;
      _dragMinimum = range.minimum;
      _dragMaximum = range.maximum;
      updateDrag(point);
      return;
    }
  }

  void mouseDragged(drawing::Event *event) {
    updateDrag(convertPoint_fromView(event->locationInWindow(), nullptr));
  }

  void mouseUp(drawing::Event *event) {
    (void)event;
    _dragParam = CLAP_INVALID_ID;
  }
};
class Editor final : public s3g::portable_gui::routing::Canvas {
public:
  using Canvas = s3g::portable_gui::routing::Canvas;

  Plugin *_plugin;
  clap_id _dragParam;
  int _openMenu;
  int _hoverMenuItem;
  drawing::Rect _menuAnchor;
  uint32_t _effectMenuLane;
  uint32_t _effectMenuSlot;
  uint32_t _effectMenuBus;
  uint32_t _randomEnergyProfile;
  uint32_t _movementBank;
  char _titlePresetName[64];
  clap_id _mixerDragParam;
  drawing::Rect _mixerDragRect;
  double _mixerDragMinimum;
  double _mixerDragMaximum;
  bool _mixerDragVertical;
  bool _mixerPopupChild;
  Editor *_mixerPopupOwner;
  std::unique_ptr<foundation::AuxiliaryWindow> _pagePanels[kPageCount];
  Editor *_pagePopupViews[kPageCount];
  std::unique_ptr<foundation::AuxiliaryWindow> _effectPanel;
  EffectEditor *_effectEditor;
  uint32_t _lockedPage;
  bool _wiringGridMode;
  int _wireDragSource;
  drawing::Point _wireDragPoint;
  bool _surfaceEdit;
  int _selectedSurfaceCell;
  int _surfaceDragCell;
#include "s3g_complex_processor_nim_mixer_editor.inc"
  explicit Editor(Plugin &plugin) : Canvas(kGuiWidth, kGuiHeight) {
    _plugin = &plugin;
    _dragParam = CLAP_INVALID_ID;
    _openMenu = kMenuNone;
    _hoverMenuItem = -1;
    _menuAnchor = drawing::Rect{};
    _effectMenuLane = 0u;
    _effectMenuSlot = 0u;
    _effectMenuBus = 0u;
    _randomEnergyProfile = static_cast<uint32_t>(s3g::NoInputRandomEnergy::Mid);
    _movementBank = 0u;
    _mixerDragParam = CLAP_INVALID_ID;
    _mixerDragRect = drawing::Rect{};
    _mixerDragMinimum = 0.0;
    _mixerDragMaximum = 1.0;
    _mixerDragVertical = false;
    _mixerPopupChild = false;
    _mixerPopupOwner = nullptr;
    for (uint32_t page = 0u; page < kPageCount; ++page) {
      _pagePanels[page] = nullptr;
      _pagePopupViews[page] = nullptr;
    }
    _effectPanel = nullptr;
    _effectEditor = nullptr;
    _lockedPage = UINT32_MAX;
    _wiringGridMode = false;
    _wireDragSource = -1;
    _wireDragPoint = drawing::Point{};
    _surfaceEdit = false;
    _selectedSurfaceCell = -1;
    _surfaceDragCell = -1;
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "INIT");
  }

  uint32_t activePage() {
    if (_lockedPage < kPageCount)
      return _lockedPage;
    auto *plugin = static_cast<Plugin *>(_plugin);
    return plugin ? plugin->guiPage.load(std::memory_order_relaxed) : 0u;
  }

  void markPatchCustom() {
    if (_mixerPopupChild && _mixerPopupOwner) {
      _mixerPopupOwner->markPatchCustom();
      return;
    }
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "CUSTOM");
  }

  void clearAllConnections() {
    auto *plugin = static_cast<Plugin *>(_plugin);
    if (!plugin)
      return;
    for (uint32_t index = 0u; index < s3g::kNoInputMixerMatrixCells; ++index) {
      publishUiParameter(*plugin, kMatrixParamBase + index, 0.0);
    }
    if (!enqueueGuiCommand(*plugin, {GuiCommandType::ClearMatrix}))
      return;
    markPatchCustom();
    setNeedsDisplay(true);
    Editor *owner =
        _mixerPopupChild && _mixerPopupOwner ? _mixerPopupOwner : this;
    owner->setNeedsDisplay(true);
    for (uint32_t page = 0u; page < kPageCount; ++page) {
      if (owner->_pagePopupViews[page])
        owner->_pagePopupViews[page]->setNeedsDisplay(true);
    }
  }

  void beginMixerDrag_rect_minimum_maximum_vertical_point(
      clap_id param, drawing::Rect rect, double minimum, double maximum,
      bool vertical, drawing::Point point) {
    _dragParam = CLAP_INVALID_ID;
    _mixerDragParam = param;
    _mixerDragRect = rect;
    _mixerDragMinimum = minimum;
    _mixerDragMaximum = maximum;
    _mixerDragVertical = vertical;
    updateMixerDrag(point);
  }

  void updateMixerDrag(drawing::Point point) {
    if (_mixerDragParam == CLAP_INVALID_ID)
      return;
    const double span =
        _mixerDragVertical
            ? std::max(1.0, static_cast<double>(_mixerDragRect.size.height))
            : std::max(1.0, static_cast<double>(_mixerDragRect.size.width));
    const double normalized = std::clamp(
        _mixerDragVertical ? (drawing::maxY(_mixerDragRect) - point.y) / span
                           : (point.x - _mixerDragRect.origin.x) / span,
        0.0, 1.0);
    applyGuiParam_value(_mixerDragParam,
                        _mixerDragMinimum + normalized * (_mixerDragMaximum -
                                                          _mixerDragMinimum));
  }

  void updateSurfaceDrag(drawing::Point point) {
    auto *plugin = static_cast<Plugin *>(_plugin);
    if (!plugin || _surfaceDragCell == -1)
      return;
    const drawing::Rect plot = surfacePlotRect();
    const float x = std::clamp<float>(
        (point.x - plot.origin.x) / plot.size.width, 0.0f, 1.0f);
    const float y = std::clamp<float>(
        (drawing::maxY(plot) - point.y) / plot.size.height, 0.0f, 1.0f);
    if (_surfaceDragCell == -2) {
      applyGuiParam_value(kSurfaceXParamId, x);
      applyGuiParam_value(kSurfaceYParamId, y);
      return;
    }
    const uint32_t index = static_cast<uint32_t>(_surfaceDragCell);
    if (index >= plugin->surface.cellCount)
      return;
    plugin->surface.cells[index].x = x;
    plugin->surface.cells[index].y = y;
    setNeedsDisplay(true);
  }

  int hoverMenuItem() { return _hoverMenuItem; }

  void drawSlider_value_norm_row_panel_label_valueAttrs_style(
      drawing::String name, drawing::String value, double norm, uint32_t row,
      const s3g::gui_layout::Panel &panel, drawing::Attrs label,
      drawing::Attrs valueAttrs, const drawing::Style &style) {
    drawing::drawProcessorSlider(
        name, value, norm, s3g::gui_layout::rowY(panel, row), panel.frame.x,
        panel.frame.width, label, valueAttrs, style);
  }

  void drawPrimaryWiring_rect_label_valueAttrs(Plugin *plugin,
                                               drawing::Rect rect,
                                               drawing::Attrs label,
                                               drawing::Attrs valueAttrs) {
    const auto visible = uiSnapshot(*plugin);
    drawing::setFill(mixerColor(0x101010));
    drawing::rectFill(rect);
    drawing::setStroke(mixerColor(0x454545));
    drawing::frameRect(rect);
    const uint32_t matrixMidiMode =
        plugin->matrixMidiMode.load(std::memory_order_relaxed);
    drawFlatButton(matrixMidiModeButtonRect(0u), "BU16 FLIP",
                   matrixMidiMode ==
                       static_cast<uint32_t>(s3g::NoInputMatrixMidiMode::Flip),
                   valueAttrs);
    drawFlatButton(matrixMidiModeButtonRect(1u), "BU16 LATCH",
                   matrixMidiMode ==
                       static_cast<uint32_t>(s3g::NoInputMatrixMidiMode::Latch),
                   valueAttrs);
    const uint32_t matrixMidiSign =
        plugin->matrixMidiSign.load(std::memory_order_relaxed);
    drawPolarityButton(
        matrixMidiSignButtonRect(0u), "NEW +",
        matrixMidiSign ==
            static_cast<uint32_t>(s3g::NoInputMatrixMidiSign::Positive),
        0xc95e3b, valueAttrs);
    drawPolarityButton(
        matrixMidiSignButtonRect(1u), "NEW -",
        matrixMidiSign ==
            static_cast<uint32_t>(s3g::NoInputMatrixMidiSign::Negative),
        0x57bfc4, valueAttrs);
    const float matrixMidiRampMs =
        plugin->matrixMidiRampMs.load(std::memory_order_relaxed);
    const double rampNorm = std::sqrt(std::clamp<double>(
        (matrixMidiRampMs - s3g::kNoInputMatrixMidiRampMinimumMs) /
            (s3g::kNoInputMatrixMidiRampMaximumMs -
             s3g::kNoInputMatrixMidiRampMinimumMs),
        0.0, 1.0));
    const drawing::Rect rampTrack = matrixMidiRampTrackRect();
    drawing::drawAtPoint_withAttributes(
        "RAMP",
        drawing::makePoint(rampTrack.origin.x - 40.0, rampTrack.origin.y - 5.0),
        valueAttrs);
    drawing::setFill(mixerColor(0x080808));
    drawing::rectFill(rampTrack);
    drawing::setStroke(mixerColor(0x454545));
    drawing::frameRect(rampTrack);
    drawing::setFill(mixerColor(0xc95e3b, 0.88));
    drawing::Rect rampFill = drawing::inset(rampTrack, 1.0, 1.0);
    rampFill.size.width *= rampNorm;
    drawing::rectFill(rampFill);
    drawing::setFill(mixerColor(0xe0e0e0));
    drawing::rectFill(drawing::makeRect(
        rampTrack.origin.x + rampNorm * rampTrack.size.width - 1.5,
        rampTrack.origin.y - 2.0, 3.0, rampTrack.size.height + 4.0));
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat("%.0f MS", matrixMidiRampMs),
        drawing::makePoint(drawing::maxX(rampTrack) + 8.0,
                           rampTrack.origin.y - 5.0),
        valueAttrs);
    drawFlatButton(wiringModeButtonRect(0u), "WIRES", !_wiringGridMode,
                   valueAttrs);
    drawFlatButton(wiringModeButtonRect(1u), "GRID", _wiringGridMode,
                   valueAttrs);
    drawFlatButton(clearConnectionsButtonRect(), "CLEAR ALL", false,
                   valueAttrs);
    if (_wiringGridMode) {
      drawPrimaryMatrix_rect_label_valueAttrs(plugin, wiringGridRect(), label,
                                              valueAttrs);
      return;
    }

    const uint64_t scopeSequence =
        plugin->routeScopeSequence.load(std::memory_order_acquire);
    const uint32_t selectedSource =
        plugin->selectedSource.load(std::memory_order_relaxed);
    const uint32_t selectedDestination =
        plugin->selectedDestination.load(std::memory_order_relaxed);
    double selectedRms = 0.0;

    for (uint32_t destination = 0u; destination < kChannelCount;
         ++destination) {
      for (uint32_t source = 0u; source < kChannelCount; ++source) {
        const uint32_t index = destination * kChannelCount + source;
        const float stored = displayedMatrixGain(*plugin, index);
        const double manual = std::abs(stored);
        if (manual <= 0.001f)
          continue;
        const drawing::Point a = wiringPortPoint(false, source);
        const drawing::Point b = wiringPortPoint(true, destination);
        drawing::Point c1;
        drawing::Point c2;
        wiringControlPoints(a, b, visible.params.vortex, c1, c2);
        drawing::Path path = drawing::Path::bezierPath();
        path.moveToPoint(a);
        path.curveToPoint_controlPoint1_controlPoint2(b, c1, c2);
        const bool selected =
            source == selectedSource && destination == selectedDestination;
        if (selected) {
          path.setLineWidth(5.0 + manual * 5.0);
          drawing::setStroke(mixerColor(0xb8b8b8, 0.22));
          path.stroke();
        }
        path.setLineWidth(0.7 + manual * 5.2);
        drawing::setStroke(mixerColor(stored >= 0.0f ? 0xc95e3b : 0x57bfc4,
                                      0.12 + manual * 0.32));
        path.stroke();

        std::array<double, kRouteScopeSamples> samples{};
        double peak = 0.0;
        double sumSquares = 0.0;
        const uint32_t available = static_cast<uint32_t>(
            std::min<uint64_t>(scopeSequence, kRouteScopeSamples));
        const uint32_t padding = kRouteScopeSamples - available;
        const uint64_t first = scopeSequence - available;
        for (uint32_t sample = padding; sample < kRouteScopeSamples; ++sample) {
          const uint64_t sequence = first + sample - padding;
          const uint32_t slot =
              static_cast<uint32_t>(sequence % kRouteScopeSamples);
          const double value =
              plugin->routeScope[index * kRouteScopeSamples + slot].load(
                  std::memory_order_relaxed);
          samples[sample] = std::isfinite(value) ? value : 0.0;
          peak = std::max(peak, std::abs(samples[sample]));
          sumSquares += samples[sample] * samples[sample];
        }
        const double rms =
            std::sqrt(sumSquares / static_cast<double>(kRouteScopeSamples));
        const double levelDb = 20.0 * std::log10(std::max<double>(rms, 1.0e-6));
        const double level =
            std::clamp<double>((levelDb + 72.0) / 66.0, 0.0, 1.0);
        if (selected)
          selectedRms = rms;
        if (peak > 1.0e-7) {
          drawing::Path waveform = drawing::Path::bezierPath();
          const double deviation = 1.2 + level * 8.8;
          for (uint32_t sample = 0u; sample < kRouteScopeSamples; ++sample) {
            const double t = (static_cast<double>(sample) + 0.5) /
                             static_cast<double>(kRouteScopeSamples);
            const drawing::Point center = cubicPoint(a, c1, c2, b, t);
            const drawing::Point tangent = cubicTangent(a, c1, c2, b, t);
            const double length =
                std::max<double>(1.0, std::hypot(tangent.x, tangent.y));
            const double normalized =
                std::clamp<double>(samples[sample] / peak, -1.0, 1.0);
            const drawing::Point signal = drawing::makePoint(
                center.x - tangent.y / length * normalized * deviation,
                center.y + tangent.x / length * normalized * deviation);
            if (sample == 0u)
              waveform.moveToPoint(signal);
            else
              waveform.lineToPoint(signal);
          }
          // Rounded cable corners match the Cocoa trace.
          waveform.setLineWidth(2.8 + level * 1.8);
          drawing::setStroke(mixerColor(0x050505, 0.80));
          drawing::strokeRoundPath(waveform);
          waveform.setLineWidth(0.9 + level * 1.5);
          drawing::setStroke(mixerColor(stored >= 0.0f ? 0xff9a73 : 0x9cf8fb,
                                        0.24 + level * 0.76));
          drawing::strokeRoundPath(waveform);
        }
      }
    }

    if (_wireDragSource >= 0) {
      const drawing::Point a =
          wiringPortPoint(false, static_cast<uint32_t>(_wireDragSource));
      const drawing::Point b = _wireDragPoint;
      drawing::Path cable = drawing::Path::bezierPath();
      cable.moveToPoint(a);
      cable.curveToPoint_controlPoint1_controlPoint2(
          b, drawing::makePoint(a.x + 180.0, a.y),
          drawing::makePoint(b.x - 180.0, b.y));
      cable.setLineWidth(2.0);
      drawing::setStroke(mixerColor(0xb8b8b8, 0.72));
      cable.stroke();
    }

    for (uint32_t lane = 0u; lane < kChannelCount; ++lane) {
      const drawing::Point source = wiringPortPoint(false, lane);
      const drawing::Point destination = wiringPortPoint(true, lane);
      const double sourceActivity = std::clamp<double>(
          plugin->laneActivity[lane].load(std::memory_order_relaxed), 0.0, 1.0);
      const drawing::Rect sourcePort =
          drawing::makeRect(source.x - 12.0, source.y - 12.0, 24.0, 24.0);
      const drawing::Rect destinationPort = drawing::makeRect(
          destination.x - 12.0, destination.y - 12.0, 24.0, 24.0);
      drawing::setFill(mixerColor(0x202020));
      drawing::rectFill(sourcePort);
      drawing::rectFill(destinationPort);
      drawing::setStroke(mixerColor(0x666666));
      drawing::frameRect(sourcePort);
      drawing::frameRect(destinationPort);
      drawing::setFill(mixerColor(0x57bfc4, 0.28 + sourceActivity * 0.72));
      drawing::rectFill(drawing::inset(sourcePort, 6.0, 6.0));
      drawing::rectFill(drawing::inset(destinationPort, 6.0, 6.0));
      drawing::drawAtPoint_withAttributes(
          drawing::stringFormat("S%u", lane + 1u),
          drawing::makePoint(source.x - 39.0, source.y - 7.0), valueAttrs);
      drawing::drawAtPoint_withAttributes(
          drawing::stringFormat("D%u", lane + 1u),
          drawing::makePoint(destination.x + 17.0, destination.y - 7.0),
          valueAttrs);
    }
    const double selectedDb =
        20.0 * std::log10(std::max<double>(selectedRms, 1.0e-6));
    const auto stages = routeStageReadout(*plugin, visible, selectedSource,
                                          selectedDestination);
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat("BASE %.2f  →  FIELD %.2f  →  BEHAV %.2f  →  "
                              "RESP %.2f  =  %.2f  ·  CHOKE %.2f",
                              std::abs(stages.base), stages.field,
                              stages.behavior, stages.response,
                              stages.effective, stages.choke),
        drawing::makePoint(rect.origin.x + 18.0, drawing::maxY(rect) - 70.0),
        valueAttrs);
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat(
            "ROUTED AUDIO  S%u > D%u  %+.1f dBFS  ·  24 kHz SCOPE",
            selectedSource + 1u, selectedDestination + 1u, selectedDb),
        drawing::makePoint(rect.origin.x + 18.0, drawing::maxY(rect) - 48.0),
        label);
    drawing::drawAtPoint_withAttributes(
        "DRAG SOURCE TO DESTINATION · OPTION: NEGATIVE · CLICK WIRE: SELECT / "
        "DISSOLVE",
        drawing::makePoint(rect.origin.x + 18.0, drawing::maxY(rect) - 25.0),
        valueAttrs);
  }

  void drawPrimaryMatrix_rect_label_valueAttrs(Plugin *plugin,
                                               drawing::Rect rect,
                                               drawing::Attrs label,
                                               drawing::Attrs valueAttrs) {
    const auto visible = uiSnapshot(*plugin);
    drawing::setFill(mixerColor(0x101010));
    drawing::rectFill(rect);
    drawing::setStroke(mixerColor(0x454545));
    drawing::frameRect(rect);
    const double gridLeft = rect.origin.x + 54.0;
    const double gridTop = rect.origin.y + 36.0;
    const double spacing = 58.0;
    const double gridExtent = spacing * 7.0;
    const uint32_t selectedSource =
        plugin->selectedSource.load(std::memory_order_relaxed);
    const uint32_t selectedDestination =
        plugin->selectedDestination.load(std::memory_order_relaxed);
    auto motionWeights = s3g::noInputMixerMotionWeights(
        visible.params, plugin->motionPhase.load(std::memory_order_relaxed));
    if (visible.behavior.behavior == s3g::NoInputMovementBehavior::Step) {
      for (uint32_t index = 0u; index < s3g::kNoInputMixerMatrixCells;
           ++index) {
        motionWeights[index] = s3g::lerp(
            motionWeights[index],
            plugin->behaviorRouteGate[index].load(std::memory_order_relaxed),
            visible.behaviorDepth);
      }
    }
    std::array<float, kChannelCount> activeMotionPeak{};
    std::array<uint32_t, kChannelCount> activeMotionRouteCount{};
    for (uint32_t destination = 0u; destination < kChannelCount;
         ++destination) {
      for (uint32_t source = 0u; source < kChannelCount; ++source) {
        const uint32_t index = destination * kChannelCount + source;
        if (std::abs(displayedMatrixGain(*plugin, index)) <= 0.001f)
          continue;
        activeMotionPeak[source] =
            std::max(activeMotionPeak[source], motionWeights[index]);
        ++activeMotionRouteCount[source];
      }
    }

    drawing::setStroke(mixerColor(0x343434));
    for (uint32_t index = 0u; index < kChannelCount; ++index) {
      const double x = gridLeft + spacing * index;
      const double y = gridTop + spacing * index;
      drawing::Path::strokeLineFromPoint_toPoint(
          drawing::makePoint(x, gridTop),
          drawing::makePoint(x, gridTop + gridExtent));
      drawing::Path::strokeLineFromPoint_toPoint(
          drawing::makePoint(gridLeft, y),
          drawing::makePoint(gridLeft + gridExtent, y));
      drawing::drawAtPoint_withAttributes(
          drawing::stringFormat("S%u", index + 1u),
          drawing::makePoint(x - 7.0, gridTop - 24.0), valueAttrs);
      drawing::drawAtPoint_withAttributes(
          drawing::stringFormat("D%u", index + 1u),
          drawing::makePoint(gridLeft - 34.0, y - 6.0), valueAttrs);
    }

    for (uint32_t destination = 0u; destination < kChannelCount;
         ++destination) {
      for (uint32_t source = 0u; source < kChannelCount; ++source) {
        const float gain =
            displayedMatrixGain(*plugin, destination * kChannelCount + source);
        const double x = gridLeft + spacing * source;
        const double y = gridTop + spacing * destination;
        const drawing::Rect node =
            drawing::makeRect(x - 7.0, y - 7.0, 14.0, 14.0);
        drawing::setFill(mixerColor(0x141414));
        drawing::rectFill(node);
        if (std::abs(gain) > 0.001f) {
          const uint32_t index = destination * kChannelCount + source;
          double motion = s3g::noInputMixerMotionGainScale(
              motionWeights[index], activeMotionPeak[source],
              activeMotionRouteCount[source], visible.params.motion);
          if (s3g::noInputMovementBehaviorUsesAmplitude(
                  visible.behavior.behavior)) {
            const float articulation = plugin->behaviorRouteGate[index].load(
                std::memory_order_relaxed);
            motion *= s3g::lerp(1.0f, articulation, visible.behaviorDepth);
          }
          if (visible.params.reactMode != s3g::NoInputReactMode::Off &&
              visible.params.reactDepth > 1.0e-6f) {
            const float response =
                0.0316227766f +
                plugin->reactRouteGate[index].load(std::memory_order_relaxed) *
                    0.9683772234f;
            motion *= s3g::lerp(1.0f, response, visible.params.reactDepth);
          }
          drawing::setFill(mixerColor(gain >= 0.0f ? 0xc95e3b : 0x5daeb6,
                                      0.18 + std::abs(gain) * 0.40));
          drawing::rectFill(drawing::inset(node, 1.0, 1.0));
          const double effectiveGain = std::abs(gain) * motion;
          const double liveSize = 2.0 + 10.0 * effectiveGain;
          drawing::setFill(mixerColor(gain >= 0.0f ? 0xff7047 : 0x69d2dc,
                                      0.40 + motion * 0.55));
          drawing::rectFill(drawing::makeRect(
              x - liveSize * 0.5, y - liveSize * 0.5, liveSize, liveSize));
        }
        drawing::setStroke(mixerColor(
            (source == selectedSource && destination == selectedDestination)
                ? 0xc8c8c8
                : 0x5a5a5a));
        drawing::frameRect(node);
      }
    }

    const double meterTop = gridTop + gridExtent + 38.0;
    const double meterHeight = 88.0;
    drawing::drawAtPoint_withAttributes(
        "OUTPUT PEAK · dBFS",
        drawing::makePoint(rect.origin.x + 8.0, meterTop - 22.0), label);
    constexpr double meterFloorDb = -60.0;
    for (double tickDb : {-60.0, -30.0, 0.0}) {
      const double normalized = (tickDb - meterFloorDb) / -meterFloorDb;
      const double y = meterTop + meterHeight * (1.0 - normalized);
      drawing::setStroke(mixerColor(0x353535, tickDb == -30.0 ? 0.58 : 0.34));
      drawing::Path::strokeLineFromPoint_toPoint(
          drawing::makePoint(gridLeft - 9.0, y),
          drawing::makePoint(gridLeft + gridExtent + 9.0, y));
      drawing::drawAtPoint_withAttributes(
          drawing::stringFormat("%.0f", tickDb),
          drawing::makePoint(rect.origin.x + 16.0, y - 6.0), valueAttrs);
    }
    for (uint32_t lane = 0u; lane < kChannelCount; ++lane) {
      const double x = gridLeft + spacing * lane;
      drawing::Rect track =
          drawing::makeRect(x - 7.0, meterTop, 14.0, meterHeight);
      drawing::setFill(mixerColor(0x070707));
      drawing::rectFill(track);
      drawing::setStroke(mixerColor(0x3e3e3e));
      drawing::frameRect(track);
      const double peak = std::max<double>(
          1.0e-6, plugin->lanePeaks[lane].load(std::memory_order_relaxed));
      const double peakDb =
          std::clamp<double>(20.0 * std::log10(peak), meterFloorDb, 0.0);
      const double normalized = (peakDb - meterFloorDb) / -meterFloorDb;
      drawing::Rect fill = drawing::inset(track, 2.0, 2.0);
      fill.origin.y += fill.size.height * (1.0 - normalized);
      fill.size.height *= normalized;
      drawing::setFill(mixerColor(0x57bfc4, 0.85));
      drawing::rectFill(fill);
      drawing::String reading = peakDb <= meterFloorDb + 0.1
                                    ? "−∞"
                                    : drawing::stringFormat("%.0f", peakDb);
      const drawing::Size readingSize =
          drawing::sizeWithAttributes(reading, valueAttrs);
      drawing::drawAtPoint_withAttributes(
          reading,
          drawing::makePoint(x - readingSize.width * 0.5,
                             drawing::maxY(track) + 4.0),
          valueAttrs);
    }
    constexpr double motionRailMargin = 18.0;
    const drawing::Rect motionRail =
        drawing::makeRect(gridLeft - motionRailMargin, rect.origin.y + 598.0,
                          gridExtent + motionRailMargin * 2.0, 38.0);
    drawing::setFill(mixerColor(0x0a0a0a));
    drawing::rectFill(motionRail);
    drawing::setStroke(mixerColor(0x454545));
    drawing::frameRect(motionRail);
    const float rateHz =
        visible.behavior.behavior == s3g::NoInputMovementBehavior::Glide
            ? s3g::noInputMixerMotionRateHz(visible.params.motionRate)
            : s3g::noInputMovementEventRateHz(visible.behavior.eventRate);
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat(
            "ROUTE GAIN · %@ / %@ %.0f%% · RESP %@ %.0f%% · FIELD %.0f%% · "
            "%.2f Hz",
            drawing::stringFromUtf8(
                s3g::matrixFlowShapeName(visible.params.motionShape)),
            drawing::stringFromUtf8(
                s3g::noInputMovementBehaviorName(visible.behavior.behavior)),
            visible.behaviorDepth * 100.0f,
            drawing::stringFromUtf8(
                s3g::noInputReactModeName(visible.params.reactMode)),
            visible.params.reactDepth * 100.0f, visible.params.motion * 100.0f,
            rateHz),
        drawing::makePoint(motionRail.origin.x + 8.0,
                           motionRail.origin.y + 4.0),
        valueAttrs);
    const auto stages = routeStageReadout(*plugin, visible, selectedSource,
                                          selectedDestination);
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat("S%u>D%u  BASE %.2f · FIELD %.2f · BEHAV %.2f · "
                              "RESP %.2f · EFF %.2f · CHOKE %.2f",
                              selectedSource + 1u, selectedDestination + 1u,
                              std::abs(stages.base), stages.field,
                              stages.behavior, stages.response,
                              stages.effective, stages.choke),
        drawing::makePoint(motionRail.origin.x + 8.0,
                           motionRail.origin.y + 19.0),
        valueAttrs);
    drawing::drawAtPoint_withAttributes(
        "CLICK: PATCH / SELECT · CLICK AGAIN: DISSOLVE · OPTION: NEGATIVE",
        drawing::makePoint(rect.origin.x + 14.0, drawing::maxY(rect) - 19.0),
        valueAttrs);
  }

  void drawPrimaryLanes_rect_label_valueAttrs(Plugin *plugin,
                                              drawing::Rect rect,
                                              drawing::Attrs label,
                                              drawing::Attrs valueAttrs) {
    const auto visible = uiSnapshot(*plugin);
    drawing::setFill(mixerColor(0x101010));
    drawing::rectFill(rect);
    drawing::setStroke(mixerColor(0x454545));
    drawing::frameRect(rect);
    const uint32_t selected =
        plugin->selectedLane.load(std::memory_order_relaxed);
    const double gap = 12.0;
    const double cellWidth = (rect.size.width - gap * 5.0) / 4.0;
    const double cellHeight = (rect.size.height - gap * 3.0) / 2.0;
    for (uint32_t lane = 0u; lane < kChannelCount; ++lane) {
      const uint32_t column = lane % 4u;
      const uint32_t row = lane / 4u;
      drawing::Rect cell =
          drawing::makeRect(rect.origin.x + gap + column * (cellWidth + gap),
                            rect.origin.y + gap + row * (cellHeight + gap),
                            cellWidth, cellHeight);
      drawing::setFill(mixerColor(lane == selected ? 0x242424 : 0x171717));
      drawing::rectFill(cell);
      drawing::setStroke(mixerColor(lane == selected ? 0xb8b8b8 : 0x494949));
      drawing::frameRect(cell);
      drawing::drawAtPoint_withAttributes(
          drawing::stringFormat("L%u", lane + 1u),
          drawing::makePoint(cell.origin.x + 10.0, cell.origin.y + 9.0), label);
      const auto &laneParams = visible.params.lanes[lane];
      drawing::drawAtPoint_withAttributes(
          drawing::stringFormat("BODY %.0f  LOSS %.0f",
                                laneParams.body * 100.0f,
                                laneParams.loss * 100.0f),
          drawing::makePoint(cell.origin.x + 10.0, cell.origin.y + 35.0),
          valueAttrs);
      drawing::drawAtPoint_withAttributes(
          drawing::stringFormat("EQ %+.0f / %+.0f / %+.0f", laneParams.lowDb,
                                laneParams.midGainDb, laneParams.highDb),
          drawing::makePoint(cell.origin.x + 10.0, cell.origin.y + 57.0),
          valueAttrs);
      for (uint32_t slot = 0u; slot < s3g::kNoInputMixerInsertSlots; ++slot) {
        drawing::String text = drawing::stringFormat(
            "%u %@%@", slot + 1u,
            drawing::stringFromUtf8(
                s3g::noInputDistortionName(laneParams.inserts[slot].type)),
            laneParams.inserts[slot].bypass != 0u ? " BYP" : "");
        drawing::drawAtPoint_withAttributes(
            text,
            drawing::makePoint(cell.origin.x + 10.0,
                               cell.origin.y + 91.0 + slot * 22.0),
            valueAttrs);
      }
      drawing::Rect activity =
          drawing::makeRect(cell.origin.x + 10.0, drawing::maxY(cell) - 25.0,
                            cell.size.width - 20.0, 8.0);
      drawing::setFill(mixerColor(0x090909));
      drawing::rectFill(activity);
      drawing::setStroke(mixerColor(0x404040));
      drawing::frameRect(activity);
      drawing::Rect activityFill = drawing::inset(activity, 1.0, 1.0);
      activityFill.size.width *= std::clamp<double>(
          plugin->laneActivity[lane].load(std::memory_order_relaxed), 0.0, 1.0);
      drawing::setFill(mixerColor(0x57bfc4, 0.8));
      drawing::rectFill(activityFill);
    }
  }

  drawing::Rect menuAnchorRect(int menu) {
    const auto &family = s3g::gui_layout::kNoInputMixerFamilyLayout;
    switch (menu) {
    case kMenuPreset:
      return drawing::cocoaRect(
          s3g::gui_layout::matrixTitleBand(family.canvas).presetMenu);
    case kMenuRandomEnergy:
      return randomButtonRect();
    case kMenuLane:
      return processorMenuRect(family.selectedLane, 0u);
    case kMenuSource:
      return processorMenuRect(family.crosspoint, 0u);
    case kMenuDestination:
      return processorMenuRect(family.crosspoint, 1u);
    case kMenuSlot0:
      return channelInsertMenuRect(0u);
    case kMenuSlot1:
      return channelInsertMenuRect(1u);
    case kMenuSlot2:
      return channelInsertMenuRect(2u);
    case kMenuMotionShape:
      return processorMenuRect(family.movement, 0u);
    case kMenuBehavior:
      return processorMenuRect(family.movement, 0u);
    case kMenuQuality:
      return drawing::makeRect(family.containment.frame.x + 108.0,
                               family.containment.frame.y + 35.0, 110.0, 15.0);
    case kMenuOutputFormat:
      return processorMenuRect(family.output, 2u);
    case kMenuReactMode:
      return processorMenuRect(family.movement, 0u);
    case kMenuFieldDivision:
      return processorMenuRect(family.movement, 5u);
    case kMenuEventDivision:
      return processorMenuRect(family.movement, 2u);
    case kMenuMixerInsert:
    case kMenuMixerAux:
    case kMenuAuxTapA:
    case kMenuAuxTapB:
      return _menuAnchor;
    default:
      return drawing::Rect{};
    }
  }

  uint32_t menuItemCount(int menu) {
    if (menu == kMenuPreset)
      return s3g::kNoInputMixerFactoryPresetCount;
    if (menu == kMenuRandomEnergy)
      return static_cast<uint32_t>(s3g::NoInputRandomEnergy::Count);
    if (menu == kMenuSlot0 || menu == kMenuSlot1 || menu == kMenuSlot2) {
      return s3g::kNoInputDistortionTypeCount;
    }
    if (menu == kMenuMixerInsert || menu == kMenuMixerAux) {
      return s3g::kNoInputDistortionTypeCount;
    }
    if (menu == kMenuReactMode)
      return static_cast<uint32_t>(s3g::NoInputReactMode::Count);
    if (menu == kMenuFieldDivision || menu == kMenuEventDivision)
      return s3g::kNoInputClockDivisionCount;
    if (menu == kMenuAuxTapA || menu == kMenuAuxTapB)
      return static_cast<uint32_t>(s3g::NoInputAuxTap::Count);
    if (menu == kMenuMotionShape)
      return s3g::kMatrixFlowShapeCount;
    if (menu == kMenuBehavior)
      return s3g::kNoInputMovementBehaviorCount;
    if (menu == kMenuQuality)
      return 3u;
    if (menu == kMenuOutputFormat)
      return s3g::kRingOutputFormatCount;
    if (menu == kMenuLane || menu == kMenuSource || menu == kMenuDestination)
      return kChannelCount;
    return 0u;
  }

  drawing::Rect menuDropdownRect(int menu) {
    const drawing::Rect anchor = menuAnchorRect(menu);
    const double height = 18.0 * static_cast<double>(menuItemCount(menu));
    double width = anchor.size.width;
    double x = anchor.origin.x;
    double y = drawing::maxY(anchor) + 2.0;
    const bool effectMenu =
        menu == kMenuMixerInsert || menu == kMenuMixerAux ||
        menu == kMenuReactMode || menu == kMenuFieldDivision ||
        menu == kMenuEventDivision || menu == kMenuAuxTapA ||
        menu == kMenuAuxTapB || menu == kMenuSlot0 || menu == kMenuSlot1 ||
        menu == kMenuSlot2;
    if (menu == kMenuRandomEnergy)
      width = std::max<double>(154.0, width);
    if (effectMenu) {
      if (menu == kMenuMixerInsert || menu == kMenuMixerAux)
        width = std::max<double>(148.0, width);
      x = std::clamp(x, bounds().origin.x + 2.0,
                     drawing::maxX(bounds()) - width - 2.0);
      if (menu == kMenuMixerInsert ||
          y + height > drawing::maxY(bounds()) - 2.0) {
        y = anchor.origin.y - height - 2.0;
      }
      y = std::clamp(y, bounds().origin.y + 2.0,
                     drawing::maxY(bounds()) - height - 2.0);
    }
    return drawing::makeRect(x, y, width, height);
  }

  void drawOpenMenu_attrs_style(Plugin *plugin, drawing::Attrs attrs,
                                const drawing::Style &style) {
    if (_openMenu == kMenuNone)
      return;
    const auto visible = uiSnapshot(*plugin);
    drawing::String laneItems[kChannelCount] = {
        "L1", "L2", "L3", "L4", "L5", "L6", "L7", "L8",
    };
    drawing::String typeItems[s3g::kNoInputDistortionTypeCount] = {
        "BYPASS",  "WOOL",  "RAT",      "ZONE A", "ZONE B",    "FUZZ I",
        "FUZZ II", "DIODE", "RING",     "RELAY",  "CRUSH",     "SPLICE",
        "LOGIC",   "SHRED", "VOID",     "ROTOR",  "PHASE",     "CHORUS",
        "THROAT",  "ROBOT", "OCT DOWN", "OCT UP", "OCT STACK",
    };
    drawing::String qualityItems[3] = {"1X", "2X", "4X"};
    drawing::String randomEnergyItems[3] = {
        "HIGH / QUICK",
        "MID / MODERATE",
        "LOW / SLOW",
    };
    drawing::String shapeItems[s3g::kMatrixFlowShapeCount] = {
        "FLOW", "PULSE", "CHASE", "SWIRL",   "SCAT",
        "HOLD", "BLOOM", "BRAID", "ATTRACT",
    };
    drawing::String behaviorItems[s3g::kNoInputMovementBehaviorCount] = {
        "GLIDE",    "STEP",    "CUT",     "BURST",
        "SCRAMBLE", "RATCHET", "CASCADE", "ERODE",
    };
    drawing::String reactItemsFull[5] = {
        "OFF", "FOLLOW", "AVOID", "EDGE", "BALANCE",
    };
    drawing::String divisionItems[s3g::kNoInputClockDivisionCount] = {
        "1/64", "1/32",  "1/16",   "1/8",    "1/4",
        "1/2",  "1 BAR", "2 BARS", "4 BARS", "8 BARS",
    };
    drawing::String tapItems[4] = {
        "RETURN",
        "PRE EQ",
        "POST EQ",
        "POST INSERT",
    };
    drawing::String outputFormatItems[s3g::kRingOutputFormatCount] = {
        "8CH DIRECT",
        "QUAD RING",
        "STEREO RING",
    };
    drawing::String presetItems[s3g::kNoInputMixerFactoryPresetCount] = {
        "INIT",
        "CIRCUIT LATTICE",
        "RAIN FOREST",
        "WOOL RING",
        "RAT CAGE",
        "ZONE WEB",
        "NEGATIVE SPACE",
        "RELAY BLOOM",
        "OPEN HOUSE",
        "MOBILE CIRCUIT",
        "STATIC CHOIR",
        "RAZOR CLOCK",
        "SUBHARMONIC WELL",
        "SPEECH CIRCUIT",
        "SPLICE STORM",
        "PHASE ORCHARD",
        "LOGIC FLOCK",
        "OCTAVE LADDER",
        "AUX MIRROR",
        "WALL ENGINE",
    };
    drawing::String *items = laneItems;
    uint32_t count = kChannelCount;
    int selected = 0;
    if (_openMenu == kMenuPreset) {
      items = presetItems;
      count = s3g::kNoInputMixerFactoryPresetCount;
      selected = -1;
      for (uint32_t index = 0u; index < count; ++index) {
        if (std::strcmp(_titlePresetName,
                        s3g::noInputMixerFactoryPresetName(index)) == 0) {
          selected = static_cast<int>(index);
          break;
        }
      }
    } else if (_openMenu == kMenuRandomEnergy) {
      items = randomEnergyItems;
      count = static_cast<uint32_t>(s3g::NoInputRandomEnergy::Count);
      selected = static_cast<int>(_randomEnergyProfile);
    } else if (_openMenu == kMenuLane) {
      selected = static_cast<int>(
          plugin->selectedLane.load(std::memory_order_relaxed));
    } else if (_openMenu == kMenuSource) {
      selected = static_cast<int>(
          plugin->selectedSource.load(std::memory_order_relaxed));
    } else if (_openMenu == kMenuDestination) {
      selected = static_cast<int>(
          plugin->selectedDestination.load(std::memory_order_relaxed));
    } else if (_openMenu == kMenuQuality) {
      items = qualityItems;
      count = 3u;
      selected = static_cast<int>(visible.params.quality);
    } else if (_openMenu == kMenuOutputFormat) {
      items = outputFormatItems;
      count = s3g::kRingOutputFormatCount;
      double format = 0.0;
      uiParameterValue(*plugin, kOutputFormatParamId, format);
      selected = static_cast<int>(std::clamp(std::lround(format), 0l, 2l));
    } else if (_openMenu == kMenuMotionShape) {
      items = shapeItems;
      count = s3g::kMatrixFlowShapeCount;
      selected = static_cast<int>(visible.params.motionShape);
    } else if (_openMenu == kMenuBehavior) {
      items = behaviorItems;
      count = s3g::kNoInputMovementBehaviorCount;
      selected = static_cast<int>(visible.behavior.behavior);
    } else if (_openMenu == kMenuMixerInsert) {
      items = typeItems;
      count = s3g::kNoInputDistortionTypeCount;
      selected = static_cast<int>(
          visible.params.lanes[_effectMenuLane].inserts[_effectMenuSlot].type);
    } else if (_openMenu == kMenuMixerAux) {
      items = typeItems;
      count = s3g::kNoInputDistortionTypeCount;
      selected =
          static_cast<int>(visible.params.aux[_effectMenuBus].effect.type);
    } else if (_openMenu == kMenuReactMode) {
      items = reactItemsFull;
      count = static_cast<uint32_t>(s3g::NoInputReactMode::Count);
      selected = static_cast<int>(visible.params.reactMode);
    } else if (_openMenu == kMenuFieldDivision ||
               _openMenu == kMenuEventDivision) {
      items = divisionItems;
      count = s3g::kNoInputClockDivisionCount;
      selected = static_cast<int>(_openMenu == kMenuFieldDivision
                                      ? visible.params.fieldDivision
                                      : visible.params.eventDivision);
    } else if (_openMenu == kMenuAuxTapA || _openMenu == kMenuAuxTapB) {
      items = tapItems;
      count = static_cast<uint32_t>(s3g::NoInputAuxTap::Count);
      selected =
          static_cast<int>(visible.params.lanes[_effectMenuLane]
                               .auxTap[_openMenu == kMenuAuxTapA ? 0u : 1u]);
    } else {
      items = typeItems;
      count = s3g::kNoInputDistortionTypeCount;
      const uint32_t lane =
          plugin->selectedLane.load(std::memory_order_relaxed);
      const uint32_t slot = static_cast<uint32_t>(_openMenu - kMenuSlot0);
      selected =
          static_cast<int>(visible.params.lanes[lane].inserts[slot].type);
    }
    drawing::drawDropdownMenu(menuDropdownRect(_openMenu), 18.0, items, count,
                              selected, _hoverMenuItem, attrs, style);
  }

  void drawPerformanceMixer_surface(Plugin *plugin, drawing::Rect surface) {
    const auto visible = uiSnapshot(*plugin);
    const drawing::Point offset = mixerSurfaceOffset(surface);
    VSTGUI::CDrawContext::Transform transform(
        *context, VSTGUI::CGraphicsTransform().translate(offset.x, offset.y));
    drawing::Style style;
    drawing::Attrs title = drawing::softTitleAttrs();
    drawing::Attrs label = drawing::softLabelAttrs();
    drawing::Attrs value = drawing::softValueAttrs();

    const uint32_t selected =
        plugin->selectedLane.load(std::memory_order_relaxed);
    const auto drawHorizontal = [&](drawing::String name, drawing::Rect track,
                                    double norm, int rgb) {
      drawing::drawAtPoint_withAttributes(
          name, drawing::makePoint(track.origin.x, track.origin.y - 17.0),
          value);
      drawing::setFill(mixerColor(0x080808));
      drawing::rectFill(track);
      drawing::setStroke(mixerColor(0x454545));
      drawing::frameRect(track);
      drawing::Rect fill = drawing::inset(track, 1.0, 1.0);
      fill.size.width *= std::clamp<double>(norm, 0.0, 1.0);
      drawing::setFill(mixerColor(rgb, 0.88));
      drawing::rectFill(fill);
    };
    const auto drawVertical = [&](drawing::Rect track, double norm, int rgb) {
      drawing::setFill(mixerColor(0x080808));
      drawing::rectFill(track);
      drawing::setStroke(mixerColor(0x454545));
      drawing::frameRect(track);
      const double y = drawing::maxY(track) -
                       std::clamp<double>(norm, 0.0, 1.0) * track.size.height;
      drawing::setFill(mixerColor(rgb));
      drawing::rectFill(drawing::makeRect(track.origin.x - 4.0, y - 2.5,
                                          track.size.width + 8.0, 5.0));
    };

    for (uint32_t lane = 0u; lane < kChannelCount; ++lane) {
      const drawing::Rect strip = popupStripRect(lane);
      drawing::setFill(mixerColor(lane == selected ? 0x222222 : 0x151515));
      drawing::rectFill(strip);
      drawing::setStroke(mixerColor(lane == selected ? 0xb8b8b8 : 0x494949));
      drawing::frameRect(strip);
      drawing::drawAtPoint_withAttributes(
          drawing::stringFormat("LANE %u", lane + 1u),
          drawing::makePoint(strip.origin.x + 10.0, strip.origin.y + 10.0),
          label);
      const auto &laneParams = visible.params.lanes[lane];
      const float loop = visible.params.matrix[lane * 8u + lane];
      drawHorizontal("BODY", popupBodyRect(strip), laneParams.body, 0x929292);
      drawHorizontal("LOSS", popupLossRect(strip), laneParams.loss, 0x747474);
      drawing::drawAtPoint_withAttributes(
          "LOOP",
          drawing::makePoint(strip.origin.x + 10.0, strip.origin.y + 112.0),
          label);
      const drawing::Rect loopTrack = popupLoopRect(strip);
      drawing::setFill(mixerColor(0x080808));
      drawing::rectFill(loopTrack);
      drawing::setStroke(mixerColor(0x454545));
      drawing::frameRect(loopTrack);
      drawing::Rect loopFill = drawing::inset(loopTrack, 2.0, 2.0);
      loopFill.origin.y += loopFill.size.height * (1.0 - std::abs(loop));
      loopFill.size.height *= std::abs(loop);
      drawing::setFill(mixerColor(loop >= 0.0f ? 0xc95e3b : 0x57bfc4, 0.88));
      drawing::rectFill(loopFill);
      drawing::drawAtPoint_withAttributes(
          drawing::stringFormat("%+.2f", loop),
          drawing::makePoint(strip.origin.x + 10.0, strip.origin.y + 248.0),
          value);
      drawing::drawAtPoint_withAttributes(
          "EQ   L   M   H",
          drawing::makePoint(strip.origin.x + 9.0, strip.origin.y + 268.0),
          label);
      const float eq[3] = {
          laneParams.lowDb,
          laneParams.midGainDb,
          laneParams.highDb,
      };
      for (uint32_t band = 0u; band < 3u; ++band) {
        drawVertical(popupEqRect(strip, band), (eq[band] + 18.0f) / 36.0f,
                     0xb8b8b8);
      }
      drawHorizontal("AUX A", popupSendRect(strip, 0u), laneParams.auxSend[0],
                     0xc95e3b);
      drawHorizontal("AUX B", popupSendRect(strip, 1u), laneParams.auxSend[1],
                     0x57bfc4);
      for (uint32_t slot = 0u; slot < s3g::kNoInputMixerInsertSlots; ++slot) {
        const auto &insert = laneParams.inserts[slot];
        drawing::String name =
            drawing::stringFromUtf8(s3g::noInputDistortionName(insert.type));
        if (name.length() > 5u)
          name = name.substr(0, 5u);
        drawFlatButton(
            popupInsertMenuRect(strip, slot),
            drawing::stringFormat("%u %@", slot + 1u, name),
            plugin->selectedLane.load(std::memory_order_relaxed) == lane &&
                plugin->selectedSlot.load(std::memory_order_relaxed) == slot,
            value);
        drawFlatButton(popupInsertEditRect(strip, slot), "EDIT", false, value);
      }
      drawing::drawAtPoint_withAttributes(
          "FADER",
          drawing::makePoint(strip.origin.x + 9.0, strip.origin.y + 564.0),
          label);
      drawVertical(popupFaderRect(strip), (laneParams.levelDb + 60.0f) / 72.0f,
                   0xb8b8b8);
      drawing::drawAtPoint_withAttributes(
          drawing::stringFormat("%+.1f dB", laneParams.levelDb),
          drawing::makePoint(strip.origin.x + 9.0, strip.origin.y + 658.0),
          value);
      drawFlatButton(popupMuteRect(strip), "MUTE", laneParams.mute != 0u,
                     value);
    }

    const drawing::Rect auxPanel = popupAuxPanelRect();
    drawing::setFill(mixerColor(0x151515));
    drawing::rectFill(auxPanel);
    drawing::setStroke(mixerColor(0x595959));
    drawing::frameRect(auxPanel);
    drawing::drawAtPoint_withAttributes(
        "AUX RETURNS / MASTER TONE",
        drawing::makePoint(auxPanel.origin.x + 14.0, auxPanel.origin.y + 14.0),
        label);
    const drawing::String auxLabels[4] = {"GAIN", "TONE", "RETURN", "LOOP"};
    for (uint32_t bus = 0u; bus < 2u; ++bus) {
      const double baseY = auxPanel.origin.y + 48.0 + bus * 248.0;
      drawing::drawAtPoint_withAttributes(
          drawing::stringFormat("AUX %c", 'A' + bus),
          drawing::makePoint(auxPanel.origin.x + 14.0, baseY - 8.0), title);
      const auto &aux = visible.params.aux[bus];
      drawFlatButton(popupAuxMuteRect(bus), "MUTE ALL",
                     visible.auxMute[bus] != 0u, value);
      drawFlatButton(
          popupAuxTypeMenuRect(bus),
          drawing::stringFromUtf8(s3g::noInputDistortionName(aux.effect.type)),
          true, value);
      drawFlatButton(popupAuxTypeEditRect(bus), "EDIT", false, value);
      const double norms[4] = {
          aux.effect.gain,
          aux.effect.tone,
          aux.returnGain,
          aux.feedback / 0.96f,
      };
      for (uint32_t local = 0u; local < 4u; ++local) {
        const drawing::Rect track = popupAuxSliderRect(bus, local);
        drawing::drawAtPoint_withAttributes(
            auxLabels[local],
            drawing::makePoint(auxPanel.origin.x + 14.0, track.origin.y - 4.0),
            value);
        drawHorizontal("", track, norms[local],
                       bus == 0u ? 0xc95e3b : 0x57bfc4);
      }
      const drawing::Rect activity =
          drawing::makeRect(auxPanel.origin.x + 14.0, baseY + 206.0,
                            auxPanel.size.width - 28.0, 8.0);
      drawing::setFill(mixerColor(0x080808));
      drawing::rectFill(activity);
      drawing::Rect active = drawing::inset(activity, 1.0, 1.0);
      active.size.width *= std::clamp<double>(
          plugin->auxActivity[bus].load(std::memory_order_relaxed), 0.0, 1.0);
      drawing::setFill(mixerColor(bus == 0u ? 0xc95e3b : 0x57bfc4));
      drawing::rectFill(active);
    }
    drawing::drawAtPoint_withAttributes(
        "MASTER TONE",
        drawing::makePoint(auxPanel.origin.x + 14.0, auxPanel.origin.y + 554.0),
        title);
    const drawing::String toneLabels[2] = {"INTERNAL", "HOUSE"};
    const double toneNorms[2] = {
        (visible.params.internalTone + 1.0f) * 0.5f,
        (visible.params.houseTone + 1.0f) * 0.5f,
    };
    for (uint32_t row = 0u; row < 2u; ++row) {
      const drawing::Rect track = popupToneRect(row);
      drawing::drawAtPoint_withAttributes(
          toneLabels[row],
          drawing::makePoint(auxPanel.origin.x + 14.0, track.origin.y - 4.0),
          value);
      drawHorizontal("", track, toneNorms[row], 0x929292);
    }
    drawing::drawAtPoint_withAttributes(
        "EFFECT MENU + EDIT · DRAG TRACKS · MUTE ALL: BUS",
        drawing::makePoint(auxPanel.origin.x + 14.0,
                           drawing::maxY(auxPanel) - 24.0),
        value);
  }

  void drawAuxTopology_label_value(Plugin *plugin, drawing::Attrs label,
                                   drawing::Attrs value) {
    const auto visible = uiSnapshot(*plugin);
    const drawing::Rect page = widePageRect();
    drawing::setFill(mixerColor(0x101010));
    drawing::rectFill(page);
    drawing::setStroke(mixerColor(0x4b4b4b));
    drawing::frameRect(page);
    drawing::drawAtPoint_withAttributes(
        "PER-LANE AUX TOPOLOGY · SOURCE TAP → SEND → AUX PROCESSOR → SIGNED "
        "LANE RETURN",
        drawing::makePoint(page.origin.x + 14.0, page.origin.y + 14.0), label);
    const auto drawTrack = [&](drawing::Rect track, double norm, int color) {
      drawing::setFill(mixerColor(0x070707));
      drawing::rectFill(track);
      drawing::setStroke(mixerColor(0x424242));
      drawing::frameRect(track);
      drawing::Rect fill = drawing::inset(track, 1.0, 1.0);
      fill.size.width *= std::clamp<double>(norm, 0.0, 1.0);
      drawing::setFill(mixerColor(color, 0.86));
      drawing::rectFill(fill);
    };
    for (uint32_t lane = 0u; lane < kChannelCount; ++lane) {
      const drawing::Rect column = auxPageColumnRect(lane);
      drawing::setFill(mixerColor(
          plugin->selectedLane.load(std::memory_order_relaxed) == lane
              ? 0x222222
              : 0x151515));
      drawing::rectFill(column);
      drawing::setStroke(mixerColor(0x4b4b4b));
      drawing::frameRect(column);
      drawing::drawAtPoint_withAttributes(
          drawing::stringFormat("LANE %u", lane + 1u),
          drawing::makePoint(column.origin.x + 12.0, column.origin.y + 12.0),
          label);
      const auto &laneParams = visible.params.lanes[lane];
      const drawing::String labels[6] = {
          "SEND A", "TAP A", "RETURN A", "SEND B", "TAP B", "RETURN B",
      };
      for (uint32_t row = 0u; row < 6u; ++row) {
        const drawing::Rect track = auxPageTrackRect(lane, row);
        drawing::drawAtPoint_withAttributes(
            labels[row],
            drawing::makePoint(track.origin.x, track.origin.y - 19.0), value);
        if (row == 1u || row == 4u) {
          const uint32_t bus = row == 1u ? 0u : 1u;
          drawFlatButton(auxPageTapRect(lane, bus),
                         drawing::stringFromUtf8(
                             s3g::noInputAuxTapName(laneParams.auxTap[bus])),
                         true, value);
          continue;
        }
        const uint32_t bus = row >= 3u ? 1u : 0u;
        const bool isReturn = row == 2u || row == 5u;
        const float raw =
            isReturn ? laneParams.auxReturn[bus] : laneParams.auxSend[bus];
        drawTrack(track, isReturn ? (raw + 1.0f) * 0.5f : raw,
                  bus == 0u ? 0xc95e3b : 0x57bfc4);
        drawing::drawAtPoint_withAttributes(
            drawing::stringFormat(isReturn ? "%+.2f" : "%.0f%%",
                                  isReturn ? raw : raw * 100.0f),
            drawing::makePoint(track.origin.x, drawing::maxY(track) + 5.0),
            value);
      }
    }
    drawing::drawAtPoint_withAttributes(
        "A RETURN IS A TRUE DESTINATION VECTOR: NEGATIVE VALUES INVERT PHASE. "
        "RETURN TAP PRESERVES THE ORIGINAL FEEDBACK-MIXER PATH.",
        drawing::makePoint(page.origin.x + 14.0, drawing::maxY(page) - 22.0),
        value);
  }

  void drawOriginal(drawing::Rect dirty) {
    (void)dirty;
    auto *plugin = static_cast<Plugin *>(_plugin);
    if (!plugin)
      return;
    const auto visible = uiSnapshot(*plugin);
    double outputFormatValue = 0.0;
    double outputRotationValue = 0.0;
    uiParameterValue(*plugin, kOutputFormatParamId, outputFormatValue);
    uiParameterValue(*plugin, kOutputRotationParamId, outputRotationValue);
    const auto visibleOutputFormat =
        s3g::sanitizeRingOutputFormat(static_cast<uint32_t>(
            std::clamp(std::lround(outputFormatValue), 0l, 2l)));
    const auto &family = s3g::gui_layout::kNoInputMixerFamilyLayout;
    drawing::Style style;
    drawing::setFill(style.bg);
    drawing::rectFill(bounds());
    drawing::Attrs title = drawing::softTitleAttrs();
    drawing::Attrs label = drawing::softLabelAttrs();
    drawing::Attrs value = drawing::softValueAttrs();
    const auto titleBand = s3g::gui_layout::matrixTitleBand(family.canvas);
    drawing::drawProcessorTitleBand(
        "s3g PROCESSOR NO INPUT MIXER 8CH",
        drawing::stringFromUtf8((_mixerPopupChild && _mixerPopupOwner
                                     ? _mixerPopupOwner->_titlePresetName
                                     : _titlePresetName)),
        drawing::peakDbText(plugin->outputPeak.load(std::memory_order_relaxed)),
        titleBand, title, label, value, style);

    drawing::drawPanelFrame(family.fieldPanel.x, family.fieldPanel.y,
                            family.fieldPanel.width, family.fieldPanel.height,
                            style);
    const uint32_t page = _lockedPage < kPageCount
                              ? _lockedPage
                              : plugin->guiPage.load(std::memory_order_relaxed);
    static drawing::String pageNames[kPageCount] = {
        "PATCH", "MIXER", "CHANNEL", "SAFETY", "AUX",
    };
    drawing::drawPanelHeader(pageNames[page], true, family.fieldPanel.x,
                             family.fieldPanel.y, family.fieldPanel.width,
                             s3g::gui_layout::kStandardMetrics.headerHeight,
                             label, style);
    for (uint32_t index = 0u; index < kPageCount; ++index) {
      drawing::drawToolboxHeaderButton(
          fieldTabRect(index), drawing::cocoaRect(family.fieldPanel),
          pageNames[index], page == index, value, style);
    }
    drawing::drawToolboxHeaderButton(
        mixerPopButtonRect(), drawing::cocoaRect(family.fieldPanel),
        _mixerPopupChild ? "DOCK" : "POP",
        _mixerPopupChild || (_pagePanels[page] && _pagePanels[page]->visible()),
        value, style);
    const drawing::Rect fieldPlot = drawing::cocoaRect(family.fieldPlot);
    if (page == 0u)
      drawPrimaryWiring_rect_label_valueAttrs(plugin, patchVisualRect(), label,
                                              value);
    else if (page == 1u)
      drawPerformanceMixer_surface(plugin, fieldPlot);
    else if (page == 2u)
      drawPrimaryLanes_rect_label_valueAttrs(plugin, channelOverviewRect(),
                                             label, value);
    else if (page == 4u)
      drawAuxTopology_label_value(plugin, label, value);
    else {
      drawing::setFill(mixerColor(0x101010));
      drawing::rectFill(fieldPlot);
      drawing::setStroke(mixerColor(0x454545));
      drawing::frameRect(fieldPlot);
    }

    const auto drawPanel = [&](drawing::String name,
                               const s3g::gui_layout::Panel &panel) {
      drawing::drawPanelFrame(panel, style);
      drawing::drawPanelHeader(name, true, panel, label, style);
    };
    const uint32_t lane = plugin->selectedLane.load(std::memory_order_relaxed);
    const uint32_t slot = plugin->selectedSlot.load(std::memory_order_relaxed);
    const auto &laneParams = visible.params.lanes[lane];
    if (page == 0u) {
      drawPanel("NETWORK", family.network);
      drawPanel("MOVEMENT", family.movement);
      drawFlatButton(movementSectionRandomRect(), "RND", false, value);
      drawFlatButton(movementBankButtonRect(0u), "FIELD", _movementBank == 0u,
                     value);
      drawFlatButton(movementBankButtonRect(1u), "BEHAV", _movementBank == 1u,
                     value);
      drawFlatButton(movementBankButtonRect(2u), "RESP", _movementBank == 2u,
                     value);
      drawPanel("CROSSPOINT", family.crosspoint);
    } else if (page == 2u) {
      drawPanel("SELECTED LANE", family.selectedLane);
      drawPanel(drawing::stringFormat("EQ — L%u", lane + 1u), family.eq);
      drawPanel(
          drawing::stringFormat("INSERTS — L%u / S%u", lane + 1u, slot + 1u),
          family.inserts);
    } else if (page == 3u) {
      drawPanel("OUTPUT", family.output);
      drawPanel("CONTAINMENT", family.containment);
    }

    if (page == 3u) {
      drawSlider_value_norm_row_panel_label_valueAttrs_style(
          "OUT", drawing::stringFormat("%+.1f dB", visible.params.outputGainDb),
          (visible.params.outputGainDb + 60.0f) / 66.0f, 0u, family.output,
          label, value, style);
      drawSlider_value_norm_row_panel_label_valueAttrs_style(
          "CEIL", drawing::stringFormat("%+.1f dB", visible.params.ceilingDb),
          (visible.params.ceilingDb + 18.0f) / 18.0f, 1u, family.output, label,
          value, style);
      drawing::drawProcessorMenu(
          "FORMAT",
          drawing::stringFromUtf8(
              s3g::ringOutputFormatName(visibleOutputFormat)),
          s3g::gui_layout::rowY(family.output, 2u), family.output.frame.x,
          family.output.frame.width, label, value, style);
      drawSlider_value_norm_row_panel_label_valueAttrs_style(
          "ROTATE", drawing::stringFormat("%+.1f°", outputRotationValue),
          (outputRotationValue + 180.0) / 360.0, 3u, family.output, label,
          value, style);
      drawing::drawToggle(
          "LIMIT", visible.params.limiterEnabled != 0u,
          s3g::gui_layout::rowY(family.output, 4u), label, value, style,
          s3g::gui_layout::processorLabelX(family.output.frame.x),
          s3g::gui_layout::processorControlX(family.output.frame.x), 82.0);
      drawing::drawToggle(
          "DC BLOCK", visible.params.dcBlockEnabled != 0u,
          s3g::gui_layout::rowY(family.output, 5u), label, value, style,
          s3g::gui_layout::processorLabelX(family.output.frame.x),
          s3g::gui_layout::processorControlX(family.output.frame.x), 82.0);
    }

    if (page == 0u) {
      drawing::drawAtPoint_withAttributes(
          "SEED",
          drawing::makePoint(
              s3g::gui_layout::processorLabelX(family.network.frame.x),
              s3g::gui_layout::rowY(family.network, 0u) - 2.0),
          label);
      drawFlatButton(seedNewButtonRect(), "NEW", false, value);
      drawFlatButton(randomButtonRect(), "RANDOM V", false, value);
      drawFlatButton(forgetButtonRect(), "FORGET", false, value);
      drawSlider_value_norm_row_panel_label_valueAttrs_style(
          "FDBK",
          drawing::stringFormat("%.0f%%", visible.params.feedback * 100.0f),
          visible.params.feedback / 1.25f, 1u, family.network, label, value,
          style);
      drawSlider_value_norm_row_panel_label_valueAttrs_style(
          "COUPL",
          drawing::stringFormat("%.0f%%", visible.params.coupling * 100.0f),
          visible.params.coupling / 1.25f, 2u, family.network, label, value,
          style);
      drawSlider_value_norm_row_panel_label_valueAttrs_style(
          "PHASE",
          drawing::stringFormat("%.0f%%", visible.params.phase * 100.0f),
          visible.params.phase, 3u, family.network, label, value, style);
      drawSlider_value_norm_row_panel_label_valueAttrs_style(
          "DRIFT",
          drawing::stringFormat("%.0f%%", visible.params.drift * 100.0f),
          visible.params.drift, 4u, family.network, label, value, style);
      drawSlider_value_norm_row_panel_label_valueAttrs_style(
          "FORMANT",
          drawing::stringFormat("%.0f%%", visible.params.formant * 100.0f),
          visible.params.formant, 5u, family.network, label, value, style);
      drawSlider_value_norm_row_panel_label_valueAttrs_style(
          "AGENCY",
          drawing::stringFormat("%.0f%%", visible.params.agency * 100.0f),
          visible.params.agency, 6u, family.network, label, value, style);
      drawSlider_value_norm_row_panel_label_valueAttrs_style(
          "SPACE",
          drawing::stringFormat("%.0f%%", visible.params.space * 100.0f),
          visible.params.space, 7u, family.network, label, value, style);
      drawSlider_value_norm_row_panel_label_valueAttrs_style(
          "VARIANCE",
          drawing::stringFormat("%.0f%%", visible.params.variance * 100.0f),
          visible.params.variance, 8u, family.network, label, value, style);
    }

    if (page == 2u) {
      drawing::drawProcessorMenu(
          "LANE", drawing::stringFormat("L%u", lane + 1u),
          s3g::gui_layout::rowY(family.selectedLane, 0u),
          family.selectedLane.frame.x, family.selectedLane.frame.width, label,
          value, style);
      drawSlider_value_norm_row_panel_label_valueAttrs_style(
          "BODY", drawing::stringFormat("%.0f%%", laneParams.body * 100.0f),
          laneParams.body, 1u, family.selectedLane, label, value, style);
      drawSlider_value_norm_row_panel_label_valueAttrs_style(
          "LOSS", drawing::stringFormat("%.0f%%", laneParams.loss * 100.0f),
          laneParams.loss, 2u, family.selectedLane, label, value, style);
      drawSlider_value_norm_row_panel_label_valueAttrs_style(
          "TUNE", drawing::stringFormat("%.2f MIDI", laneParams.tuneNote),
          (laneParams.tuneNote - 24.0f) / 84.0f, 3u, family.selectedLane, label,
          value, style);
      drawSlider_value_norm_row_panel_label_valueAttrs_style(
          "FINE", drawing::stringFormat("%+.1f ct", laneParams.tuneCents),
          (laneParams.tuneCents + 100.0f) / 200.0f, 4u, family.selectedLane,
          label, value, style);
      drawSlider_value_norm_row_panel_label_valueAttrs_style(
          "LEVEL", drawing::stringFormat("%+.1f dB", laneParams.levelDb),
          (laneParams.levelDb + 60.0f) / 72.0f, 5u, family.selectedLane, label,
          value, style);
      const double selectedButtonY =
          s3g::gui_layout::rowY(family.selectedLane, 6u) - 1.0;
      drawFlatButton(drawing::makeRect(s3g::gui_layout::processorControlX(
                                           family.selectedLane.frame.x),
                                       selectedButtonY, 50.0, 17.0),
                     "LOCK", laneParams.pitchLock != 0u, value);
      drawFlatButton(drawing::makeRect(s3g::gui_layout::processorControlX(
                                           family.selectedLane.frame.x) +
                                           58.0,
                                       selectedButtonY, 50.0, 17.0),
                     "MUTE", laneParams.mute != 0u, value);
      drawFlatButton(drawing::makeRect(s3g::gui_layout::processorControlX(
                                           family.selectedLane.frame.x) +
                                           116.0,
                                       selectedButtonY, 50.0, 17.0),
                     "KILL", false, value);
    }

    if (page == 0u) {
      const uint32_t source =
          plugin->selectedSource.load(std::memory_order_relaxed);
      const uint32_t destination =
          plugin->selectedDestination.load(std::memory_order_relaxed);
      const uint32_t routeIndex = destination * kChannelCount + source;
      const float storedRoute = visible.params.matrix[routeIndex];
      const float displayedRoute = displayedMatrixGain(*plugin, routeIndex);
      drawing::drawProcessorMenu(
          "SRC", drawing::stringFormat("L%u", source + 1u),
          s3g::gui_layout::rowY(family.crosspoint, 0u),
          family.crosspoint.frame.x, family.crosspoint.frame.width, label,
          value, style);
      drawing::drawProcessorMenu(
          "DEST", drawing::stringFormat("L%u", destination + 1u),
          s3g::gui_layout::rowY(family.crosspoint, 1u),
          family.crosspoint.frame.x, family.crosspoint.frame.width, label,
          value, style);
      drawSlider_value_norm_row_panel_label_valueAttrs_style(
          "GAIN", drawing::stringFormat("%+.2f", displayedRoute),
          (displayedRoute + 1.0f) * 0.5f, 2u, family.crosspoint, label, value,
          style);
      const double routeButtonX =
          s3g::gui_layout::processorControlX(family.crosspoint.frame.x);
      const double routeButtonY =
          s3g::gui_layout::rowY(family.crosspoint, 3u) - 1.0;
      drawFlatButton(drawing::makeRect(routeButtonX, routeButtonY, 78.0, 17.0),
                     storedRoute < 0.0f ? "NEGATIVE" : "POSITIVE",
                     storedRoute != 0.0f, value);
      drawFlatButton(
          drawing::makeRect(routeButtonX + 88.0, routeButtonY, 78.0, 17.0),
          "CLEAR", false, value);
    }

    if (page == 2u) {
      drawSlider_value_norm_row_panel_label_valueAttrs_style(
          "LOW", drawing::stringFormat("%+.1f dB", laneParams.lowDb),
          (laneParams.lowDb + 18.0f) / 36.0f, 0u, family.eq, label, value,
          style);
      const float midNorm = std::log(laneParams.midFrequencyHz / 80.0f) /
                            std::log(8000.0f / 80.0f);
      drawSlider_value_norm_row_panel_label_valueAttrs_style(
          "MID F",
          (laneParams.midFrequencyHz >= 1000.0f
               ? drawing::stringFormat("%.2f k",
                                       laneParams.midFrequencyHz * 0.001f)
               : drawing::stringFormat("%.0f Hz", laneParams.midFrequencyHz)),
          midNorm, 1u, family.eq, label, value, style);
      drawSlider_value_norm_row_panel_label_valueAttrs_style(
          "MID G", drawing::stringFormat("%+.1f dB", laneParams.midGainDb),
          (laneParams.midGainDb + 18.0f) / 36.0f, 2u, family.eq, label, value,
          style);
      drawSlider_value_norm_row_panel_label_valueAttrs_style(
          "HIGH", drawing::stringFormat("%+.1f dB", laneParams.highDb),
          (laneParams.highDb + 18.0f) / 36.0f, 3u, family.eq, label, value,
          style);

      for (uint32_t insertSlot = 0u; insertSlot < s3g::kNoInputMixerInsertSlots;
           ++insertSlot) {
        const auto &insert = laneParams.inserts[insertSlot];
        const drawing::Rect menuRect = channelInsertMenuRect(insertSlot);
        drawing::drawMenu(
            drawing::stringFormat("SLOT %u", insertSlot + 1u),
            drawing::stringFromUtf8(s3g::noInputDistortionName(insert.type)),
            s3g::gui_layout::rowY(family.inserts, insertSlot), label, value,
            style, s3g::gui_layout::processorLabelX(family.inserts.frame.x),
            menuRect.origin.x, menuRect.size.width);
        drawFlatButton(channelInsertEditRect(insertSlot), "EDIT",
                       insertSlot == slot, value);
      }
      const auto &insert = laneParams.inserts[slot];
      drawSlider_value_norm_row_panel_label_valueAttrs_style(
          "GAIN", drawing::stringFormat("%.0f%%", insert.gain * 100.0f),
          insert.gain, 3u, family.inserts, label, value, style);
      drawSlider_value_norm_row_panel_label_valueAttrs_style(
          "TONE", drawing::stringFormat("%.0f%%", insert.tone * 100.0f),
          insert.tone, 4u, family.inserts, label, value, style);
      drawSlider_value_norm_row_panel_label_valueAttrs_style(
          "BIAS", drawing::stringFormat("%+.2f", insert.bias),
          (insert.bias + 1.0f) * 0.5f, 5u, family.inserts, label, value, style);
      drawSlider_value_norm_row_panel_label_valueAttrs_style(
          "LEVEL", drawing::stringFormat("%+.1f dB", insert.levelDb),
          (insert.levelDb + 24.0f) / 36.0f, 6u, family.inserts, label, value,
          style);
      drawing::drawToggle(
          "BYPASS", insert.bypass != 0u,
          s3g::gui_layout::rowY(family.inserts, 7u), label, value, style,
          s3g::gui_layout::processorLabelX(family.inserts.frame.x),
          s3g::gui_layout::processorControlX(family.inserts.frame.x), 82.0);
    }

    if (page == 0u) {
      if (_movementBank == 0u) {
        drawing::drawProcessorMenu(
            "SHAPE",
            drawing::stringFromUtf8(
                s3g::matrixFlowShapeName(visible.params.motionShape)),
            s3g::gui_layout::rowY(family.movement, 0u), family.movement.frame.x,
            family.movement.frame.width, label, value, style);
        drawSlider_value_norm_row_panel_label_valueAttrs_style(
            "FLOW",
            drawing::stringFormat("%.0f%%", visible.params.flow * 100.0f),
            visible.params.flow, 1u, family.movement, label, value, style);
        drawSlider_value_norm_row_panel_label_valueAttrs_style(
            "SPREAD",
            drawing::stringFormat("%.0f%%", visible.params.spread * 100.0f),
            visible.params.spread, 2u, family.movement, label, value, style);
        drawSlider_value_norm_row_panel_label_valueAttrs_style(
            "VORTEX", drawing::stringFormat("%+.2f", visible.params.vortex),
            (visible.params.vortex + 1.0f) * 0.5f, 3u, family.movement, label,
            value, style);
        drawSlider_value_norm_row_panel_label_valueAttrs_style(
            "DEPTH",
            drawing::stringFormat("%.0f%%", visible.params.motion * 100.0f),
            visible.params.motion, 4u, family.movement, label, value, style);
        const float fieldRate =
            visible.params.clockSync
                ? s3g::noInputSyncedRateHz(visible.params.fieldDivision,
                                           plugin->transportTempoBpm)
                : s3g::noInputMixerMotionRateHz(visible.params.motionRate,
                                                visible.params.slowTime != 0u);
        if (visible.params.clockSync) {
          drawing::drawProcessorMenu(
              "RATE",
              drawing::stringFormat(
                  "%@ · %.3f Hz",
                  drawing::stringFromUtf8(s3g::noInputClockDivisionName(
                      visible.params.fieldDivision)),
                  fieldRate),
              s3g::gui_layout::rowY(family.movement, 5u),
              family.movement.frame.x, family.movement.frame.width, label,
              value, style);
        } else {
          drawSlider_value_norm_row_panel_label_valueAttrs_style(
              "RATE",
              drawing::stringFormat((fieldRate < 1.0f ? "%.3f Hz" : "%.1f Hz"),
                                    fieldRate),
              visible.params.motionRate, 5u, family.movement, label, value,
              style);
        }
        drawSlider_value_norm_row_panel_label_valueAttrs_style(
            "PHASE",
            drawing::stringFormat("%.0f%%",
                                  visible.params.motionPhase * 100.0f),
            visible.params.motionPhase, 6u, family.movement, label, value,
            style);
      } else if (_movementBank == 1u) {
        const auto behavior = visible.behavior.behavior;
        const bool behaviorActive =
            behavior != s3g::NoInputMovementBehavior::Glide;
        const bool hasTimedWindow =
            s3g::noInputMovementBehaviorUsesLength(behavior);
        const bool hasDensity =
            s3g::noInputMovementBehaviorUsesDensity(behavior);
        drawing::drawProcessorMenu(
            "BEHAV",
            drawing::stringFromUtf8(s3g::noInputMovementBehaviorName(behavior)),
            s3g::gui_layout::rowY(family.movement, 0u), family.movement.frame.x,
            family.movement.frame.width, label, value, style);
        drawSlider_value_norm_row_panel_label_valueAttrs_style(
            "DEPTH",
            behaviorActive ? drawing::stringFormat(
                                 "%.0f%%", visible.behaviorDepth * 100.0f)
                           : "—",
            (behaviorActive ? visible.behaviorDepth : 0.0f), 1u,
            family.movement, label, value, style);
        const float eventRate =
            visible.params.clockSync
                ? s3g::noInputSyncedRateHz(visible.params.eventDivision,
                                           plugin->transportTempoBpm)
                : s3g::noInputMovementEventRateHz(visible.behavior.eventRate,
                                                  visible.params.slowTime !=
                                                      0u);
        if (behaviorActive && visible.params.clockSync) {
          drawing::drawProcessorMenu(
              "EVENT",
              drawing::stringFormat(
                  "%@ · %.3f Hz",
                  drawing::stringFromUtf8(s3g::noInputClockDivisionName(
                      visible.params.eventDivision)),
                  eventRate),
              s3g::gui_layout::rowY(family.movement, 2u),
              family.movement.frame.x, family.movement.frame.width, label,
              value, style);
        } else {
          drawSlider_value_norm_row_panel_label_valueAttrs_style(
              "EVENT",
              behaviorActive
                  ? drawing::stringFormat(
                        (eventRate < 10.0f ? "%.3f Hz" : "%.1f Hz"), eventRate)
                  : "—",
              (behaviorActive ? visible.behavior.eventRate : 0.0f), 2u,
              family.movement, label, value, style);
        }
        drawSlider_value_norm_row_panel_label_valueAttrs_style(
            "LENGTH",
            hasTimedWindow
                ? drawing::stringFormat("%.1f ms", s3g::noInputMovementLengthMs(
                                                       visible.behavior.length))
                : "—",
            (hasTimedWindow ? visible.behavior.length : 0.0f), 3u,
            family.movement, label, value, style);
        drawSlider_value_norm_row_panel_label_valueAttrs_style(
            "DENSITY",
            hasDensity ? drawing::stringFormat(
                             "%.0f%%", visible.behavior.density * 100.0f)
                       : "—",
            (hasDensity ? visible.behavior.density : 0.0f), 4u, family.movement,
            label, value, style);
        drawSlider_value_norm_row_panel_label_valueAttrs_style(
            "CHAOS",
            behaviorActive ? drawing::stringFormat(
                                 "%.0f%%", visible.behavior.chaos * 100.0f)
                           : "—",
            (behaviorActive ? visible.behavior.chaos : 0.0f), 5u,
            family.movement, label, value, style);
        drawing::String transitionLabel =
            behavior == s3g::NoInputMovementBehavior::Step
                ? "TRANSITION"
                : (behavior == s3g::NoInputMovementBehavior::Scramble
                       ? "XFADE"
                       : (behavior == s3g::NoInputMovementBehavior::Cascade
                              ? "TRAIL"
                              : (behavior == s3g::NoInputMovementBehavior::Erode
                                     ? "SOFTEN"
                                     : "EDGE")));
        drawSlider_value_norm_row_panel_label_valueAttrs_style(
            transitionLabel,
            behaviorActive
                ? drawing::stringFormat("%.2f ms", s3g::noInputMovementSlewMs(
                                                       visible.behavior.slew))
                : "—",
            (behaviorActive ? visible.behavior.slew : 0.0f), 6u,
            family.movement, label, value, style);
        drawSlider_value_norm_row_panel_label_valueAttrs_style(
            "CHOKE",
            behaviorActive ? drawing::stringFormat(
                                 "%.0f%%", visible.behavior.choke * 100.0f)
                           : "—",
            (behaviorActive ? visible.behavior.choke : 0.0f), 7u,
            family.movement, label, value, style);
      } else {
        drawing::drawProcessorMenu(
            "MODE",
            drawing::stringFromUtf8(
                s3g::noInputReactModeName(visible.params.reactMode)),
            s3g::gui_layout::rowY(family.movement, 0u), family.movement.frame.x,
            family.movement.frame.width, label, value, style);
        drawSlider_value_norm_row_panel_label_valueAttrs_style(
            "DEPTH",
            drawing::stringFormat("%.0f%%", visible.params.reactDepth * 100.0f),
            visible.params.reactDepth, 1u, family.movement, label, value,
            style);
        const float thresholdDb =
            20.0f *
            std::log10(std::max(1.0e-6f, s3g::noInputReactThreshold(
                                             visible.params.reactThreshold)));
        drawSlider_value_norm_row_panel_label_valueAttrs_style(
            "THRESH", drawing::stringFormat("%+.1f dB", thresholdDb),
            visible.params.reactThreshold, 2u, family.movement, label, value,
            style);
        drawSlider_value_norm_row_panel_label_valueAttrs_style(
            "ATTACK",
            drawing::stringFormat("%.1f ms", s3g::noInputReactAttackMs(
                                                 visible.params.reactAttack)),
            visible.params.reactAttack, 3u, family.movement, label, value,
            style);
        drawSlider_value_norm_row_panel_label_valueAttrs_style(
            "RELEASE",
            drawing::stringFormat("%.0f ms", s3g::noInputReactReleaseMs(
                                                 visible.params.reactRelease)),
            visible.params.reactRelease, 4u, family.movement, label, value,
            style);
        drawing::drawAtPoint_withAttributes(
            "DIRECTION",
            drawing::makePoint(
                s3g::gui_layout::processorLabelX(family.movement.frame.x),
                s3g::gui_layout::rowY(family.movement, 5u) - 2.0),
            label);
        drawFlatButton(reactDirectionButtonRect(),
                       visible.params.reactPolarity < 0.0f ? "INVERT"
                                                           : "NORMAL",
                       visible.params.reactPolarity < 0.0f, value);
        const float listenLevel = std::max(
            1.0e-6f,
            plugin->laneActivity[lane].load(std::memory_order_relaxed));
        const float listenDb = 20.0f * std::log10(listenLevel);
        drawSlider_value_norm_row_panel_label_valueAttrs_style(
            drawing::stringFormat("LISTEN L%u", lane + 1u),
            listenDb <= -59.9f ? "−∞"
                               : drawing::stringFormat("%+.1f dB", listenDb),
            std::clamp((listenDb + 60.0f) / 60.0f, 0.0f, 1.0f), 6u,
            family.movement, label, value, style);
      }
      drawFlatButton(movementGlobalToggleRect(0u), "HOLD",
                     visible.params.controllerHold != 0u, value);
      drawFlatButton(movementGlobalToggleRect(1u), "SLOW",
                     visible.params.slowTime != 0u, value);
      drawFlatButton(movementGlobalToggleRect(2u), "SYNC",
                     visible.params.clockSync != 0u, value);
    }

    if (page == 3u) {
      drawing::drawAtPoint_withAttributes(
          "QUALITY",
          drawing::makePoint(family.containment.frame.x + 16.0,
                             family.containment.frame.y + 34.0),
          label);
      drawing::drawMenu(
          "", drawing::stringFormat("%uX", 1u << visible.params.quality),
          family.containment.frame.y + 36.0, label, value, style,
          family.containment.frame.x + 16.0, family.containment.frame.x + 108.0,
          110.0);
      drawing::drawAtPoint_withAttributes(
          "ENERGY",
          drawing::makePoint(family.containment.frame.x + 16.0,
                             family.containmentMeter.y - 2.0),
          label);
      drawing::Rect energyTrack = drawing::cocoaRect(family.containmentMeter);
      drawing::setFill(mixerColor(0x101010));
      drawing::rectFill(energyTrack);
      drawing::setStroke(mixerColor(0x454545));
      drawing::frameRect(energyTrack);
      drawing::Rect energyFill = drawing::inset(energyTrack, 1.0, 1.0);
      energyFill.size.width *= std::clamp<double>(
          plugin->networkActivity.load(std::memory_order_relaxed), 0.0, 1.0);
      const float governor =
          plugin->minimumGovernor.load(std::memory_order_relaxed);
      drawing::setFill(mixerColor(
          governor > 0.72f ? 0x57bfc4
                           : (governor > 0.28f ? 0xc95e3b : 0xb83b32),
          0.85));
      drawing::rectFill(energyFill);

      drawing::Rect containmentField =
          drawing::cocoaRect(family.containmentField);
      drawing::setFill(mixerColor(0x101010));
      drawing::rectFill(containmentField);
      drawing::setStroke(mixerColor(0x454545));
      drawing::frameRect(containmentField);
      for (uint32_t ring = 0u; ring < 4u; ++ring) {
        const double inset = 12.0 + ring * 10.0;
        drawing::setStroke(mixerColor(0x4a4a4a + ring * 0x080808,
                                      0.35 + (1.0f - governor) * 0.45));
        drawing::frameRect(
            drawing::inset(containmentField, inset, inset * 0.55));
      }
      for (uint32_t node = 0u; node < kChannelCount; ++node) {
        const double angle = static_cast<double>(node) * 2.0 * M_PI / 8.0;
        const double x = drawing::midX(containmentField) +
                         std::cos(angle) * containmentField.size.width * 0.36;
        const double y = drawing::midY(containmentField) +
                         std::sin(angle) * containmentField.size.height * 0.32;
        const double activity = std::clamp<double>(
            plugin->laneActivity[node].load(std::memory_order_relaxed), 0.0,
            1.0);
        drawing::setFill(mixerColor(activity > 0.72 ? 0xc95e3b : 0x777777,
                                    0.45 + activity * 0.5));
        drawing::rectFill(drawing::makeRect(x - 3.0, y - 3.0, 6.0, 6.0));
      }
      const auto containment = static_cast<s3g::NoInputContainmentState>(
          plugin->containmentState.load(std::memory_order_relaxed));
      drawing::drawAtPoint_withAttributes(
          drawing::stringFromUtf8(s3g::noInputContainmentName(containment)),
          drawing::makePoint(
              family.containment.frame.x + 16.0,
              drawing::maxY(drawing::cocoaRect(family.containmentField)) +
                  22.0),
          value);
    }
    drawing::Rect panicRect = drawing::cocoaRect(family.panicButton);
    drawing::setFill(mixerColor(0x7e2924));
    drawing::rectFill(panicRect);
    drawing::setStroke(mixerColor(0xc95e3b));
    drawing::frameRect(panicRect);
    const drawing::Size panicSize = drawing::sizeWithAttributes("PANIC", label);
    drawing::drawAtPoint_withAttributes(
        "PANIC",
        drawing::makePoint(
            panicRect.origin.x + (panicRect.size.width - panicSize.width) * 0.5,
            panicRect.origin.y +
                (panicRect.size.height - panicSize.height) * 0.5),
        label);

    drawOpenMenu_attrs_style(plugin, value, style);
  }

  const s3g::gui_layout::Panel *panelForParam(clap_id param) {
    const auto &family = s3g::gui_layout::kNoInputMixerFamilyLayout;
    if (param == kOutputParamId || param == kCeilingParamId ||
        param == kOutputRotationParamId)
      return &family.output;
    if ((param >= kFeedbackParamId && param <= kFormantParamId) ||
        (param >= kAgencyParamId && param <= kVarianceParamId))
      return &family.network;
    if (param >= kFlowParamId && param <= kMotionPhaseParamId)
      return &family.movement;
    if (param >= kEventRateParamId && param <= kEventChokeParamId)
      return &family.movement;
    if (param == kBehaviorDepthParamId)
      return &family.movement;
    if (param >= kReactDepthParamId && param <= kReactPolarityParamId)
      return &family.movement;
    uint32_t destination = 0u;
    uint32_t source = 0u;
    if (decodeMatrixParam(param, destination, source))
      return &family.crosspoint;
    uint32_t lane = 0u;
    clap_id offset = 0u;
    if (!decodeLaneParam(param, lane, offset))
      return nullptr;
    if (offset <= kLaneLevelOffset || offset == kLaneAuxAOffset ||
        offset == kLaneAuxBOffset || offset == kLaneTuneNoteOffset ||
        offset == kLaneTuneCentsOffset)
      return &family.selectedLane;
    if (offset >= kLaneLowOffset && offset <= kLaneHighOffset)
      return &family.eq;
    uint32_t slot = 0u;
    clap_id insertOffset = 0u;
    if (decodeInsertOffset(offset, slot, insertOffset))
      return &family.inserts;
    return nullptr;
  }

  void updateSlider(drawing::Point point) {
    if (_dragParam == CLAP_INVALID_ID)
      return;
    auto *plugin = static_cast<Plugin *>(_plugin);
    if (_dragParam == kMatrixMidiRampParamId) {
      const drawing::Rect track = matrixMidiRampTrackRect();
      const double normalized =
          std::clamp((point.x - track.origin.x) / track.size.width, 0.0, 1.0);
      applyGuiParam_value(_dragParam,
                          s3g::kNoInputMatrixMidiRampMinimumMs +
                              normalized * normalized *
                                  (s3g::kNoInputMatrixMidiRampMaximumMs -
                                   s3g::kNoInputMatrixMidiRampMinimumMs));
      setNeedsDisplay(true);
      return;
    }
    const auto *panel = panelForParam(_dragParam);
    ParamRange range;
    if (!panel || !paramRange(_dragParam, range))
      return;
    const double controlX = s3g::gui_layout::processorControlX(panel->frame.x);
    const double width =
        s3g::gui_layout::processorTrackWidth(panel->frame.width);
    const double normalized =
        std::clamp((point.x - controlX) / width, 0.0, 1.0);
    double value = range.minimum + normalized * (range.maximum - range.minimum);
    if (_dragParam ==
        laneParamId(plugin->selectedLane.load(std::memory_order_relaxed),
                    kLaneMidFrequencyOffset)) {
      value = 80.0 * std::pow(8000.0 / 80.0, normalized);
    }
    applyGuiParam_value(_dragParam, value);
    setNeedsDisplay(true);
  }

  void applyMenuSelection(uint32_t index) {
    auto *plugin = static_cast<Plugin *>(_plugin);
    const uint32_t lane = plugin->selectedLane.load(std::memory_order_relaxed);
    switch (_openMenu) {
    case kMenuPreset:
      if (index < s3g::kNoInputMixerFactoryPresetCount) {
        const auto previous = uiSnapshot(*plugin);
        uint32_t seed = previous.params.seed * 1664525u + 1013904223u;
        if (seed == 0u)
          seed = 1u;
        const float variance = previous.params.variance;
        auto patch = s3g::noInputMixerFactoryPreset(index);
        patch = s3g::variedNoInputMixerParams(patch, seed, variance);
        patch.outputGainDb = previous.params.outputGainDb;
        NoInputSurfaceSnapshot desired{patch,
                                       s3g::noInputMixerFactoryBehavior(index),
                                       previous.auxMute, patch.motion};
        publishUiSnapshot(*plugin, desired);
        if (!enqueueGuiCommand(*plugin,
                               {GuiCommandType::FactoryPreset, CLAP_INVALID_ID,
                                variance, index, seed})) {
          publishUiSnapshot(*plugin, previous);
          break;
        }
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                      s3g::noInputMixerFactoryPresetName(index));
      }
      break;
    case kMenuRandomEnergy:
      if (index < static_cast<uint32_t>(s3g::NoInputRandomEnergy::Count)) {
        const auto energy = static_cast<s3g::NoInputRandomEnergy>(index);
        const auto previous = uiSnapshot(*plugin);
        uint32_t seed = previous.params.seed * 1664525u + 1013904223u;
        if (seed == 0u)
          seed = 1u;
        _randomEnergyProfile = index;
        auto patch = s3g::randomizedNoInputMixerParams(seed, energy);
        patch.outputGainDb = previous.params.outputGainDb;
        NoInputSurfaceSnapshot desired{
            patch,
            s3g::randomizedNoInputMovementBehaviorParams(seed ^ 0x43564d58u,
                                                         energy),
            previous.auxMute, patch.motion};
        publishUiSnapshot(*plugin, desired);
        if (!enqueueGuiCommand(*plugin, {GuiCommandType::RandomPatch,
                                         CLAP_INVALID_ID, 0.0, index, seed})) {
          publishUiSnapshot(*plugin, previous);
          break;
        }
        std::snprintf(
            _titlePresetName, sizeof(_titlePresetName), "RANDOM %s",
            energy == s3g::NoInputRandomEnergy::High
                ? "HIGH"
                : (energy == s3g::NoInputRandomEnergy::Low ? "LOW" : "MID"));
      }
      break;
    case kMenuLane:
      plugin->selectedLane.store(std::min<uint32_t>(index, kChannelCount - 1u),
                                 std::memory_order_relaxed);
      plugin->selectedDestination.store(
          std::min<uint32_t>(index, kChannelCount - 1u),
          std::memory_order_relaxed);
      break;
    case kMenuSource:
      plugin->selectedSource.store(
          std::min<uint32_t>(index, kChannelCount - 1u),
          std::memory_order_relaxed);
      break;
    case kMenuDestination:
      plugin->selectedDestination.store(
          std::min<uint32_t>(index, kChannelCount - 1u),
          std::memory_order_relaxed);
      plugin->selectedLane.store(std::min<uint32_t>(index, kChannelCount - 1u),
                                 std::memory_order_relaxed);
      break;
    case kMenuSlot0:
    case kMenuSlot1:
    case kMenuSlot2: {
      const uint32_t slot = static_cast<uint32_t>(_openMenu - kMenuSlot0);
      plugin->selectedSlot.store(slot, std::memory_order_relaxed);
      applyGuiParam_value(insertParamId(lane, slot, kInsertTypeOffset), index);
      break;
    }
    case kMenuQuality:
      applyGuiParam_value(kQualityParamId, index);
      break;
    case kMenuOutputFormat:
      applyGuiParam_value(kOutputFormatParamId, index);
      break;
    case kMenuMotionShape:
      applyGuiParam_value(kMotionShapeParamId, index);
      break;
    case kMenuBehavior:
      applyGuiParam_value(kBehaviorParamId, index);
      break;
    case kMenuMixerInsert:
      plugin->selectedLane.store(_effectMenuLane, std::memory_order_relaxed);
      plugin->selectedSlot.store(_effectMenuSlot, std::memory_order_relaxed);
      applyGuiParam_value(
          insertParamId(_effectMenuLane, _effectMenuSlot, kInsertTypeOffset),
          index);
      break;
    case kMenuMixerAux:
      applyGuiParam_value(
          _effectMenuBus == 0u ? kAuxATypeParamId : kAuxBTypeParamId, index);
      break;
    case kMenuReactMode:
      applyGuiParam_value(kReactModeParamId, index);
      break;
    case kMenuFieldDivision:
      applyGuiParam_value(kFieldDivisionParamId, index);
      break;
    case kMenuEventDivision:
      applyGuiParam_value(kEventDivisionParamId, index);
      break;
    case kMenuAuxTapA:
    case kMenuAuxTapB:
      applyGuiParam_value(
          laneParamId(_effectMenuLane, _openMenu == kMenuAuxTapA
                                           ? kLaneAuxTapAOffset
                                           : kLaneAuxTapBOffset),
          index);
      break;
    default:
      break;
    }
  }

  void beginSlider_event_point(clap_id param, drawing::Event *event,
                               drawing::Point point) {
    auto *plugin = static_cast<Plugin *>(_plugin);
    double defaultValue = 0.0;
    if (drawing::sliderDoubleClickDefault(event, &plugin->plugin, param,
                                          &defaultValue)) {
      applyGuiParam_value(param, defaultValue);
      _dragParam = CLAP_INVALID_ID;
    } else {
      _dragParam = param;
      updateSlider(point);
    }
    setNeedsDisplay(true);
  }

  void mouseDown(drawing::Event *event) {
    focus();
    drawing::Point point =
        convertPoint_fromView(event->locationInWindow(), nullptr);
    auto *plugin = static_cast<Plugin *>(_plugin);
    const auto visible = uiSnapshot(*plugin);
    const auto &family = s3g::gui_layout::kNoInputMixerFamilyLayout;

    if (_openMenu != kMenuNone) {
      const int hit = drawing::dropdownHitIndex(
          point, menuDropdownRect(_openMenu), 18.0, menuItemCount(_openMenu));
      if (hit >= 0)
        applyMenuSelection(static_cast<uint32_t>(hit));
      _openMenu = kMenuNone;
      _hoverMenuItem = -1;
      setNeedsDisplay(true);
      if (hit >= 0)
        return;
    }

    const auto titleBand = s3g::gui_layout::matrixTitleBand(family.canvas);
    if (drawing::contains(point, drawing::cocoaRect(titleBand.presetMenu))) {
      _openMenu = kMenuPreset;
      _hoverMenuItem = -1;
      setNeedsDisplay(true);
      return;
    }
    if (handleProcessorTitleClick(point, &plugin->plugin, "No Input Mixer",
                                  titleBand, _titlePresetName,
                                  sizeof(_titlePresetName), kOutputParamId)) {
      setNeedsDisplay(true);
      return;
    }

    if (drawing::contains(point, mixerPopButtonRect())) {
      if (_mixerPopupChild) {
        dockPage();
      } else {
        openPagePopup(plugin->guiPage.load(std::memory_order_relaxed));
      }
      return;
    }

    for (uint32_t page = 0u; page < kPageCount; ++page) {
      if (drawing::contains(point, fieldTabRect(page))) {
        if (_mixerPopupChild)
          return;
        if (_pagePanels[page] && _pagePanels[page]->visible()) {
          _pagePanels[page]->show();
          return;
        }
        plugin->guiPage.store(page, std::memory_order_relaxed);
        setNeedsDisplay(true);
        return;
      }
    }

    const uint32_t page = _lockedPage < kPageCount
                              ? _lockedPage
                              : plugin->guiPage.load(std::memory_order_relaxed);
    const drawing::Rect plot = drawing::cocoaRect(family.fieldPlot);
    if (page == 4u && drawing::contains(point, widePageRect())) {
      for (uint32_t lane = 0u; lane < kChannelCount; ++lane) {
        if (!drawing::contains(point, auxPageColumnRect(lane)))
          continue;
        plugin->selectedLane.store(lane, std::memory_order_relaxed);
        for (uint32_t bus = 0u; bus < 2u; ++bus) {
          if (!drawing::contains(point, auxPageTapRect(lane, bus)))
            continue;
          _effectMenuLane = lane;
          _menuAnchor = auxPageTapRect(lane, bus);
          _openMenu = bus == 0u ? kMenuAuxTapA : kMenuAuxTapB;
          _hoverMenuItem = -1;
          setNeedsDisplay(true);
          return;
        }
        const uint32_t rows[4] = {0u, 2u, 3u, 5u};
        const clap_id ids[4] = {
            laneParamId(lane, kLaneAuxAOffset),
            laneParamId(lane, kLaneAuxReturnAOffset),
            laneParamId(lane, kLaneAuxBOffset),
            laneParamId(lane, kLaneAuxReturnBOffset),
        };
        for (uint32_t item = 0u; item < 4u; ++item) {
          const drawing::Rect track = auxPageTrackRect(lane, rows[item]);
          if (!drawing::contains(point, drawing::inset(track, -8.0, -12.0)))
            continue;
          const bool signedReturn = item == 1u || item == 3u;
          beginMixerDrag_rect_minimum_maximum_vertical_point(
              ids[item], track, signedReturn ? -1.0 : 0.0, 1.0, false, point);
          return;
        }
        setNeedsDisplay(true);
        return;
      }
      return;
    }
    /* Retired parameter-surface page remains retired. */
    if (drawing::contains(point, plot)) {
      if (page == 0u) {
        for (uint32_t mode = 0u;
             mode < static_cast<uint32_t>(s3g::NoInputMatrixMidiMode::Count);
             ++mode) {
          if (!drawing::contains(point, matrixMidiModeButtonRect(mode)))
            continue;
          applyGuiParam_value(kMatrixMidiModeParamId, mode);
          return;
        }
        for (uint32_t sign = 0u;
             sign < static_cast<uint32_t>(s3g::NoInputMatrixMidiSign::Count);
             ++sign) {
          if (!drawing::contains(point, matrixMidiSignButtonRect(sign)))
            continue;
          applyGuiParam_value(kMatrixMidiSignParamId, sign);
          return;
        }
        if (drawing::contains(point, matrixMidiRampHitRect())) {
          beginSlider_event_point(kMatrixMidiRampParamId, event, point);
          return;
        }
        if (drawing::contains(point, clearConnectionsButtonRect())) {
          clearAllConnections();
          return;
        }
        for (uint32_t mode = 0u; mode < 2u; ++mode) {
          if (!drawing::contains(point, wiringModeButtonRect(mode)))
            continue;
          _wiringGridMode = mode == 1u;
          if (_mixerPopupChild && _mixerPopupOwner) {
            _mixerPopupOwner->_wiringGridMode = _wiringGridMode;
            _mixerPopupOwner->setNeedsDisplay(true);
          } else if (_pagePopupViews[0u]) {
            _pagePopupViews[0u]->_wiringGridMode = _wiringGridMode;
            _pagePopupViews[0u]->setNeedsDisplay(true);
          }
          setNeedsDisplay(true);
          return;
        }
        if (_wiringGridMode) {
          const drawing::Rect grid = wiringGridRect();
          const double gridLeft = grid.origin.x + 54.0;
          const double gridTop = grid.origin.y + 36.0;
          const double spacing = 58.0;
          const int source =
              static_cast<int>(std::lround((point.x - gridLeft) / spacing));
          const int destination =
              static_cast<int>(std::lround((point.y - gridTop) / spacing));
          if (source >= 0 && source < static_cast<int>(kChannelCount) &&
              destination >= 0 &&
              destination < static_cast<int>(kChannelCount) &&
              std::abs(point.x - (gridLeft + spacing * source)) < 14.0 &&
              std::abs(point.y - (gridTop + spacing * destination)) < 14.0) {
            const bool alreadySelected =
                plugin->selectedSource.load(std::memory_order_relaxed) ==
                    static_cast<uint32_t>(source) &&
                plugin->selectedDestination.load(std::memory_order_relaxed) ==
                    static_cast<uint32_t>(destination);
            plugin->selectedSource.store(source, std::memory_order_relaxed);
            plugin->selectedDestination.store(destination,
                                              std::memory_order_relaxed);
            plugin->selectedLane.store(destination, std::memory_order_relaxed);
            const clap_id id = matrixParamId(destination, source);
            const float current =
                visible.params.matrix[destination * kChannelCount + source];
            const bool negative = altDown;
            const float created = source == destination
                                      ? (negative ? -0.94f : 0.94f)
                                      : (negative ? -0.25f : 0.25f);
            if (std::abs(current) <= 0.001f) {
              applyGuiParam_value(id, created);
            } else if (alreadySelected) {
              applyGuiParam_value(id, 0.0f);
            } else {
              setNeedsDisplay(true);
            }
            return;
          }
        } else {
          for (uint32_t source = 0u; source < kChannelCount; ++source) {
            const drawing::Point port = wiringPortPoint(false, source);
            if (std::hypot(point.x - port.x, point.y - port.y) > 16.0)
              continue;
            _wireDragSource = static_cast<int>(source);
            _wireDragPoint = point;
            setNeedsDisplay(true);
            return;
          }
          float closest = 11.0f;
          int hitSource = -1;
          int hitDestination = -1;
          for (uint32_t destination = 0u; destination < kChannelCount;
               ++destination) {
            for (uint32_t source = 0u; source < kChannelCount; ++source) {
              const float stored =
                  visible.params.matrix[destination * kChannelCount + source];
              if (std::abs(stored) <= 0.001f)
                continue;
              const drawing::Point a = wiringPortPoint(false, source);
              const drawing::Point b = wiringPortPoint(true, destination);
              drawing::Point c1;
              drawing::Point c2;
              wiringControlPoints(a, b, visible.params.vortex, c1, c2);
              for (uint32_t sample = 1u; sample < 32u; ++sample) {
                const drawing::Point curve = cubicPoint(
                    a, c1, c2, b, static_cast<double>(sample) / 32.0);
                const float distance = static_cast<float>(
                    std::hypot(point.x - curve.x, point.y - curve.y));
                if (distance >= closest)
                  continue;
                closest = distance;
                hitSource = static_cast<int>(source);
                hitDestination = static_cast<int>(destination);
              }
            }
          }
          if (hitSource >= 0 && hitDestination >= 0) {
            const bool alreadySelected =
                plugin->selectedSource.load(std::memory_order_relaxed) ==
                    static_cast<uint32_t>(hitSource) &&
                plugin->selectedDestination.load(std::memory_order_relaxed) ==
                    static_cast<uint32_t>(hitDestination);
            plugin->selectedSource.store(hitSource, std::memory_order_relaxed);
            plugin->selectedDestination.store(hitDestination,
                                              std::memory_order_relaxed);
            plugin->selectedLane.store(hitDestination,
                                       std::memory_order_relaxed);
            if (alreadySelected) {
              applyGuiParam_value(matrixParamId(hitDestination, hitSource),
                                  0.0);
            } else {
              setNeedsDisplay(true);
            }
            return;
          }
        }
      } else if (page == 1u) {
        const drawing::Point mixerOffset = mixerSurfaceOffset(plot);
        const auto actual = [&](drawing::Rect rect) {
          return translatedRect(rect, mixerOffset);
        };
        for (uint32_t lane = 0u; lane < kChannelCount; ++lane) {
          const drawing::Rect strip = actual(popupStripRect(lane));
          if (!drawing::contains(point, strip))
            continue;
          plugin->selectedLane.store(lane, std::memory_order_relaxed);
          plugin->selectedDestination.store(lane, std::memory_order_relaxed);
          const auto hit = [&](drawing::Rect rect) {
            return drawing::contains(point, drawing::inset(rect, -6.0, -6.0));
          };
          if (hit(actual(popupBodyRect(popupStripRect(lane))))) {
            beginMixerDrag_rect_minimum_maximum_vertical_point(
                laneParamId(lane, kLaneBodyOffset),
                actual(popupBodyRect(popupStripRect(lane))), 0.0, 1.0, false,
                point);
            return;
          }
          if (hit(actual(popupLossRect(popupStripRect(lane))))) {
            beginMixerDrag_rect_minimum_maximum_vertical_point(
                laneParamId(lane, kLaneLossOffset),
                actual(popupLossRect(popupStripRect(lane))), 0.0, 1.0, false,
                point);
            return;
          }
          if (hit(actual(popupLoopRect(popupStripRect(lane))))) {
            const double polarity = altDown ? -1.0 : 1.0;
            beginMixerDrag_rect_minimum_maximum_vertical_point(
                matrixParamId(lane, lane),
                actual(popupLoopRect(popupStripRect(lane))), 0.0, polarity,
                true, point);
            return;
          }
          const clap_id eqIds[3] = {
              laneParamId(lane, kLaneLowOffset),
              laneParamId(lane, kLaneMidGainOffset),
              laneParamId(lane, kLaneHighOffset),
          };
          for (uint32_t band = 0u; band < 3u; ++band) {
            const drawing::Rect eq =
                actual(popupEqRect(popupStripRect(lane), band));
            if (!hit(eq))
              continue;
            beginMixerDrag_rect_minimum_maximum_vertical_point(
                eqIds[band], eq, -18.0, 18.0, true, point);
            return;
          }
          for (uint32_t bus = 0u; bus < 2u; ++bus) {
            const drawing::Rect send =
                actual(popupSendRect(popupStripRect(lane), bus));
            if (!hit(send))
              continue;
            beginMixerDrag_rect_minimum_maximum_vertical_point(
                laneParamId(lane,
                            bus == 0u ? kLaneAuxAOffset : kLaneAuxBOffset),
                send, 0.0, 1.0, false, point);
            return;
          }
          for (uint32_t slot = 0u; slot < s3g::kNoInputMixerInsertSlots;
               ++slot) {
            const drawing::Rect edit =
                actual(popupInsertEditRect(popupStripRect(lane), slot));
            if (drawing::contains(point, edit)) {
              plugin->selectedSlot.store(slot, std::memory_order_relaxed);
              openEffectEditorForLane_slot(lane, slot);
              setNeedsDisplay(true);
              return;
            }
            const drawing::Rect menu =
                actual(popupInsertMenuRect(popupStripRect(lane), slot));
            if (!drawing::contains(point, menu))
              continue;
            plugin->selectedSlot.store(slot, std::memory_order_relaxed);
            _effectMenuLane = lane;
            _effectMenuSlot = slot;
            _menuAnchor = menu;
            _openMenu = kMenuMixerInsert;
            _hoverMenuItem = -1;
            setNeedsDisplay(true);
            return;
          }
          if (hit(actual(popupFaderRect(popupStripRect(lane))))) {
            beginMixerDrag_rect_minimum_maximum_vertical_point(
                laneParamId(lane, kLaneLevelOffset),
                actual(popupFaderRect(popupStripRect(lane))), -60.0, 12.0, true,
                point);
            return;
          }
          if (drawing::contains(point,
                                actual(popupMuteRect(popupStripRect(lane))))) {
            applyGuiParam_value(
                laneParamId(lane, kLaneMuteOffset),
                (visible.params.lanes[lane].mute == 0u ? 1.0 : 0.0));
            return;
          }
          setNeedsDisplay(true);
          return;
        }
        for (uint32_t bus = 0u; bus < 2u; ++bus) {
          const drawing::Rect muteRect = actual(popupAuxMuteRect(bus));
          if (drawing::contains(point, muteRect)) {
            applyGuiParam_value(bus == 0u ? kAuxAMuteParamId : kAuxBMuteParamId,
                                visible.auxMute[bus] == 0u ? 1.0 : 0.0);
            return;
          }
          const drawing::Rect editRect = actual(popupAuxTypeEditRect(bus));
          if (drawing::contains(point, editRect)) {
            openEffectEditorForAux(bus);
            return;
          }
          const drawing::Rect typeRect = actual(popupAuxTypeMenuRect(bus));
          if (drawing::contains(point, typeRect)) {
            _effectMenuBus = bus;
            _menuAnchor = typeRect;
            _openMenu = kMenuMixerAux;
            _hoverMenuItem = -1;
            setNeedsDisplay(true);
            return;
          }
          const clap_id ids[4] = {
              bus == 0u ? kAuxAGainParamId : kAuxBGainParamId,
              bus == 0u ? kAuxAToneParamId : kAuxBToneParamId,
              bus == 0u ? kAuxAReturnParamId : kAuxBReturnParamId,
              bus == 0u ? kAuxAFeedbackParamId : kAuxBFeedbackParamId,
          };
          for (uint32_t local = 0u; local < 4u; ++local) {
            const drawing::Rect track = actual(popupAuxSliderRect(bus, local));
            if (!drawing::contains(point, drawing::inset(track, -8.0, -8.0)))
              continue;
            beginMixerDrag_rect_minimum_maximum_vertical_point(
                ids[local], track, 0.0, (local == 3u ? 0.96 : 1.0), false,
                point);
            return;
          }
        }
        const clap_id toneIds[2] = {
            kInternalToneParamId,
            kHouseToneParamId,
        };
        for (uint32_t row = 0u; row < 2u; ++row) {
          const drawing::Rect track = actual(popupToneRect(row));
          if (!drawing::contains(point, drawing::inset(track, -8.0, -8.0)))
            continue;
          beginMixerDrag_rect_minimum_maximum_vertical_point(
              toneIds[row], track, -1.0, 1.0, false, point);
          return;
        }
        return;
      } else if (page == 2u) {
        const drawing::Rect overview = channelOverviewRect();
        const double gap = 12.0;
        const double cellWidth = (overview.size.width - gap * 5.0) / 4.0;
        const double cellHeight = (overview.size.height - gap * 3.0) / 2.0;
        for (uint32_t lane = 0u; lane < kChannelCount; ++lane) {
          const uint32_t column = lane % 4u;
          const uint32_t row = lane / 4u;
          drawing::Rect cell = drawing::makeRect(
              overview.origin.x + gap + column * (cellWidth + gap),
              overview.origin.y + gap + row * (cellHeight + gap), cellWidth,
              cellHeight);
          if (drawing::contains(point, cell)) {
            plugin->selectedLane.store(lane, std::memory_order_relaxed);
            setNeedsDisplay(true);
            return;
          }
        }
      }
    }

    const auto openMenuIfHit = [&](int menu) {
      if (drawing::contains(point, menuAnchorRect(menu))) {
        _openMenu = menu;
        _hoverMenuItem = -1;
        setNeedsDisplay(true);
        return true;
      }
      return false;
    };
    if (page == 2u) {
      const uint32_t lane =
          plugin->selectedLane.load(std::memory_order_relaxed);
      for (uint32_t slot = 0u; slot < s3g::kNoInputMixerInsertSlots; ++slot) {
        if (!drawing::contains(point, channelInsertEditRect(slot)))
          continue;
        plugin->selectedSlot.store(slot, std::memory_order_relaxed);
        openEffectEditorForLane_slot(lane, slot);
        setNeedsDisplay(true);
        return;
      }
    }
    if (page == 0u) {
      for (uint32_t bank = 0u; bank < 3u; ++bank) {
        if (!drawing::contains(point, movementBankButtonRect(bank)))
          continue;
        _movementBank = bank;
        _openMenu = kMenuNone;
        _hoverMenuItem = -1;
        setNeedsDisplay(true);
        return;
      }
    }
    if ((page == 0u &&
         (openMenuIfHit(kMenuSource) || openMenuIfHit(kMenuDestination) ||
          (_movementBank == 0u && openMenuIfHit(kMenuMotionShape)) ||
          (_movementBank == 1u && openMenuIfHit(kMenuBehavior)) ||
          (_movementBank == 2u && openMenuIfHit(kMenuReactMode)) ||
          (_movementBank == 0u && visible.params.clockSync &&
           openMenuIfHit(kMenuFieldDivision)) ||
          (_movementBank == 1u &&
           visible.behavior.behavior != s3g::NoInputMovementBehavior::Glide &&
           visible.params.clockSync && openMenuIfHit(kMenuEventDivision)))) ||
        (page == 2u &&
         (openMenuIfHit(kMenuLane) || openMenuIfHit(kMenuSlot0) ||
          openMenuIfHit(kMenuSlot1) || openMenuIfHit(kMenuSlot2))) ||
        (page == 3u && openMenuIfHit(kMenuQuality)))
      return;

    if (page == 0u && drawing::contains(point, seedNewButtonRect())) {
      enqueueGuiCommand(*plugin, {GuiCommandType::NewSeed});
      markPatchCustom();
      setNeedsDisplay(true);
      return;
    }
    if (page == 0u) {
      const auto visible = uiSnapshot(*plugin);
      const clap_id toggleIds[3] = {
          kControllerHoldParamId,
          kSlowTimeParamId,
          kClockSyncParamId,
      };
      const uint32_t toggleValues[3] = {
          visible.params.controllerHold,
          visible.params.slowTime,
          visible.params.clockSync,
      };
      for (uint32_t index = 0u; index < 3u; ++index) {
        if (!drawing::contains(point, movementGlobalToggleRect(index)))
          continue;
        applyGuiParam_value(toggleIds[index],
                            toggleValues[index] == 0u ? 1.0 : 0.0);
        return;
      }
    }
    if (page == 0u && _movementBank == 2u &&
        drawing::contains(point, reactDirectionButtonRect())) {
      const auto visible = uiSnapshot(*plugin);
      applyGuiParam_value(kReactPolarityParamId,
                          visible.params.reactPolarity < 0.0f ? 1.0 : -1.0);
      return;
    }
    if (page == 0u && drawing::contains(point, movementSectionRandomRect())) {
      const auto previous = uiSnapshot(*plugin);
      uint32_t seed = previous.params.seed * 1664525u + 1013904223u;
      if (seed == 0u)
        seed = 1u;
      plugin->uiSeed.store(seed, std::memory_order_relaxed);
      if (!enqueueGuiCommand(*plugin, {GuiCommandType::SetSeed, CLAP_INVALID_ID,
                                       0.0, 0u, seed})) {
        plugin->uiSeed.store(previous.params.seed, std::memory_order_relaxed);
        return;
      }
      const auto energy =
          static_cast<s3g::NoInputRandomEnergy>(std::min<uint32_t>(
              _randomEnergyProfile,
              static_cast<uint32_t>(s3g::NoInputRandomEnergy::Count) - 1u));
      const auto randomParams = s3g::randomizedNoInputMixerParams(seed, energy);
      if (_movementBank == 0u) {
        applyGuiParam_value(kFlowParamId, randomParams.flow);
        applyGuiParam_value(kSpreadParamId, randomParams.spread);
        applyGuiParam_value(kVortexParamId, randomParams.vortex);
        applyGuiParam_value(kMotionParamId, randomParams.motion);
        applyGuiParam_value(kMotionShapeParamId,
                            static_cast<double>(randomParams.motionShape));
        applyGuiParam_value(kMotionRateParamId, randomParams.motionRate);
        applyGuiParam_value(kMotionPhaseParamId, randomParams.motionPhase);
      } else if (_movementBank == 1u) {
        const auto randomBehavior =
            s3g::randomizedNoInputMovementBehaviorParams(seed ^ 0x43564d58u,
                                                         energy);
        applyGuiParam_value(kBehaviorParamId,
                            static_cast<double>(randomBehavior.behavior));
        applyGuiParam_value(kBehaviorDepthParamId, randomParams.motion);
        applyGuiParam_value(kEventRateParamId, randomBehavior.eventRate);
        applyGuiParam_value(kEventLengthParamId, randomBehavior.length);
        applyGuiParam_value(kEventDensityParamId, randomBehavior.density);
        applyGuiParam_value(kEventChaosParamId, randomBehavior.chaos);
        applyGuiParam_value(kEventSlewParamId, randomBehavior.slew);
        applyGuiParam_value(kEventChokeParamId, randomBehavior.choke);
      } else {
        applyGuiParam_value(kReactModeParamId,
                            static_cast<double>(randomParams.reactMode));
        applyGuiParam_value(kReactDepthParamId, randomParams.reactDepth);
        applyGuiParam_value(kReactThresholdParamId,
                            randomParams.reactThreshold);
        applyGuiParam_value(kReactAttackParamId, randomParams.reactAttack);
        applyGuiParam_value(kReactReleaseParamId, randomParams.reactRelease);
        applyGuiParam_value(kReactPolarityParamId, randomParams.reactPolarity);
      }
      return;
    }
    if (page == 0u && drawing::contains(point, randomButtonRect())) {
      _openMenu = kMenuRandomEnergy;
      _hoverMenuItem = -1;
      setNeedsDisplay(true);
      return;
    }
    if (page == 0u && drawing::contains(point, forgetButtonRect())) {
      const auto previous = uiSnapshot(*plugin);
      uint32_t seed = previous.params.seed * 1664525u + 1013904223u;
      if (seed == 0u)
        seed = 1u;
      auto patch = s3g::forgottenNoInputMixerParams(previous.params, seed);
      patch.outputGainDb = previous.params.outputGainDb;
      NoInputSurfaceSnapshot desired{patch, previous.behavior, previous.auxMute,
                                     previous.behaviorDepth};
      publishUiSnapshot(*plugin, desired);
      if (!enqueueGuiCommand(*plugin, {GuiCommandType::ForgetPatch,
                                       CLAP_INVALID_ID, 0.0, 0u, seed})) {
        publishUiSnapshot(*plugin, previous);
        return;
      }
      std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                    "FORGOTTEN");
      setNeedsDisplay(true);
      return;
    }
    if (drawing::contains(point, drawing::cocoaRect(family.panicButton))) {
      enqueueGuiCommand(*plugin, {GuiCommandType::Panic});
      return;
    }

    const uint32_t lane = plugin->selectedLane.load(std::memory_order_relaxed);
    const uint32_t slot = plugin->selectedSlot.load(std::memory_order_relaxed);
    if (page == 3u &&
        drawing::contains(
            point, drawing::cocoaRect(
                       s3g::gui_layout::sliderHitRect(family.output, 4u)))) {
      applyGuiParam_value(kLimiterParamId,
                          (visible.params.limiterEnabled == 0u ? 1.0 : 0.0));
      return;
    }
    if (page == 3u &&
        drawing::contains(
            point, drawing::cocoaRect(
                       s3g::gui_layout::sliderHitRect(family.output, 5u)))) {
      applyGuiParam_value(kDcBlockParamId,
                          (visible.params.dcBlockEnabled == 0u ? 1.0 : 0.0));
      return;
    }
    if (page == 2u &&
        drawing::contains(point,
                          drawing::cocoaRect(s3g::gui_layout::sliderHitRect(
                              family.selectedLane, 6u)))) {
      const double controlX =
          s3g::gui_layout::processorControlX(family.selectedLane.frame.x);
      if (point.x < controlX + 54.0) {
        applyGuiParam_value(
            laneParamId(lane, kLanePitchLockOffset),
            (visible.params.lanes[lane].pitchLock == 0u ? 1.0 : 0.0));
      } else if (point.x < controlX + 112.0) {
        applyGuiParam_value(
            laneParamId(lane, kLaneMuteOffset),
            (visible.params.lanes[lane].mute == 0u ? 1.0 : 0.0));
      } else {
        enqueueGuiCommand(*plugin, {GuiCommandType::KillLane, CLAP_INVALID_ID,
                                    0.0, lane, 0u});
      }
      return;
    }
    const drawing::Rect crosspointActions = drawing::cocoaRect(
        s3g::gui_layout::sliderHitRect(family.crosspoint, 3u));
    const double crosspointSplit =
        s3g::gui_layout::processorControlX(family.crosspoint.frame.x) + 83.0;
    if (page == 0u && drawing::contains(point, crosspointActions) &&
        point.x < crosspointSplit) {
      const uint32_t source =
          plugin->selectedSource.load(std::memory_order_relaxed);
      const uint32_t destination =
          plugin->selectedDestination.load(std::memory_order_relaxed);
      const clap_id id = matrixParamId(destination, source);
      double route = 0.0;
      uiParameterValue(*plugin, id, route);
      applyGuiParam_value(id, (std::abs(route) < 0.001 ? -0.50 : -route));
      return;
    }
    if (page == 0u && drawing::contains(point, crosspointActions) &&
        point.x >= crosspointSplit) {
      applyGuiParam_value(
          matrixParamId(
              plugin->selectedDestination.load(std::memory_order_relaxed),
              plugin->selectedSource.load(std::memory_order_relaxed)),
          0.0);
      return;
    }
    if (page == 2u &&
        drawing::contains(
            point, drawing::cocoaRect(
                       s3g::gui_layout::sliderHitRect(family.inserts, 7u)))) {
      applyGuiParam_value(
          insertParamId(lane, slot, kInsertBypassOffset),
          (visible.params.lanes[lane].inserts[slot].bypass == 0u ? 1.0 : 0.0));
      return;
    }

    const clap_id outputIds[2] = {kOutputParamId, kCeilingParamId};
    for (uint32_t row = 0u; row < 2u; ++row) {
      if (page == 3u &&
          drawing::contains(
              point, drawing::cocoaRect(
                         s3g::gui_layout::sliderHitRect(family.output, row)))) {
        beginSlider_event_point(outputIds[row], event, point);
        return;
      }
    }
    if (page == 3u &&
        drawing::contains(point, processorMenuRect(family.output, 2u))) {
      _openMenu = kMenuOutputFormat;
      _hoverMenuItem = -1;
      setNeedsDisplay(true);
      return;
    }
    if (page == 3u &&
        drawing::contains(
            point, drawing::cocoaRect(
                       s3g::gui_layout::sliderHitRect(family.output, 3u)))) {
      beginSlider_event_point(kOutputRotationParamId, event, point);
      return;
    }
    const clap_id networkIds[8] = {
        kFeedbackParamId, kCouplingParamId, kPhaseParamId, kDriftParamId,
        kFormantParamId,  kAgencyParamId,   kSpaceParamId, kVarianceParamId,
    };
    for (uint32_t row = 1u; row < 9u; ++row) {
      if (page == 0u &&
          drawing::contains(point,
                            drawing::cocoaRect(s3g::gui_layout::sliderHitRect(
                                family.network, row)))) {
        beginSlider_event_point(networkIds[row - 1u], event, point);
        return;
      }
    }
    const clap_id selectedIds[5] = {
        laneParamId(lane, kLaneBodyOffset),
        laneParamId(lane, kLaneLossOffset),
        laneParamId(lane, kLaneTuneNoteOffset),
        laneParamId(lane, kLaneTuneCentsOffset),
        laneParamId(lane, kLaneLevelOffset),
    };
    for (uint32_t row = 1u; row < 6u; ++row) {
      if (page == 2u &&
          drawing::contains(point,
                            drawing::cocoaRect(s3g::gui_layout::sliderHitRect(
                                family.selectedLane, row)))) {
        beginSlider_event_point(selectedIds[row - 1u], event, point);
        return;
      }
    }
    if (page == 0u &&
        drawing::contains(point,
                          drawing::cocoaRect(s3g::gui_layout::sliderHitRect(
                              family.crosspoint, 2u)))) {
      beginSlider_event_point(
          matrixParamId(
              plugin->selectedDestination.load(std::memory_order_relaxed),
              plugin->selectedSource.load(std::memory_order_relaxed)),
          event, point);
      return;
    }
    const clap_id eqIds[4] = {
        laneParamId(lane, kLaneLowOffset),
        laneParamId(lane, kLaneMidFrequencyOffset),
        laneParamId(lane, kLaneMidGainOffset),
        laneParamId(lane, kLaneHighOffset),
    };
    for (uint32_t row = 0u; row < 4u; ++row) {
      if (page == 2u &&
          drawing::contains(
              point, drawing::cocoaRect(
                         s3g::gui_layout::sliderHitRect(family.eq, row)))) {
        beginSlider_event_point(eqIds[row], event, point);
        return;
      }
    }
    const clap_id insertIds[4] = {
        insertParamId(lane, slot, kInsertGainOffset),
        insertParamId(lane, slot, kInsertToneOffset),
        insertParamId(lane, slot, kInsertBiasOffset),
        insertParamId(lane, slot, kInsertLevelOffset),
    };
    for (uint32_t row = 3u; row < 7u; ++row) {
      if (page == 2u &&
          drawing::contains(point,
                            drawing::cocoaRect(s3g::gui_layout::sliderHitRect(
                                family.inserts, row)))) {
        beginSlider_event_point(insertIds[row - 3u], event, point);
        return;
      }
    }
    const clap_id fieldMovementIds[6] = {
        kFlowParamId,   kSpreadParamId,     kVortexParamId,
        kMotionParamId, kMotionRateParamId, kMotionPhaseParamId,
    };
    const clap_id behaviorMovementIds[7] = {
        kBehaviorDepthParamId, kEventRateParamId,  kEventLengthParamId,
        kEventDensityParamId,  kEventChaosParamId, kEventSlewParamId,
        kEventChokeParamId,
    };
    const clap_id reactMovementIds[4] = {
        kReactDepthParamId,
        kReactThresholdParamId,
        kReactAttackParamId,
        kReactReleaseParamId,
    };
    for (uint32_t row = 1u; row < 8u; ++row) {
      if (page == 0u &&
          drawing::contains(point,
                            drawing::cocoaRect(s3g::gui_layout::sliderHitRect(
                                family.movement, row)))) {
        if (_movementBank == 0u && row > 6u)
          return;
        if (_movementBank == 2u && row > 4u)
          return;
        if (_movementBank == 1u) {
          const auto behavior = visible.behavior.behavior;
          const bool active = behavior != s3g::NoInputMovementBehavior::Glide;
          const bool timed = s3g::noInputMovementBehaviorUsesLength(behavior);
          const bool density =
              s3g::noInputMovementBehaviorUsesDensity(behavior);
          if (!active || (row == 3u && !timed) || (row == 4u && !density))
            return;
        }
        const clap_id movementParam =
            _movementBank == 0u
                ? fieldMovementIds[row - 1u]
                : (_movementBank == 1u ? behaviorMovementIds[row - 1u]
                                       : reactMovementIds[row - 1u]);
        beginSlider_event_point(movementParam, event, point);
        return;
      }
    }
  }

  void mouseDragged(drawing::Event *event) {
    const drawing::Point point =
        convertPoint_fromView(event->locationInWindow(), nullptr);
    if (_wireDragSource >= 0) {
      _wireDragPoint = point;
      setNeedsDisplay(true);
    } else if (_surfaceDragCell != -1)
      updateSurfaceDrag(point);
    else if (_mixerDragParam != CLAP_INVALID_ID)
      updateMixerDrag(point);
    else if (_dragParam != CLAP_INVALID_ID)
      updateSlider(point);
  }

  void mouseUp(drawing::Event *event) {
    if (_wireDragSource >= 0) {
      const drawing::Point point =
          convertPoint_fromView(event->locationInWindow(), nullptr);
      auto *plugin = static_cast<Plugin *>(_plugin);
      const auto visible = uiSnapshot(*plugin);
      for (uint32_t destination = 0u; destination < kChannelCount;
           ++destination) {
        const drawing::Point port = wiringPortPoint(true, destination);
        if (std::hypot(point.x - port.x, point.y - port.y) > 18.0)
          continue;
        const uint32_t source = static_cast<uint32_t>(_wireDragSource);
        plugin->selectedSource.store(source, std::memory_order_relaxed);
        plugin->selectedDestination.store(destination,
                                          std::memory_order_relaxed);
        plugin->selectedLane.store(destination, std::memory_order_relaxed);
        const clap_id id = matrixParamId(destination, source);
        const float current =
            visible.params.matrix[destination * kChannelCount + source];
        const bool negative = altDown;
        const float created = source == destination
                                  ? (negative ? -0.94f : 0.94f)
                                  : (negative ? -0.25f : 0.25f);
        applyGuiParam_value(id, (std::abs(current) > 0.001f ? 0.0f : created));
        break;
      }
      _wireDragSource = -1;
      setNeedsDisplay(true);
    }
    _surfaceDragCell = -1;
    _dragParam = CLAP_INVALID_ID;
    _mixerDragParam = CLAP_INVALID_ID;
  }

  void mouseMoved(drawing::Event *event) {
    if (_openMenu == kMenuNone)
      return;
    const drawing::Point point =
        convertPoint_fromView(event->locationInWindow(), nullptr);
    const int next = drawing::dropdownHitIndex(
        point, menuDropdownRect(_openMenu), 18.0, menuItemCount(_openMenu));
    if (next == _hoverMenuItem)
      return;
    _hoverMenuItem = next;
    setNeedsDisplay(true);
  }

  void mouseExited(drawing::Event *event) {
    (void)event;
    if (_hoverMenuItem < 0)
      return;
    _hoverMenuItem = -1;
    setNeedsDisplay(true);
  }
};
} // namespace complex_processor_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new complex_processor_canvas::Editor(p);
}
#include "s3g_clap_canvas_gui.inc"
