// Exercise the real editors and original codecs/DSP, with Cocoa captures.
#include "../plugins/clap_multichannel_meter/s3g_multichannel_meter_clap.cpp"
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/platform/platformfactory.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#if defined(__APPLE__)
#include <objc/runtime.h>
#endif

namespace test {
using namespace VSTGUI;
using meter_canvas::Editor;
namespace foundation = s3g::portable_gui::foundation;
namespace layout = s3g::gui_layout;
bool ok = true;
void expect(bool value, const std::string &message) {
  if (!value) {
    ok = false;
    std::cerr << message << '\n';
  }
}
bool near(double a, double b) {
  return std::abs(a - b) <= 1e-5 * std::max(1., std::abs(b));
}
struct Events {
  bool accept = true;
  std::vector<s3g::clap_gui::ParamEvent> values;
  clap_output_events_t out{
      this,
      [](const clap_output_events_t *out, const clap_event_header_t *header) {
        auto &self = *static_cast<Events *>(out->ctx);
        if (!self.accept)
          return false;
        using Kind = s3g::clap_gui::ParamEventKind;
        if (header->type == CLAP_EVENT_PARAM_VALUE) {
          auto *event =
              reinterpret_cast<const clap_event_param_value_t *>(header);
          self.values.push_back({Kind::Value, event->param_id, event->value});
        } else {
          auto *event =
              reinterpret_cast<const clap_event_param_gesture_t *>(header);
          self.values.push_back({header->type == CLAP_EVENT_PARAM_GESTURE_BEGIN
                                     ? Kind::GestureBegin
                                     : Kind::GestureEnd,
                                 event->param_id, 0.});
        }
        return true;
      }};
  void balanced() {
    std::map<clap_id, int> depth;
    using Kind = s3g::clap_gui::ParamEventKind;
    for (const auto &e : values) {
      if (e.kind == Kind::GestureBegin)
        expect(++depth[e.paramId] == 1, "nested gesture");
      else if (e.kind == Kind::GestureEnd)
        expect(--depth[e.paramId] == 0, "unbalanced END");
      else
        expect(depth[e.paramId] == 1, "value outside gesture");
    }
    for (const auto &entry : depth)
      expect(entry.second == 0, "unterminated gesture");
    values.clear();
  }
};
void flush(Plugin &p, Events &events) {
  paramsFlush(&p.plugin, nullptr, &events.out);
}
void down(Editor &v, CPoint point, int count = 1) {
  MouseDownEvent event;
  event.mousePosition = point;
  event.clickCount = count;
  event.buttonState.add(MouseButton::Left);
  v.onMouseDownEvent(event);
}
void move(Editor &v, CPoint point) {
  MouseMoveEvent event;
  event.mousePosition = point;
  event.buttonState.add(MouseButton::Left);
  v.onMouseMoveEvent(event);
}
void up(Editor &v) {
  MouseUpEvent event;
  v.onMouseUpEvent(event);
}
void click(Editor &v, CPoint point, int count = 1) {
  down(v, point, count);
  up(v);
}
std::vector<uint8_t> render(Editor &v, const std::string &name = "") {
  auto context = COffscreenContext::create(
      {v.getViewSize().getWidth(), v.getViewSize().getHeight()});
  expect(bool(context), "offscreen context");
  if (!context)
    return {};
  context->beginDraw();
  v.draw(context);
  context->endDraw();
  auto png = getPlatformFactory().createBitmapMemoryPNGRepresentation(
      context->getBitmap()->getPlatformBitmap());
  const char *root = std::getenv("S3G_MONITOR_CAPTURE_DIR");
  if (root && !name.empty()) {
    auto directory = foundation::pathFromUtf8(root);
    std::filesystem::create_directories(directory);
    std::ofstream file(directory /
                           (std::string(descriptor.id) + "-" + name + ".png"),
                       std::ios::binary);
    file.write(reinterpret_cast<const char *>(png.data()), png.size());
    expect(bool(file), "write portable capture");
#if defined(__APPLE__)
    auto *view = [[S3GMultichannelMeterView alloc] initWithPlugin:&v.p];
    [view setFrameSize:NSMakeSize(v.getViewSize().getWidth(),
                                  v.getViewSize().getHeight())];
    [view setFieldViewPreset:static_cast<int>(v.cameraMode)];
    // Capture the same camera and temporal data, not a reset reference view.
    const auto setScalar = [&](const char *name, const auto &value) {
      Ivar ivar = class_getInstanceVariable([view class], name);
      std::memcpy(reinterpret_cast<char *>(view) + ivar_getOffset(ivar), &value,
                  sizeof(value));
    };
    setScalar("_viewAzDeg", v.azimuth);
    setScalar("_viewElDeg", v.elevation);
    setScalar("_viewZoom", v.zoom);
    setScalar("_historyWrite", v.historyWrite);
    setScalar("_history", v.history);
    const auto path = foundation::pathToUtf8(
        directory / (std::string(descriptor.id) + "-cocoa-" + name + ".pdf"));
    expect([[view dataWithPDFInsideRect:[view bounds]]
               writeToFile:[NSString stringWithUTF8String:path.c_str()]
                atomically:YES],
           "write Cocoa capture");
    [view release];
#endif
  }
  return png;
}
struct Bytes {
  std::vector<uint8_t> data;
  size_t offset = 0;
  clap_ostream_t out{this,
                     [](const clap_ostream_t *stream, const void *data,
                        uint64_t size) -> int64_t {
                       auto &s = *static_cast<Bytes *>(stream->ctx);
                       const auto *bytes = static_cast<const uint8_t *>(data);
                       s.data.insert(s.data.end(), bytes, bytes + size);
                       return size;
                     }};
  clap_istream_t in{
      this,
      [](const clap_istream_t *stream, void *data, uint64_t size) -> int64_t {
        auto &s = *static_cast<Bytes *>(stream->ctx);
        size = std::min<uint64_t>(size, s.data.size() - s.offset);
        std::memcpy(data, s.data.data() + s.offset, size);
        s.offset += size;
        return size;
      }};
};
void controls(Editor &v, Plugin &p, Events &events) {
  const auto bar = layout::analyzerToolbarRect({kGuiWidth, kGuiHeight});
  for (clap_id id : {2u, 3u}) {
    for (unsigned choice = 0; choice < (id == 2 ? 3u : kMeterLayoutCount);
         ++choice) {
      render(v);
      click(v, {bar.x + (id == 2 ? 80. : 245.), bar.y + 15.});
      move(v, {5., 5.});
      const auto unhovered = render(v);
      const auto point = CPoint(bar.x + (id == 2 ? 80. : 245.),
                                bar.y + bar.height + 3. + choice * 20. + 10.);
      move(v, point);
      expect(render(v) != unhovered, "menu hover");
      click(v, point);
      flush(p, events);
      expect(v.value(id) == choice, "every view and layout menu entry");
      if (id == 3 && v.value(2) == 1 && meterLayoutSpeakerCount(choice))
        expect(v.value(1) == meterLayoutSpeakerCount(choice),
               "FIELD layout updates WIDTH");
    }
  }
  v.set(2, 1);
  v.set(3, 1);
  flush(p, events);
  expect(v.value(1) == 8, "FIELD cube width coupling");
  for (double norm : {0., .37, 1.}) {
    render(v);
    down(v, {bar.x + 450., bar.y + 14.});
    move(v, {bar.x + 430. + norm * 92., bar.y + 14.});
    up(v);
    flush(p, events);
    expect(v.value(1) == std::round(1. + norm * 63.),
           "WIDTH remains manually editable in FIELD");
  }
  render(v);
  click(v, {bar.x + 450., bar.y + 14.}, 2);
  flush(p, events);
  expect(v.value(1) == 64, "WIDTH double-click reset");
  for (unsigned i = 0; i < 3; ++i) {
    render(v);
    const auto bounds = v.viewButton(i, Editor::box(bar));
    click(v, bounds.getCenter());
    expect(v.cameraMode == i, "TOP / SIDE / 3/4");
  }
  const auto content = layout::analyzerContentRect({kGuiWidth, kGuiHeight});
  render(v);
  down(v, {content.x + 100., content.y + 100.});
  move(v, {content.x + 180., content.y + 140.});
  up(v);
  expect(near(v.azimuth, 63.) && near(v.elevation, 44.) && v.cameraMode == 2,
         "original camera drag sensitivity");
  for (unsigned i = 0; i < 2; ++i) {
    render(v);
    const auto bounds = v.zoomButton(i, Editor::box(bar));
    click(v, bounds.getCenter());
  }
  expect(near(v.zoom, .86 * 1.16), "original zoom factors");
  for (unsigned model = 0; model < kMeterLayoutCount; ++model) {
    v.set(3, model);
    flush(p, events);
    render(v, "field-" + std::to_string(model));
  }
  events.balanced();
}
void history(Editor &v, Plugin &p, Events &events) {
  v.set(2, 2);
  v.set(1, 8);
  flush(p, events);
  for (unsigned tick = 0; tick < 150; ++tick) {
    for (unsigned ch = 0; ch < 64; ++ch) {
      p.rms[ch].store(tick % 15 < 5 ? .01f * (ch + 1) : .000001f);
      p.peak[ch].store(tick % 15 < 5 ? .02f * (ch + 1) : .000001f);
    }
    v.advanceHeatHistory();
  }
  expect(v.historyWrite == 6, "144-column history wraps");
  const unsigned newest = (v.historyWrite + 143) % 144;
  expect(near(v.history[0][newest], Editor::normDb(.000001f)),
         "history uses actual meter magnitude");
  p.activeChannels.store(8);
  const auto before = render(v, "heat-before");
  p.rms[0].store(.8f);
  p.peak[0].store(.95f);
  v.advanceHeatHistory();
  expect(render(v, "heat-after") != before,
         "HEAT scrolls horizontally and adds live samples");
  v.set(2, 0);
  flush(p, events);
  v.advanceHeatHistory();
  expect(v.historyWrite == 8, "history continues while viewing GRID");
  render(v, "grid-active");
  events.balanced();
}
void presets(Editor &v, Plugin &p, Events &events) {
  const auto root =
      std::filesystem::temp_directory_path() /
      ("s3g-meter-parity-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto file = foundation::pathToUtf8(root / "mètre_波_Δ.s3gpreset");
  v.set(2, 1);
  v.set(3, 13);
  flush(p, events);
  expect(v.presetFile(file, true), "Unicode preset save");
  Bytes encoded;
  expect(stateSave(&p.plugin, &encoded.out) &&
             encoded.data.size() == sizeof(SavedState),
         "original state size");
  v.applySnapshot(nullptr);
  flush(p, events);
  expect(v.value(1) == 64 && v.value(2) == 0 && v.value(3) == 0,
         "INIT resets all display params");
  expect(v.presetFile(file, false), "Unicode preset load");
  flush(p, events);
  expect(v.value(1) == 24 && v.value(2) == 1 && v.value(3) == 13,
         "complete display state recall");
  for (unsigned version : {1u, 2u, 3u}) {
    Bytes legacy;
    const uint32_t data[] = {version, 11, 2, 0};
    const auto *bytes = reinterpret_cast<const uint8_t *>(data);
    legacy.data.assign(bytes, bytes + (version + 1) * sizeof(uint32_t));
    expect(stateLoad(&p.plugin, &legacy.in), "legacy display state read");
    expect(v.value(1) == 11 && v.value(2) == (version >= 2 ? 2 : 0),
           "legacy display defaults");
  }
  for (unsigned size = 0; size < encoded.data.size(); ++size) {
    Bytes truncated;
    truncated.data.assign(encoded.data.begin(), encoded.data.begin() + size);
    const auto before = v.values();
    expect(!stateLoad(&p.plugin, &truncated.in) && v.values() == before,
           "truncated recall leaves state intact");
  }
  events.balanced();
  std::filesystem::remove_all(root);
}
void backpressure(Editor &v, Plugin &p, Events &events) {
  v.beginWidth();
  for (unsigned i = 0; i < 2000; ++i)
    v.setWidth(i % 64 + 1);
  v.endWidth();
  expect(p.monitorGuiEvents.available() == 0, "notification saturation");
  const auto before = v.values();
  expect(!v.applySnapshot(nullptr) && v.values() == before,
         "whole preset rejected on queue pressure");
  events.accept = false;
  flush(p, events);
  applyVisibleChannels(p, 17.); // Newer host edit while GUI output is blocked.
  events.accept = true;
  flush(p, events);
  expect(v.value(1) == 17,
         "queued notifications cannot overwrite host automation");
  events.balanced();
  render(v);
  const auto bar = layout::analyzerToolbarRect({kGuiWidth, kGuiHeight});
  down(v, {bar.x + 450., bar.y + 14.});
  v.stopRefresh();
  flush(p, events);
  events.balanced();
}
template <typename Sample> void audio(Plugin &p, Events &events) {
  constexpr unsigned frames = 64;
  std::array<std::array<Sample, frames>, 64> input{}, output{};
  std::array<Sample *, 64> inPointers{}, outPointers{};
  for (unsigned ch = 0; ch < 64; ++ch) {
    inPointers[ch] = input[ch].data();
    outPointers[ch] = output[ch].data();
    for (unsigned i = 0; i < frames; ++i)
      input[ch][i] = Sample(.012 * (ch + 1) * std::sin(i * .4));
  }
  clap_audio_buffer_t in{}, out{};
  out.channel_count = 64;
  if constexpr (std::is_same_v<Sample, float>) {
    in.data32 = inPointers.data();
    out.data32 = outPointers.data();
  } else {
    in.data64 = inPointers.data();
    out.data64 = outPointers.data();
  }
  clap_process_t proc{};
  proc.frames_count = frames;
  proc.audio_inputs_count = proc.audio_outputs_count = 1;
  proc.audio_inputs = &in;
  proc.audio_outputs = &out;
  proc.out_events = &events.out;
  for (unsigned width : {1u, 2u, 8u, 24u, 64u})
    for (unsigned mode = 0; mode < 3; ++mode) {
      in.channel_count = width;
      applyViewMode(p, mode);
      applyVisibleChannels(p, 1.);
      for (auto &lane : output)
        lane.fill(Sample(99));
      expect(process(&p.plugin, &proc) == CLAP_PROCESS_CONTINUE,
             "audio process");
      for (unsigned ch = 0; ch < 64; ++ch)
        for (unsigned i = 0; i < frames; ++i)
          expect(output[ch][i] == (ch < width ? input[ch][i] : Sample(0)),
                 "bit-identical passthrough independent of display width/view");
      expect(p.activeChannels.load() == width && p.peak[0].load() > 0.f &&
                 p.rms[0].load() > 0.f,
             "live meter telemetry");
    }
}
void resize(Plugin &p) {
  foundation::EditorHost host(kGuiWidth, kGuiHeight, kGuiWidth, kGuiHeight);
  auto *v = new Editor(p);
  expect(host.attach(v), "responsive editor attachment");
  for (const auto size : {CPoint(720, 430), CPoint(1400, 560),
                          CPoint(980, 1000), CPoint(1960, 1120)}) {
    expect(host.setSize(size.x, size.y), "independent viewport dimensions");
    expect(near(v->getViewSize().getWidth() / v->getViewSize().getHeight(),
                size.x / size.y),
           "canvas reflows to window aspect");
    render(*v, "resize-" + std::to_string(int(size.x)) + "x" +
                   std::to_string(int(size.y)));
  }
}
} // namespace test
int main() {
  using namespace test;
  if (!foundation::acquireRuntime())
    return 2;
  expect(foundation::usingBundledFont() ==
             (std::getenv("S3G_MONITOR_EXPECT_FONT_FALLBACK") == nullptr),
         "font/fallback");
  unsigned requests = 0;
  clap_host_t host{};
  host.clap_version = CLAP_VERSION_INIT;
  host.name = "Meter parity";
  host.vendor = "s3g";
  host.url = "";
  host.version = "1";
  host.host_data = &requests;
  host.get_extension = [](const clap_host_t *, const char *) -> const void * {
    return nullptr;
  };
  host.request_process = [](const clap_host_t *host) {
    ++*static_cast<unsigned *>(host->host_data);
  };
  const auto *plugin = createPlugin(nullptr, &host, descriptor.id);
  expect(plugin && init(plugin) && activate(plugin, 48000., 64, 64),
         "plugin init/activate");
  if (plugin) {
    auto &p = *self(plugin);
    {
      Editor v(p);
      Events events;
      render(v, "initial");
      controls(v, p, events);
      history(v, p, events);
      presets(v, p, events);
      backpressure(v, p, events);
      audio<float>(p, events);
      audio<double>(p, events);
      resize(p);
      expect(requests > 0, "host service requested");
    }
    deactivate(plugin);
    destroy(plugin);
  }
  foundation::releaseRuntime();
  if (ok)
    std::cout << "Analyzer Meter canvas parity passed\n";
  return ok ? 0 : 1;
}
