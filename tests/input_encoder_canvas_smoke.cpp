#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#endif
#include "routing_canvas_test_support.inc"

namespace D = s3g::portable_gui::input_encoder_drawing;
using namespace test;
struct State {
  std::vector<uint8_t> bytes;
  size_t pos = 0;
  template <class T> void append(const T &value) {
    const auto *data = reinterpret_cast<const uint8_t *>(&value);
    bytes.insert(bytes.end(), data, data + sizeof(value));
  }
  clap_ostream_t out{
      this,
      [](const clap_ostream_t *s, const void *data, uint64_t n) -> int64_t {
        auto &st = *static_cast<State *>(s->ctx);
        n = std::min<uint64_t>(n, 13);
        const auto *b = static_cast<const uint8_t *>(data);
        st.bytes.insert(st.bytes.end(), b, b + n);
        return int64_t(n);
      }};
  clap_istream_t in{
      this, [](const clap_istream_t *s, void *data, uint64_t n) -> int64_t {
        auto &st = *static_cast<State *>(s->ctx);
        n = std::min<uint64_t>({n, 17, st.bytes.size() - st.pos});
        std::memcpy(data, st.bytes.data() + st.pos, n);
        st.pos += n;
        return int64_t(n);
      }};
};
void stateChecks(Editor &v, Plugin &p, Events &e) {
  presets(v, p, e);
  State saved;
  expect(stateSave(&p.plugin, &saved.out), "chunked state save");
  // Give a truncated state's header different values from the live instance;
  // otherwise a partial mutation could look like a successful rollback.
  v.set(S3G_TEST_OUTPUT_PARAM, -23.);
  flush(p, e);
  std::vector<double> before;
  for (auto i : v.infos)
    before.push_back(v.value(i.id));
  for (size_t len :
       {size_t(0), size_t(3), saved.bytes.size() / 2, saved.bytes.size() - 1}) {
    State bad;
    bad.bytes.assign(saved.bytes.begin(), saved.bytes.begin() + len);
    expect(!stateLoad(&p.plugin, &bad.in),
           "truncated state rejected transactionally");
    for (size_t i = 0; i < before.size(); ++i)
      expect(near(before[i], v.value(v.infos[i].id)),
             "failed load preserves every parameter");
  }
  expect(stateLoad(&p.plugin, &saved.in), "complete chunked state load");
  v.service();

  State invalid;
  invalid.bytes = saved.bytes;
  const float nan = std::numeric_limits<float>::quiet_NaN();
#if S3G_INPUT_ENCODER_KIND < 2
  const size_t outputOffset =
      offsetof(SavedState, params) +
      offsetof(decltype(SavedState::params), outputGainDb);
#else
  const size_t outputOffset = offsetof(SavedStateHeader, params) +
                              offsetof(EncoderParams, outputGainDb);
#endif
  std::memcpy(invalid.bytes.data() + outputOffset, &nan, sizeof(nan));
  const auto outputBefore = v.value(S3G_TEST_OUTPUT_PARAM);
  expect(!stateLoad(&p.plugin, &invalid.in),
         "non-finite binary state rejected");
  expect(near(v.value(S3G_TEST_OUTPUT_PARAM), outputBefore),
         "invalid state leaves output untouched");

  // Every retained Cocoa binary format must still load. Empty JSON must
  // restore the built-in room(s), including the original far-room character.
#if S3G_INPUT_ENCODER_KIND < 2
  SavedStateV3 old{};
  old.version = 3;
  old.params = snapshotEncoderParams(p);
#if S3G_INPUT_ENCODER_KIND == 0
  old.clouds = snapshotEncoderClouds(p);
#else
  old.paths = p.guiPaths;
#endif
  old.params.outputGainDb = -19.;
  State legacy;
  legacy.append(old);
  expect(stateLoad(&p.plugin, &legacy.in) &&
             near(v.value(S3G_TEST_OUTPUT_PARAM), -19.),
         "Cocoa v3 state compatibility");
#elif S3G_INPUT_ENCODER_KIND == 2
  auto legacyRay = [&](uint32_t version, auto params) {
    params.outputGainDb = -19.;
    State legacy;
    legacy.append(kStateMagic);
    legacy.append(version);
    legacy.append(params);
    legacy.append(uint32_t(0));
    expect(stateLoad(&p.plugin, &legacy.in) &&
               near(v.value(S3G_TEST_OUTPUT_PARAM), -19.),
           "legacy Ray state compatibility");
    expect(p.rayJson.empty() && p.descriptor.cells.size() == 8,
           "empty legacy Ray field restores built-in room");
  };
  legacyRay(1, SavedAmbiRayEncoderParamsV1{});
  legacyRay(2, SavedAmbiRayEncoderParamsV2{});
  legacyRay(3, SavedAmbiRayEncoderParamsV3{});
#else
  SavedAmbiRayBilocationParamsV1 old{};
  old.outputGainDb = -19.;
  State legacy;
  legacy.append(kStateMagic);
  legacy.append(uint32_t(1));
  legacy.append(old);
  legacy.append(uint32_t(0));
  legacy.append(uint32_t(0));
  expect(stateLoad(&p.plugin, &legacy.in) &&
             near(v.value(S3G_TEST_OUTPUT_PARAM), -19.),
         "Cocoa Bilocation v1 state compatibility");
  expect(p.fieldA.json.empty() && p.fieldB.json.empty(),
         "empty legacy Bilocation state restores built-in pair");
  expect(near(p.fieldB.descriptor.cells[0].late.decaySeconds,
              builtinField(true).descriptor.cells[0].late.decaySeconds),
         "far built-in room keeps its original decay");
#endif
  saved.pos = 0;
  expect(stateLoad(&p.plugin, &saved.in),
         "restore current embedded state after legacy checks");
  v.service();
  e.balanced();
}
void family(Editor &v, Plugin &p, Events &e) {
#if S3G_INPUT_ENCODER_KIND == 0
  expect(v.guiEncoder.clouds()[0].distance > 0,
         "cloud field exists before activation");
  v.set(kCloudsParamId, 4);
  flush(p, e);
  for (unsigned c = 0; c < 4; ++c) {
    v.set(kCloudParamId, c + 1);
    v.set(kAzimuthParamId, -120. + c * 62.);
    v.set(kElevationParamId, -30. + c * 20.);
    v.set(kDistanceParamId, .8 + c * .3);
    v.set(kCloudGainParamId, .5 + c * .3);
    flush(p, e);
    v.service();
    expect(near(v.value(perCloudParamId(c, CloudParamKind::Azimuth)),
                -120. + c * 62.),
           "selected and per-cloud azimuth aliases agree");
  }
  for (unsigned c = 0; c < 4; ++c) {
    v.set(kCloudParamId, c + 1);
    flush(p, e);
    v.service();
    expect(near(v.value(kCloudGainParamId), .5 + c * .3),
           "cloud gain survives changing selection");
  }
  for (int camera = 0; camera < 3; ++camera) {
    v.setViewPreset(camera);
    render(v, "camera-" + std::to_string(camera));
  }
  for (unsigned shape = 0; shape < 5; ++shape) {
    click(v, {750, 380});
    expect(v._openMenu == 4, "Cloud shape custom menu opens");
    render(v, "shape-menu");
    click(v, {750, v.menuY() + (shape + .5) * 18});
    flush(p, e);
    expect(near(v.value(kShapeParamId), shape),
           "all original cloud shapes selectable");
    v.set(kForceParamId, shape);
    v.set(kDriftParamId, .6);
    v.set(kJitterParamId, .25);
    flush(p, e);
    render(v, "shape-force-" + std::to_string(shape));
  }
  auto before = snapshotEncoderClouds(p);
  expect(activate(&p.plugin, 48000., 1, 64), "cloud activation");
  auto after = p.encoder.clouds();
  for (unsigned c = 0; c < 4; ++c)
    expect(near(before[c].azimuthDeg, after[c].azimuthDeg) &&
               near(before[c].gain, after[c].gain),
           "activation preserves all cloud geometry and gains");
  deactivate(&p.plugin);
  e.balanced();
#elif S3G_INPUT_ENCODER_KIND == 1
  expect(v.guiEncoder.paths()[0].pointCount == 8,
         "untouched path geometry exists before activation");
  v._editMode = true;
  v.setViewPreset(0);
  v.service();
  const auto count = v.guiEncoder.paths()[0].pointCount;
  v.addPointAtScreen(D::makePoint(390, 320));
  expect(v.guiEncoder.paths()[0].pointCount == count + 1, "add path point");
  auto point = v.guiEncoder.paths()[0].points[count];
  v.setViewPreset(1);
  v.updateSelectedPointFromScreen(D::makePoint(420, 260));
  auto moved = v.guiEncoder.paths()[0].points[count];
  expect(!near(point.z, moved.z), "side-view drag changes height");
  VSTGUI::KeyboardEvent del;
  del.virt = VSTGUI::VirtualKey::Delete;
  v.onKeyboardEvent(del);
  expect(v.guiEncoder.paths()[0].pointCount == count,
         "Delete removes selected point");
  v.clearSelectedPath();
  expect(v.guiEncoder.paths()[0].pointCount == 0, "clear path");
  v.set(kPathsParamId, 4);
  v.set(kInputsParamId, 18);
  v.set(kPhaseSpreadParamId, .92);
  v.set(kRateParamId, .15);
  flush(p, e);
  v.service();
  v.loadDocumentationPaths();
  for (int camera = 0; camera < 3; ++camera) {
    v.setViewPreset(camera);
    render(v, "documentation-camera-" + std::to_string(camera));
  }
  const auto paths = v.guiEncoder.paths();
  const auto json = v.pathJsonText();
  v.randomizePaths();
  expect(v.loadPathText(json), "canonical path JSON reload");
  for (unsigned pi = 0; pi < 4; ++pi)
    for (unsigned i = 0; i < paths[pi].pointCount; ++i) {
      const auto &a = paths[pi].points[i];
      const auto &b = v.guiEncoder.paths()[pi].points[i];
      expect(near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z) &&
                 near(a.time, b.time),
             "JSON retains original XYZ and timing");
    }
  expect(!v.loadPathText("{\"paths\":[{\"points\":null}]}"),
         "invalid path JSON rejected");
  expect(v.importSvgText("<svg><polyline points='0,0 100,100 200,0'/><PATH "
                         "d=\"M 0 100 L 100 0 L 200 100\"/></svg>"),
         "original polyline/path SVG subset imports");
  expect(v.guiEncoder.paths()[0].pointCount == 3 &&
             near(v.guiEncoder.paths()[0].points[0].x, -1.),
         "SVG numeric normalization retained");
  expect(v.importSvgText(v.pathSvgText()), "SVG export reimports");
  expect(v.loadPathText(json), "restore documentation JSON");
  flush(p, e);
  e.balanced();
  expect(activate(&p.plugin, 48000., 1, 64), "path activation");
  expect(near(p.encoder.paths()[0].points[0].x, paths[0].points[0].x),
         "activation does not erase imported paths");
  deactivate(&p.plugin);
