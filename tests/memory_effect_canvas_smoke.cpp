// Exercise the real editors and original codecs/DSP, with Cocoa captures.
#include S3G_TEST_MEMORY_SOURCE
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/platform/platformfactory.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>

namespace test {
using namespace VSTGUI;
using memory_canvas::Editor;
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
  const char *root = std::getenv("S3G_MEMORY_CAPTURE_DIR");
  if (root && !name.empty()) {
    auto directory = foundation::pathFromUtf8(root);
    std::filesystem::create_directories(directory);
    std::ofstream file(directory /
                           (std::string(descriptor.id) + "-" + name + ".png"),
                       std::ios::binary);
    file.write(reinterpret_cast<const char *>(png.data()), png.size());
    expect(bool(file), "write portable capture");
#if defined(__APPLE__)
    auto *view = [[S3G_TEST_MEMORY_COCOA_VIEW alloc] initWithPlugin:&v.p];
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
struct Control {
  clap_id id;
  double panelX, width, y;
  int menuCount = 0;
  bool toggle = false;
};
std::vector<Control> controls(unsigned model = 0) {
  std::vector<Control> result;
#if defined(S3G_MEMORY_SPRAY_PORT)
  constexpr auto output = layout::compactEffectOutputPanel(3u);
  constexpr auto range =
      layout::compactEffectLeftPanel(output, layout::PanelRole::ToneShape, 4u);
  constexpr auto motion =
      layout::compactEffectRightPanel(layout::PanelRole::Motion, 7u);
  const clap_id outputIds[] = {13, 12, 14}, rangeIds[] = {9, 10, 11, 8},
                motionIds[] = {1, 2, 3, 4, 5, 6, 7};
  auto add = [&](const layout::Panel &panel, auto &ids) {
    for (unsigned i = 0; i < std::size(ids); ++i)
      result.push_back(
          {ids[i], panel.frame.x, panel.frame.width, layout::rowY(panel, i)});
  };
  add(output, outputIds);
  add(range, rangeIds);
  add(motion, motionIds);
#elif defined(S3G_MEMORY_BUFFER_PORT)
  const clap_id engine[] = {1, 2, 3, 4, 14, 5, 6, 15},
                relationships[] = {7, 8, 9, 10, 11};
  for (unsigned i = 0; i < 8; ++i)
    result.push_back({engine[i], 18., 352., 78. + 26. * i});
  for (unsigned i = 0; i < 5; ++i)
    result.push_back({relationships[i], 388., 354., 170. + 26. * i});
  for (auto item : {Control{13, 388., 354., 78.},
                    {12, 388., 354., 104.},
                    {16, 388., 354., 348., 5},
                    {19, 388., 354., 374., 2},
                    {17, 388., 354., 400.},
                    {18, 388., 354., 426.},
                    {20, 388., 354., 452., 5},
                    {21, 388., 354., 478.}})
    result.push_back(item);
#else
  auto add = [&](const ParamDef &def, const layout::Panel &panel,
                 unsigned row) {
    result.push_back({def.id, panel.frame.x, panel.frame.width,
                      layout::rowY(panel, row),
                      def.id <= 2 ? 4 : def.id == 410 ? 5 : 0,
                      def.id >= 411 && def.id <= 413});
  };
  for (unsigned i = 0; i < 3; ++i)
    add(kParamDefs[i], Editor::fieldPanel, i);
  for (unsigned i = 0; i < kModelCounts[model]; ++i)
    add(kParamDefs[kModelStarts[model] + i],
        i >= 7 ? Editor::modelRight : Editor::modelLeft, i >= 7 ? i - 7 : i);
#endif
  return result;
}
void allControls(Editor &v, Plugin &p, Events &events) {
#if defined(S3G_MEMORY_FIELD_PORT)
  constexpr unsigned models = 4;
#else
  constexpr unsigned models = 1;
#endif
  unsigned visited = 0;
  for (unsigned model = 0; model < models; ++model) {
    for (const auto &c : controls(model)) {
#if defined(S3G_MEMORY_FIELD_PORT)
      v.set(1u, model);
      flush(p, events);
#endif
#if defined(S3G_MEMORY_SPRAY_PORT)
      v.set(kLoFreqParamId, 0.);
      v.set(kHiFreqParamId, 24000.);
      flush(p, events);
#endif
      render(v);
      const double x = layout::processorControlX(c.panelX);
      if (c.menuCount) {
        for (int choice = 0; choice < c.menuCount; ++choice) {
          render(v);
          click(v, {x + 15., c.y + 4.});
#if defined(S3G_MEMORY_BUFFER_PORT)
          const double top = c.y + 16.;
#else
          const double top = c.y + 17.;
#endif
          move(v, {4., kGuiHeight - 4.});
          const auto unhovered = render(v);
          move(v, {x + 12., top + 18. * choice + 9.});
          expect(render(v) != unhovered, "custom menu hover is visible");
          click(v, {x + 12., top + 18. * choice + 9.});
          flush(p, events);
          expect(near(v.value(c.id), choice) &&
                     near(memoryRawValue(p, c.id), choice),
                 "menu -> DSP " + std::to_string(c.id));
        }
        const auto closed = render(v);
        click(v, {x + 15., c.y + 4.});
        click(v, {4., kGuiHeight - 4.});
        expect(render(v) == closed, "outside menu dismissal");
      } else if (c.toggle) {
        for (int i = 0; i < 2; ++i) {
          render(v);
          const double before = v.value(c.id);
          click(v, {x + 20., c.y});
          flush(p, events);
          expect(v.value(c.id) == (before >= .5 ? 0. : 1.),
                 "toggle " + std::to_string(c.id));
        }
      } else {
        const auto info = v.info(c.id);
        for (double norm : {0., .37, 1.}) {
          render(v);
          down(v, {x + layout::processorTrackWidth(c.width) * norm, c.y + 4.});
          up(v);
          flush(p, events);
          const double expected = memory_canvas::clampParamValue(
              c.id, info.min_value + norm * (info.max_value - info.min_value));
          expect(near(v.value(c.id), expected),
                 "slider endpoint/mapping " + std::to_string(c.id));
          expect(near(memoryRawValue(p, c.id), v.value(c.id)),
                 "slider -> audio-owned DSP");
        }
        render(v);
        down(v, {x + 1., c.y});
        move(v, {x + layout::processorTrackWidth(c.width) * .62, c.y});
        up(v);
        flush(p, events);
        render(v);
        click(v, {x + 30., c.y}, 2);
        flush(p, events);
        expect(near(v.value(c.id), info.default_value),
               "double-click default " + std::to_string(c.id));
      }
      events.balanced();
      ++visited;
    }
    render(v, "model-" + std::to_string(model));
  }
#if defined(S3G_MEMORY_FIELD_PORT)
  expect(visited == kParamCount + 9, "all four model control banks");
#else
  expect(visited == kParamCount, "every exposed parameter has a GUI control");
#endif
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
void presets(Editor &v, Plugin &p, Events &events) {
  const auto root =
      std::filesystem::temp_directory_path() /
      ("s3g-memory-parity-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto file = foundation::pathToUtf8(root / "écho_波_Δ.s3gpreset");
  v.applySnapshot(nullptr);
  flush(p, events);
  for (uint32_t i = 0; i < kParamCount; ++i) {
    const auto def = v.info(kParamDefs[i].id);
    v.set(def.id, def.min_value + .31 * (def.max_value - def.min_value));
  }
#if defined(S3G_MEMORY_SPRAY_PORT)
  p.memoryDamage.store(.38f);
  p.memoryRepeat.store(.27f);
#endif
  flush(p, events);
  std::array<double, kParamCount> expected{};
  for (unsigned i = 0; i < kParamCount; ++i)
    expected[i] = v.value(kParamDefs[i].id);
  expect(v.presetFile(file, true), "Unicode preset save");
  Bytes original, portable;
  memoryCopyToRaw(p, p);
  expect(encodeMemoryState(&p.plugin, &original.out), "original codec save");
  expect(stateSave(&p.plugin, &portable.out), "portable state save");
  expect(original.data == portable.data, "unchanged saved-state bytes");
  v.applySnapshot(nullptr);
  flush(p, events);
  if (memory_canvas::preservedOutput != CLAP_INVALID_ID)
    v.set(memory_canvas::preservedOutput, -17.);
  flush(p, events);
  expect(v.presetFile(file, false), "Unicode preset load");
  flush(p, events);
  for (unsigned i = 0; i < kParamCount; ++i) {
    const auto id = kParamDefs[i].id;
    expect(near(v.value(id),
                id == memory_canvas::preservedOutput ? -17. : expected[i]),
           "preset restoration " + std::to_string(id));
  }
#if defined(S3G_MEMORY_SPRAY_PORT)
  expect(near(p.params.damage, .38) && near(p.params.repeat, .27),
         "legacy hidden spray fields survive preset load");
#endif
  Bytes project;
  project.data = original.data;
  expect(stateLoad(&p.plugin, &project.in), "legacy codec project load");
  flush(p, events);
  for (unsigned i = 0; i < kParamCount; ++i)
    expect(near(v.value(kParamDefs[i].id), expected[i]),
           "project restores OUT and all params");
  const auto before = v.value(kParamDefs[0].id);
  const auto corrupt = foundation::pathToUtf8(root / "broken.s3gpreset");
  {
    std::ofstream out(foundation::pathFromUtf8(corrupt.c_str()),
                      std::ios::binary);
    out << "bad";
  }
  expect(!v.presetFile(corrupt, false) && v.value(kParamDefs[0].id) == before,
         "truncated preset leaves live state untouched");
  Bytes invalid;
  invalid.data = original.data;
  uint32_t wrong = 999;
  std::memcpy(invalid.data.data(), &wrong, sizeof(wrong));
  expect(!stateLoad(&p.plugin, &invalid.in) &&
             v.value(kParamDefs[0].id) == before,
         "bad version is rejected atomically");
  invalid.data = original.data;
  invalid.offset = 0;
#if defined(S3G_MEMORY_FIELD_PORT)
  const double nonFinite = std::numeric_limits<double>::quiet_NaN();
  std::memcpy(invalid.data.data() + offsetof(SavedState, values) +
                  5 * sizeof(double),
              &nonFinite, sizeof(nonFinite));
#else
  const float nonFinite = std::numeric_limits<float>::quiet_NaN();
  std::memcpy(invalid.data.data() + offsetof(SavedState, params) +
                  5 * sizeof(float),
              &nonFinite, sizeof(nonFinite));
#endif
  expect(!stateLoad(&p.plugin, &invalid.in) &&
             v.value(kParamDefs[0].id) == before,
         "non-finite state is rejected before changing live controls");
  v.applySnapshot(nullptr);
  flush(p, events);
  for (unsigned i = 0; i < kParamCount; ++i) {
    auto info = v.info(kParamDefs[i].id);
    if (info.id != memory_canvas::preservedOutput)
      expect(near(v.value(info.id), info.default_value), "INIT defaults");
  }
  events.balanced();
  std::filesystem::remove_all(
      root); // This test's uniquely created fixtures only.
}
void audio(Editor &v, Plugin &p, Events &events) {
  const auto *reference = createPlugin(nullptr, p.host, descriptor.id);
  expect(reference && init(reference), "reference instance");
  auto &r = *self(reference);
#if defined(S3G_MEMORY_FIELD_PORT)
  constexpr unsigned inputs = 2, outputs = 16, models = 4, formats = 4;
#else
  constexpr unsigned inputs = kChannelCount, outputs = kChannelCount,
                     models = 1, formats = 1;
#endif
  for (unsigned model = 0; model < models; ++model)
    for (unsigned fmt = 0; fmt < formats; ++fmt) {
      v.applySnapshot(nullptr);
      flush(p, events);
#if defined(S3G_MEMORY_FIELD_PORT)
      v.set(1u, model);
      v.set(2u, fmt);
      flush(p, events);
#elif defined(S3G_MEMORY_BUFFER_PORT)
      v.set(kMixParamId, 1.);
      flush(p, events);
#else
      p.memoryDamage.store(0.f);
      p.memoryRepeat.store(0.f);
#endif
      Bytes state;
      expect(stateSave(&p.plugin, &state.out) &&
                 stateLoad(reference, &state.in),
             "reference state restore");
      expect(activate(&p.plugin, 48000., 16, 256) &&
                 activate(reference, 48000., 16, 256),
             "audio prepare");
      std::array<std::array<float, 256>, inputs> in{};
      std::array<std::array<float, 256>, outputs> out{}, ref{};
      std::array<float *, inputs> inPtrs{};
      std::array<float *, outputs> outPtrs{}, refPtrs{};
      for (unsigned ch = 0; ch < inputs; ++ch)
        inPtrs[ch] = in[ch].data();
      for (unsigned ch = 0; ch < outputs; ++ch) {
        outPtrs[ch] = out[ch].data();
        refPtrs[ch] = ref[ch].data();
      }
      clap_audio_buffer_t input{}, output{}, referenceOutput{};
      input.channel_count = inputs;
      input.data32 = inPtrs.data();
      output.channel_count = referenceOutput.channel_count = outputs;
      output.data32 = outPtrs.data();
      referenceOutput.data32 = refPtrs.data();
      clap_process_t proc{};
      proc.frames_count = 256;
      proc.audio_inputs_count = proc.audio_outputs_count = 1;
      proc.audio_inputs = &input;
      proc.audio_outputs = &output;
      proc.out_events = &events.out;
      auto refProc = proc;
      refProc.audio_outputs = &referenceOutput;
      refProc.out_events = nullptr;
      double difference = 0., energy = 0.;
      for (unsigned block = 0; block < 240; ++block) {
        for (unsigned ch = 0; ch < inputs; ++ch)
          for (unsigned i = 0; i < 256; ++i)
            in[ch][i] = .12f * std::sin((block * 256 + i) * (.018 + ch * .007));
#if defined(S3G_MEMORY_BUFFER_PORT)
        if (block == 40) {
          render(v);
          click(v, {523., 506.});
          r.processor.captureMemory();
        }
        if (block == 41) {
          v.set(kMemoryParamId, 1.);
          applyParam(r, kMemoryParamId, 1.);
        }
        if (block == 140) {
          render(v);
          click(v, {585., 506.});
          r.processor.clearMemory();
        }
#endif
        expect(process(&p.plugin, &proc) == CLAP_PROCESS_CONTINUE,
               "audio process");
        expect(process(reference, &refProc) == CLAP_PROCESS_CONTINUE,
               "reference process");
        for (unsigned ch = 0; ch < outputs; ++ch)
          for (unsigned i = 0; i < 256; ++i) {
            expect(std::isfinite(out[ch][i]), "finite output");
            difference =
                std::max(difference, std::abs(double(out[ch][i] - ref[ch][i])));
            energy += std::abs(out[ch][i]);
#if defined(S3G_MEMORY_FIELD_PORT)
            if (block > 32 &&
                ch >= s3g::fixedBusRingActiveChannels(
                          static_cast<s3g::FixedBusRingFormat>(fmt)))
              expect(out[ch][i] == 0.f, "inactive output lanes remain silent");
#endif
          }
      }
      expect(difference < 1e-6,
             "GUI/audio-command path matches original DSP: " +
                 std::to_string(difference));
      expect(energy > .01 && p.outputPeak.load() > .00001,
             "audible output / live peak");
#if defined(S3G_MEMORY_FIELD_PORT)
      float activity = 0.;
      for (const auto &peak : p.sourcePeaks)
        activity += peak.load();
      expect(activity > .00001 && p.inputPeak.load() > .00001,
             "real pre-fold delayed-lane activity");
#endif
      render(v, "audio-" + std::to_string(model) + "-" + std::to_string(fmt));
    }
  deactivate(reference);
  destroy(reference);
  events.balanced();
}
void pressure(Editor &v, Plugin &p, Events &events) {
  const auto c = controls()[0];
#if defined(S3G_MEMORY_FIELD_PORT)
  const clap_id id = 3u;
#else
  const clap_id id = c.id;
#endif
  const auto info = v.info(id);
  const double middle = info.min_value + .4 * (info.max_value - info.min_value);
  render(v);
  v.begin(id);
  for (unsigned i = 0; i < 2000; ++i)
    v.setValue(id, middle);
  expect(p.memoryGuiEvents.available() >= 1u, "END capacity reserved");
  events.accept = false;
  flush(p, events);
  v.end();
  expect(!v.applySnapshot(nullptr), "saturated queue rejects entire preset");
  const double newer = memory_canvas::clampParamValue(
      id, info.min_value + .9 * (info.max_value - info.min_value));
  applyParam(p, id, newer); // A newer host event while output is blocked.
  events.accept = true;
  flush(p, events);
  expect(near(memoryRawValue(p, id), newer) && near(v.value(id), newer),
         "older GUI notifications never roll back host automation");
  events.balanced();
  render(v);
  auto actual = controls()[0];
#if defined(S3G_MEMORY_FIELD_PORT)
  actual = controls()[2];
#endif
  down(v, {layout::processorControlX(actual.panelX) + 50., actual.y});
  v.stopRefresh();
  flush(p, events);
  events.balanced();
}
} // namespace test

int main() {
  using namespace test;
  if (!foundation::acquireRuntime())
    return 2;
  expect(foundation::usingBundledFont() !=
             (std::getenv("S3G_MEMORY_EXPECT_FONT_FALLBACK") != nullptr),
         "bundled/fallback font");
  clap_host_t host{};
  host.clap_version = CLAP_VERSION_INIT;
  host.name = "Memory effects parity";
  host.vendor = "s3g";
  host.url = "";
  host.version = "1";
  host.get_extension = [](const clap_host_t *, const char *) -> const void * {
    return nullptr;
  };
  host.request_process = [](const clap_host_t *) {};
  const auto *plugin = createPlugin(nullptr, &host, descriptor.id);
  expect(plugin && init(plugin), "plugin init");
  if (!plugin)
    return 2;
  expect(activate(plugin, 48000., 16, 256), "activation");
  auto &p = *self(plugin);
  {
    Editor v(p);
    Events events;
    render(v, "initial");
    allControls(v, p, events);
    presets(v, p, events);
    audio(v, p, events);
    pressure(v, p, events);
#if defined(S3G_MEMORY_BUFFER_PORT)
    for (auto id :
         {kSkipParamId, kSkipChaseParamId, kErrorParamId, kMemoryParamId,
          kReverseParamId, kSpreadParamId, kDeviationParamId})
      v.set(id, .75);
    flush(p, events);
    render(v, "window-details");
    events.balanced();
#endif
    Editor windows(p, {11., 11.5, 9., 8.});
    render(windows, "windows-font-metrics");
  }
  deactivate(plugin);
  destroy(plugin);
  foundation::releaseRuntime();
  if (ok)
    std::cout << descriptor.id << " memory effect canvas parity passed\n";
  return ok ? 0 : 1;
}
