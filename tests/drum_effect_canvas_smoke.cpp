// Real canvas input -> CLAP gestures -> DSP, with original Cocoa geometry as
// the reference. No stand-in UI/controller or synthetic parameter model.
#include S3G_TEST_DRUM_SOURCE
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/platform/platformfactory.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>

namespace test {
using namespace VSTGUI;
using fx_canvas::Editor;
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
  const auto path = std::getenv("S3G_DRUM_EFFECT_CAPTURE_DIR");
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
    std::ofstream f(
        captures() / (std::string(fx_canvas::pluginName) + "-" + name + ".png"),
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
  auto *view = [[S3G_TEST_DRUM_COCOA_VIEW alloc] initWithPlugin:&p];
  const auto path =
      foundation::pathToUtf8(captures() / (std::string(fx_canvas::pluginName) +
                                           "-cocoa-" + name + ".pdf"));
  expect([[view dataWithPDFInsideRect:[view bounds]]
             writeToFile:[NSString stringWithUTF8String:path.c_str()]
              atomically:YES],
         "Cocoa capture");
  [view release];
#endif
}
struct Control {
  clap_id id;
  CRect bounds, track;
  bool vertical = false, dial = false;
};
std::vector<Control> controls() {
  std::vector<Control> result;
#if defined(S3G_TEST_DRUM_MIXER)
  for (uint32_t lane = 0; lane < 8; ++lane) {
    for (uint32_t d = 0; d < 5; ++d)
      result.push_back({laneParamId(lane, fx_canvas::dialOffsets[d]),
                        fx_canvas::dialRect(lane, d),
                        {},
                        false,
                        true});
    const auto b = fx_canvas::lanePanel(lane);
    result.push_back({laneParamId(lane, kLaneAuxOffset),
                      fx_canvas::stripHit(b.left, 128., 266.),
                      fx_canvas::stripTrack(b.left, 128., 266.)});
    result.push_back({laneParamId(lane, kLaneLevelOffset),
                      fx_canvas::faderRect(lane), fx_canvas::faderRect(lane),
                      true});
#if defined(__APPLE__)
    const auto same = [](CRect a, NSRect b) {
      return near(a.left, b.origin.x) && near(a.top, b.origin.y) &&
             near(a.getWidth(), b.size.width) &&
             near(a.getHeight(), b.size.height);
    };
    expect(same(b, lanePanelRect(lane)), "original lane panel geometry");
    expect(same(fx_canvas::faderRect(lane), laneFaderRect(lane)),
           "original fader geometry");
    expect(
        same(fx_canvas::stripTrack(b.left, 128., 266.), laneAuxTrackRect(lane)),
        "original AUX track geometry");
    for (uint32_t d = 0; d < 5; ++d)
      expect(same(fx_canvas::dialRect(lane, d),
                  laneDialRect(lane, fx_canvas::dialOffsets[d])),
             "original dial geometry");
#endif
  }
  result.push_back({kMasterLevelParamId, fx_canvas::stripHit(1096., 206., 78.),
                    fx_canvas::stripTrack(1096., 206., 78.)});
  for (clap_id id = kBusDriveParamId; id <= kBusReturnParamId; ++id)
    result.push_back({id, fx_canvas::stripHit(1096., 206., fx_canvas::busY(id)),
                      fx_canvas::stripTrack(1096., 206., fx_canvas::busY(id))});
#else
  const auto add = [&](auto &panel, const auto &indices, uint32_t skip) {
    for (uint32_t row = skip; row < std::size(indices); ++row)
      result.push_back(
          {kParamDefs[indices[row]].id,
           Editor::box(s3g::gui_layout::sliderHitRect(panel, row)),
           foundation::rect(
               s3g::gui_layout::processorControlX(panel.frame.x),
               s3g::gui_layout::rowY(panel, row) + 1.,
               s3g::gui_layout::processorTrackWidth(panel.frame.width), 9.)});
  };
  add(kOutputPanel, kOutputParamIndices, 0);
#if defined(S3G_TEST_DRUM_ECHO)
  add(kEchoPanel, kEchoParamIndices, 2);
  add(kResponsePanel, kResponseParamIndices, 0);
#else
  add(kDrivePanel, kDriveParamIndices, 1);
  add(kColorPanel, kColorParamIndices, 0);
#endif
#endif
  return result;
}
void menus(Editor &v, Plugin &p, Events &events) {
#if !defined(S3G_TEST_DRUM_MIXER)
#if defined(S3G_TEST_DRUM_ECHO)
  const auto panel = kEchoPanel;
  const clap_id ids[]{kHeadModeParamId, kClockParamId};
#else
  const auto panel = kDrivePanel;
  const clap_id ids[]{kCircuitParamId};
#endif
  for (uint32_t row = 0; row < std::size(ids); ++row) {
    const auto id = ids[row];
    const int count = static_cast<int>(v.info(id).max_value) + 1;
    const auto b = foundation::rect(
        s3g::gui_layout::processorControlX(panel.frame.x),
        s3g::gui_layout::rowY(panel, row) - 1.,
        s3g::gui_layout::processorMenuWidth(panel.frame.width), 15.);
    const double top = b.bottom + 3. + count * 18. > kGuiHeight - 8.
                           ? b.top - count * 18. - 3.
                           : b.bottom + 3.;
    for (int i = 0; i < count; ++i) {
      render(v);
      click(v, b.getCenter());
      const auto before = render(v, "menu-" + std::to_string(id));
      move(v, {b.left + 20., top + i * 18. + 9.});
      expect(render(v, "hover-" + std::to_string(id)) != before,
             "custom menu hover feedback");
      click(v, {b.left + 20., top + i * 18. + 9.});
      flush(p, events);
      expect(v.value(id) == i,
             "menu option " + std::to_string(id) + ":" + std::to_string(i));
    }
    render(v);
    click(v, b.getCenter());
    render(v);
    click(v, {kGuiWidth - 2., kGuiHeight - 2.});
    expect(v.value(id) == count - 1, "outside click dismisses menu");
  }
#endif
}
void audio(Plugin &p, Events &events) {
  constexpr uint32_t channels =
#if defined(S3G_TEST_DRUM_MIXER)
      16;
#else
      2;
#endif
  std::array<std::array<float, 256>, channels> input{}, output{};
  std::array<float *, channels> inPtrs{}, outPtrs{};
  for (uint32_t ch = 0; ch < channels; ++ch) {
    inPtrs[ch] = input[ch].data();
    outPtrs[ch] = output[ch].data();
    for (uint32_t frame = 0; frame < 256; ++frame)
      input[ch][frame] =
          .7f * std::sin(frame * .1f + ch * .3f) * std::exp(-frame / 200.f);
  }
  clap_audio_buffer_t in{}, out{};
  in.channel_count = out.channel_count = channels;
  in.data32 = inPtrs.data();
  out.data32 = outPtrs.data();
  clap_process_t block{};
  block.frames_count = 256;
  block.audio_inputs = &in;
  block.audio_inputs_count = 1;
  block.audio_outputs = &out;
  block.audio_outputs_count = 1;
  block.out_events = &events.out;
  expect(process(&p.plugin, &block) != CLAP_PROCESS_ERROR, "audio process");
  double energy = 0.;
  for (auto &ch : output)
    for (float s : ch) {
      expect(std::isfinite(s), "finite audio");
      energy += s * s;
    }
  expect(energy > 0., "nonzero audio");
}
} // namespace test