#elif S3G_INPUT_ENCODER_KIND == 2
  expect(guiSnapshot(p).cells.size() == 8,
         "Ray built-in cells on untouched first open");
  for (unsigned i = 0; i < input_encoder_canvas::kRayAtlas.size(); ++i) {
    v.loadAtlasAtIndex(i);
    expect(v.error.empty(), "every bundled Ray Atlas loads");
    flush(p, e);
    e.balanced();
    auto field = guiSnapshot(p);
    expect(!field.cells.empty() && !field.room.polygon.empty(),
           "Atlas field and geometry populated");
    render(v, "atlas-" + std::to_string(i));
  }
  render(v);
  click(v, {890, 137});
  expect(v._atlasMenuOpen, "custom Atlas menu opens");
  render(v, "atlas-menu");
  click(v, {740, 164});
  flush(p, e);
  auto snap = guiSnapshot(p);
  auto src = input_encoder_canvas::projectFieldPosition(snap, snap.source, 1);
  down(v, {src.x, src.y + kContentTranslation});
  move(v, {src.x + 30, src.y + 20 + kContentTranslation});
  up(v);
  flush(p, e);
  expect(!near(snap.source.x, guiSnapshot(p).source.x),
         "top-map source drag updates position");
  v._editListener = true;
  auto listener = input_encoder_canvas::projectFieldPosition(
      guiSnapshot(p), guiSnapshot(p).listener, 2);
  down(v, {listener.x, listener.y + kContentTranslation});
  move(v, {listener.x + 12, listener.y - 12 + kContentTranslation});
  up(v);
  flush(p, e);
  for (unsigned mode = 0; mode < 4; ++mode) {
    v.set(kParamFieldListen, mode);
    flush(p, e);
    render(v, "listen-" + std::to_string(mode));
  }
  const auto before = p.rayJson;
  expect(!v.loadRayText("{}", "INVALID") && p.rayJson == before,
         "bad field leaves loaded room untouched");
  v.error.clear();
  e.balanced();
