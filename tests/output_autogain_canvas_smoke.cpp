// Exercise the real editors and original codecs/DSP, with Cocoa captures.
#include S3G_TEST_OUTPUT_SOURCE
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/platform/platformfactory.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>

namespace test {
using namespace VSTGUI;
using monitor_canvas::Editor;
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
  auto context = COffscreenContext::create({kGuiWidth, kGuiHeight});
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
    auto *view = [[S3G_TEST_OUTPUT_COCOA_VIEW alloc] initWithPlugin:&v.p];
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
void allControls(Editor &v, Plugin &p, Events &events) {
  const clap_id ids[] = {5, 4, 1, 2, 3, 6, 7, 8, 9};
  const unsigned rows[] = {0, 1, 0, 1, 2, 3, 4, 5, 6};
  for (unsigned n = 0; n < 9; ++n) {
    const auto id = ids[n];
    const auto &panel = n < 2 ? Editor::outputPanel : Editor::routingPanel;
    const double y = layout::rowY(panel, rows[n]);
    const double x = layout::processorControlX(panel.frame.x);
    const double width = layout::processorTrackWidth(panel.frame.width);
    if (id == 4 || id == 6) {
      for (unsigned choice = 0; choice < (id == 4 ? 3u : 8u); ++choice) {
        const auto closed = render(v);
        click(v, {x + 10., y + 4.});
        expect(render(v) != closed, "custom menu opens");
        move(v, {5., 5.});
        const auto unhovered = render(v);
        const auto bounds = layout::menuBoxRect(panel, rows[n]);
        const auto hit = CPoint(bounds.x + 10., bounds.y + bounds.height + 3. +
                                                    choice * 18. + 9.);
        move(v, hit);
        expect(render(v) != unhovered, "menu hover is visible");
        click(v, hit);
        flush(p, events);
        expect(v.value(id) == choice, "every menu choice applies");
      }
    } else {
      v.set(6, 5); // Enable Stereo's projection-only controls.
      flush(p, events);
      const auto info = v.info(id);
      for (double norm : {0., .37, 1.}) {
        render(v);
        down(v, {x + width * .5, y + 4.});
        move(v, {x + norm * width, y + 4.});
        up(v);
        flush(p, events);
        expect(near(v.value(id),
                    monitor_canvas::clampParamValue(
                        id, info.min_value +
                                norm * (info.max_value - info.min_value))),
               "slider map " + std::to_string(id));
      }
      render(v);
      click(v, {x + width * .5, y + 4.}, 2);
      flush(p, events);
      expect(near(v.value(id), info.default_value), "double-click default");
    }
  }
#if defined(S3G_MONITOR_STEREO_PORT)
  v.set(6, 0);
  v.set(8, 23);
  v.set(9, 147);
  flush(p, events);
  for (unsigned row : {5u, 6u}) {
    render(v);
    click(v, {layout::processorControlX(Editor::routingPanel.frame.x) + 5.,
              layout::rowY(Editor::routingPanel, row) + 4.});
  }
  flush(p, events);
  expect(v.value(8) == 23 && v.value(9) == 147,
         "inactive Stereo ATT/DST retain their values");
#endif
  for (unsigned model = 0; model < 8; ++model) {
    v.set(6, model);
    flush(p, events);
    render(v, "layout-" + std::to_string(model));
  }
  const auto closed = render(v);
  click(v, {layout::processorControlX(Editor::outputPanel.frame.x) + 10.,
            layout::rowY(Editor::outputPanel, 1) + 4.});
  click(v, {5., 5.});
  expect(render(v) == closed, "outside menu dismissal");
  events.balanced();
}
void inactiveControls(Editor &v, Plugin &p, Events &events) {
  const auto point = [&](clap_id id, double norm) {
    const unsigned row = id == kParamRotation ? 2 : id == kParamAttenuation3d ? 5 : 6;
    return CPoint(layout::processorControlX(Editor::routingPanel.frame.x) +
                      norm * layout::processorTrackWidth(Editor::routingPanel.frame.width),
                  layout::rowY(Editor::routingPanel, row) + 4.);
  };
  for (unsigned model = 0; model < 8; ++model) {
    v.set(kParamLayout, model);
    flush(p, events);
    events.balanced();
    for (clap_id id : {kParamRotation, kParamAttenuation3d, kParamDistance3d}) {
      bool enabled = id != kParamDistance3d || model >= 5;
#if defined(S3G_MONITOR_STEREO_PORT)
      if (id == kParamAttenuation3d)
        enabled = model >= 5;
      if (id == kParamRotation)
        enabled = model < 2 || model >= 5;
#endif
      expect(v.parameterEnabled(id) == enabled, "layout-specific enabled state");
      v.set(id, 23.);
      flush(p, events);
      events.balanced();
      const auto rowStyle = v.sliderStyle(id);
      const auto &activeStyle = foundation::palette();
      if (enabled)
        expect(rowStyle.label == activeStyle.label && rowStyle.value == activeStyle.value &&
                   rowStyle.fill == activeStyle.fill && rowStyle.text == activeStyle.text,
               "active slider keeps family colors");
      else
        expect(rowStyle.label.red < activeStyle.label.red &&
                   rowStyle.value.red < activeStyle.value.red &&
                   rowStyle.fill.red < activeStyle.fill.red &&
                   rowStyle.text.red < activeStyle.text.red,
               "inactive label, value, fill, and handle are all dimmed");
      render(v);
      down(v, point(id, .4));
      move(v, point(id, .8));
      up(v);
      click(v, point(id, .4), 2);
      MouseWheelEvent wheel;
      wheel.mousePosition = point(id, .4);
      wheel.deltaY = 1.;
      v.onMouseWheelEvent(wheel);
      flush(p, events);
      expect(near(v.value(id), enabled ? v.info(id).default_value : 23.),
             "inactive drag/reset/wheel preserve stored value");
      expect(enabled ? !events.values.empty() : events.values.empty(),
             "inactive controls emit no automation gestures");
      events.balanced();

      // Validate the enablement mask against the real channel-gain equations,
      // including Quad ATT's rear attenuation in non-3D layouts.
      auto low = v.readParams();
      low.inputChannels = 12;
      low.widthPercent = 73.;
      low.rotationDegrees = 31.;
      low.layoutWeightPercent = 67.;
      low.attenuation3dPercent = 45.;
      low.distance3dPercent = 83.;
      auto high = low;
      if (id == kParamRotation) {
        low.rotationDegrees = -27.; high.rotationDegrees = 62.;
      } else if (id == kParamAttenuation3d) {
        low.attenuation3dPercent = 0.; high.attenuation3dPercent = 100.;
      } else {
        low.distance3dPercent = 0.; high.distance3dPercent = 200.;
      }
      bool affectsGains = false;
      for (unsigned ch = 0; ch < low.inputChannels; ++ch) {
#if defined(S3G_MONITOR_STEREO_PORT)
        const auto a = s3g::panGainForChannel(ch, low.inputChannels, low);
        const auto b = s3g::panGainForChannel(ch, high.inputChannels, high);
        affectsGains |= !near(a.pan, b.pan) || !near(a.gain, b.gain);
#else
        const auto a = s3g::quadGainsForChannel(ch, low.inputChannels, low);
        const auto b = s3g::quadGainsForChannel(ch, high.inputChannels, high);
        affectsGains |= !near(a.left, b.left) || !near(a.right, b.right) ||
                        !near(a.leftBack, b.leftBack) || !near(a.rightBack, b.rightBack);
#endif
      }
      expect(affectsGains == enabled, "enablement matches real DSP dependencies");
    }
  }
  // Host automation can invalidate hit geometry before the next 24 Hz draw.
  v.set(kParamLayout, 5);
  v.set(kParamDistance3d, 147.);
  flush(p, events);
  events.balanced();
  render(v);
  setParamValue(p, kParamLayout, 0);
  click(v, point(kParamDistance3d, .3));
  click(v, point(kParamDistance3d, .3), 2);
  flush(p, events);
  expect(v.value(kParamDistance3d) == 147. && events.values.empty(),
         "stale hit targets cannot edit an inactive parameter");
  for (bool timerFirst : {false, true}) {
    v.set(kParamLayout, 5);
    flush(p, events);
    events.balanced();
    render(v);
    down(v, point(kParamDistance3d, .4));
    const auto before = v.value(kParamDistance3d);
    setParamValue(p, kParamLayout, 0);
    if (timerFirst)
      v.service();
    move(v, point(kParamDistance3d, .8));
    up(v);
    flush(p, events);
    expect(v.value(kParamDistance3d) == before && v.active == CLAP_INVALID_ID,
           "layout change ends an inactive drag without changing its value");
    events.balanced();
    v.set(kParamLayout, 5);
    flush(p, events);
    expect(v.value(kParamDistance3d) == before && v.parameterEnabled(kParamDistance3d),
           "returning to 3D restores the retained setting");
    events.balanced();
  }
}
void presets(Editor &v, Plugin &p, Events &events) {
  const auto root =
      std::filesystem::temp_directory_path() /
      ("s3g-autogain-parity-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto filename = foundation::pathToUtf8(root / "écho_波_Δ.s3gpreset");
  std::array<double, 9> expected{};
  for (clap_id id = 1; id <= 9; ++id) {
    const auto info = v.info(id);
    v.set(id, info.min_value + .31 * (info.max_value - info.min_value));
  }
  flush(p, events);
  for (clap_id id = 1; id <= 9; ++id)
    expected[id - 1] = v.value(id);
  expect(v.presetFile(filename, true), "Unicode preset save");
  Bytes original, portable;
  expect(encodeMonitorState(&p.plugin, &original.out) &&
             stateSave(&p.plugin, &portable.out),
         "state encodes");
  expect(original.data == portable.data, "original state bytes preserved");
  v.applySnapshot(nullptr);
  v.set(5, -17.);
  flush(p, events);
  expect(v.presetFile(filename, false), "Unicode preset load");
  flush(p, events);
  for (clap_id id = 1; id <= 9; ++id)
    expect(near(v.value(id), id == 5 ? -17. : expected[id - 1]),
           "LOAD preserves OUT and recalls other controls");
  expect(stateLoad(&p.plugin, &original.in), "original project state load");
  flush(p, events);
  for (clap_id id = 1; id <= 9; ++id)
    expect(near(v.value(id), expected[id - 1]),
           "project restores every parameter");
  const auto before = v.value(2);
  Bytes invalid;
  invalid.data = {0, 1, 2};
  expect(!stateLoad(&p.plugin, &invalid.in), "truncated state rejected");
  invalid.data = portable.data;
  invalid.offset = 0;
  const uint32_t wrong = 999;
  std::memcpy(invalid.data.data(), &wrong, sizeof(wrong));
  expect(!stateLoad(&p.plugin, &invalid.in), "unsupported version rejected");
  invalid.data = portable.data;
  invalid.offset = 0;
  const float nan = std::numeric_limits<float>::quiet_NaN();
  std::memcpy(invalid.data.data() + offsetof(SavedState, params) +
                  offsetof(decltype(Plugin::params), widthPercent),
              &nan, sizeof(nan));
  expect(!stateLoad(&p.plugin, &invalid.in) && v.value(2) == before,
         "nonfinite state rejected atomically");
#if defined(S3G_MONITOR_STEREO_PORT)
  SavedStateV1 legacy{};
  legacy.widthPercent = 63.f;
  invalid.data.resize(sizeof(legacy));
  invalid.offset = 0;
  std::memcpy(invalid.data.data(), &legacy, sizeof(legacy));
  expect(stateLoad(&p.plugin, &invalid.in), "Stereo v1 state remains readable");
  flush(p, events);
  expect(v.value(2) == 63. && v.value(9) == 100., "Stereo v1 distance default");
#endif
  v.applySnapshot(nullptr);
  flush(p, events);
  for (clap_id id = 1; id <= 9; ++id)
    if (id != 5)
      expect(near(v.value(id), v.info(id).default_value), "INIT defaults");
  events.balanced();
  std::filesystem::remove_all(
      root); // Only the test's uniquely created fixtures.
}
void backpressure(Editor &v, Plugin &p, Events &events) {
  events.accept = false;
  v.begin(2);
  for (unsigned i = 0; i < 2000; ++i)
    v.setValue(2, i % 200);
  v.end();
  expect(p.monitorGuiEvents.available() == 0, "queue exercised at saturation");
  const double before = v.value(2);
  expect(!v.applySnapshot(nullptr) && v.value(2) == before,
         "preset capacity rejection is atomic");
  flush(p, events);
  setParamValue(p, 2, 137.); // A newer host automation event.
  events.accept = true;
  flush(p, events);
  expect(v.value(2) == 137. && p.params.widthPercent == 137.,
         "old queued edit cannot overwrite newer automation");
  events.balanced();
  render(v);
  down(v, {layout::processorControlX(Editor::routingPanel.frame.x) + 30.,
           layout::rowY(Editor::routingPanel, 1) + 4.});
  v.stopRefresh();
  flush(p, events);
  events.balanced();
}
template <typename Sample> void audio(Editor &v, Plugin &p, Events &events) {
  constexpr unsigned frames = 128, hostOutputs = 8;
  std::array<std::array<Sample, frames>, 128> input{};
  std::array<std::array<Sample, frames>, hostOutputs> output{};
  std::array<Sample *, 128> inputPointers{};
  std::array<Sample *, hostOutputs> outputPointers{};
  for (unsigned ch = 0; ch < 128; ++ch) {
    inputPointers[ch] = input[ch].data();
    for (unsigned i = 0; i < frames; ++i)
      input[ch][i] = Sample(.003 * std::sin(i * .09 + ch * .43));
  }
  for (unsigned ch = 0; ch < hostOutputs; ++ch)
    outputPointers[ch] = output[ch].data();
  clap_audio_buffer_t in{}, out{};
  in.channel_count = 128;
  out.channel_count = hostOutputs;
  if constexpr (std::is_same_v<Sample, float>) {
    in.data32 = inputPointers.data();
    out.data32 = outputPointers.data();
  } else {
    in.data64 = inputPointers.data();
    out.data64 = outputPointers.data();
  }
  clap_process_t proc{};
  proc.frames_count = frames;
  proc.audio_inputs_count = proc.audio_outputs_count = 1;
  proc.audio_inputs = &in;
  proc.audio_outputs = &out;
  proc.out_events = &events.out;
  for (unsigned model = 0; model < 8; ++model)
    for (unsigned mode = 0; mode < 3; ++mode)
      for (unsigned count : {2u, 8u, 64u, 128u}) {
        v.applySnapshot(nullptr);
        v.set(5, 0);
        v.set(1, count);
        v.set(6, model);
        v.set(4, mode);
        flush(p, events);
        expect(activate(&p.plugin, 48000., frames, frames), "audio activate");
        for (auto &lane : output)
          lane.fill(Sample(99));
        expect(process(&p.plugin, &proc) == CLAP_PROCESS_CONTINUE,
               "audio processing");
        for (unsigned ch = 0; ch < hostOutputs; ++ch)
          for (unsigned i = 0; i < frames; ++i) {
            double expected = 0.;
            if (ch < monitor_canvas::outputCount) {
              for (unsigned source = 0; source < count; ++source) {
                const auto g = p.gains[source];
#if defined(S3G_MONITOR_STEREO_PORT)
                const float gain = ch == 0 ? g.left : g.right;
#else
                const float gains[] = {g.left, g.right, g.rightBack,
                                       g.leftBack};
                const float gain = gains[ch];
#endif
                expected += double(input[source][i]) * gain;
              }
            }
            expect(std::isfinite(output[ch][i]) &&
                       std::abs(double(output[ch][i]) - expected) < 2e-6,
                   "channel mapping / unused host lane clearing");
          }
        if (mode == 1 && count == 8)
          render(v, "audio-layout-" + std::to_string(model));
        deactivate(&p.plugin);
      }
  events.balanced();
}
} // namespace test
int main() {
  using namespace test;
  expect(foundation::acquireRuntime(), "VSTGUI runtime");
  expect(foundation::usingBundledFont() ==
             (std::getenv("S3G_MONITOR_EXPECT_FONT_FALLBACK") == nullptr),
         "font resource/fallback");
  unsigned requests = 0;
  clap_host_t host{};
  host.clap_version = CLAP_VERSION_INIT;
  host.name = "Output Autogain parity";
  host.vendor = "s3g";
  host.url = "";
  host.version = "1";
  host.host_data = &requests;
  host.get_extension = [](const clap_host_t *, const char *id) -> const void * {
    static const clap_host_params_t params{
        nullptr, nullptr, [](const clap_host_t *host) {
          ++*static_cast<unsigned *>(host->host_data);
        }};
    return std::strcmp(id, CLAP_EXT_PARAMS) == 0 ? &params : nullptr;
  };
  host.request_process = [](const clap_host_t *host) {
    ++*static_cast<unsigned *>(host->host_data);
  };
  const auto *plugin = createPlugin(nullptr, &host, descriptor.id);
  expect(plugin && init(plugin), "plugin init");
  if (plugin) {
    auto &p = *self(plugin);
    auto *v = new Editor(p);
    Events events;
    render(*v, "initial");
    allControls(*v, p, events);
    inactiveControls(*v, p, events);
    presets(*v, p, events);
    backpressure(*v, p, events);
    audio<float>(*v, p, events);
    audio<double>(*v, p, events);
    expect(requests > 0, "GUI requests event service");
    requests = 0;
    p.monitorHostParams = nullptr;
    v->set(2, 100.);
    flush(p, events);
    events.balanced();
    expect(requests > 0, "request_process fallback");
    v->forget();
    destroy(plugin);
  }
  foundation::releaseRuntime();
  if (ok)
    std::cout << descriptor.id << " canvas parity passed\n";
  return ok ? 0 : 1;
}
