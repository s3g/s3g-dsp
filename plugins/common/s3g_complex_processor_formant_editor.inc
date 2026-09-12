std::string error;
bool altDown = false, phraseEditing = false;
VSTGUI::CTextEdit *phraseField = nullptr;
drawing::Rect bounds() const {
  return drawing::makeRect(0, 0, kGuiWidth, kGuiHeight);
}
drawing::Point convertPoint_fromView(drawing::Point p, void *) { return p; }
void setNeedsDisplay(bool) { invalid(); }
void beep() {
  error = "ACTION UNAVAILABLE / HOST QUEUE BUSY";
  invalid();
}
void markStateDirty() {
  if (!_plugin->host || !_plugin->host->get_extension)
    return;
  const auto *state = static_cast<const clap_host_state_t *>(
      _plugin->host->get_extension(_plugin->host, CLAP_EXT_STATE));
  if (state && state->mark_dirty)
    state->mark_dirty(_plugin->host);
}
bool wantsFocus() const override { return true; }
void focus() {
  if (getFrame())
    getFrame()->setFocusView(this);
}
bool phraseIsEditing() const { return phraseEditing; }
std::string phraseText() const {
  return phraseField ? phraseField->getText().getString()
                     : formantPhraseString(loadPhrase(*_plugin));
}
void ensurePhraseField() {
  if (phraseField || !getFrame())
    return;
  phraseField =
      new VSTGUI::CTextEdit(foundation::rect(28, 106, 1188, 24), this, 7701,
                            formantPhraseString(loadPhrase(*_plugin)).c_str());
  phraseField->setFont(foundation::makeUiFont(12));
  phraseField->setFontColor(foundation::palette().value);
  phraseField->setBackColor(foundation::palette().cell);
  phraseField->setFrameColor(foundation::palette().cell);
  phraseField->setHoriAlign(VSTGUI::kLeftText);
  phraseField->setPlaceholderString("type a phrase   | = rest");
  phraseField->registerTextEditListener(this);
  phraseField->setVisible(false);
  getFrame()->addView(phraseField);
}
void syncPhraseFieldVisibility() {
  if (!phraseField)
    return;
  const bool visible =
      _page == kFormantPagePhrase && _openMenu == CLAP_INVALID_ID;
  if (!visible && phraseEditing)
    focus();
  phraseField->setVisible(visible);
}
void valueChanged(VSTGUI::CControl *c) override {
  if (c == phraseField) {
    commitPhrase(nullptr);
    markStateDirty();
  } else
    Canvas::valueChanged(c);
}
void onTextEditPlatformControlTookFocus(VSTGUI::CTextEdit *field) override {
  if (field == phraseField)
    phraseEditing = true;
  else
    Canvas::onTextEditPlatformControlTookFocus(field);
}
void onTextEditPlatformControlLostFocus(VSTGUI::CTextEdit *field) override {
  if (field == phraseField) {
    phraseEditing = false;
    commitPhrase(nullptr);
    markStateDirty();
  } else
    Canvas::onTextEditPlatformControlLostFocus(field);
}
void service() override {
  ensurePhraseField();
  if (phraseField && !phraseEditing) {
    const auto text = formantPhraseString(loadPhrase(*_plugin));
    if (phraseText() != text)
      phraseField->setText(text.c_str());
  }
  syncPhraseFieldVisibility();
  invalid();
}
double refreshPeriodMilliseconds() const override { return 50.; }
void startRefresh() override {
  service();
  Canvas::startRefresh();
}
void stopRefresh() override {
  finishDrag();
  _openMenu = CLAP_INVALID_ID;
  _hoverMenuItem = -1;
  if (phraseField) {
    phraseField->looseFocus();
    auto *field = phraseField;
    phraseField = nullptr;
    field->unregisterTextEditListener(this);
    if (field->getFrame())
      field->getFrame()->removeView(field);
  }
  phraseEditing = false;
  Canvas::stopRefresh();
}
~Editor() override { stopRefresh(); }
void paint() override {
  drawing::DrawingScope scope(*this, context, font, titleFont, &error);
  drawOriginal(bounds());
}
bool pointerDown(VSTGUI::MouseDownEvent &e) override {
  if (!e.buttonState.isLeft())
    return false;
  finishDrag();
  altDown = e.modifiers.has(VSTGUI::ModifierKey::Alt);
  error.clear();
  drawing::Event event{{e.mousePosition.x, e.mousePosition.y},
                       int(e.clickCount)};
  mouseDown(&event);
  markStateDirty();
  return true;
}
void pointerMove(VSTGUI::MouseMoveEvent &e) override {
  drawing::Event event{{e.mousePosition.x, e.mousePosition.y}, 1};
  if (e.buttonState.isLeft())
    mouseDragged(&event);
  else
    mouseMoved(&event);
}
void pointerUp(VSTGUI::MouseUpEvent &e) override {
  drawing::Event event{{e.mousePosition.x, e.mousePosition.y}, 1};
  mouseUp(&event);
  markStateDirty();
}
void onKeyboardEvent(VSTGUI::KeyboardEvent &e) override {
  using VSTGUI::VirtualKey;
  if (e.type != VSTGUI::EventType::KeyDown || _page != kFormantPageRoute)
    return;
  clampMatrixSelection();
  const auto count = activeMatrixBands();
  if (e.virt == VirtualKey::Left && _selectedSource > 0)
    --_selectedSource;
  else if (e.virt == VirtualKey::Right && _selectedSource + 1 < count)
    ++_selectedSource;
  else if (e.virt == VirtualKey::Up && _selectedDestination > 0)
    --_selectedDestination;
  else if (e.virt == VirtualKey::Down && _selectedDestination + 1 < count)
    ++_selectedDestination;
  else if (e.virt == VirtualKey::Delete || e.virt == VirtualKey::Back) {
    if (prepareMatrixCellEdit() &&
        !queueFormantParamValue(*_plugin,
                                matrixParamId(_editingSceneB,
                                              _selectedDestination,
                                              _selectedSource),
                                0.))
      beep();
    markStateDirty();
  } else
    return;
  e.consumed = true;
  invalid();
}
bool presetFile(const std::string &path, bool save) {
  if (save)
    return s3g::clap_gui::portable::saveStateFile(&_plugin->plugin, stateExt,
                                                  path.c_str());
  auto staged = std::make_unique<Plugin>();
  staged->plugin.plugin_data = staged.get();
  for (const auto &def : kParamDefs)
    storeValue(*staged, def.id, def.defaultValue);
  if (!s3g::clap_gui::portable::loadStateFile(&staged->plugin, stateExt,
                                              path.c_str()))
    return false;
  std::vector<s3g::clap_gui::ParamEvent> events;
  events.reserve(kParamCount * 3u);
  for (const auto &def : kParamDefs) {
    if (def.id == kOutputParamId)
      continue;
    events.push_back({s3g::clap_gui::ParamEventKind::GestureBegin, def.id, 0.});
    events.push_back({s3g::clap_gui::ParamEventKind::Value, def.id,
                      loadValue(*staged, def.id)});
    events.push_back({s3g::clap_gui::ParamEventKind::GestureEnd, def.id, 0.});
  }
  if (_plugin->guiParamEvents.available() < events.size() ||
      _plugin->textProgramEvents.available() < 1u)
    return false;
  if (!_plugin->guiParamEvents.pushBatch(events.data(),
                                         static_cast<uint32_t>(events.size())))
    return false;
  // Copy the exact saved scene, without reapplying a profile over custom edits.
  for (const auto &def : kParamDefs)
    if (def.id != kOutputParamId)
      storeValue(*_plugin, def.id, loadValue(*staged, def.id));
  if (!publishTextPhrase(*_plugin, loadPhrase(*staged).text.data()))
    return false;
  _plugin->routingControlDirty.store(true, std::memory_order_release);
  markTailChanged(*_plugin);
  requestParamValuesRescan(*_plugin);
  requestGuiParamService(*_plugin);
  service();
  markStateDirty();
  return true;
}
bool handleTitleClick(drawing::Point point,
                      const s3g::gui_layout::EncoderTitleBand &band) {
  const bool save =
      drawing::contains(point, drawing::cocoaRect(band.saveButton));
  if (!save && !drawing::contains(point, drawing::cocoaRect(band.loadButton)))
    return false;
  focus();
  finishDrag();
  foundation::FileDialogOptions options;
  options.save = save;
  options.title =
      save ? "Save Formant Matrix preset" : "Load Formant Matrix preset";
  options.extension = "s3gpreset";
  options.extensionDescription = "s3g Preset";
  options.initialDirectory = foundation::presetDirectory("Formant Matrix");
  options.defaultSaveName = "Preset.s3gpreset";
  const auto path = foundation::runFileDialog(getFrame(), options);
  if (path.empty())
    return true;
  if (presetFile(path, save)) {
    auto name =
        foundation::pathToUtf8(foundation::pathFromUtf8(path.c_str()).stem());
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                  name.c_str());
    error.clear();
  } else
    error = "PRESET FILE COULD NOT BE READ / WRITTEN";
  return true;
}
