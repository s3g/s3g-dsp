// Literal Cocoa geometry and interaction adaptation; see
// DECODERS_VSTGUI_PARITY.md.
namespace decoder_canvas {
namespace drawing = s3g::portable_gui::decoder_drawing;
namespace foundation = s3g::portable_gui::foundation;
using namespace VSTGUI;
using namespace s3g::portable_gui::routing;
using Kind = s3g::clap_gui::ParamEventKind;
constexpr const char *pluginName = "Ambi Decoder Sub";
constexpr const char *pluginTitle = "s3g AMBI DECODER SUB";
constexpr clap_id preservedOutput = kOutputParamId;
constexpr const auto &stateExtension = stateExt;
double routingReadValue(const Plugin &p, clap_id id) {
  return decoderIsParam(id)
             ? p.decoderValues[id].load(std::memory_order_acquire)
             : 0.;
}
void routingPublish(Plugin &p,
                    const std::vector<std::pair<clap_id, double>> &values) {
  auto params = snapshotDecoderParams(p);
  for (auto [id, v] : values)
    setDecoderScalar(params, id, v);
  for (auto [id, v] : values)
    p.decoderValues[id].store(readDecoderScalar(params, id),
                              std::memory_order_release);
  if (decoderHasLayoutCount)
    p.decoderLayoutCount.store(decoderHiddenCount(params),
                               std::memory_order_release);
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
class Editor final : public ParameterEditor {
public:
  explicit Editor(Plugin &instance) : ParameterEditor(instance) {
    auto *plugin = &p;

    _plugin = plugin;
    _dragControl = -1;
    _openMenu = 0;
    _hoverMenuItem = -1;
    _menuOrigin = drawing::Point{};
    _menuItemCount = 0;

    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "CURRENT");

    service();
  }
  ~Editor() override { stopRefresh(); }
  void stopRefresh() override {
    ParameterEditor::stopRefresh();
    pointerEditing = false;
    _openMenu = 0;
    _dragControl = -1;
  }
  double refreshPeriodMilliseconds() const override { return 1000. / 24.; }
  void service() override { guiParams = snapshotDecoderParams(p); }
  void preparePresetSave(Plugin &candidate) override {
    candidate.decoderStateBase = p.decoderStateBase;
    candidate.decoderLayoutCount.store(p.decoderLayoutCount.load());
  }
  void finishPresetLoad(const Plugin &candidate) override {
    publishDecoderStateBase(p, candidate.decoderStateBase);
    p.decoderLayoutCount.store(candidate.decoderLayoutCount.load());

    service();
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
  DecoderParams guiParams{};

  Plugin *_plugin;
  int _dragControl;
  int _openMenu;
  int _hoverMenuItem;
  drawing::Point _menuOrigin;
  uint32_t _menuItemCount;

  char _titlePresetName[64];
  drawing::String textForParam_value(clap_id param, double value) {

    char buf[64]{};
    paramsValueToText(&_plugin->plugin, param, value, buf, sizeof(buf));
    return drawing::stringFromUtf8(buf);
  }
  void openOrderMenuAt(drawing::Point origin) {

    _openMenu = 1;
    _hoverMenuItem = -1;
    _menuOrigin = origin;
    _menuItemCount = s3g::kAmbiSpeakerDecoderMaxOrder + 1u;
    setNeedsDisplay(true);
  }
  void closeMenu() {

    _openMenu = 0;
    _hoverMenuItem = -1;
    _menuItemCount = 0;
    setNeedsDisplay(true);
  }
  void drawOpenMenu_style(drawing::Attrs attrs, const drawing::Style &style) {

    if (_openMenu != 1 || _menuItemCount == 0)
      return;
    static drawing::String orderItems[] = {"0OA", "1OA", "2OA", "3OA",
                                           "4OA", "5OA", "6OA", "7OA"};
    const double itemH = 18.0;
    const drawing::Rect menuRect =
        drawing::makeRect(_menuOrigin.x, _menuOrigin.y, 112.0,
                          itemH * static_cast<double>(_menuItemCount));
    const int selected = static_cast<int>(std::clamp<uint32_t>(
        _plugin ? guiParams.order : 3u, 0u, s3g::kAmbiSpeakerDecoderMaxOrder));
    drawing::drawDropdownMenu(menuRect, itemH, orderItems, _menuItemCount,
                              selected, _hoverMenuItem, attrs, style);
  }
  void drawSubField_attrs_style(drawing::Rect rect, drawing::Attrs attrs,
                                const drawing::Style &style) {

    drawing::drawPanelFrame(rect.origin.x, rect.origin.y, rect.size.width,
                            rect.size.height, style);
    drawing::drawPanelHeader("SUB FIELD", true, rect.origin.x, rect.origin.y,
                             rect.size.width, 21, attrs, style);
    const drawing::Rect field =
        drawing::makeRect(rect.origin.x + 12.0, rect.origin.y + 34.0,
                          rect.size.width - 24.0, rect.size.height - 72.0);
    drawing::setFill(drawing::calibratedColor(0x101010));
    drawing::rectFill(field);
    drawing::setStroke(style.grid);
    drawing::frameRect(field);
    const double cx = drawing::midX(field);
    const double cy = drawing::midY(field);
    const double radius = std::min(field.size.width, field.size.height) * 0.38;
    drawing::setStroke(drawing::calibratedColor(0x575757, 0.22));
    drawing::Path ring =
        drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
            cx - radius, cy - radius, radius * 2.0, radius * 2.0));
    (ring).setLineWidth(1.0);
    (ring).stroke();
    drawing::setStroke(drawing::calibratedColor(0x383838, 0.55));
    (drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
         cx - radius * 0.5, cy - radius * 0.5, radius, radius)))
        .stroke();

    const uint32_t count = std::clamp<uint32_t>(guiParams.subCount, 1u,
                                                s3g::kAmbiSubDecoderMaxSubs);
    for (uint32_t i = 0; i < count; ++i) {
      const double az = (-45.0 - static_cast<double>(i) * 360.0 /
                                     static_cast<double>(count)) *
                        M_PI / 180.0;
      const double x = cx - std::sin(az) * radius;
      const double y = cy - std::cos(az) * radius;
      drawing::Rect sq = drawing::makeRect(x - 6.0, y - 6.0, 12.0, 12.0);
      drawing::setFill(style.fill);
      drawing::rectFill(sq);
      drawing::setStroke(style.text);
      drawing::frameRect(sq);
      drawing::String label = drawing::stringFormat("%u", i + 1u);
      drawing::drawAtPoint_withAttributes(
          label, drawing::makePoint(x + 9.0, y - 6.0), attrs);
    }

    drawing::String caption =
        count == 1u ? "W / mono sub decode" : "horizontal FOA sub decode";
    drawing::drawAtPoint_withAttributes(
        caption,
        drawing::makePoint(rect.origin.x + 12.0, drawing::maxY(rect) - 27.0),
        attrs);
  }
  void paint() override {
    drawing::DrawingScope scope(*this, context, font, titleFont, &error);
    service();

    if (!_plugin)
      return;
    const drawing::Style style{};
    drawing::Attrs titleAttrs = drawing::softTitleAttrs();
    drawing::Attrs attrs = drawing::softLabelAttrs();
    drawing::Attrs values = drawing::softValueAttrs();

    drawing::setFill(style.bg);
    drawing::rectFill(bounds());
    drawing::drawDecoderTitleBand(
        "s3g AMBI DECODER SUB", drawing::stringFromUtf8(_titlePresetName), "",
        drawing::encoderTitleBand(kGuiWidth, kGuiHeight), titleAttrs, attrs,
        values, style);

    drawSubField_attrs_style(drawing::makeRect(18, 42, 250, 290), attrs, style);

    drawing::drawPanelFrame(280, 42, 302, 54, style);
    drawing::drawPanelHeader("OUTPUT", true, 280, 42, 302, 21, attrs, style);
    drawing::drawSlider(
        "OUT", textForParam_value(kOutputParamId, guiParams.outputGainDb),
        (guiParams.outputGainDb + 60.0f) / 78.0f, 78, attrs, values, style, 296,
        388, 496, 92, 68);

    drawing::drawPanelFrame(280, 108, 302, 54, style);
    drawing::drawPanelHeader("DECODER", true, 280, 108, 302, 21, attrs, style);
    drawing::drawMenu("ORDER",
                      textForParam_value(kOrderParamId, guiParams.order), 144,
                      attrs, values, style, 296, 388, 112);

    drawing::drawPanelFrame(280, 174, 302, 158, style);
    drawing::drawPanelHeader("SUB CONTROL", true, 280, 174, 302, 21, attrs,
                             style);
    drawing::drawSlider(
        "SUBS", textForParam_value(kSubCountParamId, guiParams.subCount),
        (guiParams.subCount - 1.0) / 7.0, 210, attrs, values, style, 296, 388,
        496, 92, 68);
    drawing::drawSlider("CUTOFF",
                        textForParam_value(kCutoffParamId, guiParams.cutoffHz),
                        (guiParams.cutoffHz - 20.0f) / 220.0f, 236, attrs,
                        values, style, 296, 388, 496, 92, 68);
    drawing::drawSlider(
        "WIDTH", textForParam_value(kWidthParamId, guiParams.directionWidth),
        guiParams.directionWidth / 2.0f, 262, attrs, values, style, 296, 388,
        496, 92, 68);
    drawing::drawToggle("BYPASS", guiParams.bypass, 288, attrs, values, style,
                        296, 388, 112);
    drawOpenMenu_style(values, style);
  }
  void updateDrag(drawing::Point pt) {

    if (!_plugin || _dragControl < 0)
      return;
    const double n =
        std::clamp((static_cast<double>(pt.x) - 388.0) / 92.0, 0.0, 1.0);
    switch (_dragControl) {
    case 1:
      guiApplyParam(*_plugin, kSubCountParamId, 1.0 + n * 7.0);
      break;
    case 2:
      guiApplyParam(*_plugin, kCutoffParamId, 20.0 + n * 220.0);
      break;
    case 3:
      guiApplyParam(*_plugin, kWidthParamId, n * 2.0);
      break;
    case 4:
      guiApplyParam(*_plugin, kOutputParamId, -60.0 + n * 78.0);
      break;
    default:
      break;
    }
    setNeedsDisplay(true);
  }
  void mouseDown(drawing::Event *event) {

    const drawing::Point pt =
        convertPoint_fromView(event->locationInWindow(), nullptr);
    const auto titleBand = drawing::encoderTitleBand(kGuiWidth, kGuiHeight);
    if (handleTitleClick(pt, titleBand))
      return;
    if (_openMenu == 1) {
      const double itemH = 18.0;
      const drawing::Rect menuRect =
          drawing::makeRect(_menuOrigin.x, _menuOrigin.y, 112.0,
                            itemH * static_cast<double>(_menuItemCount));
      const int hit =
          drawing::dropdownHitIndex(pt, menuRect, itemH, _menuItemCount);
      if (hit >= 0) {
        guiApplyParam(*_plugin, kOrderParamId, static_cast<double>(hit));
        closeMenu();
        return;
      }
      closeMenu();
    }
    if (drawing::contains(pt, drawing::makeRect(388, 138, 112, 22))) {
      openOrderMenuAt(drawing::makePoint(388, 161));
      return;
    }
    if (drawing::contains(pt, drawing::makeRect(388, 282, 112, 22))) {
      guiApplyParam(*_plugin, kBypassParamId, guiParams.bypass ? 0.0 : 1.0);
      setNeedsDisplay(true);
      return;
    }
    const double ys[] = {78, 210, 236, 262};
    const int controls[] = {4, 1, 2, 3};
    const clap_id params[] = {kOutputParamId, kSubCountParamId, kCutoffParamId,
                              kWidthParamId};
    for (int i = 0; i < 4; ++i) {
      if (drawing::contains(pt, drawing::makeRect(388, ys[i] - 5, 92, 22))) {
        double defaultValue = 0.0;
        if (drawing::sliderDoubleClickDefault(event, &_plugin->plugin,
                                              params[i], &defaultValue)) {
          guiApplyParam(*_plugin, params[i], defaultValue);
          _dragControl = -1;
          setNeedsDisplay(true);
          return;
        }
        _dragControl = controls[i];
        updateDrag(pt);
        return;
      }
    }
  }
  void mouseMoved(drawing::Event *event) {

    if (_openMenu != 1 || _menuItemCount == 0)
      return;
    const drawing::Point pt =
        convertPoint_fromView(event->locationInWindow(), nullptr);
    const int hover = drawing::dropdownHitIndex(
        pt,
        drawing::makeRect(_menuOrigin.x, _menuOrigin.y, 112.0,
                          18.0 * _menuItemCount),
        18.0, _menuItemCount);
    if (hover != _hoverMenuItem) {
      _hoverMenuItem = hover;
      setNeedsDisplay(true);
    }
  }
  void mouseDragged(drawing::Event *event) {
    updateDrag(convertPoint_fromView(event->locationInWindow(), nullptr));
  }
  void mouseUp(drawing::Event *event) {
    (void)event;
    _dragControl = -1;
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
  void resetDecoder() {
    const double out = value(preservedOutput);
    DecoderParams defaults{};
    std::vector<std::pair<clap_id, double>> values;
    for (const auto &i : infos)
      if (i.id != preservedOutput)
        values.emplace_back(i.id, readDecoderScalar(defaults, i.id));
    if (batch(values)) {
      setDecoderScalar(defaults, preservedOutput, out);
      publishDecoderStateBase(p, defaults);
      p.decoderLayoutCount.store(decoderHiddenCount(defaults));
      presetName = "INIT";
    }
  }
};
} // namespace decoder_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new decoder_canvas::Editor(p);
}
#include "s3g_clap_canvas_gui.inc"