#else
  expect(input_encoder_canvas::guiSnapshot(p).fieldA.cells.size() == 8,
         "Bilocation built-in twin cells on first open");
  for (unsigned i = 0; i < kPairPresets.size(); ++i) {
    v.loadPairAtIndex(i);
    expect(v.error.empty(), "every curated contrast pair loads");
    flush(p, e);
    e.balanced();
    expect(p.selectedPair == int(i) && !p.fieldA.json.empty() &&
               !p.fieldB.json.empty(),
           "pair contains both embedded fields");
    expect(near(v.value(kParamPermeability), kPairPresets[i].permeability) &&
               near(v.value(kParamSizeB), kPairPresets[i].sizeB),
           "curated pair controls retained");
    render(v, "pair-" + std::to_string(i));
  }
  for (unsigned mode = 0; mode < 4; ++mode) {
    v.set(kParamMapMode, mode);
    v.set(kParamPlace, .2 + mode * .2);
    flush(p, e);
    render(v, "mapping-" + std::to_string(mode));
  }
  auto snap = input_encoder_canvas::guiSnapshot(p);
  const auto field = input_encoder_canvas::fieldARect();
  const auto plot = input_encoder_canvas::fieldPlanPlotRect(field);
  const auto source = input_encoder_canvas::projectPlanPosition(
      snap.fieldA, plot, snap.fieldA.source);
  down(v, {source.x, source.y + kContentTranslation});
  move(v, {source.x + 20, source.y + 10 + kContentTranslation});
  up(v);
  flush(p, e);
  expect(!near(snap.params.sourceX, v.value(kParamSourceX)),
         "Bilocation map drag changes shared source");
  const auto before = p.fieldB.json;
  expect(v.loadFieldText(p.fieldA.json, "IMPORTED A", 1) &&
             p.fieldB.json == before,
         "independent LOAD A preserves B");
  flush(p, e);
  e.balanced();
  expect(!v.loadFieldText("{}", "BAD", 2) && p.fieldB.json == before,
         "invalid B leaves both worlds intact");
  v.error.clear();
