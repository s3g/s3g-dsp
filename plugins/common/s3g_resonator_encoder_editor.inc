// Platform services only; the original editor below owns layout and hit maps.
std::string error;
std::vector<clap_id> gestures;
bool pointerEditing = false;
double lastAnimationTime = 0.;
drawing::Rect bounds() const {
  return drawing::makeRect(0, 0, kGuiWidth, kGuiHeight);
}
drawing::Point convertPoint_fromView(drawing::Point point, void *) {
  return point;
}
void setNeedsDisplay(bool) { invalid(); }
void beep() {
  if (error.empty())
    error = "ACTION UNAVAILABLE / HOST QUEUE BUSY";
  invalid();
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
  if (_plugin->guiParamEvents.available() <= gestures.size() + 3u) {
    beep();
    return false;
  }
  if (!_plugin->guiParamEvents.push({Kind::GestureBegin, id, 0.}))
    return false;
  gestures.push_back(id);
  request();
  return true;
}
void endEdit(clap_id id) {
  const auto found = std::find(gestures.begin(), gestures.end(), id);
  if (found == gestures.end())
    return;
  if (_plugin->guiParamEvents.push({Kind::GestureEnd, id, 0.})) {
    gestures.erase(found);
    request();
  }
}
void finishGestures() {
  for (size_t n = gestures.size(); n > 0; --n)
    endEdit(gestures[n - 1]);
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
  if (!valid || _plugin->guiParamEvents.available() <= gestures.size() + 3u) {
    beep();
    return;
  }
  const bool single =
      !pointerEditing &&
      std::find(gestures.begin(), gestures.end(), id) == gestures.end();
  if (!beginEdit(id))
    return;
#if S3G_RESONATOR_KIND == 1
  applyControlParam(*_plugin, id, requested);
#else
  // Optimistic display publication only. The existing process/flush queue
  // applies the value to the audio-owned engine and retains backpressure.
  publishParam(*_plugin, id, requested);
#endif
  _plugin->guiParamEvents.push({Kind::Value, id, requested});
  request();
  if (single)
    endEdit(id);
#if S3G_RESONATOR_KIND == 1
  if (id == kParamPreset)
    rescanValues();
#endif
  error.clear();
  markStateDirty();
}
void singleEdit(clap_id id, double requested) {
  if (beginEdit(id)) {
    editValue(id, requested);
    endEdit(id);
  }
}
double refreshPeriodMilliseconds() const override {
#if S3G_RESONATOR_KIND == 1
  return 1000. / 24.;
#elif S3G_RESONATOR_KIND == 2
  return 50.;
#else
  return 1000. / 30.;
#endif
}
void startRefresh() override {
#if S3G_RESONATOR_KIND != 3
  _plugin->guiVisible.store(true, std::memory_order_release);
#endif
  lastAnimationTime = drawing::uptime();
  Canvas::startRefresh();
}
void stopRefresh() override {
  Canvas::stopRefresh();
  drawing::Event event{};
  mouseUp(&event);
  finishGestures();
  pointerEditing = false;
  _openMenu = CLAP_INVALID_ID;
#if S3G_RESONATOR_KIND != 3
  storeViewState();
  _plugin->guiVisible.store(false, std::memory_order_release);
#endif
}
void service() override {
#if S3G_RESONATOR_KIND == 1
  (void)controlStateSnapshot(*_plugin);
  const double now = drawing::uptime();
  const double elapsed = lastAnimationTime > 0.
                             ? std::clamp(now - lastAnimationTime, 0., 0.1)
                             : 0.;
  lastAnimationTime = now;
  const double activity = std::clamp<double>(
      _plugin->actuatorActivity.load(std::memory_order_relaxed), 0., 1.);
  if (activity > .002)
    _actuatorFlowPhase = std::fmod(
        _actuatorFlowPhase + elapsed * 24. * (.010 + activity * .022), 1.);
#endif
}
void paint() override {
  service();
  drawing::DrawingScope scope(*this, context, font, titleFont, &error);
  drawOriginal(bounds());
}
bool pointerDown(VSTGUI::MouseDownEvent &event) override {
  if (!event.buttonState.isLeft())
    return false;
  finishGestures();
  // Reserve the largest factory/random/reset batch, plus matching ends.
  if (_plugin->guiParamEvents.available() <
      paramsCount(&_plugin->plugin) * 3u + 24u) {
    beep();
    return true;
  }
  pointerEditing = true;
  drawing::Event e{{event.mousePosition.x, event.mousePosition.y},
                   static_cast<int>(event.clickCount)};
  mouseDown(&e);
#if S3G_RESONATOR_KIND == 1
  const bool dragging =
      _dragParam != CLAP_INVALID_ID || _dragBody >= 0 || _dragSkin;
#elif S3G_RESONATOR_KIND == 2
  const bool dragging = _dragParam != CLAP_INVALID_ID;
#else
  const bool dragging = _dragParam > 0;
#endif
  if (!dragging) {
    finishGestures();
    pointerEditing = false;
  }
  return true;
}
void pointerMove(VSTGUI::MouseMoveEvent &event) override {
  drawing::Event e{{event.mousePosition.x, event.mousePosition.y}, 1};
  if (event.buttonState.isLeft()) {
    // Compound skin/body gestures cannot run out halfway through an update.
    if (_plugin->guiParamEvents.available() > gestures.size() + 12u)
      mouseDragged(&e);
    else
      beep();
  }
#if S3G_RESONATOR_KIND != 1
  else
    mouseMoved(&e);
#endif
}
void pointerUp(VSTGUI::MouseUpEvent &event) override {
  drawing::Event e{{event.mousePosition.x, event.mousePosition.y}, 1};
  mouseUp(&e);
  finishGestures();
  pointerEditing = false;
}
void setPresetName(const std::string &name, bool custom) {
#if S3G_RESONATOR_KIND == 1
  std::snprintf(_plugin->presetName, sizeof(_plugin->presetName), "%s",
                name.c_str());
#elif S3G_RESONATOR_KIND == 2
  if (custom)
    _factoryPresetIndex = -1;
  std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", name.c_str());
#else
  if (custom)
    _factoryPresetIndex = -1;
  std::snprintf(_presetName, sizeof(_presetName), "%s", name.c_str());
#endif
}
bool presetFile(const std::string &path, bool save) {
  if (save) {
    if (!s3g::clap_gui::portable::saveStateFile(&_plugin->plugin, stateExt,
                                                path.c_str()))
      return false;
  } else {
    if (_plugin->guiParamEvents.available() <
        paramsCount(&_plugin->plugin) * 3u + 24u)
      return false;
    // Decode into an isolated, inactive instance. Failed/truncated files cannot
    // partially overwrite the running voice, and file I/O never touches DSP.
    auto staged = std::make_unique<Plugin>();
    staged->plugin.plugin_data = staged.get();
    if (!s3g::clap_gui::portable::loadStateFile(&staged->plugin, stateExt,
                                                path.c_str()))
      return false;
#if S3G_RESONATOR_KIND == 1
    auto loaded = controlStateSnapshot(*staged);
    loaded.params.outputGainDb = static_cast<float>(value(kParamOutputGain));
    // Cocoa's OUT-preserving user load marks the voice Custom as well.
    replaceControlState(*_plugin, loaded.params, kCustomPresetIndex, true);
    _plugin->guiState = staged->guiState;
#else
    // Retain original user-preset policy: restore everything except OUT.
    for (uint32_t n = 0; n < paramsCount(&_plugin->plugin); ++n) {
      clap_param_info_t info{};
      if (!paramsGetInfo(&_plugin->plugin, n, &info))
        continue;
#if S3G_RESONATOR_KIND == 2
      if (info.id == kOutputGainParamId)
        continue;
#else
      if (info.id == kOutputParamId || info.id == kTriggerParamId)
        continue;
#endif
      double loaded = 0.;
      if (paramsGetValue(&staged->plugin, info.id, &loaded))
        singleEdit(info.id, loaded);
    }
    // Preserve the original state loader's sequencer/MIDI or trigger-gate
    // reset, in queue order on the audio thread rather than from the file
    // dialog.
    _plugin->guiParamEvents.push({Kind::Value, CLAP_INVALID_ID, 0.});
    request();
#endif
    rescanValues();
  }
  setPresetName(
      foundation::pathToUtf8(foundation::pathFromUtf8(path.c_str()).stem()),
      !save);
  markStateDirty();
  return true;
}
bool presetDialog(bool save, drawing::String *name = nullptr) {
  finishGestures();
  foundation::FileDialogOptions options;
  options.save = save;
  options.title = std::string(save ? "Save " : "Load ") +
                  portablePresetDirectory + " preset";
  options.extension = "s3gpreset";
  options.extensionDescription = "s3g Preset";
  options.defaultSaveName = "Preset.s3gpreset";
  options.initialDirectory =
      foundation::presetDirectory(portablePresetDirectory);
  const auto path = foundation::runFileDialog(getFrame(), options);
  if (path.empty())
    return true; // Cancellation is not an error.
  const bool result = presetFile(path, save);
  error = result ? "" : "PRESET FILE COULD NOT BE READ / WRITTEN";
  if (result && name)
    *name =
        foundation::pathToUtf8(foundation::pathFromUtf8(path.c_str()).stem());
  invalid();
  return result;
}
