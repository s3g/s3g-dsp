#include "routing_canvas_test_support.inc"
namespace D = s3g::portable_gui::decoder_drawing;
void firstOpenGeometryTests(const clap_host_t &host) {
#if S3G_DECODER_KIND == 1 || S3G_DECODER_KIND == 2
  const auto checkGeometry = [](Editor &view, const char *stage) {
    // Prepare the reference exactly as the original audio-side model was
    // prepared before Cocoa read it. Never switch layouts to prime the GUI.
    s3g::AmbiSpeakerDecoder reference;
    reference.prepare(48000.);
    reference.setParams(view.guiParams.decoder);
    const auto &actual = view.guiDecoder.fieldDecoder().speakers();
    const auto &expected = reference.speakers();
    const auto count = reference.params().activeSpeakers;
    bool match = count == 24 && view.guiParams.decoder.activeSpeakers == count;
    for (unsigned n = 0; n < count; ++n)
      match = match && near(actual[n].azimuthDeg, expected[n].azimuthDeg) &&
              near(actual[n].elevationDeg, expected[n].elevationDeg) &&
              near(actual[n].distance, expected[n].distance);
    if (!match)
      std::cerr << "Sphere 24 initial geometry: " << stage << '\n';
    expect(match,
           "first-open speaker coordinates match the prepared reference");
  };
  struct State {
    std::vector<uint8_t> bytes;
    size_t position = 0;
    clap_ostream_t out{this,
                       [](const clap_ostream_t *stream, const void *data,
                          uint64_t size) -> int64_t {
                         auto &bytes = static_cast<State *>(stream->ctx)->bytes;
                         const auto *first = static_cast<const uint8_t *>(data);
                         bytes.insert(bytes.end(), first, first + size);
                         return int64_t(size);
                       }};
    clap_istream_t in{
        this,
        [](const clap_istream_t *stream, void *data, uint64_t size) -> int64_t {
          auto &state = *static_cast<State *>(stream->ctx);
          size = std::min<uint64_t>(size, state.bytes.size() - state.position);
          std::memcpy(data, state.bytes.data() + state.position, size);
          state.position += size;
          return int64_t(size);
        }};
  } saved;
  for (bool recall : {false, true}) {
    const auto *plugin = factory.create_plugin(&factory, &host, descriptor.id);
    expect(plugin && plugin->init(plugin), "fresh geometry-test instance");
    if (!plugin)
      continue;
    if (recall)
      expect(stateLoad(plugin, &saved.in), "recall untouched default state");
    auto &p = *self(plugin);
    {
      Editor beforeActivation(p);
      checkGeometry(beforeActivation, recall ? "recalled before activation"
                                             : "fresh before activation");
      render(beforeActivation, recall ? "sphere24-recalled-before-activation"
                                      : "sphere24-fresh-before-activation");
    }
    const bool activated = plugin->activate(plugin, 48000., 1, 64);
    expect(activated, "geometry-test activation");
    for (unsigned opening = 0; opening < 2; ++opening) {
      Editor view(p);
      checkGeometry(view, opening ? "reopened editor" : "activated first open");
      for (unsigned frame = 0; frame < 3; ++frame)
        view.service();
      checkGeometry(view, "unchanged refreshes");
      render(view,
             std::string(recall ? "sphere24-recalled-" : "sphere24-fresh-") +
                 std::to_string(opening));
    }
    if (!recall)
      expect(stateSave(plugin, &saved.out), "save untouched default state");
    if (activated)
      plugin->deactivate(plugin);
    plugin->destroy(plugin);
  }
#else
  (void)host;
#endif
}
void slider(Editor &v, Plugin &p, Events &e, clap_id id, double x, double width,
            double y) {
  const auto info = v.info(id);
  render(v);
  down(v, {x + width * .2, y});
  move(v, {x + width * .8, y});
  up(v);
  flush(p, e);
  const auto expected =
      v.canonical(id, info.min_value + .8 * (info.max_value - info.min_value));
  expect(near(v.value(id), expected),
         "slider maps its original track to its parameter");
  render(v);
  click(v, {x + width * .5, y}, 2);
  flush(p, e);
  expect(near(v.value(id), info.default_value), "slider double-click default");
  e.balanced();
}
void menu(Editor &v, Plugin &p, Events &e, clap_id id, CPoint anchor,
          CPoint origin, unsigned count, int first = 0,
          const unsigned *mapping = nullptr) {
  for (unsigned i = 0; i < count; ++i) {
    render(v);
    click(v, anchor);
    expect(v._openMenu > 0, "custom menu opens");
    render(v);
    click(v, {origin.x + 8., origin.y + (i + .5) * 18.});
    flush(p, e);
    expect(near(v.value(id), mapping ? mapping[i] : i + first),
           "custom menu selection mapping");
    expect(v._openMenu == 0, "custom menu closes");
    e.balanced();
  }
}
void family(Editor &v, Plugin &p, Events &e) {
#if S3G_DECODER_KIND == 0
  const unsigned layouts[] = {0, 2, 3, 11, 8, 4, 5, 9, 12, 10, 1, 6, 13, 7};
  menu(v, p, e, kLayoutParamId, {760, 150}, {738, 162}, 14, 0, layouts);
  menu(v, p, e, kModeParamId, {760, 176}, {738, 188}, 4);
  menu(v, p, e, kOrderParamId, {760, 202}, {738, 214}, 7, 1);
  menu(v, p, e, kWeightingParamId, {760, 228}, {738, 240}, 3);
  menu(v, p, e, kCustomFieldParamId, {760, 254}, {738, 266}, 2);
  slider(v, p, e, kOutputParamId, 738, 82, 78);
  slider(v, p, e, kWidthParamId, 738, 82, 300);
  v.set(kActiveSpeakersParamId, 64);
  flush(p, e);
  expect(waitForSubmittedCommands(p, std::chrono::milliseconds(2000)),
         "speaker worker settles 64-channel layout before capture");
  for (unsigned page = 0; page < 4; ++page) {
    v._rightPage = 1;
    v._mixerPage = page;
    render(v, "mixer-" + std::to_string(page));
    const auto track =
        v.mixerSpeakerRect_inRect(page * 16, D::makeRect(34, 76, 564, 506));
    down(v, {D::midX(track), D::maxY(track) - track.size.height * .3});
    move(v, {D::midX(track), D::maxY(track) - track.size.height * .75});
    up(v);
    flush(p, e);
    expect(waitForSubmittedCommands(p, std::chrono::milliseconds(2000)),
           "speaker worker settles mixer edits");
    {
      auto snapshot = acquireDecoderSnapshot(p);
      const auto speakers = visibleDecoderSpeakers(p, snapshot.decoder(),
                                                   snapshot.runtimeMixer());
      if (!near(speakers[page * 16].gain, 1.5))
        std::cerr << "mixer page " << page << " gain "
                  << speakers[page * 16].gain << "\n";
      expect(near(speakers[page * 16].gain, 1.5),
             "mixer fader changes addressed speaker on every page");
    }
    v.toggleMixerSpeakerMute(page * 16);
    v.toggleMixerSpeakerSolo(page * 16);
    flush(p, e);
    {
      auto snapshot = acquireDecoderSnapshot(p);
      const auto speakers = visibleDecoderSpeakers(p, snapshot.decoder(),
                                                   snapshot.runtimeMixer());
      expect(!speakers[page * 16].enabled && speakers[page * 16].solo,
             "indexed mute and solo");
    }
    v.toggleMixerSpeakerMute(page * 16);
    v.toggleMixerSpeakerSolo(page * 16);
    flush(p, e);
    e.balanced();
  }
  v.set(kLayoutParamId, 5);
  v.set(kModeParamId, 3);
  flush(p, e);
  for (int page : {0, 2}) {
    v._rightPage = page;
    for (int view = 0; view < 3; ++view) {
      v.setViewPreset(view);
      render(v,
             "page-" + std::to_string(page) + "-view-" + std::to_string(view));
    }
  }
  v.set(kSelectedSpeakerParamId, 4);
  v.set(kAzimuthParamId, 37.5);
  v.set(kElevationParamId, -22.5);
  v.set(kDistanceParamId, 1.37);
  flush(p, e);
  expect(near(v.value(kAzimuthParamId), 37.5) &&
             near(v.value(kElevationParamId), -22.5) &&
             near(v.value(kDistanceParamId), 1.37),
         "exact selected coordinates");
  expect(near(v.value(kLayoutParamId), 0.), "coordinate edits select CUSTOM");
#elif S3G_DECODER_KIND == 1 || S3G_DECODER_KIND == 2
  menu(v, p, e, kLayoutParamId, {760, 150}, {738, 162}, 14);
  menu(v, p, e, kModeParamId, {760, 176}, {738, 188}, 3);
  menu(v, p, e, kOrderParamId, {760, 202}, {738, 214}, 7, 1);
  menu(v, p, e, kWeightingParamId, {760, 228}, {738, 240}, 3);
  slider(v, p, e, kOutputParamId, 738, 82, 78);
#if S3G_DECODER_KIND == 1
  menu(v, p, e, kObjectMethodParamId, {760, 294}, {738, 306}, 3);
  const clap_id ids[] = {kBlendParamId,          kFieldGainParamId,
                         kObjectGainParamId,     kObjectConfidenceParamId,
                         kObjectHighpassParamId, kDirectionSmoothingParamId};
  for (unsigned n = 0; n < 6; ++n)
    slider(v, p, e, ids[n], 738, 82, 314. + 26 * n);
#else
  const clap_id ids[] = {kFocusParamId,      kDiffuseParamId,
                         kConfidenceParamId, kTransientParamId,
                         kCrossoverParamId,  kSmoothingParamId};
  for (unsigned n = 0; n < 6; ++n)
    slider(v, p, e, ids[n], 738, 82, 288. + 26 * n);
#endif
  for (int mode = 0; mode < 3; ++mode) {
    v.setViewPreset(mode);
    render(v, "view-" + std::to_string(mode));
    expect(p.guiViewMode == mode, "camera state stored");
  }
  // Custom inherits the active count from the previously selected layout.
  v.set(kLayoutParamId, 10);
  v.set(kLayoutParamId, 0);
  flush(p, e);
  v.service();
  expect(v.guiParams.decoder.activeSpeakers == 4, "CUSTOM inherits Quad count");
  expect(p.decoder.params().decoder.activeSpeakers == 4,
         "audio decoder inherits CUSTOM count too");
#elif S3G_DECODER_KIND == 3
  const double offset = kContentTranslation;
  menu(v, p, e, kParamLayout, {760, 204 + offset}, {712, 221 + offset}, 5);
  menu(v, p, e, kParamOrder, {760, 226 + offset}, {712, 243 + offset}, 7, 1);
  menu(v, p, e, kParamWeighting, {760, 248 + offset}, {712, 265 + offset}, 4);
  menu(v, p, e, kParamAutogain, {760, 270 + offset}, {712, 287 + offset}, 3);
  menu(v, p, e, kParamMethod, {760, 344 + offset}, {712, 361 + offset}, 10);
  const clap_id ids[] = {kParamWidth,     kParamAngle,
                         kParamRotation,  kParamDirectivity,
                         kParamAbSpacing, kParamMicElevation};
  for (unsigned n = 0; n < 6; ++n)
    slider(v, p, e, ids[n], 712, 122, 366. + 22 * n + offset);
  const clap_id fieldIds[] = {kParamRearReject, kParamHeightFold, kParamDiffuse,
                              kParamBassMono};
  for (unsigned n = 0; n < 4; ++n)
    slider(v, p, e, fieldIds[n], 712, 122, 540. + 22 * n + offset);
  slider(v, p, e, kParamOutputGain, 712, 122, 70. + offset);
  for (unsigned method = 0; method < 10; ++method) {
    v.set(kParamMethod, method);
    flush(p, e);
    render(v, "pickup-" + std::to_string(method));
  }
#elif S3G_DECODER_KIND == 4
  const double offset = kContentTranslation;
  menu(v, p, e, kParamOrder, {760, 232 + offset}, {724, 249 + offset}, 7, 1);
  menu(v, p, e, kParamWeighting, {760, 254 + offset}, {724, 271 + offset}, 3);
  menu(v, p, e, kParamAutogain, {760, 276 + offset}, {724, 293 + offset}, 3);
  menu(v, p, e, kParamMode, {760, 354 + offset}, {724, 371 + offset}, 2);
  menu(v, p, e, kParamHead, {760, 376 + offset}, {724, 393 + offset}, 3);
  menu(v, p, e, kParamXtcMode, {760, 608 + offset}, {724, 625 + offset}, 2);
  const clap_id ids[] = {kParamOutput,       kParamPreserve,  kParamYaw,
                         kParamPitch,        kParamWidth,     kParamPinna,
                         kParamHeadWidth,    kParamRoom,      kParamXtcAmount,
                         kParamSpeakerAngle, kParamLowProtect};
  const double rows[] = {70, 92, 398, 420, 442, 464, 486, 564, 586, 630, 652};
  for (unsigned n = 0; n < std::size(ids); ++n)
    slider(v, p, e, ids[n], 724, 128, rows[n] + offset);
  for (unsigned item = 0; item < 6; ++item) {
    render(v);
    click(v, {760, 210 + offset});
    render(v);
    click(v, {732, 227 + offset + (item + .5) * 18.});
    flush(p, e);
    expect(near(v.value(kParamDecodeMode), item ? 1. : 0.),
           "combined field menu selects decode path");
    if (item)
      expect(near(v.value(kParamLayout), item - 1.),
             "combined field menu selects virtual layout");
  }
  for (int mode = 0; mode < 3; ++mode) {
    v.setViewPreset(mode);
    render(v, "view-" + std::to_string(mode));
  }
  v.set(kParamDecodeMode, 0);
  v.set(kParamMode, 0);
  flush(p, e);
  render(v, "direct-binaural");
  v.set(kParamDecodeMode, 1);
  v.set(kParamMode, 1);
  flush(p, e);
  render(v, "field-transaural");
  const auto before = v._viewZoom;
  MouseWheelEvent wheel;
  wheel.mousePosition = {250, 250};
  wheel.deltaY = 10;
  v.onMouseWheelEvent(wheel);
  expect(v._viewZoom > before, "head field wheel zoom");
#else
  menu(v, p, e, kOrderParamId, {410, 144}, {388, 161}, 8);
  slider(v, p, e, kOutputParamId, 388, 92, 78);
  slider(v, p, e, kSubCountParamId, 388, 92, 210);
  slider(v, p, e, kCutoffParamId, 388, 92, 236);
  slider(v, p, e, kWidthParamId, 388, 92, 262);
  click(v, {410, 288});
  flush(p, e);
  expect(near(v.value(kBypassParamId), 1.), "sub bypass toggles");
  click(v, {410, 288});
  flush(p, e);
  for (int count = 1; count <= 8; ++count) {
    v.set(kSubCountParamId, count);
    flush(p, e);
    render(v, "subs-" + std::to_string(count));
  }
#endif
  e.balanced();
  v.resetDecoder();
  flush(p, e);
  e.balanced();
}
void stateTests(Editor &v, Plugin &p, Events &e, const clap_host_t &host) {
  const auto root =
      std::filesystem::temp_directory_path() /
      ("s3g-decoder-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto path = F::pathToUtf8(root / "écho_波_Δ.s3gpreset");
  std::map<clap_id, double> saved;
  for (auto i : v.infos)
    saved[i.id] = v.value(i.id);
  expect(v.presetFile(path, true), "Unicode preset save");
  auto *recalled = factory.create_plugin(&factory, &host, descriptor.id);
  expect(recalled && recalled->init(recalled), "state target init");
  if (recalled) {
    expect(s3g::clap_gui::portable::loadStateFile(recalled, stateExt,
                                                  path.c_str()),
           "host recall before activation");
    for (auto i : v.infos) {
      double x = 0;
      expect(paramsGetValue(recalled, i.id, &x) && near(x, saved[i.id]),
             "host recall publishes parameter");
    }
    recalled->destroy(recalled);
  }
  v.set(S3G_TEST_OUTPUT_PARAM, -17.);
  flush(p, e);
  expect(v.presetFile(path, false), "Unicode preset recall");
  flush(p, e);
  e.balanced();
  for (auto i : v.infos)
    expect(
        near(v.value(i.id), i.id == S3G_TEST_OUTPUT_PARAM ? -17. : saved[i.id]),
        "preset preserves OUT and all saved controls");
  std::ofstream bad(F::pathFromUtf8(path.c_str()),
                    std::ios::binary | std::ios::trunc);
  bad << "broken";
  bad.close();
  expect(!v.presetFile(path, false), "truncated preset rejected");
  expect(near(v.value(S3G_TEST_OUTPUT_PARAM), -17.),
         "failed preset transactional");
  std::filesystem::remove_all(root);
}
void legacyStateTests(Plugin &p, Events &e) {
#if S3G_DECODER_KIND == 0 || S3G_DECODER_KIND == 1
  const auto loadLegacy = [&](const auto &saved) {
    struct Input {
      const void *data;
      size_t size;
      size_t pos = 0;
    } input{&saved, sizeof(saved)};
    clap_istream_t stream{
        &input,
        [](const clap_istream_t *stream, void *data, uint64_t size) -> int64_t {
          auto &in = *static_cast<Input *>(stream->ctx);
          size = std::min<uint64_t>({size, 7, in.size - in.pos});
          std::memcpy(data, static_cast<const uint8_t *>(in.data) + in.pos,
                      size);
          in.pos += size;
          return int64_t(size);
        }};
    expect(stateLoad(&p.plugin, &stream), "legacy chunked state loads");
    flush(p, e);
  };
#if S3G_DECODER_KIND == 0
  SavedStateV4 saved{};
  {
    auto snapshot = acquireDecoderSnapshot(p);
    saved.params =
        visibleDecoderParams(p, snapshot.decoder(), snapshot.runtimeMixer());
    saved.speakers =
        visibleDecoderSpeakers(p, snapshot.decoder(), snapshot.runtimeMixer());
  }
  saved.params.outputGainDb = -7.5f;
  saved.speakers[2].gain = .42f;
  loadLegacy(saved);
  double output = 0.;
  paramsGetValue(&p.plugin, kOutputParamId, &output);
  expect(near(output, -7.5), "v4 speaker output restored");
  expect(waitForSubmittedCommands(p, std::chrono::milliseconds(2000)),
         "legacy speaker worker settles");
  {
    auto snapshot = acquireDecoderSnapshot(p);
    const auto speakers =
        visibleDecoderSpeakers(p, snapshot.decoder(), snapshot.runtimeMixer());
    expect(near(speakers[2].gain, .42), "v4 indexed speaker gain restored");
  }
#else
  SavedStateV1 v1{};
  v1.params.objectBlend = .61f;
  v1.params.decoder.outputGainDb = -8.25f;
  loadLegacy(v1);
  auto current = snapshotDecoderParams(p);
  expect(near(current.objectBlend, .61) &&
             near(current.decoder.outputGainDb, -8.25),
         "v1 object parameters upgraded");
  expect(near(current.objectConfidence, DecoderParams{}.objectConfidence),
         "v1 supplies later confidence default");
  SavedStateV2 v2{};
  v2.params.objectBlend = .23f;
  v2.guiViewMode = 1;
  v2.guiViewAzDeg = 63.;
  v2.guiViewElDeg = -12.;
  v2.guiViewZoom = 1.4;
  loadLegacy(v2);
  current = snapshotDecoderParams(p);
  expect(near(current.objectBlend, .23) && p.guiViewMode == 1 &&
             near(p.guiViewAzDeg, 63.) && near(p.guiViewElDeg, -12.) &&
             near(p.guiViewZoom, 1.4),
         "v2 object controls and camera upgraded");
#endif
#else
  (void)p;
  (void)e;
#endif
}
void nativeFields(Plugin &p, Events &e) {
#if defined(__APPLE__) && S3G_DECODER_KIND == 0
  if (!std::getenv("S3G_DECODER_NATIVE_FIELDS"))
    return;
  NSWindow *window = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)
                styleMask:NSWindowStyleMaskTitled
                  backing:NSBackingStoreBuffered
                    defer:NO];
  {
    F::EditorHost host(kGuiWidth, kGuiHeight, kGuiWidth, kGuiHeight);
    auto *view = new Editor(p);
    expect(host.attach(view) && host.setParent([window contentView]) &&
               host.setVisible(true),
           "native speaker editor attaches");
    [window makeKeyAndOrderFront:nil];
    view->set(kSelectedSpeakerParamId, 3);
    flush(p, e);
    const auto type = [&](CPoint point, double value) {
      render(*view);
      click(*view, point);
      CTextEdit *edit = nullptr;
      for (int32_t n = 0; n < view->getFrame()->getNbViews(); ++n)
        if (auto *candidate =
                dynamic_cast<CTextEdit *>(view->getFrame()->getView(n)))
          edit = candidate;
      expect(edit != nullptr, "native numeric field opens");
      if (edit) {
        edit->setText(R::format("%.9g", value).c_str());
        view->valueChanged(edit);
        view->finishNumeric();
        flush(p, e);
      }
    };
    const double values[] = {72.5, -24.25, 1.73};
    const clap_id ids[] = {kAzimuthParamId, kElevationParamId,
                           kDistanceParamId};
    for (unsigned n = 0; n < 3; ++n) {
      type({835., 398. + 26 * n}, values[n]);
      expect(near(view->value(ids[n]), values[n]), "native exact AED commit");
    }
    view->set(kActiveSpeakersParamId, 64);
    flush(p, e);
    for (unsigned page = 0; page < 4; ++page) {
      view->_rightPage = 1;
      view->_mixerPage = page;
      const auto b =
          view->mixerGainFieldRect_inRect(3, D::makeRect(34, 76, 564, 506));
      type({D::midX(b), D::midY(b)}, .37 + page * .1);
      expect(waitForSubmittedCommands(p, std::chrono::milliseconds(2000)),
             "typed gain worker settles");
      auto snap = acquireDecoderSnapshot(p);
      auto speakers =
          visibleDecoderSpeakers(p, snap.decoder(), snap.runtimeMixer());
      expect(near(speakers[page * 16 + 3].gain, .37 + page * .1),
             "native exact gain targets page speaker");
    }
    e.balanced();
    host.setVisible(false);
  }
  [window orderOut:nil];
  [window release];
