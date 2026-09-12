void set(Plugin &p, Editor &v, Events &e, clap_id id, double x) {
#if S3G_COMPLEX_KIND == 2
  check(queueFormantParamValue(p, id, x), "Formant parameter enqueue");
#else
  check(v.editValue(id, x), "GUI parameter enqueue");
#endif
  for (int n = 0; n < 20; ++n)
    flush(p, v, e);
}
void audioProbe(Plugin &p, Editor &v, Events &e) {
  const auto *ports = static_cast<const clap_plugin_audio_ports_t *>(
      p.plugin.get_extension(&p.plugin, CLAP_EXT_AUDIO_PORTS));
  constexpr unsigned frames = 128;
  std::array<std::array<float, frames>, 64> in{}, out{};
  std::array<float *, 64> ip{}, op{};
  for (unsigned c = 0; c < 64; ++c) {
    ip[c] = in[c].data();
    op[c] = out[c].data();
  }
  clap_audio_port_info_t oi{}, ii{};
  check(ports && ports->get(&p.plugin, 0, false, &oi), "audio output port");
  const auto inputs = ports->count(&p.plugin, true);
  if (inputs)
    ports->get(&p.plugin, 0, true, &ii);
  clap_audio_buffer_t ib{}, ob{};
  ib.data32 = ip.data();
  ib.channel_count = ii.channel_count;
  ob.data32 = op.data();
  ob.channel_count = oi.channel_count;
  check(p.plugin.activate(&p.plugin, 48000., 1, frames), "audio activate");
  check(p.plugin.start_processing(&p.plugin), "audio start");
  double energy = 0.;
  std::vector<float> capture;
  for (unsigned block = 0; block < 160; ++block) {
    for (unsigned c = 0; c < 64; ++c)
      for (unsigned f = 0; f < frames; ++f)
        in[c][f] = float(.07 * std::sin((block * frames + f) * .029 + c * .2));
    clap_process_t process{};
    process.frames_count = frames;
    process.steady_time = block * frames;
    process.audio_inputs_count = inputs ? 1 : 0;
    process.audio_inputs = &ib;
    process.audio_outputs_count = 1;
    process.audio_outputs = &ob;
    process.out_events = &e.output;
    check(p.plugin.process(&p.plugin, &process) != CLAP_PROCESS_ERROR,
          "audio process");
    for (unsigned f = 0; f < frames; ++f)
      for (unsigned c = 0; c < oi.channel_count; ++c) {
        check(std::isfinite(out[c][f]), "finite audio");
        energy += std::abs(out[c][f]);
        capture.push_back(out[c][f]);
      }
    if (block % 16 == 0)
      v.service();
  }
#if S3G_COMPLEX_KIND != 2
  check(energy > 1.e-5, "non-silent audio");
#endif
  if (const auto *path = std::getenv("S3G_COMPLEX_AUDIO_CAPTURE")) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(capture.data()),
               capture.size() * sizeof(float));
    check(bool(file), "audio capture");
  }
  render(v, "playing");
  p.plugin.stop_processing(&p.plugin);
  p.plugin.deactivate(&p.plugin);
  std::cout << "Audio energy: " << energy << '\n';
}
void run(Plugin &p, Editor &v, Events &e) {
  render(v, "first-open");
  // Every exposed host parameter remains readable, finite and bounded.
  for (uint32_t i = 0; i < paramsCount(&p.plugin); ++i) {
    clap_param_info_t info{};
    check(paramsGetInfo(&p.plugin, i, &info), "parameter info");
    const auto x = value(p, info.id);
    check(std::isfinite(x) && x >= info.min_value - 1.e-4 &&
              x <= info.max_value + 1.e-4,
          "parameter bounds");
  }
#if S3G_COMPLEX_KIND == 1
  constexpr clap_id output = kParamOutput;
  for (size_t i = 0; i < kImprintAtlas.size(); ++i) {
    const auto path = std::filesystem::path(S3G_TEST_REPO) /
                      "plugins/clap_ambi_imprint/atlas" /
                      (std::string(kImprintAtlas[i].resource) + ".s3gimprint");
    check(v.importImprint(F::pathToUtf8(path), kImprintAtlas[i].title),
          "Imprint atlas load");
    render(v, "atlas-" + std::to_string(i));
  }
  v._atlasMenuOpen = true;
  render(v, "atlas-menu");
  v._atlasMenuOpen = false;
  for (int i = 0; i < 3; ++i) {
    click(v, center(v.viewButtonRect(i)));
    check(v._viewMode == i, "Imprint camera preset");
  }
  set(p, v, e, kParamMix, .37);
#elif S3G_COMPLEX_KIND == 2
  constexpr clap_id output = kOutputParamId;
  for (uint32_t page = 0; page < kFormantPageCount; ++page) {
    click(v, center(formantPageButtonRect(page)));
    check(v._page == page, "Formant page hit map");
    v.service();
    render(v, "page-" + std::to_string(page));
  }
  click(v, center(formantPageButtonRect(kFormantPagePhrase)));
  v.service();
  check(v.phraseField && v.phraseField->isVisible(),
        "Formant editable phrase field");
  v.phraseField->setText("hello | world");
  v.commitPhrase(nullptr);
  flush(p, v, e);
  check(formantPhraseString(loadPhrase(p)) == "hello | world",
        "Formant phrase commit");
  click(v, center(formantPageButtonRect(kFormantPageRoute)));
  const auto cell = formantMatrixCellRect(v.activeMatrixBands(), 2, 3);
  down(v, center(cell), 1, true);
  up(v);
  flush(p, v, e);
  check(v._selectedSource == 3 && v._selectedDestination == 2,
        "Formant matrix cell hit map");
  v._openMenu = kPitchScaleParamId;
  render(v, "scale-menu");
  v._openMenu = CLAP_INVALID_ID;
  set(p, v, e, matrixParamId(false, 2, 3), -.37);
#elif S3G_COMPLEX_KIND == 3
  constexpr clap_id output = kOutputParamId;
  for (uint32_t page = 0; page < kPageCount; ++page) {
    p.guiPage.store(page);
    render(v, "page-" + std::to_string(page));
    v.openPagePopup(page);
    check(v._pagePanels[page] && v._pagePanels[page]->visible(),
          "NIM pop-out page");
    if (v._pagePopupViews[page])
      v._pagePopupViews[page]->dockPage();
    check(!v._pagePanels[page]->visible() && p.guiPage.load() == page,
          "NIM dock page");
  }
  v.openEffectEditorForLane_slot(2, 1);
  check(v._effectPanel && v._effectPanel->visible(), "NIM lane effect editor");
  v.openEffectEditorForAux(1);
  check(v._effectPanel && v._effectPanel->visible(), "NIM aux effect editor");
  v.hideEffectEditor();
  p.guiPage.store(0);
  v._openMenu = kMenuPreset;
  render(v, "factory-menu");
  v._openMenu = kMenuNone;
#else
  constexpr clap_id output = kOutputParamId;
  click(v, center(v.pageButtonRect(1)));
  v.service();
  check(v._leftPage == 3, "Vox lyric page hit map");
  v.setLyricsText("hello world\nwe sing");
  check(v.lyricsEditor != nullptr, "Vox multiline editor created");
  check(v.lyricsView && v.lyricsView->getViewSize().left == 44 &&
            v.lyricsView->getViewSize().top == 104,
        "Vox lyric viewport stays inside its panel");
  if (v.lyricsEditor)
    v.onTextEditorTextChanged(*v.lyricsEditor);
  v.service();
  render(v, "lyrics");
  check(loadVoxLyrics(p) == "hello world\nwe sing",
        "Vox multiline lyrics commit");
  std::string longLyrics;
  for (int i = 0; i < 80; ++i)
    longLyrics += "hello world\n";
  v.setLyricsText(longLyrics);
  if (v.lyricsEditor)
    v.onTextEditorTextChanged(*v.lyricsEditor);
  render(v, "long-lyrics"); // Text layout is updated lazily on draw.
#if defined(__APPLE__)
  [[NSRunLoop currentRunLoop]
      runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.02]];