int main() {
  using namespace test;
  if (!foundation::acquireRuntime())
    return 2;
  expect(foundation::usingBundledFont() !=
             (std::getenv("S3G_DRUM_EXPECT_FONT_FALLBACK") != nullptr),
         "bundled/fallback font source");
  expect(!foundation::selectedFontFamily().empty(), "usable font family");
  clap_host_t host{};
  host.clap_version = CLAP_VERSION_INIT;
  host.name = "Drum effects parity";
  host.vendor = "s3g";
  host.url = "";
  host.version = "1";
  host.get_extension = [](const clap_host_t *, const char *) -> const void * {
    return nullptr;
  };
  host.request_process = [](const clap_host_t *) {};
  const auto *plugin = createPlugin(nullptr, &host, descriptor.id);
  expect(plugin && init(plugin), "plugin initialization");
  if (!plugin)
    return 2;
  auto &p = *self(plugin);
  expect(activate(plugin, 48000., 16, 256), "activation");
  {
    Editor v(p);
    Events events;
    render(v, "idle");
    cocoa(p, "idle");
    v.font = foundation::makeUiFont(11.);
    v.titleFont = foundation::makeUiFont(11.5);
    render(v, "windows-font-size");
    v.font = foundation::makeUiFont(10.);
    v.titleFont = foundation::makeUiFont(10.5);
    for (const auto &control : controls()) {
      const auto info = v.info(control.id);
      for (double norm : {0., .37, 1.}) {
        render(v);
        const auto start = control.bounds.getCenter();
        if (control.dial) {
#if defined(S3G_TEST_DRUM_MIXER)
          const double current =
              normalizedParamValue(control.id, v.value(control.id));
          down(v, start);
          move(v, {start.x + (norm - current) * 160., start.y});
          up(v);
#endif
        } else {
          const CPoint point =
              control.vertical
                  ? CPoint{control.track.getCenter().x,
                           control.track.bottom -
                               norm * control.track.getHeight()}
                  : CPoint{control.track.left + norm * control.track.getWidth(),
                           control.track.getCenter().y};
          down(v, start);
          move(v, point);
          up(v);
        }
        flush(p, events);
        expect(near(v.value(control.id), v.fromNormalized(control.id, norm)),
               "slider/dial range " + std::to_string(control.id));
      }
      render(v);
      click(v, control.bounds.getCenter(), 2);
      flush(p, events);
      expect(near(v.value(control.id), info.default_value),
             "double-click default " + std::to_string(control.id));
    }
    menus(v, p, events);
#if defined(S3G_TEST_DRUM_MIXER)
    for (uint32_t lane = 0; lane < 8; ++lane)
      for (bool solo : {false, true}) {
        const auto id =
            laneParamId(lane, solo ? kLaneSoloOffset : kLaneMuteOffset);
        for (double expected : {1., 0.}) {
          render(v);
          click(v,
                {fx_canvas::lanePanel(lane).left + (solo ? 90. : 38.), 306.});
          flush(p, events);
          expect(v.value(id) == expected, "mute/solo toggle");
        }
      }
    render(v);
    click(v, {1255., 116.});
    flush(p, events);
    expect(v.value(kOutputModeParamId) == 1., "DIRECT route");
    render(v, "direct");
    click(v, {1190., 116.});
    flush(p, events);
    expect(v.value(kOutputModeParamId) == 0., "SUM route");
    for (double expected : {1., 0.}) {
      render(v);
      click(v, {1200., 227.});
      flush(p, events);
      expect(v.value(kBusEnabledParamId) == expected, "AUX BUS toggle");
    }
#endif
    auto control = controls().front();
    for (auto candidate : controls())
      if (candidate.id != fx_canvas::preservedOutput) {
        control = candidate;
        break;
      }
    // UTF-8 presets round-trip through the original codec. No live audio
    // state mutation occurs until process/flush consumes the batch.
    const auto temp =
        std::filesystem::temp_directory_path() /
        ("s3g-drum-effects-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(temp);
    const auto path = foundation::pathToUtf8(temp / u8"Café 空間.s3gpreset");
    v.set(control.id, v.fromNormalized(control.id, .71));
    flush(p, events);
    const double saved = v.value(control.id);
    expect(v.presetFile(path, true), "save Unicode preset");
    v.set(control.id, v.fromNormalized(control.id, .21));
    flush(p, events);
#if !defined(S3G_TEST_DRUM_MIXER)
    v.set(kOutputParamId, -17.);
    flush(p, events);
#endif
    expect(v.presetFile(path, false), "load Unicode preset");
    flush(p, events);
    if (control.id != fx_canvas::preservedOutput)
      expect(near(v.value(control.id), saved), "preset restores parameter");
#if !defined(S3G_TEST_DRUM_MIXER)
    expect(near(v.value(kOutputParamId), -17.), "LOAD preserves output trim");
#endif
    const auto bad = foundation::pathToUtf8(temp / "invalid.s3gpreset");
    {
      std::ofstream f(bad, std::ios::binary);
      f << "invalid";
    }
    const auto before = v.value(control.id);
    expect(!v.presetFile(bad, false), "invalid preset rejected");
    flush(p, events);
    expect(near(v.value(control.id), before),
           "invalid preset leaves state intact");
    expect(s3g::clap_gui::portable::loadStateFile(plugin, stateExtension,
                                                  path.c_str()),
           "project state codec");
    expect(near(v.value(control.id), saved),
           "project restores saved trim/parameter");
    render(v);
    const auto title =
        s3g::gui_layout::encoderTitleBand({kGuiWidth, kGuiHeight});
    click(v, Editor::box(title.presetMenu).getCenter());
    flush(p, events);
    expect(v.presetName == "INIT", "INIT title action");
    for (uint32_t i = 0; i < kParamCount; ++i) {
      clap_param_info_t info{};
      paramsGetInfo(plugin, i, &info);
      if (info.id != fx_canvas::preservedOutput)
        expect(near(v.value(info.id), info.default_value),
               "INIT default " + std::to_string(info.id));
    }
    std::filesystem::remove_all(
        temp); // only this test's uniquely created directory
    const auto idle = render(v);
    audio(p, events);
    expect(render(v, "active") != idle, "DSP-driven readout/meters");
    cocoa(p, "active");
    // Host backpressure retains events; hiding mid-drag always sends END.
    render(v);
    down(v, control.bounds.getCenter());
    move(v, {control.bounds.getCenter().x + 8., control.bounds.getCenter().y});
    events.accept = false;
    const auto pending = p.guiParamEvents.available();
    flush(p, events);
    expect(p.guiParamEvents.available() == pending,
           "host rejection retains queue");
    events.accept = true;
    v.stopRefresh();
    flush(p, events);
    events.balanced();
    events.values.clear();
    v.begin(control.id);
    for (int i = 0; i < 1100; ++i)
      v.setValue(control.id, v.fromNormalized(control.id, .4));
    expect(p.guiParamEvents.available() >= 1, "reserved END slot");
    v.end();
    flush(p, events);
    events.balanced();
    events.values.clear();
    while (v.set(control.id, v.fromNormalized(control.id, .2))) {
    }
    const double unchanged = v.value(control.id);
    expect(!v.applySnapshot(nullptr, true),
           "full preset batch rejected atomically");
    expect(v.value(control.id) == unchanged, "rejected batch does not publish");
    flush(p, events);
    events.balanced();
  }
  deactivate(plugin);
  destroy(plugin);
  foundation::releaseRuntime();
  if (ok)
    std::cout << fx_canvas::pluginName << " canvas parity passed\n";
  return ok ? 0 : 1;
}
