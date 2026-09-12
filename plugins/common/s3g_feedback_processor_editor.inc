// Platform services; the per-plugin canvas retains the Cocoa layout/hit maps.
std::string error;
bool cancelled = false;
bool pointerEditing = false;
uint32_t pointerModifiers = 0;
std::vector<clap_id> gestures;
drawing::Rect bounds() const { return drawing::makeRect(0, 0, kGuiWidth, kGuiHeight); }
drawing::Point convertPoint_fromView(drawing::Point p, void*) { return p; }
bool isHidden() const { return false; }
void setNeedsDisplay(bool) { invalid(); }
void focus() { if (getFrame()) getFrame()->setFocusView(this); }
bool wantsFocus() const override { return true; }
void beep() { if (!cancelled && error.empty()) error = "ACTION UNAVAILABLE / HOST QUEUE BUSY"; cancelled = false; invalid(); }
const clap_host_params_t* hostParams() const {
  return _plugin->host && _plugin->host->get_extension
    ? static_cast<const clap_host_params_t*>(_plugin->host->get_extension(_plugin->host, CLAP_EXT_PARAMS)) : nullptr;
}
void request() { s3g::clap_gui::requestParamEventService(_plugin->host, hostParams()); }
void markStateDirty() {
  if (!_plugin->host || !_plugin->host->get_extension) return;
  const auto* state = static_cast<const clap_host_state_t*>(_plugin->host->get_extension(_plugin->host, CLAP_EXT_STATE));
  if (state && state->mark_dirty) state->mark_dirty(_plugin->host);
}
double value(clap_id id) const { double v = 0.; paramsGetValue(&_plugin->plugin, id, &v); return v; }
bool beginEdit(clap_id id) {
  if (std::find(gestures.begin(), gestures.end(), id) != gestures.end()) return true;
  if (_plugin->guiParamEvents.available() <= gestures.size() + 8u) { beep(); return false; }
  if (!_plugin->guiParamEvents.push({s3g::clap_gui::ParamEventKind::GestureBegin, id, 0.})) return false;
  gestures.push_back(id); request(); return true;
}
bool endEdit(clap_id id) {
  auto found = std::find(gestures.begin(), gestures.end(), id);
  if (found == gestures.end()) return true;
  if (!_plugin->guiParamEvents.push({s3g::clap_gui::ParamEventKind::GestureEnd, id, 0.})) return false;
  gestures.erase(found); request(); return true;
}
void finishGestures() { for (size_t n = gestures.size(); n > 0; --n) endEdit(gestures[n - 1]); }
bool editValue(clap_id id, double requested, bool continuous = false) {
  if (!std::isfinite(requested)) return false;
  bool valid = false;
  for (uint32_t i = 0; i < paramsCount(&_plugin->plugin); ++i) {
    clap_param_info_t info{};
    if (paramsGetInfo(&_plugin->plugin, i, &info) && info.id == id) {
      requested = std::clamp(requested, info.min_value, info.max_value);
      if (info.flags & CLAP_PARAM_IS_STEPPED) requested = std::round(requested);
      valid = true; break;
    }
  }
  if (!valid || _plugin->guiParamEvents.available() <= gestures.size() + 8u) { beep(); return false; }
  const bool single = !continuous && std::find(gestures.begin(), gestures.end(), id) == gestures.end();
  if (!beginEdit(id)) return false;
  publishParam(*_plugin, id, requested);
  _plugin->guiParamEvents.push({s3g::clap_gui::ParamEventKind::Value, id, requested});
  request(); if (single) endEdit(id); error.clear(); markStateDirty(); return true;
}
bool queueGuiParamGestureBegin(Plugin&, clap_id id) { return beginEdit(id); }
bool queueGuiParamGestureEnd(Plugin&, clap_id id) { return endEdit(id); }
bool queueGuiParamValue(Plugin&, clap_id id, double v) { return editValue(id, v); }
bool queueGuiParamGesture(Plugin&, clap_id id, double v) { return editValue(id, v); }
#if S3G_FEEDBACK_KIND == 2
bool queueGuiPatch(Plugin& p, const s3g::FeedbackShiftParams& patch) {
  using Kind = s3g::clap_gui::ParamEventKind;
  std::array<s3g::clap_gui::ParamEvent, kParamCount * 3u> events{};
  for (uint32_t n = 0; n < kParamCount; ++n) {
    const auto id = paramIdAtIndex(n);
    events[n * 3u] = {Kind::GestureBegin, id, 0.};
    events[n * 3u + 1u] = {Kind::Value, id, rawParamValue(patch, id)};
    events[n * 3u + 2u] = {Kind::GestureEnd, id, 0.};
  }
  if (p.guiParamEvents.available() < events.size() + gestures.size() + 8u
      || !p.guiParamEvents.pushBatch(events.data(), events.size())) { beep(); return false; }
  for (uint32_t n = 0; n < kParamCount; ++n) publishParam(p, events[n * 3u + 1u].paramId, events[n * 3u + 1u].value);
  request(); markStateDirty(); return true;
}
#endif
double refreshPeriodMilliseconds() const override {
#if S3G_FEEDBACK_KIND == 2 || S3G_FEEDBACK_KIND == 4
  return 1000. / 30.;
#else
  return 1000. / 24.;
#endif
}
void startRefresh() override { service(); Canvas::startRefresh(); }
void stopRefresh() override {
  Canvas::stopRefresh(); drawing::Event e{}; mouseUp(&e); finishGestures(); pointerEditing = false;
#if S3G_FEEDBACK_KIND == 1
  _midiReceiveMenuOpen = false;
#elif S3G_FEEDBACK_KIND == 3
  _presetMenuOpen = false;
#else
  _openMenu = 0;
#endif
}
~Editor() override { stopRefresh(); }
void paint() override {
  drawing::DrawingScope scope(*this, context, font, titleFont, &error);
  drawOriginal(bounds());
}
bool pointerDown(VSTGUI::MouseDownEvent& e) override {
  if (!e.buttonState.isLeft()) return false;
  finishGestures();
  if (_plugin->guiParamEvents.available() < paramsCount(&_plugin->plugin) * 3u + 32u) { beep(); return true; }
  focus(); service(); cancelled = false; pointerEditing = true;
  pointerModifiers = (e.modifiers.has(VSTGUI::ModifierKey::Shift) ? 1u : 0u)
                   | (e.modifiers.has(VSTGUI::ModifierKey::Alt) ? 2u : 0u);
  drawing::Event event{{e.mousePosition.x, e.mousePosition.y}, int(e.clickCount)};
  mouseDown(&event); markStateDirty(); return true;
}
void pointerMove(VSTGUI::MouseMoveEvent& e) override {
  drawing::Event event{{e.mousePosition.x, e.mousePosition.y}, 1};
#if S3G_FEEDBACK_KIND == 4
  if (getFrame()) getFrame()->setCursor(drawing::contains(event.point,
      drawing::makeRect(kEnvelopeX, kEnvelopeY, kEnvelopeWidth, kEnvelopeHeight))
          ? VSTGUI::kCursorCrosshair : VSTGUI::kCursorDefault);
#endif
  if (e.buttonState.isLeft()) {
    if (_plugin->guiParamEvents.available() > gestures.size() + 32u) mouseDragged(&event);
  } else mouseMoved(&event);
}
#if S3G_FEEDBACK_KIND == 4
void onMouseExitEvent(VSTGUI::MouseExitEvent& e) override {
  if (getFrame()) getFrame()->setCursor(VSTGUI::kCursorDefault);
  Canvas::onMouseExitEvent(e);
}
#endif
void pointerUp(VSTGUI::MouseUpEvent& e) override {
  drawing::Event event{{e.mousePosition.x, e.mousePosition.y}, 1};
  mouseUp(&event); finishGestures(); pointerEditing = false; markStateDirty();
}
bool presetFile(const std::string& path, bool save, clap_id preserve = CLAP_INVALID_ID) {
  if (save) return s3g::clap_gui::portable::saveStateFile(&_plugin->plugin, presetState, path.c_str());
  if (_plugin->guiParamEvents.available() < paramsCount(&_plugin->plugin) * 3u + 32u) return false;
  auto staged = std::make_unique<Plugin>(); staged->plugin.plugin_data = staged.get();
  if (!s3g::clap_gui::portable::loadStateFile(&staged->plugin, presetState, path.c_str())) return false;
#if S3G_FEEDBACK_KIND == 3
  return queuePortablePreset(*_plugin, *staged, preserve);
#elif S3G_FEEDBACK_KIND == 4
  return queuePortableFaultPreset(*_plugin, std::move(staged), preserve);
#else
  for (uint32_t i = 0; i < paramsCount(&_plugin->plugin); ++i) {
    clap_param_info_t info{}; double v = 0.;
    if (!paramsGetInfo(&_plugin->plugin, i, &info) || info.id == preserve) continue;
#if S3G_FEEDBACK_KIND == 1
    if (info.id == kTriggerParamId) continue;
#elif S3G_FEEDBACK_KIND == 3
    bool persistent = false;
    for (uint32_t n = 0; n < kPersistentParamCount; ++n)
      if (persistentParamIdAt(n) == info.id) { persistent = true; break; }
    if (!persistent) continue;
#endif
    if (paramsGetValue(&staged->plugin, info.id, &v)) editValue(info.id, v);
  }
  const auto* params = hostParams(); if (params && params->rescan) params->rescan(_plugin->host, CLAP_PARAM_RESCAN_VALUES);
  markStateDirty(); service(); invalid(); return true;
#endif
}
bool handleProcessorTitleClick(drawing::Point point, const clap_plugin_t*, const char*,
    const s3g::gui_layout::EncoderTitleBand& band, char* name, size_t size, clap_id preserve = CLAP_INVALID_ID) {
  const bool save = drawing::contains(point, drawing::cocoaRect(band.saveButton));
  if (!save && !drawing::contains(point, drawing::cocoaRect(band.loadButton))) return false;
  finishGestures(); foundation::FileDialogOptions options;
  options.save = save; options.title = std::string(save ? "Save " : "Load ") + portablePresetDirectory + " preset";
  options.extension = "s3gpreset"; options.extensionDescription = "s3g Preset";
  options.initialDirectory = foundation::presetDirectory(portablePresetDirectory);
  options.defaultSaveName = "Preset.s3gpreset";
  const auto path = foundation::runFileDialog(getFrame(), options);
  if (path.empty()) { cancelled = true; return true; }
  const bool result = presetFile(path, save, preserve);
  error = result ? "" : "PRESET FILE COULD NOT BE READ / WRITTEN";
  if (result) std::snprintf(name, size, "%s", foundation::pathToUtf8(foundation::pathFromUtf8(path.c_str()).stem()).c_str());
  invalid(); return true;
}
#if S3G_FEEDBACK_KIND == 4
#include "s3g_feedback_processor_fault_editor.inc"
#endif
