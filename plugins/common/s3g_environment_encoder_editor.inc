// Shared lifecycle, dialogs and gestures for the five literal field editors.
// Included inside environment_encoder_canvas::Editor.
drawing::Rect bounds() const {
  return drawing::makeRect(0, 0, kGuiWidth, kGuiHeight);
}
drawing::Point convertPoint_fromView(drawing::Point p, void *) { return p; }
void setNeedsDisplay(bool) { invalid(); }
void beep() {
  error = "SURF: CHECK CELL SELECTION / COUNT";
  invalid();
}
void markStateDirty() {
  if (_plugin->host && _plugin->host->get_extension) {
    auto *state = static_cast<const clap_host_state_t *>(
        _plugin->host->get_extension(_plugin->host, CLAP_EXT_STATE));
    if (state && state->mark_dirty)
      state->mark_dirty(_plugin->host);
  }
}
void finishGestures() {
  for (auto id : gestures)
    queueGuiParamEvent(*_plugin, Kind::GestureEnd, id);
  gestures.clear();
}
bool parameterSpace() const {
#if S3G_ENVIRONMENT_KIND == 1
  return _plugin->guiParamEvents.available() >= 16;
#else
  return _plugin->guiParamEvents.canPush(16);
#endif
}
void editValue(clap_id id, double value) {
  if (!parameterSpace()) {
    error = "HOST PARAMETER QUEUE BUSY";
    return;
  }
  if (id >= CLAP_INVALID_ID - 16) {
    queueGuiParamValue(*_plugin, id, value);
  } else if (pointerEditing) {
    if (std::find(gestures.begin(), gestures.end(), id) == gestures.end()) {
      if (!queueGuiParamEvent(*_plugin, Kind::GestureBegin, id))
        return;
      gestures.push_back(id);
    }
    queueGuiParamEvent(*_plugin, Kind::Value, id, value);
  } else {
    queueGuiParamValue(*_plugin, id, value);
  }
  error.clear();
  markStateDirty();
}
void submitSurface() {
#if S3G_ENVIRONMENT_KIND == 1
  const bool room =
      parameterSpace() && _plugin->guiSurfaceCommands.available() > 0;
#else
  const bool room = _plugin->guiSurfaceCommands.canPush(1);
#endif
  if (!room) {
    error = "SURF QUEUE BUSY";
    _surfaceSnapshot = controlSnapshot(*_plugin).surface;
    return;
  }
  queueGuiSurface(*_plugin, _surfaceSnapshot);
  // Keep both main-thread editors on the latest editable revision while the
  // audio thread is still consuming it. Do not rebase an edit on stale data.
  auto *other = _surfacePopupChild ? _surfacePopupOwner : _surfacePopupView;
  if (other) {
    other->_surfaceSnapshot = _surfaceSnapshot;
    other->invalid();
  }
  error.clear();
  markStateDirty();
}
void service() override {
  refreshControlSnapshot();
  // Before activation, publish actual initialized geometry, not origin dots.
  if (!_plugin->active.load(std::memory_order_acquire))
    publishInactiveTelemetry(*_plugin);
}
void stopRefresh() override {
  Canvas::stopRefresh();
  finishGestures();
  pointerEditing = false;
  _dragParam = 0;
  _dragView = false;
  _dragSurfaceCell = -1;
  _dragSurfaceCursor = false;
  _openMenu = 0;
  _hoverMenuItem = -1;
  if (!_surfacePopupChild)
    hideSurfacePopup();
}
void paint() override {
  service();
  drawing::DrawingScope scope(*this, context, font, titleFont, &error);
  const auto attrs = drawing::softLabelAttrs();
  const auto values = drawing::softValueAttrs();
  const auto originalStyle = drawing::softTextStyle();
  if (_surfacePopupChild) {
    VSTGUI::CDrawContext::Transform transform(
        *context, VSTGUI::CGraphicsTransform().translate(-18., -42.));
    drawField_valueAttrs_style(attrs, values, originalStyle);
    return;
  }
  drawing::drawEncoderTitleBand(
      portableTitle, drawing::uppercaseString(presetDisplayName()),
      drawing::peakDbText(_plugin->outputPeak.load(std::memory_order_relaxed)),
      drawing::encoderTitleBand(kGuiWidth, kGuiHeight), attrs, values,
      drawing::softTitleAttrs(), originalStyle);
  drawField_valueAttrs_style(attrs, values, originalStyle);
  drawPanels_valueAttrs_style(attrs, values, originalStyle);
  drawOpenMenu_style(values, originalStyle);
}
drawing::Event eventAt(VSTGUI::CPoint p, int clicks = 1) const {
  if (_surfacePopupChild)
    p.offset(18., 42.);
  return {{p.x, p.y}, clicks};
}
bool pointerDown(VSTGUI::MouseDownEvent &e) override {
  if (!e.buttonState.isLeft())
    return false;
  finishGestures();
  service();
  pointerEditing = true;
  auto event = eventAt(e.mousePosition, e.clickCount);
  mouseDown(&event);
  if (!_dragParam && !_dragSurfaceCursor) {
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
void openSurfacePopup() {
  if (_surfacePopupChild)
    return;
  if (!_surfacePanel) {
    const auto title = std::string(portableTitle) + " — SURF";
    auto panel =
        std::make_unique<foundation::AuxiliaryWindow>(title.c_str(), 596, 608);
    auto *child = new Editor(*_plugin, true);
    child->_surfacePopupOwner = this;
    if (!panel->attach(child)) {
      error = "SURF WINDOW COULD NOT OPEN";
      return;
    }
    _surfacePopupView = child;
    _surfacePanel = std::move(panel);
  }
  _surfacePopupView->_fieldPage = 1;
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
bool savePresetPath(const std::string &path) {
  refreshControlSnapshot();
  const auto name =
      foundation::pathToUtf8(foundation::pathFromUtf8(path.c_str()).stem());
  if (!saveCustomPresetFile(path.c_str(), _paramsSnapshot, name.c_str()))
    return false;
#if S3G_ENVIRONMENT_KIND == 1
  const bool queued =
      queueGuiMetadata(*_plugin, _presetIndexSnapshot, name.c_str());
#else
  const bool queued =
      queueMetadataCommand(*_plugin, _presetIndexSnapshot, name.c_str());
#endif
  if (queued)
    std::snprintf(_customPresetNameSnapshot, sizeof(_customPresetNameSnapshot),
                  "%s", name.c_str());
  return queued;
}
bool loadPresetPath(const std::string &path) {
  CustomPresetFile file{};
  if (!loadCustomPresetFile(path.c_str(), file))
    return false;
  file.name[sizeof(file.name) - 1] = '\0';
  refreshControlSnapshot();
  file.params.outputGainDb = _paramsSnapshot.outputGainDb;
  const char *name = file.name[0] ? file.name : "Custom";
#if S3G_ENVIRONMENT_KIND == 1
  SavedState state{};
  state.params = file.params;
  state.surface = _surfaceSnapshot;
  s3g::bypassParameterSurfaceForSceneChange(state.surface);
  std::snprintf(state.customPresetName, sizeof(state.customPresetName), "%s",
                name);
  if (!queueGuiState(*_plugin, state, kReplaceCustomStateActionId))
    return false;
  _surfaceSnapshot = state.surface;
  _presetIndexSnapshot = 0;
#else
  if (!queueStateCommand(*_plugin, file.params, _surfaceSnapshot,
                         _presetIndexSnapshot, name,
                         kReplaceCustomStateActionId))
    return false;
#endif
  _paramsSnapshot = file.params;
  _displayParamsSnapshot = file.params;
  std::snprintf(_customPresetNameSnapshot, sizeof(_customPresetNameSnapshot),
                "%s", name);
  markStateDirty();
  return true;
}
void presetDialog(bool save) {
  finishGestures();
  foundation::FileDialogOptions options;
  options.save = save;
  options.title =
      std::string(save ? "Save " : "Load ") + portableTitle + " preset";
  options.extensionDescription = "s3g preset";
  options.extension = portablePresetExtension;
  options.defaultSaveName =
      std::string(_customPresetNameSnapshot[0] ? _customPresetNameSnapshot
                                               : "Custom") +
      "." + portablePresetExtension;
  options.initialDirectory =
      foundation::presetDirectory(portablePresetDirectory);
  const auto path = foundation::runFileDialog(getFrame(), options);
  if (path.empty())
    return;
  error = (save ? savePresetPath(path) : loadPresetPath(path))
              ? ""
              : "PRESET FILE COULD NOT BE READ / WRITTEN";
  invalid();
}
void saveCustomPreset() { presetDialog(true); }
void loadCustomPreset() { presetDialog(false); }
