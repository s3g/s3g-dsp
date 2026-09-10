// Real canvas input -> CLAP gestures -> DSP, with original Cocoa geometry as
// the reference. No stand-in UI/controller or synthetic parameter model.
#include S3G_TEST_TOPOLOGY_SOURCE
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/platform/platformfactory.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>

namespace test {
using namespace VSTGUI;
using topology_canvas::Editor;
namespace foundation = s3g::portable_gui::foundation;
bool ok = true;
void expect(bool v, const std::string &why) {
  if (!v) {
    ok = false;
    std::cerr << why << '\n';
  }
}
bool near(double a, double b) {
  return std::abs(a - b) <= 1e-5 * std::max(1., std::abs(b));
}
struct Events {
  std::vector<s3g::clap_gui::ParamEvent> values;
  bool accept = true;
  clap_output_events_t out{
      this, [](const clap_output_events_t *out, const clap_event_header_t *e) {
        auto &self = *static_cast<Events *>(out->ctx);
        if (!self.accept)
          return false;
        using Kind = s3g::clap_gui::ParamEventKind;
        if (e->type == CLAP_EVENT_PARAM_VALUE) {
          auto *p = reinterpret_cast<const clap_event_param_value_t *>(e);
          self.values.push_back({Kind::Value, p->param_id, p->value});
        } else {
          auto *p = reinterpret_cast<const clap_event_param_gesture_t *>(e);
          self.values.push_back({e->type == CLAP_EVENT_PARAM_GESTURE_BEGIN
                                     ? Kind::GestureBegin
                                     : Kind::GestureEnd,
                                 p->param_id, 0.});
        }
        return true;
      }};
  void balanced() {
    std::map<clap_id, int> active;
    for (auto e : values) {
      using Kind = s3g::clap_gui::ParamEventKind;
      if (e.kind == Kind::GestureBegin)
        expect(++active[e.paramId] == 1, "nested gesture");
      else if (e.kind == Kind::GestureEnd)
        expect(--active[e.paramId] == 0, "unbalanced END");
      else
        expect(active[e.paramId] == 1, "value outside gesture");
    }
    for (auto pair : active)
      expect(pair.second == 0, "unterminated gesture");
  }
};
void flush(Plugin &p, Events &events) {
  paramsFlush(&p.plugin, nullptr, &events.out);
}
void down(Editor &v, CPoint point, int count = 1) {
  MouseDownEvent e;
  e.mousePosition = point;
  e.clickCount = count;
  e.buttonState.add(MouseButton::Left);
  v.onMouseDownEvent(e);
}
void move(Editor &v, CPoint point) {
  MouseMoveEvent e;
  e.mousePosition = point;
  e.buttonState.add(MouseButton::Left);
  v.onMouseMoveEvent(e);
}
void up(Editor &v) {
  MouseUpEvent e;
  v.onMouseUpEvent(e);
}
void click(Editor &v, CPoint point, int count = 1) {
  down(v, point, count);
  up(v);
}
std::filesystem::path captures() {
  const auto path = std::getenv("S3G_TOPOLOGY_CAPTURE_DIR");
  return path ? foundation::pathFromUtf8(path) : std::filesystem::path{};
}
std::vector<uint8_t> render(Editor &v, const std::string &name = "") {
  auto context = COffscreenContext::create({kGuiWidth, kGuiHeight});
  expect(bool(context), "offscreen context");
  if (!context)
    return {};
  context->beginDraw();
  v.draw(context);
  context->endDraw();
  auto bytes = getPlatformFactory().createBitmapMemoryPNGRepresentation(
      context->getBitmap()->getPlatformBitmap());
  if (!captures().empty() && !name.empty()) {
    std::filesystem::create_directories(captures());
    std::ofstream f(captures() /
                        (std::string(descriptor.id) + "-" + name + ".png"),
                    std::ios::binary);
    f.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    expect(bool(f), "write capture");
  }
  return bytes;
}
void cocoa(Plugin &p, const std::string &name) {
#if defined(__APPLE__)
  if (captures().empty())
    return;
  auto *view = [[S3G_TEST_TOPOLOGY_COCOA_VIEW alloc] initWithPlugin:&p];
  const auto path = foundation::pathToUtf8(
      captures() / (std::string(descriptor.id) + "-cocoa-" + name + ".pdf"));
  expect([[view dataWithPDFInsideRect:[view bounds]]
             writeToFile:[NSString stringWithUTF8String:path.c_str()]
              atomically:YES],
         "Cocoa capture");
  [view release];
#endif
}

struct Control {
  clap_id id;
  double x, y;
};
std::vector<Control> controls() {
  std::vector<Control> result{{topology_canvas::outputId, 752., 78.},
                              {kMixParamId, 752., 104.}};
  const auto add = [&](const std::vector<clap_id> &ids, double x, double y) {
    for (size_t i = 0; i < ids.size(); ++i)
      result.push_back({ids[i], x, y + i * 26.});
  };
#if defined(S3G_TOPOLOGY_DELAY_PORT)
  add({1, 2, 4, 14, 16, 18}, 752., 170.);
  add({26, 27, 28, 29}, 1108., 534.);
#elif defined(S3G_TOPOLOGY_WAVE_GEOMETRY_PORT)
  add({1, 2, 3, 4, 5, 6, 7, 8, 13, 14, 9, 15, 16}, 752., 170.);
  add({17, 18, 19, 20}, 1108., 534.);
#else
  add({1, 2, 3, 4, 5, 6, 7, 8, 13, 14, 9, 15, 16, 17}, 752., 170.);
  add({18, 19, 20}, 1108., 534.);
#endif
  for (uint32_t i = 0; i < 16; ++i)
    if (i != 0 && i != 9 && i != 10 && i != 13)
      result.push_back({topology_canvas::topologyIds[i], 1108., 78. + i * 26.});
  return result;
}
void allSliders(Editor &v, Plugin &p, Events &events) {
  for (auto c : controls()) {
    const auto param = v.info(c.id);
    for (double n : {0., .37, 1.}) {
      double lo = param.min_value, hi = param.max_value;
#if defined(S3G_TOPOLOGY_SPECTRAL_TOPOLOGY_PORT)
      // Native LO/HI tracks span 24k but clamp against each other.
      if (c.id == kLoFreqParamId) {
        v.set(kHiFreqParamId, 24000.);
        hi = 24000.;
      }
      if (c.id == kHiFreqParamId) {
        v.set(kLoFreqParamId, 0.);
        lo = 20.;
        hi = 24000.;
      }
#endif
      render(v);
      down(v, {c.x + 37.5, c.y});
      move(v, {c.x + 150. * n, c.y});
      up(v);
      flush(p, events);
      double expected = lo + (hi - lo) * n;
#if defined(S3G_TOPOLOGY_SPECTRAL_TOPOLOGY_PORT)
      if (c.id == kLoFreqParamId)
        expected = std::min(expected, 23980.);
#endif
      expect(near(v.value(c.id), expected),
             "slider " + std::to_string(c.id) + " n=" + std::to_string(n));
    }
    render(v);
    click(v, {c.x + 75., c.y}, 2);
    flush(p, events);
    expect(near(v.value(c.id), param.default_value),
           "double-click default " + std::to_string(c.id));
  }
}
void menus(Editor &v, Plugin &p, Events &events) {
  for (uint32_t row : {0u, 9u, 10u, 13u}) {
    const auto id = topology_canvas::topologyIds[row];
    const auto param = v.info(id);
    const int first = int(param.min_value),
              count = int(param.max_value) - first + 1;
    const double y = 78. + row * 26.,
                 top = std::clamp(y - 2., 36.,
                                  double(kGuiHeight) - 10. - count * 18.);
    for (int i = 0; i < count; ++i) {
      render(v);
      click(v, {1140., y});
      const auto before = render(v);
      move(v, {1120., top + i * 18. + 9.});
      expect(render(v, "menu-" + std::to_string(row)) != before,
             "popup hover feedback");
      click(v, {1120., top + i * 18. + 9.});
      flush(p, events);
      expect(v.value(id) == i + first,
             "menu item " + std::to_string(id) + ":" + std::to_string(i));
    }
    render(v);
    click(v, {1140., y});
    render(v);
    click(v, {1., 1.});
    expect(v.value(id) == count - 1 + first,
           "outside dismiss does not change value");
  }
}
void matrix(Editor &v, Plugin &p) {
#if defined(S3G_TOPOLOGY_DELAY_PORT)
  constexpr double panelY = 330.;
#elif defined(S3G_TOPOLOGY_WAVE_GEOMETRY_PORT)
  constexpr double panelY = 512.;
#else
  constexpr double panelY = 538.;
#endif
  const double left = kChannelCount > 8 ? 686. : 718.,
               cell = kChannelCount > 8 ? 12. : 24.;
  const double top = panelY + (kChannelCount > 8 ? 34. : 42.);
  // Every physical row/column, including the formerly clipped 24th row.
  expect(top + kChannelCount * cell + 33. <= kGuiHeight,
         "complete matrix and footer fit");
  render(v);
  for (uint32_t i = 0; i < kChannelCount; ++i)
    for (uint32_t j = 0; j < kChannelCount; ++j) {
      const auto old = v.patchRow(i);
      click(v, {left + j * cell + cell * .5, top + i * cell + cell * .5});
      expect(v.patchRow(i) == (old ^ (uint64_t{1} << j)), "matrix cell toggle");
      click(v, {left + j * cell + cell * .5, top + i * cell + cell * .5});
      expect(v.patchRow(i) == old, "matrix cell restore");
    }
#if defined(S3G_TOPOLOGY_DELAY_PORT)
  expect(p.clearUnused == !kLockUnusedChannelsToPassThrough,
         "Delay unused-channel rule");
#endif
}
void graph(Editor &v, Plugin &p, Events &events) {
  v.resetPreset();
  flush(p, events);
  render(v, "idle");
  cocoa(p, "idle");
  {
    Editor win(p, foundation::FontMetrics{11., 11.5, 8., 7.});
    render(win, "windows-font-size");
  }
  auto original = render(v);
  click(v, {500., 52.5});
  expect(v.cameraView == 0 && near(v.pitch, .95), "TOP camera");
  expect(render(v) != original, "camera changes graph");
  click(v, {548., 52.5});
  expect(v.cameraView == 1 && near(v.pitch, 0.), "SIDE camera");
  render(v);
  click(v, {596., 52.5});
  expect(v.cameraView == 2 && near(v.yaw, -.52), "3/4 camera");
  render(v);
  down(v, {200., 300.});
  move(v, {242., 328.});
  up(v);
  expect(v.cameraView == -1 && near(v.yaw, -.52 + 42. * .015) &&
             near(v.pitch, .34 + 28. * .012),
         "literal orbit sensitivity");
  render(v);
  down(v, {200., 300.});
  move(v, {200., -1000.});
  up(v);
  expect(near(v.pitch, -.75), "pitch lower clamp");
  render(v);
  down(v, {200., 300.});
  move(v, {200., 2000.});
  up(v);
  expect(near(v.pitch, .95), "pitch upper clamp");
  v.camera(2);
  render(v);
  click(v, {596., 131.5});
  expect(v.showReadout, "LST button beats field drag");
  const auto readout = render(v, "readout");
  click(v, {596., 131.5});
  expect(!v.showReadout && render(v) != readout, "readout close");
  click(v, {355., 52.5});
  expect(v.fieldPage == 1, "scope/sonogram tab");
  const auto silence = render(v, "scope-idle");
  for (uint32_t lane = 0; lane < kChannelCount; ++lane)
    for (uint32_t n = 0; n < kScopeFrames; ++n)
      p.scope[lane][n].store(.15f * float(std::sin(n * (.08 + lane * .017))),
                             std::memory_order_relaxed);
  expect(render(v, "scope-fixture") != silence,
         "all-channel output analyzer has live graphics");
  click(v, {289., 52.5});
  expect(v.fieldPage == 0, "TOPO tab");
  // Each shape uses the actual DSP topology generators, not a canned mesh.
  for (uint32_t shape = 0; shape < s3g::kTopologyShapeCount; ++shape) {
    v.set(topology_canvas::topologyIds[0], shape);
    v.set(topology_canvas::topologyIds[1], .88);
    flush(p, events);
    render(v, "shape-" + std::to_string(shape));
  }
#if defined(S3G_TOPOLOGY_DELAY_PORT)
  v.set(kRouteAmountParamId, .61);
  v.set(kDelayMsParamId, 333.);
  flush(p, events);
  render(v);
  click(v, {1307., 53.5});
  flush(p, events);
  expect(v.value(kTopologySpreadParamId) == 0. &&
             v.value(kTopologySkewParamId) == 0.,
         "topology RESET parameters");
  expect(v.value(kRouteAmountParamId) == .61 &&
             v.value(kDelayMsParamId) == 333.,
         "topology RESET leaves engine/routes");
  expect(v.cameraView == 2, "topology RESET camera");
  auto before = render(v);
  p.routeEdgeEnergy[0][1].store(.8f);
  p.routeEdgePhase[0][1].store(.2f);
  p.routeNodeEnergy[1].store(.8f);
  p.routeCentroidEnergy.store(.6f);
  p.routeCentroidPhase.store(.4f);
  expect(render(v, "telemetry") != before,
         "Delay live route/node/centroid overlays");
  p.routeEdgePhase[0][1].store(.7f);
  expect(render(v) != before, "Delay traveling pulse");
#elif defined(S3G_TOPOLOGY_WAVE_GEOMETRY_PORT)
  v.set(kMeshCouplingParamId, .8);
  v.set(kTopologyCentroidParamId, .6);
  flush(p, events);
  auto before = render(v);
  for (uint32_t i = 0; i < kChannelCount; ++i)
    for (uint32_t j = 0; j < kChannelCount; ++j) {
      p.meshEdgeEnergy[i][j].store(.8f);
      p.meshEdgePhase[i][j].store(.3f);
    }
  expect(render(v, "telemetry") != before, "Wave live edge/centroid overlays");
#endif
}
struct Audio {
  std::array<std::array<float, 256>, kChannelCount> input{}, output{};
  std::array<float *, kChannelCount> inPtrs{}, outPtrs{};
  clap_audio_buffer_t in{}, out{};
  clap_process_t block{};
  uint64_t time = 0;
  Audio(Events &events) {
    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
      inPtrs[ch] = input[ch].data();
      outPtrs[ch] = output[ch].data();
    }
    in.data32 = inPtrs.data();
    in.channel_count = kChannelCount;
    out.data32 = outPtrs.data();
    out.channel_count = kChannelCount;
    block.frames_count = 256;
    block.audio_inputs = &in;
    block.audio_inputs_count = 1;
    block.audio_outputs = &out;
    block.audio_outputs_count = 1;
    block.out_events = &events.out;
  }
  void run(Plugin &p, int blocks = 32) {
    for (int b = 0; b < blocks; ++b) {
      for (uint32_t ch = 0; ch < kChannelCount; ++ch)
        for (uint32_t i = 0; i < 256; ++i)
          input[ch][i] = float(.03 * std::sin((time + i) * (.03 + .007 * ch)));
      expect(process(&p.plugin, &block) != CLAP_PROCESS_ERROR,
             "audio processing");
      for (auto &lane : output)
        for (auto v : lane)
          expect(std::isfinite(v), "finite audio");
      time += 256;
    }
  }
};
void audio(Editor &v, Plugin &p, Events &events) {
  v.resetPreset();
  flush(p, events);
  p.portableGuiVisible.store(true);
  v.set(topology_canvas::topologyIds[1], .8);
  v.set(topology_canvas::topologyIds[9], 1);
  v.set(topology_canvas::topologyIds[12], .6);
#if defined(S3G_TOPOLOGY_DELAY_PORT)
  v.set(kRouteAmountParamId, .8);
  v.set(kFeedbackParamId, .6);
#elif defined(S3G_TOPOLOGY_WAVE_GEOMETRY_PORT)
  v.set(kMeshCouplingParamId, .8);
#endif
  flush(p, events);
  Audio a(events);
  a.run(p, 512);
  expect(p.publishedMotionPhase.load() > 0., "audio publishes moving topology");
  expect(p.outputPeak.load() > 0., "audio publishes output meter");
  float edgeEnergy = 0.f;
  for (uint32_t source = 0; source < kChannelCount; ++source)
    for (uint32_t destination = 0; destination < kChannelCount; ++destination) {
#if defined(S3G_TOPOLOGY_DELAY_PORT)
      edgeEnergy += p.routeEdgeEnergy[source][destination].load();
#elif defined(S3G_TOPOLOGY_WAVE_GEOMETRY_PORT)
      edgeEnergy += p.meshEdgeEnergy[source][destination].load();
#else
      edgeEnergy += p.processor.edgeActivity(source, destination);
#endif
    }
  expect(edgeEnergy > 0.f,
         "audio publishes real edge activity to portable GUI");
  render(v, "audio");
  cocoa(p, "audio");
#if defined(S3G_TOPOLOGY_SPECTRAL_TOPOLOGY_PORT)
  expect(!p.processor.hasCapture(), "initial capture empty");
  click(v, {921., 144.5});
  a.run(p);
  expect(p.processor.hasCapture(), "CAP captures audio");
  render(v, "captured");
  cocoa(p, "captured");
  click(v, {963., 144.5});
  a.run(p);
  expect(!p.processor.hasCapture(), "CLR empties capture");
  v.set(kFreezeParamId, .9);
  a.run(p);
  expect(p.processor.hasCapture(), "FRZ automatic capture");
  v.set(kRepeatParamId, .7);
  a.run(p);
  expect(near(p.settings.base.repeat, .7), "RPT reaches DSP");
  const auto before = render(v, "spectral-telemetry");
  a.run(p);
  expect(render(v) != before, "Spectral transport/motion telemetry changes");
  v.fieldPage = 1;
  render(v, "audio-sono");
  v.fieldPage = 0;
  const auto write = p.scopeWrite.load();
  p.portableGuiVisible.store(false);
  a.run(p, 4);
  expect(p.scopeWrite.load() == write,
         "hidden Spectral GUI disables analyzer publication");
  p.portableGuiVisible.store(true);
  a.run(p, 4);
  expect(p.scopeWrite.load() != write,
         "shown Spectral GUI resumes analyzer publication");
#endif
  p.portableGuiVisible.store(false);
}
void presets(Editor &v, Plugin &p, Events &events) {
  auto path =
      std::filesystem::temp_directory_path() /
      ("s3g-topology-parity-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(path);
  const auto preset = foundation::pathToUtf8(path / u8"Café 空間.s3gpreset");
  v.set(kMixParamId, .71);
  v.set(topology_canvas::outputId, -12.);
  flush(p, events);
  togglePatchCellFromGui(p, 2, 6);
  const auto patch = v.patchRow(2);
  expect(v.presetFile(preset, true), "UTF-8 preset SAVE");
  v.set(kMixParamId, .12);
  v.set(topology_canvas::outputId, -3.);
  flush(p, events);
  togglePatchCellFromGui(p, 2, 6);
  expect(v.presetFile(preset, false), "UTF-8 preset LOAD");
  flush(p, events);
  expect(near(v.value(kMixParamId), .71) && v.patchRow(2) == patch,
         "preset restores controls and matrix");
  expect(v.value(topology_canvas::outputId) == -3.,
         "preset LOAD preserves OUT");
  expect(v.resetPreset(), "INIT action");
  flush(p, events);
  expect(v.patchRow(2) == patch && v.value(topology_canvas::outputId) == -3.,
         "INIT preserves patch and OUT");
  expect(near(v.value(kMixParamId), v.info(kMixParamId).default_value),
         "INIT defaults");
  expect(!v.presetFile(preset + "-missing", false), "missing preset rejected");
  const auto broken = foundation::pathToUtf8(path / "broken.s3gpreset");
  {
    std::ofstream f(broken, std::ios::binary);
    f << "bad";
  }
  expect(!v.presetFile(broken, false) && v.patchRow(2) == patch,
         "truncated preset preserves live state");
  std::filesystem::remove_all(path); // uniquely created test fixtures only
}
} // namespace test

