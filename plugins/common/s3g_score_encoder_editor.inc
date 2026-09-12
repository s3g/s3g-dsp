// Platform services only. The retained Cocoa canvas owns layout and hit maps.
std::string error;
bool cancelled = false, pointerEditing = false;
bool pointerShift = false;
std::vector<clap_id> gestures;
drawing::Rect bounds() const {
  return drawing::makeRect(0, 0, kGuiWidth, kGuiHeight);
}
drawing::Point convertPoint_fromView(drawing::Point p, void *) { return p; }
void setNeedsDisplay(bool) { invalid(); }
void beep() {
  if (!cancelled && error.empty())
    error = "ACTION UNAVAILABLE / HOST QUEUE BUSY";
  cancelled = false;
  invalid();
}
auto &events() {
#if S3G_SCORE_KIND == 2
  return _plugin->guiEvents;
#else
  return _plugin->guiParamEvents;
#endif
}
const clap_host_params_t *hostParams() const {
  return _plugin->host && _plugin->host->get_extension
             ? static_cast<const clap_host_params_t *>(
                   _plugin->host->get_extension(_plugin->host, CLAP_EXT_PARAMS))
             : nullptr;
}
void request() {
  s3g::clap_gui::requestParamEventService(_plugin->host, hostParams());
}
void markStateDirty() {
  if (!_plugin->host || !_plugin->host->get_extension)
    return;
  const auto *state = static_cast<const clap_host_state_t *>(
      _plugin->host->get_extension(_plugin->host, CLAP_EXT_STATE));
  if (state && state->mark_dirty)
    state->mark_dirty(_plugin->host);
}
void rescanValues() {
  const auto *params = hostParams();
  if (params && params->rescan)
    params->rescan(_plugin->host, CLAP_PARAM_RESCAN_VALUES);
  markStateDirty();
}
double value(clap_id id) const {
  double result = 0.;
  paramsGetValue(&_plugin->plugin, id, &result);
  return result;
}
bool beginEdit(clap_id id) {
  if (std::find(gestures.begin(), gestures.end(), id) != gestures.end())
    return true;
  if (events().available() <= gestures.size() + 4u) {
    beep();
    return false;
  }
  if (!events().push({Kind::GestureBegin, id, 0.}))
    return false;
  gestures.push_back(id);
  request();
  return true;
}
void endEdit(clap_id id) {
  auto found = std::find(gestures.begin(), gestures.end(), id);
  if (found == gestures.end())
    return;
  if (events().push({Kind::GestureEnd, id, 0.})) {
    gestures.erase(found);
    request();
  }
}
void finishGestures() {
  for (size_t i = gestures.size(); i > 0; --i)
    endEdit(gestures[i - 1]);
}
void editValue(clap_id id, double requested) {
  if (!std::isfinite(requested))
    return;
  bool valid = false;
  for (uint32_t n = 0; n < paramsCount(&_plugin->plugin); ++n) {
    clap_param_info_t info{};
    if (paramsGetInfo(&_plugin->plugin, n, &info) && info.id == id) {
      requested = std::clamp(requested, info.min_value, info.max_value);
      if (info.flags & CLAP_PARAM_IS_STEPPED)
        requested = std::round(requested);
      valid = true;
      break;
    }
  }
  if (!valid || events().available() <= gestures.size() + 4u) {
    beep();
    return;
  }
  const bool single =
      !pointerEditing &&
      std::find(gestures.begin(), gestures.end(), id) == gestures.end();
  if (!beginEdit(id))
    return;
#if S3G_SCORE_KIND == 1
  publishParam(*_plugin, id, requested);
#elif S3G_SCORE_KIND == 2
  _plugin->visible[id].store(requested, std::memory_order_release);
#else
  _plugin->portableValues[id].store(requested, std::memory_order_release);
  _snapshot = portableParamsSnapshot(*_plugin);
#endif
  events().push({Kind::Value, id, requested});
  request();
  if (single)
    endEdit(id);
  error.clear();
  markStateDirty();
}
void singleEdit(clap_id id, double requested) {
  if (beginEdit(id)) {
    editValue(id, requested);
    endEdit(id);
  }
}
void startRefresh() override {
#if S3G_SCORE_KIND != 1
  _plugin->guiVisible.store(true, std::memory_order_release);
#endif
  service();
  Canvas::startRefresh();
}
void stopRefresh() override {
  Canvas::stopRefresh();
  drawing::Event e{};
  mouseUp(&e);
  finishGestures();
  pointerEditing = false;
#if S3G_SCORE_KIND == 1
  _presetMenuOpen = false;
  _scaleMenuOpen = false;
  _parameterMenuId = CLAP_INVALID_ID;
#else
  _openMenu = 0;
  storeViewState();
  _plugin->guiVisible.store(false, std::memory_order_release);
#endif
}
void paint() override {
  drawing::DrawingScope scope(*this, context, font, titleFont, &error);
  drawOriginal(bounds());
}
bool pointerDown(VSTGUI::MouseDownEvent &e) override {
  if (!e.buttonState.isLeft())
    return false;
  finishGestures();
  // Room for the largest preset/pattern batch and every matching gesture end.
  if (events().available() < paramsCount(&_plugin->plugin) * 3u + 32u) {
    beep();
    return true;
  }
  if (getFrame())
    getFrame()->setFocusView(this);
  service();
  pointerEditing = true;
  cancelled = false;
  pointerShift = e.modifiers.has(VSTGUI::ModifierKey::Shift);
  drawing::Event event{{e.mousePosition.x, e.mousePosition.y},
                       int(e.clickCount)};
  mouseDown(&event);
#if S3G_SCORE_KIND == 1
  const bool dragging =
      _dragParam > 0 || _dragStep >= 0 || _dragSpatialView >= 0;
#elif S3G_SCORE_KIND == 2
  const bool dragging = _dragParam > 0 || _dragView;
#else
  const bool dragging =
      _dragParam != CLAP_INVALID_ID || _dragView || _dragArea != 0;
#endif
  if (!dragging) {
    finishGestures();
    pointerEditing = false;
  }
  markStateDirty();
  return true;
}
void pointerMove(VSTGUI::MouseMoveEvent &e) override {
  drawing::Event event{{e.mousePosition.x, e.mousePosition.y}, 1};
  if (e.buttonState.isLeft()) {
    if (events().available() > gestures.size() + 16u)
      mouseDragged(&event);
    else
      beep();
  } else
    mouseMoved(&event);
}
void pointerUp(VSTGUI::MouseUpEvent &e) override {
  drawing::Event event{{e.mousePosition.x, e.mousePosition.y}, 1};
  mouseUp(&event);
  finishGestures();
  pointerEditing = false;
  markStateDirty();
}
#if S3G_SCORE_KIND == 1
// Keep the original selected-step shortcuts, with portable key/modifier input.
bool wheelShift = false;
void wheel(VSTGUI::MouseWheelEvent &e) override {
  if (e.deltaY == 0. || events().available() < 8u)
    return;
  wheelShift = e.modifiers.has(VSTGUI::ModifierKey::Shift);
  drawing::Event event{{e.mousePosition.x, e.mousePosition.y}, 1, e.deltaY};
  scrollWheel(&event);
  e.consumed = true;
}
void onKeyboardEvent(VSTGUI::KeyboardEvent &e) override {
  using VSTGUI::VirtualKey;
  if (e.type != VSTGUI::EventType::KeyDown || events().available() < 8u)
    return;
  if (e.virt == VirtualKey::Left || e.virt == VirtualKey::Right) {
    _selectedStep =
        (_selectedStep + (e.virt == VirtualKey::Left ? 15 : 1)) % 16;
  } else if (e.virt == VirtualKey::Up || e.virt == VirtualKey::Down) {
    wheelShift = e.modifiers.has(VSTGUI::ModifierKey::Shift);
    drawing::Event event{{}, 1, e.virt == VirtualKey::Up ? 1. : -1.};
    scrollWheel(&event);
  } else {
    auto c = e.character;
    if (c >= 'A' && c <= 'Z')
      c += 'a' - 'A';
    StepParamKind kind;
    if (c == 'g' || c == ' ' || e.virt == VirtualKey::Space)
      kind = StepParamKind::Gate;
    else if (c == 'a')
      kind = StepParamKind::Accent;
    else if (c == 's')
      kind = StepParamKind::Slide;
    else
      return;
    const clap_id id =
        kStepParamBase + _selectedStep * kStepParamStride + uint32_t(kind);
    singleEdit(id, value(id) >= .5 ? 0. : 1.);
    markCustomPattern();
  }
  invalid();
  e.consumed = true;
}
#endif
#if S3G_SCORE_KIND == 3
std::shared_ptr<const s3g::AmbiVotTableBank> displayBank() {
  if (_snapshot.preset == s3g::AmbiVotPreset::User) {
    auto user = std::atomic_load_explicit(&_plugin->userBank,
                                          std::memory_order_acquire);
    if (user)
      return user;
  }
  return _plugin
      ->presetBanks[std::min<uint32_t>(uint32_t(_snapshot.preset), 3u)];
}
void editScore(s3g::AmbiVotVectorScore score) {
  storeScore(*_plugin, score);
  markStateDirty();
  request();
}
bool loadWavePath(const std::string &path) {
  if (events().available() < 8u)
    return false;
  const auto wave = readWavMono(path);
  if (wave.size() < 32u)
    return false;
  std::shared_ptr<const s3g::AmbiVotTableBank> bank =
      std::make_shared<s3g::AmbiVotTableBank>(s3g::ambiVotBankFromWave(wave));
  std::atomic_store_explicit(&_plugin->userBank, std::move(bank),
                             std::memory_order_release);
  singleEdit(kPresetParamId, double(s3g::AmbiVotPreset::User));
  markStateDirty();
  invalid();
  return true;
}
void loadWave() {
  finishGestures();
  foundation::FileDialogOptions options;
  options.title = "Load VOT wavetable atlas";
  options.extension = "wav";
  options.extensions = {"wave"};
  options.extensionDescription = "WAV audio";
  const auto path = foundation::runFileDialog(getFrame(), options);
  if (path.empty())
    return;
  error = loadWavePath(path) ? "" : "WAV FILE COULD NOT BE READ";
  invalid();
}
#endif
bool presetFile(const std::string &path, bool save) {
  const clap_plugin_state_t stateExt{stateSave, stateLoad};
#if S3G_SCORE_KIND == 2
  refreshSnapshot();
  PresetFile preset{};
  if (save) {
    preset.params = _snapshot;
    std::snprintf(preset.name, sizeof(preset.name), "%s", presetName().c_str());
    return savePresetFile(path.c_str(), preset);
  }
  if (events().available() < paramsCount(&_plugin->plugin) * 3u + 32u ||
      !loadPresetFile(path.c_str(), preset))
    return false;
  // Cocoa's custom Horizon presets preserve OUT and ORDER, unlike projects.
  preset.params.outputGainDb = _snapshot.outputGainDb;
  preset.params.order = _snapshot.order;
  queueParams(*_plugin, preset.params, _presetSnapshot);
  _snapshot = preset.params;
#else
  if (save)
    return s3g::clap_gui::portable::saveStateFile(&_plugin->plugin, stateExt,
                                                  path.c_str());
  if (events().available() < paramsCount(&_plugin->plugin) * 3u + 32u)
    return false;
  auto staged = std::make_unique<Plugin>();
  staged->plugin.plugin_data = staged.get();
  if (!s3g::clap_gui::portable::loadStateFile(&staged->plugin, stateExt,
                                              path.c_str()))
    return false;
  for (uint32_t n = 0; n < paramsCount(&_plugin->plugin); ++n) {
    clap_param_info_t info{};
    double v = 0.;
    if (paramsGetInfo(&_plugin->plugin, n, &info) &&
        info.id != kOutputParamId &&
        paramsGetValue(&staged->plugin, info.id, &v))
      singleEdit(info.id, v);
  }
#if S3G_SCORE_KIND == 3
  editScore(loadScore(*staged));
  std::atomic_store_explicit(
      &_plugin->userBank,
      std::atomic_load_explicit(&staged->userBank, std::memory_order_acquire),
      std::memory_order_release);
  _leftPage = _plugin->guiPage = staged->guiPage;
  _viewMode = _plugin->guiViewMode = staged->guiViewMode;
  _viewAzDeg = _plugin->guiViewAzDeg = staged->guiViewAzDeg;
  _viewElDeg = _plugin->guiViewElDeg = staged->guiViewElDeg;
  _viewZoom = _plugin->guiViewZoom = staged->guiViewZoom;
#endif
#endif
  rescanValues();
  invalid();
  return true;
}
bool presetDialog(bool save, drawing::String *name = nullptr) {
  finishGestures();
  foundation::FileDialogOptions options;
  options.save = save;
  options.title = std::string(save ? "Save " : "Load ") +
                  portablePresetDirectory + " preset";
#if S3G_SCORE_KIND == 2
  options.extension = "s3ghorizon";
  options.initialDirectory =
      foundation::presetDirectory("Ambi Horizon Encoder");
#else
  options.extension = "s3gpreset";
  options.initialDirectory =
      foundation::presetDirectory(portablePresetDirectory);
#endif
  options.extensionDescription = "s3g Preset";
  options.defaultSaveName = "Preset." + options.extension;
  const auto path = foundation::runFileDialog(getFrame(), options);
  if (path.empty()) {
    cancelled = true;
    return false;
  }
  const bool result = presetFile(path, save);
  error = result ? "" : "PRESET FILE COULD NOT BE READ / WRITTEN";
  if (result && name)
    *name =
        foundation::pathToUtf8(foundation::pathFromUtf8(path.c_str()).stem());
  invalid();
  return result;
}
#if S3G_SCORE_KIND == 2
void saveCustomPreset() { presetDialog(true); }
void loadCustomPreset() { presetDialog(false); }
#endif