#else
  (void)p;
  (void)e;
#endif
}
void audio(Editor &v, Plugin &p, Events &events) {
  constexpr unsigned count = 64, frames = 64;
  std::array<std::array<float, frames>, count> in{}, out{};
  std::array<float *, count> ip{}, op{};
  for (unsigned c = 0; c < count; ++c) {
    ip[c] = in[c].data();
    op[c] = out[c].data();
    for (unsigned f = 0; f < frames; ++f)
      in[c][f] = float(.01 * std::sin(f * .13 + c * .2));
  }
  clap_audio_buffer_t ib{}, ob{};
  ib.channel_count = ob.channel_count = count;
  ib.data32 = ip.data();
  ob.data32 = op.data();
  clap_process_t block{};
  block.frames_count = frames;
  block.audio_inputs = &ib;
  block.audio_outputs = &ob;
  block.audio_inputs_count = block.audio_outputs_count = 1;
  block.out_events = &events.out;
  for (unsigned n = 0; n < 32; ++n)
    expect(process(&p.plugin, &block) == CLAP_PROCESS_CONTINUE,
           "float audio callback");
  double energy = 0;
  for (auto &ch : out)
    for (auto x : ch) {
      expect(std::isfinite(x), "finite audio");
      energy += std::abs(x);
    }
  expect(energy > 0, "non-silent decoder");
  v.set(S3G_TEST_OUTPUT_PARAM, -12.);
  clap_event_param_value_t automation{};
  automation.header = {sizeof(automation), 0, CLAP_CORE_EVENT_SPACE_ID,
                       CLAP_EVENT_PARAM_VALUE, 0};
  automation.param_id = S3G_TEST_OUTPUT_PARAM;
  automation.value = -3.;
  clap_input_events_t input{
      &automation, [](const clap_input_events_t *) -> uint32_t { return 1; },
      [](const clap_input_events_t *e,
         uint32_t) -> const clap_event_header_t * {
        return &static_cast<clap_event_param_value_t *>(e->ctx)->header;
      }};
  block.in_events = &input;
  events.limit = 0;
  process(&p.plugin, &block);
  block.in_events = nullptr;
  events.limit = 100000;
  process(&p.plugin, &block);
  expect(near(v.value(S3G_TEST_OUTPUT_PARAM), -3.),
         "host automation wins over delayed GUI notification");
  events.balanced();
}
} // namespace test
int main() {
  using namespace test;
#if defined(__APPLE__)
  [NSApplication sharedApplication];
#endif
  if (!F::acquireRuntime())
    return 2;
  clap_host_t host{};
  host.clap_version = CLAP_VERSION_INIT;
  host.name = "Decoder parity";
  host.vendor = "s3g";
  host.version = "1";
  host.url = "";
  host.get_extension = [](const clap_host_t *, const char *) -> const void * {
    return nullptr;
  };
  host.request_process = host.request_callback =
      host.request_restart = [](const clap_host_t *) {};
  firstOpenGeometryTests(host);
  auto *plugin = factory.create_plugin(&factory, &host, descriptor.id);
  expect(plugin && plugin->init(plugin) &&
             plugin->activate(plugin, 48000., 1, 64),
         "decoder lifecycle");
  if (plugin) {
    {
      auto &p = *self(plugin);
      Editor v(p);
      Events e;
      render(v, "initial");
      parameters(v, p, e);
      family(v, p, e);
      audio(v, p, e);
      stateTests(v, p, e, host);
      render(v, "restored");
      legacyStateTests(p, e);
      nativeFields(p, e);
    }
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
  }
  F::releaseRuntime();
  if (ok)
    std::cout << descriptor.id << " decoder canvas parity passed\n";
  return ok ? 0 : 1;
}
