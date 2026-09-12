// Shared platform boundary; canvas methods below retain the Cocoa hit maps.
std::string error;
bool cancelled = false;
std::vector<clap_id> gestures;
drawing::Rect bounds() const {
  return drawing::makeRect(0, 0, kGuiWidth, kGuiHeight);
}
drawing::Point convertPoint_fromView(drawing::Point p, void *) { return p; }
bool isHidden() const { return false; }
void setNeedsDisplay(bool) { invalid(); }
void focus() {
  if (getFrame())
    getFrame()->setFocusView(this);
}
bool wantsFocus() const override { return true; }
void beep() {
  if (!cancelled && error.empty())
    error = "ACTION UNAVAILABLE / HOST QUEUE BUSY";
  cancelled = false;
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
bool beginEdit(clap_id id) {
  if (std::find(gestures.begin(), gestures.end(), id) != gestures.end())
    return true;
  if (_plugin->guiParamEvents.available() <= gestures.size() + 8u) {
    beep();
    return false;
  }
  if (!_plugin->guiParamEvents.push(
          {s3g::clap_gui::ParamEventKind::GestureBegin, id, 0.}))
    return false;
  gestures.push_back(id);
  request();
  return true;
}
bool endEdit(clap_id id) {
  auto it = std::find(gestures.begin(), gestures.end(), id);
  if (it == gestures.end())
    return true;
  if (!_plugin->guiParamEvents.push(
          {s3g::clap_gui::ParamEventKind::GestureEnd, id, 0.}))
    return false;
  gestures.erase(it);
  request();
  return true;
}
void finishGestures() {
  for (size_t n = gestures.size(); n > 0; --n)
    endEdit(gestures[n - 1]);
}
bool editValue(clap_id id, double v, bool continuous = false) {
  if (!std::isfinite(v))
    return false;
  bool valid = false;
  for (uint32_t n = 0; n < paramsCount(&_plugin->plugin); ++n) {
    clap_param_info_t info{};
    if (paramsGetInfo(&_plugin->plugin, n, &info) && info.id == id &&
        !(info.flags & CLAP_PARAM_IS_READONLY)) {
      v = std::clamp(v, info.min_value, info.max_value);
      if (info.flags & CLAP_PARAM_IS_STEPPED)
        v = std::round(v);
      valid = true;
      break;
    }
  }
  if (!valid || _plugin->guiParamEvents.available() <= gestures.size() + 8u) {
    beep();
    return false;
  }
  bool single = !continuous && std::find(gestures.begin(), gestures.end(),
                                         id) == gestures.end();
  if (!beginEdit(id))
    return false;
  _plugin->publishedParams[id - 1].store(v, std::memory_order_release);
  _plugin->guiParamEvents.push({s3g::clap_gui::ParamEventKind::Value, id, v});
  if (single)
    endEdit(id);
  error.clear();
  request();
  markStateDirty();
  return true;
}
s3g::AmbiImprintParams viewParams;
explicit Editor(Plugin &p)
    : Canvas(kGuiWidth, kGuiHeight), _plugin(&p), _dragParam(CLAP_INVALID_ID),
      _orderMenuOpen(false), _orderMenuHover(-1), _atlasMenuOpen(false),
      _atlasMenuHover(-1), _viewMode(p.guiViewMode),
      _viewAzimuthDeg(p.guiViewAzimuthDeg),
      _viewElevationDeg(p.guiViewElevationDeg), _viewZoom(p.guiViewZoom),
      _dragView(false) {
  std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "INIT");
  service();
}
void service() override {
  viewParams = visibleImprintParams(*_plugin);
  if (!_dragView) {
    _viewMode = _plugin->guiViewMode;
    _viewAzimuthDeg = _plugin->guiViewAzimuthDeg;
    _viewElevationDeg = _plugin->guiViewElevationDeg;
    _viewZoom = _plugin->guiViewZoom;
  }
  invalid();
}
void setParam_value(clap_id id, double value) {
  editValue(id, value, _dragParam == id);
  viewParams = visibleImprintParams(*_plugin);
}
void wheel(VSTGUI::MouseWheelEvent &e) override {
  drawing::Event event{
      {e.mousePosition.x, e.mousePosition.y}, 1, -e.deltaY * 10.};
  scrollWheel(&event);
  markStateDirty();
  e.consumed = true;
}
bool importImprint(const std::string &path, const std::string &name) {
  std::string data, json, reason;
  GuiSpaceGeometry geometry;
  s3g::AmbiImprintDescriptor descriptor;
  if (!s3g::portable_gui::encoder_files::readText(path, data))
    reason = "IMPRINT FILE COULD NOT BE READ";
  else if (parseImprintData(data, descriptor, geometry, json, reason)) {
    setStatus("BUILDING KERNELS");
    if (installDescriptor(*_plugin, std::move(descriptor), std::move(geometry),
                          std::move(json), name, reason)) {
      error.clear();
      markStateDirty();
      invalid();
      return true;
    }
  }
  setStatus(reason);
  error = reason;
  invalid();
  return false;
}
void loadImprint() {
  finishGestures();
  foundation::FileDialogOptions options;
  options.title = "Load Imprint";
  options.extension = "s3gimprint";
  options.extensionDescription = "s3g Imprint";
  const auto path = foundation::runFileDialog(getFrame(), options);
  if (!path.empty())
    importImprint(path, foundation::pathToUtf8(
                            foundation::pathFromUtf8(path.c_str()).filename()));
}
void loadAtlasAtIndex(size_t index) {
  if (index >= kImprintAtlas.size())
    return;
  const auto &entry = kImprintAtlas[index];
  const auto path = foundation::resourceDirectory() / "Imprint Atlas" /
                    (std::string(entry.resource) + ".s3gimprint");
  importImprint(foundation::pathToUtf8(path), entry.title);
}
bool presetFile(const std::string &path, bool save) {
  if (save)
    return s3g::clap_gui::portable::saveStateFile(&_plugin->plugin, stateExt,
                                                  path.c_str());
  auto staged = std::make_unique<Plugin>();
  staged->plugin.plugin_data = staged.get();
  if (!s3g::clap_gui::portable::loadStateFile(&staged->plugin, stateExt,
                                              path.c_str()))
    return false;
  if (_plugin->guiParamEvents.available() < 32u)
    return false;
  if (staged->hasImprint.load() &&
      !installDescriptor(*_plugin, std::move(staged->descriptor),
                         std::move(staged->guiGeometry),
                         std::move(staged->imprintJson), "PRESET STATE", error))
    return false;
  for (clap_id id = kParamOrder; id <= kParamFieldListen; ++id)
    if (id != kParamOutput)
      editValue(id, getParam(*staged, id));
  _plugin->guiViewMode = staged->guiViewMode;
  _plugin->guiViewAzimuthDeg = staged->guiViewAzimuthDeg;
  _plugin->guiViewElevationDeg = staged->guiViewElevationDeg;
  _plugin->guiViewZoom = staged->guiViewZoom;
  service();
  markStateDirty();
  return true;
}
bool handleTitleClick(drawing::Point point,
                      const s3g::gui_layout::EncoderTitleBand &band) {
  if (drawing::contains(point, drawing::cocoaRect(band.presetMenu))) {
    for (uint32_t n = 0; n < paramsCount(&_plugin->plugin); ++n) {
      clap_param_info_t info{};
      if (paramsGetInfo(&_plugin->plugin, n, &info) && info.id != kParamOutput)
        editValue(info.id, info.default_value);
    }
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "INIT");
    service();
    return true;
  }
  const bool save =
      drawing::contains(point, drawing::cocoaRect(band.saveButton));
  if (!save && !drawing::contains(point, drawing::cocoaRect(band.loadButton)))
    return false;
  finishGestures();
  foundation::FileDialogOptions options;
  options.save = save;
  options.title =
      save ? "Save Ambi Imprint preset" : "Load Ambi Imprint preset";
  options.extension = "s3gpreset";
  options.extensionDescription = "s3g Preset";
  options.initialDirectory = foundation::presetDirectory("Ambi Imprint");
  options.defaultSaveName = "Preset.s3gpreset";
  const auto path = foundation::runFileDialog(getFrame(), options);
  if (path.empty())
    return true;
  if (presetFile(path, save)) {
    const auto name =
        foundation::pathToUtf8(foundation::pathFromUtf8(path.c_str()).stem());
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                  name.c_str());
    error.clear();
  } else
    error = "PRESET FILE COULD NOT BE READ / WRITTEN";
  return true;
}
double refreshPeriodMilliseconds() const override { return 1000. / 24.; }
void startRefresh() override {
  service();
  Canvas::startRefresh();
}
void stopRefresh() override {
  Canvas::stopRefresh();
  drawing::Event event{};
  mouseUp(&event);
  finishGestures();
}
~Editor() override { stopRefresh(); }
void paint() override {
  drawing::DrawingScope scope(*this, context, font, titleFont, &error);
  drawOriginal(bounds());
}
bool pointerDown(VSTGUI::MouseDownEvent &e) override {
  if (!e.buttonState.isLeft())
    return false;
  finishGestures();
  if (_plugin->guiParamEvents.available() <
      paramsCount(&_plugin->plugin) * 3u + 32u) {
    beep();
    return true;
  }
  focus();
  service();
  cancelled = false;
  drawing::Event event{{e.mousePosition.x, e.mousePosition.y},
                       int(e.clickCount)};
  mouseDown(&event);
  markStateDirty();
  return true;
}
void pointerMove(VSTGUI::MouseMoveEvent &e) override {
  drawing::Event event{{e.mousePosition.x, e.mousePosition.y}, 1};
  if (e.buttonState.isLeft()) {
    if (_plugin->guiParamEvents.available() > gestures.size() + 8u)
      mouseDragged(&event);
  } else
    mouseMoved(&event);
}
void pointerUp(VSTGUI::MouseUpEvent &e) override {
  drawing::Event event{{e.mousePosition.x, e.mousePosition.y}, 1};
  mouseUp(&event);
  finishGestures();
  markStateDirty();
}
