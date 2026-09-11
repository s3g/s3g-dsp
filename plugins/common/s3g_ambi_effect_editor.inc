// Shared parameter/preset/lifecycle integration for the pickup editors.
using namespace s3g::portable_gui::routing;
double routingReadValue(const Plugin &p, clap_id id) { return getParam(p, id); }
void routingPublish(Plugin &p,
                    const std::vector<std::pair<clap_id, double>> &values) {
  auto params = snapshotEffectParams(p);
  for (auto [id, v] : values)
    setEffectScalar(params, id, v);
  for (auto [id, v] : values)
    p.effectValues[id].store(readEffectScalar(params, id),
                             std::memory_order_release);
}
void routingApply(Plugin &p, clap_id id, double v) { applyParam(p, id, v); }
double routingRawValue(Plugin &p, clap_id id) { return getParam(p, id); }
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
    _plugin = &p;
    _dragSlider = -1;
    _openMenu = 0;
    _hoverMenuItem = -1;
    _menuItems = 0;
    _selectedPickup = 0;
    _dragView = false;
    syncView();
    guiParams = snapshotEffectParams(p);
    presetName = p.presetName;
  }
  ~Editor() override { stopRefresh(); }
  void stopRefresh() override {
    ParameterEditor::stopRefresh();
    _dragSlider = -1;
    _dragView = false;
    _openMenu = 0;
  }
  double refreshPeriodMilliseconds() const override { return 1000. / 24.; }
  void syncView() {
    _viewMode = p.guiViewMode;
    _viewAzDeg = p.guiViewAzDeg;
    _viewElDeg = p.guiViewElDeg;
  }
  void service() override {
    syncView();
    guiParams = snapshotEffectParams(p);
  }
  void preparePresetSave(Plugin &candidate) override {
    candidate.effectStateBase = p.effectStateBase;
    candidate.guiViewMode = p.guiViewMode;
    candidate.guiViewAzDeg = p.guiViewAzDeg;
    candidate.guiViewElDeg = p.guiViewElDeg;
  }
  void finishPresetLoad(const Plugin &candidate) override {
    publishEffectStateBase(p, candidate.effectStateBase);
    p.guiViewMode = candidate.guiViewMode;
    p.guiViewAzDeg = candidate.guiViewAzDeg;
    p.guiViewElDeg = candidate.guiViewElDeg;
    syncView();
    guiParams = snapshotEffectParams(p);
  }
  bool handleTitleClick(drawing::Point point, const clap_plugin_t *,
                        const drawing::String &,
                        const s3g::gui_layout::EncoderTitleBand &band,
                        char *name, size_t capacity, clap_id) {
    if (drawing::contains(point, drawing::cocoaRect(band.presetMenu)))
      resetParameters();
    else if (drawing::contains(point, drawing::cocoaRect(band.loadButton)))
      presetDialog(false);
    else if (drawing::contains(point, drawing::cocoaRect(band.saveButton)))
      presetDialog(true);
    else
      return false;
    std::snprintf(name, capacity, "%s", presetName.c_str());
    service();
    return true;
  }
  void setParam_value(clap_id id, double value) {
    if (_dragSlider == int(id)) {
      if (begin(id))
        change(id, value);
    } else
      set(id, value);
    guiParams = snapshotEffectParams(p);
    invalid();
  }
  drawing::Rect bounds() const {
    return drawing::makeRect(0, 0, kGuiWidth, kGuiHeight);
  }
  drawing::Point convertPoint_fromView(drawing::Point p, std::nullptr_t) const {
    return p;
  }
  void setNeedsDisplay(bool) { invalid(); }
  bool pointerDown(MouseDownEvent &event) override {
    if (!event.buttonState.isLeft())
      return false;
    service();
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
    invalid();
  }
  EffectParams guiParams{};