int main() {
  using namespace test;
  if (!foundation::acquireRuntime())
    return 2;
  expect(foundation::usingBundledFont() !=
             (std::getenv("S3G_TOPOLOGY_EXPECT_FONT_FALLBACK") != nullptr),
         "bundled/fallback font");
  clap_host_t host{};
  host.clap_version = CLAP_VERSION_INIT;
  host.name = "Topology parity";
  host.vendor = "s3g";
  host.url = "";
  host.version = "1";
  host.get_extension = [](const clap_host_t *, const char *) -> const void * {
    return nullptr;
  };
  host.request_process = [](const clap_host_t *) {};
  const auto *plugin = createPlugin(nullptr, &host, descriptor.id);
  expect(plugin && init(plugin), "initialization");
  if (!plugin)
    return 2;
  expect(activate(plugin, 48000., 16, 256), "activation");
  auto &p = *self(plugin);
  {
    Editor v(p);
    Events events;
    allSliders(v, p, events);
    menus(v, p, events);
    matrix(v, p);
    graph(v, p, events);
    presets(v, p, events);
    audio(v, p, events);
    events.balanced();
    events.values.clear();
    render(v);
    down(v, {800., 104.});
    move(v, {850., 104.});
    events.accept = false;
    const auto pending = p.guiParamEvents.available();
    flush(p, events);
    expect(p.guiParamEvents.available() == pending,
           "host rejection retains queue");
    v.stopRefresh();
    events.accept = true;
    flush(p, events);
    events.balanced();
    events.values.clear();
    v.begin(kMixParamId);
    for (int i = 0; i < 1100; ++i)
      v.setValue(kMixParamId, .4);
    expect(p.guiParamEvents.available() >= 1,
           "reserved END slot under saturation");
    v.end();
    flush(p, events);
    events.balanced();
    events.values.clear();
    while (v.set(kMixParamId, .2)) {
    }
    expect(!v.resetPreset() && near(v.value(kMixParamId), .2),
           "full queue rejects whole preset");
    // A later host edit must not be rolled back while draining GUI history.
    topology_canvas::publish(p, kMixParamId, .9);
    flush(p, events);
    expect(near(v.value(kMixParamId), .9),
           "queued notifications never roll back newer host values");
    events.balanced();
  }
  deactivate(plugin);
  destroy(plugin);
  foundation::releaseRuntime();
  if (ok)
    std::cout << descriptor.id << " topology canvas parity passed\n";
  return ok ? 0 : 1;
}
