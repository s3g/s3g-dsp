#include S3G_TEST_ROUTING_SOURCE
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/platform/platformfactory.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <thread>

namespace test {
using namespace VSTGUI;
using Editor = S3G_TEST_ROUTING_EDITOR;
namespace R = s3g::portable_gui::routing;
namespace F = s3g::portable_gui::foundation;
bool ok = true;
void expect(bool v, const char *message) {
  if (!v) {
    ok = false;
    std::cerr << message << '\n';
  }
}
bool near(double a, double b) { return std::abs(a - b) < 1.e-4; }
struct Events {
  std::vector<s3g::clap_gui::ParamEvent> events;
  unsigned limit = 100000;
  clap_output_events_t out{
      this, [](const clap_output_events_t *o, const clap_event_header_t *h) {
        auto &self = *static_cast<Events *>(o->ctx);
        if (self.limit == 0)
          return false;
        --self.limit;
        using K = s3g::clap_gui::ParamEventKind;
        if (h->type == CLAP_EVENT_PARAM_VALUE) {
          const auto *e = reinterpret_cast<const clap_event_param_value_t *>(h);
          self.events.push_back({K::Value, e->param_id, e->value});
        } else {
          const auto *e =
              reinterpret_cast<const clap_event_param_gesture_t *>(h);
          self.events.push_back({h->type == CLAP_EVENT_PARAM_GESTURE_BEGIN
                                     ? K::GestureBegin
                                     : K::GestureEnd,
                                 e->param_id, 0.});
        }
        return true;
      }};
  void balanced() {
    using K = s3g::clap_gui::ParamEventKind;
    std::map<clap_id, int> depth;
    for (auto e : events) {
      if (e.kind == K::GestureBegin)
        expect(++depth[e.paramId] == 1, "nested gesture");
      else if (e.kind == K::GestureEnd)
        expect(--depth[e.paramId] == 0, "unbalanced end");
      else
        expect(depth[e.paramId] == 1, "value outside gesture");
    }
    for (auto e : depth)
      expect(e.second == 0, "unterminated gesture");
    events.clear();
    limit = 100000;
  }
};
void flush(Plugin &p, Events &events) {
  paramsFlush(&p.plugin, nullptr, &events.out);
}
void down(Editor &v, CPoint p, int count = 1, bool shift = false) {
  MouseDownEvent e;
  e.mousePosition = p;
  e.buttonState.add(MouseButton::Left);
  e.clickCount = count;
  if (shift)
    e.modifiers.add(ModifierKey::Shift);
  v.onMouseDownEvent(e);
}
void move(Editor &v, CPoint p) {
  MouseMoveEvent e;
  e.mousePosition = p;
  e.buttonState.add(MouseButton::Left);
  v.onMouseMoveEvent(e);
}
void up(Editor &v) {
  MouseUpEvent e;
  v.onMouseUpEvent(e);
}
void click(Editor &v, CPoint p, int count = 1) {
  down(v, p, count);
  up(v);
}
CPoint center(R::Box b) {
  return {b.origin.x + b.size.width * .5, b.origin.y + b.size.height * .5};
}
void render(Editor &v, const std::string &suffix = "") {
  auto ctx = COffscreenContext::create({double(kGuiWidth), double(kGuiHeight)});
  expect(bool(ctx), "offscreen canvas");
  if (!ctx)
    return;
  ctx->beginDraw();
  v.draw(ctx);
  ctx->endDraw();
#if S3G_ROUTING_FAMILY == 2 && defined(S3G_AMBI_NODE_TRACK_MIXER)
  auto pixels = owned(CBitmapPixelAccess::create(ctx->getBitmap()));
  expect(bool(pixels), "meter pixels available");
  if (pixels) {
    pixels->setPosition(100, 649);
    CColor c;
    pixels->getColor(c);
    expect(c.red > 100, "ambisonic node gain meter outline visible");
    pixels->setPosition(100, 778);
    pixels->getColor(c);
    expect(c.red > 80, "ambisonic output peak meter outline visible");
  }
  pixels = nullptr;
#endif
  if (auto *root = std::getenv("S3G_ROUTING_CAPTURE_DIR");
      root && !suffix.empty()) {
    std::filesystem::create_directories(F::pathFromUtf8(root));
    auto data = getPlatformFactory().createBitmapMemoryPNGRepresentation(
        ctx->getBitmap()->getPlatformBitmap());
    std::ofstream f(F::pathFromUtf8(root) /
                        (std::string(descriptor.id) + "-" + suffix + ".png"),
                    std::ios::binary);
    f.write(reinterpret_cast<const char *>(data.data()), data.size());
    expect(bool(f), "reference PNG");
  }
}
void parameters(Editor &v, Plugin &p, Events &e) {
  for (auto i : v.infos) {
    const double before = v.value(i.id);
    expect(std::isfinite(before), "finite parameter");
    if (i.flags & CLAP_PARAM_IS_READONLY)
      continue;
    for (double n : {0., .5, 1.}) {
      expect(v.set(i.id, i.min_value + n * (i.max_value - i.min_value)),
             "parameter edit accepted");
      flush(p, e);
      double got = 0.;
      expect(paramsGetValue(&p.plugin, i.id, &got) && std::isfinite(got) &&
                 got >= i.min_value - 1.e-4 && got <= i.max_value + 1.e-4,
             "host receives legal edited value");
    }
    v.set(i.id, before);
    flush(p, e);
  }
  e.balanced();
  v.resetParameters();
  flush(p, e);
  e.balanced();
  const auto i = v.infos.back();
  expect(v.begin(i.id), "begin continuous gesture");
  expect(!v.set(i.id, i.default_value), "reject nested discrete gesture");
  for (unsigned n = 0; n < 5000; ++n)
    v.change(i.id, i.default_value);
  v.end();
  e.limit = 2;
  flush(p, e);
  e.limit = 100000;
  flush(p, e);
  e.balanced();
  expect(p.guiParamEvents.available() == 4095,
         "queue backpressure recovers all gestures");
  expect(!v.set(i.id, std::numeric_limits<double>::quiet_NaN()),
         "reject nonfinite GUI edit");
  expect(!v.begin(CLAP_INVALID_ID) && !v.set(CLAP_INVALID_ID, 0.),
         "reject invalid parameter IDs before accessing published values");
}
void presets(Editor &v, Plugin &p, Events &e) {
  auto root =
      std::filesystem::temp_directory_path() /
      ("s3g-routing-parity-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  auto path = F::pathToUtf8(root / "écho_波_Δ.s3gpreset");
  std::map<clap_id, double> saved;
  for (auto i : v.infos)
    saved[i.id] = v.value(i.id);
  expect(v.presetFile(path, true), "Unicode preset save");
  // Hosts query restored parameters before activation. Test the actual state
  // entry point independently of the editor's isolated raw-parameter loader.
  auto recalled = std::make_unique<Plugin>();
  recalled->plugin.plugin_data = recalled.get();
  init(&recalled->plugin);
  const clap_plugin_state_t codec{stateSave, stateLoad};
  expect(s3g::clap_gui::portable::loadStateFile(&recalled->plugin, codec,
                                                path.c_str()),
         "host project state load before activation");
  for (auto i : v.infos) {
    double value = 0.;
    expect(paramsGetValue(&recalled->plugin, i.id, &value) &&
               near(value, saved[i.id]),
           "host state publishes every parameter before activation");
  }
  v.resetParameters();
  flush(p, e);
#if S3G_ROUTING_FAMILY != 4
  constexpr auto out = S3G_TEST_OUTPUT_PARAM;
  v.set(out, -17.);
  flush(p, e);
#endif
  expect(v.presetFile(path, false), "Unicode preset load");
  flush(p, e);
  for (auto i : v.infos) {
#if S3G_ROUTING_FAMILY != 4
    if (i.id == out) {
      expect(near(v.value(i.id), -17.), "preset preserves OUT");
      continue;
    }
#endif
    expect(near(v.value(i.id), saved[i.id]), "preset parameter roundtrip");
  }
  e.balanced();
  std::ofstream bad(F::pathFromUtf8(path.c_str()),
                    std::ios::binary | std::ios::trunc);
  bad << "broken";
  bad.close();
  expect(!v.presetFile(path, false), "truncated preset rejected");
  std::filesystem::remove_all(root);
}
#if S3G_ROUTING_FAMILY == 2
void nodeCursorPublication(Editor &v, Plugin &p, Events &events) {
  v.set(kParamNodeCount, 8);
  for (unsigned i = 0; i < 8; ++i)
    v.set(kParamNodeBase + i * kParamNodeStride, 1.);
  flush(p, events);
  const auto before = latestParamsSnapshot(p, p.params);
  expect(v.begin(kParamCursorX) && v.change(kParamCursorX, .35),
         "cursor gesture queued");
  // Pause the GUI producer mid-publication while the audio callback runs.
  // The second bank has never been published: reading its zero-filled cells
  // used to turn the topology into one inactive node.
  p.audioParamBank.stamp.store(0);
  p.controlParamBank.sequence.fetch_add(1);
  Params snapshot{};
  expect(!tryParamsFromBank(p.audioParamBank, before, snapshot),
         "unpublished audio bank is not a valid snapshot");
  const auto stable = latestParamsSnapshot(p, before);
  expect(stable.nodeCount == 8 && stable.nodes[0].active,
         "busy bank retains the last complete node topology");
  clap_process_t block{};
  block.out_events = &events.out;
  process(&p.plugin, &block);
  p.controlParamBank.sequence.fetch_add(1);
  v.end();
  process(&p.plugin, &block);
  v.refreshState();
  expect(v.state.nodeCount == 8 && v.state.nodes[0].active,
         "audio callback cannot erase nodes during a cursor edit");
  expect(near(p.audioParams.cursorX, .35), "audio receives edited cursor");
  events.balanced();

  // While active, host flush must publish to the audio bank, never become a
  // second writer of the GUI/control bank. Delayed GUI notifications must
  // not replay older values over a newer host automation change either.
  clap_event_param_value_t automation{};
  automation.header.size = sizeof(automation);
  automation.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
  automation.header.type = CLAP_EVENT_PARAM_VALUE;
  automation.param_id = kParamCursorX;
  automation.value = .72;
  clap_input_events_t input{
      &automation, [](const clap_input_events_t *) -> uint32_t { return 1; },
      [](const clap_input_events_t *in,
         uint32_t) -> const clap_event_header_t * {
        return &static_cast<clap_event_param_value_t *>(in->ctx)->header;
      }};
  expect(v.begin(kParamCursorX) && v.change(kParamCursorX, .12),
         "deferred cursor notification");
  v.end();
  const auto controlStamp = p.controlParamBank.stamp.load();
  events.limit = 0;
  paramsFlush(&p.plugin, &input, &events.out);
  expect(p.controlParamBank.stamp.load() == controlStamp,
         "active host flush never writes the GUI bank");
  events.limit = 100000;
  process(&p.plugin, &block);
  expect(near(v.value(kParamCursorX), .72),
         "delayed GUI notifications cannot overwrite host automation");
  events.balanced();

  // Real concurrent playback plus repeated click/drag/wheel cursor gestures,
  // using every projection. Topology must be unchanged on both threads.
  v.set(kParamCursorX, 0.);
  v.set(kParamCursorY, 0.);
  v.set(kParamCursorZ, 0.);
  flush(p, events);
  events.balanced();
  std::atomic<bool> stop{false}, damaged{false};
  std::atomic<unsigned> blocks{0};
  std::thread audio([&] {
    std::array<std::array<float, 64>, 128> in{}, out{};
    std::array<float *, 128> inPtr{}, outPtr{};
    for (unsigned i = 0; i < 128; ++i) {
      inPtr[i] = in[i].data();
      outPtr[i] = out[i].data();
    }
    clap_audio_buffer_t inBuffer{}, outBuffer{};
    inBuffer.data32 = inPtr.data();
    outBuffer.data32 = outPtr.data();
    inBuffer.channel_count = outBuffer.channel_count = 128;
    clap_process_t playback{};
    playback.frames_count = 64;
    playback.audio_inputs = &inBuffer;
    playback.audio_outputs = &outBuffer;
    playback.audio_inputs_count = playback.audio_outputs_count = 1;
    playback.out_events = &events.out;
    while (!stop.load()) {
      process(&p.plugin, &playback);
      if (p.audioParams.nodeCount != 8)
        damaged.store(true);
      for (unsigned i = 0; i < 8; ++i)
        if (!p.audioParams.nodes[i].active)
          damaged.store(true);
      ++blocks;
    }
  });
  while (blocks.load() == 0)
    std::this_thread::yield();
  for (int view = 0; view < 3; ++view) {
    v.setView(view);
    for (unsigned repeat = 0; repeat < 12; ++repeat) {
      render(v);
      const auto cursor =
          v.project(v.state.cursorX, v.state.cursorY, v.state.cursorZ,
                    R::box(node_canvas::family.fieldPlot));
      down(v, cursor);
      expect(v.spatialDrag == 2, "playback cursor hit");
      for (unsigned step = 0; step < 80; ++step) {
        move(v, {cursor.x + 16. * std::sin(step * .15),
                 cursor.y + 12. * std::cos(step * .15)});
        MouseWheelEvent wheel;
        wheel.mousePosition = cursor;
        wheel.deltaY = step % 2 ? 1. : -1.;
        v.onMouseWheelEvent(wheel);
        v.refreshState();
        if (v.state.nodeCount != 8)
          damaged.store(true);
        for (unsigned i = 0; i < 8; ++i)
          if (!v.state.nodes[i].active)
            damaged.store(true);
      }
      move(v, cursor);
      up(v);
    }
  }
  stop.store(true);
  audio.join();
  process(&p.plugin, &block);
  expect(blocks.load() > 0 && !damaged.load(),
         "concurrent cursor drag/scroll preserves every node");
  events.balanced();
}
#endif
