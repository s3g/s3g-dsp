// Main-thread editor services. DSP/control ownership stays with each wrapper.
std::string error;
std::vector<clap_id> gestures;
bool pointerEditing = false;
std::vector<double> initializedDisplayValues;
void initializeDisplayGeometry() {
  if (_plugin->guiTelemetryReady.load(std::memory_order_acquire))
    return;
  std::vector<double> values;
  for (uint32_t i = 0; i < paramsCount(&_plugin->plugin); ++i) {
    clap_param_info_t info{};
    double value = 0;
    if (paramsGetInfo(&_plugin->plugin, i, &info) &&
        paramsGetValue(&_plugin->plugin, info.id, &value))
      values.push_back(value);
  }
  if (values == initializedDisplayValues)
    return;
  initializedDisplayValues = std::move(values);
  // A private display engine supplies the original initialized geometry before
  // the first audio block. Never touch the audio-owned engine from the editor.
#if S3G_CIRCUIT_KIND == 1
  auto engine = std::make_unique<s3g::AmbiPulsarEncoder>();
  engine->prepare(48000);
  engine->setParams(_plugin->params);
  engine->reset();
  const uint32_t count = s3g::kAmbiPulsarMaxPoints;
#elif S3G_CIRCUIT_KIND == 2
  auto engine = std::make_unique<s3g::AmbiNeuralEcology>();
  engine->prepare(48000);
  engine->setParams(_plugin->params);
  engine->reset();
  const uint32_t count = s3g::kAmbiNeuralEcologyMaxNodes;
#else
  auto engine = std::make_unique<s3g::AmbiWranglerEncoder>();
  engine->prepare(48000);
  engine->setParams(_paramsSnapshot);
  engine->reset();
  const uint32_t count = _paramsSnapshot.voices;
  _plugin->guiVoiceCount.store(count, std::memory_order_relaxed);
#endif
  for (uint32_t index = 0; index < count; ++index) {
#if S3G_CIRCUIT_KIND == 1
    const auto p = engine->point(index);
#elif S3G_CIRCUIT_KIND == 2
    const auto p = engine->nodePosition(index);
    _plugin->guiActivation[index].store(engine->nodeActivation(index),
                                        std::memory_order_relaxed);
    _plugin->guiValue[index].store(engine->nodeValue(index),
                                   std::memory_order_relaxed);
    _plugin->guiEnergy[index].store(engine->nodeEnergy(index),
                                    std::memory_order_relaxed);
#else
    const auto p = engine->voicePoint(index);
    _plugin->guiRenderGain[index].store(engine->voiceRenderGain(index),
                                        std::memory_order_relaxed);
#endif
    _plugin->guiAzimuth[index].store(p.azimuthDeg, std::memory_order_relaxed);
    _plugin->guiElevation[index].store(p.elevationDeg,
                                       std::memory_order_relaxed);
    _plugin->guiDistance[index].store(p.distance, std::memory_order_relaxed);
  }
}
drawing::Rect bounds() const {
  return drawing::makeRect(0, 0, kGuiWidth, kGuiHeight);
}
drawing::Point convertPoint_fromView(drawing::Point p, void *) { return p; }
void setNeedsDisplay(bool) { invalid(); }
void beep() {
  error = "ACTION UNAVAILABLE / HOST QUEUE BUSY";
  invalid();
}
static uint32_t randomBits() {
  static thread_local std::mt19937 rng(std::random_device{}());
  return rng();
}
static uint32_t randomBounded(uint32_t upper) {
  if (!upper)
    return 0;
  const uint32_t threshold = uint32_t(-upper) % upper;
  uint32_t bits;
  do {
    bits = randomBits();
  } while (bits < threshold);
  return bits % upper;
}
const clap_host_params_t *hostParams() const {
  return _plugin->host && _plugin->host->get_extension
             ? static_cast<const clap_host_params_t *>(
                   _plugin->host->get_extension(_plugin->host, CLAP_EXT_PARAMS))
             : nullptr;
}
void markStateDirty() {
  if (!_plugin->host || !_plugin->host->get_extension)
    return;
  auto *ext = static_cast<const clap_host_state_t *>(
      _plugin->host->get_extension(_plugin->host, CLAP_EXT_STATE));
  if (ext && ext->mark_dirty)
    ext->mark_dirty(_plugin->host);
}
void rescanValues() {
  const auto *ext = hostParams();
  if (ext && ext->rescan)
    ext->rescan(_plugin->host, CLAP_PARAM_RESCAN_VALUES);
  markStateDirty();
}
bool notify(Kind kind, clap_id id, double value = 0) {
  return s3g::clap_gui::enqueueParamEvent(
      _plugin->guiParamEvents, _plugin->host, hostParams(), kind, id, value);
}
void finishGestures() {
  for (size_t n = gestures.size(); n > 0; --n) {
    if (notify(Kind::GestureEnd, gestures[n - 1]))
      gestures.erase(gestures.begin() + n - 1);
  }
}
void editValue(clap_id id, double value) {
  if (!std::isfinite(value))
    return;
  bool found = false;
  for (uint32_t i = 0; i < paramsCount(&_plugin->plugin); ++i) {
    clap_param_info_t info{};
    if (paramsGetInfo(&_plugin->plugin, i, &info) && info.id == id) {
      value = std::clamp(value, info.min_value, info.max_value);
      if (info.flags & CLAP_PARAM_IS_STEPPED)
        value = std::round(value);
      found = true;
      break;
    }
  }
  if (!found)
    return;
  if (_plugin->guiParamEvents.available() <= gestures.size() + 3u) {
    beep();
    return;
  }
  if (std::find(gestures.begin(), gestures.end(), id) == gestures.end()) {
    if (!notify(Kind::GestureBegin, id))
      return;
    gestures.push_back(id);
  }
#if S3G_CIRCUIT_KIND == 1
  applyParam(*_plugin, id, value);
#elif S3G_CIRCUIT_KIND == 2
  applyGuiParam(*_plugin, id, value);
#else
  applyControlParam(*_plugin, id, value);
#endif
  notify(Kind::Value, id, value);
  if (!pointerEditing)
    finishGestures();
  if (id == kPresetParamId)
    rescanValues();
  markStateDirty();
  error.clear();
}
void service() override {
#if S3G_CIRCUIT_KIND == 3
  refreshControlSnapshot();
#else
  syncGuiParams(*_plugin);
#if S3G_CIRCUIT_KIND == 2
  if (_plugin->params.scoreMode != s3g::AmbiNeuralScoreMode::Off)
    _lastScoreMode = static_cast<uint32_t>(_plugin->params.scoreMode);
#endif
#endif
  initializeDisplayGeometry();
}
void stopRefresh() override {
  Canvas::stopRefresh();
  drawing::Event e{};
  mouseUp(&e);
  finishGestures();
  pointerEditing = false;
  _openMenu = 0;
  _hoverMenuItem = -1;
#if S3G_CIRCUIT_KIND == 3
  if (!_surfacePopupChild)
    hideSurfacePopup();
#endif
}
void paint() override {
  service();
  drawing::DrawingScope scope(*this, context, font, titleFont, &error);
#if S3G_CIRCUIT_KIND == 3
  if (_surfacePopupChild) {
    VSTGUI::CDrawContext::Transform transform(
        *context, VSTGUI::CGraphicsTransform().translate(-18., -42.));
    drawField_valueAttrs_style(drawing::softLabelAttrs(),
                               drawing::softValueAttrs(),
                               drawing::softTextStyle());
    drawOpenMenu_style(drawing::softValueAttrs(), drawing::softTextStyle());
    return;
  }
#endif
  drawOriginal(bounds());
}
drawing::Event eventAt(VSTGUI::CPoint p, int clicks = 1) const {
#if S3G_CIRCUIT_KIND == 3
  if (_surfacePopupChild)
    p.offset(18., 42.);
#endif
  return {{p.x, p.y}, clicks};
}
bool pointerDown(VSTGUI::MouseDownEvent &e) override {
  if (!e.buttonState.isLeft())
    return false;
  // Reserve complete factory/random batches and every outstanding end.
  if (_plugin->guiParamEvents.available() <
      paramsCount(&_plugin->plugin) * 3u + 16u) {
    beep();
    return true;
  }
  finishGestures();
  service();
  pointerEditing = true;
  auto event = eventAt(e.mousePosition, e.clickCount);
  mouseDown(&event);
#if S3G_CIRCUIT_KIND == 1
  const bool dragging = _dragParam != CLAP_INVALID_ID;
#elif S3G_CIRCUIT_KIND == 3
  const bool dragging = _dragParam != 0 || _dragSurfaceCursor;
#else
  const bool dragging = _dragParam != 0;
#endif
  if (!dragging) {
    finishGestures();
    pointerEditing = false;
  }
  markStateDirty();
  return true;
}
void pointerMove(VSTGUI::MouseMoveEvent &e) override {
  auto event = eventAt(e.mousePosition);
  if (e.buttonState.isLeft())
    mouseDragged(&event);
  else
    mouseMoved(&event);
}
void pointerUp(VSTGUI::MouseUpEvent &e) override {
  auto event = eventAt(e.mousePosition);
  mouseUp(&event);
  finishGestures();
  pointerEditing = false;
}
bool savePresetPath(const std::string &path) {
  service();
  const auto name =
      foundation::pathToUtf8(foundation::pathFromUtf8(path.c_str()).stem());
  if (!saveCustomPresetFile(path.c_str(), *_plugin, name.c_str()))
    return false;
#if S3G_CIRCUIT_KIND == 3
  setControlCustomPresetName(*_plugin, name.c_str());
#else
  std::snprintf(_plugin->customPresetName, sizeof(_plugin->customPresetName),
                "%s", name.c_str());
  _plugin->customPresetActive.store(true, std::memory_order_relaxed);
#endif
  markStateDirty();
  return true;
}
bool presetDialog(bool save) {
  finishGestures();
  foundation::FileDialogOptions options;
  options.save = save;
  options.title = std::string(save ? "Save " : "Load ") +
                  portablePresetDirectory + " preset";
  options.extensionDescription = "s3g preset";
  options.extension = portablePresetExtension;
  options.defaultSaveName = presetDisplayName() + "." + portablePresetExtension;
  options.initialDirectory =
      foundation::presetDirectory(portablePresetDirectory);
  const auto path = foundation::runFileDialog(getFrame(), options);
  if (path.empty())
    return false;
  const bool ok = save ? savePresetPath(path) : loadPresetPath(path);
  if (ok)
    rescanValues();
  error = ok ? "" : "PRESET FILE COULD NOT BE READ / WRITTEN";
  invalid();
  return ok;
}
bool saveCustomPreset() { return presetDialog(true); }
bool loadCustomPreset() { return presetDialog(false); }
#if S3G_CIRCUIT_KIND == 3
void openSurfacePopup() {
  if (_surfacePopupChild)
    return;
  if (!_surfacePanel) {
    auto panel = std::make_unique<foundation::AuxiliaryWindow>(
        "s3g AMBI ENCODER WRANGLER — SURF", 596, 608);
    auto *child = new Editor(*_plugin, true);
    child->_surfacePopupOwner = this;
    if (!panel->attach(child)) {
      error = "SURF WINDOW COULD NOT OPEN";
      return;
    }
    _surfacePopupView = child;
    _surfacePanel = std::move(panel);
  }
  _surfacePopupView->_fieldPage = 3;
  _surfacePopupView->_surfaceSnapshot = _surfaceSnapshot;
  syncSurfaceEditMode(_surfaceEdit);
  _fieldPage = 0;
  void *parent =
      getFrame() ? getFrame()->getPlatformFrame()->getPlatformRepresentation()
                 : nullptr;
  if (!_surfacePanel->show(parent))
    error = "SURF WINDOW COULD NOT OPEN";
  invalid();
}
void hideSurfacePopup() {
  if (_surfacePanel)
    _surfacePanel->hide();
}
#endif
