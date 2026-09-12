std::string error;
bool altDown = false;
std::vector<clap_id> gestures;
drawing::Rect bounds() const {
  return drawing::makeRect(0, 0, kGuiWidth, kGuiHeight);
}
drawing::Point convertPoint_fromView(drawing::Point p, void *) { return p; }
void setNeedsDisplay(bool) { invalid(); }
void focus() {
  if (getFrame())
    getFrame()->setFocusView(this);
}
bool wantsFocus() const override { return true; }
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
uint32_t available() const {
  const auto write = _plugin->guiCommandWrite.load(std::memory_order_relaxed);
  const auto read = _plugin->guiCommandRead.load(std::memory_order_acquire);
  return kGuiCommandCapacity - (write - read);
}
void finishGestures() {
  for (auto id : gestures)
    enqueueGuiCommand(*_plugin, {GuiCommandType::GestureEnd, id});
  gestures.clear();
}
bool editValue(clap_id id, double value, bool continuous = false) {
  if (id == CLAP_INVALID_ID) {
    finishGestures();
    return true;
  }
  ParamRange range;
  if (!std::isfinite(value) || !paramRange(id, range))
    return false;
  if (available() <= gestures.size() + 8u) {
    beep();
    return false;
  }
  const bool existing =
      std::find(gestures.begin(), gestures.end(), id) != gestures.end();
  if (!existing) {
    enqueueGuiCommand(*_plugin, {GuiCommandType::GestureBegin, id});
    gestures.push_back(id);
  }
  publishUiParameter(*_plugin, id, value);
  double applied = value;
  uiParameterValue(*_plugin, id, applied);
  enqueueGuiCommand(*_plugin, {GuiCommandType::ParamValue, id, applied});
  if (!continuous && !existing) {
    enqueueGuiCommand(*_plugin, {GuiCommandType::GestureEnd, id});
    gestures.pop_back();
  }
  markPatchCustom();
  markStateDirty();
  error.clear();
  invalid();
  return true;
}
void applyGuiParam_value(clap_id id, double value) {
  editValue(id, value,
            id == _dragParam || id == _mixerDragParam ||
                _surfaceDragCell == -2);
}
double refreshPeriodMilliseconds() const override { return 50.; }
void service() override { invalid(); }
void startRefresh() override { Canvas::startRefresh(); }
void stopRefresh() override {
  finishGestures();
  _dragParam = _mixerDragParam = CLAP_INVALID_ID;
  _wireDragSource = -1;
  _openMenu = kMenuNone;
  _hoverMenuItem = -1;
  if (!_mixerPopupChild)
    hideMixerPopup();
  Canvas::stopRefresh();
}
~Editor() override {
  stopRefresh();
  if (!_mixerPopupChild)
    destroyMixerPopup();
}
void paint() override {
  drawing::DrawingScope scope(*this, context, font, titleFont, &error);
  drawOriginal(bounds());
}
bool pointerDown(VSTGUI::MouseDownEvent &e) override {
  if (!e.buttonState.isLeft())
    return false;
  finishGestures();
  if (available() < kTotalParamCount * 3u + 16u) {
    beep();
    return true;
  }
  altDown = e.modifiers.has(VSTGUI::ModifierKey::Alt);
  error.clear();
  drawing::Event event{{e.mousePosition.x, e.mousePosition.y},
                       int(e.clickCount)};
  mouseDown(&event);
  markStateDirty();
  return true;
}
void pointerMove(VSTGUI::MouseMoveEvent &e) override {
  altDown = e.modifiers.has(VSTGUI::ModifierKey::Alt);
  drawing::Event event{{e.mousePosition.x, e.mousePosition.y}, 1};
  if (e.buttonState.isLeft())
    mouseDragged(&event);
  else
    mouseMoved(&event);
}
void pointerUp(VSTGUI::MouseUpEvent &e) override {
  altDown = e.modifiers.has(VSTGUI::ModifierKey::Alt);
  drawing::Event event{{e.mousePosition.x, e.mousePosition.y}, 1};
  mouseUp(&event);
  finishGestures();
  markStateDirty();
}
void onKeyboardEvent(VSTGUI::KeyboardEvent &e) override {
  if (e.type != VSTGUI::EventType::KeyDown ||
      e.modifiers.has(VSTGUI::ModifierKey::Alt) ||
      e.modifiers.has(VSTGUI::ModifierKey::Super) ||
      e.modifiers.has(VSTGUI::ModifierKey::Control))
    return;
  if (e.virt == VSTGUI::VirtualKey::Left)
    navigatePageBy(-1);
  else if (e.virt == VSTGUI::VirtualKey::Right)
    navigatePageBy(1);
  else
    return;
  e.consumed = true;
}
Editor *owner() {
  return _mixerPopupChild && _mixerPopupOwner ? _mixerPopupOwner : this;
}
void *popupParent() {
#if defined(_WIN32)
  auto *frame = owner()->getFrame();
  return frame ? frame->getPlatformFrame()->getPlatformRepresentation()
               : nullptr;
#else
  return nullptr; // Keep Cocoa's independent windows / Spaces behavior.
#endif
}
void navigatePageBy(int delta) {
  auto *main = owner();
  const auto page =
      (activePage() + kPageCount + (delta < 0 ? -1 : 1)) % kPageCount;
  if (main->_pagePanels[page] && main->_pagePanels[page]->visible()) {
    main->_pagePanels[page]->show(popupParent());
    return;
  }
  _plugin->guiPage.store(page);
  main->focus();
  main->invalid();
}
void openPagePopup(uint32_t page) {
  if (_mixerPopupChild || page >= kPageCount)
    return;
  static const char *names[] = {"PATCH", "MIXER", "CHANNEL", "SAFETY", "AUX"};
  if (!_pagePanels[page]) {
    const std::string title =
        std::string("s3g PROCESSOR NO INPUT MIXER — ") + names[page];
    auto window = std::make_unique<foundation::AuxiliaryWindow>(
        title.c_str(), kGuiWidth, kGuiHeight);
    auto *view = new Editor(*_plugin);
    view->_mixerPopupChild = true;
    view->_mixerPopupOwner = this;
    view->_lockedPage = page;
    view->_wiringGridMode = _wiringGridMode;
    if (!window->attach(view)) {
      beep();
      return;
    }
    _pagePopupViews[page] = view;
    _pagePanels[page] = std::move(window);
  }
  if (_plugin->guiPage.load() == page)
    for (uint32_t n = 1; n <= kPageCount; ++n) {
      const auto next = (page + n) % kPageCount;
      if (!_pagePanels[next] || !_pagePanels[next]->visible()) {
        _plugin->guiPage.store(next);
        break;
      }
    }
  if (!_pagePanels[page]->show(popupParent()))
    beep();
  invalid();
}
void dockPage() {
  if (!_mixerPopupChild || !_mixerPopupOwner)
    return;
  _plugin->guiPage.store(_lockedPage);
  _mixerPopupOwner->_pagePanels[_lockedPage]->hide();
  _mixerPopupOwner->focus();
  _mixerPopupOwner->invalid();
}
void openEffectEditorForLane_slot(uint32_t lane, uint32_t slot) {
  if (owner() != this) {
    owner()->openEffectEditorForLane_slot(lane, slot);
    return;
  }
  finishGestures();
  if (!_effectPanel) {
    auto window = std::make_unique<foundation::AuxiliaryWindow>(
        "s3g EFFECT EDITOR", 454, 292);
    auto *view = new EffectEditor(
        *_plugin, [this](clap_id id, double value, bool continuous) {
          editValue(id, value, continuous);
        });
    if (!window->attach(view)) {
      beep();
      return;
    }
    _effectEditor = view;
    _effectPanel = std::move(window);
  }
  _effectEditor->setLane_slot(lane, slot);
  _effectPanel->setTitle(drawing::stringFormat(
      "s3g EFFECT EDITOR — LANE %u / SLOT %u", lane + 1, slot + 1));
  if (!_effectPanel->show(popupParent()))
    beep();
}
void openEffectEditorForAux(uint32_t bus) {
  if (owner() != this) {
    owner()->openEffectEditorForAux(bus);
    return;
  }
  openEffectEditorForLane_slot(0, 0);
  if (!_effectEditor)
    return;
  _effectEditor->setAux(bus);
  _effectPanel->setTitle(drawing::stringFormat("s3g EFFECT EDITOR — AUX %c",
                                               'A' + std::min(bus, 1u)));
}
void hideEffectEditor() {
  if (owner() != this) {
    owner()->hideEffectEditor();
    return;
  }
  if (_effectPanel)
    _effectPanel->hide();
}
void destroyEffectEditor() {
  if (_mixerPopupChild)
    return;
  _effectPanel.reset();
  _effectEditor = nullptr;
}
void hideMixerPopup() {
  hideEffectEditor();
  for (auto &panel : _pagePanels)
    if (panel)
      panel->hide();
}
void destroyMixerPopup() {
  destroyEffectEditor();
  for (uint32_t i = 0; i < kPageCount; ++i) {
    _pagePanels[i].reset();
    _pagePopupViews[i] = nullptr;
  }
}
bool presetFile(const std::string &path, bool save) {
  if (save)
    return s3g::clap_gui::portable::saveStateFile(&_plugin->plugin, stateExt,
                                                  path.c_str());
  auto staged = std::make_unique<Plugin>();
  staged->plugin.plugin_data = staged.get();
  if (!s3g::clap_gui::portable::loadStateFile(&staged->plugin, stateExt,
                                              path.c_str()) ||
      available() < kTotalParamCount * 3u + 8u)
    return false;
  const auto current = uiSnapshot(*_plugin);
  auto loaded = uiSnapshot(*staged);
  loaded.params.outputGainDb = current.params.outputGainDb;
  // Preserve Cocoa's complete-patch commit and reseed, not just the individual
  // parameter values (which would leave the feedback engine's history alive).
  publishUiSnapshot(*_plugin, loaded);
  enqueueGuiCommand(*_plugin, {GuiCommandType::ApplyUiSnapshot, CLAP_INVALID_ID,
                               0.62, 0u, loaded.params.seed});
  // Automation notifications follow the ordered patch commit.
  for (uint32_t i = 0; i < kTotalParamCount; ++i) {
    const clap_id id = paramIdAtIndex(i);
    double value = 0.;
    if (id != kOutputParamId && uiParameterValue(*staged, id, value))
      editValue(id, value);
  }
  _plugin->selectedLane.store(staged->selectedLane.load());
  _plugin->selectedSlot.store(staged->selectedSlot.load());
  _plugin->selectedSource.store(staged->selectedSource.load());
  _plugin->selectedDestination.store(staged->selectedDestination.load());
  _plugin->guiPage.store(staged->guiPage.load());
  markStateDirty();
  return true;
}
bool handleProcessorTitleClick(drawing::Point point, const clap_plugin_t *,
                               const drawing::String &,
                               const s3g::gui_layout::EncoderTitleBand &band,
                               char *name, size_t capacity, clap_id) {
  const bool save =
      drawing::contains(point, drawing::cocoaRect(band.saveButton));
  if (!save && !drawing::contains(point, drawing::cocoaRect(band.loadButton)))
    return false;
  finishGestures();
  foundation::FileDialogOptions options;
  options.save = save;
  options.title =
      save ? "Save No Input Mixer preset" : "Load No Input Mixer preset";
  options.extension = "s3gpreset";
  options.extensionDescription = "s3g Preset";
  options.initialDirectory = foundation::presetDirectory("No Input Mixer");
  options.defaultSaveName = "Preset.s3gpreset";
  auto path = foundation::runFileDialog(getFrame(), options);
  if (path.empty())
    return true;
  if (presetFile(path, save)) {
    auto stem =
        foundation::pathToUtf8(foundation::pathFromUtf8(path.c_str()).stem());
    std::snprintf(name, capacity, "%s", stem.c_str());
  } else
    error = "PRESET FILE COULD NOT BE READ / WRITTEN";
  return true;
}
