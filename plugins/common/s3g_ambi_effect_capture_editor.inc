using namespace s3g::portable_gui::routing;
double routingReadValue(const Plugin &p, clap_id id) { return getParam(p, id); }
void routingPublish(Plugin &p,
                    const std::vector<std::pair<clap_id, double>> &values) {
  const auto before = snapshotEffectParams(p);
  auto params = before;
  for (auto [id, v] : values)
    setEffectScalar(params, id, v);
#if defined(S3G_PARTIAL_TRACE) && S3G_PARTIAL_TRACE
  double previousMaximum = before.maximumFrequencyHz;
  if (params.maximumFrequencyHz != previousMaximum)
    p.effectValues[kParamMaximumFrequency].compare_exchange_strong(
        previousMaximum, params.maximumFrequencyHz, std::memory_order_acq_rel);
#endif
  for (auto [id, v] : values) {
    if (v > .5 && id == kParamCapture)
      p.captureRequest.store(true, std::memory_order_release);
    if (v > .5 && id == kParamClear)
      p.clearRequest.store(true, std::memory_order_release);
    p.effectValues[id].store(readEffectScalar(params, id),
                             std::memory_order_release);
  }
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
    _dragView = false;
#if !defined(S3G_PARTIAL_TRACE)
    _selectedPickup = 0;
#endif
    service();
    presetName = p.presetName;
  }
  ~Editor() override { stopRefresh(); }
  void stopRefresh() override {
    ParameterEditor::stopRefresh();
    _dragSlider = -1;
    _dragView = false;
    _openMenu = 0;
    _hoverMenuItem = -1;
  }
  double refreshPeriodMilliseconds() const override { return 1000. / 24.; }
  void service() override {
    _viewMode = p.guiViewMode;
    _viewAzDeg = p.guiViewAzDeg;
    _viewElDeg = p.guiViewElDeg;
    guiParams = snapshotEffectParams(p);
    std::snprintf(p.presetName, sizeof(p.presetName), "%s", presetName.c_str());
  }
  void preparePresetSave(Plugin &candidate) override {
    prepareCapturePresetSave(p, candidate);
  }
  void finishPresetLoad(const Plugin &candidate) override {
    finishCapturePresetLoad(p, candidate);
    service();
  }
  bool handleTitleClick(drawing::Point point, const clap_plugin_t *,
                        const drawing::String &,
                        const s3g::gui_layout::EncoderTitleBand &band,
                        char *name, size_t capacity, clap_id = kParamOutput) {
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
  void setParam_value(clap_id id, double v) {
    if (_dragSlider == int(id) ||
        std::find(gestures.begin(), gestures.end(), id) != gestures.end()) {
      if (begin(id))
        change(id, v);
    } else
      set(id, v);
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