#endif
}
void audioChecks(Editor &v, Plugin &p, Events &e) {
  expect(activate(&p.plugin, 48000., 1, 64), "audio activation");
  startProcessing(&p.plugin);
  std::array<std::array<float, 64>, 64> in{}, out{};
  std::array<float *, 64> ip{}, op{};
  for (unsigned c = 0; c < 64; ++c) {
    ip[c] = in[c].data();
    op[c] = out[c].data();
    for (unsigned i = 0; i < 64; ++i)
      in[c][i] = .01f * std::sin(float(i + c) * .09f);
  }
  clap_audio_buffer_t ib{}, ob{};
  ib.data32 = ip.data();
  ob.data32 = op.data();
  ib.channel_count = kInputChannels;
  ob.channel_count = kOutputChannels;
  clap_process_t block{};
  block.frames_count = 64;
  block.audio_inputs = &ib;
  block.audio_outputs = &ob;
  block.audio_inputs_count = block.audio_outputs_count = 1;
  block.out_events = &e.out;
  double energy = 0;
  for (unsigned i = 0; i < 64; ++i) {
    process(&p.plugin, &block);
    for (const auto &ch : out)
      for (float x : ch) {
        expect(std::isfinite(x), "finite audio");
        energy += std::abs(x);
      }
  }
  expect(energy > 0, "non-silent audio");
  v.service();
#if S3G_INPUT_ENCODER_KIND >= 2
  expect(tailGet(&p.plugin) == p.activeProcessor.load()->tailFrames(),
         "race-free tail query retains original DSP duration");
#endif
#if S3G_INPUT_ENCODER_KIND < 2
  for (unsigned i = 0; i < kInputChannels; ++i) {
    const auto a = v.sourcePositions[i],
               b = p.encoder.sourcePositionForDisplay(i);
    expect(near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z),
           "moving GUI source snapshot equals DSP display positions");
  }
