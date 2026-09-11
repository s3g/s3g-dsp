#include "s3g_ambi_effect_displacement_score.inc"
double routingReadValue(const Plugin &p, clap_id id) { return getParam(p, id); }
void routingPublish(Plugin &p,
                    const std::vector<std::pair<clap_id, double>> &values) {
  auto params = snapshotEffectParams(p);
  for (auto [id, v] : values)
    setEffectScalar(params, id, v);
  for (auto [id, v] : values)
    p.effectValues[id].store(readEffectScalar(params, id),
                             std::memory_order_release);
  if (params.transport == TransportMode::Scrub)
    p.guiPhase.store(params.position, std::memory_order_relaxed);
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
    _dragParam = CLAP_INVALID_ID;
    _openMenu = 0;
    _hoverMenuItem = -1;
    _dragView = _dragTimeline = false;
    presetName = p.presetName;
    service();
  }
  ~Editor() override { stopRefresh(); }
  void stopRefresh() override {
    ParameterEditor::stopRefresh();
    _dragParam = CLAP_INVALID_ID;
    _dragView = _dragTimeline = false;
    _openMenu = 0;
  }
  double refreshPeriodMilliseconds() const override { return 1000. / 30.; }
  void service() override {
    guiParams = snapshotEffectParams(p);
    _viewMode = p.guiViewMode;
    _viewAzimuthDeg = p.guiViewAzimuthDeg;
    _viewElevationDeg = p.guiViewElevationDeg;
    _viewZoom = p.guiViewZoom;
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                  presetName.c_str());
    std::snprintf(p.presetName, sizeof(p.presetName), "%s", presetName.c_str());
  }
  void preparePresetSave(Plugin &candidate) override {
    copyDisplacementPresentation(p, candidate);
  }
  void finishPresetLoad(const Plugin &candidate) override {
    auto restored = std::make_unique<Plugin>();
    restored->params = snapshotEffectParams(p);
    copyDisplacementPresentation(candidate, *restored);
    publishDisplacementState(p, *restored);
    service();
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
  void setParam_value(clap_id id, double v) {
    if (_dragParam == id || _dragTimeline) {
      if (begin(id))
        change(id, v);
    } else
      set(id, v);
    guiParams = snapshotEffectParams(p);
    invalid();
  }
  void installScore(Plugin &instance, s3g::AmbiEffectDisplacementScore score,
                    float amount, float az, float el, float radius,
                    const std::string &name) {
    if (!batch({{kParamAmount, amount},
                {kParamAzimuthScale, az},
                {kParamElevationScale, el},
                {kParamRadiusScale, radius}})) {
      instance.status = "PARAMETER QUEUE BUSY";
      return;
    }
    instance.score = s3g::sanitizeAmbiEffectDisplacementScore(score);
    instance.scoreName = name.empty() ? "DISPLACEMENT SCORE" : name;
    instance.status =
        std::to_string(instance.score.sceneCount) + " SCENES / 24 POINTS";
    if (instance.active)
      installRuntime(instance);
    service();
    invalid();
  }
  bool scoreFile(const std::string &path) {
    std::ifstream file(foundation::pathFromUtf8(path.c_str()),
                       std::ios::binary | std::ios::ate);
    const auto bytes = file ? file.tellg() : std::streampos(-1);
    if (bytes <= 0 || bytes > std::streampos(maximumScoreBytes)) {
      p.status = "FILE SIZE IS INVALID";
      invalid();
      return false;
    }
    std::string contents(static_cast<size_t>(bytes), '\0');
    file.seekg(0);
    if (!file.read(contents.data(),
                   static_cast<std::streamsize>(contents.size()))) {
      p.status = "UNABLE TO READ SCORE";
      invalid();
      return false;
    }
    s3g::AmbiEffectDisplacementScore score{};
    float amount = .65f, az = 1.f, el = 1.f, radius = 1.f;
    std::string name, message;
    if (!parsePortableScore(contents, score, amount, az, el, radius, name,
                            message)) {
      p.status = message;
      invalid();
      return false;
    }
    if (name.empty())
      name =
          foundation::pathToUtf8(foundation::pathFromUtf8(path.c_str()).stem());
    installScore(p, score, amount, az, el, radius, name);
    return true;
  }
  void loadScore() {
    foundation::FileDialogOptions options;
    options.title = "Load Displacement Score";
    options.extension = "json";
    options.extensionDescription = "Displacement Score";
    const auto path = foundation::runFileDialog(getFrame(), options);
    if (!path.empty())
      scoreFile(path);
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
  void wheel(MouseWheelEvent &event) override {
    const drawing::Point point{event.mousePosition.x, event.mousePosition.y};
    if (!drawing::contains(point, fieldRect()) || _viewMode == 3)
      return;
    drawing::Event e{point, 1, -event.deltaY * 10.};
    scrollWheel(&e);
    event.consumed = true;
  }
  EffectParams guiParams{};
