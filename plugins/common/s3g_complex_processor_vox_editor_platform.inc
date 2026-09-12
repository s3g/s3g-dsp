VoxPresetSnapshot view;
bool shiftDown = false, phraseEditing = false;
VSTGUI::CTextEdit *phraseField = nullptr;
VSTGUI::CScrollView *lyricsView = nullptr;
const VSTGUI::ITextEditor *lyricsEditor = nullptr;
void syncVisible() { view = visibleVoxSnapshot(*_plugin); }
void applyParam(Plugin &, clap_id id, double value) {
  editValue(id, value, id == _dragParam);
  syncVisible();
}
void setPhraseText(const std::string &text) {
  if (phraseField && phraseField->getText() != text.c_str())
    phraseField->setText(text.c_str());
}
void setLyricsText(const std::string &text) {
  if (!lyricsEditor || lyricsEditor->getPlainText() == text)
    return;
  const bool updating = _updatingLyricsEditor;
  _updatingLyricsEditor = true;
  lyricsEditor->setPlainText(text);
  _updatingLyricsEditor = updating;
}
void setLyricsHidden(bool hidden) {
  if (lyricsView)
    lyricsView->setVisible(!hidden);
}
void onTextEditorCreated(const VSTGUI::ITextEditor &editor) override {
  lyricsEditor = &editor;
  VSTGUI::ITextEditor::Style style;
  style.flags.clear();
  style.font = foundation::makeUiFont(11);
  style.textColor = drawing::color(0xb0b0b0);
  style.backColor = drawing::color(0x0b0b0b);
  style.frameColor = style.backColor;
  style.cursorColor = drawing::color(0xd0d0d0);
  style.selectionBackColor = drawing::color(0x454545);
  style.lineSpacing = 2.5;
  editor.setStyle(style);
  setLyricsText(loadVoxLyrics(*_plugin));
}
void onTextEditorDestroyed(const VSTGUI::ITextEditor &editor) override {
  editor.resetController();
  if (lyricsEditor == &editor)
    lyricsEditor = nullptr;
}
void onTextEditorTextChanged(const VSTGUI::ITextEditor &editor) override {
  if (_updatingLyricsEditor)
    return;
  const auto text = editor.getPlainText();
  storeVoxLyricsText(*_plugin, text.c_str());
  rebuildVoxLyricScore(*_plugin);
  _selectedLyricCue = std::min(_selectedLyricCue,
                               std::max(1u, voxLyricCueCount(*_plugin)) - 1u);
  requestVoxLyricCue(*_plugin, _selectedLyricCue);
  setPhraseText(voxLyricCueText(*_plugin, _selectedLyricCue));
  markPresetCustom();
  markStateDirty();
  invalid();
}
void phraseTextChanged(void *) {
  if (!phraseField)
    return;
  replaceVoxLyricCueText(*_plugin, _selectedLyricCue,
                         phraseField->getText().getString().c_str());
  setLyricsText(loadVoxLyrics(*_plugin));
  markPresetCustom();
  markStateDirty();
}
void valueChanged(VSTGUI::CControl *c) override {
  if (c == phraseField)
    phraseTextChanged(nullptr);
  else
    Canvas::valueChanged(c);
}
void onTextEditPlatformControlTookFocus(VSTGUI::CTextEdit *f) override {
  if (f == phraseField)
    phraseEditing = true;
  else
    Canvas::onTextEditPlatformControlTookFocus(f);
}
void onTextEditPlatformControlLostFocus(VSTGUI::CTextEdit *f) override {
  if (f == phraseField) {
    phraseEditing = false;
    phraseTextChanged(nullptr);
  } else
    Canvas::onTextEditPlatformControlLostFocus(f);
}
void ensureFields() {
  if (!getFrame())
    return;
  if (!phraseField) {
    phraseField = new VSTGUI::CTextEdit(
        foundation::rect(912, 456, 214, 22), this, 7811,
        voxLyricCueText(*_plugin, _selectedLyricCue).c_str());
    phraseField->setFont(foundation::makeUiFont(11));
    phraseField->setFontColor(foundation::palette().value);
    phraseField->setBackColor(foundation::palette().cell);
    phraseField->setFrameColor(foundation::palette().cell);
    phraseField->setHoriAlign(VSTGUI::kLeftText);
    phraseField->setPlaceholderString("type phrase or spaced romaji");
    phraseField->setImmediateTextChange(true);
    phraseField->registerTextEditListener(this);
    getFrame()->addView(phraseField);
  }
  if (!lyricsView) {
    // The pinned VSTGUI text editor sizes itself to its document. Its factory
    // currently ignores the rectangle argument; a scroll viewport supplies the
    // fixed Cocoa lyric-sheet bounds and clips long documents correctly.
    const auto content = foundation::rect(0, 0, 544, 370);
    auto *text = VSTGUI::createNewTextEditor(content, this);
    if (text) {
      text->setViewSize(content);
      text->setMouseableArea(content);
      lyricsView = new VSTGUI::CScrollView(
          foundation::rect(44, 104, 544, 370), content,
          VSTGUI::CScrollView::kVerticalScrollbar |
              VSTGUI::CScrollView::kHorizontalScrollbar |
              VSTGUI::CScrollView::kAutoHideScrollbars |
              VSTGUI::CScrollView::kDontDrawFrame |
              VSTGUI::CScrollView::kAutoDragScrolling,
          10.);
      lyricsView->setBackgroundColor(drawing::color(0x0b0b0b));
      for (auto *bar : {lyricsView->getVerticalScrollbar(),
                        lyricsView->getHorizontalScrollbar()})
        if (bar) {
          bar->setBackgroundColor(drawing::color(0x151515));
          bar->setFrameColor(drawing::color(0x151515));
          bar->setScrollerColor(drawing::color(0x555555));
        }
      lyricsView->addView(text);
      lyricsView->setVisible(false);
      getFrame()->addView(lyricsView);
    }
  }
}
void service() override {
  syncVisible();
  ensureFields();
  const auto count = voxLyricCueCount(*_plugin);
  // A focused lyric editor owns selection until editing ends.
  const auto *focusView = getFrame() ? getFrame()->getFocusView() : nullptr;
  const bool editingLyrics =
      lyricsView && focusView && focusView != this && focusView != phraseField;
  if (count && view.lyric.mode != VoxLyricMode::Loop && !phraseEditing &&
      !editingLyrics) {
    _selectedLyricCue =
        _plugin->guiLyricCue.load(std::memory_order_relaxed) % count;
  }
  if (!phraseEditing)
    setPhraseText(voxLyricCueText(*_plugin, _selectedLyricCue));
  if (!editingLyrics)
    setLyricsText(loadVoxLyrics(*_plugin));
  setLyricsHidden(_leftPage != 3 || _showLyricGenerator || _openMenu == 12 ||
                  _openMenu == 14 || _openMenu == 15 ||
                  _openMenu == kLyricCueChannelMenuId);
  captureTrails();
  invalid();
}
double refreshPeriodMilliseconds() const override { return 1000. / 30.; }
void startRefresh() override {
  _plugin->guiVisible.store(true);
  service();
  Canvas::startRefresh();
}
void stopRefresh() override {
  Canvas::stopRefresh();
  finishGestures();
  _plugin->guiVisible.store(false);
  _openMenu = 0;
  _hoverMenuItem = -1;
  _dragView = false;
  if (phraseField) {
    phraseField->looseFocus();
    auto *f = phraseField;
    phraseField = nullptr;
    f->unregisterTextEditListener(this);
    if (f->getFrame())
      f->getFrame()->removeView(f);
  }
  if (lyricsEditor) {
    lyricsEditor->resetController();
    lyricsEditor = nullptr;
  }
  if (lyricsView) {
    auto *v = lyricsView;
    lyricsView = nullptr;
    if (v->getFrame())
      v->getFrame()->removeView(v);
  }
  phraseEditing = false;
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
  if (_plugin->guiParamEvents.available() < std::size(kParams) * 3u + 32u) {
    beep();
    return true;
  }
  focus();
  syncVisible();
  shiftDown = e.modifiers.has(VSTGUI::ModifierKey::Shift);
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
  finishGestures();
  markStateDirty();
}
bool importSource(const std::filesystem::path &path, bool folder) {
  if (folder) {
    auto bank = loadVoicebankFolder(path);
    if (!bank) {
      error = "VOICEBANK HAS NO USABLE WAV ALIASES";
      return false;
    }
    std::atomic_store_explicit(&_plugin->voicebank, std::move(bank),
                               std::memory_order_release);
    std::atomic_store_explicit(&_plugin->worldSample,
                               std::shared_ptr<const VoxWorldSample>{},
                               std::memory_order_release);
  } else {
#if defined(S3G_HAS_WORLD)
    int rate = 0;
    auto wave = readWavMono(path, &rate);
    auto analyzed = makeWorldSampleFromWave(
        wave, rate, foundation::pathToUtf8(path.filename()));
    if (!analyzed) {
      error = "WAV COULD NOT BE ANALYZED";
      return false;
    }
    std::shared_ptr<const VoxWorldSample> world = std::move(analyzed);
    std::atomic_store_explicit(&_plugin->worldSample, std::move(world),
                               std::memory_order_release);
    std::atomic_store_explicit(&_plugin->voicebank,
                               std::shared_ptr<const VoxVoicebank>{},
                               std::memory_order_release);
#else
    error = "WORLD ANALYSIS IS NOT AVAILABLE IN THIS BUILD";
    return false;
#endif
  }
  rebuildVoxLyricScore(*_plugin);
  storeVoxLoadedName(*_plugin, foundation::pathToUtf8(path.filename()).c_str());
  _plugin->sourceResetPending.store(true, std::memory_order_release);
  request();
  error.clear();
  markStateDirty();
  invalid();
  return true;
}
void loadEncodedPhrase() {
  finishGestures();
  openPopup(encodedLoadButtonRect(), {"VOCAL WAV FILE", "VOICEBANK FOLDER"}, 0,
            [this](int choice) {
              foundation::FileDialogOptions options;
              options.directory = choice == 1;
              options.title = choice == 1 ? "Load Vox voicebank folder"
                                          : "Load Vox vocal WAV";
              if (choice == 0) {
                options.extension = "wav";
                options.extensions = {"wav", "wave"};
                options.extensionDescription = "Wave audio";
              }
              auto path = foundation::runFileDialog(getFrame(), options);
              if (!path.empty())
                importSource(foundation::pathFromUtf8(path.c_str()),
                             choice == 1);
            });
}
void loadWave() {
  foundation::FileDialogOptions options;
  options.title = "Load Vox wavetable";
  options.extension = "wav";
  options.extensions = {"wav", "wave"};
  options.extensionDescription = "Wave audio";
  auto path = foundation::runFileDialog(getFrame(), options);
  if (path.empty())
    return;
  auto wave = readWavMono(foundation::pathFromUtf8(path.c_str()));
  if (wave.size() < 32u) {
    error = "WAV COULD NOT BE READ";
    return;
  }
  std::shared_ptr<const s3g::AmbiVotTableBank> bank =
      std::make_shared<s3g::AmbiVotTableBank>(s3g::ambiVotBankFromWave(wave));
  std::atomic_store_explicit(&_plugin->userBank, std::move(bank),
                             std::memory_order_release);
  applyParam(*_plugin, kPresetParamId,
             static_cast<double>(s3g::AmbiVotPreset::User));
  markStateDirty();
}