#endif
  render(v, "playback");
  e.balanced();
  std::atomic<bool> stop{false}, finite{true};
  std::atomic<unsigned> blocks{0};
  std::thread audio([&] {
    while (!stop.load()) {
      process(&p.plugin, &block);
      for (const auto &ch : out)
        for (float x : ch)
          if (!std::isfinite(x))
            finite.store(false);
      ++blocks;
    }
  });
  while (blocks.load() == 0)
    std::this_thread::yield();
  for (unsigned i = 0; i < 160; ++i) {
    v.set(S3G_TEST_OUTPUT_PARAM, -18. + double(i % 12));
#if S3G_INPUT_ENCODER_KIND == 0
    v.set(kAzimuthParamId, -120. + double(i));
#elif S3G_INPUT_ENCODER_KIND == 1
    v.set(kRateParamId, .01 + double(i) * .001);
    if (i % 16 == 0)
      v.randomizePaths();
#else
    v.set(kParamSourceX, .2 + double(i % 60) * .01);
    expect(tailGet(&p.plugin) > 0, "tail can be queried during playback");
#endif
    v.service();
    if (i % 40 == 0)
      render(v, "concurrent-playback");
  }
  stop.store(true);
  audio.join();
  process(&p.plugin, &block);
  expect(finite.load() && blocks.load() > 0,
         "concurrent GUI edits retain finite audio");
  e.balanced();
  stopProcessing(&p.plugin);
  deactivate(&p.plugin);
}
} // namespace test (opened by routing_canvas_test_support.inc)
int main() {
  using namespace test;
#if defined(__APPLE__)
  [NSApplication sharedApplication];
#endif
  if (!F::acquireRuntime())
    return 2;
  clap_host_t host{};
  host.clap_version = CLAP_VERSION_INIT;
  host.name = "Input encoder parity";
  host.vendor = "s3g";
  host.version = "1";
  host.url = "";
  host.get_extension = [](const clap_host_t *, const char *) -> const void * {
    return nullptr;
  };
  host.request_process = host.request_callback =
      host.request_restart = [](const clap_host_t *) {};
  auto *plugin = factory.create_plugin(&factory, &host, descriptor.id);
  expect(plugin && plugin->init(plugin), "plugin initializes");
  if (plugin) {
    auto &p = *self(plugin);
    {
      Editor v(p);
      Events e;
      render(v, "first-open-before-activation");
      family(v, p, e);
      stateChecks(v, p, e);
      audioChecks(v, p, e);
      parameters(v, p, e);
      render(v, "all-controls-tested");
    }
    plugin->destroy(plugin);
  }
  F::releaseRuntime();
  if (ok)
    std::cout << descriptor.id << " input encoder parity passed\n";
  return ok ? 0 : 1;
}