#endif
  check(v.lyricsView && v.lyricsView->getContainerSize().getHeight() > 370,
        "Vox long lyric sheet scrolls");
  v.setLyricsText("hello world\nwe sing");
  if (v.lyricsEditor)
    v.onTextEditorTextChanged(*v.lyricsEditor);
  v._showLyricGenerator = true;
  render(v, "lyric-generator");
  v._showLyricGenerator = false;
  click(v, center(v.pageButtonRect(0)));
  v.service();
  const auto bank = std::filesystem::path(S3G_TEST_REPO) /
                    "examples/voicebanks/s3g-demo-synthetic";
  check(v.importSource(bank, true), "Vox synthetic voicebank import");
  const auto loaded = std::atomic_load(&p.voicebank);
  check(loaded && !loaded->entries.empty(), "Vox bank aliases");
  if (loaded)
    for (const auto &entry : loaded->entries) {
      check(entry.audio && entry.audio->worldResynthesized,
            "Vox WORLD analysis retained");
      check(entry.startSample >= 0 && entry.endSample > entry.startSample,
            "Vox alias timing");
    }
  render(v, "voicebank-loaded");
  check(!v.importSource(bank / "missing.wav", false),
        "Vox bad import rejected");
  check(std::atomic_load(&p.voicebank) == loaded,
        "Vox failed import preserves source");
#endif
  audioProbe(p, v, e);
  set(p, v, e, output, -9.);
  e.balanced();
  Stream saved;
  check(stateSave(&p.plugin, &saved.output), "save state");
  set(p, v, e, output, -3.);
  check(stateLoad(&p.plugin, &saved.input), "fragmented state restore");
  for (int n = 0; n < 20; ++n)
    flush(p, v, e);
  check(near(value(p, output), -9.), "state restore publishes output");
  // Truncated streams must leave the current patch untouched.
  for (size_t size :
       {size_t(0), size_t(3), saved.bytes.size() / 2, saved.bytes.size() - 1}) {
    Stream bad;
    bad.bytes.assign(saved.bytes.begin(), saved.bytes.begin() + size);
    set(p, v, e, output, -6.);
    check(!stateLoad(&p.plugin, &bad.input), "truncated state rejected");
    check(near(value(p, output), -6.),
          "truncated state leaves parameters untouched");
  }
  const auto file =
      std::filesystem::temp_directory_path() /
      F::pathFromUtf8(("s3g-complex-préset-音-" +
                       std::to_string(D::randomBits()) + ".s3gpreset")
                          .c_str());
  check(v.presetFile(F::pathToUtf8(file), true), "Unicode preset save");
  set(p, v, e, output, -12.);
  check(v.presetFile(F::pathToUtf8(file), false), "Unicode preset recall");
  for (int n = 0; n < 30; ++n)
    flush(p, v, e);
  check(near(value(p, output), -12.), "GUI preset preserves output gain");
  e.balanced();
  std::filesystem::remove(file);
  check(!v.presetFile(F::pathToUtf8(file), false), "missing preset rejected");
  e.reject = true;
  set(p, v, e, output, -15.);
  e.reject = false;
  for (int n = 0; n < 20; ++n)
    flush(p, v, e);
  check(near(value(p, output), -15.), "host backpressure retry");
  e.balanced();
  v.stopRefresh();
  flush(p, v, e);
  e.balanced();
}
